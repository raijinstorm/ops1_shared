#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s p t prob1 [prob2 ...]\n", name);
    fprintf(stderr, "p - parts (1 <= p <= 10)\n");
    fprintf(stderr, "t - times 100 ms to finish each part (1 < t <= 10)\n");
    fprintf(stderr, "probi - probability of issue per 100 ms (0..100)\n");
    exit(EXIT_FAILURE);
}

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

/* ---------- Globals for teacher ---------- */

static pid_t *g_students = NULL;
static int   *g_issues   = NULL;
static int    g_nstudents = 0;

volatile sig_atomic_t g_left = 0;

/* ---------- Globals for students ---------- */

volatile sig_atomic_t g_confirmed = 0;  /* set when student receives SIGUSR2 */

/* ---------- Teacher handlers ---------- */

void sigchld_handler(int sig)
{
    (void)sig;
    int status;
    pid_t pid;

    /* Reap all finished children */
    while ((pid = waitpid(0, &status, WNOHANG)) > 0)
    {
        g_left--;
        if (WIFEXITED(status))
        {
            int issues = WEXITSTATUS(status);
            /* simple linear search – async-signal-safe */
            for (int i = 0; i < g_nstudents; i++)
            {
                if (g_students[i] == pid)
                {
                    g_issues[i] = issues;
                    break;
                }
            }
        }
    }
}

/* Teacher: receive SIGUSR1, print and send SIGUSR2 back */
void sigusr1_handler(int sig, siginfo_t *info, void *ucontext)
{
    (void)sig;
    (void)ucontext;

    pid_t spid = info->si_pid;
    printf("Teacher has accepted solution of student [%d].\n", (int)spid);
    fflush(stdout);
    if (kill(spid, SIGUSR2) == -1)
        ERR("kill SIGUSR2");
}

/* ---------- Student handler ---------- */

void sigusr2_handler(int sig)
{
    (void)sig;
    g_confirmed = 1;
}

/* ---------- Utils ---------- */

static void msleep(long ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1)
    {
        if (errno != EINTR)
            ERR("nanosleep");
    }
}

/* ---------- Student process code ---------- */

void student_process(int no, int p, int t, int prob)
{
    pid_t mypid  = getpid();
    pid_t parent = getppid();

    /* Unblock all signals, then set our own mask */
    sigset_t base;
    sigemptyset(&base);
    if (sigprocmask(SIG_SETMASK, &base, NULL) == -1)
        ERR("sigprocmask student unblock");

    /* Handler for SIGUSR2 (ack from teacher) */
    sethandler(sigusr2_handler, SIGUSR2);

    /* Block SIGUSR2 except when waiting via sigsuspend – prevents losing it */
    sigset_t block_sigusr2, oldmask;
    sigemptyset(&block_sigusr2);
    sigaddset(&block_sigusr2, SIGUSR2);
    if (sigprocmask(SIG_BLOCK, &block_sigusr2, &oldmask) == -1)
        ERR("sigprocmask block SIGUSR2");

    /* RNG */
    srand((unsigned int)(time(NULL) ^ mypid));

    int issues = 0;

    printf("Student[%d,%d] has started doing task!\n", no, (int)mypid);
    fflush(stdout);

    for (int part = 1; part <= p; part++)
    {
        printf("Student[%d,%d] is starting doing part %d of %d!\n",
               no, (int)mypid, part, p);
        fflush(stdout);

        for (int step = 0; step < t; step++)
        {
            msleep(100);  /* 100 ms of work */

            int r = rand() % 100;
            if (r < prob)
            {
                issues++;
                printf("Student[%d,%d] has issue (%d) doing task!\n",
                       no, (int)mypid, issues);
                fflush(stdout);
                msleep(50); /* extra 50 ms because of issue */
            }
        }

        printf("Student[%d,%d] has finished part %d of %d!\n",
               no, (int)mypid, part, p);
        fflush(stdout);

        /* Notify teacher and wait for acceptance.
           We block SIGUSR2 globally and only unblock it in sigsuspend()
           with oldmask, so SIGUSR2 cannot be lost (classic pattern). */
        g_confirmed = 0;

        if (kill(parent, SIGUSR1) == -1)
            ERR("kill SIGUSR1 to teacher");

        while (!g_confirmed)
        {
            /* Temporarily use oldmask (where SIGUSR2 is unblocked) */
            if (sigsuspend(&oldmask) == -1 && errno != EINTR)
                ERR("sigsuspend student");
        }
        /* After sigsuspend, mask is automatically restored (SIGUSR2 blocked again) */
    }

    printf("Student[%d,%d] has completed the task having %d issues!\n",
           no, (int)mypid, issues);
    fflush(stdout);

    _exit(issues & 0xff);  /* issues <= 100, fits in exit status */
}

/* ---------- Main (teacher) ---------- */

int main(int argc, char **argv)
{
    if (argc < 4)
        usage(argv[0]);

    int p = atoi(argv[1]);
    int t = atoi(argv[2]);

    if (p < 1 || p > 10 || t <= 1 || t > 10)
        usage(argv[0]);

    g_nstudents = argc - 3;
    if (g_nstudents < 1)
        usage(argv[0]);

    int *probs = malloc(g_nstudents * sizeof(int));
    if (!probs)
        ERR("malloc probs");

    for (int i = 0; i < g_nstudents; i++)
    {
        probs[i] = atoi(argv[3 + i]);
        if (probs[i] < 0 || probs[i] > 100)
        {
            free(probs);
            usage(argv[0]);
        }
    }

    g_students = calloc(g_nstudents, sizeof(pid_t));
    g_issues   = calloc(g_nstudents, sizeof(int));
    if (!g_students || !g_issues)
        ERR("calloc");

    /* Block SIGUSR1 and SIGCHLD during setup to avoid races */
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) == -1)
        ERR("sigprocmask teacher block");

    /* Install handlers */
    struct sigaction act_usr1;
    memset(&act_usr1, 0, sizeof(act_usr1));
    act_usr1.sa_sigaction = sigusr1_handler;
    sigemptyset(&act_usr1.sa_mask);
    act_usr1.sa_flags = SA_SIGINFO;
    if (sigaction(SIGUSR1, &act_usr1, NULL) == -1)
        ERR("sigaction SIGUSR1");

    sethandler(sigchld_handler, SIGCHLD);

    /* Fork students */
    g_left = g_nstudents;
    for (int i = 0; i < g_nstudents; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
            ERR("fork");
        else if (pid == 0)
        {
            /* Child */
            free(g_students);
            free(g_issues);
            student_process(i, p, t, probs[i]);
            /* never returns */
        }
        else
        {
            g_students[i] = pid;
        }
    }

    free(probs);

    /* Teacher main loop: wait for signals (SIGUSR1 / SIGCHLD).
       SIGUSR1 and SIGCHLD are currently blocked; sigsuspend will
       temporarily switch to oldmask (unblocked) so signals cannot be lost. */
    while (g_left > 0)
    {
        if (sigsuspend(&oldmask) == -1 && errno != EINTR)
            ERR("sigsuspend teacher");
    }

    /* Restore original mask */
    if (sigprocmask(SIG_SETMASK, &oldmask, NULL) == -1)
        ERR("sigprocmask restore");

    /* Print statistics */
    int total = 0;
    printf("\nNo. | Student ID | Issue count\n");
    for (int i = 0; i < g_nstudents; i++)
    {
        printf("%3d | %10d | %d\n", i + 1, (int)g_students[i], g_issues[i]);
        total += g_issues[i];
    }
    printf(" Total issues: %d\n", total);

    free(g_students);
    free(g_issues);

    return EXIT_SUCCESS;
}
