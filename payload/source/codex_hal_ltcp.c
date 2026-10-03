#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define HAL_PORT 16716
#define LTCP_FRAME_SIZE 64
#define MAX_JSON_SIZE 16383
#define MAX_RESPONSE_SIZE 32768

#include "ltcp_send.h"

static int connect_hal(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    struct timeval tv;
    unsigned char hello = 0x06;

    if (fd < 0) {
        return -1;
    }

    tv.tv_sec = 8;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HAL_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (send_all(fd, &hello, 1) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static unsigned long read_number(const unsigned char *buf, size_t len) {
    unsigned long out = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        out = (out << 8) | buf[i];
    }
    return out;
}



static int append_payload(unsigned char **payload, size_t *payload_len,
                          const unsigned char *chunk, size_t chunk_len) {
    unsigned char *next = (unsigned char *)realloc(*payload, *payload_len + chunk_len + 1);
    if (!next) {
        return -1;
    }
    *payload = next;
    memcpy(*payload + *payload_len, chunk, chunk_len);
    *payload_len += chunk_len;
    (*payload)[*payload_len] = 0;
    return 0;
}

static size_t skip_frame_padding(const unsigned char *buf, size_t pos, size_t len) {
    size_t next_frame = ((pos / LTCP_FRAME_SIZE) + 1) * LTCP_FRAME_SIZE;
    size_t i;
    if (next_frame <= pos || next_frame >= len) {
        return pos;
    }
    for (i = pos; i < next_frame; i++) {
        if (buf[i] != 0) {
            return pos;
        }
    }
    return next_frame;
}

static int decode_ltcp_payload(const unsigned char *buf, size_t len,
                               unsigned char **payload, size_t *payload_len) {
    size_t pos = 0;
    unsigned int param_count, packets = 0, i;

    *payload = NULL;
    *payload_len = 0;

    if (len < 4 || buf[0] != 0xff) {
        return -1;
    }

    param_count = buf[3] & 0x3f;
    pos = 4;

    for (i = 0; i < param_count; i++) {
        unsigned int tag, plen;
        if (pos >= len) {
            return -1;
        }
        tag = buf[pos++];
        plen = tag & 0x3f;
        if (plen == 0) {
            while (pos < len && buf[pos] != 0) {
                pos++;
            }
            if (pos >= len) {
                return -1;
            }
            pos++;
        } else {
            if (pos + plen > len) {
                return -1;
            }
            packets = (unsigned int)read_number(buf + pos, plen);
            pos += plen;
        }
    }

    if (packets == 0) {
        return 0;
    }
    packets--;

    while (packets > 0 && pos + 2 <= len) {
        unsigned int hdr, chunk_len;
        pos = skip_frame_padding(buf, pos, len);
        if (pos + 2 > len) {
            return -1;
        }
        pos++;
        hdr = buf[pos++];
        if (hdr & 0x40) {
            if (pos >= len) {
                return -1;
            }
            chunk_len = ((hdr & 0x3f) << 8) | buf[pos++];
        } else {
            chunk_len = hdr & 0x3f;
        }
        if (pos + chunk_len > len) {
            return -1;
        }
        if (append_payload(payload, payload_len, buf + pos, chunk_len) != 0) {
            return -1;
        }
        pos += chunk_len;
        packets--;
    }
    return 0;
}

static char *build_json(const char *cmd, const char *data, int timeout) {
    const char *template = "{\"id\":1,\"cmd\":\"%s\",\"data\":%s,\"timeout\":%d}";
    size_t len = strlen(template) + strlen(cmd) + strlen(data) + 32;
    char *json = (char *)malloc(len);
    if (!json) {
        return NULL;
    }
    snprintf(json, len, template, cmd, data, timeout);
    return json;
}

static char *trim_in_place(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static int parse_hex_payload(const char *hex, unsigned char **out, size_t *outlen) {
    const char *p = hex;
    unsigned char *buf;
    size_t cap, len = 0;
    int high = -1;

    *out = NULL;
    *outlen = 0;
    if (!hex || !hex[0]) {
        return 0;
    }

    cap = strlen(hex) / 2 + 2;
    buf = (unsigned char *)malloc(cap);
    if (!buf) {
        return -1;
    }

    while (*p) {
        int v;
        if (*p == ' ' || *p == ':' || *p == '-' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
            continue;
        }
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            continue;
        }
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        else {
            free(buf);
            return -1;
        }
        if (high < 0) {
            high = v;
        } else {
            if (len >= cap) {
                free(buf);
                return -1;
            }
            buf[len++] = (unsigned char)((high << 4) | v);
            high = -1;
        }
        p++;
    }
    if (high >= 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    *outlen = len;
    return 0;
}

static int run_ltcp_once(const char *cmd, const char *data, int timeout, const char *payload_hex,
                         char *response, size_t response_len) {
    char *json;
    unsigned char *binary = NULL;
    unsigned char *request = NULL;
    int fd;
    unsigned char *buf;
    unsigned char *payload = NULL;
    size_t json_len, binary_len = 0, request_len;
    size_t total = 0, payload_len = 0;

    if (response && response_len) response[0] = 0;
    json = build_json(cmd, data, timeout);
    if (!json) {
        return 1;
    }
    json_len = strlen(json);
    if (payload_hex && payload_hex[0] && parse_hex_payload(payload_hex, &binary, &binary_len) != 0) {
        fprintf(stderr, "invalid binary payload hex\n");
        free(json);
        return 2;
    }
    request_len = json_len + binary_len;
    request = (unsigned char *)malloc(request_len ? request_len : 1);
    if (!request) {
        free(binary);
        free(json);
        return 1;
    }
    memcpy(request, json, json_len);
    if (binary_len) {
        memcpy(request + json_len, binary, binary_len);
    }

    fd = connect_hal();
    if (fd < 0) {
        perror("connect_hal");
        free(request);
        free(binary);
        free(json);
        return 1;
    }
    if (send_ltcp_command(fd, request, request_len) != 0) {
        perror("send_ltcp_command");
        close(fd);
        free(request);
        free(binary);
        free(json);
        return 1;
    }
    free(request);
    free(binary);
    free(json);

    buf = (unsigned char *)malloc(MAX_RESPONSE_SIZE);
    if (!buf) {
        close(fd);
        return 1;
    }

    while (total < MAX_RESPONSE_SIZE) {
        ssize_t n = recv(fd, buf + total, MAX_RESPONSE_SIZE - total, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("recv");
            free(buf);
            close(fd);
            return 1;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    close(fd);

    if (decode_ltcp_payload(buf, total, &payload, &payload_len) != 0 || !payload) {
        fprintf(stderr, "decode_ltcp_payload failed (raw_len=%lu)\n", (unsigned long)total);
        free(payload);
        free(buf);
        return 2;
    }

    if (response && response_len) {
        size_t copy = payload_len < response_len - 1 ? payload_len : response_len - 1;
        memcpy(response, payload, copy);
        response[copy] = 0;
    } else {
        fwrite(payload, 1, payload_len, stdout);
        putchar('\n');
    }
    free(payload);
    free(buf);
    return 0;
}

static int run_sequence_file(const char *cmd, const char *data, int timeout, const char *path, int gap_ms,
                             char *last, size_t last_len, int *sent) {
    FILE *f = fopen(path, "r");
    char line[256];
    int rc = 0;
    if (!f) {
        perror("open sequence file");
        return 1;
    }
    if (sent) *sent = 0;
    if (last && last_len) last[0] = 0;
    if (gap_ms < 5) gap_ms = 5;
    if (gap_ms > 5000) gap_ms = 5000;
    while (fgets(line, sizeof(line), f)) {
        char *hex = trim_in_place(line);
        char *part, *save = NULL;
        int parts = 0;
        if (!hex[0] || hex[0] == '#') continue;
        part = strtok_r(hex, "|", &save);
        while (part) {
            part = trim_in_place(part);
            if (part[0]) {
                rc = run_ltcp_once(cmd, data, timeout, part, last, last_len);
                if (rc != 0) break;
                if (sent) (*sent)++;
                parts++;
                if (save && *save) usleep(5000);
            }
            part = strtok_r(NULL, "|", &save);
        }
        if (rc != 0) break;
        if (parts <= 0) continue;
        usleep((useconds_t)gap_ms * 1000);
    }
    fclose(f);
    return rc;
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "sys.ping";
    const char *data = argc > 2 ? argv[2] : "{}";
    int timeout = argc > 3 ? atoi(argv[3]) : 5;
    const char *seq_file = NULL;
    int gap_ms = 20;
    int first_payload = 4;
    int i, sent = 0, rc;
    char last[MAX_RESPONSE_SIZE];

    for (i = 4; i < argc; i++) {
        if (strncmp(argv[i], "--gap-ms=", 9) == 0) {
            gap_ms = atoi(argv[i] + 9);
            first_payload = i + 1;
        } else if (strncmp(argv[i], "--seq-file=", 11) == 0) {
            seq_file = argv[i] + 11;
            first_payload = i + 1;
        } else {
            break;
        }
    }

    if (seq_file && seq_file[0]) {
        rc = run_sequence_file(cmd, data, timeout, seq_file, gap_ms, last, sizeof(last), &sent);
        if (rc != 0) return rc;
        printf("seq_sent=%d last=%s\n", sent, last[0] ? last : "no response");
        return 0;
    }

    if (first_payload < argc) {
        int multiple = (argc - first_payload) > 1;
        for (i = first_payload; i < argc; i++) {
            rc = run_ltcp_once(cmd, data, timeout, argv[i], last, sizeof(last));
            if (rc != 0) return rc;
            sent++;
            if (multiple) usleep((useconds_t)gap_ms * 1000);
        }
        if (multiple) {
            printf("seq_sent=%d last=%s\n", sent, last[0] ? last : "no response");
        } else {
            printf("%s\n", last[0] ? last : "no response");
        }
        return 0;
    }

    rc = run_ltcp_once(cmd, data, timeout, NULL, last, sizeof(last));
    if (rc != 0) return rc;
    printf("%s\n", last[0] ? last : "no response");
    return 0;
}
