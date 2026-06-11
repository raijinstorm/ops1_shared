#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "l8_common.h"

void usage(char* program_name)
{
    fprintf(stderr, "Usage: <N> <port>\n");
    exit(EXIT_FAILURE);
}

#define MAX_MESSAGE_LEN 256
#define INSTRUCTION_LEN 16
#define PART_LEN 200
#define SHM_NAME "/storage"
#define INSTRUCTIONS_NUM 10

#define AIRPORT_FIFO "./airport_fifo"
#define MAXPIPES 6

typedef struct __attribute__((packed)){
    int32_t id;
    uint32_t number;
    char data[MAX_MESSAGE_LEN];
}Message;

typedef struct __attribute__((packed)){
    int32_t id;
    uint32_t number;
    char instruction[INSTRUCTION_LEN+1];
    char part[PART_LEN];
}Instruction;

typedef struct
{
    Instruction instructions[INSTRUCTIONS_NUM];
    int available[INSTRUCTIONS_NUM];
    pthread_mutex_t mutex;
    sem_t semaphores[];
}Storage;

void child_work(Storage* storage, int N, int k, int pipe[2])
{
    if (close(pipe[0])<0)
    {
        ERR("close");
    }

    while (1)
    {
        Instruction current_instruction = {0};
        sem_wait(&storage->semaphores[k]);
        pthread_mutex_lock(&storage->mutex);
        for (int i=0;i<INSTRUCTIONS_NUM;i++)
        {
            if (storage->instructions[i].id%N == k && storage->available[i] == 0)
            {
                current_instruction = storage->instructions[i];
                storage->available[i] = 1;
                break;
            }
        }
        pthread_mutex_unlock(&storage->mutex);
        // sem_post(&storage->semaphores[k]);

        printf("Local Chief [%d]: Claimed part %d.\n", k, current_instruction.id);
        ms_sleep(100);
        if (strcmp(current_instruction.instruction, "STEW") == 0)
        {
            current_instruction.number = ~(current_instruction.number);
        }
        if (write(pipe[1], &current_instruction, sizeof(Instruction))<0)
        {
            if (errno != EPIPE)
            {
                ERR("write");
            }
            break;
        }
    }

    if (close(pipe[1])<0)
    {
        ERR("close");
    }
}

void observer_work(int observer_pipe[2])
{
    if (close(observer_pipe[1])<0)
    {
        ERR("close");
    }

    int fifofd = open(AIRPORT_FIFO, O_WRONLY);
    if (fifofd<0)
    {
        ERR("open");
    }

    while (1)
    {
        Instruction current_instruction = {0};
        int bytes_read = read(observer_pipe[0], &current_instruction, sizeof(Instruction));
        if (bytes_read<0)
        {
            ERR("read");
        }
        if (bytes_read == 0)
        {
            break;
        }

        if (write(fifofd, &current_instruction, sizeof(Instruction))<0)
        {
            if (errno != EPIPE)
                ERR("write");

            break;
        }
    }

    if (close(observer_pipe[0])<0)
    {
        ERR("close");
    }
}

int main(int argc, char** argv)
{
    if (argc!=3)
    {
        usage(argv[0]);
    }

    int N = atoi(argv[1]);
    if (N<2 || N>6)
    {
        usage(argv[0]);
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting SIGPIPE handler");

    uint16_t port = atoi(argv[2]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM, 3);

    shm_unlink(SHM_NAME);
    int shm_size = sizeof(Storage) + sizeof(sem_t)*N;
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (shm_fd == -1)
        ERR("shm_open");
    if (ftruncate(shm_fd, shm_size) == -1)
        ERR("ftruncate");

    Storage* storage = (Storage*)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (storage == MAP_FAILED)
    {
        ERR("mmap");
    }
    if (close(shm_fd)<0)
    {
        ERR("close");
    }

    for (int i=0;i<N;i++)
    {
        sem_init(&storage->semaphores[i], 1, 0);
    }
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(&storage->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    for (int i=0;i<INSTRUCTIONS_NUM;i++)
    {
        storage->available[i] = 1;
    }

    int observer_pipe[2];
    pipe(observer_pipe);

    unlink(AIRPORT_FIFO);
    if (mkfifo(AIRPORT_FIFO, 0666)<0)
    {
        ERR("mkfifo");
    }

    for (int i=0;i<N;i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            child_work(storage, N, i, observer_pipe);

            exit(EXIT_SUCCESS);
        }
        if (pid<0)
        {
            ERR("fork");
        }
    }

    pid_t observer_pid = fork();
    if (observer_pid == 0)
    {
        observer_work(observer_pipe);

        exit(EXIT_SUCCESS);
    }

    if (close(observer_pipe[1])<0)
    {
        ERR("close");
    }
    if (close(observer_pipe[0])<0)
    {
        ERR("close");
    }

    int fifofd = open(AIRPORT_FIFO, O_RDONLY);
    if (fifofd<0)
    {
        ERR("open");
    }

    char result[PART_LEN] = {0};

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

                Instruction current_instruction = {0};
                message.number = ntohl(message.number);

                message.id = ntohl(message.id);
                current_instruction.id = message.id;
                current_instruction.number = message.number;
                char* whitespace = strchr(message.data, ' ');
                if (whitespace == NULL)
                {
                    continue;
                }
                *whitespace = '\0';
                strcpy(current_instruction.instruction, message.data);
                strcpy(current_instruction.part, whitespace+1);

                if (strcmp(current_instruction.instruction, "DISCARD") == 0)
                {
                    continue;
                }
                else
                {
                    printf("Chief: received %d of %s [%d]\n", current_instruction.number, current_instruction.part, current_instruction.id);
                    pthread_mutex_lock(&storage->mutex);
                    int empty_idx = -1;
                    for (int i=0;i<INSTRUCTIONS_NUM;i++)
                    {
                        if (storage->available[i] == 1)
                        {
                            empty_idx = i;
                            break;
                        }
                    }
                    if (empty_idx == -1)
                    {
                        pthread_mutex_unlock(&storage->mutex);
                        continue;
                    }
                    storage->instructions[empty_idx] = current_instruction;
                    storage->available[empty_idx] = 0;
                    int k = current_instruction.id%N;
                    pthread_mutex_unlock(&storage->mutex);
                    sem_post(&storage->semaphores[k]);
                }
            }
            else
            {
                Instruction current_instruction = {0};
                int bytes_read = read(fifofd, &current_instruction, sizeof(Instruction));
                if (bytes_read<0)
                {
                    ERR("read");
                }
                if (bytes_read == 0)
                {
                    break;
                }

                merge(result, current_instruction.part);
                if (strlen(result) == 190)
                {
                    break;
                }
            }
        }

    }

    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, server_sockfd, NULL) == -1)
    {
        perror("epoll_ctl: server_sockfd");
        exit(EXIT_FAILURE);
    }
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, fifofd, NULL) == -1)
    {
        perror("epoll_ctl: fifofd");
        exit(EXIT_FAILURE);
    }
    if (close(fifofd)<0)
    {
        ERR("close");
    }
    if (close(epoll_descriptor)<0)
    {
        ERR("close");
    }

    while (wait(NULL)>0);

    pthread_mutex_destroy(&storage->mutex);
    if (msync(storage,shm_size ,MS_SYNC)<0)
    {
        ERR("msync");
    }
    if (munmap(storage, shm_size)<0)
    {
        ERR("munmap");
    }
    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    unlink(AIRPORT_FIFO);
    shm_unlink(SHM_NAME);
}