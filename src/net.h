// Small network helper API for creating a listening TCP socket.
//
// create_and_bind_listen:
//  - Create an IPv4 TCP listening socket bound to the given port.
//  - Sets SO_REUSEADDR to allow quick restarts.
//  - Binds to INADDR_ANY (all local interfaces) and starts listening with
//    the provided backlog.
//  - Parameters:
//      port    : port number to bind (host byte order). Use 0 to request
//                an ephemeral port assigned by the OS.
//      backlog : listen backlog passed to listen().
//  - Return:
//      >=0 : listening socket file descriptor (caller must close when done).
//      -1  : error (errno set by underlying syscalls).
//  - Notes:
//      - This helper uses IPv4 (AF_INET). For IPv6/multi-protocol support
//        extend the implementation in src/net.c.
//      - Caller is responsible for closing the returned FD and for any
//        further socket options (e.g., non-blocking mode).
#pragma once

#include <stdint.h>

int create_and_bind_listen(uint16_t port, int backlog);
