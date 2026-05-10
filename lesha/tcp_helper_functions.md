# Socket Helper Functions Notes

This document summarizes the helper functions used in the socket lab code.

Each function has this structure:

1. Function name
2. Brief summary
3. Function code

TCP helpers are listed first, then local Unix socket helpers, then common utility helpers.

---

# 1. make_tcp_socket

## Brief summary

Creates an IPv4 TCP stream socket.

This function only creates the socket. It does **not** bind it, connect it, or listen on it.

## Function code

```c
int make_tcp_socket(void)
{
    int sock;
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}
```

---

# 2. make_address

## Brief summary

Converts a hostname/address and port into a `struct sockaddr_in` IPv4 address.

This is mainly used by TCP clients before calling `connect()`.

The function uses `getaddrinfo()` and then copies the first returned address into a `struct sockaddr_in`.

## Function code

```c
struct sockaddr_in make_address(char *address, char *port)
{
    int ret;
    struct sockaddr_in addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    if ((ret = getaddrinfo(address, port, &hints, &result)))
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }
    addr = *(struct sockaddr_in *)(result->ai_addr);
    freeaddrinfo(result);
    return addr;
}
```

---

# 3. connect_tcp_socket

## Brief summary

Creates a TCP socket and connects it to a server address and port.

This is used on the **TCP client side**.

After this function returns, the returned socket is connected and can be used with `read()` / `write()` or `bulk_read()` / `bulk_write()`.

## Function code

```c
int connect_tcp_socket(char *name, char *port)
{
    struct sockaddr_in addr;
    int socketfd;
    socketfd = make_tcp_socket();
    addr = make_address(name, port);
    if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
    {
        ERR("connect");
    }
    return socketfd;
}
```

---

# 4. bind_tcp_socket

## Brief summary

Creates, binds, and listens on an IPv4 TCP socket.

This is used on the **TCP server side**.

The function creates a listening socket bound to the given port. It uses `INADDR_ANY`, so the server listens on all local interfaces.

## Function code

```c
int bind_tcp_socket(uint16_t port, int backlog_size)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_tcp_socket();
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(socketfd, backlog_size) < 0)
        ERR("listen");
    return socketfd;
}
```

---

# 5. make_local_socket

## Brief summary

Creates a Unix domain stream socket and fills a `sockaddr_un` structure with the given filesystem path.

This function only creates the socket and prepares the address. It does **not** bind or connect the socket.

## Function code

```c
int make_local_socket(char *name, struct sockaddr_un *addr)
{
    int socketfd;
    if ((socketfd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
        ERR("socket");
    memset(addr, 0, sizeof(struct sockaddr_un));
    addr->sun_family = AF_UNIX;
    strncpy(addr->sun_path, name, sizeof(addr->sun_path) - 1);
    return socketfd;
}
```

---

# 6. connect_local_socket

## Brief summary

Creates a Unix domain socket and connects it to a local server socket path.

This is used on the **local Unix socket client side**.

After this function returns, the returned socket is connected and can be used with `read()` / `write()` or `bulk_read()` / `bulk_write()`.

## Function code

```c
int connect_local_socket(char *name)
{
    struct sockaddr_un addr;
    int socketfd;
    socketfd = make_local_socket(name, &addr);
    if (connect(socketfd, (struct sockaddr *)&addr, SUN_LEN(&addr)) < 0)
    {
        ERR("connect");
    }
    return socketfd;
}
```

---

# 7. bind_local_socket

## Brief summary

Creates, binds, and listens on a Unix domain socket.

This is used on the **local Unix socket server side**.

The function binds the socket to a filesystem path, for example `/tmp/lab_socket`. Before binding, it removes an old socket file with `unlink()` if it exists.

## Function code

```c
int bind_local_socket(char *name, int backlog_size)
{
    struct sockaddr_un addr;
    int socketfd;
    if (unlink(name) < 0 && errno != ENOENT)
        ERR("unlink");
    socketfd = make_local_socket(name, &addr);
    if (bind(socketfd, (struct sockaddr *)&addr, SUN_LEN(&addr)) < 0)
        ERR("bind");
    if (listen(socketfd, backlog_size) < 0)
        ERR("listen");
    return socketfd;
}
```

---

# 8. add_new_client

## Brief summary

Accepts a new client connection from a listening socket.

This function is a wrapper around `accept()` with error handling.

It works for both TCP listening sockets and Unix domain listening sockets.

## Function code

```c
int add_new_client(int sfd)
{
    int nfd;
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
}
```

---
# 9. 32-bit values and byte order

- Socket sends raw bytes, not “integers” or “chars”.
- In this lab we use:

```c
int32_t data[5];
````

* One `int32_t` is:

```text
32 bits = 4 bytes
```

* Whole message size:

```text
5 * 4 = 20 bytes
```

* `htonl()` = host to network long.
* Use it **before sending** a 32-bit value.

```c
data[0] = htonl(10);
data[3] = htonl((int32_t)'+');
```

* `ntohl()` = network to host long.
* Use it **after receiving** a 32-bit value.

```c
int32_t op1 = ntohl(data[0]);
char op = (char)ntohl(data[3]);
```

* `htonl()` / `ntohl()` do not know what the value means.
* We know it from our protocol:

```text
data[0] = first operand
data[1] = second operand
data[2] = result
data[3] = operator
data[4] = status
```

---

# 10. bulk_read

## Brief summary

Reads up to exactly `count` bytes from a file descriptor.

This is useful for fixed-size protocols, where the server expects a fixed number of bytes, for example `sizeof(int32_t[5])`.

The function repeatedly calls `read()` until it reads the requested number of bytes, gets an error, or the peer closes the connection.

## Function code

```c
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
```

---

# 11. bulk_write

## Brief summary

Writes exactly `count` bytes to a file descriptor unless an error happens.

This is useful because a single `write()` call is not guaranteed to write all bytes.

The function repeatedly calls `write()` until all requested bytes are written or an error happens.

## Function code

```c
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
```

---

# 12. sethandler

## Brief summary

Installs a signal handler for a selected signal.

For example, it can be used to handle `SIGINT` from `Ctrl+C` or ignore `SIGPIPE`.

## Function code

```c
int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}
```

---

# 13. TEMP_FAILURE_RETRY

## Brief summary

Retries a system call if it fails because it was interrupted by a signal.

This is useful for system calls such as `read()`, `write()`, `accept()`, and `close()`.

## Function code

```c
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
```

---

# 14. ERR

## Brief summary

Prints an error message, prints the file name and line number, and terminates the program.

This macro is used for fatal errors.

## Function code

```c
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
```
