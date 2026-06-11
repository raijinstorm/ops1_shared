#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <math.h>
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
    fprintf(stderr, "2<=M<=6 \n");
    exit(EXIT_FAILURE);
}

#define MAXNODES 36
#define MOTHERSHIP_FIFO "/tmp/mothership"
#define BUFLEN 16
#define SUPPLIES_CAPACITY 16

typedef struct Expedition
{
    int32_t id;
    int32_t resources;
}Expedition;

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

int count_descriptors()
{
    int count = 0;
    DIR *dir = opendir("/proc/self/fd");
    if (dir == NULL)
    {
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] != '.')
        {
            count++;
        }
    }

    closedir(dir);
    return count - 1;
}

void child_work(int pipes[MAXNODES][2], int k, int M)
{
    srand(getpid());
    int x = k/M;
    int y = k%M;

    int west_neighbour_x = x;
    int west_neighbour_y = (y-1+M)%M;
    int west_neighbour_k = west_neighbour_x*M + west_neighbour_y;

    int east_neighbour_x = x;
    int east_neighbour_y = (y+1)%M;
    int east_neighbour_k = east_neighbour_x*M + east_neighbour_y;

    int north_neighbour_x = (x+1)%M;
    int north_neighbour_y = y;
    int north_neighbour_k = north_neighbour_x*M + north_neighbour_y;

    int south_neighbour_x = (x-1+M)%M;
    int south_neighbour_y = y;
    int south_neighbour_k = south_neighbour_x*M + south_neighbour_y;

    for (int i=0;i<M*M;i++)
    {
        if (i==k)
        {
            if (close(pipes[i][1])<0)
            {
                ERR("close");
            }
        }
        else if (i==west_neighbour_k || i==east_neighbour_k || i==north_neighbour_k || i==south_neighbour_k)
        {
            if (close(pipes[i][0])<0)
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
            if (close(pipes[i][0])<0)
            {
                ERR("close");
            }
        }
    }

    printf("Node [%d] (%d,%d): Opened {%d} descriptors\n", k, x, y, count_descriptors());

    int local_supplies = rand()%3;
    while (1)
    {
        Expedition expedition = {0};
        int bytes_read = read(pipes[k][0], &expedition, sizeof(Expedition));
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
        {
            break;
        }
        if (expedition.id == 0)
        {
            break;
        }

        printf("Expedition {%d}: arrived at node [%d] (%d,%d)\n", expedition.id, k, x, y);

        if (local_supplies>0)
        {
            int to_take =  (expedition.resources<SUPPLIES_CAPACITY)?(SUPPLIES_CAPACITY-expedition.resources):0;
            if (to_take>local_supplies)
            {
                to_take = local_supplies;
            }
            local_supplies -= to_take;

            expedition.resources+=to_take;
            printf("Expedition {%d}: Good stuff\n", expedition.id);
        }
        if (expedition.resources>0)
        {
            printf("Expedition {%d}: Tommorow comes\n", expedition.id);

            expedition.resources-=1;
            usleep(200*1000);
            int neighbours[4] = {west_neighbour_k, east_neighbour_k, north_neighbour_k, south_neighbour_k};
            int rand_idx = rand()%4;
            int sent = 0, attempts = 0;
            while (!sent && attempts<4)
            {
                if (write(pipes[neighbours[rand_idx]][1], &expedition, sizeof(Expedition))<0)
                {
                    if (errno != EPIPE)
                    {
                        ERR("write");
                    }
                    rand_idx = (rand_idx+1)%4;
                    attempts+=1;
                    continue;
                }
                sent = 1;
            }
            if (attempts == 4)
            {
                printf("Node [%d] (%d,%d): Collapsing\n", k, x, y);
                break;
            }

            if (rand()%100<5)
            {
                printf("Node [%d] (%d,%d): Collapsing\n", k, x, y);
                break;
            }
        }
        else
        {
            printf("Expedition {%d}: For those who come after\n", expedition.id);
            local_supplies+=2;
        }
    }

    if (close(pipes[k][0])<0)
    {
        ERR("close");
    }
    if (close(pipes[west_neighbour_k][1])<0)
    {
        ERR("close");
    }
    if (close(pipes[east_neighbour_k][1])<0)
    {
        ERR("close");
    }
    if (close(pipes[north_neighbour_k][1])<0)
    {
        ERR("close");
    }
    if (close(pipes[south_neighbour_k][1])<0)
    {
        ERR("close");
    }
    printf("Node [%d] (%d,%d): Opened {%d} descriptors\n", k, x, y, count_descriptors());
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    int M = atoi(argv[1]);
    if (M < 3 || M > 6)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGPIPE handler");

    unlink(MOTHERSHIP_FIFO);

    int pipes[MAXNODES][2] = {0};
    int nodes_num = M*M;
    for (int i=0;i<nodes_num;i++)
    {
        pipe(pipes[i]);
    }

    for (int i=0;i<nodes_num;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(pipes, i, M);

            exit(EXIT_SUCCESS);
        }
    }

    for (int i=0;i<nodes_num;i++)
    {
        if (close(pipes[i][0])<0)
        {
            ERR("close");
        }
    }

    printf("Mothership: Opened {%d} descriptors\n", count_descriptors());

    if (mkfifo(MOTHERSHIP_FIFO, 0666)<0)
    {
        ERR("mkfifo");
    }
    FILE* fifo = fopen(MOTHERSHIP_FIFO, "r");
    if (!fifo){
        ERR("open");
    }

    int id = 100;
    while (1)
    {
        char buf[BUFLEN] = {0};
        int closing = 0;
        if (fgets(buf, BUFLEN, fifo) == NULL)
        {
            closing = 1;
        }


        if (closing)
        {
            Expedition terminating_expedition = {0};
            terminating_expedition.id = 0;
            for (int i=0;i<nodes_num;i++)
            {
                if (write(pipes[i][1], &terminating_expedition, sizeof(Expedition))<0)
                {
                    if (errno != EPIPE)
                    {
                        ERR("write");
                    }
                }
            }
            break;
        }

        int x,y;
        if (sscanf(buf, "SPAWN %d %d\n", &x, &y)!=2)
        {
            fprintf(stderr, "Incorrect fifo data\n");
            continue;
        }
        Expedition expedition = {id,SUPPLIES_CAPACITY};

        int target_node = x*M + y;
        if (write(pipes[target_node][1], &expedition, sizeof(Expedition))<0)
        {
            if (errno != EPIPE)
            {
                ERR("write");
            }
            printf("Expedition {%d}: failed\n", expedition.id);
        }

        id--;
    }

    if (fclose(fifo)!=0)
    {
        ERR("fclose");
    }
    for (int i=0;i<nodes_num;i++)
    {
        if (close(pipes[i][1])<0)
        {
            ERR("close");
        }
    }

    while (wait(NULL)>0);

    unlink(MOTHERSHIP_FIFO);
    printf("Mothership: Opened {%d} descriptors\n", count_descriptors());
    return 0;
}