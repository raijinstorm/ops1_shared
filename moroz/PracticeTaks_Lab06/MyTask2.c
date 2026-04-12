#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants used in shm_open */
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHM_NAME "/targowisko_ring"
#define MIN_BAYS 5
#define MAX_BAYS 50
#define MIN_SHIPS 1
#define MAX_SHIPS 10

#define ERR(source)                                     \
do                                                  \
{                                                   \
fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
perror(source);                                 \
kill(0, SIGKILL);                               \
exit(EXIT_FAILURE);                             \
} while (0)

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s N S F\n", program_name);
    fprintf(stderr, "\t  N - number of loading bays, %d <= N <= %d\n", MIN_BAYS, MAX_BAYS);
    fprintf(stderr, "\t  S - number of Smuggler ships, %d <= S <= %d\n", MIN_SHIPS, MAX_SHIPS);
    fprintf(stderr, "\t  F - number of Freighter ships, %d <= F <= %d\n", MIN_SHIPS, MAX_SHIPS);
    exit(EXIT_FAILURE);
}

void ms_sleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (nanosleep(&ts, &ts))
        ERR("nanosleep");
}

typedef struct LoadingBay
{
    int cargo_id;
    float tonnage;
    pthread_mutex_t mtx_bay;
}LoadingBay;

typedef struct Station
{
    sem_t empty_bays;
    sem_t full_bays;
    int station_active;
    pthread_mutex_t state_mtx;
    LoadingBay bays[];
}Station;

typedef struct
{
    Station* station;
    int S;
    int F;
    sigset_t old_mask, new_mask;
} sighandling_args_t;

void* sighandling(void* args)
{
    sighandling_args_t* sighandling_args = (sighandling_args_t*)args;
    int signo;
    if (sigwait(&sighandling_args->new_mask, &signo))
        ERR("sigwait failed.");
    if (signo != SIGQUIT)
    {
        ERR("unexpected signal");
    }

    pthread_mutex_lock(&sighandling_args->station->state_mtx);
    sighandling_args->station->station_active = 0;
    pthread_mutex_unlock(&sighandling_args->station->state_mtx);

    for (int i=0;i<sighandling_args->S;i++)
    {
        sem_post(&sighandling_args->station->empty_bays);
    }

    for (int i=0;i<sighandling_args->F;i++)
    {
        sem_post(&sighandling_args->station->full_bays);
    }
    return NULL;
}

void smuggler_work(Station* station, int N)
{
    srand(getpid());
    for (int i=0;i<3;i++)
    {
        sem_wait(&station->empty_bays);

        if (!station->station_active)
        {
            break;
        }
        for (int j=0;j<N;j++)
        {
            int found = 0;
            pthread_mutex_lock(&station->bays[j].mtx_bay);
            if (station->bays[j].tonnage == 0.0f)
            {
                found = 1;
                int rnd_id = rand()%(9000) + 1000;
                float rnd_tonnage = ((float)rand()/(float)(RAND_MAX)) * 49.0f + 1;
                station->bays[j].tonnage = rnd_tonnage;
                station->bays[j].cargo_id = rnd_id;

                printf("[%d] Smuggler dropped cargo [%d] weighing [%f] tons in bay [%d]\n", getpid(), rnd_id, rnd_tonnage, j);
            }
            pthread_mutex_unlock(&station->bays[j].mtx_bay);

            if (found)
            {
                sem_post(&station->full_bays);
                ms_sleep(100);
                break;
            }
        }
    }
}

void freighter_work(Station* station, int N)
{
    for (int i=0;i<3;i++)
    {
        sem_wait(&station->full_bays);
        if (!station->station_active)
        {
            break;
        }
        for (int j=0;j<N;j++)
        {
            int found = 0;
            pthread_mutex_lock(&station->bays[j].mtx_bay);
            if (station->bays[j].tonnage > 0.0f)
            {
                found = 1;
                station->bays[j].tonnage = 0.0f;
                station->bays[j].cargo_id = 0;
                printf("[%d] Freighter picked up cargo from bay [%d]\n", getpid(), j);
            }
            pthread_mutex_unlock(&station->bays[j].mtx_bay);

            if (found)
            {
                sem_post(&station->empty_bays);
                ms_sleep(150);
                break;
            }
        }
    }
}

void drone_work(Station* station, int N)
{
    while (1)
    {
        if (!station->station_active)
        {
            break;

        }
        float total = 0;
        for (int i=0;i<N;i++)
        {
            pthread_mutex_lock(&station->bays[i].mtx_bay);
        }

        for (int i=0;i<N;i++)
        {
            total += station->bays[i].tonnage;
        }

        for (int i=N-1;i>=0;i--)
        {
            pthread_mutex_unlock(&station->bays[i].mtx_bay);
        }

        printf("[%d] Customs Audit: Total station tonnage is [%f]\n", getpid(), total);
        ms_sleep(200);
    }

}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        usage(argv[0]);
    }
    int N = atoi(argv[1]);
    int S = atoi(argv[2]);
    int F = atoi(argv[3]);

    if (N<5 || N>50 || S<1 || S>10 || F<1 || F>10)
    {
        usage(argv[0]);
    }

    shm_unlink(SHM_NAME);

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        if (errno == EEXIST)
        {
            shm_fd = shm_open(SHM_NAME,O_RDWR, 0666);
            if (shm_fd<0) ERR("shm_open");
        }
        if (errno != EEXIST)
        {
            ERR("shm_fd");
        }
    }

    size_t memsize = sizeof(LoadingBay)*N + sizeof(Station);

    if (ftruncate(shm_fd, memsize) == -1)
    {
        ERR("ftruncate");
    }

    Station* bays = (Station*)mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (bays == MAP_FAILED)
    {
        ERR("mmap");
    }
    sem_init(&bays->empty_bays, 1, N);
    sem_init(&bays->full_bays, 1, 0);
    bays->station_active = 1;

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<N;i++)
    {
        bays->bays[i].cargo_id = i;
        bays->bays[i].tonnage = 0.0f;
        pthread_mutex_init(&bays->bays[i].mtx_bay, &mutex_attr);
    }
    pthread_mutex_init(&bays->state_mtx, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    sighandling_args_t sighandling_args;
    sighandling_args.station = bays;
    sighandling_args.F = F;
    sighandling_args.S = S;
    sigemptyset(&sighandling_args.new_mask);
    sigaddset(&sighandling_args.new_mask, SIGQUIT);
    if (pthread_sigmask(SIG_BLOCK, &sighandling_args.new_mask, &sighandling_args.old_mask))
        ERR("SIG_BLOCK error");

    pthread_t sighandling_thread;
    pthread_create(&sighandling_thread, NULL, sighandling, &sighandling_args);

    for (int i=0;i<S;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            smuggler_work(bays, N);

            exit(EXIT_SUCCESS);
        }
    }

    for (int i=0;i<F;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            freighter_work(bays, N);

            exit(EXIT_SUCCESS);
        }
    }

    pid_t drone_pid = fork();
    if (drone_pid<0)
    {
        ERR("fork");
    }
    if (drone_pid == 0)
    {
        drone_work(bays, N);

        exit(EXIT_SUCCESS);
    }

    while (wait(NULL)>0);
    printf("[%d] Commencing station purge.\n", getpid());
    for (int i=0;i<N;i++)
    {
        printf("[%d]: tonnage=%.3f\n", bays->bays[i].cargo_id, bays->bays[i].tonnage);
    }

    pthread_join(sighandling_thread, NULL);
    sem_destroy(&bays->empty_bays);
    sem_destroy(&bays->full_bays);
    for (int i=0;i<N;i++)
    {
        pthread_mutex_destroy(&bays->bays[i].mtx_bay);
    }
    pthread_mutex_destroy(&bays->state_mtx);
    if (munmap(bays, memsize)<0)
    {
        ERR("munmap");
    }
    shm_unlink(SHM_NAME);

    return EXIT_SUCCESS;
}