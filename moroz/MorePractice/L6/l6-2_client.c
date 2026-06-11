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
    fprintf(stderr, "PID - pid of the server process\n");
    exit(EXIT_FAILURE);
}

typedef struct Board
{
    int N;
    pthread_mutex_t mutex;
    char board_data[];
}Board;

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }
    srand(time(NULL));

    int server_pid = atoi(argv[1]);
    int N = atoi(argv[2]);
    char shm_name[BUFLEN] = {0};
    snprintf(shm_name, BUFLEN, "%d-board",server_pid);

    int shm_size = sizeof(Board) + N*N*sizeof(char);
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd<0)
        ERR("shm_open");

    Board* board = (Board*)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (board == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(shm_fd)<0)
    {
        ERR("close");
    }

    int score = 0;
    while (1)
    {
        if (pthread_mutex_lock(&board->mutex) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&board->mutex);
        }

        int num = rand()%10+1;
        if (num == 1)
        {
            printf("Oops...\n");
            break;
        }
        int x = rand()%N;
        int y = rand()%N;
        printf("Trying to search field (%d,%d)\n", x,y);
        if (board->board_data[x*N+y] != 0)
        {
            score+=(int)board->board_data[x*N+y];
            printf("found %d points\n", board->board_data[x*N+y]);
            board->board_data[x*N+y] = 0;
        }
        else
        {
            pthread_mutex_unlock(&board->mutex);
            printf("GAME OVER: score %d\n", score);
            break;
        }

        pthread_mutex_unlock(&board->mutex);
        usleep(1*1000*1000);
    }

    if (msync(board,shm_size, MS_SYNC) == -1)
    {
        ERR("msync");
    }
    if (munmap(board,shm_size) == -1)
    {
        ERR("munmap");
    }
    return 0;
}
