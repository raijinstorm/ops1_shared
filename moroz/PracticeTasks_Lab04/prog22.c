#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
#define BUFFERSIZE 256
#define READCHUNKS 4
#define THREAD_NUM 3

volatile sig_atomic_t work = 1;

typedef struct Worker {
    int id;
    int* idlethreads;
    int* pending_work;
    pthread_cond_t* cond;
    pthread_mutex_t* mutex;
}Worker;

void sigintHandler(int sig) {
    work = 0;
}

void set_handler(void (*f)(int), int sigNo) {
    struct sigaction act;
    memset(&act, 0x00, sizeof(struct sigaction));
    act.sa_handler = f;
    // IMPORTANT: No SA_RESTART. This ensures fgets() in main()
    // wakes up immediately when Ctrl+C is pressed.
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

ssize_t bulk_read(int fd, char *buf, size_t count) {
    int c;
    size_t len = 0;
    do {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0) return c;
        if (c == 0) return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count) {
    int c;
    size_t len = 0;
    do {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0) return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void cleanup(void *arg) {
    pthread_mutex_unlock((pthread_mutex_t *)arg);
}

void read_random(int thread_id) {
    char filename[64];
    char buffer[BUFFERSIZE];
    int local_count = 0;

    snprintf(filename, sizeof(filename), "random_t%d_%d.bin", thread_id, local_count++);
    printf("[Thread %d] Writing to %s\n", thread_id, filename);

    int out, in;
    ssize_t count;
    if ((out = TEMP_FAILURE_RETRY(open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644)))<0){
        ERR("open out");
    }
    if ((in = TEMP_FAILURE_RETRY(open("/dev/urandom",O_RDONLY)))<0){
        ERR("open urandom");
    }

    for (int i = 0; i < READCHUNKS; i++) {
        if ((count = bulk_read(in, buffer, BUFFERSIZE)) < 0) ERR("bulk_read");
        if ((count = bulk_write(out, buffer, count)) < 0) ERR("bulk_write");
        sleep(1); // Simulate work
    }

    if (TEMP_FAILURE_RETRY(close(in))) ERR("close in");
    if (TEMP_FAILURE_RETRY(close(out))) ERR("close out");
}

void* work_func(void* arg) {
    Worker args;
    memcpy(&args, arg, sizeof(args));

    while (1) {
        if (pthread_mutex_lock(args.mutex)!=0) {
            ERR("pthread_mutex_lock");
        }
        pthread_cleanup_push(cleanup, args.mutex);

        (*args.idlethreads)++;
        while (*args.pending_work==0 && work) {
            if (pthread_cond_wait(args.cond, args.mutex)!=0) {
                ERR("pthread_cond_wait");
            }
        }
        (*args.idlethreads)--;

        if (!work && *args.pending_work==0) {
            pthread_mutex_unlock(args.mutex);
            pthread_exit(0);
        }
        (*args.pending_work)--;
        pthread_cleanup_pop(1);

        read_random(args.id);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int i=0;
    int pending_work = 0;
    int idlethreads = 0;

    pthread_t threads[THREAD_NUM];
    Worker args[THREAD_NUM];
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    char buffer[BUFFERSIZE];

    set_handler(sigintHandler, SIGINT);
    for (int i=0;i<THREAD_NUM;i++) {
        args[i].id = i+1;
        args[i].cond = &cond;
        args[i].mutex = &mutex;
        args[i].idlethreads = &idlethreads;
        args[i].pending_work = &pending_work;
        if (pthread_create(&threads[i], NULL, work_func, &args[i])) {
            ERR("pthread_create");
        }
    }

    printf("Program ready. Press [Enter] to start task. [Ctrl+C] to stop.\n");

    while (work) {
        if (fgets(buffer, BUFFERSIZE, stdin)!=NULL) {
            if (pthread_mutex_lock(&mutex)!=0) {
                ERR("pthread_mutex_lock");
            }

            if (idlethreads==0) {
                printf("All threads busy! Request dropped.\n");
            }
            else {
                pending_work++;
                pthread_cond_signal(&cond);
            }
            if (pthread_mutex_unlock(&mutex)!=0) {
                ERR("pthread_mutex_unlock");
            }
        }
        else {
            if (errno == EINTR) continue;
            ERR("fgets");
        }
    }

    printf("\nShutting down... finishing pending tasks.\n");
    if (pthread_cond_broadcast(&cond)!=0) {
        ERR("pthread_cond_broadcast");
    }
    for (i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(threads[i], NULL) != 0) ERR("join");
    }
    return 0;
}