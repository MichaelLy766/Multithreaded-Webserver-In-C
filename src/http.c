#include "http.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define REQ_BUF 8192

static ssize_t write_all(int fd, const void *buf, size_t count) {
    const char *p = buf;
    size_t left = count;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        left -= n;
    }
    return count;
}

// Basic sanitization: reject ".." in path
static int sanitize_path(const char *path) {
    if (strstr(path, "..") != NULL) return 0;
    return 1;
}

int handle_client(int client_fd, const char *docroot) {
    char buf[REQ_BUF];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';

    char method[16];
    char path[1024];
    if (sscanf(buf, "%15s %1023s", method, path) != 2) {
        // malformed
        const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        const char *resp = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    if (!sanitize_path(path)) {
        const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    char file_path[4096];
    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/index.html", docroot);
    } else {
        // strip leading '/'
        const char *p = path[0] == '/' ? path + 1 : path;
        snprintf(file_path, sizeof(file_path), "%s/%s", docroot, p);
    }

    struct stat st;
    if (stat(file_path, &st) < 0) {
        const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        // try index.html inside directory
        char idx[4096];
        snprintf(idx, sizeof(idx), "%s/index.html", file_path);
        if (stat(idx, &st) < 0) {
            const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
            write_all(client_fd, resp, strlen(resp));
            return -1;
        }
        strncpy(file_path, idx, sizeof(file_path)-1);
        file_path[sizeof(file_path)-1] = '\0';
    }

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    char hdr[256];
    int hdrlen = snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\n\r\n", (long long)st.st_size);
    if (hdrlen < 0) hdrlen = 0;
    if (write_all(client_fd, hdr, hdrlen) < 0) {
        close(fd);
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
    // portable fallback
    ssize_t r;
    char tmp[8192];
    while ((r = read(fd, tmp, sizeof(tmp))) > 0) {
        if (write_all(client_fd, tmp, r) < 0) break;
    }
#endif

    close(fd);
    return 0;
}
