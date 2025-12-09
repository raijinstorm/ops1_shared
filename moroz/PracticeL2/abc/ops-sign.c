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

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define DELAY_MS 100

volatile sig_atomic_t start_flag = 0;
sigset_t sigold ;
volatile sig_atomic_t children_left = 0;

void sigusr1_handler(int sig){start_flag = 1;}

void sigchld_handler(int sig)
{
    pid_t pid;

    while (1)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (pid>0) {
            children_left--;
        }
        if (pid == 0)
            return;

        if (pid <= 0)
        {
            if (errno == ECHILD)
                return;
            ERR("waitpid");
        }
    }
}

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

ssize_t bulk_write(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void ms_sleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (TEMP_FAILURE_RETRY(nanosleep(&ts, &ts)))
        ERR("nanosleep");
}

void usage(int argc, char* argv[])
{
    printf("%s k name_1 [name_2 [...]]\n", argv[0]);
    printf("\t1 <= k -- number of pages to sign\n");
    printf("\tname_i -- name of i-th signatory\n");
    exit(EXIT_FAILURE);
}

//supposedly useless
void create_files(int k) {
    char buffer[512];
    for (int i=1;i<=k;i++) {
        snprintf(buffer, sizeof(buffer), "page%d", i);
        int fd = open(buffer, O_WRONLY | O_TRUNC | O_CREAT, 0644);
        if (fd<0) {
            ERR("open");
        }
        //
        int len = snprintf(buffer, sizeof(buffer), "Page %d\n", i);
        if (bulk_write(fd, buffer, len)<len) {
            ERR("bulk_write");
        }
        //
        if (close(fd)<0) {
            ERR("close");
        }
    }
}

void print_last_signature(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd<0) {
        ERR("open");
    }
    off_t end = lseek(fd,0,SEEK_END);
    if (end<0) {
        ERR("lseek");
    }
    off_t pos = end-1;
    char c;
    size_t len = 0;
    while (pos>=0) {
        if (lseek(fd,pos,SEEK_SET)<0) {
            ERR("lseek");
        }
        ssize_t r = bulk_read(fd,&c,1);
        if (r<0) {
            ERR("bulk_read");
        }
        if (r==0) {
            break;
        }
        if (c=='\n' && len > 0) {
            pos++;
            break;
        }
        len++;
        if (pos == 0) {
            pos = 0;
            break;
        }
        pos--;
    }
    if (len == 0) {
        printf("no signatures\n");
        if (close(fd)<0) {
            ERR("close");
        }
        return;
    }
    char buf[512];
    if (lseek(fd, pos, SEEK_SET)<0) {
        ERR("lseek");
    }
    ssize_t r = bulk_read(fd,buf,len);
    if (r<0) {
        ERR("bulk_read");
    }
    buf[r] = '\0';
    if (r>0 && buf[r-1] == '\n') {
        printf("%s", buf);
    }
    else {
        printf("%s\n", buf);
    }
    if (close(fd)<0) {
        ERR("close");
    }
}

void wait_for_start() {
    sigset_t suspend_mask = sigold;
    while (!start_flag) {
        if (sigsuspend(&suspend_mask)<0) {
            if (errno == EINTR) {
                continue;
            }
            ERR("sigsuspend");
        }
    }
}

void child_work(const char* name, int k) {
    char filename[64];
    char line[256];
    for (int i=1;i<=k;i++) {
        snprintf(filename, sizeof(filename), "page%d", i);
        int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd<0) {
            ERR("open");
        }
        int len = snprintf(line, sizeof(line), "%d %s signed page %d\n", getpid(), name, i);
        if (bulk_write(fd, line, len)<len) {
            ERR("bulk_write");
        }
        if (close(fd)<0) {
            ERR("close");
        }
        ms_sleep(DELAY_MS);
    }
}

void create_children(int k, int argc, char* argv[], pid_t* children) {
    int idx = 0;
    for (int i=2;i<=argc-1;i++) {
        const char* name = argv[i];
        pid_t pid = fork();
        if (pid<0) {
            ERR("fork");
        }
        if (pid == 0) {
            wait_for_start();
            child_work(name, k);
            exit(EXIT_SUCCESS);
        }
        children[idx++] = pid;
    }
}

int main(int argc, char* argv[]) {
    if (argc<3) {
        usage(argc, argv);
    }
    int k = atoi(argv[1]);
    if (k<1) {
        usage(argc,argv);
    }

    sethandler(sigusr1_handler, SIGUSR1);
    sethandler(sigchld_handler, SIGCHLD);
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);

    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0)
        ERR("sigprocmask");
    sigold = oldmask;

    create_files(k);

    int signers = argc-2;
    children_left = signers;
    pid_t children[signers];
    create_children(k, argc, argv, children);

    for (int i=0;i<signers;i++) {
        if (kill(children[i], SIGUSR1)<0) {
            ERR("kill");
        }
    }

    // if (sigprocmask(SIG_SETMASK, &sigold, NULL) < 0) {
    //     ERR("sigprocmask");
    // }

    while (children_left>0) {
        if (sigsuspend(&sigold)<0 && errno!=EINTR) {
            ERR("sigsuspend");
        }
    }

    printf("All children finished\n");

    for (int i=1;i<=k;i++) {
        char fname[64];
        snprintf(fname, sizeof(fname), "page%d", i);
        printf("Page %d: last signature: ", i);
        fflush(stdout);
        print_last_signature(fname);
    }

    return EXIT_SUCCESS;
}