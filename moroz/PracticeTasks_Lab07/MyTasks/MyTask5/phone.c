#include "common.h"

#define BACKLOG 3
#define MAXCONNECTIONS 16
#define MAXLEN 65536

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig) { do_work = 0; }

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s <local port> <IP> <port>\n", name);
    exit(EXIT_FAILURE);
}

typedef struct Client
{
    int sockfd;
}Client;

typedef struct Dispatch
{
    int is_blackout;
    int sockfd;
    int state;
    uint16_t length;
    int bytes_received;
    char data[MAXLEN];
}Dispatch;

int find_free_client(Client clients[MAXCONNECTIONS])
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (clients[i].sockfd == -1) return i;
    }
    return -1;
}

int find_client_index(int client_sockfd, Client clients[MAXCONNECTIONS])
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (clients[i].sockfd == client_sockfd) return i;
    }
    return -1;
}

void handle_connection(int server_sockfd, Client clients[MAXCONNECTIONS], int epoll_descriptor)
{
    int client_socket = add_new_client(server_sockfd);
    int new_flags = fcntl(client_socket, F_GETFL) | O_NONBLOCK;
    fcntl(client_socket, F_SETFL, new_flags);
    int idx = find_free_client(clients);
    if (idx == -1)
    {
        printf("No space available for a new client!\n");
        if (close(client_socket)<0)
        {
            ERR("close");
        }
        return;
    }
    clients[idx].sockfd = client_socket;
    printf("New local newsroom connected: [%d]\n", client_socket);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_socket;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_socket, &event) == -1)
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
        printf("%d\n", client_socket);
        perror("epoll_ctl: client_sock");
        exit(EXIT_FAILURE);
    }
    if (close(client_socket)<0) ERR("close");
}

void handle_client(int client_sockfd, Client clients[MAXCONNECTIONS], int epoll_descriptor)
{
    char c;
    int bytes_read = bulk_read(client_sockfd, &c, 1);
    if (bytes_read == 0)
    {
        clean_client(client_sockfd, epoll_descriptor);
        int idx = find_client_index(client_sockfd, clients);
        clients[idx].sockfd = -1;
        return;
    }
}

void handle_dispatch(int dispatch_client, Client clients[MAXCONNECTIONS], int epoll_descriptor, Dispatch* dispatch)
{
    if (dispatch->state==0)
    {
        char c;
        int bytes_read = bulk_read(dispatch_client, &c, 1);
        if (bytes_read == 0)
        {
            clean_client(dispatch_client, epoll_descriptor);
            return;
        }

        if ((unsigned char)c==0x01)
        {
            dispatch->state = 1;
        }
        if ((unsigned char)c==0xFF)
        {
            dispatch->is_blackout = 1;
        }
        if ((unsigned char)c==0x00)
        {
            dispatch->is_blackout = 0;
        }
    }
    else if (dispatch->state == 1)
    {
        int bytes_read = read(dispatch_client, dispatch->data+dispatch->bytes_received, 2-dispatch->bytes_received);
        if (bytes_read == 0)
        {
            clean_client(dispatch_client, epoll_descriptor);
            return;
        }
        dispatch->bytes_received+=bytes_read;

        if (dispatch->bytes_received == 2)
        {
            uint16_t len;
            memcpy(&len, dispatch->data, sizeof(uint16_t));
            dispatch->length = ntohs(len);

            memset(dispatch->data, 0, MAXLEN);
            dispatch->bytes_received = 0;
            dispatch->state = 2;
        }
        return;
    }
    else if (dispatch->state == 2)
    {
        int bytes_read = read(dispatch_client, dispatch->data+dispatch->bytes_received, dispatch->length-dispatch->bytes_received);
        if (bytes_read == 0)
        {
            clean_client(dispatch_client, epoll_descriptor);
            return;
        }
        dispatch->bytes_received+=bytes_read;

        if (dispatch->bytes_received == dispatch->length)
        {
            if (dispatch->is_blackout)
            {
                memset(dispatch->data, 0, MAXLEN);
                dispatch->bytes_received = 0;
                dispatch->length = 0;
                dispatch->state = 0;
                return;
            }
            for (int i=0;i<MAXCONNECTIONS;i++)
            {
                if (clients[i].sockfd!=-1)
                {
                    if (write(clients[i].sockfd, dispatch->data, dispatch->length)<0)
                    {
                        if (errno == EPIPE)
                        {
                            clean_client(clients[i].sockfd, epoll_descriptor);
                            clients[i].sockfd = -1;
                            continue;
                        }
                        ERR("write");
                    }
                }
            }

            memset(dispatch->data, 0, MAXLEN);
            dispatch->bytes_received = 0;
            dispatch->length = 0;
            dispatch->state = 0;
        }
        return;
    }

}

void server_work(int server_socket, int server_client)
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = server_socket;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_socket, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }
    event.data.fd = server_client;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_client, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    Client clients[MAXCONNECTIONS];
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        clients[i].sockfd = -1;
    }

    Dispatch dispatch;
    dispatch.sockfd = server_client;
    memset(dispatch.data,0,MAXLEN);
    dispatch.length = 0;
    dispatch.state = 0;

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, &oldmask);
    while (do_work)
    {
        if (epoll_pwait(epoll_descriptor, &current_event, 1, -1, &oldmask)>0)
        {
            if (current_event.data.fd == server_socket)
            {
                handle_connection(server_socket, clients, epoll_descriptor);
            }
            else if (current_event.data.fd == server_client)
            {
                handle_dispatch(server_client, clients, epoll_descriptor, &dispatch);
            }
            else
            {
                handle_client(current_event.data.fd, clients, epoll_descriptor);
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

    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (clients[i].sockfd!=-1)
        {
            clean_client(clients[i].sockfd, epoll_descriptor);
        }
    }
    clean_client(dispatch.sockfd, epoll_descriptor);
    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }
}

int main(int argc, char** argv)
{
    if (argc!=4)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");

    uint16_t local_port = atoi(argv[1]);
    int server_socket = bind_tcp_socket(local_port, BACKLOG);
    int new_flags = fcntl(server_socket, F_GETFL) | O_NONBLOCK;
    fcntl(server_socket, F_SETFL, new_flags);
    int server_client = connect_tcp_socket(argv[2], argv[3]);
    new_flags = fcntl(server_client, F_GETFL) | O_NONBLOCK;
    fcntl(server_client, F_SETFL, new_flags);

    server_work(server_socket, server_client);

    printf("Station going offline\n");
    if (close(server_socket)<0)
    {
        ERR("close");
    }
    return 0;
}