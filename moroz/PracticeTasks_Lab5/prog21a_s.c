#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s fifo_file\n", name);
    exit(EXIT_FAILURE);
}

void read_fifo(int fifo)
{
    int bytes_read = 0;
    char buf[PIPE_BUF] = {0};
    while ((bytes_read = read(fifo, buf, PIPE_BUF))>0)
    {
        if (bytes_read>sizeof(pid_t))
        {
            pid_t pid;
            memcpy(&pid, buf, sizeof(pid_t));
            printf("PID:%d-------------\n", pid);
            for (int i=sizeof(pid_t);i<bytes_read;i++)
            {
                if (isalnum(buf[i]))
                {
                    printf("%c", buf[i]);
                }
            }
        }

    }

    if (bytes_read<0)
    {
        ERR("read");
    }
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }
    if (unlink(argv[1])<0)
    {
        if (errno != ENOENT)
        {
            ERR("unlink");
        }
    }

    if (mkfifo(argv[1], S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP)<0)
    {
        if (errno!=EEXIST)
        {
            ERR("mkfifo");
        }
    }
    int fifo = open(argv[1], O_RDONLY);
    if (fifo<0)
    {
        ERR("open");
    }

    read_fifo(fifo);

    if (close(fifo)<0)
    {
        ERR("close");
    }
    if (unlink(argv[1])<0)
    {
        if (errno != ENOENT)
        {
            ERR("unlink");
        }
    }
}