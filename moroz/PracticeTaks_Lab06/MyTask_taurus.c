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

#define SHM_NAME "tartarus_vault"

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
    fprintf(stderr, "\t%s H N\n", program_name);
    fprintf(stderr, "\t  H - number of Hackers, 3 <= H <= 15\n");
    fprintf(stderr, "\t  N - number of security nodes, 5 <= N <= 30\n");
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

typedef struct SharedState
{
    pthread_barrier_t barrier;
    int disabled_nodes;
    int hacking;
    int entered_zero;
    pthread_mutex_t disabled_nodes_mutex;
    pthread_cond_t cond;
    pthread_mutex_t cond_mutex;
    sem_t hacker_sem;
    pthread_mutex_t nodes_mutexes[];
}SharedState;

void hacker_work(SharedState* state, int* nodes, int N)
{
    srand(getpid());
    ms_sleep(rand()%(500-100+1) + 100);
    printf("[%d] hacker in position. Waiting for a crew\n", getpid());

    pthread_barrier_wait(&state->barrier);
    printf("[%d] Crew assemebled. Breach initiated\n", getpid());

    while (1)
    {

        pthread_mutex_lock(&state->cond_mutex);
        if (state->hacking == 0)
        {
            pthread_mutex_unlock(&state->cond_mutex);
            break;
        }
        pthread_mutex_unlock(&state->cond_mutex);
        int id = rand()%N;
        if (pthread_mutex_lock(&state->nodes_mutexes[id]) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&state->nodes_mutexes[id]);
            printf("[%d] Is cleaning the remainings of the dead body\n", getpid());
        }
        if (id == 0 && !state->entered_zero)
        {
            state->entered_zero = 1;
            printf("[%d] Was killed in zone 0\n", getpid());
            abort();
        }
        int flag = 0;
        if (nodes[id] == 1)
        {
            flag = 1;
            nodes[id] = 0;
            pthread_mutex_lock(&state->disabled_nodes_mutex);
            state->disabled_nodes++;
            if (state->disabled_nodes == N)
            {
                state->hacking = 0;
            }
            pthread_mutex_unlock(&state->disabled_nodes_mutex);
            printf("[%d] Hacked a node %d\n", getpid(), id);
        }
        pthread_mutex_unlock(&state->nodes_mutexes[id]);
        ms_sleep(100);
        if (flag)
        {
            pthread_mutex_lock(&state->cond_mutex);
            if (pthread_cond_signal(&state->cond) != 0)
                ERR("pthread_cond_signal");
            pthread_mutex_unlock(&state->cond_mutex);
        }
    }

    sem_wait(&state->hacker_sem);
    ms_sleep(200);
    printf("[%d] Hacker secured loot and escaped\n", getpid());
    sem_post(&state->hacker_sem);
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        usage(argv[0]);
    }
    int H = atoi(argv[1]);
    int N = atoi(argv[2]);
    if (H<3 || H>15 || N<5 || N>30)
    {
        usage(argv[0]);
    }

    int fd = open(SHM_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1)
    {
        ERR("open");
    }

    size_t memsize = sizeof(SharedState) + sizeof(pthread_mutex_t)*N;
    if (ftruncate(fd, memsize) == -1)
    {
        ERR("ftruncate");
    }
    SharedState* state = (SharedState*)mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (state == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(fd)<0)
    {
        ERR("close");
    }

    state->disabled_nodes = 0;
    state->hacking = 1;
    state->entered_zero = 0;
    sem_init(&state->hacker_sem, 1, 2);
    pthread_barrierattr_t barrier_attr;
    pthread_barrierattr_init(&barrier_attr);
    pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);
    pthread_barrier_init(&state->barrier, &barrier_attr, H);
    pthread_barrierattr_destroy(&barrier_attr);

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<N;i++)
    {
        pthread_mutex_init(&state->nodes_mutexes[i], &mutex_attr);
    }
    pthread_mutex_init(&state->cond_mutex, &mutex_attr);
    pthread_mutex_init(&state->disabled_nodes_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&state->cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);

    size_t nodes_size = sizeof(int)*N;
    int* nodes = (int*)mmap(NULL, nodes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (nodes == MAP_FAILED)
    {
        ERR("mmap");
    }
    for (int i=0;i<N;i++)
    {
        nodes[i] = 1;
    }

    for (int i=0;i<H;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            hacker_work(state, nodes, N);

            exit(EXIT_SUCCESS);
        }
    }

    pthread_mutex_lock(&state->cond_mutex);
    while (1)
    {
        pthread_mutex_lock(&state->disabled_nodes_mutex);
        if (state->hacking == 0)
        {
            pthread_mutex_unlock(&state->disabled_nodes_mutex);
            break;
        }
        pthread_mutex_unlock(&state->disabled_nodes_mutex);

        pthread_cond_wait(&state->cond, &state->cond_mutex);
    }
    pthread_mutex_unlock(&state->cond_mutex);
    printf("[%d] MASTERMIND: All nodes disabled. Opening Vault!\n", getpid());

    while (wait(NULL)>0);

    sem_destroy(&state->hacker_sem);
    pthread_barrier_destroy(&state->barrier);
    pthread_cond_destroy(&state->cond);
    pthread_mutex_destroy(&state->cond_mutex);
    pthread_mutex_destroy(&state->disabled_nodes_mutex);
    for (int i=0;i<N;i++)
    {
        pthread_mutex_destroy(&state->nodes_mutexes[i]);
    }
    if (munmap(nodes, nodes_size) == -1)
    {
        ERR("munmap");
    }
    if (msync(state, memsize, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(state, memsize) == -1)
    {
        ERR("munmap");
    }

    return EXIT_SUCCESS;
}