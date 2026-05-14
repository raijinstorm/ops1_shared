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

typedef struct Conn
{
    int free;
    int32_t chunkNo;
    struct sockaddr_in addr;
}Conn;

typedef struct Packet
{
    int32_t chunkNo;
    int32_t last;
    char data[MAXBUF-2*sizeof(int32_t)];
}Packet __attribute__((__packed__));

int find_index(Conn* connections, struct sockaddr_in newAddr)
{
    int pos = -1, last_free_pos=-1;
    for (int i=0;i<MAXADDR;i++)
    {
        if (memcmp(&connections[i].addr, &newAddr, sizeof(struct sockaddr_in)) == 0)
        {
            pos = i;
            return pos;
        }
        if (connections[i].free)
        {
            last_free_pos = i;
        }
    }
    if (last_free_pos!=-1)
    {
        pos = last_free_pos;
        connections[last_free_pos].free = 0;
        connections[last_free_pos].addr = newAddr;
        connections[last_free_pos].chunkNo = 0;
    }
    return pos;
}

void server_work(int sockfd)
{
    char buf[MAXBUF];
    Conn connections[MAXADDR];
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    for (int i=0;i<MAXADDR;i++)
    {
        connections[i].free = 1;
    }
    while (1)
    {
        size_t received_bytes;
        if (TEMP_FAILURE_RETRY((received_bytes = recvfrom(sockfd, buf,MAXBUF-1,0,(struct sockaddr *)&addr,&addrlen))<0))
        {
            ERR("recvfrom");
        }
        int index = find_index(connections, addr);
        if (index>=0)
        {
            buf[received_bytes] = '\0';
            Packet* pckt = (Packet*)buf;
            int32_t receivedChunkNo = ntohl(pckt->chunkNo);
            int32_t last = ntohl(pckt->last);
            if (connections[index].chunkNo == receivedChunkNo)
            {
                if (last)
                {
                    printf("Last Chunk: %d\n%s\n", receivedChunkNo, pckt->data);
                    connections[index].free = 1;
                }
                else
                {
                    printf("Part %d\n%s\n", receivedChunkNo, pckt->data);
                }
                connections[index].chunkNo++;
            }
            if (connections[index].chunkNo<receivedChunkNo)
            {
                continue;
            }
            if (TEMP_FAILURE_RETRY(sendto(sockfd, buf, MAXBUF, 0,(struct sockaddr *)&addr,addrlen))<0)
            {
                if (EPIPE == errno)
                    connections[index].free = 1;
                ERR("sendto");
            }
        }
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
    int sockfd = bind_inet_socket(port, SOCK_DGRAM);

    server_work(sockfd);

    if (close(sockfd)<0)
    {
        ERR("close");
    }
    fprintf(stderr, "Server has terminated.\n");
}