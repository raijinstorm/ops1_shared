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

#define ERR(s) (perror(s), exit(EXIT_FAILURE))
#define MAX_LEN 64

char *join_paths(const char *a, const char *b) {
    int l = strlen(a);
    char *r = malloc(l + strlen(b) + 2);
    if (!r) ERR("malloc");
    strcpy(r, a);
    if (r[l - 1] != '/') r[l++] = '/';
    strcpy(r + l, b);
    return r;
}

typedef struct {
    char *author, *title, *genre;
} Meta;

void free_meta(Meta *m) {
    free(m->author);
    free(m->title);
    free(m->genre);
}

void read_meta(const char *path, Meta *m) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *line = NULL, *token;
    size_t n = 0;
    while (getline(&line, &n, f) != -1) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[len - 1] = 0;
        token = strchr(line, ':');
        if (!token) continue;
        *token++ = 0;
        if (strcmp(line, "author") == 0) m->author = strdup(token);
        else if (strcmp(line, "title") == 0) m->title = strdup(token);
        else if (strcmp(line, "genre") == 0) m->genre = strdup(token);
    }
    free(line);
    fclose(f);
}

int make_dir(const char *p) {
    if (mkdir(p, 0777) < 0) return -1;
    return 0;
}

int fd_index = -1;
char cwd[PATH_MAX];

int make_link(const char *src, const char *dst) {
    return symlink(src, dst);
}

int ftw_func(const char *fpath, const struct stat *sb, int type, struct FTW *ftwbuf) {
    if (type != FTW_F) return 0;
    const char *name = fpath + ftwbuf->base;
    char *meta_path = strdup(fpath);
    Meta m = {0};
    read_meta(fpath, &m);

    // stage 2: by-visible-title
    char *dst1 = join_paths("index/by-visible-title", name);
    if (access(dst1, F_OK) == 0) {
        fprintf(stderr, "duplicate name: %s\n", name);
        exit(EXIT_FAILURE);
    }
    if (make_link(fpath, dst1) < 0) ERR("symlink");
    free(dst1);

    // stage 3: by-title
    if (m.title) {
        char title[MAX_LEN + 1] = {0};
        strncpy(title, m.title, MAX_LEN);
        char *dst2 = join_paths("index/by-title", title);
        if (access(dst2, F_OK) == 0) {
            fprintf(stderr, "duplicate title: %s\n", title);
            exit(EXIT_FAILURE);
        }
        if (make_link(fpath, dst2) < 0) ERR("symlink");
        free(dst2);

        // by-genre
        if (m.genre) {
            char genre[MAX_LEN + 1] = {0};
            strncpy(genre, m.genre, MAX_LEN);
            char *gdir = join_paths("index/by-genre", genre);
            mkdir("index/by-genre", 0777);
            mkdir(gdir, 0777);
            char *dst3 = join_paths(gdir, title);
            if (make_link(fpath, dst3) < 0) ERR("symlink");
            free(dst3);
            free(gdir);
        }
    }

    free(meta_path);
    free_meta(&m);
    return 0;
}

void stage2_3() {
    if (make_dir("index") < 0) ERR("mkdir index");
    if (make_dir("index/by-visible-title") < 0) ERR("mkdir by-visible-title");
    if (make_dir("index/by-title") < 0) ERR("mkdir by-title");
    nftw("library", ftw_func, 20, FTW_PHYS);
}

typedef struct {
    unsigned int size;
    char title[MAX_LEN + 1];
} Record;

void stage4(const char *db_path) {
    int fd = open(db_path, O_RDONLY);
    if (fd < 0) ERR("open db");
    struct stat st;
    if (fstat(fd, &st) < 0) ERR("stat");
    int count = st.st_size / (4 + MAX_LEN);
    for (int i = 0; i < count; i++) {
        Record r = {0};
        char raw[MAX_LEN];
        struct iovec v[2] = {{&r.size, 4}, {raw, MAX_LEN}};
        if (readv(fd, v, 2) != (ssize_t)(4 + MAX_LEN)) break;
        memcpy(r.title, raw, MAX_LEN);
        r.title[MAX_LEN] = 0;

        char *p = join_paths("index/by-title", r.title);
        struct stat sb;
        if (stat(p, &sb) < 0) printf("Book \"%s\" is missing\n", r.title);
        else if ((unsigned int)sb.st_size != r.size)
            printf("Book \"%s\" size mismatch (%u vs %ld)\n", r.title, r.size, sb.st_size);
        free(p);
    }
    close(fd);
}

int main(int argc, char **argv) {
    if (argc == 2) stage4(argv[1]);
    else stage2_3();
    return 0;
}
