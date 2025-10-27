#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<stdlib.h>
#include<ctype.h>

int main() {
    char optionsLetter[] = {'A', 'B', 'C', 'D'};
    char* options[] = {"write", "read", "walk", "exit"};
    for (int i=0;i<4;i++) {
        fprintf(stdout, "%c. %s\n",optionsLetter[i], options[i]);
    }

    char choice = getchar();
    if (toupper(choice) != optionsLetter[0] && choice != optionsLetter[1] && choice != optionsLetter[2] && choice != optionsLetter[3]) {
        fprintf(stdout, "Invalid command\n");
        return 1;
    }

    return 0;
}