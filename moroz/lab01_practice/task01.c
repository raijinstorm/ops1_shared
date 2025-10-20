#include <ctype.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<fcntl.h>
#include<errno.h>
#include<string.h>

enum Operation {
    OPEN,
    WRITE
};

int expand(char*** arr, size_t* capacity) {
    *capacity = (*capacity)==0?4:(*capacity*2);
    char** new_result = (char**)realloc(*arr, *capacity*sizeof(char*));
    if (!new_result) {
        return -1;
    }
    *arr = new_result;
    return 0;
}

char** split(char* buffer, size_t* current) {
    size_t buffer_len = strlen(buffer);
    size_t start = 0, end = 0;
    char** result = NULL;
    size_t result_capacity = 0;

    size_t i = 0;
    while (i<buffer_len && isspace(buffer[i])) i++;

    while (i<buffer_len) {
        start = i;
        while (i<buffer_len && !isspace(buffer[i])) i++;
        end = i;
        size_t current_len = end - start;
        char* new_element = (char*)malloc(current_len + 1);
        if (!new_element) {
            for (int j=0;j<result_capacity;j++) {
                free(result[j]);
            }
            free(result);
            return NULL;
        }
        memcpy(new_element, buffer + start, current_len);
        new_element[current_len] = '\0';
        if ((*current)+1>result_capacity ) {
            if (expand(&result, &result_capacity) == -1) {
                for (int j=0;j<result_capacity;j++) {
                    free(result[j]);
                }
                free(result);
                return NULL;
            }
        }
        result[*current] = new_element;
        (*current)+=1;

        while (i<buffer_len && isspace(buffer[i])) i++;
    }
    return result;
}

//TODO write validation of input; depending on input open a file + read or write

int main() {
    char* buf = NULL;
    size_t size = 0;
    fprintf(stdout, "Enter the <name of the file> <operation>(read/write): ");
    ssize_t len = getline(&buf, &size,stdin);
    if (len == -1) {
        fprintf(stderr, "Error reading from stdin %s", strerror(errno));
        return 1;
    }

    size_t num_of_parameters = 0;
    char** parameters = split(buf, &num_of_parameters);
    for (int i=0;i<num_of_parameters;i++) {
        printf("%s\n", parameters[i]);
    }

    if (num_of_parameters==1) {
        fprintf(stdout, "You need to enter what you want to do with the file!");
        return 1;
    }
    if (num_of_parameters>2) {
        fprintf(stdout, "Too many parameters, there should be 2!");
        return 1;
    }

    fprintf(stdout, "%s", buf);
    free(buf);

    if (strcmp(parameters[1], "read") == 0) {
        char buf1[4096];
        int fd = open(parameters[0], O_RDONLY);
        if (fd == -1) {
            close(fd);
            perror("open");
        }
        while (1) {
            ssize_t len = read(fd, buf1, sizeof(buf1));
            if (len>0) {
                size_t offset = 0;
                while (offset<len) {
                    ssize_t w = write(STDOUT_FILENO, buf1+offset, len-offset);
                    if (w == -1) {
                        perror("write");
                        close(fd);
                    }
                    offset += w;
                }
            }
            else if (len == 0) {
                break;
            }
            else {
                perror("read");
                close(fd);
            }
        }
        close(fd);
    }

    else if (strcmp(parameters[1],  "write") == 0) {
        char* file_input = NULL;
        size_t input_size = 0;
        int fd = open(parameters[0], O_WRONLY | O_CREAT | O_APPEND, 0640);
        while (1) {
            int n = getline(&file_input, &input_size, stdin);
            if (n<=0) {
                break;
            }
            if (file_input[0] == '\n') {
                break;
            }

            if (fd == -1) {
                perror("open");
            }
            ssize_t offset = 0;
            while (offset<n) {
                ssize_t w = write(fd, file_input+offset, n - offset);
                if (w == -1) {
                    perror("write");
                    close(fd);
                    free(file_input);
                }
                offset+=w;
            }
        }
        close(fd);
    }

    // else if (strcmp(parameters[1], "delete") == 0) {
    //
    // }

    //freeing parameters
    for (int i=0;i<num_of_parameters;i++) {
        free(parameters[i]);
    }
    free(parameters);
    return 0;
}
