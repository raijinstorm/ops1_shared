Below is documentation for **wait()** and **waitpid()** written in the same structured POSIX-style documentation format you requested earlier.

---

## wait(), waitpid()

### Purpose

Retrieve and consume termination or state-change information for child processes, optionally blocking until a specific child exits, stops, or continues.

---

### Syntax

```c
#include <sys/wait.h>

pid_t wait(int *stat_loc);

pid_t waitpid(pid_t pid, int *stat_loc, int options);
```

---

### Parameters

| Parameter  | Description                                                                                                                                                                                                                   |
| ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `stat_loc` | Pointer to an `int` where status information is stored. May be `NULL`. The value is interpreted using macros such as `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, etc.                                                          |
| `pid`      | Specifies which child(ren) to wait for:<br>• `-1`: any child<br>• `>0`: specific child PID<br>• `0`: any child in caller’s process group<br>• `<-1`: any child in process group `abs(pid)`                                    |
| `options`  | Bitwise OR of flags defined in `<sys/wait.h>`:<br>• `WNOHANG`: return immediately if no status is available<br>• `WUNTRACED`: also report stopped children<br>• `WCONTINUED`: report children resumed from a job-control stop |

---

### Behavior

* `wait()` blocks until one of the caller's children terminates; if status is already available, it returns immediately.
* `waitpid()` generalizes `wait()` by allowing selective waiting (specific PID or process group) and optional non-blocking behavior via `WNOHANG`.
* When a child’s status is returned, the kernel **consumes** it, and no later wait call will report it again.
* Returned status codes must be interpreted with the macros from `<sys/wait.h>` (e.g., `WIFEXITED()`, `WTERMSIG()`).
* Both functions unblock and return with `EINTR` if a caught signal interrupts the wait.
* If `SIGCHLD` is ignored or `SA_NOCLDWAIT` is set, no child status becomes available, and waiting threads will receive `ECHILD`.

---

### Return Value

| Return | Meaning                                                                 |
| ------ | ----------------------------------------------------------------------- |
| `>= 0` | PID of the child whose status is returned.                              |
| `0`    | Only possible with `waitpid(..., WNOHANG)` when no status is available. |
| `-1`   | Failure; `errno` is set (e.g., `ECHILD`, `EINVAL`, `EINTR`).            |

---

### Common `errno` Values

| Error    | Meaning                                                                       |
| -------- | ----------------------------------------------------------------------------- |
| `ECHILD` | No unwaited-for child processes; or specified `pid` does not match any child. |
| `EINTR`  | The call was interrupted by a signal before status was returned.              |
| `EINVAL` | Invalid `options` value (waitpid only).                                       |

---

### Example — Waiting for a Child and Checking Its Exit Status

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    pid_t pid = fork();
    int status;

    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {              // Child
        _exit(42);               // Child exits with status code 42
    }

    // Parent waits for specific child
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        exit(EXIT_FAILURE);
    }

    if (WIFEXITED(status)) {
        printf("Child exited with code %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("Child killed by signal %d\n", WTERMSIG(status));
    }

    return 0;
}
```

**Explanation:**
The parent forks a child, waits for it, and inspects the returned status using the standard macros. The status is consumed, meaning future wait calls will not retrieve it again.

---

### Key Notes

* `wait()` always waits for **any** child; portable programs should use `waitpid()` instead.
* Status information is consumed on retrieval—each child produces exactly one wait result.
* Using `SIGCHLD` with `SA_NOCLDWAIT` or `SIG_IGN` prevents child status from accumulating.
* `WNOHANG` allows implementing poll-based or event-loop-style child monitoring.
* `waitpid(pid = -1, ...)` behaves like `wait()`, but with options support.

---

### Comparison

| Function    | Selective Wait    | Non-blocking    | Reports Stops                          | Reports Continues  | Notes                                                     |
| ----------- | ----------------- | --------------- | -------------------------------------- | ------------------ | --------------------------------------------------------- |
| `wait()`    | No                | No              | No (except job control semantics vary) | No                 | Simplest interface; waits for any child.                  |
| `waitpid()` | Yes (PID / group) | Yes (`WNOHANG`) | Yes (`WUNTRACED`)                      | Yes (`WCONTINUED`) | Recommended for portable and controlled child management. |

---

### Summary

`wait()` and `waitpid()` retrieve and consume status information from child processes, supporting synchronized process termination handling, job control, and selective non-blocking waiting. They form the core mechanism by which a Unix parent observes and reaps its children, preventing zombie processes and enabling controlled process supervision.

---

If you want, I can also generate a diagram explaining how **child → SIGCHLD → wait() → status consumption** works internally.
