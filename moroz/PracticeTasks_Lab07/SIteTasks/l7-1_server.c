#include "common.h"

#define BACKLOG 3
#define MAX_EVENTS 16

volatile sig_atomic_t shutdown_requested = 0;
void sig_handler(int sig)
{
    shutdown_requested = 1;
}

void usage(char *name) { fprintf(stderr, "USAGE: %s socket port\n", name); }

void calculate(int32_t data[5])
{
    int first, second, operation, result=-1, status=1;
    first = ntohl(data[0]);
    second = ntohl(data[1]);
    switch ((char)ntohl(data[3]))
    {
    case '+':
        result = first+second;
        break;
    case '-':
        result = first-second;
        break;
    case '*':
        result = first*second;
        break;
    case '/':
        if (second!=0)
        {
            result = first/second;
        }
        else
        {
            status = 0;
        }
        break;
    default:
        status = 0;
    }
    data[2] = htonl(result);
    data[4] = htonl(status);
}

void server_work(int local_socketfd, int tcp_socketfd)
{
    int epoll_fd;
    if ((epoll_fd = epoll_create1(0))<0)
    {
        ERR("epoll_create1");
    }
    struct epoll_event event, events[MAX_EVENTS];
    event.data.fd = local_socketfd;
    event.events = EPOLLIN;
    printf("DEBUG: epoll_fd=%d, socket_fd=%d\n", epoll_fd, local_socketfd);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_socketfd, &event)<0)
    {
        perror("epoll_ctl: listen_socket");
        exit(EXIT_FAILURE);
    }
    event.data.fd = tcp_socketfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_socketfd, &event)<0)
    {
        perror("epoll_ctl: accept_socket");
        exit(EXIT_FAILURE);
    }
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    int num_fds;
    int32_t data[5];
    ssize_t size;

    while (!shutdown_requested)
    {
        if ((num_fds = epoll_pwait(epoll_fd, events, MAX_EVENTS, -1, &oldmask))<0)
        {
            if (errno == EINTR) continue;
            ERR("epoll_wait");
        }
        for (int i=0;i<num_fds;i++)
        {
            int client_socketfd = add_new_client(events[i].data.fd);
            if ((size=bulk_read(client_socketfd, (char*)data, sizeof(int32_t[5]))) < 0)
            {
                ERR("read");
            }

            if (size == sizeof(int32_t[5])){
                calculate(data);
                if (bulk_write(client_socketfd, (char*)data, sizeof(int32_t[5]))<0 && errno!=EPIPE)
                {
                    ERR("write");
                }
            }
            if (TEMP_FAILURE_RETRY(close(client_socketfd))<0)
            {
                ERR("close");
            }
        }
    }

    if (TEMP_FAILURE_RETRY(close(epoll_fd))<0)
    {
        ERR("close");
    }
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }
    sethandler(SIG_IGN, SIGPIPE);
    sethandler(sig_handler, SIGINT);

    int local_socketfd = bind_local_socket(argv[1], BACKLOG);
    int flags = fcntl(local_socketfd, F_GETFL) | O_NONBLOCK;
    fcntl(local_socketfd, F_SETFL, flags);
    uint16_t port = atoi(argv[2]);
    int tcp_socketfd = bind_tcp_socket(port, BACKLOG);
    flags = fcntl(tcp_socketfd, F_GETFL) | O_NONBLOCK;
    fcntl(tcp_socketfd, F_SETFL, flags);

    server_work(local_socketfd, tcp_socketfd);

    if (close(local_socketfd)<0)
    {
        ERR("close");
    }
    if (unlink(argv[1])<0 && errno!=ENOENT)
    {
        ERR("unlink");
    }
    if (close(tcp_socketfd)<0)
    {
        ERR("close");
    }
    printf("Server ended work\n");
    return 0;
}