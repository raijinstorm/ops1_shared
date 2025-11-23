## `fopen()` / `fdopen()` / `freopen()`

### Purpose

Opens a file and associates it with a **stream** (`FILE *`) for high-level I/O operations (e.g., `fprintf`, `fscanf`, `fread`, `fwrite`).
This is the standard C interface for buffered file I/O.

---

### Syntax

```c
#include <stdio.h>

FILE *fopen(const char *restrict path, const char *restrict mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *restrict path, const char *restrict mode,
              FILE *restrict stream);
```

* **`path`** — Pathname of the file to open or create.
* **`fd`** — Existing open file descriptor (for `fdopen()`).
* **`mode`** — Mode string controlling how the file is opened.
* **`stream`** — Existing stream to reuse (`freopen()` closes it first).

---

### Mode Strings

| Mode   | Meaning                               | Equivalent `open()` flags |         |           |
| ------ | ------------------------------------- | ------------------------- | ------- | --------- |
| `"r"`  | Open for reading                      | `O_RDONLY`                |         |           |
| `"r+"` | Open for reading and writing          | `O_RDWR`                  |         |           |
| `"w"`  | Truncate or create file for writing   | `O_WRONLY                 | O_CREAT | O_TRUNC`  |
| `"w+"` | Create/truncate for read/write        | `O_RDWR                   | O_CREAT | O_TRUNC`  |
| `"a"`  | Append to file (create if not exists) | `O_WRONLY                 | O_CREAT | O_APPEND` |
| `"a+"` | Read and append                       | `O_RDWR                   | O_CREAT | O_APPEND` |

Optional flags:

* `'b'` — Ignored on POSIX systems (kept for portability).
* `'x'` — Exclusive open; fail if file exists (glibc extension).
* `'e'` — Open with `O_CLOEXEC` (glibc extension).

---

### Return Value

* On success: returns a pointer to a `FILE` stream.
* On failure: returns `NULL` and sets `errno`.

Common `errno` values:

* `EACCES` — Permission denied.
* `EEXIST` — File exists and `"x"` used.
* `ENOENT` — File or directory does not exist.
* `EINVAL` — Invalid mode.
* `EMFILE` — Too many open streams.
* `ENOMEM` — Memory allocation failed.

---

### Short Examples

#### Example 1 — Create and write file

```c
FILE *f = fopen("out.txt", "w");
if (!f) {
    perror("fopen");
    exit(EXIT_FAILURE);
}
fprintf(f, "Hello, world!\n");
fclose(f);
```

#### Example 2 — Read file

```c
FILE *f = fopen("config.txt", "r");
if (!f) perror("fopen");
char line[128];
while (fgets(line, sizeof(line), f))
    printf("%s", line);
fclose(f);
```

#### Example 3 — Append mode

```c
FILE *f = fopen("log.txt", "a");
fprintf(f, "Run finished at timestamp %ld\n", time(NULL));
fclose(f);
```

---

### Larger Examples

#### Example 4 — Copy file contents

```c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s src dst\n", argv[0]);
        return 1;
    }

    FILE *src = fopen(argv[1], "rb");
    if (!src) { perror("fopen src"); return 1; }

    FILE *dst = fopen(argv[2], "wb");
    if (!dst) { perror("fopen dst"); fclose(src); return 1; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        fwrite(buf, 1, n, dst);

    fclose(src);
    fclose(dst);
    return 0;
}
```

**Explanation:**
Uses `fopen()`, `fread()`, and `fwrite()` to implement buffered file copying.
Demonstrates binary-safe and portable approach.

---

#### Example 5 — Safe logging system

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

void log_message(const char *filename, const char *message) {
    FILE *log = fopen(filename, "a");
    if (!log) {
        fprintf(stderr, "Log open failed: %s\n", strerror(errno));
        return;
    }

    time_t now = time(NULL);
    fprintf(log, "[%ld] PID %d: %s\n", now, getpid(), message);
    fclose(log);
}

int main(void) {
    log_message("system.log", "Program started");
    sleep(1);
    log_message("system.log", "Completed task 1");
    sleep(1);
    log_message("system.log", "Program finished");
    return 0;
}
```

**Explanation:**
Creates a persistent append log file using `"a"` mode.
Timestamps and process IDs make it useful for multi-run diagnostics.

---

### Comparison

| Function    | Description                               | Notes                                                 |
| ----------- | ----------------------------------------- | ----------------------------------------------------- |
| `fopen()`   | Open file by path, return stream          | Most common; creates or truncates as per mode         |
| `fdopen()`  | Attach stream to existing file descriptor | Useful after `open()` or socket creation              |
| `freopen()` | Reassign existing stream to a new file    | Often used to redirect `stdin`, `stdout`, or `stderr` |
| `open()`    | Low-level unbuffered system call          | Returns `int` file descriptor, not `FILE *`           |

---

### Notes

* All opened streams must be closed with `fclose()` to flush buffers.
* Writing to a read-only stream or reading a write-only stream causes undefined behavior.
* Always check return values and `errno`.
* Mixing `fread()`/`fwrite()` with `fprintf()`/`fscanf()` on same stream requires `fseek()` or `fflush()` between them.
* Files created by `fopen()` use default permission mask `0666 & ~umask`.
