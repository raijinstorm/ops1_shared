## sigsuspend()

### Purpose

Atomically replace the calling thread’s signal mask and suspend execution until an unblocked signal is delivered and handled.

---

### Syntax

```c
#include <signal.h>

int sigsuspend(const sigset_t *sigmask);
```

---

### Parameters

| Parameter | Description                                                                                                                                                         |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `sigmask` | Replacement signal mask installed for the duration of the suspension. Signals in this set are blocked while waiting; all others are eligible to interrupt the call. |

---

### Behavior

`sigsuspend()` temporarily replaces the thread’s current signal mask with the one referenced by `sigmask` and immediately suspends execution. Only signals that are unblocked by the new mask can end the suspension. Pending signals do not migrate between process-level and thread-level pending queues as a result of the mask change.

The call returns only if an unblocked signal is delivered and a handler for that signal returns. If the delivered signal's disposition is termination, the process ends and `sigsuspend()` never returns. After handler completion, the thread’s original signal mask is restored automatically.

Signals that cannot be ignored (such as `SIGKILL` or `SIGSTOP`) cannot be blocked by the replacement mask; the kernel silently enforces this.

---

### Return Value

| Return | Meaning                                                                                            |
| ------ | -------------------------------------------------------------------------------------------------- |
| `-1`   | Returned only when a signal is caught and its handler returns. `errno` is set to indicate `EINTR`. |

There is no successful return value because the function always suspends indefinitely until interrupted.

---

### Common `errno` Values

| Error   | Meaning                                                                   |
| ------- | ------------------------------------------------------------------------- |
| `EINTR` | A signal handler executed and returned, causing `sigsuspend()` to resume. |

---

### Example — Waiting for a Specific Signal

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void handler(int signo)
{
    write(STDOUT_FILENO, "Caught signal\n", 14);
}

int main(void)
{
    struct sigaction sa;
    sigset_t block_all, oldmask;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    sigfillset(&block_all);
    if (sigprocmask(SIG_SETMASK, &block_all, &oldmask) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    /* Unblock SIGUSR1 only while waiting */
    sigset_t waitmask;
    sigemptyset(&waitmask);

    errno = 0;
    sigsuspend(&waitmask);  /* Returns only after SIGUSR1 handler runs */

    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    return 0;
}
```

**Explanation:**
This example blocks all signals, installs a handler for `SIGUSR1`, and then uses `sigsuspend()` with an empty mask to wait exclusively for unblocked signals. When `SIGUSR1` arrives, the handler executes, `sigsuspend()` returns with `EINTR`, and the old mask is restored.

---

### Key Notes

* `sigsuspend()` performs an atomic mask replacement and suspension, avoiding race conditions inherent in manual unblock–pause sequences.
* The original mask is always restored after handler return.
* Only signals unblocked by the temporary mask can interrupt the suspension.
* Handlers are executed before `sigsuspend()` returns.
* Cannot block uncatchable signals (`SIGKILL`, `SIGSTOP`).
* Typically paired with `sigprocmask()` in critical-section patterns.

---

### Comparison

| Feature                   | `pause()`           | `sigsuspend()`                             |
| ------------------------- | ------------------- | ------------------------------------------ |
| Modify mask while waiting | No                  | Yes                                        |
| Avoid race conditions     | No                  | Yes                                        |
| Returns after handler     | Yes (EINTR)         | Yes (EINTR)                                |
| Typical usage             | Wait for any signal | Wait for specific unblocked signals safely |

---

### Summary

`sigsuspend()` is the race-free mechanism for suspending a thread until delivery of an unblocked signal. It atomically installs a temporary mask, waits for a handled or terminating signal, and restores the previous mask afterward. It is essential for reliable signal-wait patterns that combine critical sections with controlled suspension.
