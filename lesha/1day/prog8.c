#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#define ERR(message){perror(message);fprintf(stderr,"File: %s in line: %s",__FILE__,__LINE__);}

extern  char** environ;
extern int errno;

void usage(char *pname) {
    fprintf(stderr,"Number of arguments is even in the proceess %s",pname);
    exit(EXIT_FAILURE);
}
int main(int argn,char** argv ) {
    if (argn%2==0)
        usage(argv[0]);

    for (int i=1;i<argn;i+=2) {
        if (setenv(argv[i],argv[i+1],1)) {
            if (errno == EINVAL) {
                ERR("setenv - variable name contains = sign");
            }
            ERR("problem with setenv function");
        }
    }
    int index = 0;
    while (environ[index])
        printf("%s\n", environ[index++]);
    return EXIT_SUCCESS;
}
