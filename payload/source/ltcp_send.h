#ifndef CODEX_LTCP_SEND_H
#define CODEX_LTCP_SEND_H

static int send_all(int fd, const unsigned char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int send_ltcp_command(int fd, const unsigned char *payload, size_t len) {
    unsigned char primary[6] = {0xff, 0x08, 0x00, 0x01, 0x01, 0x02};
    unsigned char secondary[3];
    size_t secondary_len;
    unsigned char *stream;
    size_t stream_len = 0;
    size_t pos = 0;

    if (len > MAX_JSON_SIZE) {
        fprintf(stderr, "payload too large\n");
        return -1;
    }

    secondary[0] = 0x01;
    if (len > 63) {
        secondary[1] = (unsigned char)(0x80 | 0x40 | ((len >> 8) & 0x3f));
        secondary[2] = (unsigned char)(len & 0xff);
        secondary_len = 3;
    } else {
        secondary[1] = (unsigned char)(0x80 | len);
        secondary_len = 2;
    }

    stream = (unsigned char *)malloc(sizeof(primary) + secondary_len + len);
    if (!stream) {
        return -1;
    }
    memcpy(stream + stream_len, primary, sizeof(primary));
    stream_len += sizeof(primary);
    memcpy(stream + stream_len, secondary, secondary_len);
    stream_len += secondary_len;
    memcpy(stream + stream_len, payload, len);
    stream_len += len;

    while (pos < stream_len) {
        unsigned char frame[LTCP_FRAME_SIZE];
        size_t chunk = stream_len - pos;
        if (chunk > sizeof(frame)) {
            chunk = sizeof(frame);
        }
        memset(frame, 0, sizeof(frame));
        memcpy(frame, stream + pos, chunk);
        if (send_all(fd, frame, sizeof(frame)) != 0) {
            free(stream);
            return -1;
        }
        pos += chunk;
    }
    free(stream);
    return 0;
}

#endif
