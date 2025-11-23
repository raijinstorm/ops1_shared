#include<stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include<signal.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<sys/time.h>
#include<time.h>
#include<errno.h>
#include<string.h>

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s 0<n\n", name);
    exit(EXIT_FAILURE);
}

volatile sig_atomic_t last_signal = 0;
volatile sig_atomic_t children_left = 0;

void handler(int sig) {
    printf("[%d] received signal %d\n", getpid(), sig);
    last_signal = sig;
}

void sethandler(void (*f)(int), int sig) {
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (sigaction(sig,&act,NULL)) {
        ERR("sigaction");
    }
}

void clear_child(int sig) {
    pid_t pid;
    while ((pid=waitpid(0,NULL,WNOHANG))>0) {
        children_left--;
    }
    if (pid<0 && errno!=ECHILD) {
        ERR("waitpid");
    }
}

void child_work(int r) {
    srand(time(NULL)*getpid());
    int seconds = rand()%(10-5+1) + 5;
    for (int i=0;i<r;i++) {
        int time_left = seconds;
        while (time_left>0) {
            time_left = sleep(time_left);
        }
        if (last_signal==SIGUSR1) {
            printf("SUCCESS [%d]\n", getpid());
        }
        else {
            printf("FAILURE [%d]\n", getpid());
        }
    }
}

void create_children(int n, int r) {
    for (int i=0;i<n;i++) {
        pid_t pid = fork();
        if (pid<0) {
            ERR("fork");
        }
        if (pid==0) {
            sethandler(handler, SIGUSR1);
            sethandler(handler, SIGUSR2);
            child_work(r);
            exit(EXIT_SUCCESS);
        }
    }
}

void parent_work(int k, int p) {
    while (children_left>0) {
        sleep(k);
        if (children_left<=0) {break;}
        if (kill(0, SIGUSR1) == -1) {
            ERR("kill");
        }

        sleep(p);
        if (children_left<=0) break;
        if (kill(0, SIGUSR2) == -1) {
            ERR("kill");
        }
    }
    printf("[PARENT] terminates \n");
}

int main(int argc, char** argv) {
    if (argc!=5) {
        usage(argv[0]);
    }
    int n,k,p,r;
    n = atoi(argv[1]);
    k = atoi(argv[2]);
    p = atoi(argv[3]);
    r = atoi(argv[4]);

    children_left = n;

    sethandler(clear_child, SIGCHLD);
    sethandler(SIG_IGN, SIGUSR1);
    sethandler(SIG_IGN, SIGUSR2);
    create_children(n,r);
    parent_work(k,p);

    while (wait(NULL)>0);
    return(EXIT_SUCCESS);
}