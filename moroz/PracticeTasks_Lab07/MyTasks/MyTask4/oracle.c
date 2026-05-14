#include "common.h"

#define BACKLOG 3
#define IDENTITYLEN 4
#define MAXCONNECTIONS 512
#define MAXLEN 65536

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s <unix_socket_path> <ipv4_port> <ipv6_port>\n", name);
    exit(EXIT_FAILURE);
}

typedef struct Client
{
    int sockfd;
    uint32_t identity;
    int bytes_read;
    int samurai_bytes_received;
    int state;
    char data[MAXLEN];
}Client;


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

void clean_data_client(Client* client)
{
    client->sockfd = -1;
    client->identity = 0;
    memset(client->data, 0, MAXLEN);
    client->bytes_read = 0;
    client->samurai_bytes_received = 0;
    client->state = 0;
}

void clean_connection(Client* client, int epoll_descriptor)
{
    clean_client(client->sockfd, epoll_descriptor);
    clean_data_client(client);
}

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

    // int sockopt = 0;
    // //for enabling dual-stack (connections from both ipv4 and ipv6)
    // if (setsockopt(socketfd, IPPROTO_IPV6, IPV6_V6ONLY, &sockopt, sizeof (sockopt)))
    //     ERR("setsockopt");
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(socketfd, backlog_size) < 0)
        ERR("listen");
    return socketfd;
}


void handle_local_connection(int server_local, int epoll_descriptor, Client* street_samurai)
{
    int client_sockfd = add_new_client(server_local);
    int new_flags = fcntl(client_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(client_sockfd, F_SETFL, new_flags);
    if (street_samurai->sockfd != -1)
    {
        if (close(client_sockfd)<0)
        {
            ERR("close");
        }
        return;
    }
    street_samurai->sockfd = client_sockfd;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
}

int find_free_index(Client connections[MAXCONNECTIONS])
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (connections[i].sockfd == -1) return i;
    }
    return -1;
}

int find_client_index(int client_sockfd, Client connections[MAXCONNECTIONS])
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (connections[i].sockfd == client_sockfd) return i;
    }
    return -1;
}

void handle_tcp_connection(int server_sockfd, int epoll_descriptor, Client* connections)
{
    int client_sockfd = add_new_client(server_sockfd);
    int new_flags = fcntl(client_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(client_sockfd, F_SETFL, new_flags);
    int idx = find_free_index(connections);
    if (idx<0)
    {
        if (close(client_sockfd)<0)
        {
            ERR("close");
        }
        return;
    }
    connections[idx].sockfd = client_sockfd;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
}

void handle_corporates_connection(int server_sockfd, Client* samurai)
{
    int client_sockfd = add_new_client(server_sockfd);
    uint32_t to_send = htonl(samurai->samurai_bytes_received);
    if (write(client_sockfd, &to_send, sizeof(uint32_t))<0)
    {
        if (errno == EPIPE)
        {
            printf("Connection lost\n");
            if (close(client_sockfd)<0)
            {
                ERR("close");
            }
            return;
        }
        ERR("write");
    }

    if (close(client_sockfd)<0)
    {
        ERR("close");
    }
}


int is_ipv4_connection(Client connections[MAXCONNECTIONS], int client_sockfd)
{
    if (client_sockfd == -1)
    {
        return 0;
    }
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (connections[i].sockfd == client_sockfd)
        {
            return 1;
        }
    }
    return 0;
}

void handle_street_samurai(int street_samurai_sockfd, Client* street_samurai, int epoll_descriptor)
{
    if (IDENTITYLEN-street_samurai->bytes_read == 0)
    {
        clean_connection(street_samurai, epoll_descriptor);

        return;
    }

    if (street_samurai->state == 0)
    {
        int bytes_read = read(street_samurai->sockfd, street_samurai->data+street_samurai->bytes_read,4-street_samurai->bytes_read);
        if (bytes_read==0)
        {
            printf("Connection lost\n");
            clean_connection(street_samurai, epoll_descriptor);

            return;
        }
        if (bytes_read<0)
        {
            ERR("read");
        }
        street_samurai->bytes_read+=bytes_read;

        if (street_samurai->bytes_read == IDENTITYLEN)
        {
            uint32_t identity;
            memcpy(&identity, street_samurai->data, sizeof(uint32_t));
            street_samurai->identity = ntohl(identity);

            street_samurai->state = 1;
            if (street_samurai->identity == 3735928559)
            {
                printf("Samurai authenticated.\n");

                memset(street_samurai->data, 0, IDENTITYLEN);
                street_samurai->bytes_read = 0;
            }
            else
            {
                printf("Intruder detected\n");
                clean_connection(street_samurai, epoll_descriptor);

                return;
            }
        }
    }

    else if (street_samurai->state == 1)
    {
        int bytes_read = read(street_samurai->sockfd, street_samurai->data+street_samurai->bytes_read,1);
        if (bytes_read==0)
        {
            printf("Connection lost\n");
            clean_connection(street_samurai, epoll_descriptor);

            return;
        }
        if (bytes_read<0)
        {
            ERR("read");
        }
    }
}

void handle_fixer(int client_socket, int epoll_descriptor, Client connections[MAXCONNECTIONS], Client* street_samurai)
{
    int idx = find_client_index(client_socket, connections);
    if (idx<0)
    {
        printf("BULLSHIT\n");
        return;
    }
    if (connections[idx].state == 0)
    {
        int bytes_read = read(connections[idx].sockfd, connections[idx].data+connections[idx].bytes_read, 2-connections[idx].bytes_read);
        if (bytes_read==0)
        {
            printf("Connection lost\n");
            clean_connection(&connections[idx], epoll_descriptor);

            return;
        }
        if (bytes_read<0)
        {
            ERR("read");
        }
        connections[idx].bytes_read+=bytes_read;

        if (connections[idx].bytes_read == sizeof(uint16_t))
        {
            uint16_t identity;
            memcpy(&identity, connections[idx].data, sizeof(uint16_t));
            connections[idx].identity = ntohs(identity);

            connections[idx].state = 1;
            memset(connections[idx].data, 0, MAXLEN);
            connections[idx].bytes_read = 0;
        }
    }
    if (connections[idx].state == 1)
    {
        int bytes_read = read(connections[idx].sockfd, connections[idx].data+connections[idx].bytes_read, connections[idx].identity-connections[idx].bytes_read);
        if (bytes_read==0)
        {
            printf("Connection lost\n");
            clean_connection(&connections[idx], epoll_descriptor);

            return;
        }
        if (bytes_read<0)
        {
            ERR("read");
        }
        connections[idx].bytes_read+=bytes_read;

        if (connections[idx].bytes_read == connections[idx].identity)
        {
            if (street_samurai->sockfd == -1)
            {
                connections[idx].state = 0;
                memset(connections[idx].data, 0, MAXLEN);
                connections[idx].bytes_read = 0;
                return;
            }
            char msg[2*MAXLEN];
            snprintf(msg, 2*MAXLEN, "Fixer: %s", connections[idx].data);
            if (write(street_samurai->sockfd, msg, strlen(msg))<0)
            {
                if (errno == EPIPE)
                {
                    printf("Connection lost\n");
                    clean_connection(street_samurai, epoll_descriptor);
                    return;
                }
            }
            street_samurai->samurai_bytes_received+=connections[idx].bytes_read;
            connections[idx].state = 0;
            memset(connections[idx].data, 0, MAXLEN);
            connections[idx].bytes_read = 0;
        }
    }


}

void server_work(int server_local, int server_ipv4, int server_ipv6)
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
    event.data.fd = server_ipv4;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_ipv4, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    event.data.fd = server_ipv6;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_ipv6, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }

    Client street_samurai;
    street_samurai.sockfd = -1;
    street_samurai.identity = 0;
    memset(street_samurai.data, 0, MAXLEN);
    street_samurai.bytes_read = 0;
    street_samurai.samurai_bytes_received = 0;
    street_samurai.state = 0;

    Client* connections_ipv4 = calloc(MAXCONNECTIONS, sizeof(Client));
    if (!connections_ipv4)
        ERR("calloc");
    for (int i =0;i<MAXCONNECTIONS;i++)
    {
        clean_data_client(&connections_ipv4[i]);
    }

    while (1)
    {
        if (epoll_wait(epoll_descriptor, &current_event, 1, -1)>0)
        {
            if (current_event.data.fd == server_local)
            {
                handle_local_connection(server_local, epoll_descriptor, &street_samurai);
            }
            else if (current_event.data.fd == street_samurai.sockfd)
            {
                handle_street_samurai(street_samurai.sockfd, &street_samurai, epoll_descriptor);
            }
            else if (current_event.data.fd == server_ipv4)
            {
                handle_tcp_connection(current_event.data.fd, epoll_descriptor, connections_ipv4);
            }
            else if (current_event.data.fd == server_ipv6)
            {
                handle_corporates_connection(server_ipv6, &street_samurai);
            }
            else if (is_ipv4_connection(connections_ipv4, current_event.data.fd))
            {
                handle_fixer(current_event.data.fd, epoll_descriptor, connections_ipv4, &street_samurai);
            }
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
    uint16_t port_ipv4 = atoi(argv[2]);
    int server_ipv4 = bind_tcp_socket(port_ipv4, BACKLOG);
    new_flags = fcntl(server_ipv4, F_GETFL) | O_NONBLOCK;
    fcntl(server_ipv4, F_SETFL, new_flags);
    int server_ipv6 = bind_tcp_ipv6_socket(argv[3], BACKLOG);
    new_flags = fcntl(server_ipv6, F_GETFL) | O_NONBLOCK;
    fcntl(server_ipv6, F_SETFL, new_flags);

    server_work(server_local, server_ipv4, server_ipv6);

    if (close(server_ipv4) < 0)
    {
        ERR("close");
    }
    if (close(server_ipv6)<0)
    {
        ERR("close");
    }
    if (close(server_local)<0)
    {
        ERR("close");
    }
    if (unlink(argv[1])<0)
    {
        ERR("unlink");
    }
}