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

#define SHOP_FILENAME "./shop"
#define MIN_SHELVES 8
#define MAX_SHELVES 256
#define MIN_WORKERS 1
#define MAX_WORKERS 64

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
    fprintf(stderr, "\t  n - number of items (shelves), %d <= n <= %d\n", MIN_SHELVES, MAX_SHELVES);
    fprintf(stderr, "\t  m - number of workers, %d <= m <= %d\n", MIN_WORKERS, MAX_WORKERS);
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
    int sorted;
    int alive_count;
    pthread_mutex_t alive_mutex;
    pthread_mutex_t mutexes[];
}SharedState;

void swap(int* x, int* y)
{
    int tmp = *y;
    *y = *x;
    *x = tmp;
}

void shuffle(int* array, int n)
{
    for (int i = n - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        swap(&array[i], &array[j]);
    }
}

void print_array(int* array, int n)
{
    for (int i = 0; i < n; ++i)
    {
        printf("%3d ", array[i]);
    }
    printf("\n");
}

void child_work(int n, int* shelves, SharedState* mutexes)
{
    srand(getpid());

    printf("[%d] Worker reports for a night shift.\n", getpid());

    while (!mutexes->sorted)
    {
        int idx1 = rand() % n;
        int idx2 = rand() % n;

        while (idx1 == idx2)
        {
            idx2 = rand()%n;
        }

        if (idx1>idx2)
        {
            int temp = idx2;
            idx2 = idx1;
            idx1 = temp;
        }

        if (pthread_mutex_lock(&mutexes->mutexes[idx1]) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&mutexes->mutexes[idx1]);
            printf("[%d] Found a dead body in aisle %d\n", getpid(), idx1);
        }
        if (pthread_mutex_lock(&mutexes->mutexes[idx2]) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&mutexes->mutexes[idx2]);
            printf("[%d] Found a dead body in aisle %d\n", getpid(), idx2);
        }
        if (rand()%100<1)
        {
            printf("[%d] Trips over pallet and dies\n", getpid());
            pthread_mutex_lock(&mutexes->alive_mutex);
            mutexes->alive_count--;
            pthread_mutex_unlock(&mutexes->alive_mutex);
            abort();
        }
        if (shelves[idx1]>shelves[idx2])
        {
            ms_sleep(100);
            swap(&shelves[idx1], &shelves[idx2]);
        }

        pthread_mutex_unlock(&mutexes->mutexes[idx2]);
        pthread_mutex_unlock(&mutexes->mutexes[idx1]);
    }
}

void create_children(int M, int N, int*shelves, SharedState* mutexes)
{
    for (int i=0;i<M;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(N,shelves, mutexes);
            exit(EXIT_SUCCESS);
        }
    }
}


void manager_work(int N, int M, int* shelves, SharedState* mutexes)
{
    printf("[%d] Manager reports for a night shift\n", getpid());
    while (1)
    {
        for (int i=0;i<N;i++)
        {
            if (pthread_mutex_lock(&mutexes->mutexes[i]) == EOWNERDEAD)
            {
                pthread_mutex_consistent(&mutexes->mutexes[i]);
                printf("[%d] Found a dead body in aisle %d\n", getpid(), i);
            }
        }

        print_array(shelves, N);
        printf("[%d] Workers alive: %d\n", getpid(), mutexes->alive_count);

        if (msync(shelves, N*sizeof(int), MS_SYNC) == -1)
        {
            ERR("msync");
        }
        int flag=0;
        for (int i=1;i<N;i++)
        {
            if (shelves[i-1]>shelves[i])
            {
                flag = 1;
                break;
            }
        }

        for (int i=N-1;i>=0;i--)
        {
            pthread_mutex_unlock(&mutexes->mutexes[i]);
        }

        if (mutexes->alive_count < 1)
        {
            printf("[%d] All workers died, I hate my job\n", getpid());
            break;
        }
        if (flag == 0)
        {
            printf("[%d] The shop shelves are sorted\n", getpid());
            mutexes->sorted = 1;

            break;
        }

        ms_sleep(500);
    }
    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }
    int N = atoi(argv[1]);
    int M = atoi(argv[2]);
    if (N<MIN_SHELVES || N>MAX_SHELVES || M<MIN_WORKERS || M>MAX_WORKERS)
    {
        usage(argv[0]);
    }

    srand(time(NULL));

    int fd = open(SHOP_FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1)
    {
        ERR("open");
    }

    size_t memsize = N*sizeof(int);
    if (ftruncate(fd, memsize) == -1)
    {
        ERR("ftruncate");
    }

    int* shelves = mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shelves == MAP_FAILED)
    {
        ERR("mmap");
    }

    if (close(fd)<0)
    {
        ERR("close");
    }

    for (int i=0;i<N;i++)
    {
        shelves[i] = i+1;
    }
    shuffle(shelves, N);

    size_t anon_size = N*sizeof(pthread_mutex_t) + sizeof(SharedState);
    SharedState *mutexes = mmap(NULL,anon_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mutexes == MAP_FAILED)
    {
        ERR("mmap");
    }
    mutexes->sorted = 0;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<N;i++)
    {
        pthread_mutex_init(&mutexes->mutexes[i], &mutex_attr);
    }
    pthread_mutex_init(&mutexes->alive_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    mutexes->alive_count = N;

    print_array(shelves, N);

    create_children(M,N,shelves, mutexes);
    pid_t pid = fork();
    if (pid == 0)
    {
        manager_work(N,M,shelves,mutexes);
    }

    while (wait(NULL)>0);
    print_array(shelves, N);
    printf("Night shift in Bitronka is over\n");

    for (int i=0;i<N;i++)
    {
        pthread_mutex_destroy(&mutexes->mutexes[i]);
    }
    pthread_mutex_destroy(&mutexes->alive_mutex);

    if (munmap(mutexes, anon_size) == -1)
    {
        ERR("munmap");
    }
    if (msync(shelves, memsize, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(shelves, memsize) == -1)
    {
        ERR("munmap");
    }
    return 0;
}
