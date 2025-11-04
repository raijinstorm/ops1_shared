## `strtok()`

### Purpose

Splits a string into **tokens** separated by specified delimiters.
Used to parse space-, comma-, or colon-separated data.

---

### Syntax

```c
#include <string.h>

char *strtok(char *restrict str, const char *restrict delim);
```

---

### Parameters

| Parameter | Description                                                                                               |
| --------- | --------------------------------------------------------------------------------------------------------- |
| `str`     | Input string to tokenize. Pass the string on the first call, then `NULL` on subsequent calls to continue. |
| `delim`   | String containing all delimiter characters. Each character is treated individually as a separator.        |

---

### Behavior

* On the **first call**, `strtok(str, delim)` scans `str` for the first token.
* Replaces the first delimiter after that token with a null byte (`'\0'`) and returns a pointer to the token.
* On **subsequent calls**, pass `NULL` as `str` to continue tokenizing the same string.
* Continues until no tokens remain, returning `NULL`.
* The internal static pointer used makes it **non-reentrant** and **not thread-safe**.
  (Use `strtok_r()` in multi-threaded code.)

---

### Return Value

| Return  | Meaning                            |
| ------- | ---------------------------------- |
| `char*` | Pointer to the next token found.   |
| `NULL`  | No more tokens or no tokens found. |

---

### Example 1 — Split words in a sentence

```c
#include <stdio.h>
#include <string.h>

int main(void) {
    char text[] = "The quick brown fox";
    char *word = strtok(text, " ");

    while (word != NULL) {
        printf("%s\n", word);
        word = strtok(NULL, " ");
    }
}
```

**Output:**

```
The
quick
brown
fox
```

**Explanation:**
Delimiters are spaces. `strtok()` replaces each space with `'\0'` and returns successive tokens.

---

### Example 2 — Parse colon-separated PATH

```c
#include <stdio.h>
#include <string.h>

int main(void) {
    char path[] = "/usr/bin:/bin:/usr/local/bin";
    for (char *dir = strtok(path, ":"); dir; dir = strtok(NULL, ":"))
        printf("Dir: %s\n", dir);
}
```

**Explanation:**
Splits `$PATH` into individual directories.

---

### Example 3 — Multiple delimiters

```c
char text[] = "red,green;blue yellow";
char *tok = strtok(text, ",; ");
while (tok) {
    printf("Color: %s\n", tok);
    tok = strtok(NULL, ",; ");
}
```

Handles commas, semicolons, and spaces all as valid separators.

---

### Notes

* `strtok()` modifies the input string (never use it on string literals).
* Tokens are non-empty substrings separated by any of the delimiter characters.
* Thread-safe alternative:

  ```c
  strtok_r(char *restrict str, const char *restrict delim,
           char **restrict saveptr);
  ```

  which keeps parsing state in `saveptr`.

---

### Comparison

| Function     | Thread-safe | Modifies input | Multi-delimiter support | Typical use                        |
| ------------ | ----------- | -------------- | ----------------------- | ---------------------------------- |
| `strtok()`   | No          | Yes            | Yes                     | Simple token parsing               |
| `strtok_r()` | Yes         | Yes            | Yes                     | Thread-safe variant                |
| `strsep()`   | Yes         | Yes            | Yes                     | BSD alternative, simpler semantics |

---

### Summary

`strtok()` is a simple, destructive tokenizer:

* Splits strings by character delimiters.
* Maintains internal state.
* Suitable for single-threaded, simple parsing tasks.
