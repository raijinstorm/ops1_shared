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

typedef struct Worker {
    int id;
    pthread_t tid;
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

    return NULL;
}

int main(int argc, char* argv[]) {
    int employeesNum, productsNum;
    readArgs(argc, argv, &productsNum,&employeesNum);

    // int* shelves = calloc(sizeof(int), productsNum);
    // pthread_mutex_t* shelves_mutex = calloc(sizeof(pthread_mutex_t), productsNum);

    Worker* workers = malloc(sizeof(Worker)*employeesNum);
    if (workers==NULL) {
        ERR("malloc");
    }
    for (int i=0;i<employeesNum;i++) {
        workers[i].id = i;
        if (pthread_create(&workers[i].tid, NULL, work, &workers[i])) {
            ERR("pthread_create");
        }
    }

    for (int i=0;i<employeesNum;i++) {
        if (pthread_join(workers[i].tid, NULL)) {
            ERR("pthread_join");
        }
    }
    free(workers);
}