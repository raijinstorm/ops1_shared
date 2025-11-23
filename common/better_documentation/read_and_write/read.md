## `read()` / `pread()`

### Purpose

Performs unbuffered, low-level input.
Reads bytes from an open file descriptor into a memory buffer.

---

### Syntax

```c
#include <unistd.h>

ssize_t read(int fildes, void *buf, size_t nbyte);
ssize_t pread(int fildes, void *buf, size_t nbyte, off_t offset);
```

* **`fildes`** — File descriptor obtained from `open()`.
* **`buf`** — Pointer to destination buffer.
* **`nbyte`** — Number of bytes requested.
* **`offset`** — Starting position in file (`pread()` only).

---

### Return Value

* Returns **number of bytes read** (≥0).
* Returns **0** at end-of-file.
* Returns **-1** on error and sets `errno`.
* May return less than `nbyte` even when not at EOF (partial read).

---

### Common `errno` values

| Error       | Meaning                                           |
| ----------- | ------------------------------------------------- |
| `EAGAIN`    | Non-blocking read would block.                    |
| `EBADF`     | File descriptor not open for reading.             |
| `EINTR`     | Interrupted by signal before completion.          |
| `EISDIR`    | File descriptor refers to a directory.            |
| `EIO`       | I/O error occurred.                               |
| `ENXIO`     | Device does not exist or request invalid.         |
| `ESPIPE`    | `pread()` used on non-seekable file (pipe, FIFO). |
| `EOVERFLOW` | Offset exceeds maximum representable value.       |

---

### Behavior

* Reads up to `nbyte` bytes starting from the file’s current offset.
* Updates the file offset by number of bytes read (`read()` only).
* `pread()` performs same operation without changing file offset.
* For regular files, reading beyond end-of-file returns zero.
* For pipes/FIFOs:

    * Returns `0` if writer closed all descriptors.
    * Blocks until data or EOF unless `O_NONBLOCK` is set.

---

### Short Examples

#### Example 1 — Basic file read

```c
int fd = open("file.txt", O_RDONLY);
char buf[128];
ssize_t r = read(fd, buf, sizeof(buf));
if (r < 0) perror("read");
else write(STDOUT_FILENO, buf, r);
close(fd);
```

#### Example 2 — Handle partial reads

```c
ssize_t total = 0, n;
char buf[1024];
while ((n = read(fd, buf, sizeof(buf))) > 0)
    total += n;
if (n < 0) perror("read");
printf("Total bytes read: %zd\n", total);
```

#### Example 3 — `pread()` for position-independent read

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int fd = open("data.bin", O_RDONLY);
char block[512];
ssize_t n = pread(fd, block, sizeof(block), 1024); // read from offset 1024
if (n == -1) perror("pread");
close(fd);
```

---

### Larger Examples

#### Example 4 — Read entire file to memory

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) { perror("open"); return 1; }

    struct stat st;
    if (fstat(fd, &st) == -1) { perror("fstat"); close(fd); return 1; }

    size_t size = st.st_size;
    char *buf = malloc(size);
    if (!buf) return 1;

    ssize_t read_bytes = 0;
    while (read_bytes < (ssize_t)size) {
        ssize_t r = read(fd, buf + read_bytes, size - read_bytes);
        if (r == 0) break;
        if (r < 0) { perror("read"); break; }
        read_bytes += r;
    }

    write(STDOUT_FILENO, buf, read_bytes);
    free(buf);
    close(fd);
    return 0;
}
```

**Explanation:**
Demonstrates reading the full content of a file using low-level system calls and printing to standard output.

---

#### Example 5 — Safe log tailer using `pread()`

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>

int main(void) {
    int fd = open("log.txt", O_RDONLY);
    if (fd == -1) { perror("open"); return 1; }

    off_t offset = -100;
    struct stat st;
    fstat(fd, &st);
    if (st.st_size + offset < 0) offset = 0;  // prevent negative start

    char buf[101];
    ssize_t n = pread(fd, buf, 100, st.st_size + offset);
    if (n > 0) write(STDOUT_FILENO, buf, n);

    close(fd);
    return 0;
}
```

**Explanation:**
Uses `pread()` to read the last 100 bytes of a file without disturbing the file offset.
Useful in log-monitoring or concurrent access scenarios.

---

### Comparison

| Function  | Seeks?                | Thread-Safe Offset? | Typical Use              |
| --------- | --------------------- | ------------------- | ------------------------ |
| `read()`  | Yes (advances offset) | No                  | Sequential file I/O      |
| `pread()` | No                    | Yes                 | Random or parallel reads |

---

### Notes

* The return value can be smaller than requested; loop until all bytes are read.
* Reading zero bytes (`nbyte == 0`) performs no I/O but may still check errors.
* `read()` is **unbuffered**—data goes directly between kernel and user memory.
* Always verify the return value; a short read is not necessarily an error.
* Use with file descriptors, not `FILE*` streams (for those, use `fread()`).
