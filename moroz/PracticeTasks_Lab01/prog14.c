#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include<unistd.h>
#include<fcntl.h>
#include <sys/stat.h>
#include<sys/types.h>
#include<dirent.h>
#include<errno.h>

ssize_t read_file(int fd1, char* buffer) {
    ssize_t off1 = 0;
    ssize_t bytesRead = 0;
    while (off1<sizeof(buffer)) {
        bytesRead = TEMP_FAILURE_RETRY(read(fd1, buffer+off1, sizeof(buffer)-off1));
        if (bytesRead==-1) {
            perror("read");
            return 1;
        }
        if (bytesRead==0) {
            break;
        }
        off1+=bytesRead;
    }
    return off1;
}

ssize_t write_to_file(int fd2, char* buffer, ssize_t off1) {
    ssize_t off2 = 0;
    ssize_t bytesWritten = 0;
    while (off2<sizeof(buffer)) {
        bytesWritten = TEMP_FAILURE_RETRY(write(fd2, buffer+off2, off1-off2));
        if (bytesWritten==-1) {
            perror("write");
            return 1;
        }
        if (bytesWritten==0) {
            break;
        }
        off2+=bytesWritten;
    }
    return off2;
}

int main(int argc, char **argv) {
    if (argc!=3) {
        fprintf(stderr,"Usage: %s <filename>\n",argv[0]);
        return 1;
    }
    char* firstFile = argv[1];
    char* secondFile = argv[2];
    struct stat st1, st2;
    stat(firstFile, &st1);
    stat(secondFile, &st2);
    if (!S_ISREG(st1.st_mode)) {
        fprintf(stderr,"%s is not a regular file\n",firstFile);
        return 1;
    }
    if (!S_ISREG(st2.st_mode)) {
        fprintf(stderr,"%s is not a regular file\n",secondFile);
    }
    int fd1 = open(firstFile, O_RDONLY);
    int fd2 = open(secondFile, O_WRONLY|O_TRUNC);
    char buffer[1024];
    while (1) {
        ssize_t bytesRead = read_file(fd1, buffer);

        ssize_t bytesWritten = write_to_file(fd2, buffer, bytesRead);

        if (bytesWritten == 0) {
            break;
        }
    }
}
