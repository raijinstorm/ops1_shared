## Simplified VS extended version
In `struct sigaction`, the fields:

```c
void (*sa_handler)(int);
void (*sa_sigaction)(int, siginfo_t *, void *);
```

**share the same memory region** and **cannot both be used at the same time**.
This is by design and required by POSIX.

Here is exactly what this means.

---

### 1. `sa_handler` and `sa_sigaction` overlap in memory

Inside `struct sigaction`, these fields are part of a **union**:

```c
struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int sa_flags;
};
```

Because they are in a union:

* They occupy **the same bytes** in memory.
* Writing to one overwrites the other.
* Only **one** of them is meaningful at a time.

This is why you cannot set **both**.

---

### 2. When to use each one

#### `sa_handler`

The "old" simple handler form:

```c
void handler(int signo);
```

Use this when you only care about the signal number.

Set via:

```c
sa.sa_handler = handler;
sa.sa_flags = 0;   // or SA_RESTART etc
```

#### `sa_sigaction`

The "advanced" handler form, with full signal info:

```c
void handler(int signo, siginfo_t *info, void *ucontext);
```

Use this when you need extra data such as:

* sender PID
* sender UID
* real-time signal value
* address of fault (for SIGSEGV, SIGBUS)
* timer overrun count
* etc.

Must be enabled with:

```c
sa.sa_sigaction = handler;
sa.sa_flags = SA_SIGINFO;
```

Because `SA_SIGINFO` tells the kernel:
**"Use sa_sigaction, not sa_handler."**

---


### 3. Practical rule (very important)

#### Use **either**:

Simple handler:

```c
sa.sa_handler = handler;
sa.sa_flags = 0;
```

#### OR advanced handler:

```c
sa.sa_sigaction = handler;
sa.sa_flags = SA_SIGINFO;
```
