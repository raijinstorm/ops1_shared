#include "common.h"

typedef struct Shared {
    pthread_mutex_t state_mutex;
    pthread_cond_t ship_present;
    pthread_mutex_t ship_mutex;

    int packages_left;
    int next_package;
    int terminate;
}Shared;

typedef struct Worker {
    Shared* shared;
    pthread_t tid;
    int id;
}Worker;

typedef struct SigArgs {
    Shared* shared;
    sigset_t mask;
}SigArgs;

void readArgs(int argc, char* argv[], int* workersCount, int* carriagesCount) {
    if (argc!=3) {
        usage(argc, argv);
    }

    int p = atoi(argv[1]);
    if (p<1 || p>10) {
        usage(argc, argv);
    }
    int q = atoi(argv[2]);
    if (q<1 || q>10) {
        usage(argc, argv);
    }

    *workersCount = p;
    *carriagesCount = q;
}

void dock_ship(Shared* shared) {
    pthread_mutex_lock(&shared->state_mutex);
    if (shared->terminate) {
        pthread_mutex_unlock(&shared->state_mutex);
        return;
    }

    if (shared->packages_left>0) {
        pthread_mutex_unlock(&shared->state_mutex);
        printf("Ship cannot dock now\n");
        return;
    }

    shared->packages_left = (rand()%31) + 30;
    if (pthread_cond_broadcast(&shared->ship_present)) {
        ERR("pthread_cond_broadcast");
    }
    pthread_mutex_unlock(&shared->state_mutex);
}

void try_terminate(Shared* shared) {
    pthread_mutex_lock(&shared->state_mutex);
    shared->terminate = 1;
    if (pthread_cond_broadcast(&shared->ship_present)) {
        ERR("pthread_cond_broadcast");
    }
    pthread_mutex_unlock(&shared->state_mutex);
}

void* sig_thread(void* args) {
    SigArgs* arg = (SigArgs*)args;
    int sig;

    while (1) {
        if (sigwait(&arg->mask, &sig)) {
            ERR("sigwait");
        }
        if (sig == SIGUSR1) {
            dock_ship(arg->shared);
        }
        else if (sig == SIGINT) {
            try_terminate(arg->shared);
            break;
        }
    }

    return NULL;
}

void* work(void* args) {
    Worker* worker = (Worker*)args;
    Shared* shared = worker->shared;
    for (;;) {
        if (pthread_mutex_lock(&worker->shared->state_mutex)) {
            ERR("pthread_mutex_lock");
        }
        while (!worker->shared->terminate && worker->shared->packages_left==0) {
            if (pthread_cond_wait(&worker->shared->ship_present, &worker->shared->state_mutex)) {
                ERR("pthread_cond_wait");
            }
        }

        if (shared->terminate) {
            pthread_mutex_unlock(&worker->shared->state_mutex);
            break;
        }
        pthread_mutex_unlock(&worker->shared->state_mutex);


        if (pthread_mutex_lock(&worker->shared->ship_mutex)) {
            ERR("pthread_mutex_lock");
        }
        msleep(5);

        if (pthread_mutex_lock(&worker->shared->state_mutex)) {
            ERR("pthread_mutex_lock");
        }

        if (shared->terminate) {
            pthread_mutex_unlock(&worker->shared->state_mutex);
            pthread_mutex_unlock(&worker->shared->ship_mutex);
            break;
        }
        if (shared->packages_left<=0) {
            pthread_mutex_unlock(&worker->shared->state_mutex);
            pthread_mutex_unlock(&worker->shared->ship_mutex);
            continue;
        }

        int pack;
        shared->packages_left--;
        pack = worker->shared->next_package++;
        pthread_mutex_unlock(&worker->shared->state_mutex);
        pthread_mutex_unlock(&worker->shared->ship_mutex);
        msleep(10);
        printf("Worker %d: I transported %d package.\n", worker->id, pack);
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL)) {
        ERR("pthread_sigmask");
    }

    srand(time(NULL));
    int workersCount, carriagesCount;
    readArgs(argc, argv, &workersCount, &carriagesCount);
    Shared shared;
    shared.next_package = 1;
    shared.packages_left = 0;
    shared.terminate = 0;
    if (pthread_mutex_init(&shared.state_mutex, NULL)) {
        ERR("pthread_mutex_init");
    }
    if (pthread_mutex_init(&shared.ship_mutex, NULL)) {
        ERR("pthread_mutex_init");
    }
    if (pthread_cond_init(&shared.ship_present, NULL)) {
        ERR("pthread_cond_init");
    }
    Worker* workers = malloc(sizeof(Worker)*workersCount);
    if (!workers) {
        ERR("malloc");
    }
    for (int i=0;i<workersCount;i++) {
        workers[i].shared = &shared;
        workers[i].id = i+1;
        pthread_create(&workers[i].tid, NULL, work, &workers[i]);
    }

    SigArgs sigArgs;
    sigArgs.shared = &shared;
    sigArgs.mask = set;
    pthread_t sigTid;
    pthread_create(&sigTid, NULL, sig_thread, &sigArgs);

    pthread_join(sigTid, NULL);

    for (int i=0;i<workersCount;i++) {
        pthread_join(workers[i].tid, NULL);
    }

    free(workers);
    if (pthread_mutex_destroy(&shared.state_mutex)!=0) {
        ERR("pthread_mutex_destroy");
    }
    if (pthread_mutex_destroy(&shared.ship_mutex)!=0) {
        ERR("pthread_mutex_destroy");
    }
    if (pthread_cond_destroy(&shared.ship_present)!=0) {
        ERR("pthread_mutex_destroy");
    }
    return 0;
}
