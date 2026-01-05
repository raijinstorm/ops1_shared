#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef struct Shared {
    pthread_mutex_t mutex;
    int year[4];
    int state[100];
}Shared;

typedef struct StudentArg {
    int id;
    Shared* shared;
    int current;
}StudentArg;

void readArgs(int argc, char** argv,int* studentNum) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s n   (n <= 100)\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int n = atoi(argv[1]);
    if (n<=0 || n>100) {
        fprintf(stderr, "n must be in 1..100\n");
        exit(EXIT_FAILURE);
    }
    *studentNum = n;
}

void ms_sleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0) {
        // if interrupted, continue sleeping remaining time
        continue;
    }
}

long long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) ERR("clock_gettime");
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void move_year(StudentArg* a, int from, int to) {
    int oldstate;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

    pthread_mutex_lock(&a->shared->mutex);
    a->shared->year[from]--;
    a->shared->year[to]++;
    a->current = to;
    pthread_mutex_unlock(&a->shared->mutex);

    pthread_setcancelstate(oldstate, NULL);
}

void student_cleanup(void* p) {
    StudentArg* a = (StudentArg*)p;

    pthread_mutex_lock(&a->shared->mutex);
    if (a->current>=0 && a->current<4) {
        a->shared->year[a->current]--;
    }
    a->shared->state[a->id] = 2;
    pthread_mutex_unlock(&a->shared->mutex);
}

void* student_thread(void* args) {
    StudentArg* a = (StudentArg*)args;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    a->current = 0;
    pthread_mutex_lock(&a->shared->mutex);
    a->shared->state[a->id] = 0;
    a->shared->year[a->current]++;
    pthread_mutex_unlock(&a->shared->mutex);

    pthread_cleanup_push(student_cleanup, a);
    ms_sleep(1000);
    pthread_testcancel();
    move_year(a, 0, 1);

    ms_sleep(1000);
    pthread_testcancel();
    move_year(a, 1, 2);

    ms_sleep(1000);
    pthread_testcancel();
    move_year(a, 2, 3);

    pthread_mutex_lock(&a->shared->mutex);
    a->shared->state[a->id] = 1;
    pthread_mutex_unlock(&a->shared->mutex);

    pthread_cleanup_pop(0);
    return NULL;
}

int main(int argc, char** argv) {
    int studentNum;
    readArgs(argc, argv, &studentNum);

    srand(time(NULL));
    Shared shared;
    if (pthread_mutex_init(&shared.mutex,NULL) != 0)
        ERR("pthread_mutex_init");
    for (int i=0;i<4;i++) {
        shared.year[i] = 0;
    }
    for (int i=0;i<100;i++) {
        shared.state[i] = 0;
    }

    pthread_t tids[100];
    StudentArg args[100];

    for (int i=0;i<studentNum;i++) {
        args[i].id = i;
        args[i].shared = &shared;
        args[i].current = -1;
        pthread_create(&tids[i], NULL, student_thread, &args[i]);
    }

    long long end = now_ms()+4000LL;
    while (now_ms()<end) {
        ms_sleep(100+(rand()%201));
        int index = rand()%studentNum;

        pthread_mutex_lock(&shared.mutex);
        int state = shared.state[index];
        pthread_mutex_unlock(&shared.mutex);

        if (state == 0) {
            pthread_cancel(tids[index]);
        }
    }

    for (int i=0;i<studentNum;i++) {
        pthread_join(tids[i], NULL);
    }
    pthread_mutex_lock(&shared.mutex);
    printf("Year 1: %d\nYear 2: %d\nYear 3: %d\nBSc  : %d\n", shared.year[0], shared.year[1], shared.year[2], shared.year[3]);
    pthread_mutex_unlock(&shared.mutex);

    pthread_mutex_destroy(&shared.mutex);
    return 0;
}
