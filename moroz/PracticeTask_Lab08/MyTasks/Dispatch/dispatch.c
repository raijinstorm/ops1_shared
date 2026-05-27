#include "common_l8.h"

//  max heap code was taken and modified from https://github.com/robin-thomas/max-heap/blob/master/maxHeap.c

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

#define BUFLEN 512
#define BACKLOG 3
#define SHIP_ID_LEN 6
#define LOG_LEN 256
#define REQ 0
#define CAN 1
#define RESCUE_DELAY 50

#define MAX_EMERGENCIES 64

#define LCHILD(x) 2 * x + 1
#define RCHILD(x) 2 * x + 2
#define PARENT(x) (x - 1) / 2

typedef struct Message
{
    char buf[BUFLEN+1];
}Message;

typedef struct __attribute__((__packed__)){
    struct sockaddr_in addr;
    uint8_t command;
    uint8_t priority_level;
    char ship_id[SHIP_ID_LEN+1];
    int32_t X;
    int32_t Y;
    int32_t Z;
    char log_message[LOG_LEN+1];
}ShipMessage;

typedef struct Node{
    ShipMessage data;
}Node;

typedef struct maxHeap {
    int count;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    Node elem[MAX_EMERGENCIES];
}MaxHeap;

/*
    Function to initialize the max heap with size = 0
*/
MaxHeap initMaxHeap(int size) {
    MaxHeap hp;
    hp.count = 0;
    memset(hp.elem, 0, size*sizeof(Node));
    pthread_mutex_init(&hp.mtx, NULL);
    pthread_cond_init(&hp.not_empty, NULL);
    return hp;
}

/*
    Function to swap data within two nodes of the max heap using pointers
*/
void swap(Node *n1, Node *n2) {
    Node temp = *n1 ;
    *n1 = *n2 ;
    *n2 = temp ;
}

/*
    Function to insert a node into the max heap
*/
void insertNode(MaxHeap* hp, ShipMessage data)
{
    Node new_node= {data};

    int i=hp->count++;
    if (i>=MAX_EMERGENCIES)
    {
        printf("[Queue] Overloaded, dropping %s\n", new_node.data.ship_id);
        return;
    }
    while (i && hp->elem[PARENT(i)].data.priority_level<new_node.data.priority_level)
    {
        hp->elem[i].data = hp->elem[PARENT(i)].data;
        i = PARENT(i);
    }
    hp->elem[i] = new_node;
    printf("Emergency on %s\n", new_node.data.ship_id);
}

/*
    Heapify function is used to make sure that the heap property is never violated
    In case of deletion of a node, or creating a max heap from an array, heap property
    may be violated. In such cases, heapify function can be called to make sure that
    heap property is never violated
*/
void heapify(MaxHeap* hp, int i)
{
    int largest = i;
    if (LCHILD(i)<hp->count && hp->elem[LCHILD(i)].data.priority_level>hp->elem[largest].data.priority_level)
    {
        largest = LCHILD(i);
    }
    if (RCHILD(i)<hp->count && hp->elem[RCHILD(i)].data.priority_level>hp->elem[largest].data.priority_level)
    {
        largest = RCHILD(i);
    }
    if (largest!=i)
    {
        swap(&hp->elem[largest], &hp->elem[i]);
        heapify(hp, largest);
    }
}

/*
    Function to pop the first node from the max heap
    It shall remove the root node, and place the last node in its place
    and then call heapify function to make sure that the heap property
    is never violated. Returns the popped value or a zeroed out Node
*/
Node popNode(MaxHeap* hp)
{
    Node ret = {0};
    if (hp->count)
    {
        printf("Deleting node %d\n\n", hp->elem[0].data.priority_level);
        ret = hp->elem[0];
        hp->count--;
        hp->elem[0].data = hp->elem[hp->count].data;
        heapify(hp,0);
    }
    else
    {
        printf("No more elements in the heap\n");
    }
    return ret;
}

void siftUp(MaxHeap* hp, int index)
{
    if (index>=hp->count || index<=0)
    {
        return;
    }
    if (hp->elem[index].data.priority_level > hp->elem[PARENT(index)].data.priority_level)
    {
        swap(&hp->elem[index], &hp->elem[PARENT(index)]);
        siftUp(hp,PARENT(index));
    }
}

/*
    Function to delete a node from the max heap at some index
    It shall swap the element at index with the last element in the heap
    and then depending on whether it is greater than the parent or smaller than children
    call siftUp function or heapify to make sure that the heap property
    is never violated.
*/
void deleteNodeAtIndex(MaxHeap* hp, int index)
{
    if (index>=hp->count || index<0)
    {
        return;
    }
    hp->count-=1;
    if (hp->count == index)
    {
        return;
    }

    swap(&hp->elem[hp->count], &hp->elem[index]);
    if (hp->elem[index].data.priority_level > hp->elem[PARENT(index)].data.priority_level)
    {
       siftUp(hp,index);
    }
    else{
        heapify(hp, index);
    }
}

typedef struct ThreadArgs
{
    int server_sockfd;
    pthread_t thread_id;
    MaxHeap* max_heap;
    ShipMessage current_message;
}ThreadArgs;

void* worker_thread(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    pthread_mutex_lock(&args->max_heap->mtx);
    insertNode(args->max_heap, args->current_message);
    pthread_cond_signal(&args->max_heap->not_empty);
    pthread_mutex_unlock(&args->max_heap->mtx);

    free(t_args);
    return NULL;
}

void* dispatcher_thread(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;

    while (1)
    {
        Node current_message;
        pthread_mutex_lock(&args->max_heap->mtx);
        while (args->max_heap->count == 0)
        {
            pthread_cond_wait(&args->max_heap->not_empty, &args->max_heap->mtx);
        }
        current_message = popNode(args->max_heap);
        pthread_mutex_unlock(&args->max_heap->mtx);

        if (current_message.data.priority_level == 0)
        {
            continue;
        }
        usleep(5000*1000);
        char message[256] = {0};
        snprintf(message, 256,"ACK;%s;RESOLVED", current_message.data.ship_id);
        socklen_t len = sizeof(struct sockaddr_in);
        struct sockaddr_in addr = current_message.data.addr;

         if (sendto(args->server_sockfd, message, strlen(message), 0, (struct sockaddr*)&addr, len)<0)
        {
            // char* errname = strerrorname_np(errno);
            // printf("%s\n", errname);
            ERR("sendto");
        }
        char ip_str[64] = {0};
        inet_ntop(addr.sin_family, &addr.sin_addr, ip_str, sizeof(ip_str));
        printf("[Dispatch] Rescued %s. ACK sent to %s:%d\n",current_message.data.ship_id, ip_str, ntohs(addr.sin_port));

    }

    free(t_args);
    return NULL;
}

int find_node_index(MaxHeap* hp, char* ship_id)
{
    for (int i=0;i<hp->count;i++)
    {
        if (strcmp(hp->elem[i].data.ship_id,ship_id) == 0)
        {
            return i;
        }
    }
    return -1;
}

void* cancel_work(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    pthread_mutex_lock(&args->max_heap->mtx);
    int index = find_node_index(args->max_heap, args->current_message.ship_id);
    if (index >= 0)
    {
        deleteNodeAtIndex(args->max_heap, index);
    }
    pthread_mutex_unlock(&args->max_heap->mtx);

    char message[64];
    if (index>=0)
    {
        snprintf(message, 64,"ACK;%s;CANCELLED", args->current_message.ship_id);
    }
    else
    {
        snprintf(message, 64, "ERR;%s;NOT_FOUND", args->current_message.ship_id);
    }
    socklen_t len = sizeof(struct sockaddr_in);
    struct sockaddr_in addr = args->current_message.addr;

    if (sendto(args->server_sockfd, message, strlen(message), 0, (struct sockaddr*)&addr, len)<0)
    {
        // char* errname = strerrorname_np(errno);
        // printf("%s\n", errname);
        ERR("sendto");
    }

    free(t_args);
    return NULL;
}

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    int port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM, BACKLOG);

    MaxHeap heap = initMaxHeap(MAX_EMERGENCIES);

    ThreadArgs* dispatcher_args = malloc(sizeof(ThreadArgs));
    dispatcher_args->max_heap = &heap;
    memset(&dispatcher_args->current_message,0,sizeof(ShipMessage));
    dispatcher_args->server_sockfd = server_sockfd;

    pthread_attr_t dispatcher_attr;
    if (pthread_attr_init(&dispatcher_attr))
        ERR("pthread_attr_init");
    if (pthread_attr_setdetachstate(&dispatcher_attr, PTHREAD_CREATE_DETACHED))
        ERR("pthread_attr_setdetachstate");

    if (pthread_create(&dispatcher_args->thread_id, &dispatcher_attr, dispatcher_thread, dispatcher_args))
    {
        ERR("pthread_create");
    }
    pthread_attr_destroy(&dispatcher_attr);

    while (1)
    {
        Message msg = {0};
        struct sockaddr_in addr;
        socklen_t len = sizeof(struct sockaddr_in);
        int bytes_received = recvfrom(server_sockfd, msg.buf, BUFLEN, 0, (struct sockaddr*)&addr, &len);
        if (bytes_received < 0)
        {
            perror("recvfrom");
            continue;
        }
        msg.buf[bytes_received] = '\0';

        ShipMessage ship_message = {0};
        int message_part = 0;
        char* saveptr;
        char* token = strtok_r(msg.buf, ";",&saveptr);
        while (token!=NULL)
        {
            if (message_part==0)
            {
                if (strcmp(token,"REQ")==0)
                {
                    ship_message.command = REQ;
                }
                else if (strcmp(token,"CAN") == 0)
                {
                    ship_message.command = CAN;
                }
                else
                {
                    printf("Incorrect command\n");
                    break;
                }
            }
            else if (message_part == 1)
            {
                if (strcmp(token,"LOW")==0)
                {
                    ship_message.priority_level = 1;
                }
                else if (strcmp(token, "MID") == 0){
                    ship_message.priority_level = 2;
                }
                else if (strcmp(token,"HIGH") == 0)
                {
                    ship_message.priority_level = 3;
                }
                else if (strcmp(token, "CRIT") == 0)
                {
                    ship_message.priority_level = 4;
                }
                else
                {
                    printf("Incorrect priority level\n");
                    break;
                }
            }
            else if (message_part == 2)
            {
                if (strlen(token)!=6)
                {
                    printf("Incorrect ship id length\n");
                    break;
                }
                memcpy(ship_message.ship_id, token, SHIP_ID_LEN+1);
            }
            else if (message_part == 3)
            {
                int32_t tempX, tempY, tempZ;
                if (sscanf(token, "[%d,%d,%d]", &tempX, &tempY, &tempZ)!=3)
                {
                    printf("Incorrect coordinates format\n");
                    break;
                }
                ship_message.X = tempX;
                ship_message.Y = tempY;
                ship_message.Z = tempZ;

                // size_t log_len = strlen(saveptr);
                // log_len = log_len<LOG_LEN+1?log_len:LOG_LEN;
                // memcpy(ship_message.log_message, saveptr, log_len);
                // ship_message.log_message[log_len] = '\0';
                //
                // message_part = 5;
                // break;
            }
            else if (message_part == 4)
            {
                size_t log_len = strlen(token);
                log_len = log_len<LOG_LEN+1?log_len:LOG_LEN;
                memcpy(ship_message.log_message, token, log_len);
                ship_message.log_message[log_len] = '\0';
            }

            token = strtok_r(NULL, ";",&saveptr);
            message_part++;
        }
        if (message_part != 5)
        {
            char ip_str[64] = {0};
            inet_ntop(addr.sin_family, &addr.sin_addr, ip_str, sizeof(ip_str));
            printf("[Error] Malformed datagram from %s:%d\n", ip_str, ntohs(addr.sin_port));
            continue;
        }

        ship_message.addr = addr;
        if (ship_message.command == REQ)
        {
            printf("[Parse] Valid REQ from %s at X:%d Y:%d Z:%d with priority %d\n", ship_message.ship_id,ship_message.X, ship_message.Y, ship_message.Z, ship_message.priority_level);

            ThreadArgs* args = malloc(sizeof(ThreadArgs));
            args->server_sockfd = server_sockfd;
            args->max_heap = &heap;
            args->current_message = ship_message;

            pthread_attr_t attr;

            if (pthread_attr_init(&attr))
                ERR("pthread_attr_init");
            if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
                ERR("pthread_attr_setdetachstate");

            if (pthread_create(&args->thread_id, &attr, worker_thread, args))
            {
                ERR("pthread_create");
            }
            pthread_attr_destroy(&attr);
        }
        else if (ship_message.command == CAN)
        {
            ThreadArgs* args = malloc(sizeof(ThreadArgs));
            args->server_sockfd = server_sockfd;
            args->max_heap = &heap;
            args->current_message = ship_message;

            pthread_attr_t attr;

            if (pthread_attr_init(&attr))
                ERR("pthread_attr_init");
            if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
                ERR("pthread_attr_setdetachstate");

            if (pthread_create(&args->thread_id, &attr, cancel_work, args))
            {
                ERR("pthread_create");
            }
            pthread_attr_destroy(&attr);
        }
    }

    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
}