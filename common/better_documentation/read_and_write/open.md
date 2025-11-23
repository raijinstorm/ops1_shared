## `open()` / `openat()`

### Purpose

Creates a connection between a pathname and a **file descriptor**, enabling low-level I/O operations (`read`, `write`, `lseek`, etc.).
`openat()` performs the same action but relative to a directory file descriptor for race-free file access.

---

### Syntax

```c
#include <sys/stat.h>
#include <fcntl.h>

int open(const char *path, int oflag, ... /* mode_t mode */);
int openat(int fd, const char *path, int oflag, ... /* mode_t mode */);
```

* **`path`** — Pathname of the file.
* **`fd`** — Directory file descriptor for `openat()` (use `AT_FDCWD` for current directory).
* **`oflag`** — Bitwise OR of access mode and additional flags.
* **`mode`** — File permission bits (used only when `O_CREAT` is set).

---

### Common Flags (`oflag`)

**Access modes (choose exactly one):**

| Flag       | Meaning        |
| ---------- | -------------- |
| `O_RDONLY` | Read only      |
| `O_WRONLY` | Write only     |
| `O_RDWR`   | Read and write |

**Creation and behavior modifiers:**

| Flag          | Meaning                                 |
| ------------- | --------------------------------------- |
| `O_CREAT`     | Create file if it does not exist        |
| `O_EXCL`      | Fail if file exists (with `O_CREAT`)    |
| `O_TRUNC`     | Truncate to zero length if it exists    |
| `O_APPEND`    | Always write at end of file             |
| `O_NONBLOCK`  | Non-blocking open (for FIFOs/devices)   |
| `O_NOFOLLOW`  | Fail if path is a symbolic link         |
| `O_DIRECTORY` | Require path to be a directory          |
| `O_CLOEXEC`   | Set close-on-exec flag (`FD_CLOEXEC`)   |
| `O_SYNC`      | Write synchronously (update metadata)   |
| `O_DSYNC`     | Synchronous data-only writes            |
| `O_RSYNC`     | Synchronous read after write completion |

---

### Return Value

* On success: returns a **non-negative integer file descriptor**.
* On failure: returns **-1** and sets `errno`.

Common `errno` values:

* `EACCES` — Permission denied.
* `EEXIST` — File exists with `O_CREAT | O_EXCL`.
* `EISDIR` — File is directory but opened for writing.
* `ENOENT` — File does not exist and `O_CREAT` not used.
* `ENOTDIR` — A path component is not a directory.
* `ELOOP` — Too many symbolic links.
* `EROFS` — File on read-only filesystem.
* `ENFILE` / `EMFILE` — Too many open files.
* `EINVAL` — Invalid combination of flags.
* `ENOSPC` — No space to create file.

---

### Short Examples

#### Example 1 — Basic open for reading

```c
int fd = open("/etc/passwd", O_RDONLY);
if (fd == -1) perror("open");
```

#### Example 2 — Create or truncate file for writing

```c
int fd = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
if (fd == -1) perror("open");
```

#### Example 3 — Safe exclusive creation

```c
int fd = open("lockfile", O_WRONLY | O_CREAT | O_EXCL, 0600);
if (fd == -1) {
    if (errno == EEXIST)
        fprintf(stderr, "Already locked.\n");
}
```

---

### Larger Examples

#### Example 4 — Implement simple file copy (low-level)

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s src dst\n", argv[0]);
        return 1;
    }

    int src = open(argv[1], O_RDONLY);
    if (src == -1) { perror("open src"); return 1; }

    int dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst == -1) { perror("open dst"); close(src); return 1; }

    char buf[4096];
    ssize_t n;
    while ((n = read(src, buf, sizeof(buf))) > 0)
        if (write(dst, buf, n) != n) { perror("write"); break; }

    if (n < 0) perror("read");
    close(src);
    close(dst);
    return 0;
}
```

**Explanation:**
Demonstrates `open()` combined with `read()` and `write()` to duplicate a file, using unbuffered I/O and explicit permission bits.

---

#### Example 5 — Race-safe file creation using `openat()`

```c
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int dirfd = open("/tmp", O_DIRECTORY | O_RDONLY);
    if (dirfd == -1) { perror("open /tmp"); return 1; }

    int fd = openat(dirfd, "report.txt", O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd == -1) {
        perror("openat");
        close(dirfd);
        return 1;
    }

    write(fd, "Report\n", 7);
    close(fd);
    close(dirfd);
    return 0;
}
```

**Explanation:**
`openat()` ensures atomic file creation relative to a known directory, avoiding race conditions where an attacker might replace a path component between checks.

---

### Comparison

| Function   | Purpose                                 | Relative path base        | Typical Use                    |            |                      |
| ---------- | --------------------------------------- | ------------------------- | ------------------------------ | ---------- | -------------------- |
| `open()`   | Open file by pathname                   | Current working directory | Standard file access           |            |                      |
| `openat()` | Open relative to directory descriptor   | Given `fd`                | Secure or sandboxed operations |            |                      |
| `creat()`  | Historical shorthand for `open(O_WRONLY | O_CREAT                   | O_TRUNC)`                      | Deprecated | Use `open()` instead |

---

### Notes

* Always `close(fd)` to release resources.
* The `mode` argument is ignored unless `O_CREAT` is used.
* Combine `open()` and `fstat()` or `fdopen()` to move between low-level and buffered I/O.
* `open()` operates below stdio buffering—data is read and written directly via system calls.
* Use `umask()` to control default file permissions during creation.
