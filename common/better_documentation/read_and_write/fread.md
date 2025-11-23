## `fread()`

### Purpose

Performs buffered binary input.
Reads a specified number of data elements from a file stream into memory.

---

### Syntax

```c
#include <stdio.h>

size_t fread(void *restrict ptr, size_t size, size_t nitems,
             FILE *restrict stream);
```

* **`ptr`** — Pointer to the memory buffer where data is stored.
* **`size`** — Size in bytes of each element to read.
* **`nitems`** — Number of elements to read.
* **`stream`** — File stream opened with `fopen()` in a readable mode.

---

### Return Value

* Returns the number of **elements successfully read**.
* The return value is less than `nitems` only if:

    * End-of-file (`EOF`) was reached, or
    * A read error occurred.
* Returns **0** if either `size` or `nitems` is zero.

Use `feof(stream)` or `ferror(stream)` to distinguish between EOF and error.

---

### Behavior

* Reads `size × nitems` bytes from the file into memory.
* Advances the file-position indicator by the number of bytes read.
* May read partial elements at EOF; their content is unspecified.
* Sets the stream’s error indicator on failure and sets `errno`.

---

### Short Examples

#### Example 1 — Read fixed-size block

```c
FILE *f = fopen("block.bin", "rb");
char buf[100];
size_t read_count = fread(buf, sizeof(buf), 1, f);
printf("Elements read: %zu\n", read_count);
fclose(f);
```

#### Example 2 — Read bytes

```c
char buf[256];
FILE *f = fopen("data.bin", "rb");
size_t bytes = fread(buf, 1, sizeof(buf), f);
printf("Bytes read: %zu\n", bytes);
fclose(f);
```

#### Example 3 — Check EOF or error

```c
char buf[64];
FILE *f = fopen("file.txt", "r");
fread(buf, 1, sizeof(buf), f);
if (feof(f)) puts("End of file");
if (ferror(f)) perror("fread error");
fclose(f);
```

---

### Larger Examples

#### Example 4 — Binary record reader

```c
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int id;
    double value;
    char name[32];
} record_t;

int main(void) {
    FILE *f = fopen("records.bin", "rb");
    if (!f) { perror("fopen"); return 1; }

    record_t r;
    while (fread(&r, sizeof(record_t), 1, f) == 1)
        printf("ID:%d Value:%.2f Name:%s\n", r.id, r.value, r.name);

    if (ferror(f)) perror("fread");
    fclose(f);
    return 0;
}
```

**Explanation:**
Reads structured binary records directly into memory.
Stops when EOF reached or a read error occurs.

---

#### Example 5 — Copy arbitrary binary file

```c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s src dst\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror("fopen src"); return 1; }

    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror("fopen dst"); fclose(in); return 1; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);

    if (ferror(in)) perror("Read error");
    fclose(in);
    fclose(out);
    return 0;
}
```

**Explanation:**
Demonstrates typical use of `fread()` with `fwrite()` for efficient binary file copying.
Handles partial reads and buffer flushing automatically through standard I/O buffering.

---

### Comparison

| Function             | Purpose                | Stream Type     | Typical Use                    |
| -------------------- | ---------------------- | --------------- | ------------------------------ |
| `fread()`            | Buffered binary read   | `FILE *`        | Copying or loading binary data |
| `fgetc()` / `getc()` | Read one byte/char     | `FILE *`        | Text or per-character input    |
| `fscanf()`           | Formatted text read    | `FILE *`        | Reading structured text        |
| `read()`             | Unbuffered system call | File descriptor | Low-level I/O (no buffering)   |

---

### Notes

* Always verify results using `feof()` or `ferror()`.
* Mixing `fread()` with formatted I/O on the same stream requires `fseek()` or `fflush()`.
* `fread()` reads raw bytes; ensure correct endianness and structure alignment when exchanging binary files across platforms.
* Returns count of complete **elements**, not bytes—use `(size_t)elements_read * size` for total bytes.

