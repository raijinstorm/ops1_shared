#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>

#define ERR(message){fprintf(stderr, message); exit(EXIT_FAILURE);}

int write_to_file(char* filename) {

    struct stat file_info;
    filename[strlen(filename)-1]='\0';
    if (stat(filename,&file_info)==0) {  // if file exists we do check type
        if(!S_ISREG(file_info.st_mode)) {
            fprintf(stderr,"File is not a regular file");
            return EXIT_FAILURE;
        }
    }
    int fd=open(filename,O_CREAT|O_WRONLY|O_TRUNC,0600);
    if(fd==-1) {
        fprintf(stderr,"Cannot open a file!");
        return EXIT_FAILURE;
    }
    fprintf(stdout,"Write message you want to write into %s\n",filename);
    char * message;
    size_t message_size=0;

    if(getline(&message,&message_size,stdin)==-1)
        return EXIT_FAILURE;

    int length=strlen(message);
    for (int i=0;i<length;i++) {
        message[i]=toupper(message[i]);
    }
    if (dprintf(fd,"%s",message)==-1) {
        fprintf(stderr,"Cannot write to file!");
        return EXIT_FAILURE;
    }
    close(fd);
    free(message);
    return EXIT_SUCCESS;
}

int show_file_info(char* filename) {

    struct stat file_info;
    filename[strlen(filename)-1]='\0';
    stat(filename,&file_info);
    long int file_size=file_info.st_size;
    if(S_ISREG(file_info.st_mode)) {
        int fd=open(filename,O_RDONLY);
        char *buffer=malloc(file_size+1);
        read(fd,buffer,file_size);
        buffer[file_size]='\0';
        fprintf(stdout,"File content: %s\n",buffer);
        fprintf(stdout,"File size is %ld bytes\n",file_size);
        fprintf(stdout,"user id is %d\n",file_info.st_uid);
        fprintf(stdout,"group id is %d\n",file_info.st_gid);
        free(buffer);
    }
    else if (S_ISDIR(file_info.st_mode)) {
        DIR *dir =opendir(filename);
        if(dir==NULL) {
            fprintf(stderr,"Cannot open the directory!");
        }

        struct dirent *entry=readdir(dir);
        if(!entry) {
            fprintf(stderr,"Directory does not have files inside!");
            return EXIT_SUCCESS;
        }
        while (entry) {
            fprintf(stdout,"%s\n",entry->d_name);
            entry=readdir(dir);
        }
        closedir(dir);
    }
    else {
        fprintf(stderr,"File is neither a regular file nor a directory");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int recursive_ls(char* filename,int recursion) {
    if (filename==NULL) {
        return EXIT_SUCCESS;
    }
    struct stat file_info;
    if(stat(filename,&file_info)==-1) {
        fprintf(stderr,"Cannot call stat syscall on this file");
        return EXIT_FAILURE;
    }
    if (S_ISREG(file_info.st_mode)) {
        for (int i=0;i<recursion;i++) {
            printf(" ");
        }
        printf("%s\n",filename);
    }
    else if (S_ISDIR(file_info.st_mode)) {
        for (int i=0;i<recursion;i++) {
            printf(" ");
        }
        printf("+%s\n",filename);
        DIR *dir=opendir(filename);
        if(dir==NULL) {
            fprintf(stderr,"Cannot open the directory!");
            return EXIT_FAILURE;
        }
        struct dirent *entry=readdir(dir);
        if (!entry)
            ERR("Broken directory ;(");
        char*name=NULL;
        while (entry) {
            int needed=(int)(strlen(entry->d_name)+strlen(filename)+2);
            name=malloc(sizeof(char)*needed);
            strcpy(name,filename);
            if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..") ) {
                entry=readdir(dir);
                continue;
            }
            snprintf(name, needed, "%s/%s", filename, entry->d_name);
            if(recursive_ls(name,recursion+1)!=EXIT_SUCCESS) {
                free(name);
                name=NULL;
                closedir(dir);
                return EXIT_FAILURE;
            }
            entry=readdir(dir);
            free(name);
            name=NULL;
        }
        closedir(dir);
        if (name)
            free(name);
    }
    return EXIT_SUCCESS;
}


int interface()
{
    int choice=1;
    char input[4];
    int prog_state=EXIT_SUCCESS;
    do {
        fprintf(stdout,"Choose one of available commands:\n"
                       "A. write\n"
                       "B. show\n"
                       "C. walk\n"
                       "D. exit\n");
        fgets(input,4,stdin);
        if (strlen(input)!=2)  // input[0] should be equal to choise and input[0] to \n
            ERR("Invalid choice");
        switch (toupper(input[0])) {
            case 'A':
                char *filename=NULL;
                size_t filename_len;
                fprintf(stdout,"Enter the file name:\n");
                if(getline(&filename,&filename_len,stdin)!=-1) {
                    if(write_to_file(filename)==EXIT_FAILURE) {
                        prog_state=EXIT_FAILURE;
                        choice=0;
                    }
                    free(filename);
                }
                else {
                    ERR("Unexpected error. Cannot read input")
                }
                break;
            case 'B':
                char *filename_info=NULL;
                size_t filename_len_info;
                fprintf(stdout,"Enter the file name:\n");
                if(getline(&filename_info,&filename_len_info,stdin)!=-1) {
                    if(show_file_info(filename_info)==EXIT_FAILURE) {
                        prog_state=EXIT_FAILURE;
                        choice=0;
                    }
                    free(filename_info);
                }
                else {
                    ERR("Unexpected error. Cannot read input")
                }
                break;
            case 'C':
                char *filename_ls=NULL;
                size_t filename_len_ls;
                fprintf(stdout,"Enter the file name:\n");
                if(getline(&filename_ls,&filename_len_ls,stdin)!=-1) {
                    filename_ls[strlen(filename_ls)-1]='\0';
                    if(recursive_ls(filename_ls,0)==EXIT_FAILURE) {
                        prog_state=EXIT_FAILURE;
                        choice=0;
                    }
                    free(filename_ls);
                }
                else {
                    ERR("Unexpected error. Cannot read input")
                }
                break;
            case 'D':
                fprintf(stdout,"Gracefully stopping!");
                choice=0;
                break;
            default:
                ERR("Invalid choice");
        }
    }while (choice);
    return prog_state;
}

int main()
{
    return interface();
}