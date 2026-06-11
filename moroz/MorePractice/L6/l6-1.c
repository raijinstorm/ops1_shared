#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define MONTE_CARLO_ITERS 100000
#define LOG_LEN 8
#define LOG_NAME "./log.txt"

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "10000 >= n > 0 - number of children\n");
    exit(EXIT_FAILURE);
}

void child_work(float* calculations, char* log, int idx, int N)
{
    int sample = 0;
    srand(getpid());
    for (int i=0;i<MONTE_CARLO_ITERS;i++)
    {
        double x = (double)rand()/RAND_MAX;
        double y = (double)rand()/RAND_MAX;
        if (x*x + y*y <= 1.0)
        {
            sample++;
        }
    }
    calculations[idx] = ((float)sample)/MONTE_CARLO_ITERS * 4;
    char buf[LOG_LEN+1] = {0};
    snprintf(buf, LOG_LEN+1, "%7.5f\n", calculations[idx]);
    memcpy(log+idx*LOG_LEN, buf, LOG_LEN);

    int log_size = N*LOG_LEN;
    int calculations_size = N*sizeof(float);
    if (munmap(calculations, calculations_size) == -1)
    {
        ERR("munmap");
    }
    if (msync(log, log_size, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(log, log_size) == -1)
    {
        ERR("munmap");
    }
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    int N = atoi(argv[1]);
    if (N<0 || N>30)
    {
        usage(argv[0]);
    }

    int calculations_size = N*sizeof(float);
    float* calculations = (float*)mmap(NULL, calculations_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (calculations == MAP_FAILED)
    {
        ERR("mmap");
    }

    int log_size = N*LOG_LEN;
    int filefd = open(LOG_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (filefd<0)
    {
        ERR("open");
    }
    if (ftruncate(filefd, log_size)<0)
    {
        ERR("ftruncate");
    }
    char* log = (char*)mmap(NULL, log_size, PROT_READ | PROT_WRITE, MAP_SHARED, filefd, 0);
    if (log==MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(filefd)<0)
    {
        ERR("close");
    }

    for (int i=0;i<N;i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            child_work(calculations, log, i, N);

            exit(EXIT_SUCCESS);
        }
    }

    while (wait(NULL)>0);
    float sum = 0;
    for (int i=0;i<N;i++)
    {
        sum+=calculations[i];
    }
    printf("Approximated PI is: %f\n",sum/N);

    if (munmap(calculations, calculations_size) == -1)
    {
        ERR("munmap");
    }
    if (msync(log, log_size, MS_SYNC) == -1)
    {
        // const char* errname = strerrorname_np(errno);
        // printf("%s\n", errname);
        ERR("msync");
    }
    if (munmap(log, log_size) == -1)
    {
        ERR("munmap");
    }
    return 0;
}