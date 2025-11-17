#include "http.h"

// standard headers: errno for errors, fcntl/open, stdio/stdlib/string for helpers
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h> // network types (not directly used here, kept for context)
#include <stdarg.h>     // varargs (not used but harmless)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h> // Linux sendfile
#include <sys/stat.h>     // stat()
#include <sys/types.h>
#include <sys/time.h> /* for struct timeval, SO_RCVTIMEO */
#include <unistd.h>       // read/write/close

#define REQ_BUF 8192 /* buffer size for reading the request */

/* write_all: repeatedly call write() until the whole buffer is sent.
   Handles short writes and EINTR. Returns total bytes written or -1 on error. */
static ssize_t write_all(int fd, const void *buf, size_t count) {
    const char *p = buf;      /* current pointer into buffer */
    size_t left = count;      /* bytes left to write */
    while (left > 0) {
        ssize_t n = write(fd, p, left); /* attempt to write remaining bytes */
        if (n < 0) {
            if (errno == EINTR) continue; /* retry on interrupt */
            return -1;                    /* other errors propagate */
        }
        p += n;    /* advance pointer by bytes written */
        left -= n; /* decrease remaining count */
    }
    return count; /* success: return original requested count */
}

/* sanitize_path: simple path traversal protection.
   Reject any path containing "..". This is minimal and not exhaustive. */
static int sanitize_path(const char *path) {
    if (strstr(path, "..") != NULL) return 0; /* found parent-traversal component */
    return 1;                                  /* otherwise accept */
}

/* configurable keep-alive limits */
#define MAX_KEEPALIVE_REQUESTS 8
#define IDLE_TIMEOUT_SECONDS 60

/* handle_client: handle up to MAX_KEEPALIVE_REQUESTS requests on client_fd.
   Enforces an idle timeout via SO_RCVTIMEO and honors Connection headers
   and HTTP version semantics. Returns 0 on normal completion, -1 on error. */
int handle_client(int client_fd, const char *docroot) {
    /* set a recv timeout so idle clients don't block workers forever */
    struct timeval tv;
    tv.tv_sec = IDLE_TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    printf("conn %d: opened\n", client_fd);
    fflush(stdout);

    char buf[REQ_BUF];
    int served = 0;

    while (served < MAX_KEEPALIVE_REQUESTS) {
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n == 0) {
            printf("conn %d: client closed connection\n", client_fd);
            fflush(stdout);
            return 0;
        }
        if (n < 0) {
            if (errno == EINTR) continue; /* retry on interrupt */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* timeout/idle: close connection gracefully */
                printf("conn %d: idle timeout after %d seconds, closing\n",
                       client_fd, IDLE_TIMEOUT_SECONDS);
                fflush(stdout);
                return 0;
            }
            perror("read");
            printf("conn %d: read error, closing\n", client_fd);
            fflush(stdout);
            return -1; /* other read error */
        }
        buf[n] = '\0';

        /* parse request line with HTTP version */
        char method[16];
        char path[1024];
        char version[16] = "";
        if (sscanf(buf, "%15s %1023s %15s", method, path, version) < 2) {
            const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            write_all(client_fd, resp, strlen(resp));
            printf("conn %d: malformed request, closing\n", client_fd);
            fflush(stdout);
            return -1;
        }

        printf("conn %d: serving request #%d: %s %s\n", client_fd, served + 1, method, path);
        fflush(stdout);

        /* determine connection semantics: default depends on version */
        int should_close = 0;
        /* HTTP/1.0 closes by default unless Connection: keep-alive present */
        if (strncmp(version, "HTTP/1.0", 8) == 0) should_close = 1;
        /* scan headers for explicit Connection: close or keep-alive */
        if (strcasestr(buf, "\r\nConnection: close") || strcasestr(buf, "\nConnection: close")) {
            should_close = 1;
        } else if (strcasestr(buf, "\r\nConnection: keep-alive") || strcasestr(buf, "\nConnection: keep-alive")) {
            should_close = 0;
        }

        /* only support GET and HEAD */
        if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
            const char *resp = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
            write_all(client_fd, resp, strlen(resp));
            printf("conn %d: method not allowed (%s), closing\n", client_fd, method);
            fflush(stdout);
            return -1;
        }

        /* basic path sanitization */
        if (!sanitize_path(path)) {
            const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
            write_all(client_fd, resp, strlen(resp));
            printf("conn %d: forbidden path %s\n", client_fd, path);
            fflush(stdout);
            if (should_close) return 0;
            served++;
            continue;
        }

        /* build filesystem path */
        char file_path[4096];
        if (path[0] == '\0' || strcmp(path, "/") == 0) {
            snprintf(file_path, sizeof(file_path), "%s/index.html", docroot);
        } else {
            const char *p = path[0] == '/' ? path + 1 : path;
            snprintf(file_path, sizeof(file_path), "%s/%s", docroot, p);
        }

        struct stat st;
        if (stat(file_path, &st) < 0) {
            const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            write_all(client_fd, resp, strlen(resp));
            printf("conn %d: 404 %s\n", client_fd, file_path);
            fflush(stdout);
            if (should_close) return 0;
            served++;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            const char *suffix = "/index.html";
            size_t base_len = strlen(file_path);
            size_t need = base_len + strlen(suffix) + 1;
            char *idx = malloc(need);
            if (!idx) {
                const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
                write_all(client_fd, resp, strlen(resp));
                printf("conn %d: OOM building index path\n", client_fd);
                fflush(stdout);
                return -1;
            }
            snprintf(idx, need, "%s%s", file_path, suffix);
            if (stat(idx, &st) < 0) {
                const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
                write_all(client_fd, resp, strlen(resp));
                free(idx);
                printf("conn %d: no index for dir %s\n", client_fd, file_path);
                fflush(stdout);
                if (should_close) return 0;
                served++;
                continue;
            }
            strncpy(file_path, idx, sizeof(file_path) - 1);
            file_path[sizeof(file_path) - 1] = '\0';
            free(idx);
        }

        int fd = open(file_path, O_RDONLY);
        if (fd < 0) {
            const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
            write_all(client_fd, resp, strlen(resp));
            printf("conn %d: failed to open %s\n", client_fd, file_path);
            fflush(stdout);
            return -1;
        }

        char hdr[256];
        int hdrlen = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\nConnection: %s\r\n\r\n",
                              (long long)st.st_size,
                              should_close ? "close" : "keep-alive");
        if (hdrlen < 0) hdrlen = 0;
        if (write_all(client_fd, hdr, hdrlen) < 0) {
            close(fd);
            printf("conn %d: write header failed\n", client_fd);
            fflush(stdout);
            return -1;
        }

#ifdef __linux__
        off_t offset = 0;
        while (offset < st.st_size) {
            ssize_t sent = sendfile(client_fd, fd, &offset, st.st_size - offset);
            if (sent <= 0) {
                if (errno == EINTR) continue;
                break;
            }
        }
#else
        ssize_t r;
        char tmp[8192];
        while ((r = read(fd, tmp, sizeof(tmp))) > 0) {
            if (write_all(client_fd, tmp, r) < 0) break;
        }
#endif

        close(fd);

        /* finished one request */
        served++;

        if (should_close) {
            printf("conn %d: client requested close, closing\n", client_fd);
            fflush(stdout);
            return 0;
        }

        if (served >= MAX_KEEPALIVE_REQUESTS) {
            printf("conn %d: max keep-alive requests (%d) reached, closing\n",
                   client_fd, MAX_KEEPALIVE_REQUESTS);
            fflush(stdout);
            return 0;
        }

        /* continue loop to handle next request on same socket (keep-alive) */
    }

    /* reached max requests; close connection */
    printf("conn %d: returning after served=%d\n", client_fd, served);
    fflush(stdout);
    return 0;
}
