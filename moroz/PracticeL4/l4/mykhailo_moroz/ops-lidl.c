#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void ms_sleep(unsigned int milli)
{
    struct timespec ts = {milli / 1000, (milli % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

void usage(int argc, char* argv[])
{
    printf("%s M N K\n", argv[0]);
    printf("\t20 <= M <= 100 - number of customers\n");
    printf("\t3 <= N <= 12, N %% 3 = 0 - number of restock workers\n");
    printf("\t2 <= K <= 5 - number of payment terminals\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) { return EXIT_SUCCESS; }
