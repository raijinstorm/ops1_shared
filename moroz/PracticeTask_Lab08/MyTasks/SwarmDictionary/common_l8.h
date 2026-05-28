#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <semaphore.h>

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

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void ms_sleep(unsigned int milli)
{
    struct timespec ts = {milli / 1000, (milli % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

int bind_inet_socket(uint16_t port, int type, int backlog)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(PF_INET, type);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (SOCK_STREAM == type)
        if (listen(socketfd, backlog) < 0)
            ERR("listen");
    return socketfd;
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (0 == c)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}



//change this to my needs
#define MAX_ITEMS 128

//calculate the number of bytes needed
#define BITMAP_SIZE ((MAX_ITEMS / 8) + (MAX_ITEMS % 8 != 0))

typedef struct {
    uint8_t bits[BITMAP_SIZE];
} BitMap;

//initialise
void init_bitmap(BitMap* bmp) {
    memset(bmp->bits, 0, sizeof(bmp->bits));
}

//turning a bit on using a "mask" 1<<bit_offset
void set_bit(BitMap* bmp, int index) {
    if (index < 0 || index >= MAX_ITEMS) return;

    int byte_idx = index / 8;
    int bit_offset = index % 8;

    bmp->bits[byte_idx] |= (1 << bit_offset);
}

//turning a bit off
void clear_bit(BitMap* bmp, int index) {
    if (index < 0 || index >= MAX_ITEMS) return;

    int byte_idx = index / 8;
    int bit_offset = index % 8;


    bmp->bits[byte_idx] &= ~(1 << bit_offset);
}

//check if a bit is ON
bool check_bit(BitMap* bmp, int index) {
    if (index < 0 || index >= MAX_ITEMS) return false;

    int byte_idx = index / 8;
    int bit_offset = index % 8;

    //use Bitwise AND (&) to isolate the bit and check if it's non-zero
    return (bmp->bits[byte_idx] & (1 << bit_offset)) != 0;
}