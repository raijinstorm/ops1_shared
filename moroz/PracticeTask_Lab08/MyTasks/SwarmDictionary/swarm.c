#include "common_l8.h"

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

#define BACKLOG 3
#define BUFLEN 256
#define DRONE_ID_LEN 8
#define OPCODE_LEN 4

#define MAX_DRONES 128

typedef struct Message
{
    char buf[BUFLEN+1];
}Message;

typedef struct __attribute__((__packed__)){
    char drone_id[DRONE_ID_LEN+1];
    char opcode[OPCODE_LEN+1];
    float X;
    float Y;
    int32_t reason_code;
}DroneMessage;


/*
 * enum for tracking the state of the cell.
 *   DELETED is needed to correctly get the collided elements
 *   if the original one on the collided index was removed
 */
typedef enum
{
    EMPTY = 0,
    OCCUPIED,
    DELETED
}SlotStatus;

//Node of the Hash table
typedef struct
{
    char drone_id[DRONE_ID_LEN+1];
    float X;
    float Y;
    time_t last_seen;
    struct sockaddr_in addr;
    SlotStatus status;
}DroneNode;

//Hash table
typedef struct
{
    DroneNode slots[MAX_DRONES];
    int count;
    pthread_mutex_t mtx;
}DroneTable;

/*
 *A function that can hash any data
 *The logic is similar to the hash_function for strings below
 *It casts raw memory to an unsigned char pointer to read it byte by byte
 *In the loop it avoids null bytes by using +ptr[i]
 */
unsigned int hash_memory(const void* data, size_t length)
{
    unsigned long hash = 5381;

    const unsigned char* ptr = (const unsigned char*)data;

    for (size_t i = 0; i < length; i++)
    {
        hash = ((hash << 5) + hash) + ptr[i]; /* hash * 33 + c */
    }

    return hash % MAX_DRONES;
}

/*
 *Initialising the hash table
 */
void init_table(DroneTable* table)
{
    memset(table, 0, sizeof(DroneTable));
    pthread_mutex_init(&table->mtx, NULL);
}

/*
 *Clever hash function
 */
unsigned int hash_function(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash % MAX_DRONES; //keep index within array bounds
}

/*
 * find: Probes until it finds the ID, or hits a definitively EMPTY slot.
 * Returns the index if found, -1 if not found
 */
int find_element(DroneTable* table, const char* id) {
    unsigned int start_idx = hash_function(id);

    for (int i = 0; i < MAX_DRONES; i++) {
        int probe = (start_idx + i) % MAX_DRONES;

        //If we hit an EMPTY slot, the element is not in the table
        if (table->slots[probe].status == EMPTY) {
            return -1;
        }

        //If it's occupied and matches our ID
        if (table->slots[probe].status == OCCUPIED && strcmp(table->slots[probe].drone_id, id) == 0) {
            return probe;
        }

        //If DELETED is hit, the loop continues
    }
    return -1;
}

/*
 * insert: Probes until it finds an EMPTY or DELETED slot.
 * if it's already in the table update it instead of adding a duplicate
 * otherwise loop until empty slot to avoid collision and insert
 * Returns the index on success, -1 if the table is full
 */
int insert_update_element(DroneTable* table, const char* drone_id, float x, float y)
{
    int existing_idx = find_element(table, drone_id);
    if (existing_idx!=-1)
    {
        table->slots[existing_idx].X = x;
        table->slots[existing_idx].Y = y;
        table->slots[existing_idx].last_seen = time(NULL);
        return existing_idx;
    }

    if (table->count+1 > MAX_DRONES)
    {
        return -1;
    }

    unsigned long start_idx = hash_function(drone_id);
    for (int i=0;i<MAX_DRONES;i++)
    {
        int probe = (start_idx+i)%MAX_DRONES;

        if (table->slots[probe].status == EMPTY || table->slots[probe].status == DELETED)
        {
            strcpy(table->slots[probe].drone_id, drone_id);
            table->slots[probe].X = x;
            table->slots[probe].Y = y;
            table->slots[probe].last_seen = time(NULL);
            table->slots[probe].status = OCCUPIED;
            table->count++;
            return probe;
        }

    }
    return -1;
}

/*
 * Lazy deletion: Finds the item and marks it as DELETED.
 */
void delete_element(DroneTable* table, char* drone_id)
{
    int idx = find_element(table, drone_id);
    if (idx!=-1)
    {
        table->slots[idx].status = DELETED;
        table->count--;
    }
}

typedef struct
{
    pthread_t thread_id;
    DroneMessage message;
    DroneTable* hash_table;
    struct sockaddr_in addr;
    int server_sockfd;
}ThreadArgs;

void* worker_thread(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;

    int idx = -1;
    if (strcmp(args->message.opcode,"MOVE") == 0)
    {
        pthread_mutex_lock(&args->hash_table->mtx);
        idx = insert_update_element(args->hash_table, args->message.drone_id, args->message.X, args->message.Y);
        pthread_mutex_unlock(&args->hash_table->mtx);
        printf("[Telemetry] The drone %s has moved to a position [%f,%f]. ACK sent\n", args->message.drone_id, args->message.X, args->message.Y);
        char ack_message[BUFLEN];
        snprintf(ack_message, BUFLEN, "ACK|%s|OK", args->message.drone_id);
        socklen_t addrlen = sizeof(struct sockaddr_in);
        if (sendto(args->server_sockfd, ack_message, strlen(ack_message), 0, (struct sockaddr*)&args->addr, addrlen)<0)
        {
            ERR("sendto");
        }
    }
    else if (strcmp(args->message.opcode,"INIT") == 0)
    {
        printf("Registered drone %s\n", args->message.drone_id);
        pthread_mutex_lock(&args->hash_table->mtx);
        idx = insert_update_element(args->hash_table, args->message.drone_id, args->message.X, args->message.Y);
        pthread_mutex_unlock(&args->hash_table->mtx);
    }
    else if (strcmp(args->message.opcode, "EXIT") == 0)
    {
        pthread_mutex_lock(&args->hash_table->mtx);
        delete_element(args->hash_table, args->message.drone_id);
        pthread_mutex_unlock(&args->hash_table->mtx);
        printf("Drone %s exited with code %d\n", args->message.drone_id, args->message.reason_code);
    }
    else if (strcmp(args->message.opcode, "PING")==0)
    {
        pthread_mutex_lock(&args->hash_table->mtx);
        idx = find_element(args->hash_table, args->message.drone_id);
        if (idx!=-1)
        {
            args->hash_table->slots[idx].last_seen = time(NULL);
        }
        pthread_mutex_unlock(&args->hash_table->mtx);
    }

    if (idx<0)
    {
        printf("[Swarm] Capacity reached. Rejecting %s\n", args->message.drone_id);
    }

    free(t_args);
    return NULL;
}

void* watchdog_thread(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    while (1)
    {
        usleep(5*1000000);
        pthread_mutex_lock(&args->hash_table->mtx);
        for (int i=0;i<MAX_DRONES;i++)
        {
            if (args->hash_table->slots[i].status == OCCUPIED)
            {
                if (time(NULL) - args->hash_table->slots[i].last_seen > 15)
                {
                    printf("== Drone %s signal lost. Evicted. ==\n", args->hash_table->slots[i].drone_id);
                    delete_element(args->hash_table, args->hash_table->slots[i].drone_id);
                }
            }
        }
        pthread_mutex_unlock(&args->hash_table->mtx);
    }
}

void create_thread(DroneTable* hash_table, DroneMessage drone_message, char* token, int server_sockfd, struct sockaddr_in addr)
{
    ThreadArgs* args = malloc(sizeof(ThreadArgs));
    args->hash_table = hash_table;
    args->message = drone_message;
    args->addr = addr;
    args->server_sockfd = server_sockfd;

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

int main(int argc, char** argv)
{
    if (argc!=2)
    {
        usage(argv[0]);
    }

    int port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM, BACKLOG);

    DroneTable hash_table;
    init_table(&hash_table);

    ThreadArgs* args = malloc(sizeof(ThreadArgs));
    args->hash_table = &hash_table;
    memset(&args->message,0,sizeof(DroneMessage));
    args->server_sockfd = server_sockfd;

    pthread_attr_t attr;

    if (pthread_attr_init(&attr))
        ERR("pthread_attr_init");
    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
        ERR("pthread_attr_setdetachstate");

    if (pthread_create(&args->thread_id, &attr, watchdog_thread, args))
    {
        ERR("pthread_create");
    }
    pthread_attr_destroy(&attr);

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

        DroneMessage drone_message = {0};

        int message_part = 0;
        char* saveptr;
        char* token = strtok_r(msg.buf, "|", &saveptr);
        while (token != NULL)
        {
            if (message_part == 0)
            {
                if (strlen(token)!=8)
                {
                    printf("Incorrect drone id %s\n", token);
                    break;
                }
                strcpy(drone_message.drone_id, token);
            }
            else if (message_part == 1)
            {
                if (strcmp(token, "INIT") == 0)
                {
                    strcpy(drone_message.opcode, token);
                    message_part = 3;

                    create_thread(&hash_table, drone_message, token, server_sockfd, addr);
                    break;
                }
                else if (strcmp(token, "PING") == 0)
                {
                    strcpy(drone_message.opcode, token);
                    message_part = 3;;

                    create_thread(&hash_table, drone_message, token, server_sockfd, addr);
                    break;
                }
                else if (strcmp(token, "MOVE") == 0)
                {
                    strcpy(drone_message.opcode, token);

                    float x,y;
                    if (sscanf(saveptr, "%f,%f", &x, &y)!=2)
                    {
                        fprintf(stderr,"Incorrect move coordinates formatting\n");
                        break;
                    }
                    drone_message.X = x;
                    drone_message.Y = y;
                    printf("%f\n", drone_message.X);

                    create_thread(&hash_table, drone_message, token, server_sockfd, addr);
                    message_part = 3;
                    break;
                }
                else if (strcmp(token, "EXIT") == 0)
                {
                    strcpy(drone_message.opcode, token);

                    int32_t er;
                    if (sscanf(saveptr, "%d", &er)!=1)
                    {
                        fprintf(stderr,"Incorrect exit code formatting\n");
                        break;
                    }
                    drone_message.reason_code = er;
                    message_part = 3;
                    create_thread(&hash_table, drone_message, token, server_sockfd, addr);
                    printf("%d\n", drone_message.reason_code);
                    break;
                }
                else
                {
                    printf("Incorrect opcode\n");
                    break;
                }
            }

            token = strtok_r(NULL,"|",&saveptr);
            message_part++;
        }

        if (message_part!=3)
        {
            char ip_str[64] = {0};
            inet_ntop(addr.sin_family, &addr.sin_addr, ip_str, sizeof(ip_str));
            printf("%s %s\n", drone_message.drone_id, drone_message.opcode);
            printf("[Error] Malformed packet from %s:%d\n", ip_str, ntohs(addr.sin_port));
            continue;
        }
        printf("[Parse] Received %s from Drone %s\n", drone_message.opcode, drone_message.drone_id);

    }

    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}