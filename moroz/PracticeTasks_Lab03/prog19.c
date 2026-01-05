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
    int stop;
    int* array;
    int len;
}Shared;

typedef struct SigArgs {
    Shared* shared;
    sigset_t set;
    unsigned int seed;
}SigArgs;

void readArgs(int argc, char** argv, int* len) {
    if (argc!=2) {
        fprintf(stderr, "Usage: %s k\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int a = atoi(argv[1]);
    if (a<=0) {
        fprintf(stderr, "k must be positive");
        exit(EXIT_FAILURE);
    }
    *len = a;
}

void remove_entry(Shared* shared, unsigned int* set) {
    pthread_mutex_lock(&shared->mutex);
    if (shared->len>0) {
        int rnd_index = rand_r(set)%shared->len;
        for (int i=rnd_index;i<shared->len-1;i++) {
            shared->array[i] = shared->array[i+1];
        }
        shared->len--;
    }
    pthread_mutex_unlock(&shared->mutex);
}

void print_list(Shared* shared) {
    pthread_mutex_lock(&shared->mutex);
    if (shared->len<=0) {
        printf("list is empty\n");
        pthread_mutex_unlock(&shared->mutex);
        return;
    }
    for (int i=0;i<shared->len;i++) {
        printf("%d ", shared->array[i]);
    }
    printf("\n");
    pthread_mutex_unlock(&shared->mutex);
}

void* sig_thread(void* arg) {
    SigArgs* args = (SigArgs*)arg;

    while (1) {
        int sig;
        if (sigwait(&args->set, &sig)) {
            ERR("sigwait");
        }
        if (sig==SIGINT) {
            remove_entry(args->shared, &args->seed);
        }
        else if (sig == SIGQUIT) {
            pthread_mutex_lock(&args->shared->mutex);
            args->shared->stop = 1;
            pthread_mutex_unlock(&args->shared->mutex);
            break;
        }
    }
    return NULL;
}

int main(int argc, char** argv) {
    int length;
    readArgs(argc, argv, &length);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGQUIT);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL)) {
        ERR("pthread_sigmask");
    }

    Shared shared;

    shared.array = (int*)malloc(length*sizeof(int));
    if (!shared.array) {
        ERR("malloc");
    }
    shared.len = length;
    if (pthread_mutex_init(&shared.mutex, NULL)!=0) {
        ERR("pthread_mutex_init");
    }
    for (int i=0;i<length;i++) {
        shared.array[i] = i+1;
    }

    SigArgs args;
    pthread_t tid;
    args.shared = &shared;
    args.set = set;
    srand(time(NULL));
    args.seed = rand();

    if (pthread_create(&tid,NULL,sig_thread,&args)) {
        ERR("pthread_create");
    }

    while (1) {
        pthread_mutex_lock(&shared.mutex);
        int st = shared.stop;
        pthread_mutex_unlock(&shared.mutex);
        if (st) {
            break;
        }
        print_list(&shared);
        sleep(1);
    }

    if (pthread_join(tid,NULL)) {
        ERR("pthread_join");
    }
    free(shared.array);
    pthread_mutex_destroy(&shared.mutex);
    return 0;
}
