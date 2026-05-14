#include "common.h"

void usage(char *name) {
    fprintf(stderr, "USAGE: %s hostname port\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
    if (argc != 3) usage(argv[0]);

    // Ignore SIGPIPE so the drone doesn't crash if the relay drops it
    if (sethandler(SIG_IGN, SIGPIPE)) ERR("Setting SIGPIPE:");

    // Connect to the Relay Station
    int sockfd = connect_tcp_socket(argv[1], argv[2]);
    printf("[*] Connected to Relay Station.\n");

    // Stage 1: Send the 5 initial bytes
    char initial_msg[5] = "DRONE";
    if (TEMP_FAILURE_RETRY(bulk_write(sockfd, initial_msg, 5)) < 0) ERR("write");
    printf("[*] Initial 5 bytes ('DRONE') sent.\n");

    // Set up epoll for stdin and sockfd
    int epoll_fd;
    if ((epoll_fd = epoll_create1(0)) < 0) ERR("epoll_create1");

    struct epoll_event event, events[2];

    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1) ERR("epoll_ctl stdin");

    event.data.fd = sockfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &event) == -1) ERR("epoll_ctl sockfd");

    printf("[*] Enter telemetry (e.g. 'T 01 +125.5') or press Ctrl+D to exit:\n");
    char buf[256];

    while(1) {
        int n = epoll_wait(epoll_fd, events, 2, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            ERR("epoll_wait");
        }

        for(int i = 0; i < n; i++) {
            if(events[i].data.fd == STDIN_FILENO) {
                // Read from terminal and send to server
                if(fgets(buf, sizeof(buf), stdin) == NULL) {
                    printf("[*] Powering down drone...\n");
                    close(sockfd);
                    return 0;
                }
                if(TEMP_FAILURE_RETRY(bulk_write(sockfd, buf, strlen(buf))) < 0) {
                    if (errno == EPIPE) {
                        printf("[!] Relay Station severed the connection.\n");
                        return 0;
                    }
                    ERR("write");
                }
            } else if(events[i].data.fd == sockfd) {
                // Read broadcast from server and print to terminal
                int r = TEMP_FAILURE_RETRY(read(sockfd, buf, sizeof(buf)-1));
                if(r == 0) {
                    printf("[!] Relay Station disconnected.\n");
                    return 0;
                }
                if(r < 0) ERR("read");
                buf[r] = '\0';
                printf("\n>>> BROADCAST RECEIVED: %s", buf);
            }
        }
    }
    return 0;
}