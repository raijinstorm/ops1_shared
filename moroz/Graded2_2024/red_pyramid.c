#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <linux/limits.h>

#define ERR(source)                                                            \
(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),             \
kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_K 8                 /* max number of workers we support */
#define STONE_LINES 8           /* each stone has 8 lines          */
#define STONE_MAX_CHARS 512

volatile sig_atomic_t last_signal = 0;
volatile sig_atomic_t sigint_flag = 0;
static char stones[MAX_K][STONE_MAX_CHARS+1];
static int child_out_fd = -1;

void sig_handler(int sig) { last_signal = sig; }
void sigint_handler(int sig){ sigint_flag = 1; }

ssize_t bulk_read(int fd, char *buf, size_t count) {
    ssize_t c;
    ssize_t len = 0;
    do {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len; // EOF
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count) {
    ssize_t c;
    ssize_t len = 0;
    do {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void sethandler(void (*f)(int), int sigNo) {
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void ms_sleep(unsigned int milli) {
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (TEMP_FAILURE_RETRY(nanosleep(&ts, &ts)))
        ERR("nanosleep");
}

void usage(int argc, char *argv[]) {
    printf("%s k\n", argv[0]);
    printf("\t1 <= k <= 4 -- number of ???\n");
    exit(EXIT_FAILURE);
}

void child_handle(int index) {
    printf("Child %d with PID[%d] received SIGINT", index, getpid());
    if (child_out_fd>=0) {
        if (close(child_out_fd)<0) {
            ERR("close");
        }
        child_out_fd = -1;
    }

    if (kill(getppid(), SIGINT)<0 && errno!=ESRCH) {
        ERR("kill");
    }
}

void wait_for_sigusr1() {
    sethandler(sig_handler, SIGUSR1);

    sigset_t mask, suspend_mask;
    if (sigprocmask(SIG_BLOCK, NULL, &mask)) {
        ERR("sigprocmask");
    }
    suspend_mask = mask;
    sigdelset(&suspend_mask, SIGUSR1);
    last_signal = 0;
    // while (last_signal!=SIGUSR1) {
    //     sigsuspend(&suspend_mask);
    // }
    // while (sigsuspend(&suspend_mask)) {
    //     if (last_signal == SIGUSR1) {
    //         break;
    //     }
    // }
}

void get_one_stone(int fd, char* dest) {
    int lines = 0;
    int pos = 0;
    char c;
    ssize_t r;
    while (lines<STONE_LINES) {
        r = bulk_read(fd, &c, 1);
        if (r<0) {
            ERR("bulk_read");
        }
        if (pos>=STONE_MAX_CHARS) {
            fprintf(stderr, "Stone too large (>%d chars)\n", STONE_MAX_CHARS);
            exit(EXIT_FAILURE);
        }
        dest[pos++] = c;
        if (c == '\n') {
            lines++;
        }
    }
    dest[pos] = '\0';

    r = bulk_read(fd, &c, 1);
    if (r<0) {
        ERR("bulk_read");
    }
    if (r==1) {
        if (c!='\n') {
            lseek(fd, -1, SEEK_END);
        }
    }
}

void load_stones(char* path, int k) {
    int fd = open(path, O_RDONLY);
    if (fd<0) {
        ERR("open");
    }
    for (int i=0;i<k;i++) {
        get_one_stone(fd, stones[i]);
    }
    if (close(fd)<0) {
        ERR("close");
    }
}

void save_rectangle(int i) {
    char buffer[128];
    snprintf(buffer,sizeof(buffer),"p-%d", i);
    int fd = open(buffer, O_WRONLY | O_CREAT, 0644);
    child_out_fd = fd;
    char star = '*';
    for (int j=0;j<6;j++) {
        write(fd, &star,sizeof(char));
    }
    write(fd, "\n", 1);
    write(fd, "*", 1);
    for (int j=0;j<4;j++) {
        write(fd, " ",sizeof(char));
    }
    write(fd, "*\n", 2*sizeof(char));
    for (int j=0;j<6;j++) {
        write(fd, &star,sizeof(char));
    }
    if (close(fd)<0) {
        ERR("close");
    }

    child_out_fd = -1;
}

void child_work(int index) {
    printf("Child with index %d was created\n", index);
    fflush(stdout);
    wait_for_sigusr1();

    if (sigint_flag) {
        child_handle(index);
        return;
    }

    int weight = 0;
    for (int i=0;i<(int)strlen(stones[index]);i++) {
        if (stones[index][i] == '*') {
            weight++;
        }
        if (weight%18==0) {
            if (sigint_flag != 0) {
                child_handle(index);
                return;
            }
            save_rectangle(index);
            ms_sleep(500);
        }
    }
}

void create_children(int k, pid_t* workers) {
    for (int i=0;i<k;i++) {
        pid_t pid = fork();
        if (pid<0) {
            ERR("fork");
        }
        if (pid == 0) {
            child_work(i);
            exit(EXIT_SUCCESS);
        }
        workers[i] = pid;
    }
}

void wait_for_children(int k, pid_t* workers) {
    int remaining = k;
    int parent_sigint_handled = 0;
    pid_t pid;
    while (1) {
        pid = wait(NULL);
        if (pid<0) {
            if (errno == EINTR) {
                if (sigint_flag && !parent_sigint_handled) {
                    parent_sigint_handled = 1;
                    printf("Parent %d received SIGINT\n", getpid());

                    for (int i = 0;i<k;i++) {
                        if (kill(workers[i], SIGINT)<0) {
                            ERR("kill");
                        }
                    }
                }
                continue;;
            }
            if (errno == ECHILD) {
                break;
            }
            ERR("wait");
        }
        else {
            remaining--;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc!=3) {
        usage(argc,argv);
    }
    int k = atoi(argv[1]);
    if (k<1 || k>MAX_K) {
        usage(argc,argv);
    }
    char* temp_path = argv[2];
    char path[PATH_MAX];
    strncpy(path,temp_path,sizeof(path)-1);
    path[sizeof(path)-1] = '\0';

    printf("Parent pid: [%d]\n", getpid());

    sethandler(sigint_handler, SIGINT);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL)<0) {
        ERR("sigprocmask");
    }

    pid_t workers[MAX_K];
    load_stones(path,k);
    create_children(k, workers);
    for (int i=0;i<k;i++) {
        kill(workers[i], SIGUSR1);
    }
    wait_for_children(k, workers);
    printf("All workers have gone home.\n");
}