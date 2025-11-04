## `strdup()`

### Purpose

Creates a **duplicate of a string** in newly allocated memory.
Equivalent to `malloc(strlen(s) + 1)` followed by `strcpy()`.

---

### Syntax

```c
#include <string.h>

char *strdup(const char *s);
```

---

### Parameters

| Parameter | Description                                       |
| --------- | ------------------------------------------------- |
| `s`       | Pointer to a null-terminated string to duplicate. |

---

### Behavior

* Allocates enough memory using `malloc()` to hold a copy of `s` plus the terminating null byte.
* Copies the string contents into the new memory.
* Returns a pointer to the newly allocated copy.
* The returned pointer must be freed with `free()` when no longer needed.
* If memory cannot be allocated, returns `NULL` and sets `errno` to `ENOMEM`.

---

### Return Value

| Return  | Meaning                                                    |
| ------- | ---------------------------------------------------------- |
| `char*` | Pointer to a heap-allocated duplicate of the input string. |
| `NULL`  | Memory allocation failed. `errno` = `ENOMEM`.              |

---

### Example 1 — Basic duplication

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char *original = "Hello, POSIX!";
    char *copy = strdup(original);

    if (!copy) {
        perror("strdup");
        return 1;
    }

    printf("Original: %s\nCopy: %s\n", original, copy);
    free(copy);
}
```

**Explanation:**
Allocates a new buffer and copies `"Hello, POSIX!"` into it.
Both pointers point to distinct memory blocks.

---

### Example 2 — Safe use in string management

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct person {
    char *name;
};

int main(void) {
    struct person p;
    p.name = strdup("Ada Lovelace");
    if (!p.name) return 1;

    printf("Name: %s\n", p.name);
    free(p.name);
}
```

**Explanation:**
`strdup()` is often used in structures or dynamic containers where string ownership must be transferred or persisted independently of the original buffer.

---

### Notes

* The allocated memory comes from the **heap**, not static storage.
* You must call `free()` on the returned pointer.
* `strdup()` is **not part of ISO C**, but it is standardized by POSIX.1-2008.
* Some systems also provide `strndup(const char *s, size_t n)` — duplicates at most `n` bytes.

---

### Comparison

| Function    | Allocation | Copies null terminator | Truncates input    | Standard     |
| ----------- | ---------- | ---------------------- | ------------------ | ------------ |
| `strdup()`  | Yes        | Yes                    | No                 | POSIX.1      |
| `strndup()` | Yes        | Yes                    | Yes (limit by `n`) | POSIX.1-2008 |
| `strcpy()`  | No         | Yes                    | No                 | ISO C        |

---

### Summary

`strdup()` = allocate + copy + null-terminate.
Fast way to create an independent heap copy of an existing string.
