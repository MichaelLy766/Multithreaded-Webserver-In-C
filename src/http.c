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

/* handle_client: handle a single client connection.
   Reads a request (one read), parses method+path, enforces simple checks,
   maps path to filesystem under docroot, opens and sends the file with headers.
   Returns 0 on success, -1 on error (caller should close socket). */
int handle_client(int client_fd, const char *docroot) {
    char buf[REQ_BUF];
    /* read request data (single-shot read, not a full HTTP parser) */
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        return -1; /* read error or client closed connection */
    }
    buf[n] = '\0'; /* NUL-terminate to allow string parsing */

    /* small buffers for parsed method and path from the request line */
    char method[16];
    char path[1024];
    /* parse the start-line: METHOD PATH (we ignore HTTP version and headers) */
    if (sscanf(buf, "%15s %1023s", method, path) != 2) {
        /* malformed request: respond 400 Bad Request */
        const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    /* only support GET and HEAD */
    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        const char *resp = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    /* basic path sanitization to avoid simple traversal attempts */
    if (!sanitize_path(path)) {
        const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    /* build the filesystem path to the requested resource */
    char file_path[4096];
    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        /* default to index.html for root requests */
        snprintf(file_path, sizeof(file_path), "%s/index.html", docroot);
    } else {
        /* strip leading slash if present and join with docroot */
        const char *p = path[0] == '/' ? path + 1 : path;
        snprintf(file_path, sizeof(file_path), "%s/%s", docroot, p);
    }

    /* stat the file to check existence and size */
    struct stat st;
    if (stat(file_path, &st) < 0) {
        /* file missing: return 404 */
        const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    /* if the path is a directory, attempt to serve index.html inside it */
    if (S_ISDIR(st.st_mode)) {
        const char *suffix = "/index.html";
        size_t base_len = strlen(file_path); /* current path length */
        size_t need = base_len + strlen(suffix) + 1; /* required bytes including NUL */

        /* allocate temporary buffer for concatenated path to avoid overflow */
        char *idx = malloc(need);
        if (!idx) {
            /* out-of-memory: this branch currently builds into a small tmp buffer
               but does not use it; ideally should respond 500. Keep conservative behavior. */
            char tmp[256];
            /* attempt to build truncated path into tmp (not used further) */
            snprintf(tmp, sizeof(tmp), "%s%s", file_path, suffix);
            /* Proper handling would be to return a 500 here; current code falls through. */
        } else {
            /* build index path and stat it */
            snprintf(idx, need, "%s%s", file_path, suffix);
            if (stat(idx, &st) < 0) {
                /* no index: treat as forbidden for this server implementation */
                const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
                write_all(client_fd, resp, strlen(resp));
                free(idx);
                return -1;
            }
            /* copy resolved index path back into file_path (bounded) */
            strncpy(file_path, idx, sizeof(file_path) - 1);
            file_path[sizeof(file_path) - 1] = '\0';
            free(idx);
        }
    }

    /* open the file for reading */
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        /* failure opening file: respond 500 Internal Server Error */
        const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        write_all(client_fd, resp, strlen(resp));
        return -1;
    }

    /* write a minimal 200 OK response header with Content-Length */
    char hdr[256];
    int hdrlen = snprintf(hdr, sizeof(hdr),
                          "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\n\r\n",
                          (long long)st.st_size);
    if (hdrlen < 0) hdrlen = 0; /* snprintf error guard */
    if (write_all(client_fd, hdr, hdrlen) < 0) {
        close(fd);
        return -1;
    }

#ifdef __linux__
    /* On Linux use sendfile to copy file -> socket efficiently.
       sendfile here uses an offset pointer updated by the syscall. */
    off_t offset = 0;
    while (offset < st.st_size) {
        ssize_t sent = sendfile(client_fd, fd, &offset, st.st_size - offset);
        if (sent <= 0) {
            if (errno == EINTR) continue; /* retry on interrupt */
            break;                        /* other errors: stop sending */
        }
    }
#else
    /* Portable fallback: read from file into a buffer and write to socket */
    ssize_t r;
    char tmp[8192];
    while ((r = read(fd, tmp, sizeof(tmp))) > 0) {
        if (write_all(client_fd, tmp, r) < 0) break; /* stop on write error */
    }
#endif

    close(fd); /* close file descriptor */
    return 0;   /* success (connection closing handled by caller) */
}
