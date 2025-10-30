#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<stdlib.h>
#include<ctype.h>
#include <asm-generic/errno-base.h>
#include<errno.h>
#include<dirent.h>

void write_stage2(char *filename) {
    struct stat st;
    stat(filename, &st);
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr,"%s is not a regular file\n", filename);
        return;
    }
    int unlinkStatus = unlink(filename);
    if (unlinkStatus < 0) {
        perror("unlink");
        return;
    }
    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0640);
    char* buf = NULL;
    size_t cap = 0;
    int len = 0;
    while ((len = getline(&buf, &cap, stdin)) != -1) {
        if (len == 1 && buf[0] == '\n') {
            break;
        }
        int off = 0;
        while (off<len) {
            ssize_t w = write(fd, buf+off, len-off);
            if (w<0 && errno == EINTR) {
                continue;
            }
            if (w < 0) {
                perror("write");
                free(buf);
                close(fd);
                return;
            }
            off+=w;
        }
    }
    free(buf);
    close(fd);
}

void show_stage3(char* filename) {
    struct stat st;
    stat(filename, &st);
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(filename);
        if (dir == NULL) {
            perror("opendir");
            return;
        }
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            struct stat st2;
            stat(ent->d_name, &st2);
            if (S_ISREG(st2.st_mode)) {
                fprintf(stdout, "%s\n", ent->d_name);
            }
        }
    }
    else if (S_ISREG(st.st_mode)) {
        char buf[4096];
        int fd = open(filename, O_RDONLY);
        while (1) {
            ssize_t off = 0;
            ssize_t len = read(fd, buf, sizeof(buf));
            if (len<0 && errno != EINTR) {
                perror("read");
                close(fd);
                return;
            }
            if (len == 0) {
                break;
            }
            while (off<len) {
                int w = write(STDOUT_FILENO, buf+off, len-off);
                if (w<0) {
                    perror("write");
                    close(fd);
                    return;
                }
                off+=w;
            }

            //printing size
            fprintf(stdout, "--File info--\nsize: %ld bytes\nUID of the owner: %d\nGID of the owner: %d\n\n", st.st_size, st.st_uid, st.st_gid);
        }
    }
    else {
        fprintf(stderr, "Type of the file is unknown!\n");
        return;
    }
}

int main() {
    while (1) {
        char optionsLetter[] = {'A', 'B', 'C', 'D'};
        char* options[] = {"write", "read", "walk", "exit"};
        for (int i=0;i<4;i++) {
            fprintf(stdout, "%c. %s\n",optionsLetter[i], options[i]);
        }

        char* choice = NULL;
        size_t cap = 0;
        int len = getline(&choice, &cap, stdin);
        if (len == -1) {
            perror("getline");
            return 1;
        }
        if (len>0 && choice[len-1] == '\n') {
            choice[len-1] = '\0';
            len--;
        }
        choice[0] = toupper(choice[0]);
        if (len>1 || choice[0] != optionsLetter[0] && choice[0] != optionsLetter[1] && choice[0] != optionsLetter[2] && choice[0] != optionsLetter[3]) {
            free(choice);
            fprintf(stderr, "Invalid command\n");
            return 1;
        }
        if (choice[0] == 'D') {
            free(choice);
            fprintf(stdout, "Gracefully exiting\n");
            break;
        }

        char* filename = NULL;
        size_t nameSize = 0;
        int filenameLen = getline(&filename,&nameSize,stdin );
        if (filenameLen>0 && filename[filenameLen - 1] == '\n') {
            filename[filenameLen - 1] = '\0';
            filenameLen--;
        }
        if (len == -1) {
            perror("getline");
            return 1;
        }
        struct stat st1;
        if (stat(filename, &st1) != 0) {
            free(filename);
            free(choice);
            fprintf(stderr, "File does not exist\n");
            return 1;
        }
        fprintf(stdout, "%s\n", filename);

        if (choice[0] == 'A') {
            write_stage2(filename);
        }
        if (choice[0] == 'B') {
            show_stage3(filename);
        }
        free(filename);
        free(choice);
    }

    return 0;
}
