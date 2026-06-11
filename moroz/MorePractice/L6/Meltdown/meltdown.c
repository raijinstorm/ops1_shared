#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>


#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define SERVER_ROOM_DATA "./server_room"

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "10<= N <=100 - the number of server racks\n");
    fprintf(stderr, "2<= M <=20 - the number of interns\n");
    exit(EXIT_FAILURE);
}

void msleep(long msec) {
    struct timespec ts;
    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

typedef struct ServerRoom
{
    int N;
    int alive;
    int racks[];
}ServerRoom;

typedef struct Synchro
{
    pthread_barrier_t barrier;
    pthread_cond_t temp_cond;
    pthread_mutex_t cond_mtx;
    int temp_flag;
    pthread_mutex_t alive_mutex;
    pthread_mutex_t mutexes[];
}Synchro;

void print_rack(ServerRoom* server_room)
{
    for (int i=0;i<server_room->N;i++)
    {
        printf("%d ", server_room->racks[i]);
    }
    printf("\n");
}

void child_work(ServerRoom* server_room, Synchro* synchro)
{
    printf("[%d] Intern reporting for duty\n", getpid());
    pthread_barrier_wait(&synchro->barrier);
    srand(getpid());

    while (1)
    {
        int rand_rack = rand()%(server_room->N);
        if (pthread_mutex_lock(&synchro->mutexes[rand_rack]) == EOWNERDEAD)
        {
            printf("[%d] Found a frozen intern at rack %d. Kicking them out of the way\n", getpid(), rand_rack);
            pthread_mutex_consistent(&synchro->mutexes[rand_rack]);
        }
        msleep(50);
        server_room->racks[rand_rack] -= 5;
        int curr_rack_temp = server_room->racks[rand_rack];
        printf("[%d] Cooled rack %d to %d degrees\n", getpid(), rand_rack, server_room->racks[rand_rack]);

        if (rand()%100<2)
        {
            printf("[%d] Intern accidentally froze themselves solid!\n", getpid());
            pthread_mutex_lock(&synchro->alive_mutex);
            server_room->alive -= 1;
            int stop = (server_room->alive<=0);
            pthread_mutex_unlock(&synchro->alive_mutex);
            if (stop)
            {
                pthread_mutex_lock(&synchro->cond_mtx);
                synchro->temp_flag = 1;
                if (pthread_cond_signal(&synchro->temp_cond)!=0)
                {
                    ERR("pthread_cond_signal");
                }
                pthread_mutex_unlock(&synchro->cond_mtx);
            }
            abort();
        }
        pthread_mutex_unlock(&synchro->mutexes[rand_rack]);

        if (curr_rack_temp<20)
        {
            pthread_mutex_lock(&synchro->cond_mtx);
            synchro->temp_flag = 1;
            if (pthread_cond_signal(&synchro->temp_cond)!=0)
            {
                ERR("pthread_cond_signal");
            }
            pthread_mutex_unlock(&synchro->cond_mtx);
        }
        msleep(100);
    }

    int synchro_size = sizeof(Synchro) + sizeof(pthread_mutex_t)*server_room->N;
    int size_server_room = sizeof(ServerRoom) + server_room->N*sizeof(int);
    if (munmap(synchro, synchro_size) == -1)
    {
        ERR("munmap");
    }
    if (msync(server_room, size_server_room, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(server_room, size_server_room) == -1)
    {
        ERR("munmap");
    }
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }

    int N = atoi(argv[1]);
    int M = atoi(argv[2]);
    if (N<10 || N>100 || M<2 || M>20)
    {
        usage(argv[0]);
    }
    srand(time(NULL));

    int size_server_room = sizeof(ServerRoom) + N*sizeof(int);
    int fd = open(SERVER_ROOM_DATA, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd<0)
    {
        ERR("open");
    }
    if (ftruncate(fd, size_server_room) == -1)
    {
        ERR("ftruncate");
    }
    ServerRoom* server_room = (ServerRoom*)mmap(NULL, size_server_room, PROT_READ | PROT_WRITE, MAP_SHARED,fd,0);
    if (server_room == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(fd)<0)
    {
        ERR("close");
    }

    for (int i=0;i<N;i++)
    {
        server_room->racks[i] = rand()%(100-80+1)+80;
    }
    server_room->N = N;
    server_room->alive = M;

    int synchro_size = sizeof(Synchro) + sizeof(pthread_mutex_t)*N;
    Synchro* synchro = (Synchro*)mmap(NULL, synchro_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (synchro == MAP_FAILED)
    {
        ERR("mmap");
    }
    pthread_barrierattr_t barrier_attr;
    pthread_barrierattr_init(&barrier_attr);
    pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);

    pthread_barrier_init(&synchro->barrier, &barrier_attr, M+1);

    pthread_barrierattr_destroy(&barrier_attr);

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<N;i++)
    {
        pthread_mutex_init(&synchro->mutexes[i], &mutex_attr);
    }
    pthread_mutex_init(&synchro->cond_mtx, &mutex_attr);
    pthread_mutex_init(&synchro->alive_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&synchro->temp_cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
    synchro->temp_flag = 0;

    for (int i=0;i<M;i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(server_room, synchro);

            exit(EXIT_SUCCESS);
        }
    }

    msleep(500);
    print_rack(server_room);
    printf("[%d] Sysadmin: Open the doors!\n", getpid());
    pthread_barrier_wait(&synchro->barrier);

    while (1)
    {
        pthread_mutex_lock(&synchro->alive_mutex);
        int stop = (server_room->alive<=0);
        pthread_mutex_unlock(&synchro->alive_mutex);
        if (stop)
        {
            printf("[%d] All interns are frozen. Shift is over.\n", getpid());
            break;
        }

        pthread_mutex_lock(&synchro->cond_mtx);
        while (!synchro->temp_flag)
        {
            pthread_cond_wait(&synchro->temp_cond, &synchro->cond_mtx);
        }
        printf("[%d] Sysadmin: It's too cold in here!\n", getpid());
        for (int i=0;i<N;i++)
        {
            if (pthread_mutex_lock(&synchro->mutexes[i]) == EOWNERDEAD)
            {
                printf("[%d] Found a frozen intern at rack %d. Kicking them out of the way\n", getpid(), i);
                pthread_mutex_consistent(&synchro->mutexes[i]);
            }
            if (server_room->racks[i]<20)
            {
                server_room->racks[i] = 80;
                printf("[%d] Sysadmin defrosted rack %d\n", getpid(), i);
            }
            pthread_mutex_unlock(&synchro->mutexes[i]);
        }
        synchro->temp_flag = 0;
        pthread_mutex_unlock(&synchro->cond_mtx);
    }

    while (wait(NULL)>0);

    pthread_mutex_destroy(&synchro->cond_mtx);
    pthread_mutex_destroy(&synchro->alive_mutex);
    pthread_cond_destroy(&synchro->temp_cond);
    for (int i=0;i<N;i++)
    {
        pthread_mutex_destroy(&synchro->mutexes[i]);
    }
    pthread_barrier_destroy(&synchro->barrier);
    if (munmap(synchro, synchro_size) == -1)
    {
        ERR("munmap");
    }
    if (msync(server_room, size_server_room, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(server_room, size_server_room) == -1)
    {
        ERR("munmap");
    }
}