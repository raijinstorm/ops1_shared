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
#define MAXPACKAGES 20
#define MAXWORKERS 4

typedef struct __attribute__((__packed__)){
    int32_t package_id;
    int32_t sorting_time_ms;
}Packet;

typedef struct Data
{
    Packet packet;
    struct sockaddr_in addr;
}Data;

typedef struct CircularBuffer
{
    int head;
    int tail;
    int count;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    Data data[MAXPACKAGES];
}CircularBuffer;

typedef struct ThreadArgs
{
    CircularBuffer* circular_buffer;
    pthread_t thread_id;
    int server_sockfd;
    int* work;
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
        pthread_mutex_lock(&args->circular_buffer->mtx);
        while (args->circular_buffer->count == 0 && *args->work)
        {
            pthread_cond_wait(&args->circular_buffer->not_empty,&args->circular_buffer->mtx);
        }
        if (!args->work && args->circular_buffer->count == 0)
        {
            pthread_mutex_unlock(&args->circular_buffer->mtx);
            break;
        }
        Data curr_data = args->circular_buffer->data[args->circular_buffer->head];
        args->circular_buffer->head = (args->circular_buffer->head + 1)%MAXPACKAGES;
        args->circular_buffer->count--;

        pthread_cond_signal(&args->circular_buffer->not_full);
        pthread_mutex_unlock(&args->circular_buffer->mtx);

        usleep(curr_data.packet.sorting_time_ms*1000);
        printf("Package %d sorted!\n", curr_data.packet.package_id);

        int32_t to_send = htonl(curr_data.packet.package_id);
        socklen_t len = sizeof(curr_data.addr);
        if (sendto(args->server_sockfd,&to_send, sizeof(int32_t), 0, (struct sockaddr*)&curr_data.addr, len)<0)
        {
            ERR("sendto");
        }
    }
    return NULL;
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }
    uint16_t port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port,SOCK_DGRAM);

    CircularBuffer circular_buffer;
    circular_buffer.count = 0;
    circular_buffer.tail = 0;
    circular_buffer.head = 0;
    pthread_mutex_init(&circular_buffer.mtx, NULL);
    pthread_cond_init(&circular_buffer.not_empty, NULL);
    pthread_cond_init(&circular_buffer.not_full, NULL);
    for (int i=0;i<MAXPACKAGES;i++)
    {
        memset(&circular_buffer.data[i].addr,0,sizeof(struct sockaddr_in));
        memset(&circular_buffer.data[i].packet, 0, sizeof(Packet));
    }

    int work = 1;
    ThreadArgs thread_args[MAXWORKERS];
    for (int i=0;i<MAXWORKERS;i++)
    {
        thread_args[i].circular_buffer = &circular_buffer;
        thread_args[i].server_sockfd = server_sockfd;
        thread_args[i].work = &work;
        pthread_create(&thread_args[i].thread_id, NULL, worker_work, &thread_args[i]);
    }

    while (1)
    {
        Packet pckt = {0};
        struct sockaddr_in addr;
        socklen_t len = sizeof(struct sockaddr_in);
        int bytes_received = recvfrom(server_sockfd, &pckt, sizeof(Packet), 0, (struct sockaddr*)&addr, &len);
        if (bytes_received<0)
        {
            ERR("recvfrom");
        }
        if (bytes_received != sizeof(Packet))
            continue;

        pckt.package_id = ntohl(pckt.package_id);
        pckt.sorting_time_ms = ntohl(pckt.sorting_time_ms);
        if (pckt.package_id == -1)
        {
            work = 0;
            break;
        }

        pthread_mutex_lock(&circular_buffer.mtx);
        while (circular_buffer.count==MAXPACKAGES)
        {
            pthread_cond_wait(&circular_buffer.not_full, &circular_buffer.mtx);
        }
        Data curr_data = {pckt, addr};
        circular_buffer.data[circular_buffer.tail] = curr_data;
        circular_buffer.tail = (circular_buffer.tail+1)%MAXPACKAGES;
        circular_buffer.count++;

        pthread_cond_signal(&circular_buffer.not_empty);
        pthread_mutex_unlock(&circular_buffer.mtx);
    }

    pthread_cond_broadcast(&circular_buffer.not_empty);
    for (int i=0;i<MAXPACKAGES;i++)
    {
        pthread_join(thread_args[i].thread_id, NULL);
    }

    printf("Post office closed!\n");
    pthread_mutex_destroy(&circular_buffer.mtx);
    pthread_cond_destroy(&circular_buffer.not_empty);
    pthread_cond_destroy(&circular_buffer.not_full);
    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}

