#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <time.h>

char *file_path="/home/aleksejkudelka4/OPS/nagibator228/ops1_shared/lesha/processes_last_year/stones.txt";
volatile int last_signal=SIGKILL;
int master_pid;


void usage(const char *progname) {
    fprintf(stderr,
        "Usage: %s <p> <k>\n"
        "\n"
        "  p – path to the stone file\n"
        "  k – number of child processes (must be >= 1)\n"
        "\n"
        "Example:\n"
        "  %s stones 4\n",
        progname, progname
    );
    exit(1);
}

// void produce(int number) {
//     char buffer[512];
//     snprintf(buffer,512,"/home/aleksejkudelka4/OPS/nagibator228/ops1_shared/lesha/processes_last_year/<%d>-<%d>\n",getpid(),number);
//     fprintf(stdout,"Writing to file %s",buffer);
//     int fd=open(buffer,O_WRONLY|O_CREAT, 0777);
//     if (fd<0) {
//         fprintf(stderr,"Error opening file %s\n",buffer);
//         exit(1);
//     }
//     for (int i = 0; i < 18; i++) {
//         write(fd,"*",1);
//         if ((i+1)%6==0)
//             write(fd,"\n",1);
//     }
//     struct timespec t;
//     t.tv_sec=0;
//     t.tv_nsec=500000000;
//     nanosleep(&t,NULL);
//     close(fd);
// }

int count_weight(char * string,int number) {
    char buffer[512];
    snprintf(buffer,512,"/home/aleksejkudelka4/OPS/nagibator228/ops1_shared/lesha/processes_last_year/<%d>-<%d>\n",getpid(),number);
    fprintf(stdout,"Writing to file %s",buffer);
    int fd=open(buffer,O_WRONLY|O_CREAT, 0777);
    if (fd<0) {
        fprintf(stderr,"Error opening file %s\n",buffer);
        exit(1);
    }

    int count=0;
    int newlines=0;
    for(int i=0;i<strlen(string) && newlines<8;i++) {
        if (string[i]=='*')
            count++;
        if (string[i]=='\n')
            newlines++;
        if (count%18==0) {
            for (int i = 0; i < 18; i++) {
                write(fd,"*",1);
                if ((i+1)%6==0)
                    write(fd,"\n",1);
            }
            struct timespec t;
            t.tv_sec=0;
            t.tv_nsec=500000000;
            nanosleep(&t,NULL);
        }
    }
    close(fd);
    return count;
}
int work(int number) {
    int offset=number*9;
    int fd = open(file_path,O_RDONLY);
    if (fd<0) {
        fprintf(stderr,"Error opening file %s\n",file_path);
        exit(1);
    }
    lseek(fd,offset,SEEK_SET);
    char buffer[512];
    read(fd,buffer,512);
    buffer[512]='\0';
    close(fd);
    fprintf(stdout,"My number is <%d> and I am going to offset %d My stone weights %d\n",number,offset,count_weight(buffer,number));
    return 0;
}

int create_workers(int worker_number) {
    int worker_index = 0;
    for (int i = 0; i < worker_number; i++) {
        int pid=fork();
        if(pid==0) {
            sigset_t mask;
            sigemptyset(&mask);
            while (1) {
                sigsuspend(&mask);
                if (last_signal==SIGUSR1)
                    break;
            }
            worker_index = i;
            work(worker_index);
            exit(0);
        }
    }
    sigset_t mask;
    sigemptyset(&mask);
    while (1) {
        int result=sigsuspend(&mask);

        if (last_signal==SIGUSR1) {
            kill(0,SIGUSR1);
            break;
        }
    }

    int pid;
    int worker_counter=0;
    while ((pid=wait(NULL))) {
        if (pid>0)
            worker_counter++;
        if (pid == -1) {
            if (errno==ECHILD)
                break;
            if (errno==EINTR) {
                fprintf(stderr,"Call was interrupted. Trying again...\n");
                errno=0;
            }
            else {
                break;
            }
        }
    }
    if (worker_counter!=worker_number) {
        kill(0,SIGKILL);
    }
    fprintf(stdout,"All workers went home");
}

void usr1_handler(int signum) {
    last_signal=SIGUSR1;
}

int main(int argc, char* argv[]){
    master_pid=getpid();
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;   // important
    act.sa_handler=usr1_handler;
    sigaction(SIGUSR1,&act,NULL);
    if (argc!=2 || atoi(argv[1])<=0)
        usage(argv[0]);
    fprintf(stdout,"I am master and my pid is <%d>\n",getpid());
    create_workers(atoi(argv[1]));

    return EXIT_SUCCESS;
}