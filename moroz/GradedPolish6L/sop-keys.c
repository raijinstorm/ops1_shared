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

#define KEYBOARD_CAP 10
#define SHARED_MEM_NAME "/memory"
#define MIN_STUDENTS KEYBOARD_CAP
#define MAX_STUDENTS 20
#define MIN_KEYBOARDS 1
#define MAX_KEYBOARDS 5
#define MIN_KEYS 5
#define MAX_KEYS KEYBOARD_CAP

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
    fprintf(stderr, "\t%s n m k\n", program_name);
    fprintf(stderr, "\t  n - number of students, %d <= n <= %d\n", MIN_STUDENTS, MAX_STUDENTS);
    fprintf(stderr, "\t  m - number of keyboards, %d <= m <= %d\n", MIN_KEYBOARDS, MAX_KEYBOARDS);
    fprintf(stderr, "\t  k - number of keys in a keyboard, %d <= k <= %d\n", MIN_KEYS, MAX_KEYS);
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

void print_keyboards_state(double* keyboards, int m, int k)
{
    for (int i=0;i<m;++i)
    {
        printf("Klawiatura nr %d:\n", i + 1);
        for (int j=0;j<k;++j)
            printf("  %e", keyboards[i * k + j]);
        printf("\n\n");
    }
}

typedef struct SharedState
{
    pthread_barrier_t barrier;
    pthread_mutex_t flag_mutex;
    int dead_flag;
    pthread_mutex_t keyboards[MAX_KEYBOARDS*MAX_KEYS];
}SharedState;

void child_work(int m, SharedState* state, int k)
{
    srand(getpid());

    pthread_barrier_wait(&state->barrier);

    int fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1)
    {
        ERR("shm_open");
    }
    int keyboards_memsize = sizeof(double)*m*k;
    double* keyboards = (double*)mmap(NULL, keyboards_memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (keyboards == MAP_FAILED)
    {
        ERR("mmap");
    }

    sem_t* sem_array[MAX_KEYBOARDS];
    for (int i=0;i<m;i++)
    {
        char buf[256];
        snprintf(buf,sizeof(buf), "/sop-sem-%d", i);
        sem_t* semaphore = sem_open(buf, O_CREAT, 0666, KEYBOARD_CAP);
        if (semaphore == SEM_FAILED)
        {
            ERR("sem_open");
        }
        sem_array[i] = semaphore;
    }


    for (;;)
    {
        if (state->dead_flag)
        {
            break;
        }
        int sem_id = rand()%m;
        sem_wait(sem_array[sem_id]);
        int id = rand()%(k) + sem_id*k;
        printf("Student %d: cleaning keyboard %d, key %d\n", getpid(),sem_id, id);
        ms_sleep(300);
        if (pthread_mutex_lock(&state->keyboards[id])==EOWNERDEAD)
        {
            pthread_mutex_consistent(&state->keyboards[id]);
            printf("Student %d: someone is lying here, help!!!\n", getpid());
            pthread_mutex_lock(&state->flag_mutex);
            state->dead_flag = 1;
            pthread_mutex_unlock(&state->flag_mutex);
            pthread_mutex_unlock(&state->keyboards[id]);
            break;
        }
        if (rand()%100<1)
        {
            printf("Student %d: I have no more strength!\n", getpid());
            sem_post(sem_array[sem_id]);
            abort();
        }
        keyboards[id]/=3;
        pthread_mutex_unlock(&state->keyboards[id]);
        sem_post(sem_array[sem_id]);
    }


    if (msync(keyboards, keyboards_memsize, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(keyboards, keyboards_memsize) == -1)
    {
        ERR("munmap");
    }
    int memsize = sizeof(SharedState);
    if (munmap(state, memsize) == -1)
    {
        ERR("munmap");
    }
    close(fd);
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

    shm_unlink(SHARED_MEM_NAME);

    if (n<KEYBOARD_CAP || n>20 || m<1 || m>5 || k<5 || k>KEYBOARD_CAP)
    {
        usage(argv[0]);
    }

    char buf[256];
    for (int i=0;i<m;i++)
    {
        snprintf(buf,sizeof(buf), "/sop-sem-%d", i);
        sem_unlink(buf);
    }

    int memsize = sizeof(SharedState);
    SharedState* state = (SharedState*)mmap(NULL,memsize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (state == MAP_FAILED)
    {
        ERR("mmap");
    }
    state->dead_flag=0;
    pthread_barrierattr_t barrier_attr;
    pthread_barrierattr_init(&barrier_attr);
    pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);
    pthread_barrier_init(&state->barrier, &barrier_attr, n+1);
    pthread_barrierattr_destroy(&barrier_attr);

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<m*k;i++)
    {
        pthread_mutex_init(&state->keyboards[i], &mutex_attr);
    }
    pthread_mutex_init(&state->flag_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    for (int i=0;i<n;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(m, state,k);

            exit(EXIT_SUCCESS);
        }
    }

    int fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1)
    {
        ERR("shm_open");
    }
    int keyboards_memsize = sizeof(double)*m*k;
    if (ftruncate(fd, keyboards_memsize) == -1)
    {
        ERR("ftruncate");
    }
    double* keyboards = (double*)mmap(NULL, keyboards_memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (keyboards == MAP_FAILED)
    {
        ERR("mmap");
    }
    for (int i=0;i<m*k;i++)
    {
        keyboards[i] = 1.0;
    }

    ms_sleep(500);
    pthread_barrier_wait(&state->barrier);

    while (wait(NULL)>0);

    for (int i=0;i<m;i++)
    {
        snprintf(buf,sizeof(buf), "/sop-sem-%d", i);
        sem_unlink(buf);
    }
    print_keyboards_state(keyboards, m,k);

    printf("Cleaning finished!\n");

    for (int i=0;i<m*k;i++)
    {
        pthread_mutex_destroy(&state->keyboards[i]);
    }
    pthread_mutex_destroy(&state->flag_mutex);
    pthread_barrier_destroy(&state->barrier);

    if (msync(keyboards, keyboards_memsize, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(keyboards, keyboards_memsize) == -1)
    {
        ERR("munmap");
    }
    if (munmap(state, memsize) == -1)
    {
        ERR("munmap");
    }

    shm_unlink(SHARED_MEM_NAME);
    close(fd);
    return 0;
}