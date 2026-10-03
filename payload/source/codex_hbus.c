#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdint.h>

static int write_all(int fd, const void *buf, size_t len) {
    const unsigned char *p = buf;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static long long millis(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int hold_alive(const char *lease, const char *cancel, long long started) {
    long long renewed = 0, now = millis();
    FILE *f;
    if (access(cancel, F_OK) == 0 || now - started >= 30000) return 0;
    f = fopen(lease, "r");
    if (!f) return 0;
    if (fscanf(f, "%lld", &renewed) != 1) renewed = 0;
    fclose(f);
    return renewed > 0 && now >= renewed && now - renewed < 1200;
}

static int read_some(int fd, unsigned char *buf, size_t len) {
    ssize_t n = recv(fd, buf, len, 0);
    if (n <= 0) {
        return -1;
    }
    return (int)n;
}

static int read_exact(int fd, unsigned char *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, buf + got, len - got, 0);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

static int connect_local(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    struct timeval tv;
    if (fd < 0) {
        return -1;
    }
    tv.tv_sec = 8;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8088);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int websocket_handshake(int fd, const char *hub_id) {
    char req[512];
    unsigned char resp[2048];
    int n;
    snprintf(req, sizeof(req),
        "GET /?domain=svcs.myharmony.com&hubId=%s HTTP/1.1\r\n"
        "Host: 127.0.0.1:8088\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: MDEyMzQ1Njc4OWFiY2RlZg==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", hub_id);
    if (write_all(fd, req, strlen(req)) != 0) {
        return -1;
    }
    n = read_some(fd, resp, sizeof(resp) - 1);
    if (n < 0) {
        return -1;
    }
    resp[n] = 0;
    if (!strstr((char *)resp, "101 Switching Protocols")) {
        fprintf(stderr, "%s\n", resp);
        return -1;
    }
    return 0;
}

static int send_ws_text(int fd, const char *payload) {
    size_t len = strlen(payload);
    unsigned char hdr[14];
    unsigned char mask[4] = {0x13, 0x37, 0x42, 0x99};
    size_t hlen = 0;
    size_t i;
    hdr[hlen++] = 0x81;
    if (len < 126) {
        hdr[hlen++] = 0x80 | (unsigned char)len;
    } else if (len <= 65535) {
        hdr[hlen++] = 0x80 | 126;
        hdr[hlen++] = (unsigned char)((len >> 8) & 0xff);
        hdr[hlen++] = (unsigned char)(len & 0xff);
    } else {
        hdr[hlen++] = 0x80 | 127;
        for (i = 0; i < 8; i++) {
            hdr[hlen++] = (unsigned char)(((uint64_t)len >> (56 - 8 * i)) & 0xff);
        }
    }
    memcpy(hdr + hlen, mask, 4);
    hlen += 4;
    if (write_all(fd, hdr, hlen) != 0) {
        return -1;
    }
    for (i = 0; i < len; i += 1024) {
        unsigned char out[1024];
        size_t j;
        size_t chunk = len - i > sizeof(out) ? sizeof(out) : len - i;
        for (j = 0; j < chunk; j++) {
            out[j] = (unsigned char)payload[i + j] ^ mask[(i + j) & 3];
        }
        if (write_all(fd, out, chunk) != 0) {
            return -1;
        }
    }
    return 0;
}

static int recv_ws_text(int fd, int holding) {
    unsigned char hdr[2];
    uint64_t len;
    unsigned char *payload;
    int rc = 0;
    if (read_exact(fd, hdr, 2) != 0) {
        return -1;
    }
    if ((hdr[0] & 15) == 8 || (hdr[1] & 128)) return -1;
    len = hdr[1] & 0x7f;
    if (len == 126) {
        unsigned char ext[2];
        if (read_exact(fd, ext, 2) != 0) return -1;
        len = ((unsigned long)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        unsigned char ext[8];
        int i;
        if (read_exact(fd, ext, 8) != 0) return -1;
        len = 0;
        for (i = 0; i < 8; i++) {
            len = (len << 8) | ext[i];
        }
    }
    if (len > 1024 * 1024) return -1;
    payload = (unsigned char *)malloc((size_t)len + 1);
    if (!payload) {
        return -1;
    }
    if (read_exact(fd, payload, len) != 0) {
        free(payload);
        return -1;
    }
    payload[len] = 0;
    printf("%s\n", payload);
    if (holding && strstr((char *)payload, "harmony.engine?holdaction")) {
        char *code = strstr((char *)payload, "\"code\"");
        int status;
        if (code && sscanf(code + 6, " : %d", &status) == 1 && status >= 300) rc = -1;
    }
    free(payload);
    return rc;
}

static int send_hold_status(int fd, const char *hub_id, const char *cmd,
                            const char *params, const char *status) {
    char payload[2048];
    int n = snprintf(payload, sizeof(payload),
        "{\"hubId\":\"%s\",\"timeout\":5,\"hbus\":{\"id\":\"hold-%ld\",\"cmd\":\"%s\","
        "\"params\":{\"status\":\"%s\",\"timestamp\":0,%s}}",
        hub_id, (long)getpid(), cmd, status, params + 1);
    if (n < 0 || (size_t)n >= sizeof(payload)) return -1;
    return send_ws_text(fd, payload);
}

static int run_hold(int fd, const char *hub_id, const char *cmd, const char *params,
                    const char *lease, const char *cancel) {
    long long started = millis();
    int rc = 0, sent = 0, released;
    struct timeval timeout = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (!hold_alive(lease, cancel, started)) return 0;
    if (send_hold_status(fd, hub_id, cmd, params, "press") != 0) rc = 1;
    else sent = 1;
    while (!rc && hold_alive(lease, cancel, started)) {
        fd_set reads;
        struct timeval poll = {0, 0};
        FD_ZERO(&reads);
        FD_SET(fd, &reads);
        if (select(fd + 1, &reads, NULL, NULL, &poll) > 0 && recv_ws_text(fd, 1) != 0) {
            rc = 1;
            break;
        }
        usleep(50000);
        if (millis() - started >= sent * 200 && hold_alive(lease, cancel, started)) {
            if (send_hold_status(fd, hub_id, cmd, params, "hold") != 0) rc = 1;
            else sent++;
        }
    }
    /* These statuses do not receive success replies. Always send release, even
       after a write failure; the caller reports transmission, not TV response. */
    released = send_hold_status(fd, hub_id, cmd, params, "release") == 0;
    if (!released || rc) {
        int retry = connect_local();
        if (retry >= 0) {
            if (websocket_handshake(retry, hub_id) == 0)
                released = send_hold_status(retry, hub_id, cmd, params, "release") == 0;
            close(retry);
        }
    }
    if (!released) rc = 1;
    printf("hold messages sent=%d; release %s\n", sent, released ? "sent" : "failed");
    return rc;
}

int main(int argc, char **argv) {
    const char *hub_id;
    const char *cmd;
    const char *params;
    char *payload;
    int fd;
    int rc = 1;
    signal(SIGPIPE, SIG_IGN);
    if (argc < 3) {
        fprintf(stderr, "usage: %s <hub_id> <cmd> [params-json] [--hold lease-file cancel-file]\n", argv[0]);
        return 2;
    }
    hub_id = argv[1];
    cmd = argv[2];
    params = argc > 3 ? argv[3] : "{}";
    payload = (char *)malloc(strlen(hub_id) + strlen(cmd) + strlen(params) + 256);
    if (!payload) {
        return 1;
    }
    snprintf(payload, strlen(hub_id) + strlen(cmd) + strlen(params) + 256,
        "{\"hubId\":\"%s\",\"timeout\":30,\"hbus\":{\"id\":\"codex-%ld\",\"cmd\":\"%s\",\"params\":%s}}",
        hub_id, (long)time(NULL), cmd, params);
    fd = connect_local();
    if (fd < 0) {
        perror("connect");
        goto out;
    }
    if (websocket_handshake(fd, hub_id) != 0) {
        goto close_out;
    }
    if (argc == 7 && strcmp(argv[4], "--hold") == 0) {
        if (params[0] != '{') goto close_out;
        rc = run_hold(fd, hub_id, cmd, params, argv[5], argv[6]);
        goto close_out;
    }
    if (send_ws_text(fd, payload) != 0) {
        perror("send");
        goto close_out;
    }
    if (recv_ws_text(fd, 0) == 0) {
        rc = 0;
    }
close_out:
    close(fd);
out:
    free(payload);
    return rc;
}
