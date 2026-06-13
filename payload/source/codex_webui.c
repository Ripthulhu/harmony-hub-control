#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "remote_skin_jpg.h"

#define MQTT_CONFIG "/data/codexmqtt/config.json"
#define WPA_CONFIG "/etc/wpa_supplicant.conf"
#define HUB_ID_FILE "/data/codex/hub_id"
#define CLOUD_BLOCKER_CONFIG "/data/codex/cloud_blocker.conf"
#define WEBUI_AUTH_CONFIG "/data/codex/webui_auth.conf"
#define UPDATE_STATE_CONFIG "/data/codex/update_state.conf"
#define DEVICE_LIST "/data/resources/DeviceList.json"
#define FUNCTION_LIST "/data/resources/FunctionList.json"
#define PROTOCOL_LIST "/data/resources/ProtocolList.json"
#define RESOURCE_RELOAD_FLAG "/data/codex/reload_resources"
#define RESOURCE_BACKUP_DIR "/data/codex/resource-backups"
#define IR_EVENT_LOG "/data/codex/ir-events.log"
#define IR_CANCEL_PREFIX "/tmp/codex_ir_cancel_"
#define BT_TEXT_FIFO "/tmp/bthid_input"
#define BT_TEXT_STATUS "/tmp/bthid_status"
#define BT_TARGET_FILE "/data/codex/bthid_target"
#define BT_DEVICE_STORE "/data/codex/bt-devices.json"
#define CODEX_BIN_DIR "/data/codex/bin"
#define UPDATE_STAGE_DIR "/tmp/codex_update"
#define UPDATE_BACKUP_DIR "/data/codex/update-backups"
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

static const char *UPDATE_FILES[] = {
    "codex_webui",
    "codex_bthid_keyboard",
    "codex_hal_ltcp",
    "codex_hbus",
    "codex_portal",
    "codex_dhcpd"
};

static const char *BUILTIN_PROTOCOL_TOSHIBA_32 =
    "{\"IRSegments\":[{\"Header\":[{\"Value\":8990,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":4490,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Payload\":{\"NumberOfBits\":32,\"Encodings\":[{\"Atoms\":[{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":552,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"BitType\":0},{\"Atoms\":[{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":1662,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"BitType\":1}],\"ToggleBit\":null,\"EncodingType\":0},\"Trailer\":[{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null}],\"TotalLength\":107870,\"Name\":\"Toshiba 32 Bit\"}],\"Attributes\":[],\"IsPadded\":true,\"IsFullSequence\":true,\"Rating\":null,\"NumberOfLinkedLanguage\":0,\"Status\":null,\"IsPublic\":true,\"HoldDelay\":null,\"PressMinimumRepeats\":1,\"SendingType\":0,\"Name\":\"Toshiba 32 Bit\",\"ControlSection\":null,\"Flags\":[],\"__type\":\"IrProtocol\",\"CarrierFrequency\":38000,\"Id-\":2,\"CodeSegments\":[{\"Header\":[{\"Value\":8990,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":2230,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Payload\":null,\"TotalLength\":0,\"Trailer\":[{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":96077,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Atoms\":[{\"Value\":8990,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":2230,\"Type\":0,\"MinValue\":null,\"MaxValue\":null},{\"Value\":568,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":96077,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Name\":\"Toshiba 32 Bit KeyCodeRepeat\"}],\"KeyCode\":{\"Start\":[{\"SegmentType\":1,\"SegmentName\":\"Toshiba 32 Bit\"}],\"Repeat\":[{\"SegmentType\":0,\"SegmentName\":\"Toshiba 32 Bit KeyCodeRepeat\"}],\"Finish\":null},\"HoldMinimumRepeats\":null,\"RelatedProtocols\":[]}";

static const char *BUILTIN_PROTOCOL_MEMOREX_O1 =
    "{\"IRSegments\":[{\"Header\":[{\"Value\":9000,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":4500,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"Payload\":{\"NumberOfBits\":32,\"Encodings\":[{\"Atoms\":[{\"Value\":560,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":560,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"BitType\":0},{\"Atoms\":[{\"Value\":560,\"Type\":1,\"MinValue\":null,\"MaxValue\":null},{\"Value\":1690,\"Type\":0,\"MinValue\":null,\"MaxValue\":null}],\"BitType\":1}],\"ToggleBit\":null,\"EncodingType\":0},\"Trailer\":[{\"Value\":560,\"Type\":1,\"MinValue\":null,\"MaxValue\":null}],\"TotalLength\":107600,\"Name\":\"MemorexO1 32 Bit\"}],\"Attributes\":[],\"IsPadded\":null,\"IsFullSequence\":null,\"Rating\":null,\"NumberOfLinkedLanguage\":0,\"Status\":null,\"IsPublic\":true,\"HoldDelay\":null,\"PressMinimumRepeats\":null,\"SendingType\":0,\"Name\":\"MemorexO1 32 Bit\",\"ControlSection\":null,\"Flags\":[],\"__type\":\"IrProtocol\",\"CarrierFrequency\":38000,\"Id-\":679,\"CodeSegments\":[],\"KeyCode\":{\"Start\":null,\"Repeat\":[{\"SegmentType\":1,\"SegmentName\":\"MemorexO1 32 Bit\"}],\"Finish\":null},\"HoldMinimumRepeats\":null,\"RelatedProtocols\":[]}";

struct request {
    char method[8];
    char path[256];
    char auth[512];
    int body_truncated;
    char *body;
    size_t body_len;
};

struct mqtt_config {
    int enabled;
    int ha_discovery;
    int port;
    int poll_seconds;
    int keep_alive;
    char host[128];
    char username[128];
    char password[256];
    char base_topic[128];
    char discovery_prefix[128];
    char client_id[128];
    char name[128];
};

struct wifi_config {
    int hidden;
    int open;
    char ssid[256];
    char psk[256];
};

struct webui_auth_config {
    int enabled;
    char username[64];
    char password[128];
};

struct update_check_state {
    long checked_at;
    int available;
    int changes;
    char message[192];
    char source[192];
};

struct ir_command {
    char id[32];
    char name[128];
    char keycode[256];
    char raw[2048];
    int protocol_id;
    int learned;
    int has_raw;
};

struct ir_device {
    char id[32];
    char name[128];
    char manufacturer[128];
    char model[128];
    char type[80];
    int control_port;
    int transport;
    int command_count;
    int command_cap;
    struct ir_command *commands;
};

struct ir_inventory {
    int device_count;
    long max_device_id;
    long max_command_id;
    struct ir_device devices[MAX_IR_DEVICES];
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
    char tmp[256];
    FILE *f;
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    f = fopen(tmp, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, len, f) != len) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fputc('\n', f);
    fclose(f);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    sync();
    return 0;
}

static int cloud_value_enabled(const char *raw) {
    char value[32];
    size_t i = 0;
    while (*raw && isspace((unsigned char)*raw)) raw++;
    while (*raw && !isspace((unsigned char)*raw) && i < sizeof(value) - 1) {
        value[i++] = (char)tolower((unsigned char)*raw++);
    }
    value[i] = 0;
    if (!value[0]) return 1;
    return !(strcmp(value, "0") == 0 ||
             strcmp(value, "off") == 0 ||
             strcmp(value, "false") == 0 ||
             strcmp(value, "disabled") == 0 ||
             strcmp(value, "allow") == 0 ||
             strcmp(value, "allowed") == 0);
}

static int cloud_value_known(const char *raw) {
    char value[32];
    size_t i = 0;
    while (*raw && isspace((unsigned char)*raw)) raw++;
    while (*raw && !isspace((unsigned char)*raw) && i < sizeof(value) - 1) {
        value[i++] = (char)tolower((unsigned char)*raw++);
    }
    value[i] = 0;
    return strcmp(value, "1") == 0 ||
           strcmp(value, "on") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "enabled") == 0 ||
           strcmp(value, "block") == 0 ||
           strcmp(value, "blocked") == 0 ||
           strcmp(value, "0") == 0 ||
           strcmp(value, "off") == 0 ||
           strcmp(value, "false") == 0 ||
           strcmp(value, "disabled") == 0 ||
           strcmp(value, "allow") == 0 ||
           strcmp(value, "allowed") == 0;
}


static int load_cloud_blocker(void) {
    char raw[32];
    if (read_text(CLOUD_BLOCKER_CONFIG, raw, sizeof(raw)) <= 0) return 1;
    return cloud_value_enabled(raw);
}

static int save_cloud_blocker(int enabled) {
    const char *value = enabled ? "1" : "0";
    if (write_file_atomic(CLOUD_BLOCKER_CONFIG, value, strlen(value)) != 0) return -1;
    chmod(CLOUD_BLOCKER_CONFIG, 0644);
    return 0;
}

static int config_line_value(const char *raw, const char *key, char *out, size_t outlen) {
    size_t keylen = strlen(key), n;
    const char *p = raw, *v, *e;
    if (!outlen) return 0;
    out[0] = 0;
    while (p && *p) {
        if (strncmp(p, key, keylen) == 0 && p[keylen] == '=') {
            v = p + keylen + 1;
            e = strchr(v, '\n');
            if (!e) e = v + strlen(v);
            n = (size_t)(e - v);
            if (n >= outlen) n = outlen - 1;
            memcpy(out, v, n);
            out[n] = 0;
            chomp(out);
            return 1;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return 0;
}

static void clean_config_value(char *s) {
    while (*s) {
        if (*s == '\n' || *s == '\r' || *s == '\t') *s = ' ';
        s++;
    }
}

static int safe_auth_field(const char *s, int allow_colon) {
    if (!s || !*s) return 0;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 32 || c == 127) return 0;
        if (!allow_colon && c == ':') return 0;
    }
    return 1;
}

static void load_webui_auth(struct webui_auth_config *cfg) {
    char raw[512], value[160];
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->username, "admin");
    if (read_text(WEBUI_AUTH_CONFIG, raw, sizeof(raw)) <= 0) return;
    if (config_line_value(raw, "enabled", value, sizeof(value))) cfg->enabled = atoi(value) ? 1 : 0;
    if (config_line_value(raw, "username", value, sizeof(value)) && safe_auth_field(value, 0)) {
        snprintf(cfg->username, sizeof(cfg->username), "%s", value);
    }
    if (config_line_value(raw, "password", value, sizeof(value)) && safe_auth_field(value, 1)) {
        snprintf(cfg->password, sizeof(cfg->password), "%s", value);
    }
    if (cfg->enabled && (!cfg->username[0] || !cfg->password[0])) cfg->enabled = 0;
}

static int save_webui_auth(const struct webui_auth_config *cfg) {
    char data[384], user[64], pass[128];
    snprintf(user, sizeof(user), "%s", cfg->username);
    snprintf(pass, sizeof(pass), "%s", cfg->password);
    clean_config_value(user);
    clean_config_value(pass);
    snprintf(data, sizeof(data), "enabled=%d\nusername=%s\npassword=%s\n",
        cfg->enabled ? 1 : 0, user, pass);
    if (write_file_atomic(WEBUI_AUTH_CONFIG, data, strlen(data)) != 0) return -1;
    chmod(WEBUI_AUTH_CONFIG, 0600);
    return 0;
}

static void load_update_state(struct update_check_state *st) {
    char raw[768], value[256];
    memset(st, 0, sizeof(*st));
    strcpy(st->message, "Update status has not been checked yet.");
    if (read_text(UPDATE_STATE_CONFIG, raw, sizeof(raw)) <= 0) return;
    if (config_line_value(raw, "checkedAt", value, sizeof(value))) st->checked_at = atol(value);
    if (config_line_value(raw, "available", value, sizeof(value))) st->available = atoi(value) ? 1 : 0;
    if (config_line_value(raw, "changes", value, sizeof(value))) st->changes = atoi(value);
    if (config_line_value(raw, "message", value, sizeof(value))) snprintf(st->message, sizeof(st->message), "%s", value);
    if (config_line_value(raw, "source", value, sizeof(value))) snprintf(st->source, sizeof(st->source), "%s", value);
}

static int save_update_state(const struct update_check_state *st) {
    char data[768], msg[192], source[192];
    snprintf(msg, sizeof(msg), "%s", st->message);
    snprintf(source, sizeof(source), "%s", st->source);
    clean_config_value(msg);
    clean_config_value(source);
    snprintf(data, sizeof(data), "checkedAt=%ld\navailable=%d\nchanges=%d\nmessage=%s\nsource=%s\n",
        st->checked_at, st->available ? 1 : 0, st->changes, msg, source);
    if (write_file_atomic(UPDATE_STATE_CONFIG, data, strlen(data)) != 0) return -1;
    chmod(UPDATE_STATE_CONFIG, 0644);
    return 0;
}

static int copy_file_raw(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[4096];
    size_t n;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            int saved_errno = errno;
            fclose(in);
            fclose(out);
            unlink(dst);
            errno = saved_errno;
            return -1;
        }
    }
    if (ferror(in)) {
        int saved_errno = errno;
        fclose(in);
        fclose(out);
        unlink(dst);
        errno = saved_errno;
        return -1;
    }
    fclose(in);
    if (fclose(out) != 0) {
        unlink(dst);
        return -1;
    }
    return 0;
}

static void remove_tree_simple(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *de;
        if (d) {
            while ((de = readdir(d)) != NULL) {
                char child[512];
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
                remove_tree_simple(child);
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

static void remove_dir_entries_with_prefix(const char *dir, const char *prefix) {
    DIR *d = opendir(dir);
    struct dirent *de;
    size_t prefix_len = strlen(prefix);
    if (!d) return;
    while ((de = readdir(d)) != NULL) {
        char path[512];
        if (strncmp(de->d_name, prefix, prefix_len) != 0) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        remove_tree_simple(path);
    }
    closedir(d);
}

static int is_digit_name(const char *s) {
    if (!s || !*s) return 0;
    while (*s) {
        if (!isdigit((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

static void prune_update_backups(int keep) {
    struct backup_entry { long stamp; char name[64]; } entries[32], tmp;
    DIR *d = opendir(UPDATE_BACKUP_DIR);
    struct dirent *de;
    int count = 0, i, j;
    if (!d) return;
    while ((de = readdir(d)) != NULL) {
        if (!is_digit_name(de->d_name)) continue;
        if (count >= (int)(sizeof(entries) / sizeof(entries[0]))) break;
        entries[count].stamp = atol(de->d_name);
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", de->d_name);
        count++;
    }
    closedir(d);
    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (entries[j].stamp > entries[i].stamp) {
                tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
    for (i = keep; i < count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", UPDATE_BACKUP_DIR, entries[i].name);
        remove_tree_simple(path);
    }
}

static char *json_escape_alloc(const char *s) {
    size_t need = 3;
    const char *p;
    char *out, *w;
    for (p = s; *p; p++) {
        if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t') need += 2;
        else need++;
    }
    out = (char *)malloc(need);
    if (!out) return NULL;
    w = out;
    *w++ = '"';
    for (p = s; *p; p++) {
        if (*p == '"' || *p == '\\') {
            *w++ = '\\';
            *w++ = *p;
        } else if (*p == '\n') {
            *w++ = '\\'; *w++ = 'n';
        } else if (*p == '\r') {
            *w++ = '\\'; *w++ = 'r';
        } else if (*p == '\t') {
            *w++ = '\\'; *w++ = 't';
        } else {
            *w++ = *p;
        }
    }
    *w++ = '"';
    *w = 0;
    return out;
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

static void html(FILE *f, const char *s) {
    while (*s) {
        switch (*s) {
        case '&': fputs("&amp;", f); break;
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        case '"': fputs("&quot;", f); break;
        case '\'': fputs("&#39;", f); break;
        default: fputc(*s, f); break;
        }
        s++;
    }
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

static int form_checked(const char *body, const char *name) {
    char tmp[8];
    form_value(body, name, tmp, sizeof(tmp));
    return tmp[0] != 0;
}

static int content_length(const char *headers) {
    const char *p = headers;
    const char *needle = "content-length:";
    size_t nlen = strlen(needle);
    while (*p) {
        size_t i;
        for (i = 0; i < nlen; i++) {
            if (!p[i] || tolower((unsigned char)p[i]) != needle[i]) break;
        }
        if (i == nlen) {
            long value;
            char *end;
            p += nlen;
            while (*p == ' ' || *p == '\t') p++;
            errno = 0;
            value = strtol(p, &end, 10);
            if (errno != 0 || end == p || value < 0) return 0;
            if (value > MAX_REQUEST_BODY) return MAX_REQUEST_BODY + 1;
            return (int)value;
        }
        p++;
    }
    return 0;
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

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode_text(const char *in, char *out, size_t outlen) {
    int val = 0, valb = -8;
    size_t w = 0;
    if (!outlen) return -1;
    while (*in && *in != '\r' && *in != '\n') {
        int d;
        if (*in == '=') break;
        if (isspace((unsigned char)*in)) {
            in++;
            continue;
        }
        d = b64_value(*in++);
        if (d < 0) return -1;
        val = (val << 6) | d;
        valb += 6;
        if (valb >= 0) {
            if (w + 1 >= outlen) return -1;
            out[w++] = (char)((val >> valb) & 0xff);
            valb -= 8;
        }
    }
    out[w] = 0;
    return (int)w;
}

static int webui_auth_ok(const struct request *req) {
    struct webui_auth_config cfg;
    char decoded[256], expected[256];
    const char *prefix = "Basic ";
    load_webui_auth(&cfg);
    if (!cfg.enabled) return 1;
    if (strncasecmp(req->auth, prefix, strlen(prefix)) != 0) return 0;
    if (base64_decode_text(req->auth + strlen(prefix), decoded, sizeof(decoded)) < 0) return 0;
    snprintf(expected, sizeof(expected), "%s:%s", cfg.username, cfg.password);
    return strcmp(decoded, expected) == 0;
}

static void send_auth_required(int fd) {
    const char *body = "authentication required\n";
    char hdr[384];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Basic realm=\"Harmony Hub Control\", charset=\"UTF-8\"\r\n"
        "Content-Type: text/plain\r\nContent-Length: %lu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        (unsigned long)strlen(body));
    send_all(fd, hdr, strlen(hdr));
    send_all(fd, body, strlen(body));
}

static int http_get_body(const char *host, const char *path, char **body, size_t max_body, char *err, size_t errlen) {
    struct hostent *he;
    struct sockaddr_in addr;
    struct timeval tv;
    int fd, status = 0;
    char req[1024];
    char *buf, *hdr_end, *p;
    size_t cap = max_body + 8192, n = 0, body_len;
    ssize_t got;
    *body = NULL;
    if (errlen) err[0] = 0;
    he = gethostbyname(host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        snprintf(err, errlen, "dns failed");
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(err, errlen, "socket failed");
        return -1;
    }
    tv.tv_sec = 8;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        snprintf(err, errlen, "connect failed");
        return -1;
    }
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: HarmonyHubControl/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n",
        path, host);
    send_all(fd, req, strlen(req));
    buf = (char *)malloc(cap + 1);
    if (!buf) {
        close(fd);
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    while (n < cap) {
        got = recv(fd, buf + n, cap - n, 0);
        if (got <= 0) break;
        n += (size_t)got;
    }
    close(fd);
    buf[n] = 0;
    if (n == cap) {
        free(buf);
        snprintf(err, errlen, "response too large");
        return -1;
    }
    sscanf(buf, "HTTP/%*s %d", &status);
    hdr_end = strstr(buf, "\r\n\r\n");
    if (!hdr_end || status != 200) {
        free(buf);
        snprintf(err, errlen, "http %d", status);
        return -1;
    }
    p = hdr_end + 4;
    while (*p && isspace((unsigned char)*p)) p++;
    body_len = strlen(p);
    if (body_len > max_body) {
        free(buf);
        snprintf(err, errlen, "body too large");
        return -1;
    }
    *body = (char *)malloc(body_len + 1);
    if (!*body) {
        free(buf);
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    memcpy(*body, p, body_len + 1);
    free(buf);
    return 0;
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

static void send_cloud_download(int fd) {
    char hdr[512];
    const char *body = load_cloud_blocker() ? "1\n" : "0\n";
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %lu\r\n"
        "Cache-Control: no-store\r\nContent-Disposition: attachment; filename=\"cloud-blocker.conf\"\r\nConnection: close\r\n\r\n",
        (unsigned long)strlen(body));
    send_all(fd, hdr, strlen(hdr));
    send_all(fd, body, strlen(body));
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

static int json_string(const char *json, const char *key, char *out, size_t outlen) {
    char needle[64];
    const char *p;
    char *w = out;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) {
        out[0] = 0;
        return 0;
    }
    p = strchr(p + strlen(needle), ':');
    if (!p) {
        out[0] = 0;
        return 0;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') {
        out[0] = 0;
        return 0;
    }
    p++;
    while (*p && *p != '"' && (size_t)(w - out) + 1 < outlen) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') *w++ = '\n';
            else if (*p == 't') *w++ = '\t';
            else *w++ = *p;
        } else {
            *w++ = *p;
        }
        p++;
    }
    *w = 0;
    return 1;
}

static int json_int(const char *json, const char *key, int def) {
    char needle[64];
    const char *p;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p + strlen(needle), ':');
    if (!p) return def;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    return atoi(p);
}

static int json_bool(const char *json, const char *key, int def) {
    char needle[64];
    const char *p;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p + strlen(needle), ':');
    if (!p) return def;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return def;
}

static const char *find_matching_json(const char *open, char opener, char closer) {
    int depth = 0, in_string = 0, esc = 0;
    const char *p;
    for (p = open; *p; p++) {
        if (in_string) {
            if (esc) esc = 0;
            else if (*p == '\\') esc = 1;
            else if (*p == '"') in_string = 0;
        } else {
            if (*p == '"') in_string = 1;
            else if (*p == opener) depth++;
            else if (*p == closer) {
                depth--;
                if (depth == 0) return p;
            }
        }
    }
    return NULL;
}

static const char *find_key_range(const char *start, const char *end, const char *key) {
    char needle[96];
    const char *p = start;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    while (p && (!end || p < end)) {
        p = strstr(p, needle);
        if (!p || (end && p >= end)) return NULL;
        return p;
    }
    return NULL;
}

static int json_string_range(const char *start, const char *end, const char *key, char *out, size_t outlen) {
    const char *p = find_key_range(start, end, key);
    char *w = out;
    if (!outlen) return 0;
    out[0] = 0;
    if (!p) return 0;
    p = strchr(p + strlen(key) + 2, ':');
    if (!p || (end && p >= end)) return 0;
    p++;
    while (*p && (!end || p < end) && isspace((unsigned char)*p)) p++;
    if (*p != '"') return 0;
    p++;
    while (*p && (!end || p < end) && *p != '"' && (size_t)(w - out) + 1 < outlen) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') *w++ = '\n';
            else if (*p == 't') *w++ = '\t';
            else if (*p == 'r') *w++ = '\r';
            else *w++ = *p;
        } else {
            *w++ = *p;
        }
        p++;
    }
    *w = 0;
    return out[0] != 0;
}

static long json_long_range(const char *start, const char *end, const char *key, long def) {
    const char *p = find_key_range(start, end, key);
    if (!p) return def;
    p = strchr(p + strlen(key) + 2, ':');
    if (!p || (end && p >= end)) return def;
    p++;
    while (*p && (!end || p < end) && isspace((unsigned char)*p)) p++;
    return strtol(p, NULL, 10);
}

static int json_bool_range(const char *start, const char *end, const char *key, int def) {
    const char *p = find_key_range(start, end, key);
    if (!p) return def;
    p = strchr(p + strlen(key) + 2, ':');
    if (!p || (end && p >= end)) return def;
    p++;
    while (*p && (!end || p < end) && isspace((unsigned char)*p)) p++;
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return def;
}

static int json_has_nonnull_value(const char *start, const char *end, const char *key) {
    const char *p = find_key_range(start, end, key);
    if (!p) return 0;
    p = strchr(p + strlen(key) + 2, ':');
    if (!p || (end && p >= end)) return 0;
    p++;
    while (*p && (!end || p < end) && isspace((unsigned char)*p)) p++;
    return strncmp(p, "null", 4) != 0;
}

static void load_mqtt(struct mqtt_config *cfg) {
    char raw[8192];
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 0;
    cfg->ha_discovery = 1;
    cfg->port = 1883;
    cfg->poll_seconds = 10;
    cfg->keep_alive = 60;
    strcpy(cfg->base_topic, "harmony/hub");
    strcpy(cfg->discovery_prefix, "homeassistant");
    strcpy(cfg->client_id, "harmony-local-mqtt");
    strcpy(cfg->name, "Harmony Hub");
    if (read_text(MQTT_CONFIG, raw, sizeof(raw)) <= 0) return;
    cfg->enabled = json_bool(raw, "enabled", cfg->enabled);
    cfg->ha_discovery = json_bool(raw, "haDiscovery", cfg->ha_discovery);
    cfg->port = json_int(raw, "port", cfg->port);
    cfg->poll_seconds = json_int(raw, "pollSeconds", cfg->poll_seconds);
    cfg->keep_alive = json_int(raw, "keepAlive", cfg->keep_alive);
    json_string(raw, "host", cfg->host, sizeof(cfg->host));
    json_string(raw, "username", cfg->username, sizeof(cfg->username));
    json_string(raw, "password", cfg->password, sizeof(cfg->password));
    json_string(raw, "baseTopic", cfg->base_topic, sizeof(cfg->base_topic));
    json_string(raw, "discoveryPrefix", cfg->discovery_prefix, sizeof(cfg->discovery_prefix));
    json_string(raw, "clientId", cfg->client_id, sizeof(cfg->client_id));
    json_string(raw, "name", cfg->name, sizeof(cfg->name));
}

static void json_write_string(FILE *f, const char *s) {
    fputc('"', f);
    while (*s) {
        if (*s == '"' || *s == '\\') {
            fputc('\\', f);
            fputc(*s, f);
        } else if (*s == '\n') {
            fputs("\\n", f);
        } else if (*s == '\r') {
            fputs("\\r", f);
        } else if (*s == '\t') {
            fputs("\\t", f);
        } else {
            fputc(*s, f);
        }
        s++;
    }
    fputc('"', f);
}

static int save_mqtt(const struct mqtt_config *cfg) {
    FILE *f = fopen(MQTT_CONFIG ".new", "w");
    if (!f) return -1;
    fprintf(f, "{");
    fprintf(f, "\"baseTopic\":"); json_write_string(f, cfg->base_topic); fprintf(f, ",");
    fprintf(f, "\"broker\":{\"host\":"); json_write_string(f, cfg->host);
    fprintf(f, ",\"password\":"); json_write_string(f, cfg->password);
    fprintf(f, ",\"port\":%d,\"username\":", cfg->port); json_write_string(f, cfg->username);
    fprintf(f, "},");
    fprintf(f, "\"clientId\":"); json_write_string(f, cfg->client_id); fprintf(f, ",");
    fprintf(f, "\"discoveryPrefix\":"); json_write_string(f, cfg->discovery_prefix); fprintf(f, ",");
    fprintf(f, "\"enabled\":%s,", cfg->enabled ? "true" : "false");
    fprintf(f, "\"haDiscovery\":%s,", cfg->ha_discovery ? "true" : "false");
    fprintf(f, "\"keepAlive\":%d,", cfg->keep_alive);
    fprintf(f, "\"name\":"); json_write_string(f, cfg->name); fprintf(f, ",");
    fprintf(f, "\"pollSeconds\":%d", cfg->poll_seconds);
    fprintf(f, "}\n");
    fclose(f);
    chmod(MQTT_CONFIG ".new", 0600);
    if (rename(MQTT_CONFIG ".new", MQTT_CONFIG) != 0) return -1;
    chmod(MQTT_CONFIG, 0600);
    sync();
    return 0;
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

static int tcp_established(const char *host, int port) {
    FILE *f;
    char line[256];
    struct in_addr addr;
    unsigned long want_addr;
    if (!host[0] || inet_aton(host, &addr) == 0) return 0;
    want_addr = ntohl(addr.s_addr);
    f = fopen("/proc/net/tcp", "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long rem_addr, rem_port, state;
        if (sscanf(line, " %*d: %*X:%*X %lX:%lX %lX", &rem_addr, &rem_port, &state) == 3) {
            if (rem_addr == want_addr && rem_port == (unsigned long)port && state == 1) {
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

static void scan_command_array(const char *arr, const char *arr_end, int *count, long *max_id) {
    const char *cpos;
    if (count) *count = 0;
    if (!arr || !arr_end) return;
    cpos = arr + 1;
    while ((cpos = strchr(cpos, '{')) != NULL && cpos < arr_end) {
        const char *obj_end = find_matching_json(cpos, '{', '}');
        long cid;
        if (!obj_end || obj_end > arr_end) break;
        cid = json_long_range(cpos, obj_end, "Id-", 0);
        if (max_id && cid > *max_id) *max_id = cid;
        if (count) (*count)++;
        cpos = obj_end + 1;
    }
}

static int command_array_has_name(const char *arr, const char *arr_end, const char *name) {
    const char *cpos;
    if (!arr || !arr_end || !name || !name[0]) return 0;
    cpos = arr + 1;
    while ((cpos = strchr(cpos, '{')) != NULL && cpos < arr_end) {
        const char *obj_end = find_matching_json(cpos, '{', '}');
        char existing[128];
        if (!obj_end || obj_end > arr_end) break;
        json_string_range(cpos, obj_end, "Name", existing, sizeof(existing));
        if (strcmp(existing, name) == 0) return 1;
        cpos = obj_end + 1;
    }
    return 0;
}

static int command_fragment_has_name(const char *fragment, const char *name) {
    const char *cpos = fragment;
    if (!fragment || !name || !name[0]) return 0;
    while ((cpos = strchr(cpos, '{')) != NULL) {
        const char *obj_end = find_matching_json(cpos, '{', '}');
        char existing[128];
        if (!obj_end) break;
        json_string_range(cpos, obj_end, "Name", existing, sizeof(existing));
        if (strcmp(existing, name) == 0) return 1;
        cpos = obj_end + 1;
    }
    return 0;
}

static void scan_ir_resource_stats_from_raw(const char *raw, int *device_count, int *total_commands, long *max_device_id, long *max_command_id) {
    const char *p = raw;
    if (device_count) *device_count = 0;
    if (total_commands) *total_commands = 0;
    if (max_device_id) *max_device_id = 0;
    if (max_command_id) *max_command_id = 0;
    while ((p = strstr(p, "\"Device\":{")) != NULL) {
        const char *dev_obj = strchr(p, '{');
        const char *dev_end = dev_obj ? find_matching_json(dev_obj, '{', '}') : NULL;
        const char *next_dev, *cmd_key, *cmd_arr, *cmd_end;
        long id;
        int cmd_count = 0;
        if (!dev_obj || !dev_end) break;
        id = json_long_range(dev_obj, dev_end, "Id-", 0);
        if (id > 0) {
            if (device_count) (*device_count)++;
            if (max_device_id && id > *max_device_id) *max_device_id = id;
        }
        next_dev = strstr(dev_end + 1, "\"Device\":{");
        cmd_key = strstr(dev_end, "\"Commands\":[");
        if (cmd_key && (!next_dev || cmd_key < next_dev)) {
            cmd_arr = strchr(cmd_key, '[');
            cmd_end = cmd_arr ? find_matching_json(cmd_arr, '[', ']') : NULL;
            scan_command_array(cmd_arr, cmd_end, &cmd_count, max_command_id);
        }
        if (total_commands) *total_commands += cmd_count;
        p = dev_end + 1;
    }
}

static int scan_ir_resource_stats(int *device_count, int *total_commands, long *max_device_id, long *max_command_id) {
    char *raw;
    size_t len = 0;
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    (void)len;
    if (!raw) return -1;
    scan_ir_resource_stats_from_raw(raw, device_count, total_commands, max_device_id, max_command_id);
    free(raw);
    return 0;
}

static int load_ir_inventory(struct ir_inventory *inv) {
    char *raw;
    const char *p;
    size_t len = 0;
    memset(inv, 0, sizeof(*inv));
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw) return -1;
    p = raw;
    while ((p = strstr(p, "\"Device\":{")) != NULL && inv->device_count < MAX_IR_DEVICES) {
        struct ir_device *dev = &inv->devices[inv->device_count];
        const char *dev_obj = strchr(p, '{');
        const char *dev_end = dev_obj ? find_matching_json(dev_obj, '{', '}') : NULL;
        const char *next_dev, *cmd_key, *cmd_arr, *cmd_end, *cpos;
        long id;
        if (!dev_obj || !dev_end) break;
        id = json_long_range(dev_obj, dev_end, "Id-", 0);
        if (id <= 0) {
            p = dev_end + 1;
            continue;
        }
        snprintf(dev->id, sizeof(dev->id), "%ld", id);
        if (id > inv->max_device_id) inv->max_device_id = id;
        json_string_range(dev_obj, dev_end, "Name", dev->name, sizeof(dev->name));
        json_string_range(dev_obj, dev_end, "Manufacturer", dev->manufacturer, sizeof(dev->manufacturer));
        json_string_range(dev_obj, dev_end, "Model", dev->model, sizeof(dev->model));
        json_string_range(dev_obj, dev_end, "DeviceTypeDisplayName", dev->type, sizeof(dev->type));
        dev->control_port = (int)json_long_range(dev_obj, dev_end, "ControlPort", 7);
        dev->transport = (int)json_long_range(dev_obj, dev_end, "Transport", 1);
        next_dev = strstr(dev_end + 1, "\"Device\":{");
        cmd_key = strstr(dev_end, "\"Commands\":[");
        cmd_arr = (cmd_key && (!next_dev || cmd_key < next_dev)) ? strchr(cmd_key, '[') : NULL;
        cmd_end = cmd_arr ? find_matching_json(cmd_arr, '[', ']') : NULL;
        if (cmd_arr && cmd_end) {
            cpos = cmd_arr + 1;
            while ((cpos = strchr(cpos, '{')) != NULL && cpos < cmd_end && dev->command_count < MAX_IR_COMMANDS) {
                struct ir_command *cmd;
                const char *obj_end = find_matching_json(cpos, '{', '}');
                long cid;
                if (!obj_end || obj_end > cmd_end) break;
                if (dev->command_count >= dev->command_cap) {
                    int ncap = dev->command_cap ? dev->command_cap * 2 : 8;
                    struct ir_command *grown;
                    if (ncap > MAX_IR_COMMANDS) ncap = MAX_IR_COMMANDS;
                    grown = (struct ir_command *)realloc(dev->commands, (size_t)ncap * sizeof(*grown));
                    if (!grown) break;
                    dev->commands = grown;
                    dev->command_cap = ncap;
                }
                cmd = &dev->commands[dev->command_count];
                memset(cmd, 0, sizeof(*cmd));
                cid = json_long_range(cpos, obj_end, "Id-", 0);
                snprintf(cmd->id, sizeof(cmd->id), "%ld", cid);
                if (cid > inv->max_command_id) inv->max_command_id = cid;
                json_string_range(cpos, obj_end, "Name", cmd->name, sizeof(cmd->name));
                json_string_range(cpos, obj_end, "KeyCode", cmd->keycode, sizeof(cmd->keycode));
                json_string_range(cpos, obj_end, "Raw", cmd->raw, sizeof(cmd->raw));
                cmd->protocol_id = (int)json_long_range(cpos, obj_end, "ProtocolId", 0);
                cmd->learned = json_bool_range(cpos, obj_end, "IsLearned", 0);
                cmd->has_raw = json_has_nonnull_value(cpos, obj_end, "Raw") && !cmd->keycode[0];
                dev->command_count++;
                cpos = obj_end + 1;
            }
        }
        inv->device_count++;
        p = dev_end + 1;
    }
    free(raw);
    return 0;
}

/* Release the per-device command arrays allocated by load_ir_inventory.
 * Safe to call on a zero-initialized or partially loaded inventory. */
static void free_ir_inventory(struct ir_inventory *inv) {
    int i;
    if (!inv) return;
    for (i = 0; i < inv->device_count; i++) {
        free(inv->devices[i].commands);
        inv->devices[i].commands = NULL;
        inv->devices[i].command_cap = 0;
        inv->devices[i].command_count = 0;
    }
    inv->device_count = 0;
}

/* Free a heap-allocated inventory together with its command arrays. */
static void destroy_ir_inventory(struct ir_inventory *inv) {
    if (!inv) return;
    free_ir_inventory(inv);
    free(inv);
}

static int find_ir_device_by_name(const char *name, char *device_id, size_t device_id_len) {
    struct ir_inventory inv;
    int i, rc = -1;
    if (!name || !name[0] || !device_id || !device_id_len) return -1;
    if (load_ir_inventory(&inv) != 0) return -1;
    for (i = 0; i < inv.device_count; i++) {
        if (strcasecmp(inv.devices[i].name, name) == 0) {
            snprintf(device_id, device_id_len, "%s", inv.devices[i].id);
            rc = 0;
            break;
        }
    }
    free_ir_inventory(&inv);
    return rc;
}

static void capture_ir_command_action(char *out, size_t outlen);
static int find_device_command_array(const char *raw, const char *device_id, const char **arr, const char **arr_end);

static void render_inventory_json(int fd) {
    char *raw;
    size_t len = 0;
    const char *p;
    FILE *f = send_json_start(fd, "200 OK");
    int device_count = 0, total_commands = 0, emitted = 0;
    if (!f) {
        return;
    }
    repair_known_protocols_for_current_commands();
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw) {
        fputs("{\"ok\":false,\"error\":\"unable to read inventory\"}\n", f);
        fclose(f);
        return;
    }
    scan_ir_resource_stats_from_raw(raw, &device_count, &total_commands, NULL, NULL);
    fprintf(f, "{\"ok\":true,\"deviceCount\":%d,\"totalCommandCount\":%d,\"displayDeviceLimit\":%d,\"displayCommandLimit\":%d,\"storageCommandLimit\":%d,\"batchCommandLimit\":%d,\"requestBodyLimit\":%d,\"resourceFileLimit\":%d,\"devices\":[",
        device_count, total_commands, MAX_IR_DEVICES, MAX_IR_COMMANDS, MAX_IR_STORED_COMMANDS,
        MAX_IR_BATCH_COMMANDS, MAX_REQUEST_BODY, MAX_RESOURCE_FILE);
    p = raw;
    while ((p = strstr(p, "\"Device\":{")) != NULL) {
        const char *dev_obj = strchr(p, '{');
        const char *dev_end = dev_obj ? find_matching_json(dev_obj, '{', '}') : NULL;
        const char *next_dev, *cmd_key, *cmd_arr, *cmd_end, *cpos;
        char id[32], name[128], manufacturer[128], model[128], type[80];
        long did;
        int command_emitted = 0;
        if (!dev_obj || !dev_end) break;
        did = json_long_range(dev_obj, dev_end, "Id-", 0);
        if (did <= 0) {
            p = dev_end + 1;
            continue;
        }
        snprintf(id, sizeof(id), "%ld", did);
        json_string_range(dev_obj, dev_end, "Name", name, sizeof(name));
        json_string_range(dev_obj, dev_end, "Manufacturer", manufacturer, sizeof(manufacturer));
        json_string_range(dev_obj, dev_end, "Model", model, sizeof(model));
        json_string_range(dev_obj, dev_end, "DeviceTypeDisplayName", type, sizeof(type));
        if (emitted++) fputc(',', f);
        fputs("{\"id\":", f); json_write_string(f, id);
        fputs(",\"name\":", f); json_write_string(f, name);
        fputs(",\"manufacturer\":", f); json_write_string(f, manufacturer);
        fputs(",\"model\":", f); json_write_string(f, model);
        fputs(",\"type\":", f); json_write_string(f, type);
        fprintf(f, ",\"controlPort\":%ld,\"transport\":%ld,\"commands\":[",
            json_long_range(dev_obj, dev_end, "ControlPort", 7),
            json_long_range(dev_obj, dev_end, "Transport", 1));
        next_dev = strstr(dev_end + 1, "\"Device\":{");
        cmd_key = strstr(dev_end, "\"Commands\":[");
        cmd_arr = (cmd_key && (!next_dev || cmd_key < next_dev)) ? strchr(cmd_key, '[') : NULL;
        cmd_end = cmd_arr ? find_matching_json(cmd_arr, '[', ']') : NULL;
        cpos = (cmd_arr && cmd_end) ? cmd_arr + 1 : NULL;
        while (cpos && (cpos = strchr(cpos, '{')) != NULL && cpos < cmd_end) {
            const char *obj_end = find_matching_json(cpos, '{', '}');
            char cid[32], cmd_name[128], keycode[256];
            long cid_num;
            int has_raw;
            if (!obj_end || obj_end > cmd_end) break;
            cid_num = json_long_range(cpos, obj_end, "Id-", 0);
            snprintf(cid, sizeof(cid), "%ld", cid_num);
            json_string_range(cpos, obj_end, "Name", cmd_name, sizeof(cmd_name));
            json_string_range(cpos, obj_end, "KeyCode", keycode, sizeof(keycode));
            has_raw = json_has_nonnull_value(cpos, obj_end, "Raw") && !keycode[0];
            if (command_emitted++) fputc(',', f);
            fputs("{\"id\":", f); json_write_string(f, cid);
            fputs(",\"name\":", f); json_write_string(f, cmd_name);
            fputs(",\"keycode\":", f); json_write_string(f, keycode);
            fprintf(f, ",\"protocolId\":%ld,\"learned\":%s,\"raw\":%s}",
                json_long_range(cpos, obj_end, "ProtocolId", 0),
                json_bool_range(cpos, obj_end, "IsLearned", 0) ? "true" : "false",
                has_raw ? "true" : "false");
            cpos = obj_end + 1;
        }
        fputs("]}", f);
        p = dev_end + 1;
    }
    fputs("]}\n", f);
    fclose(f);
    free(raw);
}

static void render_device_commands_json(int fd, const struct request *req) {
    char device_id[64];
    char *raw;
    size_t len = 0;
    const char *arr, *arr_end, *cpos;
    int emitted = 0;
    FILE *f;
    if (strcmp(req->method, "POST") == 0) {
        form_value(req->body, "deviceId", device_id, sizeof(device_id));
    } else {
        query_value(req->path, "deviceId", device_id, sizeof(device_id));
    }
    if (!safe_label(device_id)) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"invalid IR device id\"}\n", f);
        fclose(f);
        return;
    }
    repair_known_protocols_for_current_commands();
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw || find_device_command_array(raw, device_id, &arr, &arr_end) != 0) {
        free(raw);
        f = send_json_start(fd, "404 Not Found");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"device not found\"}\n", f);
        fclose(f);
        return;
    }
    f = send_json_start(fd, "200 OK");
    if (!f) {
        free(raw);
        return;
    }
    fputs("{\"ok\":true,\"deviceId\":", f); json_write_string(f, device_id);
    fputs(",\"commands\":[", f);
    cpos = arr + 1;
    while ((cpos = strchr(cpos, '{')) != NULL && cpos < arr_end) {
        const char *obj_end = find_matching_json(cpos, '{', '}');
        char cid[32], cmd_name[128], keycode[256];
        long cid_num;
        int has_raw;
        if (!obj_end || obj_end > arr_end) break;
        cid_num = json_long_range(cpos, obj_end, "Id-", 0);
        snprintf(cid, sizeof(cid), "%ld", cid_num);
        json_string_range(cpos, obj_end, "Name", cmd_name, sizeof(cmd_name));
        json_string_range(cpos, obj_end, "KeyCode", keycode, sizeof(keycode));
        has_raw = json_has_nonnull_value(cpos, obj_end, "Raw") && !keycode[0];
        if (emitted++) fputc(',', f);
        fputs("{\"id\":", f); json_write_string(f, cid);
        fputs(",\"name\":", f); json_write_string(f, cmd_name);
        fputs(",\"keycode\":", f); json_write_string(f, keycode);
        fprintf(f, ",\"protocolId\":%ld,\"learned\":%s,\"raw\":%s}",
            json_long_range(cpos, obj_end, "ProtocolId", 0),
            json_bool_range(cpos, obj_end, "IsLearned", 0) ? "true" : "false",
            has_raw ? "true" : "false");
        cpos = obj_end + 1;
    }
    fprintf(f, "],\"count\":%d}\n", emitted);
    fclose(f);
    free(raw);
}

static void render_capture_json(int fd) {
    char reply[4096];
    char mode[16], keycode[512], nec[64], summary[160];
    int protocol_id = 2;
    FILE *f = send_json_start(fd, "200 OK");
    if (!f) return;
    capture_ir_command_action(reply, sizeof(reply));
    analyze_capture_storage(reply, "", "", "2", mode, sizeof(mode), keycode, sizeof(keycode), nec, sizeof(nec), &protocol_id, summary, sizeof(summary));
    fputs("{\"ok\":true,\"raw\":", f); json_write_string(f, reply);
    fputs(",\"mode\":", f); json_write_string(f, mode);
    fprintf(f, ",\"protocolId\":%d,\"keycode\":", protocol_id); json_write_string(f, keycode);
    fputs(",\"nec\":", f); json_write_string(f, nec);
    fputs(",\"analysis\":", f); json_write_string(f, summary);
    fputs("}\n", f);
    fclose(f);
}

static int safe_remotecentral_path(const char *path) {
    const unsigned char *p = (const unsigned char *)path;
    size_t n = strlen(path);
    if (n == 0 || n > 360) return 0;
    if (strncmp(path, "/cgi-bin/codes/", 15) != 0) return 0;
    if (strstr(path, "..") || strstr(path, "://")) return 0;
    while (*p) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '/' || *p == '_' ||
            *p == '-' || *p == '.' || *p == '?' || *p == '=' ||
            *p == '&') {
            p++;
            continue;
        }
        if (*p == '%') {
            int a, b;
            if (!isxdigit((unsigned char)p[1]) || !isxdigit((unsigned char)p[2])) return 0;
            a = tolower((unsigned char)p[1]);
            b = tolower((unsigned char)p[2]);
            if (a == '2' && b == 'e') return 0;
            p += 3;
            continue;
        }
        return 0;
    }
    return 1;
}

static void normalize_remotecentral_path(char *path) {
    const char *hosts[] = {
        "https://www.remotecentral.com",
        "http://www.remotecentral.com",
        "https://remotecentral.com",
        "http://remotecentral.com"
    };
    int i;
    for (i = 0; i < 4; i++) {
        const char *host = hosts[i];
        size_t n = strlen(host);
        if (strncasecmp(path, host, n) == 0) {
            memmove(path, path + n, strlen(path + n) + 1);
            return;
        }
    }
}

static void render_remotecentral_fetch_json(int fd, const struct request *req) {
    char path[384], err[128];
    char *remote = NULL;
    FILE *f;
    if (strcmp(req->method, "POST") == 0) {
        form_value(req->body, "path", path, sizeof(path));
    } else {
        query_value(req->path, "path", path, sizeof(path));
    }
    chomp(path);
    normalize_remotecentral_path(path);
    if (!safe_remotecentral_path(path)) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"source\":\"remotecentral\",\"error\":\"invalid RemoteCentral path\"}\n", f);
        fclose(f);
        return;
    }
    if (http_get_body("www.remotecentral.com", path, &remote, MAX_REQUEST_BODY, err, sizeof(err)) != 0) {
        f = send_json_start(fd, "200 OK");
        if (!f) return;
        fputs("{\"ok\":false,\"source\":\"remotecentral\",\"path\":", f);
        json_write_string(f, path);
        fputs(",\"error\":", f);
        json_write_string(f, err[0] ? err : "RemoteCentral request failed");
        fputs("}\n", f);
        fclose(f);
        return;
    }
    f = send_json_start(fd, "200 OK");
    if (!f) {
        free(remote);
        return;
    }
    fputs("{\"ok\":true,\"source\":\"remotecentral\",\"path\":", f);
    json_write_string(f, path);
    fputs(",\"html\":", f);
    json_write_string(f, remote);
    fputs("}\n", f);
    fclose(f);
    free(remote);
}

static void request_resource_reload(void) {
    FILE *f = fopen(RESOURCE_RELOAD_FLAG, "w");
    if (f) {
        fputs("DeviceList\nFunctionList\nProtocolList\n", f);
        fclose(f);
    }
    sync();
    trigger_mqtt_discover();
}

static void backup_resources(void) {
    char cmd[512];
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

static void bundle_value(FILE *f, const char *key, const char *path, int comma) {
    char *data;
    size_t len = 0;
    if (comma) fputc(',', f);
    json_write_string(f, key);
    fputc(':', f);
    data = read_file_alloc(path, MAX_RESOURCE_FILE, &len);
    (void)len;
    json_write_string(f, data ? data : "");
    free(data);
}

static void send_bundle_download(int fd) {
    const char *hdr =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Disposition: attachment; filename=\"harmony-owner-bundle.json\"\r\n"
        "Connection: close\r\n\r\n";
    FILE *f;
    send_all(fd, hdr, strlen(hdr));
    f = fdopen(dup(fd), "w");
    if (!f) return;
    fputs("{\"format\":\"harmony-owner-bundle-v1\",\"files\":{", f);
    bundle_value(f, "DeviceList.json", DEVICE_LIST, 0);
    bundle_value(f, "FunctionList.json", FUNCTION_LIST, 1);
    bundle_value(f, "ProtocolList.json", PROTOCOL_LIST, 1);
    bundle_value(f, "mqtt-config.json", MQTT_CONFIG, 1);
    bundle_value(f, "wpa_supplicant.conf", WPA_CONFIG, 1);
    bundle_value(f, "bt-devices.json", BT_DEVICE_STORE, 1);
    fputc(',', f);
    json_write_string(f, "cloud-blocker.conf");
    fputc(':', f);
    json_write_string(f, load_cloud_blocker() ? "1\n" : "0\n");
    fputs("}}\n", f);
    fclose(f);
}

static int append_top_array_item(const char *path, const char *array_key, const char *item) {
    size_t len, prefix_len, suffix_len, item_len, out_len;
    char *raw, *out;
    const char *key, *arr, *arr_end, *scan;
    int empty = 1;
    raw = read_file_alloc(path, MAX_RESOURCE_FILE, &len);
    if (!raw) return -1;
    key = strstr(raw, array_key);
    arr = key ? strchr(key, '[') : NULL;
    arr_end = arr ? find_matching_json(arr, '[', ']') : NULL;
    if (!arr || !arr_end) {
        free(raw);
        return -1;
    }
    for (scan = arr + 1; scan < arr_end; scan++) {
        if (!isspace((unsigned char)*scan)) {
            empty = 0;
            break;
        }
    }
    prefix_len = (size_t)(arr_end - raw);
    suffix_len = len - prefix_len;
    item_len = strlen(item);
    out_len = prefix_len + item_len + (empty ? 0 : 1) + suffix_len;
    out = (char *)malloc(out_len + 1);
    if (!out) {
        free(raw);
        return -1;
    }
    memcpy(out, raw, prefix_len);
    if (!empty) out[prefix_len++] = ',';
    memcpy(out + prefix_len, item, item_len);
    memcpy(out + prefix_len + item_len, raw + (arr_end - raw), suffix_len);
    out[out_len] = 0;
    free(raw);
    if (write_file_atomic(path, out, out_len) != 0) {
        free(out);
        return -1;
    }
    free(out);
    return 0;
}

static int protocol_list_has_id(const char *raw, int protocol_id) {
    char needle[32];
    snprintf(needle, sizeof(needle), "\"Id-\":%d", protocol_id);
    return raw && strstr(raw, needle) != NULL;
}

static int ensure_protocol_object(int protocol_id, const char *protocol_json) {
    char *raw = read_file_alloc(PROTOCOL_LIST, MAX_RESOURCE_FILE, NULL);
    int present;
    if (!raw) return -1;
    present = protocol_list_has_id(raw, protocol_id);
    free(raw);
    if (present) return 0;
    return append_top_array_item(PROTOCOL_LIST, "\"Protocols\"", protocol_json);
}

static int ensure_builtin_protocol_for_id(int protocol_id) {
    if (protocol_id == 2) return ensure_protocol_object(2, BUILTIN_PROTOCOL_TOSHIBA_32);
    if (protocol_id == 679) return ensure_protocol_object(679, BUILTIN_PROTOCOL_MEMOREX_O1);
    return 0;
}

static int protocol_file_has_id(int protocol_id) {
    char *raw = read_file_alloc(PROTOCOL_LIST, MAX_RESOURCE_FILE, NULL);
    int present;
    if (!raw) return -1;
    present = protocol_list_has_id(raw, protocol_id);
    free(raw);
    return present ? 1 : 0;
}

static int repair_known_protocols_for_current_commands(void) {
    char *devices = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, NULL);
    int need2, need679, missing2 = 0, missing679 = 0, changed = 0;
    if (!devices) return -1;
    need2 = strstr(devices, "\"ProtocolId\":2") != NULL;
    need679 = strstr(devices, "\"ProtocolId\":679") != NULL;
    free(devices);
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

static int find_device_command_array(const char *raw, const char *device_id, const char **arr, const char **arr_end) {
    const char *p = raw;
    char idneedle[64];
    snprintf(idneedle, sizeof(idneedle), "\"Id-\":%s", device_id);
    while ((p = strstr(p, "\"Device\":{")) != NULL) {
        const char *dev_obj = strchr(p, '{');
        const char *dev_end = dev_obj ? find_matching_json(dev_obj, '{', '}') : NULL;
        const char *cmd_key, *next_dev;
        if (!dev_obj || !dev_end) return -1;
        if (strstr(dev_obj, idneedle) && strstr(dev_obj, idneedle) < dev_end) {
            next_dev = strstr(dev_end + 1, "\"Device\":{");
            cmd_key = strstr(dev_end, "\"Commands\":[");
            if (cmd_key && next_dev && cmd_key > next_dev) cmd_key = NULL;
            *arr = cmd_key ? strchr(cmd_key, '[') : NULL;
            *arr_end = *arr ? find_matching_json(*arr, '[', ']') : NULL;
            return (*arr && *arr_end) ? 0 : -1;
        }
        p = dev_end + 1;
    }
    return -1;
}

static int append_command_to_device(const char *device_id, const char *command_json) {
    size_t len, prefix_len, suffix_len, item_len, out_len;
    char *raw, *out;
    const char *arr, *arr_end, *scan;
    int empty = 1;
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw) return -1;
    if (find_device_command_array(raw, device_id, &arr, &arr_end) != 0) {
        free(raw);
        return -1;
    }
    for (scan = arr + 1; scan < arr_end; scan++) {
        if (!isspace((unsigned char)*scan)) {
            empty = 0;
            break;
        }
    }
    prefix_len = (size_t)(arr_end - raw);
    suffix_len = len - prefix_len;
    item_len = strlen(command_json);
    out_len = prefix_len + item_len + (empty ? 0 : 1) + suffix_len;
    out = (char *)malloc(out_len + 1);
    if (!out) {
        free(raw);
        return -1;
    }
    memcpy(out, raw, prefix_len);
    if (!empty) out[prefix_len++] = ',';
    memcpy(out + prefix_len, command_json, item_len);
    memcpy(out + prefix_len + item_len, raw + (arr_end - raw), suffix_len);
    out[out_len] = 0;
    free(raw);
    if (write_file_atomic(DEVICE_LIST, out, out_len) != 0) {
        free(out);
        return -1;
    }
    free(out);
    return 0;
}

static int remove_json_span(char **pdata, size_t *plen, size_t start, size_t end) {
    char *raw = *pdata, *out;
    size_t newlen;
    if (start > 0 && raw[start - 1] == ',') {
        start--;
    } else if (end < *plen && raw[end] == ',') {
        end++;
    }
    newlen = *plen - (end - start);
    out = (char *)malloc(newlen + 1);
    if (!out) return -1;
    memcpy(out, raw, start);
    memcpy(out + start, raw + end, *plen - end);
    out[newlen] = 0;
    free(raw);
    *pdata = out;
    *plen = newlen;
    return 0;
}

static int delete_ir_device(const char *device_id, char *msg, size_t msglen) {
    char *raw;
    const char *p;
    size_t len;
    char idneedle[64];
    snprintf(idneedle, sizeof(idneedle), "\"Id-\":%s", device_id);
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw) {
        snprintf(msg, msglen, "Failed to read DeviceList.");
        return -1;
    }
    p = raw;
    while ((p = strstr(p, "\"Device\":{")) != NULL) {
        const char *item_start = p;
        const char *item_end;
        while (item_start > raw && *item_start != '{') item_start--;
        item_end = find_matching_json(item_start, '{', '}');
        if (!item_end) break;
        if (strstr(item_start, idneedle) && strstr(item_start, idneedle) < item_end) {
            backup_resources();
            if (remove_json_span(&raw, &len, (size_t)(item_start - raw), (size_t)(item_end + 1 - raw)) != 0 ||
                write_file_atomic(DEVICE_LIST, raw, len) != 0) {
                free(raw);
                snprintf(msg, msglen, "Failed to delete device %s.", device_id);
                return -1;
            }
            free(raw);
            request_resource_reload();
            snprintf(msg, msglen, "Deleted device %s.", device_id);
            return 0;
        }
        p = item_end + 1;
    }
    free(raw);
    snprintf(msg, msglen, "Device %s not found.", device_id);
    return -1;
}

static int delete_ir_command(const char *device_id, const char *command_name, char *msg, size_t msglen) {
    char *raw;
    size_t len;
    const char *arr, *arr_end, *cpos;
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw) {
        snprintf(msg, msglen, "Failed to read DeviceList.");
        return -1;
    }
    if (find_device_command_array(raw, device_id, &arr, &arr_end) != 0) {
        free(raw);
        snprintf(msg, msglen, "Device %s not found.", device_id);
        return -1;
    }
    cpos = arr + 1;
    while ((cpos = strchr(cpos, '{')) != NULL && cpos < arr_end) {
        char name[128];
        const char *obj_end = find_matching_json(cpos, '{', '}');
        if (!obj_end || obj_end > arr_end) break;
        json_string_range(cpos, obj_end, "Name", name, sizeof(name));
        if (strcmp(name, command_name) == 0) {
            backup_resources();
            if (remove_json_span(&raw, &len, (size_t)(cpos - raw), (size_t)(obj_end + 1 - raw)) != 0 ||
                write_file_atomic(DEVICE_LIST, raw, len) != 0) {
                free(raw);
                snprintf(msg, msglen, "Failed to delete command %s.", command_name);
                return -1;
            }
            free(raw);
            request_resource_reload();
            snprintf(msg, msglen, "Deleted command %s from device %s.", command_name, device_id);
            return 0;
        }
        cpos = obj_end + 1;
    }
    free(raw);
    snprintf(msg, msglen, "Command %s not found on device %s.", command_name, device_id);
    return -1;
}

static int replace_span(char **pdata, size_t *plen, size_t start, size_t end, const char *replacement);

static int clear_ir_commands(const char *device_id, char *msg, size_t msglen) {
    char *raw;
    size_t len;
    const char *arr, *arr_end;
    int count = 0;
    if (!safe_label(device_id)) {
        snprintf(msg, msglen, "Invalid device %s.", device_id ? device_id : "");
        return -1;
    }
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw) {
        snprintf(msg, msglen, "Failed to read DeviceList.");
        return -1;
    }
    if (find_device_command_array(raw, device_id, &arr, &arr_end) != 0) {
        free(raw);
        snprintf(msg, msglen, "Device %s not found.", device_id);
        return -1;
    }
    scan_command_array(arr, arr_end, &count, NULL);
    backup_resources();
    if (replace_span(&raw, &len, (size_t)(arr + 1 - raw), (size_t)(arr_end - raw), "") != 0 ||
        write_file_atomic(DEVICE_LIST, raw, len) != 0) {
        free(raw);
        snprintf(msg, msglen, "Failed to clear commands from device %s.", device_id);
        return -1;
    }
    free(raw);
    request_resource_reload();
    snprintf(msg, msglen, "Cleared %d commands from device %s.", count, device_id);
    return 0;
}

static int replace_span(char **pdata, size_t *plen, size_t start, size_t end, const char *replacement) {
    char *raw = *pdata, *out;
    size_t rlen = strlen(replacement);
    size_t newlen = *plen - (end - start) + rlen;
    out = (char *)malloc(newlen + 1);
    if (!out) return -1;
    memcpy(out, raw, start);
    memcpy(out + start, replacement, rlen);
    memcpy(out + start + rlen, raw + end, *plen - end);
    out[newlen] = 0;
    free(raw);
    *pdata = out;
    *plen = newlen;
    return 0;
}

static int update_device_string_field(char **pdata, size_t *plen, const char *device_id, const char *key, const char *value) {
    char idneedle[64];
    const char *raw = *pdata;
    const char *p = raw;
    char *escaped;
    snprintf(idneedle, sizeof(idneedle), "\"Id-\":%s", device_id);
    while ((p = strstr(p, "\"Device\":{")) != NULL) {
        const char *dev_obj = strchr(p, '{');
        const char *dev_end = dev_obj ? find_matching_json(dev_obj, '{', '}') : NULL;
        const char *field, *colon, *vstart, *vend;
        if (!dev_obj || !dev_end) return -1;
        if (!(strstr(dev_obj, idneedle) && strstr(dev_obj, idneedle) < dev_end)) {
            p = dev_end + 1;
            continue;
        }
        field = find_key_range(dev_obj, dev_end, key);
        if (!field) return -1;
        colon = strchr(field + strlen(key) + 2, ':');
        if (!colon || colon >= dev_end) return -1;
        vstart = colon + 1;
        while (*vstart && vstart < dev_end && isspace((unsigned char)*vstart)) vstart++;
        if (*vstart != '"') return -1;
        vend = vstart + 1;
        while (*vend && vend < dev_end) {
            if (*vend == '\\' && vend[1]) vend += 2;
            else if (*vend == '"') break;
            else vend++;
        }
        if (*vend != '"') return -1;
        escaped = json_escape_alloc(value);
        if (!escaped) return -1;
        if (replace_span(pdata, plen, (size_t)(vstart - raw), (size_t)(vend + 1 - raw), escaped) != 0) {
            free(escaped);
            return -1;
        }
        free(escaped);
        return 0;
    }
    return -1;
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
    char *en = NULL, *em = NULL, *emo = NULL, *et = NULL, *item = NULL;
    long id, max_device_id = 0;
    int dtype;
    if (!safe_label(name) || !safe_label(manufacturer) || !safe_label(model) || !safe_label(type)) {
        snprintf(msg, msglen, "Device fields cannot be empty or contain control characters.");
        return -1;
    }
    scan_ir_resource_stats(NULL, NULL, &max_device_id, NULL);
    id = max_device_id + 1;
    if (id < 90000000) id = 90000000 + (long)(time(NULL) % 9000000);
    dtype = harmony_device_type_id(type);
    en = json_escape_alloc(name);
    em = json_escape_alloc(manufacturer);
    emo = json_escape_alloc(model);
    et = json_escape_alloc(type);
    item = (char *)malloc(4096);
    if (!en || !em || !emo || !et || !item) goto fail;
    snprintf(item, 4096,
        "{\"Device\":{\"DongleIndex\":0,\"InterDeviceDelay\":500,\"HoldInterKeyDelay\":100,\"DeviceType\":%d,"
        "\"RegisterSection\":null,\"ContentProfileKey\":%ld,\"DeviceOrder\":0,\"DeviceProfileUri\":\"\","
        "\"SetupState\":1,\"DeviceSearchType\":-1,\"GlobalDeviceVersionId-\":0,\"DeviceTypeDisplayName\":%s,"
        "\"FriendlyName\":null,\"Transport\":1,\"PressMinRepeats\":3,\"PrivateAddType\":1,\"RegionalCharset\":null,"
        "\"Tokens\":null,\"BTAddress\":null,\"DongleRFID\":0,\"AutoDetectedDevice\":null,\"GlobalLanguageVersionId-\":0,"
        "\"ParentDeviceId\":null,\"DecodedEdid\":null,\"DefaultPressMinRepeats\":3,\"ParentDeviceModel\":\"\","
        "\"RenewSection\":null,\"IsMultiCode\":false,\"GroupName\":null,\"DefaultInterDeviceDelay\":0,\"ParentDevice-\":0,"
        "\"DeviceAddedDate\":\"/Date(%ld000+0000)/\",\"ParentDeviceManufacturer\":%s,\"AppLaunchConfigs\":null,"
        "\"PictureId\":null,\"IsKeyboardAssociated\":false,\"IsScartCableSupported\":false,\"Manufacturer\":%s,"
        "\"Icon\":%d,\"Id-\":%ld,\"IsInterKeyDelayOptimized\":false,\"SuggestedDisplay\":\"DEFAULT\",\"ControlPort\":7,"
        "\"EncodedEdid\":null,\"InterKeyDelay\":300,\"DeviceClassification\":0,\"DeviceCapabilitiesWithPriority\":[],"
        "\"CopiedDeviceSource\":-1,\"State\":1,\"HoldInterDeviceDelay\":0,\"Name\":%s,\"Model\":%s,"
        "\"ActivityIds\":null,\"Characterization\":0,\"HoldMinRepeats\":-1,\"DefaultInterKeyDelay\":0},"
        "\"Commands\":[],\"DeviceFeatures\":[]}",
        dtype, id, et, (long)time(NULL), em, em, dtype, id, en, emo);
    backup_resources();
    if (append_top_array_item(DEVICE_LIST, "\"DevicesWithFeatures\"", item) != 0) goto fail;
    request_resource_reload();
    if (created_id && created_id_len) snprintf(created_id, created_id_len, "%ld", id);
    snprintf(msg, msglen, "Created IR device %s (%ld).", name, id);
    free(en); free(em); free(emo); free(et); free(item);
    return 0;
fail:
    free(en); free(em); free(emo); free(et); free(item);
    snprintf(msg, msglen, "Failed to create IR device.");
    return -1;
}

static int create_ir_device(const char *name, const char *manufacturer, const char *model, const char *type, char *msg, size_t msglen) {
    return create_ir_device_ex(name, manufacturer, model, type, msg, msglen, NULL, 0);
}

static int ensure_lab_target_device(char *device_id, size_t device_id_len, int *created, char *msg, size_t msglen) {
    const char *name = "Temporary IR Test";
    if (created) *created = 0;
    if (find_ir_device_by_name(name, device_id, device_id_len) == 0) {
        snprintf(msg, msglen, "Using existing %s (%s).", name, device_id);
        return 0;
    }
    if (create_ir_device_ex(name, "Local", "Command Sweep", "HomeAppliance", msg, msglen, device_id, device_id_len) == 0) {
        if (created) *created = 1;
        return 0;
    }
    return -1;
}

static int update_ir_device(const char *device_id, const char *name, const char *manufacturer, const char *model, const char *type, char *msg, size_t msglen) {
    char *raw;
    size_t len;
    if (!safe_label(device_id) || !safe_label(name) || !safe_label(manufacturer) || !safe_label(model) || !safe_label(type)) {
        snprintf(msg, msglen, "Device fields cannot be empty or contain control characters.");
        return -1;
    }
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw) {
        snprintf(msg, msglen, "Failed to read DeviceList.");
        return -1;
    }
    backup_resources();
    if (update_device_string_field(&raw, &len, device_id, "Name", name) != 0 ||
        update_device_string_field(&raw, &len, device_id, "Manufacturer", manufacturer) != 0 ||
        update_device_string_field(&raw, &len, device_id, "Model", model) != 0 ||
        update_device_string_field(&raw, &len, device_id, "DeviceTypeDisplayName", type) != 0) {
        free(raw);
        snprintf(msg, msglen, "Failed to update device %s.", device_id);
        return -1;
    }
    if (write_file_atomic(DEVICE_LIST, raw, len) != 0) {
        free(raw);
        snprintf(msg, msglen, "Failed to save DeviceList.");
        return -1;
    }
    free(raw);
    request_resource_reload();
    snprintf(msg, msglen, "Updated device %s.", device_id);
    return 0;
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

static char *build_ir_command_json(long id, const char *name, const char *mode, int protocol_id, const char *code, const char *raw_code) {
    char *ename = NULL, *ecode = NULL, *eraw = NULL, *cmd = NULL;
    size_t cap;
    ename = json_escape_alloc(name);
    ecode = json_escape_alloc(code);
    eraw = json_escape_alloc(raw_code);
    cap = (ename ? strlen(ename) : 0) + (ecode ? strlen(ecode) : 0) + (eraw ? strlen(eraw) : 0) + 1024;
    if (cap < 2048) cap = 2048;
    cmd = (char *)malloc(cap);
    if (!ename || !ecode || !eraw || !cmd) goto fail;
    if (strcmp(mode, "raw") == 0) {
        snprintf(cmd, cap,
            "{\"Raw\":%s,\"Id-\":%ld,\"KeyCode\":\"\",\"DateTaught\":\"/Date(%ld000+0000)/\","
            "\"FunctionId\":null,\"Parameters\":null,\"Name\":%s,\"FunctionGroupId\":0,\"TransportType\":1,"
            "\"ProtocolId\":null,\"CommandTypeId\":\"\",\"IsLearned\":true}",
            eraw, id, (long)time(NULL), ename);
    } else {
        snprintf(cmd, cap,
            "{\"Raw\":null,\"Id-\":%ld,\"KeyCode\":%s,\"DateTaught\":\"/Date(%ld000+0000)/\","
            "\"FunctionId\":null,\"Parameters\":null,\"Name\":%s,\"FunctionGroupId\":0,\"TransportType\":1,"
            "\"ProtocolId\":%d,\"CommandTypeId\":\"\",\"IsLearned\":true}",
            id, ecode, (long)time(NULL), ename, protocol_id);
    }
    free(ename); free(ecode); free(eraw);
    return cmd;
fail:
    free(ename); free(ecode); free(eraw); free(cmd);
    return NULL;
}

static int find_ir_command_span(const char *raw, const char *device_id, const char *command_name, const char **obj, const char **obj_end) {
    const char *arr, *arr_end, *cpos;
    if (find_device_command_array(raw, device_id, &arr, &arr_end) != 0) return -1;
    cpos = arr + 1;
    while ((cpos = strchr(cpos, '{')) != NULL && cpos < arr_end) {
        char name[128];
        const char *end = find_matching_json(cpos, '{', '}');
        if (!end || end > arr_end) break;
        json_string_range(cpos, end, "Name", name, sizeof(name));
        if (strcmp(name, command_name) == 0) {
            if (obj) *obj = cpos;
            if (obj_end) *obj_end = end;
            return 0;
        }
        cpos = end + 1;
    }
    return -1;
}

static int update_ir_command(const char *device_id, const char *old_name, const char *new_name, const char *mode, const char *protocol_text, const char *nec, const char *keycode, const char *raw_code, char *msg, size_t msglen) {
    char *raw;
    const char *arr, *arr_end, *obj, *obj_end;
    size_t len;
    long id;
    int protocol_id = atoi(protocol_text);
    char effective_mode[16];
    char code[512];
    char *cmd = NULL;
    if (!safe_label(device_id) || !safe_label(old_name) || !safe_label(new_name)) {
        snprintf(msg, msglen, "Device, current command name, and new command name are required.");
        return -1;
    }
    if (protocol_id <= 0) protocol_id = 2;
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &len);
    if (!raw || find_device_command_array(raw, device_id, &arr, &arr_end) != 0 ||
        find_ir_command_span(raw, device_id, old_name, &obj, &obj_end) != 0) {
        free(raw);
        snprintf(msg, msglen, "Command %s was not found on device %s.", old_name, device_id);
        return -1;
    }
    if (strcmp(old_name, new_name) != 0 && command_array_has_name(arr, arr_end, new_name)) {
        free(raw);
        snprintf(msg, msglen, "Command %s already exists on device %s.", new_name, device_id);
        return -1;
    }
    id = json_long_range(obj, obj_end, "Id-", 0);
    if (id <= 0) id = 39000000 + (long)(time(NULL) % 9000000);
    copy_text(effective_mode, sizeof(effective_mode), mode && mode[0] ? mode : "keycode");
    code[0] = 0;
    if (strcmp(effective_mode, "raw") == 0) {
        if (!raw_code[0]) {
            free(raw);
            snprintf(msg, msglen, "Raw command data is required.");
            return -1;
        }
    } else if (strcmp(effective_mode, "nec") == 0) {
        if (build_nec_keycode(nec, protocol_id, code, sizeof(code)) != 0) {
            free(raw);
            snprintf(msg, msglen, "NEC value must be 1-8 hex digits.");
            return -1;
        }
    } else {
        copy_text(effective_mode, sizeof(effective_mode), "keycode");
        if (!keycode[0]) {
            free(raw);
            snprintf(msg, msglen, "Harmony compact code is required.");
            return -1;
        }
        copy_text(code, sizeof(code), keycode);
    }
    cmd = build_ir_command_json(id, new_name, effective_mode, protocol_id, code, raw_code);
    if (!cmd) {
        free(raw);
        snprintf(msg, msglen, "Failed to build updated command.");
        return -1;
    }
    backup_resources();
    if (strcmp(effective_mode, "raw") != 0 && ensure_builtin_protocol_for_id(protocol_id) != 0) {
        free(raw);
        free(cmd);
        snprintf(msg, msglen, "Failed to ensure IR protocol %d.", protocol_id);
        return -1;
    }
    if (replace_span(&raw, &len, (size_t)(obj - raw), (size_t)(obj_end + 1 - raw), cmd) != 0 ||
        write_file_atomic(DEVICE_LIST, raw, len) != 0) {
        free(raw);
        free(cmd);
        snprintf(msg, msglen, "Failed to save updated command %s.", new_name);
        return -1;
    }
    free(raw);
    free(cmd);
    request_resource_reload();
    snprintf(msg, msglen, "Updated command %s on device %s.", new_name, device_id);
    return 0;
}

static int add_ir_command(const char *device_id, const char *name, const char *mode, const char *protocol_text, const char *nec, const char *keycode, const char *raw_code, char *msg, size_t msglen) {
    char *raw;
    const char *arr, *arr_end;
    long id, max_command_id = 0;
    int protocol_id = atoi(protocol_text);
    int existing_commands = 0;
    char effective_mode[16];
    char effective_keycode[512];
    char effective_nec[64];
    char storage_note[160];
    char code[512];
    char *cmd = NULL;
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
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, NULL);
    if (!raw || find_device_command_array(raw, device_id, &arr, &arr_end) != 0) {
        free(raw);
        snprintf(msg, msglen, "Device %s not found.", device_id);
        return -1;
    }
    scan_ir_resource_stats_from_raw(raw, NULL, NULL, NULL, &max_command_id);
    scan_command_array(arr, arr_end, &existing_commands, NULL);
    if (existing_commands >= MAX_IR_STORED_COMMANDS) {
        free(raw);
        snprintf(msg, msglen, "Device %s already has the local storage limit of %d commands.", device_id, MAX_IR_STORED_COMMANDS);
        return -1;
    }
    if (command_array_has_name(arr, arr_end, name)) {
        free(raw);
        snprintf(msg, msglen, "Command %s already exists on device %s.", name, device_id);
        return -1;
    }
    free(raw);
    id = max_command_id + 1;
    if (id < 39000000) id = 39000000 + (long)(time(NULL) % 9000000);
    cmd = build_ir_command_json(id, name, effective_mode, protocol_id, code, raw_code);
    if (!cmd) goto fail;
    backup_resources();
    if (strcmp(effective_mode, "raw") != 0 && ensure_builtin_protocol_for_id(protocol_id) != 0) {
        snprintf(msg, msglen, "Failed to ensure IR protocol %d.", protocol_id);
        goto fail;
    }
    if (append_command_to_device(device_id, cmd) != 0) goto fail;
    request_resource_reload();
    if (auto_mode && storage_note[0]) snprintf(msg, msglen, "Saved command %s on device %s. %s", name, device_id, storage_note);
    else snprintf(msg, msglen, "Saved command %s on device %s.", name, device_id);
    free(cmd);
    return 0;
fail:
    free(cmd);
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

static int bulk_import_irdb_commands(const char *device_id, char *payload, char *msg, size_t msglen) {
    char *line, *save, *joined = NULL, *raw, *out;
    const char *arr, *arr_end, *scan;
    size_t joined_len = 0, joined_cap = 0, raw_len, prefix_len, suffix_len, out_len;
    long next_id, max_command_id = 0;
    int imported = 0, skipped = 0, empty = 1, remaining, existing_commands = 0, imported_keycodes = 0;
    if (!safe_label(device_id)) {
        snprintf(msg, msglen, "Invalid device for IRDB import.");
        return -1;
    }
    raw = read_file_alloc(DEVICE_LIST, MAX_RESOURCE_FILE, &raw_len);
    if (!raw || find_device_command_array(raw, device_id, &arr, &arr_end) != 0) {
        free(raw);
        snprintf(msg, msglen, "Failed to locate device command list.");
        return -1;
    }
    scan_ir_resource_stats_from_raw(raw, NULL, NULL, NULL, &max_command_id);
    scan_command_array(arr, arr_end, &existing_commands, NULL);
    remaining = MAX_IR_STORED_COMMANDS - existing_commands;
    if (remaining <= 0) {
        free(raw);
        snprintf(msg, msglen, "Device %s already has the local storage limit of %d commands.", device_id, MAX_IR_STORED_COMMANDS);
        return -1;
    }
    next_id = max_command_id + 1;
    if (next_id < 39000000) next_id = 39000000 + (long)(time(NULL) % 9000000);
    line = strtok_r(payload, "\n", &save);
    while (line) {
        char *fields[8], *name = NULL, *mode = "keycode", *keycode = NULL, *raw_code = NULL;
        char generated[512];
        char *cmd_json;
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
        if (!name || !name[0] || !safe_label(name) || command_array_has_name(arr, arr_end, name) ||
            command_fragment_has_name(joined, name) ||
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
        cmd_json = strcasecmp(mode, "raw") == 0 ?
            build_ir_command_json(next_id++, name, "raw", 2, "", raw_code) :
            build_ir_command_json(next_id++, name, "keycode", 2, keycode, "");
        if (!cmd_json || append_text(&joined, &joined_len, &joined_cap, cmd_json, imported > 0) != 0) {
            free(cmd_json);
            free(joined);
            free(raw);
            snprintf(msg, msglen, "Failed to stage IRDB commands.");
            return -1;
        }
        free(cmd_json);
        imported++;
        if (strcasecmp(mode, "raw") != 0) imported_keycodes++;
        line = strtok_r(NULL, "\n", &save);
    }
    if (imported == 0) {
        free(joined);
        free(raw);
        snprintf(msg, msglen, "No supported new IRDB commands were selected.");
        return -1;
    }
    for (scan = arr + 1; scan < arr_end; scan++) {
        if (!isspace((unsigned char)*scan)) {
            empty = 0;
            break;
        }
    }
    prefix_len = (size_t)(arr_end - raw);
    suffix_len = raw_len - prefix_len;
    out_len = prefix_len + joined_len + (empty ? 0 : 1) + suffix_len;
    out = (char *)malloc(out_len + 1);
    if (!out) {
        free(raw);
        free(joined);
        snprintf(msg, msglen, "Not enough memory to import IRDB commands.");
        return -1;
    }
    memcpy(out, raw, prefix_len);
    if (!empty) out[prefix_len++] = ',';
    memcpy(out + prefix_len, joined, joined_len);
    memcpy(out + prefix_len + joined_len, raw + (arr_end - raw), suffix_len);
    out[out_len] = 0;
    backup_resources();
    if (imported_keycodes > 0 && ensure_builtin_protocol_for_id(2) != 0) {
        free(raw);
        free(joined);
        free(out);
        snprintf(msg, msglen, "Failed to ensure NEC-compatible IR protocol.");
        return -1;
    }
    if (write_file_atomic(DEVICE_LIST, out, out_len) != 0) {
        free(raw);
        free(joined);
        free(out);
        snprintf(msg, msglen, "Failed to write imported IRDB commands.");
        return -1;
    }
    free(raw);
    free(joined);
    free(out);
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

static int mark_ir_run_canceled(const char *run_id) {
    char path[192];
    FILE *f;
    int ok = ir_cancel_path(run_id, path, sizeof(path));
    if (ok <= 0) return -1;
    f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%ld\n", (long)time(NULL));
    fclose(f);
    return 0;
}

static int cancelable_sleep_ms(int delay_ms, const char *run_id) {
    int slept = 0;
    while (slept < delay_ms) {
        int step = delay_ms - slept;
        if (ir_run_canceled(run_id)) return 1;
        if (step > 100) step = 100;
        usleep((useconds_t)step * 1000);
        slept += step;
    }
    return ir_run_canceled(run_id);
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

static void log_ir_note_event(const char *event, const char *source, const char *run_id, const char *detail) {
    rotate_ir_event_log();
    FILE *f = fopen(IR_EVENT_LOG, "a");
    if (!f) return;
    fputs("{\"event\":", f); json_write_string(f, event ? event : "ir_note");
    fputs(",\"ts\":", f); fprintf(f, "%ld", (long)time(NULL));
    fputs(",\"source\":", f); json_write_string(f, source ? source : "");
    fputs(",\"runId\":", f); json_write_string(f, run_id ? run_id : "");
    fputs(",\"detail\":", f); json_write_string(f, detail ? detail : "");
    fputs("}\n", f);
    fclose(f);
}

static void send_ir_command_action_ex(const char *device_id, const char *command, const char *source, const char *run_id, char *out, size_t outlen) {
    char hub_id[64];
    char action[512], params[768], esc_id[128], esc_params[1024], cmd[1400];
    if (!load_hub_id(hub_id, sizeof(hub_id))) {
        snprintf(out, outlen, "Hub ID is missing. Re-run the root tool or reinstall with the numeric Hub ID printed as hub_id=...");
        log_ir_event(source, run_id, device_id, command, out);
        return;
    }
    snprintf(action, sizeof(action), "{\\\"type\\\":\\\"IRCommand\\\",\\\"deviceId\\\":\\\"%s\\\",\\\"command\\\":\\\"%s\\\"}", device_id, command);
    snprintf(params, sizeof(params), "{\"status\":\"pressrelease\",\"count\":1,\"action\":\"%s\"}", action);
    shell_escape_single(hub_id, esc_id, sizeof(esc_id));
    shell_escape_single(params, esc_params, sizeof(esc_params));
    snprintf(cmd, sizeof(cmd), "/data/codex/bin/codex_hbus '%s' harmony.engine?holdaction '%s' 2>&1", esc_id, esc_params);
    run_cmd(cmd, out, outlen);
    log_ir_event(source, run_id, device_id, command, out && out[0] ? out : "no response");
}

static void send_ir_command_action(const char *device_id, const char *command, char *out, size_t outlen) {
    send_ir_command_action_ex(device_id, command, "webui", "", out, outlen);
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

static void page_head(FILE *f, const char *title) {
    int cloud_blocked = load_cloud_blocker();
    fprintf(f,
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>");
    html(f, title);
    fputs(
        "</title><style>"
        ":root{color-scheme:light;--bg:#f5f7f9;--fg:#182321;--muted:#687672;--line:#dce3e1;--panel:#fff;--soft:#f2f6f5;--soft2:#eaf6f4;--accent:#0f766e;--accent2:#32649d;--bad:#b42318;--ok:#087443;--warn:#a05a00;--wash:#fafcfb;--shadow:0 10px 28px rgba(25,41,37,.07)}"
        "*{box-sizing:border-box}html,body{width:100%;max-width:100%;overflow-x:hidden}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.48 system-ui,-apple-system,Segoe UI,sans-serif}"
        "header{position:sticky;top:0;background:rgba(255,255,255,.98);backdrop-filter:saturate(1.15) blur(12px);border-bottom:1px solid var(--line);padding:12px 20px;z-index:3}"
        ".topbar{max-width:1280px;margin:0 auto;display:flex;align-items:center;justify-content:space-between;gap:16px}.brand{display:flex;align-items:center;gap:11px}.brand-mark{width:34px;height:34px;border-radius:8px;background:var(--accent);display:grid;place-items:center;color:#fff;font-weight:750}.brand h1{letter-spacing:0}.brand small{display:block;color:var(--muted);font-size:12px}.top-status{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}"
        ".app-shell{max-width:1280px;margin:0 auto;padding:22px 20px;display:grid;grid-template-columns:236px minmax(0,1fr);gap:22px}.side-menu{position:sticky;top:78px;align-self:start;display:grid;gap:5px;background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:9px;box-shadow:var(--shadow);min-width:0;max-width:100%}.menu-item{display:grid;grid-template-columns:34px 1fr;gap:10px;align-items:center;text-align:left;border:0;background:transparent;color:var(--fg);border-radius:8px;padding:10px 11px;box-shadow:none;min-height:54px}.menu-item>*{min-width:0}.menu-item:hover{background:var(--soft)}.menu-item span:first-child{width:28px;height:28px;border-radius:8px;display:grid;place-items:center;background:#edf3f2;color:var(--accent);font-size:0;position:relative}.menu-item span:first-child:before{content:\"\";display:block;width:13px;height:13px;border:2px solid currentColor;border-radius:4px}.menu-item[data-view-target=control] span:first-child:before{width:16px;height:16px;border-radius:50%;box-shadow:inset 0 0 0 4px #fff}.menu-item[data-view-target=ir] span:first-child:before{width:15px;height:9px;border-radius:9px}.menu-item[data-view-target=lab] span:first-child:before{width:15px;height:15px;border-radius:50%;box-shadow:inset 0 0 0 3px #fff}.menu-item[data-view-target=bluetooth] span:first-child:before{width:15px;height:15px;border-radius:50%;border-width:2px;box-shadow:0 -5px 0 -3px currentColor,0 5px 0 -3px currentColor}.menu-item[data-view-target=mqtt] span:first-child:before{width:14px;height:14px;border-radius:50%;border-width:2px}.menu-item[data-view-target=wifi] span:first-child:before{width:15px;height:10px;border:0;border-top:2px solid currentColor;border-radius:50%}.menu-item[data-view-target=backup] span:first-child:before{width:15px;height:12px;border-radius:3px}.menu-item[data-view-target=system] span:first-child:before{width:13px;height:13px;border-radius:50%}.menu-item strong{display:block;font-size:13px;line-height:1.15;overflow-wrap:anywhere}.menu-item small{display:block;color:var(--muted);font-size:11px;margin-top:2px;line-height:1.15;overflow-wrap:anywhere}.menu-item.active{background:var(--soft2);color:#0c514d}.menu-item.active span:first-child{background:#fff;box-shadow:inset 0 0 0 1px rgba(15,118,110,.14)}.content{min-width:0;max-width:100%;display:grid;gap:18px}.section{display:none;scroll-margin-top:94px;min-width:0;max-width:100%}.section.active{display:grid;gap:15px}.section-head{display:flex;align-items:end;justify-content:space-between;gap:12px;padding:2px 0 4px}.section-lead{max-width:100%;color:var(--muted);font-size:13px;margin-top:5px;overflow-wrap:break-word}"
        "h1{font-size:20px;margin:0}h2{font-size:24px;line-height:1.12;margin:0;font-weight:750}h3{font-size:15px;margin:0 0 10px}.panel h2{font-size:18px;line-height:1.2;margin:0 0 6px}.muted{color:var(--muted)}.mini{font-size:12px}.nowrap{white-space:nowrap}.subtle{font-size:12px;color:var(--muted);margin-top:2px}.help{font-size:12px;color:var(--muted);margin-top:5px}.help,.muted,.subtle,.callout{overflow-wrap:anywhere}.callout{border:1px solid #dbe7ef;border-left:4px solid var(--accent2);background:#fbfdff;border-radius:8px;padding:12px 14px;margin:0 0 12px;color:#263a52}.callout strong{display:block;margin-bottom:2px}.quick-actions{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px;margin-top:14px}.quick-actions button{min-height:72px;text-align:left;background:#fff;color:var(--fg);border-color:var(--line);box-shadow:0 1px 2px rgba(20,40,32,.04);padding:14px}.quick-actions button strong{display:block;margin-bottom:4px}.quick-actions button:hover{border-color:#b9c8c4;background:#fbfefd}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px}.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px}.dashboard-cards{grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:12px}.panel,.stat,.setup-shell{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:16px;box-shadow:0 1px 2px rgba(20,40,32,.04);min-width:0;max-width:100%}.panel>*,.stat>*,.setup-shell>*{min-width:0;max-width:100%}.stat{min-height:92px;padding:17px}.stat .value{font-size:20px;font-weight:700;margin-top:5px}.stat .label{text-transform:uppercase;letter-spacing:0;color:var(--muted);font-size:11px}"
        ".kv{display:grid;grid-template-columns:132px 1fr;gap:6px 10px}.badge,.pill{display:inline-flex;align-items:center;border:1px solid var(--line);border-radius:999px;padding:3px 9px;background:var(--wash);font-size:12px}.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}"
        "label{display:block;margin:10px 0 4px;color:var(--muted);font-size:12px}input,select,textarea{width:100%;max-width:100%;min-width:0;padding:10px 11px;border:1px solid var(--line);border-radius:6px;background:#fff;color:var(--fg);min-height:38px}input:focus,select:focus,textarea:focus{outline:2px solid rgba(15,118,110,.16);border-color:var(--accent)}textarea{min-height:88px;resize:vertical}.textarea-tall{min-height:190px;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px}"
        "input[type=checkbox]{width:auto;min-height:0}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}"
        "button,a.button{border:1px solid var(--accent);background:var(--accent);color:#fff;border-radius:6px;padding:9px 12px;min-height:38px;cursor:pointer;font-weight:650;transition:border-color .12s,background .12s,box-shadow .12s,transform .08s;text-decoration:none;line-height:1.15}button:hover,a.button:hover{box-shadow:0 2px 8px rgba(15,118,110,.12)}button:active,a.button:active{transform:translateY(1px)}button:focus-visible,a.button:focus-visible,input:focus-visible,select:focus-visible,textarea:focus-visible{outline:2px solid rgba(15,118,110,.28);outline-offset:2px}a.button{display:inline-flex;align-items:center;justify-content:center}.secondary{background:#fff;color:var(--accent)}.danger{border-color:var(--bad);color:var(--bad);background:#fff}.ghost{background:var(--wash);border-color:var(--line);color:var(--fg)}"
        "pre{white-space:pre-wrap;word-break:break-word;margin:0;background:var(--soft);border-radius:6px;padding:10px;max-height:340px;overflow:auto}details{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:11px 13px}summary{cursor:pointer;font-weight:650}"
        ".msg{border-left:4px solid var(--accent2);padding:10px 12px;background:#f8fbff;border-radius:8px}.table{width:100%;border-collapse:collapse}.table th,.table td{border-top:1px solid var(--line);padding:7px;text-align:left;vertical-align:top}.device-list{display:grid;gap:14px}.command-list{display:grid;gap:8px}.command{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;border-top:1px solid var(--line);padding-top:10px}.command .command-editor{grid-column:1/-1;background:var(--wash)}.command-editor{margin-top:10px}.command-preview{max-height:110px;background:#fff;border:1px solid var(--line);margin-top:6px}.bt-saved{grid-column:1/-1}.bt-device-card{border-top:1px solid var(--line);padding-top:14px;margin-top:14px}.bt-device-card h4{margin:0 0 4px;font-size:15px}.save-script-box{border-top:1px solid var(--line);padding-top:10px;margin-top:4px}.export-list{display:flex;gap:8px;flex-wrap:wrap}.hidden{display:none!important}.preview{display:grid;gap:6px;max-height:260px;overflow:auto;border:1px solid var(--line);border-radius:6px;padding:8px;background:var(--soft)}.preview label{display:grid;grid-template-columns:auto 1fr;gap:8px;align-items:start;margin:0;color:var(--fg)}.match{width:100%;text-align:left;background:#fff;color:var(--fg);border-color:var(--line);padding:8px;white-space:normal;overflow-wrap:anywhere;word-break:break-word}.match strong{color:var(--accent2)}"
        ".ir-stored-layout{display:grid;grid-template-columns:minmax(230px,.34fr) minmax(0,1fr);gap:14px;align-items:start;margin-bottom:15px}.ir-stored-layout>*,.ir-control-layout>*,.ir-device-workspace>*{min-width:0;max-width:100%}.ir-device-picks{display:grid;gap:8px}.ir-device-pick{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;text-align:left;background:#fff;color:var(--fg);border-color:var(--line);padding:11px}.ir-device-pick:hover,.ir-device-pick.active{background:var(--soft2);border-color:#b9d8d3}.ir-device-pick strong{display:block}.ir-device-workspace{display:none;gap:14px;min-width:0;max-width:100%}.ir-device-workspace.active{display:grid}.ir-work-head{display:flex;align-items:start;justify-content:space-between;gap:12px}.ir-control-layout{display:grid;grid-template-columns:minmax(250px,360px) minmax(0,1fr);gap:16px;align-items:start}.ir-remote-card{display:grid;justify-items:center;gap:10px;width:100%;min-width:0;max-width:100%}.ir-remote-card>.help{justify-self:stretch;width:100%;max-width:100%;white-space:normal}.ir-remote-shell{width:100%;display:grid;place-items:center;background:linear-gradient(180deg,#f7faf9,#edf3f1);border:1px solid var(--line);border-radius:8px;padding:10px}.ir-remote-skin{position:relative;width:min(100%,315px);aspect-ratio:591/1280}.ir-remote-skin img{display:block;width:100%;height:100%;object-fit:contain;border-radius:12px;box-shadow:0 14px 34px rgba(10,18,16,.18)}.remote-hotspot{position:absolute;min-height:0;padding:0;border-radius:999px;border:1px solid rgba(15,118,110,.55);background:rgba(15,118,110,.12);color:transparent;box-shadow:0 0 0 1px rgba(255,255,255,.18) inset;touch-action:manipulation;margin:0}.remote-hotspot button{width:100%;height:100%;min-height:0;padding:0;border:0;background:transparent;color:transparent;box-shadow:none;border-radius:inherit}.remote-hotspot button:hover{box-shadow:none}.remote-hotspot:hover,.remote-hotspot.sending{background:rgba(15,118,110,.28);border-color:#00a899;box-shadow:0 0 0 2px rgba(255,255,255,.5),0 8px 18px rgba(0,0,0,.2)}.remote-hotspot:focus-within{outline:2px solid #fff;outline-offset:2px}.remote-hotspot.disabled{background:transparent;border-color:transparent;box-shadow:none;cursor:default;pointer-events:none}.ir-quick-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(128px,1fr));gap:8px}.ir-remote-card .ir-quick-grid{width:100%}.ir-quick-grid .ir-send-form button{width:100%;background:#fff;color:var(--accent);border-color:var(--line);text-align:left;min-height:42px}.ir-quick-grid .ir-send-form button:hover,.ir-quick-grid .ir-send-form button.sending{background:var(--soft2);border-color:var(--accent)}.ir-unmapped{width:100%;border-top:1px solid var(--line);padding-top:10px}.ir-command-tools{display:grid;gap:10px}.ir-command-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:center;border-top:1px solid var(--line);padding-top:10px}.ir-command-row form{margin:0}.ir-status{min-height:20px}.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}"
        ".setup-shell{padding:0;overflow:hidden}.wizard-top{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:16px 18px;border-bottom:1px solid var(--line);background:#fff}.wizard-top h3{margin:0}.wizard-grid{display:grid;grid-template-columns:210px 1fr;min-height:420px}.stepper{border-right:1px solid var(--line);background:#f8fbfa;padding:12px;display:grid;align-content:start;gap:6px}.step{display:grid;grid-template-columns:28px 1fr;gap:9px;align-items:center;width:100%;text-align:left;background:transparent;color:var(--fg);border-color:transparent;padding:10px}.step span{width:26px;height:26px;border-radius:999px;display:grid;place-items:center;background:#fff;border:1px solid var(--line);color:var(--accent);font-weight:750}.step.active{background:#fff;border-color:var(--line);box-shadow:0 1px 2px rgba(20,40,32,.04)}.wizard-body{padding:18px;min-width:0;max-width:100%}.wizard-panel{display:none;min-width:0;max-width:100%}.wizard-panel.active{display:block}.wizard-status{min-height:20px;margin-top:10px;color:var(--muted)}.device-sync{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:end}.guide-steps{display:grid;gap:8px;margin:10px 0 12px}.guide-step{display:grid;grid-template-columns:28px 1fr;gap:10px;align-items:start;border:1px solid var(--line);background:var(--wash);border-radius:8px;padding:9px 10px}.guide-step>*{min-width:0}.guide-step b{width:22px;height:22px;border-radius:999px;background:var(--soft2);color:var(--accent);display:grid;place-items:center;font-size:12px}.lab-layout,.bt-layout{display:grid;grid-template-columns:minmax(0,1.05fr) minmax(320px,.95fr);gap:14px;min-width:0}.lab-layout>*,.bt-layout>*{min-width:0}.bt-script-layout{display:grid;grid-template-columns:1fr;gap:12px;min-width:0}.bt-script-tools{display:grid;gap:8px;align-content:start;min-width:0}.lab-toolbar{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}.lab-quick{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:8px;margin:10px 0 12px}.lab-quick button{text-align:left;min-height:50px;background:#fff;color:var(--accent);border-color:var(--accent)}.lab-quick button:hover{background:#f8fbfa;border-color:#0b625c}.lab-quick button .queue-meta{color:var(--muted);font-weight:600}.lab-presets,.queue-tools{display:flex;gap:7px;flex-wrap:wrap;margin-top:10px}.lab-presets button,.queue-tools button{padding:6px 9px;font-size:12px}.lab-advanced{margin-top:12px}.inline-check{display:inline-flex;align-items:center;gap:8px;margin-top:10px}.lab-summary{display:flex;justify-content:space-between;gap:10px;align-items:center;border:1px solid var(--line);border-radius:8px;background:var(--wash);padding:9px 10px;margin:8px 0;color:var(--muted);font-size:12px}.queue-list{display:grid;gap:7px;max-height:390px;overflow:auto;border:1px solid var(--line);border-radius:8px;background:var(--soft);padding:8px}.queue-row{display:grid;grid-template-columns:auto 1fr auto;gap:9px;align-items:start;background:#fff;border:1px solid var(--line);border-radius:7px;padding:8px}.queue-row strong{display:block}.queue-meta{color:var(--muted);font-size:11px;overflow-wrap:anywhere}.meter{height:8px;border-radius:999px;background:#e7eeec;overflow:hidden}.meter span{display:block;height:100%;width:0;background:var(--accent)}button:disabled{opacity:.55;cursor:not-allowed;transform:none}.kb-panel{width:100%;max-width:100%;min-width:0;margin-top:10px;overflow-x:auto;overflow-y:hidden;padding-bottom:6px;-webkit-overflow-scrolling:touch}.kb-row{display:flex;gap:3px;margin-bottom:3px;justify-content:flex-start;min-width:max-content}.kb-key{min-width:36px;height:38px;padding:0 6px;border:1px solid var(--line);border-radius:5px;background:#fff;cursor:pointer;font-size:12px;font-family:inherit;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0;transition:background .06s,color .06s;color:var(--fg)}.kb-key:hover{background:var(--soft);border-color:var(--accent)}.kb-key.kb-on{background:var(--accent);color:#fff;border-color:var(--accent)}.kb-15{min-width:54px}.kb-2{min-width:72px}.kb-225{min-width:82px}.kb-25{min-width:90px}.kb-275{min-width:100px}.kb-sp{flex:1;min-width:180px;max-width:360px}.kb-fwd{margin-bottom:8px}"
        "@media(max-width:980px){.ir-stored-layout,.ir-control-layout{grid-template-columns:minmax(0,1fr)}.ir-remote-skin{width:min(100%,250px)}}"
        "@media(max-width:860px){header{padding:12px 14px}.topbar{max-width:none;width:100%}.app-shell{width:100%;max-width:100%;grid-template-columns:minmax(0,1fr);padding:14px;gap:16px}.side-menu{position:sticky;top:62px;z-index:2;display:flex;max-width:100%;overflow-x:auto;gap:6px;border-radius:10px;box-shadow:0 4px 16px rgba(25,41,37,.06);scrollbar-width:thin}.menu-item{min-width:168px}.row,.wizard-grid,.device-sync,.lab-layout,.bt-layout,.bt-script-layout{grid-template-columns:minmax(0,1fr)}.kv{grid-template-columns:1fr}.command,.ir-command-row{grid-template-columns:1fr}.stepper{border-right:0;border-bottom:1px solid var(--line);grid-template-columns:repeat(2,1fr)}}"
        "@media(max-width:520px){body{font-size:13px}header{position:static;padding:10px}.topbar{align-items:flex-start;flex-direction:column;gap:8px}.brand-mark{width:30px;height:30px}.brand h1{font-size:16px}.top-status{justify-content:flex-start}.app-shell{padding:10px;gap:14px}.side-menu{position:static;display:grid;grid-template-columns:minmax(0,1fr);gap:7px;padding:7px}.menu-item{min-width:0;min-height:46px;padding:8px;grid-template-columns:28px 1fr}.menu-item span:first-child{width:24px;height:24px}.menu-item strong{font-size:12px}.menu-item small{font-size:10px}.section-head,.ir-work-head{align-items:flex-start;flex-direction:column}.panel,.stat,.wizard-body{padding:14px}.grid,.cards,.quick-actions,.lab-toolbar,.lab-quick{grid-template-columns:minmax(0,1fr)}.guide-step{grid-template-columns:24px 1fr;padding:8px}.actions button,.actions a.button{width:100%}.ir-quick-grid{grid-template-columns:minmax(0,1fr) minmax(0,1fr)}.ir-remote-shell{padding:8px}.ir-remote-skin{width:min(100%,230px)}.kb-panel{margin-left:-2px;margin-right:-2px}.kb-key{min-width:32px;height:36px;font-size:11px}.kb-15{min-width:48px}.kb-2{min-width:64px}.kb-225{min-width:74px}.kb-sp{min-width:150px}}"
        "@media(max-width:420px){.side-menu{grid-template-columns:minmax(0,1fr)}.menu-item{min-height:46px}}"
        "</style></head><body>",
        f);
    fprintf(f,
        "<header><div class='topbar'><div class='brand'><div class='brand-mark'>H</div><div><h1>Harmony Hub Control</h1><small>Local smart home console</small></div></div><div class='top-status'><span class='pill'>Local control</span><span class='pill %s'>Logitech cloud %s</span></div></div></header><main class='app-shell'><aside class='side-menu' aria-label='Main menu'><button type='button' class='menu-item active' data-view-target='overview'><span>D</span><div><strong>Dashboard</strong><small>Status</small></div></button><button type='button' class='menu-item' data-view-target='control'><span>R</span><div><strong>Control</strong><small>Send buttons</small></div></button><button type='button' class='menu-item' data-view-target='ir'><span>IR</span><div><strong>IR Setup</strong><small>Add remotes</small></div></button><button type='button' class='menu-item' data-view-target='lab'><span>L</span><div><strong>Bulk IR Test</strong><small>Queue IR codes</small></div></button><button type='button' class='menu-item' data-view-target='bluetooth'><span>BT</span><div><strong>Bluetooth</strong><small>Keyboard</small></div></button><button type='button' class='menu-item' data-view-target='mqtt'><span>M</span><div><strong>MQTT</strong><small>Home Assistant</small></div></button><button type='button' class='menu-item' data-view-target='wifi'><span>W</span><div><strong>Wi-Fi</strong><small>Network</small></div></button><button type='button' class='menu-item' data-view-target='backup'><span>B</span><div><strong>Backup</strong><small>Import/export</small></div></button><button type='button' class='menu-item' data-view-target='system'><span>S</span><div><strong>System</strong><small>Logs/update</small></div></button></aside><div class='content'>",
        cloud_blocked ? "ok" : "warn",
        cloud_blocked ? "blocked" : "allowed");
}

static void page_end(FILE *f) {
    fputs("<script>const REMOTE_SKIN_SRC='data:image/jpeg;base64,", f);
    fputs(REMOTE_SKIN_JPG_B64, f);
    fputs("';", f);
    fputs(
        "const $=id=>document.getElementById(id);"
        "if('scrollRestoration' in history)history.scrollRestoration='manual';"
        "document.querySelectorAll('[data-remote-skin]').forEach(img=>{img.src=REMOTE_SKIN_SRC;});"
        "function showView(name){let panel='';if(name&&name.startsWith('ir-')){panel=name.slice(3);name='ir';}if(!name)name='overview';let found=false;document.querySelectorAll('[data-view]').forEach(s=>{const on=s.dataset.view===name;s.classList.toggle('active',on);if(on)found=true;});if(!found&&name!=='overview'){showView('overview');return;}document.querySelectorAll('[data-view-target]').forEach(b=>b.classList.toggle('active',b.dataset.viewTarget===name));if(location.hash!=='#'+name)history.replaceState(null,'','#'+name);if(name==='ir'&&panel)setTimeout(()=>showWizardPanel(panel),0);window.scrollTo(0,0);setTimeout(()=>window.scrollTo(0,0),0);}"
        "document.querySelectorAll('[data-view-target]').forEach(b=>b.addEventListener('click',()=>showView(b.dataset.viewTarget)));"
        "showView((location.hash||'#overview').slice(1));"
        "const importFile=$('importFile');if(importFile){importFile.addEventListener('change',()=>{const file=importFile.files&&importFile.files[0];if(!file)return;const reader=new FileReader();reader.onload=()=>{const box=document.querySelector('textarea[name=payload]');if(box)box.value=reader.result||''};reader.readAsText(file);});}"
        "const cap=$('captureNow');if(cap){cap.addEventListener('click',async()=>{const s=$('captureStatus'),raw=$('captureRaw');try{s.textContent='capturing...';const r=await fetch('/api/capture',{method:'POST'});const j=await r.json();raw.value=j.raw||'';s.textContent=j.raw?'capture received':'no capture payload';}catch(e){s.textContent='capture failed';}});}"
        "let wizardInventory=null;"
        "function plainText(html){return String(html||'').replace(/<script[\\s\\S]*?<\\/script>/gi,'').replace(/<style[\\s\\S]*?<\\/style>/gi,'').replace(/<[^>]+>/g,' ').replace(/\\s+/g,' ').trim();}"
        "function escHtml(s){return String(s||'').replace(/[&<>\"']/g,c=>c==='&'?'&amp;':c==='<'?'&lt;':c==='>'?'&gt;':c==='\"'?'&quot;':'&#39;');}"
        "function wizStatus(id,t){const el=$(id);if(el)el.textContent=t||'';}"
        "function showWizardPanel(name){document.querySelectorAll('.wizard-panel').forEach(p=>p.classList.toggle('active',p.dataset.panel===name));document.querySelectorAll('.step').forEach(b=>b.classList.toggle('active',b.dataset.stepTarget===name));if(name==='library')syncProfileToSearch(false);}"
        "document.querySelectorAll('[data-step-target]').forEach(b=>b.addEventListener('click',()=>showWizardPanel(b.dataset.stepTarget)));"
        "function framePost(path,data){return new Promise((resolve,reject)=>{const frame=document.createElement('iframe'),form=document.createElement('form'),name='wizFrame'+Date.now();let done=false,submitted=false;frame.name=name;frame.style.display='none';form.method='post';form.action=path;form.target=name;form.style.display='none';Object.keys(data||{}).forEach(k=>{const i=document.createElement('input');i.type='hidden';i.name=k;i.value=data[k]||'';form.append(i);});function finish(ok,text){if(done)return;done=true;clearTimeout(timer);setTimeout(()=>{frame.remove();form.remove();},200);resolve({ok:ok,text:text||''});}const timer=setTimeout(()=>finish(false,'timeout'),30000);frame.onload=()=>{let text='',href='';try{href=frame.contentWindow.location.href||'';if(!submitted||href==='about:blank')return;const d=frame.contentDocument;text=(d.body&&(d.body.innerText||d.body.textContent))||d.documentElement.textContent||'';}catch(e){text='';}finish(true,text);};document.body.append(frame,form);submitted=true;form.submit();});}"
        "async function postForm(path,data){return await framePost(path,data);}"
        "function keyProtocol(k){return /MemorexO1/i.test(k||'')?'679':'2';}"
        "function extractKeycode(t){const m=String(t||'').match(/G:[^\\r\\n\"'<>]+?:\\d+/);return m?m[0].replace(/,$/,'').trim():'';}"
        "function extractNecHex(t){t=String(t||'').trim();let m=t.match(/^0x([0-9a-f]{1,8})$/i)||t.match(/^([0-9a-f]{1,8})$/i);if(m)return m[1].toUpperCase();m=t.match(/0x([0-9a-f]{8})(?![0-9a-f])/i);if(m)return m[1].toUpperCase();if(/nec|samsung|toshiba|memorex|protocol|keycode/i.test(t)){m=t.match(/(^|[^0-9a-f])([0-9a-f]{8})(?![0-9a-f])/i);if(m)return m[2].toUpperCase();}return '';}"
        "function captureLooksEmpty(t){return !String(t||'').trim()||/no payload|returned no|timeout/i.test(t);}"
        "function applyCaptureAnalysis(j,text){text=String(text||j?.raw||'').trim();if(captureLooksEmpty(text))return false;const mode=$('wizardMode'),proto=$('wizardProtocol'),raw=$('wizardRaw'),key=$('wizardKeycode'),nec=$('wizardNec');if(raw)raw.value=text;let bestMode=j?.mode||'',bestKey=j?.keycode||'',bestNec=j?.nec||'',note=j?.analysis||'';if(!bestKey)bestKey=extractKeycode(text);if(!bestNec)bestNec=extractNecHex(text);if(bestKey){if(key)key.value=bestKey;if(proto)proto.value=String(j?.protocolId||keyProtocol(bestKey));if(mode)mode.value='keycode';wizStatus('wizardLearnStatus',note||'decoded a compact Harmony code');return true;}if(bestNec){if(nec)nec.value=bestNec;if(key)key.value='';if(proto)proto.value=String(j?.protocolId||keyProtocol(text));if(mode)mode.value='nec';wizStatus('wizardLearnStatus',note||'decoded an NEC-style code');return true;}if(mode)mode.value='raw';wizStatus('wizardLearnStatus',note||'captured timing data; storing as raw replay');return true;}"
        "async function loadWizardInventory(){try{const r=await fetch('/api/inventory');wizardInventory=await r.json();}catch(e){wizardInventory=null;}populateVerifyCommands();}"
        "function selectedDeviceId(id){const el=$(id);return el?el.value:'';}"
        "function currentCommandsFor(id){const dev=(wizardInventory&&wizardInventory.devices||[]).find(d=>d.id===id);return dev&&dev.commands?dev.commands:[];}"
        "async function loadDeviceCommands(id){if(!id)return[];try{const j=await(await fetch('/api/device-commands?deviceId='+encodeURIComponent(id))).json();if(j.ok&&Array.isArray(j.commands))return j.commands;}catch(e){}return currentCommandsFor(id);}"
        "function populateVerifyCommands(){const dev=selectedDeviceId('verifyDevice')||selectedDeviceId('wizardDevice'),sel=$('verifyCommand');if(!sel)return;sel.replaceChildren();currentCommandsFor(dev).forEach(c=>{const o=document.createElement('option');o.value=c.name;o.textContent=c.name;sel.append(o);});const typed=($('wizardCommandName')?.value||'').trim();if(typed&&!Array.from(sel.options).some(o=>o.value===typed)){const o=document.createElement('option');o.value=typed;o.textContent=typed;sel.prepend(o);sel.value=typed;}}"
        "function syncWizardDevice(from,to){const a=$(from),b=$(to);if(a&&b)b.value=a.value;populateVerifyCommands();}"
        "function profileData(){return{name:($('newDeviceName')?.value||'').trim(),type:($('newDeviceType')?.value||'').trim(),manufacturer:($('newDeviceManufacturer')?.value||'').trim(),model:($('newDeviceModel')?.value||'').trim()};}"
        "function profileQuery(){const d=profileData();return(d.manufacturer&&d.model)?d.manufacturer+' '+d.model:(d.name||d.model||d.manufacturer);}"
        "function syncProfileToSearch(force){const q=profileQuery(),s=$('irdbSearch'),pre=$('irdbPrefix');if(s&&q&&(force||!s.value.trim()))s.value=q;if(pre&&!pre.value.trim())pre.value='';}"
        "function findProfileDeviceId(d){const list=(wizardInventory&&wizardInventory.devices)||[],same=x=>String(x||'').trim().toLowerCase();let dev=list.find(x=>same(x.name)===same(d.name)&&same(x.manufacturer)===same(d.manufacturer)&&same(x.model)===same(d.model));if(!dev)dev=list.find(x=>same(x.manufacturer)===same(d.manufacturer)&&same(x.model)===same(d.model));return dev&&dev.id?dev.id:'';}"
        "function selectDeviceEverywhere(id,name){if(!id)return;['wizardDevice','verifyDevice','irdbDevice','labDevice'].forEach(selId=>{const s=$(selId);if(!s)return;if(!Array.from(s.options).some(o=>o.value===id)){const o=document.createElement('option');o.value=id;o.textContent=(name||'Device')+' ('+id+')';s.append(o);}s.value=id;});populateVerifyCommands();}"
        "function openIrSetup(panel,id){if(id)selectDeviceEverywhere(id);showView('ir-'+(panel||'device'));setTimeout(()=>{if(id)selectDeviceEverywhere(id);document.querySelector('.setup-shell')?.scrollIntoView({behavior:'smooth',block:'start'});},0);}"
        "document.querySelectorAll('[data-next-step]').forEach(b=>b.addEventListener('click',()=>{const p=b.closest('[data-ir-device]');openIrSetup(b.dataset.nextStep,p&&p.dataset.irDevice);}));"
        "const wizDevice=$('wizardDevice');if(wizDevice)wizDevice.addEventListener('change',()=>{syncWizardDevice('wizardDevice','verifyDevice');const imp=$('irdbDevice');if(imp)imp.value=wizDevice.value;});"
        "const verDevice=$('verifyDevice');if(verDevice)verDevice.addEventListener('change',populateVerifyCommands);"
        "const wizName=$('wizardCommandName');if(wizName)wizName.addEventListener('input',populateVerifyCommands);"
        "const wizNec=$('wizardNec');if(wizNec)wizNec.addEventListener('input',()=>{if(wizNec.value.trim()&&$('wizardMode'))$('wizardMode').value='nec';});"
        "const wizKey=$('wizardKeycode');if(wizKey)wizKey.addEventListener('input',()=>{if(wizKey.value.trim()&&$('wizardMode'))$('wizardMode').value='keycode';});"
        "const wizRaw=$('wizardRaw');if(wizRaw)wizRaw.addEventListener('input',()=>{const m=$('wizardMode');if(wizRaw.value.trim()&&m&&m.value!=='auto')m.value='raw';});"
        "const wizCap=$('wizardCapture');if(wizCap){wizCap.addEventListener('click',async()=>{try{wizStatus('wizardLearnStatus','listening...');const r=await postForm('/api/capture',{});let txt=(r.text||'').trim(),j={};try{j=JSON.parse(txt);}catch(e){j={raw:txt};}if(!applyCaptureAnalysis(j,j.raw||txt))wizStatus('wizardLearnStatus','no signal received');}catch(e){wizStatus('wizardLearnStatus','capture failed');}});}"
        "function learnFormData(){return{deviceId:selectedDeviceId('wizardDevice'),name:($('wizardCommandName')?.value||'').trim(),mode:$('wizardMode')?.value||'raw',protocol:$('wizardProtocol')?.value||'2',nec:$('wizardNec')?.value||'',keycode:$('wizardKeycode')?.value||'',raw:$('wizardRaw')?.value||''};}"
        "function learnHasSignal(d){return!!((d.keycode||'').trim()||(d.nec||'').trim()||(d.raw||'').trim());}"
        "const wizLearnTest=$('wizardLearnTest');if(wizLearnTest){wizLearnTest.addEventListener('click',async()=>{const data=learnFormData();if(!data.deviceId){wizStatus('wizardLearnStatus','choose a device before testing');return;}if(!learnHasSignal(data)){wizStatus('wizardLearnStatus','learn or enter a signal before testing');return;}try{wizStatus('wizardLearnStatus','testing learned signal...');const j=await postJson('/api/ir-test-learned',data);const tail=String(j.reply||'').trim();wizStatus('wizardLearnStatus','test sent'+(tail?': '+tail.slice(0,180):''));await loadWizardInventory();}catch(e){wizStatus('wizardLearnStatus','test failed: '+(e.message||e));}});}"
        "const wizForm=$('wizardLearnForm');if(wizForm){wizForm.addEventListener('submit',async e=>{e.preventDefault();const data=learnFormData();try{wizStatus('wizardLearnStatus','saving...');const res=await postForm('/ir/command',data);const msg=plainText(res.text);wizStatus('wizardLearnStatus',msg.includes('Saved command')?'command saved. You can learn another signal or open Verify.':(msg||'save request complete'));await loadWizardInventory();const vd=$('verifyDevice');if(vd)vd.value=data.deviceId;populateVerifyCommands();}catch(err){wizStatus('wizardLearnStatus','save failed');}});}"
        "const wizTest=$('wizardTest');if(wizTest){wizTest.addEventListener('click',async()=>{const data={deviceId:selectedDeviceId('verifyDevice')||selectedDeviceId('wizardDevice'),command:$('verifyCommand')?.value||($('wizardCommandName')?.value||'')};if(!data.deviceId||!data.command){wizStatus('wizardVerifyStatus','choose a command');return;}try{wizStatus('wizardVerifyStatus','sending...');const res=await postForm('/ir/send',data);const msg=plainText(res.text);wizStatus('wizardVerifyStatus',msg||'test sent');}catch(e){wizStatus('wizardVerifyStatus','test failed');}});}"
        "loadWizardInventory();"
        "function irStatus(deviceId,msg){document.querySelectorAll('[data-ir-status]').forEach(el=>{if(el.dataset.irStatus===deviceId)el.textContent=msg;});}"
        "function showIrStoredDevice(id){if(!id)return;document.querySelectorAll('[data-ir-device-pick]').forEach(b=>b.classList.toggle('active',b.dataset.irDevicePick===id));document.querySelectorAll('[data-ir-device]').forEach(p=>p.classList.toggle('active',p.dataset.irDevice===id));selectDeviceEverywhere(id);irStatus(id,'Ready.');}"
        "document.querySelectorAll('[data-ir-device-pick]').forEach(b=>b.addEventListener('click',()=>showIrStoredDevice(b.dataset.irDevicePick)));"
        "function irFormData(form){const data={};new FormData(form).forEach((v,k)=>data[k]=v);return data;}"
        "document.querySelectorAll('.ir-send-form').forEach(form=>form.addEventListener('submit',async e=>{e.preventDefault();const data=irFormData(form),btn=form.querySelector('button')||form;if(!data.deviceId||!data.command)return;try{btn.classList.add('sending');form.classList.add('sending');irStatus(data.deviceId,'Sending '+data.command+'...');const j=await postJson('/api/ir-send',data);irStatus(data.deviceId,'Sent '+data.command+(j.reply?': '+String(j.reply).slice(0,140):''));}catch(err){irStatus(data.deviceId,'Send failed: '+(err.message||err));}finally{setTimeout(()=>{btn.classList.remove('sending');form.classList.remove('sending');},220);}}));"
        "document.querySelectorAll('.ir-command-filter').forEach(input=>input.addEventListener('input',()=>{const id=input.dataset.commandFilter,q=normText(input.value||'');document.querySelectorAll('[data-command-list=\"'+id+'\"]').forEach(list=>Array.from(list.children).forEach(row=>{const t=normText(row.textContent||row.dataset.commandName||'');row.classList.toggle('hidden',!!q&&!t.includes(q));}));}));"
        "const IRDB_BASE='https://cdn.jsdelivr.net/gh/probonopd/irdb@master/codes/';"
        "const FLIPPER_BASE='https://cdn.jsdelivr.net/gh/Lucaslhm/Flipper-IRDB@main/';"
        "const FLIPPER_INDEX='https://api.github.com/repos/Lucaslhm/Flipper-IRDB/git/trees/main?recursive=1';"
        "const LIRC_BASE='https://raw.githubusercontent.com/probonopd/lirc-remotes/master/';const LIRC_INDEX='https://api.github.com/repos/probonopd/lirc-remotes/git/trees/master?recursive=1';"
        "const SMARTIR_BASE='https://raw.githubusercontent.com/smartHomeHub/SmartIR/master/';const SMARTIR_INDEX='https://api.github.com/repos/smartHomeHub/SmartIR/git/trees/master?recursive=1';let irdbIndex=[],irdbCache={irdb:null,flipper:null,lirc:null,smartir:null};"
        "function rev8(v){v=((v&240)>>4)|((v&15)<<4);v=((v&204)>>2)|((v&51)<<2);v=((v&170)>>1)|((v&85)<<1);return v&255;}"
        "function keyFromParts(proto,d,s,f){if(!/^(NEC|Samsung32|Pioneer)/i.test(proto))return '';d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?(d^255):Number(s);f=Number(f);if([d,s,f].some(x=>!Number.isFinite(x)||x<0||x>255))return '';if(/^Samsung32/i.test(proto))s=d;const val=((rev8(d)<<24)|(rev8(s)<<16)|(rev8(f)<<8)|rev8((~f)&255))>>>0;return 'G:Toshiba 32 Bit:(0x'+val.toString(16).toUpperCase().padStart(8,'0')+')(Repeat)():3';}"
        "function csvCells(line){const out=[];let cur='',q=false;for(let i=0;i<line.length;i++){const ch=line[i];if(ch==='\"'){if(q&&line[i+1]==='\"'){cur+='\"';i++;}else q=!q;}else if(ch===','&&!q){out.push(cur);cur='';}else cur+=ch;}out.push(cur);return out.map(x=>x.trim());}"
        "function csvEntries(t){return t.replace(/\\r/g,'').split('\\n').map(x=>x.trim()).filter(Boolean).map(line=>{const p=csvCells(line),proto=p[1]||'',key=p[0]==='functionname'?'':keyFromParts(proto,p[2]||'',p[3]||'',p[4]||''),raw=p[0]==='functionname'?'':csvProtocolRaw(proto,p[2]||'',p[3]||'',p[4]||'',p[0]||'');return{name:p[0]||'',meta:proto+' '+(p[2]||'')+','+(p[3]||'')+','+(p[4]||''),protocol:proto,keycode:key,raw:raw};}).filter(r=>r.name&&r.name!=='functionname');}"
        "function hexBytes(v){return String(v||'').trim().split(/\\s+/).filter(Boolean).map(x=>parseInt(x,16)||0);}"
        "function hexValue(v){return hexBytes(v).slice(0,4).reduce((a,b,i)=>a|((b&255)<<(8*i)),0)>>>0;}"
        "function harmonyRawFromTimings(freq,vals){freq=Math.max(10000,Math.min(60000,Math.round(Number(freq)||38000)));vals=(vals||[]).map(v=>Math.max(1,Math.min(0xfffff,Math.round(Math.abs(Number(v)||0))))).filter(Boolean);if(vals.length===3)vals.push(100000);if(vals.length<4)return'';let raw='F'+freq.toString(16).toUpperCase();vals.forEach((v,i)=>{raw+=(i%2?'S':'P')+v.toString(16).toUpperCase();});return raw.length<=4096?raw:'';}"
        "function pulse(seq,level,dur){dur=Math.round(dur);if(dur<=0)return;const last=seq[seq.length-1];if(last&&last.level===level)last.dur+=dur;else seq.push({level:level,dur:dur});}"
        "function seqRaw(freq,seq){if(!seq.length)return'';if(seq[0].level===0)seq.unshift({level:1,dur:1});return harmonyRawFromTimings(freq,seq.map(x=>x.dur));}"
        "function manchester(seq,bits,half,doubleIndex){bits.forEach((bit,i)=>{const h=i===doubleIndex?half*2:half;if(bit){pulse(seq,1,h);pulse(seq,0,h);}else{pulse(seq,0,h);pulse(seq,1,h);}});}"
        "function msbBits(v,n){const a=[];for(let i=n-1;i>=0;i--)a.push((v>>i)&1);return a;}"
        "function lsbBits(v,n){const a=[];for(let i=0;i<n;i++)a.push((v>>i)&1);return a;}"
        "function pop8(v){v&=255;let n=0;while(v){n+=v&1;v>>=1;}return n;}"
        "function rc5Raw(cur){const addr=hexValue(cur.address)&31,cmd=hexValue(cur.command)&127,tog=hexValue(cur.toggle)&1,seq=[];const bits=[1,cmd<64?1:0,tog].concat(msbBits(addr,5),msbBits(cmd&63,6));manchester(seq,bits,889,-1);return seqRaw(36000,seq);}"
        "function rc6Raw(cur){const addr=hexValue(cur.address)&255,cmd=hexValue(cur.command)&255,tog=hexValue(cur.toggle)&1,seq=[];pulse(seq,1,2666);pulse(seq,0,889);const bits=[1,0,0,0,tog].concat(msbBits(addr,8),msbBits(cmd,8));manchester(seq,bits,444,4);return seqRaw(36000,seq);}"
        "function mceRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?15:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>127||s<0||s>255||f<0||f>255)return'';const seq=[],bits=[1,1,1,0,0].concat(msbBits(128,8),msbBits(s,8),[0],msbBits(d,7),msbBits(f,8));pulse(seq,1,2664);pulse(seq,0,888);manchester(seq,bits,444,4);pulse(seq,0,100000);return seqRaw(36000,seq);}"
        "function recs80Raw(d,s,f,name=''){d=Number(d);f=Number(f);const t=/\\bT1\\b/i.test(String(name||''))?1:0;if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>7||f<0||f>63)return'';const seq=[];pulse(seq,1,158);pulse(seq,0,7432);[t].concat(msbBits(d,3),msbBits(f,6)).forEach(b=>{pulse(seq,1,158);pulse(seq,0,b?7432:4902);});pulse(seq,1,158);pulse(seq,0,45000);return seqRaw(38000,seq);}"
        "function akaiRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>7||f<0||f>127)return'';const seq=[];lsbBits(d,3).concat(lsbBits(f,7),[1]).forEach(b=>{pulse(seq,1,289);pulse(seq,0,Math.round((b?6.3:2.6)*289));});pulse(seq,0,25300);return seqRaw(38000,seq);}"
        "function denonRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>31||f<0||f>255)return'';const seq=[],u=264;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*7:u*3);}function half(func,suffix){lsbBits(d,5).concat(lsbBits(func,8),lsbBits(suffix,2)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*165);}half(f,0);half((~f)&255,3);return seqRaw(38000,seq);}"
        "function denonKRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>15||s<0||s>15||f<0||f>4095)return'';const seq=[],u=432,c=((d<<4)^s^((f<<4)&255)^((f>>4)&255))&255;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*3:u);}pulse(seq,1,u*8);pulse(seq,0,u*4);lsbBits(84,8).concat(lsbBits(50,8),lsbBits(0,4),lsbBits(d,4),lsbBits(s,4),lsbBits(f,12),lsbBits(c,8)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*173);return seqRaw(37000,seq);}"
        "function mitsubishiRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>255||f<0||f>255)return'';const seq=[],u=300;lsbBits(d,8).concat(lsbBits(f,8)).forEach(b=>{pulse(seq,1,u);pulse(seq,0,b?u*7:u*3);});pulse(seq,1,u);pulse(seq,0,u*80);return seqRaw(32600,seq);}"
        "function vellemanRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>7||f<0||f>63)return'';const seq=[];[1,0].concat(msbBits(d,3),msbBits(f,6),[1]).forEach(b=>{pulse(seq,1,700);pulse(seq,0,b?7590:5060);});pulse(seq,0,55000);return seqRaw(38000,seq);}"
        "function fujitsuRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>255||s<0||s>255||f<0||f>255)return'';const seq=[],u=432;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*3:u);}pulse(seq,1,u*8);pulse(seq,0,u*4);lsbBits(20,8).concat(lsbBits(99,8),lsbBits(0,4),lsbBits(0,4),lsbBits(d,8),lsbBits(s,8),lsbBits(f,8)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*110);return seqRaw(37000,seq);}"
        "function sharpRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>31||f<0||f>255)return'';const seq=[],u=264;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*7:u*3);}function half(func,suffix){lsbBits(d,5).concat(lsbBits(func,8),lsbBits(suffix,2)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*165);}half(f,1);half((~f)&255,2);return seqRaw(38000,seq);}"
        "function directvRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>15||f<0||f>255)return'';const c=(7*((f>>6)&3)+5*((f>>4)&3)+3*((f>>2)&3)+(f&3))&15,seq=[],u=600,bits=msbBits(d,4).concat(msbBits(f,8),msbBits(c,4));pulse(seq,1,u*10);pulse(seq,0,u*2);for(let i=0;i<bits.length;i+=2){const v=(bits[i]<<1)|bits[i+1];pulse(seq,1,(v&2)?u*2:u);pulse(seq,0,(v&1)?u*2:u);}pulse(seq,1,u);pulse(seq,0,u*50);return seqRaw(38000,seq);}"
        "function gxbRaw(d,s,f){d=Number(d);f=Number(f);const ss=String(s===undefined?'':s).trim();const p=(ss&&ss!=='-1')?(Number(ss)&1):0;if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>15||f<0||f>255)return'';const seq=[],u=520;pulse(seq,1,u);pulse(seq,0,u);msbBits(d,4).concat(msbBits(f,8),[p]).forEach(b=>{pulse(seq,1,b?u*3:u);pulse(seq,0,b?u:u*3);});pulse(seq,1,u);pulse(seq,0,60000);return seqRaw(38300,seq);}"
        "function giCableRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>15||f<0||f>255)return'';const seq=[],u=490,c=(-(d+(f&15)+((f>>4)&15)))&15;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*9:u*4.5);}pulse(seq,1,u*18);pulse(seq,0,u*9);lsbBits(f,8).concat(lsbBits(d,4),lsbBits(c,4)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*84);pulse(seq,1,u*18);pulse(seq,0,u*4.5);pulse(seq,1,u);pulse(seq,0,u*178);return seqRaw(38700,seq);}"
        "function protonRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>255||f<0||f>255)return'';const seq=[],u=500;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*3:u);}pulse(seq,1,u*16);pulse(seq,0,u*8);lsbBits(d,8).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*8);lsbBits(f,8).forEach(bit);pulse(seq,1,u);pulse(seq,0,63000);return seqRaw(38000,seq);}"
        "function f12Raw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>7||s<0||s>1||f<0||f>255)return'';const seq=[],u=422,bits=lsbBits(d,3).concat([s&1],lsbBits(f,8));function bit(b){pulse(seq,1,b?u*3:u);pulse(seq,0,b?u:u*3);}function part(){bits.forEach(bit);pulse(seq,0,u*34);bits.forEach(bit);}part();if(s&1){pulse(seq,0,u*88);part();}return seqRaw(37900,seq);}"
        "function nokiaRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(![d,s,f].every(x=>Number.isFinite(x)&&x>=0&&x<=255))return'';const seq=[],bits=msbBits(d,8).concat(msbBits(s,8),msbBits(f,8));pulse(seq,1,412);pulse(seq,0,276);for(let i=0;i<bits.length;i+=2){const v=(bits[i]<<1)|bits[i+1];pulse(seq,1,164);pulse(seq,0,[276,445,614,783][v]);}pulse(seq,1,164);pulse(seq,0,100000);return seqRaw(36000,seq);}"
        "function nokia12Raw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>15||f<0||f>255)return'';const seq=[],bits=msbBits(d,4).concat(msbBits(f,8));pulse(seq,1,412);pulse(seq,0,276);for(let i=0;i<bits.length;i+=2){const v=(bits[i]<<1)|bits[i+1];pulse(seq,1,164);pulse(seq,0,[276,445,614,783][v]);}pulse(seq,1,164);pulse(seq,0,60000);return seqRaw(36000,seq);}"
        "function nrc17Raw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>255||f<0||f>255)return'';const seq=[],u=500;function frame(cmd,addr,gap){pulse(seq,1,u);pulse(seq,0,u*5);manchester(seq,[1].concat(lsbBits(cmd,8),lsbBits(addr,8)),u,-1);pulse(seq,0,gap);}frame(254,255,u*28);frame(f,d,u*220);frame(254,255,u*200);return seqRaw(38000,seq);}"
        "function streamZapRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>63||f<0||f>127)return'';const seq=[],bits=[1,((~f)>>6)&1,0].concat(msbBits(d,6),msbBits(f&63,6));manchester(seq,bits,889,-1);pulse(seq,0,114000);return seqRaw(36000,seq);}"
        "function logitechRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>15||f<0||f>255)return'';const seq=[],u=127;function bit(b){pulse(seq,1,u*3);pulse(seq,0,b?u*8:u*4);}pulse(seq,1,u*31);pulse(seq,0,u*36);lsbBits(d,4).concat(lsbBits((~d)&15,4),lsbBits(f,8),lsbBits((~f)&255,8)).forEach(bit);pulse(seq,1,u*3);pulse(seq,0,50000);return seqRaw(38000,seq);}"
        "function sircRaw(cur,proto){const cmd=hexValue(cur.command)&127,addr=hexValue(cur.address),bits=/20/.test(proto)?20:(/15/.test(proto)?15:12),addrBits=bits-7,seq=[];pulse(seq,1,2400);pulse(seq,0,600);lsbBits(cmd,7).concat(lsbBits(addr,addrBits)).forEach(b=>{pulse(seq,1,b?1200:600);pulse(seq,0,600);});return seqRaw(40000,seq);}"
        "function jvcRaw(d,f){d=Number(d);f=Number(f);if(![d,f].every(x=>Number.isFinite(x)&&x>=0&&x<=255))return'';const seq=[];pulse(seq,1,8400);pulse(seq,0,4200);lsbBits(d,8).concat(lsbBits(f,8)).forEach(b=>{pulse(seq,1,525);pulse(seq,0,b?1575:525);});pulse(seq,1,525);pulse(seq,0,23625);return seqRaw(38000,seq);}"
        "function jvc48Raw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(![d,s,f].every(x=>Number.isFinite(x)&&x>=0&&x<=255))return'';const seq=[],u=432;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*3:u);}pulse(seq,1,u*8);pulse(seq,0,u*4);lsbBits(3,8).concat(lsbBits(1,8),lsbBits(d,8),lsbBits(s,8),lsbBits(f,8),lsbBits((d^s^f)&255,8)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*173);return seqRaw(37000,seq);}"
        "function konkaRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>255||f<0||f>255)return'';const seq=[],u=500;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*5:u*3);}pulse(seq,1,u*6);pulse(seq,0,u*6);msbBits(d,8).concat(msbBits(f,8)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*8);pulse(seq,1,u);pulse(seq,0,u*46);return seqRaw(38000,seq);}"
        "function tivoRaw(proto,d,s,f){f=Number(f);const m=String(proto||'').match(/unit\\s*=\\s*(\\d+)/i),u=m?Number(m[1]):0;if(!Number.isFinite(f)||!Number.isFinite(u)||f<0||f>255||u<0||u>15)return'';const seq=[],unit=564;function bit(b){pulse(seq,1,unit);pulse(seq,0,b?unit*3:unit);}pulse(seq,1,unit*16);pulse(seq,0,unit*8);lsbBits(133,8).concat(lsbBits(48,8),lsbBits(f,8),lsbBits(u,4),lsbBits(((~f)>>4)&15,4)).forEach(bit);pulse(seq,1,unit);pulse(seq,0,unit*78);pulse(seq,1,unit*16);pulse(seq,0,unit*4);pulse(seq,1,unit);pulse(seq,0,unit*173);return seqRaw(38400,seq);}"
        "function xmpRaw(proto,d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(![d,s,f].every(x=>Number.isFinite(x))||d<0||d>255||s<0||s>255||f<0||f>65535)return'';const p=String(proto||''),oem=68,full=/XMP-1/i.test(p)?((f&255)<<8):(/XMP-2/i.test(p)?(f&255):(f&65535)),seq=[],u=136,sh=(s>>4)&15,sl=s&15,oh=(oem>>4)&15,ol=oem&15,dh=(d>>4)&15,dl=d&15,fn=[(full>>12)&15,(full>>8)&15,(full>>4)&15,full&15],c1=(-(sh+sl+15+oh+ol+dh+dl))&15;function nib(n){pulse(seq,1,210);pulse(seq,0,760+(n&15)*u);}function frame(t){const c2=(-(sh+t+sl+fn[0]+fn[1]+fn[2]+fn[3]))&15;[sh,c1,sl,15,oh,ol,dh,dl].forEach(nib);pulse(seq,1,210);pulse(seq,0,13800);[sh,c2,t,sl].concat(fn).forEach(nib);pulse(seq,1,210);pulse(seq,0,80400);}frame(0);frame(8);return seqRaw(38000,seq);}"
        "function sharpDvdRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>15||s<0||s>255||f<0||f>255)return'';const e=1,c=(d^(s&15)^((s>>4)&15)^(f&15)^((f>>4)&15)^e)&15,seq=[],u=400;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*3:u);}pulse(seq,1,u*8);pulse(seq,0,u*4);lsbBits(170,8).concat(lsbBits(90,8),lsbBits(15,4),lsbBits(d,4),lsbBits(s,8),lsbBits(f,8),lsbBits(e,4),lsbBits(c,4)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*48);return seqRaw(38000,seq);}"
        "function rcaRaw(proto,d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>15||f<0||f>255)return'';const old=/old/i.test(proto||''),freq=/38/.test(proto||'')?38700:58000,seq=[],u=460;if(old)pulse(seq,1,u*32);pulse(seq,1,u*8);pulse(seq,0,u*8);msbBits(d,4).concat(msbBits(f,8),msbBits((~d)&15,4),msbBits((~f)&255,8)).forEach(b=>{pulse(seq,1,u);pulse(seq,0,b?u*4:u*2);});pulse(seq,1,u*(old?2:1));pulse(seq,0,u*16);return seqRaw(freq,seq);}"
        "function panasonicRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(![d,s,f].every(x=>Number.isFinite(x)&&x>=0&&x<=255))return'';const seq=[],bytes=[2,32,d&255,s&255,f&255,(d^s^f)&255];pulse(seq,1,3456);pulse(seq,0,1728);bytes.flatMap(b=>lsbBits(b,8)).forEach(b=>{pulse(seq,1,432);pulse(seq,0,b?1296:432);});pulse(seq,1,432);pulse(seq,0,74400);return seqRaw(37000,seq);}"
        "function panasonic2Raw(d,s,f,x=0){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);x=Number(x);if(![d,s,f,x].every(v=>Number.isFinite(v)&&v>=0&&v<=255))return'';const seq=[],bytes=[2,32,d&255,s&255,x&255,f&255,(d^s^x^f)&255];pulse(seq,1,3456);pulse(seq,0,1728);bytes.flatMap(b=>lsbBits(b,8)).forEach(b=>{pulse(seq,1,432);pulse(seq,0,b?1296:432);});pulse(seq,1,432);pulse(seq,0,74400);return seqRaw(37000,seq);}"
        "function aiwaRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>255||s<0||s>31||f<0||f>255)return'';const seq=[];pulse(seq,1,8800);pulse(seq,0,4400);lsbBits(d,8).concat(lsbBits(s,5),lsbBits((~d)&255,8),lsbBits((~s)&31,5),lsbBits(f,8),lsbBits((~f)&255,8)).forEach(b=>{pulse(seq,1,550);pulse(seq,0,b?1650:550);});pulse(seq,1,550);pulse(seq,0,23100);pulse(seq,1,8800);pulse(seq,0,4400);pulse(seq,1,550);pulse(seq,0,90750);return seqRaw(38000,seq);}"
        "function panasonicOldRaw(d,s,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>31||f<0||f>63)return'';const seq=[];pulse(seq,1,3332);pulse(seq,0,3332);lsbBits(d,5).concat(lsbBits(f,6),lsbBits((~d)&31,5),lsbBits((~f)&63,6)).forEach(b=>{pulse(seq,1,833);pulse(seq,0,b?2499:833);});pulse(seq,1,833);pulse(seq,0,100000);return seqRaw(57600,seq);}"
        "function nec48Raw(d,s,f,e=0){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?(d^255):Number(s);f=Number(f);e=Number(e);if(![d,s,f,e].every(x=>Number.isFinite(x)&&x>=0&&x<=255))return'';const seq=[];pulse(seq,1,9024);pulse(seq,0,4512);lsbBits(d,8).concat(lsbBits(s,8),lsbBits(f,8),lsbBits((~f)&255,8),lsbBits(e,8),lsbBits((~e)&255,8)).forEach(b=>{pulse(seq,1,564);pulse(seq,0,b?1692:564);});pulse(seq,1,564);pulse(seq,0,108000);pulse(seq,1,9024);pulse(seq,0,2256);pulse(seq,1,564);pulse(seq,0,108000);return seqRaw(38000,seq);}"
        "function blaupunktRaw(d,s,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>7||f<0||f>63)return'';const seq=[];pulse(seq,1,528);pulse(seq,0,2640);manchester(seq,Array(10).fill(1),528,-1);pulse(seq,0,20592);pulse(seq,1,528);pulse(seq,0,2640);manchester(seq,[1].concat(lsbBits(f,6),lsbBits(d,3)),528,-1);pulse(seq,0,121440);return seqRaw(30300,seq);}"
        "function dishRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>31||s<0||s>31||f<0||f>63)return'';const bits=msbBits(f,6).concat(msbBits(s,5),msbBits(d,5)),seq=[];pulse(seq,1,400);pulse(seq,0,6100);for(let r=0;r<4;r++){bits.forEach(b=>{pulse(seq,1,400);pulse(seq,0,b?1700:2800);});pulse(seq,1,400);pulse(seq,0,6100);}return seqRaw(57600,seq);}"
        "function barcoRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>127||s<0||s>63||f<0||f>127)return'';const seq=[],u=250;function bit(b){if(b){pulse(seq,1,u);pulse(seq,0,u);}else{pulse(seq,0,u);pulse(seq,1,u);}}pulse(seq,1,u);pulse(seq,0,u);msbBits(d,7).concat(msbBits(s,6),[0,0],msbBits(f,7)).forEach(bit);pulse(seq,0,89000);return seqRaw(55500,seq);}"
        "function thomsonRaw(proto,d,f){d=Number(d);f=Number(f);const seven=/7$/i.test(proto);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>(seven?15:31)||f<0||f>(seven?127:63))return'';const seq=[],u=500;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*9:u*4);}const bits=seven?lsbBits(d,4).concat([0],lsbBits(f,7)):lsbBits(d,4).concat([0],lsbBits(d>>4,1),lsbBits(f,6));bits.concat([1]).forEach(bit);pulse(seq,0,80000);return seqRaw(33000,seq);}"
        "function emersonLikeRaw(proto,d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>63||f<0||f>63)return'';const sc=/^ScAtl-6$/i.test(proto),u=sc?846:872,seq=[];function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*3:u);}pulse(seq,1,u*4);pulse(seq,0,u*4);lsbBits(d,6).concat(lsbBits(f,6),lsbBits((~d)&63,6),lsbBits((~f)&63,6)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*(sc?40:39));return seqRaw(sc?57600:36700,seq);}"
        "function appleRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?135:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>255||s<0||s>255||f<0||f>127)return'';const i=0,c=((pop8(f&127)+pop8(i))%2)===0?1:0,seq=[],u=564;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*3:u);}pulse(seq,1,u*16);pulse(seq,0,u*8);lsbBits(d,8).concat(lsbBits(s,8),[c],lsbBits(f,7),lsbBits(i,8)).forEach(bit);pulse(seq,1,u);pulse(seq,0,u*78);pulse(seq,1,u*16);pulse(seq,0,u*4);pulse(seq,1,u);pulse(seq,0,u*173);return seqRaw(38400,seq);}"
        "function nokia32Raw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(![d,s,f].every(x=>Number.isFinite(x)&&x>=0&&x<=255))return'';const seq=[],bits=msbBits(d,8).concat(msbBits(s,8),msbBits(0,8),msbBits(f,8));pulse(seq,1,412);pulse(seq,0,276);for(let i=0;i<bits.length;i+=2){const v=(bits[i]<<1)|bits[i+1];pulse(seq,1,164);pulse(seq,0,[276,445,614,783][v]);}pulse(seq,1,164);pulse(seq,0,100000);return seqRaw(36000,seq);}"
        "function paceMssRaw(d,f){d=Number(d);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(f)||d<0||d>1||f<0||f>255)return'';const seq=[],u=630;function bit(b){pulse(seq,1,u);pulse(seq,0,b?u*11:u*7);}pulse(seq,1,u);pulse(seq,0,u*5);pulse(seq,1,u);pulse(seq,0,u*5);[0,d&1].concat(msbBits(f,8)).forEach(bit);pulse(seq,1,u);pulse(seq,0,120000);return seqRaw(38000,seq);}"
        "function sejinRaw(proto,d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<0||d>255||s<0||s>255||f<0||f>255)return'';const freq=/-38$/i.test(proto)?38800:56300,u=310,dx=d||1,fx=s,fy=f,e=0,c=((dx&15)+((dx>>4)&15)+(fx&15)+((fx>>4)&15)+(fy&15)+((fy>>4)&15)+e)&15,seq=[],bits=msbBits(3,2).concat(msbBits(dx,8),msbBits(fx,8),msbBits(fy,8),msbBits(e,4),msbBits(c,4));function slot(b){pulse(seq,b?1:0,u);}function pair(v){msbBits([8,4,2,1][v&3],4).forEach(slot);}pulse(seq,1,u*3);for(let i=0;i<bits.length;i+=2)pair((bits[i]<<1)|bits[i+1]);pulse(seq,0,76000);return seqRaw(freq,seq);}"
        "function zenithRaw(d,s,f){d=Number(d);const ss=String(s===undefined?'':s).trim();s=(ss===''||ss==='-1')?0:Number(s);f=Number(f);if(!Number.isFinite(d)||!Number.isFinite(s)||!Number.isFinite(f)||d<1||d>12||s<0||s>1||f<0||f>=Math.pow(2,d))return'';const seq=[],u=520;function zbit(b){if(b){pulse(seq,1,u);pulse(seq,0,u);pulse(seq,1,u);pulse(seq,0,u*8);}else{pulse(seq,1,u);pulse(seq,0,u*10);}}zbit(s&1);for(let i=d-1;i>=0;i--){const b=(Math.floor(f/Math.pow(2,i))&1);if(b){zbit(1);zbit(0);}else{zbit(0);zbit(1);}}pulse(seq,0,90000);return seqRaw(40000,seq);}"
        "function csvProtocolRaw(proto,d,s,f,name=''){proto=String(proto||'');const D=Number(d),F=Number(f),S=String(s===undefined?'':s).trim();if(!Number.isFinite(D)||!Number.isFinite(F)||D<0||F<0)return'';if(/^RC5X?/i.test(proto))return rc5Raw({address:D.toString(16),command:F.toString(16),toggle:'0'});if(/^RC6/i.test(proto))return rc6Raw({address:D.toString(16),command:F.toString(16),toggle:'0'});if(/^MCE$/i.test(proto))return mceRaw(D,S,F);if(/^RECS80$/i.test(proto))return recs80Raw(D,S,F,name);if(/^Akai$/i.test(proto))return akaiRaw(D,F);if(/^Denon-K$/i.test(proto))return denonKRaw(D,S,F);if(/^Denon(?:\\{[12]\\})?$/i.test(proto))return denonRaw(D,F);if(/^Mitsubishi$/i.test(proto))return mitsubishiRaw(D,F);if(/^Velleman$/i.test(proto))return vellemanRaw(D,F);if(/^Fujitsu$/i.test(proto))return fujitsuRaw(D,S,F);if(/^SharpDVD$/i.test(proto))return sharpDvdRaw(D,S,F);if(/^Sharp(?:\\{[12]\\})?$/i.test(proto))return sharpRaw(D,F);if(/^DirecTV$/i.test(proto))return directvRaw(D,F);if(/^GXB$/i.test(proto))return gxbRaw(D,S,F);if(/^G\\.I\\.Cable(?:\\{[12]\\})?$/i.test(proto))return giCableRaw(D,F);if(/^Proton$/i.test(proto))return protonRaw(D,F);if(/^F12$/i.test(proto))return f12Raw(D,S,F);if(/^NRC17$/i.test(proto))return nrc17Raw(D,F);if(/^Nokia32$/i.test(proto))return nokia32Raw(D,S,F);if(/^Nokia$/i.test(proto))return nokiaRaw(D,S,F);if(/^Nokia12$/i.test(proto))return nokia12Raw(D,F);if(/^StreamZap$/i.test(proto))return streamZapRaw(D,F);if(/^Logitech$/i.test(proto))return logitechRaw(D,F);if(/^JVC-48$/i.test(proto))return jvc48Raw(D,S,F);if(/^JVC(?:\\{[12]\\})?$/i.test(proto))return jvcRaw(D,F);if(/^Konka$/i.test(proto))return konkaRaw(D,F);if(/^Tivo\\s+unit\\s*=\\s*\\d+$/i.test(proto))return tivoRaw(proto,D,S,F);if(/^XMP(?:-[12])?$/i.test(proto))return xmpRaw(proto,D,S,F);if(/^Apple$/i.test(proto))return appleRaw(D,S,F);if(/^(?:Emerson|ScAtl-6)$/i.test(proto))return emersonLikeRaw(proto,D,F);if(/^PaceMSS$/i.test(proto))return paceMssRaw(D,F);if(/^Sejin-1-(?:38|56)$/i.test(proto))return sejinRaw(proto,D,S,F);if(/^Zenith$/i.test(proto))return zenithRaw(D,S,F);if(/^Barco$/i.test(proto))return barcoRaw(D,S,F);if(/^Thomson7?$/i.test(proto))return thomsonRaw(proto,D,F);if(/^Panasonic2$/i.test(proto))return panasonic2Raw(D,S,F);if(/^Panasonic$/i.test(proto))return panasonicRaw(D,S,F);if(/^Aiwa$/i.test(proto))return aiwaRaw(D,S,F);if(/^Panasonic_Old$/i.test(proto))return panasonicOldRaw(D,S,F);if(/^Dish_Network$/i.test(proto))return dishRaw(D,S,F);if(/^48-NEC1$/i.test(proto))return nec48Raw(D,S,F,0);if(/^Blaupunkt$/i.test(proto))return blaupunktRaw(D,S,F);if(/^RCA(?:-38)?(?:\\(Old\\))?$/i.test(proto))return rcaRaw(proto,D,F);if(/^Sony(12|15|20)?/i.test(proto)){const bits=(proto.match(/Sony(\\d+)/i)||[])[1]||'12';let addr=D;if(bits==='20'&&S&&S!=='-1'){const sub=Number(S);if(Number.isFinite(sub)&&sub>=0)addr=(D&31)|((sub&255)<<5);}return sircRaw({address:addr.toString(16),command:F.toString(16)},'SIRC'+bits);}return'';}"
        "function prontoToHarmonyRaw(hex){const words=String(hex||'').trim().split(/\\s+/).filter(Boolean).map(x=>parseInt(x,16));if(words.length<8||words.some(x=>!Number.isFinite(x)))return'';if(words[0]!==0)return'';const unit=(words[1]||1)*0.241246,freq=Math.round(1000000/unit),pairs=(words[2]||0)+(words[3]||0),vals=words.slice(4,4+pairs*2).map(w=>Math.round(w*unit));return harmonyRawFromTimings(freq,vals);}"
        "function kaseikyoRaw(cur){const a=hexBytes(cur.address),c=hexBytes(cur.command);if(a.length<4||c.length<1)return'';const bytes=[a[1],a[2],a[0],a[3],c[0],(a[0]^a[3]^c[0])&255],seq=[];pulse(seq,1,3456);pulse(seq,0,1728);bytes.flatMap(b=>lsbBits(b,8)).forEach(b=>{pulse(seq,1,432);pulse(seq,0,b?1296:432);});pulse(seq,1,432);pulse(seq,0,74736);return seqRaw(38000,seq);}"
        "function flipperEntries(t,path=''){const out=[],src=String(path||'');let cur={};function push(){if(!cur.name){cur={};return;}let key='',raw='',meta=(cur.protocol||cur.type||'raw');if(String(cur.type||'').toLowerCase()==='parsed'){const a=hexBytes(cur.address),c=hexBytes(cur.command),p=cur.protocol||'';if(/^Samsung32/i.test(p))key=keyFromParts(p,a[0],a[0],c[0]);else if(/^NECext/i.test(p)){key=keyFromParts(p,a[0],a[1],c[0]);if(!key&&/_Converted_\\/CSV\\/D\\/Denon\\//i.test(src)){raw=denonKRaw(a[0],a[1],c[0]);meta=raw?'Denon-K converted timing':'NECext unsupported';}}else if(/^NEC/i.test(p))key=keyFromParts(p,a[0],a[0]^255,c[0]);else if(/^Pioneer/i.test(p))key=keyFromParts(p,a[0],a[0]^255,c[0]);else if(/^RCA/i.test(p)){raw=rcaRaw(p,a[0],c[0]);meta=raw?p+' converted timing':p+' unsupported';}else if(/^RC5/i.test(p)){raw=rc5Raw(cur);meta=raw?'RC5 converted timing':'RC5 unsupported';}else if(/^RC6/i.test(p)){raw=rc6Raw(cur);meta=raw?'RC6 converted timing':'RC6 unsupported';}else if(/^SIRC/i.test(p)){raw=sircRaw(cur,p);meta=raw?p+' converted timing':p+' unsupported';}else if(/^Kaseikyo/i.test(p)){raw=kaseikyoRaw(cur);meta=raw?'Kaseikyo converted timing':'Kaseikyo unsupported';}}else if(String(cur.type||'').toLowerCase()==='raw'){const vals=String(cur.data||'').trim().split(/\\s+/).filter(Boolean).map(Number);raw=harmonyRawFromTimings(cur.frequency||38000,vals);meta='raw timings '+(cur.frequency||38000)+' Hz ('+vals.length+' durations)';}out.push({name:cur.name,meta:meta,keycode:key,raw:raw});cur={};}t.replace(/\\r/g,'').split('\\n').forEach(line=>{line=line.trim();if(!line)return;if(line[0]==='#'){push();return;}const i=line.indexOf(':');if(i<0)return;const k=line.slice(0,i).trim().toLowerCase(),v=line.slice(i+1).trim();if(k==='name'&&cur.name)push();cur[k]=cur[k]&&k==='data'?cur[k]+' '+v:v;});push();return out;}"
        "function base64Bytes(s){try{const bin=atob(String(s||'').replace(/\\s+/g,''));return Array.from(bin,c=>c.charCodeAt(0));}catch(e){return[];}}"
        "function broadlinkKind(s){const b=base64Bytes(s);if(b.length<8)return'';if(b[0]===0x26)return'ir';if(b[0]===0xb2||b[0]===0xd7)return'rf';return'';}"
        "function broadlinkRaw(s){const b=base64Bytes(s);if(b.length<8||b[0]!==0x26)return'';let len=b[2]|(b[3]<<8),end=Math.min(b.length,4+len),vals=[];for(let i=4;i<end;){let v=b[i++];if(v===0){if(i+1>=end)break;v=(b[i]<<8)|b[i+1];i+=2;}vals.push(Math.round(v*269000/8192));}return harmonyRawFromTimings(38000,vals);}"
        "function miioRaw(s){const b=base64Bytes(s);if(b.length<8)return'';let vals=[];if(b[0]===0x67&&b[1]===0xa5&&b.length>=72){const edge=b[2]|(b[3]<<8),pairs=Math.floor((edge+1)/2),times=[];for(let i=0;i<16;i++){const o=4+i*4;times.push((b[o]|(b[o+1]<<8)|(b[o+2]<<16)|(b[o+3]<<24))>>>0);}for(let i=68;i<Math.min(b.length,68+pairs);i++){const p=times[b[i]&15]||0,g=times[(b[i]>>4)&15]||0;vals.push(p,g);}}else if(b[0]===0xa5&&b[1]===0x67){const pairs=Math.floor(((b[2]<<8)+b[3]+1)/2),dataStart=Math.max(4,b.length-pairs),times=[];for(let i=4;i+1<dataStart;i+=2)times.push((b[i]<<8)+b[i+1]);for(let i=dataStart;i<b.length;i++)vals.push(times[b[i]&15]||0,times[(b[i]>>4)&15]||0);}vals=vals.filter(Boolean);return harmonyRawFromTimings(38000,vals);}"
        "function sendirRaw(s){const m=String(s||'').match(/sendir\\s*,\\s*[^,]+\\s*,\\s*\\d+\\s*,\\s*(\\d+)\\s*,\\s*\\d+\\s*,\\s*\\d+\\s*,\\s*([0-9,\\s]+)/i);if(!m)return'';const freq=Math.max(10000,Math.min(60000,parseInt(m[1],10)||38000)),vals=m[2].split(',').map(x=>Math.round((parseInt(x,10)||0)*1000000/freq)).filter(Boolean);return harmonyRawFromTimings(freq,vals);}"
        "function rawTimingRows(t,label){const rows=[];String(t||'').replace(/\\r/g,'').split('\\n').forEach((line,i)=>{let m=line.match(/^\\s*([^:=]+?)\\s*[:=]\\s*(\\[?\\s*(?:[-+]?\\d+[\\s,]+){3,}[-+]?\\d+\\s*\\]?)\\s*$/),body=m?m[2]:String(line||'').trim();if(!m){const bm=body.match(/^\\[?\\s*((?:[-+]?\\d+[\\s,]+){3,}[-+]?\\d+)\\s*\\]?\\s*$/);if(!bm)return;body=bm[1];}body=String(body).replace(/^\\s*\\[/,'').replace(/\\]\\s*$/,'');const vals=body.split(/[\\s,]+/).map(Number).filter(Number.isFinite),raw=harmonyRawFromTimings(38000,vals);if(raw)rows.push({name:m?safeImportName(m[1]):(label||'Raw command')+' '+(i+1),meta:'raw timings 38000 Hz ('+vals.length+' durations)',raw:raw});});return rows;}"
        "function prontoEntries(t){const rows=[],lines=String(t||'').replace(/\\r/g,'').split('\\n'),re=/((?:0000|0100|5000|6000|7000)(?:\\s+[0-9a-fA-F]{4}){6,})/g;lines.forEach((line,i)=>{let m;while((m=re.exec(line))){const hex=m[1].replace(/\\s+/g,' ').trim(),raw=prontoToHarmonyRaw(hex),name=safeImportName(line.slice(0,m.index).trim().replace(/[:=,]+$/,'').trim()||'Pronto '+(i+1));rows.push({name:name,meta:raw?'Pronto Hex converted timing':'Pronto Hex unsupported',raw:raw});}});return rows;}"
        "function codeStringRow(name,s,meta){s=String(s||'').trim();if(!s)return null;if(/^G:[^\\r\\n]+?:\\d+$/i.test(s))return{name:name,meta:meta||'Harmony compact code',keycode:s};if(/^F[0-9A-F]+(?:[PS][0-9A-F]+)+$/i.test(s))return{name:name,meta:meta||'Harmony raw timing',raw:s};let raw=sendirRaw(s);if(raw)return{name:name,meta:'Global Cache sendir converted timing',raw:raw};raw=prontoToHarmonyRaw((s.match(/(?:0000|0100|5000|6000|7000)(?:\\s+[0-9a-fA-F]{4}){6,}/)||[])[0]||'');if(raw)return{name:name,meta:'Pronto Hex converted timing',raw:raw};const bk=broadlinkKind(s);raw=broadlinkRaw(s);if(raw)return{name:name,meta:'BroadLink IR base64 converted timing',raw:raw};if(bk==='rf')return{name:name,meta:'BroadLink RF packet - not an IR command',raw:''};raw=miioRaw(s);if(raw)return{name:name,meta:'Xiaomi Miio raw converted timing',raw:raw};const rr=rawTimingRows(s,name);if(rr[0])rr[0].name=name;return rr[0]||null;}"
        "function lircProp(block,key){const m=String(block||'').match(new RegExp('^\\\\s*'+key+'\\\\s+([^\\\\n]+)','im'));return m?m[1].trim():'';}function lircNums(block,key){return lircProp(block,key).split(/\\s+/).map(Number).filter(Number.isFinite);}"
        "function lircCodeBits(hex,n,rev){hex=String(hex||'').replace(/^0x/i,'')||'0';if(typeof BigInt==='function'){try{const v=BigInt('0x'+hex),one=BigInt(1),a=[];if(rev){for(let i=0;i<n;i++)a.push(Number((v>>BigInt(i))&one));}else{for(let i=n-1;i>=0;i--)a.push(Number((v>>BigInt(i))&one));}return a;}catch(e){}}const v=parseInt(hex,16)||0;return rev?lsbBits(v,n):msbBits(v,n);}"
        "function lircCodeRaw(block,hex){const bits=parseInt(lircProp(block,'bits'),10)||32,preBits=parseInt(lircProp(block,'pre_data_bits'),10)||0,one=lircNums(block,'one'),zero=lircNums(block,'zero'),head=lircNums(block,'header'),ptrail=parseInt(lircProp(block,'ptrail'),10)||0,gap=parseInt(lircProp(block,'gap'),10)||45000,freq=parseInt(lircProp(block,'frequency'),10)||38000,flags=lircProp(block,'flags'),rev=/REVERSE/i.test(flags);if(one.length<2||zero.length<2)return'';let arr=[];if(preBits)arr=arr.concat(lircCodeBits(lircProp(block,'pre_data'),preBits,rev));arr=arr.concat(lircCodeBits(hex,bits,rev));const seq=[];if(head.length>=2){pulse(seq,1,head[0]);pulse(seq,0,head[1]);}if(/SHIFT_ENC/i.test(flags)){const half=Math.max(250,Math.round(((one[0]||889)+(one[1]||889))/2));manchester(seq,arr,half,-1);}else arr.forEach(b=>{pulse(seq,1,b?one[0]:zero[0]);pulse(seq,0,b?one[1]:zero[1]);});if(ptrail)pulse(seq,1,ptrail);pulse(seq,0,gap);return seqRaw(freq,seq);}"
        "function lircUnsupportedReason(block){const driver=lircProp(block,'driver'),one=lircNums(block,'one'),zero=lircNums(block,'zero');if(/irman/i.test(driver)||(one[0]===0&&one[1]===0&&zero[0]===0&&zero[1]===0))return'LIRC IRMan decoded code - no replayable timing data';if(driver&&one.length<2&&zero.length<2)return'LIRC driver decoded code - no replayable timing data';return'LIRC code unsupported';}"
        "function lircEntries(t){const out=[];String(t||'').replace(/\\r/g,'').split(/begin\\s+remote/i).slice(1).forEach((part,bi)=>{const block=(part.split(/end\\s+remote/i)[0]||''),remote=safeImportName(lircProp(block,'name')||'LIRC remote '+(bi+1)),freq=parseInt(lircProp(block,'frequency'),10)||38000,rawSec=(block.match(/begin\\s+raw_codes([\\s\\S]*?)end\\s+raw_codes/i)||[])[1];if(rawSec){let name='',vals=[];function push(){const raw=harmonyRawFromTimings(freq,vals);if(name&&raw)out.push({name:name,meta:'LIRC raw timings '+freq+' Hz ('+vals.length+' durations)',raw:raw});vals=[];}rawSec.split('\\n').forEach(line=>{line=line.trim();if(!line)return;const m=line.match(/^name\\s+(.+)/i);if(m){push();name=safeImportName(m[1]);}else vals=vals.concat(line.split(/\\s+/).map(Number).filter(Number.isFinite));});push();}const codes=(block.match(/begin\\s+codes([\\s\\S]*?)end\\s+codes/i)||[])[1];if(codes){codes.split('\\n').forEach(line=>{line=line.trim();const m=line.match(/^(.+?)\\s+(0x[0-9a-fA-F]+|[0-9a-fA-F]+)\\b/);if(!m)return;const raw=lircCodeRaw(block,m[2]);out.push({name:safeImportName(m[1]),meta:raw?'LIRC '+remote+' rendered timing':lircUnsupportedReason(block),raw:raw});});}});return out;}"
        "function girrRegexEntries(t){const out=[],cmdRe=/<(?:[^:>\\s]+:)?command\\b([^>]*)>([\\s\\S]*?)<\\/(?:[^:>\\s]+:)?command>/gi;let m,i=0;while((m=cmdRe.exec(String(t||'')))){const attrs=m[1]||'',body=m[2]||'',am=attrs.match(/\\b(?:name|id)=[\"']([^\"']+)[\"']/i),name=safeImportName((am&&am[1])||'GIRR '+(++i));['raw','pronto','ccf','sendir'].forEach(tag=>{const re=new RegExp('<(?:[^:>\\\\s]+:)?'+tag+'\\\\b[^>]*>([\\\\s\\\\S]*?)<\\\\/(?:[^:>\\\\s]+:)?'+tag+'>','gi');let tm;while((tm=re.exec(body))){const txt=decodeHtml((tm[1]||'').replace(/<[^>]+>/g,' ')).trim();let row=null;if(tag==='raw'){row=rawTimingRows(txt,name)[0];if(row)row.name=name;}else row=codeStringRow(name,txt,tag==='sendir'?'Global Cache sendir converted timing':'GIRR '+tag+' converted timing');if(row)out.push(row);}});}return out;}"
        "function girrEntries(t){const out=[];try{const doc=new DOMParser().parseFromString(String(t||''),'application/xml');if(doc.querySelector('parsererror'))return girrRegexEntries(t);doc.querySelectorAll('command').forEach((cmd,i)=>{const name=safeImportName(cmd.getAttribute('name')||cmd.getAttribute('id')||'GIRR '+(i+1));['raw','pronto','ccf','sendir'].forEach(tag=>{cmd.querySelectorAll(tag).forEach(el=>{const txt=(el.textContent||'').trim();let row=null;if(tag==='raw'){row=rawTimingRows(txt,name)[0];if(row)row.name=name;}else row=codeStringRow(name,txt,tag==='sendir'?'Global Cache sendir converted timing':'GIRR '+tag+' converted timing');if(row)out.push(row);});});});}catch(e){}return out.length?out:girrRegexEntries(t);}"
        "function jsonEntries(t){let obj;try{obj=JSON.parse(String(t||''));}catch(e){return[];}const out=[];function walk(v,path){const name=safeImportName(path.filter(Boolean).slice(-4).join(' ')||'JSON command');if(typeof v==='string'){const row=codeStringRow(name,v,'JSON code');if(row)out.push(row);return;}if(Array.isArray(v)){if(v.length>=4&&v.every(x=>Number.isFinite(Number(x)))){const raw=harmonyRawFromTimings(38000,v.map(Number));if(raw)out.push({name:name,meta:'JSON raw timing array',raw:raw});}else v.forEach((x,i)=>walk(x,path.concat(String(i+1))));return;}if(v&&typeof v==='object'){const label=v.name||v.command||v.button||v.key||v.label;['code','data','raw','pronto','prontoHex','sendir','broadlink','base64','command'].forEach(k=>{if(typeof v[k]==='string'){const row=codeStringRow(safeImportName(label||name),v[k],'JSON '+k);if(row)out.push(row);}});Object.keys(v).forEach(k=>walk(v[k],path.concat(k)));}}walk(obj,[]);return out;}"
        "function genericCsvEntries(t){const lines=String(t||'').replace(/\\r/g,'').split('\\n').filter(x=>x.trim());if(!lines.length)return[];const head=csvCells(lines[0]).map(x=>x.toLowerCase());if(head[0]==='functionname')return csvEntries(t);const nameIdx=head.findIndex(x=>/^(name|button|command|function|key)$/.test(x)),codeIdx=head.findIndex(x=>/(code|raw|pronto|sendir|broadlink|base64|data)/.test(x));if(nameIdx<0||codeIdx<0)return csvEntries(t);return lines.slice(1).map((line,i)=>{const c=csvCells(line);return codeStringRow(safeImportName(c[nameIdx]||'CSV '+(i+1)),c[codeIdx],'CSV code');}).filter(Boolean);}"
        "function ircEntries(t){const rows=[];String(t||'').replace(/\\r/g,'').split('\\n').forEach((line,i)=>{const m=line.match(/^\\s*([^:=,]+?)\\s*[:=,]\\s*(.+)$/);const row=codeStringRow(safeImportName(m?m[1]:'IRC '+(i+1)),m?m[2]:line,'IRC code');if(row)rows.push(row);});return rows;}"
        "function dedupeCommandRows(rows){const seen=new Set(),names=new Set(),out=[];(rows||[]).forEach(r=>{if(!(r&&r.name))return;const supported=!!(r.raw||r.keycode),fp=(supported?(r.raw?'raw:'+r.raw:'key:'+r.keycode):'unsupported:'+(r.meta||'')+':'+r.name).replace(/\\s+/g,'').toLowerCase();if(seen.has(fp))return;seen.add(fp);let base=safeImportName(r.name),name=base,n=2;while(names.has(name.toLowerCase()))name=(base+' '+(n++)).slice(0,96);names.add(name.toLowerCase());out.push({...r,name:name});});return out;}"
        "function parseIrText(t,source,path){t=String(t||'');let rows=[];const low=String(path||'').toLowerCase(),hint=String(source||'').toLowerCase();if(hint==='flipper'||/\\.ir$/.test(low)||/^Filetype:\\s*IR/i.test(t))rows=rows.concat(flipperEntries(t,path));if(hint==='irdb'||/\\.csv$/.test(low))rows=rows.concat(genericCsvEntries(t));if(hint==='lirc'||/begin\\s+remote/i.test(t)||/\\.lirc|\\.lircd|\\.conf/.test(low))rows=rows.concat(lircEntries(t));if(hint==='smartir'||/\\.json$/.test(low)||/^\\s*[\\[{]/.test(t))rows=rows.concat(jsonEntries(t));if(hint==='remotecentral'||/<html|Copy to Clipboard|Infrared Hex/i.test(t))rows=rows.concat(remoteCentralCommandEntries(t));if(/<\\?xml|<girr|<command/i.test(t))rows=rows.concat(girrEntries(t));const structured=/^(flipper|irdb|lirc|smartir|remotecentral)$/.test(hint)||/^Filetype:\\s*IR/i.test(t)||/begin\\s+remote/i.test(t)||/^\\s*[\\[{]/.test(t);if(hint==='custom'||!structured||!rows.length)rows=rows.concat(prontoEntries(t),ircEntries(t),rawTimingRows(t,'Raw command'));return dedupeCommandRows(rows);}"
        "function irdbStatus(t){const s=$('irdbStatus');if(s)s.textContent=t;}"
        "function irdbLog(t){const l=$('irdbLog');if(!l)return;let p=l.textContent||'';if(/^Ready\\./.test(p))p='';l.textContent=(t?new Date().toLocaleTimeString()+'  '+t+'\\n':'')+p.slice(0,6000);}"
        "function clearIrdPreview(){const p=$('irdbPayload'),box=$('irdbPreview');if(p)p.value='';if(box){box.replaceChildren();box.classList.add('hidden');}}"
        "function safeImportName(s){s=String(s||'Command').replace(/[|\"\\\\\\r\\n]/g,' ').replace(/\\s+/g,' ').trim().slice(0,96);return s||'Command';}"
        "function sourceLabel(s){return({irdb:'probonopd/irdb',flipper:'Flipper-IRDB',lirc:'LIRC remotes',smartir:'SmartIR JSON',remotecentral:'RemoteCentral',custom:'Pasted file or URL',stored:'Saved device'}[s]||s||'Unknown source');}"
        "function harmonyQueryParts(q){q=String(q||'').trim();let m=q.match(/^([^,]+),\\s*(.+)$/);if(m)return{man:m[1].trim(),model:m[2].trim()};const p=q.split(/\\s+/);return{man:p.shift()||'',model:p.join(' ')||''};}"
        "function decodeHtml(s){const e=document.createElement('textarea');e.innerHTML=String(s||'');return e.value;}"
        "function rcSlug(s){return normText(String(s||'').replace(/&/g,' ')).replace(/\\s+/g,'_').replace(/^_+|_+$/g,'');}"
        "function rcNormalizePath(href,base){href=String(href||'').trim();try{if(/^https?:/i.test(href)){const u=new URL(href);href=u.pathname;}}catch(e){}if(!href||href[0]==='#'||href[0]==='?')return'';if(href[0]==='/')return href;return String(base||'/cgi-bin/codes/').replace(/\\/?$/,'/')+href.replace(/^\\.\\//,'');}"
        "let rcProxyBlockedUntil=0;async function rcFetch(path){path=rcNormalizePath(path,'/cgi-bin/codes/');try{const r=await fetch('/api/remotecentral-fetch?path='+encodeURIComponent(path));const j=await r.json();if(j.ok)return j.html||'';if(!/http 30[1278]/i.test(j.error||''))throw new Error(j.error||'RemoteCentral fetch failed');}catch(e){if(/invalid RemoteCentral/i.test(e.message||''))throw e;}if(Date.now()<rcProxyBlockedUntil)throw new Error('RemoteCentral reader temporarily unavailable; try again shortly');const proxy='https://r.jina.ai/http://www.remotecentral.com'+path,ctl=new AbortController(),timer=setTimeout(()=>ctl.abort(),9000);let pr;try{pr=await fetch(proxy,{headers:{Accept:'text/plain'},signal:ctl.signal});}catch(e){throw new Error(e&&e.name==='AbortError'?'RemoteCentral reader timed out':'RemoteCentral reader request failed');}finally{clearTimeout(timer);}if(!pr.ok){if(pr.status===429)rcProxyBlockedUntil=Date.now()+120000;throw new Error('RemoteCentral reader http '+pr.status);}return await pr.text();}"
        "function rcPageLinks(html,brand){const base='/cgi-bin/codes/'+brand+'/',seen=new Set(),out=[],re=/<a\\s+[^>]*href=[\"']([^\"']+)[\"'][^>]*>([\\s\\S]*?)<\\/a>/gi;let m;while((m=re.exec(html))){const path=rcNormalizePath(m[1],base);if(path.startsWith(base)&&/\\/page-\\d+\\/?$/i.test(path)&&!seen.has(path)){seen.add(path);out.push(path);}}return out.slice(0,5);}"
        "function scoreRcMatch(label,q){const toks=queryTokens(q),n=normText(label);if(!toks.length)return 0;let score=0;for(const t of toks){if(!n.includes(t))return -1;score+=n.split(/\\s+/).includes(t)?45:18;}if(n.includes(normText(q)))score+=80;return score-Math.min(n.length/24,20);}"
        "function rcPushModelLink(out,seen,path,title,base,q,weak){path=rcNormalizePath(path,base);title=plainText(title);if(!path.startsWith(base)||path===base||/\\/page-\\d+\\/?$/i.test(path)||!title||seen.has(path))return;let score=scoreRcMatch(title+' '+path,q),browse=false;if(score<0){if(!weak)return;score=8;browse=true;}seen.add(path);out.push({source:'remotecentral',path:path,title:title,meta:browse?'RemoteCentral category page':'RemoteCentral model page',score:score,browseFallback:browse});}"
        "function rcModelLinks(html,brand,q,weak){const base='/cgi-bin/codes/'+brand+'/',seen=new Set(),out=[];let m,re=/<a\\s+[^>]*href=[\"']([^\"']+)[\"'][^>]*>([\\s\\S]*?)<\\/a>/gi;while((m=re.exec(html)))rcPushModelLink(out,seen,m[1],m[2],base,q,weak);re=/\\[([^\\]\\n]{1,180})\\]\\(((?:https?:\\/\\/(?:www\\.)?remotecentral\\.com)?[^)\\s]+)\\)/gi;while((m=re.exec(html)))rcPushModelLink(out,seen,m[2],m[1],base,q,weak);return out;}"
        "async function remoteCentralEntries(q){const parts=harmonyQueryParts(q),brand=rcSlug(parts.man),model=(parts.model||q).trim();if(!brand||!model||normText(model).length<3)return[];let first;try{first=await rcFetch('/cgi-bin/codes/'+brand+'/');}catch(e){return[];}const pages=[first],links=rcPageLinks(first,brand);for(const p of links){try{pages.push(await rcFetch(p));}catch(e){}}let rows=pages.flatMap(h=>rcModelLinks(h,brand,model,false));if(!rows.length)rows=pages.flatMap(h=>rcModelLinks(h,brand,model,true)).slice(0,24);return rows.sort((a,b)=>b.score-a.score||a.title.length-b.title.length).slice(0,80);}"
        "function rcCleanCommandName(s){s=decodeHtml(String(s||'')).replace(/\\(\\s*Copy\\s+to\\s+Clipboard\\s*\\)/ig,'').replace(/[|\"\\\\]/g,'').replace(/\\s+/g,' ').trim();if(!s||/^(Image|Return|Remote Model|Infrared Hex|This model|Features|Hex Codes|Page:|Copyright|Home|News|Reviews|Files|Forums)$/i.test(s))return'';return s.slice(0,96);}"
        "function rcCommandName(lines,i,prefix){let n=rcCleanCommandName(prefix);if(n)return n;for(let j=i-1;j>=0&&j>=i-8;j--){n=rcCleanCommandName(lines[j]);if(n&&!/^[0-9a-f]{4}\\s/i.test(n))return n;}return 'Command '+(i+1);}"
        "function remoteCentralCommandEntries(html){let text=String(html||'').replace(/<script[\\s\\S]*?<\\/script>/gi,'').replace(/<style[\\s\\S]*?<\\/style>/gi,'').replace(/<br\\s*\\/?\\s*>/gi,'\\n').replace(/<\\/(td|tr|p|div|li)>/gi,'\\n').replace(/<[^>]+>/g,' ');text=decodeHtml(text).replace(/\\(Copy to Clipboard\\)/ig,'\\n(Copy to Clipboard)\\n');const lines=text.replace(/\\r/g,'').split('\\n').map(x=>x.replace(/\\s+/g,' ').trim()).filter(Boolean),rows=[];const pronto=/((?:0000|0100|5000|6000|7000)(?:\\s+[0-9a-fA-F]{4}){10,})/g;lines.forEach((line,i)=>{let m;while((m=pronto.exec(line))){const hex=m[1].replace(/\\s+/g,' ').trim(),raw=prontoToHarmonyRaw(hex);rows.push({name:rcCommandName(lines,i,line.slice(0,m.index)),meta:raw?'Pronto converted raw ('+hex.split(' ').length+' words)':'Pronto unsupported format',raw:raw});}});return rows;}"
        "function normText(s){return String(s||'').toLowerCase().replace(/[^a-z0-9]+/g,' ').trim();}"
        "function queryTokens(s){return normText(s).split(/\\s+/).filter(Boolean);}"
        "function scoreIrdPath(entry,q){if(entry.source==='remotecentral'&&entry.browseFallback)return entry.score||1;const label=(entry.title||'')+' '+entry.path+' '+(entry.meta||''),toks=queryTokens(q),n=normText(label),parts=n.split(/\\s+/).filter(Boolean);if(!toks.length)return 0;let score=0;for(const t of toks){if(!n.includes(t))return -1;if(parts.includes(t))score+=40;else if(parts.some(p=>p.startsWith(t)))score+=22;else score+=10;}const nq=normText(q);if(n.includes(nq))score+=80;if(label.toLowerCase().includes(q.toLowerCase()))score+=30;if(entry.source==='flipper')score+=4;if(entry.source==='irdb')score+=2;score-=Math.min(label.length/16,25);return score;}"
        "function categoryHints(q){const n=normText(q),p=harmonyQueryParts(q),man=normText(p.man),model=normText(p.model),h=[];function add(){[...arguments].forEach(x=>{if(!h.includes(x))h.push(x);});}if(/\\b(tv|television|hdtv|plasma|oled|qled|qned|webos|roku|bravia|viera|4k|8k)\\b/.test(n)||(man==='lg'&&/^(?:[a-z]?\\d{1,2}|[cbgmz]\\d)\\b/.test(model)))add('tv','tvs','television','plasma','oled');if(/\\b(ac|air|conditioner|climate|heat|cool)\\b/.test(n))add('ac','acs','air','conditioner');if(/\\b(sound ?bar|speaker|receiver|avr|amplifier|audio)\\b/.test(n))add('sound','soundbar','soundbars','receiver','audio','speaker');if(/\\b(blu ?ray|dvd|player|disc)\\b/.test(n))add('blu','ray','dvd','player');if(/\\b(projector|beamer)\\b/.test(n))add('projector','projectors');return h;}"
        "function scoreIrdFallback(entry,q){const label=(entry.title||'')+' '+entry.path+' '+(entry.meta||''),n=normText(label),parts=n.split(/\\s+/).filter(Boolean),qp=harmonyQueryParts(q),man=normText(qp.man);if(!man||!n.includes(man))return-1;let score=18;if(parts.includes(man))score+=38;else if(parts.some(p=>p.startsWith(man)))score+=20;const hints=categoryHints(q);let hintHits=0;hints.forEach(h=>{if(n.includes(h)){score+=24;hintHits++;}});queryTokens(qp.model).filter(t=>t.length>=3).forEach(t=>{if(n.includes(t))score+=45;});if(hints.length&&!hintHits)score-=12;if(entry.source==='flipper')score+=4;if(entry.source==='irdb')score+=2;score-=Math.min(label.length/22,18);return score;}"
        "function rankIrdEntries(entries,q){let rows=dedupeIrdEntries((entries||[]).map(r=>({...r,score:scoreIrdPath(r,q)})).filter(r=>r.score>=0)).sort((a,b)=>b.score-a.score||String(a.path).length-String(b.path).length);rows.fallback=false;if(q&&rows.length===0){rows=dedupeIrdEntries((entries||[]).map(r=>({...r,score:scoreIrdFallback(r,q)})).filter(r=>r.score>=0)).sort((a,b)=>b.score-a.score||String(a.path).length-String(b.path).length);rows.fallback=rows.length>0;}return rows;}"
        "function dedupeIrdEntries(rows){const seen=new Set(),out=[],dupes=[];(rows||[]).forEach(r=>{const key=(r.source||'')+'|'+String(r.path||'').toLowerCase();if(!r.path)return;if(seen.has(key)){dupes.push(r);return;}seen.add(key);out.push(r);});out.dupes=dupes.length;return out;}"
        "function sourceBreakdown(rows){const c={};(rows||[]).forEach(r=>{const k=sourceLabel(r.source||'irdb');c[k]=(c[k]||0)+1;});return Object.entries(c).sort((a,b)=>b[1]-a[1]||a[0].localeCompare(b[0])).map(x=>x[0]+' '+x[1]).join(', ');}"
        "async function jsonOk(url){const r=await fetch(url);if(!r.ok)throw new Error('http '+r.status);return r.json();}"
        "async function loadIrdSource(src){if(irdbCache[src])return irdbCache[src];if(src==='flipper'){const j=await jsonOk(FLIPPER_INDEX),files=j.tree||j.files||[];irdbCache[src]=files.filter(x=>!x.type||x.type==='blob').map(x=>(x.path||x.name||x).replace(/^\\//,'')).filter(x=>x.endsWith('.ir')).map(path=>({source:src,path:path}));}else if(src==='lirc'){let paths;try{const j=await jsonOk(LIRC_INDEX);paths=(j.tree||[]).filter(x=>x.type==='blob').map(x=>(x.path||'').replace(/^\\//,''));}catch(e){const j=await jsonOk('https://data.jsdelivr.com/v1/package/gh/probonopd/lirc-remotes@master/flat');paths=(j.files||[]).map(x=>(x.name||'').replace(/^\\//,''));}irdbCache[src]=paths.filter(x=>x&&x!=='README.md'&&!/\\.(png|jpg|jpeg|gif|md|html)$/i.test(x)).map(path=>({source:src,path:path}));}else if(src==='smartir'){let paths;try{const j=await jsonOk(SMARTIR_INDEX);paths=(j.tree||[]).filter(x=>x.type==='blob').map(x=>(x.path||'').replace(/^\\//,''));}catch(e){const j=await jsonOk('https://data.jsdelivr.com/v1/package/gh/smartHomeHub/SmartIR@master/flat');paths=(j.files||[]).map(x=>(x.name||'').replace(/^\\//,''));}irdbCache[src]=paths.filter(x=>/^codes\\/.+\\.json$/i.test(x)).map(path=>({source:src,path:path}));}else{const r=await fetch(IRDB_BASE+'index');if(!r.ok)throw new Error('http '+r.status);const t=await r.text();irdbCache[src]=t.replace(/\\r/g,'').split('\\n').map(x=>x.trim()).filter(x=>x.endsWith('.csv')).map(path=>({source:src,path:path}));}return irdbCache[src];}"
        "async function loadIrdSources(names,logFn){const settled=await Promise.all(names.map(s=>loadIrdSource(s).then(rows=>({s:s,rows:rows})).catch(e=>({s:s,error:e}))));const missed=settled.filter(x=>x.error).map(x=>x.s+' '+(x.error.message||x.error));if(missed.length&&logFn)logFn('source index skipped: '+missed.join('; '));return settled.filter(x=>!x.error).flatMap(x=>x.rows);}"
        "async function loadIrdIndexes(){const src=$('irdbSource')?.value||'all',want=src==='all'?['irdb','flipper','lirc','smartir']:(src==='remotecentral'||src==='custom'?[]:[src]);irdbIndex=await loadIrdSources(want,irdbLog);if(want.length&&!irdbIndex.length)throw new Error('no database indexes loaded');return irdbIndex;}"
        "function updateIrdPayload(){const p=$('irdbPayload'),pre=$('irdbPrefix');if(!p)return;const prefix=pre?pre.value:'';p.value=Array.from(document.querySelectorAll('#irdbPreview input[type=checkbox]:checked')).map(cb=>{const name=prefix+cb.dataset.name;if(cb.dataset.mode==='raw')return name+'|raw|'+(cb.dataset.raw||'');return name+'|'+cb.dataset.keycode;}).join('\\n');}"
        "function renderIrdRows(rows){const box=$('irdbPreview');if(!box)return;box.replaceChildren();let ok=0,raw=0,key=0,unsupported={};if(!rows.length){const d=document.createElement('div');d.className='muted mini';d.textContent='No importable commands found on this page. Try another model page or learn the command manually.';box.append(d);box.classList.remove('hidden');updateIrdPayload();irdbStatus('no importable commands found');irdbLog('parsed 0 importable commands');return;}rows.forEach(r=>{const name=safeImportName(r.name),label=document.createElement('label'),cb=document.createElement('input'),span=document.createElement('span'),supported=!!(r.keycode||r.raw);cb.type='checkbox';cb.checked=supported;cb.disabled=!supported;cb.dataset.name=name;cb.dataset.keycode=r.keycode||'';cb.dataset.raw=r.raw||'';cb.dataset.mode=r.raw?'raw':'keycode';span.textContent=name+' / '+(r.meta||'unknown')+(supported?'':' / unsupported');label.append(cb,span);box.append(label);if(supported){ok++;if(r.raw)raw++;else key++;}else{const m=r.meta||'unknown';unsupported[m]=(unsupported[m]||0)+1;}});box.classList.remove('hidden');box.querySelectorAll('input').forEach(x=>x.addEventListener('change',updateIrdPayload));updateIrdPayload();const bad=rows.length-ok;irdbStatus(ok+' importable commands from '+rows.length+' rows ('+key+' compact, '+raw+' timing, '+bad+' unsupported)');irdbLog('parsed '+rows.length+' rows: '+ok+' importable ('+key+' compact, '+raw+' timing), '+bad+' unsupported');const top=Object.entries(unsupported).sort((a,b)=>b[1]-a[1]).slice(0,4).map(x=>x[0]+' x'+x[1]).join(', ');if(top)irdbLog('unsupported protocols: '+top);}"
        "function renderIrdResults(rows){const box=$('irdbResults'),dl=$('irdbMatches');if(dl)dl.replaceChildren();if(!box)return;box.replaceChildren();if(!rows.length){box.classList.add('hidden');return;}rows.slice(0,100).forEach(r=>{const label=r.title? r.title+' - '+r.path:r.path;if(dl){const o=document.createElement('option');o.value=r.path;o.label=sourceLabel(r.source);dl.append(o);}const b=document.createElement('button');b.type='button';b.className='match';b.innerHTML='<strong>'+escHtml(sourceLabel(r.source))+'</strong> '+escHtml(label)+'<div class=\"queue-meta\">score '+Math.round(r.score||0)+' - '+escHtml(r.path)+'</div>';b.addEventListener('click',()=>pickIrdResult(r,true));box.append(b);});box.classList.remove('hidden');}"
        "function updateIrdMatches(){const q=$('irdbSearch')?.value||'',path=$('irdbPath'),rows=rankIrdEntries(irdbIndex,q);renderIrdResults(rows);if(path&&irdbIndex.some(r=>r.path===q)){const r=irdbIndex.find(r=>r.path===q);pickIrdResult(r,false);}else if(q&&rows.length){if(path&&!path.value){path.value=rows[0].path;path.dataset.source=rows[0].source;}if(rows.fallback)irdbLog('no exact match for '+JSON.stringify(q)+'; showing broader manufacturer/category matches');irdbStatus((rows.fallback?'no exact model match; showing ':'found ')+rows.length+' candidate files'+(rows.dupes?' after '+rows.dupes+' duplicate paths skipped':'')+' ('+sourceBreakdown(rows)+')');}else if(q){irdbStatus('no matching files');}}"
        "function pickSourceForPath(path){const p=$('irdbPath'),forced=p?.dataset.source,src=$('irdbSource')?.value||'all',low=String(path||'').toLowerCase();if(forced)return forced;if(src!=='all')return src;if(low.includes('remotecentral.com/cgi-bin/codes/')||low.startsWith('/cgi-bin/codes/'))return'remotecentral';if(low.includes('lirc-remotes')||/\\.(conf|lircd|lirc)$/.test(low))return'lirc';if(low.includes('smarthomehub/smartir')||/codes\\/.+\\.json$/.test(low))return'smartir';return low.endsWith('.ir')?'flipper':(low.endsWith('.csv')?'irdb':'custom');}"
        "function pickIrdResult(r,fetchNow){const p=$('irdbPath');if(p){p.value=r.path;p.dataset.source=r.source;}irdbStatus('selected '+sourceLabel(r.source)+' '+r.path);if(fetchNow)fetchIrdPath();}"
        "async function runIrdSearch(){try{const src=$('irdbSource')?.value||'all',q=$('irdbSearch')?.value||'';clearIrdPreview();irdbStatus('searching libraries...');irdbLog('search '+(q.trim()?JSON.stringify(q.trim()):'all files')+' in '+src);await loadIrdIndexes();irdbLog('loaded '+irdbIndex.length+' local database file entries ('+sourceBreakdown(irdbIndex)+')');if((src==='all'||src==='remotecentral')&&q.trim()){try{const rr=await remoteCentralEntries(q);irdbIndex=irdbIndex.concat(rr);irdbLog('RemoteCentral candidates: '+rr.length);}catch(e){irdbStatus('RemoteCentral search skipped: '+(e.message||e));irdbLog('RemoteCentral skipped: '+(e.message||e));}}irdbIndex=dedupeIrdEntries(irdbIndex);if(irdbIndex.dupes)irdbLog('skipped '+irdbIndex.dupes+' duplicate candidate paths');updateIrdMatches();if(!q.trim())irdbStatus('loaded '+irdbIndex.length+' library files ('+sourceBreakdown(irdbIndex)+')');}catch(e){irdbStatus('library search failed: '+(e.message||e));irdbLog('search failed: '+(e.message||e));}}"
        "const loadIndex=$('irdbLoadIndex');if(loadIndex){loadIndex.addEventListener('click',()=>{syncProfileToSearch(false);runIrdSearch();});}"
        "async function searchFromProfile(saveFirst){const d=profileData(),q=profileQuery();if(!q){irdbStatus('enter manufacturer and model first');return;}try{if(saveFirst){wizStatus('profileStatus','saving device...');await loadWizardInventory();let id=findProfileDeviceId(d);if(!id){await postForm('/ir/new-device',d);await loadWizardInventory();id=findProfileDeviceId(d);}selectDeviceEverywhere(id,d.name);wizStatus('profileStatus',id?'device saved and selected':'device saved; choose it from the device list');}const src=$('irdbSource'),s=$('irdbSearch');if(src)src.value='all';if(s)s.value=q;const p=$('irdbPath'),r=$('irdbResults');if(p){p.value='';delete p.dataset.source;}if(r){r.replaceChildren();r.classList.add('hidden');}clearIrdPreview();showWizardPanel('library');await runIrdSearch();}catch(e){wizStatus('profileStatus','device save failed: '+(e.message||e));}}"
        "const profileSearch=$('profileSearch');if(profileSearch){profileSearch.addEventListener('click',()=>searchFromProfile(false));}"
        "const profileForm=$('profileDeviceForm');if(profileForm){profileForm.addEventListener('submit',e=>{e.preventDefault();searchFromProfile(true);});}"
        "const search=$('irdbSearch');if(search){search.addEventListener('input',()=>{if(irdbIndex.length)updateIrdMatches();});search.addEventListener('change',()=>{if(irdbIndex.length)updateIrdMatches();});}"
        "const source=$('irdbSource');if(source)source.addEventListener('change',()=>{irdbIndex=[];syncProfileToSearch(false);const p=$('irdbPath'),s=$('irdbSearch'),r=$('irdbResults');if(p){p.value='';delete p.dataset.source;}if(r){r.replaceChildren();r.classList.add('hidden');}clearIrdPreview();if(s&&s.value.trim())runIrdSearch();else irdbStatus('choose what to search for');});"
        "const prefix=$('irdbPrefix');if(prefix)prefix.addEventListener('input',updateIrdPayload);"
        "async function fetchIrdPath(){const pathBox=$('irdbPath');let path=(pathBox?.value||'').trim(),source=pickSourceForPath(path);if(!path){irdbStatus('choose a file, page, URL, or paste codes below');return;}try{clearIrdPreview();irdbLog('fetch '+sourceLabel(source)+' '+path);if(source==='remotecentral'){irdbStatus('fetching RemoteCentral Pronto hex...');const html=await rcFetch(path);const rows=parseIrText(html,source,path);irdbLog('downloaded RemoteCentral page, '+html.length+' bytes');renderIrdRows(rows);return;}irdbStatus('fetching commands...');let url=path;if(!/^https?:/i.test(url)){url=url.replace(/^\\//,'');if(source==='flipper')url=FLIPPER_BASE+url;else if(source==='lirc')url=LIRC_BASE+url;else if(source==='smartir')url=SMARTIR_BASE+url;else url=IRDB_BASE+url.replace(/^codes\\//,'');}const r=await fetch(url);if(!r.ok)throw new Error('http '+r.status);const text=await r.text();irdbLog('downloaded '+text.length+' bytes from '+sourceLabel(source));renderIrdRows(parseIrText(text,source,path));}catch(e){irdbStatus('command fetch failed: '+(e.message||e));irdbLog('fetch failed: '+(e.message||e));}}"
        "const pathBox=$('irdbPath');if(pathBox)pathBox.addEventListener('input',()=>{delete pathBox.dataset.source;});"
        "const fetchBtn=$('irdbFetch');if(fetchBtn){fetchBtn.addEventListener('click',fetchIrdPath);}"
        "const irdbFile=$('irdbFile');if(irdbFile){irdbFile.addEventListener('change',()=>{const file=irdbFile.files&&irdbFile.files[0];if(!file)return;const reader=new FileReader();reader.onload=()=>{const p=$('irdbPaste');if(p)p.value=reader.result||'';irdbStatus('loaded file '+file.name+'; click Parse Pasted Codes');};reader.readAsText(file);});}"
        "const parsePaste=$('irdbParsePaste');if(parsePaste){parsePaste.addEventListener('click',()=>{const text=$('irdbPaste')?.value||'',path=$('irdbFile')?.files?.[0]?.name||$('irdbPath')?.value||'pasted codes',source=pickSourceForPath(path);clearIrdPreview();if(!text.trim()){irdbStatus('paste a code file or choose a file first');return;}const rows=parseIrText(text,source,path);irdbLog('parsed pasted/file input as '+sourceLabel(source)+': '+rows.length+' commands');renderIrdRows(rows);});}"
        "async function postJson(path,data){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data||{})});const t=await r.text();let j;try{j=JSON.parse(t);}catch(e){throw new Error(t||('http '+r.status));}if(!r.ok||j.ok===false)throw new Error(j.message||j.error||('http '+r.status));return j;}"
        "const UPDATE_NAMES=['codex_webui','codex_bthid_keyboard','codex_hal_ltcp','codex_hbus','codex_portal','codex_dhcpd'];const UPDATE_API='https://api.github.com/repos/Ripthulhu/harmony-hub-control/contents/payload/bin/';const UPDATE_DEFAULT_RAW='https://raw.githubusercontent.com/Ripthulhu/harmony-hub-control/main/payload/bin/';const UPDATE_CDN='https://cdn.jsdelivr.net/gh/Ripthulhu/harmony-hub-control@main/payload/bin/';const UPDATE_CDN_FAST='https://fastly.jsdelivr.net/gh/Ripthulhu/harmony-hub-control@main/payload/bin/';const UPDATE_AUTO_INTERVAL=6*60*60*1000;const HEX=Array.from({length:256},(_,i)=>i.toString(16).padStart(2,'0'));"
        "function updateLog(t){const el=$('updateLog');if(el)el.textContent=t||'';}function updateAppend(t){const el=$('updateLog');if(!el)return;let p=el.textContent||'';if(/^Ready\\./.test(p))p='';el.textContent=(p?p+'\\n':'')+String(t||'');el.scrollTop=el.scrollHeight;}"
        "function updateBase(){let b=($('updateRepo')?.value||'').trim()||UPDATE_DEFAULT_RAW;return b.endsWith('/')?b:b+'/';}function updateToken(){return($('updateToken')?.value||'').trim();}function updateUsesDefaultRepo(){return updateBase().toLowerCase()===UPDATE_DEFAULT_RAW.toLowerCase();}function updateBases(){const a=[updateBase()];if(updateUsesDefaultRepo())a.push(UPDATE_CDN,UPDATE_CDN_FAST);return Array.from(new Set(a.map(x=>x.endsWith('/')?x:x+'/')));}"
        "function parseUpdateManifest(t){return String(t||'').replace(/\\r/g,'').split('\\n').map(line=>{const m=line.match(/^([0-9a-fA-F]{32})\\s+(\\S+)$/);return m?{md5:m[1].toLowerCase(),name:m[2]}:null;}).filter(x=>x&&UPDATE_NAMES.includes(x.name));}"
        "async function updateFetchUrl(label,url,asText){const ctrl=new AbortController(),timer=setTimeout(()=>ctrl.abort(),25000);try{const r=await fetch(url,{cache:'no-store',signal:ctrl.signal});if(!r.ok)throw new Error('http '+r.status);return asText?await r.text():await r.arrayBuffer();}catch(e){throw new Error(label+' '+((e&&e.name)==='AbortError'?'timeout':(e&&e.message?e.message:e)));}finally{clearTimeout(timer);}}"
        "async function updateFetch(name,token){const errs=[],asText=name==='MANIFEST.txt';if(token||updateUsesDefaultRepo()){try{const headers={'Accept':'application/vnd.github+json'};if(token)headers.Authorization='Bearer '+token;const r=await fetch(UPDATE_API+encodeURIComponent(name)+'?ref=main&t='+Date.now(),{headers:headers,cache:'no-store'});if(!r.ok)throw new Error('http '+r.status+([401,403,404].includes(r.status)?' - token required for private repos or exhausted anonymous API access':''));const j=await r.json(),bin=atob(String(j.content||'').replace(/\\s/g,'')),u=new Uint8Array(bin.length);for(let i=0;i<bin.length;i++)u[i]=bin.charCodeAt(i);return asText?new TextDecoder().decode(u):u.buffer;}catch(e){errs.push('GitHub API '+(e.message||e));if(token)throw new Error(errs.join('; '));}}for(const base of updateBases()){try{return await updateFetchUrl(base.replace(/^https?:\\/\\//,''),base+encodeURIComponent(name)+'?t='+Date.now(),asText);}catch(e){errs.push(e.message||String(e));}}throw new Error(errs.join('; ')||'no update source configured');}"
        "function updateAgeText(ts){const d=Math.max(0,Date.now()-Number(ts||0)*1000),m=Math.floor(d/60000),h=Math.floor(d/3600000),days=Math.floor(d/86400000);if(!ts)return'not checked';if(days>0)return'checked '+days+'d ago';if(h>0)return'checked '+h+'h ago';if(m>0)return'checked '+m+'m ago';return'checked just now';}"
        "function updateDashboardBadge(st){const b=$('dashUpdateBadge'),d=$('dashUpdateDetail');if(!b||!d)return;st=st||{};const checked=Number(st.checkedAt||0),changes=Number(st.changes||0);b.classList.remove('ok','warn','bad');if(!checked){b.classList.add('warn');b.textContent='not checked';d.textContent='Checks automatically while this page is open.';return;}if(changes<0){b.classList.add('bad');b.textContent='check failed';}else if(st.available){const n=changes||1;b.classList.add('warn');b.textContent=n+' update'+(n===1?'':'s');}else{b.classList.add('ok');b.textContent='current';}d.textContent=updateAgeText(checked)+'. '+(st.message||'');}"
        "async function updateLoadState(){try{const r=await fetch('/api/update-check-state',{cache:'no-store'});if(!r.ok)throw new Error('http '+r.status);const j=await r.json();updateDashboardBadge(j);return j;}catch(e){return null;}}"
        "async function updateSaveState(st){st=st||{};try{await postJson('/api/update-check-state',{checkedAt:String(st.checkedAt||Math.floor(Date.now()/1000)),available:st.available?'1':'0',changes:String(st.changes||0),message:st.message||'',source:st.source||updateBase()});updateDashboardBadge(st);}catch(e){}}"
        "function chunkHex(bytes,start,end){let s='';for(let i=start;i<end;i++)s+=HEX[bytes[i]];return s;}async function updateLocalStatus(){const j=await(await fetch('/api/update-status')).json();const lines=['Local control binaries:'];(j.files||[]).forEach(f=>lines.push((f.present?'ok ':'missing ')+f.name+' '+(f.md5||'')+' '+(f.size||0)+' bytes'));updateLog(lines.join('\\n'));return j;}"
        "async function updateCheckRepo(silent){try{if(!silent)updateLog('checking repository...');const token=updateToken(),manifest=await updateFetch('MANIFEST.txt',token),entries=parseUpdateManifest(manifest),local=await(await fetch('/api/update-status')).json(),byName={};(local.files||[]).forEach(f=>byName[f.name]=f);let changes=0;const lines=['Repository files:'];entries.forEach(e=>{const cur=byName[e.name]||{},same=cur.md5&&cur.md5.toLowerCase()===e.md5;changes+=same?0:1;lines.push((same?'current ':'update  ')+e.name+' repo '+e.md5+' local '+(cur.md5||'missing'));});const msg=changes?changes+' file(s) need update.':'Already current.';lines.push(msg);const st={checkedAt:Math.floor(Date.now()/1000),available:changes>0,changes:changes,message:msg,source:updateBase()};await updateSaveState(st);if(!silent)updateLog(lines.join('\\n'));return st;}catch(e){const msg='update check failed: '+(e.message||e),st={checkedAt:Math.floor(Date.now()/1000),available:false,changes:-1,message:msg,source:updateBase()};await updateSaveState(st);if(!silent)updateLog(msg);return st;}}"
        "async function updateInstallRepo(){try{const token=updateToken();updateLog('loading manifest...');const manifest=await updateFetch('MANIFEST.txt',token),entries=parseUpdateManifest(manifest);if(!entries.length)throw new Error('manifest has no updateable codex binaries');const local=await(await fetch('/api/update-status')).json(),byName={};(local.files||[]).forEach(f=>byName[f.name]=f);const todo=entries.filter(e=>!(byName[e.name]&&String(byName[e.name].md5||'').toLowerCase()===e.md5));if(!todo.length){updateLog('Already current.');await updateSaveState({checkedAt:Math.floor(Date.now()/1000),available:false,changes:0,message:'Already current.',source:updateBase()});return;}await postJson('/api/update-begin',{manifest:manifest});updateLog('staging '+todo.length+' file(s)...');for(const e of todo){updateAppend('fetch '+e.name);const buf=await updateFetch(e.name,token),bytes=new Uint8Array(buf);let off=0;while(off<bytes.length){const end=Math.min(off+24576,bytes.length),hex=chunkHex(bytes,off,end);await postJson('/api/update-chunk',{file:e.name,offset:String(off),hex:hex});off=end;updateAppend('  '+e.name+' '+off+' / '+bytes.length);} }const j=await postJson('/api/update-apply',{restart:'1'});updateAppend('installed: '+j.updated);updateAppend('backup: '+j.backupDir);updateAppend('services are restarting; refresh in about 5 seconds.');await updateSaveState({checkedAt:Math.floor(Date.now()/1000),available:false,changes:0,message:'Update installed; services are restarting.',source:updateBase()});setTimeout(updateLocalStatus,6000);}catch(e){updateAppend('update failed: '+(e.message||e));}}"
        "const updateRefresh=$('updateRefresh');if(updateRefresh)updateRefresh.addEventListener('click',updateLocalStatus);const updateCheck=$('updateCheck');if(updateCheck)updateCheck.addEventListener('click',()=>updateCheckRepo(false));const updateInstall=$('updateInstall');if(updateInstall)updateInstall.addEventListener('click',updateInstallRepo);if($('updateLog'))updateLocalStatus();updateLoadState().then(st=>{const last=Number(st&&st.checkedAt||0)*1000;if(!last||Date.now()-last>UPDATE_AUTO_INTERVAL)updateCheckRepo(true);});setInterval(()=>updateCheckRepo(true),UPDATE_AUTO_INTERVAL);"
        "function btFields(){return{type:$('btType')?.value||'btkeyboard',name:($('btName')?.value||'Harmony Keyboard').trim(),bdaddr:($('btAddr')?.value||'').trim(),pin:($('btPin')?.value||'').trim(),code:($('btCode')?.value||'').trim(),timeout:$('btTimeout')?.value||'8'};}"
        "function btLog(t){const el=$('btLog');if(el)el.textContent=t||'';}"
        "function btAppend(t){const el=$('btLog');if(!el)return;let p=el.textContent||'';if(/^Ready\\./.test(p))p='';if(p.length>6000)p=p.slice(-5000);el.textContent=(p?p+'\\n':'')+String(t||'');el.scrollTop=el.scrollHeight;}"
        "function btPairStatus(t){const el=$('btPairStatus');if(el)el.textContent=t||'';}"
        "function btMaybeFillAddr(raw){raw=String(raw||'');let m=raw.match(/ACL\\s+([0-9A-F]{2}(?::[0-9A-F]{2}){5})/i)||raw.match(/dev_([0-9A-F]{2}(?:_[0-9A-F]{2}){5})/i);if(m){const addr=m[1].replace(/_/g,':').toUpperCase(),el=$('btAddr');if(el&&!el.value.trim()){el.value=addr;btAppend('target address detected: '+addr);}}}"
        "function btSummarizeAdapter(raw){raw=String(raw||'');const addr=(raw.match(/BD Address:\\s*([0-9A-F:]{17})/i)||[])[1]||'',name=(raw.match(/Name:\\s*'([^']+)'/i)||[])[1]||'',mode=(raw.match(/UP RUNNING[^\\n]*/)||[])[0]||'',disc=/\"Discoverable\"[\\s\\S]{0,80}boolean true/.test(raw),pair=/\"Pairable\"[\\s\\S]{0,80}boolean true/.test(raw);return [name?('Name: '+name):'',addr?('Address: '+addr):'',mode?('Mode: '+mode.trim()):'',('Discoverable: '+(disc?'yes':'no')),('Pairable: '+(pair?'yes':'no'))].filter(Boolean).join('\\n')+'\\n\\n'+raw;}"
        "async function btRuntimeStatus(){try{const j=await (await fetch('/api/bt-text-status')).json(),s=$('btRuntimeStatus');const age=j.updated?Math.max(0,Math.round(Date.now()/1000-j.updated)):null;const lines=['Runtime: '+(j.runtime?'running':'missing'),'State: '+(j.state||'unknown'),j.pid?('PID: '+j.pid):'',j.target?('Target: '+j.target):'Target: none',age!==null?('Updated: '+age+'s ago'):'','Sent: '+(j.sent||0)+'  Skipped: '+(j.skipped||0),j.error?('Note: '+j.error):''];if(s)s.textContent=lines.filter(Boolean).join('\\n');btAppend('FIFO runtime '+(j.runtime?j.state:(j.state||'missing')));return j;}catch(e){const s=$('btRuntimeStatus');if(s)s.textContent='Runtime status failed: '+(e.message||e);btAppend('runtime status failed: '+(e.message||e));}}"
        "async function btPost(action,extra,quiet){const data=Object.assign(btFields(),extra||{},{action:action});if(!quiet)btLog(action+'...');try{const j=await postJson('/api/bt-call',data);if(j.detectedAddress){const el=$('btAddr');if(el&&!el.value.trim())el.value=j.detectedAddress;btAppend('target address detected: '+j.detectedAddress);}if(['pairing_on','pairing_off','adapter_status'].includes(action)){btPairStatus(btSummarizeAdapter(j.responseRaw||''));btMaybeFillAddr(j.responseRaw||'');}if(j.connectionRaw)btMaybeFillAddr(j.connectionRaw);if(!quiet)btLog(JSON.stringify(j,null,2));return j;}catch(e){if(quiet)throw e;btLog(action+' failed: '+(e.message||e));}}"
        "function btSavedTargetData(){const d=btFields();return{name:d.name||'Bluetooth keyboard target',type:d.type||'btkeyboard',bdaddr:String(d.bdaddr||'').toUpperCase()};}"
        "const btSaveTargetForm=$('btSaveTargetForm');if(btSaveTargetForm){btSaveTargetForm.addEventListener('submit',e=>{const d=btSavedTargetData();if(!/^[0-9A-F]{2}(:[0-9A-F]{2}){5}$/.test(d.bdaddr)){e.preventDefault();btAppend('save target needs a paired target address. Refresh status after pairing, or enter the address manually.');return;}const n=$('btSaveTargetName'),t=$('btSaveTargetType'),a=$('btSaveTargetAddr');if(n)n.value=d.name;if(t)t.value=d.type;if(a)a.value=d.bdaddr;});}"
        "const btSaveScript=$('btSaveScriptCommand');if(btSaveScript){btSaveScript.addEventListener('click',async()=>{const deviceId=$('btScriptDevice')?.value||'',name=($('btScriptCommandName')?.value||'').trim(),script=$('btScript')?.value||'',delay=String(btDelay());if(!deviceId){btAppend('choose a saved Bluetooth device first');return;}if(!name){btAppend('enter a command name before saving the script');return;}if(!script.trim()){btAppend('paste a keyboard script before saving');return;}try{btAppend('saving Bluetooth command '+name+'...');const res=await postForm('/bt/command',{deviceId:deviceId,name:name,delayMs:delay,script:script});btAppend(plainText(res.text)||'Bluetooth command saved');setTimeout(()=>location.reload(),600);}catch(e){btAppend('save command failed: '+(e.message||e));}});}"
        "function btSleep(ms){return new Promise(r=>setTimeout(r,ms));}"
        "function btDelay(){let v=parseInt($('btScriptDelay')?.value||'35',10);if(!Number.isFinite(v))v=35;v=Math.max(15,Math.min(5000,v));const e=$('btScriptDelay');if(e)e.value=String(v);return v;}"
        "function btCleanKey(s){s=String(s||'').trim().toLowerCase();const a={'return':'enter','esc':'escape','del':'delete','up':'directionup','down':'directiondown','left':'directionleft','right':'directionright','pgup':'pageup','pgdn':'pagedown','vol+':'volumeup','vol-':'volumedown','mute':'volumemute'};s=a[s]||s;if(/^f([1-9]|1[0-2])$/.test(s))return s;if(/^[0-9]$/.test(s))return'number'+s;return s.replace(/[^a-z0-9_-]/g,'');}"
        "function btComboCode(s){const parts=String(s||'').toLowerCase().split(/[+\\s]+/).map(btCleanKey).filter(Boolean);if(!parts.length)return'';let key=parts.pop();const mods=parts.map(m=>m==='control'?'ctrl':((m==='cmd'||m==='meta'||m==='win')?'windows':m)).filter(m=>['ctrl','alt','shift','windows'].includes(m));return mods.join('')+key;}"
        "const btTextMap={' ':'space','\\t':'tab','\\n':'enter','.':'period',',':'comma','-':'minus','_':'shiftminus','/':'slash','?':'shiftslash','=':'equal','+':'shiftequal',';':'semicolon',':':'shiftsemicolon','`':'grave','~':'shiftgrave','[':'leftbracket',']':'rightbracket','\\\\':'backslash','|':'shiftbackslash','!':'shift1','@':'shift2','#':'shift3','$':'shift4','%':'shift5','^':'shift6','&':'shift7','*':'shift8','(':'shift9',')':'shift0'};"
        "function btCharCode(ch){if(/[A-Z]/.test(ch))return'shift'+ch.toLowerCase();if(/[a-z]/.test(ch))return ch;if(/[0-9]/.test(ch))return'number'+ch;return btTextMap[ch]||'';}"
        "function btAddText(text,steps,notes,lineNo){text=String(text||'');if(text)steps.push({kind:'text',text:text,line:lineNo});else notes.push('line '+lineNo+': empty text');}"
        "function btParseScript(){const steps=[],notes=[],raw=$('btScript')?.value||'';raw.replace(/\\r/g,'').split('\\n').forEach((line,i)=>{const n=i+1,clean=line.trim();if(!clean||clean.startsWith('#')||clean.startsWith('//'))return;const m=clean.match(/^([^\\s]+)\\s*(.*)$/),op=(m?m[1]:clean).toUpperCase(),arg=m?(m[2]||''):'';if(op==='WAIT'||op==='SLEEP'){let ms=/s$/i.test(arg.trim())?parseFloat(arg)*1000:parseInt(arg,10);if(!Number.isFinite(ms)||ms<0)notes.push('line '+n+': invalid wait');else steps.push({kind:'wait',ms:Math.min(10000,Math.max(0,ms)),line:n});}else if(op==='TEXT'||op==='TYPE'){btAddText(arg,steps,notes,n);}else if(op==='KEY'||op==='SEND'||op==='PRESS'){const c=btCleanKey(arg);if(c)steps.push({kind:'key',code:c,line:n});else notes.push('line '+n+': missing key name');}else if(op==='COMBO'||op==='HOTKEY'){const c=btComboCode(arg);if(c)steps.push({kind:'key',code:c,line:n});else notes.push('line '+n+': missing combo');}else{const c=btCleanKey(clean);if(c)steps.push({kind:'key',code:c,line:n});else notes.push('line '+n+': unsupported line');}});return{steps:steps,notes:notes};}"
        "function btPreviewScript(){const p=$('btScriptPreview'),r=btParseScript(),shown=r.steps.slice(0,160).map((s,i)=>String(i+1).padStart(3,' ')+'. '+(s.kind==='wait'?'WAIT '+s.ms+' ms':(s.kind==='text'?'TEXT '+JSON.stringify(s.text):'KEY '+s.code)));const more=r.steps.length>shown.length?'\\n... '+(r.steps.length-shown.length)+' more steps':'';if(p)p.textContent=(r.steps.length?shown.join('\\n')+more:'No script steps found.')+(r.notes.length?'\\n\\nNotes:\\n'+r.notes.join('\\n'):'');return r;}"
        "let btScriptStop=false,btScriptRunning=false;function btScriptButtons(on){btScriptRunning=on;['btScriptRun','btScriptPreviewBtn'].forEach(id=>{const e=$(id);if(e)e.disabled=on;});}"
        "async function btRunScript(){if(btScriptRunning)return;const r=btPreviewScript(),gap=btDelay(),chunkSize=24,releaseAll='hex:A1010000000000000000';if(!r.steps.length){btAppend('script has no steps');return;}btScriptStop=false;btScriptButtons(true);let sent=0,buf=[];await btRuntimeStatus();async function sendKeyChunk(keys,label){if(!keys.length)return;const input=$('btCode');if(input)input.value=keys.join('\\n');btAppend(label+': '+keys.length+' keys, '+gap+' ms after release');const j=await btPost('reportseq',{code:keys.join('\\n'),gapMs:String(gap)},true);sent+=keys.length;const tail=String(j.responseRaw||'').trim();btAppend('key chunk ok: '+tail.slice(0,180));await btSleep(25);}async function flush(){if(!buf.length)return;const keys=buf.slice();buf=[];await sendKeyChunk(keys,'send key chunk');}async function sendTextFallback(text){let keys=[],skipped=0;for(const ch of Array.from(text)){const c=btCharCode(ch);if(c)keys.push(c);else skipped++;if(keys.length>=chunkSize){await sendKeyChunk(keys,'fallback text chunk');keys=[];}}if(keys.length)await sendKeyChunk(keys,'fallback text chunk');if(skipped)btAppend('fallback skipped '+skipped+' unsupported text chars');}async function sendText(text){await flush();btAppend('send text: '+text.length+' chars through keyboard FIFO');try{const j=await postJson('/api/bt-text',{text:text});sent+=text.length;btAppend('text ok: '+(j.bytes||text.length)+' bytes');await btSleep(gap);}catch(e){btAppend('FIFO text unavailable, using paired HID reports: '+(e.message||e));await sendTextFallback(text);}}btAppend('script start: '+r.steps.length+' steps, '+gap+' ms post-step gap');try{await btPost('report',{code:releaseAll,gapMs:String(gap)},true);for(const step of r.steps){if(btScriptStop){await flush();btAppend('script stopped after '+sent+' units');break;}if(step.kind==='wait'){await flush();btAppend('wait '+step.ms+' ms');await btSleep(step.ms);continue;}if(step.kind==='text'){await sendText(step.text);continue;}buf.push(step.code);if(buf.length>=chunkSize)await flush();}if(!btScriptStop){await flush();btAppend('script complete: '+sent+' units');}}catch(e){btAppend('script failed: '+(e.message||e));}finally{try{await btPost('report',{code:releaseAll,gapMs:String(gap)},true);btAppend('release all sent');}catch(e){}btScriptButtons(false);btRuntimeStatus();}}"
        "async function btSendTextBlock(){const el=$('btTextBlock'),text=el?el.value:'';if(!text){btAppend('text box is empty');return;}const send=$('btTextSend');if(send)send.disabled=true;try{btAppend('sending text block: '+text.length+' chars');const j=await postJson('/api/bt-text',{text:text});btAppend('text block sent: '+(j.bytes||text.length)+' bytes');await btRuntimeStatus();}catch(e){btAppend('text block failed: '+(e.message||e));}finally{if(send)send.disabled=false;}}"
        "const btPairOn=$('btPairingOn');if(btPairOn)btPairOn.addEventListener('click',()=>btPost('pairing_on'));const btPairOff=$('btPairingOff');if(btPairOff)btPairOff.addEventListener('click',()=>btPost('pairing_off'));const btAdapter=$('btAdapterStatus');if(btAdapter)btAdapter.addEventListener('click',()=>btPost('adapter_status'));const btRuntime=$('btRuntimeRefresh');if(btRuntime)btRuntime.addEventListener('click',btRuntimeStatus);const btClassic=$('btClassicScan');if(btClassic)btClassic.addEventListener('click',()=>btPost('classic_scan'));const btScan=$('btScan');if(btScan)btScan.addEventListener('click',()=>btPost('scan'));const btStatus=$('btStatus');if(btStatus)btStatus.addEventListener('click',()=>btPost('status'));const btConnect=$('btConnect');if(btConnect)btConnect.addEventListener('click',()=>btPost('connect'));const btDisconnect=$('btDisconnect');if(btDisconnect)btDisconnect.addEventListener('click',()=>btPost('disconnect'));const btRelease=$('btReleaseAll');if(btRelease)btRelease.addEventListener('click',()=>btPost('report',{code:'hex:A1010000000000000000',gapMs:String(btDelay())}));const btEnter=$('btEnterTest');if(btEnter)btEnter.addEventListener('click',()=>sendBtKey('enter'));const btPrev=$('btScriptPreviewBtn');if(btPrev)btPrev.addEventListener('click',btPreviewScript);const btRun=$('btScriptRun');if(btRun)btRun.addEventListener('click',btRunScript);const btStop=$('btScriptStop');if(btStop)btStop.addEventListener('click',()=>{btScriptStop=true;btAppend('stop requested');});"
        "const btTextSend=$('btTextSend');if(btTextSend)btTextSend.addEventListener('click',btSendTextBlock);const btTextClear=$('btTextClear');if(btTextClear)btTextClear.addEventListener('click',()=>{const el=$('btTextBlock');if(el)el.value='';});"
        "const KB_MODS={lctrl:'ctrl',rctrl:'ctrl',lshift:'shift',rshift:'shift',lalt:'alt',ralt:'alt',lwin:'win',rwin:'win'};"
        "const kbMods={ctrl:false,shift:false,alt:false,win:false};"
        "function kbModFlash(m,on){['l'+m,'r'+m].forEach(id=>document.querySelectorAll('[data-kb=\"'+id+'\"]').forEach(b=>b.classList.toggle('kb-on',on)));}"
        "function kbToggleMod(code){const m=KB_MODS[code];kbMods[m]=!kbMods[m];kbModFlash(m,kbMods[m]);}"
        "function kbClearMods(){Object.keys(kbMods).forEach(m=>{if(kbMods[m]){kbMods[m]=false;kbModFlash(m,false);}});}"
        "function kbBuildCode(base){let p='';if(kbMods.ctrl)p+='ctrl';if(kbMods.shift)p+='shift';if(kbMods.alt)p+='alt';if(kbMods.win)p+='win';return p+base;}"
        "const RELEASE_ALL='hex:A1010000000000000000';"
        "function kbTextChar(base,mods){if(mods.ctrl||mods.alt||mods.win)return '';const sh=!!mods.shift;if(/^[a-z]$/.test(base))return sh?base.toUpperCase():base;if(base==='space')return ' ';const nums={number1:['1','!'],number2:['2','@'],number3:['3','#'],number4:['4','$'],number5:['5','%'],number6:['6','^'],number7:['7','&'],number8:['8','*'],number9:['9','('],number0:['0',')']};if(nums[base])return nums[base][sh?1:0];const p={minus:['-','_'],equal:['=','+'],leftbracket:['[','{'],rightbracket:[']','}'],backslash:['\\\\','|'],semicolon:[';',':'],apostrophe:[\"'\",'\\\"'],grave:['`','~'],comma:[',','<'],period:['.','>'],slash:['/','?']};return p[base]?p[base][sh?1:0]:'';}"
        "function sendBtKey(code){if(!code)return;if(KB_MODS[code]){kbToggleMod(code);return;}const mods={ctrl:kbMods.ctrl,shift:kbMods.shift,alt:kbMods.alt,win:kbMods.win},full=kbBuildCode(code),text=kbTextChar(code,mods);kbClearMods();const btn=document.querySelector('[data-kb=\"'+code+'\"]');if(btn){btn.classList.add('kb-on');setTimeout(()=>btn.classList.remove('kb-on'),120);}if(text){postJson('/api/bt-text',{text:text}).catch(e=>btPost('report',{code:full,gapMs:'80'},true).catch(()=>btAppend('key: '+(e.message||e))));return;}btPost('report',{code:full,gapMs:'80'},true).catch(e=>btAppend('key: '+(e.message||e)));}"
        "let kbLastSend=0;document.querySelectorAll('[data-kb]').forEach(btn=>{btn.addEventListener('touchstart',e=>{e.preventDefault();const now=Date.now();if(now-kbLastSend<200)return;kbLastSend=now;sendBtKey(btn.dataset.kb);},{passive:false});btn.addEventListener('mousedown',e=>{e.preventDefault();const now=Date.now();if(now-kbLastSend<200)return;kbLastSend=now;sendBtKey(btn.dataset.kb);});});"
        "const shiftSym={'!':'number1','@':'number2','#':'number3','$':'number4','%':'number5','^':'number6','&':'number7','*':'number8','(':'number9',')':'number0','_':'minus','+':'equal','{':'leftbracket','}':'rightbracket','|':'backslash',':':'semicolon','~':'grave','<':'comma','>':'period','?':'slash'};"
        "const keyNameMap={'ArrowUp':'up','ArrowDown':'down','ArrowLeft':'left','ArrowRight':'right','Enter':'enter','Backspace':'backspace','Tab':'tab','Escape':'escape','Delete':'delete','Home':'home','End':'end','PageUp':'pageup','PageDown':'pagedown','CapsLock':'capslock','-':'minus','=':'equal','[':'leftbracket',']':'rightbracket',';':'semicolon','`':'grave',',':'comma','.':'period','/':'slash',' ':'space'};"
        "function domKeyToCode(e){const k=e.key;if(['Control','Shift','Alt','Meta','OS','Dead'].includes(k))return '';let base='';if(k.length===1&&/^[a-z]$/i.test(k))base=k.toLowerCase();else if(k.length===1&&/^[0-9]$/.test(k))base='number'+k;else if(k.charCodeAt(0)===39)base='apostrophe';else if(k.charCodeAt(0)===92)base='backslash';else if(k.charCodeAt(0)===34)base='apostrophe';else if(keyNameMap[k])base=keyNameMap[k];else if(shiftSym[k])base=shiftSym[k];else if(/^F([1-9]|1[0-2])$/.test(k))base=k.toLowerCase();if(!base)return '';let mods='';if(e.ctrlKey)mods+='ctrl';if(e.shiftKey&&!shiftSym[k]&&!/^[A-Z]$/.test(k))mods+='shift';if(e.altKey)mods+='alt';if(e.metaKey)mods+='win';if(/^[A-Z]$/.test(k))mods+='shift';return mods+base;}"
        "let btFwdActive=false;const btFwdToggle=$('btFwdToggle');if(btFwdToggle){btFwdToggle.addEventListener('click',()=>{btFwdActive=!btFwdActive;btFwdToggle.textContent=btFwdActive?'⌨ Stop forwarding':'⌨ Forward my keyboard';btFwdToggle.classList.toggle('danger',btFwdActive);btFwdToggle.classList.toggle('secondary',!btFwdActive);btAppend(btFwdActive?'keyboard forwarding on':'keyboard forwarding off');});}"
        "document.addEventListener('keydown',e=>{if(!btFwdActive)return;if(e.repeat)return;const code=domKeyToCode(e);if(!code)return;e.preventDefault();const btn=document.querySelector('[data-kb=\"'+code.replace(/^(ctrl|shift|alt|win)+/,'')+'\"]');if(btn){btn.classList.add('kb-on');setTimeout(()=>btn.classList.remove('kb-on'),120);}if(!e.ctrlKey&&!e.altKey&&!e.metaKey&&e.key&&e.key.length===1){postJson('/api/bt-text',{text:e.key}).catch(err=>btPost('report',{code:code,gapMs:'60'},true).catch(()=>btAppend('fwd: '+(err.message||err))));return;}btPost('report',{code:code,gapMs:'60'},true).catch(e=>btAppend('fwd: '+(e.message||e)));});"
        "btRuntimeStatus();"
        "const LAB_AUTO_DEVICE='__auto_lab__';const lab={queue:[],index:[],cursor:0,key:'',running:false,stop:false,imported:false,runId:'',streaming:false,seenCodes:new Set(),dupes:0};"
        "function labStatus(t){const s=$('labStatus');if(s)s.textContent=t||'';}function labSleep(ms){return new Promise(r=>setTimeout(r,ms));}"
        "function labNum(id,def,min,max){let v=parseInt($(id)?.value||def,10);if(!Number.isFinite(v))v=def;v=Math.max(min,Math.min(max,v));const e=$(id);if(e)e.value=String(v);return v;}"
        "function labLog(t){const l=$('labLog');if(!l)return;l.textContent=(t?new Date().toLocaleTimeString()+'  '+t+'\\n':'')+l.textContent.slice(0,5000);}"
        "function labMeter(done,total){const m=$('labMeter');if(m)m.style.width=(total?Math.round(done*100/total):0)+'%';}"
        "function labSelectDevice(id,name){['labDevice','irdbDevice','wizardDevice','verifyDevice'].forEach(selId=>{const s=$(selId);if(!s)return;if(!Array.from(s.options).some(o=>o.value===id)){const o=document.createElement('option');o.value=id;o.textContent=(name||'Temporary IR Test')+' ('+id+')';s.append(o);}s.value=id;});populateVerifyCommands();}"
        "async function labResolveDevice(){const sel=$('labDevice');let id=sel?sel.value:'';if(id&&id!==LAB_AUTO_DEVICE)return id;labStatus('preparing temporary test device...');const j=await postJson('/api/ir-lab-target',{});labSelectDevice(j.deviceId,j.name);await loadWizardInventory();labLog((j.created?'created ':'using ')+(j.name||'Temporary IR Test')+' '+j.deviceId);return j.deviceId;}"
        "function labTerms(id){return String($(id)?.value||'').split(',').map(x=>normText(x)).filter(Boolean);}function labMatchName(name){const toks=labTerms('labCommandFilter');if(!toks.length)return true;const n=normText(name);return toks.some(t=>n.includes(t));}"
        "function labSummaryText(){const s=$('labSummary');if(!s)return;const checked=document.querySelectorAll('#labQueue input.labPick:checked').length,total=lab.queue.length,stored=lab.queue.filter(x=>x.stored).length,raw=lab.queue.filter(x=>x.raw).length,dupes=lab.dupes||0;s.textContent=checked+' selected / '+total+' queued / '+stored+' saved / '+raw+' timing'+(dupes?' / '+dupes+' duplicate codes skipped':'');}"
        "function labKey(){return [$('labSource')?.value||'all',$('labPathFilter')?.value||'',$('labCommandFilter')?.value||'',$('labRcPath')?.value||''].join('|');}"
        "function labSourceList(){const s=$('labSource')?.value||'all';if(s==='all')return['irdb','flipper','lirc','smartir'];return(['irdb','flipper','lirc','smartir'].includes(s))?[s]:[];}"
        "function labPathTitle(path){const p=String(path||'').replace(/\\.(csv|ir|json|conf|lircd|lirc|girr|xml|irc)$/i,'').split('/').filter(Boolean);return p.slice(-3).join(' / ')||path;}"
        "function labRenderCandidates(rows){const box=$('labCandidates');if(!box)return;box.replaceChildren();if(!rows.length){box.classList.add('hidden');return;}rows.slice(0,300).forEach(r=>{const b=document.createElement('button');b.type='button';b.className='match';b.innerHTML='<strong>'+escHtml(sourceLabel(r.source))+'</strong> '+escHtml(labPathTitle(r.path))+'<div class=\"queue-meta\">score '+Math.round(r.score||0)+' - '+escHtml(r.path)+'</div>';b.addEventListener('click',()=>{const src=$('labSource'),q=$('labPathFilter');if(src)src.value=r.source;if(q)q.value=r.path;lab.key=labKey();lab.index=[{source:r.source,path:r.path,score:r.score||1,man:r.man||'',model:r.model||''}];lab.cursor=0;labLog('selected '+sourceLabel(r.source)+' '+r.path);labStatus('selected file; click Load commands');});box.append(b);});box.classList.remove('hidden');}"
        "async function labFindCandidates(){try{const src=$('labSource')?.value||'all',q=($('labPathFilter')?.value||'').trim();labStatus('searching device files...');labLog('device search '+(q?JSON.stringify(q):'all files')+' in '+src);let rows=[];const sourceNames=labSourceList();if(sourceNames.length){const files=(await loadIrdSources(sourceNames,labLog)).map(r=>({...r,title:labPathTitle(r.path)})),ranked=q?rankIrdEntries(files,q):files.map(r=>({...r,score:1}));rows=rows.concat(ranked);labLog((q&&ranked.fallback?'no exact local match; broadened to manufacturer/category. ':'')+'loaded local indexes: '+sourceBreakdown(ranked));}if(q&&(src==='all'||src==='remotecentral')){try{const rr=await remoteCentralEntries(q);rows=rows.concat(rr);labLog('RemoteCentral candidates: '+rr.length);}catch(e){labLog('RemoteCentral search skipped: '+(e.message||e));}}rows=dedupeIrdEntries(rows).sort((a,b)=>(b.score||0)-(a.score||0)||String(a.path).length-String(b.path).length);labRenderCandidates(rows);if(rows.length){labStatus((q&&rows.some(r=>r.score&&r.score<60)?'showing likely ':'found ')+rows.length+' possible device files'+(rows.dupes?' after '+rows.dupes+' duplicate paths skipped':'')+' ('+sourceBreakdown(rows)+')');labLog('showing first '+Math.min(rows.length,300)+' candidates');}else labStatus(q?'no matching device files found':'no database files loaded');}catch(e){labStatus('device search failed: '+(e.message||e));labLog('device search failed: '+(e.message||e));}}"
        "function labUniqueName(base){base=safeImportName(base);let name=base,n=2,seen=new Set(lab.queue.map(x=>x.name));while(seen.has(name)){name=(base+' '+n++).slice(0,96);}return name;}function labCodeFingerprint(r){const raw=String(r&&r.raw||'').replace(/\\s+/g,'').toLowerCase(),key=String(r&&r.keycode||'').replace(/\\s+/g,'').toLowerCase();return raw?'raw:'+raw:(key?'key:'+key:'');}function labResetSeen(){lab.seenCodes=new Set();lab.dupes=0;}function labPrimeSeen(){if(!lab.seenCodes)lab.seenCodes=new Set();lab.queue.forEach(x=>{const fp=labCodeFingerprint(x);if(fp)lab.seenCodes.add(fp);});}"
        "function labSelected(){return Array.from(document.querySelectorAll('#labQueue input.labPick:checked')).map(cb=>lab.queue[Number(cb.dataset.i)]).filter(Boolean);}"
        "function labPayloadLines(rows){return (rows||labSelected()).filter(r=>!r.stored&&(r.keycode||r.raw)).map(r=>r.raw?r.name+'|raw|'+r.raw:r.name+'|'+r.keycode);}function labPayload(rows){return labPayloadLines(rows).join('\\n');}"
        "function labRender(){const box=$('labQueue');if(!box)return;box.replaceChildren();if(!lab.queue.length){const d=document.createElement('div');d.className='muted mini';d.textContent='No commands queued.';box.append(d);labMeter(0,1);labSummaryText();return;}lab.queue.forEach((r,i)=>{const row=document.createElement('label'),cb=document.createElement('input'),main=document.createElement('div'),tag=document.createElement('span');row.className='queue-row';cb.className='labPick';cb.type='checkbox';cb.checked=r.checked!==false;cb.dataset.i=i;cb.addEventListener('change',()=>{r.checked=cb.checked;labSummaryText();});const strong=document.createElement('strong'),meta=document.createElement('div');strong.textContent=r.name;meta.className='queue-meta';meta.textContent=(r.stored?'Saved':'Queued')+' / '+sourceLabel(r.source||'irdb')+' / '+(r.meta||r.path||'');tag.className='badge';tag.textContent=r.raw?'timing':(r.stored?'saved':'code');main.append(strong,meta);row.append(cb,main,tag);box.append(row);});labMeter(lab.queue.length,labNum('labMaxCommands',900,1,2048));labSummaryText();}"
        "function labAddRows(rows,source,path){const max=labNum('labMaxCommands',900,1,2048);let added=0;labPrimeSeen();for(const r of rows){if(lab.queue.length>=max)break;if(!(r.keycode||r.raw)||!labMatchName(r.name))continue;const codeFp=labCodeFingerprint(r);if(!codeFp)continue;if(lab.seenCodes.has(codeFp)){lab.dupes=(lab.dupes||0)+1;continue;}const src=r.source||source,pth=r.path||path;lab.seenCodes.add(codeFp);lab.queue.push({source:src,path:pth,name:labUniqueName(r.name),meta:r.meta||'',keycode:r.keycode||'',raw:r.raw||'',stored:false,checked:true});added++;}lab.imported=false;labRender();return added;}"
        "async function labFetchEntry(e){if(e.source==='remotecentral'){const html=await rcFetch(e.path);return parseIrText(html,'remotecentral',e.path).map(r=>({...r,source:'remotecentral',path:e.path}));}let url=e.path.replace(/^\\//,'');if(e.source==='flipper')url=FLIPPER_BASE+url;else if(e.source==='lirc')url=LIRC_BASE+url;else if(e.source==='smartir')url=SMARTIR_BASE+url;else url=IRDB_BASE+url.replace(/^codes\\//,'');const r=await fetch(url);if(!r.ok)throw new Error('http '+r.status);const text=await r.text();return parseIrText(text,e.source,e.path).map(r=>({...r,source:e.source,path:e.path}));}"
        "async function labEnsureIndex(){const key=labKey();if(key===lab.key&&lab.index.length)return;lab.key=key;lab.cursor=0;lab.index=[];const src=$('labSource')?.value||'all',q=($('labPathFilter')?.value||'').trim(),rc=($('labRcPath')?.value||'').trim();let rows=[];if(src==='remotecentral'&&rc){lab.index=[{source:'remotecentral',path:rc,score:1}];labLog('using direct RemoteCentral path '+rc);return;}const sourceNames=labSourceList();if(sourceNames.length){const files=(await loadIrdSources(sourceNames,labLog)).map(r=>({...r,title:labPathTitle(r.path)})),ranked=q?rankIrdEntries(files,q):files.map(r=>({...r,score:1}));rows=rows.concat(ranked);labLog((q&&ranked.fallback?'no exact local match; broadened to manufacturer/category. ':'')+'matched local indexes: '+sourceBreakdown(ranked));}if(q&&(src==='all'||src==='remotecentral')){try{const rr=await remoteCentralEntries(q);rows=rows.concat(rr);labLog('RemoteCentral candidates: '+rr.length);}catch(e){labLog('RemoteCentral search skipped: '+(e.message||e));}}rows=dedupeIrdEntries(rows).sort((a,b)=>(b.score||0)-(a.score||0)||String(a.path).length-String(b.path).length);lab.index=rows;if(rows.dupes)labLog('skipped '+rows.dupes+' duplicate candidate paths');labLog('search index ready: '+rows.length+' files ('+sourceBreakdown(rows)+')');}"
        "async function labScanBatch(){try{const maxFiles=labNum('labMaxFiles',600,1,5000),pause=labNum('labFetchDelay',0,0,3000),maxCmd=labNum('labMaxCommands',900,1,2048),workers=labNum('labWorkers',14,1,24),before=lab.queue.length,beforeDupes=lab.dupes||0;labStatus('loading search index...');await labEnsureIndex();if(lab.cursor>=lab.index.length){labStatus('end of matching library files');return;}const state={files:0,rows:0,supported:0,added:0};async function worker(){while(lab.cursor<lab.index.length&&state.files<maxFiles&&lab.queue.length<maxCmd){const e=lab.index[lab.cursor++],n=++state.files;labStatus('fetching '+n+' / '+maxFiles+' with '+workers+' workers - '+e.path);try{const rows=await labFetchEntry(e);state.rows+=rows.length;state.supported+=rows.filter(x=>x.keycode||x.raw).length;state.added+=labAddRows(rows,e.source,e.path);}catch(err){labLog('skip '+e.path+': '+(err.message||err));}labMeter(lab.cursor,lab.index.length);if(pause)await labSleep(pause);}}await Promise.all(Array.from({length:workers},worker));const dupes=(lab.dupes||0)-beforeDupes,unsupported=Math.max(0,state.rows-state.supported);labStatus('queued '+(lab.queue.length-before)+' new commands'+(dupes?'; skipped '+dupes+' duplicate codes':'')+' from '+state.files+' files; cursor '+lab.cursor+' / '+lab.index.length);labLog('loaded '+state.files+' files, parsed '+state.rows+' commands, '+state.supported+' supported, '+unsupported+' unsupported, '+state.added+' queued');}catch(e){labStatus(e.message||String(e));labLog('load failed: '+(e.message||e));}}"
        "async function labUseStored(){if(!wizardInventory)await loadWizardInventory();const devId=await labResolveDevice(),dev=(wizardInventory&&wizardInventory.devices||[]).find(d=>d.id===devId),cmds=await loadDeviceCommands(devId);lab.queue=[];labResetSeen();cmds.forEach(c=>{if(!labMatchName(c.name))return;const row={source:'stored',stored:true,name:c.name,meta:'saved on '+((dev&&dev.name)||devId),keycode:c.keycode||'',raw:''},fp=labCodeFingerprint(row);if(fp&&lab.seenCodes.has(fp)){lab.dupes++;return;}if(fp)lab.seenCodes.add(fp);lab.queue.push(row);});lab.imported=true;labRender();labStatus('loaded '+lab.queue.length+' saved matching commands'+(lab.dupes?' and skipped '+lab.dupes+' duplicate codes':''));}"
        "async function labImportQueue(rowsArg){const rows=rowsArg||labSelected(),lines=labPayloadLines(rows),dev=await labResolveDevice();if(!lines.length){lab.imported=true;labStatus('selected commands are already saved');return true;}labStatus('saving '+lines.length+' commands...');const limit=420000;let part=[],size=0,done=0,last='';async function flush(){if(!part.length)return;let j;try{j=await postJson('/api/irdb-import',{deviceId:dev,payload:part.join('\\n')});}catch(e){if(!/No supported new/i.test(e.message||''))throw e;j={message:'No new commands were saved; using existing saved names.'};}done+=part.length;last=j.message||last;labLog((j.message||'saved')+' ('+done+' / '+lines.length+')');part=[];size=0;}for(const line of lines){const n=line.length+1;if(part.length&&size+n>limit)await flush();part.push(line);size+=n;}await flush();lab.imported=true;labStatus(last||'save complete');await labSleep(1000);await loadWizardInventory();const saved=await loadDeviceCommands(dev),names=new Set(saved.map(c=>c.name));rows.forEach(r=>{if(names.has(r.name))r.stored=true;});labLog('verified '+names.size+' saved commands on '+dev);labRender();return true;}"
        "async function labClearLabTarget(dev){try{const j=await postJson('/api/ir-lab-clear',{deviceId:dev});labLog(j.message||'temporary device cleared');await labSleep(700);await loadWizardInventory();lab.queue.forEach(r=>{r.stored=false;});lab.imported=false;labRender();return true;}catch(e){labLog('temporary device clear failed: '+(e.message||e));return false;}}"
        "async function labCancelRun(){const id=lab.runId;if(!id)return;try{await postJson('/api/ir-cancel',{runId:id});labLog('cancel sent for '+id);}catch(e){labLog('cancel failed: '+(e.message||e));}}"
        "function labServerChunk(requested,delay){const maxHold=4000,byTime=Math.max(1,Math.floor(maxHold/Math.max(40,delay)));return Math.max(1,Math.min(requested,byTime,1024));}"
        "async function labRunRows(rows,dev,dry,delay,requestedChunk,offset,total){const chunk=labServerChunk(requestedChunk,delay);let done=0;labLog('run '+lab.runId+' using hub chunk '+chunk+' (requested '+requestedChunk+')');for(let i=0;i<rows.length;i+=chunk){if(lab.stop){labStatus('stopped after '+(offset+done)+' commands');break;}const slice=rows.slice(i,i+chunk),label=(dry?'dry-running ':'sending ')+(i+1)+'-'+(i+slice.length)+' / '+rows.length;labStatus((total&&total>rows.length?('stream '+(offset+done)+' sent, '+label):label));const j=await postJson('/api/ir-batch-send',{deviceId:dev,commands:slice.map(r=>r.name).join('\\n'),delayMs:delay,dryRun:dry?'1':'0',runId:lab.runId});done+=j.sent||0;labMeter(total?Math.min(offset+done,total):done,total||rows.length);const failText=j.failed?(', '+j.failed+' failed'):'';labLog((dry?'dry run ':'batch sent ')+(j.sent||0)+' commands'+failText+' in '+(j.elapsedMs||0)+' ms; last '+String(j.lastReply||'').slice(0,120));if(j.canceled){lab.stop=true;labStatus('stopped after '+(offset+done)+' commands');break;}}return done;}"
        "async function labRunQueue(){if(lab.running)return;const rows=labSelected(),dry=$('labDryRun')?.checked,delay=labNum('labSendDelay',80,40,10000),requestedChunk=labNum('labBatchSize',100,1,1024);if(!rows.length){labStatus('select at least one command');return;}try{const dev=await labResolveDevice();if(!dry&&rows.some(r=>!r.stored))await labImportQueue(rows);lab.running=true;lab.stop=false;lab.runId='run_'+Date.now().toString(36)+'_'+Math.random().toString(36).slice(2,8);const ready=dry?rows:rows.filter(r=>r.stored);const done=await labRunRows(ready,dev,dry,delay,requestedChunk,0,ready.length);if(!lab.stop)labStatus((dry?'dry run complete: ':'send complete: ')+done+' commands');labMeter(done,ready.length);}catch(e){labStatus('send failed: '+(e.message||e));}finally{lab.running=false;lab.runId='';}}"
        "function labSetFilter(text,msg){const e=$('labCommandFilter');if(e)e.value=text;lab.index=[];lab.cursor=0;lab.key='';labStatus(msg||'filter updated');}"
        "const labOff=$('labOffFilter');if(labOff)labOff.addEventListener('click',()=>labSetFilter('off, power off, poweroff, standby, shutdown','off-oriented filter loaded'));const labPower=$('labPowerFilter');if(labPower)labPower.addEventListener('click',()=>labSetFilter('power, toggle, on, off, standby','power filter loaded'));const labVol=$('labVolumeFilter');if(labVol)labVol.addEventListener('click',()=>labSetFilter('volume, mute, vol up, vol down','volume filter loaded'));const labInput=$('labInputFilter');if(labInput)labInput.addEventListener('click',()=>labSetFilter('input, hdmi, source, aux, optical, bluetooth','input filter loaded'));const labClearFilter=$('labClearFilter');if(labClearFilter)labClearFilter.addEventListener('click',()=>labSetFilter('','filter cleared'));"
        "const labCandidatesBtn=$('labCandidatesBtn');if(labCandidatesBtn)labCandidatesBtn.addEventListener('click',labFindCandidates);const labScan=$('labScan');if(labScan)labScan.addEventListener('click',labScanBatch);const labStored=$('labStored');if(labStored)labStored.addEventListener('click',labUseStored);const labClear=$('labClear');if(labClear)labClear.addEventListener('click',()=>{lab.queue=[];lab.index=[];lab.cursor=0;lab.key='';labResetSeen();lab.imported=false;labRender();labStatus('queue cleared');});const labImport=$('labImport');if(labImport)labImport.addEventListener('click',()=>labImportQueue().catch(e=>labStatus('import failed: '+(e.message||e))));const labRun=$('labRun');if(labRun)labRun.addEventListener('click',labRunQueue);const labStop=$('labStop');if(labStop)labStop.addEventListener('click',()=>{lab.stop=true;labStatus('stop requested');labCancelRun();});const labSelectAll=$('labSelectAll');if(labSelectAll)labSelectAll.addEventListener('click',()=>{lab.queue.forEach(r=>r.checked=true);labRender();});const labSelectNone=$('labSelectNone');if(labSelectNone)labSelectNone.addEventListener('click',()=>{lab.queue.forEach(r=>r.checked=false);labRender();});const labDropUnchecked=$('labDropUnchecked');if(labDropUnchecked)labDropUnchecked.addEventListener('click',()=>{lab.queue=lab.queue.filter(r=>r.checked!==false);labRender();});['labSource','labPathFilter','labCommandFilter','labRcPath'].forEach(id=>{const e=$(id);if(e)e.addEventListener('change',()=>{lab.index=[];lab.cursor=0;lab.key='';});});labRender();"
        "const irdbForm=$('irdbImportForm');if(irdbForm){irdbForm.addEventListener('submit',e=>{updateIrdPayload();const payload=$('irdbPayload')?.value||'';if(!payload){e.preventDefault();irdbStatus('select at least one supported command');irdbLog('import blocked: no supported checked commands');return;}irdbLog('submitting '+payload.split('\\n').filter(Boolean).length+' selected commands');});}"
        "</script>",
        f);
    fputs("</div></main></body></html>", f);
}

static void status_panel(FILE *f, const struct mqtt_config *mqtt) {
    char uptime[128], version[128], ifconfig[2048], activity[1024];
    char uptime_label[80], inventory_label[80];
    char update_badge[48], update_detail[224], update_age[64];
    char hub_id[64];
    struct update_check_state update_state;
    const char *update_class;
    int hub_id_ok;
    int mqtt_up = tcp_established(mqtt->host, mqtt->port);
    int device_count = -1, command_count = 0;
    int cloud_blocked = load_cloud_blocker();
    long now = time(NULL), age;
    read_text("/proc/uptime", uptime, sizeof(uptime));
    read_text("/etc/version", version, sizeof(version));
    chomp(uptime);
    chomp(version);
    {
        long seconds = atol(uptime);
        long days = seconds / 86400;
        long hours = (seconds % 86400) / 3600;
        long minutes = (seconds % 3600) / 60;
        if (days > 0) snprintf(uptime_label, sizeof(uptime_label), "%ldd %ldh %ldm", days, hours, minutes);
        else snprintf(uptime_label, sizeof(uptime_label), "%ldh %ldm", hours, minutes);
    }
    hub_id_ok = load_hub_id(hub_id, sizeof(hub_id));
    run_cmd("ifconfig ath0 2>/dev/null", ifconfig, sizeof(ifconfig));
    if (scan_ir_resource_stats(&device_count, &command_count, NULL, NULL) != 0) {
        device_count = -1;
        command_count = 0;
    }
    if (device_count < 0) snprintf(inventory_label, sizeof(inventory_label), "unknown");
    else snprintf(inventory_label, sizeof(inventory_label), "%d devices", device_count);
    load_update_state(&update_state);
    if (update_state.checked_at <= 0) {
        snprintf(update_badge, sizeof(update_badge), "not checked");
        snprintf(update_detail, sizeof(update_detail), "Checks automatically while this page is open.");
        update_class = "warn";
    } else {
        age = now - update_state.checked_at;
        if (age < 0) age = 0;
        if (age >= 86400) snprintf(update_age, sizeof(update_age), "%ldd ago", age / 86400);
        else if (age >= 3600) snprintf(update_age, sizeof(update_age), "%ldh ago", age / 3600);
        else if (age >= 60) snprintf(update_age, sizeof(update_age), "%ldm ago", age / 60);
        else snprintf(update_age, sizeof(update_age), "just now");
        if (update_state.changes < 0) {
            snprintf(update_badge, sizeof(update_badge), "check failed");
            update_class = "bad";
        } else if (update_state.available) {
            snprintf(update_badge, sizeof(update_badge), "%d update%s", update_state.changes, update_state.changes == 1 ? "" : "s");
            update_class = "warn";
        } else {
            snprintf(update_badge, sizeof(update_badge), "current");
            update_class = "ok";
        }
        snprintf(update_detail, sizeof(update_detail), "Checked %s. %s", update_age, update_state.message[0] ? update_state.message : "No details.");
    }
    {
        char esc_id[128], cmd[384];
        if (hub_id_ok) {
            shell_escape_single(hub_id, esc_id, sizeof(esc_id));
            snprintf(cmd, sizeof(cmd), "/data/codex/bin/codex_hbus '%s' harmony.engine?getCurrentActivity '{}' 2>/dev/null", esc_id);
            run_cmd(cmd, activity, sizeof(activity));
        } else {
            activity[0] = '\0';
        }
    }
    fprintf(f, "<section id='view-overview' data-view='overview' class='section active'><div class='section-head'><div><h2>Dashboard</h2><div class='section-lead'>Check hub health, saved devices, and service status before changing settings.</div></div></div>");
    fprintf(f, "<div class='cards dashboard-cards'>");
    fprintf(f, "<div class='stat'><div class='label'>Firmware</div><div class='value'>"); html(f, version[0] ? version : "unknown"); fprintf(f, "</div></div>");
    fprintf(f, "<div class='stat'><div class='label'>Uptime</div><div class='value'>"); html(f, uptime_label); fprintf(f, "</div></div>");
    fprintf(f, "<div class='stat'><div class='label'>MQTT</div><div class='value'><span class='badge %s'>%s</span></div><div class='muted mini'>%s</div></div>",
        mqtt_up ? "ok" : "bad", mqtt_up ? "connected" : "not connected", mqtt->enabled ? "bridge enabled" : "bridge disabled");
    fprintf(f, "<div class='stat'><div class='label'>IR Inventory</div><div class='value'>");
    html(f, inventory_label);
    fprintf(f, "</div><div class='muted mini'>%d commands</div></div>", command_count);
    fprintf(f, "<div class='stat'><div class='label'>Logitech cloud</div><div class='value'><span class='badge %s'>%s</span></div><div class='muted mini'>%s</div></div>",
        cloud_blocked ? "ok" : "warn",
        cloud_blocked ? "blocked" : "allowed",
        cloud_blocked ? "local control only" : "takes effect after restart");
    fprintf(f, "<div class='stat'><div class='label'>Activity API</div><div class='value'><span class='badge %s'>%s</span></div></div>",
        activity[0] ? "ok" : "warn", activity[0] ? "responding" : "quiet");
    fprintf(f, "<div class='stat'><div class='label'>Software update</div><div class='value'><span id='dashUpdateBadge' class='badge %s'>", update_class);
    html(f, update_badge);
    fprintf(f, "</span></div><div id='dashUpdateDetail' class='muted mini'>");
    html(f, update_detail);
    fprintf(f, "</div></div>");
    fprintf(f, "</div><div class='quick-actions'><button type='button' data-view-target='control'><strong>Use a remote</strong><div class='muted mini'>Send saved buttons from the remote skin or command list.</div></button><button type='button' data-view-target='ir'><strong>Add or edit remotes</strong><div class='muted mini'>Create devices, search databases, learn buttons, and edit commands.</div></button><button type='button' data-view-target='lab'><strong>Bulk test IR codes</strong><div class='muted mini'>Search many code files, skip duplicates, then send a queue.</div></button><button type='button' data-view-target='mqtt'><strong>Set up Home Assistant</strong><div class='muted mini'>Configure MQTT topics, discovery, and state publishing.</div></button><button type='button' data-view-target='wifi'><strong>Change Wi-Fi</strong><div class='muted mini'>Update the network and reboot when you are ready.</div></button><button type='button' data-view-target='backup'><strong>Back up settings</strong><div class='muted mini'>Download a restore point before larger changes.</div></button></div><div class='grid' style='margin-top:12px'>");
    fprintf(f, "<details><summary>Network details</summary><pre>");
    html(f, ifconfig[0] ? ifconfig : "ath0 not available");
    fprintf(f, "</pre></details>");
    fprintf(f, "<details><summary>Current activity payload</summary><pre>");
    html(f, activity[0] ? activity : "no response");
    fprintf(f, "</pre></details></div>");
    fprintf(f, "</section>");
}

static void mqtt_form(FILE *f, const struct mqtt_config *cfg) {
    fprintf(f, "<div class='panel'><h3>Broker connection</h3><div class='help'>Connect the hub to your MQTT broker. Leave the password blank when you only want to change topics or timing.</div><form method='post' action='/mqtt#mqtt'>");
    fprintf(f, "<label><input type='checkbox' name='enabled' value='1' %s> Enabled</label>", cfg->enabled ? "checked" : "");
    fprintf(f, "<div class='row'><div><label>MQTT broker address</label><input name='host' value='"); html(f, cfg->host); fprintf(f, "'><div class='help'>IP address or DNS name of the MQTT broker.</div></div>");
    fprintf(f, "<div><label>MQTT broker port</label><input name='port' inputmode='numeric' value='%d'><div class='help'>Usually 1883 unless your broker is configured differently.</div></div></div>", cfg->port);
    fprintf(f, "<div class='row'><div><label>Username</label><input name='username' autocomplete='username' value='"); html(f, cfg->username); fprintf(f, "'></div>");
    fprintf(f, "<div><label>Password</label><input name='password' type='password' autocomplete='current-password' placeholder='Leave blank to keep current'><label><input type='checkbox' name='keep_password' value='1' checked> Keep current password when blank</label></div></div>");
    fprintf(f, "<div class='row'><div><label>MQTT base topic</label><input name='baseTopic' value='"); html(f, cfg->base_topic); fprintf(f, "'></div>");
    fprintf(f, "<div><label>Home Assistant discovery prefix</label><input name='discoveryPrefix' value='"); html(f, cfg->discovery_prefix); fprintf(f, "'><div class='help'>Home Assistant commonly uses homeassistant.</div></div></div>");
    fprintf(f, "<div class='row'><div><label>MQTT client ID</label><input name='clientId' value='"); html(f, cfg->client_id); fprintf(f, "'></div>");
    fprintf(f, "<div><label>Display name</label><input name='name' value='"); html(f, cfg->name); fprintf(f, "'></div></div>");
    fprintf(f, "<div class='row'><div><label>State update interval (seconds)</label><input name='pollSeconds' inputmode='numeric' value='%d'></div>", cfg->poll_seconds);
    fprintf(f, "<div><label>MQTT keepalive (seconds)</label><input name='keepAlive' inputmode='numeric' value='%d'></div></div>", cfg->keep_alive);
    fprintf(f, "<label><input type='checkbox' name='haDiscovery' value='1' %s> Create Home Assistant entities automatically</label>", cfg->ha_discovery ? "checked" : "");
    fprintf(f, "<div class='actions'><button type='submit'>Save MQTT settings</button></div></form></div>");
}

static void wifi_form(FILE *f, const struct wifi_config *cfg) {
    fprintf(f, "<div class='panel'><h3>Network connection</h3><div class='help'>Save stores the new network. Save and reboot applies it now, so make a backup first if you are changing the network you are currently using.</div><form method='post' action='/wifi#wifi'>");
    fprintf(f, "<label>Wi-Fi network name (SSID)</label><input name='ssid' autocomplete='off' required value='"); html(f, cfg->ssid); fprintf(f, "'><div class='help'>The hub will join this network after reboot.</div>");
    fprintf(f, "<label>Password</label><input name='password' type='password' autocomplete='current-password' placeholder='Leave blank to keep current'>");
    fprintf(f, "<label><input type='checkbox' name='keep_password' value='1' checked> Keep current password when blank</label>");
    fprintf(f, "<label><input type='checkbox' name='hidden' value='1' %s> Hidden network</label>", cfg->hidden ? "checked" : "");
    fprintf(f, "<label><input type='checkbox' name='open' value='1' %s> No password / open network</label>", cfg->open ? "checked" : "");
    fprintf(f, "<div class='actions'><button name='apply' value='save' type='submit'>Save Wi-Fi</button><button name='apply' value='reboot' type='submit' class='secondary'>Save and reboot</button></div>");
    fprintf(f, "</form></div>");
}

static void backup_panel(FILE *f) {
    fprintf(f, "<section id='view-backup' data-view='backup' class='section'><div class='section-head'><div><h2>Backup and restore</h2><div class='section-lead'>Download a restore point before large edits, database imports, or network changes. Imports create an automatic timestamped backup first.</div></div></div>");
    fprintf(f, "<div class='grid'><div class='panel'><h3>Download backup</h3><div class='help'>Use Full backup for normal restore points. Use the smaller downloads only when you want one part of the configuration.</div><div class='export-list'>");
    fprintf(f, "<a class='button' href='/export/bundle'>Full backup</a>");
    fprintf(f, "<a class='button' href='/export/devices'>Devices</a>");
    fprintf(f, "<a class='button' href='/export/functions'>Functions</a>");
    fprintf(f, "<a class='button' href='/export/protocols'>Protocols</a>");
    fprintf(f, "<a class='button' href='/export/mqtt'>MQTT</a>");
    fprintf(f, "<a class='button' href='/export/wifi'>Wi-Fi</a>");
    fprintf(f, "<a class='button' href='/export/cloud'>Cloud blocker</a>");
    fprintf(f, "<a class='button' href='/export/bluetooth'>Bluetooth devices</a>");
    fprintf(f, "</div></div>");
    fprintf(f, "<div class='panel'><h3>Restore from backup</h3><div class='help'>Choose what the pasted backup contains. Wi-Fi restores are saved immediately but do not take effect until reboot.</div><form method='post' action='/import#backup'>");
    fprintf(f, "<label for='backupTarget'>Backup type</label><select id='backupTarget' name='target'>");
    fprintf(f, "<option value='bundle'>Full backup bundle</option>");
    fprintf(f, "<option value='devices'>DeviceList.json</option>");
    fprintf(f, "<option value='functions'>FunctionList.json</option>");
    fprintf(f, "<option value='protocols'>ProtocolList.json</option>");
    fprintf(f, "<option value='mqtt'>MQTT config</option>");
    fprintf(f, "<option value='wifi'>Wi-Fi config</option>");
    fprintf(f, "<option value='cloud'>Cloud blocker setting</option>");
    fprintf(f, "<option value='bluetooth'>Bluetooth devices</option>");
    fprintf(f, "</select>");
    fprintf(f, "<label for='importFile'>Backup file</label><input id='importFile' type='file'>");
    fprintf(f, "<label for='backupPayload'>Backup contents</label><textarea id='backupPayload' class='textarea-tall' name='payload' spellcheck='false' required></textarea>");
    fprintf(f, "<div class='actions'><button type='submit'>Restore backup</button></div>");
    fprintf(f, "</form></div></div></section>");
}

static void ir_device_options(FILE *f, const struct ir_inventory *inv, const char *selected) {
    int i;
    for (i = 0; i < inv->device_count; i++) {
        const struct ir_device *dev = &inv->devices[i];
        fprintf(f, "<option value='");
        html(f, dev->id);
        fprintf(f, "'%s>", selected && strcmp(selected, dev->id) == 0 ? " selected" : "");
        html(f, dev->name[0] ? dev->name : dev->id);
        fprintf(f, " (");
        html(f, dev->id);
        fprintf(f, ")</option>");
    }
}

static void ir_command_editor(FILE *f, const char *device_id, const struct ir_command *cmd, int edit) {
    char protocol[24];
    const char *mode = "keycode";
    const char *name = "";
    const char *keycode = "";
    const char *raw = "";
    snprintf(protocol, sizeof(protocol), "%d", cmd ? (cmd->protocol_id > 0 ? cmd->protocol_id : 2) : 2);
    if (cmd) {
        name = cmd->name;
        keycode = cmd->keycode;
        raw = cmd->raw;
        mode = cmd->has_raw ? "raw" : "keycode";
    }
    fprintf(f, "<details class='command-editor'><summary>%s</summary><form method='post' action='%s#ir'>",
        edit ? "Edit command" : "Add command manually",
        edit ? "/ir/update-command" : "/ir/command");
    fprintf(f, "<input type='hidden' name='deviceId' value='");
    html(f, device_id);
    fprintf(f, "'>");
    if (edit) {
        fprintf(f, "<input type='hidden' name='oldName' value='");
        html(f, name);
        fprintf(f, "'>");
    }
    fprintf(f, "<div class='row'><div><label>Command name</label><input name='name' value='");
    html(f, name);
    fprintf(f, "' required placeholder='Power, Input HDMI 1, Volume Up'></div><div><label>Save format</label><select name='mode'>");
    fprintf(f, "<option value='keycode'%s>Harmony compact code</option>", strcmp(mode, "keycode") == 0 ? " selected" : "");
    fprintf(f, "<option value='nec'>NEC 32-bit code</option>");
    fprintf(f, "<option value='raw'%s>Raw timing replay</option>", strcmp(mode, "raw") == 0 ? " selected" : "");
    fprintf(f, "</select></div></div>");
    fprintf(f, "<div class='row'><div><label>Protocol id</label><input name='protocol' value='");
    html(f, protocol);
    fprintf(f, "' placeholder='2'></div><div><label>NEC hex code</label><input name='nec' placeholder='E0E040BF'></div></div>");
    fprintf(f, "<label>Harmony compact code</label><input name='keycode' value='");
    html(f, keycode);
    fprintf(f, "' placeholder='G:Toshiba 32 Bit:(0xE0E040BF)(Repeat)():3'>");
    fprintf(f, "<label>Raw timing data</label><textarea name='raw' class='textarea-tall' spellcheck='false' placeholder='F38000P...'>");
    html(f, raw);
    fprintf(f, "</textarea><div class='actions'><button type='submit'>%s</button></div></form></details>",
        edit ? "Save command changes" : "Add command");
}

struct ir_remote_button {
    const char *label;
    const char *aliases;
    double x, y, w, h;
};

static const struct ir_remote_button IR_REMOTE_BUTTONS[] = {
    {"Power off", "poweroff|power off|standby|shutdown|off|powertoggle|power toggle|power", 13.55, 0.45, 18.60, 3.35},
    {"Music", "music|audio", 8.80, 6.85, 26.75, 5.10},
    {"TV", "tv|television|watchtv|display", 35.50, 6.85, 27.10, 5.10},
    {"Movie", "movie|video|media", 62.60, 6.85, 27.10, 5.10},
    {"Rewind", "rewind|rev|skipback|previous track", 8.80, 14.90, 27.95, 5.25},
    {"Play", "play", 41.45, 15.00, 16.25, 5.40},
    {"Forward", "fastforward|forward|ffwd|next track|skipforward", 62.45, 14.85, 27.95, 5.30},
    {"Record", "record|rec", 8.95, 21.80, 27.90, 5.35},
    {"Pause", "pause", 41.45, 21.80, 16.25, 5.35},
    {"Stop", "stop", 62.45, 21.80, 27.95, 5.35},
    {"Red", "red", 9.30, 30.00, 18.60, 3.75},
    {"Green", "green", 30.10, 30.00, 18.95, 3.75},
    {"Yellow", "yellow", 51.80, 30.00, 18.15, 3.75},
    {"Blue", "blue", 73.25, 30.00, 17.45, 3.75},
    {"DVR", "dvr|recordings", 9.00, 36.95, 27.40, 5.05},
    {"Guide", "guide|epg", 36.40, 36.95, 26.40, 5.05},
    {"Info", "info|information|displayinfo|settings|setting|setup|option|options", 62.80, 36.95, 26.75, 5.05},
    {"Exit", "exit|clear|cancel", 9.10, 45.45, 36.05, 4.95},
    {"Menu", "menu|home", 53.95, 45.45, 35.70, 4.95},
    {"Up", "up|directionup|arrowup|cursorup", 36.40, 49.85, 27.60, 6.30},
    {"Left", "left|directionleft|arrowleft|cursorleft", 28.80, 54.70, 14.90, 12.10},
    {"Right", "right|directionright|arrowright|cursorright", 56.30, 54.70, 14.90, 12.10},
    {"Down", "down|directiondown|arrowdown|cursordown", 36.40, 64.55, 27.60, 6.20},
    {"OK", "ok|select|enter", 40.10, 56.10, 19.80, 9.10},
    {"Volume up", "volumeup|volup|vol up|vol_up|volume up", 9.30, 52.20, 19.30, 8.85},
    {"Volume down", "volumedown|voldown|voldn|vol down|vol_down|vol_dn|volume down", 9.30, 61.05, 19.30, 9.05},
    {"Channel up", "channelup|chup|chnext|ch_next|ch up|channel next|channelnext|pageup|pgup", 70.20, 52.20, 19.15, 8.85},
    {"Channel down", "channeldown|chdown|chdn|chprev|ch_prev|ch down|ch_dn|channel prev|channel previous|channel down|channelprev|channeldn|pagedown|pgdown", 70.20, 61.05, 19.15, 9.05},
    {"Mute", "mute", 9.30, 71.70, 36.20, 5.30},
    {"Back", "back|return|previous", 54.15, 71.70, 35.55, 5.30},
    {"1", "1|digit1|number1|num1", 9.30, 80.45, 27.10, 3.45},
    {"2", "2|digit2|number2|num2", 36.40, 80.45, 26.25, 3.45},
    {"3", "3|digit3|number3|num3", 62.60, 80.45, 27.05, 3.45},
    {"4", "4|digit4|number4|num4", 9.30, 85.55, 27.10, 3.40},
    {"5", "5|digit5|number5|num5", 36.40, 85.55, 26.25, 3.40},
    {"6", "6|digit6|number6|num6", 62.60, 85.55, 27.05, 3.40},
    {"7", "7|digit7|number7|num7", 9.30, 90.60, 27.10, 3.35},
    {"8", "8|digit8|number8|num8", 36.40, 90.60, 26.25, 3.35},
    {"9", "9|digit9|number9|num9", 62.60, 90.60, 27.05, 3.35},
    {"Dash", "dash|hyphen|separator|dot|period|minus", 9.30, 95.70, 27.10, 3.35},
    {"0", "0|digit0|number0|num0", 36.40, 95.70, 26.25, 3.35},
    {"Enter", "enter|e", 62.60, 95.70, 27.05, 3.35}
};

static void ir_command_key(const char *s, char *out, size_t outlen) {
    size_t w = 0;
    if (!outlen) return;
    while (s && *s && w + 1 < outlen) {
        unsigned char c = (unsigned char)*s++;
        if (isalnum(c)) out[w++] = (char)tolower(c);
    }
    out[w] = 0;
}

static int ir_alias_match(const char *cmd_key, const char *aliases) {
    char alias[96], alias_key[96];
    const char *p = aliases, *start;
    size_t n;
    if (!cmd_key[0]) return 0;
    while (p && *p) {
        while (*p == '|') p++;
        start = p;
        while (*p && *p != '|') p++;
        n = (size_t)(p - start);
        if (n >= sizeof(alias)) n = sizeof(alias) - 1;
        memcpy(alias, start, n);
        alias[n] = 0;
        ir_command_key(alias, alias_key, sizeof(alias_key));
        if (alias_key[0] && strcmp(cmd_key, alias_key) == 0) return 1;
        if (strlen(alias_key) > 4 && strstr(cmd_key, alias_key) != NULL) return 1;
    }
    return 0;
}

static const struct ir_command *ir_find_remote_command(const struct ir_device *dev, const char *aliases) {
    int i;
    char key[160];
    for (i = 0; i < dev->command_count; i++) {
        ir_command_key(dev->commands[i].name, key, sizeof(key));
        if (ir_alias_match(key, aliases)) return &dev->commands[i];
    }
    return NULL;
}

static int ir_command_has_remote_button(const struct ir_command *cmd) {
    size_t i;
    char key[160];
    ir_command_key(cmd->name, key, sizeof(key));
    for (i = 0; i < sizeof(IR_REMOTE_BUTTONS) / sizeof(IR_REMOTE_BUTTONS[0]); i++) {
        if (ir_alias_match(key, IR_REMOTE_BUTTONS[i].aliases)) return 1;
    }
    return 0;
}

static void ir_send_hidden_inputs(FILE *f, const char *device_id, const char *command) {
    fprintf(f, "<input type='hidden' name='deviceId' value='");
    html(f, device_id);
    fprintf(f, "'><input type='hidden' name='command' value='");
    html(f, command);
    fprintf(f, "'>");
}

static void ir_quick_send_button(FILE *f, const struct ir_device *dev, const struct ir_command *cmd) {
    fprintf(f, "<form class='ir-send-form' method='post' action='/ir/send#ir'>");
    ir_send_hidden_inputs(f, dev->id, cmd->name);
    fprintf(f, "<button type='submit'>");
    html(f, cmd->name);
    fprintf(f, "</button></form>");
}

static void ir_remote_hotspot(FILE *f, const struct ir_device *dev, const struct ir_remote_button *button) {
    const struct ir_command *cmd = ir_find_remote_command(dev, button->aliases);
    if (cmd) {
        fprintf(f, "<form class='ir-send-form remote-hotspot' method='post' action='/ir/send#ir' data-label='");
        html(f, button->label);
        fprintf(f, "' title='Send ");
        html(f, cmd->name);
        fprintf(f, "' style='left:%.2f%%;top:%.2f%%;width:%.2f%%;height:%.2f%%'>",
            button->x, button->y, button->w, button->h);
        ir_send_hidden_inputs(f, dev->id, cmd->name);
        fprintf(f, "<button type='submit'><span class='sr-only'>Send ");
        html(f, cmd->name);
        fprintf(f, "</span></button></form>");
    } else {
        fprintf(f, "<span class='remote-hotspot disabled' title='No saved command for ");
        html(f, button->label);
        fprintf(f, "' style='left:%.2f%%;top:%.2f%%;width:%.2f%%;height:%.2f%%'></span>",
            button->x, button->y, button->w, button->h);
    }
}

static void ir_render_unmapped_commands(FILE *f, const struct ir_device *dev) {
    int i, mapped = 0, unmapped = 0;
    for (i = 0; i < dev->command_count; i++) {
        if (ir_command_has_remote_button(&dev->commands[i])) mapped++;
        else unmapped++;
    }
    fprintf(f, "<div class='ir-unmapped'><div class='muted mini'>Remote photo maps %d of %d saved commands.</div>", mapped, dev->command_count);
    if (unmapped > 0) {
        fprintf(f, "<h3>Other saved buttons</h3><div class='help'>Send buttons not shown on the photo.</div><div class='ir-quick-grid'>");
        for (i = 0; i < dev->command_count; i++) {
            if (!ir_command_has_remote_button(&dev->commands[i])) {
                ir_quick_send_button(f, dev, &dev->commands[i]);
            }
        }
        fprintf(f, "</div>");
    } else if (dev->command_count > 0) {
        fprintf(f, "<div class='help'>Every saved command has a matching button on this remote skin.</div>");
    }
    fprintf(f, "</div>");
}

static void ir_render_remote_skin(FILE *f, const struct ir_device *dev) {
    size_t i;
    fprintf(f, "<div class='ir-remote-card'><h3>Remote control</h3><div class='ir-remote-shell'><div class='ir-remote-skin'><img data-remote-skin alt='Harmony remote control layout'>");
    for (i = 0; i < sizeof(IR_REMOTE_BUTTONS) / sizeof(IR_REMOTE_BUTTONS[0]); i++) {
        ir_remote_hotspot(f, dev, &IR_REMOTE_BUTTONS[i]);
    }
    fprintf(f, "</div></div><div class='help'>Tap highlights to send buttons.</div>");
    ir_render_unmapped_commands(f, dev);
    fprintf(f, "</div>");
}

static void ir_render_device_workspace(FILE *f, const struct ir_device *dev, int active) {
    int j, quick_count = dev->command_count <= 80 ? dev->command_count : 80;
    fprintf(f, "<div class='ir-device-workspace%s' data-ir-device='", active ? " active" : "");
    html(f, dev->id);
    fprintf(f, "'><div class='ir-work-head'><div><h3>");
    html(f, dev->name[0] ? dev->name : "Unnamed device");
    fprintf(f, "</h3><div class='muted mini'>");
    html(f, dev->manufacturer);
    fprintf(f, " ");
    html(f, dev->model);
    fprintf(f, " / ");
    html(f, dev->type);
    fprintf(f, " / %d saved commands</div></div><div class='actions'><button type='button' class='secondary' data-next-step='library'>Add from database</button><button type='button' class='ghost' data-next-step='learn'>Learn button</button></div></div>", dev->command_count);
    fprintf(f, "<div class='ir-status muted mini' data-ir-status='");
    html(f, dev->id);
    fprintf(f, "'>Ready.</div>");
    fprintf(f, "<div class='ir-control-layout'>");
    ir_render_remote_skin(f, dev);
    fprintf(f, "<div class='ir-command-tools'><div><label>Find saved command</label><input class='ir-command-filter' data-command-filter='");
    html(f, dev->id);
    fprintf(f, "' placeholder='power, hdmi, volume, menu'></div>");
    fprintf(f, "<div><h3>All saved buttons</h3><div class='ir-quick-grid' data-command-list='");
    html(f, dev->id);
    fprintf(f, "'>");
    for (j = 0; j < quick_count; j++) ir_quick_send_button(f, dev, &dev->commands[j]);
    if (quick_count == 0) fprintf(f, "<div class='muted mini'>No commands saved yet. Search a database or learn buttons below.</div>");
    fprintf(f, "</div>");
    if (dev->command_count > quick_count) {
        fprintf(f, "<div class='help'>Showing the first %d commands. Use the full command list below to send or edit the rest.</div>", quick_count);
    }
    fprintf(f, "</div></div></div></div>");
}

static void ir_render_device_editor(FILE *f, const struct ir_device *dev, int active) {
    int j;
    fprintf(f, "<div class='ir-device-workspace%s' data-ir-device='", active ? " active" : "");
    html(f, dev->id);
    fprintf(f, "'><div class='ir-work-head'><div><h3>");
    html(f, dev->name[0] ? dev->name : "Unnamed device");
    fprintf(f, "</h3><div class='muted mini'>");
    html(f, dev->manufacturer);
    fprintf(f, " ");
    html(f, dev->model);
    fprintf(f, " / ");
    html(f, dev->type);
    fprintf(f, " / %d saved commands</div></div><div class='actions'><button type='button' class='secondary' data-next-step='library'>Add commands from database</button><button type='button' class='ghost' data-next-step='learn'>Learn button</button><button type='button' class='ghost' data-view-target='control'>Open remote control</button></div></div>", dev->command_count);
    fprintf(f, "<h3>Device details</h3><form method='post' action='/ir/device#ir'><input type='hidden' name='deviceId' value='");
    html(f, dev->id);
    fprintf(f, "'><div class='row'><div><label>Name</label><input name='name' value='");
    html(f, dev->name);
    fprintf(f, "'></div><div><label>Type</label><input name='type' value='");
    html(f, dev->type);
    fprintf(f, "'></div></div><div class='row'><div><label>Manufacturer</label><input name='manufacturer' value='");
    html(f, dev->manufacturer);
    fprintf(f, "'></div><div><label>Model</label><input name='model' value='");
    html(f, dev->model);
    fprintf(f, "'></div></div><div class='actions'><button type='submit'>Save device details</button></div></form>");
    fprintf(f, "<form method='post' action='/ir/delete-device#ir'><input type='hidden' name='deviceId' value='");
    html(f, dev->id);
    fprintf(f, "'><div class='actions'><button class='danger' type='submit'>Delete device</button></div></form>");
    fprintf(f, "<h3 style='margin-top:16px'>Commands</h3><div class='help'>Add, edit, or delete saved commands.</div>");
    ir_command_editor(f, dev->id, NULL, 0);
    fprintf(f, "<details open><summary>Saved commands</summary><div class='command-list mini' data-command-list='");
    html(f, dev->id);
    fprintf(f, "'>");
    for (j = 0; j < dev->command_count; j++) {
        const struct ir_command *cmd = &dev->commands[j];
        fprintf(f, "<div class='ir-command-row' data-command-name='");
        html(f, cmd->name);
        fprintf(f, "'><div><strong>");
        html(f, cmd->name);
        fprintf(f, "</strong><div class='muted'>Protocol %d / learned from remote: %s</div><div class='muted'>",
            cmd->protocol_id, cmd->learned ? "yes" : "no");
        if (cmd->has_raw) fprintf(f, "raw timing recording");
        else html(f, cmd->keycode);
        fprintf(f, "</div></div><div class='actions'>");
        ir_quick_send_button(f, dev, cmd);
        fprintf(f, "<form method='post' action='/ir/delete-command#ir'><input type='hidden' name='deviceId' value='");
        html(f, dev->id);
        fprintf(f, "'><input type='hidden' name='command' value='");
        html(f, cmd->name);
        fprintf(f, "'><button class='danger' type='submit'>Delete</button></form></div>");
        ir_command_editor(f, dev->id, cmd, 1);
        fprintf(f, "</div>");
    }
    if (dev->command_count == 0) fprintf(f, "<div class='muted'>No commands saved yet.</div>");
    fprintf(f, "</div></details></div>");
}

static void ir_setup_flow(FILE *f, const struct ir_inventory *inv) {
    fprintf(f, "<div class='setup-shell'><div class='wizard-top'><div><h3>Add or find remote commands</h3><div class='subtle'>Save the device, find codes, learn missing buttons, then test.</div></div><span class='pill'>%d devices</span></div>", inv->device_count);
    fprintf(f, "<div class='wizard-grid'><div class='stepper'>");
    fprintf(f, "<button type='button' class='step active' data-step-target='device'><span>1</span><div>Device<div class='subtle'>What it is</div></div></button>");
    fprintf(f, "<button type='button' class='step' data-step-target='library'><span>2</span><div>Search<div class='subtle'>Find codes</div></div></button>");
    fprintf(f, "<button type='button' class='step' data-step-target='learn'><span>3</span><div>Learn<div class='subtle'>Record remote</div></div></button>");
    fprintf(f, "<button type='button' class='step' data-step-target='verify'><span>4</span><div>Test<div class='subtle'>Try buttons</div></div></button>");
    fprintf(f, "</div><div class='wizard-body'>");

    fprintf(f, "<div class='wizard-panel active' data-panel='device'><div class='callout'><strong>Start with the device profile.</strong>Enter brand and model. Search uses this.</div><form id='profileDeviceForm' method='post' action='/ir/new-device#ir'>");
    fprintf(f, "<div class='row'><div><label>Device name</label><input id='newDeviceName' name='name' required placeholder='Living Room TV'></div><div><label>Device type</label><select id='newDeviceType' name='type'><option value='HomeAppliance'>Home appliance</option><option value='Amplifier'>Receiver / amplifier</option><option value='Television'>Television</option><option value='Media Player'>Media player</option><option value='Game Console'>Game console</option></select></div></div>");
    fprintf(f, "<div class='row'><div><label>Manufacturer</label><input id='newDeviceManufacturer' name='manufacturer' required placeholder='Pioneer'></div><div><label>Model</label><input id='newDeviceModel' name='model' required placeholder='VSX-D1'></div></div>");
    fprintf(f, "<div id='profileStatus' class='wizard-status mini'></div>");
    fprintf(f, "<div class='actions'><button type='submit'>Save device and search</button><button id='profileSearch' type='button' class='secondary'>Search without saving</button><button type='button' class='ghost' data-next-step='learn'>Learn from remote</button></div></form></div>");

    fprintf(f, "<div class='wizard-panel' data-panel='learn'><div class='callout'><strong>Learn only when search does not find the button.</strong>Point the original remote at the hub, capture the button, test it here, then save it when it works.</div><form id='wizardLearnForm' method='post' action='/ir/command#ir'>");
    fprintf(f, "<label>Device</label><select id='wizardDevice' name='deviceId'>");
    ir_device_options(f, inv, NULL);
    fprintf(f, "</select><label>Command name</label><input id='wizardCommandName' name='name' required placeholder='Power Toggle'>");
    fprintf(f, "<div class='row'><div><label>Save format</label><select id='wizardMode' name='mode'><option value='auto' selected>Choose automatically</option><option value='keycode'>Harmony compact code</option><option value='nec'>NEC 32-bit code</option><option value='raw'>Raw timing recording</option></select></div>");
    fprintf(f, "<div><label>Protocol</label><select id='wizardProtocol' name='protocol'><option value='2'>NEC-compatible</option><option value='679'>MemorexO1 32 Bit</option></select></div></div>");
    fprintf(f, "<label>NEC hex code</label><input id='wizardNec' name='nec' placeholder='E0E040BF'>");
    fprintf(f, "<label>Harmony compact code</label><input id='wizardKeycode' name='keycode' placeholder='G:Toshiba 32 Bit:(0xE0E040BF)(Repeat)():3'>");
    fprintf(f, "<label>Captured signal data</label><textarea id='wizardRaw' name='raw' placeholder=''></textarea>");
    fprintf(f, "<div id='wizardLearnStatus' class='wizard-status mini'></div>");
    fprintf(f, "<div class='actions'><button id='wizardCapture' type='button'>Capture from remote</button><button id='wizardLearnTest' type='button' class='secondary'>Test captured button</button><button type='submit' class='secondary'>Save command</button><button type='button' class='ghost' data-next-step='library'>Search databases</button><button type='button' class='ghost' data-next-step='verify'>Test saved commands</button></div></form></div>");

    fprintf(f, "<div class='wizard-panel' data-panel='verify'><div class='callout'><strong>Test before adding lots of commands.</strong>Send one saved command and confirm the real device reacts the way you expect.</div>");
    fprintf(f, "<div class='device-sync'><div><label>Device</label><select id='verifyDevice'>");
    ir_device_options(f, inv, NULL);
    fprintf(f, "</select></div><button id='wizardTest' type='button'>Send test command</button></div>");
    fprintf(f, "<label>Command</label><select id='verifyCommand'></select>");
    fprintf(f, "<div id='wizardVerifyStatus' class='wizard-status mini'></div>");
    fprintf(f, "<div class='actions'><button type='button' class='secondary' data-next-step='learn'>Learn another button</button><button type='button' class='ghost' data-next-step='library'>Search more codes</button></div></div>");

    fprintf(f, "<div class='wizard-panel' data-panel='library'><div class='callout'><strong>Search code databases before learning.</strong>Try the manufacturer and model first. Choose a match, load its commands, then test one or two before saving a large set.</div><form id='irdbImportForm' method='post' action='/ir/irdb-import#ir'>");
    fprintf(f, "<label>Save commands to</label><select id='irdbDevice' name='deviceId'>");
    ir_device_options(f, inv, NULL);
    fprintf(f, "</select><label>Database source</label><select id='irdbSource'><option value='all'>All databases</option><option value='irdb'>probonopd/irdb CSV</option><option value='flipper'>Flipper-IRDB .ir</option><option value='lirc'>LIRC remotes</option><option value='smartir'>SmartIR JSON</option><option value='remotecentral'>RemoteCentral Pronto hex</option><option value='custom'>Pasted file or URL</option></select>");
    fprintf(f, "<label>Search terms</label><input id='irdbSearch' list='irdbMatches' placeholder='Pioneer VSX-D1, LG C5, Samsung TV, BeoVision'><datalist id='irdbMatches'></datalist>");
    fprintf(f, "<div id='irdbResults' class='preview hidden'></div>");
    fprintf(f, "<label>Selected file, page, or URL</label><input id='irdbPath' placeholder='Samsung/TV/7,7.csv, TVs/Samsung/Samsung_TV.ir, /cgi-bin/codes/pioneer/receiver/, or a raw URL'>");
    fprintf(f, "<label>Paste IR codes or file contents</label><textarea id='irdbPaste' placeholder='Paste Flipper .ir, LIRC, Pronto Hex, Global Cache sendir, GIRR XML, BroadLink, .irc, CSV, or JSON'></textarea><input id='irdbFile' type='file' accept='.ir,.conf,.lircd,.lirc,.txt,.xml,.girr,.irc,.csv,.json'>");
    fprintf(f, "<label>Command name prefix</label><input id='irdbPrefix' placeholder='TV '>");
    fprintf(f, "<textarea id='irdbPayload' name='payload' class='hidden'></textarea>");
    fprintf(f, "<div id='irdbStatus' class='muted mini'></div><div id='irdbPreview' class='preview hidden'></div><label>Search and import log</label><pre id='irdbLog' class='mini' style='max-height:170px'>Ready.</pre>");
    fprintf(f, "<div class='actions'><button id='irdbLoadIndex' type='button' class='secondary'>Search databases</button><button id='irdbFetch' type='button' class='secondary'>Load selected file</button><button id='irdbParsePaste' type='button' class='secondary'>Parse pasted codes</button><button type='submit'>Save selected commands</button><button type='button' class='ghost' data-next-step='learn'>Learn from remote</button></div></form></div>");
    fprintf(f, "</div></div></div>");
}

static void ir_control_panel(FILE *f) {
    struct ir_inventory *inv = (struct ir_inventory *)calloc(1, sizeof(*inv));
    int i;
    if (!inv) return;
    fprintf(f, "<section id='view-control' data-view='control' class='section'><div class='section-head'><div><h2>Remote control</h2><div class='section-lead'>Choose a saved device, then send its buttons.</div></div></div>");
    if (load_ir_inventory(inv) != 0) {
        fprintf(f, "<div class='panel'><pre>Unable to read ");
        html(f, DEVICE_LIST);
        fprintf(f, "</pre></div></section>");
        destroy_ir_inventory(inv);
        return;
    }
    if (inv->device_count > 0) {
        fprintf(f, "<div class='ir-stored-layout'><div class='panel'><h3>Stored remotes</h3><div class='muted mini'>Choose a saved remote.</div><div class='ir-device-picks'>");
        for (i = 0; i < inv->device_count; i++) {
            const struct ir_device *dev = &inv->devices[i];
            fprintf(f, "<button type='button' class='ir-device-pick%s' data-ir-device-pick='",
                i == 0 ? " active" : "");
            html(f, dev->id);
            fprintf(f, "'><div><strong>");
            html(f, dev->name[0] ? dev->name : "Unnamed device");
            fprintf(f, "</strong><div class='muted mini'>");
            html(f, dev->manufacturer);
            fprintf(f, " ");
            html(f, dev->model);
            fprintf(f, "</div></div><span class='badge'>%d</span></button>", dev->command_count);
        }
        fprintf(f, "</div></div><div class='panel'>");
        for (i = 0; i < inv->device_count; i++) {
            ir_render_device_workspace(f, &inv->devices[i], i == 0);
        }
        fprintf(f, "</div></div>");
    } else {
        fprintf(f, "<div class='panel'><h3>No stored remotes yet</h3><div class='muted'>Create a device in IR Setup, search a code database, or learn commands from an existing remote.</div><div class='actions'><button type='button' data-view-target='ir'>Open IR Setup</button></div></div>");
    }
    fprintf(f, "</section>");
    destroy_ir_inventory(inv);
}

static void ir_panel(FILE *f) {
    struct ir_inventory *inv = (struct ir_inventory *)calloc(1, sizeof(*inv));
    int i;
    if (!inv) return;
    fprintf(f, "<section id='view-ir' data-view='ir' class='section'><div class='section-head'><div><h2>IR setup</h2><div class='section-lead'>Create or edit devices, find codes, and learn missing buttons.</div></div></div>");
    if (load_ir_inventory(inv) != 0) {
        fprintf(f, "<div class='panel'><pre>Unable to read ");
        html(f, DEVICE_LIST);
        fprintf(f, "</pre></div></section>");
        destroy_ir_inventory(inv);
        return;
    }
    ir_setup_flow(f, inv);
    if (inv->device_count > 0) {
        fprintf(f, "<div class='ir-stored-layout'><div class='panel'><h3>Edit existing devices</h3><div class='muted mini'>Choose a saved device to edit details or commands.</div><div class='ir-device-picks'>");
        for (i = 0; i < inv->device_count; i++) {
            const struct ir_device *dev = &inv->devices[i];
            fprintf(f, "<button type='button' class='ir-device-pick%s' data-ir-device-pick='",
                i == 0 ? " active" : "");
            html(f, dev->id);
            fprintf(f, "'><div><strong>");
            html(f, dev->name[0] ? dev->name : "Unnamed device");
            fprintf(f, "</strong><div class='muted mini'>");
            html(f, dev->manufacturer);
            fprintf(f, " ");
            html(f, dev->model);
            fprintf(f, "</div></div><span class='badge'>%d</span></button>", dev->command_count);
        }
        fprintf(f, "</div></div><div class='panel'>");
        for (i = 0; i < inv->device_count; i++) {
            ir_render_device_editor(f, &inv->devices[i], i == 0);
        }
        fprintf(f, "</div></div>");
    } else {
        fprintf(f, "<div class='panel'><h3>No stored remotes yet</h3><div class='muted'>Create a device above, search a code database, or learn commands from an existing remote.</div></div>");
    }
    fprintf(f, "</section>");
    destroy_ir_inventory(inv);
}

static void ir_lab_panel(FILE *f) {
    struct ir_inventory *inv = (struct ir_inventory *)calloc(1, sizeof(*inv));
    if (!inv) return;
    fprintf(f, "<section id='view-lab' data-view='lab' class='section'><div class='section-head'><div><h2>Bulk IR test</h2><div class='section-lead'>Search code libraries, load matching commands into a queue, then save or send them at a controlled pace.</div></div><span class='pill'>up to %d saved commands per device</span></div>", MAX_IR_STORED_COMMANDS);
    if (load_ir_inventory(inv) != 0) {
        fprintf(f, "<div class='panel'><pre>Unable to read ");
        html(f, DEVICE_LIST);
        fprintf(f, "</pre></div></section>");
        destroy_ir_inventory(inv);
        return;
    }
    fprintf(f, "<div class='lab-layout'><div class='panel'><h3>Find command files</h3><div class='callout'><strong>Search first, send second.</strong>Start with a device, brand, model, or database path. Search shows matching files; Load commands reads those files and adds usable, non-duplicate commands to the queue.</div>");
    fprintf(f, "<div class='lab-toolbar'><div><label>Device to save or test on</label><select id='labDevice'><option value='__auto_lab__' selected>Temporary test device</option>");
    ir_device_options(f, inv, NULL);
    fprintf(f, "</select></div><div><label>Database source</label><select id='labSource'><option value='all'>All databases</option><option value='irdb'>probonopd/irdb</option><option value='flipper'>Flipper-IRDB</option><option value='lirc'>LIRC remotes</option><option value='smartir'>SmartIR JSON</option><option value='remotecentral'>RemoteCentral</option></select></div></div>");
    fprintf(f, "<div class='row'><div><label>Device, brand, model, or path</label><input id='labPathFilter' placeholder='air conditioner, Pioneer, Samsung/TV'></div><div><label>Button names to include</label><input id='labCommandFilter' placeholder='off, power off, standby'></div></div>");
    fprintf(f, "<div class='lab-presets'><button id='labOffFilter' type='button' class='ghost'>Power off</button><button id='labPowerFilter' type='button' class='ghost'>Any power</button><button id='labVolumeFilter' type='button' class='ghost'>Volume</button><button id='labInputFilter' type='button' class='ghost'>Inputs</button><button id='labClearFilter' type='button' class='ghost'>Clear</button></div>");
    fprintf(f, "<details class='lab-advanced'><summary>Search limits and timing</summary><label>RemoteCentral page path</label><input id='labRcPath' placeholder='Optional direct page, e.g. /cgi-bin/codes/pioneer/receiver/'>");
    fprintf(f, "<div class='lab-toolbar'><div><label>Files to scan per click</label><input id='labMaxFiles' inputmode='numeric' value='600'></div><div><label>Parallel file reads</label><input id='labWorkers' inputmode='numeric' value='14'></div><div><label>Maximum commands in queue</label><input id='labMaxCommands' inputmode='numeric' value='900'></div><div><label>Delay between file reads (ms)</label><input id='labFetchDelay' inputmode='numeric' value='0'></div></div>");
    fprintf(f, "<div class='lab-toolbar'><div><label>Delay between IR sends (ms)</label><input id='labSendDelay' inputmode='numeric' value='80'></div><div><label>Commands sent per request</label><input id='labBatchSize' inputmode='numeric' value='100'></div></div>");
    fprintf(f, "<label class='inline-check'><input id='labDryRun' type='checkbox'> Dry run (do not send IR)</label></details>");
    fprintf(f, "<div class='actions'><button id='labCandidatesBtn' type='button' class='secondary'>Find matching files</button><button id='labScan' type='button'>Load commands</button><button id='labStored' type='button' class='secondary'>Use saved commands</button><button id='labClear' type='button' class='ghost'>Clear search and queue</button></div>");
    fprintf(f, "<div id='labStatus' class='wizard-status mini'></div><div class='meter'><span id='labMeter'></span></div><div id='labCandidates' class='preview hidden' style='margin-top:10px'></div></div>");
    fprintf(f, "<div class='panel'><h3>Command queue</h3><div class='help'>Selected commands are saved if needed, then sent in small groups so Stop can cancel quickly. Duplicate IR codes are skipped even when databases use different names.</div><div class='lab-summary'><span id='labSummary'>0 queued</span><span>send group limit %d</span></div><div class='queue-tools'><button id='labSelectAll' type='button' class='ghost'>Select all</button><button id='labSelectNone' type='button' class='ghost'>Select none</button><button id='labDropUnchecked' type='button' class='ghost'>Remove unchecked</button></div><div id='labQueue' class='queue-list'><div class='muted mini'>No commands queued.</div></div>", MAX_IR_BATCH_COMMANDS);
    fprintf(f, "<div class='actions'><button id='labImport' type='button' class='secondary'>Save selected</button><button id='labRun' type='button'>Send selected</button><button id='labStop' type='button' class='danger'>Stop sending</button></div>");
    fprintf(f, "<pre id='labLog' class='mini' style='margin-top:12px;max-height:180px'>Ready.</pre></div></div></section>");
    destroy_ir_inventory(inv);
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
    char *raw;
    const char *key, *arr, *arr_end, *dpos;
    memset(inv, 0, sizeof(*inv));
    raw = read_file_alloc(BT_DEVICE_STORE, MAX_RESOURCE_FILE, NULL);
    if (!raw) return 0;
    key = strstr(raw, "\"devices\"");
    arr = key ? strchr(key, '[') : NULL;
    arr_end = arr ? find_matching_json(arr, '[', ']') : NULL;
    if (!arr || !arr_end) {
        free(raw);
        return 0;
    }
    dpos = arr + 1;
    while ((dpos = strchr(dpos, '{')) != NULL && dpos < arr_end && inv->device_count < MAX_BT_DEVICES) {
        struct bt_saved_device *dev = &inv->devices[inv->device_count];
        const char *dev_end = find_matching_json(dpos, '{', '}');
        const char *cmd_key, *cmd_arr, *cmd_end, *cpos;
        if (!dev_end || dev_end > arr_end) break;
        json_string_range(dpos, dev_end, "id", dev->id, sizeof(dev->id));
        json_string_range(dpos, dev_end, "name", dev->name, sizeof(dev->name));
        json_string_range(dpos, dev_end, "type", dev->type, sizeof(dev->type));
        json_string_range(dpos, dev_end, "bdaddr", dev->bdaddr, sizeof(dev->bdaddr));
        if (!dev->id[0] || !dev->name[0]) {
            dpos = dev_end + 1;
            continue;
        }
        if (!dev->type[0]) strcpy(dev->type, "btkeyboard");
        cmd_key = strstr(dpos, "\"commands\"");
        cmd_arr = (cmd_key && cmd_key < dev_end) ? strchr(cmd_key, '[') : NULL;
        cmd_end = cmd_arr ? find_matching_json(cmd_arr, '[', ']') : NULL;
        if (cmd_arr && cmd_end && cmd_end < dev_end) {
            cpos = cmd_arr + 1;
            while ((cpos = strchr(cpos, '{')) != NULL && cpos < cmd_end && dev->command_count < MAX_BT_COMMANDS) {
                struct bt_saved_command *cmd = &dev->commands[dev->command_count];
                const char *obj_end = find_matching_json(cpos, '{', '}');
                if (!obj_end || obj_end > cmd_end) break;
                json_string_range(cpos, obj_end, "name", cmd->name, sizeof(cmd->name));
                json_string_range(cpos, obj_end, "script", cmd->script, sizeof(cmd->script));
                cmd->delay_ms = (int)json_long_range(cpos, obj_end, "delayMs", 35);
                if (cmd->delay_ms < 15) cmd->delay_ms = 35;
                if (cmd->delay_ms > 5000) cmd->delay_ms = 5000;
                if (cmd->name[0] && cmd->script[0]) dev->command_count++;
                cpos = obj_end + 1;
            }
        }
        inv->device_count++;
        dpos = dev_end + 1;
    }
    free(raw);
    return 0;
}

static int save_bt_inventory(const struct bt_inventory *inv) {
    char tmp[256];
    FILE *f;
    int i, j;
    snprintf(tmp, sizeof(tmp), "%s.new", BT_DEVICE_STORE);
    f = fopen(tmp, "wb");
    if (!f) return -1;
    fputs("{\"version\":1,\"devices\":[", f);
    for (i = 0; i < inv->device_count; i++) {
        const struct bt_saved_device *dev = &inv->devices[i];
        if (i) fputc(',', f);
        fputs("{\"id\":", f); json_write_string(f, dev->id);
        fputs(",\"name\":", f); json_write_string(f, dev->name);
        fputs(",\"type\":", f); json_write_string(f, dev->type);
        fputs(",\"bdaddr\":", f); json_write_string(f, dev->bdaddr);
        fputs(",\"commands\":[", f);
        for (j = 0; j < dev->command_count; j++) {
            const struct bt_saved_command *cmd = &dev->commands[j];
            if (j) fputc(',', f);
            fputs("{\"name\":", f); json_write_string(f, cmd->name);
            fputs(",\"delayMs\":", f); fprintf(f, "%d", cmd->delay_ms);
            fputs(",\"script\":", f); json_write_string(f, cmd->script);
            fputc('}', f);
        }
        fputs("]}", f);
    }
    fputs("]}\n", f);
    if (fclose(f) != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, BT_DEVICE_STORE) != 0) {
        unlink(tmp);
        return -1;
    }
    chmod(BT_DEVICE_STORE, 0644);
    sync();
    return 0;
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

static int delete_bt_command(const char *device_id, const char *name, char *msg, size_t msglen) {
    struct bt_inventory inv;
    struct bt_saved_device *dev;
    int didx, cidx, i;
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
    for (i = cidx; i + 1 < dev->command_count; i++) dev->commands[i] = dev->commands[i + 1];
    dev->command_count--;
    backup_settings();
    if (save_bt_inventory(&inv) != 0) {
        snprintf(msg, msglen, "Failed to delete Bluetooth command.");
        return -1;
    }
    snprintf(msg, msglen, "Deleted Bluetooth command %s.", name);
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

static const char *bt_type_label(const char *type) {
    if (strcmp(type, "btkeyboard-nexus") == 0) return "Nexus keyboard";
    if (strcmp(type, "fire") == 0) return "Fire TV / media keys";
    if (strcmp(type, "ps3") == 0) return "PlayStation 3";
    if (strcmp(type, "wii") == 0) return "Nintendo Wii";
    return "Standard keyboard";
}

static void bt_type_options(FILE *f, const char *selected) {
    const char *types[] = {"btkeyboard", "btkeyboard-nexus", "fire", "ps3", "wii"};
    size_t i;
    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        fprintf(f, "<option value='");
        html(f, types[i]);
        fprintf(f, "'%s>", selected && strcmp(selected, types[i]) == 0 ? " selected" : "");
        html(f, bt_type_label(types[i]));
        fprintf(f, "</option>");
    }
}

static void bt_device_options(FILE *f, const struct bt_inventory *inv) {
    int i;
    for (i = 0; i < inv->device_count; i++) {
        fprintf(f, "<option value='");
        html(f, inv->devices[i].id);
        fprintf(f, "'>");
        html(f, inv->devices[i].name);
        fprintf(f, " (");
        html(f, inv->devices[i].bdaddr);
        fprintf(f, ")</option>");
    }
}

static void bt_command_editor(FILE *f, const struct bt_saved_device *dev, const struct bt_saved_command *cmd, int edit) {
    fprintf(f, "<details class='command-editor'><summary>%s</summary><form method='post' action='/bt/command#bluetooth'>",
        edit ? "Edit keyboard command" : "Add keyboard command");
    fprintf(f, "<input type='hidden' name='deviceId' value='");
    html(f, dev->id);
    fprintf(f, "'>");
    if (edit) {
        fprintf(f, "<input type='hidden' name='oldName' value='");
        html(f, cmd->name);
        fprintf(f, "'>");
    }
    fprintf(f, "<div class='row'><div><label>Command name</label><input name='name' required value='");
    html(f, edit ? cmd->name : "");
    fprintf(f, "' placeholder='Open YouTube, Home, Search'></div><div><label>Pause between keys (ms)</label><input name='delayMs' inputmode='numeric' value='%d'></div></div>",
        edit ? cmd->delay_ms : 35);
    fprintf(f, "<label>Keyboard script</label><textarea name='script' class='textarea-tall' spellcheck='false' required placeholder='TEXT hello from harmony&#10;KEY enter&#10;COMBO ctrl+l'>");
    if (edit) html(f, cmd->script);
    fprintf(f, "</textarea><div class='help'>Use TEXT words, KEY enter, COMBO ctrl+l, WAIT 500. TEXT lines are sent through the accurate FIFO helper.</div><div class='actions'><button type='submit'>%s</button></div></form></details>",
        edit ? "Save command changes" : "Add command");
}

static void bt_saved_devices_panel(FILE *f, const struct bt_inventory *inv) {
    int i, j;
    fprintf(f, "<div class='panel bt-saved'><h3>Saved Bluetooth devices</h3><div class='help'>Store paired targets here, then save reusable keyboard scripts as commands. These devices behave like IR devices: you can send, edit, add, or delete commands.</div>");
    if (inv->device_count == 0) {
        fprintf(f, "<div class='muted mini'>No Bluetooth devices saved yet. Pair a target above, refresh status, then save it here.</div></div>");
        return;
    }
    for (i = 0; i < inv->device_count; i++) {
        const struct bt_saved_device *dev = &inv->devices[i];
        fprintf(f, "<div class='bt-device-card'><h4>");
        html(f, dev->name);
        fprintf(f, "</h4><div class='muted mini'>");
        html(f, bt_type_label(dev->type));
        fprintf(f, " / ");
        html(f, dev->bdaddr);
        fprintf(f, " / id ");
        html(f, dev->id);
        fprintf(f, "</div><form method='post' action='/bt/device#bluetooth'><input type='hidden' name='deviceId' value='");
        html(f, dev->id);
        fprintf(f, "'><div class='row'><div><label>Name</label><input name='name' value='");
        html(f, dev->name);
        fprintf(f, "' required></div><div><label>Keyboard type</label><select name='type'>");
        bt_type_options(f, dev->type);
        fprintf(f, "</select></div></div><label>Bluetooth address</label><input name='bdaddr' value='");
        html(f, dev->bdaddr);
        fprintf(f, "' required><div class='actions'><button type='submit'>Save device details</button></div></form>");
        fprintf(f, "<form method='post' action='/bt/delete-device#bluetooth'><input type='hidden' name='deviceId' value='");
        html(f, dev->id);
        fprintf(f, "'><div class='actions'><button class='danger' type='submit'>Delete device</button></div></form>");
        bt_command_editor(f, dev, NULL, 0);
        fprintf(f, "<div class='command-list mini'>");
        for (j = 0; j < dev->command_count; j++) {
            const struct bt_saved_command *cmd = &dev->commands[j];
            fprintf(f, "<div class='command'><div><strong>");
            html(f, cmd->name);
            fprintf(f, "</strong><div class='muted'>Keyboard script / %d ms between keys</div><pre class='mini command-preview'>", cmd->delay_ms);
            html(f, cmd->script);
            fprintf(f, "</pre></div><div class='actions'>");
            fprintf(f, "<form method='post' action='/bt/send-command#bluetooth'><input type='hidden' name='deviceId' value='");
            html(f, dev->id);
            fprintf(f, "'><input type='hidden' name='command' value='");
            html(f, cmd->name);
            fprintf(f, "'><button type='submit'>Run command</button></form>");
            fprintf(f, "<form method='post' action='/bt/delete-command#bluetooth'><input type='hidden' name='deviceId' value='");
            html(f, dev->id);
            fprintf(f, "'><input type='hidden' name='command' value='");
            html(f, cmd->name);
            fprintf(f, "'><button class='danger' type='submit'>Delete command</button></form></div>");
            bt_command_editor(f, dev, cmd, 1);
            fprintf(f, "</div>");
        }
        if (dev->command_count == 0) fprintf(f, "<div class='muted mini'>No keyboard commands saved for this device yet.</div>");
        fprintf(f, "</div></div>");
    }
    fprintf(f, "</div>");
}

static void bluetooth_panel(FILE *f) {
    struct bt_inventory btinv;
    const struct bt_quick_key commands[] = {
        {"directionup", "Up"}, {"directiondown", "Down"}, {"directionleft", "Left"}, {"directionright", "Right"},
        {"enter", "Enter"}, {"escape", "Escape"}, {"back", "Back"}, {"menu", "Menu"}, {"home", "Home"}, {"end", "End"}, {"insert", "Insert"},
        {"space", "Space"}, {"tab", "Tab"}, {"backspace", "Backspace"}, {"delete", "Delete"}, {"pageup", "Page Up"}, {"pagedown", "Page Down"},
        {"number1", "1"}, {"number2", "2"}, {"number3", "3"}, {"number4", "4"}, {"number5", "5"},
        {"number6", "6"}, {"number7", "7"}, {"number8", "8"}, {"number9", "9"}, {"number0", "0"},
        {"f1", "F1"}, {"f2", "F2"}, {"f3", "F3"}, {"f4", "F4"}, {"f5", "F5"}, {"f6", "F6"}, {"f7", "F7"}, {"f8", "F8"},
        {"f9", "F9"}, {"f10", "F10"}, {"f11", "F11"}, {"f12", "F12"}, {"ctrll", "Ctrl+L"}, {"ctrlr", "Ctrl+R"}, {"ctrlw", "Ctrl+W"},
        {"ctrlc", "Ctrl+C"}, {"ctrlv", "Ctrl+V"}, {"ctrlx", "Ctrl+X"}, {"alttab", "Alt+Tab"}, {"altf4", "Alt+F4"}, {"ctrlaltdelete", "Ctrl+Alt+Delete"}
    };
    size_t i;
    load_bt_inventory(&btinv);
    fprintf(f, "<section id='view-bluetooth' data-view='bluetooth' class='section'><div class='section-head'><div><h2>Bluetooth keyboard</h2><div class='section-lead'>Make the hub appear as a Bluetooth keyboard, pair it from the device you want to control, then send keys or scripts.</div></div><span class='pill'>Keyboard mode</span></div>");
    fprintf(f, "<div class='bt-layout'><div class='panel'><h3>Pair a device</h3><div class='guide-steps'><div class='guide-step'><b>1</b><div><strong>Start pairing mode here.</strong><div class='muted mini'>The hub becomes visible as the name below.</div></div></div><div class='guide-step'><b>2</b><div><strong>Pair from the target device.</strong><div class='muted mini'>Open Bluetooth settings on the TV, computer, console, or media box and choose the hub.</div></div></div><div class='guide-step'><b>3</b><div><strong>Refresh status, then send keys.</strong><div class='muted mini'>The status box should show a connected target before scripts run.</div></div></div></div>");
    fprintf(f, "<div class='row'><div><label>Name shown during pairing</label><input id='btName' value='Harmony Keyboard' maxlength='48'></div><div><label>Keyboard type</label><select id='btType'><option value='btkeyboard' selected>Standard keyboard</option><option value='btkeyboard-nexus'>Nexus keyboard</option><option value='fire'>Fire TV / media keys</option><option value='ps3'>PlayStation 3</option><option value='wii'>Nintendo Wii</option></select></div></div>");
    fprintf(f, "<div class='actions'><button id='btPairingOn' type='button'>Start pairing mode</button><button id='btAdapterStatus' type='button' class='secondary'>Refresh status</button><button id='btPairingOff' type='button' class='danger'>Stop pairing mode</button></div>");
    fprintf(f, "<div class='help'>After pairing, Refresh status usually finds the connected device automatically. The status box also shows technical details for troubleshooting.</div><pre id='btPairStatus' class='mini' style='margin-top:12px'>Pairing status has not been refreshed yet.</pre>");
    fprintf(f, "<details class='lab-advanced'><summary>Manual connection tools</summary><div class='row'><div><label>Bluetooth address</label><input id='btAddr' placeholder='AA:BB:CC:DD:EE:FF'></div><div><label>PIN if requested</label><input id='btPin' inputmode='numeric' placeholder='Optional legacy PIN'></div></div><div class='row'><div><label>Search time (seconds)</label><input id='btTimeout' inputmode='numeric' value='8'></div><div></div></div><div class='actions'><button id='btClassicScan' type='button' class='secondary'>Find classic devices</button><button id='btScan' type='button' class='secondary'>Find BLE devices</button><button id='btStatus' type='button' class='secondary'>Check connection</button><button id='btConnect' type='button' class='secondary'>Connect</button><button id='btDisconnect' type='button' class='danger'>Disconnect</button></div></details>");
    fprintf(f, "<form id='btSaveTargetForm' method='post' action='/bt/device#bluetooth'><input id='btSaveTargetName' type='hidden' name='name'><input id='btSaveTargetType' type='hidden' name='type'><input id='btSaveTargetAddr' type='hidden' name='bdaddr'><div class='actions'><button type='submit' class='secondary'>Save paired target</button></div><div class='help'>Saves the connected Bluetooth target so scripts can be stored as reusable commands.</div></form></div>");
    fprintf(f, "<div class='panel'><h3>Keyboard</h3><div class='help'>Click keys or enable forwarding to type from your physical keyboard. Shift / Ctrl / Alt / Win are sticky — click one then click the target key.</div>");
    fprintf(f, "<div class='actions kb-fwd'><button id='btFwdToggle' type='button' class='secondary'>Forward my keyboard</button><button id='btReleaseAll' type='button' class='danger' style='margin-left:auto'>Release all</button></div>");
    fprintf(f, "<div class='kb-panel'>");
    /* F-row */
    fprintf(f, "<div class='kb-row'>");
    fprintf(f, "<button class='kb-key kb-15' data-kb='escape'>Esc</button>");
    fprintf(f, "<button class='kb-key' data-kb='f1'>F1</button><button class='kb-key' data-kb='f2'>F2</button><button class='kb-key' data-kb='f3'>F3</button><button class='kb-key' data-kb='f4'>F4</button>");
    fprintf(f, "<button class='kb-key' data-kb='f5'>F5</button><button class='kb-key' data-kb='f6'>F6</button><button class='kb-key' data-kb='f7'>F7</button><button class='kb-key' data-kb='f8'>F8</button>");
    fprintf(f, "<button class='kb-key' data-kb='f9'>F9</button><button class='kb-key' data-kb='f10'>F10</button><button class='kb-key' data-kb='f11'>F11</button><button class='kb-key' data-kb='f12'>F12</button>");
    fprintf(f, "<button class='kb-key' data-kb='insert'>Ins</button><button class='kb-key' data-kb='delete'>Del</button>");
    fprintf(f, "</div>");
    /* Number row */
    fprintf(f, "<div class='kb-row'>");
    fprintf(f, "<button class='kb-key' data-kb='grave'>`</button>");
    fprintf(f, "<button class='kb-key' data-kb='number1'>1</button><button class='kb-key' data-kb='number2'>2</button><button class='kb-key' data-kb='number3'>3</button><button class='kb-key' data-kb='number4'>4</button><button class='kb-key' data-kb='number5'>5</button><button class='kb-key' data-kb='number6'>6</button><button class='kb-key' data-kb='number7'>7</button><button class='kb-key' data-kb='number8'>8</button><button class='kb-key' data-kb='number9'>9</button><button class='kb-key' data-kb='number0'>0</button>");
    fprintf(f, "<button class='kb-key' data-kb='minus'>-</button><button class='kb-key' data-kb='equal'>=</button>");
    fprintf(f, "<button class='kb-key kb-2' data-kb='backspace'>⌫ Back</button>");
    fprintf(f, "<button class='kb-key' data-kb='home'>Home</button>");
    fprintf(f, "</div>");
    /* QWERTY row */
    fprintf(f, "<div class='kb-row'>");
    fprintf(f, "<button class='kb-key kb-15' data-kb='tab'>Tab ⇥</button>");
    fprintf(f, "<button class='kb-key' data-kb='q'>Q</button><button class='kb-key' data-kb='w'>W</button><button class='kb-key' data-kb='e'>E</button><button class='kb-key' data-kb='r'>R</button><button class='kb-key' data-kb='t'>T</button><button class='kb-key' data-kb='y'>Y</button><button class='kb-key' data-kb='u'>U</button><button class='kb-key' data-kb='i'>I</button><button class='kb-key' data-kb='o'>O</button><button class='kb-key' data-kb='p'>P</button>");
    fprintf(f, "<button class='kb-key' data-kb='leftbracket'>[</button><button class='kb-key' data-kb='rightbracket'>]</button><button class='kb-key' data-kb='backslash'>\\</button>");
    fprintf(f, "<button class='kb-key' data-kb='end'>End</button>");
    fprintf(f, "</div>");
    /* ASDF row */
    fprintf(f, "<div class='kb-row'>");
    fprintf(f, "<button class='kb-key kb-2' data-kb='capslock'>Caps</button>");
    fprintf(f, "<button class='kb-key' data-kb='a'>A</button><button class='kb-key' data-kb='s'>S</button><button class='kb-key' data-kb='d'>D</button><button class='kb-key' data-kb='f'>F</button><button class='kb-key' data-kb='g'>G</button><button class='kb-key' data-kb='h'>H</button><button class='kb-key' data-kb='j'>J</button><button class='kb-key' data-kb='k'>K</button><button class='kb-key' data-kb='l'>L</button>");
    fprintf(f, "<button class='kb-key' data-kb='semicolon'>;</button><button class='kb-key' data-kb='apostrophe'>'</button>");
    fprintf(f, "<button class='kb-key kb-225' data-kb='enter'>Enter ↵</button>");
    fprintf(f, "<button class='kb-key' data-kb='pageup'>PgUp</button>");
    fprintf(f, "</div>");
    /* ZXCV row */
    fprintf(f, "<div class='kb-row'>");
    fprintf(f, "<button class='kb-key kb-225 kb-mod' data-kb='lshift'>⇧ Shift</button>");
    fprintf(f, "<button class='kb-key' data-kb='z'>Z</button><button class='kb-key' data-kb='x'>X</button><button class='kb-key' data-kb='c'>C</button><button class='kb-key' data-kb='v'>V</button><button class='kb-key' data-kb='b'>B</button><button class='kb-key' data-kb='n'>N</button><button class='kb-key' data-kb='m'>M</button>");
    fprintf(f, "<button class='kb-key' data-kb='comma'>,</button><button class='kb-key' data-kb='period'>.</button><button class='kb-key' data-kb='slash'>/</button>");
    fprintf(f, "<button class='kb-key kb-225 kb-mod' data-kb='rshift'>⇧ Shift</button>");
    fprintf(f, "<button class='kb-key' data-kb='up'>▲</button>");
    fprintf(f, "<button class='kb-key' data-kb='pagedown'>PgDn</button>");
    fprintf(f, "</div>");
    /* Bottom row: modifiers + space */
    fprintf(f, "<div class='kb-row'>");
    fprintf(f, "<button class='kb-key kb-15 kb-mod' data-kb='lctrl'>Ctrl</button>");
    fprintf(f, "<button class='kb-key kb-15 kb-mod' data-kb='lwin'>Win</button>");
    fprintf(f, "<button class='kb-key kb-2 kb-mod' data-kb='lalt'>Alt</button>");
    fprintf(f, "<button class='kb-key kb-sp' data-kb='space'>Space</button>");
    fprintf(f, "<button class='kb-key kb-2 kb-mod' data-kb='ralt'>Alt</button>");
    fprintf(f, "<button class='kb-key kb-15 kb-mod' data-kb='rwin'>Win</button>");
    fprintf(f, "<button class='kb-key kb-15 kb-mod' data-kb='rctrl'>Ctrl</button>");
    fprintf(f, "<button class='kb-key' data-kb='left'>◀</button><button class='kb-key' data-kb='down'>▼</button><button class='kb-key' data-kb='right'>▶</button>");
    fprintf(f, "</div>");
    fprintf(f, "</div>"); /* kb-panel */
    fprintf(f, "<input id='btCode' type='hidden' value=''>");
    fprintf(f, "<div class='actions' style='margin-top:10px'><button id='btEnterTest' type='button' class='secondary'>Test Enter</button></div>");
    fprintf(f, "<div class='save-script-box'><h3>Text block</h3><label>Text to type</label><textarea id='btTextBlock' class='textarea-tall' spellcheck='false' placeholder='Paste text here'></textarea><div class='actions'><button id='btTextSend' type='button'>Send text</button><button id='btTextClear' type='button' class='secondary'>Clear</button></div></div>");
    fprintf(f, "<div class='callout' style='margin-top:14px'><strong>Reliable text sending</strong> The background keyboard helper types TEXT script lines as exact press-and-release pairs. If it is not running, the page falls back to slower key reports.</div><div class='actions'><button id='btRuntimeRefresh' type='button' class='secondary'>Check text helper</button></div><pre id='btRuntimeStatus' class='mini'>Text helper status has not loaded yet.</pre>");
    fprintf(f, "<div class='save-script-box'><h3>Mouse mode</h3><div class='help'>The current Harmony Bluetooth HAL rejects mouse HID profile and mouse report probes, so a real trackpad cannot be enabled from this firmware path yet.</div><div class='preview' style='height:180px;display:grid;place-items:center;color:var(--muted)'>Mouse HID unavailable</div></div>");
    fprintf(f, "<h3 style='margin-top:18px'>Keyboard script</h3><div class='bt-script-layout'><div><label>Script</label><textarea id='btScript' class='textarea-tall' spellcheck='false' placeholder='TEXT hello from harmony&#10;WAIT 300&#10;KEY enter&#10;COMBO ctrl+l&#10;TEXT https://home-assistant.io&#10;KEY enter'></textarea></div><div class='bt-script-tools'><div class='callout'><strong>Script examples</strong>TEXT words, KEY enter, COMBO ctrl+l, WAIT 500. Bare key names are treated as KEY lines. TEXT uses the accurate text helper when available.</div><label>Pause between keys (ms)</label><input id='btScriptDelay' inputmode='numeric' value='35'><div class='actions'><button id='btScriptPreviewBtn' type='button' class='secondary'>Preview script</button><button id='btScriptRun' type='button'>Run script</button><button id='btScriptStop' type='button' class='danger'>Stop script</button></div>");
    if (btinv.device_count > 0) {
        fprintf(f, "<div class='save-script-box'><label>Save this script to</label><select id='btScriptDevice'>");
        bt_device_options(f, &btinv);
        fprintf(f, "</select><label>Command name</label><input id='btScriptCommandName' placeholder='Open YouTube'><div class='actions'><button id='btSaveScriptCommand' type='button' class='secondary'>Save as command</button></div></div>");
    }
    fprintf(f, "</div></div><pre id='btScriptPreview' class='mini' style='margin-top:12px'>Paste a script to preview or run.</pre></div></div>");
    fprintf(f, "<div class='panel'><h3>Bluetooth log</h3><pre id='btLog' class='mini'>Ready. Start pairing mode, then pair from the target device.</pre></div>");
    bt_saved_devices_panel(f, &btinv);
    fprintf(f, "</section>");
}

static void system_panel(FILE *f) {
    char info[8192], logs[8192];
    int cloud_blocked = load_cloud_blocker();
    struct webui_auth_config auth;
    load_webui_auth(&auth);
    run_cmd("echo '--- uname ---'; uname -a; echo; echo '--- memory ---'; cat /proc/meminfo; echo; echo '--- mounts ---'; mount; echo; echo '--- processes ---'; ps", info, sizeof(info));
    run_cmd("echo '--- startup log ---'; cat /cache/codex-init.log 2>/dev/null; echo; echo '--- recovery log ---'; cat /cache/codex-recovery.log 2>/dev/null; echo; echo '--- local service syslog ---'; logread 2>/dev/null | grep -i 'codex\\|mqtt' 2>/dev/null", logs, sizeof(logs));
    fprintf(f, "<section id='view-system' data-view='system' class='section'><div class='section-head'><div><h2>System</h2><div class='section-lead'>Check device information, read logs, update the web interface, or reboot when a saved change needs it.</div></div></div>");
    fprintf(f, "<div class='grid'><details><summary>System information</summary><pre>");
    html(f, info);
    fprintf(f, "</pre></details><details><summary>Logs</summary><pre>");
    html(f, logs[0] ? logs : "no matching logs");
    fprintf(f, "</pre></details></div>");
    fprintf(f, "<div class='panel' style='margin-top:12px'><h3>Cloud blocker</h3><div class='help'>Block Logitech cloud background services while keeping local web, MQTT, Bluetooth, Wi-Fi recovery, and SSH control available. The change is saved immediately and is applied by the Harmony network service after reboot or network reconnect.</div><form method='post' action='/system#system'>");
    fprintf(f, "<label class='inline-check'><input type='checkbox' name='cloudBlocker' value='1' %s> Block Logitech cloud services</label>", cloud_blocked ? "checked" : "");
    fprintf(f, "<div class='help'>Current saved mode: <strong>%s</strong>. Blocking prevents cloudapi, PubNub, and package manager tasks from starting.</div>", cloud_blocked ? "blocked" : "allowed after restart");
    fprintf(f, "<div class='actions'><button name='action' value='cloud' type='submit'>Save cloud blocker setting</button><button name='action' value='cloud_reboot' type='submit' class='secondary'>Save and reboot</button></div></form></div>");
    fprintf(f, "<div class='panel' style='margin-top:12px'><h3>Web UI sign-in</h3><div class='help'>Optional HTTP Basic authentication for every page, API call, and export. Leave it off for a trusted local-only hub, or enable it when the hub is reachable by guests or other devices.</div><form method='post' action='/system#system' autocomplete='off'>");
    fprintf(f, "<label class='inline-check'><input type='checkbox' name='authEnabled' value='1' %s> Require username and password</label>", auth.enabled ? "checked" : "");
    fprintf(f, "<div class='grid two'><div><label>Username</label><input name='authUsername' autocomplete='username' required value='");
    html(f, auth.username[0] ? auth.username : "admin");
    fprintf(f, "'><div class='help'>Use plain text without a colon.</div></div><div><label>New password</label><input name='authPassword' type='password' autocomplete='new-password' placeholder='Leave blank to keep current password'><div class='help'>Required the first time you enable sign-in.</div></div></div>");
    fprintf(f, "<div class='help'>Current mode: <strong>%s</strong>.</div>", auth.enabled ? "sign-in required" : "open on local network");
    fprintf(f, "<div class='actions'><button name='action' value='auth' type='submit'>Save sign-in setting</button></div></form></div>");
    fprintf(f, "<div class='panel' style='margin-top:12px'><h3>Software update</h3><div class='help'>Check the public release files, copy newer binaries to the hub, verify checksums, and restart the local services. SSH access is not changed. The default public repository tries GitHub and CDN mirrors without a token. Use the token field only for private repositories.</div><form id='updateForm' autocomplete='off' onsubmit='return false'><div class='grid two'><div><label for='updateRepo'>Optional update mirror URL</label><input id='updateRepo' autocomplete='url' value='https://raw.githubusercontent.com/Ripthulhu/harmony-hub-control/main/payload/bin/'></div><div><label for='updateToken'>GitHub token (private repos only)</label><input id='updateToken' type='password' autocomplete='new-password' placeholder='optional; used only by this browser'></div></div><div class='actions'><button id='updateCheck' type='button' class='secondary'>Check for updates</button><button id='updateInstall' type='button'>Install update</button><button id='updateRefresh' type='button' class='secondary'>Show installed versions</button></div></form><pre id='updateLog' class='mini'>Ready. Check the public repo, or paste a token if the repo is private.</pre></div><div class='panel' style='margin-top:12px'><div class='help'>Refresh Home Assistant discovery if new devices or commands do not appear after changes.</div><form method='post' action='/system#system'><div class='actions'><button name='action' value='rediscover' type='submit'>Refresh Home Assistant discovery</button><button name='action' value='reboot' type='submit' class='secondary'>Reboot hub</button></div></form></div></section>");
}

static void render_page(int fd, const char *message) {
    struct mqtt_config mqtt;
    struct wifi_config wifi;
    char *body = NULL;
    size_t body_len = 0;
    char hdr[200];
    /* Buffer the page so we can send a Content-Length. If a render is ever cut
     * short (e.g. an out-of-memory kill), no headers are emitted and the client
     * sees a reset/short read instead of silently rendering a half page. */
    FILE *f = open_memstream(&body, &body_len);
    if (!f) return;
    load_mqtt(&mqtt);
    load_wifi(&wifi);
    page_head(f, "Harmony Hub Control");
    if (message && message[0]) {
        fprintf(f, "<div class='msg'>");
        html(f, message);
        fprintf(f, "</div>");
    }
    status_panel(f, &mqtt);
    fprintf(f, "<section id='view-mqtt' data-view='mqtt' class='section'><div class='section-head'><div><h2>MQTT</h2><div class='section-lead'>Connect the hub to Home Assistant through MQTT. The hub can publish its state and listen for activity or IR commands.</div></div></div><div class='grid'>");
    mqtt_form(f, &mqtt);
    fprintf(f, "</div></section>");
    fprintf(f, "<section id='view-wifi' data-view='wifi' class='section'><div class='section-head'><div><h2>Wi-Fi</h2><div class='section-lead'>Change the network the hub joins. If the saved Wi-Fi stops working, hold the reset button to start the recovery access point.</div></div></div><div class='grid'>");
    wifi_form(f, &wifi);
    fprintf(f, "</div></section>");
    ir_control_panel(f);
    ir_panel(f);
    ir_lab_panel(f);
    bluetooth_panel(f);
    backup_panel(f);
    system_panel(f);
    page_end(f);
    if (fclose(f) != 0 || !body) { free(body); return; }
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
        (unsigned long)body_len);
    send_all(fd, hdr, strlen(hdr));
    send_all(fd, body, body_len);
    free(body);
}

static void handle_mqtt(int fd, const struct request *req) {
    struct mqtt_config old, cfg;
    char tmp[256];
    load_mqtt(&old);
    cfg = old;
    cfg.enabled = form_checked(req->body, "enabled");
    cfg.ha_discovery = form_checked(req->body, "haDiscovery");
    form_value(req->body, "host", cfg.host, sizeof(cfg.host));
    form_value(req->body, "username", cfg.username, sizeof(cfg.username));
    form_value(req->body, "baseTopic", cfg.base_topic, sizeof(cfg.base_topic));
    form_value(req->body, "discoveryPrefix", cfg.discovery_prefix, sizeof(cfg.discovery_prefix));
    form_value(req->body, "clientId", cfg.client_id, sizeof(cfg.client_id));
    form_value(req->body, "name", cfg.name, sizeof(cfg.name));
    form_value(req->body, "port", tmp, sizeof(tmp));
    cfg.port = atoi(tmp);
    if (cfg.port <= 0) cfg.port = 1883;
    form_value(req->body, "pollSeconds", tmp, sizeof(tmp));
    cfg.poll_seconds = atoi(tmp);
    if (cfg.poll_seconds <= 0) cfg.poll_seconds = 10;
    form_value(req->body, "keepAlive", tmp, sizeof(tmp));
    cfg.keep_alive = atoi(tmp);
    if (cfg.keep_alive <= 0) cfg.keep_alive = 60;
    form_value(req->body, "password", tmp, sizeof(tmp));
    if (tmp[0] || !form_checked(req->body, "keep_password")) {
        strncpy(cfg.password, tmp, sizeof(cfg.password) - 1);
        cfg.password[sizeof(cfg.password) - 1] = 0;
    }
    if (save_mqtt(&cfg) == 0) {
        trigger_mqtt_discover();
        render_page(fd, "MQTT settings saved. The bridge will reconnect when it notices the config change.");
    } else {
        render_page(fd, "Failed to save MQTT settings.");
    }
}

static void handle_wifi(int fd, const struct request *req) {
    struct wifi_config old, cfg;
    char tmp[256], apply[32];
    load_wifi(&old);
    cfg = old;
    form_value(req->body, "ssid", cfg.ssid, sizeof(cfg.ssid));
    form_value(req->body, "password", tmp, sizeof(tmp));
    cfg.hidden = form_checked(req->body, "hidden");
    cfg.open = form_checked(req->body, "open");
    if (tmp[0] || !form_checked(req->body, "keep_password")) {
        strncpy(cfg.psk, tmp, sizeof(cfg.psk) - 1);
        cfg.psk[sizeof(cfg.psk) - 1] = 0;
    }
    if (!cfg.ssid[0]) {
        render_page(fd, "Wi-Fi SSID is required.");
        return;
    }
    if (!cfg.open && !cfg.psk[0]) {
        render_page(fd, "Wi-Fi password is required unless Open network is checked.");
        return;
    }
    if (save_wifi(&cfg) != 0) {
        render_page(fd, "Failed to save Wi-Fi settings.");
        return;
    }
    form_value(req->body, "apply", apply, sizeof(apply));
    if (strcmp(apply, "reboot") == 0) {
        render_page(fd, "Wi-Fi settings saved. Rebooting now.");
        sync();
        system("/sbin/reboot >/dev/null 2>&1 &");
    } else {
        render_page(fd, "Wi-Fi settings saved. Reboot when ready to use them.");
    }
}

static void handle_system(int fd, const struct request *req) {
    char action[64];
    form_value(req->body, "action", action, sizeof(action));
    if (strcmp(action, "reboot") == 0) {
        render_page(fd, "Rebooting now.");
        sync();
        system("/sbin/reboot >/dev/null 2>&1 &");
    } else if (strcmp(action, "cloud") == 0 || strcmp(action, "cloud_reboot") == 0) {
        int enabled = form_checked(req->body, "cloudBlocker");
        if (save_cloud_blocker(enabled) != 0) {
            render_page(fd, "Failed to save cloud blocker setting.");
            return;
        }
        if (strcmp(action, "cloud_reboot") == 0) {
            render_page(fd, enabled ? "Cloud blocker enabled. Rebooting now." : "Cloud blocker disabled. Rebooting now.");
            sync();
            system("/sbin/reboot >/dev/null 2>&1 &");
        } else {
            render_page(fd, enabled ? "Cloud blocker enabled. Reboot when ready to apply it." : "Cloud blocker disabled. Reboot when ready to allow Logitech cloud services.");
        }
    } else if (strcmp(action, "auth") == 0) {
        struct webui_auth_config old, cfg;
        char username[64], password[128];
        load_webui_auth(&old);
        cfg = old;
        cfg.enabled = form_checked(req->body, "authEnabled");
        form_value(req->body, "authUsername", username, sizeof(username));
        form_value(req->body, "authPassword", password, sizeof(password));
        if (username[0]) snprintf(cfg.username, sizeof(cfg.username), "%s", username);
        if (password[0]) snprintf(cfg.password, sizeof(cfg.password), "%s", password);
        if (!safe_auth_field(cfg.username, 0)) {
            render_page(fd, "Username is required and cannot contain a colon.");
            return;
        }
        if (cfg.enabled && !cfg.password[0]) {
            render_page(fd, "Enter a password before enabling web UI sign-in.");
            return;
        }
        if (cfg.password[0] && !safe_auth_field(cfg.password, 1)) {
            render_page(fd, "Password cannot contain control characters.");
            return;
        }
        if (save_webui_auth(&cfg) != 0) {
            render_page(fd, "Failed to save web UI sign-in setting.");
            return;
        }
        render_page(fd, cfg.enabled ? "Web UI sign-in enabled. Your browser may ask you to sign in again on the next page load." : "Web UI sign-in disabled.");
    } else if (strcmp(action, "rediscover") == 0) {
        trigger_mqtt_discover();
        render_page(fd, "MQTT discovery reload requested.");
    } else {
        render_page(fd, "Unknown system action.");
    }
}

static char *trim_payload(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static int looks_like_json_object(const char *s) {
    const char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s != '{') return 0;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    return end > s && end[-1] == '}';
}

static const char *import_path_for_target(const char *target) {
    if (strcmp(target, "devices") == 0) return DEVICE_LIST;
    if (strcmp(target, "functions") == 0) return FUNCTION_LIST;
    if (strcmp(target, "protocols") == 0) return PROTOCOL_LIST;
    if (strcmp(target, "mqtt") == 0) return MQTT_CONFIG;
    if (strcmp(target, "wifi") == 0) return WPA_CONFIG;
    if (strcmp(target, "cloud") == 0) return CLOUD_BLOCKER_CONFIG;
    if (strcmp(target, "bluetooth") == 0) return BT_DEVICE_STORE;
    return NULL;
}

static const char *import_label_for_target(const char *target) {
    if (strcmp(target, "bundle") == 0) return "backup bundle";
    if (strcmp(target, "devices") == 0) return "DeviceList.json";
    if (strcmp(target, "functions") == 0) return "FunctionList.json";
    if (strcmp(target, "protocols") == 0) return "ProtocolList.json";
    if (strcmp(target, "mqtt") == 0) return "MQTT config";
    if (strcmp(target, "wifi") == 0) return "Wi-Fi config";
    if (strcmp(target, "cloud") == 0) return "cloud blocker setting";
    if (strcmp(target, "bluetooth") == 0) return "Bluetooth devices";
    return "import";
}

static int validate_import_payload(const char *target, const char *payload, char *msg, size_t msglen) {
    if (!payload[0]) {
        snprintf(msg, msglen, "Import payload is empty.");
        return -1;
    }
    if (strcmp(target, "bundle") == 0) {
        if (looks_like_json_object(payload) && strstr(payload, "harmony-owner-bundle-v1") && strstr(payload, "\"DeviceList.json\"")) return 0;
        snprintf(msg, msglen, "Bundle import must be a harmony-owner-bundle-v1 JSON export.");
        return -1;
    }
    if (strcmp(target, "wifi") == 0) {
        if (strstr(payload, "network={") && strstr(payload, "ssid=")) return 0;
        snprintf(msg, msglen, "Wi-Fi import must look like a wpa_supplicant config with a network block and ssid.");
        return -1;
    }
    if (strcmp(target, "cloud") == 0) {
        if (cloud_value_known(payload)) return 0;
        snprintf(msg, msglen, "Cloud blocker import must be 1, 0, on, off, true, false, enabled, or disabled.");
        return -1;
    }
    if (strcmp(target, "bluetooth") == 0) {
        if (looks_like_json_object(payload) && strstr(payload, "\"devices\"")) return 0;
        snprintf(msg, msglen, "Bluetooth devices import must be a JSON object with a devices list.");
        return -1;
    }
    if (!looks_like_json_object(payload)) {
        snprintf(msg, msglen, "%s import must be a JSON object.", import_label_for_target(target));
        return -1;
    }
    if (strcmp(target, "devices") == 0 && !strstr(payload, "\"DevicesWithFeatures\"")) {
        snprintf(msg, msglen, "DeviceList import must contain DevicesWithFeatures.");
        return -1;
    }
    if (strcmp(target, "functions") == 0 && !strstr(payload, "\"FunctionMaps\"")) {
        snprintf(msg, msglen, "FunctionList import must contain FunctionMaps.");
        return -1;
    }
    if (strcmp(target, "protocols") == 0 && !strstr(payload, "\"Protocols\"")) {
        snprintf(msg, msglen, "ProtocolList import must contain Protocols.");
        return -1;
    }
    if (strcmp(target, "mqtt") == 0 && (!strstr(payload, "\"broker\"") || !strstr(payload, "\"baseTopic\""))) {
        snprintf(msg, msglen, "MQTT import must contain broker and baseTopic.");
        return -1;
    }
    return 0;
}

static int bundle_extract(const char *bundle, const char *key, char *out, size_t outlen, char *msg, size_t msglen) {
    if (!json_string(bundle, key, out, outlen) || !out[0]) {
        snprintf(msg, msglen, "Bundle is missing %s.", key);
        return -1;
    }
    return 0;
}

static void handle_import_bundle(int fd, const char *payload) {
    char msg[256];
    char cloud[32] = "";
    char *cloud_value;
    char *devices, *functions, *protocols, *mqtt, *wifi, *bluetooth;
    devices = (char *)malloc(MAX_REQUEST_BODY);
    functions = (char *)malloc(MAX_REQUEST_BODY);
    protocols = (char *)malloc(MAX_REQUEST_BODY);
    mqtt = (char *)malloc(MAX_REQUEST_BODY);
    wifi = (char *)malloc(MAX_REQUEST_BODY);
    bluetooth = (char *)malloc(MAX_REQUEST_BODY);
    if (!devices || !functions || !protocols || !mqtt || !wifi || !bluetooth) {
        free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
        render_page(fd, "Not enough memory to import bundle.");
        return;
    }
    bluetooth[0] = 0;
    if (bundle_extract(payload, "DeviceList.json", devices, MAX_REQUEST_BODY, msg, sizeof(msg)) != 0 ||
        bundle_extract(payload, "FunctionList.json", functions, MAX_REQUEST_BODY, msg, sizeof(msg)) != 0 ||
        bundle_extract(payload, "ProtocolList.json", protocols, MAX_REQUEST_BODY, msg, sizeof(msg)) != 0 ||
        bundle_extract(payload, "mqtt-config.json", mqtt, MAX_REQUEST_BODY, msg, sizeof(msg)) != 0 ||
        bundle_extract(payload, "wpa_supplicant.conf", wifi, MAX_REQUEST_BODY, msg, sizeof(msg)) != 0) {
        render_page(fd, msg);
        free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
        return;
    }
    if (validate_import_payload("devices", devices, msg, sizeof(msg)) != 0 ||
        validate_import_payload("functions", functions, msg, sizeof(msg)) != 0 ||
        validate_import_payload("protocols", protocols, msg, sizeof(msg)) != 0 ||
        validate_import_payload("mqtt", mqtt, msg, sizeof(msg)) != 0 ||
        validate_import_payload("wifi", wifi, msg, sizeof(msg)) != 0) {
        render_page(fd, msg);
        free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
        return;
    }
    json_string(payload, "bt-devices.json", bluetooth, MAX_REQUEST_BODY);
    if (bluetooth[0] && validate_import_payload("bluetooth", bluetooth, msg, sizeof(msg)) != 0) {
        render_page(fd, msg);
        free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
        return;
    }
    json_string(payload, "cloud-blocker.conf", cloud, sizeof(cloud));
    cloud_value = trim_payload(cloud);
    if (cloud_value[0] && validate_import_payload("cloud", cloud_value, msg, sizeof(msg)) != 0) {
        render_page(fd, msg);
        free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
        return;
    }
    backup_resources();
    backup_settings();
    if (write_file_atomic(DEVICE_LIST, devices, strlen(devices)) != 0 ||
        write_file_atomic(FUNCTION_LIST, functions, strlen(functions)) != 0 ||
        write_file_atomic(PROTOCOL_LIST, protocols, strlen(protocols)) != 0 ||
        write_file_atomic(MQTT_CONFIG, mqtt, strlen(mqtt)) != 0 ||
        write_file_atomic(WPA_CONFIG, wifi, strlen(wifi)) != 0) {
        render_page(fd, "Failed to import owner bundle.");
        free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
        return;
    }
    chmod(MQTT_CONFIG, 0600);
    chmod(WPA_CONFIG, 0600);
    if (bluetooth[0] && write_file_atomic(BT_DEVICE_STORE, bluetooth, strlen(bluetooth)) != 0) {
        render_page(fd, "Bundle imported most files, but failed to save Bluetooth devices.");
        free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
        return;
    }
    if (bluetooth[0]) chmod(BT_DEVICE_STORE, 0644);
    if (cloud_value[0] && save_cloud_blocker(cloud_value_enabled(cloud_value)) != 0) {
        render_page(fd, "Bundle imported most files, but failed to save the cloud blocker setting.");
        free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
        return;
    }
    request_resource_reload();
    render_page(fd, "Backup bundle imported. Reboot when ready if Wi-Fi settings changed.");
    free(devices); free(functions); free(protocols); free(mqtt); free(wifi); free(bluetooth);
}

static void handle_import(int fd, const struct request *req) {
    char target[64], msg[256];
    char *payload_buf, *payload;
    const char *path;
    size_t len;
    if (req->body_truncated) {
        render_page(fd, "Import payload was too large for this device-side form. Use a smaller file or import one resource at a time.");
        return;
    }
    form_value(req->body, "target", target, sizeof(target));
    path = import_path_for_target(target);
    if (!path && strcmp(target, "bundle") != 0) {
        render_page(fd, "Unknown import target.");
        return;
    }
    payload_buf = (char *)malloc(MAX_REQUEST_BODY);
    if (!payload_buf) {
        render_page(fd, "Not enough memory to receive import payload.");
        return;
    }
    form_value(req->body, "payload", payload_buf, MAX_REQUEST_BODY);
    payload = trim_payload(payload_buf);
    if (validate_import_payload(target, payload, msg, sizeof(msg)) != 0) {
        render_page(fd, msg);
        free(payload_buf);
        return;
    }
    if (strcmp(target, "bundle") == 0) {
        handle_import_bundle(fd, payload);
        free(payload_buf);
        return;
    }
    len = strlen(payload);
    if (strcmp(target, "devices") == 0 || strcmp(target, "functions") == 0 || strcmp(target, "protocols") == 0) {
        backup_resources();
    } else {
        backup_settings();
    }
    if (write_file_atomic(path, payload, len) != 0) {
        snprintf(msg, sizeof(msg), "Failed to import %s.", import_label_for_target(target));
        render_page(fd, msg);
        free(payload_buf);
        return;
    }
    if (strcmp(target, "mqtt") == 0) {
        chmod(MQTT_CONFIG, 0600);
        trigger_mqtt_discover();
        render_page(fd, "MQTT settings imported. The bridge will reconnect when it notices the config change.");
    } else if (strcmp(target, "wifi") == 0) {
        chmod(WPA_CONFIG, 0600);
        render_page(fd, "Wi-Fi settings imported. Reboot when ready to use them.");
    } else if (strcmp(target, "cloud") == 0) {
        if (save_cloud_blocker(cloud_value_enabled(payload)) != 0) {
            render_page(fd, "Failed to import cloud blocker setting.");
        } else {
            render_page(fd, "Cloud blocker setting imported. Reboot when ready to apply it.");
        }
    } else if (strcmp(target, "bluetooth") == 0) {
        chmod(BT_DEVICE_STORE, 0644);
        render_page(fd, "Bluetooth devices imported.");
    } else {
        request_resource_reload();
        snprintf(msg, sizeof(msg), "Imported %s and requested a Harmony resource reload.", import_label_for_target(target));
        render_page(fd, msg);
    }
    free(payload_buf);
}

static void handle_ir_send(int fd, const struct request *req) {
    char device_id[64], command[128], reply[4096], message[4608];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "command", command, sizeof(command));
    if (!safe_label(device_id) || !safe_label(command)) {
        render_page(fd, "Invalid IR command request.");
        return;
    }
    repair_known_protocols_for_current_commands();
    send_ir_command_action(device_id, command, reply, sizeof(reply));
    snprintf(message, sizeof(message), "Sent %s to %s. Reply: %s", command, device_id, reply[0] ? reply : "no response");
    render_page(fd, message);
}

static void render_ir_send_json(int fd, const struct request *req) {
    char device_id[64], command[128], reply[4096];
    FILE *f;
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "command", command, sizeof(command));
    if (!safe_label(device_id) || !safe_label(command)) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"invalid IR command request\"}\n", f);
        fclose(f);
        return;
    }
    repair_known_protocols_for_current_commands();
    send_ir_command_action_ex(device_id, command, "api", "", reply, sizeof(reply));
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"deviceId\":", f); json_write_string(f, device_id);
    fputs(",\"command\":", f); json_write_string(f, command);
    fputs(",\"reply\":", f); json_write_string(f, reply[0] ? reply : "no response");
    fputs("}\n", f);
    fclose(f);
}

static void render_ir_batch_send_json(int fd, const struct request *req) {
    char device_id[64], delay_text[32], dry_text[16], run_id[128], reply[1024], last_reply[1024];
    char *commands, *line, *save;
    int delay_ms, dry_run, sent = 0, skipped = 0, attempted = 0, failed = 0, canceled = 0;
    struct timeval start, end;
    FILE *f;
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "delayMs", delay_text, sizeof(delay_text));
    form_value(req->body, "dryRun", dry_text, sizeof(dry_text));
    form_value(req->body, "runId", run_id, sizeof(run_id));
    if (!safe_label(device_id)) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"invalid IR batch device\"}\n", f);
        fclose(f);
        return;
    }
    if (run_id[0] && !safe_run_id(run_id)) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"invalid IR run id\"}\n", f);
        fclose(f);
        return;
    }
    dry_run = strcmp(dry_text, "1") == 0 || strcasecmp(dry_text, "true") == 0;
    delay_ms = atoi(delay_text);
    if (delay_ms < 40) delay_ms = 40;
    if (delay_ms > 10000) delay_ms = 10000;
    if (!dry_run) repair_known_protocols_for_current_commands();
    commands = (char *)malloc(MAX_REQUEST_BODY);
    if (!commands) {
        f = send_json_start(fd, "500 Internal Server Error");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"not enough memory for IR batch\"}\n", f);
        fclose(f);
        return;
    }
    form_value(req->body, "commands", commands, MAX_REQUEST_BODY);
    gettimeofday(&start, NULL);
    last_reply[0] = 0;
    line = strtok_r(commands, "\n", &save);
    while (line && attempted < MAX_IR_BATCH_COMMANDS) {
        char *cmd_name = trim_in_place(line);
        size_t n = strlen(cmd_name);
        if (ir_run_canceled(run_id)) {
            canceled = 1;
            break;
        }
        while (n && cmd_name[n - 1] == '\r') cmd_name[--n] = 0;
        if (!cmd_name[0] || !safe_label(cmd_name)) {
            skipped++;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        attempted++;
        if (!dry_run) {
            send_ir_command_action_ex(device_id, cmd_name, "api-batch", run_id, reply, sizeof(reply));
            copy_text(last_reply, sizeof(last_reply), reply[0] ? reply : "no response");
            if (strstr(reply, "\"code\":500") || strstr(reply, "Invalid command")) failed++;
            if (delay_ms > 0 && cancelable_sleep_ms(delay_ms, run_id)) {
                canceled = 1;
                sent++;
                break;
            }
        }
        sent++;
        line = strtok_r(NULL, "\n", &save);
    }
    while (line) {
        skipped++;
        line = strtok_r(NULL, "\n", &save);
    }
    gettimeofday(&end, NULL);
    free(commands);
    if (sent == 0 && !canceled) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"no valid commands selected\"}\n", f);
        fclose(f);
        return;
    }
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"deviceId\":", f); json_write_string(f, device_id);
    fputs(",\"runId\":", f); json_write_string(f, run_id);
    fprintf(f, ",\"dryRun\":%s,\"canceled\":%s,\"sent\":%d,\"skipped\":%d,\"attempted\":%d,\"failed\":%d,\"delayMs\":%d,\"elapsedMs\":%ld,\"lastReply\":",
        dry_run ? "true" : "false", canceled ? "true" : "false", sent, skipped, attempted, failed, delay_ms,
        (long)((end.tv_sec - start.tv_sec) * 1000L + (end.tv_usec - start.tv_usec) / 1000L));
    json_write_string(f, last_reply);
    fputs("}\n", f);
    fclose(f);
}

static void render_ir_cancel_json(int fd, const struct request *req) {
    char run_id[128];
    FILE *f;
    form_value(req->body, "runId", run_id, sizeof(run_id));
    if (!safe_run_id(run_id)) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"invalid IR run id\"}\n", f);
        fclose(f);
        return;
    }
    if (mark_ir_run_canceled(run_id) != 0) {
        f = send_json_start(fd, "500 Internal Server Error");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"unable to mark IR run canceled\"}\n", f);
        fclose(f);
        return;
    }
    log_ir_note_event("ir_cancel", "api", run_id, "cancel requested");
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"runId\":", f); json_write_string(f, run_id);
    fputs(",\"canceled\":true}\n", f);
    fclose(f);
}

static void render_ir_lab_target_json(int fd) {
    char device_id[64], msg[512];
    int created = 0;
    int rc = ensure_lab_target_device(device_id, sizeof(device_id), &created, msg, sizeof(msg));
    FILE *f = send_json_start(fd, rc == 0 ? "200 OK" : "500 Internal Server Error");
    if (!f) return;
    fputs("{\"ok\":", f); fputs(rc == 0 ? "true" : "false", f);
    fputs(",\"deviceId\":", f); json_write_string(f, rc == 0 ? device_id : "");
    fputs(",\"name\":\"Temporary IR Test\",\"created\":", f); fputs(created ? "true" : "false", f);
    fputs(",\"message\":", f); json_write_string(f, msg);
    fputs("}\n", f);
    fclose(f);
}

static void render_ir_lab_clear_json(int fd, const struct request *req) {
    char device_id[64], lab_id[64], msg[512];
    int rc;
    FILE *f;
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    rc = ensure_lab_target_device(lab_id, sizeof(lab_id), NULL, msg, sizeof(msg));
    if (rc != 0) {
        f = send_json_start(fd, "500 Internal Server Error");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":", f); json_write_string(f, msg);
        fputs("}\n", f);
        fclose(f);
        return;
    }
    if (!device_id[0]) copy_text(device_id, sizeof(device_id), lab_id);
    if (strcmp(device_id, lab_id) != 0) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"clear only supports the temporary sweep device\"}\n", f);
        fclose(f);
        return;
    }
    rc = clear_ir_commands(device_id, msg, sizeof(msg));
    f = send_json_start(fd, rc == 0 ? "200 OK" : "500 Internal Server Error");
    if (!f) return;
    fputs("{\"ok\":", f); fputs(rc == 0 ? "true" : "false", f);
    fputs(",\"deviceId\":", f); json_write_string(f, device_id);
    fputs(",\"message\":", f); json_write_string(f, msg);
    fputs("}\n", f);
    fclose(f);
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

static int safe_bt_token(const char *s, size_t maxlen) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s);
    if (n == 0 || n > maxlen) return 0;
    while (*p) {
        if (!isalnum(*p) && *p != '_' && *p != '-') return 0;
        p++;
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

static int run_hal_json_binary_hex(const char *cmd_name, const char *params_json, const char *payload_hex, int timeout, char *out, size_t outlen) {
    char esc_cmd[128], esc_params[2048], esc_payload[512], cmd[3000];
    if (timeout < 1) timeout = 1;
    if (timeout > 30) timeout = 30;
    shell_escape_single(cmd_name, esc_cmd, sizeof(esc_cmd));
    shell_escape_single(params_json, esc_params, sizeof(esc_params));
    shell_escape_single(payload_hex, esc_payload, sizeof(esc_payload));
    snprintf(cmd, sizeof(cmd), "/data/codex/bin/codex_hal_ltcp '%s' '%s' %d '%s' 2>&1", esc_cmd, esc_params, timeout, esc_payload);
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
    int pid;
    if (!raw || !json_bool(raw, "runtime", 0)) return 0;
    pid = json_int(raw, "pid", 0);
    if (pid <= 0) {
        char ps[256];
        return run_cmd("ps | grep '[c]odex_bthid_keyboard'", ps, sizeof(ps)) == 0 && ps[0] != 0;
    }
    if (kill((pid_t)pid, 0) == 0) return 1;
    return errno == EPERM;
}

static void write_bthid_missing_status(FILE *f, const char *state, const char *message, int sent, int skipped) {
    fputs("{\"ok\":true,\"runtime\":false,\"state\":", f);
    json_write_string(f, state ? state : "missing");
    fputs(",\"target\":\"\",\"sent\":", f);
    fprintf(f, "%d", sent);
    fputs(",\"skipped\":", f);
    fprintf(f, "%d", skipped);
    fputs(",\"error\":", f);
    json_write_string(f, message ? message : "Bluetooth FIFO runtime is not running");
    fputs("}\n", f);
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

static void render_bluetooth_text_json(int fd, const struct request *req) {
    char text[MAX_BT_SEQUENCE_BODY], err[256];
    FILE *f;
    form_value(req->body, "text", text, sizeof(text));
    if (write_bt_text_fifo(text, err, sizeof(err)) != 0) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":", f); json_write_string(f, err);
        fputs("}\n", f);
        fclose(f);
        return;
    }
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"path\":\"/api/bt-text\",\"bytes\":", f);
    fprintf(f, "%lu", (unsigned long)strlen(text));
    fputs("}\n", f);
    fclose(f);
}

static void render_bluetooth_text_status_json(int fd) {
    char raw[1024];
    FILE *f = send_json_start(fd, "200 OK");
    if (!f) return;
    if (read_text(BT_TEXT_STATUS, raw, sizeof(raw)) > 0 && raw[0] == '{') {
        if (bthid_status_runtime_alive(raw)) {
            fputs(raw, f);
            if (raw[strlen(raw) - 1] != '\n') fputc('\n', f);
        } else {
            write_bthid_missing_status(f, "stale", "Bluetooth FIFO runtime status is stale; restart bthid_keyboard",
                json_int(raw, "sent", 0), json_int(raw, "skipped", 0));
        }
    } else {
        write_bthid_missing_status(f, "missing", "Bluetooth FIFO runtime is not running", 0, 0);
    }
    fclose(f);
}

static int update_file_allowed(const char *name) {
    size_t i;
    if (!name || !name[0]) return 0;
    for (i = 0; i < sizeof(UPDATE_FILES) / sizeof(UPDATE_FILES[0]); i++) {
        if (strcmp(name, UPDATE_FILES[i]) == 0) return 1;
    }
    return 0;
}

static void update_stage_path(const char *name, char *out, size_t outlen) {
    snprintf(out, outlen, UPDATE_STAGE_DIR "/%s", name);
}

static void update_dest_path(const char *name, char *out, size_t outlen) {
    snprintf(out, outlen, CODEX_BIN_DIR "/%s", name);
}

static int is_hex32(const char *s) {
    int i;
    if (!s) return 0;
    for (i = 0; i < 32; i++) {
        if (!isxdigit((unsigned char)s[i])) return 0;
    }
    return s[32] == 0;
}

static int manifest_expected_md5(const char *manifest, const char *name, char *out, size_t outlen) {
    const char *p = manifest;
    if (!manifest || !name || !out || outlen < 33) return 0;
    out[0] = 0;
    while (p && *p) {
        const char *end = strchr(p, '\n');
        const char *q;
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 34) {
            char hash[33], file[96];
            size_t n = 0;
            memcpy(hash, p, 32);
            hash[32] = 0;
            q = p + 32;
            while (q < p + len && isspace((unsigned char)*q)) q++;
            while (q < p + len && *q != '\r' && !isspace((unsigned char)*q) && n + 1 < sizeof(file)) {
                file[n++] = *q++;
            }
            file[n] = 0;
            if (is_hex32(hash) && strcmp(file, name) == 0) {
                snprintf(out, outlen, "%s", hash);
                return 1;
            }
        }
        p = end ? end + 1 : NULL;
    }
    return 0;
}

static int parse_md5_text(const char *reply, char *out, size_t outlen) {
    char hash[33];
    int i;
    if (!reply || !out || outlen < 33 || strlen(reply) < 32) return -1;
    for (i = 0; i < 32; i++) {
        if (!isxdigit((unsigned char)reply[i])) return -1;
        hash[i] = (char)tolower((unsigned char)reply[i]);
    }
    hash[32] = 0;
    snprintf(out, outlen, "%s", hash);
    return 0;
}

static int file_md5(const char *path, char *out, size_t outlen) {
    char cmd[768], reply[256], tmp[96], esc_path[384], esc_tmp[160];
    FILE *f;
    size_t n = 0;
    int rc;
    if (!path || !out || outlen < 33) return -1;
    shell_escape_single(path, esc_path, sizeof(esc_path));
    snprintf(cmd, sizeof(cmd), "/bin/busybox md5sum '%s' 2>/dev/null", esc_path);
    reply[0] = 0;
    if (run_cmd(cmd, reply, sizeof(reply)) == 0 && parse_md5_text(reply, out, outlen) == 0) return 0;
    snprintf(tmp, sizeof(tmp), "/tmp/codex-md5-%ld.txt", (long)getpid());
    shell_escape_single(tmp, esc_tmp, sizeof(esc_tmp));
    snprintf(cmd, sizeof(cmd), "/bin/busybox md5sum '%s' > '%s' 2>/dev/null", esc_path, esc_tmp);
    rc = system(cmd);
    if (rc != 0) {
        unlink(tmp);
        return -1;
    }
    f = fopen(tmp, "rb");
    if (f) {
        n = fread(reply, 1, sizeof(reply) - 1, f);
        fclose(f);
    }
    unlink(tmp);
    reply[n] = 0;
    return parse_md5_text(reply, out, outlen);
}

static int write_update_hex_chunk(const char *name, long offset, const char *hex, char *err, size_t errlen, long *bytes_out) {
    char path[256];
    struct stat st;
    FILE *f;
    size_t len, i;
    long current = 0, wrote = 0;
    if (bytes_out) *bytes_out = 0;
    if (!update_file_allowed(name)) {
        snprintf(err, errlen, "file is not part of the update allow-list");
        return -1;
    }
    if (!hex || !hex[0]) {
        snprintf(err, errlen, "missing update chunk");
        return -1;
    }
    len = strlen(hex);
    if ((len & 1) != 0) {
        snprintf(err, errlen, "chunk hex length is odd");
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)hex[i])) {
            snprintf(err, errlen, "chunk contains non-hex data");
            return -1;
        }
    }
    mkdir(UPDATE_STAGE_DIR, 0755);
    update_stage_path(name, path, sizeof(path));
    if (stat(path, &st) == 0) current = (long)st.st_size;
    if (offset != current) {
        snprintf(err, errlen, "chunk offset mismatch for %s: got %ld expected %ld", name, offset, current);
        return -1;
    }
    f = fopen(path, offset == 0 ? "wb" : "ab");
    if (!f) {
        snprintf(err, errlen, "cannot open staged update file");
        return -1;
    }
    for (i = 0; i < len; i += 2) {
        int hi = hexval(hex[i]), lo = hexval(hex[i + 1]);
        unsigned char b = (unsigned char)((hi << 4) | lo);
        if (hi < 0 || lo < 0 || fwrite(&b, 1, 1, f) != 1) {
            fclose(f);
            snprintf(err, errlen, "failed writing staged update chunk");
            return -1;
        }
        wrote++;
    }
    fclose(f);
    if (bytes_out) *bytes_out = wrote;
    return 0;
}

static void render_update_status_json(int fd) {
    FILE *f = send_json_start(fd, "200 OK");
    size_t i;
    struct stat st;
    char path[256], md5[40];
    if (!f) return;
    fputs("{\"ok\":true,\"repo\":\"https://github.com/Ripthulhu/harmony-hub-control\",\"rawBase\":\"https://raw.githubusercontent.com/Ripthulhu/harmony-hub-control/main/payload/bin/\",\"files\":[", f);
    for (i = 0; i < sizeof(UPDATE_FILES) / sizeof(UPDATE_FILES[0]); i++) {
        if (i) fputc(',', f);
        update_dest_path(UPDATE_FILES[i], path, sizeof(path));
        fputs("{\"name\":", f); json_write_string(f, UPDATE_FILES[i]);
        if (stat(path, &st) == 0) {
            fputs(",\"present\":true,\"size\":", f); fprintf(f, "%ld", (long)st.st_size);
            if (file_md5(path, md5, sizeof(md5)) == 0) {
                fputs(",\"md5\":", f); json_write_string(f, md5);
            }
        } else {
            fputs(",\"present\":false,\"size\":0", f);
        }
        fputc('}', f);
    }
    fputs("]}\n", f);
    fclose(f);
}

static void render_update_check_state_json(int fd) {
    struct update_check_state st;
    FILE *f = send_json_start(fd, "200 OK");
    if (!f) return;
    load_update_state(&st);
    fputs("{\"ok\":true,\"checkedAt\":", f); fprintf(f, "%ld", st.checked_at);
    fputs(",\"available\":", f); fputs(st.available ? "true" : "false", f);
    fputs(",\"changes\":", f); fprintf(f, "%d", st.changes);
    fputs(",\"message\":", f); json_write_string(f, st.message);
    fputs(",\"source\":", f); json_write_string(f, st.source);
    fputs("}\n", f);
    fclose(f);
}

static void render_update_check_state_post_json(int fd, const struct request *req) {
    struct update_check_state st;
    char tmp[256];
    FILE *f;
    memset(&st, 0, sizeof(st));
    form_value(req->body, "checkedAt", tmp, sizeof(tmp));
    st.checked_at = atol(tmp);
    if (st.checked_at <= 0) st.checked_at = time(NULL);
    form_value(req->body, "available", tmp, sizeof(tmp));
    st.available = (strcmp(tmp, "1") == 0 || strcasecmp(tmp, "true") == 0 ||
        strcasecmp(tmp, "yes") == 0 || strcasecmp(tmp, "on") == 0) ? 1 : 0;
    form_value(req->body, "changes", tmp, sizeof(tmp));
    st.changes = tmp[0] ? atoi(tmp) : 0;
    form_value(req->body, "message", st.message, sizeof(st.message));
    if (!st.message[0]) snprintf(st.message, sizeof(st.message), "Update check completed.");
    form_value(req->body, "source", st.source, sizeof(st.source));
    if (save_update_state(&st) != 0) {
        f = send_json_start(fd, "500 Internal Server Error");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"failed to save update check state\"}\n", f);
        fclose(f);
        return;
    }
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"checkedAt\":", f); fprintf(f, "%ld", st.checked_at);
    fputs(",\"available\":", f); fputs(st.available ? "true" : "false", f);
    fputs(",\"changes\":", f); fprintf(f, "%d", st.changes);
    fputs(",\"message\":", f); json_write_string(f, st.message);
    fputs(",\"source\":", f); json_write_string(f, st.source);
    fputs("}\n", f);
    fclose(f);
}

static void render_update_begin_json(int fd, const struct request *req) {
    char *manifest;
    size_t i;
    FILE *f;
    if (req->body_truncated) {
        f = send_json_start(fd, "413 Payload Too Large");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"update manifest request is too large\"}\n", f);
        fclose(f);
        return;
    }
    manifest = (char *)malloc(MAX_REQUEST_BODY);
    if (!manifest) {
        f = send_json_start(fd, "500 Internal Server Error");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"not enough memory for update manifest\"}\n", f);
        fclose(f);
        return;
    }
    form_value(req->body, "manifest", manifest, MAX_REQUEST_BODY);
    if (!strstr(manifest, "codex_webui")) {
        free(manifest);
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"manifest does not look like a Harmony control build\"}\n", f);
        fclose(f);
        return;
    }
    mkdir(UPDATE_STAGE_DIR, 0755);
    for (i = 0; i < sizeof(UPDATE_FILES) / sizeof(UPDATE_FILES[0]); i++) {
        char path[256];
        update_stage_path(UPDATE_FILES[i], path, sizeof(path));
        unlink(path);
    }
    if (write_file_atomic(UPDATE_STAGE_DIR "/MANIFEST.txt", manifest, strlen(manifest)) != 0) {
        free(manifest);
        f = send_json_start(fd, "500 Internal Server Error");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"failed to stage update manifest\"}\n", f);
        fclose(f);
        return;
    }
    free(manifest);
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"message\":\"update staging started\"}\n", f);
    fclose(f);
}

static void render_update_chunk_json(int fd, const struct request *req) {
    char name[64], offset_text[32], err[256];
    char *hex;
    long offset, wrote = 0;
    FILE *f;
    if (req->body_truncated) {
        f = send_json_start(fd, "413 Payload Too Large");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"update chunk is too large\"}\n", f);
        fclose(f);
        return;
    }
    form_value(req->body, "file", name, sizeof(name));
    form_value(req->body, "offset", offset_text, sizeof(offset_text));
    offset = atol(offset_text);
    hex = (char *)malloc(MAX_REQUEST_BODY);
    if (!hex) {
        f = send_json_start(fd, "500 Internal Server Error");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"not enough memory for update chunk\"}\n", f);
        fclose(f);
        return;
    }
    form_value(req->body, "hex", hex, MAX_REQUEST_BODY);
    if (write_update_hex_chunk(name, offset, hex, err, sizeof(err), &wrote) != 0) {
        free(hex);
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":", f); json_write_string(f, err);
        fputs("}\n", f);
        fclose(f);
        return;
    }
    free(hex);
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"file\":", f); json_write_string(f, name);
    fprintf(f, ",\"offset\":%ld,\"bytes\":%ld}\n", offset, wrote);
    fclose(f);
}

static void render_update_apply_json(int fd, const struct request *req) {
    char *manifest;
    char restart_text[16], backup_dir[256], updated[512];
    char stage[256], dest[256], dest_tmp[288], backup[256], expected[40], actual[40];
    int restart, count = 0, rc = 0;
    size_t i;
    struct stat st;
    FILE *f;
    form_value(req->body, "restart", restart_text, sizeof(restart_text));
    restart = strcmp(restart_text, "0") != 0;
    manifest = read_file_alloc(UPDATE_STAGE_DIR "/MANIFEST.txt", MAX_REQUEST_BODY, NULL);
    if (!manifest) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"no staged update manifest found\"}\n", f);
        fclose(f);
        return;
    }
    for (i = 0; i < sizeof(UPDATE_FILES) / sizeof(UPDATE_FILES[0]); i++) {
        update_stage_path(UPDATE_FILES[i], stage, sizeof(stage));
        if (stat(stage, &st) != 0) continue;
        count++;
        if (!manifest_expected_md5(manifest, UPDATE_FILES[i], expected, sizeof(expected))) {
            rc = -1;
            snprintf(updated, sizeof(updated), "manifest has no md5 for %s", UPDATE_FILES[i]);
            break;
        }
        if (file_md5(stage, actual, sizeof(actual)) != 0 || strcasecmp(expected, actual) != 0) {
            rc = -1;
            snprintf(updated, sizeof(updated), "md5 mismatch for %s", UPDATE_FILES[i]);
            break;
        }
    }
    if (rc == 0 && count <= 0) {
        rc = -1;
        snprintf(updated, sizeof(updated), "no staged binaries found");
    }
    if (rc != 0) {
        free(manifest);
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":", f); json_write_string(f, updated);
        fputs("}\n", f);
        fclose(f);
        return;
    }
    remove_dir_entries_with_prefix(CODEX_BIN_DIR, "codex_webui.prev");
    mkdir(UPDATE_BACKUP_DIR, 0755);
    prune_update_backups(3);
    snprintf(backup_dir, sizeof(backup_dir), UPDATE_BACKUP_DIR "/%ld", (long)time(NULL));
    mkdir(backup_dir, 0755);
    updated[0] = 0;
    for (i = 0; i < sizeof(UPDATE_FILES) / sizeof(UPDATE_FILES[0]); i++) {
        update_stage_path(UPDATE_FILES[i], stage, sizeof(stage));
        if (stat(stage, &st) != 0) continue;
        update_dest_path(UPDATE_FILES[i], dest, sizeof(dest));
        snprintf(dest_tmp, sizeof(dest_tmp), "%s.update", dest);
        snprintf(backup, sizeof(backup), "%s/%s", backup_dir, UPDATE_FILES[i]);
        if (stat(dest, &st) == 0) copy_file_raw(dest, backup);
        unlink(dest_tmp);
        if (copy_file_raw(stage, dest_tmp) != 0) {
            char msg[160];
            int saved_errno = errno;
            unlink(dest_tmp);
            free(manifest);
            f = send_json_start(fd, "500 Internal Server Error");
            if (!f) return;
            snprintf(msg, sizeof(msg), "failed to copy %s: %s", UPDATE_FILES[i], strerror(saved_errno));
            fputs("{\"ok\":false,\"error\":", f); json_write_string(f, msg);
            fputs("}\n", f);
            fclose(f);
            return;
        }
        if (chmod(dest_tmp, 0755) != 0) {
            char msg[160];
            int saved_errno = errno;
            unlink(dest_tmp);
            free(manifest);
            f = send_json_start(fd, "500 Internal Server Error");
            if (!f) return;
            snprintf(msg, sizeof(msg), "failed to chmod %s: %s", UPDATE_FILES[i], strerror(saved_errno));
            fputs("{\"ok\":false,\"error\":", f); json_write_string(f, msg);
            fputs("}\n", f);
            fclose(f);
            return;
        }
        if (rename(dest_tmp, dest) != 0) {
            char msg[160];
            int saved_errno = errno;
            unlink(dest_tmp);
            free(manifest);
            f = send_json_start(fd, "500 Internal Server Error");
            if (!f) return;
            snprintf(msg, sizeof(msg), "failed to rename %s: %s", UPDATE_FILES[i], strerror(saved_errno));
            fputs("{\"ok\":false,\"error\":", f); json_write_string(f, msg);
            fputs("}\n", f);
            fclose(f);
            return;
        }
        unlink(stage);
        if (updated[0]) strncat(updated, ", ", sizeof(updated) - strlen(updated) - 1);
        strncat(updated, UPDATE_FILES[i], sizeof(updated) - strlen(updated) - 1);
    }
    copy_file_raw(UPDATE_STAGE_DIR "/MANIFEST.txt", CODEX_BIN_DIR "/MANIFEST.txt");
    chmod(CODEX_BIN_DIR "/MANIFEST.txt", 0644);
    unlink(UPDATE_STAGE_DIR "/MANIFEST.txt");
    prune_update_backups(3);
    sync();
    free(manifest);
    f = send_json_start(fd, "200 OK");
    if (!f) return;
    fputs("{\"ok\":true,\"updated\":", f); json_write_string(f, updated);
    fputs(",\"backupDir\":", f); json_write_string(f, backup_dir);
    fputs(",\"restart\":", f); fputs(restart ? "true" : "false", f);
    fputs("}\n", f);
    fclose(f);
    if (restart) {
        pid_t pid;
        shutdown(fd, SHUT_RDWR);
        pid = fork();
        if (pid == 0) {
            close(fd);
            setsid();
            execl("/bin/sh", "sh", "-c",
                  "sleep 3; "
                  "killall codex_bthid_keyboard 2>/dev/null; "
                  "/data/codex/bin/codex_bthid_keyboard >> /cache/codex-bthid-keyboard.log 2>&1 & "
                  "killall codex_webui 2>/dev/null; "
                  "/data/codex/bin/codex_webui 8080 >> /cache/codex-init.log 2>&1 &",
                  (char *)NULL);
            _exit(127);
        }
    }
}

static void render_bluetooth_call_json(int fd, const struct request *req) {
    char action[32], type[40], bdaddr[32], pin[24], code[MAX_BT_SEQUENCE_BODY], name[64], timeout_text[24], gap_text[24];
    char params[768], reply[8192], cmd[2048], esc_name[128], *jt = NULL, *ja = NULL, *jp = NULL, *jc = NULL;
    char detected_addr[32], connection_raw[2048];
    const char *cmd_name = NULL;
    int timeout, call_timeout, gap_ms, auto_detected_addr = 0, command_rc = 0;
    FILE *f;

    form_value(req->body, "action", action, sizeof(action));
    form_value(req->body, "type", type, sizeof(type));
    form_value(req->body, "bdaddr", bdaddr, sizeof(bdaddr));
    form_value(req->body, "pin", pin, sizeof(pin));
    form_value(req->body, "code", code, sizeof(code));
    form_value(req->body, "name", name, sizeof(name));
    form_value(req->body, "timeout", timeout_text, sizeof(timeout_text));
    form_value(req->body, "gapMs", gap_text, sizeof(gap_text));
    chomp(action);
    chomp(type);
    chomp(bdaddr);
    chomp(pin);
    chomp(code);
    chomp(name);
    if (!type[0]) strcpy(type, "fire");
    if (!name[0]) strcpy(name, "Harmony Keyboard");
    timeout = atoi(timeout_text);
    if (timeout < 1) timeout = 2;
    if (timeout > 20) timeout = 20;
    gap_ms = atoi(gap_text);
    if (gap_ms < 15) gap_ms = 35;
    if (gap_ms > 5000) gap_ms = 5000;
    params[0] = 0;
    detected_addr[0] = 0;
    connection_raw[0] = 0;

    if (strcmp(action, "adapter_status") == 0) {
        reply[0] = 0;
        run_cmd("echo '--- adapter ---'; hciconfig hci0 -a 2>&1; echo; echo '--- connections ---'; hcitool con 2>&1; echo; echo '--- bluez ---'; adapter=$(dbus-send --system --print-reply --dest=org.bluez / org.bluez.Manager.DefaultAdapter 2>/dev/null | sed -n 's/.*object path \"\\(.*\\)\".*/\\1/p'); if [ -n \"$adapter\" ]; then dbus-send --system --print-reply --dest=org.bluez \"$adapter\" org.bluez.Adapter.GetProperties 2>&1; else echo 'BlueZ adapter not found'; fi", reply, sizeof(reply));
        f = send_json_start(fd, "200 OK");
        if (!f) return;
        fputs("{\"ok\":true,\"action\":", f); json_write_string(f, action);
        fputs(",\"cmd\":\"adapter_status\",\"params\":\"\",\"responseRaw\":", f);
        json_write_string(f, reply[0] ? reply : "no response");
        fputs("}\n", f);
        fclose(f);
        return;
    } else if (strcmp(action, "pairing_on") == 0) {
        if (!safe_bt_name(name)) {
            f = send_json_start(fd, "400 Bad Request");
            if (!f) return;
            fputs("{\"ok\":false,\"error\":\"invalid Bluetooth display name\"}\n", f);
            fclose(f);
            return;
        }
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
        f = send_json_start(fd, "200 OK");
        if (!f) return;
        fputs("{\"ok\":true,\"action\":", f); json_write_string(f, action);
        fputs(",\"cmd\":\"pairing_on\",\"params\":", f); json_write_string(f, name);
        fputs(",\"responseRaw\":", f); json_write_string(f, reply[0] ? reply : "no response");
        fputs("}\n", f);
        fclose(f);
        return;
    } else if (strcmp(action, "pairing_off") == 0) {
        reply[0] = 0;
        run_cmd("echo '--- disabling discoverable mode ---'; adapter=$(dbus-send --system --print-reply --dest=org.bluez / org.bluez.Manager.DefaultAdapter 2>/dev/null | sed -n 's/.*object path \"\\(.*\\)\".*/\\1/p'); if [ -n \"$adapter\" ]; then dbus-send --system --dest=org.bluez \"$adapter\" org.bluez.Adapter.SetProperty string:Discoverable variant:boolean:false 2>&1; fi; hciconfig hci0 pscan 2>&1; echo; echo '--- adapter ---'; hciconfig hci0 -a 2>&1; echo; echo '--- bluez ---'; if [ -n \"$adapter\" ]; then dbus-send --system --print-reply --dest=org.bluez \"$adapter\" org.bluez.Adapter.GetProperties 2>&1; fi", reply, sizeof(reply));
        f = send_json_start(fd, "200 OK");
        if (!f) return;
        fputs("{\"ok\":true,\"action\":", f); json_write_string(f, action);
        fputs(",\"cmd\":\"pairing_off\",\"params\":\"\",\"responseRaw\":", f);
        json_write_string(f, reply[0] ? reply : "no response");
        fputs("}\n", f);
        fclose(f);
        return;
    } else if (strcmp(action, "scan") == 0) {
        cmd_name = "bt.lediscovery";
        snprintf(params, sizeof(params), "{\"timeout\":%d}", timeout);
        call_timeout = timeout + 3;
    } else if (strcmp(action, "classic_scan") == 0) {
        reply[0] = 0;
        run_cmd("hcitool scan 2>&1", reply, sizeof(reply));
        f = send_json_start(fd, "200 OK");
        if (!f) return;
        fputs("{\"ok\":true,\"action\":", f); json_write_string(f, action);
        fputs(",\"cmd\":\"hcitool scan\",\"params\":\"\",\"responseRaw\":", f);
        json_write_string(f, reply[0] ? reply : "no response");
        fputs("}\n", f);
        fclose(f);
        return;
    } else {
        if (!bt_type_allowed(type)) {
            f = send_json_start(fd, "400 Bad Request");
            if (!f) return;
            fputs("{\"ok\":false,\"error\":\"unsupported Bluetooth HID type\"}\n", f);
            fclose(f);
            return;
        }
        if (!bdaddr[0] && (strcmp(action, "report") == 0 || strcmp(action, "reportseq") == 0 || strcmp(action, "status") == 0 || strcmp(action, "disconnect") == 0)) {
            auto_detected_addr = detect_connected_bt_addr(detected_addr, sizeof(detected_addr), connection_raw, sizeof(connection_raw));
            if (auto_detected_addr) copy_text(bdaddr, sizeof(bdaddr), detected_addr);
        }
        if (!bdaddr[0] && strcmp(action, "status") == 0) {
            strcpy(bdaddr, "00:00:00:00:00:00");
        }
        if (!bdaddr[0] && (strcmp(action, "report") == 0 || strcmp(action, "reportseq") == 0)) {
            f = send_json_start(fd, "400 Bad Request");
            if (!f) return;
            fputs("{\"ok\":false,\"error\":\"no connected Bluetooth target found; pair the target, click Adapter Status, or enter the paired target address\"}\n", f);
            fclose(f);
            return;
        }
        if (!safe_bt_addr(bdaddr) || !safe_bt_pin(pin)) {
            f = send_json_start(fd, "400 Bad Request");
            if (!f) return;
            fputs("{\"ok\":false,\"error\":\"invalid Bluetooth address or PIN\"}\n", f);
            fclose(f);
            return;
        }
        if (strcmp(action, "disconnect") == 0) {
            unlink(BT_TARGET_FILE);
        } else if (strcmp(action, "connect") == 0 || strcmp(action, "status") == 0 ||
            strcmp(action, "report") == 0 || strcmp(action, "reportseq") == 0) {
            save_bthid_target(type, bdaddr);
        }
        jt = json_escape_alloc(type);
        ja = json_escape_alloc(bdaddr);
        jp = json_escape_alloc(pin);
        jc = json_escape_alloc(code);
        if (!jt || !ja || !jp || !jc) {
            free(jt); free(ja); free(jp); free(jc);
            f = send_json_start(fd, "500 Internal Server Error");
            if (!f) return;
            fputs("{\"ok\":false,\"error\":\"not enough memory for Bluetooth command\"}\n", f);
            fclose(f);
            return;
        }
        if (strcmp(action, "status") == 0) {
            cmd_name = "bthid.status";
            snprintf(params, sizeof(params), "{\"type\":%s,\"bdaddr\":%s}", jt, ja);
        } else if (strcmp(action, "connect") == 0) {
            cmd_name = "bthid.connect";
            if (pin[0]) snprintf(params, sizeof(params), "{\"type\":%s,\"bdaddr\":%s,\"pin\":%s}", jt, ja, jp);
            else snprintf(params, sizeof(params), "{\"type\":%s,\"bdaddr\":%s}", jt, ja);
        } else if (strcmp(action, "disconnect") == 0) {
            cmd_name = "bthid.disconnect";
            snprintf(params, sizeof(params), "{\"type\":%s,\"bdaddr\":%s}", jt, ja);
        } else if (strcmp(action, "report") == 0 || strcmp(action, "reportseq") == 0) {
            cmd_name = "bthid.report";
            snprintf(params, sizeof(params), "{\"type\":%s,\"bdaddr\":%s}", jt, ja);
        }
        free(jt); free(ja); free(jp); free(jc);
        call_timeout = 8;
    }

    if (!cmd_name) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"unknown Bluetooth action\"}\n", f);
        fclose(f);
        return;
    }

    reply[0] = 0;
    if (strcmp(action, "report") == 0 || strcmp(action, "reportseq") == 0) {
        char *seq = NULL, *line, *save;
        size_t seq_len = 0, seq_cap = 0;
        int sent_keys = 0;
        char report_err[160];
        if (strcmp(action, "report") == 0) {
            if (bt_sequence_add_code(code, &seq, &seq_len, &seq_cap, &sent_keys, report_err, sizeof(report_err)) != 0) {
                free(seq);
                f = send_json_start(fd, "400 Bad Request");
                if (!f) return;
                fputs("{\"ok\":false,\"error\":", f); json_write_string(f, report_err);
                fputs("}\n", f);
                fclose(f);
                return;
            }
        } else {
            line = strtok_r(code, "\n", &save);
            while (line) {
                if (bt_sequence_add_code(line, &seq, &seq_len, &seq_cap, &sent_keys, report_err, sizeof(report_err)) != 0) {
                    free(seq);
                    f = send_json_start(fd, "400 Bad Request");
                    if (!f) return;
                    fputs("{\"ok\":false,\"error\":", f); json_write_string(f, report_err);
                    fputs("}\n", f);
                    fclose(f);
                    return;
                }
                line = strtok_r(NULL, "\n", &save);
            }
        }
        if (!seq || sent_keys <= 0) {
            free(seq);
            f = send_json_start(fd, "400 Bad Request");
            if (!f) return;
            fputs("{\"ok\":false,\"error\":\"no Bluetooth keyboard reports to send\"}\n", f);
            fclose(f);
            return;
        }
        command_rc = run_hal_json_binary_sequence(cmd_name, params, seq, call_timeout, gap_ms, reply, sizeof(reply));
        free(seq);
        {
            size_t used = strlen(reply);
            snprintf(reply + used, sizeof(reply) - used, "\nkeys=%d gapMs=%d", sent_keys, gap_ms);
        }
    } else {
        command_rc = run_hal_json(cmd_name, params, call_timeout, reply, sizeof(reply));
    }
    f = send_json_start(fd, command_rc == 0 ? "200 OK" : "502 Bad Gateway");
    if (!f) return;
    fputs("{\"ok\":", f); fputs(command_rc == 0 ? "true" : "false", f);
    fputs(",\"action\":", f); json_write_string(f, action);
    fputs(",\"cmd\":", f); json_write_string(f, cmd_name);
    fputs(",\"params\":", f); json_write_string(f, params);
    fputs(",\"exitCode\":", f); fprintf(f, "%d", command_rc);
    fputs(",\"responseRaw\":", f); json_write_string(f, reply[0] ? reply : "no response");
    if (auto_detected_addr) {
        fputs(",\"detectedAddress\":", f); json_write_string(f, detected_addr);
        fputs(",\"connectionRaw\":", f); json_write_string(f, connection_raw);
    }
    fputs("}\n", f);
    fclose(f);
}

static void render_ir_test_learned_json(int fd, const struct request *req) {
    char device_id[64], name[128], mode[32], protocol[32], nec[64], keycode[512], raw[2048];
    char temp_name[128], add_msg[512], cleanup_msg[512], reply[4096], run_id[128];
    int add_rc, cleanup_rc = -1;
    FILE *f;

    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "name", name, sizeof(name));
    form_value(req->body, "mode", mode, sizeof(mode));
    form_value(req->body, "protocol", protocol, sizeof(protocol));
    form_value(req->body, "nec", nec, sizeof(nec));
    form_value(req->body, "keycode", keycode, sizeof(keycode));
    form_value(req->body, "raw", raw, sizeof(raw));
    if (!mode[0]) strcpy(mode, "auto");
    if (!protocol[0]) strcpy(protocol, "2");
    if (!safe_label(device_id)) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"choose a device before testing\"}\n", f);
        fclose(f);
        return;
    }
    if (!raw[0] && !keycode[0] && !nec[0]) {
        f = send_json_start(fd, "400 Bad Request");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"learn or enter a signal before testing\"}\n", f);
        fclose(f);
        return;
    }
    snprintf(temp_name, sizeof(temp_name), "Signal Test %ld %d", (long)time(NULL), (int)(getpid() % 10000));
    add_rc = add_ir_command(device_id, temp_name, mode, protocol, nec, keycode, raw, add_msg, sizeof(add_msg));
    reply[0] = 0;
    if (add_rc == 0) {
        snprintf(run_id, sizeof(run_id), "learn_%ld_%d", (long)time(NULL), (int)(getpid() % 10000));
        usleep(800000);
        send_ir_command_action_ex(device_id, temp_name, "api-learn-test", run_id, reply, sizeof(reply));
        cleanup_rc = delete_ir_command(device_id, temp_name, cleanup_msg, sizeof(cleanup_msg));
    } else {
        cleanup_msg[0] = 0;
    }
    f = send_json_start(fd, add_rc == 0 ? "200 OK" : "400 Bad Request");
    if (!f) return;
    fputs("{\"ok\":", f); fputs(add_rc == 0 ? "true" : "false", f);
    fputs(",\"deviceId\":", f); json_write_string(f, device_id);
    fputs(",\"tempCommand\":", f); json_write_string(f, add_rc == 0 ? temp_name : "");
    fputs(",\"addMessage\":", f); json_write_string(f, add_msg);
    fputs(",\"reply\":", f); json_write_string(f, reply[0] ? reply : "");
    fputs(",\"cleanupOk\":", f); fputs(cleanup_rc == 0 ? "true" : "false", f);
    fputs(",\"cleanupMessage\":", f); json_write_string(f, cleanup_msg);
    fputs("}\n", f);
    fclose(f);
}

static void handle_ir_device(int fd, const struct request *req) {
    char device_id[64], name[128], manufacturer[128], model[128], type[80], msg[512];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "name", name, sizeof(name));
    form_value(req->body, "manufacturer", manufacturer, sizeof(manufacturer));
    form_value(req->body, "model", model, sizeof(model));
    form_value(req->body, "type", type, sizeof(type));
    update_ir_device(device_id, name, manufacturer, model, type, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_ir_new_device(int fd, const struct request *req) {
    char name[128], manufacturer[128], model[128], type[80], msg[512];
    form_value(req->body, "name", name, sizeof(name));
    form_value(req->body, "manufacturer", manufacturer, sizeof(manufacturer));
    form_value(req->body, "model", model, sizeof(model));
    form_value(req->body, "type", type, sizeof(type));
    create_ir_device(name, manufacturer, model, type, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_ir_command(int fd, const struct request *req) {
    char device_id[64], name[128], mode[32], protocol[32], nec[64], keycode[512], raw[2048], msg[512];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "name", name, sizeof(name));
    form_value(req->body, "mode", mode, sizeof(mode));
    form_value(req->body, "protocol", protocol, sizeof(protocol));
    form_value(req->body, "nec", nec, sizeof(nec));
    form_value(req->body, "keycode", keycode, sizeof(keycode));
    form_value(req->body, "raw", raw, sizeof(raw));
    if (!mode[0]) strcpy(mode, "auto");
    add_ir_command(device_id, name, mode, protocol, nec, keycode, raw, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_ir_update_command(int fd, const struct request *req) {
    char device_id[64], old_name[128], name[128], mode[32], protocol[32], nec[64], keycode[512], raw[2048], msg[512];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "oldName", old_name, sizeof(old_name));
    form_value(req->body, "name", name, sizeof(name));
    form_value(req->body, "mode", mode, sizeof(mode));
    form_value(req->body, "protocol", protocol, sizeof(protocol));
    form_value(req->body, "nec", nec, sizeof(nec));
    form_value(req->body, "keycode", keycode, sizeof(keycode));
    form_value(req->body, "raw", raw, sizeof(raw));
    if (!mode[0]) strcpy(mode, "keycode");
    update_ir_command(device_id, old_name, name, mode, protocol, nec, keycode, raw, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_irdb_import(int fd, const struct request *req) {
    char device_id[64], msg[512];
    char *payload;
    if (req->body_truncated) {
        render_page(fd, "IRDB import payload was too large.");
        return;
    }
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    payload = (char *)malloc(MAX_REQUEST_BODY);
    if (!payload) {
        render_page(fd, "Not enough memory to receive IRDB import.");
        return;
    }
    form_value(req->body, "payload", payload, MAX_REQUEST_BODY);
    if (bulk_import_irdb_commands(device_id, payload, msg, sizeof(msg)) != 0) {
        render_page(fd, msg);
    } else {
        render_page(fd, msg);
    }
    free(payload);
}

static void render_irdb_import_json(int fd, const struct request *req) {
    char device_id[64], msg[512];
    char *payload;
    FILE *f;
    int rc;
    if (req->body_truncated) {
        f = send_json_start(fd, "413 Payload Too Large");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"IRDB import payload was too large\"}\n", f);
        fclose(f);
        return;
    }
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    payload = (char *)malloc(MAX_REQUEST_BODY);
    if (!payload) {
        f = send_json_start(fd, "500 Internal Server Error");
        if (!f) return;
        fputs("{\"ok\":false,\"error\":\"not enough memory to receive IRDB import\"}\n", f);
        fclose(f);
        return;
    }
    form_value(req->body, "payload", payload, MAX_REQUEST_BODY);
    rc = bulk_import_irdb_commands(device_id, payload, msg, sizeof(msg));
    if (rc != 0) {
        f = send_json_start(fd, "400 Bad Request");
    } else {
        f = send_json_start(fd, "200 OK");
    }
    if (f) {
        fputs("{\"ok\":", f); fputs(rc == 0 ? "true" : "false", f);
        fputs(",\"message\":", f); json_write_string(f, msg);
        fputs("}\n", f);
        fclose(f);
    }
    free(payload);
}

static void handle_ir_capture(int fd, const struct request *req) {
    char reply[4096], msg[4608];
    (void)req;
    capture_ir_command_action(reply, sizeof(reply));
    snprintf(msg, sizeof(msg), "Capture result: %s", reply);
    render_page(fd, msg);
}

static void handle_ir_delete_device(int fd, const struct request *req) {
    char device_id[64], msg[512];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    delete_ir_device(device_id, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_ir_delete_command(int fd, const struct request *req) {
    char device_id[64], command[128], msg[512];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "command", command, sizeof(command));
    delete_ir_command(device_id, command, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_bt_device(int fd, const struct request *req) {
    char device_id[64], name[128], type[40], bdaddr[32], msg[512];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "name", name, sizeof(name));
    form_value(req->body, "type", type, sizeof(type));
    form_value(req->body, "bdaddr", bdaddr, sizeof(bdaddr));
    if (!type[0]) strcpy(type, "btkeyboard");
    upsert_bt_device(device_id, name, type, bdaddr, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_bt_delete_device(int fd, const struct request *req) {
    char device_id[64], msg[512];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    delete_bt_device(device_id, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_bt_command(int fd, const struct request *req) {
    char device_id[64], old_name[128], name[128], delay_text[24], msg[512];
    char *script;
    int delay_ms;
    if (req->body_truncated) {
        render_page(fd, "Bluetooth script was too large.");
        return;
    }
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "oldName", old_name, sizeof(old_name));
    form_value(req->body, "name", name, sizeof(name));
    form_value(req->body, "delayMs", delay_text, sizeof(delay_text));
    script = (char *)malloc(MAX_BT_SCRIPT_LEN);
    if (!script) {
        render_page(fd, "Not enough memory to save Bluetooth command.");
        return;
    }
    form_value(req->body, "script", script, MAX_BT_SCRIPT_LEN);
    delay_ms = atoi(delay_text);
    upsert_bt_command(device_id, old_name, name, script, delay_ms, msg, sizeof(msg));
    render_page(fd, msg);
    free(script);
}

static void handle_bt_delete_command(int fd, const struct request *req) {
    char device_id[64], command[128], msg[512];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "command", command, sizeof(command));
    delete_bt_command(device_id, command, msg, sizeof(msg));
    render_page(fd, msg);
}

static void handle_bt_send_command(int fd, const struct request *req) {
    char device_id[64], command[128], msg[1024];
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "command", command, sizeof(command));
    send_bt_saved_command(device_id, command, msg, sizeof(msg));
    render_page(fd, msg);
}

static void render_bt_saved_command_json(int fd, const struct request *req) {
    char device_id[64], command[128], msg[1024];
    int rc;
    FILE *f;
    form_value(req->body, "deviceId", device_id, sizeof(device_id));
    form_value(req->body, "command", command, sizeof(command));
    rc = send_bt_saved_command(device_id, command, msg, sizeof(msg));
    f = send_json_start(fd, rc == 0 ? "200 OK" : "400 Bad Request");
    if (!f) return;
    fputs("{\"ok\":", f); fputs(rc == 0 ? "true" : "false", f);
    fputs(",\"message\":", f); json_write_string(f, msg);
    fputs("}\n", f);
    fclose(f);
}

static void free_request(struct request *req) {
    if (!req) return;
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
}

static int read_request(int fd, struct request *req) {
    char *buf;
    char *header_end, *line_end, *p;
    int n = 0, clen, orig_clen, header_len;
    memset(req, 0, sizeof(*req));
    buf = (char *)malloc(MAX_REQUEST_BYTES);
    if (!buf) return -1;
    while (n < MAX_REQUEST_BYTES - 1) {
        int got = recv(fd, buf + n, MAX_REQUEST_BYTES - 1 - n, 0);
        if (got <= 0) {
            free(buf);
            return -1;
        }
        n += got;
        buf[n] = 0;
        header_end = strstr(buf, "\r\n\r\n");
        if (header_end) {
            header_len = (int)(header_end + 4 - buf);
            orig_clen = content_length(buf);
            if (orig_clen < 0) orig_clen = 0;
            clen = orig_clen;
            while (n < header_len + orig_clen && n < MAX_REQUEST_BYTES - 1) {
                got = recv(fd, buf + n, MAX_REQUEST_BYTES - 1 - n, 0);
                if (got <= 0) break;
                n += got;
                buf[n] = 0;
            }
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
                    snprintf(req->auth, sizeof(req->auth), "%.*s", (int)(e - v), v);
                }
                p = e + 2;
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
    if (read_request(client, &req) != 0) {
        free_request(&req);
        return;
    }
    if (!webui_auth_ok(&req)) {
        send_auth_required(client);
        free_request(&req);
        return;
    }
    if (req.body_truncated) {
        send_payload_too_large(client, &req);
        free_request(&req);
        return;
    }
    if (strcmp(req.method, "GET") == 0 && (strcmp(req.path, "/") == 0 || strcmp(req.path, "/index.html") == 0)) {
        render_page(client, "");
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/inventory") == 0) {
        render_inventory_json(client);
    } else if ((strcmp(req.method, "GET") == 0 || strcmp(req.method, "POST") == 0) &&
        strncmp(req.path, "/api/device-commands", 20) == 0 &&
        (req.path[20] == 0 || req.path[20] == '?')) {
        render_device_commands_json(client, &req);
    } else if ((strcmp(req.method, "GET") == 0 || strcmp(req.method, "POST") == 0) &&
        strncmp(req.path, "/api/remotecentral-fetch", 24) == 0 &&
        (req.path[24] == 0 || req.path[24] == '?')) {
        render_remotecentral_fetch_json(client, &req);
    } else if ((strcmp(req.method, "GET") == 0 || strcmp(req.method, "POST") == 0) && strcmp(req.path, "/api/capture") == 0) {
        render_capture_json(client);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/ir-send") == 0) {
        render_ir_send_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/ir-test-learned") == 0) {
        render_ir_test_learned_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/ir-batch-send") == 0) {
        render_ir_batch_send_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/ir-cancel") == 0) {
        render_ir_cancel_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/ir-lab-target") == 0) {
        render_ir_lab_target_json(client);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/ir-lab-clear") == 0) {
        render_ir_lab_clear_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/bt-call") == 0) {
        render_bluetooth_call_json(client, &req);
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/bt-text-status") == 0) {
        render_bluetooth_text_status_json(client);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/bt-text") == 0) {
        render_bluetooth_text_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/bt-saved-command") == 0) {
        render_bt_saved_command_json(client, &req);
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/update-status") == 0) {
        render_update_status_json(client);
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/update-check-state") == 0) {
        render_update_check_state_json(client);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/update-check-state") == 0) {
        render_update_check_state_post_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/update-begin") == 0) {
        render_update_begin_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/update-chunk") == 0) {
        render_update_chunk_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/update-apply") == 0) {
        render_update_apply_json(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/irdb-import") == 0) {
        render_irdb_import_json(client, &req);
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/export/bundle") == 0) {
        send_bundle_download(client);
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/export/devices") == 0) {
        send_file_download(client, DEVICE_LIST, "DeviceList.json", "application/json");
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/export/functions") == 0) {
        send_file_download(client, FUNCTION_LIST, "FunctionList.json", "application/json");
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/export/protocols") == 0) {
        send_file_download(client, PROTOCOL_LIST, "ProtocolList.json", "application/json");
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/export/mqtt") == 0) {
        send_file_download(client, MQTT_CONFIG, "mqtt-config.json", "application/json");
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/export/wifi") == 0) {
        send_file_download(client, WPA_CONFIG, "wpa_supplicant.conf", "text/plain");
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/export/cloud") == 0) {
        send_cloud_download(client);
    } else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/export/bluetooth") == 0) {
        send_bt_devices_download(client);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/mqtt") == 0) {
        handle_mqtt(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/wifi") == 0) {
        handle_wifi(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/system") == 0) {
        handle_system(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/import") == 0) {
        handle_import(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/send") == 0) {
        handle_ir_send(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/device") == 0) {
        handle_ir_device(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/new-device") == 0) {
        handle_ir_new_device(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/command") == 0) {
        handle_ir_command(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/update-command") == 0) {
        handle_ir_update_command(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/irdb-import") == 0) {
        handle_irdb_import(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/capture") == 0) {
        handle_ir_capture(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/delete-device") == 0) {
        handle_ir_delete_device(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/ir/delete-command") == 0) {
        handle_ir_delete_command(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/bt/device") == 0) {
        handle_bt_device(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/bt/delete-device") == 0) {
        handle_bt_delete_device(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/bt/command") == 0) {
        handle_bt_command(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/bt/delete-command") == 0) {
        handle_bt_delete_command(client, &req);
    } else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/bt/send-command") == 0) {
        handle_bt_send_command(client, &req);
    } else {
        send_text(client, "404 Not Found", "not found\n");
    }
    free_request(&req);
}

static void reap_children(void) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
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
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    int fd, one = 1;
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
    while (1) {
        int client;
        pid_t pid;
        reap_children();
        client = accept(fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            continue;
        }
        pid = fork();
        if (pid == 0) {
            close(fd);
            handle_client(client);
            close(client);
            _exit(0);
        }
        if (pid < 0) {
            handle_client(client);
        }
        close(client);
    }
}
