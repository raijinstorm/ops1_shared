#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>

#define MAXJOBS 10
#define WORKERNUM 4
#define JOBNUM 20

// good source on circular buffers:  https://www.ewskills.com/embedded-c/circular-buffer

typedef struct Job
{
    int packet_id;
    int processing_time_ms;
}Job;

typedef struct CircularBuffer
{
    Job jobs[MAXJOBS];
    int count;
    int head;
    int tail;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
}CircularBuffer;

typedef struct ThreadArgs
{
    pthread_t thread_id;
    CircularBuffer* circular_buffer;
    int* work;
}ThreadArgs;

void* thread_work(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    while (1)
    {
        pthread_mutex_lock(&args->circular_buffer->mtx);
        while (args->circular_buffer->count == 0 && *args->work)
        {
            pthread_cond_wait(&args->circular_buffer->not_empty, &args->circular_buffer->mtx);
        }
        if (*(args->work)==0 && args->circular_buffer->count == 0)
        {
            pthread_mutex_unlock(&args->circular_buffer->mtx);
            break;
        }
        Job current_job = args->circular_buffer->jobs[args->circular_buffer->head];
        args->circular_buffer->head = (args->circular_buffer->head + 1)%MAXJOBS;
        args->circular_buffer->count--;
        pthread_cond_signal(&args->circular_buffer->not_full);
        pthread_mutex_unlock(&args->circular_buffer->mtx);

        printf("Thread [%lu] processing packet [%d]...\n", args->thread_id, current_job.packet_id);
        usleep(current_job.processing_time_ms*1000);
    }
    return NULL;
}

int main(int argc, char** argv)
{
    srand(time(NULL));
    CircularBuffer circular_buffer;
    circular_buffer.count = 0;
    circular_buffer.head = 0;
    circular_buffer.tail = 0;
    pthread_mutex_init(&circular_buffer.mtx, NULL);
    pthread_cond_init(&circular_buffer.not_empty, NULL);
    pthread_cond_init(&circular_buffer.not_full, NULL);

    ThreadArgs thread_args[WORKERNUM];

    int work=1;
    for (int i=0;i<WORKERNUM;i++)
    {
        thread_args[i].thread_id = i;
        thread_args[i].circular_buffer = &circular_buffer;
        thread_args[i].work = &work;
        pthread_create(&thread_args[i].thread_id, NULL, thread_work, &thread_args[i]);
    }

    Job generated_jobs[JOBNUM];
    for (int i=0;i<JOBNUM;i++)
    {
        generated_jobs[i].packet_id = i;
        generated_jobs[i].processing_time_ms = rand()%(800-100) + 100;

        pthread_mutex_lock(&circular_buffer.mtx);
        while (circular_buffer.count == MAXJOBS)
        {
            pthread_cond_wait(&circular_buffer.not_full, &circular_buffer.mtx);
        }
        circular_buffer.jobs[circular_buffer.tail] = generated_jobs[i];
        circular_buffer.tail = (circular_buffer.tail+1)%MAXJOBS;
        circular_buffer.count++;

        pthread_cond_signal(&circular_buffer.not_empty);
        pthread_mutex_unlock(&circular_buffer.mtx);
    }

    work = 0;
    pthread_cond_broadcast(&circular_buffer.not_empty);

    for (int i=0;i<WORKERNUM;i++)
    {
        pthread_join(thread_args[i].thread_id, NULL);
    }
    pthread_mutex_destroy(&circular_buffer.mtx);
    pthread_cond_destroy(&circular_buffer.not_empty);
    pthread_cond_destroy(&circular_buffer.not_full);

    return 0;
}