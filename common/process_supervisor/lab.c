#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>


int percentage=50;
int usr1_parent_call_number=0;
int sigint_parent_call_number=0;
int root_pid;

void children_work() {
    srand(time(NULL)& getpid());
    sleep(rand()%10+10);
}

void fork_tree(int depth) {
    if (depth<=0)
        exit(EXIT_SUCCESS);
    int left_pid=fork();
    if (!left_pid) {
        printf("parent is <%d> I am <%d>\n",getppid(),getpid());
        setpgid(getpid(),getppid());
        fork_tree(depth-1);
        children_work();
        exit(EXIT_SUCCESS);
    }
    int right_pid=fork();
    if (!right_pid) {
        printf("parent is <%d> I am <%d>\n",getppid(),getpid());
        setpgid(getpid(),getppid());
        fork_tree(depth-1);
        children_work();
        exit(EXIT_SUCCESS);
    }
    if (right_pid==-1 || left_pid==-1)
    {
        fprintf(stderr,"fork_tree failed\n");
        kill(0,SIGKILL);
        exit(EXIT_FAILURE);
    }
    fprintf(stdout,"I am <%d> and I have left child <%d> and right child <%d>\n",
        getpid(),left_pid,right_pid);
    int awaited=0;
    int pid;
    while (awaited<2) {
        pid=wait(NULL);
        if (pid==-1 && errno==EINTR) {
            fprintf(stderr,"<%d> wait() was interrupted. Trying again\n",getpid());
            errno=0;
            continue;
        }
        else if (pid==-1 && errno!=0) {
            fprintf(stderr,"wait() failed\n");
            kill(pid,SIGKILL);
        }
        else {
            awaited++;
        }
    }
    fprintf(stdout,"I am <%d> and I successfully awaited all children\n",getpid());
}

void usr1_handler(int signo) {
    if (usr1_parent_call_number!=0 && getpid()==root_pid )
        return;
    srand(getpid());
    if (getpid()==root_pid)
        usr1_parent_call_number++;
    if ((rand()%100)>percentage) {
        (void)signo;
        char message[1024];
        snprintf(message,1024,"<%d> received USR1 signal\n",getpid());
        write(1,message,strlen(message));
        kill((-1)*getpid(),SIGUSR1);
    }
    else {
        char message[1024];
        snprintf(message,1024,"<%d> skipped USR1 signal\n",getpid());
        write(1,message,strlen(message));
        kill((-1)*getpid(),SIGUSR1);
    }
}

void sigint_handler(int signo) {
    if (sigint_parent_call_number!=0 && getpid()==root_pid ) {
        exit(EXIT_FAILURE);
    }
    (void)signo;
    char message[1024];
    snprintf(message,1024,"<%d> received SIGINT signal\n",getpid());
    write(2,message,strlen(message));

    if (getpid()==root_pid)
        sigint_parent_call_number++;
    kill((-1)*getpid(),SIGINT);
}
int main(int argc, char *argv[]) {
    percentage=50;
    if (argc!=2) {
        fprintf(stderr,"There is no percentage rate specified. Default to 50%%.\n");
    }
    else
        percentage=atoi(argv[1]);
    fprintf(stdout,"Root pid is %d\n",getpid());
    struct sigaction act;
    struct sigaction killact;
    act.sa_handler = usr1_handler;
    killact.sa_handler = sigint_handler;
    sigaction(SIGUSR1,&act,NULL);
    sigaction(SIGINT,&killact,NULL);
    root_pid=getpid();
    fork_tree(4);
    return 0;
}