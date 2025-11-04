## `getcwd()`

### Purpose

Retrieves the absolute pathname of the process’s current working directory.
Used to identify where in the filesystem a program is currently operating.

---

### Syntax

```c
#include <unistd.h>

char *getcwd(char *buf, size_t size);
```

* **`buf`** — Pointer to a preallocated character array where the pathname will be stored.
* **`size`** — Size of the buffer in bytes.

If `buf` is `NULL`, `getcwd()` may allocate memory using `malloc()` (non-POSIX extension, GNU only).

---

### Return Value

* On success: returns a pointer to `buf` (or to allocated memory if `buf == NULL`).
* On failure: returns `NULL` and sets `errno`.

Common `errno` values:

* `ERANGE` — Buffer too small to hold pathname.
* `EACCES` — Search permission denied for one of the directories in the path.
* `ENOENT` — Current directory no longer exists.
* `ENOMEM` — Insufficient memory (if dynamic allocation used).

---

### Short Examples

#### Example 1 — Basic usage

```c
char path[256];
if (getcwd(path, sizeof(path)) != NULL)
    printf("Current directory: %s\n", path);
else
    perror("getcwd");
```

#### Example 2 — Detect current directory removal

```c
char path[1024];
if (getcwd(path, sizeof(path)) == NULL) {
    if (errno == ENOENT)
        fprintf(stderr, "Working directory no longer exists.\n");
}
```

#### Example 3 — Change and verify

```c
char path[512];
getcwd(path, sizeof(path));
printf("Before: %s\n", path);
chdir("..");
getcwd(path, sizeof(path));
printf("After: %s\n", path);
```

---

### Larger Examples

#### Example 4 — Save and restore working directory

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define PATH_MAX_LEN 1024

int main(void) {
    char original[PATH_MAX_LEN];
    if (!getcwd(original, sizeof(original))) {
        perror("getcwd");
        return 1;
    }

    printf("Starting in: %s\n", original);

    if (chdir("/tmp") == -1) {
        perror("chdir");
        return 1;
    }

    char current[PATH_MAX_LEN];
    if (getcwd(current, sizeof(current)))
        printf("Now in: %s\n", current);

    if (chdir(original) == -1)
        perror("chdir");

    getcwd(current, sizeof(current));
    printf("Returned to: %s\n", current);
    return 0;
}
```

**Explanation:**
Stores current directory before moving elsewhere, then restores it. This pattern is common when scanning multiple folders in sequence.

---

#### Example 5 — Dynamic path-safe listing

```c
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void list_current_dir(void) {
    DIR *d = opendir(".");
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL)
        printf("%s\n", e->d_name);
    closedir(d);
}

int main(void) {
    char start[512];
    getcwd(start, sizeof(start));
    printf("Scanning directories below: %s\n", start);

    list_current_dir();

    if (chdir("/etc") == 0) {
        printf("\nNow listing /etc:\n");
        list_current_dir();
        chdir(start); // return
    }
    return 0;
}
```

**Explanation:**
Demonstrates `getcwd()` in cooperation with `chdir()` to manage relative directory traversal safely.

---

### Comparison

| Function                 | Purpose                                    | Notes                          |
| ------------------------ | ------------------------------------------ | ------------------------------ |
| `getcwd()`               | Get absolute pathname of current directory | POSIX-compliant and portable   |
| `getwd()`                | Deprecated alternative                     | Unsafe (no buffer size check)  |
| `get_current_dir_name()` | GNU extension                              | Automatically allocates buffer |

---

### Notes

* Always check for `ERANGE`: buffer size must handle full absolute path.
* `PATH_MAX` (from `<limits.h>`) defines the typical upper bound but is not guaranteed.
* Combine with `chdir()` to navigate and restore directories in structured programs.
