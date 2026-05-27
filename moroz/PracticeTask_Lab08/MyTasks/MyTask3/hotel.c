#define _GNU_SOURCE
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
#define BACKLOG 3
#define BUFLEN 512
#define MAXNAME 32
#define STACK_SIZE 20
#define MAXWORKERS 4
#define FLOORNUM 13

typedef struct Message
{
    char message[BUFLEN];
    struct sockaddr_in addr;
}Message;

typedef struct GhostMessage
{
    uint8_t floor;
    uint16_t room;
    uint8_t scare_level;
    char name[MAXNAME];
}GhostMessage;

typedef struct CircularBuffer
{
    int tail;
    int count;
    pthread_mutex_t mtx;
    sem_t empty;
    sem_t full;
    Message data[STACK_SIZE];
}CircularBuffer;

typedef struct Hotel
{
    int fright_ledger[FLOORNUM];
    pthread_mutex_t floor_mutexes[FLOORNUM];
    struct sockaddr_in recent_ghosts[FLOORNUM];
}Hotel;

typedef struct ThreadArgs
{
    int server_sockfd;
    pthread_t thread_id;
    Hotel* hotel;
    CircularBuffer* circular_buffer;
}ThreadArgs;

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s <port>\n", name);
    exit(EXIT_FAILURE);
}

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

void* worker_work(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    while (1)
    {
        Message current_message = {0};
        sem_wait(&args->circular_buffer->full);
        pthread_mutex_lock(&args->circular_buffer->mtx);

        args->circular_buffer->tail = (args->circular_buffer->tail-1);
        current_message = args->circular_buffer->data[args->circular_buffer->tail];
        args->circular_buffer->count--;

        pthread_mutex_unlock(&args->circular_buffer->mtx);
        sem_post(&args->circular_buffer->empty);

        usleep(15*1000);
        GhostMessage message = {0};
        if (sscanf(current_message.message, "%hhu|%hu|%hhu|%31s", &message.floor, &message.room, &message.scare_level, message.name)!=4)
        {
            printf("Unreadable wails!\n");
            continue;
        }
        pthread_mutex_lock(&args->hotel->floor_mutexes[message.floor]);
        args->hotel->fright_ledger[message.floor] += message.scare_level;
        args->hotel->recent_ghosts[message.floor] = current_message.addr;
        pthread_mutex_unlock(&args->hotel->floor_mutexes[message.floor]);
        printf("Ghost %s spooked room %d on floor %d with level %d!\n", message.name, message.room, message.floor, message.scare_level);

    }
}

void* manager_work(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    while (1)
    {
        usleep(50*1000);
        int most_scared = 0;
        int idx_most_scared = 0;
        for (int i=0;i<FLOORNUM;i++)
        {
            pthread_mutex_lock(&args->hotel->floor_mutexes[i]);
        }

        for (int i=0;i<FLOORNUM;i++)
        {
            if (most_scared<args->hotel->fright_ledger[i])
            {
                idx_most_scared = i;
                most_scared = args->hotel->fright_ledger[i];
            }
        }

        for (int i=FLOORNUM-1;i>=0;i--)
        {
            pthread_mutex_unlock(&args->hotel->floor_mutexes[i]);
        }

        struct sockaddr_in empty_addr = {0};
        if (memcmp(&args->hotel->recent_ghosts[idx_most_scared], &empty_addr, sizeof(struct sockaddr_in)) == 0)
        {
            continue;
        }

        printf("Floor %d is in panic! Sending calming music...\n", idx_most_scared);
        pthread_mutex_lock(&args->hotel->floor_mutexes[idx_most_scared]);
        char* msg = "CHILL_OUT";
        socklen_t len = sizeof(struct sockaddr_in);
        if (sendto(args->server_sockfd, msg, strlen(msg), 0, (struct sockaddr*)&args->hotel->recent_ghosts[idx_most_scared], len)<0)
        {
            ERR("sendto");
        }
        pthread_mutex_unlock(&args->hotel->floor_mutexes[idx_most_scared]);
    }
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    uint16_t port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM);

    CircularBuffer circular_buffer;
    circular_buffer.count = 0;
    circular_buffer.tail = 0;
    pthread_mutex_init(&circular_buffer.mtx, NULL);
    sem_init(&circular_buffer.empty, 0, STACK_SIZE);
    sem_init(&circular_buffer.full, 0, 0);
    for (int i=0;i<STACK_SIZE;i++)
    {
        memset(circular_buffer.data[i].message,0,BUFLEN);
    }

    Hotel hotel = {0};
    for (int i=0;i<FLOORNUM;i++)
    {
        pthread_mutex_init(&hotel.floor_mutexes[i], NULL);
    }

    ThreadArgs thread_args[MAXWORKERS];
    for (int i=0;i<MAXWORKERS;i++)
    {
        thread_args[i].circular_buffer = &circular_buffer;
        thread_args[i].hotel = &hotel;
        thread_args[i].server_sockfd = server_sockfd;
        pthread_create(&thread_args[i].thread_id, NULL, worker_work, &thread_args[i]);
    }

    ThreadArgs manager_args;
    manager_args.server_sockfd = server_sockfd;
    manager_args.circular_buffer = &circular_buffer;
    manager_args.hotel = &hotel;
    pthread_create(&manager_args.thread_id, NULL, manager_work, &manager_args);
    while (1)
    {
        char buf[BUFLEN];
        struct sockaddr_in addr;
        socklen_t len = sizeof(struct sockaddr_in);
        int bytes_received = recvfrom(server_sockfd, buf, BUFLEN-1, 0, (struct sockaddr*)&addr, &len);
        if (bytes_received < 0)
        {
            ERR("recvfrom");
        }
        buf[bytes_received] = '\0';
        Message current_message = {0};
        memcpy(current_message.message, buf, bytes_received);
        current_message.addr = addr;

        sem_wait(&circular_buffer.empty);
        pthread_mutex_lock(&circular_buffer.mtx);

        circular_buffer.data[circular_buffer.tail] = current_message;
        circular_buffer.tail = (circular_buffer.tail + 1);
        circular_buffer.count++;

        pthread_mutex_unlock(&circular_buffer.mtx);
        sem_post(&circular_buffer.full);

    }

    for (int i=0;i<MAXWORKERS;i++)
    {
        pthread_join(thread_args[i].thread_id, NULL);
    }
    pthread_join(manager_args.thread_id, NULL);
    for (int i=0;i<FLOORNUM;i++)
    {
        pthread_mutex_destroy(&hotel.floor_mutexes[i]);
    }
    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}