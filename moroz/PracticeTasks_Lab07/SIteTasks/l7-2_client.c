#include "common.h"
#include <sys/stat.h>
#include <poll.h>

void usage(char *name) { fprintf(stderr, "USAGE: %s address port\n", name); }

int make_udp_socket(void)
{
    int sock;
    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

#define MAXBUF 576
#define MAX_TRIALS 5

typedef struct Packet
{
    int32_t chunkNo;
    int32_t last;
    char data[MAXBUF-2*sizeof(int32_t)];
}Packet __attribute__((__packed__));

int confirmReceive(int sockfd, char buffer[MAXBUF], struct sockaddr_in addr,int currentChunk, size_t payload_len)
{
    if (sendto(sockfd, buffer, payload_len, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))<0)
    {
        ERR("sendto");
    }

    char recieveBuf[MAXBUF];
    struct pollfd pfd;
    pfd.events = POLLIN;
    pfd.fd = sockfd;
    int ret = poll(&pfd, 1, 500);
    if (ret<0)
    {
        ERR("poll");
    }
    if (ret!=0)
    {
        Packet* recPacket;
        socklen_t addrlen = sizeof(struct sockaddr_in);
        while (1)
        {
            int read_received;
            struct sockaddr_in sender_addr;
            if ((read_received = recvfrom(sockfd, recieveBuf, MAXBUF, MSG_DONTWAIT, (struct sockaddr*)&sender_addr, &addrlen))<0)
            {
                if (errno == EAGAIN) return 0;
                ERR("recv");
            }

            if (memcmp(&addr, &sender_addr, sizeof(struct sockaddr_in)) != 0)
            {
                continue;
            }

            if (read_received == 0)
            {
                return 0;
            }
            recPacket = (Packet*)recieveBuf;
            if (ntohl(recPacket->chunkNo) == currentChunk)
            {
                return ret;
            }
        }
    }

    return ret; //returns non-zero value on success
}

void client_work(int sockfd, struct sockaddr_in addr, int filefd, size_t file_size)
{
    char buffer[MAXBUF];
    memset(buffer, 0, MAXBUF);
    size_t bytes_read = 0;
    int offset = 2*sizeof(int32_t);
    size_t size = MAXBUF - offset;
    int currentChunk = 0;
    int total_read = 0;

    while ((bytes_read = bulk_read(filefd, buffer+offset,size)))
    {
        total_read += bytes_read;

        Packet* pckt = (Packet*)buffer;
        pckt->chunkNo = htonl(currentChunk);
        int last = 0;
        if (total_read == file_size)
        {
            last = 1;
        }
        pckt->last = htonl(last);
        int counter = 0;
        int ret = 0;
        while (counter < MAX_TRIALS && ret==0)
        {
            ret = confirmReceive(sockfd, buffer, addr, currentChunk, bytes_read+offset);
            counter++;
        }
        if (counter == MAX_TRIALS)
        {
            printf("Failed to confirm the recieval of packet %d\nExiting...\n", currentChunk);
            if (close(sockfd)<0)
            {
                ERR("close");
            }
            exit(EXIT_FAILURE);
        }
        currentChunk++;
        memset(buffer, 0, MAXBUF);
    }
}

int main(int argc, char** argv)
{
    if (argc!=4)
    {
        usage(argv[0]);
    }
    int sockfd = make_udp_socket();
    char* address = argv[1];
    char* port = argv[2];
    struct sockaddr_in server_address = make_address(address, port);

    int filefd = open(argv[3], O_RDONLY, 0666);
    if (filefd<0)
    {
        ERR("fopen");
    }

    struct stat file_status;
    if (stat(argv[3], &file_status) < 0) {
        return -1;
    }
    size_t file_size = file_status.st_size;
    client_work(sockfd, server_address, filefd, file_size);

    if (TEMP_FAILURE_RETRY(close(sockfd))<0)
    {
        ERR("close");
    }
    if (TEMP_FAILURE_RETRY(close(filefd))<0)
    {
        ERR("fclose");
    }
    return 0;
}
