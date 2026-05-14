#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(int argc, char *argv[]) {
    if (argc != 3) { fprintf(stderr, "Usage: %s <ipv6> <port>\n", argv[0]); return 1; }

    int sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(atoi(argv[2]));
    inet_pton(AF_INET6, argv[1], &addr.sin6_addr);

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    uint32_t net_bytes;
    int r = read(sockfd, &net_bytes, 4);
    if (r == 4) {
        printf("[Corporate] Data audit complete. Total bytes routed by Oracle: %u\n", ntohl(net_bytes));
    } else {
        printf("[Corporate] Failed to retrieve data audit.\n");
    }
    
    close(sockfd);
    return 0;
}