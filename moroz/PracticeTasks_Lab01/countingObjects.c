#include<stdlib.h>
#include<stdio.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<dirent.h>
#include<string.h>
#include<errno.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void scan_dir(char* filename) {
    DIR* dir = opendir(filename);
    if (!dir) {
        ERR("opendir");
    }
    struct dirent* ent;
    int reg_count = 0, link_count = 0, dir_count = 0, other_count = 0;
    errno = 0;
    while ((ent = readdir(dir)) != NULL) {
        struct stat st1;
        if (lstat(ent->d_name, &st1) == -1) {
            ERR("stat");
        }
        if (S_ISDIR(st1.st_mode)) {
            dir_count++;
        }
        else if (S_ISLNK(st1.st_mode)) {
            link_count++;
        }
        else if (S_ISREG(st1.st_mode)) {
            reg_count++;
        }
        else {
            other_count++;
        }
    }
    int readdir_err = errno;
    int closedir_err = closedir(dir)==-1?errno:0; //closedir returns -1 on error
    if (readdir_err!=0) {
        errno = readdir_err;
        ERR("readdir");
    }
    if (closedir_err!=0) {
        errno = closedir_err;
        ERR("closedir");
    }
    fprintf(stdout,"The total number of objects in a folder is: %d\n files: %d\n directories: %d\n links: %d\n other: %d", reg_count+link_count+dir_count+other_count, reg_count, dir_count, link_count, other_count);
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

    char buf[256];
    if (!getcwd(buf, sizeof(buf))) {
        ERR("getcwd");
    }
    if (chdir(filename)) {
        ERR("chdir");
    }
    scan_dir(".");
    if (chdir(buf)) {
        ERR("chdir");
    }

    return 0;
}