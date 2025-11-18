# **LAB: Recursive Process Trees, Process Groups, and Probabilistic Signal Propagation**

### **Course:** Operating Systems

### **Topic:** `fork()`, recursive tree creation, process groups, signal handlers, and probabilistic broadcast propagation

### **Difficulty:** Medium → Hard

### **Goal:** Implement a tree of processes that communicate using signals, skip/accept signals based on probability, propagate them across the process group, and coordinate clean shutdown.

---

# **🎯 General Idea of the Lab**

You will build a program that:

1. Creates a **binary process tree** of configurable depth (default: 4).
2. Assigns each child to its **parent’s process group**.
3. Installs two signal handlers:

    * `SIGUSR1` — simulated “information broadcast”
    * `SIGINT` — emergency propagation
4. Each process that receives SIGUSR1 flips a probability coin:

    * **Accepts** the signal → prints `"received"`
    * **Skips**    the signal → prints `"skipped"`
5. Regardless of accept/skip, the process **re-broadcasts the signal to its entire process group**.
6. Children terminate only after finishing their workload.
7. Parents wait for both children and print that they finished waiting.
8. Root process can trigger a controlled shutdown via SIGINT.

By the end, you produce a chaotic, probabilistic broadcast system running on top of a recursively built process tree.

---

# **📌 PROGRAM REQUIREMENTS**

## **Stage 1 — Basic Tree Construction (fork_tree)**

Implement a function:

```c
void fork_tree(int depth)
```

Behavior:

* If `depth <= 0` → process returns immediately.
* The process forks **two children**: left and right.
* Each child:

    * prints:
      `parent is <PPID> I am <PID>`
    * joins the *parent’s process group*:
      `setpgid(getpid(), getppid());`
    * recursively calls `fork_tree(depth - 1)`
    * performs `children_work()` (sleep for random 10–20 sec)
    * exits

The parent:

* prints:
  `I am <PID> and I have left child <L> and right child <R>`
* waits for **both** children, handling EINTR
* prints success after both children terminate.

If any fork fails → kills entire tree.

---

## **Stage 2 — Process Group Assignment**

Every child must be placed in the **same process group as its parent** using:

```c
setpgid(getpid(), getppid());
```

This ensures signal broadcasting covers the entire tree.

---

## **Stage 3 — Randomized Child Work**

Implement:

```c
void children_work() {
    srand(time(NULL) & getpid());
    sleep(rand() % 10 + 10);
}
```

So child processes behave unpredictably.

---

## **Stage 4 — Install Two Signal Handlers**

### Required handlers:

#### **SIGUSR1 handler**

* For root: ensure it only handles the first SIGUSR1.
* Every process generates a random number.
* Based on probability **percentage** (CLI argument), the process:

    * **accepts** the signal → prints `<PID> received USR1`
    * **skips** the signal   → prints `<PID> skipped USR1`
* Regardless of accept/skip — broadcast again via:

```c
kill(-getpid(), SIGUSR1);
```

#### **SIGINT handler**

* Root only handles SIGINT the first time.
* Every process prints receiving SIGINT.
* Every process broadcasts SIGINT to group.

If root receives SIGINT twice → exit immediately.

---

## **Stage 5 — Command-line Probability Argument**

Program takes 1 optional argument:

```
./prog <percentage>
```

Example:

```
./prog 30
```

Means:
Each process will skip 30% of USR1 deliveries.

If missing → default to 50%.

Printed at startup:

```
There is no percentage rate specified. Default to 50%.
```

---

## **Stage 6 — Full Program Flow**

1. Install handlers.
2. Build process tree (depth = 4).
3. Let processes run and exchange signals.
4. Test behavior:

    * Send USR1 to root → cascade of receives/skips.
    * Send SIGINT to root → whole tree prints shutdown messages.
5. Tree must always terminate cleanly.

---

# **📌 EXPECTED OBSERVABLE OUTPUT**

Examples:

```
Root pid is 1234
parent is <1234> I am <1235>
parent is <1234> I am <1236>
I am <1234> and I have left child <1235> and right child <1236>

<1234> received USR1 signal
<1235> skipped USR1 signal
<1236> received USR1 signal
...
<1240> received SIGINT signal
<1234> received SIGINT signal
```

And eventually:

```
I am <PID> and I successfully awaited all children
```

---

# **📌 DELIVERABLES**

Students must provide:

* **Complete C source code**
* **Explanation of process group logic**
* **Explanation of probabilistic skipping**
* **Diagram of the process tree**
* **Run logs showing signal propagation**
* **(Optional) Makefile with -Wall -Wextra -O2**

---

# **📌 FINAL RESULT**

If a student implements:

* recursive forking
* process groups
* two signal handlers
* skipping logic based on percentage
* rebroadcasting
* waiting logic
* random child work
* clean exit