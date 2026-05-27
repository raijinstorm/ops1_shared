#include "common.h"

#define BACKLOG 3
#define MAXBUF 576
#define MAXADDR 5

void usage(char *name) { fprintf(stderr, "USAGE: %s port\n", name); }

int make_inet_socket(int type)
{
    int sock;
    sock = socket(PF_INET, type, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

int bind_inet_socket(uint16_t port,int type)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_inet_socket(type);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (type == SOCK_STREAM)
    {
        if (listen(socketfd, BACKLOG) < 0)
            ERR("listen");
    }

    return socketfd;
}

typedef struct __attribute__((__packed__))
{
    int32_t chunkNo;
    int32_t last;
    char buf[MAXBUF - 2*sizeof(int32_t)];
}Packet;

typedef struct Connection
{
    int free;
    int32_t chunkNo;
    int32_t last;
    struct sockaddr_in addr;
}Connection;

int find_client(Connection* connections, struct sockaddr_in* addr)
{
    int pos = -1, last_client = -1, empty = -1;
    for (int i=0;i<MAXADDR;i++)
    {
        if (memcmp(&connections[i].addr, addr, sizeof(struct sockaddr_in)) == 0)
        {
            pos = i;
            break;
        }
        if (connections[i].free)
        {
            empty = i;
        }
        if (connections[i].last)
        {
            last_client = i;
        }
    }
    if (pos==-1 && empty!=-1)
    {
        connections[pos].free = 0;
        connections[pos].addr = *addr;
        connections[pos].chunkNo = 0;
        connections[pos].last = 0;
    }
    else if (pos == -1 && last_client!=-1)
    {
        pos = last_client;
        connections[pos].free = 0;
        connections[pos].addr = *addr;
        connections[pos].chunkNo = 0;
        connections[pos].last = 0;
    }
    return pos;
}

void server_work(int server_sock)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buffer[MAXBUF+1];
    Connection connections[MAXADDR];
    for (int i=0;i<MAXADDR;i++)
    {
        connections[i].free = 1;
        memset(&connections[i].addr,0,sizeof(struct sockaddr_in));
        connections[i].chunkNo = 0;
        connections[i].last = 0;
    }
    while (1)
    {
        if (recvfrom(server_sock, buffer, MAXBUF+1, 0, (struct sockaddr*)&addr, &addrlen)<0)
        {
            ERR("recvfrom");
        }
        buffer[MAXBUF] = '\0';
        Packet* packet = (Packet*)buffer;
        int idx = find_client(connections, &addr);
        if (idx == -1)
        {
            memset(buffer, 0, sizeof(buffer));
            continue;
        }
        int32_t chunkNo =  ntohl(packet->chunkNo);
        int32_t lastFlag = ntohl(packet->last);
        if (connections[idx].chunkNo+1 == chunkNo)
        {
            if (lastFlag)
            {
                printf("Last part: %d\n%s\n",chunkNo,packet->buf);
                connections[idx].last = 1;
            }
            else
            {
                printf("Part %d\n%s\n",chunkNo,packet->buf);
            }
            connections[idx].chunkNo++;
        }
        else if (chunkNo>connections[idx].chunkNo+1)
        {
            continue;
        }
        if (sendto(server_sock, buffer, MAXBUF, 0, (struct sockaddr*)&connections[idx].addr, addrlen)<0)
        {
            ERR("sendto");
        }
    }
}


int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }
    uint16_t port = atoi(argv[1]);
    int server_sock = bind_inet_socket(port, SOCK_DGRAM);

    server_work(server_sock);

    if (close(server_sock)<0)
    {
        ERR("close");
    }
    return 0;
}