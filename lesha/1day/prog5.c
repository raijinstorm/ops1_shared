#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

extern char** environ;

int main(int argc,char ** argv) {
    for (int i=0;environ[i]!=NULL;i++)
        printf("%d)%s\n",i+1,environ[i]);
    return EXIT_SUCCESS;
}