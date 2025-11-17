#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* create_and_bind_listen:
   - create a TCP listening socket on the given port
   - set SO_REUSEADDR so restarts can bind quickly
   - bind to INADDR_ANY and start listening with the provided backlog
   - returns the listening file descriptor on success or -1 on error */
int create_and_bind_listen(uint16_t port, int backlog) {
    /* create a TCP socket (IPv4) */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    /* allow quick reuse of the address/port after restart */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    /* prepare sockaddr struct for bind() */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;           /* IPv4 */
    addr.sin_addr.s_addr = INADDR_ANY;   /* bind to all interfaces */
    addr.sin_port = htons(port);         /* network byte order port */

    /* bind the socket to the requested port on all interfaces */
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    /* start listening with the requested backlog */
    if (listen(server_fd, backlog) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    /* success: return listening socket fd */
    return server_fd;
}
