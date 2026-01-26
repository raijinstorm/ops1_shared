#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void ms_sleep(unsigned int milli)
{
    struct timespec ts = {milli / 1000, (milli % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

void usage(int argc, char* argv[])
{
    printf("%s N M\n", argv[0]);
    printf("\t10 <= N <= 20 - number of banner threads\n");
    printf("\t2 <= M <= 8 - number of artillery threads\n");
    exit(EXIT_FAILURE);
}

typedef struct Shared {
    sem_t gorge_sem;

    int enemy_hp;
    pthread_mutex_t hp_mutex;
    pthread_cond_t charge_cond;
    pthread_barrier_t artillery_barrier;
}Shared;

typedef struct ThreadArgs {
    int id;
    unsigned int seed;
    Shared *shared;
    pthread_t tid;
}ThreadArgs;

void readArgs(int argc, char* argv[], int* N, int* M) {
    if (argc!=3) {
        usage(argc, argv);
    }

    *N = atoi(argv[1]);
    *M = atoi(argv[2]);
    if (*N < 10 || *N > 20 || *M<2 || *M > 8) {
        usage(argc, argv);
    }
}

void* cavalry_work(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    if (sem_wait(&args->shared->gorge_sem)!=0) {
        ERR("sem_wait");
    }
    int travel_time = 80 + (rand_r(&args->seed) % 41);
    ms_sleep(travel_time);
    if (sem_post(&args->shared->gorge_sem)!=0) {
        ERR("sem_post");
    }
    printf("CAVALRY %d: IN POSITION\n", args->id);
    pthread_mutex_lock(&args->shared->hp_mutex);
    while (args->shared->enemy_hp>=50) {
        pthread_cond_wait(&args->shared->charge_cond, &args->shared->hp_mutex);
    }
    pthread_mutex_unlock(&args->shared->hp_mutex);

    printf("CAVALRY %d: READY TO CHARGE\n", args->id);

    return NULL;
}

void* artillery_work(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    Shared* shared = args->shared;

    while (1) {
        pthread_barrier_wait(&shared->artillery_barrier);

        pthread_mutex_lock(&shared->hp_mutex);
        shared->enemy_hp -= 1 + (rand_r(&args->seed) % 6);
        pthread_mutex_unlock(&shared->hp_mutex);

        int serial = pthread_barrier_wait(&shared->artillery_barrier);
        int stop = 0;
        pthread_mutex_lock(&shared->hp_mutex);

        if (shared->enemy_hp < 50) {
            stop = 1;
            if (serial == PTHREAD_BARRIER_SERIAL_THREAD) {
                printf("ARTILLERY: ENEMY HP %d\n", shared->enemy_hp);
                pthread_cond_broadcast(&shared->charge_cond);
            }
        } else {
            if (serial == PTHREAD_BARRIER_SERIAL_THREAD) {
                printf("ARTILLERY: ENEMY HP %d\n", shared->enemy_hp);
            }
        }
        pthread_mutex_unlock(&shared->hp_mutex);

        if (stop) break;
        ms_sleep(400);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    int N,M;
    readArgs(argc, argv, &N, &M);
    Shared state;
    state.enemy_hp = 100;
    if (sem_init(&state.gorge_sem, 0, 3) != 0) {
        ERR("sem_init");
    }
    if (pthread_mutex_init(&state.hp_mutex, NULL) != 0) {
        ERR("pthread_mutex_init");
    }
    if (pthread_cond_init(&state.charge_cond, NULL) != 0) {
        ERR("pthread_cond_init");
    }
    if (pthread_barrier_init(&state.artillery_barrier, NULL, M)!=0) {
        ERR("pthread_barrier_init");
    }

    ThreadArgs* cavalry_args = malloc(sizeof(ThreadArgs) * N);
    ThreadArgs* artillery_args = malloc(sizeof(ThreadArgs) * M);
    if (!cavalry_args || !artillery_args) ERR("malloc args");
    for (int i=0;i<N;i++) {
        cavalry_args[i].id = i;
        cavalry_args[i].seed = time(NULL);
        cavalry_args[i].shared = &state;

        if (pthread_create(&cavalry_args[i].tid, NULL, cavalry_work, &cavalry_args[i]) != 0)
            ERR("pthread_create");
    }

    for (int i=0;i<M;i++) {
        artillery_args[i].id = i;
        artillery_args[i].seed = time(NULL);
        artillery_args[i].shared = &state;

        if (pthread_create(&artillery_args[i].tid, NULL, artillery_work, &artillery_args[i]) != 0)
            ERR("pthread_create");
    }

    for (int i=0;i<N;i++) {
        if (pthread_join(cavalry_args[i].tid, NULL) != 0) {
            ERR("pthread_join");
        }
    }
    for (int i=0;i<M;i++) {
        if (pthread_join(artillery_args[i].tid, NULL) != 0) {
            ERR("pthread_join");
        }
    }
    free(cavalry_args);
    free(artillery_args);
    sem_destroy(&state.gorge_sem);
    pthread_mutex_destroy(&state.hp_mutex);
    pthread_cond_destroy(&state.charge_cond);
    pthread_barrier_destroy(&state.artillery_barrier);

    return EXIT_SUCCESS;
}