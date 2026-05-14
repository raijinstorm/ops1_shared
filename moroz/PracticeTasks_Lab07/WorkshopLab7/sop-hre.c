#include "w7-common.h"
#include <pthread.h>

#define BACKLOG 3
#define MAX_EVENTS 16
#define MAXBUF 256
#define MAXELECTORS 7

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig) { do_work = 0; }

typedef struct Elector
{
    int index;
    int sockfd;
    int vote;
}Elector;

typedef struct Results
{
    int res1;
    int res2;
    int res3;
    pthread_mutex_t mtx;
}Results;

typedef struct udp_args
{
    Results* results;
    pthread_t tid;
    char* port;
}udp_args_t;

char* States[] = {"Mainz", "Trier", "Cologne", "Bohemia", "Palantiante", "Saxony", "Brandenburg"};

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int make_udp_socket()
{
    int sock;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
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


int find_elector(int curr_fd, Elector electors[MAXELECTORS])
{
    for (int i=0;i<MAXELECTORS;i++)
    {
        if (electors[i].sockfd == curr_fd)
        {
            return electors[i].index;
        }
    }

    return -1;
}

void clean_elector(int client_socket, int epoll_descriptor, Elector electors[MAXELECTORS])
{
    int index = find_elector(client_socket, electors);
    clean_client(client_socket, epoll_descriptor);

    if (index!=-1)
    {
        electors[index].sockfd = -1;
        electors[index].vote = 0;
        electors[index].index = -1;
    }
}

void handle_drone(int curr_fd, int epoll_descriptor, Elector electors[MAXELECTORS], Results* results)
{
    char c;
    int bytes_read;
    bytes_read = TEMP_FAILURE_RETRY(bulk_read(curr_fd, &c, 1));
    if (bytes_read == 0)
    {
        clean_elector(curr_fd, epoll_descriptor, electors);
        return;
    }

    int curr_index = find_elector(curr_fd, electors);

    printf("%c", c);

    if (curr_index!=-1)
    {
        if (c<'1' || c>'3') return;

        int new_vote = c-'1'+1;
        int old_vote = electors[curr_index].vote;
        electors[curr_index].vote = new_vote;
        pthread_mutex_lock(&results->mtx);
        if (old_vote == 1) results->res1--;
        if (old_vote == 2) results->res2--;
        if (old_vote == 3) results->res3--;
        if (new_vote == 1)
        {
            results->res1++;
        }
        if (new_vote == 2)
        {
            results->res2++;
        }
        if (new_vote == 3)
        {
            results->res3++;
        }
        pthread_mutex_unlock(&results->mtx);
        return;
    }

    if (c < '1' || c > '7')
    {
        clean_elector(curr_fd, epoll_descriptor, electors);
        return;
    }
    int client_num = c-'1';

    if (electors[client_num].sockfd!=-1 && electors[client_num].sockfd!=curr_fd)
    {
        clean_elector(curr_fd, epoll_descriptor, electors);
        return;
    }

    electors[client_num].sockfd = curr_fd;
    electors[client_num].index = client_num;

    char response_buf[64];
    snprintf(response_buf, sizeof(response_buf), "Welcome, elector of %s!\n", States[client_num]);
    if (bulk_write(curr_fd, response_buf, strlen(response_buf)) < 0)
    {
        if (errno == EPIPE)
        {
            clean_elector(curr_fd, epoll_descriptor, electors);
        }
        else
        {
            ERR("write");
        }
    }

}


void handle_connection(int epoll_descriptor, int server_socketfd)
{
    int client_sockfd = add_new_client(server_socketfd);

    printf("Client connected\n");
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
}

void server_work(int server_socketfd, Results* results)
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = server_socketfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_socketfd, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    Elector electors[MAXELECTORS];
    for (int i=0;i<MAXELECTORS;i++)
    {
        electors[i].sockfd = -1;
        electors[i].vote = 0;
        electors[i].index = -1;
    }

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, &oldmask);
    while (do_work)
    {
        if (epoll_pwait(epoll_descriptor, &current_event, 1,-1,&oldmask)>0)
        {
            if (current_event.data.fd == server_socketfd)
            {
                handle_connection(epoll_descriptor, server_socketfd);
            }

            else
            {
                handle_drone(current_event.data.fd, epoll_descriptor, electors, results);
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

    for (int i=0;i<MAXELECTORS;i++)
    {
        if (electors[i].sockfd!=-1)
        {
            clean_elector(electors[i].sockfd, epoll_descriptor, electors);
        }
    }

    if (TEMP_FAILURE_RETRY(close(epoll_descriptor)) < 0)
        ERR("close");
    if (pthread_sigmask(SIG_UNBLOCK, &oldmask, NULL))
        ERR("SIG_BLOCK error");
}

void* udp_client(void* args)
{
    udp_args_t* udp_args = (udp_args_t*)args;
    int udp_sockfd = make_udp_socket();
    struct sockaddr_in addr = make_address("localhost", udp_args->port);

    while (do_work)
    {
        pthread_mutex_lock(&udp_args->results->mtx);
        int32_t res_to_send[3] = {htonl(udp_args->results->res1), htonl(udp_args->results->res2), htonl(udp_args->results->res3)};
        if (TEMP_FAILURE_RETRY(sendto(udp_sockfd, res_to_send, sizeof(int32_t[3]), 0,(struct sockaddr*)&addr, sizeof(addr)))<0)
        {
            ERR("sendto");
        }
        pthread_mutex_unlock(&udp_args->results->mtx);

        sleep(1);
    }
    return NULL;
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    int server_socketfd = bind_tcp_socket(atoi(argv[1]), BACKLOG);
    int new_flags = fcntl(server_socketfd, F_GETFL) | O_NONBLOCK;
    fcntl(server_socketfd, F_SETFL, new_flags);

    Results results;
    results.res1 =0;
    results.res2 = 0;
    results.res3 = 0;
    pthread_mutex_init(&results.mtx, NULL);

    udp_args_t udp_args;
    udp_args.results = &results;
    udp_args.port = argv[2];

    pthread_create(&udp_args.tid, NULL, udp_client, &udp_args);

    server_work(server_socketfd, &results);

    pthread_join(udp_args.tid, NULL);
    pthread_mutex_destroy(&results.mtx);
    if (close(server_socketfd)<0)
    {
        ERR("close");
    }

    printf("Candidate 1: %d\nCandidate 2: %d\nCandidate 3: %d\n", results.res1, results.res2, results.res3);

    return 0;
}
