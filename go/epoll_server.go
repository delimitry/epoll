// Simple concurrent epoll HTTP server example

package main

import (
	"fmt"
	"net"
	"os"
	"sync"
	"syscall"
)

const (
	EXIT_FAILURE = 1

	MAX_EVENTS = 128
	BUF_SIZE   = 4096

	HOST = "0.0.0.0"
	PORT = 8000

	GOROUTINE_COUNT = 2

	RespData = "HTTP/1.1 200 OK\r\n" +
		"Content-Length: 9\r\n" +
		"Content-Type: json\r\n" +
		"\r\n" +
		"{\"a\":\"b\"}"

	SO_REUSEPORT = 15

	USE_TCP_NODELAY  = false
	USE_TCP_QUICKACK = false
)

// Set listener socket options
func SetListenerSocketOpts(fd int) {
	if err := syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
	if err := syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, SO_REUSEPORT, 1); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
	if USE_TCP_NODELAY {
		// TCP_NODELAY with opt = 1 disables Nagle's algorithm
		// (i.e. send the data (partial frames) the moment you get,
		// regardless if you have enough frames for a full network packet)
		if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, syscall.TCP_NODELAY, 1); err != nil {
			fmt.Println(err)
			os.Exit(EXIT_FAILURE)
		}
	}
	if USE_TCP_QUICKACK {
		// TCP_QUICKACK with opt = 1 means to send ACKs as early as possible than
		// delayed under some protocol level exchanging
		if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, syscall.TCP_QUICKACK, 1); err != nil {
			fmt.Println(err)
			os.Exit(EXIT_FAILURE)
		}
	}

}

// Add fd to epoll
func AddFdToEpoll(epfd int, fd int, events int) {
	ev := syscall.EpollEvent{
		Events: uint32(events),
		Fd:     int32(fd),
	}
	if err := syscall.EpollCtl(epfd, syscall.EPOLL_CTL_ADD, fd, &ev); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
}

// Remove fd from epoll
func DelFdToEpoll(epfd int, fd int, events int) {
	if err := syscall.EpollCtl(epfd, syscall.EPOLL_CTL_DEL, fd, nil); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
}

// Set socket options
func SetSocketOpts(fd int) {
	if err := syscall.SetsockoptInt(fd, syscall.SOL_TCP, syscall.TCP_NODELAY, 1); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
	if err := syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_KEEPALIVE, 1); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
}

// Handle request
func HandleRequest(fd int) {
	var buf [BUF_SIZE]byte
	n, _ := syscall.Read(fd, buf[:])
	// if err != nil {
	// 	fmt.Printf("read failed: %s\n", err)
	// 	//os.Exit(EXIT_FAILURE)
	// }
	if n <= 0 {
		syscall.Close(fd)
		return
	}
	if _, err := syscall.Write(fd, []byte(RespData)); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
}

// Main loop
func loop() {
	var events [MAX_EVENTS]syscall.EpollEvent
	// if epoll_wait timeout = 0 (i.e. busy wait), response rate increses a bit,
	// but CPU usage increases as well, therefore consider using timeout = -1
	var timeout int = -1

	listenSockFd, err := syscall.Socket(syscall.AF_INET, syscall.SOCK_STREAM|syscall.SOCK_CLOEXEC|syscall.SOCK_NONBLOCK, 0)
	if err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
	defer syscall.Close(listenSockFd)

	// set listener socket options
	SetListenerSocketOpts(listenSockFd)

	// prepare address
	ip4Addr := net.ParseIP(HOST).To4()
	addr := syscall.SockaddrInet4{
		Addr: [4]byte{ip4Addr[0], ip4Addr[1], ip4Addr[2], ip4Addr[3]},
		Port: PORT,
	}

	if err := syscall.Bind(listenSockFd, &addr); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
	if err := syscall.Listen(listenSockFd, syscall.SOMAXCONN); err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
	epollFd, err := syscall.EpollCreate1(syscall.EPOLL_CLOEXEC)
	if err != nil {
		fmt.Println(err)
		os.Exit(EXIT_FAILURE)
	}
	defer syscall.Close(epollFd)

	// add listen_sock_fd to epoll
	AddFdToEpoll(epollFd, listenSockFd, syscall.EPOLLIN)

	for {
		fdsNum, err := syscall.EpollWait(epollFd, events[:], timeout)
		if err != nil {
			fmt.Println(err)
			os.Exit(EXIT_FAILURE)
		}
		for index := 0; index < fdsNum; index++ {
			if int(events[index].Fd) == listenSockFd {
				connFd, _, err := syscall.Accept4(listenSockFd, syscall.SOCK_CLOEXEC|syscall.O_NONBLOCK)
				if err != nil {
					fmt.Println(err)
					os.Exit(EXIT_FAILURE)
				}
				// set socket options
				SetSocketOpts(connFd)
				// add conn_fd to epoll
				AddFdToEpoll(epollFd, connFd, syscall.EPOLLIN|syscall.EPOLLET)
			} else {
				HandleRequest(int(events[index].Fd))
			}
		}
	}
}

// Concurrent loop
func ConcurrentLoop() {
	var wg sync.WaitGroup
	for index := 0; index < GOROUTINE_COUNT; index++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			loop()
		}()
	}
	wg.Wait()
}

// Main
func main() {
	fmt.Printf("Server listening on %s:%d\n", HOST, PORT)
	ConcurrentLoop()
}
