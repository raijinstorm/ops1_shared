#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

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

FILE * logger=NULL;
static struct BackupSource *backups = NULL;
static size_t  backup_count = 0;
static size_t  backup_capacity = 0;


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

static void print_help(void) {
    printf("Available commands:\n\n");
    printf("  add <source_path> <target_path> [target_path ...]\n");
    printf("      Create a backup of source_path into one or more target directories.\n\n");
    printf("  end <source_path> <target_path> [target_path ...]\n");
    printf("      Stop backup for selected target directories.\n\n");
    printf("  list\n");
    printf("      List all active backups.\n\n");
    printf("  restore <source_path> <target_path>\n");
    printf("      Restore source directory from a selected backup.\n\n");
    printf("  exit\n");
    printf("      Terminate the program.\n\n");
}

static void print_prompt(void) {
    printf("backup> ");
    fflush(stdout); /* prompt must appear immediately */
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

static void print_list_header() {
    log_printf("\nActive backups:\n");
    log_printf("------------------------------------------------------------\n");
}

static void print_list_entry(const char *src, const char *target) {
    log_printf("  %s  ->  %s\n", src, target);
}

static void print_list_empty() {
    log_printf("No active backups.\n");
}

static void handle_list(void) {
    if (backup_count == 0) {
        print_list_empty();
        return;
    }

    print_list_header();

    for (size_t i = 0; i < backup_count; i++) {
        struct BackupSource *b = &backups[i];
        for (size_t j = 0; j < b->target_count; j++) {
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

static void err_recursive_backup(const char *src, const char *target) {
    log_printf("[ERROR] Invalid backup: target is inside source.\n");
    log_printf("        Source: %s\n", src);
    log_printf("        Target: %s\n", target);
}

static void err_restore_blocked(void) {
    log_printf("[ERROR] Restore failed.\n");
}

static void err_file_open(const char *path) {
    log_printf("[ERROR] Cannot open file: %s\n", path);
}

/* ---------- Exit ---------- */

static void msg_exit(void) {
    log_printf("\nShutting down...\n");
    log_printf("Cleaning up resources and terminating child processes.\n");
    log_printf("Goodbye.\n\n");
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

static int backup_exists(const char *source, const char *target) {
    for (size_t i = 0; i < backup_count; i++) {
        struct BackupSource *b = &backups[i];
        if (strcmp(b->source_path, source) != 0)
            continue;

        for (size_t j = 0; j < b->target_count; j++) {
            if (strcmp(b->targets[j].target_path, target) == 0)
                return 1;
        }
    }
    return 0;
}

static void init_targets(struct BackupSource *b, size_t count) {
    b->targets = calloc(count, sizeof(struct BackupTarget));
    if (!b->targets) {
        perror("calloc");
        exit(1);
    }
    b->target_capacity = count;
    b->target_count = 0;
}

static void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p) {
        perror("realloc");
        exit(1);
    }
    return p;
}

/* ---------- Handlers ---------- */

void handle_add(const char *source, const char **targets, size_t target_count) {
    /* find or create BackupSource */
    struct BackupSource *bs = NULL;

    for (size_t i = 0; i < backup_count; i++) {
        if (strcmp(backups[i].source_path, source) == 0) {
            bs = &backups[i];
            break;
        }
    }

    if (!bs) {
        /* new source */
        if (backup_count == backup_capacity) {
            backup_capacity = backup_capacity ? backup_capacity * 2 : 4;
            backups = xrealloc(backups,
                               backup_capacity * sizeof(*backups));
        }

        bs = &backups[backup_count++];
        bs->source_path = strdup(source);
        bs->targets = NULL;
        bs->target_count = 0;
        bs->target_capacity = 0;

        msg_backup_started(source);
    }

    /* process each target */
    for (size_t i = 0; i < target_count; i++) {
        const char *target = targets[i];

        if (backup_exists(source, target)) {
            err_backup_exists(source, target);
            continue;
        }

        /* grow target array */
        if (bs->target_count == bs->target_capacity) {
            bs->target_capacity = bs->target_capacity ? bs->target_capacity * 2 : 2;
            bs->targets = xrealloc(bs->targets,
                                    bs->target_capacity * sizeof(*bs->targets));
        }

        struct BackupTarget *bt = &bs->targets[bs->target_count++];

        bt->target_path = strdup(target);
        bt->active = 1;
        bt->inotify_fd = -1;

        /* ---------- fork per target ---------- */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            bt->active = 0;
            continue;
        }

        if (pid == 0) {
            /* ===== CHILD PROCESS ===== */

            /*
             * 1. Validate / create target directory
             *    - mkdir if missing
             *    - error if exists and not empty
             */

            /*
             * 2. Initial copy:
             *    - recursive copy source → target
             *    - handle files + symlinks
             */

            /*
             * 3. Setup inotify:
             *    - inotify_init()
             *    - add watch on source directory (recursive)
             */

            /*
             * 4. Event loop:
             *    - read inotify events
             *    - mirror changes into target
             *    - exit on source deletion / SIGTERM
             */

            _exit(0); /* never return to parent */
        }

        /* ===== PARENT PROCESS ===== */
        bt->worker_pid = pid;
        msg_target_added(target);
    }
}


void handle_end(const char *source, const char **targets, size_t target_count) {

}

void handle_restore(const char *source, const char *target) {

}

/* ---------- Other ---------- */

static void startup_message() {
    fprintf(stdout,"Before we start: would you like to log everything"
                    "into .log file.\nIf yes type it's path else just click ENTER:\n");
    char *buffer;
    size_t buffer_size;
    getline(&buffer, &buffer_size, stdin);
    if (strlen(buffer) == 1 && buffer[0] == '\n') {
        printf("No log file specified. Continue without logging.");
        return;
    }
    buffer[strlen(buffer)-1]='\0';
    log_printf("Every backup system message will be logged into <%s> file\n",buffer);
    logger=fopen(buffer,"w");
    if (logger == NULL) {
        err_file_open(buffer);
        free(buffer);
        exit(1);
    }

    free(buffer);
}

static void dispatch_command(char *line) {
    char *argv[64];          // Hardcoded 64 since this is enough for the project
    size_t argc = 0;

    /* tokenize */
    char *tok = strtok(line, " \n");
    while (tok && argc < 64) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \n");
    }

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
}


int main(void) {
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

        if (strcmp(buffer, "exit") == 0) {
            msg_exit();
            break;
        }

        dispatch_command(buffer);
    }

    free(buffer);
    return 0;
}

