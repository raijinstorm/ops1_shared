#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
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

#define MAX_EVENTS 16

#define MAX_LINE_LENGTH 4096

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

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


typedef struct NodeConfig
{
    char* node_id;
    int sockfd;
}NodeConfig;


NodeConfig config = {NULL, -1};

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

ssize_t smart_read(int fd, char *buf, size_t count)
{
    int bytes_read;
    bytes_read = TEMP_FAILURE_RETRY(read(fd, buf, count));
    if (bytes_read < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
        ERR("read");
    }
    return bytes_read;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void init_server(int epoll_descriptor)
{
    int sock;
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        ERR("socket");
    int new_flags = fcntl(sock, F_GETFL) | O_NONBLOCK;
    fcntl(sock, F_SETFL, new_flags);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sock;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, sock, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }
    config.sockfd = sock;
}

void bind_tcp_socket(uint16_t port, int backlog_size)
{
    struct sockaddr_in addr;
    int t = 1;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(config.sockfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(config.sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(config.sockfd, backlog_size) < 0)
        ERR("listen");
}

int add_new_client(int sfd)
{
    int nfd;
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
}

int handle_input(int epoll_descriptor, int total_connections, int total_bytes)
{
    static int message_id_response = 0;
    char buffer[MAX_LINE_LENGTH];
    fgets(buffer, sizeof(buffer), stdin);

    cJSON *json = cJSON_Parse(buffer);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            printf("Error: %s\n", error_ptr);
        }
        cJSON_Delete(json);
        return 1;
    }

    cJSON* src = cJSON_GetObjectItem(json, "src");
    cJSON* dst = cJSON_GetObjectItem(json, "dst");
    cJSON *Body = cJSON_GetObjectItemCaseSensitive(json, "body");
    if (cJSON_IsObject(Body))
    {
        cJSON* type = cJSON_GetObjectItem(Body, "type");
        cJSON* message_id = cJSON_GetObjectItem(Body, "msg_id");
        cJSON* node_id = cJSON_GetObjectItem(Body, "node_id");
        config.node_id = strdup(node_id->valuestring);
        if (cJSON_IsString(type) && type->valuestring!=NULL)
        {
            if (strcmp(type->valuestring, "init") == 0)
            {
                init_server(epoll_descriptor);
                cJSON *resJson = cJSON_CreateObject();
                cJSON_AddStringToObject(resJson, "src", config.node_id);
                cJSON_AddStringToObject(resJson, "dst", src->valuestring);
                cJSON* resBody = cJSON_CreateObject();
                cJSON_AddStringToObject(resBody, "type", "init_ok");
                cJSON_AddNumberToObject(resBody, "in_reply_to", message_id->valueint);
                cJSON_AddNumberToObject(resBody, "msg_id", message_id_response++);
                cJSON_AddItemToObject(resJson, "body", resBody);

                char* out = cJSON_PrintUnformatted(resJson);
                printf("%s\n", out);
                cJSON_Delete(resJson);
            }
            if (strcmp(type->valuestring, "tcp_echo_start")==0)
            {
                if (config.sockfd == -1)
                {
                    printf("Server not initialised, perform <init> command first\n");
                    return 1;
                }
                cJSON* _port = cJSON_GetObjectItem(Body, "port");
                uint16_t port = _port->valueint;
                bind_tcp_socket(port, 3); //backlog = 3

                cJSON *resJson = cJSON_CreateObject();
                cJSON_AddStringToObject(resJson, "src", config.node_id);
                cJSON_AddStringToObject(resJson, "dst", src->valuestring);
                cJSON* resBody = cJSON_CreateObject();
                cJSON_AddStringToObject(resBody, "type", "tcp_echo_start_ok");
                cJSON_AddNumberToObject(resBody, "in_reply_to", message_id->valueint);
                cJSON_AddStringToObject(resBody, "status", "listening");
                cJSON_AddNumberToObject(resBody, "port", port);
                cJSON_AddNumberToObject(resBody, "msg_id", message_id_response++);
                cJSON_AddItemToObject(resJson, "body", resBody);

                char* out = cJSON_PrintUnformatted(resJson);
                printf("%s\n", out);
                cJSON_Delete(resJson);
            }
            if (strcmp(type->valuestring, "tcp_echo_stats") == 0)
            {
                if (config.sockfd == -1)
                {
                    printf("Server not initialised, perform <init> command first\n");
                    return 1;
                }

                cJSON *resJson = cJSON_CreateObject();
                cJSON_AddStringToObject(resJson, "src", config.node_id);
                cJSON_AddStringToObject(resJson, "dst", src->valuestring);
                cJSON* resBody = cJSON_CreateObject();
                cJSON_AddStringToObject(resBody, "type", "tcp_echo_stats_ok");
                cJSON_AddNumberToObject(resBody, "in_reply_to", message_id->valueint);
                cJSON_AddNumberToObject(resBody, "total_connections", total_connections);
                cJSON_AddNumberToObject(resBody, "total_bytes", total_bytes);
                cJSON_AddNumberToObject(resBody, "msg_id", message_id_response++);
                cJSON_AddItemToObject(resJson, "body", resBody);

                char* out = cJSON_PrintUnformatted(resJson);
                printf("%s\n", out);
                cJSON_Delete(resJson);
            }
        }
    }

    cJSON_Delete(json);
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

void handle_drone(int client_socket, int* total_bytes, int* total_connections, int epoll_descriptor)
{
    char buffer[MAX_LINE_LENGTH];
    int bytes_read = 0;
    bytes_read = smart_read(client_socket, buffer, sizeof(buffer));
    if (bytes_read==0)
    {
        clean_client(client_socket, epoll_descriptor);
        (*total_connections)-=1;
        return;
    }
    if (bytes_read<0)
    {
        return;
    }
    *total_bytes += bytes_read;
    if (TEMP_FAILURE_RETRY(bulk_write(client_socket, buffer, bytes_read))<0)
    {
        if (errno == EPIPE)
        {
            clean_client(client_socket, epoll_descriptor);
            (*total_connections)-=1;
        }
        else
        {
            ERR("write");
        }
    }
}


int main() {
    char line[MAX_LINE_LENGTH];

    int total_connections = 0, total_bytes=0;
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, events[MAX_EVENTS];
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    sethandler(SIG_IGN, SIGPIPE);

    int num_fds;
    while (1) {

        if ((num_fds = epoll_wait(epoll_descriptor, events, MAX_EVENTS, -1))<0)
        {
            ERR("epoll_wait");
        }
        for (int i=0;i<num_fds;i++)
        {
            if (events[i].data.fd == STDIN_FILENO)
            {
                handle_input(epoll_descriptor, total_connections, total_bytes);
            }
            if (config.sockfd != -1 && events[i].data.fd == config.sockfd)
            {
                int client_socket = add_new_client(config.sockfd);
                int new_flags = fcntl(client_socket, F_GETFL) | O_NONBLOCK;
                fcntl(client_socket, F_SETFL, new_flags);
                event.events = EPOLLIN;
                event.data.fd = client_socket;
                if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_socket, &event) == -1)
                {
                    perror("epoll_ctl: client_sock");
                    exit(EXIT_FAILURE);
                }
                total_connections++;
            }
            if (config.sockfd!=-1 && events[i].data.fd != config.sockfd && events[i].data.fd!=STDIN_FILENO)
            {
                handle_drone(events[i].data.fd,&total_bytes, &total_connections, epoll_descriptor);
            }
        }


    }

    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }

    return 0;
}
