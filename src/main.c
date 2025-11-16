#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"
#include "threadpool.h"

static volatile int keep_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

int main(int argc, char **argv) {
    uint16_t port = 8080;
    size_t nworkers = 4;
    const char *docroot = "./www";

    if (argc >= 2) port = (uint16_t)atoi(argv[1]);
    if (argc >= 3) nworkers = (size_t)atoi(argv[2]);
    if (argc >= 4) docroot = argv[3];

    signal(SIGINT, sigint_handler);

    int server_fd = create_and_bind_listen(port, 128);
    if (server_fd < 0) return 1;

    printf("Listening on port %u with %zu workers, docroot=%s\n", (unsigned)port, nworkers, docroot);

    threadpool_t *tp = threadpool_create(nworkers, 1024, docroot);
    if (!tp) {
        fprintf(stderr, "Failed to create threadpool\n");
        close(server_fd);
        return 1;
    }

    while (keep_running) {
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        if (threadpool_submit(tp, client) != 0) {
            // couldn't submit, close
            close(client);
        }
    }

    printf("Shutting down...\n");
    close(server_fd);
    threadpool_destroy(tp);
    return 0;
}
