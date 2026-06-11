#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

/*
*USEFUL FOR DEBUGGING MULTIPLE PROCESSES (OR NOT): //before run
*                                         set detach-on-fork off
                                          set follow-fork-mode parent
                                          set schedule-multiple on
                                          //after run
                                          info inferiors   //lists all the processes GDB is currently tracking
                                          inferior <inferior_number> //switch inferiors and use bt
                                          // or
                                          thread apply all bt
 */

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)             \
(__extension__({                               \
long int __result;                         \
do                                         \
__result = (long int)(expression);     \
while (__result == -1L && errno == EINTR); \
__result;                                  \
}))
#endif


#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAXSTUDENTS 20
#define MAXMESSAGE 32
#define NUMSTAGES 4

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "3<=n<=20 - number of students\n");
    exit(EXIT_FAILURE);
}

typedef struct StudentResult
{
    int32_t student_idx;
    char stage_result;
}StudentResult;

typedef struct StudentsData
{
    int students_current_stage[MAXSTUDENTS];
    int score[MAXSTUDENTS];
    pid_t pids[MAXSTUDENTS];
}StudentsData;

volatile sig_atomic_t alarm_recieved = 0;

void sigalrm_handler(int sig)
{
    alarm_recieved = 1;
}

void clean_child(int idx, int pipes[MAXSTUDENTS][2], int teacher_pipe[2])
{
    if (close(pipes[idx][0])<0)
    {
        ERR("close");
    }
    if (close(teacher_pipe[1])<0)
    {
        ERR("close");
    }
}

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

int find_student(pid_t students[MAXSTUDENTS], int n, pid_t student)
{
    for (int i =0;i<n;i++)
    {
        if (students[i] == student)
        {
            return i;
        }
    }
    return -1;
}

void child_work(int pipes[MAXSTUDENTS][2], int teacher_pipe[2], int idx, int n)
{
    srand(getpid());
    printf("[%d] Student %d\n", getpid(), idx);
    int k = rand()%(9-3+1)+3;
    for (int i=0;i<n;i++)
    {
        if (idx != i)
        {
            if (close(pipes[i][0])<0)
            {
                ERR("close");
            }
            if (close(pipes[i][1])<0)
            {
                ERR("close");
            }
        }
        else
        {
            if (close(pipes[i][1])<0)
            {
                ERR("close");
            }
        }
    }
    if (close(teacher_pipe[0])<0)
    {
        ERR("close");
    }

    char c;
    int bytes_read = read(pipes[idx][0], &c, 1);
    if (bytes_read<0)
    {
        ERR("read");
    }
    if (bytes_read == 0)
    {
        clean_child(idx, pipes, teacher_pipe);

        return;
    }

    if (c!='a')
    {
        clean_child(idx, pipes, teacher_pipe);
        printf("Incorrect attendance message from teacher\n");
        return;
    }
    printf("Student [%d]: HERE\n", getpid());

    char send_buf[MAXMESSAGE] = {0};
    pid_t pid = getpid();
    memcpy(send_buf, &pid, sizeof(pid));
    int32_t valid_response = 1;
    memcpy(send_buf+sizeof(pid), &valid_response, sizeof(int32_t));
    if (write(teacher_pipe[1], send_buf, MAXMESSAGE)<0)
    {
        if (errno == EPIPE)
        {
            clean_child(idx, pipes, teacher_pipe);
            return;
        }
        ERR("wrte");
    }

    int i=0;
    while (i<NUMSTAGES)
    {
        int t = rand()%(500-100+1)+100;
        usleep(t*1000);

        int q = rand()%(20-1+1)+1;
        int32_t attempt_score = q+k;
        memset(send_buf,0,MAXMESSAGE);
        memcpy(send_buf, &pid, sizeof(pid));
        memcpy(send_buf+sizeof(pid), &attempt_score, sizeof(int32_t));
        if (write(teacher_pipe[1], send_buf, MAXMESSAGE)<0)
        {
            if (errno == EPIPE)
            {
                printf("Student %d: Oh no, I haven't finished stage %d. I need more time\n", getpid(), i+1);
                clean_child(idx, pipes, teacher_pipe);
                return;
            }
            ERR("write");
        }

        char recv_buf[MAXMESSAGE] = {0};
        bytes_read = read(pipes[idx][0], recv_buf, MAXMESSAGE);
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
        {
            printf("Student %d: Oh no, I haven't finished stage %d. I need more time\n", getpid(), i+1);
            clean_child(idx, pipes, teacher_pipe);
            return;
        }

        if (recv_buf[0] == 'f')
        {
            continue;
        }
        i++;
    }

    printf("Student [%d]: I NAILED IT!\n", pid);

    if (close(pipes[idx][0])<0)
    {
        ERR("close");
    }
    if (close(teacher_pipe[1])<0)
    {
        ERR("close");
    }
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    int n = atoi(argv[1]);
    if (n<3 || n>20)
    {
        usage(argv[0]);
    }

    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGPIPE handler");
    if (sethandler(sigalrm_handler, SIGALRM))
        ERR("Setting SIGALRM handler");
    srand(time(NULL));

    int pipes[MAXSTUDENTS][2];
    for (int i=0;i<n;i++)
    {
        pipe(pipes[i]);
    }
    int teacher_pipe[2];
    pipe(teacher_pipe);

    pid_t students_pids[MAXSTUDENTS] = {0};
    for (int i=0;i<n;i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            child_work(pipes, teacher_pipe, i, n);

            exit(EXIT_SUCCESS);
        }
        else if (pid > 0)
        {
            students_pids[i] = pid;
        }
        else
        {
            ERR("fork");
        }
    }

    for (int i=0;i<n;i++)
    {
        if (close(pipes[i][0])<0)
        {
            ERR("close");
        }
    }
    if (close(teacher_pipe[1])<0)
    {
        ERR("close");
    }

    for (int i=0;i<n;i++)
    {
        printf("Teacher: Is [%d] here?\n", students_pids[i]);

        char c = 'a';
        if (write(pipes[i][1], &c, 1)<0)
        {
            if (errno == EPIPE)
            {
                students_pids[i] = 0;
                continue;
            }
            ERR("write");
        }

        if (students_pids[i] == 0)
            continue;

        char attendance_recv[MAXMESSAGE] = {0};
        int bytes_read = read(teacher_pipe[0], attendance_recv, MAXMESSAGE);
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
        {
            continue;
        }
        attendance_recv[bytes_read-1] = '\0';
    }

    alarm(2);

    int stages_dificulty[NUMSTAGES] = {3,6,7,5};
    int finished_students = 0;
    StudentsData students_data = {0};
    while (finished_students<n && !alarm_recieved)
    {
        char stage_result[MAXMESSAGE] = {0};
        int bytes_read = read(teacher_pipe[0], stage_result, MAXMESSAGE);
        if (bytes_read<0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ERR("read");
        }
        if (bytes_read == 0)
        {
            break;
        }

        pid_t current_student;
        memcpy(&current_student, stage_result, sizeof(pid_t));
        int32_t score;
        memcpy(&score, stage_result+sizeof(pid_t), sizeof(int32_t));

        int student_idx = find_student(students_pids, n, current_student);
        if (student_idx<0)
        {
            continue;
        }

        char res = 'f';
        int d = rand()%(20-1+1)+1 + stages_dificulty[students_data.students_current_stage[student_idx]];
        if (score>=d)
        {
            res = 's';
            printf("Teacher: Student [%d] finished stage [%d]\n", current_student, students_data.students_current_stage[student_idx]+1);
            students_data.score[student_idx]+=stages_dificulty[students_data.students_current_stage[student_idx]];
            students_data.students_current_stage[student_idx]++;
            if (students_data.students_current_stage[student_idx] == NUMSTAGES)
            {
                finished_students++;
            }
        }
        else
        {
            printf("Teacher: Student [%d] needs to fix stage [%d]\n", current_student, students_data.students_current_stage[student_idx]);
        }

        char result_response[MAXMESSAGE] = {0};
        result_response[0] = res;
        if (write(pipes[student_idx][1], result_response, MAXMESSAGE)<0)
        {
            if (errno == EPIPE || errno==EINTR)
                continue;
            ERR("write");
        }
    }

    if (alarm_recieved)
    {
        printf("Teacher: END OF TIME!\n");
    }

    for (int i=0;i<n;i++)
    {
        printf("Teacher: %d - %d\n", students_pids[i], students_data.score[i]);
    }

    printf("Teacher: IT'S FINALLY OVER!\n");

    for (int i=0;i<n;i++)
    {
        if (close(pipes[i][1])<0)
        {
            ERR("close");
        }
    }
    if (close(teacher_pipe[0])<0)
    {
        ERR("close");
    }

    while (wait(NULL)>0);


    return 0;
}