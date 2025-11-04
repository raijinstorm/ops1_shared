## `writev()`

### Purpose

Performs **gathered I/O** — writes data from multiple memory buffers to a single file descriptor in one system call.
This reduces system-call overhead compared to multiple `write()` calls.

---

### Syntax

```c
#include <sys/uio.h>

ssize_t writev(int fildes, const struct iovec *iov, int iovcnt);
```

* **`fildes`** — File descriptor opened for writing.
* **`iov`** — Pointer to an array of `struct iovec` elements.
* **`iovcnt`** — Number of buffers in `iov` (must be >0 and ≤ `IOV_MAX`).

```c
struct iovec {
    void  *iov_base;  // starting address of buffer
    size_t iov_len;   // length of buffer
};
```

---

### Return Value

* Returns **number of bytes written** (≤ sum of all `iov_len`).
* Returns **-1** on failure, leaves file offset unchanged, sets `errno`.
* A short write is valid (e.g., if disk full or interrupted).

---

### Common `errno` values

Inherits all `write()` errors, plus:

| Error                          | Meaning                                                               |
| ------------------------------ | --------------------------------------------------------------------- |
| `EINVAL`                       | `iovcnt` ≤ 0 or > `IOV_MAX`; or sum of `iov_len` overflows `ssize_t`. |
| `EFAULT`                       | Any `iov_base` points to inaccessible memory.                         |
| `ENOSPC`, `EPIPE`, `EIO`, etc. | Same as `write()`.                                                    |

---

### Behavior

* Equivalent to calling multiple `write()`s sequentially for each buffer, but atomic at the system-call level.
* File offset advances by total bytes written.
* For sockets or pipes, writes may be partial if capacity limits are reached.
* If all `iov_len` values are 0 for a regular file, returns 0 and does nothing.

---

### Short Examples

#### Example 1 — Basic multi-buffer write

```c
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>

struct iovec iov[3];
iov[0].iov_base = "first\n";
iov[0].iov_len  = 6;
iov[1].iov_base = "second\n";
iov[1].iov_len  = 7;
iov[2].iov_base = "third\n";
iov[2].iov_len  = 6;

int fd = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
ssize_t n = writev(fd, iov, 3);
if (n < 0) perror("writev");
close(fd);
```

#### Example 2 — Writing to a socket

```c
struct iovec parts[2];
parts[0].iov_base = "HTTP/1.1 200 OK\r\n";
parts[0].iov_len  = 17;
parts[1].iov_base = "Content-Length: 0\r\n\r\n";
parts[1].iov_len  = 21;
writev(sockfd, parts, 2);
```

---

### Larger Examples

#### Example 3 — Efficient log write with prefix and message

```c
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

void log_message(int fd, const char *msg) {
    char prefix[64];
    time_t t = time(NULL);
    int len = snprintf(prefix, sizeof(prefix), "[%ld] ", t);

    struct iovec iov[2];
    iov[0].iov_base = prefix;
    iov[0].iov_len = len;
    iov[1].iov_base = (void *)msg;
    iov[1].iov_len = strlen(msg);

    if (writev(fd, iov, 2) == -1) perror("writev");
}

int main(void) {
    int fd = open("app.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    log_message(fd, "Started service\n");
    log_message(fd, "Task complete\n");
    close(fd);
    return 0;
}
```

**Explanation:**
Combines timestamp and message writes atomically, preventing interleaving in concurrent logging.

---

#### Example 4 — Gathered write for binary structures

```c
#include <sys/uio.h>
#include <unistd.h>
#include <stdint.h>

struct header {
    uint32_t type;
    uint32_t length;
} hdr = {1, 8};
uint64_t data = 0x123456789ABCDEF0ULL;

struct iovec iov[2] = {
    { &hdr, sizeof(hdr) },
    { &data, sizeof(data) }
};

int fd = open("packet.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
if (writev(fd, iov, 2) == -1) perror("writev");
close(fd);
```

**Explanation:**
Writes structured binary data (header + payload) in one atomic system call.

---

### Comparison

| Function    | Direction | Buffers  | Moves offset | Atomic across buffers | Typical use        |
| ----------- | --------- | -------- | ------------ | --------------------- | ------------------ |
| `write()`   | Output    | 1        | Yes          | N/A                   | Write single block |
| `writev()`  | Output    | Multiple | Yes          | Yes                   | Gathered I/O       |
| `readv()`   | Input     | Multiple | Yes          | Yes                   | Scatter I/O        |
| `pwritev()` | Output    | Multiple | No           | Yes                   | Random writes      |

---

### Notes

* Reduces syscall overhead for fragmented data output.
* Always check return value; partial writes can occur.
* `IOV_MAX` (from `<limits.h>`) defines the maximum number of vectors allowed.
* Works with files, sockets, pipes, and devices.
* Thread-safe on distinct file descriptors; synchronize if sharing one.
