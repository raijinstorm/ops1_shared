## sigaction()

### Purpose

Install, modify, or query the disposition of a POSIX signal, including handler functions, masks, and delivery behavior.

---

### Syntax

```c
#include <signal.h>

int sigaction(int sig, const struct sigaction *restrict act,
              struct sigaction *restrict oact);
```

---

### Parameters

| Parameter | Description                                                                                                      |
| --------- | ---------------------------------------------------------------------------------------------------------------- |
| `sig`     | Signal number to query or modify. Must be a valid signal defined in `<signal.h>`.                                |
| `act`     | Pointer to a `struct sigaction` describing the new action for `sig`. If `NULL`, the disposition is unchanged.    |
| `oact`    | Pointer to a `struct sigaction` where the previous action is stored. If `NULL`, previous action is not returned. |

---

### Behavior

`sigaction()` sets or retrieves the disposition of a signal. When `act` is non-NULL, it specifies how the process should handle `sig`: ignore, perform the default action, or catch the signal via a handler. When `SA_SIGINFO` is cleared, the handler uses the `sa_handler` function pointer; when set, the 3-argument `sa_sigaction` form is used.

Upon setting an action, the kernel associates `sa_mask`, `sa_flags`, and either `sa_handler` or `sa_sigaction` with the signal. When a signal is delivered, the kernel forms a temporary signal mask for the handler by combining the thread’s current mask with `sa_mask`, and unless `SA_NODEFER` or `SA_RESETHAND` applies, the signal itself is added to that mask.

Dispositions remain in effect until changed, reset by `SA_RESETHAND`, or cleared by a successful `execve(2)`. `SIGKILL` and `SIGSTOP` cannot be caught or ignored; attempts to modify them are restricted. If `oact` is provided, it receives the previously installed action.

---

### Return Value

| Return | Meaning                                                              |
| ------ | -------------------------------------------------------------------- |
| `0`    | Operation succeeded; action installed or queried.                    |
| `-1`   | Failure; no new action installed. `errno` set to indicate the error. |

---

### Common `errno` Values

| Error                     | Meaning                                                                                                           |
| ------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `EINVAL`                  | `sig` invalid; attempt to catch/ignore an uncatchable signal; attempt to set `SIG_DFL` for an uncatchable signal. |
| `EINVAL` (optional cases) | Using `SA_SIGINFO` for signals outside the real-time range on systems lacking XSI option support.                 |

---

### Example — Installing a Simple Handler

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void handler(int signo)
{
    printf("Received SIGINT\n");
}

int main(void)
{
    struct sigaction sa;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        pause(); /* Wait for signals */
    }
}
```

**Explanation:**
This program sets a one-argument handler for `SIGINT`, requesting system call restart via `SA_RESTART`. The handler prints a message, and the process waits indefinitely using `pause()`.

---

### Example — Using `SA_SIGINFO`

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void handler(int signo, siginfo_t *info, void *ucontext)
{
    printf("Signal %d from PID %ld\n", signo, (long)info->si_pid);
}

int main(void)
{
    struct sigaction sa;

    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        pause();
    }
}
```

**Explanation:**
With `SA_SIGINFO`, the handler receives a `siginfo_t` object. This example prints the sender PID for `SIGUSR1`.

---


### Key Notes

* `sa_handler` and `sa_sigaction` share storage and are mutually exclusive; only one may be used.
* Handlers execute with a temporary mask combining `sa_mask` and possibly the signal itself.
* `SA_RESETHAND` resets the disposition on entry and behaves as if `SA_NODEFER` may be implied.
* `SA_ONSTACK` routes handler execution to an alternate stack configured via `sigaltstack()`.
* `SIGKILL` and `SIGSTOP` cannot be caught, blocked, or ignored.
* Dispositions survive `fork()` but caught dispositions reset to defaults on `execve()`.

---

### Comparison

| Feature                     | `signal()` | `sigaction()`      |
| --------------------------- | ---------- | ------------------ |
| Portable handler semantics  | No         | Yes                |
| Mask control                | No         | Yes (`sa_mask`)    |
| Flags controlling behavior  | No         | Yes (`sa_flags`)   |
| Optional 3-argument handler | No         | Yes (`SA_SIGINFO`) |
| Recommended API             | No         | Yes                |

---

### Summary

`sigaction()` is the standard POSIX interface for installing and querying signal dispositions. It provides fine-grained control over handler behavior, blocked signals during handler execution, extended information delivery via `SA_SIGINFO`, and restart semantics through `SA_RESTART`. It supersedes `signal()` and should be used in all modern signal-handling code.
