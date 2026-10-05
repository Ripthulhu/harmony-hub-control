#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "vendor/cJSON.h"

#ifndef MQTT_CONFIG
#define MQTT_CONFIG "/data/codexmqtt/config.json"
#endif
#ifndef WPA_CONFIG
#define WPA_CONFIG "/etc/wpa_supplicant.conf"
#endif
#ifndef HUB_ID_FILE
#define HUB_ID_FILE "/data/codex/hub_id"
#endif
#define CLOUD_BLOCKER_CONFIG "/data/codex/cloud_blocker.conf"
#ifndef DEVICE_LIST
#define DEVICE_LIST "/data/resources/DeviceList.json"
#endif
#ifndef FUNCTION_LIST
#define FUNCTION_LIST "/data/resources/FunctionList.json"
#endif
#ifndef PROTOCOL_LIST
#define PROTOCOL_LIST "/data/resources/ProtocolList.json"
#endif
#ifndef ACTIVITY_LIST
#define ACTIVITY_LIST "/data/resources/ActivityList.json"
#endif
#ifndef MAP_LIST
#define MAP_LIST "/data/resources/MapList.json"
#endif
#ifndef RESOURCE_RELOAD_FLAG
#define RESOURCE_RELOAD_FLAG "/data/codex/reload_resources"
#endif
#define RESOURCE_BACKUP_DIR "/data/codex/resource-backups"
#define IR_EVENT_LOG "/data/codex/ir-events.log"
#define IR_CANCEL_PREFIX "/tmp/codex_ir_cancel_"
#define IR_HOLD_PREFIX "/tmp/codex_ir_hold_"
#ifndef IR_SEND_LOCK
#define IR_SEND_LOCK "/tmp/codex_ir_send.lock"
#endif
#define BT_TEXT_FIFO "/tmp/bthid_input"
#define BT_TEXT_STATUS "/tmp/bthid_status"
#define BT_TARGET_FILE "/data/codex/bthid_target"
#ifndef BT_DEVICE_STORE
#define BT_DEVICE_STORE "/data/codex/bt-devices.json"
#endif
#ifndef CODEX_BIN_DIR
#define CODEX_BIN_DIR "/data/codex/bin"
#endif
#define IR_EVENT_MAX_BYTES 65536
#define MAX_REQUEST_BODY (512 * 1024)
#define MAX_REQUEST_BYTES (MAX_REQUEST_BODY + 8192)
#define MAX_RESOURCE_FILE (2 * 1024 * 1024)
#define MAX_IR_DEVICES 32
#define MAX_IR_COMMANDS 160
#define MAX_IR_STORED_COMMANDS 2048
#define MAX_IR_BATCH_COMMANDS 1024
#define MAX_BT_SEQUENCE_BODY 32768
#define MAX_BT_DEVICES 12
#define MAX_BT_COMMANDS 32
#define MAX_BT_SCRIPT_LEN 2048

static const char *BUILTIN_PROTOCOL_TOSHIBA_32 =
    "{\"IRSegments\":[{\"Header\":[{\"Value\":8990,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":4490,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Payload\":{\"NumberOfBits\":32,\"Encodings\":[{\"Atoms\":[{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":552,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"BitType\":0},{\"Atoms\":[{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":1662,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"BitType\":1}],\"ToggleBit\":null,\"EncodingType\":0},\"Trailer\":[{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null}],\"TotalLength\":107870,\"Name\":\"Toshiba 32 Bit\"}],\"Attributes\":[],\"IsPadded\":true,\"IsFullSequence\":true,\"Rating\":null,\"NumberOfLinkedLanguage\":0,\"Status\":null,\"IsPublic\":true,\"HoldDelay\":null,\"PressMinimumRepeats\":1,\"SendingType\":0,\"Name\":\"Toshiba 32 Bit\",\"ControlSection\":null,\"Flags\":[],\"__type\":\"IrProtocol\",\"CarrierFrequency\":38000,\"Id-\":2,\"CodeSegments\":[{\"Header\":[{\"Value\":8990,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":2230,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Payload\":null,\"TotalLength\":0,\"Trailer\":[{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":96077,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Atoms\":[{\"Value\":8990,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":2230,\"Type\":0,\"MinValue\":null,\"MaxValue\":null},{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":96077,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Name\":\"Toshiba 32 Bit KeyCodeRepeat\"}],\"KeyCode\":{\"Start\":[{\"SegmentType\":1,\"SegmentName\":\"Toshiba 32 Bit\"}],\"Repeat\":[{\"SegmentType\":0,\"SegmentName\":\"Toshiba 32 Bit KeyCodeRepeat\"}],\"Finish\":null},\"HoldMinimumRepeats\":null,\"RelatedProtocols\":[]}";

static const char *BUILTIN_PROTOCOL_MEMOREX_O1 =
    "{\"IRSegments\":[{\"Header\":[{\"Value\":9000,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":4500,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Payload\":{\"NumberOfBits\":32,\"Encodings\":[{\"Atoms\":[{\"Value\":560,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":560,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"BitType\":0},{\"Atoms\":[{\"Value\":560,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":1690,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"BitType\":1}],\"ToggleBit\":null,\"EncodingType\":0},\"Trailer\":[{\"Value\":560,\"Type\":1,\"MinValue\":null,\"MaxValue\":null}],\"TotalLength\":107600,\"Name\":\"MemorexO1 32 Bit\"}],\"Attributes\":[],\"IsPadded\":null,\"IsFullSequence\":null,\"Rating\":null,\"NumberOfLinkedLanguage\":0,\"Status\":null,\"IsPublic\":true,\"HoldDelay\":null,\"PressMinimumRepeats\":null,\"SendingType\":0,\"Name\":\"MemorexO1 32 Bit\",\"ControlSection\":null,\"Flags\":[],\"__type\":\"IrProtocol\",\"CarrierFrequency\":38000,\"Id-\":679,\"CodeSegments\":[],\"KeyCode\":{\"Start\":null,\"Repeat\":[{\"SegmentType\":1,\"SegmentName\":\"MemorexO1 32 Bit\"}],\"Finish\":null},\"HoldMinimumRepeats\":null,\"RelatedProtocols\":[]}";

struct request {
    char method[8];
    char path[256];
    char auth[512];
    char host[128], origin[256], cookie[512], csrf[80], revision[32];
    int body_truncated;
    char *body;
    size_t body_len;
};

struct wifi_config {
    int hidden;
    int open;
    char ssid[256];
    char psk[256];
};

struct bt_saved_command {
    char name[128];
    char script[MAX_BT_SCRIPT_LEN];
    int delay_ms;
};

struct bt_saved_device {
    char id[40];
    char name[128];
    char type[40];
    char bdaddr[32];
    int command_count;
    struct bt_saved_command commands[MAX_BT_COMMANDS];
};

struct bt_inventory {
    int device_count;
    struct bt_saved_device devices[MAX_BT_DEVICES];
};

static void analyze_capture_storage(const char *raw_code, const char *keycode_in, const char *nec_in, const char *protocol_text, char *mode_out, size_t mode_len, char *keycode_out, size_t keycode_len, char *nec_out, size_t nec_len, int *protocol_id_out, char *summary, size_t summary_len);
static int repair_known_protocols_for_current_commands(void);
static int safe_bt_addr(const char *s);
static int bt_type_allowed(const char *type);
static void save_bthid_target(const char *type, const char *bdaddr);
static int run_bt_saved_script(const char *type, const char *bdaddr, const char *script, int gap_ms, char *out, size_t outlen);

static void chomp(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = 0;
    }
}

static int read_text(const char *path, char *out, size_t outlen) {
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f) {
        if (outlen) out[0] = 0;
        return -1;
    }
    n = fread(out, 1, outlen - 1, f);
    out[n] = 0;
    fclose(f);
    return (int)n;
}

static char *read_file_alloc(const char *path, size_t maxlen, size_t *outlen) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    size_t got;
    if (outlen) *outlen = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 0 || (size_t)n > maxlen) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    if (outlen) *outlen = got;
    return buf;
}

static int write_file_atomic(const char *path, const char *data, size_t len) {
    char tmp[320];
    FILE *f;
    int fd, rc, parent;
    char dir[256], *slash;
    snprintf(tmp, sizeof(tmp), "%s.new-XXXXXX", path);
    fd = mkstemp(tmp);
    if (fd < 0) return -1;
    f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(tmp); return -1; }
    if (fwrite(data, 1, len, f) != len) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    rc = fflush(f);
    if (rc == 0) rc = fsync(fileno(f));
    if (fclose(f) != 0) rc = -1;
    if (rc != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    if (slash) { *slash = 0; parent = open(dir, O_RDONLY); if (parent >= 0) { fsync(parent); close(parent); } }
    return 0;
}

static char *json_escape_alloc(const char *s) {
    cJSON *value = cJSON_CreateString(s); char *out = cJSON_PrintUnformatted(value);
    cJSON_Delete(value); return out;
}

static int safe_label(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    if (!s[0]) return 0;
    while (*p) {
        if (*p < 32 || *p == 127) return 0;
        if (*p == '"' || *p == '\\') return 0;
        p++;
    }
    return 1;
}

static int safe_run_id(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s);
    if (n == 0 || n > 96) return 0;
    while (*p) {
        if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.') return 0;
        p++;
    }
    return 1;
}

static void shell_escape_single(const char *s, char *out, size_t outlen) {
    size_t w = 0;
    if (outlen == 0) return;
    while (*s && w + 5 < outlen) {
        if (*s == '\'') {
            memcpy(out + w, "'\\''", 4);
            w += 4;
        } else {
            out[w++] = *s;
        }
        s++;
    }
    out[w] = 0;
}

static int run_cmd(const char *cmd, char *out, size_t outlen) {
    FILE *p = popen(cmd, "r");
    size_t n = 0;
    if (!p) {
        if (outlen) out[0] = 0;
        return -1;
    }
    if (outlen) {
        n = fread(out, 1, outlen - 1, p);
        out[n] = 0;
    }
    return pclose(p);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '+') {
            *w++ = ' ';
            r++;
        } else if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            *w++ = (char)(hexval(r[1]) * 16 + hexval(r[2]));
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

static void form_value(const char *body, const char *name, char *out, size_t outlen) {
    size_t namelen = strlen(name);
    const char *p = body;
    if (!outlen) return;
    out[0] = 0;
    if (!body) return;
    while (p && *p) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        size_t keylen, vallen;
        if (!eq) return;
        if (amp && amp < eq) {
            p = amp + 1;
            continue;
        }
        keylen = (size_t)(eq - p);
        if (keylen == namelen && strncmp(p, name, namelen) == 0) {
            const char *vstart = eq + 1;
            const char *vend = amp ? amp : p + strlen(p);
            vallen = (size_t)(vend - vstart);
            if (vallen >= outlen) vallen = outlen - 1;
            memcpy(out, vstart, vallen);
            out[vallen] = 0;
            url_decode(out);
            return;
        }
        p = amp ? amp + 1 : NULL;
    }
}

static void query_value(const char *path, const char *name, char *out, size_t outlen) {
    const char *q = strchr(path, '?');
    if (!q) {
        if (outlen) out[0] = 0;
        return;
    }
    form_value(q + 1, name, out, outlen);
}

static void send_text(int fd, const char *status, const char *body) {
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        status);
    send(fd, hdr, strlen(hdr), 0);
    send(fd, body, strlen(body), 0);
}

static void send_all(int fd, const char *data, size_t len) {
    while (len > 0) {
        ssize_t sent = send(fd, data, len, 0);
        if (sent <= 0) return;
        data += sent;
        len -= (size_t)sent;
    }
}

static void send_file_download(int fd, const char *path, const char *filename, const char *ctype) {
    char hdr[512];
    char *data;
    size_t len = 0;
    data = read_file_alloc(path, MAX_RESOURCE_FILE, &len);
    if (!data) {
        send_text(fd, "404 Not Found", "file not available\n");
        return;
    }
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
        "Cache-Control: no-store\r\nContent-Disposition: attachment; filename=\"%s\"\r\nConnection: close\r\n\r\n",
        ctype, (unsigned long)len, filename);
    send_all(fd, hdr, strlen(hdr));
    send_all(fd, data, len);
    free(data);
}

static void send_bt_devices_download(int fd) {
    char hdr[512];
    char *data;
    const char *empty = "{\"version\":1,\"devices\":[]}\n";
    size_t len = 0;
    int owned = 1;
    data = read_file_alloc(BT_DEVICE_STORE, MAX_RESOURCE_FILE, &len);
    if (!data) {
        data = (char *)empty;
        len = strlen(empty);
        owned = 0;
    }
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\n"
        "Cache-Control: no-store\r\nContent-Disposition: attachment; filename=\"bt-devices.json\"\r\nConnection: close\r\n\r\n",
        (unsigned long)len);
    send_all(fd, hdr, strlen(hdr));
    send_all(fd, data, len);
    if (owned) free(data);
}

static FILE *send_json_start(int fd, const char *status) {
    char hdr[256];
    FILE *f;
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        status);
    send(fd, hdr, strlen(hdr), 0);
    f = fdopen(dup(fd), "w");
    return f;
}

static cJSON *lj_get(const cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }
static const char *lj_str(const cJSON *o, const char *key) {
    cJSON *v = lj_get(o, key); return cJSON_IsString(v) ? v->valuestring : "";
}
static int lj_int(const cJSON *o, const char *key, int def) {
    cJSON *v = lj_get(o, key); return cJSON_IsNumber(v) ? v->valueint : def;
}
static int lj_unique(const cJSON *v, int depth) {
    const cJSON *a, *b;
    if (depth > 32) return 0;
    for (a = v->child; a; a = a->next) {
        if (cJSON_IsObject(v)) for (b = a->next; b; b = b->next)
            if (a->string && b->string && strcmp(a->string, b->string) == 0) return 0;
        if (!lj_unique(a, depth + 1)) return 0;
    }
    return 1;
}
static cJSON *lj_parse(const char *s) {
    cJSON *o = s ? cJSON_ParseWithOpts(s, NULL, 1) : NULL;
    if (o && !lj_unique(o, 0)) { cJSON_Delete(o); o = NULL; }
    return o;
}
static cJSON *lj_read(const char *path, size_t max) {
    char *raw = read_file_alloc(path, max, NULL);
    cJSON *o = lj_parse(raw); free(raw); return o;
}
static int lj_write(const char *path, const cJSON *o) {
    char *raw = cJSON_PrintUnformatted(o);
    int rc = raw ? write_file_atomic(path, raw, strlen(raw)) : -1;
    free(raw); return rc;
}
static void lj_reply(int fd, const char *status, const cJSON *o) {
    char *raw = cJSON_PrintUnformatted(o); FILE *f = send_json_start(fd, status);
    if (f) { fputs(raw ? raw : "{\"ok\":false,\"error\":\"Out of memory\"}", f); fclose(f); }
    free(raw);
}
static void local_error(int fd, const char *status, const char *msg) {
    cJSON *o = cJSON_CreateObject(); cJSON_AddBoolToObject(o, "ok", 0);
    cJSON_AddStringToObject(o, "error", msg); lj_reply(fd, status, o); cJSON_Delete(o);
}

static void json_write_string(FILE *f, const char *s) {
    char *raw = json_escape_alloc(s);
    fputs(raw ? raw : "null", f); free(raw);
}

static void parse_wpa_quoted(const char *raw, const char *key, char *out, size_t outlen) {
    char needle[32];
    const char *p;
    char *w = out;
    snprintf(needle, sizeof(needle), "%s=", key);
    p = strstr(raw, needle);
    out[0] = 0;
    if (!p) return;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return;
    p++;
    while (*p && *p != '"' && (size_t)(w - out) + 1 < outlen) {
        if (*p == '\\' && p[1]) p++;
        *w++ = *p++;
    }
    *w = 0;
}

static void load_wifi(struct wifi_config *cfg) {
    char raw[4096];
    memset(cfg, 0, sizeof(*cfg));
    if (read_text(WPA_CONFIG, raw, sizeof(raw)) <= 0) return;
    parse_wpa_quoted(raw, "ssid", cfg->ssid, sizeof(cfg->ssid));
    parse_wpa_quoted(raw, "psk", cfg->psk, sizeof(cfg->psk));
    cfg->hidden = strstr(raw, "scan_ssid=1") != NULL;
    cfg->open = strstr(raw, "key_mgmt=NONE") != NULL;
}

static void wpa_write_quoted(FILE *f, const char *s) {
    fputc('"', f);
    while (*s) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        fputc(*s, f);
        s++;
    }
    fputc('"', f);
}

static int save_wifi(const struct wifi_config *cfg) {
    FILE *f = fopen(WPA_CONFIG ".new", "w");
    if (!f) return -1;
    fprintf(f, "ctrl_interface=/var/run/wpa_supplicant\n");
    fprintf(f, "ap_scan=1\n\n");
    fprintf(f, "network={\n\tssid=");
    wpa_write_quoted(f, cfg->ssid);
    fprintf(f, "\n");
    if (cfg->hidden) fprintf(f, "\tscan_ssid=1\n");
    if (cfg->open) {
        fprintf(f, "\tkey_mgmt=NONE\n");
    } else {
        fprintf(f, "\tkey_mgmt=WPA-PSK\n\tpsk=");
        wpa_write_quoted(f, cfg->psk);
        fprintf(f, "\n");
    }
    fprintf(f, "}\n");
    fclose(f);
    chmod(WPA_CONFIG ".new", 0600);
    if (rename(WPA_CONFIG ".new", WPA_CONFIG) != 0) return -1;
    chmod(WPA_CONFIG, 0600);
    sync();
    return 0;
}

static int load_hub_id(char *hub_id, size_t hub_id_len) {
    size_t i, n;
    if (!hub_id || hub_id_len == 0) return 0;
    hub_id[0] = '\0';
    read_text(HUB_ID_FILE, hub_id, hub_id_len);
    chomp(hub_id);
    n = strlen(hub_id);
    if (n < 4) return 0;
    for (i = 0; i < n; i++) {
        if (!isdigit((unsigned char)hub_id[i])) return 0;
    }
    return 1;
}

static void trigger_mqtt_discover(void) {
    char hub_id[64];
    char escaped[128];
    char cmd[384];
    if (!load_hub_id(hub_id, sizeof(hub_id))) return;
    shell_escape_single(hub_id, escaped, sizeof(escaped));
    snprintf(cmd, sizeof(cmd),
        "/data/codex/bin/codex_hbus '%s' harmony.automation?discover '{\"gatewayType\":\"codexmqtt\"}' >/dev/null 2>&1 &",
        escaped);
    system(cmd);
}

/* Stock resource objects may contain fields unknown to this userspace package. */
static cJSON *resource_read(const char *path, const char *key) {
    cJSON *root = lj_read(path, MAX_RESOURCE_FILE);
    if (!cJSON_IsObject(root) || !cJSON_IsArray(lj_get(root, key))) { cJSON_Delete(root); return NULL; }
    return root;
}

static int resource_write(const char *path, const cJSON *root) {
    char *raw = cJSON_PrintUnformatted(root);
    int rc = raw && strlen(raw) <= MAX_RESOURCE_FILE ? write_file_atomic(path, raw, strlen(raw)) : -1;
    free(raw); return rc;
}

static int resource_merge(cJSON *target, const cJSON *fields) {
    const cJSON *field;
    cJSON_ArrayForEach(field, fields) {
        cJSON *value = cJSON_Duplicate(field, 1); int ok;
        if (!value) return -1;
        ok = lj_get(target, field->string) ? cJSON_ReplaceItemInObjectCaseSensitive(target, field->string, value) :
            cJSON_AddItemToObject(target, field->string, value);
        if (!ok) { cJSON_Delete(value); return -1; }
    }
    return 0;
}

static cJSON *resource_device(const cJSON *root, const char *id) {
    cJSON *item; char expected[32];
    cJSON_ArrayForEach(item, lj_get(root, "DevicesWithFeatures")) {
        cJSON *value = lj_get(lj_get(item, "Device"), "Id-");
        if (!cJSON_IsNumber(value) || value->valuedouble <= 0 || value->valuedouble > INT_MAX ||
            value->valuedouble != value->valueint) continue;
        snprintf(expected, sizeof(expected), "%d", value->valueint);
        if (!strcmp(expected, id)) return item;
    }
    return NULL;
}

static cJSON *resource_command(const cJSON *commands, const char *name) {
    cJSON *command;
    cJSON_ArrayForEach(command, commands) if (!strcmp(lj_str(command, "Name"), name)) return command;
    return NULL;
}

static void scan_command_array(const cJSON *commands, int *count, long *max_id) {
    cJSON *command;
    if (count) *count = cJSON_GetArraySize(commands);
    cJSON_ArrayForEach(command, commands) {
        int id = lj_int(command, "Id-", 0);
        if (max_id && id > *max_id) *max_id = id;
    }
}

static void scan_ir_resource_stats(const cJSON *root, int *device_count, int *total_commands, long *max_device_id, long *max_command_id) {
    cJSON *item;
    if (device_count) *device_count = 0;
    if (total_commands) *total_commands = 0;
    if (max_device_id) *max_device_id = 0;
    if (max_command_id) *max_command_id = 0;
    cJSON_ArrayForEach(item, lj_get(root, "DevicesWithFeatures")) {
        int count = 0, id = lj_int(lj_get(item, "Device"), "Id-", 0);
        if (id > 0) {
            if (device_count) (*device_count)++;
            if (max_device_id && id > *max_device_id) *max_device_id = id;
        }
        scan_command_array(lj_get(item, "Commands"), &count, max_command_id);
        if (total_commands) *total_commands += count;
    }
}

static void capture_ir_command_action(char *out, size_t outlen);

static cJSON *command_inventory(const cJSON *commands) {
    cJSON *out = cJSON_CreateArray(), *command;
    cJSON_ArrayForEach(command, commands) {
        char id[32]; cJSON *item = cJSON_CreateObject(), *raw = lj_get(command, "Raw");
        snprintf(id, sizeof(id), "%d", lj_int(command, "Id-", 0));
        cJSON_AddStringToObject(item, "id", id);
        cJSON_AddStringToObject(item, "name", lj_str(command, "Name"));
        cJSON_AddStringToObject(item, "keycode", lj_str(command, "KeyCode"));
        cJSON_AddNumberToObject(item, "protocolId", lj_int(command, "ProtocolId", 0));
        cJSON_AddBoolToObject(item, "learned", cJSON_IsTrue(lj_get(command, "IsLearned")));
        cJSON_AddBoolToObject(item, "raw", raw && !cJSON_IsNull(raw) && !lj_str(command, "KeyCode")[0]);
        cJSON_AddItemToArray(out, item);
    }
    return out;
}

static void render_inventory_json(int fd) {
    cJSON *root = resource_read(DEVICE_LIST, "DevicesWithFeatures"), *out, *devices, *item;
    int count, commands;
    if (!root) { local_error(fd, "200 OK", "Unable to read inventory."); return; }
    scan_ir_resource_stats(root, &count, &commands, NULL, NULL);
    out = cJSON_CreateObject(); devices = cJSON_AddArrayToObject(out, "devices");
    cJSON_AddBoolToObject(out, "ok", 1); cJSON_AddNumberToObject(out, "deviceCount", count);
    cJSON_AddNumberToObject(out, "totalCommandCount", commands);
    cJSON_AddNumberToObject(out, "displayDeviceLimit", MAX_IR_DEVICES);
    cJSON_AddNumberToObject(out, "displayCommandLimit", MAX_IR_COMMANDS);
    cJSON_AddNumberToObject(out, "storageCommandLimit", MAX_IR_STORED_COMMANDS);
    cJSON_AddNumberToObject(out, "batchCommandLimit", MAX_IR_BATCH_COMMANDS);
    cJSON_AddNumberToObject(out, "requestBodyLimit", MAX_REQUEST_BODY);
    cJSON_AddNumberToObject(out, "resourceFileLimit", MAX_RESOURCE_FILE);
    cJSON_ArrayForEach(item, lj_get(root, "DevicesWithFeatures")) {
        cJSON *device = lj_get(item, "Device"), *entry; char id[32];
        if (lj_int(device, "Id-", 0) <= 0) continue;
        entry = cJSON_CreateObject(); snprintf(id, sizeof(id), "%d", lj_int(device, "Id-", 0));
        cJSON_AddStringToObject(entry, "id", id); cJSON_AddStringToObject(entry, "name", lj_str(device, "Name"));
        cJSON_AddStringToObject(entry, "manufacturer", lj_str(device, "Manufacturer"));
        cJSON_AddStringToObject(entry, "model", lj_str(device, "Model"));
        cJSON_AddStringToObject(entry, "type", lj_str(device, "DeviceTypeDisplayName"));
        cJSON_AddNumberToObject(entry, "controlPort", lj_int(device, "ControlPort", 7));
        cJSON_AddNumberToObject(entry, "transport", lj_int(device, "Transport", 1));
        cJSON_AddItemToObject(entry, "commands", command_inventory(lj_get(item, "Commands")));
        cJSON_AddItemToArray(devices, entry);
    }
    lj_reply(fd, "200 OK", out); cJSON_Delete(out); cJSON_Delete(root);
}

static void render_device_commands_json(int fd, const struct request *req) {
    char id[64]; cJSON *root, *device, *out, *commands;
    if (!strcmp(req->method, "POST")) form_value(req->body, "deviceId", id, sizeof(id));
    else query_value(req->path, "deviceId", id, sizeof(id));
    if (!safe_label(id)) { local_error(fd, "400 Bad Request", "Invalid IR device id."); return; }
    root = resource_read(DEVICE_LIST, "DevicesWithFeatures"); device = resource_device(root, id);
    if (!device) { cJSON_Delete(root); local_error(fd, "404 Not Found", "Device not found."); return; }
    commands = command_inventory(lj_get(device, "Commands"));
    out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "ok", 1); cJSON_AddStringToObject(out, "deviceId", id);
    cJSON_AddNumberToObject(out, "count", cJSON_GetArraySize(commands)); cJSON_AddItemToObject(out, "commands", commands);
    lj_reply(fd, "200 OK", out); cJSON_Delete(out); cJSON_Delete(root);
}

static void render_capture_json(int fd) {
    char reply[4096];
    char mode[16], keycode[512], nec[64], summary[160];
    const char *p; int has_timing = 0;
    int protocol_id = 2;
    FILE *f;
    capture_ir_command_action(reply, sizeof(reply));
    for (p = reply; *p && !has_timing; p++) if (*p == 'F' && isxdigit((unsigned char)p[1])) {
        const char *end = p + 1; int timings = 0;
        while (isxdigit((unsigned char)*end)) end++;
        while ((*end == 'P' || *end == 'S') && isxdigit((unsigned char)end[1])) {
            timings++; end++; while (isxdigit((unsigned char)*end)) end++;
        }
        if (timings >= 4 && (size_t)(end - p) < sizeof(reply)) {
            size_t n = end - p; memmove(reply, p, n); reply[n] = 0; has_timing = 1;
        }
    }
    analyze_capture_storage(reply, "", "", "2", mode, sizeof(mode), keycode, sizeof(keycode), nec, sizeof(nec), &protocol_id, summary, sizeof(summary));
    if (!has_timing && !keycode[0] && !nec[0]) {
        f = send_json_start(fd, "502 Bad Gateway");
        if (f) { fputs("{\"ok\":false,\"error\":", f); json_write_string(f, reply[0] ? reply : "No IR signal received."); fputs("}", f); fclose(f); }
        return;
    }
    if (has_timing) { strcpy(mode, "raw"); keycode[0] = nec[0] = 0; }
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"raw\":", f); json_write_string(f, reply);
    fputs(",\"mode\":", f); json_write_string(f, mode);
    fprintf(f, ",\"protocolId\":%d,\"keycode\":", protocol_id); json_write_string(f, keycode);
    fputs(",\"nec\":", f); json_write_string(f, nec);
    fputs(",\"analysis\":", f); json_write_string(f, summary);
    fputs("}\n", f);
    fclose(f);
}

static int local_transaction_active, local_reload_deferred;
static void request_resource_reload(void) {
    const char *resources = "DeviceList\nFunctionList\nProtocolList\n";
    if (local_transaction_active) { local_reload_deferred = 1; return; }
    write_file_atomic(RESOURCE_RELOAD_FLAG, resources, strlen(resources));
    sync();
}

static void backup_resources(void) {
    char cmd[512];
    if (local_transaction_active) return;
    snprintf(cmd, sizeof(cmd),
        "mkdir -p " RESOURCE_BACKUP_DIR "; d=" RESOURCE_BACKUP_DIR "/$(date +%%Y%%m%%d_%%H%%M%%S); "
        "mkdir -p \"$d\"; cp " DEVICE_LIST " " FUNCTION_LIST " " PROTOCOL_LIST " \"$d\" 2>/dev/null");
    system(cmd);
}

static void backup_settings(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "mkdir -p " RESOURCE_BACKUP_DIR "; d=" RESOURCE_BACKUP_DIR "/settings_$(date +%%Y%%m%%d_%%H%%M%%S); "
        "mkdir -p \"$d\"; cp " MQTT_CONFIG " " WPA_CONFIG " " CLOUD_BLOCKER_CONFIG " " BT_DEVICE_STORE " \"$d\" 2>/dev/null");
    system(cmd);
}

static int protocol_list_has_id(const cJSON *root, int protocol_id) {
    cJSON *protocol;
    cJSON_ArrayForEach(protocol, lj_get(root, "Protocols")) if (lj_int(protocol, "Id-", 0) == protocol_id) return 1;
    return 0;
}

static int ensure_protocol_object(int protocol_id, const char *protocol_json) {
    cJSON *root = resource_read(PROTOCOL_LIST, "Protocols"), *protocol; int rc = -1;
    if (!root) return -1;
    if (protocol_list_has_id(root, protocol_id)) rc = 0;
    else {
        protocol = lj_parse(protocol_json);
        if (protocol && cJSON_AddItemToArray(lj_get(root, "Protocols"), protocol)) rc = resource_write(PROTOCOL_LIST, root);
        else cJSON_Delete(protocol);
    }
    cJSON_Delete(root); return rc;
}

static int ensure_builtin_protocol_for_id(int protocol_id) {
    if (protocol_id == 2) return ensure_protocol_object(2, BUILTIN_PROTOCOL_TOSHIBA_32);
    if (protocol_id == 679) return ensure_protocol_object(679, BUILTIN_PROTOCOL_MEMOREX_O1);
    return 0;
}

static int protocol_file_has_id(int protocol_id) {
    cJSON *root = resource_read(PROTOCOL_LIST, "Protocols");
    int present = root ? protocol_list_has_id(root, protocol_id) : -1;
    cJSON_Delete(root); return present;
}

static int repair_known_protocols_for_current_commands(void) {
    if (!local_transaction_active) return 0;
    cJSON *devices = resource_read(DEVICE_LIST, "DevicesWithFeatures"), *device, *command;
    int need2 = 0, need679 = 0, missing2 = 0, missing679 = 0, changed = 0;
    if (!devices) return -1;
    cJSON_ArrayForEach(device, lj_get(devices, "DevicesWithFeatures")) {
        cJSON_ArrayForEach(command, lj_get(device, "Commands")) {
            int id = lj_int(command, "ProtocolId", 0);
            if (id == 2) need2 = 1;
            if (id == 679) need679 = 1;
        }
    }
    cJSON_Delete(devices);
    if (need2) {
        int present = protocol_file_has_id(2);
        if (present < 0) return -1;
        missing2 = !present;
    }
    if (need679) {
        int present = protocol_file_has_id(679);
        if (present < 0) return -1;
        missing679 = !present;
    }
    if (!missing2 && !missing679) return 0;
    backup_resources();
    if (missing2 && ensure_builtin_protocol_for_id(2) != 0) return -1;
    if (missing679 && ensure_builtin_protocol_for_id(679) != 0) return -1;
    changed = missing2 || missing679;
    if (changed) request_resource_reload();
    return changed ? 1 : 0;
}

static int delete_ir_device(const char *device_id, char *msg, size_t msglen) {
    cJSON *root = resource_read(DEVICE_LIST, "DevicesWithFeatures"), *item = resource_device(root, device_id); int rc = -1;
    if (item) {
        backup_resources(); cJSON_Delete(cJSON_DetachItemViaPointer(lj_get(root, "DevicesWithFeatures"), item));
        rc = resource_write(DEVICE_LIST, root);
    }
    cJSON_Delete(root);
    if (!rc) request_resource_reload();
    snprintf(msg, msglen, rc ? "Failed to delete device %s." : "Deleted device %s.", device_id);
    return rc;
}

static int delete_ir_command(const char *device_id, const char *command_name, char *msg, size_t msglen) {
    cJSON *root = resource_read(DEVICE_LIST, "DevicesWithFeatures");
    cJSON *commands = lj_get(resource_device(root, device_id), "Commands"), *command = resource_command(commands, command_name);
    int rc = -1;
    if (command) {
        backup_resources(); cJSON_Delete(cJSON_DetachItemViaPointer(commands, command));
        rc = resource_write(DEVICE_LIST, root);
    }
    cJSON_Delete(root);
    if (!rc) request_resource_reload();
    snprintf(msg, msglen, rc ? "Failed to delete command %s." : "Deleted command %s.", command_name);
    return rc;
}


static int harmony_device_type_id(const char *type) {
    if (strcasecmp(type, "Amplifier") == 0) return 19;
    if (strcasecmp(type, "Television") == 0 || strcasecmp(type, "TV") == 0) return 2;
    if (strcasecmp(type, "Media Player") == 0) return 22;
    if (strcasecmp(type, "Game Console") == 0) return 32;
    if (strcasecmp(type, "HomeAppliance") == 0 || strcasecmp(type, "Home Appliance") == 0) return 44;
    return 44;
}

static int create_ir_device_ex(const char *name, const char *manufacturer, const char *model, const char *type, char *msg, size_t msglen, char *created_id, size_t created_id_len) {
    cJSON *root = NULL, *item = NULL, *device; long max_id = 0, id; int rc = -1;
    char date[64];
    if (!safe_label(name) || !safe_label(manufacturer) || !safe_label(model) || !safe_label(type)) {
        snprintf(msg, msglen, "Device fields cannot be empty or contain control characters."); return -1;
    }
    root = resource_read(DEVICE_LIST, "DevicesWithFeatures");
    if (!root) goto done;
    scan_ir_resource_stats(root, NULL, NULL, &max_id, NULL);
    if (max_id >= INT_MAX) goto done;
    id = max_id + 1;
    if (id < 90000000) id = 90000000 + (long)(time(NULL) % 9000000);
    item = lj_parse("{\"Device\":{\"DongleIndex\":0,\"InterDeviceDelay\":500,\"HoldInterKeyDelay\":100,\"DeviceType\":0,\"RegisterSection\":null,\"ContentProfileKey\":0,\"DeviceOrder\":0,\"DeviceProfileUri\":\"\",\"SetupState\":1,\"DeviceSearchType\":-1,\"GlobalDeviceVersionId-\":0,\"DeviceTypeDisplayName\":\"\",\"FriendlyName\":null,\"Transport\":1,\"PressMinRepeats\":3,\"PrivateAddType\":1,\"RegionalCharset\":null,\"Tokens\":null,\"BTAddress\":null,\"DongleRFID\":0,\"AutoDetectedDevice\":null,\"GlobalLanguageVersionId-\":0,\"ParentDeviceId\":null,\"DecodedEdid\":null,\"DefaultPressMinRepeats\":3,\"ParentDeviceModel\":\"\",\"RenewSection\":null,\"IsMultiCode\":false,\"GroupName\":null,\"DefaultInterDeviceDelay\":0,\"ParentDevice-\":0,\"DeviceAddedDate\":\"/Date(0000+0000)/\",\"ParentDeviceManufacturer\":\"\",\"AppLaunchConfigs\":null,\"PictureId\":null,\"IsKeyboardAssociated\":false,\"IsScartCableSupported\":false,\"Manufacturer\":\"\",\"Icon\":0,\"Id-\":0,\"IsInterKeyDelayOptimized\":false,\"SuggestedDisplay\":\"DEFAULT\",\"ControlPort\":7,\"EncodedEdid\":null,\"InterKeyDelay\":300,\"DeviceClassification\":0,\"DeviceCapabilitiesWithPriority\":[],\"CopiedDeviceSource\":-1,\"State\":1,\"HoldInterDeviceDelay\":0,\"Name\":\"\",\"Model\":\"\",\"ActivityIds\":null,\"Characterization\":0,\"HoldMinRepeats\":-1,\"DefaultInterKeyDelay\":0},\"Commands\":[],\"DeviceFeatures\":[]}");
    if (!item) goto done;
    device = lj_get(item, "Device");
    cJSON_SetNumberValue(lj_get(device, "Id-"), id);
    cJSON_SetNumberValue(lj_get(device, "ContentProfileKey"), id);
    cJSON_SetNumberValue(lj_get(device, "DeviceType"), harmony_device_type_id(type));
    cJSON_SetNumberValue(lj_get(device, "Icon"), harmony_device_type_id(type));
    snprintf(date, sizeof(date), "/Date(%ld000+0000)/", (long)time(NULL));
    if (!cJSON_SetValuestring(lj_get(device, "DeviceAddedDate"), date) ||
        !cJSON_SetValuestring(lj_get(device, "DeviceTypeDisplayName"), type) ||
        !cJSON_SetValuestring(lj_get(device, "ParentDeviceManufacturer"), manufacturer) ||
        !cJSON_SetValuestring(lj_get(device, "Manufacturer"), manufacturer) ||
        !cJSON_SetValuestring(lj_get(device, "Name"), name) ||
        !cJSON_SetValuestring(lj_get(device, "Model"), model)) goto done;
    if (!cJSON_AddItemToArray(lj_get(root, "DevicesWithFeatures"), item)) goto done;
    item = NULL; backup_resources(); rc = resource_write(DEVICE_LIST, root);
    if (!rc) {
        request_resource_reload();
        if (created_id && created_id_len) snprintf(created_id, created_id_len, "%ld", id);
    }
done:
    cJSON_Delete(item); cJSON_Delete(root);
    if (rc) snprintf(msg, msglen, "Failed to create IR device.");
    else snprintf(msg, msglen, "Created IR device %s (%ld).", name, id);
    return rc;
}

static int update_ir_device(const char *device_id, const char *name, const char *manufacturer, const char *model, const char *type, char *msg, size_t msglen) {
    cJSON *root, *device, *fields; int rc = -1;
    if (!safe_label(device_id) || !safe_label(name) || !safe_label(manufacturer) || !safe_label(model) || !safe_label(type)) {
        snprintf(msg, msglen, "Device fields cannot be empty or contain control characters."); return -1;
    }
    root = resource_read(DEVICE_LIST, "DevicesWithFeatures"); device = lj_get(resource_device(root, device_id), "Device");
    fields = cJSON_CreateObject();
    if (device && fields && cJSON_AddStringToObject(fields, "Name", name) &&
        cJSON_AddStringToObject(fields, "Manufacturer", manufacturer) &&
        cJSON_AddStringToObject(fields, "Model", model) && cJSON_AddStringToObject(fields, "DeviceTypeDisplayName", type)) {
        backup_resources();
        if (!resource_merge(device, fields)) rc = resource_write(DEVICE_LIST, root);
    }
    cJSON_Delete(fields); cJSON_Delete(root);
    if (!rc) request_resource_reload();
    snprintf(msg, msglen, rc ? "Failed to update device %s." : "Updated device %s.", device_id);
    return rc;
}

static int build_nec_keycode(const char *hex, int protocol_id, char *out, size_t outlen) {
    const char *p = hex;
    char clean[16];
    int n = 0;
    unsigned long value;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (*p && n < 8) {
        if (!isxdigit((unsigned char)*p)) return -1;
        clean[n++] = *p++;
    }
    if (*p || n == 0 || n > 8) return -1;
    clean[n] = 0;
    value = strtoul(clean, NULL, 16);
    if (protocol_id == 679) {
        snprintf(out, outlen, "G:MemorexO1 32 Bit:()(0x%08lX)():3", value & 0xffffffffUL);
    } else {
        snprintf(out, outlen, "G:Toshiba 32 Bit:(0x%08lX)(Repeat)():3", value & 0xffffffffUL);
    }
    return 0;
}

static void copy_text(char *out, size_t outlen, const char *value) {
    if (!out || !outlen) return;
    strncpy(out, value ? value : "", outlen - 1);
    out[outlen - 1] = 0;
}

static int contains_ci(const char *haystack, const char *needle) {
    size_t n;
    if (!haystack || !needle || !needle[0]) return 0;
    n = strlen(needle);
    while (*haystack) {
        if (strncasecmp(haystack, needle, n) == 0) return 1;
        haystack++;
    }
    return 0;
}

static int extract_harmony_keycode(const char *src, char *out, size_t outlen) {
    const char *p;
    size_t n = 0;
    if (!src || !outlen) return 0;
    p = strstr(src, "G:");
    if (!p) return 0;
    while (p[n] && p[n] != '"' && p[n] != '\'' && p[n] != '<' && p[n] != '>' &&
           p[n] != '\r' && p[n] != '\n' && n + 1 < outlen) {
        n++;
    }
    while (n && (isspace((unsigned char)p[n - 1]) || p[n - 1] == ',')) n--;
    if (n < 4) return 0;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

static int clean_hex_token(const char *src, char *out, size_t outlen) {
    const char *p;
    size_t n = 0;
    if (!src || !outlen) return 0;
    while (*src && isspace((unsigned char)*src)) src++;
    p = src;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (isxdigit((unsigned char)p[n]) && n < 8 && n + 1 < outlen) n++;
    if (n == 0 || n > 8) return 0;
    if (isxdigit((unsigned char)p[n])) return 0;
    while (p[n] && isspace((unsigned char)p[n])) n++;
    if (p[n]) return 0;
    p = src;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    n = 0;
    while (isxdigit((unsigned char)p[n]) && n < 8 && n + 1 < outlen) {
        out[n] = (char)toupper((unsigned char)p[n]);
        n++;
    }
    out[n] = 0;
    return 1;
}

static int extract_nec_hex(const char *src, char *out, size_t outlen) {
    const char *p;
    int hinted;
    if (!src || !outlen) return 0;
    if (clean_hex_token(src, out, outlen)) return 1;
    for (p = src; *p; p++) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            size_t i;
            p += 2;
            for (i = 0; i < 8 && isxdigit((unsigned char)p[i]); i++) {
                if (i + 1 < outlen) out[i] = (char)toupper((unsigned char)p[i]);
            }
            if (i == 8 && !isxdigit((unsigned char)p[i])) {
                out[8] = 0;
                return 1;
            }
            p--;
        }
    }
    hinted = contains_ci(src, "nec") || contains_ci(src, "samsung") ||
             contains_ci(src, "toshiba") || contains_ci(src, "memorex") ||
             contains_ci(src, "protocol") || contains_ci(src, "keycode");
    if (!hinted) return 0;
    for (p = src; *p; p++) {
        size_t i;
        if (isxdigit((unsigned char)*p) && (p == src || !isxdigit((unsigned char)p[-1]))) {
            for (i = 0; i < 8 && isxdigit((unsigned char)p[i]); i++) {
                if (i + 1 < outlen) out[i] = (char)toupper((unsigned char)p[i]);
            }
            if (i == 8 && !isxdigit((unsigned char)p[i])) {
                out[8] = 0;
                return 1;
            }
        }
    }
    return 0;
}

static int infer_capture_protocol(const char *raw_code, const char *keycode) {
    if (contains_ci(keycode, "MemorexO1") || contains_ci(raw_code, "MemorexO1")) return 679;
    return 2;
}

static void analyze_capture_storage(const char *raw_code, const char *keycode_in, const char *nec_in, const char *protocol_text, char *mode_out, size_t mode_len, char *keycode_out, size_t keycode_len, char *nec_out, size_t nec_len, int *protocol_id_out, char *summary, size_t summary_len) {
    int protocol_id = protocol_text && protocol_text[0] ? atoi(protocol_text) : 2;
    if (protocol_id <= 0) protocol_id = 2;
    if (mode_out && mode_len) mode_out[0] = 0;
    if (keycode_out && keycode_len) keycode_out[0] = 0;
    if (nec_out && nec_len) nec_out[0] = 0;
    if (summary && summary_len) summary[0] = 0;
    if (extract_harmony_keycode(keycode_in, keycode_out, keycode_len) ||
        extract_harmony_keycode(raw_code, keycode_out, keycode_len)) {
        protocol_id = infer_capture_protocol(raw_code, keycode_out);
        copy_text(mode_out, mode_len, "keycode");
        copy_text(summary, summary_len, "Decoded Harmony KeyCode; storing compact protocol/keycode data.");
    } else if (extract_nec_hex(nec_in, nec_out, nec_len) ||
               extract_nec_hex(raw_code, nec_out, nec_len)) {
        protocol_id = infer_capture_protocol(raw_code, keycode_in);
        copy_text(mode_out, mode_len, "nec");
        if (build_nec_keycode(nec_out, protocol_id, keycode_out, keycode_len) == 0) {
            copy_text(summary, summary_len, "Decoded NEC-style 32-bit value; storing as compact Harmony KeyCode.");
        } else {
            copy_text(summary, summary_len, "Decoded a hex value, but it was not valid for compact storage.");
        }
    } else {
        copy_text(mode_out, mode_len, "raw");
        copy_text(summary, summary_len, "No supported protocol signature found; storing raw timing data.");
    }
    if (protocol_id_out) *protocol_id_out = protocol_id;
}

static unsigned int reverse8(unsigned int v) {
    v = ((v & 0xf0) >> 4) | ((v & 0x0f) << 4);
    v = ((v & 0xcc) >> 2) | ((v & 0x33) << 2);
    v = ((v & 0xaa) >> 1) | ((v & 0x55) << 1);
    return v & 0xff;
}

static int build_irdb_nec_keycode(const char *protocol, const char *device, const char *subdevice, const char *function, char *out, size_t outlen) {
    unsigned long d, s, fn, inv;
    unsigned long value;
    if (strncasecmp(protocol, "NEC", 3) != 0 && strncasecmp(protocol, "Pioneer", 7) != 0) return -1;
    d = strtoul(device, NULL, 10);
    s = (!subdevice[0] || strcmp(subdevice, "-1") == 0) ? (d ^ 0xff) : strtoul(subdevice, NULL, 10);
    fn = strtoul(function, NULL, 10);
    if (d > 255 || s > 255 || fn > 255) return -1;
    inv = (~fn) & 0xff;
    value = (reverse8((unsigned int)d) << 24) |
            (reverse8((unsigned int)s) << 16) |
            (reverse8((unsigned int)fn) << 8) |
            reverse8((unsigned int)inv);
    snprintf(out, outlen, "G:Toshiba 32 Bit:(0x%08lX)(Repeat)():3", value & 0xffffffffUL);
    return 0;
}

static cJSON *build_ir_command(long id, const char *name, const char *mode, int protocol_id, const char *code, const char *raw_code) {
    cJSON *command = lj_parse("{\"Raw\":null,\"Id-\":0,\"KeyCode\":\"\",\"DateTaught\":\"\",\"FunctionId\":null,\"Parameters\":null,\"Name\":\"\",\"FunctionGroupId\":0,\"TransportType\":1,\"ProtocolId\":null,\"CommandTypeId\":\"\",\"IsLearned\":true}");
    char date[64]; int raw = !strcmp(mode, "raw"); cJSON *value;
    if (!command) return NULL;
    cJSON_SetNumberValue(lj_get(command, "Id-"), id);
    snprintf(date, sizeof(date), "/Date(%ld000+0000)/", (long)time(NULL));
    if (!cJSON_SetValuestring(lj_get(command, "Name"), name) ||
        !cJSON_SetValuestring(lj_get(command, "KeyCode"), raw ? "" : code) ||
        !cJSON_SetValuestring(lj_get(command, "DateTaught"), date)) goto fail;
    value = raw ? cJSON_CreateString(raw_code) : cJSON_CreateNumber(protocol_id);
    if (!value || !cJSON_ReplaceItemInObjectCaseSensitive(command, raw ? "Raw" : "ProtocolId", value)) {
        cJSON_Delete(value); goto fail;
    }
    return command;
fail:
    cJSON_Delete(command); return NULL;
}

static int update_ir_command(const char *device_id, const char *old_name, const char *new_name, const char *mode, const char *protocol_text, const char *nec, const char *keycode, const char *raw_code, char *msg, size_t msglen) {
    cJSON *root, *commands, *command;
    long id;
    int protocol_id = atoi(protocol_text);
    char effective_mode[16];
    char code[512];
    cJSON *cmd = NULL;
    if (!safe_label(device_id) || !safe_label(old_name) || !safe_label(new_name)) {
        snprintf(msg, msglen, "Device, current command name, and new command name are required.");
        return -1;
    }
    if (protocol_id <= 0) protocol_id = 2;
    root = resource_read(DEVICE_LIST, "DevicesWithFeatures");
    commands = lj_get(resource_device(root, device_id), "Commands");
    command = resource_command(commands, old_name);
    if (!root || !command) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Command %s was not found on device %s.", old_name, device_id);
        return -1;
    }
    if (strcmp(old_name, new_name) != 0 && resource_command(commands, new_name)) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Command %s already exists on device %s.", new_name, device_id);
        return -1;
    }
    id = lj_int(command, "Id-", 0);
    if (id <= 0) id = 39000000 + (long)(time(NULL) % 9000000);
    copy_text(effective_mode, sizeof(effective_mode), mode && mode[0] ? mode : "keycode");
    code[0] = 0;
    if (strcmp(effective_mode, "raw") == 0) {
        if (!raw_code[0]) {
            cJSON_Delete(root);
            snprintf(msg, msglen, "Raw command data is required.");
            return -1;
        }
    } else if (strcmp(effective_mode, "nec") == 0) {
        if (build_nec_keycode(nec, protocol_id, code, sizeof(code)) != 0) {
            cJSON_Delete(root);
            snprintf(msg, msglen, "NEC value must be 1-8 hex digits.");
            return -1;
        }
    } else {
        copy_text(effective_mode, sizeof(effective_mode), "keycode");
        if (!keycode[0]) {
            cJSON_Delete(root);
            snprintf(msg, msglen, "Harmony compact code is required.");
            return -1;
        }
        copy_text(code, sizeof(code), keycode);
    }
    cmd = build_ir_command(id, new_name, effective_mode, protocol_id, code, raw_code);
    if (!cmd) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Failed to build updated command.");
        return -1;
    }
    backup_resources();
    if (strcmp(effective_mode, "raw") != 0 && ensure_builtin_protocol_for_id(protocol_id) != 0) {
        cJSON_Delete(root);
        cJSON_Delete(cmd);
        snprintf(msg, msglen, "Failed to ensure IR protocol %d.", protocol_id);
        return -1;
    }
    if (resource_merge(command, cmd) != 0 || resource_write(DEVICE_LIST, root) != 0) {
        cJSON_Delete(root);
        cJSON_Delete(cmd);
        snprintf(msg, msglen, "Failed to save updated command %s.", new_name);
        return -1;
    }
    cJSON_Delete(root);
    cJSON_Delete(cmd);
    request_resource_reload();
    snprintf(msg, msglen, "Updated command %s on device %s.", new_name, device_id);
    return 0;
}

static int add_ir_command(const char *device_id, const char *name, const char *mode, const char *protocol_text, const char *nec, const char *keycode, const char *raw_code, char *msg, size_t msglen) {
    cJSON *root = NULL, *commands;
    long id, max_command_id = 0;
    int protocol_id = atoi(protocol_text);
    int existing_commands = 0;
    char effective_mode[16];
    char effective_keycode[512];
    char effective_nec[64];
    char storage_note[160];
    char code[512];
    cJSON *cmd = NULL;
    int auto_mode;
    if (!safe_label(device_id) || !safe_label(name)) {
        snprintf(msg, msglen, "Device and command name are required.");
        return -1;
    }
    if (protocol_id <= 0) protocol_id = 2;
    copy_text(effective_mode, sizeof(effective_mode), mode && mode[0] ? mode : "auto");
    copy_text(effective_keycode, sizeof(effective_keycode), keycode);
    copy_text(effective_nec, sizeof(effective_nec), nec);
    auto_mode = strcmp(effective_mode, "auto") == 0;
    if (auto_mode) {
        analyze_capture_storage(raw_code, keycode, nec, protocol_text, effective_mode, sizeof(effective_mode), effective_keycode, sizeof(effective_keycode), effective_nec, sizeof(effective_nec), &protocol_id, storage_note, sizeof(storage_note));
    }
    code[0] = 0;
    if (strcmp(effective_mode, "raw") == 0) {
        if (!raw_code[0]) {
            snprintf(msg, msglen, "Raw command data is required.");
            return -1;
        }
    } else if (strcmp(effective_mode, "keycode") == 0) {
        if (!effective_keycode[0]) {
            snprintf(msg, msglen, "KeyCode is required.");
            return -1;
        }
        strncpy(code, effective_keycode, sizeof(code) - 1);
        code[sizeof(code) - 1] = 0;
    } else {
        if (build_nec_keycode(effective_nec, protocol_id, code, sizeof(code)) != 0) {
            snprintf(msg, msglen, "NEC value must be 1-8 hex digits.");
            return -1;
        }
    }
    root = resource_read(DEVICE_LIST, "DevicesWithFeatures");
    commands = lj_get(resource_device(root, device_id), "Commands");
    if (!root || !cJSON_IsArray(commands)) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Device %s not found.", device_id);
        return -1;
    }
    scan_ir_resource_stats(root, NULL, NULL, NULL, &max_command_id);
    scan_command_array(commands, &existing_commands, NULL);
    if (existing_commands >= MAX_IR_STORED_COMMANDS) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Device %s already has the local storage limit of %d commands.", device_id, MAX_IR_STORED_COMMANDS);
        return -1;
    }
    if (resource_command(commands, name)) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Command %s already exists on device %s.", name, device_id);
        return -1;
    }
    if (max_command_id >= INT_MAX) goto fail;
    id = max_command_id + 1;
    if (id < 39000000) id = 39000000 + (long)(time(NULL) % 9000000);
    cmd = build_ir_command(id, name, effective_mode, protocol_id, code, raw_code);
    if (!cmd) goto fail;
    backup_resources();
    if (strcmp(effective_mode, "raw") != 0 && ensure_builtin_protocol_for_id(protocol_id) != 0) {
        snprintf(msg, msglen, "Failed to ensure IR protocol %d.", protocol_id);
        goto fail;
    }
    if (!cJSON_AddItemToArray(commands, cmd)) goto fail;
    cmd = NULL;
    if (resource_write(DEVICE_LIST, root) != 0) goto fail;
    request_resource_reload();
    if (auto_mode && storage_note[0]) snprintf(msg, msglen, "Saved command %s on device %s. %s", name, device_id, storage_note);
    else snprintf(msg, msglen, "Saved command %s on device %s.", name, device_id);
    cJSON_Delete(cmd); cJSON_Delete(root);
    return 0;
fail:
    cJSON_Delete(cmd); cJSON_Delete(root);
    if (!msg[0]) snprintf(msg, msglen, "Failed to save IR command.");
    return -1;
}

static char *trim_in_place(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static int split_fields(char *line, char sep, char **fields, int max_fields) {
    int count = 0;
    char *p = line;
    while (count < max_fields) {
        fields[count++] = p;
        p = strchr(p, sep);
        if (!p) break;
        *p++ = 0;
    }
    return count;
}

static int safe_raw_import_value(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s);
    if (n == 0 || n > 4096) return 0;
    while (*p) {
        if (*p < 32 || *p == 127) return 0;
        p++;
    }
    return 1;
}

static int append_text(char **buf, size_t *len, size_t *cap, const char *text, int comma) {
    size_t n = strlen(text);
    size_t need = *len + n + (comma ? 1 : 0) + 1;
    char *next;
    if (need > *cap) {
        size_t newcap = *cap ? *cap : 4096;
        while (newcap < need) newcap *= 2;
        next = (char *)realloc(*buf, newcap);
        if (!next) return -1;
        *buf = next;
        *cap = newcap;
    }
    if (comma) (*buf)[(*len)++] = ',';
    memcpy(*buf + *len, text, n);
    *len += n;
    (*buf)[*len] = 0;
    return 0;
}

static int bulk_import_irdb_commands(const char *device_id, char *payload, char *msg, size_t msglen, int strict) {
    char *line, *save;
    cJSON *root, *commands;
    long next_id, max_command_id = 0;
    int imported = 0, skipped = 0, remaining, existing_commands = 0, imported_keycodes = 0;
    if (!safe_label(device_id)) {
        snprintf(msg, msglen, "Invalid device for IRDB import.");
        return -1;
    }
    root = resource_read(DEVICE_LIST, "DevicesWithFeatures");
    commands = lj_get(resource_device(root, device_id), "Commands");
    if (!root || !cJSON_IsArray(commands)) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Failed to locate device command list.");
        return -1;
    }
    scan_ir_resource_stats(root, NULL, NULL, NULL, &max_command_id);
    scan_command_array(commands, &existing_commands, NULL);
    remaining = MAX_IR_STORED_COMMANDS - existing_commands;
    if (remaining <= 0) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Device %s already has the local storage limit of %d commands.", device_id, MAX_IR_STORED_COMMANDS);
        return -1;
    }
    if (max_command_id >= INT_MAX - remaining) { cJSON_Delete(root); snprintf(msg, msglen, "Command IDs exhausted."); return -1; }
    next_id = max_command_id + 1;
    if (next_id < 39000000) next_id = 39000000 + (long)(time(NULL) % 9000000);
    line = strtok_r(payload, "\n", &save);
    while (line) {
        char *fields[8], *name = NULL, *mode = "keycode", *keycode = NULL, *raw_code = NULL;
        char generated[512];
        cJSON *command;
        int field_count;
        line = trim_in_place(line);
        if (!line[0]) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (strchr(line, '|')) {
            field_count = split_fields(line, '|', fields, 8);
            if (field_count >= 2) {
                name = trim_in_place(fields[0]);
                if (field_count >= 3) {
                    mode = trim_in_place(fields[1]);
                    if (strcasecmp(mode, "raw") == 0) {
                        raw_code = trim_in_place(fields[2]);
                    } else if (strcasecmp(mode, "keycode") == 0) {
                        keycode = trim_in_place(fields[2]);
                    } else {
                        keycode = trim_in_place(fields[1]);
                        mode = "keycode";
                    }
                } else {
                    keycode = trim_in_place(fields[1]);
                }
            }
        } else {
            field_count = split_fields(line, ',', fields, 8);
            if (field_count >= 5 && strcasecmp(trim_in_place(fields[0]), "functionname") != 0) {
                name = trim_in_place(fields[0]);
                if (build_irdb_nec_keycode(trim_in_place(fields[1]), trim_in_place(fields[2]), trim_in_place(fields[3]), trim_in_place(fields[4]), generated, sizeof(generated)) == 0) {
                    keycode = generated;
                }
            }
        }
        if (!name || !name[0] || !safe_label(name) || resource_command(commands, name) ||
            (strcasecmp(mode, "raw") == 0 ? (!raw_code || !safe_raw_import_value(raw_code)) : (!keycode || !keycode[0]))) {
            skipped++;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (imported >= remaining) {
            skipped++;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        command = strcasecmp(mode, "raw") == 0 ?
            build_ir_command(next_id++, name, "raw", 2, "", raw_code) :
            build_ir_command(next_id++, name, "keycode", 2, keycode, "");
        if (!command || !cJSON_AddItemToArray(commands, command)) {
            cJSON_Delete(command); cJSON_Delete(root);
        snprintf(msg, msglen, "Failed to stage IRDB commands."); return -1;
        }
        imported++;
        if (strcasecmp(mode, "raw") != 0) imported_keycodes++;
        line = strtok_r(NULL, "\n", &save);
    }
    if (strict && skipped) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Profile contains duplicate, invalid or too many commands. Nothing was saved.");
        return -1;
    }
    if (imported == 0) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "No supported new IRDB commands were selected.");
        return -1;
    }
    backup_resources();
    if (imported_keycodes > 0 && ensure_builtin_protocol_for_id(2) != 0) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Failed to ensure NEC-compatible IR protocol.");
        return -1;
    }
    if (resource_write(DEVICE_LIST, root) != 0) {
        cJSON_Delete(root);
        snprintf(msg, msglen, "Failed to write imported IRDB commands.");
        return -1;
    }
    cJSON_Delete(root);
    request_resource_reload();
    snprintf(msg, msglen, "Imported %d IRDB commands to %s. Skipped %d.", imported, device_id, skipped);
    return 0;
}

static int ir_cancel_path(const char *run_id, char *path, size_t pathlen) {
    if (!run_id || !run_id[0]) return 0;
    if (!safe_run_id(run_id)) return -1;
    snprintf(path, pathlen, "%s%s", IR_CANCEL_PREFIX, run_id);
    return 1;
}

static int ir_run_canceled(const char *run_id) {
    char path[192];
    int ok = ir_cancel_path(run_id, path, sizeof(path));
    if (ok <= 0) return 0;
    return access(path, F_OK) == 0;
}

static void rotate_ir_event_log(void) {
    struct stat st;
    if (stat(IR_EVENT_LOG, &st) == 0 && st.st_size > IR_EVENT_MAX_BYTES) {
        unlink(IR_EVENT_LOG ".1");
        rename(IR_EVENT_LOG, IR_EVENT_LOG ".1");
    }
}

static void log_ir_event(const char *source, const char *run_id, const char *device_id, const char *command, const char *reply) {
    rotate_ir_event_log();
    FILE *f = fopen(IR_EVENT_LOG, "a");
    if (!f) return;
    fputs("{\"event\":\"ir_send\",\"ts\":", f);
    fprintf(f, "%ld", (long)time(NULL));
    fputs(",\"source\":", f); json_write_string(f, source ? source : "");
    fputs(",\"runId\":", f); json_write_string(f, run_id ? run_id : "");
    fputs(",\"deviceId\":", f); json_write_string(f, device_id ? device_id : "");
    fputs(",\"command\":", f); json_write_string(f, command ? command : "");
    fputs(",\"reply\":", f); json_write_string(f, reply ? reply : "");
    fputs("}\n", f);
    fclose(f);
}

static int ir_reply_ok(const char *reply) {
    cJSON *result = lj_parse(reply);
    int ok = lj_int(result, "code", -1) == 200 && !strcmp(lj_str(result, "cmd"), "harmony.engine?holdaction");
    cJSON_Delete(result); return ok;
}

static int prepare_ir(char *hub_id, size_t idlen, char *out, size_t outlen) {
    int i;
    if (access(RESOURCE_RELOAD_FLAG, F_OK) == 0) trigger_mqtt_discover();
    for (i = 0; i < 100; i++) {
        if (access(RESOURCE_RELOAD_FLAG, F_OK) != 0 && access(RESOURCE_RELOAD_FLAG ".loading", F_OK) != 0) break;
        usleep(100000);
    }
    if (i == 100) {
        snprintf(out, outlen, "Saved commands have not loaded into the hub. Restart the hub and try again.");
        return -1;
    }
    if (!load_hub_id(hub_id, idlen)) {
        snprintf(out, outlen, "Hub ID is missing. Re-run the root tool or reinstall with the numeric Hub ID printed as hub_id=...");
        return -1;
    }
    return 0;
}

static int lock_ir(char *out, size_t outlen) {
    int fd = open(IR_SEND_LOCK, O_CREAT | O_RDWR, 0600);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (fd >= 0) close(fd);
        snprintf(out, outlen, "Another IR command is running.");
        return -1;
    }
    return fd;
}

static int send_ir_command_unlocked(const char *device_id, const char *command, const char *source, const char *run_id, char *out, size_t outlen) {
    char hub_id[64], action[512], params[768], esc_id[128], esc_params[1024], cmd[1400];
    int rc;
    if (prepare_ir(hub_id, sizeof(hub_id), out, outlen) != 0) return -1;
    snprintf(action, sizeof(action), "{\\\"type\\\":\\\"IRCommand\\\",\\\"deviceId\\\":\\\"%s\\\",\\\"command\\\":\\\"%s\\\"}", device_id, command);
    snprintf(params, sizeof(params), "{\"status\":\"pressrelease\",\"count\":1,\"action\":\"%s\"}", action);
    shell_escape_single(hub_id, esc_id, sizeof(esc_id));
    shell_escape_single(params, esc_params, sizeof(esc_params));
    snprintf(cmd, sizeof(cmd), "/data/codex/bin/codex_hbus '%s' harmony.engine?holdaction '%s' 2>&1", esc_id, esc_params);
    rc = run_cmd(cmd, out, outlen);
    log_ir_event(source, run_id, device_id, command, out && out[0] ? out : "no response");
    if (rc == 0 && ir_reply_ok(out)) return 0;
    if (out[0] == '{') {
        cJSON *result = lj_parse(out);
        if (lj_str(result, "msg")[0]) snprintf(out, outlen, "Hub rejected command: %s", lj_str(result, "msg"));
        else snprintf(out, outlen, "Unexpected reply from the hub.");
        cJSON_Delete(result);
    } else if (!out[0]) snprintf(out, outlen, "No reply from the hub.");
    return -1;
}

static int send_ir_command_action_ex(const char *device_id, const char *command, const char *source, const char *run_id, char *out, size_t outlen) {
    int lock = lock_ir(out, outlen), rc;
    if (lock < 0) return -1;
    rc = send_ir_command_unlocked(device_id, command, source, run_id, out, outlen);
    close(lock);
    return rc;
}

static void capture_ir_command_action(char *out, size_t outlen) {
    char hub_id[64];
    char esc_id[128], cmd[512];
    if (!load_hub_id(hub_id, sizeof(hub_id))) {
        snprintf(out, outlen, "Hub ID is missing. Re-run the root tool or reinstall with the numeric Hub ID printed as hub_id=...");
        return;
    }
    shell_escape_single(hub_id, esc_id, sizeof(esc_id));
    snprintf(cmd, sizeof(cmd), "/data/codex/bin/codex_hbus '%s' ir.cap '{\"hbusData\":{\"timeout\":15000}}' 2>&1", esc_id);
    run_cmd(cmd, out, outlen);
    if (!out[0]) {
        snprintf(out, outlen, "IR capture returned no payload. The HAL capture path is present, but it only replies when a frame is received during the transaction.");
    }
}

static int safe_bt_store_id(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s);
    if (n == 0 || n > 36) return 0;
    while (*p) {
        if (!isalnum(*p) && *p != '_' && *p != '-') return 0;
        p++;
    }
    return 1;
}

static int safe_bt_script_text(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s);
    if (n == 0 || n >= MAX_BT_SCRIPT_LEN) return 0;
    while (*p) {
        if (*p < 32 && *p != '\n' && *p != '\r' && *p != '\t') return 0;
        if (*p == 127) return 0;
        p++;
    }
    return 1;
}

static void make_bt_device_id(char *out, size_t outlen) {
    snprintf(out, outlen, "bt_%ld_%d", (long)time(NULL), (int)(getpid() % 10000));
}

static int load_bt_inventory(struct bt_inventory *inv) {
    cJSON *root = resource_read(BT_DEVICE_STORE, "devices"), *item, *command;
    memset(inv, 0, sizeof(*inv));
    cJSON_ArrayForEach(item, lj_get(root, "devices")) {
        struct bt_saved_device *device;
        if (inv->device_count >= MAX_BT_DEVICES) break;
        device = &inv->devices[inv->device_count];
        copy_text(device->id, sizeof(device->id), lj_str(item, "id"));
        copy_text(device->name, sizeof(device->name), lj_str(item, "name"));
        copy_text(device->type, sizeof(device->type), lj_str(item, "type"));
        copy_text(device->bdaddr, sizeof(device->bdaddr), lj_str(item, "bdaddr"));
        if (!device->id[0] || !device->name[0]) continue;
        if (!device->type[0]) strcpy(device->type, "btkeyboard");
        cJSON_ArrayForEach(command, lj_get(item, "commands")) {
            struct bt_saved_command *saved;
            if (device->command_count >= MAX_BT_COMMANDS) break;
            saved = &device->commands[device->command_count];
            copy_text(saved->name, sizeof(saved->name), lj_str(command, "name"));
            copy_text(saved->script, sizeof(saved->script), lj_str(command, "script"));
            saved->delay_ms = lj_int(command, "delayMs", 35);
            if (saved->delay_ms < 15) saved->delay_ms = 35;
            if (saved->delay_ms > 5000) saved->delay_ms = 5000;
            if (saved->name[0] && saved->script[0]) device->command_count++;
        }
        inv->device_count++;
    }
    cJSON_Delete(root); return 0;
}

static int save_bt_inventory(const struct bt_inventory *inv) {
    cJSON *root = cJSON_CreateObject(), *devices = cJSON_AddArrayToObject(root, "devices"); int i, j, rc = -1;
    if (!devices || !cJSON_AddNumberToObject(root, "version", 1)) goto done;
    for (i = 0; i < inv->device_count; i++) {
        const struct bt_saved_device *device = &inv->devices[i];
        cJSON *item = cJSON_CreateObject(), *commands;
        if (!item || !cJSON_AddItemToArray(devices, item)) { cJSON_Delete(item); goto done; }
        commands = cJSON_AddArrayToObject(item, "commands");
        if (!commands || !cJSON_AddStringToObject(item, "id", device->id) || !cJSON_AddStringToObject(item, "name", device->name) ||
            !cJSON_AddStringToObject(item, "type", device->type) || !cJSON_AddStringToObject(item, "bdaddr", device->bdaddr)) goto done;
        for (j = 0; j < device->command_count; j++) {
            const struct bt_saved_command *saved = &device->commands[j]; cJSON *command = cJSON_CreateObject();
            if (!command || !cJSON_AddItemToArray(commands, command)) { cJSON_Delete(command); goto done; }
            if (!cJSON_AddStringToObject(command, "name", saved->name) || !cJSON_AddStringToObject(command, "script", saved->script) ||
                !cJSON_AddNumberToObject(command, "delayMs", saved->delay_ms)) goto done;
        }
    }
    rc = resource_write(BT_DEVICE_STORE, root);
done:
    cJSON_Delete(root);
    if (!rc) { chmod(BT_DEVICE_STORE, 0644); sync(); }
    return rc;
}

static int find_bt_device_index(const struct bt_inventory *inv, const char *device_id) {
    int i;
    for (i = 0; i < inv->device_count; i++) {
        if (strcmp(inv->devices[i].id, device_id) == 0) return i;
    }
    return -1;
}

static int find_bt_command_index(const struct bt_saved_device *dev, const char *name) {
    int i;
    for (i = 0; i < dev->command_count; i++) {
        if (strcmp(dev->commands[i].name, name) == 0) return i;
    }
    return -1;
}

static int upsert_bt_device(const char *device_id, const char *name, const char *type, const char *bdaddr, char *msg, size_t msglen) {
    struct bt_inventory inv;
    int idx;
    if (!safe_label(name) || !bt_type_allowed(type) || !safe_bt_addr(bdaddr)) {
        snprintf(msg, msglen, "Bluetooth device needs a name, supported keyboard type, and address.");
        return -1;
    }
    load_bt_inventory(&inv);
    if (device_id && device_id[0]) {
        if (!safe_bt_store_id(device_id)) {
            snprintf(msg, msglen, "Invalid Bluetooth device id.");
            return -1;
        }
        idx = find_bt_device_index(&inv, device_id);
        if (idx < 0) {
            snprintf(msg, msglen, "Bluetooth device %s was not found.", device_id);
            return -1;
        }
    } else {
        if (inv.device_count >= MAX_BT_DEVICES) {
            snprintf(msg, msglen, "Bluetooth device limit reached.");
            return -1;
        }
        idx = inv.device_count++;
        memset(&inv.devices[idx], 0, sizeof(inv.devices[idx]));
        make_bt_device_id(inv.devices[idx].id, sizeof(inv.devices[idx].id));
    }
    copy_text(inv.devices[idx].name, sizeof(inv.devices[idx].name), name);
    copy_text(inv.devices[idx].type, sizeof(inv.devices[idx].type), type);
    copy_text(inv.devices[idx].bdaddr, sizeof(inv.devices[idx].bdaddr), bdaddr);
    backup_settings();
    if (save_bt_inventory(&inv) != 0) {
        snprintf(msg, msglen, "Failed to save Bluetooth devices.");
        return -1;
    }
    save_bthid_target(type, bdaddr);
    snprintf(msg, msglen, "Saved Bluetooth device %s.", name);
    return 0;
}

static int delete_bt_device(const char *device_id, char *msg, size_t msglen) {
    struct bt_inventory inv;
    int idx, i;
    if (!safe_bt_store_id(device_id)) {
        snprintf(msg, msglen, "Invalid Bluetooth device id.");
        return -1;
    }
    load_bt_inventory(&inv);
    idx = find_bt_device_index(&inv, device_id);
    if (idx < 0) {
        snprintf(msg, msglen, "Bluetooth device %s was not found.", device_id);
        return -1;
    }
    for (i = idx; i + 1 < inv.device_count; i++) inv.devices[i] = inv.devices[i + 1];
    inv.device_count--;
    backup_settings();
    if (save_bt_inventory(&inv) != 0) {
        snprintf(msg, msglen, "Failed to delete Bluetooth device.");
        return -1;
    }
    snprintf(msg, msglen, "Deleted Bluetooth device %s.", device_id);
    return 0;
}

static int upsert_bt_command(const char *device_id, const char *old_name, const char *name, const char *script, int delay_ms, char *msg, size_t msglen) {
    struct bt_inventory inv;
    struct bt_saved_device *dev;
    int didx, cidx;
    if (!safe_bt_store_id(device_id) || !safe_label(name) || !safe_bt_script_text(script)) {
        snprintf(msg, msglen, "Bluetooth command needs a saved device, command name, and script.");
        return -1;
    }
    if (delay_ms < 15) delay_ms = 35;
    if (delay_ms > 5000) delay_ms = 5000;
    load_bt_inventory(&inv);
    didx = find_bt_device_index(&inv, device_id);
    if (didx < 0) {
        snprintf(msg, msglen, "Bluetooth device %s was not found.", device_id);
        return -1;
    }
    dev = &inv.devices[didx];
    if (old_name && old_name[0]) {
        if (!safe_label(old_name)) {
            snprintf(msg, msglen, "Invalid old Bluetooth command name.");
            return -1;
        }
        cidx = find_bt_command_index(dev, old_name);
        if (cidx < 0) {
            snprintf(msg, msglen, "Bluetooth command %s was not found.", old_name);
            return -1;
        }
        if (strcmp(old_name, name) != 0 && find_bt_command_index(dev, name) >= 0) {
            snprintf(msg, msglen, "Bluetooth command %s already exists.", name);
            return -1;
        }
    } else {
        if (find_bt_command_index(dev, name) >= 0) {
            snprintf(msg, msglen, "Bluetooth command %s already exists.", name);
            return -1;
        }
        if (dev->command_count >= MAX_BT_COMMANDS) {
            snprintf(msg, msglen, "Bluetooth command limit reached for this device.");
            return -1;
        }
        cidx = dev->command_count++;
        memset(&dev->commands[cidx], 0, sizeof(dev->commands[cidx]));
    }
    copy_text(dev->commands[cidx].name, sizeof(dev->commands[cidx].name), name);
    copy_text(dev->commands[cidx].script, sizeof(dev->commands[cidx].script), script);
    dev->commands[cidx].delay_ms = delay_ms;
    backup_settings();
    if (save_bt_inventory(&inv) != 0) {
        snprintf(msg, msglen, "Failed to save Bluetooth command.");
        return -1;
    }
    snprintf(msg, msglen, "Saved Bluetooth command %s.", name);
    return 0;
}

static int send_bt_saved_command(const char *device_id, const char *name, char *msg, size_t msglen) {
    struct bt_inventory inv;
    struct bt_saved_device *dev;
    struct bt_saved_command *cmd;
    int didx, cidx, rc;
    char reply[8192];
    if (!safe_bt_store_id(device_id) || !safe_label(name)) {
        snprintf(msg, msglen, "Invalid Bluetooth command request.");
        return -1;
    }
    load_bt_inventory(&inv);
    didx = find_bt_device_index(&inv, device_id);
    if (didx < 0) {
        snprintf(msg, msglen, "Bluetooth device %s was not found.", device_id);
        return -1;
    }
    dev = &inv.devices[didx];
    cidx = find_bt_command_index(dev, name);
    if (cidx < 0) {
        snprintf(msg, msglen, "Bluetooth command %s was not found.", name);
        return -1;
    }
    cmd = &dev->commands[cidx];
    reply[0] = 0;
    rc = run_bt_saved_script(dev->type, dev->bdaddr, cmd->script, cmd->delay_ms, reply, sizeof(reply));
    snprintf(msg, msglen, "%s Bluetooth command %s on %s. %s",
        rc == 0 ? "Sent" : "Failed to send", name, dev->name, reply[0] ? reply : "");
    return rc;
}

struct bt_quick_key {
    const char *code;
    const char *label;
};

static cJSON *command_result(int ok, const char *message) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", ok);
    cJSON_AddStringToObject(result, ok ? "reply" : "error", message);
    return result;
}

static cJSON *execute_ir_tap(const cJSON *body) {
    const char *device = lj_str(body, "deviceId"), *command = lj_str(body, "command");
    char reply[4096] = ""; int rc; cJSON *result;
    if (!safe_label(device) || strlen(device) >= 64 || !safe_label(command) || strlen(command) >= 128)
        return command_result(0, "Invalid IR command request.");
    repair_known_protocols_for_current_commands();
    rc = send_ir_command_action_ex(device, command, "api", "", reply, sizeof(reply));
    result = command_result(rc == 0, reply[0] ? reply : "no response");
    cJSON_AddStringToObject(result, "deviceId", device);
    cJSON_AddStringToObject(result, "command", command);
    if (rc != 0) cJSON_AddStringToObject(result, "reply", reply[0] ? reply : "no response");
    return result;
}

static int renew_ir_hold(const char *path) {
    struct timespec t;
    char text[64];
    clock_gettime(CLOCK_MONOTONIC, &t);
    snprintf(text, sizeof(text), "%lld\n", (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000);
    return write_file_atomic(path, text, strlen(text));
}

static cJSON *execute_ir_hold(const cJSON *body) {
    const char *device = lj_str(body, "deviceId"), *command = lj_str(body, "command");
    const char *run = lj_str(body, "runId"), *phase = lj_str(body, "phase");
    char lease[192], cancel[192];
    char hub[64], action[512], params[768], esc_hub[128], esc_params[1536], cmd[2304], reply[4096] = "";
    int lock = -1, rc = -1;
    if (!safe_run_id(run) || (strcmp(phase, "start") != 0 && strcmp(phase, "keepalive") != 0)) {
        snprintf(reply, sizeof(reply), "Invalid hold request.");
        goto respond;
    }
    snprintf(lease, sizeof(lease), IR_HOLD_PREFIX "%s", run);
    ir_cancel_path(run, cancel, sizeof(cancel));
    if (strcmp(phase, "keepalive") == 0) {
        /* A renewal must never create a hold or revive one that has stopped. */
        rc = access(lease, F_OK) == 0 && !ir_run_canceled(run) ? renew_ir_hold(lease) : 0;
        goto respond;
    }
    if (!safe_label(device) || strlen(device) >= 64 || !safe_label(command) || strlen(command) >= 128) {
        snprintf(reply, sizeof(reply), "Invalid IR command request.");
        goto respond;
    }
    lock = lock_ir(reply, sizeof(reply));
    if (lock < 0) goto respond;
    {
        cJSON *root = resource_read(DEVICE_LIST, "DevicesWithFeatures");
        int found = resource_command(lj_get(resource_device(root, device), "Commands"), command) != NULL;
        cJSON_Delete(root);
        if (!found) {
            snprintf(reply, sizeof(reply), "Saved IR command not found.");
            goto done;
        }
    }
    repair_known_protocols_for_current_commands();
    if (prepare_ir(hub, sizeof(hub), reply, sizeof(reply)) != 0) goto done;
    if (ir_run_canceled(run)) { rc = 0; goto done; }
    if (renew_ir_hold(lease) != 0) {
        snprintf(reply, sizeof(reply), "Cannot start hold timer.");
        goto done;
    }
    snprintf(action, sizeof(action), "{\\\"type\\\":\\\"IRCommand\\\",\\\"deviceId\\\":\\\"%s\\\",\\\"command\\\":\\\"%s\\\"}", device, command);
    snprintf(params, sizeof(params), "{\"action\":\"%s\"}", action);
    shell_escape_single(hub, esc_hub, sizeof(esc_hub));
    shell_escape_single(params, esc_params, sizeof(esc_params));
    snprintf(cmd, sizeof(cmd), CODEX_BIN_DIR "/codex_hbus '%s' harmony.engine?holdaction '%s' --hold '%s' '%s' 2>&1", esc_hub, esc_params, lease, cancel);
    rc = run_cmd(cmd, reply, sizeof(reply));
    log_ir_event("hold", run, device, command, reply);
done:
    unlink(lease);
    unlink(cancel);
    close(lock);
respond:
    return command_result(rc == 0, reply[0] ? reply : (rc == 0 ? "Stopped." : "Hold failed."));
}

static int safe_bt_addr(const char *s) {
    int i;
    if (strlen(s) != 17) return 0;
    for (i = 0; i < 17; i++) {
        if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
            if (s[i] != ':') return 0;
        } else if (!isxdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int safe_bt_pin(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    if (strlen(s) > 16) return 0;
    while (*p) {
        if (!isdigit(*p)) return 0;
        p++;
    }
    return 1;
}

static int safe_bt_name(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s);
    if (n == 0 || n > 48) return 0;
    while (*p) {
        if (!isalnum(*p) && *p != ' ' && *p != '_' && *p != '-' && *p != '.') return 0;
        p++;
    }
    return 1;
}

static int extract_bt_addr_from_text(const char *text, char *out, size_t outlen) {
    const char *p = text;
    if (!text || !out || outlen < 18) return 0;
    out[0] = 0;
    while (*p) {
        int i;
        if (isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]) &&
            p[2] == ':' && isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
            char candidate[18];
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

static int detect_connected_bt_addr(char *out, size_t outlen, char *raw, size_t rawlen) {
    char reply[2048];
    if (!out || outlen < 18) return 0;
    out[0] = 0;
    reply[0] = 0;
    run_cmd("hcitool con 2>&1", reply, sizeof(reply));
    if (raw && rawlen) {
        snprintf(raw, rawlen, "%s", reply);
    }
    return extract_bt_addr_from_text(reply, out, outlen);
}

static int bt_type_allowed(const char *type) {
    return strcmp(type, "fire") == 0 ||
        strcmp(type, "btkeyboard") == 0 ||
        strcmp(type, "btkeyboard-nexus") == 0 ||
        strcmp(type, "ps3") == 0 ||
        strcmp(type, "wii") == 0;
}

static void save_bthid_target(const char *type, const char *bdaddr) {
    char buf[128];
    if (!bt_type_allowed(type) || !safe_bt_addr(bdaddr) || strcmp(bdaddr, "00:00:00:00:00:00") == 0) return;
    snprintf(buf, sizeof(buf), "type=%s\nbdaddr=%s\n", type, bdaddr);
    write_file_atomic(BT_TARGET_FILE, buf, strlen(buf));
    chmod(BT_TARGET_FILE, 0644);
}

static int run_hal_json(const char *cmd_name, const char *params_json, int timeout, char *out, size_t outlen) {
    char esc_cmd[128], esc_params[2048], cmd[2400];
    if (timeout < 1) timeout = 1;
    if (timeout > 30) timeout = 30;
    shell_escape_single(cmd_name, esc_cmd, sizeof(esc_cmd));
    shell_escape_single(params_json, esc_params, sizeof(esc_params));
    snprintf(cmd, sizeof(cmd), "/data/codex/bin/codex_hal_ltcp '%s' '%s' %d 2>&1", esc_cmd, esc_params, timeout);
    return run_cmd(cmd, out, outlen);
}

static int run_hal_json_binary_sequence(const char *cmd_name, const char *params_json, const char *sequence, int timeout, int gap_ms, char *out, size_t outlen) {
    char esc_cmd[128], esc_params[2048], esc_path[256], cmd[3000], path[160];
    long stamp = (long)time(NULL);
    if (timeout < 1) timeout = 1;
    if (timeout > 30) timeout = 30;
    if (gap_ms < 15) gap_ms = 15;
    if (gap_ms > 5000) gap_ms = 5000;
    snprintf(path, sizeof(path), "/tmp/codex_bt_seq_%ld_%ld", (long)getpid(), stamp);
    if (write_file_atomic(path, sequence, strlen(sequence)) != 0) {
        snprintf(out, outlen, "failed to stage Bluetooth report sequence");
        return -1;
    }
    shell_escape_single(cmd_name, esc_cmd, sizeof(esc_cmd));
    shell_escape_single(params_json, esc_params, sizeof(esc_params));
    shell_escape_single(path, esc_path, sizeof(esc_path));
    snprintf(cmd, sizeof(cmd), "/data/codex/bin/codex_hal_ltcp '%s' '%s' %d --gap-ms=%d --seq-file='%s' 2>&1; rc=$?; rm -f '%s'; exit $rc",
        esc_cmd, esc_params, timeout, gap_ms, esc_path, esc_path);
    return run_cmd(cmd, out, outlen);
}

static int bt_hex_payload_from_input(const char *s, char *out, size_t outlen) {
    const char *p = s;
    int saw_prefix = 0, saw_separator = 0;
    size_t n = 0, digits = 0;
    if (!s || !out || outlen < 3) return 0;
    out[0] = 0;
    if (strncasecmp(p, "hex:", 4) == 0) {
        saw_prefix = 1;
        p += 4;
    }
    while (*p) {
        if (*p == ' ' || *p == ':' || *p == '-' || *p == '\t' || *p == '\r' || *p == '\n') {
            saw_separator = 1;
            p++;
            continue;
        }
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
            saw_prefix = 1;
            p += 2;
            continue;
        }
        if (!isxdigit((unsigned char)*p)) return 0;
        if (n + 2 >= outlen) return 0;
        out[n++] = (char)toupper((unsigned char)*p);
        digits++;
        p++;
    }
    out[n] = 0;
    if (!digits || (digits & 1)) return 0;
    if (!saw_prefix && !saw_separator && digits < 8) return 0;
    if (digits > 64) return 0;
    return 1;
}

static int bt_key_usage(const char *key) {
    if (!key || !key[0]) return -1;
    if (strlen(key) == 1) {
        if (key[0] >= 'a' && key[0] <= 'z') return 0x04 + (key[0] - 'a');
        if (key[0] >= '1' && key[0] <= '9') return 0x1e + (key[0] - '1');
        if (key[0] == '0') return 0x27;
    }
    if (strncmp(key, "number", 6) == 0 && key[6] && !key[7]) {
        if (key[6] >= '1' && key[6] <= '9') return 0x1e + (key[6] - '1');
        if (key[6] == '0') return 0x27;
    }
    if (key[0] == 'f' && isdigit((unsigned char)key[1])) {
        int n = atoi(key + 1);
        if (n >= 1 && n <= 12) return 0x3a + (n - 1);
    }
    if (strcmp(key, "enter") == 0 || strcmp(key, "return") == 0) return 0x28;
    if (strcmp(key, "escape") == 0 || strcmp(key, "esc") == 0 || strcmp(key, "back") == 0) return 0x29;
    if (strcmp(key, "backspace") == 0) return 0x2a;
    if (strcmp(key, "tab") == 0) return 0x2b;
    if (strcmp(key, "space") == 0) return 0x2c;
    if (strcmp(key, "minus") == 0 || strcmp(key, "dash") == 0) return 0x2d;
    if (strcmp(key, "equal") == 0 || strcmp(key, "equals") == 0) return 0x2e;
    if (strcmp(key, "leftbracket") == 0 || strcmp(key, "openbracket") == 0) return 0x2f;
    if (strcmp(key, "rightbracket") == 0 || strcmp(key, "closebracket") == 0) return 0x30;
    if (strcmp(key, "backslash") == 0) return 0x31;
    if (strcmp(key, "semicolon") == 0) return 0x33;
    if (strcmp(key, "apostrophe") == 0 || strcmp(key, "quote") == 0) return 0x34;
    if (strcmp(key, "grave") == 0 || strcmp(key, "graveaccent") == 0) return 0x35;
    if (strcmp(key, "comma") == 0) return 0x36;
    if (strcmp(key, "period") == 0 || strcmp(key, "dot") == 0) return 0x37;
    if (strcmp(key, "slash") == 0) return 0x38;
    if (strcmp(key, "capslock") == 0) return 0x39;
    if (strcmp(key, "printscreen") == 0) return 0x46;
    if (strcmp(key, "scrolllock") == 0) return 0x47;
    if (strcmp(key, "pause") == 0) return 0x48;
    if (strcmp(key, "insert") == 0) return 0x49;
    if (strcmp(key, "home") == 0) return 0x4a;
    if (strcmp(key, "pageup") == 0 || strcmp(key, "pgup") == 0) return 0x4b;
    if (strcmp(key, "delete") == 0 || strcmp(key, "del") == 0) return 0x4c;
    if (strcmp(key, "end") == 0) return 0x4d;
    if (strcmp(key, "pagedown") == 0 || strcmp(key, "pgdn") == 0) return 0x4e;
    if (strcmp(key, "directionright") == 0 || strcmp(key, "right") == 0) return 0x4f;
    if (strcmp(key, "directionleft") == 0 || strcmp(key, "left") == 0) return 0x50;
    if (strcmp(key, "directiondown") == 0 || strcmp(key, "down") == 0) return 0x51;
    if (strcmp(key, "directionup") == 0 || strcmp(key, "up") == 0) return 0x52;
    if (strcmp(key, "menu") == 0 || strcmp(key, "application") == 0) return 0x65;
    return -1;
}

static int bt_keyboard_report_hex(const char *input, char *press, size_t presslen, char *release, size_t releaselen, char *err, size_t errlen) {
    char norm[80], key[80];
    const char *p = input;
    size_t n = 0;
    unsigned int mod = 0;
    int usage;

    if (!input || !input[0]) {
        snprintf(err, errlen, "missing Bluetooth HID command");
        return -1;
    }
    if (bt_hex_payload_from_input(input, press, presslen)) {
        release[0] = 0;
        return 0;
    }
    while (*p && n + 1 < sizeof(norm)) {
        if (isalnum((unsigned char)*p)) norm[n++] = (char)tolower((unsigned char)*p);
        p++;
    }
    norm[n] = 0;
    copy_text(key, sizeof(key), norm);
    while (key[0]) {
        if (strncmp(key, "control", 7) == 0) {
            mod |= 0x01;
            memmove(key, key + 7, strlen(key + 7) + 1);
        } else if (strncmp(key, "ctrl", 4) == 0) {
            mod |= 0x01;
            memmove(key, key + 4, strlen(key + 4) + 1);
        } else if (strncmp(key, "shift", 5) == 0) {
            mod |= 0x02;
            memmove(key, key + 5, strlen(key + 5) + 1);
        } else if (strncmp(key, "altgr", 5) == 0) {
            mod |= 0x40;
            memmove(key, key + 5, strlen(key + 5) + 1);
        } else if (strncmp(key, "alt", 3) == 0) {
            mod |= 0x04;
            memmove(key, key + 3, strlen(key + 3) + 1);
        } else if (strncmp(key, "windows", 7) == 0) {
            mod |= 0x08;
            memmove(key, key + 7, strlen(key + 7) + 1);
        } else if (strncmp(key, "win", 3) == 0) {
            mod |= 0x08;
            memmove(key, key + 3, strlen(key + 3) + 1);
        } else if (strncmp(key, "cmd", 3) == 0 || strncmp(key, "meta", 4) == 0) {
            mod |= 0x08;
            memmove(key, key + (key[0] == 'm' ? 4 : 3), strlen(key + (key[0] == 'm' ? 4 : 3)) + 1);
        } else {
            break;
        }
    }
    usage = bt_key_usage(key);
    if (usage < 0) {
        snprintf(err, errlen, "unsupported Bluetooth keyboard command: %s", input);
        return -1;
    }
    snprintf(press, presslen, "A101%02X00%02X0000000000", mod & 0xff, usage & 0xff);
    snprintf(release, releaselen, "A1010000000000000000");
    return 0;
}

static int bt_sequence_add_code(const char *input, char **seq, size_t *seq_len, size_t *seq_cap, int *keys, char *err, size_t errlen) {
    char code[128], press[96], release[32];
    char *clean;
    copy_text(code, sizeof(code), input);
    clean = trim_in_place(code);
    while (*clean && clean[strlen(clean) - 1] == '\r') clean[strlen(clean) - 1] = 0;
    if (!clean[0]) return 0;
    if (bt_keyboard_report_hex(clean, press, sizeof(press), release, sizeof(release), err, errlen) != 0) {
        return -1;
    }
    /* Keep press and release on one sequence line. The HAL sequencer uses
       '|' for the short intra-key release delay, then newline for the gap
       before the next key. */
    if (append_text(seq, seq_len, seq_cap, press, 0) != 0 ||
        (release[0] && (append_text(seq, seq_len, seq_cap, "|", 0) != 0 ||
        append_text(seq, seq_len, seq_cap, release, 0) != 0)) ||
        append_text(seq, seq_len, seq_cap, "\n", 0) != 0) {
        snprintf(err, errlen, "not enough memory for Bluetooth report sequence");
        return -1;
    }
    if (keys) (*keys)++;
    return 0;
}

static int bthid_status_runtime_alive(const char *raw) {
    cJSON *status = lj_parse(raw);
    int pid = lj_int(status, "pid", 0), runtime = cJSON_IsTrue(lj_get(status, "runtime"));
    cJSON_Delete(status);
    if (!runtime) return 0;
    if (pid <= 0) {
        char ps[256];
        return run_cmd("ps | grep '[c]odex_bthid_keyboard'", ps, sizeof(ps)) == 0 && ps[0] != 0;
    }
    if (kill((pid_t)pid, 0) == 0) return 1;
    return errno == EPERM;
}

static int write_bt_text_fifo(const char *text, char *err, size_t errlen) {
    int fd, idle_waits = 0, max_idle_waits;
    size_t len, off = 0;
    if (!text || !text[0]) {
        snprintf(err, errlen, "missing Bluetooth text");
        return -1;
    }
    len = strlen(text);
    if (len > MAX_BT_SEQUENCE_BODY) {
        snprintf(err, errlen, "Bluetooth text is too large");
        return -1;
    }
    fd = open(BT_TEXT_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        char status[512];
        if (read_text(BT_TEXT_STATUS, status, sizeof(status)) > 0 && strstr(status, "\"runtime\":true")) {
            if (!bthid_status_runtime_alive(status)) {
                snprintf(err, errlen, "Bluetooth text runtime status is stale; restart bthid_keyboard");
            } else if (strstr(status, "\"state\":\"no_target\"")) {
                snprintf(err, errlen, "Bluetooth text runtime is waiting for a paired target");
            } else {
                snprintf(err, errlen, "Bluetooth text runtime is not ready for FIFO writes");
            }
        } else {
            snprintf(err, errlen, "Bluetooth text FIFO is not available; start bthid_keyboard first");
        }
        return -1;
    }
    max_idle_waits = 600 + (int)(len / 16);
    if (max_idle_waits > 6000) max_idle_waits = 6000;
    while (off < len) {
        ssize_t n = write(fd, text + off, len - off);
        if (n > 0) {
            off += (size_t)n;
            idle_waits = 0;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fd_set wfds;
            struct timeval tv;
            if (idle_waits++ >= max_idle_waits) {
                snprintf(err, errlen, "Bluetooth FIFO write timed out after %lu/%lu bytes; target may be disconnected or busy",
                    (unsigned long)off, (unsigned long)len);
                close(fd);
                return -1;
            }
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            tv.tv_sec = 0;
            tv.tv_usec = 10000;
            select(fd + 1, NULL, &wfds, NULL, &tv);
            continue;
        }
        snprintf(err, errlen, "Bluetooth FIFO write failed at byte %lu: %s", (unsigned long)off, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static void append_run_status(char *out, size_t outlen, const char *text) {
    size_t used;
    if (!out || !outlen || !text || !text[0]) return;
    used = strlen(out);
    if (used + 2 >= outlen) return;
    snprintf(out + used, outlen - used, "%s%s", used ? "\n" : "", text);
}

static int flush_bt_saved_sequence(const char *type, const char *bdaddr, char **seq, size_t *seq_len, size_t *seq_cap, int *chunk_keys, int gap_ms, int *total_keys, char *out, size_t outlen) {
    char params[256], reply[2048], note[256];
    char *jt = NULL, *ja = NULL;
    int rc;
    if (!seq || !*seq || !seq_len || *seq_len == 0 || !chunk_keys || *chunk_keys <= 0) return 0;
    jt = json_escape_alloc(type);
    ja = json_escape_alloc(bdaddr);
    if (!jt || !ja) {
        free(jt); free(ja);
        append_run_status(out, outlen, "not enough memory to send Bluetooth key sequence");
        return -1;
    }
    snprintf(params, sizeof(params), "{\"type\":%s,\"bdaddr\":%s}", jt, ja);
    free(jt);
    free(ja);
    reply[0] = 0;
    rc = run_hal_json_binary_sequence("bthid.report", params, *seq, 8, gap_ms, reply, sizeof(reply));
    if (rc != 0) {
        snprintf(note, sizeof(note), "key sequence failed after %d keys: %s", total_keys ? *total_keys : 0, reply[0] ? reply : "no response");
        append_run_status(out, outlen, note);
        free(*seq);
        *seq = NULL;
        *seq_len = 0;
        *seq_cap = 0;
        *chunk_keys = 0;
        return -1;
    }
    if (total_keys) *total_keys += *chunk_keys;
    snprintf(note, sizeof(note), "sent %d key%s", *chunk_keys, *chunk_keys == 1 ? "" : "s");
    append_run_status(out, outlen, note);
    free(*seq);
    *seq = NULL;
    *seq_len = 0;
    *seq_cap = 0;
    *chunk_keys = 0;
    return 0;
}

static int run_bt_saved_script(const char *type, const char *bdaddr, const char *script, int gap_ms, char *out, size_t outlen) {
    char *copy, *line, *save;
    char *seq = NULL;
    size_t seq_len = 0, seq_cap = 0, script_len;
    int chunk_keys = 0, total_keys = 0, text_bytes = 0, waits = 0;
    char err[256], note[256];
    if (out && outlen) out[0] = 0;
    if (!bt_type_allowed(type) || !safe_bt_addr(bdaddr)) {
        snprintf(out, outlen, "saved Bluetooth device has an invalid keyboard type or address");
        return -1;
    }
    if (!safe_bt_script_text(script)) {
        snprintf(out, outlen, "saved Bluetooth script is empty or contains unsupported control characters");
        return -1;
    }
    if (gap_ms < 15) gap_ms = 35;
    if (gap_ms > 5000) gap_ms = 5000;
    save_bthid_target(type, bdaddr);
    script_len = strlen(script);
    copy = (char *)malloc(script_len + 1);
    if (!copy) {
        snprintf(out, outlen, "not enough memory to run Bluetooth script");
        return -1;
    }
    memcpy(copy, script, script_len + 1);
    line = strtok_r(copy, "\n", &save);
    while (line) {
        char *clean = trim_in_place(line);
        char *arg = clean;
        while (*clean && clean[strlen(clean) - 1] == '\r') clean[strlen(clean) - 1] = 0;
        if (!clean[0] || clean[0] == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        while (*arg && !isspace((unsigned char)*arg)) arg++;
        if (*arg) {
            *arg++ = 0;
            while (*arg == ' ' || *arg == '\t') arg++;
        }
        if (strcasecmp(clean, "WAIT") == 0 || strcasecmp(clean, "SLEEP") == 0) {
            int ms = atoi(arg);
            if (flush_bt_saved_sequence(type, bdaddr, &seq, &seq_len, &seq_cap, &chunk_keys, gap_ms, &total_keys, out, outlen) != 0) {
                free(copy);
                return -1;
            }
            if (ms < 0) ms = 0;
            if (ms > 60000) ms = 60000;
            usleep((useconds_t)ms * 1000);
            waits++;
        } else if (strcasecmp(clean, "TEXT") == 0 || strcasecmp(clean, "TYPE") == 0) {
            size_t chars = strlen(arg);
            int settle_ms;
            if (flush_bt_saved_sequence(type, bdaddr, &seq, &seq_len, &seq_cap, &chunk_keys, gap_ms, &total_keys, out, outlen) != 0) {
                free(copy);
                return -1;
            }
            if (!chars) {
                snprintf(out, outlen, "TEXT step needs text after the command");
                free(copy);
                return -1;
            }
            if (write_bt_text_fifo(arg, err, sizeof(err)) != 0) {
                snprintf(out, outlen, "Bluetooth text helper failed: %s", err);
                free(copy);
                return -1;
            }
            text_bytes += (int)chars;
            settle_ms = 100 + (int)chars * (gap_ms + 5);
            if (settle_ms > 120000) settle_ms = 120000;
            usleep((useconds_t)settle_ms * 1000);
            snprintf(note, sizeof(note), "typed %lu text byte%s", (unsigned long)chars, chars == 1 ? "" : "s");
            append_run_status(out, outlen, note);
        } else {
            const char *key = clean;
            if (strcasecmp(clean, "KEY") == 0 || strcasecmp(clean, "SEND") == 0 ||
                strcasecmp(clean, "PRESS") == 0 || strcasecmp(clean, "COMBO") == 0 ||
                strcasecmp(clean, "HOTKEY") == 0) {
                key = arg;
            }
            if (!key || !key[0] || bt_sequence_add_code(key, &seq, &seq_len, &seq_cap, &chunk_keys, err, sizeof(err)) != 0) {
                snprintf(out, outlen, "%s", err[0] ? err : "unsupported Bluetooth keyboard script line");
                free(seq);
                free(copy);
                return -1;
            }
            if (chunk_keys >= 64 || seq_len > 12000) {
                if (flush_bt_saved_sequence(type, bdaddr, &seq, &seq_len, &seq_cap, &chunk_keys, gap_ms, &total_keys, out, outlen) != 0) {
                    free(copy);
                    return -1;
                }
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    if (flush_bt_saved_sequence(type, bdaddr, &seq, &seq_len, &seq_cap, &chunk_keys, gap_ms, &total_keys, out, outlen) != 0) {
        free(copy);
        return -1;
    }
    snprintf(note, sizeof(note), "script complete: %d key%s, %d text byte%s, %d wait%s",
        total_keys, total_keys == 1 ? "" : "s",
        text_bytes, text_bytes == 1 ? "" : "s",
        waits, waits == 1 ? "" : "s");
    append_run_status(out, outlen, note);
    free(copy);
    return 0;
}

static cJSON *execute_bluetooth_pair(const cJSON *body) {
    const char *action = lj_str(body, "action"), *type = lj_str(body, "type");
    const char *pin = lj_str(body, "pin"), *name = lj_str(body, "name"), *cmd_name = action;
    char bdaddr[32], reply[8192] = "", cmd[2048], esc_name[128], connection_raw[2048] = "";
    char *params = NULL; int detected = 0, rc = 0, native_call = 0;
    cJSON *result, *args;
    if (!type[0]) type = "fire";
    if (!name[0]) name = "Harmony Keyboard";
    if (strcmp(action, "adapter_status") == 0) {
        reply[0] = 0;
        run_cmd("echo '--- adapter ---'; hciconfig hci0 -a 2>&1; echo; echo '--- connections ---'; hcitool con 2>&1; echo; echo '--- bluez ---'; adapter=$(dbus-send --system --print-reply --dest=org.bluez / org.bluez.Manager.DefaultAdapter 2>/dev/null | sed -n 's/.*object path \"\\(.*\\)\".*/\\1/p'); if [ -n \"$adapter\" ]; then dbus-send --system --print-reply --dest=org.bluez \"$adapter\" org.bluez.Adapter.GetProperties 2>&1; else echo 'BlueZ adapter not found'; fi", reply, sizeof(reply));
    } else if (strcmp(action, "pairing_on") == 0) {
        if (strlen(name) >= 64 || !safe_bt_name(name))
            return command_result(0, "Invalid Bluetooth display name.");
        shell_escape_single(name, esc_name, sizeof(esc_name));
        snprintf(cmd, sizeof(cmd),
            "echo '--- enabling keyboard pairing mode ---'; "
            "hciconfig hci0 up 2>&1; "
            "hciconfig hci0 name '%s' 2>&1; "
            "hciconfig hci0 class 0x002540 2>&1; "
            "adapter=$(dbus-send --system --print-reply --dest=org.bluez / org.bluez.Manager.DefaultAdapter 2>/dev/null | sed -n 's/.*object path \"\\(.*\\)\".*/\\1/p'); "
            "if [ -n \"$adapter\" ]; then "
            "dbus-send --system --dest=org.bluez \"$adapter\" org.bluez.Adapter.SetProperty string:Pairable variant:boolean:true 2>&1; "
            "dbus-send --system --dest=org.bluez \"$adapter\" org.bluez.Adapter.SetProperty string:Discoverable variant:boolean:true 2>&1; "
            "fi; "
            "hciconfig hci0 piscan 2>&1; "
            "echo; echo '--- adapter ---'; hciconfig hci0 -a 2>&1; "
            "echo; echo '--- bluez ---'; if [ -n \"$adapter\" ]; then dbus-send --system --print-reply --dest=org.bluez \"$adapter\" org.bluez.Adapter.GetProperties 2>&1; fi",
            esc_name);
        reply[0] = 0;
        run_cmd(cmd, reply, sizeof(reply));
    } else if (strcmp(action, "pairing_off") == 0) {
        reply[0] = 0;
        run_cmd("echo '--- disabling discoverable mode ---'; adapter=$(dbus-send --system --print-reply --dest=org.bluez / org.bluez.Manager.DefaultAdapter 2>/dev/null | sed -n 's/.*object path \"\\(.*\\)\".*/\\1/p'); if [ -n \"$adapter\" ]; then dbus-send --system --dest=org.bluez \"$adapter\" org.bluez.Adapter.SetProperty string:Discoverable variant:boolean:false 2>&1; fi; hciconfig hci0 pscan 2>&1; echo; echo '--- adapter ---'; hciconfig hci0 -a 2>&1; echo; echo '--- bluez ---'; if [ -n \"$adapter\" ]; then dbus-send --system --print-reply --dest=org.bluez \"$adapter\" org.bluez.Adapter.GetProperties 2>&1; fi", reply, sizeof(reply));
    } else if (!strcmp(action, "connect") || !strcmp(action, "status")) {
        if (!bt_type_allowed(type)) return command_result(0, "Unsupported Bluetooth HID type.");
        if (strlen(lj_str(body, "bdaddr")) >= sizeof(bdaddr))
            return command_result(0, "Invalid Bluetooth address.");
        copy_text(bdaddr, sizeof(bdaddr), lj_str(body, "bdaddr"));
        if (!bdaddr[0] && !strcmp(action, "status")) {
            detected = detect_connected_bt_addr(bdaddr, sizeof(bdaddr), connection_raw, sizeof(connection_raw));
            if (!detected) strcpy(bdaddr, "00:00:00:00:00:00");
        }
        if (!safe_bt_addr(bdaddr) || !safe_bt_pin(pin))
            return command_result(0, "Invalid Bluetooth address or PIN.");
        save_bthid_target(type, bdaddr);
        args = cJSON_CreateObject();
        cJSON_AddStringToObject(args, "type", type); cJSON_AddStringToObject(args, "bdaddr", bdaddr);
        if (!strcmp(action, "connect") && pin[0]) cJSON_AddStringToObject(args, "pin", pin);
        params = cJSON_PrintUnformatted(args); cJSON_Delete(args);
        if (!params) return command_result(0, "Not enough memory for Bluetooth command.");
        cmd_name = !strcmp(action, "connect") ? "bthid.connect" : "bthid.status";
        rc = run_hal_json(cmd_name, params, 8, reply, sizeof(reply)); native_call = 1;
    } else return command_result(0, "Unknown Bluetooth pairing action.");
    result = cJSON_CreateObject(); cJSON_AddBoolToObject(result, "ok", rc == 0);
    cJSON_AddStringToObject(result, "action", action); cJSON_AddStringToObject(result, "cmd", cmd_name);
    cJSON_AddStringToObject(result, "params", params ? params : (!strcmp(action, "pairing_on") ? name : ""));
    cJSON_AddStringToObject(result, "responseRaw", reply[0] ? reply : "no response");
    if (native_call) cJSON_AddNumberToObject(result, "exitCode", rc);
    if (detected) {
        cJSON_AddStringToObject(result, "detectedAddress", bdaddr);
        cJSON_AddStringToObject(result, "connectionRaw", connection_raw);
    }
    free(params); return result;
}

static void free_request(struct request *req) {
    if (!req) return;
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
}

#include "local_api.h"

static int read_request(int fd, struct request *req) {
    char *buf;
    char *header_end, *line_end, *p;
    int n = 0, clen, orig_clen = 0, header_len, have_length = 0;
    memset(req, 0, sizeof(*req));
    buf = (char *)malloc(MAX_REQUEST_BYTES);
    if (!buf) return -1;
    while (n < MAX_REQUEST_BYTES - 1) {
        int got = recv(fd, buf + n, n < 8192 ? 8192 - n : 0, 0);
        if (got <= 0) {
            free(buf);
            return -1;
        }
        n += got;
        if (memchr(buf, 0, n)) { free(buf); return -1; }
        buf[n] = 0;
        header_end = strstr(buf, "\r\n\r\n");
        if (header_end) {
            header_len = (int)(header_end + 4 - buf);
            line_end = strstr(buf, "\r\n");
            if (!line_end) {
                free(buf);
                return -1;
            }
            *line_end = 0;
            sscanf(buf, "%7s %255s", req->method, req->path);
            *line_end = '\r';
            p = line_end + 2;
            while (p < header_end) {
                char *e = strstr(p, "\r\n");
                if (!e) break;
                if (strncasecmp(p, "Authorization:", 14) == 0) {
                    char *v = p + 14;
                    while (*v == ' ' || *v == '\t') v++;
                    if (req->auth[0] || e - v >= sizeof(req->auth)) { free(buf); return -1; }
                    snprintf(req->auth, sizeof(req->auth), "%.*s", (int)(e - v), v);
                }
                if (strncasecmp(p, "Content-Length:", 15) == 0) {
                    char *v = p + 15, *end; unsigned long length;
                    while (v < e && (*v == ' ' || *v == '\t')) v++;
                    if (have_length++ || v == e || !isdigit((unsigned char)*v)) { free(buf); return -1; }
                    errno = 0; length = strtoul(v, &end, 10);
                    while (end < e && (*end == ' ' || *end == '\t')) end++;
                    if (errno || end != e) { free(buf); return -1; }
                    if (length > MAX_REQUEST_BODY) { req->body_truncated = 1; free(buf); return 0; }
                    orig_clen = (int)length;
                }
                if (local_read_header(req, p, e) != 0) { free(buf); return -1; }
                p = e + 2;
            }
            while (n < header_len + orig_clen) {
                got = recv(fd, buf + n, header_len + orig_clen - n, 0);
                if (got <= 0) break;
                if (memchr(buf + n, 0, got)) { free(buf); return -1; }
                n += got; buf[n] = 0;
            }
            if (orig_clen > 0) {
                int available = n - header_len;
                if (available < 0) available = 0;
                if (available < orig_clen) req->body_truncated = 1;
                if (orig_clen > MAX_REQUEST_BODY) req->body_truncated = 1;
                clen = available < orig_clen ? available : orig_clen;
                if (clen > MAX_REQUEST_BODY) clen = MAX_REQUEST_BODY;
                req->body = (char *)malloc((size_t)clen + 1);
                if (!req->body) {
                    free(buf);
                    return -1;
                }
                memcpy(req->body, buf + header_len, clen);
                req->body[clen] = 0;
                req->body_len = (size_t)clen;
            }
            free(buf);
            return 0;
        }
    }
    free(buf);
    return -1;
}

static void send_payload_too_large(int fd, const struct request *req) {
    if (strncmp(req->path, "/api/", 5) == 0) {
        FILE *f = send_json_start(fd, "413 Payload Too Large");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"request body is too large for this hub\"}\n", f);
        fclose(f);
        return;
    }
    send_text(fd, "413 Payload Too Large", "request body is too large for this hub\n");
}

static void handle_client(int client) {
    struct request req;
    if (read_request(client, &req) != 0) { free_request(&req); return; }
    if (req.body_truncated) { send_payload_too_large(client, &req); free_request(&req); return; }
    if (local_dispatch(client, &req) || local_compat_dispatch(client, &req)) { free_request(&req); return; }
    if (!local_legacy_authorize(client, &req)) { free_request(&req); return; }
    if (!strcmp(req.path, "/api/inventory")) render_inventory_json(client);
    else if (!strncmp(req.path, "/api/device-commands", 20) && (!req.path[20] || req.path[20] == '?')) render_device_commands_json(client, &req);
    else if (!strcmp(req.path, "/export/devices")) send_file_download(client, DEVICE_LIST, "DeviceList.json", "application/json");
    else if (!strcmp(req.path, "/export/functions")) send_file_download(client, FUNCTION_LIST, "FunctionList.json", "application/json");
    else if (!strcmp(req.path, "/export/protocols")) send_file_download(client, PROTOCOL_LIST, "ProtocolList.json", "application/json");
    else if (!strcmp(req.path, "/export/bluetooth")) send_bt_devices_download(client);
    else local_error(client, "410 Gone", "Use the versioned local API.");
    free_request(&req);
}

static pid_t http_workers[8];
static void reap_children(void) {
    int saved_errno = errno;
    pid_t pid; size_t i;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
        for (i = 0; i < 8; i++) if (http_workers[i] == pid) http_workers[i] = 0;
    errno = saved_errno;
}

static void handle_sigchld(int signo) {
    (void)signo;
    reap_children();
}

static void start_bthid_keyboard_runtime(void) {
    pid_t pid;
    if (access("/data/codex/bin/codex_bthid_keyboard", X_OK) != 0) return;
    pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c",
              "mkdir -p /cache/bin; "
              "ln -sf /data/codex/bin/codex_bthid_keyboard /cache/bin/bthid_keyboard; "
              "if ! ps | grep '[c]odex_bthid_keyboard' >/dev/null 2>&1; then "
              "/data/codex/bin/codex_bthid_keyboard </dev/null >> /cache/codex-bthid-keyboard.log 2>&1 & "
              "fi",
              (char *)NULL);
        _exit(127);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--trim-logs") == 0) return local_trim_logs();
    if (argc > 1 && strcmp(argv[1], "--inventory") == 0) return local_inventory();
    if (argc > 1 && strcmp(argv[1], "--sync") == 0) { sync(); return 0; }
    if (argc > 1 && strcmp(argv[1], "--claim-code") == 0) return local_claim_code();
    if (argc > 1 && strcmp(argv[1], "--coordinator") == 0) return local_coordinator();
    if (argc > 1 && strcmp(argv[1], "--health") == 0) return local_health();
    if (argc > 1 && strcmp(argv[1], "--update-health") == 0) return local_update_health();
    if (argc > 1 && strcmp(argv[1], "--rollback") == 0) return local_rollback();
    if (argc > 1 && strcmp(argv[1], "--internal-key") == 0) {
        char key[65]; if (local_initialize()) return 1;
        if (access(LOCAL_ROOT "/internal.key", F_OK) == 0) return 0;
        return local_random(key, 32) || write_file_atomic(LOCAL_ROOT "/internal.key", key, strlen(key));
    }
    if (argc > 3 && strcmp(argv[1], "--space") == 0) {
        struct statvfs s; unsigned long long need = strtoull(argv[3], NULL, 10), available;
        if (statvfs(argv[2], &s)) return 1; available = (unsigned long long)s.f_bavail * s.f_frsize;
        printf("Available: %llu bytes; required: %llu bytes\n", available, need + LOCAL_RESERVE);
        return available < need + LOCAL_RESERVE;
    }
    if (local_initialize() != 0) { fprintf(stderr, "Local configuration recovery failed; refusing startup.\n"); return 1; }
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    int fd, button_fd, one = 1;
    struct local_button button = {0};
    struct sigaction sa;
    struct sockaddr_in addr;
    signal(SIGPIPE, SIG_IGN);
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
    start_bthid_keyboard_runtime();
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }
    if (listen(fd, 8) != 0) {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "codex_webui listening on %d\n", port);
    /* Restart requires a fresh request and press, never a buffered approval. */
    unlink(LOCAL_PAIR_WINDOW);
    button_fd = open(LOCAL_BUTTON_DEVICE, O_RDONLY | O_NONBLOCK);
    local_button_available = button_fd >= 0;
    while (1) {
        int client;
        fd_set ready;
        size_t slot;
        sigset_t blocked, saved;
        pid_t pid;
        reap_children();
        FD_ZERO(&ready); FD_SET(fd, &ready);
        if (button_fd >= 0) FD_SET(button_fd, &ready);
        if (select((button_fd > fd ? button_fd : fd) + 1, &ready, NULL, NULL, NULL) < 0) continue;
        if (button_fd >= 0 && FD_ISSET(button_fd, &ready)) {
            struct input_event event; ssize_t n;
            while ((n = read(button_fd, &event, sizeof(event))) == sizeof(event)) local_button_event(&button, &event);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                close(button_fd); button_fd = -1; local_button_available = 0;
                unlink(LOCAL_PAIR_WINDOW); memset(&button, 0, sizeof(button));
            }
        }
        if (!FD_ISSET(fd, &ready)) continue;
        client = accept(fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            continue;
        }
        sigemptyset(&blocked); sigaddset(&blocked, SIGCHLD); sigprocmask(SIG_BLOCK, &blocked, &saved);
        reap_children();
        for (slot = 0; slot < 8 && http_workers[slot]; slot++) {}
        if (slot == 8) {
            sigprocmask(SIG_SETMASK, &saved, NULL);
            send_text(client, "503 Service Unavailable", "Hub is busy. Try again.\n"); close(client); continue;
        }
        pid = fork();
        if (pid == 0) {
            /* The listener reaps workers; each worker must reap its own helpers. */
            signal(SIGCHLD, SIG_DFL);
            sigprocmask(SIG_SETMASK, &saved, NULL);
            close(fd);
            if (button_fd >= 0) close(button_fd);
            { struct timeval timeout = {5, 0}; setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); }
            alarm(45);
            handle_client(client);
            close(client);
            _exit(0);
        }
        if (pid > 0) http_workers[slot] = pid;
        sigprocmask(SIG_SETMASK, &saved, NULL);
        if (pid < 0) {
            send_text(client, "503 Service Unavailable", "Hub is busy. Try again.\n");
        }
        close(client);
    }
}
