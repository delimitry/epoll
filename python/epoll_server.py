#!/usr/bin/env python
"""
Simple multiprocessing (and multithreaded) epoll HTTP server example

Use multiprocessed_loop() or threaded_loop() in main() function for
multiprocessing and multithreaded version correspondingly
"""

import errno
import fcntl
import multiprocessing
import os
import select
import socket
import sys
import threading


def perror(msg):
    """Print a message to stderr"""
    sys.stderr.write(msg + '\n')


def set_cloexec(fd):
    """Set the close-on-exec flag on the file descriptor"""
    fcntl.fcntl(fd, fcntl.F_SETFD, fcntl.FD_CLOEXEC)


EXIT_FAILURE = 1

# some backporting to Python2.x
if not hasattr(socket, 'SOCK_CLOEXEC'):
    socket.SOCK_CLOEXEC = 0o2000000

if not hasattr(socket, 'SOCK_NONBLOCK'):
    socket.SOCK_NONBLOCK = 0o4000

if not hasattr(select, 'EPOLL_CLOEXEC'):
    select.EPOLL_CLOEXEC = 0o2000000


MAX_EVENTS = 128
BUF_SIZE = 4096

HOST = '0.0.0.0'  # i.e. socket.INADDR_ANY
PORT = 8000

THREAD_NUMBER = 2
PROCESS_NUMBER = 2

USE_TCP_NODELAY = False
USE_TCP_QUICKACK = False

RESP_DATA = (
    'HTTP/1.1 200 OK\r\n'
    'Content-Length: 9\r\n'
    'Content-Type: json\r\n'
    '\r\n'
    '{"a":"b"}'
)


def set_listener_socket_opts(sock):
    """Set listener socket options"""
    enable = 1
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, enable)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, enable)
        if USE_TCP_NODELAY:
            # TCP_NODELAY with opt = 1 disables Nagle's algorithm
            # (i.e. send the data (partial frames) the moment you get,
            # regardless if you have enough frames for a full network packet)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, enable)
            sys.exit(EXIT_FAILURE)
        if USE_TCP_QUICKACK:
            # TCP_QUICKACK with opt = 1 means to send ACKs as early as possible than
            # delayed under some protocol level exchanging
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_QUICKACK, enable)
    except socket.error as ex:
        perror(ex)
        sys.exit(EXIT_FAILURE)


def add_fd_to_epoll(epoll, fd, events):
    """Add fd to epoll"""
    try:
        epoll.register(fd, events)
    except Exception as ex:
        perror('epoll_ctl(epdf, EPOLL_CTL_ADD, fd) failed: {} (fd={})'.format(str(ex), fd))
        sys.exit(EXIT_FAILURE)


def del_fd_from_epoll(epoll, fd):
    """Remove fd from epoll"""
    try:
        epoll.unregister(fd)
    except Exception as ex:
        perror('epoll_ctl(epfd, EPOLL_CTL_DEL, fd) failed: {} (fd={})'.format(str(ex), fd))
        sys.exit(EXIT_FAILURE)


def set_socket_opts(sock):
    """Set socket options"""
    enable = 1
    try:
        sock.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, enable)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, enable)
    except socket.error as ex:
        perror('setsockopt() failed: {} (fd={})'.format(str(ex), sock.fileno()))
        sys.exit(EXIT_FAILURE)


def handle_request(epoll, fd, sock):
    """Handle request"""
    try:
        data = os.read(fd, BUF_SIZE)
        if not data:
            del_fd_from_epoll(epoll, fd)
            sock.shutdown(socket.SHUT_RDWR)
            return
    except Exception:
        return
    sent_len = os.write(fd, RESP_DATA.encode())
    if sent_len == 0:
        sys.exit(EXIT_FAILURE)


def loop():
    """Main loop"""
    listen_sock = socket.socket(
        socket.AF_INET, socket.SOCK_STREAM | socket.SOCK_CLOEXEC | socket.SOCK_NONBLOCK, 0)
    listen_sock_fd = listen_sock.fileno()

    # set listener socket options
    set_listener_socket_opts(listen_sock)

    listen_sock.bind((HOST, PORT))
    listen_sock.listen(socket.SOMAXCONN)

    epoll = select.epoll(select.EPOLL_CLOEXEC)

    # add listen_sock_fd to epoll
    add_fd_to_epoll(epoll, listen_sock_fd, select.EPOLLIN)

    # if epoll_wait timeout = 0 (i.e. busy wait), response rate increses a bit,
    # but CPU usage increases as well, therefore consider using timeout = -1
    ep_timeout = -1

    connections = {}
    while 1:
        events = epoll.poll(ep_timeout, MAX_EVENTS)
        for fd, _ in events:
            if fd == listen_sock_fd:
                try:
                    conn, _ = listen_sock.accept()
                except socket.error as ex:
                    if ex.args[0] != errno.EAGAIN:
                        perror('listen_sock.accept() failed: {}'.format(str(ex)))
                        sys.exit(EXIT_FAILURE)
                conn_fd = conn.fileno()
                set_cloexec(conn_fd)  # SOCK_CLOEXEC
                conn.setblocking(0)  # O_NONBLOCK
                # set socket options
                set_socket_opts(conn)
                # add conn_fd to epoll
                add_fd_to_epoll(epoll, conn_fd, select.EPOLLIN | select.EPOLLET)
                connections[conn_fd] = conn
            else:
                handle_request(epoll, fd, connections[fd])

    # close fds
    epoll.close()
    listen_sock.close()



def multiprocessed_loop():
    """Multiprocessed loop"""
    processes = []
    for _ in range(PROCESS_NUMBER):
        process = multiprocessing.Process(target=loop)
        process.start()
        processes.append(process)
    for i in range(PROCESS_NUMBER):
        processes[i].join()


def threaded_loop():
    """Threaded loop"""
    try:
        threads = []
        for _ in range(THREAD_NUMBER):
            thread = threading.Thread(target=loop)
            thread.daemon = True
            threads.append(thread)
            thread.start()
        while 1:
            import time
            time.sleep(60)
        for i in range(THREAD_NUMBER):
            threads[i].join()
    except KeyboardInterrupt:
        print('Stopped by Ctrl+C')


def main():
    """Main"""
    print('Server listening on {}:{}'.format(HOST, PORT))
    # threaded_loop()
    multiprocessed_loop()


if __name__ == '__main__':
    main()
