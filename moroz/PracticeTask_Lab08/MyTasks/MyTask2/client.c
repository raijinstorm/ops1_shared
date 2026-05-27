#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>

typedef struct __attribute__((__packed__)) {
    int32_t package_id;
    int32_t sorting_time_ms;
} Packet;

int main(int argc, char** argv) {
    if (argc != 5) {
        printf("Usage: %s <ip> <port> <package_id> <sorting_time>\n", argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    addr.sin_addr.s_addr = inet_addr(argv[1]);

    Packet p;
    p.package_id = htonl(atoi(argv[3]));
    p.sorting_time_ms = htonl(atoi(argv[4]));

    sendto(sock, &p, sizeof(p), 0, (struct sockaddr*)&addr, sizeof(addr));
    
    printf("Threw package %d through the window!\n", atoi(argv[3]));
    return 0;
}