## `write()` / `pwrite()`

### Purpose

Performs unbuffered, low-level output.
Writes bytes from a memory buffer to an open file descriptor.

---

### Syntax

```c
#include <unistd.h>

ssize_t write(int fildes, const void *buf, size_t nbyte);
ssize_t pwrite(int fildes, const void *buf, size_t nbyte, off_t offset);
```

* **`fildes`** — File descriptor obtained from `open()`.
* **`buf`** — Pointer to data to be written.
* **`nbyte`** — Number of bytes to write.
* **`offset`** — File position for `pwrite()` (does not affect current offset).

---

### Return Value

* Returns **number of bytes written** (≤ `nbyte`).
* Returns **-1** on error, sets `errno`.
* A short write (less than `nbyte`) is possible and valid.
* `write()` returns 0 only when writing 0 bytes to a regular file.

---

### Common `errno` values

| Error    | Meaning                                                    |
| -------- | ---------------------------------------------------------- |
| `EAGAIN` | Non-blocking write would block.                            |
| `EBADF`  | Descriptor not open for writing.                           |
| `EFBIG`  | Exceeded file or process size limit.                       |
| `EINTR`  | Interrupted by signal before data written.                 |
| `EIO`    | I/O error (hardware or terminal-related).                  |
| `ENOSPC` | No space on device.                                        |
| `EPIPE`  | Write to pipe/FIFO/socket with no reader → `SIGPIPE` sent. |
| `ESPIPE` | `pwrite()` used on non-seekable descriptor.                |
| `EINVAL` | Invalid arguments, negative offset, or stream mismatch.    |

---

### Behavior

* Writes begin at the file’s **current offset**, which advances by the number of bytes written.
* If `O_APPEND` is set, each write appends to the file end atomically.
* Partial writes can occur when disk space is low or due to signal interruption.
* For pipes/FIFOs:

    * ≤ `PIPE_BUF` bytes → atomic write.
    * > `PIPE_BUF` bytes → may interleave with other writers.
* If `O_NONBLOCK` is set, the call never blocks.
* `pwrite()` writes to a fixed offset without changing the file offset.

---

### Short Examples

#### Example 1 — Basic write

```c
int fd = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
const char *msg = "Hello, world\n";
ssize_t n = write(fd, msg, strlen(msg));
if (n < 0) perror("write");
close(fd);
```

#### Example 2 — Handle partial writes safely

```c
ssize_t total = 0;
while (total < len) {
    ssize_t n = write(fd, buf + total, len - total);
    if (n < 0) { if (errno == EINTR) continue; perror("write"); break; }
    total += n;
}
```

#### Example 3 — Position-specific write using `pwrite`

```c
int fd = open("log.bin", O_WRONLY);
const char data[] = "ENTRY";
if (pwrite(fd, data, sizeof(data) - 1, 1024) == -1)
    perror("pwrite");
close(fd);
```

---

### Larger Examples

#### Example 4 — Simple file copy using `read()` + `write()`

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) return 1;

    int src = open(argv[1], O_RDONLY);
    if (src == -1) { perror("open src"); return 1; }

    int dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst == -1) { perror("open dst"); close(src); return 1; }

    char buf[4096];
    ssize_t n;
    while ((n = read(src, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dst, buf + written, n - written);
            if (w < 0) { perror("write"); close(src); close(dst); return 1; }
            written += w;
        }
    }
    if (n < 0) perror("read");

    close(src);
    close(dst);
    return 0;
}
```

**Explanation:**
Demonstrates low-level copying using system calls. Handles partial writes and ensures full data transfer.

---

#### Example 5 — Log append with `O_APPEND` and signal-safe writes

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

void safe_log(const char *msg) {
    int fd = open("audit.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) return;
    size_t len = strlen(msg);
    while (len > 0) {
        ssize_t n = write(fd, msg, len);
        if (n > 0) { msg += n; len -= n; }
        else if (errno != EINTR) break;
    }
    close(fd);
}

int main(void) {
    safe_log("Session started\n");
    // ... application logic ...
    safe_log("Session ended\n");
    return 0;
}
```

**Explanation:**
Uses `O_APPEND` to prevent race conditions between multiple writers.
`EINTR` is handled to ensure signal safety.

---

### Comparison

| Function   | Seeks? | Offset updated? | Thread-safe offset? | Typical use                |
| ---------- | ------ | --------------- | ------------------- | -------------------------- |
| `write()`  | Yes    | Yes             | No                  | Sequential write           |
| `pwrite()` | Yes    | No              | Yes                 | Random or concurrent write |

---

### Notes

* `write()` is **unbuffered**; it interacts directly with the kernel, not `stdio`.
* Always handle **partial writes**.
* On pipes, writes ≤ `PIPE_BUF` are atomic.
* A `SIGPIPE` signal is sent if writing to a closed pipe/socket.
* For performance, prefer buffered I/O (`fwrite()`) unless precise control is required.
* Use `fsync()` or `fdatasync()` for guaranteed persistence after writing.
