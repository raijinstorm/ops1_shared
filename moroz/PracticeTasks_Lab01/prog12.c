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
#include<unistd.h>
#include<time.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *pname)
{
    fprintf(stderr, "USAGE:%s -n Name -p OCTAL -s SIZE\n", pname);
    exit(EXIT_FAILURE);
}

void create_file(char* filename, ssize_t size, mode_t permissions, int perc) {
    FILE* file;
    umask(~permissions&0777);
    if ((file = fopen(filename, "w+")) == NULL) {
        ERR("fopen");
    }
    for (int i=0;i<(size*perc)/100;i++) {
        if (fseek(file, rand()%size, SEEK_SET)) {
            ERR("fseek");
        }
        fprintf(file, "%c", 'A' + (i%('Z' - 'A'+1)));
    }
    if (fclose(file)) {
        ERR("close");
    }
}

int main(int argc, char** argv) {
    int c;
    char* name = NULL;
    mode_t permissions = -1;
    ssize_t size = -1;
    while ((c = getopt(argc,argv,"p:n:s:")) != -1) {
        switch (c) {
            case 'p':
                permissions = strtol(optarg, NULL, 8);
                break;
            case 's':
                size = strtol(optarg, NULL, 10);
                break;
            case 'n':
                name = optarg;
                break;
            default:
                usage(argv[0]);
        }
    }
    if (name == NULL || permissions == (mode_t)-1 || size < 0) {
        usage(argv[0]);
    }
    if (unlink(name)==-1 && errno != ENOENT) {
        ERR("unlink");
    }
    srand(time(NULL));
    create_file(name, size, permissions, 10);
    return 0;
}