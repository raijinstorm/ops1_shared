#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

volatile sig_atomic_t last_signal = 0;

void child_work() {
    alarm(20);
    srandom(time(NULL) ^ getpid());
    int rand_number=random()%6+5;
    sleep(rand_number);
    fprintf(stdout, "[CHILD INFO] Slept for %d seconds and have pid <%d>\n",rand_number,getpid());
    while(1) {
        pause();
    }
}

void await_all_children(pid_t *children,int size) {
    int status;
    for (int i=0; i<size; i++) {
        if(waitpid(children[i], &status, 0)==-1) {
            fprintf(stderr, "[WARNING] waitpid pid = <%d>\n", children[i]);
        }
        else {
            fprintf(stdout, "[INFO] Finished waiting child <%d>."
                            "It's return is %d\n",children[i],WEXITSTATUS(status));
        }
    }
}

void create_forks(char *argv) {
    long number_of_forks=strtol(argv,NULL,10);
    if (errno!=0) {
        fprintf(stderr, "Argument error");
        exit(1);
    }
    int *fork_array=malloc((number_of_forks-1)*sizeof(int));
    for(int i=0; i<number_of_forks; i++) {
        pid_t pid=fork();
        if (pid==0) {
            child_work();
            exit(EXIT_SUCCESS);
        }
        else {
            fork_array[i]=pid;
        }
    }
    // await_all_children(fork_array,(int)number_of_forks);
    free(fork_array);
}

void signal_handler(int sig) {
    last_signal=sig;
    if (last_signal==SIGUSR1) {
        fprintf(stdout,"SUCCESS\n");
    }
    else if (last_signal==SIGUSR2) {
        fprintf(stdout,"FAILURE\n");
    }
    else if (last_signal==SIGALRM) {
        fprintf(stdout,"Encountered timeout. Gracefully stopping <%d>\n",getpid());
        kill(getpid(),SIGTERM);
    }
    else {
        fprintf(stderr,"[FATAL] Unknown signal <%d> encountered. Stop execution.",sig);
        kill(0,SIGKILL);
    }
}

void signal_sender(char* k,char* p, char* r) {
    long initial_delay=strtol(k,NULL,10);
    long secondary_delay=strtol(p,NULL,10);
    long iteration_number=strtol(r,NULL,10);
    if (errno!=0) {
        fprintf(stderr, "Incorrect initially passed arguments");
        kill(0,SIGTERM);
        exit(1);
    }
    struct timespec ts1={initial_delay,0};
    struct timespec ts2={secondary_delay,0};
    for (int i=0; i<iteration_number; i++) {
        nanosleep(&ts1, NULL);
        kill(0,SIGUSR1);
        nanosleep(&ts2, NULL);
        kill(0,SIGUSR2);
    }
}

int main(int argc, char *argv[]) {
    struct sigaction act;
    act.sa_handler=signal_handler;
    act.sa_flags=0;

    sigaction(SIGUSR1,&act,NULL);
    sigaction(SIGUSR2,&act,NULL);
    sigaction(SIGALRM,&act,NULL);
    if (argc!=5) {
        fprintf(stderr, "Number of arguments not equal 2");
        kill(0, SIGKILL);
        exit(1);
    }
    create_forks(argv[1]);
    signal_sender(argv[2],argv[3],argv[4]);
    while (wait(NULL) > 0){}
    return 0;
}