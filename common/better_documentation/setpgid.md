## `setpgid()`

### Purpose

Sets or changes the **process group ID (PGID)** of a process, enabling **job control** and **signal grouping** in UNIX systems.

---

### Syntax

```c
#include <unistd.h>

int setpgid(pid_t pid, pid_t pgid);
```

---

### Parameters

| Parameter | Description                                                 |
| --------- | ----------------------------------------------------------- |
| `pid`     | Target process ID. If `0`, uses the calling process’s PID.  |
| `pgid`    | New process group ID. If `0`, uses the same value as `pid`. |

---

### Behavior

* Used to **create** or **join** a process group within the same session.
* A process group is a collection of related processes that receive signals together (e.g., from the terminal).
* Typically used by shells and daemons to organize processes for job control.
* The **session leader** (the process that created the session) **cannot** change its PGID.
* Can be called by either:

    * The process itself (to set its own group ID), or
    * Its parent (to assign the child’s group before `exec()`).

---

### Return Value

| Return | Meaning                                          |
| ------ | ------------------------------------------------ |
| `0`    | Success — process group ID successfully changed. |
| `-1`   | Error — `errno` set to indicate cause.           |

---

### Common `errno` Values

| Error    | Meaning                                                                |
| -------- | ---------------------------------------------------------------------- |
| `EACCES` | Tried to set PGID of a child **after** it executed an `exec()` call.   |
| `EINVAL` | Invalid PGID (negative or unsupported value).                          |
| `EPERM`  | Process is a session leader or target process not in the same session. |
| `ESRCH`  | No process exists with the specified `pid` (or not a child).           |

---

### Example 1 — Child sets its own process group

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {  // Child
        setpgid(0, 0); // Create new process group with child as leader
        printf("Child PID: %d, PGID: %d\n", getpid(), getpgrp());
        pause(); // wait to demonstrate job control
    } else {
        printf("Parent PID: %d, Child PID: %d\n", getpid(), pid);
    }
}
```

**Explanation:**

* Child calls `setpgid(0, 0)` → creates a new process group with itself as leader.
* Useful for background jobs or daemons that need to be independent of the parent group.

---

### Example 2 — Parent sets the child’s PGID before `exec`

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child: wait for parent to change our PGID
        sleep(1);
        printf("Child: PID=%d, PGID=%d\n", getpid(), getpgrp());
        execlp("sleep", "sleep", "5", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    } else {
        // Parent: make child the leader of a new process group
        if (setpgid(pid, pid) == -1) {
            perror("setpgid");
            exit(EXIT_FAILURE);
        }
        printf("Parent: set child %d to PGID %d\n", pid, pid);
    }
}
```

**Explanation:**

* The parent assigns a new PGID to the child **before** it executes a new program.
* This pattern is used by shells to group commands in a pipeline or background job.

---

### Key Notes

* A process group ID uniquely identifies a group within a **session**.
* Signals like `SIGINT` or `SIGTERM` can be sent to all processes in a group:

  ```c
  kill(-pgid, SIGTERM);  // negative PGID → signal whole group
  ```
* Process groups are fundamental for:

    * Job control (`fg`, `bg` in shells)
    * Foreground/background distinction
    * Signal propagation from terminals

---

### Comparison

| Function      | Purpose                                              |
| ------------- | ---------------------------------------------------- |
| `getpgrp()`   | Get process’s group ID.                              |
| `setsid()`    | Create new session (makes process a session leader). |
| `setpgid()`   | Join/create a process group in a session.            |
| `tcsetpgrp()` | Set foreground process group for a terminal.         |

---

### Summary

`setpgid()` manages process grouping for **job control** and **signal management**.
It allows processes to:

* Form new groups (`setpgid(0, 0)`), or
* Join existing ones (`setpgid(pid, pgid)`),
  within the same session.
  Used primarily by shells and daemons to organize processes and control terminal interaction safely.
