#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

int last_signal;
pid_t children_pgid;

void usage(void)
{
    fprintf(stderr, "USAGE: signals n k p l\n");
    fprintf(stderr, "n - number of children\n");
    fprintf(stderr, "k - Interval before SIGUSR1\n");
    fprintf(stderr, "p - Interval before SIGUSR2\n");
    fprintf(stderr, "l - lifetime of child in cycles\n");

    exit(EXIT_FAILURE);
}

void handler(int const sig)
{
    if (sig == SIGUSR1)
        last_signal = SIGUSR1;
    else if (sig == SIGUSR2)
        last_signal = SIGUSR2;
}

void child_work(int r)
{
    for (int i = 0; i < r; i++)
    {
        srand(time(NULL) * getpid());
        int t = 5 + rand() % (10 - 5 + 1);
        sleep(t);
        char* l_status;
        if (last_signal == SIGUSR1)
            l_status = "SUCCESS";
        else if (last_signal == SIGUSR2)
            l_status = "FAILURE";
        else
            l_status = "UNDEFINED";
        printf("PROCESS with pid %d latest status is %s\n", getpid(), l_status);
    }
}

void create_children(const int n , const int r)
{
    pid_t s;
    for (int i = 0; i < n; ++i)
    {
        if ((s = fork()) < 0)
            ERR("Fork:");

        if (s == 0)
        {
            child_work(r);
            exit(EXIT_SUCCESS);
        }

        // parent
        if (children_pgid == 0)
            children_pgid = s;              // first child is group leader

        if (setpgid(s, children_pgid) < 0)
            ERR("setpgid");
    }
}

int main(int const argc, char **argv)
{
    if (argc != 5)
        usage();
    int n,k,p,r;
    n = atoi(argv[1]);
    k = atoi(argv[2]);
    p = atoi(argv[3]);
    r = atoi(argv[4]);

    struct sigaction sa;
    sa.sa_handler = handler;
    sa.sa_flags = 0;              // no special behavior
    sigemptyset(&sa.sa_mask);     // no additional signals blocked

    sigaction(SIGUSR1, &sa, NULL); // install handler for SIGUSR1
    sigaction(SIGUSR2, &sa, NULL); // install handler for SIGUSR2



    printf("Creating children\n");
    create_children(n , r);

    printf("Sending SIGUSR1 to all children\n");
    kill(-children_pgid, SIGUSR1);
    sleep(k);

    printf("Sending SIGUSR2 to all children\n");
    kill(-children_pgid, SIGUSR2);
    sleep(p);


    // wait for all children
    while (n > 0)
    {
        pid_t pid;
        while (1)
        {
            pid = waitpid(0, NULL, WNOHANG);
            if (0 == pid)
                break;
            if (0 >= pid)
            {
                if (ECHILD == errno)
                    break;
                ERR("waitpid:");
            }
        }
    }
    return EXIT_SUCCESS;
}