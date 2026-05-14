#define _POSIX_C_SOURCE 200809L


#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

#define MOTHERSHIP_FIFO "/tmp/mothership"

#define SUPPLIES_CAPACITY 4
#define MAX_NODE_NUM 36

typedef struct
{
    int id;        // 100, 99, ..., 1, 0
    int supplies;  // Current supplies
} expedition_t;



void usage(int argc, char* argv[])
{
    fprintf(stderr, "Usage: %s M\n", argv[0]);
    fprintf(stderr, "  M - size of the board (3 <= M <= 6)\n");
    exit(EXIT_FAILURE);
}



int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
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


void msleep(const int ms)
{
    struct timespec tt;
    tt.tv_sec = ms / 1000;
    tt.tv_nsec = (ms % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

void child_work(int k, int x, int y, int M, int pipes[][2], int n_id, int w_id, int s_id, int e_id)
{
    srand(getpid());
    int descriptors_cnt = count_descriptors();
    printf("Node [%d] (%d, %d): Opened %d descriptors\n", k, x,y,descriptors_cnt);

    int local_supplies = rand()%3;

    while (1)
    {
        expedition_t expedition;
        int read_bytes = read(pipes[k][0], &expedition, sizeof(expedition));
        if (read_bytes<=0)
        {
            break;
        }
        if (expedition.id == 0)
        {
            break;
        }
        printf("Expedition %d: arrived at node [%d] (%d, %d)\n", expedition.id, k, x, y);

        if (local_supplies>0 && expedition.supplies<SUPPLIES_CAPACITY)
        {
            int left = SUPPLIES_CAPACITY - expedition.supplies;
            int take = (local_supplies < left)?local_supplies:left;
            expedition.supplies += take;
            local_supplies -= take;

            printf("Expedition %d: Good stuff\n", expedition.id);
        }

        if (expedition.supplies>0)
        {
            printf("Expedition %d: Tomorrow comes.\n", expedition.id);
            expedition.supplies--;

            msleep(200);
            int neighbours[4] = {w_id, n_id, s_id, e_id};
            int valid_heighbours[4] = {1,1,1,1};
            int flag = 0;
            for (int i=0;i<4;i++)
            {
                int cnt = 0;
                int ids_n[4];
                for (int j=0;j<4;j++)
                {
                    if (valid_heighbours[j])
                    {
                        ids_n[cnt++]=j;
                    }
                }
                if (cnt == 0)
                {
                    break;
                }
                int rnd_id = ids_n[rand()%cnt];
                int next_neighbour = neighbours[rnd_id];

                if (write(pipes[next_neighbour][1], &expedition, sizeof(expedition)) == -1)
                {
                    if (errno == EPIPE)
                    {
                        valid_heighbours[rnd_id] = 0;
                    }
                    else
                    {
                        ERR("write");
                    }
                }
                else
                {
                    flag = 1;
                    break;
                }
            }

            if (!flag)
            {
                printf("Node [%d] (%d,%d): Collapsing\n", k, x, y);
                break;
            }
            if (rand()%100 < 5)
            {
                printf("Node [%d] (%d,%d): Collapsing\n", k, x, y);
                break;
            }
        }
        else
        {
            printf("Expedition %d: For those who come after\n", expedition.id);
            local_supplies += 2;
        }
    }

    int num_nodes = M*M;
    for (int i=0;i<num_nodes;i++)
    {
        if (i==k)
        {
            close(pipes[i][0]);
        }
        else if (i==n_id || i==w_id || i==s_id || i==e_id)
        {
            close(pipes[i][1]);
        }
    }
    descriptors_cnt = count_descriptors();
    printf("Node [%d] (%d, %d): Opened %d descriptors\n", k, x,y,descriptors_cnt);
}

void process_deployments(int M, int pipes[][2])
{
    if (mkfifo(MOTHERSHIP_FIFO, 0666)==-1 && errno!=EEXIST)
    {
        ERR("mkfifo");
    }

    FILE* fifo = fopen(MOTHERSHIP_FIFO, "r");
    if (!fifo)
    {
        ERR("fopen");
    }

    char line[256];
    int id = 100;
    while (fgets(line, sizeof(line), fifo) != NULL)
    {
        int x,y;
        if (sscanf(line, "SPAWN %d %d", &x, &y) == 2)
        {
            if (x<M && x>=0 && y<M && y>=0 && id>0)
            {
                int target = x*M+y;
                expedition_t exp;
                exp.id = id--;
                exp.supplies = SUPPLIES_CAPACITY;

                if (write(pipes[target][1], &exp, sizeof(exp)) == -1)
                {
                    if (errno == EPIPE)
                    {
                        printf("Expedition %d: failed\n", exp.id);
                    }
                    ERR("write");
                }
            }
        }
    }
    fclose(fifo);

    expedition_t term_expedition = {0,0};
    for (int i=0;i<M*M;i++)
    {
        if (write(pipes[i][1],&term_expedition,sizeof(term_expedition)) == -1 && errno!=EPIPE)
        {
            ERR("write");
        }
    }

    if (unlink(MOTHERSHIP_FIFO) == -1)
    {
        ERR("unlink");
    }
}

int main(int argc, char* argv[])
{
    if (argc!=2)
    {
        usage(argc, argv);
    }
    int M=atoi(argv[1]);
    if (M<2 || M>6)
    {
        usage(argc, argv);
    }

    int pipes[MAX_NODE_NUM][2];
    int num_nodes = M*M;
    for (int i=0;i<num_nodes;i++)
    {
        pipe(pipes[i]);
    }

    for (int k=0;k<num_nodes;k++)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            int x = k/M;
            int y = k%M;

            int n_id = ((x-1+M)%M)*M + y;
            int e_id = x*M + ((y+1)%M);
            int s_id = ((x+1)%M)*M + y;
            int w_id = x*M + ((y-1+M)%M);

            for (int i=0;i<num_nodes;i++)
            {
                if (i==k)
                {
                    close(pipes[i][1]);
                }
                else if (i==n_id || i==w_id || i==s_id || i==e_id)
                {
                    close(pipes[i][0]);
                }
                else
                {
                    close(pipes[i][0]);
                    close(pipes[i][1]);
                }
            }

            child_work(k,x,y,M,pipes,n_id,w_id,s_id,e_id);
            exit(EXIT_SUCCESS);
        }
    }

    for (int i=0;i<num_nodes;i++)
    {
        close(pipes[i][0]);
    }

    int descriptor_count = count_descriptors();
    printf("Mothership: Opened %d descriptors\n", descriptor_count);

    process_deployments(M, pipes);

    while (wait(NULL)>0);

    for (int i=0;i<num_nodes;i++)
    {
        close(pipes[i][1]);
    }

    descriptor_count = count_descriptors();
    printf("Mothership: Opened %d descriptors\n", descriptor_count);

    return 0;
}