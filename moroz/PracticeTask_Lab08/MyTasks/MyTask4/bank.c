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

#define BACKLOG 3
#define BUFLEN 256
#define QUEUE_LEN 12
#define MAXWORKERS 4
#define BRANCHNUM 5

typedef struct Message
{
    char message[BUFLEN];
}Message;

typedef struct TransferMessage
{
    int amount;
    int sender;
    int receiver;
}TransferMessage;

typedef struct CircularBuffer
{
    int head;
    int tail;
    int count;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    Message data[QUEUE_LEN];
}CircularBuffer;

typedef struct Bank
{
    int vaults[BRANCHNUM];
    pthread_mutex_t vault_mutexes[BRANCHNUM];
}Bank;

typedef struct ThreadArgs
{
    int* work;
    sigset_t mask;
    int server_sockfd;
    pthread_t thread_id;
    CircularBuffer* circular_buffer;
    Bank* bank;
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

void* clerk_work(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    while (*args->work)
    {
        Message current_message = {0};
        pthread_mutex_lock(&args->circular_buffer->mtx);
        while (args->circular_buffer->count == 0 && *args->work)
        {
            pthread_cond_wait(&args->circular_buffer->not_empty,&args->circular_buffer->mtx);
        }
        if (!(*args->work) && args->circular_buffer->count==0)
        {
            pthread_mutex_unlock(&args->circular_buffer->mtx);
            break;
        }
        current_message = args->circular_buffer->data[args->circular_buffer->head];
        args->circular_buffer->head = (args->circular_buffer->head + 1)%QUEUE_LEN;
        args->circular_buffer->count--;
        pthread_cond_signal(&args->circular_buffer->not_full);
        pthread_mutex_unlock(&args->circular_buffer->mtx);

        TransferMessage msg={0};
        if (sscanf(current_message.message, "%d %d %d", &msg.sender, &msg.receiver, &msg.amount)!=3)
        {
            fprintf(stderr, "Malformed message\n");
            continue;
        }
        if (msg.sender<0 || msg.sender>4 || msg.receiver<0 || msg.receiver>4 || msg.amount<0)
        {
            fprintf(stderr, "Incorrect message data\n");
            continue;
        }
        printf("Transferring %d florins from branch %d to branch %d\n", msg.amount, msg.sender, msg.receiver);

        int first = msg.sender, second = msg.receiver;
        if (msg.sender>msg.receiver)
        {
            first = msg.receiver;
            second = msg.sender;
        }
        if (msg.sender == msg.receiver)
        {
            printf("Sender and receiver are the same. Transfer ignored\n");
            continue;
        }
        pthread_mutex_lock(&args->bank->vault_mutexes[first]);
        pthread_mutex_lock(&args->bank->vault_mutexes[second]);

        if (args->bank->vaults[msg.sender]<msg.amount)
        {
            printf("Not enough funds in vault %d\n", msg.sender);
        }
        else
        {
            args->bank->vaults[msg.sender] -= msg.amount;
            args->bank->vaults[msg.receiver] += msg.amount;
            usleep(10*1000);
        }

        pthread_mutex_unlock(&args->bank->vault_mutexes[second]);
        pthread_mutex_unlock(&args->bank->vault_mutexes[first]);
    }

    return NULL;
}

void* signal_handling(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    int signo;
    if (sigwait(&args->mask, &signo))
        ERR("sigwait failed.");
    switch (signo)
    {
    case SIGINT:
        *args->work = 0;
        shutdown(args->server_sockfd, SHUT_RDWR);
        if (close(args->server_sockfd)<0)
        {
            ERR("close");
        }
        pthread_cond_broadcast(&args->circular_buffer->not_empty);
        pthread_cond_broadcast(&args->circular_buffer->not_full);
        break;
    default:
        printf("unexpected signal %d\n", signo);
        exit(1);
    }

    return NULL;
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    sigset_t oldMask, newMask;
    sigemptyset(&newMask);
    sigaddset(&newMask, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &newMask, &oldMask))
        ERR("SIG_BLOCK error");

    int port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM);

    CircularBuffer circular_buffer;
    circular_buffer.count = 0;
    circular_buffer.tail = 0;
    circular_buffer.head = 0;
    pthread_mutex_init(&circular_buffer.mtx, NULL);
    pthread_cond_init(&circular_buffer.not_empty, NULL);
    pthread_cond_init(&circular_buffer.not_full, NULL);
    for (int i=0;i<QUEUE_LEN;i++)
    {
        memset(&circular_buffer.data[i].message,0,BUFLEN);
    }

    Bank bank;
    for (int i=0;i<BRANCHNUM;i++)
    {
        bank.vaults[i] = 1000;
        pthread_mutex_init(&bank.vault_mutexes[i], NULL);
    }

    int work = 1;
    ThreadArgs thread_args[MAXWORKERS];
    for (int i=0;i<MAXWORKERS;i++)
    {
        thread_args[i].work = &work;
        thread_args[i].mask = newMask;
        thread_args[i].circular_buffer = &circular_buffer;
        thread_args[i].server_sockfd = server_sockfd;
        thread_args[i].bank = &bank;
        pthread_create(&thread_args[i].thread_id, NULL, clerk_work, &thread_args[i]);
    }

    ThreadArgs sighandler_args;
    sighandler_args.work = &work;
    sighandler_args.mask = newMask;
    sighandler_args.circular_buffer = &circular_buffer;
    sighandler_args.server_sockfd = server_sockfd;
    sighandler_args.bank = &bank;
    pthread_create(&sighandler_args.thread_id, NULL, signal_handling, &sighandler_args);

    while (work)
    {
        Message current_message = {0};
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int bytes_received = recvfrom(server_sockfd, current_message.message, BUFLEN-1,0, (struct sockaddr*)&addr, &len);
        if (bytes_received == 0)
        {
            break;
        }
        if (bytes_received<0)
        {
            if (errno == EBADF && !work)
            {
                break;
            }
            ERR("recvrom");
        }
        current_message.message[bytes_received] = '\0';

        pthread_mutex_lock(&circular_buffer.mtx);
        while (circular_buffer.count == QUEUE_LEN && work)
        {
            pthread_cond_wait(&circular_buffer.not_full,&circular_buffer.mtx);
        }
        if (!work)
        {
            pthread_mutex_unlock(&circular_buffer.mtx);
            break;
        }
        circular_buffer.data[circular_buffer.tail] = current_message;
        circular_buffer.tail = (circular_buffer.tail + 1) % QUEUE_LEN;
        circular_buffer.count++;
        pthread_cond_signal(&circular_buffer.not_empty);
        pthread_mutex_unlock(&circular_buffer.mtx);

    }

    printf("Bank is closing\n");
    for (int i=0;i<MAXWORKERS;i++)
    {
        pthread_join(thread_args[i].thread_id, NULL);
    }
    pthread_join(sighandler_args.thread_id, NULL);
    for (int i=0;i<BRANCHNUM;i++)
    {
        printf("Branch %d Vault: %d florins\n", i, bank.vaults[i]);
        pthread_mutex_destroy(&bank.vault_mutexes[i]);
    }
    // if (close(server_sockfd)<0)
    // {
    //     ERR("close");
    // }
}