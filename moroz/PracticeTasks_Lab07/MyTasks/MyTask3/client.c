#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/select.h>

void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <listen_port> <target_host> <target_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint16_t listen_port = atoi(argv[1]);
    const char *target_host = argv[2];
    uint16_t target_port = atoi(argv[3]);

    // 1. Setup listening socket (for Crone or other Witches to connect to)
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(listen_port);

    if (bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) error_exit("bind");
    if (listen(listen_fd, 5) < 0) error_exit("listen");

    printf("[Witch:%d] Listening for incoming connections...\n", listen_port);

    // 2. Connect to the initial target (The Crone)
    int out_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *server = gethostbyname(target_host);
    if (server == NULL) { fprintf(stderr, "No such host\n"); exit(EXIT_FAILURE); }

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    memcpy(&target_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    target_addr.sin_port = htons(target_port);

    printf("[Witch:%d] Connecting to %s:%d...\n", listen_port, target_host, target_port);
    if (connect(out_fd, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0) error_exit("connect");

    // 3. Send initial protocol message: [Header=2] [Body=listen_port]
    uint8_t init_msg[3];
    init_msg[0] = 2;
    uint16_t net_port = htons(listen_port);
    memcpy(&init_msg[1], &net_port, 2);
    send(out_fd, init_msg, 3, 0);
    printf("[Witch:%d] Sent port info. Waiting for instructions...\n", listen_port);

    int in_fd = -1; // To store whoever connects TO us

    fd_set read_fds;
    int max_fd;

    // 4. The Event Loop
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        max_fd = listen_fd;

        if (out_fd != -1) {
            FD_SET(out_fd, &read_fds);
            if (out_fd > max_fd) max_fd = out_fd;
        }
        if (in_fd != -1) {
            FD_SET(in_fd, &read_fds);
            if (in_fd > max_fd) max_fd = in_fd;
        }

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) error_exit("select");

        // EVENT A: Someone is connecting to our listen_port
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            printf("[Witch:%d] Accepted connection from %s:%d\n", listen_port, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            if (in_fd == -1) in_fd = new_fd;
            else close(new_fd); // For this test, we only care about the first incoming connection
        }

        // EVENT B: Data arrived on our outgoing connection (from Crone or previous Maiden)
        if (out_fd != -1 && FD_ISSET(out_fd, &read_fds)) {
            uint8_t header;
            int r = recv(out_fd, &header, 1, 0);

            if (r <= 0) {
                printf("[Witch:%d] Outgoing connection closed by remote host.\n", listen_port);
                close(out_fd);
                out_fd = -1;
                continue;
            }

            if (header == 4) {
                // STAGE 3: Ritual Started
                uint8_t zeroes[4];
                recv(out_fd, zeroes, 4, MSG_WAITALL); // block briefly to grab body
                printf("[Witch:%d] Received [4][0,0,0,0]. Ritual started!\n", listen_port);

                if (in_fd != -1) {
                    printf("[Witch:%d] Firing Stage 3 test messages to Mother socket...\n", listen_port);

                    // Send Integer
                    uint8_t int_msg[5] = {4};
                    uint32_t test_val = htonl(42);
                    memcpy(&int_msg[1], &test_val, 4);
                    send(in_fd, int_msg, 5, 0);

                    // Send String
                    const char *str = "Hello Crone!";
                    uint8_t str_len = strlen(str);
                    uint8_t *str_msg = malloc(1 + str_len);
                    str_msg[0] = str_len;
                    memcpy(&str_msg[1], str, str_len);
                    send(in_fd, str_msg, 1 + str_len, 0);
                    free(str_msg);
                } else {
                    printf("[Witch:%d] Warning: Received ritual start but no incoming connection to send tests to!\n", listen_port);
                }
            }
            else if (header == 6) {
                // STAGE 4: Relocate to new Candidate
                uint8_t info[6];
                recv(out_fd, info, 6, MSG_WAITALL);

                uint32_t new_ip;
                uint16_t new_port;
                memcpy(&new_ip, &info[0], 4);
                memcpy(&new_port, &info[4], 2);

                struct in_addr ip_addr;
                ip_addr.s_addr = new_ip;

                printf("[Witch:%d] Stage 4 Triggered! Relocating to Candidate %s:%d\n", listen_port, inet_ntoa(ip_addr), ntohs(new_port));

                close(out_fd); // Break connection with Crone

                // Connect to new Candidate
                out_fd = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in cand_addr;
                memset(&cand_addr, 0, sizeof(cand_addr));
                cand_addr.sin_family = AF_INET;
                cand_addr.sin_addr.s_addr = new_ip; // Assuming Crone sent it in network byte order
                cand_addr.sin_port = new_port;      // Assuming Crone sent it in network byte order

                if (connect(out_fd, (struct sockaddr *)&cand_addr, sizeof(cand_addr)) == 0) {
                    printf("[Witch:%d] Successfully linked hands with new Candidate!\n", listen_port);
                } else {
                    perror("[Witch] Failed to connect to candidate");
                    out_fd = -1;
                }
            }
        }

        // EVENT C: Incoming data from Mother (We mostly ignore this as a mock, just clear the buffer)
        if (in_fd != -1 && FD_ISSET(in_fd, &read_fds)) {
            uint8_t buf[256];
            int r = recv(in_fd, buf, sizeof(buf), 0);
            if (r <= 0) {
                printf("[Witch:%d] Incoming connection closed.\n", listen_port);
                close(in_fd);
                in_fd = -1;
            }
        }
    }
    return 0;
}