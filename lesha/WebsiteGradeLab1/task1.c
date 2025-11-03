#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>


#define FILE_BUF_LEN 2

#define TASK4

int global_total_count = 0;
int global_dir_count = 0;
int global_reg_count = 0;
int global_link_count = 0;

int list_path_files_task1(char *path) {
    if (!path) {
        fprintf(stderr, "No path provided\n");
        return EXIT_FAILURE;
    }

    if (path[strlen(path)-1]=='\n') {
        path[strlen(path)-1]='\0';
    }
    struct stat file_stat;
    if (stat(path, &file_stat) == -1) {
        fprintf(stderr,"Error getting file stats");
        return EXIT_FAILURE;
    }

    if (!S_ISDIR(file_stat.st_mode)) {
        fprintf(stderr, "%s is not a directory\n", path);
        return EXIT_FAILURE;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr,"Error opening directory %s\n", path);
        return EXIT_FAILURE;
    }

    struct dirent *entry=readdir(dir);
    int total_count = 0;
    int dir_count = 0;
    int reg_count = 0;
    int link_count = 0;
    int other_count = 0;
    int unknown=0;
    while (entry) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            errno=0;
            if(lstat(entry->d_name, &file_stat)==-1) {
                fprintf(stderr,"Error stating file %s in %s directory\n", entry->d_name,path);
                entry=readdir(dir);
                total_count++;
                unknown++;
                continue;
            }
            if (S_ISDIR(file_stat.st_mode))
                dir_count++;
            else if (S_ISREG(file_stat.st_mode))
                reg_count++;
            else if (S_ISLNK(file_stat.st_mode))
                link_count++;
            else {
                if (errno!=0) {
                    fprintf(stderr,"Error reading file %s\n", entry->d_name);

                }
                other_count++;
            }

            total_count++;
        }
        entry=readdir(dir);
    }
    fprintf(stdout, "Total file count in dir %s: %d\n"
                    "\t1) Directories count: %d\n"
                    "\t2) Regular file count: %d\n"
                    "\t3) Link file count: %d\n"
                    "\t4) Other file count: %d\n"
                    "\t5) Unknown file count: %d\n",
                    path, total_count, dir_count, reg_count,
                    link_count, other_count,unknown);
    closedir(dir);
    return EXIT_SUCCESS;

}

int fn_walk_helper_task2(const char *fpath, const struct stat *sb,int typeflag, struct FTW *ftwbuf) {
    global_total_count++;
    const char *name=fpath+ftwbuf->base;
    if (!strcmp(".", name) || !strcmp("..", name)) {
        return 0;
    }

    switch (typeflag) {
        case FTW_D:
            global_dir_count++;
            break;
        case FTW_F:
            global_reg_count++;
            break;
        case FTW_SL:
            global_link_count++;
            break;
        default:
            fprintf(stderr,"Cannot access to file with path: %s\n",fpath);
            break;
    }
    return 0;

}

int walk_task2(const char *starting_dir) {
    global_total_count = 0;
    global_dir_count = 0;
    global_reg_count = 0;
    global_link_count = 0;
    struct stat start_dir_stat;
    char* dir_name=NULL;
    int length=strlen(starting_dir);
    if (starting_dir[length-1]=='\n') {
        dir_name=malloc(sizeof(char)*(length+1));
        strcpy(dir_name, starting_dir);
        if (!dir_name) {
            fprintf(stderr,"Error allocating directory name\n");
            exit(EXIT_FAILURE);
        }
        dir_name[length-1]='\0';
    }
    if (stat(dir_name, &start_dir_stat) == -1) {
        fprintf(stderr,"Error stating directory %s\n", dir_name);
        exit(EXIT_FAILURE);
    }
    if (!S_ISDIR(start_dir_stat.st_mode)) {
        fprintf(stderr,"%s is not a directory\n", dir_name);
        exit(EXIT_FAILURE);
    }
    int result=EXIT_FAILURE;
    if (!nftw(dir_name,fn_walk_helper_task2,10, FTW_PHYS))
        result= EXIT_SUCCESS;
    if (result==EXIT_SUCCESS)
        fprintf(stdout, "Total file count in dir %s: %d\n"
                        "1) Total link count: %d\n"
                        "2) Total directory count: %d\n"
                        "3) Total regular file count: %d\n",
                        dir_name,global_total_count,global_link_count,
                        global_dir_count,global_reg_count);
    if (dir_name)
        free(dir_name);
    return result;
}

int file_filler_task3(const char *path,const mode_t permissions,const int length) {

    umask(0777&(~permissions));
    FILE *file=fopen(path,"w+");
    int min=(int)'A';
    int helper=(int)('Z'-'A');
    fseek(file,length,SEEK_SET);
    fprintf(file,'\0');
    for(int i=0;i<length/10;i++) {
        fseek(file,random()%length,SEEK_SET);
        char rand_letter=(char)(random()%helper+min);
        fprintf(file,"%c",rand_letter);
    }
    fclose(file);
    return 0;
}

int file_buffer_read_helper_task4(const int fd, char *buffer, int buffer_length) {
    int last_read=0;
    int bytes_read=0;
    do{
        bytes_read=(int)read(fd,buffer,buffer_length);
        if (bytes_read==0) {
            return last_read;
        }
        if (bytes_read<0) {
            fprintf(stderr,"Error reading from file\n");
            exit(EXIT_FAILURE);
        }
        last_read+=bytes_read;
        buffer+=bytes_read;
        buffer_length-=bytes_read;
    }while (buffer_length!=0);
    return last_read;
}

int file_buffer_write_helper_task4(int fd, char *buffer, int buffer_length) {
    int last_write=0;
    int bytes_write=0;
    do{
        bytes_write=(int)write(fd,buffer,buffer_length);
        if (bytes_write==0) {
            return last_write;
        }
        if (bytes_write<0) {
            fprintf(stderr,"Error reading from file\n");
            exit(EXIT_FAILURE);
        }
        last_write+=bytes_write;
        buffer+=bytes_write;
        buffer_length-=bytes_write;
    }while (buffer_length!=0);
    return last_write;
}

int file_cloning_task4(char * source,char * dest) {
    if (!source || !dest) {
        fprintf(stderr,"No path provided\n");
        exit(EXIT_FAILURE);
    }

    if (source[strlen(source)-1]=='\n') {
        source[strlen(source)-1]='\0';
    }
    if (dest[strlen(dest)-1]=='\n') {
        dest[strlen(dest)-1]='\0';
    }
    struct stat source_stat;
    if (stat(source, &source_stat) == -1) {
        fprintf(stderr,"Error stating source file %s\n", source);
        exit(EXIT_FAILURE);
    }

    int source_fd=open(source,O_RDONLY);
    if (source_fd==-1) {
        fprintf(stderr,"Error opening source file %s\n", source);
        exit(EXIT_FAILURE);
    }

    int dest_fd=open(dest,O_WRONLY|O_CREAT|O_TRUNC|O_CREAT,0666);
    if (dest_fd==-1) {
        fprintf(stderr,"Error opening destination file %s\n", dest);
        exit(EXIT_FAILURE);
    }
    char buffer[FILE_BUF_LEN];
    int size;
    while ((size=file_buffer_read_helper_task4(source_fd,buffer,FILE_BUF_LEN))) {
        if (size==0)
            break;
        file_buffer_write_helper_task4(dest_fd,buffer,size);
    }
    close(dest_fd);
    close(source_fd);
    return EXIT_SUCCESS;
}


int main(int argc,char **argv)
{
//--------------------1-TASK---------------
#ifdef TASK1

    if (argc<2){
        fprintf(stderr,"No directories passed\n");
    }
    for(int i=1;i<argc;i++) {
        int result=list_path_files_task1(argv[i]);
        if (result==EXIT_FAILURE) {
            fprintf(stderr,"Error listing files in %s directory\n",argv[i]);
        }
    }
#endif

//--------------------2-TASK---------------
#ifdef TASK2
    char*filename=NULL;
    size_t size=0;
    fprintf(stdout,"Write a path to directory:\n");
    if (getline(&filename,&size,stdin)==-1) {
        fprintf(stderr,"Error reading file %s\n", filename);
        return EXIT_FAILURE;
    }
    walk_task2(filename);

#endif

#ifdef TASK3
    char *file_path=NULL;
    mode_t octal_permissions=0;
    int file_size=0;
    for (int i=1;i<argc;i+=2) {
        if (argv[i][0]!='-')
            return EXIT_FAILURE;
        switch (argv[i][1]) {
            case 'n':
                file_path=argv[i+1];
                break;
            case 'p':
                octal_permissions=strtol(argv[i+1],NULL,8);
                break;
            case 's':
                file_size=strtol(argv[i+1],NULL,10);
                break;
            default:
                fprintf(stderr,"[WARNING] Unrecognized flag %s passed\n."
                               "Gracefully skipping)",argv[i]);
        }
    }
    if (!file_path || !file_size || !octal_permissions) {
        fprintf(stderr,"[ERROR] Not all parameters were passed\n");
        return EXIT_FAILURE;
    }
    file_filler_task3(file_path,octal_permissions,file_size);
#endif

#ifdef TASK4
    char* source_file_path=NULL;
    char* dest_file_path=NULL;
    size_t source_size=0;
    size_t dest_size=0;
    fprintf(stdout,"Write source file path:\n");
    getline(&source_file_path,&source_size,stdin);
    fprintf(stdout,"Write dest file path:\n");
    getline(&dest_file_path,&dest_size,stdin);
    file_cloning_task4(source_file_path,dest_file_path);
    free(source_file_path);
    free(dest_file_path);
#endif
    return  EXIT_SUCCESS;
}