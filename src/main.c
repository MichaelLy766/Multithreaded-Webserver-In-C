#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>   /* for PATH_MAX */

#include "net.h"
#include "threadpool.h"
#include "scheduler.h"

static volatile sig_atomic_t stop = 0;
static void sigint_handler(int sig) { (void)sig; stop = 1; }

/* monotonic ms */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv) {
    unsigned short port = 8080;
    size_t nworkers = 4;
    const char *docroot = "./www";
    size_t queue_capacity = 1024;

    if (argc >= 2) port = (unsigned short)atoi(argv[1]);
    if (argc >= 3) nworkers = (size_t)atoi(argv[2]);
    if (argc >= 4) docroot = argv[3];

    signal(SIGINT, sigint_handler);

    int server_fd = create_and_bind_listen(port, 128);
    if (server_fd < 0) return 1;

    printf("Listening on port %u with %zu workers, docroot=%s\n", port, nworkers, docroot);

    threadpool_t *tp = threadpool_create(nworkers, queue_capacity, docroot);
    if (!tp) {
        fprintf(stderr, "failed to create threadpool\n");
        close(server_fd);
        return 1;
    }

    /* switch to SJF scheduler */
    scheduler_t *sjf = scheduler_sjf_create(queue_capacity);
    if (sjf) {
        threadpool_set_scheduler(tp, sjf);
        printf("Using SJF scheduler\n");
    } else {
        printf("Using FIFO scheduler (sjf create failed)\n");
    }

    while (!stop) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        /* attempt to peek request headers to estimate file size (SJF est_cost) */
        long est = 0;
        char peek[4096];
        ssize_t n = recv(client_fd, peek, sizeof(peek) - 1, MSG_PEEK);
        if (n > 0) {
            peek[n] = '\0';
            char method[16], path[1024], ver[16] = "";
            if (sscanf(peek, "%15s %1023s %15s", method, path, ver) >= 2) {
                /* basic sanitize: reject .. in path for stat attempt */
                if (strstr(path, "..") == NULL) {
                    /* map "/" -> /index.html */
                    char file_path[PATH_MAX];
                    if (path[0] == '\0' || strcmp(path, "/") == 0) {
                        snprintf(file_path, sizeof(file_path), "%s/index.html", docroot);
                    } else {
                        const char *p = path[0] == '/' ? path + 1 : path;
                        snprintf(file_path, sizeof(file_path), "%s/%s", docroot, p);
                    }
                    struct stat st;
                    if (stat(file_path, &st) == 0) est = (long)st.st_size;
                }
            }
        }

        job_t j = { .client_fd = client_fd,
                    .est_cost = est,
                    .priority = 0,
                    .arrival_ms = now_ms() };

        /* log estimated cost for debugging/verification */
        printf("submit: fd=%d est=%ld\n", client_fd, est);
        fflush(stdout);

        if (threadpool_submit_job(tp, j) != 0) {
            close(client_fd);
        }
    }

    threadpool_destroy(tp);
    close(server_fd);
    return 0;
}
