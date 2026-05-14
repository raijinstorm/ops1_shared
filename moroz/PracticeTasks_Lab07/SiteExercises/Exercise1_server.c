#include "common.h"

#define BACKLOG 3
#define BUFMAXSIZE 256

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

typedef struct Packet
{
    char pid_len;
    char pid[8];
}Packet __attribute__((__packed__));

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig) { do_work = 0; }

void handle_connection(int epoll_descriptor, int server_socketfd)
{
    int client_sockfd = add_new_client(server_socketfd);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
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


int16_t calculate_sum(char pid[5], char len)
{
    int sum = 0;
    for (int i=0;i<len;i++)
    {
        sum+=(pid[i]-'0');
    }
    return sum;
}

void handle_drone(int client_socket, int epoll_descriptor, int* highest_sum)
{
    Packet pckt;
    int bytes_read = TEMP_FAILURE_RETRY(bulk_read(client_socket, (char*)&pckt, sizeof(Packet)));
    if (bytes_read == 0)
    {
        clean_client(client_socket, epoll_descriptor);
        return;
    }

    int16_t res = calculate_sum(pckt.pid, pckt.pid_len);
    int16_t res_to_send = htons(res);
    if (bulk_write(client_socket, (char*)&res_to_send, sizeof(int16_t))<0)
    {
        if (errno != EPIPE)
        {
            ERR("write");
        }
    }
    clean_client(client_socket, epoll_descriptor);

    if (res>(*highest_sum))
    {
        *highest_sum = res;
    }
}

void server_work(int server_fd, int* highest_sum)
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_fd, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    while (do_work)
    {
        if (epoll_pwait(epoll_descriptor, &current_event, 1, -1, &oldmask)>0)
        {
            if (current_event.data.fd == server_fd)
            {
                handle_connection(epoll_descriptor, server_fd);
            }
            else
            {
                handle_drone(current_event.data.fd, epoll_descriptor, highest_sum);
            }
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            ERR("epoll_pwait");
        }
    }

    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }
    sigprocmask(SIG_UNBLOCK, &oldmask, NULL);
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    int server_socketfd = bind_tcp_socket(atoi(argv[1]), BACKLOG);

    int highest_sum = 0;
    server_work(server_socketfd, &highest_sum);
    printf("HIGH SUM=%d\n", highest_sum);

    if (close(server_socketfd)<0)
    {
        ERR("close");
    }
    return 0;
}