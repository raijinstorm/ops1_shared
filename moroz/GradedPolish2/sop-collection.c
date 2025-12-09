#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source)                                                            \
  (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),             \
   kill(0, SIGKILL), exit(EXIT_FAILURE))

#define ITER_COUNT 25

volatile sig_atomic_t last_signal = 0;

void sig_handler(int sig) { last_signal = sig; }

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

void ms_sleep(unsigned int milli) {
  time_t sec = (int)(milli / 1000);
  milli = milli - (sec * 1000);
  struct timespec ts = {0};
  ts.tv_sec = sec;
  ts.tv_nsec = milli * 1000000L;
  if (TEMP_FAILURE_RETRY(nanosleep(&ts, &ts)))
    ERR("nanosleep");
}

void usage(int argc, char *argv[]) {
  printf("%s n\n", argv[0]);
  printf("\t1 <= n <= 4 -- number of moneyboxes\n");
  exit(EXIT_FAILURE);
}

void child_work() {
  int total_donated = 0;
  printf("[%d] Collection box opened\n", getpid());
  char buffer[128];
  snprintf(buffer,sizeof(buffer), "skarbona_%d", getpid());
  int fd = open(buffer, O_RDONLY | O_TRUNC | O_CREAT, 0644);
  if (fd<0) {
    ERR("open");
  }

  sethandler(sig_handler, SIGUSR1);
  sigset_t mask, oldmask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGTERM);
  sigprocmask(SIG_BLOCK, &mask, &oldmask);

  int donation;
  int pid;
  while (sigsuspend(&oldmask)) {
    if (last_signal == SIGUSR1) {
      lseek(fd,-8,SEEK_END);
      if (bulk_read(fd,(char*)&pid,sizeof(int))<0) {
        ERR("bulk_read");
      }
      if (bulk_read(fd,(char*)&donation,sizeof(int))<0) {
        ERR("bulk_read");
      }
      total_donated+=donation;
      printf("[%d] Citizen %d threw in %d PLN. Thank you! Total collected: <%d> PLN\n", getpid(), pid, donation, total_donated);
    }
    if (last_signal == SIGTERM) {
      kill(0, SIGTERM);
      break;
    }
  }

  close(fd);
}

void create_children(int n,pid_t* collection) {
  for (int i=0;i<n;i++) {
    pid_t pid = fork();
    if (pid<0) {
      ERR("fork");
    }
    if (pid == 0) {
      child_work();
      exit(EXIT_SUCCESS);
    }
    collection[i] = pid;
  }
}

void donor_work(int n, pid_t* collection) {
  sethandler(sig_handler, SIGINT);
  sethandler(sig_handler, SIGUSR1);
  sethandler(sig_handler, SIGUSR2);
  sethandler(sig_handler, SIGPIPE);

  sigset_t mask, oldmask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGUSR2);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGPIPE);
  sigaddset(&mask, SIGTERM);
  sigprocmask(SIG_BLOCK, &mask, &oldmask);

  int box_num=0;
  while (sigsuspend(&oldmask)) {
    if(last_signal==SIGUSR1){
      box_num = 0;
      break;
    }
    if (last_signal== SIGUSR2) {
      box_num = 1;
      break;
    }
    if (last_signal == SIGINT) {
      box_num = 2;
      break;
    }
    if (last_signal == SIGPIPE) {
      box_num = 3;
      break;
    }
    if (last_signal == SIGTERM) {
      kill(0, SIGTERM);
      break;
    }
  }
  printf("[%d] Directed to collection box no %d\n", getpid(), box_num);

  if (box_num>=n) {
    printf("[%d] Nothing here, I'm going home!\n", getpid());
    return;
  }
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "skarbona_%d", collection[box_num]);
  int fd = open(buffer, O_WRONLY | O_APPEND);
  if (fd<0) {
    ERR("open");
  }
  int pid = getpid();
  if (write(fd, &pid, sizeof(pid_t)) < 0) {
    ERR("write");
  }
  int donation = rand()%(2000-100+1)+100;
  if (write(fd, &donation, sizeof(donation)) < 0) {
    ERR("write");
  }
  printf("[%d] I'm throwing in %d PLN\n", pid, donation);
  if (close(fd)<0) {
    ERR("close");
  }
  kill(collection[box_num], SIGUSR1);
}

void create_donors(int n, pid_t* collection) {
  srand(getpid());
  int s[4] = {SIGUSR1, SIGUSR2, SIGPIPE, SIGINT};
  for (;;) {
    pid_t pid = fork();
    if (pid<0) {
      ERR("fork");
    }
    if (pid == 0) {
      donor_work(n, collection);
      exit(EXIT_SUCCESS);
    }
    ms_sleep(100);
    kill(pid, s[rand()%4]);
    while (waitpid(pid, NULL, 0) != pid) ;
    if (last_signal == SIGTERM) {
      kill(0,SIGTERM);
      break;
    }
  }
}


int main(int argc, char *argv[]) {
  if (argc!=2) {
    usage(argc, argv);
  }
  int n = atoi(argv[1]);
  if (n<1 || n>4) {
    usage(argc, argv);
  }
  sethandler(sig_handler, SIGTERM);
  pid_t collection[n];
  create_children(n,collection);
  create_donors(n, collection);

  while (wait(NULL) > 0);
  printf("Collection ended!\n");
}