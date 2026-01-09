#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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

typedef struct Shared {
    int* shelves;
    pthread_mutex_t* shelf_mutexes;
    int flag;
    pthread_mutex_t flag_mutex;
    int productsNum;
}Shared;

typedef struct Worker {
    Shared* shared;
    int id;
    pthread_t tid;
    unsigned int seed;
}Worker;

typedef struct SigArgs {
    sigset_t mask;
    unsigned int sig_seed;
    Worker* workers;
    int workersNum;
}SigArgs;

void readArgs(int argc, char* argv[], int* p, int*e) {
    if (argc!=3) {
        usage(argc, argv);
    }
    int a = atoi(argv[1]);
    if (a<8 || a>256) {
        usage(argc, argv);
    }
    int b = atoi(argv[2]);
    if (b<1 || b>16) {
        usage(argc, argv);
    }
    *p = a;
    *e = b;
}

void cleanup(void* args) {
    pthread_mutex_t* mutex_arg = (pthread_mutex_t*)args;
    pthread_mutex_lock(mutex_arg);
}

void* work(void* args) {
    Worker* worker = (Worker*) args;
    printf("Worker %d: Reporting for the night shift!\n", worker->id);

    for (;;) {
        pthread_mutex_lock(&worker->shared->flag_mutex);
        if (worker->shared->flag) {
            pthread_mutex_unlock(&worker->shared->flag_mutex);
            break;
        }
        pthread_mutex_unlock(&worker->shared->flag_mutex);
        int product1 = (rand_r(&worker->seed))%worker->shared->productsNum;
        int product2 = (rand_r(&worker->seed))%worker->shared->productsNum;
        while (product1 == product2) {
            product1 = (rand_r(&worker->seed))%worker->shared->productsNum;
        }
        ms_sleep(100);
        if (product1>product2) {
            SWAP(product1, product2);
        }

        pthread_mutex_lock(&worker->shared->shelf_mutexes[product1]);
        pthread_cleanup_push(cleanup, &worker->shared->shelf_mutexes[product1]);
        pthread_mutex_lock(&worker->shared->shelf_mutexes[product2]);
        pthread_cleanup_push(cleanup, &worker->shared->shelf_mutexes[product2]);
        if (worker->shared->shelves[product1] > worker->shared->shelves[product2]) {
            SWAP(worker->shared->shelves[product1], worker->shared->shelves[product2]);
            ms_sleep(50);
        }

        pthread_mutex_unlock(&worker->shared->shelf_mutexes[product1]);
        pthread_mutex_unlock(&worker->shared->shelf_mutexes[product2]);
        pthread_cleanup_pop(0);
        pthread_cleanup_pop(0);
    }

    return NULL;
}

void* sig_handler(void* args) {
    SigArgs* sig_args = (SigArgs*) args;
    int active_workers = sig_args->workersNum;
    int* active_indices = malloc(sizeof(int)*sig_args->workersNum);
    if (!active_indices) {
        ERR("malloc");
    }
    for (int i = 0;i<active_workers;i++) {
        active_indices[i] = i;
    }

    while (1) {
        int sig;
        if (sigwait(&sig_args->mask,&sig)) {
            ERR("sigwait");
        }
        if (sig == SIGINT) {
            pthread_mutex_lock(&sig_args->workers->shared->flag_mutex);
            sig_args->workers->shared->flag = 1;
            pthread_mutex_unlock(&sig_args->workers->shared->flag_mutex);
            break;
        }
        if (sig == SIGALRM) {
            for (int i=0;i<sig_args->workers->shared->productsNum;i++) {
                pthread_mutex_lock(&sig_args->workers->shared->shelf_mutexes[i]);
            }
            print_shop(sig_args->workers->shared->shelves, sig_args->workers->shared->productsNum);
            for (int i=0;i<sig_args->workers->shared->productsNum;i++) {
                pthread_mutex_unlock(&sig_args->workers->shared->shelf_mutexes[i]);
            }
            alarm(1);
        }
        else if (sig == SIGUSR1) {
            for (int i=0;i<sig_args->workers->shared->productsNum;i++) {
                pthread_mutex_lock(&sig_args->workers->shared->shelf_mutexes[i]);
            }
            shuffle(sig_args->workers->shared->shelves, sig_args->workers->shared->productsNum);
            for (int i=0;i<sig_args->workers->shared->productsNum;i++) {
                pthread_mutex_unlock(&sig_args->workers->shared->shelf_mutexes[i]);
            }
        }
        else if (sig == SIGUSR2) {
            if (active_workers>0) {
                int id = rand_r(&sig_args->sig_seed)%(active_workers);
                int empIdx = active_indices[id];

                printf("Cancelling worker %d (SIGUSR2)\n", empIdx);
                pthread_cancel(sig_args->workers[empIdx].tid);
                
                active_indices[id] = active_indices[active_workers-1];
                active_workers--;
                if (active_workers==0) {
                    pthread_mutex_lock(&sig_args->workers->shared->flag_mutex);
                    sig_args->workers->shared->flag = 1;
                    pthread_mutex_unlock(&sig_args->workers->shared->flag_mutex);
                    break;
                }
            }
        }
    }

    free(active_indices);
    return NULL;
}

int main(int argc, char* argv[]) {
    sigset_t newMask;
    sigemptyset(&newMask);
    sigaddset(&newMask, SIGINT);
    sigaddset(&newMask, SIGALRM);
    sigaddset(&newMask, SIGUSR1);
    sigaddset(&newMask, SIGUSR2);
    if (pthread_sigmask(SIG_BLOCK, &newMask, NULL))
        ERR("SIG_BLOCK error");

    int employeesNum, productsNum;
    readArgs(argc, argv, &productsNum,&employeesNum);
    srand(time(NULL));

    int* shelves = malloc(productsNum * sizeof(int));
    if (!shelves) {
        ERR("malloc");
    }
    pthread_mutex_t* shelf_mutexes = malloc(productsNum * sizeof(pthread_mutex_t));
    if (!shelf_mutexes) {
        ERR("malloc");
    }
    for (int i=0;i<productsNum;i++) {
        shelves[i] = i+1;
        if (pthread_mutex_init(&shelf_mutexes[i], NULL)) {
            ERR("pthread_mutex_init");
        }
    }
    Shared shared;
    shared.shelves = shelves;
    shared.shelf_mutexes = shelf_mutexes;
    shared.flag = 0;
    pthread_mutex_init(&shared.flag_mutex, NULL);
    shared.productsNum = productsNum;
    shuffle(shelves, productsNum);

    Worker* workers = malloc(sizeof(Worker)*employeesNum);
    if (workers==NULL) {
        ERR("malloc");
    }
    print_shop(shelves, productsNum);
    for (int i=0;i<employeesNum;i++) {
        workers[i].shared = &shared;
        workers[i].id = i;
        workers[i].seed = rand();
        if (pthread_create(&workers[i].tid, NULL, work, &workers[i])) {
            ERR("pthread_create");
        }
    }

    pthread_t sigTid;
    SigArgs sig_args;
    sig_args.workers = workers;
    sig_args.sig_seed = rand();
    sig_args.mask = newMask;
    sig_args.workersNum = employeesNum;
    if (pthread_create(&sigTid, NULL, sig_handler, &sig_args)) {
        ERR("pthread_create");
    }

    for (int i=0;i<employeesNum;i++) {
        if (pthread_join(workers[i].tid, NULL)) {
            ERR("pthread_join");
        }
    }
    if (pthread_join(sigTid, NULL)) {
        ERR("pthread_join");
    }
    print_shop(shelves, productsNum);

    for (int i=0;i<productsNum;i++) {
        if (pthread_mutex_destroy(&shelf_mutexes[i])) {
            ERR("pthread_mutex_destroy");
        }
    }
    if (pthread_mutex_destroy(&shared.flag_mutex)) {
        ERR("pthread_mutex_destroy");
    }
    free(shelves);
    free(shelf_mutexes);
    free(workers);
}