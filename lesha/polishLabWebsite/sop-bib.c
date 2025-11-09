#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#define ERR(source) \
    (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

// join 2 paths. returned pointer is newly allocated and must be freed
char *join_paths(const char *path1, const char *path2) {
    char *res;
    const int l1 = strlen(path1);

    if (path1[l1 - 1] == '/') {
        res = malloc(strlen(path1) + strlen(path2) + 1);
        if (!res)
            ERR("malloc");
        strcpy(res, path1);
    } else {
        // extra space for '/'
        res = malloc(strlen(path1) + strlen(path2) + 2);
        if (!res)
            ERR("malloc");
        strcpy(res, path1);
        res[l1] = '/';
        res[l1 + 1] = 0;
    }

    return strcat(res, path2);
}

void print_info(const char *path) {
    FILE *file_on_path = fopen(path, "r");
    if (!file_on_path)
        ERR("fopen");

    char *line = NULL;
    size_t line_size = 0;
    int author_found = 0;
    int title_found = 0;
    int genre_found = 0;

    while (getline(&line, &line_size, file_on_path) != -1) {
        if (strstr(line, "author") == &line[0]) {
            int i;
            for (i = strlen("author"); i < strlen(line); i++) {
                if ((line[i] != ' ' && line[i] != ':') && i==strlen("author"))
                    goto skip_author;
                if (line[i] == ' ' || line[i] == ':')
                    continue;
                else
                    break;
            }
            fprintf(stdout, "\nauthor: ");
            for (; i < strlen(line); i++)
                fprintf(stdout, "%c", line[i]);
            puts("");
            author_found = 1;
        }

        skip_author:
        if (strstr(line, "title") == &line[0]) {
            int i;
            for (i = strlen("title"); i < strlen(line); i++) {
                if ((line[i] != ' ' && line[i] != ':') && i==strlen("title"))
                    goto skip_title;
                if (line[i] == ' ' || line[i] == ':')
                    continue;
                else
                    break;
            }
            fprintf(stdout, "\ntitle: ");
            for (; i < strlen(line); i++)
                fprintf(stdout, "%c", line[i]);
            puts("");
            title_found = 1;
        }

        skip_title:
        if (strstr(line, "genre") == &line[0]) {
            int i;
            for (i = strlen("genre"); i < strlen(line); i++) {
                if ((line[i] != ' ' && line[i] != ':') && i==strlen("genre"))
                    goto skip_genre;
                if (line[i] == ' ' || line[i] == ':')
                    continue;
                else
                    break;
            }
            fprintf(stdout, "\ngenre: ");
            for (; i < strlen(line); i++)
                fprintf(stdout, "%c", line[i]);
            puts("");
            genre_found = 1;
        }
        skip_genre:
    }

    if (line) {
        free(line);
        line = NULL;
    }

    if (!author_found)
        fprintf(stdout, "author not found\n");
    if (!title_found)
        fprintf(stdout, "title not found\n");
    if (!genre_found)
        fprintf(stdout, "genre not found\n");

    fclose(file_on_path);
}


int fn_helper(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    char* final_path=join_paths("/home/aleksejkudelka4/OPS/nagibator228/ops1_shared/"
                                "lesha/polishLabWebsite/index/by-visible-title",fpath+ftwbuf->base);
    if (typeflag == FTW_F) {
        symlink(fpath,final_path);
    }
    if (final_path)
        free(final_path);
    return 0;
}

int create_symlinks_recursively(char *  lib_path){
    char * dir_name = "index";
    char * subdir_name="index/by-visible-title";
    if (mkdir(dir_name,0777)==-1 || mkdir(subdir_name,0777)==-1)
        ERR("mkdir");
    struct stat dir_stat;
    if (stat(subdir_name,&dir_stat)==-1)
        ERR("stat");
    nftw(lib_path,fn_helper,10,FTW_PHYS);
    return 0;
}

void usage(int argc, char **argv) {
    (void)argc;
    fprintf(stderr, "USAGE: %s path\n", argv[0]);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    if (argc < 2)
        usage(argc, argv);

    for (int i = 1; i < argc; i++) {
        print_info(argv[i]);
        puts("\n\n\n\n");
    }

    create_symlinks_recursively("/home/aleksejkudelka4/OPS/nagibator228/ops1_shared/lesha/polishLabWebsite/library");

    return EXIT_SUCCESS;
}
