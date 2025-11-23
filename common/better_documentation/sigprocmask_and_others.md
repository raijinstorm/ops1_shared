## sigemptyset()

### Purpose

Initialize a signal set so that it contains no signals.

### Syntax

```c
#include <signal.h>

int sigemptyset(sigset_t *set);
```

### Behavior

Sets all bits in the signal set to zero, producing an empty set. Used before adding signals with `sigaddset()`.

### Return Value

| Return | Meaning                                                          |
| ------ | ---------------------------------------------------------------- |
| `0`    | Success.                                                         |
| `-1`   | Failure; `errno` may be set (rare and implementation-dependent). |

---

## sigfillset()

### Purpose

Initialize a signal set so that it contains all signals.

### Syntax

```c
#include <signal.h>

int sigfillset(sigset_t *set);
```

### Behavior

Sets all bits in the signal set, marking every signal as a member. Useful when selectively removing signals.

### Return Value

| Return | Meaning                                                          |
| ------ | ---------------------------------------------------------------- |
| `0`    | Success.                                                         |
| `-1`   | Failure; `errno` may be set (rare and implementation-dependent). |

---

## sigprocmask()

### Purpose

Examine and/or change the calling thread’s signal mask.

### Syntax

```c
#include <signal.h>

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
```

### Behavior

Modifies or retrieves the thread’s current signal mask.

* `how` determines the operation:

    * `SIG_BLOCK`: add signals in `set` to the mask
    * `SIG_UNBLOCK`: remove signals in `set` from the mask
    * `SIG_SETMASK`: replace the mask with `set`
      If `oldset` is non-NULL, the previous mask is returned. Cannot block uncatchable signals (`SIGKILL`, `SIGSTOP`).

### Return Value

| Return | Meaning                                                              |
| ------ | -------------------------------------------------------------------- |
| `0`    | Success; mask updated or queried.                                    |
| `-1`   | Error; `errno` indicates failure (e.g., `EINVAL` for invalid `how`). |

---

## sigaddset()

### Purpose

Add a specific signal to an existing signal set.

### Syntax

```c
#include <signal.h>

int sigaddset(sigset_t *set, int signo);
```

### Behavior

Adds `signo` to the signal set referenced by `set`. The set must be initialized with `sigemptyset()` or `sigfillset()` first.

### Return Value

| Return | Meaning                                                           |
| ------ | ----------------------------------------------------------------- |
| `0`    | Success; signal added to the set.                                 |
| `-1`   | Error; invalid `signo`, or other implementation-dependent errors. |

---

If you want, I can also generate compact comparison tables or combined usage patterns (e.g., "block signals safely in critical sections").
