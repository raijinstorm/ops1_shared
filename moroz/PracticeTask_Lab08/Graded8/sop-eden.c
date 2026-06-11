#include "l8_common.h"

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

#define COMMANDS_NUM 6
static char* COMMANDS[COMMANDS_NUM]  = {"RUN", "EXIT", "PAUSE", "COMPUTE", "LIST", "GATHER"};

#define LOGINLEN 16
#define MAX_COMMAND_LEN 8
#define MAX_PARAMS_NUM 10

typedef struct __attribute__((__packed__)){
    char login[LOGINLEN];
    char command[MAX_COMMAND_LEN];
    uint32_t parameters[MAX_PARAMS_NUM];
}Message;

typedef struct Job
{
    uint32_t sample_count;
    uint32_t seed;
    char user[LOGINLEN+1];
}Job;

typedef struct Node {
    Job data;
    struct Node* next;
    struct Node* prev;
}Node;

typedef struct DoublyLinkedList
{
    pthread_mutex_t mutex;
    pthread_cond_t non_empty;
    Node *head;
    Node *tail;
}DoublyLinkedList;

typedef struct Approximations
{
    pthread_mutex_t appr_mutex;
    double approximations[USERS];
    int approximation_counts[USERS];
}Approximations;

typedef struct ThreadArgs
{
    DoublyLinkedList* list;
    int* running_users;
    pthread_mutex_t* running_users_mutex;
    pthread_t thread_id;
    int server_sockfd;
    int* work;
    Approximations* approximations;
}ThreadArgs;

int add_item_to_end(DoublyLinkedList *list, Job item)
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

int is_valid_command(Message message)
{
    for (int i=0;i<COMMANDS_NUM;i++)
    {
        if (strncmp(message.command, COMMANDS[i], MAX_COMMAND_LEN) == 0)
        {
            return 1;
        }
    }
    return 0;
}

int is_valid_login(Message message)
{
    for (int i=0;i<USERS;i++)
    {
        if (strncmp(message.login, LOGINS[i], LOGINLEN) == 0)
        {
            return 1;
        }
    }
    return 0;
}

int find_user_index(char username[LOGINLEN+1])
{
    for (int i=0;i<USERS;i++)
    {
        if (strncmp(username, LOGINS[i], LOGINLEN) == 0)
        {
            return i;
        }
    }
    return -1;
}

void* thread_work(void* args_t)
{
    ThreadArgs* args = (ThreadArgs*)args_t;
    while (1)
    {
        pthread_mutex_lock(&args->list->mutex);
        while (*args->work && args->list->head == NULL)
        {
            pthread_cond_wait(&args->list->non_empty, &args->list->mutex);
        }
        if (args->list->head == NULL && *args->work == 0)
        {
            pthread_mutex_unlock(&args->list->mutex);
            break;
        }
        //printf("I BLOCK THEREFORE I AM %d\n", *args->work);
        Node* temp = args->list->head;
        Job current_job = {0};
        while (temp)
        {
            current_job = temp->data;
            int user_idx = find_user_index(current_job.user);
            int is_paused = 0;
            pthread_mutex_lock(args->running_users_mutex);
            if (args->running_users[user_idx] == 0)
            {
                is_paused = 1;
            }
            pthread_mutex_unlock(args->running_users_mutex);

            if (!is_paused)
            {
                break;
            }
            temp = temp->next;
        }
        if (temp == NULL)
        {
            pthread_cond_wait(&args->list->non_empty, &args->list->mutex);
            pthread_mutex_unlock(&args->list->mutex);
            continue;
        }
        delete_item(args->list, temp);
        pthread_mutex_unlock(&args->list->mutex);

        int samples_todo = (current_job.sample_count>1000)?1000:current_job.sample_count;

        const int seed = current_job.seed;
        double result = compute_pi(samples_todo, &seed);
        int user_idx = find_user_index(current_job.user);
        if (user_idx<0)
        {
            continue;
        }
        pthread_mutex_lock(&args->approximations->appr_mutex);
        int old_s = args->approximations->approximation_counts[user_idx];
        double old_ps = args->approximations->approximations[user_idx]*args->approximations->approximation_counts[user_idx];
        args->approximations->approximation_counts[user_idx]+=samples_todo;
        args->approximations->approximations[user_idx] = (old_ps + result*samples_todo)/(old_s+samples_todo);
        pthread_mutex_unlock(&args->approximations->appr_mutex);

        if ((uint32_t)samples_todo<current_job.sample_count)
        {
            pthread_mutex_lock(&args->list->mutex);
            Job remainder = {0};
            remainder.sample_count = current_job.sample_count-samples_todo;
            remainder.seed = current_job.seed;
            strcpy(remainder.user, current_job.user);
            add_item_to_end(args->list,remainder);
            pthread_cond_signal(&args->list->non_empty);
            pthread_mutex_unlock(&args->list->mutex);
        }
    }

    return NULL;
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    int port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM, 3);

    DoublyLinkedList list;
    list.head = NULL;
    list.tail = NULL;
    pthread_mutex_init(&list.mutex, NULL);
    pthread_cond_init(&list.non_empty, NULL);

    Approximations approximations = {0};
    pthread_mutex_init(&approximations.appr_mutex, NULL);

    int running_users[USERS];
    for (int i=0;i<USERS;i++)
    {
        running_users[i] = 1;
    }
    pthread_mutex_t running_users_mutex;
    pthread_mutex_init(&running_users_mutex, NULL);

    ThreadArgs thread_args[THREADS];
    int work = 1;
    for (int i=0;i<THREADS;i++)
    {
        thread_args[i].list = &list;
        thread_args[i].server_sockfd = server_sockfd;
        thread_args[i].approximations = &approximations;
        thread_args[i].work = &work;
        thread_args[i].running_users = running_users;
        thread_args[i].running_users_mutex = &running_users_mutex;
        pthread_create(&thread_args[i].thread_id, NULL, thread_work, &thread_args[i]);
    }

    while (1)
    {
        char raw_message[MSG_MAX+1] = {0};
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(struct sockaddr);
        int bytes_received = recvfrom(server_sockfd, &raw_message, sizeof(raw_message), 0, &addr, &addrlen);
        if (bytes_received<0)
        {
            ERR("recvfrom");
        }
        if (bytes_received<LOGINLEN+MAX_COMMAND_LEN || bytes_received>MSG_MAX)
        {
            printf("error: wrong message length %d\n", bytes_received);
            continue;
        }
        Message message = {0};
        memcpy(&message, raw_message, bytes_received);

        if (!is_valid_command(message))
        {
            printf("error: unknown command %8s\n", message.command);
            continue;
        }
        if (!is_valid_login(message))
        {
            printf("error: unknown user %16s\n", message.login);
            continue;
        }

        char current_command[MAX_COMMAND_LEN+1] = {0};
        memcpy(current_command, message.command, MAX_COMMAND_LEN);
        char current_login[LOGINLEN+1] = {0};
        memcpy(current_login, message.login, LOGINLEN);

        if (strcmp(current_command, "COMPUTE") == 0)
        {
            int parameters_bytes = bytes_received - (LOGINLEN+MAX_COMMAND_LEN);
            if (parameters_bytes%(sizeof(uint32_t)*2) != 0)
            {
                printf("error: wrong message length %d\n", bytes_received);
                continue;
            }
            int parameters_count = parameters_bytes/sizeof(uint32_t);

            for (int i=0;i<parameters_count;i++)
            {
                message.parameters[i] = ntohl(message.parameters[i]);
            }

            for (int i=0;i<parameters_count;i+=2)
            {
                Job current_job = {0};
                strcpy(current_job.user, current_login);
                current_job.sample_count = message.parameters[i];
                if (current_job.sample_count > 1000000)
                {
                    printf("error: too large sample count\n");
                    continue;
                }
                current_job.seed = message.parameters[i+1];

                pthread_mutex_lock(&list.mutex);
                add_item_to_end(&list, current_job);
                pthread_cond_signal(&list.non_empty);
                pthread_mutex_unlock(&list.mutex);

            }

            printf("%s: %s ", current_login, current_command);

            for (int i=0;i<parameters_count;i++)
            {
                printf("%d ", message.parameters[i]);
            }
            printf("\n");
        }
        else
        {
            if (bytes_received - (LOGINLEN+MAX_COMMAND_LEN) != 0)
            {
                printf("error: wrong message length %d\n", bytes_received);
                continue;
            }
            printf("%s: %s\n", current_login, current_command);

            if (strcmp(current_command, "EXIT") == 0)
            {
                work = 0;
                break;
            }

            if (strcmp(current_command, "LIST") == 0)
            {
                struct sockaddr_in send_addr = addr;
                send_addr.sin_port = htons(port+1);
                socklen_t send_socklen = sizeof(struct sockaddr_in);

                pthread_mutex_lock(&list.mutex);
                Node* temp = list.head;
                uint32_t sendbuf[MSG_MAX/sizeof(uint32_t)];
                int i = 0;
                while (temp)
                {
                    if (strcmp(temp->data.user, current_login) == 0)
                    {
                        sendbuf[i++] = htonl(temp->data.sample_count);
                        sendbuf[i++] = htonl(temp->data.seed);
                        if (i == MSG_MAX/sizeof(uint32_t))
                        {
                            if (sendto(server_sockfd,sendbuf, MSG_MAX, 0, (struct sockaddr*)&send_addr, send_socklen)<0)
                            {
                                ERR("sendto");
                            }
                            i = 0;
                        }
                    }
                    temp = temp->next;
                }
                pthread_mutex_unlock(&list.mutex);

                if (i>0)
                {
                    if (sendto(server_sockfd,sendbuf, MSG_MAX, 0, (struct sockaddr*)&send_addr, send_socklen)<0)
                    {
                        ERR("sendto");
                    }
                }
            }

            if (strcmp(current_command, "GATHER") == 0)
            {
                char sendbuf[MSG_MAX] = {0};
                int idx = find_user_index(current_login);
                pthread_mutex_lock(&approximations.appr_mutex);
                snprintf(sendbuf, MSG_MAX, "%s: %.12lf\n", current_login, approximations.approximations[idx]);
                pthread_mutex_unlock(&approximations.appr_mutex);

                // printf("%s\n", sendbuf);
                struct sockaddr_in send_addr = addr;
                send_addr.sin_port = htons(port+1);
                socklen_t send_socklen = sizeof(struct sockaddr_in);
                if (sendto(server_sockfd, sendbuf, strlen(sendbuf), 0, (struct sockaddr*)&send_addr, send_socklen)<0)
                {
                    ERR("sendto");
                }
            }

            if (strcmp(current_command, "PAUSE") == 0)
            {
                int idx = find_user_index(current_login);
                if (idx<0)
                {
                    continue;
                }
                pthread_mutex_lock(&running_users_mutex);
                if (running_users[idx] == 0)
                {
                    char sendbuf[MSG_MAX] = {0};
                    snprintf(sendbuf,MSG_MAX, "%s: already paused\n", current_login);

                    printf("%s", sendbuf);

                    struct sockaddr_in send_addr = addr;
                    send_addr.sin_port = htons(port+1);
                    socklen_t send_socklen = sizeof(struct sockaddr_in);
                    if (sendto(server_sockfd, sendbuf, strlen(sendbuf), 0, (struct sockaddr*)&send_addr, send_socklen)<0)
                    {
                        ERR("sendto");
                    }
                }
                else
                {
                    running_users[idx] = 0;
                }
                pthread_mutex_unlock(&running_users_mutex);
            }

            if (strcmp(current_command, "RUN") == 0)
            {
                int idx = find_user_index(current_login);
                if (idx<0)
                {
                    continue;
                }
                pthread_mutex_lock(&running_users_mutex);
                if (running_users[idx] == 1)
                {
                    char sendbuf[MSG_MAX] = {0};
                    snprintf(sendbuf,MSG_MAX, "%s: already running\n", current_login);

                    printf("%s", sendbuf);

                    struct sockaddr_in send_addr = addr;
                    send_addr.sin_port = htons(port+1);
                    socklen_t send_socklen = sizeof(struct sockaddr_in);
                    if (sendto(server_sockfd, sendbuf, strlen(sendbuf), 0, (struct sockaddr*)&send_addr, send_socklen)<0)
                    {
                        ERR("sendto");
                    }
                }
                else
                {
                    running_users[idx] = 1;
                }
                pthread_mutex_unlock(&running_users_mutex);

                pthread_mutex_lock(&list.mutex);
                pthread_cond_broadcast(&list.non_empty);
                pthread_mutex_unlock(&list.mutex);
            }
        }
    }

    pthread_cond_broadcast(&list.non_empty);

    for (int i=0;i<THREADS;i++)
    {
        pthread_join(thread_args[i].thread_id, NULL);
    }
    pthread_mutex_destroy(&list.mutex);
    pthread_mutex_destroy(&running_users_mutex);
    pthread_cond_destroy(&list.non_empty);
    clean_list(&list);
    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}