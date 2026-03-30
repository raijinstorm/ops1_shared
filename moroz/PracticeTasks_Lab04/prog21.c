#define _GNU_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef struct Shared {
    sem_t semaphore;
}Shared;

typedef struct Timer {
    int duration;
    Shared *shared;
}Timer;

void* worker(void* arg) {
    Timer* t = (Timer*)arg;

    int sec = t->duration;
    Shared* s = t->shared;
    free(t);
    sleep(sec);
    printf("\n[Thread] Wake up! (%d seconds elapsed)\n", sec);
    sem_post(&s->semaphore);
    return NULL;
}

int main(int argc, char* argv[]) {
    Shared state;
    int seconds;

    if (sem_init(&state.semaphore, 0, 5)!=0) {
        ERR("sem_init");
    }

    printf("Enter seconds to set a timer.\n");
    while (1) {
        if (scanf("%d", &seconds) != 1) {
            while (getchar() != '\n');
            continue;
        }
        if (sem_trywait(&state.semaphore)!=0) {
            printf("Only 5 alarms can be set at the time\n");
            continue;
        }
        pthread_t tid;
        Timer* timer = malloc(sizeof(Timer));
        if (timer == NULL) {
            sem_post(&state.semaphore);
            fprintf(stderr, "timer allocation failed\n");
            continue;
        }
        timer->duration = seconds;
        timer->shared = &state;
        if (pthread_create(&tid,NULL,worker,timer)!=0) {
            fprintf(stderr, "pthread_create failed\n");
            sem_post(&state.semaphore);
            free(timer);
            continue;
        }
        pthread_detach(tid);
        printf("Timer set for %d s.\n", timer->duration);
    }

    sem_destroy(&state.semaphore);
    return 0;
}