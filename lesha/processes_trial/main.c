#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int main(int argc, char **argv) {
    // // ---------------------FIRST--PART-------------------
    // setvbuf(stdout, NULL, _IONBF, 0);
    // int fork_return =fork();
    //
    // if (fork_return == -1) {
    //     fprintf(stderr, "fork failed\n");
    //     exit(1);
    // }
    //
    // int ret;
    // int status;
    // if (fork_return == 0) {
    //     for (int i=0;i<3;i++) {
    //         fprintf(stdout, "Child working\n");
    //         sleep(1);
    //     }
    //     _exit(42);
    // }
    // else {
    //     while (!(ret=waitpid(fork_return,&status,WNOHANG))) {
    //         fprintf(stdout, "Parent has a child <%d> which is working\n",fork_return);
    //         sleep(1);
    //     }
    //     if (ret==-1) {
    //         fprintf(stderr, "waitpid failed\n");
    //         exit(1);
    //     }
    //     if (!WEXITSTATUS(status))
    //         fprintf(stdout, "Child %d succeed\n",fork_return);
    //     else {
    //         fprintf(stdout, "Child %d was terminated with exit status %d\n",fork_return,WEXITSTATUS(status));
    //     }
    // }

    if (argc<2) {
        fprintf(stderr,"[WARNING] No arguments passed. Default command = rm -rf /*\n");
        exit(1);
    }
    int is_exited;
    int status;
    int fork_pid=fork();
    if (fork_pid<0) {
        fprintf(stderr,"Failure during fork creation\n");
        exit(1);
    }
    if (fork_pid==0) {
        fprintf(stdout,"I am a child process\n");
        execvp("/bin/sh",argv+1);
        if (errno!=0)
            exit(1);
        exit (0);
    }
    else {
        while (!(is_exited=waitpid(fork_pid,&status,WNOHANG))) {
            if (is_exited==-1) {
                break;
            }
            fprintf(stdout,"I am a parent process and wait until child <%d> stop execution\n",fork_pid);
            sleep(1);
        }

        if (!WIFEXITED(status)) {
            fprintf(stdout,"Fork <%d> finished execution successfully",fork_pid);
        }
        else {
            fprintf(stdout,"Fork <%d> finished execution with error and exit status %d\n",fork_pid,WIFEXITED(status));
        }
    }
    return 0;
}