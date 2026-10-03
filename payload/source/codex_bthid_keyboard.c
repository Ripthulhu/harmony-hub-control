#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define HAL_PORT 16716
#define LTCP_FRAME_SIZE 64
#define MAX_JSON_SIZE 16383
#define MAX_RESPONSE_SIZE 4096
#define DEFAULT_FIFO "/tmp/bthid_input"
#define DEFAULT_STATUS "/tmp/bthid_status"
#define DEFAULT_LOG "/cache/codex-bthid-keyboard.log"
#define TARGET_FILE "/data/codex/bthid_target"
#define LOG_MAX_BYTES 32768

static const unsigned char RELEASE_REPORT[10] = {0xa1, 0x01, 0, 0, 0, 0, 0, 0, 0, 0};

#include "ltcp_send.h"

static int connect_hal(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    struct timeval tv;
    unsigned char hello = 0x06;
    if (fd < 0) return -1;
    tv.tv_sec = 0;
    tv.tv_usec = 250000;
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



static void drain_response(int fd) {
    unsigned char buf[MAX_RESPONSE_SIZE];
    recv(fd, buf, sizeof(buf), 0);
}

static int safe_bt_addr(const char *s) {
    int i;
    if (!s || strlen(s) != 17) return 0;
    for (i = 0; i < 17; i++) {
        if ((i % 3) == 2) {
            if (s[i] != ':') return 0;
        } else if (!isxdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int safe_bt_type(const char *s) {
    return s && (
        strcmp(s, "btkeyboard") == 0 ||
        strcmp(s, "btkeyboard-nexus") == 0 ||
        strcmp(s, "fire") == 0 ||
        strcmp(s, "ps3") == 0 ||
        strcmp(s, "wii") == 0);
}

static int send_report(const char *type, const char *bdaddr, const unsigned char *report, size_t report_len) {
    char json[256];
    unsigned char request[512];
    int fd, rc;
    size_t json_len;
    snprintf(json, sizeof(json),
        "{\"id\":1,\"cmd\":\"bthid.report\",\"data\":{\"type\":\"%s\",\"bdaddr\":\"%s\"},\"timeout\":8}",
        type, bdaddr);
    json_len = strlen(json);
    if (json_len + report_len > sizeof(request)) return -1;
    memcpy(request, json, json_len);
    memcpy(request + json_len, report, report_len);
    fd = connect_hal();
    if (fd < 0) return -1;
    rc = send_ltcp_command(fd, request, json_len + report_len);
    if (rc == 0) drain_response(fd);
    close(fd);
    return rc;
}

static void rotate_log_if_needed(void) {
    struct stat st;
    if (stat(DEFAULT_LOG, &st) == 0 && st.st_size > LOG_MAX_BYTES) {
        unlink(DEFAULT_LOG ".1");
        rename(DEFAULT_LOG, DEFAULT_LOG ".1");
    }
}

static void log_line(const char *fmt, ...) {
    FILE *f;
    va_list ap;
    time_t now = time(NULL);
    rotate_log_if_needed();
    f = fopen(DEFAULT_LOG, "a");
    if (!f) return;
    fprintf(f, "%ld ", (long)now);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void status_json_string(FILE *f, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    fputc('"', f);
    while (*p) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', f);
            fputc(*p, f);
        } else if (*p == '\n') {
            fputs("\\n", f);
        } else if (*p == '\r') {
            fputs("\\r", f);
        } else if (*p == '\t') {
            fputs("\\t", f);
        } else if (*p < 32) {
            fprintf(f, "\\u%04x", (unsigned int)*p);
        } else {
            fputc(*p, f);
        }
        p++;
    }
    fputc('"', f);
}

static void write_status(const char *state, const char *target, unsigned long sent, unsigned long skipped, const char *error) {
    FILE *f = fopen(DEFAULT_STATUS ".new", "w");
    time_t now = time(NULL);
    if (!f) return;
    fprintf(f, "{\"ok\":true,\"runtime\":true,\"pid\":%ld,\"updated\":%ld,\"state\":",
        (long)getpid(), (long)now);
    status_json_string(f, state ? state : "unknown");
    fputs(",\"target\":", f);
    status_json_string(f, target ? target : "");
    fprintf(f, ",\"sent\":%lu,\"skipped\":%lu,\"error\":", sent, skipped);
    status_json_string(f, error ? error : "");
    fputs("}\n", f);
    fclose(f);
    rename(DEFAULT_STATUS ".new", DEFAULT_STATUS);
}

static int extract_bt_addr(const char *text, char *out, size_t outlen) {
    const char *p = text;
    if (!text || !out || outlen < 18) return 0;
    out[0] = 0;
    while (*p) {
        if (isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]) &&
            p[2] == ':' && isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
            char candidate[18];
            int i;
            for (i = 0; i < 17 && p[i]; i++) candidate[i] = (char)toupper((unsigned char)p[i]);
            candidate[17] = 0;
            if (safe_bt_addr(candidate)) {
                snprintf(out, outlen, "%s", candidate);
                return 1;
            }
        }
        p++;
    }
    return 0;
}

static int read_target_file(char *type, size_t typelen, char *addr, size_t addrlen) {
    FILE *f = fopen(TARGET_FILE, "r");
    char line[160];
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        char *v;
        while (*line && isspace((unsigned char)*line)) memmove(line, line + 1, strlen(line));
        v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        while (*v && isspace((unsigned char)*v)) v++;
        line[strcspn(line, " \t\r\n")] = 0;
        v[strcspn(v, " \t\r\n")] = 0;
        if (strcmp(line, "type") == 0 && safe_bt_type(v)) snprintf(type, typelen, "%s", v);
        else if (strcmp(line, "bdaddr") == 0 && safe_bt_addr(v)) snprintf(addr, addrlen, "%s", v);
    }
    fclose(f);
    return safe_bt_addr(addr);
}

static int detect_connected_target(char *type, size_t typelen, char *addr, size_t addrlen) {
    FILE *p;
    char buf[512], all[2048];
    size_t n = 0;
    addr[0] = 0;
    if (!type[0]) snprintf(type, typelen, "%s", "btkeyboard");
    p = popen("hcitool con 2>/dev/null", "r");
    if (p) {
        all[0] = 0;
        while (fgets(buf, sizeof(buf), p)) {
            size_t len = strlen(buf);
            if (n + len + 1 < sizeof(all)) {
                memcpy(all + n, buf, len);
                n += len;
                all[n] = 0;
            }
        }
        pclose(p);
        if (extract_bt_addr(all, addr, addrlen)) return 1;
    }
    return read_target_file(type, typelen, addr, addrlen);
}

static int map_ascii(unsigned char ch, unsigned char *mod, unsigned char *usage) {
    *mod = 0;
    *usage = 0;
    if (ch >= 'a' && ch <= 'z') {
        *usage = (unsigned char)(0x04 + (ch - 'a'));
        return 1;
    }
    if (ch >= 'A' && ch <= 'Z') {
        *mod = 0x02;
        *usage = (unsigned char)(0x04 + (ch - 'A'));
        return 1;
    }
    if (ch >= '1' && ch <= '9') {
        *usage = (unsigned char)(0x1e + (ch - '1'));
        return 1;
    }
    if (ch == '0') { *usage = 0x27; return 1; }
    if (ch == '\n' || ch == '\r') { *usage = 0x28; return 1; }
    if (ch == '\t') { *usage = 0x2b; return 1; }
    if (ch == ' ') { *usage = 0x2c; return 1; }
    switch (ch) {
    case '!': *mod = 0x02; *usage = 0x1e; return 1;
    case '@': *mod = 0x02; *usage = 0x1f; return 1;
    case '#': *mod = 0x02; *usage = 0x20; return 1;
    case '$': *mod = 0x02; *usage = 0x21; return 1;
    case '%': *mod = 0x02; *usage = 0x22; return 1;
    case '^': *mod = 0x02; *usage = 0x23; return 1;
    case '&': *mod = 0x02; *usage = 0x24; return 1;
    case '*': *mod = 0x02; *usage = 0x25; return 1;
    case '(': *mod = 0x02; *usage = 0x26; return 1;
    case ')': *mod = 0x02; *usage = 0x27; return 1;
    case '-': *usage = 0x2d; return 1;
    case '_': *mod = 0x02; *usage = 0x2d; return 1;
    case '=': *usage = 0x2e; return 1;
    case '+': *mod = 0x02; *usage = 0x2e; return 1;
    case '[': *usage = 0x2f; return 1;
    case '{': *mod = 0x02; *usage = 0x2f; return 1;
    case ']': *usage = 0x30; return 1;
    case '}': *mod = 0x02; *usage = 0x30; return 1;
    case '\\': *usage = 0x31; return 1;
    case '|': *mod = 0x02; *usage = 0x31; return 1;
    case ';': *usage = 0x33; return 1;
    case ':': *mod = 0x02; *usage = 0x33; return 1;
    case '\'': *usage = 0x34; return 1;
    case '"': *mod = 0x02; *usage = 0x34; return 1;
    case '`': *usage = 0x35; return 1;
    case '~': *mod = 0x02; *usage = 0x35; return 1;
    case ',': *usage = 0x36; return 1;
    case '<': *mod = 0x02; *usage = 0x36; return 1;
    case '.': *usage = 0x37; return 1;
    case '>': *mod = 0x02; *usage = 0x37; return 1;
    case '/': *usage = 0x38; return 1;
    case '?': *mod = 0x02; *usage = 0x38; return 1;
    default: return 0;
    }
}

static int send_ascii_char(const char *type, const char *addr, unsigned char ch, int release_ms, int gap_ms) {
    unsigned char mod, usage;
    unsigned char report[10] = {0xa1, 0x01, 0, 0, 0, 0, 0, 0, 0, 0};
    if (!map_ascii(ch, &mod, &usage)) return 0;
    report[2] = mod;
    report[4] = usage;
    if (send_report(type, addr, report, sizeof(report)) != 0) return -1;
    usleep((useconds_t)release_ms * 1000);
    if (send_report(type, addr, RELEASE_REPORT, sizeof(RELEASE_REPORT)) != 0) return -1;
    usleep((useconds_t)gap_ms * 1000);
    return 1;
}

static void ensure_fifo(const char *fifo) {
    struct stat st;
    if (stat(fifo, &st) == 0 && !S_ISFIFO(st.st_mode)) unlink(fifo);
    if (stat(fifo, &st) != 0) mkfifo(fifo, 0666);
    chmod(fifo, 0666);
}

int main(int argc, char **argv) {
    const char *fifo = DEFAULT_FIFO;
    char type[40] = "btkeyboard";
    char addr[32] = "";
    unsigned long sent = 0, skipped = 0;
    int gap_ms = 20, release_ms = 5;
    int i;
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--fifo=", 7) == 0) fifo = argv[i] + 7;
        else if (strncmp(argv[i], "--type=", 7) == 0 && safe_bt_type(argv[i] + 7)) snprintf(type, sizeof(type), "%s", argv[i] + 7);
        else if (strncmp(argv[i], "--gap-ms=", 9) == 0) gap_ms = atoi(argv[i] + 9);
        else if (strncmp(argv[i], "--release-ms=", 13) == 0) release_ms = atoi(argv[i] + 13);
    }
    if (gap_ms < 5) gap_ms = 5;
    if (gap_ms > 1000) gap_ms = 1000;
    if (release_ms < 2) release_ms = 2;
    if (release_ms > 100) release_ms = 100;

    ensure_fifo(fifo);
    log_line("bthid keyboard runtime start fifo=%s type=%s gap=%d release=%d", fifo, type, gap_ms, release_ms);

    for (;;) {
        int fd;
        if (!detect_connected_target(type, sizeof(type), addr, sizeof(addr))) {
            write_status("no_target", "", sent, skipped, "pair/connect a Bluetooth HID target");
            sleep(1);
            continue;
        }
        fd = open(fifo, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            write_status("fifo_error", addr, sent, skipped, strerror(errno));
            sleep(1);
            continue;
        }
        write_status("listening", addr, sent, skipped, "");
        log_line("listening target=%s type=%s", addr, type);
        while (1) {
            fd_set rfds;
            struct timeval tv;
            unsigned char buf[256];
            ssize_t n;
            char latest_type[40] = "";
            char latest_addr[32] = "";
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
                if (!detect_connected_target(latest_type, sizeof(latest_type), latest_addr, sizeof(latest_addr)) ||
                    strcmp(latest_addr, addr) != 0) {
                    close(fd);
                    break;
                }
                write_status("listening", addr, sent, skipped, "");
                continue;
            }
            n = read(fd, buf, sizeof(buf));
            if (n <= 0) continue;
            for (i = 0; i < n; i++) {
                int rc;
                if (buf[i] >= 0x80) {
                    skipped++;
                    continue;
                }
                rc = send_ascii_char(type, addr, buf[i], release_ms, gap_ms);
                if (rc > 0) sent++;
                else if (rc == 0) skipped++;
                else {
                    skipped++;
                    write_status("send_error", addr, sent, skipped, "bthid.report failed");
                    log_line("send failed target=%s byte=%u", addr, (unsigned int)buf[i]);
                    close(fd);
                    fd = -1;
                    break;
                }
            }
            write_status("listening", addr, sent, skipped, "");
            if (fd < 0) break;
        }
        if (fd >= 0) close(fd);
    }
}
