#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bits/sigaction.h>
#include <bits/signum-arch.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

void usage(void)
{
    fprintf(stderr, "USAGE: signals n k p l\n");
    fprintf(stderr, "n - number of children\n");
    fprintf(stderr, "k - Interval before SIGUSR1\n");
    fprintf(stderr, "p - Interval before SIGUSR2\n");
    fprintf(stderr, "l - lifetime of child in cycles\n");
    exit(EXIT_FAILURE);
}

void zombie_awaiter(int signal) {
    signal=signal;
    while (1) {
        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid==0 || (pid<0 && errno==ECHILD)){
            errno=0;
            break;//  all awaited
        }
        if (pid<0 && errno==EINTR) {
            errno=0;
            continue;
        }
        if (pid<0) {
            fprintf(stderr,"[ERROR] waitpid");
            kill(0,SIGKILL);
            exit(EXIT_FAILURE);
        }
    }
}

void child_handler(int signal) {
    if (signal==SIGUSR1)
        fprintf(stdout, "[INFO] SUCCED\n");
    else if (signal==SIGUSR2)
        fprintf(stdout, "[INFO] FAILED\n");
    else {
        fprintf(stderr,"[FATAL] unexpected signal <%d>\n", signal);
        kill(0,SIGKILL);
    }
}

void sethandler(void (*f)(int),int signal) {
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = f;
    if (sigaction(signal, &act, NULL) < 0) {
        fprintf(stderr, "sigaction error setting <%d> signal handling\n",signal);
        exit(EXIT_FAILURE);
    }

}

void child_working(int child_lifecycle) {
    srandom(time(NULL) & getpid());
    for (int i=0;i<child_lifecycle;i++) {
        struct timespec t={random()%6+5,0};
        nanosleep(&t,NULL);
        fprintf(stdout,"[INFO] I am a child <%d> and slept for %ld seconds\n",getpid(),t.tv_sec);
    }
    fprintf(stdout,"[INFO] I am a child <%d> and was terminated\n",getpid());
}

void create_children(int child_number,int child_lifecycle) {
    for (int i=0;i<child_number;i++) {
        int fork_pid=fork();
        if (fork_pid<0) {
            fprintf(stderr,"[ERROR] fork");
            kill(0,SIGKILL);
        }
        if (fork_pid==0) {
            sethandler(child_handler,SIGUSR1);
            sethandler(child_handler,SIGUSR2);
            child_working(child_lifecycle);
            exit(EXIT_SUCCESS);
        }
    }
}
void parent_work(int first_delay,int second_delay,int iterations) {

    for (int i=0;i<iterations;i++) {
        struct timespec t1={first_delay,0};
        struct timespec t2={second_delay,0};
        nanosleep(&t1,NULL);
        kill(0,SIGUSR1);
        nanosleep(&t2,NULL);
        kill(0,SIGUSR2);
    }
    fprintf(stdout, "Parent finish work");
}

int main(int argc, char **argv) {
    int n, k, p, l;
    if (argc != 5)
        usage();
    n = atoi(argv[1]);
    k = atoi(argv[2]);
    p = atoi(argv[3]);
    l = atoi(argv[4]);
    if (n <= 0 || k <= 0 || p <= 0 || l <= 0)
        usage();
    sethandler(SIG_IGN,SIGUSR1);
    sethandler(SIG_IGN,SIGUSR2);
    sethandler(zombie_awaiter,SIGCHLD);

    create_children(n,l);
    parent_work(k,p,l);
    return EXIT_SUCCESS;
}