#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define SWAP(x, y)         \
    do                     \
    {                      \
        typeof(x) __x = x; \
        typeof(y) __y = y; \
        x = __y;           \
        y = __x;           \
    } while (0)

void usage(int argc, char* argv[])
{
    printf("%s N M\n", argv[0]);
    printf("\t8 <= N <= 256 - number of products\n");
    printf("\t1 <= M <= 16 - number of workers\n");
    exit(EXIT_FAILURE);
}

void shuffle(int* shop, int n)
{
    for (int i = n - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        SWAP(shop[i], shop[j]);
    }
}

void print_shop(int* shop, int n)
{
    for (int i = 0; i < n; i++)
        printf("%3d ", shop[i]);
    printf("\n");
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

typedef struct stop_flag
{
    bool stop;
    pthread_mutex_t mx;
} stop_flag_t;

typedef struct worker_args
{
    int index;
    int n;
    int* shop;
    pthread_mutex_t* mxShop;      // per-product mutexes
    pthread_mutex_t* mxWholeShop; // full-array lock (for SIGALRM/SIGUSR1 coherence)
    unsigned int seed;
    stop_flag_t* stopFlag;
} worker_args_t;

typedef struct signal_args
{
    stop_flag_t* stopFlag;
    sigset_t* mask;

    int* shop;
    int n;
    pthread_mutex_t* mxWholeShop;

    pthread_t* workers;
    bool* removed;
    int m;
    int present;
    unsigned int seed;
} signal_args_t;

void cleanup_mutex_unlock(void* arg)
{
    pthread_mutex_t* mx = arg;
    int err = pthread_mutex_unlock(mx);
    if (err)
    {
        errno = err;
        ERR("pthread_mutex_unlock");
    }
}

void* worker_func(void* arg)
{
    worker_args_t* a = arg;
    printf("Worker %d: Reporting for the night shift!\n", a->index);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (1)
    {
        pthread_mutex_lock(&a->stopFlag->mx);
        if (a->stopFlag->stop)
        {
            pthread_mutex_unlock(&a->stopFlag->mx);
            break;
        }
        pthread_mutex_unlock(&a->stopFlag->mx);

        int i = rand_r(&a->seed) % a->n;
        int j;
        do
        {
            j = rand_r(&a->seed) % a->n;
        } while (j == i);

        ms_sleep(100);

        int first = i < j ? i : j;
        int second = i < j ? j : i;

        pthread_mutex_lock(a->mxWholeShop);
        pthread_cleanup_push(cleanup_mutex_unlock, a->mxWholeShop);

        pthread_mutex_lock(&a->mxShop[first]);
        pthread_cleanup_push(cleanup_mutex_unlock, &a->mxShop[first]);

        pthread_mutex_lock(&a->mxShop[second]);
        pthread_cleanup_push(cleanup_mutex_unlock, &a->mxShop[second]);

        if (a->shop[first] > a->shop[second])
        {
            SWAP(a->shop[first], a->shop[second]);
            ms_sleep(50);
        }

        pthread_cleanup_pop(1); // unlock mxShop[second]
        pthread_cleanup_pop(1); // unlock mxShop[first]
        pthread_cleanup_pop(1); // unlock mxWholeShop
    }
    return NULL;
}

void* signal_thread(void* arg)
{
    signal_args_t* a = arg;
    int sig;

    alarm(1);

    while (1)
    {
        if (sigwait(a->mask, &sig))
            ERR("sigwait");

        switch (sig)
        {
            case SIGINT:
                pthread_mutex_lock(&a->stopFlag->mx);
                a->stopFlag->stop = true;
                pthread_mutex_unlock(&a->stopFlag->mx);
                return NULL;

            case SIGALRM:
                pthread_mutex_lock(a->mxWholeShop);
                print_shop(a->shop, a->n);
                pthread_mutex_unlock(a->mxWholeShop);
                alarm(1);
                break;

            case SIGUSR1:
                pthread_mutex_lock(a->mxWholeShop);
                shuffle(a->shop, a->n);
                pthread_mutex_unlock(a->mxWholeShop);
                break;

            case SIGUSR2:
                if (a->present == 0)
                    break;

                int idx;
                do
                {
                    idx = rand_r(&a->seed) % a->m;
                } while (a->removed[idx]);

                if (pthread_cancel(a->workers[idx]))
                    ERR("pthread_cancel");

                a->removed[idx] = true;
                a->present--;

                if (a->present == 0)
                {
                    pthread_mutex_lock(&a->stopFlag->mx);
                    a->stopFlag->stop = true;
                    pthread_mutex_unlock(&a->stopFlag->mx);
                    return NULL;
                }
                break;
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc != 3)
        usage(argc, argv);

    int N = atoi(argv[1]);
    int M = atoi(argv[2]);
    if (N < 8 || N > 256 || M < 1 || M > 16)
        usage(argc, argv);

    srand(time(NULL));

    int* shop = malloc(sizeof(int) * N);
    pthread_mutex_t* mxShop = malloc(sizeof(pthread_mutex_t) * N);
    if (!shop || !mxShop)
        ERR("malloc");

    for (int i = 0; i < N; i++)
    {
        shop[i] = i + 1;
        if (pthread_mutex_init(&mxShop[i], NULL))
            ERR("pthread_mutex_init");
    }
    shuffle(shop, N);

    pthread_mutex_t mxWholeShop = PTHREAD_MUTEX_INITIALIZER;

    stop_flag_t stopFlag = {
        .stop = false,
        .mx = PTHREAD_MUTEX_INITIALIZER
    };

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGALRM);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL))
        ERR("pthread_sigmask");

    pthread_t* workers = malloc(sizeof(pthread_t) * M);
    worker_args_t* wargs = malloc(sizeof(worker_args_t) * M);
    bool* removed = malloc(sizeof(bool) * M);
    if (!workers || !wargs || !removed)
        ERR("malloc");

    for (int i = 0; i < M; i++)
        removed[i] = false;

    for (int i = 0; i < M; i++)
    {
        wargs[i].index = i;
        wargs[i].n = N;
        wargs[i].shop = shop;
        wargs[i].mxShop = mxShop;
        wargs[i].mxWholeShop = &mxWholeShop;
        wargs[i].seed = rand();
        wargs[i].stopFlag = &stopFlag;

        if (pthread_create(&workers[i], NULL, worker_func, &wargs[i]))
            ERR("pthread_create");
    }

    pthread_t sig_tid;
    signal_args_t sargs = {
        .stopFlag = &stopFlag,
        .mask = &mask,
        .shop = shop,
        .n = N,
        .mxWholeShop = &mxWholeShop,
        .workers = workers,
        .removed = removed,
        .m = M,
        .present = M,
        .seed = rand()
    };

    if (pthread_create(&sig_tid, NULL, signal_thread, &sargs))
        ERR("pthread_create (signal)");

    for (int i = 0; i < M; i++)
        if (pthread_join(workers[i], NULL))
            ERR("pthread_join");

    if (pthread_join(sig_tid, NULL))
        ERR("pthread_join (signal)");

    for (int i = 0; i < N; i++)
        pthread_mutex_destroy(&mxShop[i]);
    pthread_mutex_destroy(&mxWholeShop);
    pthread_mutex_destroy(&stopFlag.mx);

    free(removed);
    free(wargs);
    free(workers);
    free(mxShop);
    free(shop);

    return EXIT_SUCCESS;
}
