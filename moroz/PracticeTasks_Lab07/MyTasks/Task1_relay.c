#include "common.h"

#define BACKLOG 3
#define MAXCONNECTIONS 10
#define MAXLEN 256

//USEFUL:  gcc -Wall -g -fsanitize=address Task1_relay.c -o Task1_relay
//         strace ./Task1_relay 8080 /tmp/relay.sock

void usage(char *name) {
    fprintf(stderr, "USAGE: %s <tcp_port> <unix_socket_path>\n", name);
    exit(EXIT_FAILURE);
}

typedef struct Client
{
    int sockfd;
    char buf[MAXLEN];
    int offset;
    int state;
}Client;

typedef struct CommandClient
{
    int sockfd;
    char buf[MAXLEN];
    int offset;
}CommandClient;

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

int find_free_client(Client drones[MAXCONNECTIONS])
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (drones[i].sockfd == -1) return i;
    }
    return -1;
}

int find_client(int client_sockfd, Client drones[MAXCONNECTIONS])
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (drones[i].sockfd == client_sockfd) return i;
    }
    return -1;
}

void clean_drone(int client_socket, int epoll_descriptor, Client drones[MAXCONNECTIONS])
{
    int idx = find_client(client_socket, drones);
    drones[idx].sockfd = -1;
    memset(drones[idx].buf, 0, MAXLEN);
    drones[idx].offset = 0;
    drones[idx].state = 0;

    clean_client(client_socket, epoll_descriptor);
}

void clean_cmd_client(int client_socket, int epoll_descriptor, CommandClient* cmd_client)
{
    cmd_client->sockfd = -1;
    memset(cmd_client->buf, 0, MAXLEN);
    cmd_client->offset = 0;

    clean_client(client_socket, epoll_descriptor);
}

void handle_connection(int epoll_descriptor, int server_sockfd, Client drones[MAXCONNECTIONS])
{
    int client_sockfd = add_new_client(server_sockfd);

    int idx = find_free_client(drones);
    if (idx == -1)
    {
        if (close(client_sockfd)<0)
        {
            ERR("close");
        }
        return;
    }
    drones[idx].sockfd = client_sockfd;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
}

int handle_drone(int client_sockfd, Client drones[MAXCONNECTIONS], int epoll_descriptor)
{
    int idx = find_client(client_sockfd, drones);

    if (drones[idx].offset >= MAXLEN-1)
    {
        printf("Drone [%d] exceeded buffer limit. Purged.\n", idx);
        clean_drone(client_sockfd, epoll_descriptor, drones);
        return 0;
    }

    int bytes_received = bulk_read(client_sockfd, drones[idx].buf+drones[idx].offset,1);
    if (bytes_received<0)
    {
        if (errno == ECONNRESET)
        {
            printf("A drone abruptly dropped the connection.\n");
            clean_drone(client_sockfd, epoll_descriptor, drones);
            return 0;
        }
        ERR("read");
    }
    if (bytes_received == 0)
    {
        printf("Signal from drone [%d] lost in the void...\n", idx);
        clean_drone(client_sockfd, epoll_descriptor, drones);
        return 0;
    }
    if (bytes_received<0)
    {
        ERR("read");
    }
    drones[idx].offset += 1;
    int last_byte = drones[idx].offset-1;
    if (drones[idx].state == 0)
    {
        if (strcmp(drones[idx].buf, "DRONE") == 0)
        {
            drones[idx].state = 1;
            memset(drones[idx].buf,0,MAXLEN);
            drones[idx].offset = 0;
        }
        if (drones[idx].offset == 5)
        {
            printf("Corrupted telemetry. Drone purged.\n");
            clean_drone(client_sockfd, epoll_descriptor, drones);
            return 0;
        }
        return 0;
    }
    if (drones[idx].buf[last_byte] == '\n')
    {
        int id;
        float value;
        if (sscanf(drones[idx].buf, "T %d %f", &id, &value)!=2)
        {
            printf("Corrupted telemetry. Drone purged.\n");
            clean_drone(client_sockfd, epoll_descriptor, drones);
            return 0;
        }
        printf("Drone [%d] reports anomaly level: [%f]\n", id, value);
        memset(drones[idx].buf, 0, MAXLEN);
        drones[idx].offset = 0;
    }


    return 0;
}

void handle_connection_cmd(int local_server_sockfd, int epoll_descriptor, CommandClient* client)
{
    int cmd_client = add_new_client(local_server_sockfd);
    if (client->sockfd!=-1)
    {
        printf("Status Commander is already present\n");
        if (close(cmd_client)<0)
        {
            ERR("close");
        }
        return;
    }
    client->sockfd = cmd_client;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = cmd_client;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, cmd_client, &event) == -1)
    {
        ERR("epoll_ctl: cmd_sockfd");
        exit(EXIT_FAILURE);
    }
}

void handle_cmd(int client_sockfd, int epoll_descriptor, CommandClient* cmd_client, Client drones[MAXCONNECTIONS])
{
    if (cmd_client->offset>=MAXLEN-1)
    {
        printf("Too large message was sent. Command disonnected\n");
        clean_cmd_client(client_sockfd, epoll_descriptor, cmd_client);
        return;
    }

    int bytes_read = bulk_read(client_sockfd, cmd_client->buf+cmd_client->offset, 1);
    if (bytes_read<0)
    {
        if (errno == ECONNRESET)
        {
            printf("The Commander abruptly dropped the connection.\n");
            clean_cmd_client(client_sockfd, epoll_descriptor, cmd_client);
            return;
        }
        ERR("read");
    }
    if (bytes_read == 0)
    {
        printf("Command disonnected\n");
        clean_cmd_client(client_sockfd, epoll_descriptor, cmd_client);
        return;
    }

    cmd_client->offset++;
    int last_byte = cmd_client->offset - 1;
    if (cmd_client->buf[last_byte] == '\n')
    {
        char msg[MAXLEN] = {0};
        if (strcmp(cmd_client->buf, "STATUS\n") == 0)
        {

            int total_drones = 0;
            for (int i=0;i<MAXCONNECTIONS;i++)
            {
                if (drones[i].sockfd!=-1) total_drones++;
            }

            snprintf(msg, MAXLEN, "Active Drones: %d\n", total_drones);

            if (bulk_write(client_sockfd, msg, strlen(msg))<0)
            {
                if (errno == EPIPE)
                {
                    printf("Commander disconnected\n");
                    clean_cmd_client(client_sockfd, epoll_descriptor, cmd_client);
                    return;
                }
                ERR("write");
            }
        }
        else if ((sscanf(cmd_client->buf, "B %25s", msg)) == 1 && strlen(msg)<20)
        {
            char response[2*MAXLEN] = {0};
            snprintf(response, 2*MAXLEN, "CMD: %s\n", msg);
            for (int i=0;i<MAXCONNECTIONS;i++)
            {
                if (drones[i].sockfd!=-1 && drones[i].state == 1)
                {
                    if (bulk_write(drones[i].sockfd, response, strlen(response))<0)
                    {
                        if (errno == EPIPE)
                        {
                            printf("Drone offline\n");
                            clean_drone(drones[i].sockfd, epoll_descriptor, drones);
                        }
                        else
                        {
                            ERR("write");
                        }
                    }
                }
            }
        }
        else
        {
            char* err_msg = "Incorrect command!\n";
            if (bulk_write(client_sockfd, err_msg, strlen(err_msg))<0)
            {
                if (errno == EPIPE)
                {
                    printf("Commander disconnected\n");
                    clean_cmd_client(client_sockfd, epoll_descriptor, cmd_client);
                    return;
                }
                ERR("write");
            }
        }
        memset(cmd_client->buf,0,MAXLEN);
        cmd_client->offset = 0;
        return;
    }
}

void server_work(int server_sockfd, int local_server_sockfd)
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

    event.data.fd =local_server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, local_server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: local_server");
        exit(EXIT_FAILURE);
    }

    Client drones[MAXCONNECTIONS];
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        drones[i].sockfd = -1;
        memset(drones[i].buf, 0, MAXLEN);
        drones[i].offset = 0;
        drones[i].state = 0;
    }

    CommandClient cmd_client;
    cmd_client.offset = 0;
    cmd_client.sockfd = -1;
    memset(cmd_client.buf, 0, MAXLEN);


    while (1)
    {
        if (epoll_wait(epoll_descriptor, &current_event, 1, -1)>0)
        {
            if (current_event.data.fd == server_sockfd)
            {
                handle_connection(epoll_descriptor, server_sockfd, drones);
            }
            else if (current_event.data.fd == local_server_sockfd)
            {
                handle_connection_cmd(local_server_sockfd, epoll_descriptor, &cmd_client);
            }
            else if (current_event.data.fd == cmd_client.sockfd)
            {
                handle_cmd(current_event.data.fd, epoll_descriptor, &cmd_client, drones);
            }
            else
            {
                int ret = handle_drone(current_event.data.fd, drones, epoll_descriptor);
                if (ret<0) break;
            }
        }
    }

    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (drones[i].sockfd != -1)
        {
            clean_drone(drones[i].sockfd, epoll_descriptor, drones);
        }
    }
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, server_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, local_server_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: local_sockfd");
        exit(EXIT_FAILURE);
    }
    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }
}


int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }
    sethandler(SIG_IGN, SIGPIPE);

    int server_sockfd = bind_tcp_ipv6_socket(argv[1], BACKLOG);
    int new_flags = fcntl(server_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(server_sockfd, F_SETFL, new_flags);

    int local_server_sockfd = bind_local_socket(argv[2], BACKLOG);
    new_flags = fcntl(local_server_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(local_server_sockfd, F_SETFL, new_flags);

    server_work(server_sockfd, local_server_sockfd);

    if (close(local_server_sockfd)<0)
    {
        ERR("close");
    }
    if (unlink(argv[2])<0)
    {
        ERR("unlink");
    }
    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}