#define  _XOPEN_SOURCE 700

#include<stdlib.h>
#include<stdio.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<dirent.h>
#include<string.h>
#include<errno.h>
#include <unistd.h>
#include<ftw.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

int directories = 0; int file = 0; int links = 0; int other = 0;

int count(const char* fpath, const struct stat* sb, int tflag, struct FTW* ftwbuf) {
    if (tflag == FTW_F) {
        file++;
    }
    else if (tflag == FTW_D || tflag == FTW_DNR || tflag == FTW_DP) {
        directories++;
    }
    else if (tflag == FTW_SL || tflag == FTW_NS) {
        links++;
    }
    else {
        other++;
    }
    return 0;
}

int main() {
    char* filename = NULL;
    size_t cap=0;
    fprintf(stdout, "Enter the directory name: ");
    fflush(stdout);
    ssize_t filename_len = getline(&filename, &cap, stdin);
    if (filename_len == -1) {
        ERR("getline");
    }
    filename[filename_len - 1] = '\0';
    struct stat st;
    if (stat(filename, &st) == -1) {
        ERR("stat");
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s is not a directory!\n", filename);
        return 1;
    }

    if (nftw(filename,count,1,FTW_PHYS) == -1) {
        ERR("nftw");
    }

    fprintf(stdout,"The number of objects in a folder is:\n files: %d\n directories: %d\n links: %d\n other: %d", file, directories, links, other);

    return 0;
}