## `fwrite()`

### Purpose

Performs buffered binary output.
Writes a specified number of elements from memory to a file stream.

---

### Syntax

```c
#include <stdio.h>

size_t fwrite(const void *restrict ptr, size_t size, size_t nitems,
              FILE *restrict stream);
```

* **`ptr`** — Pointer to the memory buffer containing the data to write.
* **`size`** — Size in bytes of each element to be written.
* **`nitems`** — Number of elements to write.
* **`stream`** — File stream opened for writing (e.g., `"w"`, `"a"`, `"r+"`, etc.).

---

### Return Value

* Returns the number of **elements successfully written**.
* If this number is less than `nitems`, a write error occurred.
* Returns **0** if either `size` or `nitems` is zero.

On error, the stream’s error indicator is set, and `errno` is updated.
Use `ferror(stream)` to detect the error.

---

### Behavior

* Writes `size × nitems` bytes to the file in order.
* Advances the file-position indicator by the number of bytes written.
* Data may remain buffered until `fflush()` or `fclose()` is called.
* Updates the file’s modification timestamps upon successful completion.
* When writing binary data, no translation (e.g., newline conversion) occurs.

---

### Short Examples

#### Example 1 — Write fixed-size structure

```c
typedef struct {
    int id;
    float value;
} record_t;

record_t rec = {42, 3.14};
FILE *f = fopen("data.bin", "wb");
if (!f) perror("fopen");
size_t written = fwrite(&rec, sizeof(record_t), 1, f);
printf("Elements written: %zu\n", written);
fclose(f);
```

#### Example 2 — Write text buffer

```c
const char text[] = "Hello fwrite()\n";
FILE *f = fopen("output.txt", "w");
fwrite(text, sizeof(char), sizeof(text) - 1, f);
fclose(f);
```

#### Example 3 — Verify write success

```c
FILE *f = fopen("file.bin", "wb");
char data[512] = {0};
if (fwrite(data, 1, sizeof(data), f) != sizeof(data))
    perror("Write failed");
fclose(f);
```

---

### Larger Examples

#### Example 4 — Binary file writer and reader pair

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int id;
    double score;
    char name[32];
} student_t;

int main(void) {
    student_t list[] = {
        {1, 88.5, "Alice"},
        {2, 91.2, "Bob"},
        {3, 79.8, "Charlie"}
    };

    FILE *f = fopen("students.bin", "wb");
    if (!f) { perror("fopen"); return 1; }

    fwrite(list, sizeof(student_t), 3, f);
    fclose(f);

    // Reopen and verify
    f = fopen("students.bin", "rb");
    if (!f) { perror("fopen"); return 1; }

    student_t buf[3];
    fread(buf, sizeof(student_t), 3, f);
    fclose(f);

    for (int i = 0; i < 3; i++)
        printf("%d %s %.1f\n", buf[i].id, buf[i].name, buf[i].score);

    return 0;
}
```

**Explanation:**
Writes an array of structures using `fwrite()` and reads it back using `fread()`.
Demonstrates matching record structure and binary I/O consistency.

---

#### Example 5 — Create a large file with controlled content

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FILE *f = fopen("pattern.bin", "wb");
    if (!f) { perror("fopen"); return 1; }

    char buf[1024];
    memset(buf, 'A', sizeof(buf));

    for (int i = 0; i < 1024; i++) { // ~1 MB total
        if (fwrite(buf, 1, sizeof(buf), f) != sizeof(buf)) {
            perror("fwrite");
            break;
        }
    }

    fclose(f);
    printf("Created 1 MB file filled with 'A'.\n");
    return 0;
}
```

**Explanation:**
Demonstrates repeated buffered writes to generate large binary files efficiently.
`fwrite()`’s internal buffering avoids frequent system calls for high throughput.

---

### Comparison

| Function    | Purpose                     | Direction | Type            |
| ----------- | --------------------------- | --------- | --------------- |
| `fread()`   | Read binary data            | Input     | `FILE *`        |
| `fwrite()`  | Write binary data           | Output    | `FILE *`        |
| `fprintf()` | Write formatted text        | Output    | `FILE *`        |
| `write()`   | Low-level, unbuffered write | Output    | File descriptor |

---

### Notes

* Always check the return value against expected element count.
* Call `fflush()` after `fwrite()` if immediate persistence is needed.
* Mixing text and binary I/O in the same stream is unsafe.
* When exchanging binary files between architectures, account for byte order and structure padding.
* Returns **number of complete elements**, not bytes — multiply by `size` to get total bytes written.
