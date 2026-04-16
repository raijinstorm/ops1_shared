#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <time.h>

#define MANIFEST_FILE "manifest.bin"
#define MAX_SHIPS 20
#define MAX_BAYS 5
#define MAX_SLOTS 10

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
    fprintf(stderr, "Usage: %s n m k\n", program_name);
    exit(EXIT_FAILURE);
}

void ms_sleep(unsigned int milli)
{
    struct timespec ts = {milli / 1000, (milli % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

void print_hub_state(int* slots, int m, int k)
{
    for (int i=0; i<m; ++i) {
        printf("Bay %d: ", i + 1);
        for (int j=0; j<k; ++j) printf("[%d] ", slots[i * k + j]);
        printf("\n");
    }
}

typedef struct SharedState
{
    int cnt;
    pthread_cond_t cond;
    int cond_flag;
    pthread_mutex_t cond_mtx;
    pthread_mutex_t slot_mutexes[MAX_BAYS*MAX_SLOTS];
    pthread_mutex_t start_mtx[];
}SharedState;

void child_work(int m, int idx, SharedState* state, int k)
{
    srand(getpid());
    pthread_mutex_lock(&state->start_mtx[idx]);

    pthread_mutex_lock(&state->cond_mtx);
    while (!state->cond_flag)
    {
        pthread_cond_wait(&state->cond, &state->cond_mtx);
    }
    pthread_mutex_unlock(&state->cond_mtx);

    int fd = open(MANIFEST_FILE, O_RDWR, 0666);
    if (fd<0)
    {
        ERR("open");
    }
    int memsize = sizeof(int)*m*k;
    int* cargo = (int*)mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (cargo == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(fd)<0)
    {
        ERR("close");
    }

    char buf[256];
    sem_t* semaphores[MAX_BAYS];
    for (int i=0;i<m;i++)
    {
        snprintf(buf, sizeof(buf), "/hub-bay-%d", m);
        sem_t* semaphore = sem_open(buf, O_CREAT, 0666, 1);
        if (semaphore == SEM_FAILED)
        {
            ERR("sem_open");
        }
        semaphores[i] = semaphore;
    }

    for (int i=0;i<10;i++)
    {
        int rnd_bay = rand()%m;
        int rnd_slot = rand()%k + rnd_bay*k;
        sem_wait(semaphores[rnd_bay]);
        printf("Ship %d: docking at bay %d\n", getpid(), rnd_bay);
        pthread_mutex_lock(&state->slot_mutexes[rnd_slot]);
        cargo[rnd_slot]++;
        printf("Ship %d: bay %d, slot %d, cargo: %d\n", getpid(),rnd_bay, rand()%k,cargo[rnd_slot]);
        pthread_mutex_unlock(&state->slot_mutexes[rnd_slot]);
        ms_sleep(100);
        sem_post(semaphores[rnd_bay]);
    }

    for (int i=0;i<m;i++)
    {
        if (sem_close(semaphores[i]) < 0)
        {
            ERR("sem_close");
        }
    }

    int state_size = sizeof(SharedState) + state->cnt*sizeof(pthread_mutex_t);
    if (msync(cargo, memsize, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(cargo, memsize) < 0)
    {
        ERR("munmap");
    }
    if (munmap(state, state_size) < 0)
    {
        ERR("munmap");
    }
}

int main(int argc, char** argv)
{
    if (argc!=4)
    {
        usage(argv[0]);
    }

    int n = atoi(argv[1]);
    int m = atoi(argv[2]);
    int k = atoi(argv[3]);
    if (n<10 || n>MAX_SHIPS || m<1 || m>MAX_BAYS || k<5 || k>MAX_SLOTS)
    {
        usage(argv[0]);
    }

    char buf[256];
    for (int i=0;i<m;i++)
    {
        snprintf(buf, sizeof(buf), "/hub-bay-%d", m);
        sem_unlink(buf);
    }

    int state_size = sizeof(SharedState) + n*sizeof(pthread_mutex_t);
    SharedState* state = (SharedState*)mmap(NULL, state_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (state == MAP_FAILED)
    {
        ERR("mmap");
    }
    state->cnt = n;
    state->cond_flag = 0;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<n;i++)
    {
        pthread_mutex_init(&state->start_mtx[i], &mutex_attr);
    }
    for (int i=0;i<m*k;i++)
    {
        pthread_mutex_init(&state->slot_mutexes[i], &mutex_attr);
    }
    pthread_mutex_init(&state->cond_mtx, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&state->cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);

    for (int i=0;i<n;i++)
    {
        pthread_mutex_lock(&state->start_mtx[i]);
    }
    for (int i=0;i<n;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(m,i,state, k);

            exit(EXIT_SUCCESS);
        }
    }

    ms_sleep(500);
    for (int i=n-1;i>=0;i--)
    {
        pthread_mutex_unlock(&state->start_mtx[i]);
    }

    int fd = open(MANIFEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd<0)
    {
        ERR("open");
    }
    int memsize = sizeof(int)*m*k;
    if (ftruncate(fd, memsize) == -1)
    {
        ERR("ftruncate");
    }
    int* cargo = (int*)mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (cargo == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(fd)<0)
    {
        ERR("close");
    }
    for (int i=0;i<m*k;i++)
    {
        cargo[i] = 0;
    }

    pthread_mutex_lock(&state->cond_mtx);
    state->cond_flag = 1;
    pthread_cond_broadcast(&state->cond);
    pthread_mutex_unlock(&state->cond_mtx);

    while (wait(NULL)>0);

    print_hub_state(cargo, m,k);

    for (int i=0;i<m;i++)
    {
        snprintf(buf, sizeof(buf), "/hub-bay-%d", m);
        sem_unlink(buf);
    }

    for (int i=0;i<n;i++)
    {
        pthread_mutex_destroy(&state->start_mtx[i]);
    }
    for (int i=0;i<m*k;i++)
    {
        pthread_mutex_destroy(&state->slot_mutexes[i]);
    }
    pthread_mutex_destroy(&state->cond_mtx);
    pthread_cond_destroy(&state->cond);
    if (msync(cargo, memsize, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(cargo, memsize) < 0)
    {
        ERR("munmap");
    }
    if (munmap(state, state_size) < 0)
    {
        ERR("munmap");
    }

    printf("Station offline!\n");
    unlink(MANIFEST_FILE);
    return 0;
}

