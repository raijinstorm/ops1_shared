#include "common.h"
#include<time.h>

void usage(char *name) { fprintf(stderr, "USAGE: %s hostname port\n", name); }

#define BUFLEN 16

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig) { do_work = 0; write(STDOUT_FILENO, "FIRED\n", 6);}

void print_city_states(char cityStates[20])
{
    for (int i = 0;i<20;i++)
    {
        char* ownership = "???";
        if (cityStates[i] == 'p')
        {
            ownership = "persians";
        }
        if (cityStates[i] == 'g')
        {
            ownership = "greeks";
        }
        printf("City %d belongs to the %s\n", i+1, ownership);
    }
}


int handle_commands(int client_sockfd, char cityStates[20])
{
    char buf[BUFLEN] = {0};
    if (fgets(buf, sizeof(buf), stdin)!=NULL)
    {
        if (strchr(buf,'\n') == NULL)
        {
            //drains the rest of the line
            int c;
            while ((c=getchar())!='\n' && c!=EOF);
        }
    }
    else
    {
        return -1;
    }

    int one_char_flag = 0;
    if (buf[0]!='\0' && buf[2]=='\0')
    {
        one_char_flag = 1;
    }
    if (one_char_flag)
    {
        char command = buf[0];

        if (command == 'e')
        {
            return -1;
        }
        else if (command == 'o')
        {
            print_city_states(cityStates);
        }
        else
        {
            printf("Invalid command\n");
        }
        return 0;
    }

    char msg[10]={0};
    char command;
    if (sscanf(buf, "%c %9s", &command, msg)!=2)
    {
        printf("Invalid command!\n");
        return 0;
    }
    if (strlen(msg) == 3)
    {
        msg[3] = '\n';
    }

    if (command == 'm')
    {
        // char m_buf[4];
        // memcpy(m_buf, msg, 3);
        // m_buf[3] = '\n';
        if (TEMP_FAILURE_RETRY(bulk_write(client_sockfd, msg, strlen(msg)))<0)
        {
            if (errno == EPIPE)
            {
                printf("No server on the other side!\n");
                return -1;
            }
            ERR("write");
        }
    }
    else if (command == 't')
    {
        if (strlen(msg)>2)
        {
            printf("Invalid city number!\n");
            return 0;
        }
        int city_num = (msg[0]-'0')*10 + (msg[1]-'0');
        if (city_num<0 || city_num>20)
        {
            printf("Invalid city number!\n");
            return 0;
        }

        int rnd = rand()%2;
        char to_send = 'g';
        if (rnd == 1)
        {
            to_send = 'p';
        }

        char notif_buf[4];
        // snprintf(notif_buf, 5, "%c%s", to_send, msg);//snprintf reserves one byte for '\0'

        notif_buf[0] = to_send;
        notif_buf[1] = msg[0];
        notif_buf[2] = msg[1];
        notif_buf[3] = '\n';
        if (TEMP_FAILURE_RETRY(bulk_write(client_sockfd, notif_buf, 4))<0)
        {
            if (errno == EPIPE)
            {
                printf("No server on the other side!\n");
                return -1;
            }
            ERR("write");
        }

        cityStates[city_num-1] = to_send;
    }
    else
    {
        printf("Invalid command\n");
    }

    return 0;
}

int handle_incoming_info(int client_sockfd, char cityStates[20])
{
    char msg[5];
    int bytes_read;
    if ((bytes_read = TEMP_FAILURE_RETRY(bulk_read(client_sockfd, msg, 4)))<0)
    {
        if (errno == ECONNRESET)
        {
            printf("The server abruptly dropped the connection.\n");
            return -1;
        }
        ERR("read");
    }
    if (bytes_read == 0)
    {
        return -1;
    }

    msg[4] = '\0';
    int city_num = (msg[1]-'0')*10 + (msg[2]-'0') - 1;
    char new_owner = msg[0];
    cityStates[city_num] = new_owner;

    return 0;
}

void client_work(int client_sockfd, char cityStates[20])
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    while (do_work)
    {
        if (epoll_pwait(epoll_descriptor, &current_event, 1, -1, &oldmask)>0)
        {
            if (current_event.data.fd == STDIN_FILENO)
            {
                int ret = handle_commands(client_sockfd, cityStates);
                if (ret<0) break;
            }
            else
            {
                int ret = handle_incoming_info(client_sockfd, cityStates);
                if (ret<0) break;
            }
        }
        else
        {
            if (errno == EINTR) continue;
            ERR("epoll_pwait");
        }
    }

    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, client_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: client_sock");
        exit(EXIT_FAILURE);
    }
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, STDIN_FILENO, NULL) == -1)
    {
        perror("epoll_ctl: stdin");
        exit(EXIT_FAILURE);
    }
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

int main(int argc, char** argv)
{
    srand(time(NULL));
    if (argc!=3)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");

    char cityStates[20];
    for (int i=0;i<20;i++)
    {
        cityStates[i] = 'u';
    }
    int client_sockfd = connect_tcp_socket(argv[1], argv[2]);
    client_work(client_sockfd, cityStates);

    print_city_states(cityStates);
    if (close(client_sockfd)<0)
    {
        ERR("close");
    }
}