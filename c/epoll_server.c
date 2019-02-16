/*
 * Simple multithreaded epoll HTTP server example
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);

#define MAX_EVENTS 128
#define BUF_SIZE 4096

#define HOST INADDR_ANY // INADDR_ANY = 0.0.0.0
#define PORT 1337

#define THREAD_NUMBER 2

const char *resp_data = \
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 9\r\n"
    "Content-Type: json\r\n"
    "\r\n"
    "{\"a\":\"b\"}";

// #define USE_TCP_NODELAY
// #define USE_TCP_QUICKACK

/* Set listener socket options */
void set_listener_socket_opts(int sockfd)
{
    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEPORT) failed");
        exit(EXIT_FAILURE);
    }
#ifdef USE_TCP_NODELAY
    // TCP_NODELAY with opt = 1 disables Nagle's algorithm
    // (i.e. send the data (partial frames) the moment you get,
    // regardless if you have enough frames for a full network packet)
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0) {
        perror("setsockopt(TCP_NODELAY) failed");
        exit(EXIT_FAILURE);
    }
#endif
#ifdef USE_TCP_QUICKACK
    // TCP_QUICKACK with opt = 1 means to send ACKs as early as possible than
    // delayed under some protocol level exchanging
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &enable, sizeof(int)) < 0) {
        perror("setsockopt(TCP_QUICKACK) failed");
        exit(EXIT_FAILURE);
    }
#endif
}

/* Add fd to epoll */
void add_fd_to_epoll(int epfd, int fd, uint32_t events)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl(epdf, EPOLL_CTL_ADD, fd) failed");
        exit(EXIT_FAILURE);
    }
}

/* Remove fd from epoll */
void del_fd_from_epoll(int epfd, int fd)
{
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl(epfd, EPOLL_CTL_DEL, fd) failed");
        exit(EXIT_FAILURE);
    }
}

/* Set socket options */
void set_socket_opts(int sockfd)
{
    int enable = 1;
    setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &enable, sizeof(int));
    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(int));
}

/* Handle request */
void handle_request(int fd)
{
    char buf[BUF_SIZE];
    int data_read;
    int data_sent;
    data_read = read(fd, buf, BUF_SIZE);
    if (data_read <= 0) {
        close(fd);
        return;
    }
    data_sent = write(fd, resp_data, strlen(resp_data));
    if (data_sent == -1) {
        if (errno != EAGAIN) {
            perror("write(fd) failed");
            exit(EXIT_FAILURE);
        }
    }
}

/* Main loop */
void *loop()
{
    int listen_sock_fd;
    struct sockaddr_in addr;
    int epoll_fd;
    struct epoll_event events[MAX_EVENTS];
    int fds_num;
    int conn_fd;
    // if epoll_wait timeout = 0 (i.e. busy wait), response rate increses a bit,
    // but CPU usage increases as well, therefore consider using timeout = -1
    int ep_timeout = -1;

    // create socket
    listen_sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listen_sock_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // set listener socket options
    set_listener_socket_opts(listen_sock_fd);

    // prepare address
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(HOST);
    addr.sin_port = htons(PORT);

    if (bind(listen_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(listen_sock_fd, SOMAXCONN) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        perror("epoll_create failed");
        exit(EXIT_FAILURE);
    }
    // add listen_sock_fd to epoll
    add_fd_to_epoll(epoll_fd, listen_sock_fd, EPOLLIN);

    while (1) {
        fds_num = epoll_wait(epoll_fd, events, MAX_EVENTS, ep_timeout);
        if (fds_num == -1) {
            perror("epoll_wait failed");
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < fds_num; i++) {
            if (events[i].data.fd == listen_sock_fd) {
                conn_fd = accept4(listen_sock_fd, NULL, NULL, SOCK_CLOEXEC | O_NONBLOCK);
                if (conn_fd == -1) {
                    if (errno != EAGAIN) {
                        perror("accept4(listen_sock_fd) failed");
                        exit(EXIT_FAILURE);
                    }
                }
                // set socket options
                set_socket_opts(conn_fd);
                // add conn_fd to epoll
                add_fd_to_epoll(epoll_fd, conn_fd, EPOLLIN | EPOLLET);
            } else {
                handle_request(events[i].data.fd);
            }
        }
    }
    // close fds
    close(epoll_fd);
    close(listen_sock_fd);
    return NULL;
}

/* Run main loop in threads */
void threaded_loop()
{
    pthread_t threads[THREAD_NUMBER];
    // create threads
    for (int i = 0; i < THREAD_NUMBER; i++) {
        if (pthread_create(&threads[i], NULL, loop, NULL) != 0) {
            printf("pthread_create(%d) failed", i);
            exit(EXIT_FAILURE);
        }
    }
    // join threads
    for (int i = 0; i < THREAD_NUMBER; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "pthread_join(%d) failed", i);
            exit(EXIT_FAILURE);
        }
    }
}

/* Main */
int main(int argc, char const *argv[])
{
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = htonl(HOST);
    printf("Server listening on %s:%d\n", inet_ntoa(addr.sin_addr), PORT);
    threaded_loop();
    return 0;
}
