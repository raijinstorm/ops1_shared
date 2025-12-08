#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#define ERR(source) \
    (kill(0, SIGKILL), perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define FILE_MAX_SIZE 512

volatile sig_atomic_t usr1_status = 0;
volatile sig_atomic_t usr2_status = 0;
void wait_usr2(char* name, sigset_t oldmask);

ssize_t bulk_read(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len;  // EOF
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void usage(int argc, char* argv[])
{
    printf("%s p h\n", argv[0]);
    printf("\tp - path to directory describing the structure of the Austro-Hungarian office in Prague.\n");
    printf("\th - Name of the highest administrator.\n");
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

void reap_children() {
    pid_t pid;
    while ((pid = wait(NULL)) > 0);
    if (pid<0) {
        if (errno == ECHILD) {
            return;;
        }
        ERR("waitpid");
    }
}

void handler_usr1(int sigNo)
{
    usr1_status = 1;
}
void handler_usr2(int sigNo) {
    usr2_status = 1;
}

void child_choice(char* name) {
    srand(time(NULL)*getpid());
    int choice = rand()%3;
    if (choice == 0) {
        printf("%s received a document. Sending to the archive\n", name);
    }
    else {
        if (strcmp(name, "Franz_Joseph")==0) {
            printf("%s received a document. Ignoring.\n", name);
        }
        else {
            printf("%s received a document. Passing on to the superintendent\n", name);
            pid_t ppid = getppid();
            kill(ppid, SIGUSR2);
        }
    }
}

void wait_usr2(char* name, sigset_t oldmask) {
    sigset_t mask = oldmask;
    sigdelset(&mask, SIGUSR2);
    while (1) {
        sigsuspend(&mask);
        child_choice(name);
    }
}

void get_subordinates(char* path, char* name, sigset_t oldmask)
{
    printf("My name is %s and my PID is %d\n", name, getpid());
    char dir[512];
    strcpy(dir, path);

    strcat(path, "/");
    strcat(path, name);
    int fd = open(path, O_RDONLY);
    if (fd == -1)
    {
        ERR("open");
    }
    char names[FILE_MAX_SIZE+1];
    ssize_t len = bulk_read(fd, names, FILE_MAX_SIZE);
    names[len] = '\0';
    char* token = strtok(names, "\n");
    while (token!=NULL) {
        if (strcmp(token, "-")==0) {
            printf("%s inspecting %s\n", name, token);
        }
        else {
            printf("%s inspecting %s\n", name, token);
            pid_t s = fork();
            if (s<0) {
                ERR("fork");
            }
            if (s==0) {
                char new_path[512];
                strcpy(new_path, dir);
                get_subordinates(new_path, token, oldmask);
                exit(EXIT_SUCCESS);
            }
        }
        token = strtok(NULL, "\n");
    }
    close(fd);
    printf("%s has inspected all subordinates\n", name);
    wait_usr2(name, oldmask);
    reap_children();
    printf("%s is leaving the office\n", name);
}

int main(int argc, char* argv[])
{
    if (argc!=3)
    {
        usage(argc, argv);
    }

    sethandler(handler_usr1, SIGUSR1);
    sethandler(handler_usr2, SIGUSR2);
    sethandler(SIG_IGN, SIGINT);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask)<0)
    {
        ERR("sigprocmask");
    }

    printf("Waiting for SIGUSR1\n");

    sigsuspend(&oldmask);

    char path[512];
    strcpy(path, argv[1]);
    get_subordinates(path, argv[2], oldmask);
    reap_children();

    if (sigprocmask(SIG_UNBLOCK, &mask, NULL)<0)
    {
        ERR("sigprocmask");
    }
    return EXIT_SUCCESS;
}


