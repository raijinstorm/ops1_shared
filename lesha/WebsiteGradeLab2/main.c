#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/wait.h>
int sigint_counter=0;

void work() {
    srand(getpid());
    if (rand()%4==1){
        fprintf(stderr,"[CHILD] My pid is: %d and I have suddenly started infinite loop\n",getpid());
        while (true){}  //Endless loop simulation
    }
    else
        sleep(rand()%10);
    fprintf(stdout,"[CHILD] %d working\n",getpid());
}

void wait_all_children() {
    int status;
    int child_number;
    fprintf(stdout,"[SUPERVISOR] Started waiting for children\n");
    while ((child_number=wait(&status))) {
        if (child_number==-1 && errno==EINTR)  // call was blocked restart
            continue;
        if (child_number==-1)
            break;
        fprintf(stdout,"[SUPERVISOR] child %d exited with status: %d\n",child_number,status);
    }
    if (child_number==-1 && errno==ECHILD) {
        fprintf(stdout,"[SUPERVISOR] Finished waiting for children\n");
        exit(EXIT_SUCCESS);
    }

    fprintf(stdout,"[SUPERVISOR] Some error happened during "
                   "awaiting child processes. Killing all children\n");

    exit(EXIT_FAILURE);
}

void child_handler(int signal_number) {
    if (signal_number==SIGINT) {
        fprintf(stderr,"[CHILD] My pid is %d I received SIGINT signal\n",getpid());
        exit(EXIT_FAILURE);
    }
}
void add_child_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = child_handler;
    sigaction(SIGINT,&sa,NULL);
}

void supervisor_handler(int signal_number) {
    if (signal_number==SIGINT) {
        if(++sigint_counter>2) {
            fprintf(stderr,"[SUPERVISOR] Second SIGINT was received gracefully stopping.\n");
            exit(EXIT_SUCCESS);
        }
            fprintf(stderr,"[SUPERVISOR] First SIGINT was received. "
                           "To stop execution send SIGINT one more time\n");
    }
    if (signal_number==SIGALRM) {
        fprintf(stderr,"[SUPERVISOR] Timeout: gracefully stopping children.\n");
        kill(0,SIGINT);
    }
}
void add_supervisor_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = supervisor_handler;
    sigaction(SIGINT,&sa,NULL);
    sigaction(SIGALRM,&sa,NULL);
}

void spawn_children(long child_number) {
    for (int i=0;i<child_number;i++) {
        pid_t child_pid=fork();
        if (child_pid==0) {
            add_child_signal_handlers();
            work();
            exit(EXIT_SUCCESS);
        }
    }
    add_supervisor_signal_handlers();
    alarm(20);
    fprintf(stdout,"[SUPERVISOR] Spawned %ld children\n",child_number);
    wait_all_children();
}

int main(int argc, char *argv[]) {
    long child_number=-1;
    if (argc>2) {
        fprintf(stderr,"Invalid number of arguments");
        exit(EXIT_FAILURE);
    }
    if (argc==2) {
        char *endptr;
        child_number=strtol(argv[1],&endptr,10);

        if (errno!=0 || endptr==argv[1])
        {
            fprintf(stderr,"Invalid argument(cannot cast "
                           "child number to intiger type)");
            exit(EXIT_FAILURE);
        }
    }
    else {
        fprintf(stdout,"Write number of childen you want");
        scanf("%ld",&child_number);
    }
    spawn_children(child_number);
    return 0;
}