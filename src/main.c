#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"         /* create_and_bind_listen() */
#include "threadpool.h"  /* threadpool API */

static volatile int keep_running = 1; /* flag set by signal handler to stop main loop */

/* SIGINT handler: set flag to request graceful shutdown.
   Avoids doing non-reentrant work inside the handler. */
static void sigint_handler(int sig) {
    (void)sig;         /* unused param */
    keep_running = 0;  /* main loop will observe this and exit */
}

int main(int argc, char **argv) {
    /* defaults: port, number of worker threads, and document root */
    uint16_t port = 8080;
    size_t nworkers = 4;
    const char *docroot = "./www";

    /* simple CLI parsing: argv[1]=port, argv[2]=num_workers, argv[3]=docroot */
    if (argc >= 2) port = (uint16_t)atoi(argv[1]);       /* convert port */
    if (argc >= 3) nworkers = (size_t)atoi(argv[2]);     /* convert worker count */
    if (argc >= 4) docroot = argv[3];                   /* use provided docroot */

    /* install SIGINT handler so Ctrl-C triggers graceful shutdown */
    signal(SIGINT, sigint_handler);

    /* create, bind and listen on the server socket (backlog 128) */
    int server_fd = create_and_bind_listen(port, 128);
    if (server_fd < 0) return 1; /* cannot continue if we can't listen */

    printf("Listening on port %u with %zu workers, docroot=%s\n",
           (unsigned)port, nworkers, docroot);

    /* create thread pool: nworkers threads, task queue capacity 1024,
       and pass docroot as the context/arg used by worker tasks */
    threadpool_t *tp = threadpool_create(nworkers, 1024, docroot);
    if (!tp) {
        fprintf(stderr, "Failed to create threadpool\n");
        close(server_fd);
        return 1;
    }

    /* main accept loop: accept connections and submit client fds to the pool */
    while (keep_running) {
        int client = accept(server_fd, NULL, NULL); /* accept blocks until a client connects */
        if (client < 0) {
            if (errno == EINTR) continue; /* interrupted by signal; check keep_running again */
            perror("accept");
            break;
        }
        /* submit client fd to thread pool; on failure, close the socket immediately */
        if (threadpool_submit(tp, client) != 0) {
            /* submission failed (queue full or shutting down) */
            close(client);
        }
    }

    /* shutdown sequence: stop accepting, close server socket, destroy pool (joins workers) */
    printf("Shutting down...\n");
    close(server_fd);
    threadpool_destroy(tp);
    return 0;
}
