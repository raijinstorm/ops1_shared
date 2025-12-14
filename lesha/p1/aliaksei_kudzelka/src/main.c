#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <stdbool.h>
#include <limits.h>
#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

static volatile sig_atomic_t exit_requested = 0;

FILE * logger=NULL;
static struct BackupSource *backups = NULL;
static size_t  backup_count = 0;
static size_t  backup_capacity = 0;

/*Code from this website : https://benhoyt.com/writings/hash-table-in-c/#:~:text=%23define%20FNV_OFFSET%2014695981039346656037UL,hash%3B%0A%7D
 where guy implemented his hashmap in c  */

// Return 64-bit FNV-1a hash for key (NUL-terminated). See description:
// https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
static uint64_t hash_key(const char* key) {
    uint64_t hash = FNV_OFFSET;
    for (const char* p = key; *p; p++) {
        hash ^= (uint64_t)(unsigned char)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}


struct BackupTarget {
    char *target_path;
    pid_t worker_pid;
    int inotify_fd;
    int active;   // 1 = running, 0 = stopped
};

struct BackupSource {
    char *source_path;

    struct BackupTarget *targets;
    size_t target_count;
    size_t target_capacity;
};

static void on_signal(int signo) {
    (void) signo;
    exit_requested = 1;
}


static void log_printf(const char *fmt, ...) {
    va_list args;
    /* stdout */
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    /* log file */
    if (logger) {
        va_start(args, fmt);
        vfprintf(logger, fmt, args);
        fflush(logger);
        va_end(args);
    }
}

static void print_banner(void) {
    log_printf("\n");
    log_printf("============================================================\n");
    log_printf(" Interactive Backup Management System\n");
    log_printf("------------------------------------------------------------\n");
    log_printf(" \"There are those who make backups and those who will\"\n");
    log_printf("============================================================\n");
    log_printf("\n");
}

static void print_prompt(void) {
    printf("backup> ");
    fflush(stdout); /* prompt must appear immediately */
}

static int is_separator(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

static char *trim_trailing_slash(char *path) {
    size_t len = strlen(path);
    while (len > 1 && path[len-1] == '/') {
        path[len-1] = '\0';
        len--;
    }
    return path;
}

/* ---------- Status messages ---------- */

static void msg_backup_started(const char *src) {
    log_printf("[OK] Backup started for source: %s\n", src);
}

static void msg_target_added(const char *target) {
    log_printf("     -> Target added: %s\n", target);
}

static void msg_backup_stopped(const char *src, const char *target) {
    log_printf("[OK] Backup stopped: %s -> %s\n", src, target);
}

static void msg_restore_started(const char *src, const char *target) {
    log_printf("[INFO] Restoring backup:\n");
    log_printf("       Source : %s\n", src);
    log_printf("       Backup : %s\n", target);
}

static void msg_restore_finished() {
    log_printf("[OK] Restore completed successfully.\n");
}

/* ---------- List output ---------- */

static void print_active_list_header() {
    log_printf("\nActive backups:\n");
    log_printf("------------------------------------------------------------\n");
}

static void print_inctive_list_header() {
    log_printf("\nInctive backups:\n");
    log_printf("------------------------------------------------------------\n");
}

static void print_list_entry(const char *src, const char *target) {
    log_printf("  %s  ->  %s\n", src, target);
}

static void print_list_empty() {
    log_printf("No active backups.\n");
}

static void handle_list(void) {
    if (backup_count == 0 ) {
        print_list_empty();
        return;
    }

    print_active_list_header();

    for (size_t i = 0; i < backup_count; i++) {
        struct BackupSource *b = &backups[i];
        for (size_t j = 0; j < b->target_count; j++) {
            if (b->targets[j].active==1)
                print_list_entry(b->source_path, b->targets[j].target_path);
        }
    }

    print_inctive_list_header();

    for (size_t i = 0; i < backup_count; i++) {
        struct BackupSource *b = &backups[i];
        for (size_t j = 0; j < b->target_count; j++) {
            if (b->targets[j].active==0)
                print_list_entry(b->source_path, b->targets[j].target_path);
        }
    }
}

/* ---------- Error messages ---------- */
static void err_unknown_command() {
    log_printf("[ERROR] Unknown command. Type 'help' to see available commands.\n");
}

static void err_invalid_arguments(void) {
    log_printf("[ERROR] Invalid arguments.\n");
}

static void err_target_not_empty(const char *path) {
    log_printf("[ERROR] Target directory is not empty: %s\n", path);
}

static void err_backup_exists(const char *src, const char *target) {
    log_printf("[ERROR] Backup already exists: %s -> %s\n", src, target);
}

static void err_restore_blocked(void) {
    log_printf("[ERROR] Restore failed.\n");
}

static void err_file_open(const char *path) {
    log_printf("[ERROR] Cannot open file: %s\n", path);
}

static void err_directory_does_not_exist(const char *path) {
    log_printf("[ERROR] Cannot find directory: %s\n", path);
}

static void err_path_inside(const char *src, const char *target) {
    log_printf("[ERROR] Cannot create backup inside source. Source: %s Target: %s\n", src, target);
}

/* ---------- Exit ---------- */

static void msg_exit(void) {
    log_printf("\nShutting down...\n");
    log_printf("Cleaning up resources and terminating child processes.\n");
    log_printf("Goodbye.\n\n");
}

static void cleanup(void) {
    for (size_t i = 0; i < backup_count; i++) {
        struct BackupSource *bs = &backups[i];
        for (size_t j = 0; j < bs->target_count; j++) {
            if (bs->targets[j].active) {
                kill(bs->targets[j].worker_pid, SIGTERM);
                waitpid(bs->targets[j].worker_pid, NULL, 0);
                bs->targets[j].active = 0;
            }
            free(bs->targets[j].target_path);
        }
        free(bs->targets);
        free(bs->source_path);
    }
    free(backups);
    backups = NULL;
    backup_count = backup_capacity = 0;
    if (logger)
        fclose(logger);
}

/* ---------- Handler Helpers ---------- */

static void ensure_backup_capacity(void) {
    if (backup_count < backup_capacity)
        return;

    size_t new_cap = (backup_capacity == 0) ? 4 : backup_capacity * 2;
    struct BackupSource *tmp =
        realloc(backups, new_cap * sizeof(struct BackupSource));

    if (!tmp) {
        perror("realloc");
        exit(1);
    }

    backups = tmp;
    backup_capacity = new_cap;
}

static struct BackupSource *find_backup(const char *source) {
    for (size_t i = 0; i < backup_count; i++) {
        if (strcmp(backups[i].source_path, source) == 0)
            return &backups[i];
    }
    return NULL;
}

static int canonical_path(const char *path, char *out, size_t out_sz) {
    char *resolved = realpath(path, NULL);
    if (resolved) {
        snprintf(out, out_sz, "%s", resolved);
        free(resolved);
        trim_trailing_slash(out);
        return 0;
    }

    /* path may not exist yet - resolve parent */
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash) {
        /* relative simple name */
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd)))
            return -1;
        snprintf(out, out_sz, "%s/%s", cwd, path);
        trim_trailing_slash(out);
        return 0;
    }

    *slash = '\0';
    char *parent = tmp;
    char *name = slash + 1;

    char *parent_real = realpath(parent, NULL);
    if (!parent_real)
        return -1;
    snprintf(out, out_sz, "%s/%s", parent_real, name);
    free(parent_real);
    trim_trailing_slash(out);
    return 0;
}

static int backup_exists(const char *source, const char *target) {
    for (size_t i = 0; i < backup_count; i++) {
        struct BackupSource *b = &backups[i];
        if (strcmp(b->source_path, source) != 0)
            continue;

        for (size_t j = 0; j < b->target_count; j++) {
            if (strcmp(b->targets[j].target_path, target) == 0) {
                if (b->targets[j].active==0) {
                    return 2;
                }
                return 1;
            }
        }
    }
    return 0;
}

static void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p) {
        perror("realloc");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s) {
    char *dup = strdup(s);
    if (!dup) {
        perror("strdup");
        exit(1);
    }
    return dup;
}

static int make_dir_recursive(const char *path, mode_t mode) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (strlen(tmp) > 0 && mkdir(tmp, mode) == -1 && errno != EEXIST)
                return -1;
            tmp[i] = '/';
        }
    }

    if (mkdir(tmp, mode) == -1 && errno != EEXIST)
        return -1;
    return 0;
}

static int directory_empty(const char *path) {
    DIR *d = opendir(path);
    if (!d)
        return -1;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        closedir(d);
        return 0; /* not empty */
    }
    closedir(d);
    return 1; /* empty */
}

static int is_subpath(const char *parent, const char *child) {
    size_t len = strlen(parent);
    if (strncmp(parent, child, len) != 0)
        return 0;
    return child[len] == '/' || child[len] == '\0';
}

static int copy_file_contents(const char *src, const char *dst, mode_t mode) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0)
        return -1;

    int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }

    char buf[8192];
    ssize_t r;
    while ((r = read(in_fd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out_fd, buf + off, r - off);
            if (w < 0) {
                close(in_fd);
                close(out_fd);
                return -1;
            }
            off += w;
        }
    }

    close(in_fd);
    close(out_fd);
    return (r < 0) ? -1 : 0;
}

static int adjust_symlink_target(const char *source_root, const char *target_root,
                                 const char *link_target, char *buffer, size_t buf_size) {
    if (link_target[0] != '/') {
        snprintf(buffer, buf_size, "%s", link_target);
        return 0;
    }

    size_t src_len = strlen(source_root);
    if (strncmp(link_target, source_root, src_len) == 0) {
        snprintf(buffer, buf_size, "%s%s", target_root, link_target + src_len);
        return 0;
    }

    snprintf(buffer, buf_size, "%s", link_target);
    return 0;
}

static int copy_entry(const char *source_root, const char *target_root,
                      const char *src_path, const char *dst_path);

static int copy_symlink(const char *source_root, const char *target_root,
                        const char *src_path, const char *dst_path) {
    char buf[4096];
    ssize_t len = readlink(src_path, buf, sizeof(buf) - 1);
    if (len < 0)
        return -1;
    buf[len] = '\0';

    char adjusted[4096];
    if (adjust_symlink_target(source_root, target_root, buf, adjusted, sizeof(adjusted)) != 0)
        return -1;

    unlink(dst_path);
    return symlink(adjusted, dst_path);
}

static int copy_directory(const char *source_root, const char *target_root,
                          const char *src_path, const char *dst_path) {
    if (mkdir(dst_path, 0755) == -1 && errno != EEXIST)
        return -1;

    DIR *d = opendir(src_path);
    if (!d)
        return -1;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char child_src[4096];
        char child_dst[4096];
        snprintf(child_src, sizeof(child_src), "%s/%s", src_path, de->d_name);
        snprintf(child_dst, sizeof(child_dst), "%s/%s", dst_path, de->d_name);

        if (copy_entry(source_root, target_root, child_src, child_dst) != 0) {
            closedir(d);
            return -1;
        }
    }

    closedir(d);
    return 0;
}

static int copy_entry(const char *source_root, const char *target_root,
                      const char *src_path, const char *dst_path) {
    struct stat st;
    if (lstat(src_path, &st) == -1)
        return -1;

    if (S_ISLNK(st.st_mode))
        return copy_symlink(source_root, target_root, src_path, dst_path);
    else if (S_ISDIR(st.st_mode))
        return copy_directory(source_root, target_root, src_path, dst_path);
    else if (S_ISREG(st.st_mode))
        return copy_file_contents(src_path, dst_path, st.st_mode);
    return 0;
}

struct WatchEntry {
    int wd;
    char path[4096];
    struct WatchEntry *next;
};

static struct WatchEntry *watch_list_append(struct WatchEntry **head, int wd, const char *path) {
    struct WatchEntry *node = malloc(sizeof(struct WatchEntry));
    if (!node)
        return NULL;
    node->wd = wd;
    snprintf(node->path, sizeof(node->path), "%s", path);
    node->next = *head;
    *head = node;
    return node;
}

static void watch_list_free(struct WatchEntry *head) {
    while (head) {
        struct WatchEntry *tmp = head->next;
        free(head);
        head = tmp;
    }
}

static const char *watch_list_find(struct WatchEntry *head, int wd) {
    for (struct WatchEntry *n = head; n; n = n->next) {
        if (n->wd == wd)
            return n->path;
    }
    return NULL;
}

static int remove_path_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) == -1)
        return (errno == ENOENT) ? 0 : -1;

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d)
            return -1;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
            if (remove_path_recursive(child) != 0) {
                closedir(d);
                return -1;
            }
        }
        closedir(d);
        if (rmdir(path) != 0)
            return -1;
    } else {
        if (unlink(path) != 0)
            return -1;
    }
    return 0;
}

static int watch_directory_tree(int fd, const char *path, struct WatchEntry **list) {
    int wd = inotify_add_watch(fd, path,
                               IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM |
                                   IN_MOVED_TO | IN_ATTRIB | IN_DELETE_SELF | IN_CLOSE_WRITE |
                                   IN_MOVE_SELF);
    if (wd < 0)
        return -1;
    if (!watch_list_append(list, wd, path))
        return -1;

    DIR *d = opendir(path);
    if (!d)
        return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
        if (de->d_type == DT_DIR) {
            watch_directory_tree(fd, child, list);
        }
    }
    closedir(d);
    return 0;
}

static int sync_directories(const char *source_root, const char *target_root) {
    struct stat st;
    if (stat(source_root, &st) == -1)
        return -1;
    if (!S_ISDIR(st.st_mode))
        return -1;
    if (mkdir(target_root, 0755) == -1 && errno != EEXIST)
        return -1;
    return copy_directory(source_root, target_root, source_root, target_root);
}

static void relative_from_root(const char *root, const char *path, char *out, size_t out_sz) {
    size_t root_len = strlen(root);
    if (strncmp(root, path, root_len) == 0) {
        const char *p = path + root_len;
        if (*p == '/')
            p++;
        snprintf(out, out_sz, "%s", p);
    } else {
        snprintf(out, out_sz, "%s", path);
    }
}

static void mirror_event_loop(const char *source_root, const char *target_root) {
    int fd = inotify_init();
    if (fd < 0)
        _exit(1);

    struct WatchEntry *watchers = NULL;
    if (watch_directory_tree(fd, source_root, &watchers) != 0) {
        watch_list_free(watchers);
        close(fd);
        _exit(1);
    }

    char buf[4096];
    while (1) {
        if (exit_requested>0) {
            watch_list_free(watchers);
            close(fd);
            exit(0);
        }
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EINTR) {
                errno=0;
                continue;
            }
            break;
        }

        ssize_t offset = 0;
        while (offset < len) {
            if (exit_requested>0) {
                watch_list_free(watchers);
                close(fd);
                exit(0);
            }
            struct inotify_event *ev = (struct inotify_event *)(buf + offset);
            const char *base = watch_list_find(watchers, ev->wd);
            if (!base) {
                offset += sizeof(struct inotify_event) + ev->len;
                continue;
            }

            char src_path[4096];
            if (ev->len && ev->name[0])
                snprintf(src_path, sizeof(src_path), "%s/%s", base, ev->name);
            else
                snprintf(src_path, sizeof(src_path), "%s", base);

            if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                /* source removed; stop worker */
                watch_list_free(watchers);
                close(fd);
                _exit(0);
            }

            char rel[4096];
            relative_from_root(source_root, src_path, rel, sizeof(rel));
            char dst_path[4096];
            int n;
            if (strlen(rel) > 0)
                n = snprintf(dst_path, sizeof(dst_path), "%s/%s", target_root, rel);
            else
                n = snprintf(dst_path, sizeof(dst_path), "%s", target_root);
            if (n < 0 || (size_t)n >= sizeof(dst_path)) {
                offset += sizeof(struct inotify_event) + ev->len;
                continue;
            }

            if (ev->mask & (IN_DELETE | IN_MOVED_FROM)) {
                remove_path_recursive(dst_path);
            } else {
                char *slash = strrchr(dst_path, '/');
                if (slash) {
                    *slash = '\0';
                    make_dir_recursive(dst_path, 0755);
                    *slash = '/';
                }

                if (ev->mask & IN_ISDIR) {
                    copy_directory(source_root, target_root, src_path, dst_path);
                    watch_directory_tree(fd, src_path, &watchers);
                } else {
                    copy_entry(source_root, target_root, src_path, dst_path);
                }
            }

            offset += sizeof(struct inotify_event) + ev->len;
        }
    }

    watch_list_free(watchers);
    close(fd);
    _exit(1);
}

static int remove_if_missing(const char *source_root, const char *target_root,
                             const char *src_path, const char *rel_path) {
    char target_path[4096];
    snprintf(target_path, sizeof(target_path), "%s/%s", target_root, rel_path);

    struct stat st;
    if (lstat(target_path, &st) == -1) {
        /* target side missing => remove from source */
        if (lstat(src_path, &st) == -1)
            return 0;

        if (S_ISDIR(st.st_mode)) {
            DIR *d = opendir(src_path);
            if (d) {
                struct dirent *de;
                while ((de = readdir(d)) != NULL) {
                    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                        continue;
                    char child_src[4096];
                    char child_rel[4096];
                    snprintf(child_src, sizeof(child_src), "%s/%s", src_path, de->d_name);
                    snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_path, de->d_name);
                    remove_if_missing(source_root, target_root, child_src, child_rel);
                }
                closedir(d);
            }
            rmdir(src_path);
        } else {
            unlink(src_path);
        }
        return 0;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(src_path);
        if (!d)
            return -1;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            char child_src[4096];
            char child_rel[4096];
            snprintf(child_src, sizeof(child_src), "%s/%s", src_path, de->d_name);
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_path, de->d_name);
            if (remove_if_missing(source_root, target_root, child_src, child_rel) != 0) {
                closedir(d);
                return -1;
            }
        }
        closedir(d);
    }
    return 0;
}

static unsigned long file_hash(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    unsigned long hash = 0;
    char buf[4096];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; i++) {
            hash += (long)hash_key(buf);
            hash%=LONG_MAX;
        }
    }
    close(fd);
    return hash;
}

static int restore_entry(const char *source_root, const char *target_root,
                         const char *src_path, const char *dst_path) {
    struct stat st;
    if (lstat(src_path, &st) == -1)
        return -1;

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst_path, 0755) == -1 && errno != EEXIST)
            return -1;
        DIR *d = opendir(src_path);
        if (!d)
            return -1;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            char child_src[4096];
            char child_dst[4096];
            snprintf(child_src, sizeof(child_src), "%s/%s", src_path, de->d_name);
            snprintf(child_dst, sizeof(child_dst), "%s/%s", dst_path, de->d_name);
            if (restore_entry(source_root, target_root, child_src, child_dst) != 0) {
                closedir(d);
                return -1;
            }
        }
        closedir(d);
        return 0;
    }

    if (S_ISLNK(st.st_mode)) {
        return copy_symlink(source_root, target_root, src_path, dst_path);
    }

    if (S_ISREG(st.st_mode)) {
        unsigned long src_hash = file_hash(src_path);
        unsigned long dst_hash = file_hash(dst_path);
        if (src_hash == dst_hash && src_hash != 0)
            return 0; /* unchanged */
        return copy_file_contents(src_path, dst_path, st.st_mode);
    }
    return 0;
}

/* ---------- Handlers ---------- */

void handle_add(const char *source, const char **targets, size_t target_count) {
    char src_real[4096];
    if (canonical_path(source, src_real, sizeof(src_real)) != 0) {
        err_file_open(source);
        return;
    }

    struct stat temp_st;
    if (stat(src_real, &temp_st) != 0 || !S_ISDIR(temp_st.st_mode)) {
        err_directory_does_not_exist(src_real);
        return;
    }

    struct BackupSource *bs = find_backup(src_real);
    if (!bs) {
        ensure_backup_capacity();
        bs = &backups[backup_count++];
        bs->source_path = xstrdup(src_real);
        bs->targets = NULL;
        bs->target_count = 0;
        bs->target_capacity = 0;
        msg_backup_started(src_real);
    }

    for (size_t i = 0; i < target_count; i++) {
        char tgt_real[4096];
        if (canonical_path(targets[i], tgt_real, sizeof(tgt_real)) != 0) {
            err_file_open(targets[i]);
            continue;
        }

        if (is_subpath(src_real, tgt_real)) {
            err_path_inside(src_real, tgt_real);
            continue;
        }

        int backup_state=backup_exists(src_real, tgt_real);
        struct BackupTarget *bt=NULL;
        if (backup_state == 1) {
            err_backup_exists(src_real, tgt_real);
            continue;
        }
        else if (backup_state == 2) { //backup existed but was ended

            char tgt_abs[PATH_MAX];
            if (canonical_path(targets[i], tgt_abs, sizeof(tgt_abs)) != 0) {
                err_file_open(targets[i]);
                continue;
            }

            for (size_t j = 0; j < bs->target_count; j++) {
                if (strcmp(bs->targets[j].target_path, tgt_abs) == 0) {
                    bt = &bs->targets[j];
                    bs->targets[j].active = 1;
                    break;
                }
            }

            if (bt==NULL) {
                fprintf(stderr,"[DEBUG] Error");
            }
        }
        else if (backup_state == 0)
        {
            struct stat st;
            if (stat(tgt_real, &st) == -1) {
                if (errno == ENOENT) {
                    if (make_dir_recursive(tgt_real, 0755) == -1) {
                        perror("mkdir");
                        continue;
                    }
                }
            } else {
                if (!S_ISDIR(st.st_mode)) {
                    err_target_not_empty(tgt_real);
                    continue;
                }
                int empty = directory_empty(tgt_real);
                if (empty == 0) {
                    err_target_not_empty(tgt_real);
                    continue;
                } else if (empty < 0) {
                    err_file_open(tgt_real);
                    continue;
                }
            }

            /* grow target array */
            if (bs->target_count == bs->target_capacity) {
                bs->target_capacity = bs->target_capacity ? bs->target_capacity * 2 : 2;
                bs->targets = xrealloc(bs->targets,
                                        bs->target_capacity * sizeof(*bs->targets));
            }

            bt = &bs->targets[bs->target_count++];
            bt->target_path = xstrdup(tgt_real);
            bt->active = 1;
            bt->inotify_fd = -1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            bt->active = 0;
            continue;
        }

        if (pid == 0) {
            /* child: perform initial copy then wait for termination */
            if (sync_directories(src_real, tgt_real) != 0) {
                perror("copy");
                _exit(1);
            }

            signal(SIGTERM, SIG_DFL);
            mirror_event_loop(src_real, tgt_real);
        }

        bt->worker_pid = pid;
        msg_target_added(tgt_real);
    }
}


void handle_end(const char *source, const char **targets, size_t target_count) {
    char src_real[4096];
    if (canonical_path(source, src_real, sizeof(src_real)) != 0)
        return;

    struct BackupSource *bs = find_backup(src_real);
    if (!bs)
        return;

    for (size_t i = 0; i < target_count; i++) {
        char tgt_real[4096];
        if (canonical_path(targets[i], tgt_real, sizeof(tgt_real)) != 0)
            continue;

        for (size_t j = 0; j < bs->target_count; j++) {
            struct BackupTarget *bt = &bs->targets[j];
            if (!bt->active)
                continue;
            if (strcmp(bt->target_path, tgt_real) != 0)
                continue;

            kill(bt->worker_pid, SIGTERM);
            waitpid(bt->worker_pid, NULL, 0);
            bt->active = 0;
            msg_backup_stopped(src_real, tgt_real);
        }
    }
}

void handle_restore(const char *source, const char *target) {
    char src_real[4096];
    char tgt_real[4096];

    if (canonical_path(source, src_real, sizeof(src_real)) != 0) {
        err_file_open(source);
        return;
    }
    if (canonical_path(target, tgt_real, sizeof(tgt_real)) != 0) {
        err_file_open(target);
        return;
    }

    msg_restore_started(src_real, tgt_real);

    /* copy from target back to source */
    if (restore_entry(tgt_real, src_real, tgt_real, src_real) != 0) {
        err_restore_blocked();
        return;
    }

    remove_if_missing(src_real, tgt_real, src_real, "");
    msg_restore_finished();
}

/* ---------- Other ---------- */

static void startup_message() {
    fprintf(stdout,"Before we start: would you like to log everything"
                    "into .log file.\nIf yes type it's path else just click ENTER:\n");
    char *buffer=NULL;
    size_t buffer_size=0;
    getline(&buffer, &buffer_size, stdin);
    if (strlen(buffer) == 1 && buffer[0] == '\n') {
        printf("No log file specified. Continue without logging.");
        if (buffer) {
            free(buffer);
            buffer = NULL;
        }
        return;
    }
    buffer[strlen(buffer)-1]='\0';
    log_printf("Every backup system message will be logged into <%s> file\n",buffer);
    logger=fopen(buffer,"w");
    if (logger == NULL) {
        err_file_open(buffer);
        free(buffer);
        buffer = NULL;
        exit(1);
    }

    free(buffer);
    buffer = NULL;
}

static size_t arg_length(const char *p)
{
    size_t len = 0;
    bool in_single = false, in_double = false;

    while (*p) {
        if (!in_double && *p == '\'') {
            in_single = !in_single;
            p++;
            continue;
        }
        if (!in_single && *p == '"') {
            in_double = !in_double;
            p++;
            continue;
        }
        if (!in_single && !in_double && is_separator(*p))
            break;

        if (*p == '\\' && !in_single && p[1]) {
            p++;
        }

        len++;
        p++;
    }
    return len;
}

size_t parse_command(const char *line, char **argv, size_t max_args)
{
    size_t argc = 0;
    const char *p = line;

    while (*p && argc < max_args) {
        while (*p && is_separator(*p))
            p++;

        if (!*p)
            break;

        size_t len = arg_length(p);
        char *arg = malloc(len + 1);
        if (!arg)
            break;

        size_t i = 0;
        bool in_single = false, in_double = false;

        while (*p) {
            if (!in_double && *p == '\'') {
                in_single = !in_single;
                p++;
                continue;
            }
            if (!in_single && *p == '"') {
                in_double = !in_double;
                p++;
                continue;
            }
            if (!in_single && !in_double && is_separator(*p))
                break;

            if (*p == '\\' && !in_single && p[1]) {
                p++;
            }

            arg[i++] = *p++;
        }

        arg[i] = '\0';
        argv[argc++] = arg;

        while (*p && is_separator(*p))
            p++;
    }

    argv[argc] = NULL;
    return argc;
}

static void dispatch_command(char *line) {
    char *argv[64];          // Hardcoded 64 since this is enough for the project
    for (int i=0;i<64;i++) {
        argv[i]=NULL;
    }
    size_t argc = parse_command(line, argv, 64);

    if (argc == 0) {
        return;
    }

    /* command dispatch */
    if (strcmp(argv[0], "list") == 0) {
        if (argc != 1) {
            err_invalid_arguments();
            return;
        }
        handle_list();
    }
    else if (strcmp(argv[0], "add") == 0) {
        if (argc < 3) {
            err_invalid_arguments();
            return;
        }

        const char *source = argv[1];
        const char **targets = (const char **) &argv[2];
        size_t target_count = argc - 2;

        handle_add(source, targets, target_count);
    }
    else if (strcmp(argv[0], "end") == 0) {
        if (argc < 3) {
            err_invalid_arguments();
            return;
        }

        const char *source = argv[1];
        const char **targets = (const char **) &argv[2];
        size_t target_count = argc - 2;

        handle_end(source, targets, target_count);
    }
    else if (strcmp(argv[0], "restore") == 0) {
        if (argc != 3) {
            err_invalid_arguments();
            return;
        }

        const char *source = argv[1];
        const char *target = argv[2];

        handle_restore(source, target);
    }
    else {
        err_unknown_command();
    }
    for (int i=0;i<(int)argc;i++) {
        if (argv[i]!=NULL) {
            free(argv[i]);
            argv[i] = NULL;
        }
    }
}


int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    startup_message();
    print_banner();

    char *buffer = NULL;
    size_t buffer_size = 0;

    while (1) {
        print_prompt();

        if (getline(&buffer, &buffer_size, stdin) == -1)
            break;

        if (strcmp(buffer, "\n") == 0)
            continue;

        buffer[strlen(buffer)-1]='\0';

        if (strcmp(buffer, "exit") == 0 || exit_requested) {
            msg_exit();
            break;
        }

        dispatch_command(buffer);

        if (exit_requested) {
            msg_exit();
            break;
        }
    }

    free(buffer);
    buffer=NULL;
    cleanup();
    return 0;
}

