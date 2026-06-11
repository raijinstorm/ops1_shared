#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)             \
(__extension__({                               \
long int __result;                         \
do                                         \
__result = (long int)(expression);     \
while (__result == -1L && errno == EINTR); \
__result;                                  \
}))
#endif

#define PROCESSNUM 2
#define BUFLEN 8

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sigint_handler(int sig)
{
    last_signal = sig;
}

void child_work(int pipes[PROCESSNUM+1][2], int idx)
{
    srand(getpid());
    int next = (idx+1)%(PROCESSNUM+1);
    for (int i=0;i<PROCESSNUM+1;i++)
    {
        if (idx == i)
        {
            if (close(pipes[i][1])<0)
            {
                ERR("close");
            }
        }
        else if (i == next)
        {
            if (close(pipes[next][0])<0)
            {
                ERR("close");
            }
        }
        else
        {
            if (close(pipes[i][1])<0)
            {
                ERR("close");
            }
            if (close(pipes[i][0])<0)
            {
                ERR("close");
            }
        }
    }

    if (sethandler(sigint_handler, SIGINT))
        ERR("Setting SIGINT handler");
    while (!last_signal)
    {
        int32_t num;
        int bytes_read = read(pipes[idx][0], &num, sizeof(int32_t));
        if (bytes_read<0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ERR("read");
        }
        if (bytes_read == 0)
        {
            break;
        }
        printf("PID:%d -- Num:%d\n", getpid(), num);
        if (num == 0)
        {
            break;
        }

        int rand_offset = rand()%21-10;
        num+=rand_offset;
        if (write(pipes[next][1],&num,sizeof(int32_t))<0)
        {
            if (errno == EPIPE)
            {
                break;
            }
            ERR("write");
        }
    }

    if (close(pipes[idx][0])<0)
    {
        ERR("close");
    }
    if (close(pipes[next][1])<0)
    {
        ERR("close");
    }
}

int main(int argc, char** argv)
{
    srand(time(NULL));

    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGINT handler");
    if (sethandler(SIG_IGN, SIGINT))
        ERR("Setting SIGINT handler");

    int pipes[PROCESSNUM+1][2];
    for (int i=0;i<PROCESSNUM+1;i++)
    {
        pipe(pipes[i]);
    }
    for (int i=1;i<PROCESSNUM+1;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(pipes, i);

            exit(EXIT_SUCCESS);
        }
    }

    int32_t c = 1;
    if (write(pipes[1][1],&c,sizeof(int32_t))<0)
    {
        if (errno != EPIPE)
        {
            ERR("write");
        }
    }
    child_work(pipes, 0);

    while (wait(NULL)>0);
    return 0;
}