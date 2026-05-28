#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
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


/*
 * given a string like "METRICS=[X:12, Y:45, Z:90]"
 * it is convenietnt to use complex delimiter like
 * char *token = strtok_r(str, "=[], ", &saveptr);
 * then we will be able to get tokens "METRICS", "X:12", "Y:45", "Z:90"
 */

/*
 *function that does not skip consecutive delimiters:  strsep
 */

/*
 *sometimes it is useful to use sscanf but it is not convenient most of the time
 *
 *   char string[] = "REQ;CRIT;SHIP12";
     char part1[10], part2[10], part3[10];

     // Note the semicolons outside the brackets to consume the actual delimiter!
     int items_read = sscanf(string, "%9[^;];%9[^;];%9[^;]", part1, part2, part3);
 */

int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

int bind_inet_socket(uint16_t port, int type, int backlog)
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
        if (listen(socketfd, backlog) < 0)
            ERR("listen");
    return socketfd;
}

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

#define BACKLOG 3
#define BUFLEN 512
#define NODE_ID_LEN 5
#define STATUS_CODE_MAXLEN 4
#define LOG_LEN 128

typedef struct Message
{
    char buf[BUFLEN+1];
}Message;

typedef struct __attribute__((__packed__)){
    char node_id[NODE_ID_LEN+1];
    char status_code[STATUS_CODE_MAXLEN+1];
    int32_t depth;
    float temperature;
    int32_t batt;
    int32_t tilt;
    char log[LOG_LEN+1];
}Datagram;

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    int port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM, BACKLOG);

    while (1)
    {
        Message message = {0};
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(struct sockaddr);
        int bytes_received = recvfrom(server_sockfd, message.buf, BUFLEN, 0, (struct sockaddr*)&addr, &addrlen);
        if (bytes_received<0)
        {
            ERR("recvfrom");
        }
        message.buf[bytes_received] = '\0';

        Datagram datagram = {0};
        char* outer_saveptr = NULL;
        char* inner_saveptr = NULL;
        char* token = strtok_r(message.buf, "|", &outer_saveptr);
        int message_part = 0;
        while (token)
        {
            if (message_part == 0)
            {
                strcpy(datagram.node_id, token);
            }
            else if (message_part == 1)
            {
                if (strcmp(token, "OK") != 0 && strcmp(token,"WARN")!=0 && strcmp(token,"ERR")!=0 && strcmp(token,"FAIL")!=0)
                {
                    printf("Incorrect status code\n");
                    break;
                }
                strcpy(datagram.status_code, token);
            }
            else if (message_part == 2)
            {
                char* inner_token = strtok_r(token, ",", &inner_saveptr);
                int exit_flag = 0;
                while (inner_token)
                {
                    char* number = strchr(inner_token,':');
                    if (!number)
                    {
                        printf("Incorrect metrics: No number provided\n");
                        exit_flag = 1;
                        break;
                    }
                    *number = '\0';
                    number+=1;
                    if (strcmp(inner_token, "DEPTH") == 0)
                    {
                        datagram.depth = atoi(number);
                    }
                    else if (strcmp(inner_token, "TEMP") == 0)
                    {
                        datagram.temperature = atof(number);
                    }
                    else if (strcmp(inner_token, "BATT") == 0)
                    {
                        datagram.batt = atoi(number);
                    }
                    else if (strcmp(inner_token, "TILT") == 0)
                    {
                        datagram.tilt = atoi(number);
                    }
                    else
                    {
                        printf("Incorrect metrics code %s\n", inner_token);
                        exit_flag = 1;
                        break;
                    }

                    inner_token = strtok_r(NULL,",",&inner_saveptr);
                }

                if (exit_flag)
                {
                    break;
                }

                if (outer_saveptr == NULL)
                {
                    datagram.log[0] = '\0';
                }
                else
                {
                    size_t loglen = strlen(outer_saveptr);
                    memcpy(datagram.log,outer_saveptr, loglen);
                    datagram.log[loglen] = '\0';
                }
                message_part = 3;
                break;
            }

            message_part++;
            token = strtok_r(NULL,"|", &outer_saveptr);
        }

        if (message_part!=3)
        {
            char ip_str[64] = {0};
            inet_ntop(addr.sin_family, &addr.sin_addr, ip_str, sizeof(ip_str));
            printf("[Error] Malformed datagram from %s:%d\n", ip_str, ntohs(addr.sin_port));
            continue;
        }
        printf("[Telemetry] Node %s (Status: %s) reported:\n - DEPTH = %d\n - TEMP = %f\n - BATT = %d\nLog: %s\n", datagram.node_id,datagram.status_code,datagram.depth,datagram.temperature,datagram.batt,datagram.log);
    }

    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}
