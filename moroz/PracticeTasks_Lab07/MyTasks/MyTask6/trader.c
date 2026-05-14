#include <bits/mman-shared.h>

#include "common.h"

#define BACKLOG 3
#define MAXCONNECTIONS 16
#define MAXLEN 256

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s <unix_socket_path> <voc_ip_address> <voc_port>\n", name);
    exit(EXIT_FAILURE);
}

typedef struct Client
{
    int sockfd;
    int state;
    int bytes_received;
    int is_selling;
    char data[MAXLEN];
}Client;

typedef struct Upstream
{
    int sockfd;
    int bytes_received;
    char data[MAXLEN];
}Upstream;

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

int find_free_client(Client merchants[MAXCONNECTIONS])
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (merchants[i].sockfd == -1) return i;
    }
    return -1;
}

int find_client(Client clients[MAXCONNECTIONS], int client_sockfd)
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (clients[i].sockfd == client_sockfd) return i;
    }
    return -1;
}

void handle_local_connection(int server_local, int epoll_descriptor, Client merchants[MAXCONNECTIONS])
{
    int client_sockfd = add_new_client(server_local);
    int idx = find_free_client(merchants);
    if (idx<0)
    {
        printf("NO SPACE\n");
        if (close(client_sockfd)<0)
        {
            ERR("close");
        }
        return;
    }
    merchants[idx].sockfd = client_sockfd;
    int new_flags = fcntl(client_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(client_sockfd, F_SETFL, new_flags);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
    printf("New merchant arrived at the house\n");
}

void handle_upstream(int server_client, Upstream* upstream, int epoll_descriptor)
{
    int bytes_read = read(server_client, upstream->data+upstream->bytes_received, MAXLEN-upstream->bytes_received);
    if (bytes_read == 0)
    {
        printf("voc disconnected\n");
        clean_client(server_client, epoll_descriptor);
        return;
    }
    upstream->bytes_received+=bytes_read;

    int stop = upstream->bytes_received/4;
    for (int i=0;i<stop;i+=1)
    {
        uint16_t id, price;
        memcpy(&id, upstream->data+i*4, 2);
        memcpy(&price, upstream->data+i*4+2, 2);
        id = ntohs(id);
        price = ntohs(price);
        printf("Item [%d] is now trading at [%d] forins\n", id, price);
    }
    memmove(upstream->data, upstream->data+(upstream->bytes_received - upstream->bytes_received%4), upstream->bytes_received%4);
    upstream->bytes_received = upstream->bytes_received%4;
}

void handle_merchant(int client_socket, Upstream* upstream, int epoll_descriptor, Client merchants[MAXCONNECTIONS])
{
    int idx = find_client(merchants, client_socket);
    if (idx<0)
    {
        printf("INCORRECT SOCKET\n");
        return;
    }
    if (merchants[idx].state == 0)
    {
        char c;
        int bytes_read = bulk_read(client_socket, &c, 1);
        if (bytes_read == 0)
        {
            clean_client(client_socket, epoll_descriptor);
            memset(merchants[idx].data,0,MAXLEN);
            merchants[idx].bytes_received = 0;
            merchants[idx].sockfd = -1;
            merchants[idx].state = 0;
            return;
        }
        if (c == (char)0x02)
        {
            merchants[idx].is_selling = 1;
        }
        merchants[idx].state = 1;
        return;
    }
    else if (merchants[idx].state == 1)
    {
        int bytes_read = bulk_read(client_socket, merchants[idx].data+merchants[idx].bytes_received, 2-merchants[idx].bytes_received);
        if (bytes_read == 0)
        {
            clean_client(client_socket, epoll_descriptor);
            memset(merchants[idx].data,0,MAXLEN);
            merchants[idx].bytes_received = 0;
            merchants[idx].sockfd = -1;
            merchants[idx].state = 0;
            return;
        }
        merchants[idx].bytes_received+=bytes_read;

        if (merchants[idx].bytes_received == 2)
        {
            uint16_t id;
            memcpy(&id, merchants[idx].data, 2);
            id = ntohs(id);

            char* preamble = "BUY";
            if (merchants[idx].is_selling)
            {
                preamble = "SELL";
            }
            char msg[MAXLEN] = {0};
            snprintf(msg, MAXLEN, "%s: [%d]", preamble, id);
            if (write(upstream->sockfd, msg, strlen(msg))<0)
            {
                if (errno == EPIPE)
                {
                    clean_client(upstream->sockfd, epoll_descriptor);
                    upstream->bytes_received = 0;
                    memset(upstream->data,0,MAXLEN);
                    return;
                }
                ERR("write");
            }
            merchants[idx].bytes_received = 0;
            memset(merchants[idx].data,0,MAXLEN);
            merchants[idx].is_selling = 0;
            merchants[idx].state = 0;
        }
    }
}

void server_work(int server_local, int server_client)
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = server_local;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_local, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    event.data.fd = server_client;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_client, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    Client merchants[MAXCONNECTIONS];
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        merchants[i].sockfd = -1;
        merchants[i].bytes_received = 0;
        merchants[i].state = 0;
        merchants[i].is_selling = 0;
        memset(merchants[i].data,0,MAXLEN);
    }

    Upstream upstream;
    upstream.sockfd = server_client;
    upstream.bytes_received = 0;
    memset(upstream.data,0,MAXLEN);

    while (1)
    {
        if (epoll_wait(epoll_descriptor, &current_event, 1, -1)>0)
        {
            if (current_event.data.fd == server_local)
            {
                handle_local_connection(server_local, epoll_descriptor, merchants);
            }
            else if (current_event.data.fd == server_client)
            {
                handle_upstream(server_client, &upstream, epoll_descriptor);
            }
            else
            {
                handle_merchant(current_event.data.fd, &upstream, epoll_descriptor, merchants);
            }
        }
        else
        {
            ERR("epoll_wait");
        }
    }
}

int main(int argc, char** argv)
{
    if (argc!=4)
    {
        usage(argv[0]);
    }
    sethandler(SIG_IGN, SIGPIPE);

    int server_local = bind_local_socket(argv[1], BACKLOG);
    int new_flags = fcntl(server_local, F_GETFL) | O_NONBLOCK;
    fcntl(server_local, F_SETFL, new_flags);
    int server_client = connect_tcp_socket(argv[2], argv[3]);
    new_flags = fcntl(server_client, F_GETFL) | O_NONBLOCK;
    fcntl(server_client, F_SETFL, new_flags);

    server_work(server_local, server_client);

    if (close(server_local)<0)
    {
        ERR("close");
    }
    if (unlink(argv[1])<0)
    {
        ERR("unlink");
    }
}