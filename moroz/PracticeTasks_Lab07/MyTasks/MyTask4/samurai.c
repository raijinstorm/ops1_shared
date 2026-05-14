#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr, "Usage: %s <socket_path>\n", argv[0]); return 1; }

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, argv[1], sizeof(addr.sun_path) - 1);

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    // Authenticate
    uint32_t payload = htonl(0xDEADBEEF);
    write(sockfd, &payload, sizeof(uint32_t));

    printf("[Samurai] Jacked in. Listening for routed intel...\n");

    // Listen for incoming routed strings
    char buffer[1024];
    while (1) {
        int bytes = read(sockfd, buffer, sizeof(buffer) - 1);
        if (bytes <= 0) {
            printf("[Samurai] Connection dropped by Oracle.\n");
            break;
        }
        buffer[bytes] = '\0';
        printf("[Samurai HUD]: %s\n", buffer);
    }
    close(sockfd);
    return 0;
}