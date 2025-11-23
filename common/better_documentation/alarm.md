## alarm()

### Purpose

Schedule delivery of `SIGALRM` to the calling process after a specified number of real-time seconds.

---

### Syntax

```c
#include <unistd.h>

unsigned alarm(unsigned seconds);
```

---

### Parameters

| Parameter | Description                                                                                                                 |
| --------- | --------------------------------------------------------------------------------------------------------------------------- |
| `seconds` | Number of real-time (wall-clock) seconds after which `SIGALRM` will be generated. A value of `0` cancels any pending alarm. |

---

### Behavior

`alarm()` schedules a single future `SIGALRM` delivery for the calling process. Only one alarm request may exist at a time; a new call overrides any pending one. If `seconds` is greater than zero, the kernel arms a countdown timer, and when it expires, a `SIGALRM` is generated. If `seconds` is zero, the function cancels any pending alarm.

The delivery time is based on real-time seconds, but handler execution may be delayed by process scheduling. The interface does not stack multiple alarms—rescheduling always replaces the prior timer. The interaction between `alarm()` and `setitimer()` is unspecified by POSIX.

`SIGALRM` is delivered to the process, not a specific thread.

---

### Return Value

| Return | Meaning                                                                                      |
| ------ | -------------------------------------------------------------------------------------------- |
| `0`    | No previous alarm was pending.                                                               |
| `>0`   | Number of seconds that remained on the previous alarm before it was rescheduled or canceled. |

This function never reports errors and does not set `errno`.

---

### Common `errno` Values

This function does not fail and does not set `errno`.

---

### Example — Scheduling an Alarm

```c
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void handler(int signo)
{
    write(STDOUT_FILENO, "Alarm fired\n", 12);
}

int main(void)
{
    struct sigaction sa;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    alarm(5); /* Fire in 5 seconds */

    for (;;) {
        pause(); /* Wait for signal */
    }
}
```

**Explanation:**
This program installs a simple `SIGALRM` handler and schedules an alarm for 5 seconds. `pause()` suspends execution until the signal occurs.

---

### Key Notes

* Only one pending alarm exists per process; subsequent calls replace the previous timer.
* Passing `0` cancels any pending alarm.
* `SIGALRM` is delivered after at least the requested number of real-time seconds, subject to scheduling delays.
* Pending alarms are cleared after `fork()` in the child.
* Remaining alarm time is preserved across `execve()`.
* Argument and return type are `unsigned`, limiting strictly portable values to the minimum guaranteed `UINT_MAX`.

---

### Comparison

| Feature         | `alarm()`                     | `setitimer(2)`                              |
| --------------- | ----------------------------- | ------------------------------------------- |
| Multiple timers | No                            | Yes                                         |
| Interval timers | No                            | Yes (`ITIMER_REAL`, `ITIMER_VIRTUAL`, etc.) |
| Resolution      | Seconds                       | Microseconds                                |
| Use case        | Simple wakeup after N seconds | Fine-grained or periodic timers             |

---

### Summary

`alarm()` provides a simple mechanism to schedule delivery of `SIGALRM` after a specified number of real-time seconds. It supports only one outstanding timer and is suited for coarse-grained timeout or reminder mechanisms. For higher-resolution or more flexible timers, `setitimer()` or POSIX timers should be used instead.
