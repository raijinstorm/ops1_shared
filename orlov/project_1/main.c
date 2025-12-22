#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_BACKUPS 256
#define MAX_WATCHES 8192
#define EVENT_BUF_LEN (64 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define MAX_ARGS 64

#define ERR(msg) perror(msg)

int debug_info = 0;

static void log_info(const char *fmt, ...) {
  if (!debug_info)
    return;
  va_list ap; // create a pointer to go through a list of optional variables.
  va_start(ap, fmt);
  printf("[INFO] ");
  vprintf(fmt, ap); // use this print to embed arguments into string
  printf("\n");
  va_end(ap);
}

static void log_error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[ERROR] ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

struct Backup {
  char source[PATH_MAX];
  char target[PATH_MAX];
  pid_t pid;
};

struct Watch {
  int wd;              // inotify watch descriptor
  char path[PATH_MAX]; // absolute path
};

struct WatchMap {
  struct Watch list[MAX_WATCHES]; // watch map is needed to store multiple
  int count;
};

static struct Backup backups[MAX_BACKUPS];
static int backup_count = 0;
static volatile sig_atomic_t stop_flag = 0;

static void on_term(int sig) {
  (void)sig;
  stop_flag = 1;
}
static void free_args(char **argv, int argc);

static void usage(void) {
  printf("Commands:\n");
  printf("  add <source> <target1> [target2 ...]\n");
  printf("  end <source> <target1> [target2 ...]\n");
  printf("  list\n");
  printf("  restore <source> <target>\n");
  printf("  exit\n");
}

// used to detect cases when user might try to create target inside a source
static int path_is_prefix(const char *base, const char *path) {
  size_t len = strlen(base);
  if (strncmp(base, path, len) != 0) {
    return 0;
  }
  if (path[len] == '\0') {
    return 1;
  }
  return path[len] == '/';
}

static int ensure_dir(const char *path) {
  log_info("Ensuring directory: %s", path);
  struct stat st;
  if (stat(path, &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      log_error("not a directory: %s", path);
      return -1;
    }
    log_info("Directory already exists: %s", path);
    return 0;
  }
  if (errno != ENOENT) {
    log_error("stat failed for %s: %s", path, strerror(errno));
    return -1;
  }

  char tmp[PATH_MAX];
  strncpy(tmp, path, sizeof(tmp));
  tmp[sizeof(tmp) - 1] = '\0';

  char *slash =
      strrchr(tmp, '/'); // Finds the last slash in the path to separate the
                         // current directory from its paren
  if (slash && slash != tmp) {
    *slash = '\0';
    if (ensure_dir(tmp) < 0) {
      return -1;
    }
    *slash = '/';
  }

  log_info("Creating directory: %s", path);
  if (mkdir(path, 0755) < 0) {
    log_error("mkdir failed for %s: %s", path, strerror(errno));
    return -1;
  }
  log_info("Directory created: %s", path);
  return 0;
}

// wrapper around ensure_dir. Ensure dir would not work correctly if we path a
// file to it, so we separate parent dirs and path them to ensure_dir
static int ensure_parent_dirs(const char *path) {
  log_info("Ensuring parent directories for: %s", path);
  char tmp[PATH_MAX];
  strncpy(tmp, path, sizeof(tmp));
  tmp[sizeof(tmp) - 1] = '\0';

  char *slash = strrchr(tmp, '/');
  if (!slash || slash == tmp) {
    log_info("No parent directories needed for: %s", path);
    return 0;
  }
  *slash = '\0';
  return ensure_dir(tmp);
}

static int is_empty_dir(const char *path) {
  log_info("Checking if directory is empty: %s", path);
  DIR *dir = opendir(path);
  if (!dir) {
    log_error("opendir failed for %s: %s", path, strerror(errno));
    return -1;
  }
  struct dirent *e; // declares a pointer to a directory entry structure
  while ((e = readdir(dir)) != NULL) { // iterate through each entry inside dir
    if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
      closedir(dir);
      log_info("Directory not empty: %s", path);
      return 0;
    }
  }
  closedir(dir);
  log_info("Directory is empty: %s", path);
  return 1;
}

static int copy_file(const char *src, const char *dst, mode_t mode) {
  log_info("Copying file %s -> %s", src, dst);
  int in_fd = open(src, O_RDONLY);
  if (in_fd < 0) {
    log_error("open source failed for %s: %s", src, strerror(errno));
    return -1;
  }
  if (ensure_parent_dirs(dst) < 0) {
    close(in_fd);
    return -1;
  }

  int out_fd = open(dst, O_CREAT | O_WRONLY | O_TRUNC, mode);
  if (out_fd < 0) {
    log_error("open destination failed for %s: %s", dst, strerror(errno));
    close(in_fd);
    return -1;
  }

  char buf[4096];
  ssize_t r;

  // reads the source file in 4KB chunks until the end of the file is reached
  while ((r = read(in_fd, buf, sizeof(buf))) > 0) {
    ssize_t off = 0;
    while (
        off <
        r) { // ensures all bytes read in the current chunk are actually written
      ssize_t w = write(out_fd, buf + off, r - off);
      if (w < 0) {
        log_error("write failed for %s: %s", dst, strerror(errno));
        close(in_fd);
        close(out_fd);
        return -1;
      }
      off += w;
    }
  }
  if (r < 0) {
    log_error("read failed for %s: %s", src, strerror(errno));
    close(in_fd);
    close(out_fd);
    return -1;
  }

  if (fchmod(out_fd, mode) < 0) {
    log_error("fchmod failed for %s: %s", dst, strerror(errno));
  }

  close(in_fd);
  close(out_fd);
  log_info("Copied file %s -> %s", src, dst);
  return 0;
}

// overwriting existing symlinks , clean before copying
static int unlink_if_exists(const char *path) {
  log_info("Unlinking path if exists: %s", path);
  if (unlink(path) == 0) {
    log_info("Path removed: %s", path);
    return 0;
  }
  if (errno == ENOENT) {
    log_info("Path did not exist: %s", path);
    return 0;
  }
  log_error("unlink failed for %s: %s", path, strerror(errno));
  return -1;
}

static int copy_symlink(const char *src, const char *dst, const char *from_root,
                        const char *to_root) {
  log_info("Copying symlink %s -> %s", src, dst);
  char link_target[PATH_MAX];
  ssize_t len =
      readlink(src, link_target,
               sizeof(link_target) - 1); // find out where the existing symlink
                                         // at src is actually pointing.
  if (len < 0) {
    log_error("readlink failed for %s: %s", src, strerror(errno));
    return -1;
  }
  link_target[len] = '\0'; // readlink does not append a null byte

  // If a symbolic link points to a file inside the source directory using an
  // absolute path, we fix the link in the target to point to the new backup
  // version Otherwise, the link is copied exactly as it is.
  char adjusted[PATH_MAX];
  if (link_target[0] == '/' &&
      path_is_prefix(from_root, link_target)) { // checks if the link is an
                                                // absolute path and is in root
    snprintf(adjusted, sizeof(adjusted), "%s%s", to_root,
             link_target +
                 strlen(from_root)); // use pointer arithm to skip from_root
  } else {
    strncpy(adjusted, link_target, sizeof(adjusted));
    adjusted[sizeof(adjusted) - 1] = '\0';
  }

  if (ensure_parent_dirs(dst) < 0) {
    log_error("ensure_parent_dirs failed for %s", dst);
    return -1;
  }
  if (unlink_if_exists(dst) < 0) {
    log_error("unlink_if_exists failed for %s", dst);
    return -1;
  }
  if (symlink(adjusted, dst) < 0) {
    log_error("symlink creation failed for %s -> %s: %s", dst, adjusted,
              strerror(errno));
    return -1;
  }
  log_info("Created symlink %s -> %s", dst, adjusted);
  return 0;
}

// basically works the same as rm -rf
static int remove_path(const char *path) {
  log_info("Removing path: %s", path);
  struct stat st;
  if (lstat(path, &st) < 0) {
    if (errno == ENOENT) {
      return 0;
    }
    log_error("lstat failed for %s: %s", path, strerror(errno));
    return -1;
  }

  if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
    DIR *dir = opendir(path);
    if (!dir) {
      ERR("opendir");
      return -1;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
        continue;
      }
      char sub_src[PATH_MAX];
      snprintf(sub_src, sizeof(sub_src), "%s/%s", path,
               e->d_name);            // construct full path
      if (remove_path(sub_src) < 0) { // recursive call
        closedir(dir);
        return -1;
      }
    }
    closedir(dir);
    if (rmdir(path) < 0) {
      log_error("rmdir failed for %s: %s", path, strerror(errno));
      return -1;
    }
  } else {
    if (unlink(path) < 0) {
      log_error("unlink failed for %s: %s", path, strerror(errno));
      return -1;
    }
  }
  log_info("Removed path: %s", path);
  return 0;
}

static int copy_entry(const char *src, const char *dst, const char *from_root,
                      const char *to_root);

static int copy_dir(const char *src, const char *dst, const char *from_root,
                    const char *to_root) {
  log_info("Copying directory %s -> %s", src, dst);
  struct stat st;
  if (lstat(src, &st) < 0) {
    log_error("lstat failed for %s: %s", src, strerror(errno));
    return -1;
  }

  if (ensure_dir(dst) < 0) {
    return -1;
  }
  if (chmod(dst, st.st_mode & 0777) < 0) {
    ERR("chmod");
  }

  DIR *dir = opendir(src);
  if (!dir) {
    ERR("opendir");
    return -1;
  }
  struct dirent *e;
  while ((e = readdir(dir)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
      continue;
    }
    char sub_src[PATH_MAX];
    char sub_dst[PATH_MAX];
    snprintf(sub_src, sizeof(sub_src), "%s/%s", src, e->d_name);
    snprintf(sub_dst, sizeof(sub_dst), "%s/%s", dst, e->d_name);
    if (copy_entry(sub_src, sub_dst, from_root, to_root) < 0) {
      closedir(dir);
      return -1;
    }
  }
  closedir(dir);
  log_info("Copied directory %s -> %s", src, dst);
  return 0;
}

// universal copy function which call corresponding copy func for each type of
// entry
static int copy_entry(const char *src, const char *dst, const char *from_root,
                      const char *to_root) {
  log_info("Copying entry %s -> %s", src, dst);
  struct stat st;
  if (lstat(src, &st) < 0) {
    log_error("lstat failed for %s: %s", src, strerror(errno));
    return -1;
  }

  if (S_ISLNK(st.st_mode)) {
    return copy_symlink(src, dst, from_root, to_root);
  }

  if (S_ISDIR(st.st_mode)) {
    return copy_dir(src, dst, from_root, to_root);
  }

  if (S_ISREG(st.st_mode)) {
    return copy_file(src, dst, st.st_mode & 0777);
  }

  log_info("Skipping unsupported file: %s", src);
  return 0;
}

static int restore_dir(const char *backup_dir, const char *src_dir,
                       const char *backup_root, const char *source_root);

static int restore_entry(const char *backup_path, const char *src_path,
                         const char *backup_root, const char *source_root) {
  log_info("Restoring entry %s -> %s", backup_path, src_path);
  struct stat st;
  if (lstat(backup_path, &st) < 0) {
    log_error("lstat failed for %s: %s", backup_path, strerror(errno));
    return -1;
  }

  struct stat dst_st;
  int dst_exists = lstat(src_path, &dst_st);

  if (S_ISDIR(st.st_mode)) {
    if (dst_exists == 0 && !S_ISDIR(dst_st.st_mode)) {
      if (remove_path(src_path) < 0) {
        return -1;
      }
    }
    if (ensure_dir(src_path) < 0) {
      return -1;
    }
    if (chmod(src_path, st.st_mode & 0777) < 0) {
      ERR("chmod");
    }
    return restore_dir(backup_path, src_path, backup_root, source_root);
  }

  if (S_ISLNK(st.st_mode)) {
    char current[PATH_MAX];
    if (dst_exists == 0 && S_ISLNK(dst_st.st_mode)) {
      ssize_t len = readlink(src_path, current, sizeof(current) - 1);
      if (len >= 0) {
        current[len] = '\0';
        char target[PATH_MAX];
        ssize_t back_len = readlink(backup_path, target, sizeof(target) - 1);
        if (back_len >= 0) {
          target[back_len] = '\0';
          if (strcmp(current, target) == 0) {
            return 0;
          }
        }
      }
    }
    return copy_symlink(backup_path, src_path, backup_root, source_root);
  }

  if (S_ISREG(st.st_mode)) {
    if (dst_exists == 0 && S_ISREG(dst_st.st_mode) &&
        dst_st.st_size == st.st_size && dst_st.st_mtime >= st.st_mtime) {
      return 0;
    }
    return copy_file(backup_path, src_path, st.st_mode & 0777);
  }

  return 0;
}

static int restore_dir(const char *backup_dir, const char *src_dir,
                       const char *backup_root, const char *source_root) {
  log_info("Restoring directory %s -> %s", backup_dir, src_dir);
  DIR *dir = opendir(backup_dir);
  if (!dir) {
    log_error("opendir failed for %s: %s", backup_dir, strerror(errno));
    return -1;
  }

  struct dirent *e;
  while ((e = readdir(dir)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
      continue;
    }
    char sub_backup[PATH_MAX];
    char sub_src[PATH_MAX];
    snprintf(sub_backup, sizeof(sub_backup), "%s/%s", backup_dir, e->d_name);
    snprintf(sub_src, sizeof(sub_src), "%s/%s", src_dir, e->d_name);
    if (restore_entry(sub_backup, sub_src, backup_root, source_root) < 0) {
      closedir(dir);
      return -1;
    }
  }
  closedir(dir);

  DIR *src = opendir(src_dir);
  if (!src) {
    log_error("opendir failed for %s: %s", src_dir, strerror(errno));
    return -1;
  }
  while ((e = readdir(src)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
      continue;
    }
    char check_backup[PATH_MAX];
    snprintf(check_backup, sizeof(check_backup), "%s/%s", backup_dir,
             e->d_name);
    struct stat st;
    if (lstat(check_backup, &st) < 0 && errno == ENOENT) {
      char extra_path[PATH_MAX];
      snprintf(extra_path, sizeof(extra_path), "%s/%s", src_dir, e->d_name);
      if (remove_path(extra_path) < 0) {
        closedir(src);
        return -1;
      }
    }
  }
  closedir(src);
  log_info("Restored directory %s -> %s", backup_dir, src_dir);
  return 0;
}

static void watch_map_remove(struct WatchMap *map, int wd) {
  log_info("Removing watch wd=%d", wd);
  for (int i = 0; i < map->count; i++) {
    if (map->list[i].wd == wd) {
      map->list[i] = map->list[map->count - 1];
      map->count--;
      return;
    }
  }
}

// convert ID number (watch descriptor) back into a string path after reading
// event from inotify
static struct Watch *watch_map_find(struct WatchMap *map, int wd) {
  log_info("Looking up watch wd=%d", wd);
  for (int i = 0; i < map->count; i++) {
    if (map->list[i].wd == wd) {
      return &map->list[i];
    }
  }
  return NULL;
}

// basic add of inotify entry to monitor specific function
static int add_watch_entry(int fd, struct WatchMap *map, const char *path,
                           uint32_t mask) {
  log_info("Adding watch for %s", path);
  if (map->count >= MAX_WATCHES) {
    fprintf(stderr, "too many watches\n");
    return -1;
  }
  int wd = inotify_add_watch(fd, path, mask);
  if (wd < 0) {
    log_error("inotify_add_watch failed for %s: %s", path, strerror(errno));
    return -1;
  }
  log_info("Watch added for %s (wd=%d)", path, wd);
  map->list[map->count].wd = wd;
  strncpy(map->list[map->count].path, path, sizeof(map->list[map->count].path));
  map->list[map->count].path[sizeof(map->list[map->count].path) - 1] = '\0';
  map->count++;
  return 0;
}

// opens the directory, loops through its contents, and calls itself for every
// subdirectory
static int add_watch_recursive(int fd, struct WatchMap *map, const char *path,
                               uint32_t mask) {
  log_info("Recursively adding watch for %s", path);
  struct stat st;
  if (lstat(path, &st) < 0) {
    log_error("lstat failed for watch path %s: %s", path, strerror(errno));
    return -1;
  }
  if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
    return 0;
  }

  if (add_watch_entry(fd, map, path, mask) < 0) {
    return -1;
  }

  DIR *dir = opendir(path);
  if (!dir) {
    ERR("opendir");
    return -1;
  }
  struct dirent *e;
  while ((e = readdir(dir)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
      continue;
    }
    char sub_path[PATH_MAX];
    snprintf(sub_path, sizeof(sub_path), "%s/%s", path, e->d_name);
    if (add_watch_recursive(fd, map, sub_path, mask) < 0) {
      closedir(dir);
      return -1;
    }
  }
  closedir(dir);
  return 0;
}

// ff a directory in the source is deleted or moved we stop watching it and all
// its subfolders
static void remove_watches_under(int fd, struct WatchMap *map,
                                 const char *path) {
  log_info("Removing watches under path %s", path);
  for (int i = 0; i < map->count;) {
    if (path_is_prefix(path, map->list[i].path)) {
      inotify_rm_watch(fd, map->list[i].wd);
      map->list[i] = map->list[map->count - 1];
      map->count--;
    } else {
      i++;
    }
  }
  log_info("Watches remaining: %d", map->count);
}

static volatile sig_atomic_t worker_stop = 0;
static void worker_term(int sig) {
  (void)sig;
  worker_stop = 1;
}

static int run_worker(const char *source, const char *target) {
  log_info("Worker starting for %s -> %s", source, target);
  if (copy_entry(source, target, source, target) < 0) {
    log_error("Initial copy failed for %s -> %s", source, target);
    return 1;
  }
  log_info("Initial sync complete for %s -> %s", source, target);

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = worker_term;
  if (sigaction(SIGTERM, &sa, NULL) < 0) {
    ERR("sigaction");
    return 1;
  }
  sa.sa_handler = SIG_IGN;
  sigaction(SIGINT, &sa, NULL);

  int fd = inotify_init();
  if (fd < 0) {
    ERR("inotify_init");
    return 1;
  }

  struct WatchMap *map = calloc(1, sizeof(*map));
  if (!map) {
    log_error("Failed to allocate watch map");
    close(fd);
    return 1;
  }

  // create mask to decide which operations should be tracked by inotify
  uint32_t mask = IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM |
                  IN_MOVED_TO | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF |
                  IN_CLOSE_WRITE;

  if (add_watch_recursive(fd, map, source, mask) < 0) {
    free(map);
    close(fd);
    return 1;
  }

  log_info("Watching %s recursively (%d watches)", source, map->count);

  char buffer[EVENT_BUF_LEN];
  while (!worker_stop) {
    ssize_t len = read(fd, buffer, sizeof(buffer));
    if (len < 0) {
      if (errno == EINTR) {
        continue;
      }
      log_error("read failed: %s", strerror(errno));
      break;
    }
    log_info("Read %zd bytes from inotify", len);

    // process events one by one
    ssize_t i = 0;
    while (i < len) {
      struct inotify_event *ev = (struct inotify_event *)&buffer[i];
      struct Watch *w = watch_map_find(map, ev->wd);
      if (!w) {
        i += sizeof(struct inotify_event) + ev->len;
        continue;
      }

      char src_path[PATH_MAX * 2];
      if (ev->len >
          0) { // check if event hapenned to file/dir inside monitored dir
        snprintf(src_path, sizeof(src_path), "%s/%s", w->path, ev->name);
      } else { // in case it happend to monitered dir itself
        strncpy(src_path, w->path, sizeof(src_path));
        src_path[sizeof(src_path) - 1] = '\0';
      }

      // calc desination path
      char dst_path[PATH_MAX];
      if (strcmp(src_path, source) == 0) {
        strncpy(dst_path, target, sizeof(dst_path));
      } else {
        const char *rel = src_path + strlen(source) + 1;
        snprintf(dst_path, sizeof(dst_path), "%s/%s", target,
                 rel); // concat target root folder and path from source
      }
      dst_path[sizeof(dst_path) - 1] = '\0';

      log_info("Event mask 0x%x for %s", ev->mask, src_path);
      //        //Deleticase if
      if (ev->mask & IN_IGNORED) {
        watch_map_remove(map, ev->wd);
        i += sizeof(struct inotify_event) + ev->len;
        continue;
      }

      if ((ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) &&
          strcmp(w->path, source) == 0) {
        worker_stop = 1;
        break;
      }

      if (ev->mask & (IN_DELETE | IN_MOVED_FROM)) {
        if (ev->mask & IN_ISDIR) {
          remove_watches_under(fd, map, src_path);
        }
        log_info("Removing %s -> %s due to delete/move", src_path, dst_path);
        remove_path(dst_path);
      } else if (ev->mask & (IN_CREATE | IN_MOVED_TO)) {
        struct stat st;
        if (lstat(src_path, &st) == 0 && (ev->mask & IN_ISDIR)) {
          log_info("Directory created/moved at %s", src_path);
          copy_dir(src_path, dst_path, source, target);
          add_watch_recursive(fd, map, src_path, mask);
        } else {
          log_info("Entry created/moved at %s", src_path);
          copy_entry(src_path, dst_path, source, target);
        }
      } else if (ev->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB)) {
        struct stat st;
        if (lstat(src_path, &st) == 0) {
          if (S_ISDIR(st.st_mode)) {
            log_info("Directory attributes_changed %s", src_path);
            chmod(dst_path, st.st_mode & 0777);
          } else {
            log_info("File modified/attrib %s", src_path);
            copy_entry(src_path, dst_path, source, target);
          }
        }
      }
      // move the pointer to the start of the next event in the buffer
      i += sizeof(struct inotify_event) + ev->len;
    }
  }

  log_info("Worker shutting down for %s -> %s", source, target);
  for (int i = 0; i < map->count; i++) {
    inotify_rm_watch(fd, map->list[i].wd);
  }
  close(fd);
  free(map);
  return 0;
}

static int find_backup(const char *source, const char *target) {
  log_info("Searching for backup %s -> %s", source, target);
  for (int i = 0; i < backup_count; i++) {
    if (strcmp(backups[i].source, source) == 0 &&
        strcmp(backups[i].target, target) == 0) {
      log_info("Found backup index %d for %s -> %s", i, source, target);
      return i;
    }
  }
  return -1;
}

static void reap_children(void) {
  log_info("Reaping child processes");
  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    for (int i = 0; i < backup_count; i++) {
      if (backups[i].pid == pid) {
        backups[i] =
            backups[backup_count - 1]; // fill the blank space in the middle
                                       // (after remove) with the last elem
        backup_count--; // we "forget" that last element exists, it will be
                        // overwritte
        break;
      }
    }
  }
}

static void stop_backup(const char *source, const char *target) {
  log_info("Stopping backup %s -> %s", source, target);
  int idx = find_backup(source, target);
  if (idx < 0) {
    log_error("No such backup: %s -> %s", source, target);
    return;
  }
  kill(backups[idx].pid, SIGTERM);
  waitpid(backups[idx].pid, NULL, 0);
  backups[idx] = backups[backup_count - 1]; // same logic as in function above
  backup_count--;
}

static int parse_args(const char *line, char **argv, int max_args) {
  log_info("Parsing arguments: %s", line);
  int argc = 0;
  int in_quote = 0;
  char quote = 0;
  char token[PATH_MAX];
  int ti = 0;

  for (size_t i = 0; line[i]; i++) {
    char c = line[i];
    if (in_quote) {
      if (c == quote) {
        in_quote = 0;
        continue;
      }
      if (c == '\\' && line[i + 1]) {
        if (ti >= (int)sizeof(token) - 1) {
          fprintf(stderr, "Argument too long\n");
          free_args(argv, argc);
          return -1;
        }
        token[ti++] = line[++i];
        continue;
      }
      if (ti >= (int)sizeof(token) - 1) {
        fprintf(stderr, "Argument too long\n");
        free_args(argv, argc);
        return -1;
      }
      token[ti++] = c;
    } else {
      if (isspace((unsigned char)c)) {
        if (ti > 0) {
          token[ti] = '\0';
          argv[argc++] = strdup(token);
          ti = 0;
          if (argc >= max_args) {
            break;
          }
        }
        continue;
      }
      if (c == '"' || c == '\'') {
        in_quote = 1;
        quote = c;
        continue;
      }
      if (ti >= (int)sizeof(token) - 1) {
        fprintf(stderr, "Argument too long\n");
        free_args(argv, argc);
        return -1;
      }
      token[ti++] = c;
    }
  }
  if (in_quote) {
    fprintf(stderr, "Unterminated quote\n");
    free_args(argv, argc);
    return -1;
  }
  if (ti > 0 && argc < max_args) {
    token[ti] = '\0';
    argv[argc++] = strdup(token);
  }
  return argc;
}

static int canonical_path(const char *path, char *out) {
  log_info("Resolving canonical path for %s", path);
  char *res = realpath(path, out);
  if (!res) {
    log_error("realpath failed for %s: %s", path, strerror(errno));
    return -1;
  }
  return 0;
}

static int validate_source(const char *path, char *resolved) {
  log_info("Validating source directory %s", path);
  if (canonical_path(path, resolved) < 0) {
    return -1;
  }
  struct stat st;
  if (stat(resolved, &st) < 0) {
    log_error("stat failed for %s: %s", resolved, strerror(errno));
    return -1;
  }
  if (!S_ISDIR(st.st_mode)) {
    log_error("not a directory: %s", path);
    return -1;
  }
  return 0;
}

static int validate_target(const char *path, char *resolved) {
  log_info("Validating target directory %s", path);
  struct stat st;
  if (stat(path, &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      log_error("not a directory: %s", path);
      return -1;
    }
    int empty = is_empty_dir(path);
    if (empty < 0) {
      return -1;
    }
    if (!empty) {
      log_error("target not empty: %s", path);
      return -1;
    }
  } else {
    if (ensure_dir(path) < 0) {
      return -1;
    }
  }
  if (canonical_path(path, resolved) < 0) {
    return -1;
  }
  return 0;
}

static int add_backup(const char *source, const char *target) {
  log_info("Adding backup %s -> %s", source, target);
  if (backup_count >= MAX_BACKUPS) {
    fprintf(stderr, "Too many backups\n");
    return -1;
  }
  pid_t pid = fork();
  if (pid < 0) {
    log_error("fork failed: %s", strerror(errno));
    return -1;
  }
  if (pid == 0) {
    int ret = run_worker(source, target);
    _exit(ret);
  }

  struct Backup b;
  strncpy(b.source, source, sizeof(b.source));
  b.source[sizeof(b.source) - 1] = '\0';
  strncpy(b.target, target, sizeof(b.target));
  b.target[sizeof(b.target) - 1] = '\0';
  b.pid = pid;
  backups[backup_count++] = b;
  log_info("Backup registered: %s -> %s pid=%d", source, target, pid);
  return 0;
}

static void list_backups(void) {
  log_info("Listing backups");
  if (backup_count == 0) {
    printf("No active backups\n");
    return;
  }
  for (int i = 0; i < backup_count; i++) {
    printf("[%d] %s -> %s\n", backups[i].pid, backups[i].source,
           backups[i].target);
  }
}

static void free_args(char **argv, int argc) {
  for (int i = 0; i < argc; i++) {
    free(argv[i]);
  }
}

static void stop_all(void) {
  log_info("Stopping all backups");
  for (int i = 0; i < backup_count; i++) {
    kill(backups[i].pid, SIGTERM);
  }
  for (int i = 0; i < backup_count; i++) {
    waitpid(backups[i].pid, NULL, 0);
  }
  backup_count = 0;
}

int main(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  if (sigaction(SIGINT, &sa, NULL) < 0) {
    ERR("sigaction");
    return 1;
  }
  if (sigaction(SIGTERM, &sa, NULL) < 0) {
    ERR("sigaction");
    return 1;
  }

  usage();
  log_info("Command interface ready");

  char *line = NULL;
  size_t linecap = 0;

  while (!stop_flag) {
    printf("> ");
    fflush(stdout);

    ssize_t nread = getline(&line, &linecap, stdin);
    if (nread < 0) {
      if (feof(stdin)) {
        break;
      }
      continue;
    }
    if (nread > 0 && line[nread - 1] == '\n') {
      line[nread - 1] = '\0';
    }

    char *argv[MAX_ARGS] = {0};
    int argc = parse_args(line, argv, MAX_ARGS);
    if (argc < 0) {
      continue;
    }
    if (argc == 0) {
      continue;
    }

    reap_children();
    log_info("Processing command: %s", argv[0]);

    if (strcmp(argv[0], "exit") == 0) {
      log_info("Exit command received");
      free_args(argv, argc);
      break;
    } else if (strcmp(argv[0], "list") == 0) {
      log_info("List command received");
      list_backups();
    } else if (strcmp(argv[0], "add") == 0) {
      log_info("Add command received with %d arguments", argc);
      if (argc < 3) {
        usage();
        free_args(argv, argc);
        continue;
      }
      char source[PATH_MAX];
      if (validate_source(argv[1], source) < 0) {
        free_args(argv, argc);
        continue;
      }
      log_info("Validated source for add: %s", source);
      int ok = 1;
      char targets[MAX_ARGS][PATH_MAX];
      int tcount = 0;
      for (int i = 2; i < argc; i++) {
        char target[PATH_MAX];
        if (validate_target(argv[i], target) < 0) {
          ok = 0;
          break;
        }
        if (path_is_prefix(source, target)) {
          fprintf(stderr, "target inside source is not allowed\n");
          ok = 0;
          break;
        }
        if (find_backup(source, target) >= 0) {
          fprintf(stderr, "backup already exists: %s -> %s\n", source, target);
          ok = 0;
          break;
        }
        for (int j = 0; j < tcount; j++) {
          if (strcmp(targets[j], target) == 0) {
            fprintf(stderr, "duplicate target: %s\n", target);
            ok = 0;
            break;
          }
        }
        if (!ok) {
          break;
        }
        strncpy(targets[tcount], target, PATH_MAX);
        targets[tcount][PATH_MAX - 1] = '\0';
        tcount++;
      }
      if (!ok) {
        free_args(argv, argc);
        continue;
      }
      for (int i = 0; i < tcount; i++) {
        add_backup(source, targets[i]);
      }
    } else if (strcmp(argv[0], "end") == 0) {
      log_info("End command received with %d arguments", argc);
      if (argc < 3) {
        usage();
        free_args(argv, argc);
        continue;
      }
      char source[PATH_MAX];
      if (validate_source(argv[1], source) < 0) {
        free_args(argv, argc);
        continue;
      }
      log_info("Validated source for end: %s", source);
      for (int i = 2; i < argc; i++) {
        char target[PATH_MAX];
        if (canonical_path(argv[i], target) < 0) {
          continue;
        }
        stop_backup(source, target);
      }
    } else if (strcmp(argv[0], "restore") == 0) {
      log_info("Restore command received with %d arguments", argc);
      if (argc != 3) {
        usage();
        free_args(argv, argc);
        continue;
      }
      char source[PATH_MAX];
      char target[PATH_MAX];
      if (validate_source(argv[1], source) < 0) {
        free_args(argv, argc);
        continue;
      }
      if (canonical_path(argv[2], target) < 0) {
        free_args(argv, argc);
        continue;
      }
      int idx = find_backup(source, target);
      if (idx >= 0) {
        stop_backup(source, target);
      }
      if (restore_entry(target, source, target, source) < 0) {
        fprintf(stderr, "restore failed\n");
        log_error("Restore failed for %s from %s", source, target);
      } else {
        printf("restored %s from %s\n", source, target);
        log_info("Restore succeeded for %s from %s", source, target);
      }
    } else {
      log_info("Unknown command: %s", argv[0]);
      fprintf(stderr, "Unknown command\n");
      usage();
    }
    free_args(argv, argc);
  }

  free(line);
  stop_all();
  reap_children();
  return 0;
}