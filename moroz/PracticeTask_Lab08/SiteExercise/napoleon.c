#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

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
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

//   gdb ./napoleon
//   run <port>
//   Ctrl+C
//   thread apply all bt

#define BACKLOG 3
#define NAMELEN 128
#define BUFLEN 256
#define STACKLEN 16
#define ADJUTANTNUM 4
#define DIVISION_NAMES_SIZE 128
#define MAPDIM 100

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s <port>\n", name);
    exit(EXIT_FAILURE);
}

typedef struct Message
{
    char message[BUFLEN];
    struct sockaddr_in addr;
}Message;

typedef struct StackMessage
{
    int tail;
    int count;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    Message data[STACKLEN];
}StackMessage;

typedef struct Division
{
    char name[NAMELEN+1];
    int idx;
    int is_allied;
    pthread_mutex_t division_mutex;
    struct sockaddr_in addr;
}Division;

typedef struct BoardData
{
    int divisions_count;
    pthread_mutex_t divisions_mutex;
    Division divisions[DIVISION_NAMES_SIZE];
    int map[MAPDIM][MAPDIM];
    pthread_mutex_t row_mutexes[MAPDIM];
}BoardData;

typedef struct ThreadArgs
{
    int server_sockfd;
    pthread_t thread_id;
    StackMessage* message_stack;
    BoardData* board_data;
}ThreadArgs;

typedef struct ReportMessage
{
    uint8_t x;
    uint8_t y;
    uint8_t allegiance;
    char division_name[NAMELEN+1];
}ReportMessage;

int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

int bind_inet_socket(uint16_t port, int type)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(PF_INET, type);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (SOCK_STREAM == type)
        if (listen(socketfd, BACKLOG) < 0)
            ERR("listen");
    return socketfd;
}

void* adjutant_work(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    while (1)
    {
        Message current_message = {0};
        pthread_mutex_lock(&args->message_stack->mtx);
        while (args->message_stack->count==0)
        {
            pthread_cond_wait(&args->message_stack->not_empty, &args->message_stack->mtx);
        }
        args->message_stack->tail-=1;
        current_message = args->message_stack->data[args->message_stack->tail];
        args->message_stack->count-=1;
        pthread_cond_signal(&args->message_stack->not_full);
        pthread_mutex_unlock(&args->message_stack->mtx);

        ReportMessage msg = {0};
        if (sscanf(current_message.message, "%hhu %hhu %hhu %128s", &msg.x, &msg.y, &msg.allegiance, msg.division_name)!=4)
        {
            printf("Malformed message\n");
            continue;
        }
        if (msg.x>99 || msg.x<0 || msg.y>99 || msg.y<0 || msg.allegiance>1 || msg.allegiance<0)
        {
            printf("Incorrect data\n");
            continue;
        }
        printf("%s division %s was seen at position %d:%d\n", (msg.allegiance==1)?"our":"enemy", msg.division_name, msg.x, msg.y);
        usleep(10*1000);

        pthread_mutex_lock(&args->board_data->divisions_mutex);
        int division_idx = -1;
        for (int i=0;i<args->board_data->divisions_count;i++)
        {
            if (strcmp(args->board_data->divisions[i].name, msg.division_name) == 0)
            {
                division_idx = i;

                pthread_mutex_lock(&args->board_data->divisions[i].division_mutex);
                args->board_data->divisions[i].addr = current_message.addr;
                pthread_mutex_unlock(&args->board_data->divisions[i].division_mutex);

                break;
            }
        }
        if (division_idx == -1)
        {
            size_t len = strlen(msg.division_name);
            memcpy(args->board_data->divisions[args->board_data->divisions_count].name,msg.division_name,len);
            args->board_data->divisions[args->board_data->divisions_count].name[len] = '\0';
            args->board_data->divisions[args->board_data->divisions_count].idx = args->board_data->divisions_count;
            args->board_data->divisions[args->board_data->divisions_count].is_allied = msg.allegiance;
            args->board_data->divisions[args->board_data->divisions_count].addr = current_message.addr;
            division_idx = args->board_data->divisions_count;
            args->board_data->divisions_count +=1;
        }
        pthread_mutex_unlock(&args->board_data->divisions_mutex);

        pthread_mutex_lock(&args->board_data->divisions[division_idx].division_mutex);
        int found = 0;
        for (int y=0;y<MAPDIM;y++)
        {
            pthread_mutex_lock(&args->board_data->row_mutexes[y]);
            for (int x=0;x<MAPDIM;x++)
            {
                if (args->board_data->map[y][x] == division_idx)
                {
                    args->board_data->map[y][x] = -1;
                    found = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&args->board_data->row_mutexes[y]);
            if (found == 1)
                break;
        }
        pthread_mutex_lock(&args->board_data->row_mutexes[msg.y]);
        args->board_data->map[msg.y][msg.x] = division_idx;
        pthread_mutex_unlock(&args->board_data->row_mutexes[msg.y]);

        pthread_mutex_unlock(&args->board_data->divisions[division_idx].division_mutex);
    }
}

void* napoleon_work(void* t_args)
{
    srand(time(NULL));
    ThreadArgs* args = (ThreadArgs*)t_args;
    while (1)
    {
        usleep(30*1000);
        for (int i=0;i<MAPDIM;i++)
        {
            pthread_mutex_lock(&args->board_data->row_mutexes[i]);
            for (int j=0;j<MAPDIM;j++)
            {
                printf("%d ",args->board_data->map[i][j]);
                fflush(stdout);
            }
            pthread_mutex_unlock(&args->board_data->row_mutexes[i]);
            printf("\n");
        }

        pthread_mutex_lock(&args->board_data->divisions_mutex);
        int allied[DIVISION_NAMES_SIZE] = {-1};
        int allied_count = 0;
        for (int i=0;i<args->board_data->divisions_count;i++)
        {
            if (args->board_data->divisions[i].is_allied)
            {
                allied[allied_count] = i;
                allied_count++;
                printf("%d  AAAA\n", allied_count);
            }
        }
        if (allied_count == 0)
        {
            pthread_mutex_unlock(&args->board_data->divisions_mutex);
            continue;
        }
        int idx = rand()%allied_count;
        pthread_mutex_lock(&args->board_data->divisions[allied[idx]].division_mutex);
        int new_x = rand()%MAPDIM, new_y = rand()%MAPDIM;
        char msg[BUFLEN];
        snprintf(msg,BUFLEN, "%d %d %d %s", new_x, new_y, 1, args->board_data->divisions[allied[idx]].name);
        socklen_t s_len = sizeof(struct sockaddr_in);
        sendto(args->server_sockfd, msg, strlen(msg),0,(struct sockaddr*)&args->board_data->divisions[allied[idx]].addr,s_len);
        pthread_mutex_unlock(&args->board_data->divisions[allied[idx]].division_mutex);
        pthread_mutex_unlock(&args->board_data->divisions_mutex);
    }
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }
    int port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM);

    StackMessage message_stack;
    message_stack.count = 0;
    message_stack.tail = 0;
    pthread_mutex_init(&message_stack.mtx, NULL);
    pthread_cond_init(&message_stack.not_empty, NULL);
    pthread_cond_init(&message_stack.not_full, NULL);
    for (int i=0;i<STACKLEN;i++)
    {
        memset(&message_stack.data[i].message,0,BUFLEN);
    }

    BoardData board_data;
    board_data.divisions_count = 0;
    pthread_mutex_init(&board_data.divisions_mutex, NULL);
    for (int i = 0;i<MAPDIM;i++)
    {
        for (int j=0;j<MAPDIM;j++)
        {
            board_data.map[i][j] = -1;
        }
        pthread_mutex_init(&board_data.row_mutexes[i], NULL);
    }
    for (int i=0;i<DIVISION_NAMES_SIZE;i++)
    {
        board_data.divisions[i].idx = -1;
        board_data.divisions[i].is_allied = 0;
        memset(board_data.divisions[i].name, 0, NAMELEN+1);
        pthread_mutex_init(&board_data.divisions[i].division_mutex, NULL);
    }

    ThreadArgs t_args[ADJUTANTNUM];
    for (int i=0;i<ADJUTANTNUM;i++)
    {
        t_args[i].server_sockfd = server_sockfd;
        t_args[i].message_stack = &message_stack;
        t_args[i].board_data = &board_data;
        pthread_create(&t_args[i].thread_id, NULL, adjutant_work, &t_args[i]);
    }

    ThreadArgs napoleon_args;
    napoleon_args.server_sockfd = server_sockfd;
    napoleon_args.message_stack = &message_stack;
    napoleon_args.board_data = &board_data;
    pthread_create(&napoleon_args.thread_id, NULL, napoleon_work, &napoleon_args);

    while (1)
    {
        Message current_message = {0};
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int bytes_received = recvfrom(server_sockfd, current_message.message, BUFLEN-1,0, (struct sockaddr*)&addr, &len);
        if (bytes_received<0)
        {
            ERR("recvrom");
        }
        current_message.message[bytes_received] = '\0';
        current_message.addr = addr;

        pthread_mutex_lock(&message_stack.mtx);
        while (message_stack.count == STACKLEN)
        {
            pthread_cond_wait(&message_stack.not_full, &message_stack.mtx);
        }
        message_stack.data[message_stack.tail] = current_message;
        message_stack.tail += 1;
        message_stack.count += 1;
        pthread_cond_signal(&message_stack.not_empty);
        pthread_mutex_unlock(&message_stack.mtx);

    }

    for (int i=0;i<ADJUTANTNUM;i++)
    {
        pthread_join(t_args[i].thread_id, NULL);
    }
    pthread_mutex_destroy(&message_stack.mtx);
    pthread_cond_destroy(&message_stack.not_empty);
    pthread_cond_destroy(&message_stack.not_full);
    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
}
