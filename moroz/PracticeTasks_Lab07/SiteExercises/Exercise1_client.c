#include "common.h"

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s host port\n", name);
    exit(EXIT_FAILURE);
}

typedef struct Packet
{
    char pid_len;
    char pid[8];
}Packet __attribute__((__packed__));

int main(int argc,char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }

    int sockfd = connect_tcp_socket(argv[1], argv[2]);

    Packet pckt;
    memset(pckt.pid, '0', sizeof(char[8]));
    sprintf(pckt.pid, "%d", getpid());
    pckt.pid_len = (char)strlen(pckt.pid);

    bulk_write(sockfd, (char*)&pckt, sizeof(Packet));

    int16_t res;
    bulk_read(sockfd, (char*)&res, sizeof(int16_t));
    res = ntohs(res);
    printf("SUM=%d", res);

    if (TEMP_FAILURE_RETRY(close(sockfd))<0)
    {
        ERR("close");
    }
    return 0;
}