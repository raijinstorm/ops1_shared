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

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "n>=1 - number of players\n");
    fprintf(stderr, "m>=100 - number of players\n");
    exit(EXIT_FAILURE);
}

#define NUMBERS 37

typedef struct __attribute__((__packed__))
{
    pid_t pid;
    int32_t amount;
    int32_t number;
    int32_t child_index;
}BetMessage;

void clean_child(int dealer_pipe[2], int N, int** pipes, int idx)
{
    if (close(pipes[idx][0])<0)
    {
        ERR("close");
    }
    if (close(dealer_pipe[1])<0)
    {
        ERR("close");
    }

    for (int i=0;i<N;i++)
    {
        free(pipes[i]);
        pipes[i] = NULL;
    }
    free(pipes);
    pipes = NULL;
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

void child_work(int M, int N, int dealer_pipe[2], int** pipes, int idx)
{
    srand(getpid());
    printf("[%d]: I have [%d] and I'm going to play roulette\n", getpid(), M);
    if (close(dealer_pipe[0])<0)
    {
        ERR("close");
    }
    for (int i=0;i<N;i++)
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

    int leave = 0;
    while (M>0)
    {
        if (rand()%100<10)
        {
            leave = 1;
            printf("[%d]: I saved [%d]\n", getpid(), M);
            break;
        }

        int bet_amount = rand()%M+1;
        int32_t chosen_number = rand()%NUMBERS;
        BetMessage bet_message = {getpid(), bet_amount, chosen_number, idx};
        if (write(dealer_pipe[1], &bet_message, sizeof(BetMessage))<0)
        {
            if (errno == EPIPE)
            {
                clean_child(dealer_pipe, N, pipes, idx);
                return;
            }
        }

        int prise;
        int bytes_read = read(pipes[idx][0], &prise, sizeof(int));
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
        {
            clean_child(dealer_pipe, N, pipes, idx);
            return;
        }
        if (prise == 0)
        {
            M-=bet_amount;
        }
        else
        {
            printf("[%d]: I won [%d]\n", getpid(), prise);
            M+=prise;
        }
    }

    if (M<=0)
    {
        printf("[%d]: I'm broke\n", getpid());
    }

    if (M<=0 || leave)
    {
        BetMessage bancrupcy_message = {getpid(), 0, 0, idx};

        if (write(dealer_pipe[1], &bancrupcy_message, sizeof(BetMessage))<0)
        {
            if (errno == EPIPE)
            {
                clean_child(dealer_pipe, N, pipes, idx);
                return;
            }
        }
    }

    clean_child(dealer_pipe, N, pipes, idx);
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }

    int N = atoi(argv[1]);
    int M = atoi(argv[2]);

    if (N<1 || M<100)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGPIPE handler");
    srand(time(NULL));

    int dealer_pipe[2];
    pipe(dealer_pipe);

    int** pipes = malloc(N*sizeof(int*));
    if (!pipes)
    {
        ERR("malloc");
    }
    for (int i=0;i<N;i++)
    {
        pipes[i] = malloc(2*sizeof(int));
        if (!pipes[i])
        {
            ERR("malloc");
        }
    }
    for (int i=0;i<N;i++)
    {
        pipe(pipes[i]);
    }

    for (int i=0;i<N;i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            ERR("fork");
        }
        else if (pid == 0)
        {
            child_work(M, N, dealer_pipe, pipes, i);

            exit(EXIT_SUCCESS);
        }
    }

    for (int i=0;i<N;i++)
    {
        if (close(pipes[i][0])<0)
        {
            ERR("close");
        }
    }
    if (close(dealer_pipe[1])<0)
    {
        ERR("close");
    }

    int players_num = N;
    BetMessage* bets = malloc(N*sizeof(BetMessage));
    if (!bets)
    {
        ERR("malloc");
    }
    while (players_num>0)
    {
        int players_num_snapshot = players_num;
        memset(bets, 0, sizeof(BetMessage)*N);
        for (int i=0;i<players_num_snapshot;i++)
        {
            BetMessage curr_player_bet;
            int bytes_read = read(dealer_pipe[0], &curr_player_bet, sizeof(BetMessage));
            if (bytes_read<0)
            {
                ERR("read");
            }
            if (bytes_read == 0)
            {
                players_num = 0;
                players_num_snapshot = 0;
                break;
            }

            if (curr_player_bet.amount == 0 && curr_player_bet.number == 0)
            {
                players_num--;
                continue;
            }
            printf("Dealer: [%d] placed [%d] on [%d]\n", curr_player_bet.pid, curr_player_bet.amount, curr_player_bet.number);

            int player_idx = curr_player_bet.child_index;
            bets[player_idx] = curr_player_bet;
        }

        if (players_num<=0)
        {
            break;
        }

        int lucky_number = rand()%NUMBERS;
        printf("Dealer: [%d] is the lucky number.\n", lucky_number);

        for (int i=0;i<N;i++)
        {
            int res = 0;
            if (bets[i].number == lucky_number)
            {
                res = bets[i].amount*35;
            }
            if (write(pipes[i][1], &res, sizeof(int))<0)
            {
                if (errno == EPIPE)
                {
                    continue;
                }
                ERR("write");
            }
        }
    }

    printf("Dealer: Casino always wins\n");


    for (int i=0;i<N;i++)
    {
        if (close(pipes[i][1])<0)
        {
            ERR("close");
        }
    }
    if (close(dealer_pipe[0])<0)
    {
        ERR("close");
    }

    while (wait(NULL)>0);

    for (int i=0;i<N;i++)
    {
        free(pipes[i]);
        pipes[i] = NULL;
    }
    free(pipes);
    pipes = NULL;
    free(bets);
    return 0;
}

