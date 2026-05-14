#include <pthread.h>

#include "common.h"
#include <sys/wait.h>

#define BACKLOG 3
#define BUFLEN 96
#define LOGGERBUF 256

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s <path> <port> <port>\n", name);
    exit(EXIT_FAILURE);
}

typedef struct Logger
{
    int sockfd;
}Logger;

typedef struct __attribute__((__packed__))
{
    uint32_t id;
    uint16_t depth;
    uint16_t status;
    uint32_t timestamp;
}Packet;

typedef struct __attribute__((__packed__))
{
    Packet packet;
    uint16_t length;
    char data[];
}LoggerPacket;

typedef struct AnomalyRecord
{
    uint32_t id;
    uint32_t timestamp;
}AnomalyRecord;

typedef struct SharedState
{
    int corruption_count;
    pthread_mutex_t mtx;
    AnomalyRecord records[50];
} SharedState;

typedef struct ThreadArgs
{
    SharedState* shared_state;
    pthread_t tid;
    uint32_t id;
    uint32_t timestamp;
}ThreadArgs;


int bind_tcp_ipv6_socket(char* port, int backlog_size)
{
    int t = 1;
    int ret;
    struct sockaddr_in6 addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET6;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    if ((ret = getaddrinfo(NULL, port, &hints, &result)))
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }
    addr = *(struct sockaddr_in6 *)(result->ai_addr);
    int socketfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (socketfd<0)
    {
        ERR("socket");
    }
    freeaddrinfo(result);

    int sockopt = 0;
    //for enabling dual-stack (connections from both ipv4 and ipv6)
    if (setsockopt(socketfd, IPPROTO_IPV6, IPV6_V6ONLY, &sockopt, sizeof (sockopt)))
        ERR("setsockopt");
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(socketfd, backlog_size) < 0)
        ERR("listen");
    return socketfd;
}

void clean_client(int client_socket, int epoll_descriptor)
{
    struct epoll_event event;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, client_socket, &event) == -1)
    {
        perror("epoll_ctl: client_sock");
        exit(EXIT_FAILURE);
    }
    if (close(client_socket)<0) ERR("close");
}

void* thread_func(void* void_args)
{
    ThreadArgs* args = (ThreadArgs*)void_args;
    sleep(2);
    int local_anomalies_cnt = 0;
    pthread_mutex_lock(&args->shared_state->mtx);
    if (args->shared_state->corruption_count<50)
    {
        args->shared_state->records[args->shared_state->corruption_count].id = args->id;
        args->shared_state->records[args->shared_state->corruption_count].timestamp = args->timestamp;
    }
    args->shared_state->corruption_count++;
    local_anomalies_cnt = args->shared_state->corruption_count;
    pthread_mutex_unlock(&args->shared_state->mtx);

    printf("Worker [%lu]: Diagnostics complete for Probe [%d]. Total anomalies: %d\n", args->tid, args->id, local_anomalies_cnt);
    free(args);

    return NULL;
}

void logger_work(char* name, SharedState* shared_state)
{
    int logger_sockfd = connect_local_socket(name);
    printf("Logger: Connected to local socket %s\n", name);

    while (1)
    {
        char buf[LOGGERBUF];
        LoggerPacket* logger_packet = (LoggerPacket*)buf;
        memset(logger_packet->data, 0, sizeof(buf) - sizeof(LoggerPacket));
        int bytes_received = bulk_read(logger_sockfd, buf, sizeof(LoggerPacket));
        if (bytes_received == 0)
        {
            break;
        }
        int msg_len = ntohs(logger_packet->length);
        bytes_received = bulk_read(logger_sockfd, (char*)(logger_packet->data), msg_len);
        if (bytes_received == 0)
        {
            break;
        }
        printf("Logger received: %s (Struct ID: %u)\n", logger_packet->data, logger_packet->packet.id);

    }

    if (close(logger_sockfd)<0)
    {
        ERR("close");
    }
}

void handle_local_connection(int local_server_sockfd, Logger* logger, int epoll_descriptor)
{
    int logger_sockfd = add_new_client(local_server_sockfd);
    if (logger->sockfd != -1)
    {
        if (close(logger_sockfd)<0) ERR("close");
        printf("Logger already present!\n");
    }
    logger->sockfd = logger_sockfd;
}

void handle_connection(int epoll_descriptor, int server_socketfd)
{
    int client_sockfd = add_new_client(server_socketfd);
    int new_flags = fcntl(client_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(client_sockfd, F_SETFL, new_flags);

    printf("Client connected\n");
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
    printf("Hub: Registered new probe on fd {%d}.\n", client_sockfd);
}

void handle_client(int client_sockfd, int epoll_descriptor, Logger* logger, SharedState* shared_state)
{
    char buf[BUFLEN];
    int bytes_read = TEMP_FAILURE_RETRY(bulk_read(client_sockfd, buf,sizeof(Packet)));
    if (bytes_read == 0)
    {
        clean_client(client_sockfd, epoll_descriptor);
        return;
    }
    Packet* packet = (Packet*)buf;
    uint16_t depth = ntohs(packet->depth);
    uint32_t id = ntohl(packet->id);
    uint32_t timestamp = ntohl(packet->timestamp);
    uint16_t status = ntohs(packet->status);
    printf("%d %d %d %d\n", depth, id, timestamp, status);

    if (status == 65535)
    {
        ThreadArgs* args = malloc(sizeof(ThreadArgs));
        args->shared_state = shared_state;
        args->id = id;
        args->timestamp = timestamp;

        pthread_attr_t attr;

        if (pthread_attr_init(&attr))
            ERR("pthread_attr_init");
        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
            ERR("pthread_attr_setdetachstate");

        if (pthread_create(&args->tid, &attr, thread_func, args))
        {
            ERR("pthread_create");
        }
        pthread_attr_destroy(&attr);
    }

    printf("Hub: Parsed frame from Probe [%d] at depth %dm.\n", id, depth);
    if (logger->sockfd == -1)
    {
        printf("Logger is not ready yet!\n");
        return;
    }

    char logger_buf[LOGGERBUF];
    LoggerPacket* logger_packet = (LoggerPacket*)logger_buf;
    logger_packet->packet.depth = (depth);
    logger_packet->packet.status = (status);
    logger_packet->packet.id = (id);
    logger_packet->packet.timestamp = (timestamp);
    int msg_len = snprintf(logger_packet->data, sizeof(logger_buf) - sizeof(LoggerPacket), "[DIAGNOSTIC] Probe %d reporting.\n", id);
    logger_packet->length = htons((uint16_t)msg_len);
    if (TEMP_FAILURE_RETRY(bulk_write(logger->sockfd, logger_buf, sizeof(LoggerPacket)+msg_len))<0)
    {
        if (errno == EPIPE)
        {
            if (close(logger->sockfd)<0)
            {
                ERR("close");
            }
            logger->sockfd = -1;
            printf("Connection with logger was lost\n");
            return;
        }
        ERR("write");
    }
}

void server_work(int ipv4_server_sockfd, int ipv6_server_sockfd, int local_server_sockfd, SharedState* shared_state)
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = ipv4_server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, ipv4_server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    event.data.fd =local_server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, local_server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: local_server");
        exit(EXIT_FAILURE);
    }
    event.data.fd = ipv6_server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, ipv6_server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: server_ipv6_sockfd");
        exit(EXIT_FAILURE);
    }

    Logger logger = {-1};
    while (1)
    {
        if (epoll_wait(epoll_descriptor, &current_event, 1, -1)>0)
        {
            if (current_event.data.fd == local_server_sockfd)
            {
                handle_local_connection(local_server_sockfd, &logger, epoll_descriptor);
            }
            else if (current_event.data.fd == ipv6_server_sockfd)
            {
                handle_connection(epoll_descriptor, ipv6_server_sockfd);
            }
            else if (current_event.data.fd == ipv4_server_sockfd)
            {
                handle_connection(epoll_descriptor, ipv4_server_sockfd);
            }
            else
            {
                handle_client(current_event.data.fd, epoll_descriptor, &logger, shared_state);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc!=4)
    {
        usage(argv[0]);
    }
    sethandler(SIG_IGN, SIGPIPE);

    int local_server_sockfd = bind_local_socket(argv[1], BACKLOG);

    int ipv4_sockfd = bind_tcp_socket(atoi(argv[2]), BACKLOG);
    int new_flags = fcntl(ipv4_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(ipv4_sockfd, F_SETFL, new_flags);

    int ipv6_sockfd = bind_tcp_ipv6_socket(argv[3], BACKLOG);
    new_flags = fcntl(ipv6_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(ipv6_sockfd, F_SETFL, new_flags);

    SharedState shared_state={.corruption_count=0,.mtx=PTHREAD_MUTEX_INITIALIZER};

    pid_t logger_pid = fork();
    if (logger_pid == 0)
    {
        logger_work(argv[1], &shared_state);

        exit(EXIT_SUCCESS);
    }

    server_work(ipv4_sockfd, ipv6_sockfd, local_server_sockfd, &shared_state);

    while (wait(NULL)>0);
    if (close(ipv4_sockfd)<0)
    {
        ERR("close");
    }
    if (close(ipv6_sockfd)<0)
    {
        ERR("close");
    }
    if (close(local_server_sockfd) < 0)
    {
        ERR("close");
    }
    if (unlink(argv[1])<0)
    {
        ERR("unlink");
    }
    return 0;
}