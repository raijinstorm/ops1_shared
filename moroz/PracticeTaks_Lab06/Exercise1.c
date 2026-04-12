#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include<string.h>

#define ERR(source)                                     \
do                                                  \
{                                                   \
fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
perror(source);                                 \
kill(0, SIGKILL);                               \
exit(EXIT_FAILURE);                             \
} while (0)

#define FILENAME "./src"
#define ASCII_SIZE 128

typedef struct SharedState
{
    int failure;
    size_t characters[ASCII_SIZE];
    pthread_mutex_t mutexes[];
}SharedState;

void count_chars(char* data, size_t start, size_t size, SharedState* shared)
{
    srand(getpid());
    int dies = 0;
    if (rand()%100<3)
    {
        dies = 1;
    }
    for (int i=start;i<size+start;i++)
    {
        int idx = (int)(data[i]);
        if (pthread_mutex_lock(&shared->mutexes[idx]) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&shared->mutexes[idx]);
        }
        if (dies)
        {
            shared->failure = 1;
            abort();
        }
        shared->characters[idx]++;
        pthread_mutex_unlock(&shared->mutexes[idx]);
    }
}

void create_children(int N, char* data, size_t size, SharedState* shared)
{
    size_t size_per_process = size/N;
    for (int i=0;i<N;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            if (i!=N-1)
            {
                count_chars(data, size_per_process*i, size_per_process, shared);
            }
            else
            {
                count_chars(data, size_per_process*i, size_per_process+size%N, shared);
            }

            exit(EXIT_SUCCESS);
        }
    }
}


int main(int argc, char** argv)
{
    if (argc!=2)
    {
        perror("Usage");
        exit(1);
    }
    int N = atoi(argv[1]);

    int fd = open(FILENAME, O_RDWR | O_CREAT, 0666);
    if (fd<0)
    {
        ERR("open");
    }
    struct stat buffer;
    int status = fstat(fd, &buffer);
    size_t size = buffer.st_size;

    char* data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(fd)<0)
    {
        ERR("close");
    }

    printf("%s\n", data);

    SharedState* shared = (SharedState*)mmap(NULL, sizeof(pthread_mutex_t)*ASCII_SIZE + sizeof(SharedState), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS,-1,0);
    if (shared == MAP_FAILED) ERR("mmap");
    memset(shared->characters, 0, ASCII_SIZE*sizeof(size_t));
    shared->failure = 0;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i=0;i<ASCII_SIZE;i++)
    {
        pthread_mutex_init(&shared->mutexes[i], &mutex_attr);
    }
    pthread_mutexattr_destroy(&mutex_attr);

    create_children(N, data, size, shared);

    while (wait(NULL)>0);

    if (shared->failure)
    {
        printf("computation failed.\n");
    }
    else
    {
        for (int i=0;i<ASCII_SIZE;i++)
        {
            printf("%c: %lu\n", (char)i, shared->characters[i]);
        }
    }


    for (int i=0;i<ASCII_SIZE;i++)
    {
        pthread_mutex_destroy(&shared->mutexes[i]);
    }
    if (munmap(shared, sizeof(pthread_mutex_t)*ASCII_SIZE + sizeof(SharedState))<0)
    {
        ERR("munmap");
    }
    if (msync(data, size, MS_SYNC)<0)
    {
        ERR("msync");
    }
    if (munmap(data, size)<0)
    {
        ERR("munmap");
    }
}