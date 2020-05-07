// This server can do 20k requests/sec on my laptop...
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>

#define PORT 2020
#define _200 "HTTP/1.0 200 OK\r\n"
#define _404 "HTTP/1.0 404 Not Found\r\n\r\nnot found\n"
#define _400 "HTTP/1.0 400 Bad Request\r\n\r\nbad request\n"

// Extract path from a request.
struct ref_t {const char *path, *ctyp;};
int xpath(char *req, struct ref_t *ref) {
    // Determine path length. Truncate string.
    char *p = strchr(req,  ' ');
    int len = p - req;
    req[len] = '\0';

    // Remove query strings.
    p = strchr(req, '?');
    if (p) *p = '\0';

    ref->path = (!len ? "index.html" : req);
    ref->ctyp = (!len ? "text/html" : "text/plain");
    return (!!strchr(req, '/'));
}

// Given a HTTP request, write to directly to the output fd.
int respond(char *req, int ofd) {
    struct ref_t ref; // Drop non-GETs / netcat garbage / path traversals
    if (strncmp("GET ", req, 4) || !strchr(req+5, ' ') || xpath(req+5, &ref))
        return write(ofd, _400, strlen(_400));

    // Attempt to open file, else 404
    int ifd = open(ref.path, O_RDONLY);
    if (ifd == -1) return write(ofd, _404, strlen(_404));

    // Read and transmit file
    struct stat st;
    stat(ref.path, &st);
    sprintf(req, _200 "Content-Type: %s\r\nContent-Length: %ld\r\n\r\n", ref.ctyp, st.st_size);
    int ret = write(ofd, req, strlen(req));
    if (ret == -1) goto cleanup;
    ret = sendfile(ofd, ifd, 0, st.st_size);

cleanup:
    close(ifd);
    return ret;
}

int main(void) {
    // Ignore remotely terminated sockets
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) err(1, "signal");

    // Socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) err(1, "sock");
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))) err(1, "setsockopt");

    // Bind/Listen
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr))) err(1, "bind");
    if (listen(sock, 16)) err(1, "listen"); // Backlog=16

    while (1) {
        // Accept
        socklen_t alen = sizeof(addr);
        int fd = accept(sock, (struct sockaddr *) &addr, &alen);
        if (fd == -1) goto err;

        // Receive
        char buf[256];
        int len = read(fd, buf, sizeof(buf));
        if (len < 1) goto err;

        // Transmit
        buf[len-1] = '\0';
        if (respond(buf, fd) == -1) goto err;

        // Close socket
        shutdown(fd, SHUT_RDWR);
        err: close(fd);
    }
}
