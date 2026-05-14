#include "common.h"

void usage(char *name) { fprintf(stderr, "USAGE: %s socket port\n", name); }

int main(int argc, char** argv)
{
    if (argc!=5)
    {
        usage(argv[0]);
    }
    sethandler(SIG_IGN, SIGPIPE);

    char* filename = argv[1];
    int op1 = atoi(argv[2]);
    int op2 = atoi(argv[3]);
    int32_t operator = (int32_t)argv[4][0];

    int sockfd = connect_local_socket(filename);
    int32_t data[5] = {htonl(op1), htonl(op2), htonl(0), htonl(operator), htonl(0)};
    if (bulk_write(sockfd, (char*)data,sizeof(int32_t[5]))<0)
    {
        if (errno==EPIPE)
        {
            fprintf(stderr, "No server on the other side!\n");
        }
        ERR("write");
    }
    if (bulk_read(sockfd, (char*)data, sizeof(int32_t[5]))<(size_t)sizeof(int32_t[5]))
    {
        ERR("read");
    }

    if (!ntohl(data[4]))
    {
        printf("Invalid operation\n");
    }
    else
    {
        printf("%d %c %d = %d\n",ntohl(data[0]),(char)ntohl(data[3]), ntohl(data[1]), (int32_t)ntohl(data[2]));
    }

    if (TEMP_FAILURE_RETRY(close(sockfd))<0)
    {
        ERR("close");
    }
    return 0;
}

