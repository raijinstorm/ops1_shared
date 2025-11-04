## `fseek()` / `fseeko()`

### Purpose

Moves the **file-position indicator** within a stream.
Used to jump to a specific byte offset in a file for reading or writing.

---

### Syntax

```c
#include <stdio.h>

int fseek(FILE *stream, long offset, int whence);
int fseeko(FILE *stream, off_t offset, int whence);
```

* **`stream`** — Pointer to a `FILE` stream opened with `fopen()` or similar.
* **`offset`** — Number of bytes to move relative to a reference point.
* **`whence`** — Specifies the reference position:

    * `SEEK_SET`: from beginning of file
    * `SEEK_CUR`: from current position
    * `SEEK_END`: from end of file

`fseeko()` is identical to `fseek()` but uses `off_t` instead of `long` for large files.

---

### Return Value

* On success: returns **0**.
* On failure: returns **-1** and sets `errno`.

Common `errno` values:

* `EINVAL` — Invalid `whence`, or resulting position < 0.
* `ESPIPE` — Stream not seekable (e.g. pipe, socket).
* `EOVERFLOW` — Resulting offset too large.
* `EIO`, `ENOSPC`, `EFBIG` — I/O or space-related failure while syncing buffers.

---

### Behavior

* Clears the end-of-file indicator (`feof`).
* Flushes unwritten buffered data if the stream is writable.
* Allows seeking beyond EOF; unwritten space is filled with zeros when written.
* Requires an `fseek()` or `fflush()` between read and write operations on a read/write stream (`r+`, `w+`, `a+`).

---

### Short Examples

#### Example 1 — Move to file start

```c
FILE *f = fopen("data.txt", "r");
fseek(f, 0, SEEK_SET);
```

#### Example 2 — Skip first 100 bytes

```c
fseek(f, 100, SEEK_CUR);
```

#### Example 3 — Get file size

```c
fseek(f, 0, SEEK_END);
long size = ftell(f);
printf("File size: %ld bytes\n", size);
fseek(f, 0, SEEK_SET);
```

---

### Larger Examples

#### Example 4 — Random access read

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *f = fopen("records.bin", "rb");
    if (!f) { perror("fopen"); return 1; }

    int index = 5;
    long record_size = sizeof(int) * 3; // assume record = 3 integers
    if (fseek(f, index * record_size, SEEK_SET) == -1) {
        perror("fseek");
        fclose(f);
        return 1;
    }

    int data[3];
    if (fread(data, sizeof(int), 3, f) == 3)
        printf("Record %d: %d %d %d\n", index, data[0], data[1], data[2]);
    else
        perror("fread");

    fclose(f);
    return 0;
}
```

**Explanation:**
Reads a specific fixed-size record directly from a binary file without scanning earlier data.

---

#### Example 5 — Sparse file creation

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *f = fopen("sparse.bin", "w+");
    if (!f) { perror("fopen"); return 1; }

    // Write header
    fprintf(f, "HEADER\n");

    // Move 1 MB ahead without writing data
    if (fseek(f, 1024 * 1024, SEEK_CUR) == -1) {
        perror("fseek");
        fclose(f);
        return 1;
    }

    // Write footer
    fprintf(f, "FOOTER\n");

    fclose(f);
    printf("Created sparse file of 1 MB with only header/footer data.\n");
    return 0;
}
```

**Explanation:**
Demonstrates how `fseek()` can move the write pointer past EOF to create sparse files filled with implicit zeroes.
Efficient on modern filesystems (no actual disk space used for skipped gaps).

---

### Comparison

| Function               | Purpose                                   | Type safety                 | Large file support          |
| ---------------------- | ----------------------------------------- | --------------------------- | --------------------------- |
| `fseek()`              | Move file position                        | Uses `long`                 | May overflow on large files |
| `fseeko()`             | Same, but uses `off_t`                    | Safer for big files (>2 GB) | Yes                         |
| `ftell()` / `ftello()` | Query current file position               | Complements `fseek()`       | Same type difference        |
| `rewind()`             | Shortcut for `fseek(stream, 0, SEEK_SET)` | Simplest reset              | Always clears errors        |

---

### Notes

* Not all streams support seeking (e.g., pipes, sockets).
* After seeking, the next read/write occurs from the new position.
* Always pair with `ftell()` when storing and restoring positions.
* `fseeko()` is preferred in POSIX code for large-file portability.
