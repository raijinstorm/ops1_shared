## `getline()`

### Purpose

Reads a **line** (or delimited record) from a stream, allocating or expanding the buffer automatically.
It is a convenience wrapper over `getdelim()` with delimiter fixed to newline (`'\n'`).

---

### Syntax

```c
#include <stdio.h>

ssize_t getline(char **restrict lineptr, size_t *restrict n,
                FILE *restrict stream);
```

---

### Parameters

| Parameter | Description                                                                            |
| --------- | -------------------------------------------------------------------------------------- |
| `lineptr` | Pointer to a `char*` buffer. `getline()` allocates memory if `*lineptr == NULL`.       |
| `n`       | Pointer to `size_t` that stores current buffer size. Updated if buffer is reallocated. |
| `stream`  | Input stream (e.g. `stdin`, or a file opened by `fopen`).                              |

---

### Behavior

* Reads characters from `stream` until a newline (`'\n'`) or EOF.
* Stores the line, including the newline, into `*lineptr`, null-terminated.
* If needed, reallocates `*lineptr` and updates `*n`.
* The buffer persists for reuse by future `getline()` calls (no need to free and reallocate each time).
* On EOF with no data read, returns `-1`.

---

### Return Value

| Return | Meaning                                                                     |
| ------ | --------------------------------------------------------------------------- |
| ≥ 0    | Number of characters read (including the newline, excluding the null byte). |
| -1     | End of file or error. In both cases, `*lineptr` remains valid.              |

---

### Common `errno` values

| Error    | Meaning                                         |
| -------- | ----------------------------------------------- |
| `EINVAL` | Invalid pointers (`lineptr` or `n` are `NULL`). |
| `ENOMEM` | Memory allocation failure.                      |

---

### Example 1 — Read lines from stdin

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    while ((nread = getline(&line, &len, stdin)) != -1)
        printf("Read %zd chars: %s", nread, line);

    free(line);
    return 0;
}
```

**Explanation:**
`getline()` dynamically resizes `line` to hold each full input line.
It is ideal for reading unknown-length text safely.

---

### Example 2 — Read configuration file

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FILE *fp = fopen("config.txt", "r");
    if (!fp) return 1;

    char *line = NULL;
    size_t len = 0;

    while (getline(&line, &len, fp) != -1) {
        if (line[0] == '#' || line[0] == '\n') continue;  // skip comments
        line[strcspn(line, "\n")] = '\0';                 // remove newline
        printf("Config entry: %s\n", line);
    }

    free(line);
    fclose(fp);
    return 0;
}
```

**Explanation:**
Reads a text file line by line, ignoring blank and comment lines.

---

### Notes

* Equivalent to:

  ```c
  getdelim(lineptr, n, '\n', stream);
  ```
* Works safely with long or unbounded lines.
* Always free `*lineptr` when done.
* Do **not** use with streams opened in binary mode.
* Portable to all POSIX.1-2008 systems.

---

### Comparison

| Function     | Delimiter | Auto memory allocation | Typical use        |
| ------------ | --------- | ---------------------- | ------------------ |
| `fgets()`    | Newline   | No                     | Fixed buffer reads |
| `getdelim()` | Custom    | Yes                    | Custom delimiter   |
| `getline()`  | Newline   | Yes                    | Read text lines    |

---

### Summary

`getline()` simplifies dynamic line reading:

* Reads complete lines safely.
* Handles buffer allocation automatically.
* Returns precise byte count for flexible parsing.
