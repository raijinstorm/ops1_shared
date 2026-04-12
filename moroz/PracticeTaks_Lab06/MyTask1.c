#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include<semaphore.h>
#include<string.h>

#define FILE_NAME "./dispatch_log.bin"
#define MIN_CAPACITY 10
#define MAX_CAPACITY 100
#define MIN_TAXIS 1
#define MAX_TAXIS 20
#define CUSTOMER_NUM 5

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
    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n - the capacity of the dispatch board, %d <= n <= %d\n", MIN_CAPACITY, MAX_CAPACITY);
    fprintf(stderr, "\t  m - the number of taxis in the fleet, %d <= m <= %d\n", MIN_TAXIS, MAX_TAXIS);
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

typedef struct RideRequest
{
    int id;
    int pickup_zone;
    int dropoff_zone;
    int status;
}RideRequest;

typedef struct SharedState
{
    int is_running;
    pthread_mutex_t mutex;
    sem_t sem;
    sem_t pending_rides;
    sem_t empty_slots;
}SharedState;

typedef struct sighandling_args_t{
    SharedState* state;
    int M;
    sigset_t old_mask, new_mask;
} sighandling_args_t;


void* sighandling(void* args)
{
    sighandling_args_t* sighandling_args = (sighandling_args_t*)args;
    int signo;
    if (sigwait(&sighandling_args->new_mask, &signo))
        ERR("sigwait failed.");
    if (signo != SIGINT)
    {
        ERR("unexpected signal");
    }

    pthread_mutex_lock(&sighandling_args->state->mutex);
    sighandling_args->state->is_running = 0;
    pthread_mutex_unlock(&sighandling_args->state->mutex);

    for (int i=0;i<sighandling_args->M;i++)
    {
        sem_post(&sighandling_args->state->pending_rides);
    }
    return NULL;
}

void child_work(RideRequest* requests, int N, SharedState* state)
{
    srand(getpid());
    for (int i=0;i<3;i++)
    {
        sem_wait(&state->empty_slots);

        sem_wait(&state->sem);
        for (int j=0;j<N;j++)
        {
            if (requests[j].status == 0)
            {
                requests[j].status = 1;
                requests[j].pickup_zone = rand()%100 +1;
                requests[j].dropoff_zone = rand()%100 +1;
                printf("[%d] Customer requested ride from zone %d to %d in slot %d\n", getpid(), requests[j].pickup_zone, requests[j].dropoff_zone, j);
                break;
            }
        }
        sem_post(&state->sem);
        sem_post(&state->pending_rides);
        ms_sleep(rand()%(501-100) + 100);
    }
}

void taxi_work(SharedState* state, RideRequest* requests, int N)
{
    while (1)
    {
        sem_wait(&state->pending_rides);

        if (!state->is_running)
        {
            break;
        }

        sem_wait(&state->sem);
        for (int i=0;i<N;i++)
        {
            if (requests[i].status == 1)
            {
                requests[i].status = 0;
                printf("[%d] Taxi completed ride from zone %d to zone %d\n", getpid(), requests[i].pickup_zone, requests[i].dropoff_zone);
                break;
            }
        }
        sem_post(&state->sem);
        sem_post(&state->empty_slots);
        ms_sleep(200);
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

    if (N<10 || N>100 || M<1 || M>20)
    {
        usage(argv[0]);
    }


    int fd = open(FILE_NAME, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd<0)
    {
        ERR("open");
    }
    int memsize = sizeof(RideRequest)*N;
    if (ftruncate(fd, memsize) == -1)
    {
        ERR("ftruncate");
    }
    RideRequest* requests = (RideRequest*)mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (requests == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(fd)<0)
    {
        ERR("close");
    }

    int state_size = sizeof(SharedState);
    SharedState* state = (SharedState*)mmap(NULL,state_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (state == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (sem_init(&state->sem, 1, 1)==-1)
    {
        ERR("sem_init");
    }
    if (sem_init(&state->empty_slots, 1, N)==-1)
    {
        ERR("sem_init");
    }
    if (sem_init(&state->pending_rides, 1, 0)==-1)
    {
        ERR("sem_init");
    }
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&state->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    state->is_running = 1;

    for (int i=0;i<N;i++)
    {
        requests[i].id = i;
        requests[i].dropoff_zone = 0;
        requests[i].pickup_zone = 0;
        requests[i].status = 0;
    }

    sighandling_args_t sighandling_args;
    sighandling_args.state = state;
    sighandling_args.M = M;

    sigemptyset(&sighandling_args.new_mask);
    sigaddset(&sighandling_args.new_mask, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &sighandling_args.new_mask, &sighandling_args.old_mask))
        ERR("SIG_BLOCK error");

    pthread_t sighandling_thread;
    pthread_create(&sighandling_thread, NULL, sighandling, &sighandling_args);

    for (int i=0;i<CUSTOMER_NUM;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(requests, N, state);

            exit(EXIT_SUCCESS);
        }
    }

    for (int i=0;i<M;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            taxi_work(state, requests, N);

            exit(EXIT_SUCCESS);
        }
    }

    while (wait(NULL)>0);
    pthread_join(sighandling_thread, NULL);
    printf("[%d] Dispatch system offline. Clean shutdown.\n", getpid());

    for (int i=0;i<N;i++)
    {
        printf("[%d]: %d -> %d\n", requests[i].id, requests[i].pickup_zone, requests[i].dropoff_zone);
    }


    if (sem_destroy(&state->sem)==-1)
    {
        ERR("sem_init");
    }
    if (sem_destroy(&state->pending_rides)==-1)
    {
        ERR("sem_init");
    }
    if (sem_destroy(&state->empty_slots)==-1)
    {
        ERR("sem_init");
    }
    pthread_mutex_destroy(&state->mutex);
    if (msync(requests, memsize, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(requests, memsize) == -1)
    {
        ERR("munmap");
    }
    if (munmap(state, state_size) == -1)
    {
        ERR("munmap");
    }
}

