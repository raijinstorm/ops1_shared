#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include<semaphore.h>
#include<errno.h>
#include<signal.h>
#include <string.h>

#define ERR(source)                                     \
do                                                  \
{                                                   \
fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
perror(source);                                 \
kill(0, SIGKILL);                               \
exit(EXIT_FAILURE);                             \
} while (0)

#define SEM_NAME "/integral_sem"
#define SHM_NAME "/integral_shm"
// Values of this function are in range (0,1]
double func(double x)
{
    usleep(2000);
    return exp(-x * x);
}

/**
 * It counts hit points by Monte Carlo method.
 * Use it to process one batch of computation.
 * @param N Number of points to randomize
 * @param a Lower bound of integration
 * @param b Upper bound of integration
 * @return Number of points which was hit.
 */
int randomize_points(int N, float a, float b)
{
    int result = 0;
    for (int i = 0; i < N; ++i)
    {
        double rand_x = ((double)rand() / RAND_MAX) * (b - a) + a;
        double rand_y = ((double)rand() / RAND_MAX);
        double real_y = func(rand_x);

        if (rand_y <= real_y)
            result++;
    }
    return result;
}

/**
 * This function calculates approximation of integral from counters of hit and total points.
 * @param total_randomized_points Number of total randomized points.
 * @param hit_points Number of hit points.
 * @param a Lower bound of integration
 * @param b Upper bound of integration
 * @return The approximation of integral
 */
double summarize_calculations(uint64_t total_randomized_points, uint64_t hit_points, float a, float b)
{
    return (b - a) * ((double)hit_points / (double)total_randomized_points);
}

/**
 * This function locks mutex and can sometime die (it has 2% chance to die).
 * It cannot die if lock would return an error.
 * It doesn't handle any errors. It's users responsibility.
 * Use it only in STAGE 4.
 *
 * @param mtx Mutex to lock
 * @return Value returned from pthread_mutex_lock.
 */
int random_death_lock(pthread_mutex_t* mtx)
{
    int ret = pthread_mutex_lock(mtx);
    if (ret)
        return ret;

    // 2% chance to die
    if (rand() % 50 == 0)
        abort();
    return ret;
}

void usage(char* argv[])
{
    printf("%s a b N - calculating integral with multiple processes\n", argv[0]);
    printf("a - Start of segment for integral (default: -1)\n");
    printf("b - End of segment for integral (default: 1)\n");
    printf("N - Size of batch to calculate before reporting to shared memory (default: 1000)\n");
}

volatile sig_atomic_t received_signal = 0;

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void sig_handler(int sig)
{
    received_signal = 1;
}

typedef struct SharedState
{
    int process_counter;
    pthread_mutex_t mtx;
    int total_samples;
    int successful_hits;
    float a;
    float b;
}SharedState;


void calculate_batches(int N, float a, float b, SharedState* state)
{
    int i=0;
    for (;;)
    {
        int hit_points = randomize_points(N,a,b);
        if (random_death_lock(&state->mtx)==EOWNERDEAD)
        {
            pthread_mutex_consistent(&state->mtx);
            state->process_counter--;
        }
        state->total_samples += N;
        state->successful_hits += hit_points;
        int t_samples = state->total_samples;
        int s_hits = state->successful_hits;
        printf("After batch %d: total_samples=%d , successful_hits=%d\n", i, t_samples, s_hits);
        pthread_mutex_unlock(&state->mtx);
        i++;

        if (received_signal)
        {
            return;
        }
    }
}

int main(int argc, char* argv[])
{
    usage(argv);

    float a=-1;
    float b=1;
    int N=1000;
    if (argc==4)
    {
        a = atof(argv[1]);
        b = atof(argv[2]);
        N = atoi(argv[3]);
    }
    if (argc!=4 && argc!=1)
    {
        usage(argv);
    }

    sethandler(sig_handler, SIGINT);

    sem_t* semaphore = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (semaphore == SEM_FAILED) ERR("sem_open");
    sem_wait(semaphore);

    int first =1;
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd<0)
    {
        if (errno == EEXIST)
        {
            first = 0;
            shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
            if (shm_fd<0) ERR("shm_open");
        }
        else
        {
            ERR("shm_open");
        }
    }

    if (first)
    {
        ftruncate(shm_fd, sizeof(SharedState));
    }
    SharedState* state = (SharedState*)mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (state==MAP_FAILED)
    {
        ERR("mmap");
    }

    if (first)
    {
        pthread_mutexattr_t mutex_attr;
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(&state->mtx, &mutex_attr);
        pthread_mutexattr_destroy(&mutex_attr);

        state->process_counter = 1;
        state->successful_hits = 0;
        state->total_samples = 0;
        state->a = a;
        state->b = b;
        sem_post(semaphore);
    }
    else
    {
        sem_post(semaphore);
        if (state->a != a || state->b != b)
        {
            perror("Error: Integration bounds do not match the running computation.\n");
            munmap(state, sizeof(SharedState));
            sem_close(semaphore);
            exit(EXIT_FAILURE);
        }

        pthread_mutex_lock(&state->mtx);
        state->process_counter++;
        pthread_mutex_unlock(&state->mtx);
    }

    pthread_mutex_lock(&state->mtx);
    printf("Collaborating processes: %d\n", state->process_counter);
    pthread_mutex_unlock(&state->mtx);

    calculate_batches(N,a,b,state);

    sleep(2);
    pthread_mutex_lock(&state->mtx);
    state->process_counter--;
    int processes_remaining = state->process_counter;
    pthread_mutex_unlock(&state->mtx);
    sem_close(semaphore);

    if (processes_remaining == 0)
    {
        double result = summarize_calculations(state->total_samples, state->successful_hits, a, b);
        printf("FINAL APPROXIMATION: %f\n", result);

        pthread_mutex_destroy(&state->mtx);
        munmap(state, sizeof(SharedState));
        shm_unlink(SHM_NAME);
        sem_unlink(SEM_NAME);
    }
    else
    {
        munmap(state, sizeof(SharedState));
    }

    return EXIT_SUCCESS;
}