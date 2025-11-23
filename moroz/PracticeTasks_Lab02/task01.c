#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/wait.h>
#include <signal.h>

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s 0<n\n", name);
    exit(EXIT_FAILURE);
}

void child_work(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec ^ getpid());

    int seconds = rand() % 6 + 5;
    sleep(seconds);
    printf("PROCESS with pid [%d] terminates after %d s\n", getpid(), seconds);
}

void create_children(int n) {
    pid_t s;
    for (int i = 0; i < n; i++) {
        if ((s = fork()) < 0)
            ERR("fork");
        if (s == 0) {
            child_work();
            exit(EXIT_SUCCESS);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2)
        usage(argv[0]);

    int n = atoi(argv[1]);
    if (n <= 0)
        usage(argv[0]);

    create_children(n);

    // for (int i=0;i<n;i++) {
    //     int status;
    //     pid_t pid = wait(&status);
    //     if (pid == -1) {
    //         if (errno!=ECHILD) {
    //             ERR("wait");
    //         }
    //         break;
    //     }
    //
    //     if (WIFEXITED(status)) {
    //         printf("child process %d exited with status %d\n", pid, WEXITSTATUS(status));
    //     }
    //     else if (WIFSIGNALED(status)) {
    //         printf("child process %d was killed by the signal %d\n", pid, WTERMSIG(status));
    //     }
    //     else if (WIFSTOPPED(status)) {
    //         printf("child process %d was stopped by the signal %d\n", pid, WSTOPSIG(status));
    //     }
    // }

    while (n>0) {
        pid_t pid;
        int status;
        sleep(1);
        while ((pid=waitpid(0,&status, WNOHANG))>0) {
            n--;
            if (WIFEXITED(status)) {
                printf("child process %d exited with status %d\n", pid, WEXITSTATUS(status));
            }
            else if (WIFSIGNALED(status)) {
                printf("child process %d was killed by the signal %d\n", pid, WTERMSIG(status));
            }
            else if (WIFSTOPPED(status)) {
                printf("child process %d was stopped by the signal %d\n", pid, WSTOPSIG(status));
            }
        }
        if (pid==-1) {
            if (errno != ECHILD) {
                ERR("waitpid");
            }
        }
        printf("PARENT: %d processes remain\n", n);
    }

    return EXIT_SUCCESS;
}
