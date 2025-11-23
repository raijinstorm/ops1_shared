You parse **option-style arguments** manually or with a helper. Three main approaches:

---

### 1. Manual parsing

Loop through `argv` and match flags:

```c
for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
        p_name = argv[++i];
    } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
        version = argv[++i];
    } else {
        fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
    }
}
```

Simple, portable, no library needed.

---

### 2. Using `getopt()` (standard POSIX)

`<unistd.h>` provides this parser for flags:

```c
#include <unistd.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    char *p_name = NULL;
    char *version = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "p:v:")) != -1) {
        switch (opt) {
        case 'p':
            p_name = optarg;
            break;
        case 'v':
            version = optarg;
            break;
        default:
            fprintf(stderr, "Usage: %s -p name -v version\n", argv[0]);
            return 1;
        }
    }

    printf("p_name=%s, version=%s\n", p_name, version);
}
```

* `optarg` → argument for the current option
* `optind` → next argument index after parsing
* `"p:v:"` → `p` and `v` require arguments

---

### 3. Using `getopt_long()` (GNU extension)

For long options like `--path` or `--version`:

```c
#include <getopt.h>

static struct option long_opts[] = {
    {"path", required_argument, 0, 'p'},
    {"version", required_argument, 0, 'v'},
    {0, 0, 0, 0}
};
```

Then call `getopt_long()` instead of `getopt()`.

---

**Summary:**

* Manual loop = simplest, portable C.
* `getopt()` = standard POSIX.
* `getopt_long()` = GNU extension for `--long` flags.
