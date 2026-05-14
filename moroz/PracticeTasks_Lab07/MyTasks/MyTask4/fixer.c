#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(int argc, char *argv[]) {
    if (argc != 4) { fprintf(stderr, "Usage: %s <ipv4> <port> <\"message\">\n", argv[0]); return 1; }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    // Send 2-byte header (Network Byte Order) + Body
    uint16_t msg_len = strlen(argv[3]);
    uint16_t net_len = htons(msg_len);

    write(sockfd, &net_len, 2);
    write(sockfd, argv[3], msg_len);

    printf("[Fixer] Intel transmitted to Oracle: '%s'\n", argv[3]);
    close(sockfd);
    return 0;
}