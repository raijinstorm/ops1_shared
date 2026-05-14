#include "common.h"

#define BACKLOG 3
#define MAXLEN 255

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

typedef struct __attribute__((__packed__))
{
    char length;
    char data[MAXLEN];
}Packet;

typedef struct Client
{
    struct sockaddr_in addr;
    int sockfd;
    int bytes_received;
    uint16_t maiden_port;
    Packet packet;
    int offset;
}Client;

int add_new_client_with_address(int sfd, struct sockaddr_in* addr)
{
    int nfd;
    socklen_t size = sizeof(struct sockaddr_in);
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, (struct sockaddr*)addr, &size))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
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

void clean_witch(int client_socket, int epoll_descriptor, Client* witch)
{
    clean_client(client_socket, epoll_descriptor);
    witch->bytes_received = 0;
    witch->maiden_port = -1;
    witch->sockfd = -1;
    memset(&witch->addr, 0, sizeof(struct sockaddr_in));
    witch->offset = 0;
    memset(witch->packet.data,0,MAXLEN);
    witch->packet.length = 0;
}

void handle_connection(int epoll_descriptor, int server_socketfd, Client* maiden, Client* candidate)
{
    struct sockaddr_in addr;
    int client_sockfd = add_new_client_with_address(server_socketfd, &addr);
    if ((maiden->sockfd!=-1 && maiden->bytes_received<3) || (candidate->sockfd!=-1))
    {
        if (close(client_sockfd)<0)
        {
            ERR("close");
        }
        return;
    }
    if (maiden->sockfd == -1)
    {
        maiden->sockfd = client_sockfd;
        maiden->addr = addr;
    }
    else
    {
        candidate->sockfd=client_sockfd;
        candidate->addr = addr;
    }

    // printf("Client connected\n");
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
}

int reading_check_maiden(int bytes_read, Client* maiden, int client_sockfd, int epoll_descriptor)
{
    if (bytes_read<0)
    {
        ERR("read");
    }
    if (bytes_read == 0)
    {
        if (maiden->bytes_received<3)
        {
            printf("No! The ritual...\n");
            clean_witch(client_sockfd, epoll_descriptor, maiden);
            return -2;
        }
        printf("The maiden witch left the coven, we are hopeless\n");
        clean_witch(client_sockfd, epoll_descriptor, maiden);
        return -2;
    }
    return 0;
}

int reading_check_candidate(int bytes_read, Client* candidate, int client_sockfd, int epoll_descriptor)
{
    if (bytes_read<0)
    {
        ERR("read");
    }
    if (bytes_read == 0)
    {
        if (candidate->bytes_received<3)
        {
            printf("Another young one lost to the shadows\n");
            clean_witch(client_sockfd, epoll_descriptor, candidate);
            return -2;
        }
        printf("Another young one lost to the shadows\n");
        clean_witch(client_sockfd, epoll_descriptor, candidate);
        return -2;
    }
    return 0;
}


int handle_maiden(int client_sockfd, int epoll_descriptor, Client* maiden, Client* mother)
{
    if (maiden->bytes_received>=3)
    {
        char dummy;
        int ret = bulk_read(client_sockfd, &dummy, 1);
        if (ret == 0)
        {
            printf("The maiden left the coven, we are hopeless\n");
            clean_witch(client_sockfd, epoll_descriptor, maiden);
            return -1;
        }
        if (ret<0) ERR("read");
        return 0;
    }
    if (maiden->bytes_received==0)
    {
        char c;
        int bytes_read = TEMP_FAILURE_RETRY(bulk_read(client_sockfd, &c, 1));
        int ret = reading_check_maiden(bytes_read, maiden, client_sockfd, epoll_descriptor);
        if (ret==-2){
            return -1;
        }
        if (ret == -1)
            return 0;

        maiden->bytes_received++;
        maiden->packet.length = c;
    }
    else
    {
        int bytes_read = TEMP_FAILURE_RETRY(read(client_sockfd, maiden->packet.data+maiden->offset, maiden->packet.length-maiden->offset));
        int ret = reading_check_maiden(bytes_read, maiden, client_sockfd, epoll_descriptor);
        if (ret==-2){
            return -1;
        }
        if (ret == -1)
            return 0;

        maiden->bytes_received+=bytes_read;
        maiden->offset+=bytes_read;
    }


    if (maiden->bytes_received == 3)
    {
        printf("The ritual has started!\n");
        uint16_t maiden_port;
        memcpy(&maiden_port, maiden->packet.data, sizeof(uint16_t));
        maiden->maiden_port = ntohs(maiden_port);

        maiden->addr.sin_port = htons(maiden->maiden_port);

        int mother_sockfd = make_tcp_socket();
        if (connect(mother_sockfd, (struct sockaddr *)&(maiden->addr), sizeof(struct sockaddr_in)) < 0)
        {
            ERR("connect");
        }
        mother->sockfd = mother_sockfd;
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = mother_sockfd;
        if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, mother_sockfd, &event) == -1)
        {
            perror("epoll_ctl: mother_sockfd");
            exit(EXIT_FAILURE);
        }

        char msg[5] = {0};
        msg[0] = 4;
        if (bulk_write(maiden->sockfd, msg, sizeof(msg))<0)
        {
            if (errno == EPIPE)
            {
                printf("The maiden left the coven, we are hopeless\n");
                clean_witch(client_sockfd, epoll_descriptor, maiden);
                return -1;
            }
            ERR("write");
        }
    }
    return 0;
}

int handle_mother(int mother_sockfd, int epoll_descriptor, Client* mother)
{
    if (mother->bytes_received==0)
    {
        char c;
        int bytes_read = TEMP_FAILURE_RETRY(bulk_read(mother_sockfd, &c, 1));
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
        {
            clean_witch(mother_sockfd, epoll_descriptor, mother);
            printf("The mother left the coven, we are hopeless\n");
            return -1;
        }

        mother->bytes_received++;
        mother->packet.length = c;
    }
    else
    {
        int bytes_received = read(mother_sockfd, mother->packet.data+mother->offset, mother->packet.length-mother->offset);
        if (bytes_received<0)
        {
            ERR("read");
        }
        if (bytes_received == 0)
        {
            clean_witch(mother_sockfd, epoll_descriptor, mother);
            printf("The mother left the coven, we are hopeless\n");
            return -1;
        }
        mother->bytes_received+=bytes_received;
        mother->offset+=bytes_received;

        if (mother->offset == mother->packet.length && mother->packet.length == 4)
        {
            int32_t res;
            memcpy(&res, mother->packet.data, sizeof(int32_t));
            res = ntohl(res);
            printf("%d\n", res);
        }
        if(mother->offset == mother->packet.length && mother->packet.length > 6)
        {
            printf("%s\n",mother->packet.data);
        }
        if (mother->offset == mother->packet.length)
        {
            memset(mother->packet.data, 0, MAXLEN);
            mother->bytes_received = 0;
            mother->offset = 0;
        }
    }

    return 0;
}

void replace_maiden_with_candidate(Client* candidate, Client* maiden, int epoll_descriptor)
{
    clean_client(maiden->sockfd, epoll_descriptor);
    memcpy(maiden, candidate, sizeof(Client));
    candidate->bytes_received = 0;
    candidate->maiden_port = -1;
    candidate->sockfd = -1;
    memset(&candidate->addr, 0, sizeof(struct sockaddr_in));
    candidate->offset = 0;
    memset(candidate->packet.data,0,MAXLEN);
    candidate->packet.length = 0;
}

int handle_candidate(int candidate_sockfd, int epoll_descriptor, Client* candidate, Client* maiden)
{
    if (candidate->bytes_received>=3)
    {
        char dummy;
        int ret = bulk_read(candidate_sockfd, &dummy, 1);
        if (ret == 0)
        {
            clean_witch(candidate_sockfd, epoll_descriptor, candidate);
            return -1;
        }
        if (ret<0) ERR("read");
        return 0;
    }
    if (candidate->bytes_received==0)
    {
        char c;
        int bytes_read = TEMP_FAILURE_RETRY(bulk_read(candidate_sockfd, &c, 1));
        int ret = reading_check_candidate(bytes_read, candidate, candidate_sockfd, epoll_descriptor);
        if (ret==-2){
            return -1;
        }
        if (ret == -1)
            return 0;

        candidate->bytes_received++;
        candidate->packet.length = c;
    }
    else
    {
        int bytes_read = TEMP_FAILURE_RETRY(read(candidate_sockfd, candidate->packet.data+candidate->offset, candidate->packet.length-candidate->offset));
        int ret = reading_check_candidate(bytes_read, candidate, candidate_sockfd, epoll_descriptor);
        if (ret==-2){
            return -1;
        }
        if (ret == -1)
            return 0;

        candidate->bytes_received+=bytes_read;
        candidate->offset+=bytes_read;
    }

    if (candidate->bytes_received == 3)
    {
        uint16_t candidate_port;
        memcpy(&candidate_port, candidate->packet.data, sizeof(uint16_t));
        candidate->maiden_port = candidate_port;

        uint32_t candidate_address = candidate->addr.sin_addr.s_addr;

        Packet pckt = {0};
        pckt.length = 6;
        memcpy(pckt.data, &candidate_address, sizeof(uint32_t));
        memcpy(pckt.data+sizeof(uint32_t), &candidate_port, sizeof(uint16_t));

        ssize_t sent = 0;
        if ((sent = bulk_write(maiden->sockfd, (char*)&pckt, 7))<0)
        {
            if (errno == EPIPE)
            {
                printf("The maiden left the coven, we are hopeless\n");
                clean_witch(maiden->sockfd, epoll_descriptor, maiden);
                return -1;
            }
            ERR("write");
        }
        printf("%ld \n", sent);

        replace_maiden_with_candidate(candidate, maiden, epoll_descriptor);
        char msg[5] = {0};
        msg[0] = 4;
        if (bulk_write(maiden->sockfd, msg, sizeof(msg))<0)
        {
            if (errno == EPIPE)
            {
                printf("The maiden left the coven, we are hopeless\n");
                clean_witch(maiden->sockfd, epoll_descriptor, maiden);
                return -1;
            }
            ERR("write");
        }
    }
    return 0;
}

void server_work(int server_sockfd)
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

    Client maiden;
    maiden.bytes_received = 0;
    maiden.maiden_port = -1;
    maiden.sockfd = -1;
    memset(&maiden.addr, 0, sizeof(struct sockaddr_in));
    maiden.offset = 0;
    memset(maiden.packet.data,0,MAXLEN);
    maiden.packet.length = 0;

    Client mother;
    mother.bytes_received = 0;
    mother.maiden_port = -1;
    mother.sockfd = -1;
    memset(&mother.addr, 0, sizeof(struct sockaddr_in));
    mother.offset = 0;
    memset(mother.packet.data,0,MAXLEN);
    mother.packet.length = 0;

    Client candidate;
    candidate.bytes_received = 0;
    candidate.maiden_port = -1;
    candidate.sockfd = -1;
    memset(&candidate.addr, 0, sizeof(struct sockaddr_in));
    candidate.offset = 0;
    memset(candidate.packet.data,0,MAXLEN);
    candidate.packet.length = 0;

    while (1)
    {
        if (epoll_wait(epoll_descriptor, &current_event, 1, -1)>0)
        {
            if (current_event.data.fd == server_sockfd)
            {
                handle_connection(epoll_descriptor, server_sockfd, &maiden, &candidate);
            }
            else if (current_event.data.fd == maiden.sockfd)
            {
                int ret = handle_maiden(maiden.sockfd, epoll_descriptor, &maiden, &mother);
                if (ret == -1)
                    break;
            }
            else if (current_event.data.fd == mother.sockfd)
            {
                int ret = handle_mother(current_event.data.fd,epoll_descriptor, &mother);
                if (ret == -1)
                    break;
            }
            else
            {
                int ret = handle_candidate(current_event.data.fd, epoll_descriptor, &candidate, &maiden);
                if (ret == -1) break;
            }
        }
    }

    if (mother.sockfd!=-1)
    {
        clean_witch(mother.sockfd, epoll_descriptor, &mother);
    }
    if (maiden.sockfd!=-1)
    {
        clean_witch(maiden.sockfd, epoll_descriptor, &maiden);
    }
    if (candidate.sockfd!=-1)
    {
        clean_witch(candidate.sockfd, epoll_descriptor, &candidate);
    }
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, server_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }
    sethandler(SIG_IGN, SIGPIPE);
    uint16_t port = atoi(argv[1]);

    int server_sockfd = bind_tcp_socket(port, BACKLOG);
    int new_flags = fcntl(server_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(server_sockfd, F_SETFL, new_flags);

    server_work(server_sockfd);

    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}