#include "l7-common.h"

void usage(char *name)
{
    printf("%s <timeout>\n", name);
    printf("  timeout - max waiting time after receiving the last message/connection (in seconds)\n");
    exit(EXIT_FAILURE);
}

#define SWAP(a, b)                      \
do                                  \
{                                   \
__typeof__(a) __a = (a);        \
__typeof__(b) __b = (b);        \
__typeof__(*__a) __tmp = *__a;  \
*__a = *__b;                    \
*__b = __tmp;                   \
} while (0)

#define MAX_CLIENTS 10
#define MAX_PAIRS 3
#define UNIX_SK_NAME "Laurenty"
#define MAX_MSG_LEN 63
#define BACKLOG 3

typedef struct Client
{
    int sockfd;
    int state;
    int bytes_read;
    char client_name[MAX_MSG_LEN];
    char beloved_name[MAX_MSG_LEN];
    char message[MAX_MSG_LEN];
}Client;

void handle_local_connection(int server_local, int epoll_descriptor)
{
    int client_sockfd = add_new_client(server_local);
    int new_flags = fcntl(client_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(server_local, F_SETFL, new_flags);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
}

int find_free_client(Client clients[MAX_CLIENTS])
{
    for (int i=0;i<MAX_CLIENTS;i++)
    {
        if (clients[i].sockfd == -1) return i;
    }
    return -1;
}

int find_client(Client clients[MAX_CLIENTS], int client_sockfd)
{
    for (int i=0;i<MAX_CLIENTS;i++)
    {
        if (clients[i].sockfd == client_sockfd) return i;
    }
    return -1;
}

void handle_connection(int epoll_descriptor, int server_socketfd, Client clients[MAX_CLIENTS])
{
    int client_sockfd = add_new_client(server_socketfd);
    int idx = find_free_client(clients);
    if (idx<0)
    {
        if (close(client_sockfd)<0)
        {
            ERR("close");
        }
        return;
    }
    clients[idx].sockfd = client_sockfd;

    printf("Another young person ([%d]) needs my help!\n", client_sockfd);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
}

void clean_client_data(int client_socket, Client clients[MAX_CLIENTS])
{
    int idx = find_client(clients, client_socket);
    if (idx<0)
    {
        printf("NO CLIENT\n");
        return;
    }
    clients[idx].sockfd = -1;
    clients[idx].bytes_read = 0;
    memset(clients[idx].beloved_name,0,MAX_MSG_LEN);
    memset(clients[idx].message, 0, MAX_MSG_LEN);
    memset(clients[idx].client_name, 0, MAX_MSG_LEN);
    clients[idx].state = 0;
}

void clean_client(int client_socket, int epoll_descriptor, Client clients[MAX_CLIENTS])
{
    clean_client_data(client_socket, clients);

    struct epoll_event event;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, client_socket, &event) == -1)
    {
        perror("epoll_ctl: client_sock");
        exit(EXIT_FAILURE);
    }
    if (close(client_socket)<0) ERR("close");
}

void handle_client(int client_sockfd, Client clients[MAX_CLIENTS], int epoll_descriptor)
{
    int idx = find_client(clients, client_sockfd);
    if (idx<0)
    {
        printf("NO CLIENT\n");
        return;
    }
    int bytes_read = read(client_sockfd, clients[idx].message+clients[idx].bytes_read,MAX_MSG_LEN-clients[idx].bytes_read-1);
    clients[idx].message[bytes_read] = '\0';
    if (bytes_read==0)
    {
        if (clients[idx].state == 0)
        {
            printf("I lost contact with ??\n");
        }
        else
        {
            printf("I lost contact with %s\n", clients[idx].client_name);
        }
        clean_client(client_sockfd, epoll_descriptor, clients);

        return;
    }
    if (bytes_read<0)
    {
        ERR("read");
    }
    clients[idx].bytes_read+=bytes_read;

    char* newline;
    while ((newline = strchr(clients[idx].message, '\n'))!=NULL)
    {
        *newline = '\0';
        if (clients[idx].state == 0)
        {
            strcpy(clients[idx].client_name, clients[idx].message);
            clients[idx].state = 1;
            memmove(clients[idx].message, clients[idx].message+strlen(clients[idx].client_name)+1, clients[idx].bytes_read-strlen(clients[idx].client_name)-1);
            clients[idx].bytes_read = clients[idx].bytes_read-strlen(clients[idx].client_name)-1;
            clients[idx].message[clients[idx].bytes_read] = '\0';
        }
        else if (clients[idx].state == 1)
        {
            strcpy(clients[idx].beloved_name, clients[idx].message);
            clients[idx].state = 2;
            memmove(clients[idx].message, clients[idx].message+strlen(clients[idx].beloved_name)+1, clients[idx].bytes_read-strlen(clients[idx].beloved_name)-1);
            clients[idx].bytes_read = clients[idx].bytes_read-strlen(clients[idx].beloved_name)-1;
            clients[idx].message[clients[idx].bytes_read] = '\0';

            for (int i=0;i<MAX_CLIENTS;i++)
            {
                if (clients[i].sockfd!=-1)
                {
                    if (strcmp(clients[i].client_name, clients[idx].beloved_name) == 0 && strcmp(clients[i].beloved_name, clients[idx].client_name)==0)
                    {
                        printf("%s and %s got married!\n", clients[idx].client_name,  clients[idx].beloved_name);
                        char msg[3*MAX_MSG_LEN];
                        snprintf(msg,3*MAX_MSG_LEN, "Congratulations, %s and %s!\n", clients[idx].client_name, clients[idx].beloved_name);
                        if (write(clients[idx].sockfd, msg, strlen(msg))<0)
                        {
                            if (errno == EPIPE)
                            {
                                clean_client(clients[idx].sockfd, epoll_descriptor, clients);
                                break;
                            }
                            ERR("write");
                        }
                        if (write(clients[i].sockfd, msg, strlen(msg))<0)
                        {
                            if (errno == EPIPE)
                            {
                                clean_client(clients[i].sockfd, epoll_descriptor, clients);
                                break;
                            }
                            ERR("write");
                        }
                        // clean_client(clients[idx].sockfd, epoll_descriptor, clients);
                        // clean_client(clients[i].sockfd, epoll_descriptor, clients);
                        break;
                    }
                }
            }
        }

        else if (clients[idx].state == 2)
        {
            for (int i=0;i<MAX_CLIENTS;i++)
            {
                if (clients[i].sockfd!=-1)
                {
                    if (strcmp(clients[i].client_name, clients[idx].beloved_name) == 0 && strcmp(clients[i].beloved_name, clients[idx].client_name)==0)
                    {
                        if (write(clients[i].sockfd, clients[idx].message, strlen(clients[idx].message))<0)
                        {
                            if (errno == EPIPE)
                            {
                                clean_client(clients[i].sockfd, epoll_descriptor, clients);
                                break;
                            }
                            ERR("write");
                        }
                    }
                }
            }
            int new_len = clients[idx].bytes_read-strlen(clients[idx].message)-1;
            memmove(clients[idx].message, newline+1, clients[idx].bytes_read-strlen(clients[idx].message)-1);
            clients[idx].bytes_read = new_len;
            clients[idx].message[new_len] = '\0';
        }
    }

    // char* newline = strchr(clients[idx].message+strlen(clients[idx].client_name), '\n');
    // if (clients[idx].state == 0)
    // {
    //     if (newline!=NULL)
    //     {
    //         memcpy(clients[idx].client_name, clients[idx].message, (newline - clients[idx].message));
    //         clients[idx].state = 1;
    //     }
    // }
    // else if (clients[idx].state == 1)
    // {
    //     if (newline!=NULL)
    //     {
    //         memcpy(clients[idx].beloved_name, clients[idx].message+strlen(clients[idx].client_name)+1, clients[idx].bytes_read-strlen(clients[idx].client_name)-2);
    //         clients[idx].state = 2;
    //     }
    // }

    if (clients[idx].state == 2)
    {
        printf("%s wants to marry %s\n", clients[idx].client_name, clients[idx].beloved_name);
        //clean_client(client_sockfd, epoll_descriptor, clients);
    }

}

void handle_input(Client clients[MAX_CLIENTS], int epoll_descriptor)
{
    char buf[MAX_MSG_LEN];
    fgets(buf, MAX_MSG_LEN, stdin);

    char* addressee = buf;
    char* message = strchr(buf,':');
    if (!message)
    {
        printf("Incorrect format!\n");
        return;
    }
    *message = '\0';
    message++;

    for (int i=0;i<MAX_CLIENTS;i++)
    {
        if (strcmp(clients[i].client_name, addressee)==0)
        {
            if (write(clients[i].sockfd, message, strlen(message))<0)
            {
                if (errno == EPIPE)
                {
                    clean_client(clients[i].sockfd, epoll_descriptor, clients);
                    return;
                }
                ERR("write");
            }
            return;
        }
    }
    printf("Incorrect addressee!\n");
}

void server_work(int server_local, int timeout)
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
    event.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    Client clients[MAX_CLIENTS];
    for (int i=0;i<MAX_CLIENTS;i++)
    {
        clients[i].sockfd = -1;
        clients[i].state = 0;
        clients[i].bytes_read = 0;
        memset(clients[i].beloved_name,0,MAX_MSG_LEN);
        memset(clients[i].beloved_name, 0, MAX_MSG_LEN);
        memset(clients[i].message, 0, MAX_MSG_LEN);
    }

    while (1)
    {
        int ret;
        if ((ret=epoll_wait(epoll_descriptor, &current_event, 1, timeout*1000))>0)
        {
            if (current_event.data.fd == server_local)
            {
                handle_connection(epoll_descriptor, server_local, clients);
            }
            else if (current_event.data.fd == STDIN_FILENO)
            {
                handle_input(clients, epoll_descriptor);
            }
            else
            {
                handle_client(current_event.data.fd, clients, epoll_descriptor);
            }
        }
        else if (ret == 0)
        {
            printf("No one needs my help anymore!\n");
            if (close(epoll_descriptor)<0)
            {
                ERR("close");
            }
            return;
        }
        else
        {
            ERR("epoll_wait");
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    int timeout = atoi(argv[1]);
    if (timeout < 1)
    {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    sethandler(SIG_IGN, SIGPIPE);

    int server_local = bind_local_socket(UNIX_SK_NAME, BACKLOG);
    int new_flags = fcntl(server_local, F_GETFL) | O_NONBLOCK;
    fcntl(server_local, F_SETFL, new_flags);

    server_work(server_local, timeout);

    if (close(server_local)<0)
    {
        ERR("close");
    }
    if (unlink(UNIX_SK_NAME)<0)
    {
        ERR("unlink");
    }
    return EXIT_SUCCESS;
}