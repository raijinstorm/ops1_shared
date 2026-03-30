#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_KNIGHT_NAME_LENGHT 20

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

typedef struct knight
{
    char name[MAX_KNIGHT_NAME_LENGHT + 1];
    int hp;
    int attack;
} knight;

void msleep(int millisec)
{
    struct timespec tt;
    tt.tv_sec = millisec / 1000;
    tt.tv_nsec = (millisec % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

int count_descriptors()
{
    int count = 0;
    DIR* dir;
    struct dirent* entry;
    struct stat stats;
    if ((dir = opendir("/proc/self/fd")) == NULL)
        ERR("opendir");
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    chdir("/proc/self/fd");
    do
    {
        errno = 0;
        if ((entry = readdir(dir)) != NULL)
        {
            if (lstat(entry->d_name, &stats))
                ERR("lstat");
            if (!S_ISDIR(stats.st_mode))
                count++;
        }
    } while (entry != NULL);
    if (chdir(path))
        ERR("chdir");
    if (closedir(dir))
        ERR("closedir");
    return count - 1;  // one descriptor for open directory
}

void child_work(int is_frank, int id, knight* this_knight, int num_franci, int (*pipes_franci)[2],
    int num_saraceni, int (*pipes_saraceni)[2], knight *franks, knight* saracens) {

    if (is_frank) {
        for (int i=0;i<num_franci;i++) {
            if (i!=id) {
                close(pipes_franci[i][0]);
            }
            close(pipes_franci[i][1]);
        }

        for (int i=0;i<num_saraceni;i++) {
            close(pipes_saraceni[i][0]);
        }

        printf("I am Frankish knight %s. I will serve my king with my %d HP and %d attack.\n", this_knight->name,
               this_knight->hp, this_knight->attack);
    }

    else {
        for (int i=0;i<num_saraceni;i++) {
            if (i!=id) {
                close(pipes_saraceni[i][0]);
            }
            close(pipes_saraceni[i][1]);
        }

        for (int i=0;i<num_franci;i++) {
            close(pipes_franci[i][0]);
        }

        printf("I am Spanish knight %s. I will serve my king with my %d HP and %d attack.\n", this_knight->name,
               this_knight->hp, this_knight->attack);
    }

    printf("Opened descriptors: %d\n", count_descriptors());

    srand(getpid());

    int read_fd;
    int (*enemy_pipes)[2];
    int num_enemies;

    if (is_frank) {
        read_fd = pipes_franci[id][0];
        enemy_pipes = pipes_saraceni;
        num_enemies = num_saraceni;
    }
    else {
        read_fd = pipes_saraceni[id][0];
        enemy_pipes = pipes_franci;
        num_enemies = num_franci;
    }

    int flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    int* living_enemies = malloc(num_enemies*sizeof(int));
    if (!living_enemies) {
        ERR("malloc liv. enemies");
    }
    for (int i=0;i<num_enemies;i++) {
        living_enemies[i] = i;
    }
    int p = num_enemies - 1;

    while (this_knight->hp > 0 && p>=0) {
        uint8_t blow;
        ssize_t bytes_read;

        while ((bytes_read = read(read_fd, &blow, sizeof(blow)))>0) {
            this_knight->hp -= blow;
        }
        if (bytes_read < 0 && errno != EAGAIN)
            ERR("read");

        if (this_knight->hp <=0) {
            printf("%s dies glorious death\n", this_knight->name);
            break;
        }
        if (bytes_read == 0) {
            break;
        }

        int attack_successful = 0;
        while (!attack_successful && p>=0) {
            int target_id = rand()%(p+1);
            int target = living_enemies[target_id];

            uint8_t S = rand()%(this_knight->attack+1);

            if (write(enemy_pipes[target][1], &S, sizeof(S)) == -1) {
                if (errno == EPIPE) {
                    living_enemies[target_id] = living_enemies[p];
                    p--;

                    if (close(enemy_pipes[target][1])) ERR("close");
                }
                else {
                    ERR("write to enemy pipe");
                }
            }
            else {
                attack_successful = 1;
                if (S==0) {
                    printf("%s attacks his enemy, however he deflected\n", this_knight->name);
                }
                else if (S>=1 && S<=5) {
                    printf("%s goes to strike, he hit right and well\n", this_knight->name);
                }
                else {
                    printf("%s strikes powerful blow, the shield he breaks and inflicts a big wound\n", this_knight->name);
                }
            }
        }

        int t=(rand()%10)+1;
        msleep(t);
    }

    close(read_fd);
    for (int i=0;i<=p;i++) {
        close(enemy_pipes[living_enemies[i]][1]);
    }

    free(living_enemies);
    free(franks);
    free(saracens);
    free(pipes_franci);
    free(pipes_saraceni);

    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[])
{
    if (set_handler(SIG_IGN, SIGPIPE) == -1) {
        ERR("set_handler");
    }
    srand(time(NULL));

    FILE* franci = fopen("franci.txt", "r");
    if (!franci)
    {
        printf("Franks have not arrived on the battlefield\n");
        return EXIT_FAILURE;
    }

    FILE* saraceni = fopen("saraceni.txt", "r");
    if (!saraceni)
    {
        printf("Saraceni have not arrived on the battlefield\n");
        return EXIT_FAILURE;
    }

    int franci_n;
    if (fscanf(franci, "%d", &franci_n) != 1)
        ERR("fscanf");

    knight* franks = malloc(franci_n * sizeof(knight));
    if (!franks)
    {
        ERR("malloc");
    }
    for (int i = 0; i < franci_n; i++)
    {
        if (fscanf(franci, "%20s %d %d", franks[i].name, &franks[i].hp, &franks[i].attack) != 3)
        {
            ERR("parsing franks");
        }
    }

    int saraceni_n;
    if (fscanf(saraceni, "%d", &saraceni_n) != 1)
        ERR("fscanf");

    knight* saracens = malloc(saraceni_n * sizeof(knight));
    if (!saracens)
    {
        ERR("malloc");
    }
    for (int i = 0; i < saraceni_n; i++)
    {
        if (fscanf(saraceni, "%20s %d %d", saracens[i].name, &saracens[i].hp, &saracens[i].attack) != 3)
        {
            ERR("parsing franks");
        }
    }

    fclose(franci);
    fclose(saraceni);

    int (*pipes_franci)[2] = malloc(franci_n* sizeof(int[2]));
    int (*pipes_saraceni)[2] = malloc(saraceni_n * sizeof(int[2]));
    if (!pipes_franci || !pipes_saraceni) {
        ERR("malloc");
    }

    for (int i = 0; i < franci_n; i++) {
        if (pipe(pipes_franci[i]) == -1) {
            ERR("pipe");
        }
    }
    for (int i=0;i<saraceni_n;i++) {
        if (pipe(pipes_saraceni[i]) == -1) {
            ERR("pipe");
        }
    }

    for (int i=0;i<franci_n;i++) {
        pid_t pid = fork();
        if (pid == 0) {
            child_work(1, i, &franks[i], franci_n, pipes_franci, saraceni_n, pipes_saraceni, franks, saracens);
        }
        if (pid == -1) {
            ERR("fork");
        }
    }

    for (int i=0;i<saraceni_n;i++) {
        pid_t pid = fork();
        if (pid == 0) {
            child_work(0, i, &saracens[i], franci_n, pipes_franci, saraceni_n, pipes_saraceni, franks, saracens);
        }
        if (pid == -1) {
            ERR("fork");
        }
    }


    for (int i=0;i<franci_n;i++) {
        if (close(pipes_franci[i][0])) {
            ERR("close");
        }
        if (close(pipes_franci[i][1])) {
            ERR("close");
        }
    }

    for (int i=0;i<saraceni_n;i++) {
        if (close(pipes_saraceni[i][0])) {
            ERR("close");
        }
        if (close(pipes_saraceni[i][1])) {
            ERR("close");
        }
    }

    for (int i=0;i<franci_n+saraceni_n;i++) {
        if (wait(NULL) == -1 && errno!=ECHILD) {
            ERR("wait");
        }
    }

    free(franks);
    free(saracens);
    free(pipes_franci);
    free(pipes_saraceni);

    printf("Opened descriptors: %d\n", count_descriptors());

    srand(time(NULL));
}
