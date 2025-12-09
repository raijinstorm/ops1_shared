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

volatile sig_atomic_t child_no = -1;
volatile sig_atomic_t issue_cnt = 0;
volatile sig_atomic_t parts_cnt = 0;
volatile sig_atomic_t student_pid = 0;
volatile sig_atomic_t sigusr2 = 0;

void sigusr1_handler(int sig, siginfo_t *info, void *ucontext)
{
    student_pid = info->si_pid;
    parts_cnt++;
    // printf("Teacher has accepted solution from student %d.\n", student_pid);
    kill(student_pid, SIGUSR2);
}

void siguser2_handler()
{
    sigusr2 = 1;
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s m b s \n", name);
    fprintf(stderr, "p - parts (1 <= p <= 10) \n");
    fprintf(stderr, "t - times 100 ms to finish (1 < t <= 10) \n");
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

void child_work(const int p, const int t, const int prob, const int c_no)
{
    child_no = c_no;
    printf("Student[%d,%d] has started doing task!\n", child_no, getpid());
    printf("Student[%d,%d] has prob = %d\n", child_no, getpid(), prob);

    //set unique seed
    srand(getpid());

    for (int i = 0 ; i < p; i++)
    {
        for (int j = 0; j < t; j++)
        {
            int base_inverval = 100; //100 ms
            if ((rand() % 101) < prob)
            {
                issue_cnt++;
                printf("Student[%d,%d] has issue (%d) doing task!\n", child_no, getpid(), issue_cnt);
            }
                base_inverval += 50;
            struct timespec t = {0, base_inverval * 1000000};
            nanosleep(&t, NULL);
        }
        printf("Student[%d,%d] has finished part %d out of %d!\n", child_no, getpid(), i + 1, p);
        kill(getppid(), SIGUSR1);
        while (sigusr2 == 0)
            sleep(10);
        sigusr2 = 0;
        printf("Student[%d,%d] got approval to continue\n", child_no, getpid());
    }
    exit(issue_cnt);
}


int main (int argc, char **argv)
{
    if (argc < 3)
        usage(argv[0]);

    int p = atoi(argv[1]);
    int t = atoi(argv[2]);

    if ( 1 >= t || 10 < t || 1 >= p || 10 < p)
        usage(argv[0]);

    struct sigaction sa;

    sa.sa_sigaction = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }


    for (int i = 3; i < argc; i++)
    {
        pid_t pid;
        if ((pid = fork()) < 0)
            ERR("fork");
        if (pid == 0)
        {
            sethandler(siguser2_handler, SIGUSR2);
            child_work(p, t, atoi(argv[i]), i - 2);
        }
    }

    while (parts_cnt < p * (argc - 3))
    {
        ;
    }

    printf("Waiting for children..\n");
    pid_t pid;
    int status;
    while ((pid = wait(&status)))
    {
        if (pid>0)
        {
            printf("Student %d finished with %d issues\n", pid, WEXITSTATUS(status));
        }
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

    printf("Teacher exists\n");
    return EXIT_SUCCESS;
}