#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "./../common.h"

void usage(char* program_name)
{
    fprintf(stderr, "Usage: <port>\n");
    exit(EXIT_FAILURE);
}

#define MESSAGE_LEN 28
#define NAME_LEN 15
#define BINARY_LEN 3
#define MAXPACKAGES 12
#define THREADNUM 4
#define SHM_NAME "/nereus_map"
#define SEM_NAME "/nereus_sem"
#define SNOOP_FIFO "./snoop_fifo"

#define MAP_SIZE 1000

typedef struct __attribute__((packed))
{
    char type;
    char drone_name[NAME_LEN];
    uint32_t binaries[BINARY_LEN];
}Message;

typedef struct Data
{
    Message message;
    struct sockaddr_in addr;
}Data;

typedef struct CircularBuffer
{
    int head;
    int tail;
    int count;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    Data data[MAXPACKAGES];
}CircularBuffer;

typedef struct ThreadArgs
{
    pthread_t thread_id;
    int server_sockfd;
    int* work;
    char* map;
    CircularBuffer* circular_buffer;
}ThreadArgs;

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

void* thread_work(void* args_t)
{
    ThreadArgs* args = (ThreadArgs*)args_t;
    sem_t* semaphore = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    while (1)
    {
        pthread_mutex_lock(&args->circular_buffer->mtx);
        while ((args->circular_buffer->count == 0 && *args->work) || (*args->work == 0))
        {
            pthread_cond_wait(&args->circular_buffer->not_empty,&args->circular_buffer->mtx);
        }
        // if (!args->work)
        // {
        //     pthread_mutex_unlock(&args->circular_buffer->mtx);
        //     pthread_cond_wait
        // }
        Data current_data = args->circular_buffer->data[args->circular_buffer->head];
        args->circular_buffer->head = (args->circular_buffer->head+1)%MAXPACKAGES;
        args->circular_buffer->count--;
        pthread_cond_signal(&args->circular_buffer->not_full);
        pthread_mutex_unlock(&args->circular_buffer->mtx);

        sem_wait(semaphore);
        uint32_t idx = current_data.message.binaries[0]*MAP_SIZE + current_data.message.binaries[1];
        args->map[idx] = current_data.message.drone_name[0];
        sem_post(semaphore);

        socklen_t send_socklen = sizeof(struct sockaddr_in);
        char ack = 'A';
        if (sendto(args->server_sockfd,&ack, 1, 0, (struct sockaddr*)&current_data.addr, send_socklen)<0)
        {
            ERR("sendto");
        }
    }

    return NULL;
}

void director_work(int director_pipe[2])
{
    if (close(director_pipe[0])<0)
    {
        ERR("close");
    }

    while (1)
    {
        char buf[MESSAGE_LEN] = {0};
        fgets(buf, MESSAGE_LEN, stdin);

        if (strcmp(buf, "PAUSE\n") == 0)
        {
            if (write(director_pipe[1], "P", 1)<0)
            {
                if (errno != EPIPE)
                {
                    ERR("write");
                }
                break;
            }
        }
        if (strcmp(buf, "RUN\n") == 0)
        {
            if (write(director_pipe[1], "C", 1)<0)
            {
                if (errno != EPIPE)
                {
                    ERR("write");
                }
                break;
            }
        }
    }

    if (close(director_pipe[1])<0)
    {
        ERR("close");
    }
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGPIPE handler");
    shm_unlink(SHM_NAME);

    int server_sockfd = bind_udp_socket(atoi(argv[1]));

    int shm_size = MAP_SIZE*MAP_SIZE;
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (shm_fd == -1)
        ERR("shm_open");
    if (ftruncate(shm_fd, shm_size) == -1)
        ERR("ftruncate");

    char* map = (char*)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (map == MAP_FAILED)
        ERR("mmap");

    for (int i=0;i<shm_size;i++)
    {
        map[i] = '~';
    }

    unlink(SEM_NAME);
    sem_t* semaphore = sem_open(SEM_NAME, O_CREAT, 0666, 1);

    unlink(SNOOP_FIFO);
    if (mkfifo(SNOOP_FIFO, 0666)<0)
    {
        ERR("create fifo");
    }
    int fifofd = open(SNOOP_FIFO, O_RDWR | O_NONBLOCK); //IF REUSE, CHANGE O_RDWR

    CircularBuffer circular_buffer;
    circular_buffer.count = 0;
    circular_buffer.head = 0;
    circular_buffer.tail = 0;
    memset(circular_buffer.data, 0, sizeof(Data)*MAXPACKAGES);
    pthread_mutex_init(&circular_buffer.mtx, NULL);
    pthread_cond_init(&circular_buffer.not_empty, NULL);
    pthread_cond_init(&circular_buffer.not_full, NULL);

    ThreadArgs thread_args[THREADNUM];
    int work = 1;
    for (int i=0;i<THREADNUM;i++)
    {
        thread_args[i].circular_buffer = &circular_buffer;
        thread_args[i].server_sockfd = server_sockfd;
        thread_args[i].work = &work;
        thread_args[i].map = map;
        pthread_create(&thread_args[i].thread_id, NULL, thread_work, &thread_args[i]);
    }

    int director_pipe[2] = {0};
    pipe(director_pipe);
    pid_t director_pid = fork();
    if (director_pid == 0)
    {
        director_work(director_pipe);

        exit(EXIT_SUCCESS);
    }
    if (director_pid<0)
    {
        ERR("fork");
    }

    if (close(director_pipe[1])<0)
    {
        ERR("close");
    }

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
    event.data.fd = director_pipe[0];
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, director_pipe[0], &event) == -1)
    {
        perror("epoll_ctl: director_pipe[0]");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        if (epoll_wait(epoll_descriptor, &current_event, 1, -1)>0)
        {
            if (current_event.data.fd == server_sockfd)
            {
                Message message = {0};
                struct sockaddr_in addr;
                socklen_t addrlen = sizeof(addr);
                int bytes_received = recvfrom(server_sockfd, &message, sizeof(Message), 0, (struct sockaddr*)&addr, &addrlen);
                if (bytes_received<0)
                {
                    ERR("recvfrom");
                }
                if (bytes_received!=MESSAGE_LEN)
                {
                    printf("error: wrong message length\n");
                    continue;
                }

                for (int i=0;i<BINARY_LEN;i++)
                {
                    message.binaries[i] = ntohl(message.binaries[i]);
                }
                char drone_name[NAME_LEN+1] = {0};
                memcpy(drone_name, message.drone_name, NAME_LEN);

                if (message.type == 'R')
                {
                    Data current_data = {message, addr};
                    pthread_mutex_lock(&circular_buffer.mtx);
                    while (circular_buffer.count==MAXPACKAGES)
                    {
                        pthread_cond_wait(&circular_buffer.not_full, &circular_buffer.mtx);
                    }
                    circular_buffer.data[circular_buffer.tail] = current_data;
                    circular_buffer.count++;
                    circular_buffer.tail = (circular_buffer.tail+1)%MAXPACKAGES;

                    pthread_cond_signal(&circular_buffer.not_empty);
                    pthread_mutex_unlock(&circular_buffer.mtx);

                    printf("[Report] Drone %s at %d,%d gathered %d tons.\n", drone_name, message.binaries[0], message.binaries[1], message.binaries[2]);

                }
                else if (message.type == 'Q')
                {
                    printf("[Quit] Drone %s is returning to base.\n", drone_name);
                    if (write(fifofd, drone_name, strlen(drone_name)) < 0)
                    {
                        if (errno != EPIPE)
                        {
                            ERR("write");
                        }
                        continue;
                    }
                }
                else
                {
                    printf("error: unknown type %c\n", message.type);
                }
            }
            else
            {
                char c;
                int bytes_read = read(director_pipe[0], &c, 1);
                if (bytes_read == 0)
                {
                    break;
                }
                if (bytes_read<0)
                {
                    ERR("read");
                }

                if (c=='P')
                {
                    printf("== System Paused ==\n");
                    if (work!=0)
                    {
                        work = 0;
                    }
                }
                if (c=='C')
                {
                    printf("== System Running ==\n");
                    if (work!=1)
                    {
                        work = 1;
                        pthread_mutex_lock(&circular_buffer.mtx);
                        pthread_cond_broadcast(&circular_buffer.not_empty);
                        pthread_mutex_unlock(&circular_buffer.mtx);
                    }
                }
            }
        }
    }

    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, server_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, director_pipe[0], NULL) == -1)
    {
        perror("epoll_ctl: director_pipe[0]");
        exit(EXIT_FAILURE);
    }
    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }

    if (close(director_pipe[0])<0)
    {
        ERR("close");
    }

    while (wait(NULL)>0);

    if (close(shm_fd)<0)
    {
        ERR("close");
    }
    sem_close(semaphore);
    unlink(SEM_NAME);
    shm_unlink(SHM_NAME);
    return 0;
}