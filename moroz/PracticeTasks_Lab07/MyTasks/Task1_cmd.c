#include "common.h"

void usage(char *name) {
    fprintf(stderr, "USAGE: %s socket_path\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
    if (argc != 2) usage(argv[0]);
    if (sethandler(SIG_IGN, SIGPIPE)) ERR("Setting SIGPIPE:");

    // Connect to the local UNIX socket
    int sockfd = connect_local_socket(argv[1]);
    printf("[*] Commander Terminal uplink established.\n");
    printf("[*] Available commands: 'STATUS', 'B <message>'\n");

    int epoll_fd;
    if ((epoll_fd = epoll_create1(0)) < 0) ERR("epoll_create1");
    
    struct epoll_event event, events[2];
    
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1) ERR("epoll_ctl stdin");
    
    event.data.fd = sockfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &event) == -1) ERR("epoll_ctl sockfd");

    char buf[256];
    
    while(1) {
        int n = epoll_wait(epoll_fd, events, 2, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            ERR("epoll_wait");
        }
        
        for(int i = 0; i < n; i++) {
            if(events[i].data.fd == STDIN_FILENO) {
                // Send command to Relay
                if(fgets(buf, sizeof(buf), stdin) == NULL) {
                    close(sockfd);
                    return 0;
                }
                if(TEMP_FAILURE_RETRY(bulk_write(sockfd, buf, strlen(buf))) < 0) ERR("write");
            } else if(events[i].data.fd == sockfd) {
                // Read response from Relay
                int r = TEMP_FAILURE_RETRY(read(sockfd, buf, sizeof(buf)-1));
                if(r == 0) {
                    printf("[!] Relay Core shut down.\n");
                    return 0;
                }
                if(r < 0) ERR("read");
                buf[r] = '\0';
                printf("Relay: %s", buf);
            }
        }
    }
    return 0;
}