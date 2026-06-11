#include <pthread.h>
#include  <sys/mman.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "./../common.h"

#define UNIX_SOCK_NAME "/tmp/hyperloop.sock"

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)             \
(__extension__({                               \
long int __result;                         \
do                                         \
__result = (long int)(expression);     \
while (__result == -1L && errno == EINTR); \
__result;                                  \
}))
#endif

void usage(char* name)
{
    printf("%s <S> <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

#define BACKLOG 3
#define MESSAGE_LEN 128
#define MAXCONNECTIONS 16
#define THREADNUM 3
#define SYSTEM_NAME_LEN 12
#define UDP_MESSAGE_LEN 16
#define FIFO_NAME "/tmp/dispatch_fifo"

typedef struct Client
{
    int sockfd;
    int bytes_read;
    char buf[MESSAGE_LEN+1];
}Client;

typedef struct Stations
{
    int32_t station_cnt;
    pthread_mutex_t mutex;
    uint32_t capsules[];
}Stations;

typedef struct Capsule
{
    uint32_t id;
    uint32_t jumps_left;
}Capsule;

// defining a node
typedef struct Node {
    Capsule data;
    struct Node* next;
    struct Node* prev;
}Node;

typedef struct DoublyLinkedList
{
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    Node *head;
    Node *tail;
}DoublyLinkedList;

typedef struct ThreadArgs
{
    int* work;
    int idx;
    int next_pipe_writefd;
    pthread_t thread_id;
    DoublyLinkedList* list;
    Stations* stations;
}ThreadArgs;

typedef struct __attribute__((__packed__)){
    char err_code;
    char padding[3];
    char system_name[SYSTEM_NAME_LEN+1];
}Datagram;

volatile sig_atomic_t system_work = 1;

void sigchld_handler(int sig) { system_work = 0; }

int make_udp_socket(int domain)
{
    int sock;
    sock = socket(domain, SOCK_DGRAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

int bind_udp_socket(uint16_t port)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_udp_socket(PF_INET);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    // if (SOCK_STREAM == type)
    //     if (listen(socketfd, BACKLOG) < 0)
    //         ERR("listen");
    return socketfd;
}

int add_item_to_end(DoublyLinkedList *list, Capsule item)
{
    if (list == NULL) return -1;

    Node *newNode = (Node *) malloc(sizeof(Node));
    if (newNode == NULL) return -2;

    newNode->prev = newNode->next = NULL;
    newNode->data = item;

    if (list->tail)
    {
        list->tail->next = newNode;
        newNode->prev = list->tail;
        list->tail = newNode;
    }
    else
    {
        list->head = list->tail = newNode;
    }

    return 0;
}

int delete_item(DoublyLinkedList *list, Node *node)
{
    if (node == NULL) return -1;
    if (list == NULL) return -2;

    if (node->prev)
    {
        node->prev->next = node->next;
    }
    else
    {
        list->head = node->next;
    }

    if (node->next)
    {
        node->next->prev = node->prev;
    }
    else
    {
        list->tail = node->prev;
    }

    free(node);
    return 0;
}

void clean_list(DoublyLinkedList* list)
{
    while (list->head)
    {
        int ret = delete_item(list, list->head);
        if (ret == -1)
        {
            printf("Node is null\n");
            break;
        }
        if (ret == -2)
        {
            printf("Linked list is not initialised");
            break;
        }
    }
}

int find_free_client(Client passenger[MAXCONNECTIONS])
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (passenger[i].sockfd == -1) return i;
    }
    return -1;
}

int find_client(Client clients[MAXCONNECTIONS], int client_sockfd)
{
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        if (clients[i].sockfd == client_sockfd) return i;
    }
    return -1;
}

void clean_client(int client_socket, int epoll_descriptor, Client passengers[MAXCONNECTIONS])
{
    struct epoll_event event;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, client_socket, &event) == -1)
    {
        perror("epoll_ctl: client_sock");
        exit(EXIT_FAILURE);
    }
    if (close(client_socket)<0) ERR("close");

    int passenger_idx = find_client(passengers, client_socket);
    if (passenger_idx<0)
    {
        printf("No such passenger\n");
        return;
    }
    passengers[passenger_idx].sockfd = -1;
    memset(passengers[passenger_idx].buf,0,MESSAGE_LEN+1);
    passengers[passenger_idx].bytes_read = 0;
}

void handle_connection(int server_sockfd, int epoll_descriptor, Client passenger[MAXCONNECTIONS])
{
    int client_sockfd = add_new_client(server_sockfd);
    int idx = find_free_client(passenger);
    if (idx<0)
    {
        printf("NO SPACE\n");
        if (close(client_sockfd)<0)
        {
            ERR("close");
        }
        return;
    }
    passenger[idx].sockfd = client_sockfd;
    int new_flags = fcntl(client_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(client_sockfd, F_SETFL, new_flags);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_sockfd, &event) == -1)
    {
        perror("epoll_ctl: client_sockfd");
        exit(EXIT_FAILURE);
    }
    printf("Somebody wants a ticket...\n");
}

int handle_client(int client_sockfd, int epoll_descriptor, Client passengers[MAXCONNECTIONS])
{
    int idx = find_client(passengers, client_sockfd);
    if (idx<0)
    {
        return 0;
    }
    int bytes_read = read(client_sockfd, passengers[idx].buf+passengers[idx].bytes_read, MESSAGE_LEN-passengers[idx].bytes_read);
    if (bytes_read<0)
    {
        ERR("read");
    }
    if (bytes_read == 0)
    {
        clean_client(client_sockfd, epoll_descriptor, passengers);
        return 0;
    }

    passengers[idx].bytes_read += bytes_read;
    passengers[idx].buf[passengers[idx].bytes_read] = '\0';
    char* newline = strchr(passengers[idx].buf, '\n');
    if (newline == NULL)
    {
        return 0;
    }
    *newline = '\0';
    printf("Passenger %s bought a ticket!\n", passengers[idx].buf);
    clean_client(client_sockfd, epoll_descriptor, passengers);
    return 0;
}

int handle_fifo(Client* fifo_info, int station0_write_fd, int S)
{
    int bytes_read = read(fifo_info->sockfd, fifo_info->buf+fifo_info->bytes_read, MESSAGE_LEN-fifo_info->bytes_read);
    if (bytes_read<0)
    {
        ERR("read");
    }
    if (bytes_read == 0)
    {
        Capsule term_capsule = {0,0};
        if (write(station0_write_fd, &term_capsule, sizeof(Capsule))<0)
        {
            if (errno!=EPIPE)
                ERR("write");
            return -1;
        }
        return -1;
    }

    fifo_info->bytes_read+=bytes_read;
    fifo_info->buf[fifo_info->bytes_read] = '\0';
    char* newline = strchr(fifo_info->buf, '\n');
    if (newline == NULL)
    {
        printf("Incorrect message format, no \\n\n");
        memset(fifo_info->buf, 0, MESSAGE_LEN);
        fifo_info->bytes_read = 0;
        return 0;
    }
    uint32_t id;
    if (sscanf(fifo_info->buf, "LAUNCH %u", &id)!=1)
    {
        printf("Incorrect format?\n");
        memset(fifo_info->buf, 0, MESSAGE_LEN);
        fifo_info->bytes_read = 0;
        return 0;
    }

    uint32_t rand_jumps = rand()%S+1;
    Capsule current_capsule = {id, rand_jumps};
    if (write(station0_write_fd, &current_capsule, sizeof(Capsule))<0)
    {
        if (errno!=EPIPE)
            ERR("write");
        return -1;
    }
    memset(fifo_info->buf, 0, MESSAGE_LEN);
    fifo_info->bytes_read = 0;
    return 0;
}

int handle_udp(int udp_server_sockfd)
{
    char buf[UDP_MESSAGE_LEN+1] = {0};
    struct sockaddr_in addr;
    socklen_t len = sizeof(struct sockaddr_in);
    int bytes_received = recvfrom(udp_server_sockfd, buf, UDP_MESSAGE_LEN, 0, (struct sockaddr*)&addr, &len);
    if (bytes_received<0)
    {
        ERR("recvfrom");
    }
    if (bytes_received != UDP_MESSAGE_LEN)
        return 0;

    buf[bytes_received] = '\0';
    Datagram* message = (Datagram*)buf;
    if (message->err_code == 'F')
    {
        printf("[Dispatcher] Critical failure! Sealing the tunnels!\n");
        return -1;
    }
    printf("[Dispatcher] Received a message with err.code %c and text %s\n", message->err_code, message->system_name);
    return 0;
}

int server_work(int local_server_sockfd, int tcp_server_sockfd, int udp_server_sockfd, int** pipes, pid_t* children_pids, int S)
{
    srand(time(NULL));
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, current_event;
    event.events = EPOLLIN;
    event.data.fd = local_server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, local_server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    event.data.fd = tcp_server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, tcp_server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: tcp_server_sockfd");
        exit(EXIT_FAILURE);
    }
    event.data.fd = udp_server_sockfd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, udp_server_sockfd, &event) == -1)
    {
        perror("epoll_ctl: udp_server_sockfd");
        exit(EXIT_FAILURE);
    }

    unlink(FIFO_NAME);
    if (mkfifo(FIFO_NAME, 0666)<0)
    {
        ERR("create fifo");
    }
    int fifofd = open(FIFO_NAME, O_RDONLY);
    if (fifofd<0)
    {
        ERR("open");
    }
    event.data.fd = fifofd;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, fifofd, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    Client passengers[MAXCONNECTIONS] = {0};
    for (int i=0;i<MAXCONNECTIONS;i++)
    {
        passengers[i].sockfd = -1;
        memset(passengers[i].buf,0,MESSAGE_LEN+1);
        passengers[i].bytes_read = 0;
    }

    Client fifo_info = {fifofd, 0};
    memset(fifo_info.buf, 0, MESSAGE_LEN);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    int flag_kill = 0;
    while (system_work)
    {
        if (epoll_pwait(epoll_descriptor, &current_event, 1, -1, &oldmask)>0)
        {
            if (current_event.data.fd == tcp_server_sockfd || current_event.data.fd == local_server_sockfd)
            {
                handle_connection(current_event.data.fd, epoll_descriptor, passengers);
            }
            else if (current_event.data.fd == fifofd)
            {
                int ret = handle_fifo(&fifo_info, pipes[0][1], S);
                if (ret<0)
                {
                    break;
                }
            }
            else if (current_event.data.fd == udp_server_sockfd)
            {
                int ret = handle_udp(udp_server_sockfd);
                if (ret<0)
                {
                    for (int i=0;i<S;i++)
                    {
                        kill(children_pids[i], SIGKILL);
                    }
                    flag_kill = 1;
                    break;
                }
            }
            else
            {
                int ret = handle_client(current_event.data.fd, epoll_descriptor, passengers);
                if (ret<0)
                {
                    break;
                }
            }
        }
        else
        {
            if (errno == EINTR)
                continue;
            ERR("epoll_wait");
        }
    }

    if (!system_work)
    {
        for (int i=0;i<S;i++)
        {
            kill(children_pids[i], SIGKILL);
        }
        flag_kill = 1;
    }

    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, udp_server_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: udp_server_sockfd");
        exit(EXIT_FAILURE);
    }
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, fifofd, NULL) == -1)
    {
        perror("epoll_ctl: fifofd");
        exit(EXIT_FAILURE);
    }
    unlink(FIFO_NAME);
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, local_server_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: local_server_sockfd");
        exit(EXIT_FAILURE);
    }
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, tcp_server_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: tcp_server_sockfd");
        exit(EXIT_FAILURE);
    }
    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }
    if (flag_kill)
    {
        return -1;
    }
    return 0;
}

void* thread_work(void* args_t)
{
    ThreadArgs* args = (ThreadArgs*)args_t;
    unsigned int tid = args->thread_id;
    while (1)
    {
        pthread_mutex_lock(&args->list->mtx);
        while (args->list->tail == NULL && *args->work)
        {
            pthread_cond_wait(&args->list->not_empty, &args->list->mtx);
        }
        if (args->list->tail == NULL && *args->work==0)
        {
            pthread_mutex_unlock(&args->list->mtx);
            break;
        }
        Capsule current_capsule = args->list->head->data;
        delete_item(args->list, args->list->head);
        // pthread_cond_signal(&args->list->not_full);
        pthread_mutex_unlock(&args->list->mtx);

        if (pthread_mutex_lock(&args->stations->mutex) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&args->stations->mutex);
        }
        args->stations->capsules[args->idx]+=1;
        pthread_mutex_unlock(&args->stations->mutex);

        usleep(200*1000);
        if (pthread_mutex_lock(&args->stations->mutex) == EOWNERDEAD)
        {
            pthread_mutex_consistent(&args->stations->mutex);
        }
        if (rand_r(&tid)%100<2)
        {
            printf("[Station %d] Tube breached! Mutants inside!\n", args->idx);
            abort();
        }

        args->stations->capsules[args->idx]-=1;
        pthread_mutex_unlock(&args->stations->mutex);

        if (current_capsule.jumps_left == 0)
        {
            printf("[Station %d] Capsule %d arrived at destination.\n", args->idx, current_capsule.id);
        }
        else if (current_capsule.jumps_left>0)
        {
            current_capsule.jumps_left-=1;
            if (write(args->next_pipe_writefd, &current_capsule, sizeof(Capsule))<0)
            {
                if (errno == EPIPE)
                {
                    pthread_mutex_lock(&args->list->mtx);
                    *args->work = 0;
                    pthread_cond_broadcast(&args->list->not_empty);
                    pthread_mutex_unlock(&args->list->mtx);
                    break;
                }
                ERR("write");
            }
            printf("[Station %d] Capsule %d forwarded.\n", args->idx, current_capsule.id);
        }
    }

    return NULL;
}

void child_work(int** pipes, int idx, int S, Stations* stations)
{
    int next_idx = (idx+1)%S;
    for (int i=0;i<S;i++)
    {
        if (i==idx)
        {
            if (close(pipes[i][1])<0)
            {
                ERR("close");
            }
        }
        else if (i == next_idx)
        {
            if (close(pipes[i][0])<0)
            {
                ERR("close");
            }
        }
        else
        {
            if (close(pipes[i][1])<0)
            {
                ERR("close");
            }
            if (close(pipes[i][0])<0)
            {
                ERR("close");
            }
        }
    }

    DoublyLinkedList list;
    pthread_mutex_init(&list.mtx, NULL);
    pthread_cond_init(&list.not_empty, NULL);
    list.head = list.tail = NULL;

    int work = 1;
    ThreadArgs thread_args[THREADNUM];
    for (int i=0;i<THREADNUM;i++)
    {
        thread_args[i].list = &list;
        thread_args[i].stations = stations;
        thread_args[i].work = &work;
        thread_args[i].idx = idx;
        thread_args[i].next_pipe_writefd = pipes[next_idx][1];
        pthread_create(&thread_args[i].thread_id, NULL, thread_work, &thread_args[i]);
    }

    while (1)
    {
        Capsule current_capsule;
        // printf("I am again here %d\n", idx);
        int bytes_read = read(pipes[idx][0], &current_capsule, sizeof(Capsule));
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
        {
            work = 0;
            break;
        }
        if (current_capsule.id == 0)
        {
            work = 0;
            break;
        }

        printf("Station[%d] received capsule with id %d. Curr num of jumps left: %d\n", idx, current_capsule.id, current_capsule.jumps_left);
        pthread_mutex_lock(&list.mtx);
        add_item_to_end(&list, current_capsule);
        pthread_cond_signal(&list.not_empty);
        pthread_mutex_unlock(&list.mtx);
    }

    pthread_cond_broadcast(&list.not_empty);
    for (int i=0;i<THREADNUM;i++)
    {
        pthread_join(thread_args[i].thread_id, NULL);
    }
    ////
    pthread_mutex_destroy(&list.mtx);
    pthread_cond_destroy(&list.not_empty);

    clean_list(&list);
    if (close(pipes[idx][0])<0)
    {
        ERR("close");
    }
    if (close(pipes[next_idx][1])<0)
    {
        ERR("close");
    }
    for (int i=0;i<S;i++)
    {
        free(pipes[i]);
    }
    free(pipes);
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }
    sethandler(SIG_IGN, SIGPIPE);
    sethandler(sigchld_handler, SIGCHLD);

    int local_server_sockfd = bind_local_socket(UNIX_SOCK_NAME, BACKLOG);
    int new_flags = fcntl(local_server_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(local_server_sockfd, F_SETFL, new_flags);

    uint16_t port = atoi(argv[2]);
    int tcp_server_sockfd = bind_tcp_socket(port, BACKLOG);
    new_flags = fcntl(tcp_server_sockfd, F_GETFL) | O_NONBLOCK;
    fcntl(tcp_server_sockfd, F_SETFL, new_flags);

    uint16_t udp_port = atoi(argv[2])+1;
    // printf("%u\n", udp_port);
    int udp_server_sockfd = bind_udp_socket(udp_port);

    int S = atoi(argv[1]);

    int capsules_size = sizeof(Stations)+sizeof(uint32_t)*S;

    Stations* stations = (Stations*)mmap(NULL, capsules_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,-1, 0);
    if (stations == MAP_FAILED)
    {
        ERR("mmap");
    }
    stations->station_cnt = S;
    for (int i=0;i<S;i++)
    {
        stations->capsules[i] = 0;
    }

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(&stations->mutex, &mutex_attr);

    pthread_mutexattr_destroy(&mutex_attr);

    int** pipes = malloc(sizeof(int*)*S);
    if (!pipes)
        ERR("malloc");
    for (int i=0;i<S;i++)
    {
        pipes[i] = malloc(sizeof(int)*2);
    }
    for (int i=0;i<S;i++)
    {
        pipe(pipes[i]);
    }

    pid_t* children_pids = malloc(sizeof(pid_t)*S);
    if (!children_pids)
    {
        ERR("malloc");
    }
    for (int i=0;i<S;i++)
    {
        pid_t pid = fork();
        if (pid<0)
        {
            ERR("fork");
        }
        if (pid == 0)
        {
            child_work(pipes, i, S, stations);

            exit(EXIT_SUCCESS);
        }
        if (pid >0)
        {
            children_pids[i] = pid;
        }
    }

    if (close(pipes[0][0])<0)
    {
        ERR("close");
    }
    for (int i=1;i<S;i++)
    {
        if (close(pipes[i][0])<0)
        {
            ERR("close");
        }
        if (close(pipes[i][1])<0)
        {
            ERR("close");
        }
    }

    int ret = server_work(local_server_sockfd, tcp_server_sockfd, udp_server_sockfd, pipes, children_pids, S);

    if (close(pipes[0][1])<0)
    {
        ERR("close");
    }
    while (wait(NULL)>0);

    for (int i=0;i<S;i++)
    {
        free(pipes[i]);
    }
    free(pipes);
    free(children_pids);
    pthread_mutex_destroy(&stations->mutex);
    if (munmap(stations, capsules_size)<0)
    {
        ERR("munmap");
    }
    if (close(local_server_sockfd)<0)
    {
        ERR("close");
    }
    unlink(UNIX_SOCK_NAME);
    if (close(tcp_server_sockfd)<0)
    {
        ERR("close");
    }
    if (close(udp_server_sockfd)<0)
    {
        ERR("close");
    }

    if (ret == -1)
    {
        exit(EXIT_FAILURE);
    }
    return 0;
}

