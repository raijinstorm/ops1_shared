#include "common.h"

#define BACKLOG 3
#define MAXCLIENTS 4
#define CITIESNUM 20

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig) { do_work = 0; }

typedef struct Client
{
    int sockfd;
}Client;

int find_free_client(Client clients[MAXCLIENTS])
{
    for (int i=0;i<MAXCLIENTS;i++)
    {
        if (clients[i].sockfd ==-1)
        {
            return i;
        }
    }
    return -1;
}

int find_client(Client clients[MAXCLIENTS], int client_sockfd)
{
    for (int i=0;i<MAXCLIENTS;i++)
    {
        if (clients[i].sockfd == client_sockfd) return i;
    }
    return -1;
}

void clean_client(int client_socket, int epoll_descriptor, Client clients[MAXCLIENTS])
{
    struct epoll_event event;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, client_socket, &event) == -1)
    {
        perror("epoll_ctl: client_sock");
        exit(EXIT_FAILURE);
    }

    int idx = find_client(clients, client_socket);
    if (idx == -1) return;
    clients[idx].sockfd = -1;
    if (close(client_socket)<0) ERR("close");
}

void handle_connection(int epoll_descriptor, int server_socketfd, Client clients[MAXCLIENTS])
{
    int client_sockfd = add_new_client(server_socketfd);
    int next_idx = find_free_client(clients);
    if (next_idx == -1)
    {
        if (close(client_sockfd)<0)
        {
            ERR("close");
        }
        return;
    }
    clients[next_idx].sockfd = client_sockfd;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
}

void handle_drone(int client_sockfd, int epoll_descriptor, Client clients[MAXCLIENTS], char cityStates[CITIESNUM])
{
    char buf[5]={0};
    int bytes_received = TEMP_FAILURE_RETRY(bulk_read(client_sockfd, buf, sizeof(buf)-1));
    if (bytes_received == 0)
    {
        clean_client(client_sockfd, epoll_descriptor, clients);
        return;
    }
    buf[4] = '\0';

    if (buf[3]!='\n') //if in incorrect format
    {
        clean_client(client_sockfd, epoll_descriptor, clients);
        return;
    }

    if (buf[0] == 'p' || buf[0]=='g')
    {
        int city_num = (buf[1]-'0')*10 + (buf[2]-'0') - 1;
        if (city_num<0 || city_num>19)
        {
            clean_client(client_sockfd, epoll_descriptor, clients);
            return;
        }
        if (cityStates[city_num] == buf[0]) return;

        cityStates[city_num] = buf[0];

        int curr_client_idx = find_client(clients, client_sockfd);
        for (int i=0;i<MAXCLIENTS;i++)
        {
            if (clients[i].sockfd == -1) continue;
            if (i == curr_client_idx) continue;
            if (TEMP_FAILURE_RETRY(bulk_write(clients[i].sockfd, buf, sizeof(buf)-1))<0)
            {
                if (errno == EPIPE)
                {
                    clean_client(clients[i].sockfd, epoll_descriptor, clients);
                }
                ERR("write");
            }
        }
    }

    printf("%s", buf);
}

void server_work(int server_sockfd, char cityStates[CITIESNUM])
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }

    Client clients[MAXCLIENTS];
    for (int i=0;i<MAXCLIENTS;i++)
    {
        clients[i].sockfd = -1;
    }

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    while (do_work)
    {
        if (epoll_pwait(epoll_descriptor, &current_event, 1, -1, &oldmask)>0)
        {
            if (current_event.data.fd == server_sockfd)
            {
                handle_connection(epoll_descriptor, server_sockfd, clients);
            }
            else
            {
                handle_drone(current_event.data.fd, epoll_descriptor, clients, cityStates);
            }
        }
        else
        {
            if (errno == EINTR) continue;
            ERR("epoll_pwait");
        }
    }

    for (int i=0;i<MAXCLIENTS;i++)
    {
        if (clients[i].sockfd!=-1)
        {
            clean_client(clients[i].sockfd, epoll_descriptor, clients);
        }
    }
    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }
    sethandler(SIG_IGN, SIGPIPE);
    sethandler(sigint_handler, SIGINT);

    uint16_t port = atoi(argv[1]);
    int server_sockfd = bind_tcp_socket(port, BACKLOG);
    int new_flags = fcntl(server_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(server_sockfd, F_SETFL, new_flags);

    char cityStates[CITIESNUM];
    for (int i=0;i<CITIESNUM;i++)
    {
        cityStates[i] = 'g';
    }
    server_work(server_sockfd, cityStates);

    for (int i=0;i<CITIESNUM;i++)
    {
        printf("City %d belongs to the %s\n", i+1, cityStates[i]=='g'?"greeks":"persians");
    }

    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}