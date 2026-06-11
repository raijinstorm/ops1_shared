#include "common.h"

#include "board_utils.h"

#define BOARD_FILE "board"
#define FIFO_NAME "fifo"
#define STEP_COUNT 500
#define WAIT_N 10

#define PORT 12345

#define EPOLL_MAX_EVENTS 10

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");

    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n, m - board width and height, respectively\n");

    exit(EXIT_FAILURE);
}

typedef struct Synchro
{
    pthread_mutex_t mutex;
}Synchro;

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void clean_client(int client_socket, int epoll_descriptor)
{
    struct epoll_event event;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, client_socket, &event) == -1)
    {
        perror("epoll_ctl: client_sock");
        exit(EXIT_FAILURE);
    }
    if (close(client_socket)<0) ERR("close");
}

void don_pedro_work(char* board, Synchro* synchro, int n, int m)
{
    ms_sleep(WAIT_N*100);

    int fifofd = open(FIFO_NAME, O_RDONLY);
    if (fifofd<0)
    {
        ERR("open");
    }

    int server_sockfd = bind_tcp_socket(PORT, 3);

    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    event.data.fd = fifofd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, fifofd, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }

    int client_sockfd = -1;

    int rand_x = rand()%n;
    int rand_y = rand()%m;
    for (;;)
    {
        if (epoll_wait(epoll_descriptor, &current_event, 1, -1)>0)
        {
            if (current_event.data.fd == fifofd)
            {
                char false_move;
                int bytes_read = read(fifofd, &false_move, 1);
                if (bytes_read<0)
                {
                    ERR("read");
                }
                if (bytes_read == 0)
                {
                    printf("fifo closed!\n");
                    break;
                }
                printf("Direstion %c? Don't try these tricks on me, carramba!\n", false_move);

                pthread_mutex_lock(&synchro->mutex);
                if (has_trail(board, rand_x, rand_y, n, m)>0)
                {
                    set_char(board, rand_x, rand_y, n, m, ' ');
                }
                else
                {
                    set_char(board, rand_x, rand_y, n, m, '.');
                    printf("Carramba!\n");
                }
                char move = get_trail_move(board, rand_x, rand_y, n, m);
                move_pos(board, move, n, m, &rand_x, &rand_y);
                pthread_mutex_unlock(&synchro->mutex);
            }
            else if (current_event.data.fd == server_sockfd)
            {
                if (client_sockfd != -1)
                {
                    int dummy_socket = add_new_client(server_sockfd);
                    if (close(dummy_socket)<0)
                    {
                        ERR("close");
                    }
                    continue;
                }

                client_sockfd = add_new_client(server_sockfd);

                printf("Headquarters connected -- over!\n");
                event.events = EPOLLIN;
                event.data.fd = client_sockfd;
                if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
                {
                    perror("epoll_ctl: server_sockfd");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                char c;
                int bytes_read = read(client_sockfd, &c, 1);
                if (bytes_read<0)
                {
                    ERR("read");
                }
                if (bytes_read == 0)
                {
                    clean_client(client_sockfd, epoll_descriptor);
                    client_sockfd = -1;
                    continue;
                }

                if (c=='W' || c=='A' || c=='S' || c=='D')
                {
                    printf("Message %c -- accepted!\n", c);
                    pthread_mutex_lock(&synchro->mutex);
                    set_char(board, rand_x, rand_y, n, m, '*');
                    move_pos(board, c, n, m, &rand_x, &rand_y);
                    pthread_mutex_unlock(&synchro->mutex);
                }
            }
        }
    }

    if (close(fifofd)<0)
    {
        ERR("close");
    }
    int board_size = m*(n+1)*sizeof(char);
    int synchro_size = sizeof(Synchro);
    if (msync(board, board_size, MS_SYNC)<0)
    {
        ERR("msync");
    }
    if (munmap(board, board_size)<0)
    {
        ERR("munmap");
    }
    if (munmap(synchro, synchro_size)<0)
    {
        ERR("munmap");
    }
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }
    srand(time(NULL));

    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGPIPE handler");

    int n = atoi(argv[1]);
    int m = atoi(argv[2]);

    int board_size = m*(n+1)*sizeof(char);
    int filefd = open(BOARD_FILE, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (filefd<0)
    {
        ERR("open");
    }
    if (ftruncate(filefd, board_size)<0)
    {
        ERR("ftruncate");
    }
    char* board = (char*)mmap(NULL, board_size, PROT_READ | PROT_WRITE, MAP_SHARED, filefd, 0);
    if (board==MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(filefd)<0)
    {
        ERR("close");
    }

    int synchro_size = sizeof(Synchro);
    Synchro* synchro = (Synchro*)mmap(NULL, synchro_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (synchro == MAP_FAILED)
    {
        ERR("mmap");
    }

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(&synchro->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    fill_board(board, n, m);

    unlink(FIFO_NAME);
    if (mkfifo(FIFO_NAME, 0666)<0)
    {
        ERR("create fifo");
    }

    pid_t don_pedro_process = fork();
    if (don_pedro_process<0)
    {
        ERR("fork");
    }
    if (don_pedro_process == 0)
    {
        don_pedro_work(board, synchro, n, m);

        exit(EXIT_SUCCESS);
    }

    int fifofd = open(FIFO_NAME, O_WRONLY);
    if (fifofd<0)
    {
        ERR("open");
    }

    int rand_x = rand()%n;
    int rand_y = rand()%m;
    for (int i=0;i<STEP_COUNT;i++)
    {
        pthread_mutex_lock(&synchro->mutex);
        char move = get_random_move(board, rand_x, rand_y, n, m);
        set_char(board, rand_x, rand_y, n, m, '=');
        move_pos(board, move, n, m, &rand_x, &rand_y);
        set_char(board, rand_x, rand_y, n, m, 'S');
        pthread_mutex_unlock(&synchro->mutex);

        char false_move = get_random_move(board, rand_x, rand_y, n, m);
        if (write(fifofd, &false_move, 1)<0)
        {
            if (errno != EPIPE)
            {
                ERR("write");
            }
            //
        }

        ms_sleep(100);
    }
    if (close(fifofd)<0)
    {
        ERR("close");
    }

    printf("Smok-Expedition completed!\n");

    while (wait(NULL)>0);

    pthread_mutex_destroy(&synchro->mutex);
    if (msync(board, board_size, MS_SYNC)<0)
    {
        ERR("msync");
    }
    if (munmap(board, board_size)<0)
    {
        ERR("munmap");
    }
    if (munmap(synchro, synchro_size)<0)
    {
        ERR("munmap");
    }
    unlink(FIFO_NAME);
    return 0;
}

