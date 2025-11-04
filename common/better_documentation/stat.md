## `stat()` / `lstat()`

### Purpose

Retrieve detailed information about a file or symbolic link, including size, permissions, timestamps, and type.
Used to inspect file metadata in the filesystem.

---

### Syntax

```c
#include <sys/stat.h>

int stat(const char *restrict path, struct stat *restrict buf);
int lstat(const char *restrict path, struct stat *restrict buf);
int fstat(int fd, struct stat *restrict buf);
```

* **`path`** — Path to the file.
* **`fd`** — Open file descriptor (for `fstat()`).
* **`buf`** — Pointer to an allocated `struct stat` where the result is stored.

---

### Return Value

* On success: returns **0**.
* On failure: returns **-1** and sets **`errno`**.

Common `errno` values:

* `ENOENT` — File does not exist.
* `EACCES` — Permission denied.
* `ENOTDIR` — A component of the path is not a directory.
* `ELOOP` — Too many symbolic links encountered.
* `EBADF` — Invalid file descriptor (for `fstat`).

---

### Key Fields of `struct stat`

```c
struct stat {
    dev_t     st_dev;     // ID of device containing file
    ino_t     st_ino;     // Inode number
    mode_t    st_mode;    // File type and mode (permissions)
    nlink_t   st_nlink;   // Number of hard links
    uid_t     st_uid;     // Owner ID
    gid_t     st_gid;     // Group ID
    off_t     st_size;    // Total size in bytes
    time_t    st_mtime;   // Last modification time
    ...
};
```

#### File type macros (accepting `st_mode`):

* `S_ISREG(m)` — Regular file
* `S_ISDIR(m)` — Directory
* `S_ISLNK(m)` — Symbolic link
* `S_ISCHR(m)` — Character device
* `S_ISBLK(m)` — Block device
* `S_ISFIFO(m)` — FIFO (pipe)
* `S_ISSOCK(m)` — Socket

#### Permission bits (mask from `st_mode`):

* Owner: `S_IRUSR`, `S_IWUSR`, `S_IXUSR`
* Group: `S_IRGRP`, `S_IWGRP`, `S_IXGRP`
* Others: `S_IROTH`, `S_IWOTH`, `S_IXOTH`

---

### Difference Between `stat()` and `lstat()`

| Function  | Follows symbolic links | Example use                               |
| --------- | ---------------------- | ----------------------------------------- |
| `stat()`  | Yes                    | To check the file a link points to        |
| `lstat()` | No                     | To check the link itself (not its target) |

---

### Short Examples

#### Example 1 — Print file size

```c
struct stat sb;
if (stat("file.txt", &sb) == 0)
    printf("Size: %ld bytes\n", sb.st_size);
else
    perror("stat");
```

#### Example 2 — Detect file type

```c
struct stat sb;
if (lstat("target", &sb) == 0) {
    if (S_ISDIR(sb.st_mode))
        puts("Directory");
    else if (S_ISLNK(sb.st_mode))
        puts("Symbolic link");
    else if (S_ISREG(sb.st_mode))
        puts("Regular file");
}
```

#### Example 3 — Compare modification times

```c
struct stat a, b;
stat("file1.txt", &a);
stat("file2.txt", &b);
if (a.st_mtime > b.st_mtime)
    puts("file1.txt is newer");
else
    puts("file2.txt is newer or same age");
```

---

### Larger Examples

#### Example 4 — List directory entries with file info

```c
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

int main(void) {
    DIR *dir = opendir(".");
    if (!dir) { perror("opendir"); return 1; }

    struct dirent *entry;
    struct stat sb;

    while ((entry = readdir(dir)) != NULL) {
        if (lstat(entry->d_name, &sb) == -1) continue;

        printf("%-20s %10ld bytes ", entry->d_name, sb.st_size);
        if (S_ISDIR(sb.st_mode)) printf("[DIR]\n");
        else if (S_ISLNK(sb.st_mode)) printf("[LINK]\n");
        else if (S_ISREG(sb.st_mode)) printf("[FILE]\n");
        else printf("[OTHER]\n");
    }

    closedir(dir);
    return 0;
}
```

**Explanation:**
Lists all entries in the current directory and prints their types and sizes using `lstat()`.
This emulates a minimal version of `ls -l` type detection.

---

#### Example 5 — Check file permissions

```c
#include <sys/stat.h>
#include <stdio.h>

void check_perms(const char *path) {
    struct stat sb;
    if (stat(path, &sb) == -1) {
        perror("stat");
        return;
    }

    printf("%s:\n", path);
    printf("Owner: %s%s%s\n",
           (sb.st_mode & S_IRUSR) ? "r" : "-",
           (sb.st_mode & S_IWUSR) ? "w" : "-",
           (sb.st_mode & S_IXUSR) ? "x" : "-");
    printf("Group: %s%s%s\n",
           (sb.st_mode & S_IRGRP) ? "r" : "-",
           (sb.st_mode & S_IWGRP) ? "w" : "-",
           (sb.st_mode & S_IXGRP) ? "x" : "-");
    printf("Others: %s%s%s\n",
           (sb.st_mode & S_IROTH) ? "r" : "-",
           (sb.st_mode & S_IWOTH) ? "w" : "-",
           (sb.st_mode & S_IXOTH) ? "x" : "-");
}

int main(void) {
    check_perms("example.txt");
    return 0;
}
```

**Explanation:**
Displays file permissions in a symbolic form (like `rwx`) for each class of users.
Shows how `st_mode` encodes permission bits.

---

### Comparison

| Function    | Description                    | Follows Links | Typical Use                          |
| ----------- | ------------------------------ | ------------- | ------------------------------------ |
| `stat()`    | File information               | Yes           | Inspect real file target             |
| `lstat()`   | File information               | No            | Inspect symbolic link                |
| `fstat()`   | File descriptor info           | N/A           | When file already opened             |
| `access()`  | Tests permissions              | N/A           | To check if user *can* access a file |
| `fstatat()` | Like `stat()` with base dir fd | Optional      | Safer relative paths in modern code  |

---

### Notes

* Always check return values and handle `errno`.
* For symbolic link inspection, prefer `lstat()`.
* POSIX does not guarantee all fields are meaningful on all systems.
* `st_mtime`, `st_ctime`, and `st_atime` can differ by update semantics (modification, status change, last access).
