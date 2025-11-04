## `chdir()`

### Purpose

Changes the current working directory of the calling process.
After calling `chdir()`, all relative path operations are interpreted relative to the new directory.

---

### Syntax

```c
#include <unistd.h>

int chdir(const char *path);
```

* **`path`** — Pathname of the target directory.
  It may be absolute (starting with `/`) or relative to the current working directory.

---

### Return Value

* On success: returns **0**.
* On failure: returns **-1**, leaves the working directory unchanged, and sets `errno`.

Common `errno` values:

* `EACCES` — Search permission denied for one of the directories in `path`.
* `ENOENT` — Directory does not exist or path is empty.
* `ENOTDIR` — Component of path is not a directory.
* `ELOOP` — Too many symbolic links encountered or cyclic links.
* `ENAMETOOLONG` — Path or a component exceeds system-defined limits.

---

### Short Examples

#### Example 1 — Basic directory change

```c
if (chdir("/tmp") == -1)
    perror("chdir");
else
    puts("Directory changed to /tmp");
```

#### Example 2 — Using relative paths

```c
if (chdir("../logs") == -1)
    perror("chdir");
else
    system("pwd");
```

#### Example 3 — Restore previous directory

```c
char old[256];
getcwd(old, sizeof(old));
chdir("/etc");
printf("Now in /etc\n");
chdir(old);
printf("Returned to %s\n", old);
```

---

### Larger Examples

#### Example 4 — Loop through directories from command-line arguments

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv) {
    char original[512];
    if (!getcwd(original, sizeof(original))) {
        perror("getcwd");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (chdir(argv[i]) == -1) {
            fprintf(stderr, "Cannot enter %s: %s\n", argv[i], strerror(errno));
            continue;
        }

        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)))
            printf("Now in %s\n", cwd);

        if (chdir(original) == -1) {
            perror("chdir restore");
            return 1;
        }
    }

    return 0;
}
```

**Explanation:**
Saves initial directory, then iterates through paths given as parameters, entering each and returning safely.

---

#### Example 5 — Safe directory traversal with `stat()`

```c
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int safe_cd(const char *dir) {
    struct stat sb;
    if (stat(dir, &sb) == -1) {
        perror("stat");
        return -1;
    }

    if (!S_ISDIR(sb.st_mode)) {
        fprintf(stderr, "%s is not a directory\n", dir);
        return -1;
    }

    if (chdir(dir) == -1) {
        perror("chdir");
        return -1;
    }

    char path[256];
    getcwd(path, sizeof(path));
    printf("Now working in: %s\n", path);
    return 0;
}

int main(void) {
    safe_cd("/tmp");
    safe_cd("/does/not/exist");
    return 0;
}
```

**Explanation:**
Checks directory existence and type before changing into it.
Prevents invalid or unsafe path transitions.

---

### Comparison

| Function   | Purpose                                  | Affects process | Notes                                           |
| ---------- | ---------------------------------------- | --------------- | ----------------------------------------------- |
| `chdir()`  | Change current working directory         | Yes             | Applies only to current process                 |
| `fchdir()` | Change directory using a file descriptor | Yes             | Useful for returning to saved locations         |
| `getcwd()` | Retrieve current directory path          | No              | Often paired with `chdir()` to restore position |

---

### Notes

* `chdir()` affects only the calling process, not its parent or children.
* Does not allocate memory; safe to call repeatedly.
* Always check return values to detect missing directories or permission issues.
* Commonly paired with `getcwd()` to save and restore working locations during directory traversal.
