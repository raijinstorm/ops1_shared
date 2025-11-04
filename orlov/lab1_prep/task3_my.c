#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
#define MAX_ENV_NUM 1024

char* join_paths(const char* path1, const char* path2)
{
    char* res;
    const int l1 = strlen(path1);
    if (path1[l1 - 1] == '/')
    {
        res = malloc(strlen(path1) + strlen(path2) + 1);
        if (!res)
            ERR("malloc");
        strcpy(res, path1);
    }
    else
    {
        res = malloc(strlen(path1) + strlen(path2) + 2);  // additional space for "/"
        if (!res)
            ERR("malloc");
        strcpy(res, path1);
        res[l1] = '/';
        res[l1 + 1] = 0;
    }
    return strcat(res, path2);
}


void create_env(const char* name)
{
    if (mkdir(name, 0755) == -1)
        ERR("mkdir");

    char* path2 = join_paths(name, "requirements");
    FILE *f = fopen(path2, "w");
    if (!f)
        ERR("creating requirements");
    free(path2);
    fclose(f);
}

void add_package(const char* env_name, const char* package)
{
    char* req_path = join_paths(env_name, "requirements");
    FILE *f = fopen(req_path, "r");
    if (!f)
    {
        fprintf(stderr, "the environment does not exist\n");
        free(req_path);
        return;
    }
    f = fopen(req_path, "a");
    fwrite(package, 1, strlen(package), f);

    free(req_path);
}


int main(int argc, char *argv[])
{
    int op_mode = -1;
    char* dir_paths[MAX_ENV_NUM];
    char* packages[MAX_ENV_NUM];
    int env_cnt = 0, package_cnt = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c" ) == 0) {
            if ( i + 1 >= argc)
                ERR("missing argument");
            op_mode = 0;
            dir_paths[0] = argv[i+1];
            env_cnt = 1;
            break;
        } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            dir_paths[env_cnt] = argv[++i];
            env_cnt++;
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            packages[package_cnt] = argv[++i];
            package_cnt++;
        }
        else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
        }
    }

    add_package(dir_paths[0], packages[0]);

    return EXIT_SUCCESS;
}