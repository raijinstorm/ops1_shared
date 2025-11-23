## signal(7)

### Purpose

Overview of Linux and POSIX signal concepts, including generation, disposition, delivery, handlers, masks, and real-time semantics.

---

### Scope

This is a **conceptual reference page**, not an API specification. It documents signal behavior in Linux/POSIX: types of signals, default actions, delivery rules, signal masks, queued signals, handler execution, and restart semantics for system calls.

---

### Components of the Signal Model

#### Signal Dispositions

Each signal has a disposition determining how the process reacts:

| Action   | Meaning                            |
| -------- | ---------------------------------- |
| **Term** | Terminate the process.             |
| **Ign**  | Ignore the signal.                 |
| **Core** | Terminate and produce a core dump. |
| **Stop** | Stop the process.                  |
| **Cont** | Continue a stopped process.        |

A process sets dispositions using `sigaction(2)` or `signal(2)`. A disposition may be:

* Default action
* Ignore
* Catch with a signal handler

Handlers run on the normal stack unless an alternate stack is set with `sigaltstack(2)`.
Signal dispositions are **per-process**, shared by all threads.
Dispositions are inherited across `fork(2)`. During `execve(2)`, ignored signals remain ignored; caught signals revert to defaults.

---

### Sending Signals

Signals may be sent using:

| Interface              | Target                                   |
| ---------------------- | ---------------------------------------- |
| `raise(3)`             | Calling thread                           |
| `kill(2)`              | Process, process group, or all processes |
| `pidfd_send_signal(2)` | Process via PID file descriptor          |
| `killpg(3)`            | Process group                            |
| `pthread_kill(3)`      | Specific POSIX thread                    |
| `tgkill(2)`            | Specific thread within a process         |
| `sigqueue(3)`          | Real-time signal with accompanying data  |

---

### Waiting for Signal Delivery

#### Asynchronous wait

Suspends until a signal is caught:

* `pause(2)` — suspend until any signal is caught.
* `sigsuspend(2)` — temporarily replace mask and wait for an unmasked signal.

#### Synchronous acceptance

Used when the caller wants structured delivery:

* `sigwaitinfo(2)`, `sigtimedwait(2)`, `sigwait(3)` — wait for signals and return information.
* `signalfd(2)` — read structured signal events from a file descriptor.

---

### Signal Masks and Pending Signals

Each thread has an independent **signal mask** controlling which signals are blocked.
Masks manipulated via:

* `pthread_sigmask(3)` in multithreaded programs
* `sigprocmask(2)` in single-threaded programs

Rules:

* Newly forked child inherits mask; mask preserved across `execve(2)`.
* Signals may be **process-directed** or **thread-directed**.
* A pending signal is delivered only when unblocked.
* The kernel may deliver a process-directed signal to **any thread** that does not block it.
* `sigpending(2)` returns union of per-thread pending signals and process-wide pending signals.

---

### Execution of Signal Handlers

When user-space returns from a system call or is scheduled, if an unblocked pending signal with a handler exists, the kernel:

1. **Prepares state**

    * Removes signal from pending set
    * Installs alternate stack if `SA_ONSTACK`
    * Saves registers, program counter, mask, and alt-stack settings in a frame
    * Applies `sa_mask` and blocks the signal unless `SA_NODEFER` was used

2. **Builds handler frame**

    * Sets program counter to handler
    * Installs trampoline return address (`sigreturn(2)`)

3. **Transfers execution** to the handler

4. **Handler returns** and transitions to the trampoline

5. **sigreturn(2)** restores saved user context and resumes interrupted code

If the handler does not return normally (e.g., `siglongjmp(3)`, `execve(2)`), restoration of masks is the programmer’s responsibility.

Handlers may nest; recursion depth limited only by stack size.

---

### Standard (Non–Real-Time) Signals

Characteristics:

* Do **not** queue: multiple occurrences collapse into one pending instance.
* Delivery order among different pending standard signals is **unspecified**.
* Default actions vary (Terminate, Ignore, Core, Stop, Continue).
* `SIGKILL` and `SIGSTOP` cannot be caught, blocked, or ignored.

Linux supports the standard POSIX and historical signals listed in the original man page, with architecture-dependent numeric values.

---

### Real-Time Signals (SIGRTMIN … SIGRTMAX)

Features of real-time signals:

* **Queued**: multiple instances are kept in order.
* **Carry data** if sent via `sigqueue(3)` (via `si_value` in `siginfo_t`).
* **Ordered** by numeric value and send order.
* Default action is **terminate**.
* Number available varies because glibc reserves a few internally; programs must use `SIGRTMIN+n` and verify `<= SIGRTMAX`.

Queue limits:

* Older kernels: global `/proc/sys/kernel/rtsig-max` limit
* Modern kernels: per-user `RLIMIT_SIGPENDING`

Real-time support expanded `sigset_t` from 32 to 64 bits, resulting in `rt_*` versions of system calls.

---

### System Call Interruption and Restart

A blocking system call interrupted by a handler either:

* **Restarts automatically** (if `SA_RESTART` was used), or
* **Fails with `EINTR`**

Linux-specific restart rules apply to:

* I/O on slow devices
* Socket operations
* File locks
* POSIX message queues
* Futex waits
* Semaphores
* `wait*` calls
* Some random and inotify reads

Some interfaces **never restart**, including:

* Multiplexing (`select`, `poll`, `epoll_wait`)
* Sleep functions (`nanosleep`, `usleep`)
* Signal-wait interfaces (`pause`, `sigsuspend`, `sigwaitinfo`)
* System V IPC

Stop/resume via `SIGSTOP`/`SIGCONT` may cause additional `EINTR` failures on Linux even without handlers.

---

### Key Notes

* Signal dispositions are shared across threads; masks are per-thread.
* Standard signals collapse to a single pending instance; real-time signals queue.
* Real-time signals should never be referenced by hard-coded numbers.
* Handlers execute on user stacks unless an alternate stack is configured.
* `SIGKILL` and `SIGSTOP` bypass all user-space handling.
* Delivery rules differ for process-directed vs thread-directed signals.

---

### Comparison

| Feature                      | Standard Signals | Real-Time Signals                           |
| ---------------------------- | ---------------- | ------------------------------------------- |
| Queuing                      | No               | Yes                                         |
| Ordering                     | Unspecified      | Strict (lowest number first; FIFO per type) |
| Can carry data               | No               | Yes (`sigqueue`)                            |
| Default action               | Varies           | Terminate                                   |
| Multiple instances preserved | No               | Yes                                         |

---

### Summary

`signal(7)` describes the full POSIX/Linux signal model: dispositions, delivery semantics, handlers, masks, synchronous and asynchronous reception, distinction between standard and real-time signals, and system call interruption rules. Understanding this model is essential for writing robust, asynchronous, and multithreaded UNIX/Linux applications, especially those interacting with system calls, process groups, or real-time facilities.
