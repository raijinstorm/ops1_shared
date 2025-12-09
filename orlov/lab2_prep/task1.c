#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t sigusr1_cnt = 0;
volatile sig_atomic_t sigusr2 = 0;

void sigusr1_handler(int sig)
{
    sigusr1_cnt++;
}

void sigusr2_handler(int sig)
{
    sigusr2 = 1;
}


void usage(char *name)
{
    fprintf(stderr, "USAGE: %s m b s \n", name);
    fprintf(stderr, "n - number of processes. \n");
    exit(EXIT_FAILURE);
}

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void child_work()
{
    srand(getpid());
    int s = rand() % 100 + 101;
    struct timespec t = {0, s * 1000000};

    while (sigusr2 == 0)
    {
        nanosleep(&t, NULL);
        kill(getppid(), SIGUSR1);
    }
    printf("Child %d received SIGUSR2. Exiting\n" , getpid());
    exit(EXIT_SUCCESS);
}

void parent_work(const int n)
{
    for (int i = 0; i < n; i++)
    {
        pid_t pid;
        if ((pid = fork()) < 0)
            ERR("fork");
        if (pid == 0)
        {
            child_work();
            break;
        }
    }
}

int main (int argc, char **argv)
{
    if (2 != argc)
        usage(argv[0]);

    int n = atoi(argv[1]);

    sethandler(sigusr1_handler, SIGUSR1);
    sethandler(sigusr2_handler, SIGUSR2);

    parent_work(n);

    while (sigusr1_cnt < 100)
        pause();

    printf("SIGUSR1 cnt =  %d .Killing all children\n", sigusr1_cnt);
    kill(0, SIGUSR2);
    printf("Waiting for children..\n");
    pid_t pid;
    while ((pid = wait(NULL)))
    {
        if (pid>0)
            continue;
        if (pid == -1)
        {
            if (errno == ECHILD)
                break;
            if (errno == EINTR)
                errno = 0;
        }
        else
            break;
    }

    printf("Parent exists\n");
    return EXIT_SUCCESS;
}