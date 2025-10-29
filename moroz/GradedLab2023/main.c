#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<stdlib.h>
#include<ctype.h>

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
            fprintf(stdout, "Invalid command\n");
            return 1;
        }
        if (choice[0] == 'D') {
            free(choice);
            fprintf(stdout, "Gracefully exiting\n");
            break;
        }

        char* filename = NULL;
        size_t nameSize = 0;
        getline(&filename,&nameSize,stdin );
        if (len == -1) {
            perror("getline");
            return 1;
        }
        struct stat st1;
        if (!stat(filename, &st1)) {
            free(filename);
            free(choice);
            fprintf(stdout, "File does not exist\n");
            return 1;
        }
        fprintf(stdout, "%s", filename);
        free(filename);
        free(choice);
    }

    return 0;
}
