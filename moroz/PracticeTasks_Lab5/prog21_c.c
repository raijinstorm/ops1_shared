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

void write_fifo(int fifo, char* filename)
{
    struct stat file_status;
    if (stat(filename, &file_status) < 0) {
        ERR("stat");
    }
    size_t file_size = file_status.st_size;

    int filefd = open(filename, O_RDONLY);

    int bytes_sent = 0;
    char buf[PIPE_BUF] = {0};
    while (bytes_sent<file_size)
    {
        pid_t pid = getpid();
        memcpy(buf, &pid, sizeof(pid_t));
        int bytes_read = read(filefd, buf+sizeof(pid_t), MSG_SIZE);
        if (bytes_read<0)
        {
            ERR("read");
        }

        if (write(fifo, buf, PIPE_BUF)<0)
        {
            ERR("write");
        }
        bytes_sent += bytes_read;
        memset(buf,0,PIPE_BUF);
    }

    if (close(filefd)<0)
    {
        ERR("close");
    }
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }

    if (mkfifo(argv[1], S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP)<0)
    {
        if (errno != EEXIST)
        {
            ERR("mkfifo");
        }
    }

    int fifo = open(argv[1], O_WRONLY);
    if (fifo<0)
    {
        ERR("open");
    }


    write_fifo(fifo, argv[2]);

    if (close(fifo)<0)
    {
        ERR("close");
    }
    return 0;
}