#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define ERR(source) \
(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define BUFLEN 32

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "3 < N <= 20 - size of the board\n");
    exit(EXIT_FAILURE);
}

typedef struct Board
{
    int N;
    pthread_mutex_t mutex;
    char board_data[];
}Board;

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

volatile sig_atomic_t sigint_recieved = 0;

void sigint_handler(int sig)
{
    sigint_recieved = 1;
}

void server_work(Board* board)
{
    if (sethandler(sigint_handler, SIGINT))
        ERR("Setting SIGINT handler");

    while (!sigint_recieved)
    {
        usleep(3*1000*1000);
        if (pthread_mutex_lock(&board->mutex) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&board->mutex);
        }
        for (int i=0;i<board->N;i++)
        {
            for (int j=0;j<board->N;j++)
            {
                int curr_idx = i*board->N+j;
                printf("%d ", board->board_data[curr_idx]);
            }
            printf("\n");
        }
        printf("\n");
        if (sigint_recieved)
        {
            break;
        }
        pthread_mutex_unlock(&board->mutex);
    }
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    int N = atoi(argv[1]);
    if (N<3 || N>20)
    {
        usage(argv[0]);
    }
    srand(time(NULL));

    if (sethandler(SIG_IGN, SIGINT))
        ERR("Setting SIGINT handler");

    printf("My PID is: %d\n", getpid());

    char shm_name[BUFLEN] = {0};
    snprintf(shm_name, BUFLEN, "%d-board", getpid());

    shm_unlink(shm_name);
    int shm_size = sizeof(Board) + N*N*sizeof(char);
    int shm_fd = shm_open(shm_name, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (shm_fd<0)
        ERR("shm_open");
    if (ftruncate(shm_fd, shm_size)<0)
    {
        ERR("ftruncate");
    }

    Board* board = (Board*)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (board == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(shm_fd)<0)
    {
        ERR("close");
    }

    board->N = N;
    for (int i=0;i<N*N;i++)
    {
        board->board_data[i] = (char)(rand()%9+1);
    }
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(&board->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    server_work(board);

    pthread_mutex_destroy(&board->mutex);
    if (msync(board,shm_size, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(board,shm_size) == -1)
    {
        ERR("munmap");
    }
    shm_unlink(shm_name);
    return 0;
}