#include "l8_common.h"

#define SPELL_TYPES 3
const char* spell_names[SPELL_TYPES] = {"Divination", "Summon Elemental", "Fireball"};
#define BOARD_SIZE 8
#define BACKLOG 16

#define MAX_QUEUE 10
#define THREAD_COUNT 3
#define FAMILIAR_DELAY 100

#define MAX_CLIENTS 2
#define MAX_NAME_LENGTH 14

#define BUFLEN 16

// typedef struct __attribute__((__packed__)) packed
// {
//     char c1;
//     int i1;
//     char c2;
//     int i2;
// };
// typedef struct not_packed
// {
//     char c1;
//     int i1;
//     char c2;
//     int i2;
// };

typedef struct Message
{
    char buf[BUFLEN+1];
}Message;

typedef struct __attribute__((__packed__))
{
    char type;
    char padding;
    uint16_t spell;
    uint16_t X;
    uint16_t Y;
}CastMessage;

typedef struct __attribute__((__packed__))
{
    char type;
}QuitMessage;

typedef struct __attribute__((__packed__))
{
    char type;
    char padding;
    char name[MAX_NAME_LENGTH];
}LoginMessage;

typedef struct Player
{
    struct sockaddr_in addr;
    int pebbles;
    int logged;
    pthread_mutex_t player_mutex;
    char name[MAX_NAME_LENGTH+1];
}Player;

typedef struct State
{
    Player players[2];
}State;

typedef struct CircularBuffer
{
    int head;
    int tail;
    int count;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    // pthread_cond_t not_full;
    CastMessage data[MAX_QUEUE];
}CircularBuffer;

typedef struct ThreadArgs
{
    int* work;
    int server_sockfd;
    pthread_t thread_id;
    CircularBuffer* circular_buffer;
    State* state;
}ThreadArgs;

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

int register_player(State* state, Player player)
{
    if (state->players[0].logged == 1 && state->players[1].logged == 1)
    {
        printf("All registered\n");
        return 0;
    }

    if ((state->players[0].logged && memcmp(&state->players[0].addr, &player.addr, sizeof(struct sockaddr_in))==0) || (state->players[1].logged && memcmp(&state->players[1].addr, &player.addr, sizeof(struct sockaddr_in))==0))
    {
        printf("Rejecting!\n");
        return 0;
    }

    if (!state->players[0].logged)
    {
        state->players[0] = player;
        state->players[0].logged = 1;
    }
    else if (!state->players[1].logged)
    {
        state->players[1] = player;
        state->players[1].logged = 1;
        printf("Game is started!\n");
    }
    return 1;
}

int is_logged(State* state, struct sockaddr_in addr)
{
    if (memcmp(&state->players[0].addr, &addr, sizeof(struct sockaddr_in)) != 0 && memcmp(&state->players[1].addr, &addr, sizeof(struct sockaddr_in))!=0)
    {
        printf("Received message from unknown player\n");
        return 0;
    }
    return 1;
}

int get_logged_index(State* state, struct sockaddr_in addr)
{
    if (memcmp(&state->players[0].addr, &addr, sizeof(struct sockaddr_in)) == 0)
    {
        return 0;
    }
    else if (memcmp(&state->players[1].addr, &addr, sizeof(struct sockaddr_in))==0)
    {
        return 1;
    }
    return 1;
}

void* thread_work(void* t_args)
{
    ThreadArgs* args = (ThreadArgs*)t_args;
    while (1)
    {
        CastMessage current_message;
        pthread_mutex_lock(&args->circular_buffer->mtx);
        while (args->circular_buffer->count == 0 && *args->work)
        {
            pthread_cond_wait(&args->circular_buffer->not_empty,&args->circular_buffer->mtx);
        }
        if (args->circular_buffer->count==0 && !(*args->work))
        {
            break;
        }
        current_message = args->circular_buffer->data[args->circular_buffer->head];
        args->circular_buffer->head = (args->circular_buffer->head + 1)%MAX_QUEUE;
        args->circular_buffer->count--;
        pthread_mutex_unlock(&args->circular_buffer->mtx);

        pthread_mutex_lock(&args->state->players[(int)current_message.padding].player_mutex);
        char* name = args->state->players[(int)current_message.padding].name;
        int pebble_amount = 1;
        if (current_message.spell == 1)
        {
            pebble_amount = 3;
        }
        else if (current_message.spell == 2)
        {
            pebble_amount = 4;
        }

        if (args->state->players[(int)current_message.padding].pebbles<pebble_amount)
        {
            printf("[tee hee] Not enough pebbles, %s!\n", name);
            pthread_mutex_unlock(&args->state->players[(int)current_message.padding].player_mutex);
        }
        else
        {
            args->state->players[(int)current_message.padding].pebbles -= pebble_amount;
            printf("[Cast] %s casts %s onto %d, %d\n",name,spell_names[current_message.spell], current_message.X, current_message.Y);
            pthread_mutex_unlock(&args->state->players[(int)current_message.padding].player_mutex);
        }

        usleep(FAMILIAR_DELAY*1000);
    }
}

int main(int argc, char** argv)
{
    // printf("sizeof(struct packed) == %d\n", sizeof(struct packed));
    // printf("sizeof(struct not_packed) == %d\n", sizeof(struct not_packed));

    if (argc!=2)
    {
        usage(argv[0]);
    }
    int port = atoi(argv[1]);
    int server_sockfd = bind_inet_socket(port, SOCK_DGRAM, BACKLOG);

    CircularBuffer circular_buffer;
    circular_buffer.count = 0;
    circular_buffer.tail = 0;
    circular_buffer.head = 0;
    pthread_mutex_init(&circular_buffer.mtx, NULL);
    pthread_cond_init(&circular_buffer.not_empty, NULL);
    for (int i=0;i<MAX_QUEUE;i++)
    {
        memset(&circular_buffer.data[i],0,sizeof(CastMessage));
    }

    State state = {0};
    for (int i=0;i<2;i++)
    {
        state.players[i].logged = 0;
        state.players[i].pebbles = 10;
        pthread_mutex_init(&state.players[i].player_mutex,NULL);
    }

    int work = 1;
    ThreadArgs thread_args[THREAD_COUNT];
    for (int i=0;i<THREAD_COUNT;i++)
    {
        thread_args[i].work = &work;
        thread_args[i].state = &state;
        thread_args[i].circular_buffer = &circular_buffer;
        thread_args[i].server_sockfd = server_sockfd;
        pthread_create(&thread_args[i].thread_id, NULL, thread_work, &thread_args[i]);
    }

    for (;;)
    {
        Message msg = {0};
        struct sockaddr_in addr;
        socklen_t len = sizeof(struct sockaddr_in);
        int bytes_received = recvfrom(server_sockfd,msg.buf ,BUFLEN, 0, (struct sockaddr*)&addr, &len);
        if (bytes_received<0)
        {
            perror("recvfrom");
        }
        if (bytes_received > 16)
        {
            printf("incorrect message length\n");
            continue;
        }
        msg.buf[bytes_received] = '\0';
        if (msg.buf[0] == 'l')
        {
            LoginMessage* login_message = (LoginMessage*)msg.buf;

            Player new_player = {0};
            new_player.addr = addr;
            size_t name_len = strlen(login_message->name);
            memcpy(new_player.name, login_message->name, name_len);
            new_player.name[name_len] = '\0';
            new_player.pebbles = 10;
            int ret = register_player(&state, new_player);
            if (ret)
            {
                printf("[Login] Welcome, %s\n", login_message->name);
            }
        }
        else if (msg.buf[0] == 'c')
        {
            if (state.players[0].logged == 0 || state.players[1].logged == 0)
            {
                continue;
            }
            if (!is_logged(&state, addr))
            {
                printf("Rejecting spell from unknown player\n");
                continue;
            }

            CastMessage* cast_message = (CastMessage*)msg.buf;
            cast_message->spell = ntohs(cast_message->spell);
            cast_message->X = ntohs(cast_message->X);
            cast_message->Y = ntohs(cast_message->Y);
            cast_message->padding = (char)get_logged_index(&state, addr);
            if (cast_message->spell >= SPELL_TYPES)
            {
                printf("Incorrect spell\n");
                continue;
            }
            if (cast_message->X >= BOARD_SIZE || cast_message->Y >= BOARD_SIZE)
            {
                printf("Incorrect coordinates\n");
                continue;
            }

            pthread_mutex_lock(&circular_buffer.mtx);
            if (circular_buffer.count == MAX_QUEUE)
            {
                printf("No room for the command\n");
                pthread_mutex_unlock(&circular_buffer.mtx);
                continue;
            }
            memcpy(&circular_buffer.data[circular_buffer.tail], cast_message, sizeof(CastMessage));
            circular_buffer.tail = (circular_buffer.tail + 1) % MAX_QUEUE;
            circular_buffer.count++;
            pthread_cond_signal(&circular_buffer.not_empty);
            pthread_mutex_unlock(&circular_buffer.mtx);
        }
        else if (msg.buf[0] == 'q')
        {
            if (state.players[0].logged == 0 || state.players[1].logged == 0)
            {
                continue;
            }
            if (!is_logged(&state, addr))
            {
                printf("Rejecting spell from unknown player\n");
                continue;
            }
            int player_idx = get_logged_index(&state, addr);
            //QuitMessage* quit_message = (QuitMessage*)msg.buf;
            int opponent_idx = 0;
            if (player_idx == 0)
                opponent_idx = 1;
            printf("[Quit] %s quit. Goodbye!\n", state.players[player_idx].name);
            printf("-= Congratulations, %s, you win! =-\n", state.players[opponent_idx].name);
            work = 0;
            pthread_cond_broadcast(&circular_buffer.not_empty);
            break;
        }
        else
        {
            printf("incorrect messsage type\n");
            continue;
        }
    }
    for (int i=0;i<THREAD_COUNT;i++)
    {
        pthread_join(thread_args[i].thread_id, NULL);
    }
    for (int i=0;i<2;i++)
    {
        pthread_mutex_destroy(&state.players[i].player_mutex);
    }
    pthread_mutex_destroy(&circular_buffer.mtx);
    pthread_cond_destroy(&circular_buffer.not_empty);
    if (close(server_sockfd)<0)
    {
        ERR("close");
    }
    return 0;
}