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
#define DATA_LEN 4

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "10000 >= n > 0 - number of children\n");
    exit(EXIT_FAILURE);
}

void child_work(int n, float* result, char* log)
{
    int cnt = 0;
    srand(getpid());
    for (int i=0;i<MONTE_CARLO_ITERS;i++)
    {
        double x = (double)rand()/RAND_MAX;
        double y = (double)rand()/RAND_MAX;
        if (x*x + y*y <= 1)
        {
            cnt+=1;
        }
    }
    float res = ((float)cnt/MONTE_CARLO_ITERS) * 4.0f;
    printf("%f\n", res);
    result[n] = res;
    char buf[LOG_LEN+1];
    snprintf(buf,sizeof(buf), "%7.5f\n", res);
    memcpy(log+n*LOG_LEN, buf, LOG_LEN);
}

void reap_children(int n, float* data)
{
    while (1)
    {
        pid_t pid = wait(NULL);
        if (pid<=0)
        {
            if (errno == ECHILD)
            {
                break;
            }
            perror("wait");
        }
    }

    float sum=0.0;
    for (int i=0;i<n;i++)
    {
        sum+=data[i];
    }
    sum = sum/n;
    printf("Pi is approximately: %f\n", sum);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        usage(argv[0]);
    }
    int N = atoi(argv[1]);
    if (N<=0 || N>=30)
    {
        usage(argv[0]);
    }

    int fd = open("./log.txt", O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1)
    {
        perror("open");
    }
    if (ftruncate(fd, N*LOG_LEN))
    {
        perror("ftruncate");
    }

    char* log = (char*)mmap(NULL,N*LOG_LEN, PROT_WRITE|PROT_READ, MAP_SHARED, fd,0);
    if (log == MAP_FAILED)
    {
        perror("mmap");
    }
    if (close(fd) < 0)
    {
        perror("close");
    }
    float* data = (float*)mmap(NULL, N*sizeof(float), PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (data == MAP_FAILED)
    {
        perror("mmap");
    }

    for (int i=0;i<N;i++)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork");
        }
        else if (pid == 0)
        {
            child_work(i,data, log);
            exit(EXIT_SUCCESS);
        }
    }

    reap_children(N,data);

    if (munmap(data, N*sizeof(float)) < 0)
    {
        perror("munmap");
    }
    if (msync(log, N*LOG_LEN, MS_SYNC))
    {
        perror("msync");
    }
    if (munmap(log, N*LOG_LEN))
    {
        perror("munmap");
    }
}