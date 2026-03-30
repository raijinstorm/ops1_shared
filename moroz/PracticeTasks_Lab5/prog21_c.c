#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MSG_SIZE (PIPE_BUF - sizeof(pid_t))
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s fifo_file file\n", name);
    exit(EXIT_FAILURE);
}

void write_to_fifo(int fifo, int filefd) {
    ssize_t count;
    char buffer[PIPE_BUF];
    char* buf;
    *((pid_t *)buffer) = getpid();
    buf = buffer + sizeof(pid_t);
    do {
        if ((count = read(filefd,buf,MSG_SIZE))<0) {
            ERR("read");
        }
        if (count<MSG_SIZE) {
            memset(buf+count,0,MSG_SIZE - count);
        }
        if (count>0) {
            if (write(fifo, buffer, PIPE_BUF) < 0) {
                ERR("write");
            }
        }
    }while (count == MSG_SIZE);
}

int main (int argc, char** argv) {
    int fifo, filefd;
    if (argc!=3) {
        usage(argv[0]);
    }

    if (mkfifo(argv[1], S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) {
        if (errno!=EEXIST) {
            ERR("mkfifo");
        }
    }
    if ((fifo = open(argv[1], O_WRONLY))<0) {
        ERR("open");
    }
    if ((filefd = open(argv[2], O_RDONLY))<0) {
        ERR("open");
    }
    write_to_fifo(fifo, filefd);
    if (close(fifo) < 0) {
        ERR("close");
    }
    if (close(filefd) < 0) {
        ERR("close");
    }
    return 0;
}