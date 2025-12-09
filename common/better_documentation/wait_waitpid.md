Below is a **concise, clear documentation summary** for **wait()** and **waitpid()**, suitable for study and system-programming exercises.

---

# wait(), waitpid() — Documentation Summary

## Purpose

Retrieve status information about a terminated, stopped, or continued **child process**.
Used to reap children (prevent zombies) and monitor their exit conditions.

---

# 1. Function Signatures

```c
#include <sys/wait.h>

pid_t wait(int *stat_loc);
pid_t waitpid(pid_t pid, int *stat_loc, int options);
```

---

# 2. Behavior Overview

### **wait()**

* Blocks until **any child process** terminates.
* Equivalent to:

```c
waitpid(-1, stat_loc, 0);
```

* Returns immediately if a child has already terminated.

### **waitpid()**

Allows selecting which child(ren) to wait for:

| pid value | Meaning                                         |
| --------- | ----------------------------------------------- |
| `-1`      | Any child process (same as wait()).             |
| `> 0`     | Wait for that specific PID.                     |
| `0`       | Wait for any child in the same process group.   |
| `< -1`    | Wait for any child in process group `abs(pid)`. |

Optional **flags** modify behavior:

| Flag         | Meaning                                                          |
| ------------ | ---------------------------------------------------------------- |
| `WNOHANG`    | Return immediately if no child has changed state (non-blocking). |
| `WUNTRACED`  | Also report stopped children.                                    |
| `WCONTINUED` | Report children continued after SIGCONT.                         |

---

# 3. Return Values

### If a child status is available

```
return child's PID
```

### If no child state is available

* `wait()` → never returns 0
* `waitpid(..., WNOHANG)` → returns **0**

### If interrupted by a signal

```
return -1, errno = EINTR
```

### If no children exist

```
return -1, errno = ECHILD
```

---

# 4. Reading Child Status — Macros

All macros operate on `stat_loc` (the integer written by wait/waitpid).

### Termination (normal exit)

```c
WIFEXITED(stat_loc)        → non-zero if child exited normally
WEXITSTATUS(stat_loc)      → exit code (0–255)
```

### Termination by signal

```c
WIFSIGNALED(stat_loc)      → non-zero if child died from a signal
WTERMSIG(stat_loc)         → signal number
```

### Stopped (e.g., SIGSTOP)

```c
WIFSTOPPED(stat_loc)
WSTOPSIG(stat_loc)
```

### Continued (job control)

```c
WIFCONTINUED(stat_loc)
```

---

# 5. Important Characteristics

* **wait/waitpid consume child status:** once read, it cannot be retrieved again.
* If multiple children terminate before calling wait(), the kernel stores them as **zombies** until reaped.
* If the parent does not wait and exits, children are reparented to PID 1 (init/systemd).
* If SIGCHLD is blocked, pending SIGCHLD associated with a reaped child is discarded.

---

# 6. Typical Usage Example

```c
pid_t pid = fork();

if (pid == 0) {
    _exit(7);
} else {
    int status;
    pid_t w = waitpid(pid, &status, 0);

    if (WIFEXITED(status))
        printf("exit code: %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("killed by signal %d\n", WTERMSIG(status));
}
```

---

# 7. Non-blocking Example (WNOHANG)

```c
if ((wpid = waitpid(child, &status, WNOHANG)) == 0) {
    printf("child still running\n");
}
```

---

# 8. Usage Guidelines

### Prefer `waitpid()` over `wait()`:

* More control, avoids collisions with other code that also waits on children (e.g., libraries).
* `wait()` may reap a child that another subsystem expected to wait on.

### Avoid:

* `waitpid(-1, ...)` in libraries or handlers unless intentional.

---

# 9. Common Errors

| errno    | Description                                           |
| -------- | ----------------------------------------------------- |
| `ECHILD` | No unwaited-for child exists.                         |
| `EINTR`  | Interrupted by a signal before a child changed state. |
| `EINVAL` | Invalid options (waitpid only).                       |

---

# 10. Summary

`wait()` and `waitpid()` are essential for:

* Reaping child processes (avoiding zombies)
* Reading exit codes
* Supporting job control (stopped/continued states)
* Writing robust parent-child process logic

`waitpid()` is the preferred interface due to its flexibility and predictability.

---

If you want, I can also write **short actionable cheat-sheet** or **visual diagrams** for wait/waitpid.
