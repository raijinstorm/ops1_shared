## `mkdir()` / `mkdirat()`

### Purpose

Creates a new directory in the filesystem.
`mkdirat()` is the file-descriptor–based variant that avoids race conditions when working relative to an open directory.

---

### Syntax

```c
#include <sys/stat.h>

int mkdir(const char *path, mode_t mode);

#include <fcntl.h>

int mkdirat(int fd, const char *path, mode_t mode);
```

* **`path`** — Directory name or relative path.
* **`mode`** — Permission bits for the new directory (subject to process `umask`).
* **`fd`** — Directory file descriptor used as base when `path` is relative.

    * Use `AT_FDCWD` to refer to the current working directory.

---

### Return Value

* **0** on success.
* **-1** on failure and sets `errno`. No directory is created on failure.

---

### Common `errno` values

| Error              | Meaning                                               |
| ------------------ | ----------------------------------------------------- |
| `EACCES`           | Permission denied on path prefix or parent directory. |
| `EEXIST`           | File or directory already exists.                     |
| `ELOOP`            | Too many symbolic links encountered.                  |
| `EMLINK`           | Parent directory link count would exceed `LINK_MAX`.  |
| `ENAMETOOLONG`     | Path or component name too long.                      |
| `ENOENT`           | Component in path does not exist.                     |
| `ENOSPC`           | No space on device.                                   |
| `ENOTDIR`          | Component of path not a directory.                    |
| `EROFS`            | Parent directory on read-only filesystem.             |
| `EBADF`, `ENOTDIR` | (for `mkdirat()`) Invalid or non-directory `fd`.      |

---

### Behavior

* Creates an empty directory with access permissions determined by `mode & ~umask`.
* Owner: set to process effective UID.
* Group: implementation-defined (parent’s group or process’s effective GID).
* Updates modification and status timestamps of both parent and new directory.
* Fails if the path points to an existing file or symlink.
* `mkdirat()` allows relative creation with respect to a directory file descriptor for race-safe operations.

---

### Short Examples

#### Example 1 — Basic creation

```c
#include <sys/stat.h>

if (mkdir("/tmp/example", 0755) == -1)
    perror("mkdir");
```

#### Example 2 — Respecting `umask`

```c
mode_t old = umask(0022);
mkdir("secure", 0777);  // actually created as 0755
umask(old);
```

#### Example 3 — Using `mkdirat`

```c
#include <fcntl.h>
#include <sys/stat.h>

int dirfd = open("/tmp", O_DIRECTORY);
if (dirfd != -1) {
    mkdirat(dirfd, "child", 0700);
    close(dirfd);
}
```

---

### Larger Examples

#### Example 4 — Recursive directory creation

```c
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

int mkdir_recursive(const char *path, mode_t mode) {
    char tmp[256];
    char *p = NULL;
    size_t len = strlen(path);

    if (len >= sizeof(tmp)) return -1;
    strcpy(tmp, path);

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) == -1 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) == -1 && errno != EEXIST) return -1;
    return 0;
}
```

**Explanation:**
Creates all intermediate directories safely (similar to `mkdir -p`).

---

#### Example 5 — Safe directory creation under open parent

```c
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    int base = open("/var/tmp", O_DIRECTORY);
    if (base == -1) return 1;

    if (mkdirat(base, "session_001", 0700) == -1)
        perror("mkdirat");

    close(base);
    return 0;
}
```

**Explanation:**
Uses `mkdirat()` to avoid race conditions when the path under `/var/tmp` might be modified concurrently by another process.

---

### Comparison

| Function    | Relative Path Base        | Race-Safe | Typical Use                            |
| ----------- | ------------------------- | --------- | -------------------------------------- |
| `mkdir()`   | Current working directory | No        | Standard directory creation            |
| `mkdirat()` | Directory file descriptor | Yes       | Atomic or sandboxed directory creation |

---

### Notes

* `mode` only applies to permission bits; other bits are ignored.
* Newly created directories always contain `.` and `..`.
* To control final permissions precisely, apply `chmod()` after creation.
* To remove a directory, use `rmdir()`.
