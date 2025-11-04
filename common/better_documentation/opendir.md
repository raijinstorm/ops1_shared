## `opendir()`

### Purpose

Opens a directory stream corresponding to a given directory path, allowing iteration through its entries with `readdir()`.

---

### Syntax

```c
#include <dirent.h>

DIR *opendir(const char *dirname);
DIR *fdopendir(int fd);
```

* **`dirname`** — Pathname of the directory to open.
* **`fd`** — File descriptor referring to an open directory (from `open()` with `O_DIRECTORY` flag).

Both return a pointer to a `DIR` object used by directory functions such as `readdir()` and `closedir()`.

---

### Return Value

* On success: returns a non-NULL pointer to a `DIR` structure.
* On failure: returns `NULL` and sets `errno` to indicate the error.

Common `errno` values:

* `EACCES` — Permission denied.
* `ENOTDIR` — `dirname` is not a directory.
* `ENOENT` — Directory does not exist.
* `EMFILE` or `ENFILE` — Too many open file descriptors.
* `ENOMEM` — Insufficient memory.

---

### Examples

#### Example 1 — Basic open and read

```c
DIR *dir = opendir("/etc");
if (!dir) {
    perror("opendir");
    exit(EXIT_FAILURE);
}

struct dirent *entry;
while ((entry = readdir(dir)) != NULL)
    printf("%s\n", entry->d_name);

closedir(dir);
```

#### Example 2 — Handling errors gracefully

```c
DIR *dir = opendir("/root");
if (!dir) {
    fprintf(stderr, "Cannot open directory: %s\n", strerror(errno));
} else {
    closedir(dir);
}
```

#### Example 3 — Using `fdopendir`

```c
int fd = open("/usr/bin", O_RDONLY | O_DIRECTORY);
if (fd == -1) ERR("open");

DIR *dir = fdopendir(fd);
if (!dir) ERR("fdopendir");

closedir(dir);
```

---

### Comparison

| Function      | Purpose                                       | Input               | Notes                                              |
| ------------- | --------------------------------------------- | ------------------- | -------------------------------------------------- |
| `opendir()`   | Opens a directory by name                     | Path string         | Standard way to begin directory traversal          |
| `fdopendir()` | Opens directory from existing file descriptor | File descriptor     | Useful when directory already opened with `open()` |
| `open()`      | Opens any file (regular or directory)         | Path string + flags | Returns int descriptor, not a DIR stream           |
| `readdir()`   | Reads entries from DIR stream                 | `DIR *`             | Used after `opendir()`                             |

---

### Notes

* Always close with `closedir()` to free resources.
* After successful `opendir()`, directory entries are read in unspecified order.
* Behavior is defined by POSIX.1-2008.
