#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include<string.h>
#include <stdarg.h>

#define ERR(message){fprintf(stderr,"%s\n",message);exit(EXIT_FAILURE);}

enum mode{
    FILENAME = 0,
    EXTENSION = 1,
    TEXT=2
};

void clear_strings(enum mode *m,char** filename,char** file_mode,char** text) {
    *m=FILENAME;
    if (filename) {
        free(*filename);
        *filename=NULL;
    }
    if (file_mode) {
        free(*file_mode);
        *file_mode=NULL;
    }
    if (text) {
        free(*text);
        *text=NULL;
    }
}

int execute_command(enum mode *m,char* filename,char* file_mode,...) {
    //-----------------WRITE------------------
    if (!strcmp("write",file_mode)) {
        int descriptor=open(filename,O_CREAT | O_WRONLY | O_TRUNC,0600);
        if (descriptor<0) {
            char error_message[1024];
            snprintf(error_message,1024,"Error opening file \"%s\" for writing",filename);
            ERR(error_message);
        }
        va_list args;
        va_start(args,1);
        char *text=va_arg(args,char*);

        if (!text) {
            fprintf(stderr,"[WARNING] nothing to write to \"%s\"\n",filename);
            return 1;
        }
        dprintf(descriptor,"%s",text);
        close(descriptor);
    }
    //-----------------APPEND-----------------
    else if (!strcmp("append",file_mode)) {
        int descriptor=open(filename,O_CREAT | O_WRONLY | O_APPEND,0600);
        if (descriptor<0) {
            char error_message[1024];
            snprintf(error_message,1024,"Error openning file: %s",filename);
            ERR(error_message);
        }
        va_list args;
        va_start(args,1);
        char * text=va_arg(args,char*);
        if (!text) {
            ERR("Didn't get a needed parameter for appending");
        }
        dprintf(descriptor,"%s",text);
        close(descriptor);
    }
    //-----------------READ-----------------
    else if (!strcmp("read",file_mode)) {
        int descriptor=open(filename,O_RDONLY,0600);
        if (descriptor<0) {
            char error_message[1024];
            snprintf(error_message,1024,"Error opening file: %s",filename);
            ERR(error_message);
        }
        char buffer[1024];
        while (read(descriptor,buffer,sizeof(buffer))) {
            fprintf(stdout,buffer);
        }
        close(descriptor);
    }
    else {
        char error_message[1024];
        snprintf(error_message,1024,"%s is not a valid mode",file_mode);
        ERR(error_message);
    }
}

char * update_string(char * string,char new_char) {
    int position=0;
    if (!string) {
        string=malloc(sizeof(char)*2);
    }
    else {
        position=strlen(string);
        string=realloc(string,strlen(string)+1);
    }
    string[position]=new_char;
    string[position+1]='\0';
    return string;
}



int main() {
    fprintf(stdout,"enter sequence of words such that:\n"
                   "1st word is name of file with extensions(absolute/relative path)\n"
                   "2nd word is name of mode in which you want to open file\n"
                   "3d is optional and you should write it iff the mode is append/overwrite\n");


    char *filename = NULL;
    char *file_mode = NULL;
    char *text = NULL;
    char ch=' ';

    char *line=NULL;
    size_t line_size=0;

    enum mode modes = FILENAME;
    while (getline(&line,&line_size,stdin)!=-1) {

        for (int i=0;i<strlen(line);i++){
            ch=line[i];

            if (ch==EOF)
                break;

            if (ch=='\n' ||ch==' ' ) {
                if (filename) {
                    if (file_mode) {
                        if (!strcmp(file_mode,"read")) {
                            execute_command(&modes,filename,file_mode,text);
                            clear_strings(&modes,&filename,&file_mode,&text);
                            free(line);
                            line=NULL;
                            break;
                        }
                        if (!strcmp(file_mode,"write")||!strcmp(file_mode,"append")) {
                            if (text) {
                                execute_command(&modes,filename,file_mode,text);
                                clear_strings(&modes,&filename,&file_mode,&text);
                                free(line);
                                line=NULL;
                                break;
                            }
                            else {
                                modes=TEXT;
                                continue;
                            }
                            continue;
                        }
                    }
                    else {
                        modes=EXTENSION;
                        continue;
                    }
                }
                continue;
            }
            if (modes==FILENAME)
                filename=update_string(filename,ch);
            else if (modes==EXTENSION)
                file_mode=update_string(file_mode,ch);
            else if (modes==TEXT && ch=='"')
                while (1) {
                    i++;
                    ch=line[i];
                    if (ch==EOF)
                        ERR("incorrect string was pasted");
                    if (ch=='"')
                        break;
                    text=update_string(text,ch);
                }
        }
    }
    clear_strings(&modes,&filename,&file_mode,&text);
    return 0;
}