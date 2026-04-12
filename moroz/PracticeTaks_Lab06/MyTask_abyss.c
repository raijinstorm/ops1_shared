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

#define GRID_FILENAME "./ABYSS_GRID"
#define MIN_SIDE 4
#define MAX_SIDE 16
#define MIN_DRONES 1
#define MAX_DRONES 32
#define NUM_NEIGHBOURS 6

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
    fprintf(stderr, "\t%s s w\n", program_name);
    fprintf(stderr, "\t  s - length of grid side, %d <= s <= %d\n", MIN_SIDE, MAX_SIDE);
    fprintf(stderr, "\t  w - number of drones, %d <= w <= %d\n", MIN_DRONES, MAX_DRONES);
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

typedef struct SharedState
{
    pthread_mutex_t alarm_mtx;
    int alarm;
    pthread_mutex_t cnt_mutex;
    int cnt;
    pthread_mutex_t grid_mutexes[];
}SharedState;

static int cmp_int(const void* f,const void* s)
{
    return (*((int*)f))-(*((int*)s));
}

void drone_work(int* grid, int N, int S, SharedState* state)
{
    srand(getpid());
    printf("[%d] Drone deployed\n", getpid());

    for (;;)
    {
        pthread_mutex_lock(&state->alarm_mtx);
        if (state->alarm == 1)
        {
            printf("[%d] Finishes work\n", getpid());
            pthread_mutex_unlock(&state->alarm_mtx);
            break;
        }
        pthread_mutex_unlock(&state->alarm_mtx);

        if (pthread_mutex_lock(&state->cnt_mutex) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&state->cnt_mutex);
        }
        if (rand()%100<2)
        {
            state->cnt--;
            printf("[%d] Drone crushed by pressure.\n", getpid());
            abort();
        }
        pthread_mutex_unlock(&state->cnt_mutex);

        int id = rand()%N;

        int r = id/S;
        int c = id%S;

        int neighbours[NUM_NEIGHBOURS+1];
        for (int j=0;j<NUM_NEIGHBOURS;j++)
        {
            neighbours[j] = -1;
        }
        neighbours[NUM_NEIGHBOURS] = id;
        if (c!=0)
        {
            neighbours[0] = r*S+c-1;
        }
        if (c!=(S-1))
        {
            neighbours[1] = r*S+c+1;
        }
        if (r%2==0)
        {
            if (r!=0)
            {
                if (c!=0)
                {
                    neighbours[2] = (r-1)*S+c-1;
                }
                neighbours[3] = (r-1)*S+c;
            }
            if (r!=S-1)
            {
                if (c!=0)
                {
                    neighbours[4] = (r+1)*S + c-1;
                }
                neighbours[5] = (r+1)*S + c;
            }
        }
        else
        {
            if (r!=0)
            {
                neighbours[2] = (r-1)*S+c;
                if (c!=S-1)
                {
                    neighbours[3] = (r-1)*S+c+1;
                }
            }
            if (r!=S-1)
            {
                neighbours[4] = (r+1)*S + c;
                if (c!=S-1)
                {
                    neighbours[5] = (r+1)*S + c+1;
                }
            }
        }


        qsort(neighbours, NUM_NEIGHBOURS+1, sizeof(int), cmp_int);
        int max_id = 0;
        int min_id = 0;
        for (int j=0;j<NUM_NEIGHBOURS+1;j++)
        {
            if (neighbours[max_id] == -1) max_id++;
            if (neighbours[min_id] == -1) min_id++;
        }
        for (int j=0;j<NUM_NEIGHBOURS+1;j++)
        {
            if (neighbours[j]!=-1)
            {
                pthread_mutex_lock(&state->grid_mutexes[neighbours[j]]);
            }
        }
        for (int j=0;j<NUM_NEIGHBOURS+1;j++)
        {
            if (neighbours[j]!=-1 && neighbours[j]!=id)
            {
                if (grid[neighbours[j]] > grid[neighbours[max_id]])
                {
                    max_id = j;
                }
                if (grid[neighbours[j]] < grid[neighbours[min_id]])
                {
                    min_id  = j;
                }
            }
        }

        swap(&grid[neighbours[max_id]], &grid[neighbours[min_id]]);

        for (int j=NUM_NEIGHBOURS+1-1;j>=0;j--)
        {
            if (neighbours[j]!=-1)
            {
                pthread_mutex_unlock(&state->grid_mutexes[neighbours[j]]);
            }
        }

        printf("[%d] Performed a stablisation routine\n", getpid());
        ms_sleep(50);
    }

    if (pthread_mutex_lock(&state->cnt_mutex) == EOWNERDEAD)
    {
        pthread_mutex_consistent(&state->cnt_mutex);
    }
    state->cnt--;
    pthread_mutex_unlock(&state->cnt_mutex);
}

void mcp_work(int N, int S, int* grid, SharedState* state)
{
    printf("[%d] MCP online.\n", getpid());

    while (1)
    {
        ms_sleep(300);

        int grid_size = N*sizeof(int);
        if (msync(grid, grid_size, MS_SYNC) == -1)
        {
            ERR("msync");
        }
        int delta_variance = 0;
        for (int i=0;i<S;i++)
        {
            for (int j=1;j<S;j++)
            {
                int id = i*S+j;
                pthread_mutex_lock(&state->grid_mutexes[id-1]);
                pthread_mutex_lock(&state->grid_mutexes[id]);

                delta_variance += abs(grid[id-1] - grid[id]);

                pthread_mutex_unlock(&state->grid_mutexes[id]);
                pthread_mutex_unlock(&state->grid_mutexes[id-1]);
            }
        }
        printf("[%d] Current Delta Variance: %d\n", getpid(), delta_variance);

        if (pthread_mutex_lock(&state->cnt_mutex) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&state->cnt_mutex);
        }
        printf("[%d] active Drones: %d\n", getpid(), state->cnt);
        int active_num = state->cnt;
        pthread_mutex_unlock(&state->cnt_mutex);
        if (active_num<=0)
        {
            pthread_mutex_lock(&state->alarm_mtx);
            state->alarm = 1;
            pthread_mutex_unlock(&state->alarm_mtx);
            printf("[%d] All drones destroyed. We are doomed\n", getpid());
            break;
        }

        int threshold = N*S;
        if (delta_variance>threshold)
        {
            pthread_mutex_lock(&state->alarm_mtx);
            state->alarm = 1;
            pthread_mutex_unlock(&state->alarm_mtx);

            printf("[%d] Hull integrity critical. Evacuate.\n", getpid());
            break;
        }
    }
}

int main(int argc, char** argv)
{
    srand(time(NULL));
    if (argc != 3) usage(argv[0]);
    int S = atoi(argv[1]);
    int W = atoi(argv[2]);
    if (S<MIN_SIDE || S>MAX_SIDE || W<MIN_DRONES || W>MAX_DRONES)
    {
        usage(argv[0]);
    }

    int N = S*S;

    int fd = open(GRID_FILENAME, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd<0)
    {
        ERR("open");
    }
    size_t pressure_memsize = N*sizeof(int);
    if (ftruncate(fd, pressure_memsize) == -1)
    {
        ERR("ftruncate");
    }
    int* pressure = (int*)mmap(NULL, pressure_memsize, PROT_READ | PROT_WRITE, MAP_SHARED,fd,0);
    if (pressure == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(fd)<0)
    {
        ERR("close");
    }
    for (int i=0;i<N;i++)
    {
        pressure[i] = i+1;
    }
    shuffle(pressure, N);

    int state_size = sizeof(SharedState) + N*sizeof(pthread_mutex_t);
    SharedState* state = (SharedState*)mmap(NULL, state_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (state == MAP_FAILED)
    {
        ERR("mmap");
    }
    state->alarm = 0;
    state->cnt = W;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<N;i++)
    {
        pthread_mutex_init(&state->grid_mutexes[i], &mutex_attr);
    }
    pthread_mutex_init(&state->alarm_mtx, &mutex_attr);
    pthread_mutex_init(&state->cnt_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    for (int i=0;i<W;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            drone_work(pressure, N, S, state);

            exit(EXIT_SUCCESS);
        }
    }

    pid_t mcp_pid = fork();
    if (mcp_pid<0)
    {
        ERR("fork");
    }
    if (mcp_pid == 0)
    {
        mcp_work(N,S,pressure,state);

        exit(EXIT_SUCCESS);
    }

    while (wait(NULL)>0);

    print_array(pressure, N);
    for (int i=0;i<N;i++)
    {
        pthread_mutex_destroy(&state->grid_mutexes[i]);
    }
    pthread_mutex_destroy(&state->alarm_mtx);
    pthread_mutex_destroy(&state->cnt_mutex);
    if (munmap(state, state_size) == -1)
    {
        ERR("munmap");
    }
    if (msync(pressure, pressure_memsize, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(pressure, pressure_memsize) == -1)
    {
        ERR("munmap");
    }

    return EXIT_SUCCESS;
}