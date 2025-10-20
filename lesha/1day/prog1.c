#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LENGTH
#define ERR(source) {perror(source);fprintf(stderr,"Error occurred in %s file, in %d line",__FILE__,__LINE__);exit(EXIT_FAILURE);}

int main() {

    fprintf(stdout,"Enter your name: ");
    char *name=NULL;
    size_t name_size=0;
    getline(&name,&name_size,stdin);
    if (strlen(name)>MAX_LENGTH) {
        ERR("Too long name");
    }
    fprintf(stdout,"Your name is %s",name);
    free(name);


    return EXIT_SUCCESS;
}