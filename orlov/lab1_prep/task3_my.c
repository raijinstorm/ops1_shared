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

void create_env(const char* path)
{
    if (mkdir(path, 0755) == -1)
        ERR("mkdir");

    FILE *f = fopen("requirements", "w");
    if (!f)
        ERR("creating requirements");
    fclose(f);
}


int main(int argc, char *argv[])
{
    int op_mode = -1;
    char* dir_paths[MAX_ENV_NUM];
    int env_cnt = 0;
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
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
        }
    }

    if (op_mode == 0)
        create_env(dir_paths[0]);

    return EXIT_SUCCESS;
}