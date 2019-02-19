# epoll

[![MIT license](http://img.shields.io/badge/license-MIT-brightgreen.svg)](https://github.com/delimitry/compressed_rtf/blob/master/LICENSE)

Epoll examples and benchmarks

Description:
------------

Simple epoll based HTTP servers with constant JSON response to any request.  
It's possible to make and run examples on your system as well as in Docker.  

To run C version in the Docker container use the next commands:
```
# cd c/
# docker build -f Dockerfile_C -t epoll .
# docker run --net=host -p 8000:8000 -t epoll
```
After that you'll see the next output:
```
Successfully tagged epoll:latest
Server listening on 0.0.0.0:8000
```
For benchmarks [wrk](https://github.com/wg/wrk) tool was used. 

NB: Even with `--net=host` key in docker run command the performance of examples on a host system is better than in the Docker.

Results:
--------

To test I've used the next `wrk` confuguration: 2 threads, 100 clients and 10 seconds.  
Tested epoll servers were run in Docker, whereas `wrk` was on the host system (Ubuntu 16.04).

```
C (threaded_loop (2 threads)):
$ wrk -t2 -c100 -d10s http://0.0.0.0:8000/
Requests/sec:  77176.69

=============================================

CPython3 (multiprocessed_loop (2 processes)):
$ wrk -t2 -c100 -d10s http://0.0.0.0:8000/
Requests/sec:  48215.67

CPython3 (threaded_loop (2 threads)):
$ wrk -t2 -c100 -d10s http://0.0.0.0:8000/
Requests/sec:  29849.85

=============================================

PyPy3 (multiprocessed_loop (2 processes)):
$ wrk -t2 -c100 -d10s http://0.0.0.0:8000/
Requests/sec:  76399.70

PyPy3 (threaded_loop (2 threads)):
$ wrk -t2 -c100 -d10s http://0.0.0.0:8000/
Requests/sec:  74526.86

=============================================

Go (ConcurrentLoop (2 threads)):

$ wrk -t2 -c100 -d10s http://0.0.0.0:8000/
Requests/sec:  76362.73
```

It's interesting that PyPy3 performance is close to C.

License:
--------
Released under [The MIT License](https://github.com/delimitry/epoll/blob/master/LICENSE).
