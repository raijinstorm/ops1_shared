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


#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_CHILDREN_NUM 10
#define BUFLEN 200

volatile sig_atomic_t last_signal = 0;
volatile sig_atomic_t must_die = 0;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "0<n<=10 - number of children\n");
    exit(EXIT_FAILURE);
}

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
    last_signal += 1;
}

void sigint_child_handler(int sig)
{
    if (rand()%100<20)
    {
        must_die = 1;
    }
}

void child_work(int pipes[MAX_CHILDREN_NUM][2], int shared_pipe[2], int idx, int n)
{
    srand(getpid());
    if (sethandler(sigint_child_handler, SIGINT))
        ERR("Setting SIGINT handler");
    if (close(shared_pipe[0])<0)
    {
        ERR("close");
    }
    for (int i=0;i<n;i++)
    {
        if (i!=idx)
        {
            if (close(pipes[i][0])<0)
            {
                printf("%d\n", pipes[i][0]);
                ERR("close");
            }
            if (close(pipes[i][1])<0)
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
        }
    }

    while (1)
    {
        if (must_die)
        {
            break;
        }

        char c;
        int bytes_read = TEMP_FAILURE_RETRY(read(pipes[idx][0], &c, 1));
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
            break;
        unsigned char rand_size = rand()%200 + 1;
        char buf[BUFLEN+1] = {0};
        buf[0] = rand_size;
        memset(buf+1, c, rand_size);
        if (TEMP_FAILURE_RETRY(write(shared_pipe[1], buf, rand_size+1))<0)
        {
            if (errno==EPIPE)
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
    if (close(shared_pipe[1])<0)
    {
        ERR("close");
    }
}

void parent_work(int pipes[MAX_CHILDREN_NUM][2], int shared_pipe[2], int n)
{
    srand(getpid());
    if (sethandler(sigint_handler, SIGINT))
        ERR("Setting SIGINT handler");
    while (1)
    {
        while (last_signal > 0)
        {
            int rand_idx = rand()%n;
            int attempts = 0;
            while (pipes[rand_idx%n][1] == -1 && attempts<n)
            {
                rand_idx++;
                attempts++;
            }
            if (attempts == n)
            {
                last_signal = 0;
                break;
            }

            char rand_char = rand()%('z' - 'a'+1) + 'a';
            if (write(pipes[rand_idx][1], &rand_char, 1)<0)
            {
                if (errno == EPIPE)
                {
                    if (close(pipes[rand_idx][1])<0)
                    {
                        ERR("close");
                    }
                    pipes[rand_idx][1] = -1;
                    continue;
                }
                ERR("write");
            }
            last_signal--;
        }

        unsigned char message_len;
        int bytes_received = read(shared_pipe[0], &message_len, 1);
        if (bytes_received<0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ERR("read");
        }
        if (bytes_received == 0)
            break;

        char buf[BUFLEN+1] = {0};
        bytes_received = TEMP_FAILURE_RETRY(read(shared_pipe[0], buf, message_len));
        if (bytes_received<0)
        {
            ERR("read");
        }
        if (bytes_received == 0)
            break;

        printf("\n%s\n", buf);
    }
}

int main(int argc, char **argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGINT handler");
    if (sethandler(SIG_IGN, SIGINT))
        ERR("Setting SIGINT handler");

    int n = atoi(argv[1]);
    int pipes[MAX_CHILDREN_NUM][2] = {0};
    for (int i=0;i<n;i++)
    {
        pipe(pipes[i]);
    }

    int shared_pipe[2];
    pipe(shared_pipe);

    for (int i=0;i<n;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        else if (pid == 0)
        {
            child_work(pipes, shared_pipe, i, n);

            exit(EXIT_SUCCESS);
        }
    }
    for (int i=0;i<n;i++)
    {
        if (close(pipes[i][0])<0)
        {
            ERR("close");
        }
    }

    if (close(shared_pipe[1])<0)
    {
        ERR("close");
    }
    parent_work(pipes, shared_pipe, n);

    while (wait(NULL)>0);
    for (int i=0;i<n;i++)
    {
        if (pipes[i][1]!=-1)
        {
            if (close(pipes[i][1])<0)
            {
                ERR("close");
            }
        }
    }
    if (close(shared_pipe[0])<0)
    {
        ERR("close");
    }
}