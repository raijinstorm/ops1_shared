#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define MAX_LINE 20
#define ERR(message){fprintf(stderr,"Error occurred during run: %s\n",message);exit(EXIT_FAILURE);}
int main(int argn,char** argv)
{

    char* times_str=getenv("TIMES");
    int times=-1;
    if (times_str)
        times=atoi(times_str);
    if (times<=0)
        ERR("Environmental variable TIMES was eather not set eather set incorrectly");
    char name[22];
    while (fgets(name,22,stdin)) {
        if (name[21]!='\0')
            ERR("Name has more than 20 chars. Poshel nahui");
        for (int i=0;i<times;i++) {
            fprintf(stdout,"Hello, %s",name);
        }
    }

    if(putenv("RESULT=DONE")) {
        ERR("putenv failed");
    }

    if (system("env | grep RESULT=DONE")) {
        ERR("evironment set failed");
    }
    return EXIT_SUCCESS;
}
