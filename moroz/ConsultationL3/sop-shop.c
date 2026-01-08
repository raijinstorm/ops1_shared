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
}Shared;

typedef struct Worker {
    Shared* shared;
    int id;
    pthread_t tid;
    unsigned int seed;
    int productsNum;
}Worker;

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

void* work(void* args) {
    Worker* worker = (Worker*) args;
    printf("Worker %d: Reporting for the night shift!\n", worker->id);

    for (int i=0;i<10;i++) {
        int product1 = (rand_r(&worker->seed))%worker->productsNum;
        int product2 = (rand_r(&worker->seed))%worker->productsNum;
        while (product1 == product2) {
            product1 = (rand_r(&worker->seed))%worker->productsNum;
        }
        if (product1>product2) {
            SWAP(product1, product2);
        }

        pthread_mutex_lock(&worker->shared->shelf_mutexes[product1]);
        pthread_mutex_lock(&worker->shared->shelf_mutexes[product2]);
        if (worker->shared->shelves[product1] > worker->shared->shelves[product2]) {
            SWAP(worker->shared->shelves[product1], worker->shared->shelves[product2]);
            ms_sleep(50);
        }

        pthread_mutex_unlock(&worker->shared->shelf_mutexes[product1]);
        pthread_mutex_unlock(&worker->shared->shelf_mutexes[product2]);
    }
    ms_sleep(100);

    return NULL;
}

int main(int argc, char* argv[]) {
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
    shuffle(shelves, productsNum);

    Worker* workers = malloc(sizeof(Worker)*employeesNum);
    if (workers==NULL) {
        ERR("malloc");
    }
    print_shop(shelves, productsNum);
    for (int i=0;i<employeesNum;i++) {
        workers[i].shared = &shared;
        workers[i].id = i;
        workers[i].productsNum = productsNum;
        workers[i].seed = rand();
        if (pthread_create(&workers[i].tid, NULL, work, &workers[i])) {
            ERR("pthread_create");
        }
    }

    for (int i=0;i<employeesNum;i++) {
        if (pthread_join(workers[i].tid, NULL)) {
            ERR("pthread_join");
        }
    }
    print_shop(shelves, productsNum);

    for (int i=0;i<productsNum;i++) {
        if (pthread_mutex_destroy(&shelf_mutexes[i])) {
            ERR("pthread_mutex_destroy");
        }
    }
    free(shelves);
    free(workers);
}