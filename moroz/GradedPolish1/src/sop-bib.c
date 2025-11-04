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

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

struct Book {
    char* author;
    char* title;
    char* genre;
}typedef book_t;

struct DBentry {
    int size;
    char title[64];
}typedef dbentry_t;

// join 2 path. returned pointer is for newly allocated memory and must be freed
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

void usage(int argc, char** argv)
{
    (void)argc;
    fprintf(stderr, "USAGE: %s path\n", argv[0]);
    exit(EXIT_FAILURE);
}

book_t get_contents(char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        ERR("fopen");
    }
    char* buffer = NULL;
    size_t cap = 0;
    ssize_t len = 0;
    char* author = NULL;
    char* genre = NULL;
    char* title = NULL;
    while ((len = getline(&buffer, &cap, f)) != -1) {
        if (len == 0) {
            break;
        }
        buffer[len - 1] = '\0';
        char* token = strtok(buffer, ":");
        if ((token = strtok(NULL, ":")) == NULL) {
            continue;
        }
        if (strcmp(buffer, "author") == 0) {
            author = strdup(token);
        }
        if (strcmp(buffer, "title") == 0) {
            title = strdup(token);
        }
        if (strcmp(buffer, "genre") == 0) {
            genre = strdup(token);
        }
    }
    fclose(f);
    free(buffer);
    if (author != NULL) {
        printf("author: %s\n", author);
    }
    else {
        printf("author: missing!\n");
    }
    if (title!=NULL) {
        printf("titile: %s\n", title);
    }
    else {
        printf("titile: missing!\n");
    }
    if (genre != NULL) {
        printf("genre: %s\n", genre);
    }
    else {
        printf("genre: missing!\n");
    }

    book_t book;
    book.author = author;
    book.title = title;
    book.genre = genre;
    return book;
}

int walk(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
    if (tflag == FTW_F) {
        char target[256];
        if (realpath(fpath, target) == NULL) {
            ERR("realpath");
        }
        int fd = open("index/by-visible-title", O_DIRECTORY|O_RDONLY);
        if (fd == -1) {
            ERR("open");
        }
        if (symlinkat(target, fd, &fpath[ftwbuf->base]) == -1) {
            ERR("symlinkat");
        }
        close(fd);

        book_t book = get_contents(target);
        char trun_title[65];
        if (book.title != NULL) {
            int fd = open("index/by-title", O_DIRECTORY|O_RDONLY);
            if (fd == -1) {
                ERR("open");
            }
            strncpy(trun_title, book.title, 64);
            trun_title[64] = '\0';
            char target[256];
            if (realpath(fpath, target) == NULL) {
                ERR("realpath");
            }
            if (symlinkat(target, fd, trun_title) == -1) {
                ERR("symlinkat");
            }
            close(fd);
        }

        char genre_trunc[65];
        if (book.genre != NULL) {
            char genre_path[128] = "index/by-genre/";
            strncpy(genre_trunc, book.genre, 64);
            genre_trunc[64] = '\0';
            strcat(genre_path, book.genre);
            int fd = open(genre_path, O_DIRECTORY|O_RDONLY);
            if (fd == -1) {
                ERR("open");
            }
            strncpy(trun_title, book.title, 64);
            trun_title[64] = '\0';
            char target[256];
            if (realpath(fpath, target) == NULL) {
                ERR("realpath");
            }
            if (symlinkat(target, fd, trun_title) == -1) {
                ERR("symlinkat");
            }
            close(fd);
        }
        free(book.title);
        free(book.genre);
        free(book.author);
    }
    return 0;
}

void read_db(char* path) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        ERR("open");
    }
    struct stat st;
    stat(path,&st);
    size_t size = st.st_size;
    if (size%68 != 0) {
        ERR("size is not a multiple of 68");
    }
    else {
        int count = size/68;
        struct iovec* storage = (struct iovec*)malloc(count*sizeof(struct iovec));
        dbentry_t* entry = malloc(count*sizeof(dbentry_t));
        for (int i=0;i<count;i++) {
            storage[i].iov_base = &entry[i];
            storage[i].iov_len = 68;
        }
        readv(fd, storage, count);
        if (chdir("index/by-title")) {
            ERR("chdir");
        }
        for (int i=0;i<count;i++) {
            struct stat st2;
            if (stat(entry[i].title, &st2) == -1) {
                if (errno == ENOENT) {
                    printf("Book %s is missing\n", entry[i].title);
                }
            }
            else {
                if (st2.st_size != entry[i].size) {
                    printf("Book %s size mismatch (%ld vs %d).\n", entry[i].title, st2.st_size, entry[i].size);
                }
            }
        }
        free(storage);
        free(entry);
    }
    close(fd);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        usage(argc, argv);
    }
    // get_contents(argv[1]);
    if (mkdir("index", 0755) == -1) {
        ERR("mkdir");
    }
    if (mkdir("index/by-visible-title", 0755) == -1) {
        ERR("mkdir");
    }
    if (mkdir("index/by-title", 0755) == -1) {
        ERR("mkdir");
    }
    if (mkdir("index/by-genre", 0755) == -1) {
        ERR("mkdir");
    }
    if (mkdir("index/by-genre/history", 0755) == -1) {
        ERR("mkdir");
    }
    if (mkdir("index/by-genre/comedy", 0755) == -1) {
        ERR("mkdir");
    }
    if (mkdir("index/by-genre/epic", 0755) == -1) {
        ERR("mkdir");
    }
    if (mkdir("index/by-genre/geography", 0755) == -1) {
        ERR("mkdir");
    }
    if (nftw("library",walk, 20, FTW_PHYS) != 0) {
        ERR("nftw");
    }
    read_db(argv[1]);
}
