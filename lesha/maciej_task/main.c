#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>


#define ERR(source)                                                            \
  (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),             \
   kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t start_job=0;
volatile sig_atomic_t stop_job=0;
volatile sig_atomic_t send_new_child=0;

#define ITER_COUNT 25
ssize_t bulk_read(int fd, char *buf, size_t count) {
  ssize_t c;
  ssize_t len = 0;
  do {
    c = TEMP_FAILURE_RETRY(read(fd, buf, count));
    if (c < 0)
      return c;
    if (c == 0)
      return len; // EOF
    buf += c;
    len += c;
    count -= c;
  } while (count > 0);
  return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count) {
  ssize_t c;
  ssize_t len = 0;
  do {
    c = TEMP_FAILURE_RETRY(write(fd, buf, count));
    if (c < 0)
      return c;
    buf += c;
    len += c;
    count -= c;
  } while (count > 0);
  return len;
}

void sethandler(void (*f)(int), int sigNo) {
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = f;
  if (-1 == sigaction(sigNo, &act, NULL))
    ERR("sigaction");
}

int child_work() {
  srand(getpid());
  int task_done=0;
  while (!start_job) {
    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 1000000;
    nanosleep(&sleep_time, NULL);
  }
  fprintf(stdout,"[%d]Started working\n",getpid());
  while (!stop_job) {
    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = (100+rand()%101)*1000;
    nanosleep(&sleep_time, NULL);
  }
  exit(task_done);
}

void child_usr1_handler(int sig_no) {
  start_job=1;
}
void child_usr2_handler(int sig_no) {
  stop_job=1;
}

void parent_usr1_handler(int sig_no) {
  send_new_child=1;
}
void create_children(int child_count) {
  sethandler(child_usr1_handler,SIGUSR1);
  sethandler(child_usr2_handler,SIGUSR2);
  pid_t *pid_array=malloc(child_count*sizeof(pid_t));
  for (int i=0; i<child_count; i++) {
    pid_t pid=fork();
    if (pid==0) {
      fprintf(stderr,"child %d created with pid <%d>\n",i,getpid());
      child_work();
      exit(0);
    }
    pid_array[i]=pid;
  }
  sethandler(parent_usr1_handler,SIGUSR1);
  sethandler(NULL,SIGUSR2);
  for (int i=0; i<child_count; i++) {
    kill(pid_array[i],SIGUSR1);
    while (!send_new_child) {
      sleep(1);
    }
    kill(pid_array[i],SIGUSR2);
    send_new_child=0;
  }
  free(pid_array);
}
void await_all_children() {
  int status;
  pid_t pid;
  while ((pid=wait(&status))) {
    if (pid==-1) {
      if (errno==EINTR) {
        fprintf(stderr,"[EINTR] Retrying\n");
        errno=0;
      }
      else if (errno==ECHILD) {
        fprintf(stderr,"[ECHILD] Finished collecting children\n");
        break;
      }
      else {
        ERR("Unexpected error\n");
      }
    }
    fprintf(stderr,"Awaited <%d> children which exited with %d status\n",pid,WIFEXITED(status));
  }
}
int main(int argc, char *argv[])
{
  if (argc!=2) {
    ERR("argc");
  }
  int child_count=atoi(argv[1]);
  if (child_count<0) {
    ERR("argv");
  }
  create_children(child_count);
  await_all_children();
  printf("Collection ended!\n");
  return 0;
}