#include <stdio.h>
#include <stdlib.h>


#define ERR(message){fprintf(stderr,"%s\n",message);exit(EXIT_FAILURE);}

int main(int argc,char** argv) {
    if (argc != 3)
        ERR("Number of parameters does not equal to two");
    int number1=strtol(argv[1],NULL,10);
    int number2=strtol(argv[2],NULL,10);
    if (number1 && number2)
        ERR("2 numbers were found!")
    if (!number1 && !number2)
        ERR("No numbers were found!")

    if (number1)
        for (int i=0;i<number1;i++)
            fprintf(stdout,"%d) My greetings,%s",i+1,argv[2]);
    else
        for (int i=0;i<number2;i++)
            fprintf(stdout,"%d) My greetings,%s",i+1,argv[1]);
    return EXIT_SUCCESS;
}