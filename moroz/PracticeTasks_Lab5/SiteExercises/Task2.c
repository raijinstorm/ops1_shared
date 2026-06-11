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
    fprintf(stderr, "2<=n<=5 - number of processes\n");
    fprintf(stderr, "5<=m<=10 - number of cards\n");
    exit(EXIT_FAILURE);
}

#define MAXPROCESSES 5
#define MAXCARDS 10
#define MESSAGELEN 16

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void child_work(int pipes[MAXPROCESSES][2], int cards[MAXCARDS], int server_pipe[2], int N, int M, int idx)
{
    srand(getpid());
    for (int i=0;i<N;i++)
    {
        if (idx!=i)
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
    if (close(server_pipe[0])<0)
    {
        ERR("close");
    }

    int num_of_rounds = M;
    while (num_of_rounds>0)
    {
        char buf[MESSAGELEN+1] = {0};
        int bytes_read = read(pipes[idx][0], buf, MESSAGELEN);
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
        {
            break;
        }
        buf[bytes_read] = '\0';
        if (strcmp(buf, "new_round") != 0)
        {
            printf("Incorrect message received from server\n");
            continue;
        }

        int rand_idx = rand()%M;
        int attempts = 0;
        while (cards[rand_idx]==0 && attempts<M)
        {
            rand_idx = (rand_idx+1)%M;
            attempts++;
        }
        if (attempts == M)
        {
            break;
        }
        cards[rand_idx] = 0;

        char send_buf[MESSAGELEN] = {0};
        send_buf[0] = (char)idx;
        send_buf[1] = rand_idx+1;
        int dies = 0;
        if (rand()%100<5)
        {
            send_buf[1] = 0;
            dies = 1;
        }
        if (write(server_pipe[1], send_buf, MESSAGELEN)<0)
        {
            if (errno == EPIPE)
            {
                break;
            }
            ERR("write");
        }

        if (dies)
            break;
        num_of_rounds--;
    }

    if (close(pipes[idx][0])<0)
    {
        ERR("close");
    }
    if (close(server_pipe[1])<0)
    {
        ERR("close");
    }
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }

    int N = atoi(argv[1]);
    int M = atoi(argv[2]);
    if (N<2 || N>5 || M<5 || M>10)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGINT handler");

    int pipes[MAXPROCESSES][2];
    for (int i=0;i<N;i++)
    {
        pipe(pipes[i]);
    }
    int server_pipe[2];
    pipe(server_pipe);

    int cards[MAXCARDS] = {0};
    for (int i =0;i<M;i++)
    {
        cards[i] = 1;
    }

    for (int i=0;i<N;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(pipes, cards, server_pipe, N, M, i);

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
    if (close(server_pipe[1])<0)
    {
        ERR("close");
    }

    int player_scores[MAXPROCESSES] = {0};
    int is_dead[MAXPROCESSES] = {0};
    for (int i=0;i<M;i++)
    {
        char msg[MESSAGELEN] = "new_round";
        for (int j=0;j<N;j++)
        {
            if (is_dead[j]==0 && write(pipes[j][1], msg, MESSAGELEN)<0)
            {
                if (EPIPE == errno)
                {
                    is_dead[j] = 1;
                    continue;
                }
                ERR("write");
            }
        }

        int children_left = 1;
        unsigned char max_score = 0;
        int round_results[MAXPROCESSES] = {0};
        int winners_count = 0;

        int alive_count = 0;
        for (int j=0;j<N;j++)
        {
            if (is_dead[j]==0)
            {
                alive_count++;
            }
        }
        for (int j=0;j<alive_count;j++)
        {
            printf("%d alive\n", alive_count);
            char buf[MESSAGELEN] = {0};
            int bytes_read = read(server_pipe[0], buf, MESSAGELEN);
            if (bytes_read<0)
            {
                ERR("read");
            }
            if (bytes_read == 0)
            {
                children_left = 0;
                break;
            }

            unsigned char curr_score = buf[1];
            unsigned char curr_player = buf[0];
            if (curr_score == 0)
            {
                is_dead[curr_player] = 1;
            }
            round_results[curr_player] += (int)curr_score;
            printf("Got number %d from player %d\n", curr_score, curr_player);
            if (curr_score>max_score)
            {
                max_score = curr_score;
            }
        }
        if (!children_left)
            break;
        for (int j=0;j<N;j++)
        {
            if (round_results[j] == max_score)
            {
                winners_count++;
            }
        }
        int award = N/winners_count;
        for (int j=0;j<N;j++)
        {
            if (round_results[j] == max_score)
            {
                player_scores[j] += (int)award;
            }
        }
    }

    int winner = 0;
    int max_score = 0;
    for (int i = 0;i<N;i++)
    {
        if (player_scores[i]>max_score)
        {
            max_score = player_scores[i];
            winner = i;
        }
    }
    printf("Player %d won\n", winner);

    for (int i=0;i<N;i++)
    {
        if (close(pipes[i][1])<0)
        {
            ERR("close");
        }
    }
    if (close(server_pipe[0])<0)
    {
        ERR("close");
    }

    while (wait(NULL)>0);
}