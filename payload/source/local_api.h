/* Local API shares the proven hardware senders above, not their old HTTP policy. */
#include "vendor/cJSON.h"
#include "vendor/monocypher.h"
#include "vendor/monocypher-ed25519.h"
#include <linux/input.h>
#ifndef LOCAL_ROOT
#define LOCAL_ROOT "/data/codex/local"
#endif
#ifndef LOCAL_WWW
#define LOCAL_WWW "/data/codex/www"
#endif
#ifndef LOCAL_OPS
#define LOCAL_OPS "/tmp/harmony-operations"
#endif
#define LOCAL_CONFIG LOCAL_ROOT "/config.json"
#define LOCAL_PAIRS LOCAL_ROOT "/controllers.json"
#define LOCAL_LOCK LOCAL_ROOT "/write.lock"
#define LOCAL_JOURNAL LOCAL_ROOT "/transaction"
#define LOCAL_RESERVE (128 * 1024)
#define LOCAL_MAX_OPS 32
#define LOCAL_PAIR_WINDOW LOCAL_OPS "/pair-window"
#ifndef LOCAL_BUTTON_DEVICE
#define LOCAL_BUTTON_DEVICE "/dev/input/event0"
#endif
static int local_button_available;

struct local_controller { char id[40], role[16], csrf[65]; };
static const char *local_files[] = {LOCAL_CONFIG, DEVICE_LIST, FUNCTION_LIST,
    PROTOCOL_LIST, BT_DEVICE_STORE, ACTIVITY_LIST, MAP_LIST};
static int local_validate_config(const cJSON *o);

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
static int local_random(char *out, size_t bytes) {
    unsigned char raw[32]; size_t n = 0; int fd;
    if (bytes > sizeof(raw)) return -1;
    fd = open("/dev/urandom", O_RDONLY); if (fd < 0) return -1;
    while (n < bytes) { ssize_t got = read(fd, raw + n, bytes - n); if (got <= 0) { close(fd); return -1; } n += got; }
    close(fd);
    for (n = 0; n < bytes; n++) sprintf(out + 2 * n, "%02x", raw[n]);
    out[bytes * 2] = 0; crypto_wipe(raw, sizeof(raw)); return 0;
}
static int local_hex(const char *hex, unsigned char *out, size_t bytes) {
    size_t i; if (strlen(hex) != bytes * 2) return -1;
    for (i = 0; i < bytes; i++) {
        int a = hexval(hex[i * 2]), b = hexval(hex[i * 2 + 1]);
        if (a < 0 || b < 0) return -1; out[i] = (unsigned char)((a << 4) | b);
    }
    return 0;
}
static void local_hash(const char *s, char out[65]) {
    unsigned char h[32]; size_t i;
    crypto_blake2b(h, 32, (const unsigned char *)s, strlen(s));
    for (i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", h[i]); out[64] = 0;
}
static int local_equal(const char *a, const char *b) {
    unsigned char x[32], y[32];
    return local_hex(a, x, 32) == 0 && local_hex(b, y, 32) == 0 && crypto_verify32(x, y) == 0;
}
static int local_lock(void) {
    int fd = open(LOCAL_LOCK, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return -1; }
    return fd;
}
static int local_space(size_t need) {
    struct statvfs s;
    return statvfs(LOCAL_ROOT, &s) == 0 &&
        (unsigned long long)s.f_bavail * s.f_frsize >= need + LOCAL_RESERVE;
}
static double local_milliseconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int local_has_owner(const cJSON *pairs) {
    const cJSON *p;
    cJSON_ArrayForEach(p, pairs) if (!strcmp(lj_str(p, "role"), "owner")) return 1;
    return 0;
}
static cJSON *local_pair_window(void) {
    cJSON *window = lj_read(LOCAL_PAIR_WINDOW, 1024);
    double created = cJSON_GetNumberValue(lj_get(window, "created")), now = local_milliseconds();
    if (!window || !(created > 0 && created <= now && now - created < 90000)) { cJSON_Delete(window); return NULL; }
    return window;
}
static void local_pair_window_clear(const char *id) {
    cJSON *window = lj_read(LOCAL_PAIR_WINDOW, 1024);
    if (!strcmp(lj_str(window, "id"), id)) unlink(LOCAL_PAIR_WINDOW);
    cJSON_Delete(window);
}
static int local_pair_window_start(const char *id) {
    struct timeval now; cJSON *window = cJSON_CreateObject(); int rc;
    gettimeofday(&now, NULL);
    cJSON_AddStringToObject(window, "id", id); cJSON_AddNumberToObject(window, "created", local_milliseconds());
    cJSON_AddNumberToObject(window, "wall", (double)now.tv_sec * 1000 + now.tv_usec / 1000);
    rc = lj_write(LOCAL_PAIR_WINDOW, window); cJSON_Delete(window); return rc;
}
static int local_copy(const char *from, const char *to) {
    size_t n; char *raw = read_file_alloc(from, MAX_RESOURCE_FILE, &n); int rc;
    if (!raw) return -1; rc = write_file_atomic(to, raw, n); free(raw); return rc;
}
static int local_recover(void) {
    size_t i; char path[320], absent[320]; int rc = 0;
    if (access(LOCAL_JOURNAL "/pending", F_OK) != 0) return 0;
    for (i = 0; i < sizeof(local_files) / sizeof(local_files[0]); i++) {
        snprintf(path, sizeof(path), LOCAL_JOURNAL "/%u", (unsigned)i);
        snprintf(absent, sizeof(absent), "%s.absent", path);
        if (access(absent, F_OK) == 0) unlink(local_files[i]);
        else if (local_copy(path, local_files[i]) != 0) rc = -1;
    }
    if (rc == 0) { unlink(LOCAL_JOURNAL "/pending"); request_resource_reload(); }
    return rc;
}
static int local_begin(void) {
    size_t i, need = MAX_REQUEST_BODY; struct stat s; char path[320], absent[320];
    if (local_recover() != 0) return -1;
    for (i = 0; i < sizeof(local_files) / sizeof(local_files[0]); i++)
        if (stat(local_files[i], &s) == 0) need += s.st_size;
    if (!local_space(need)) return -1;
    mkdir(LOCAL_JOURNAL, 0700);
    for (i = 0; i < sizeof(local_files) / sizeof(local_files[0]); i++) {
        snprintf(path, sizeof(path), LOCAL_JOURNAL "/%u", (unsigned)i);
        snprintf(absent, sizeof(absent), "%s.absent", path);
        unlink(absent);
        if (access(local_files[i], F_OK) == 0) { if (local_copy(local_files[i], path) != 0) return -1; }
        else if (write_file_atomic(absent, "", 0) != 0) return -1;
    }
    return write_file_atomic(LOCAL_JOURNAL "/pending", "1", 1);
}
static cJSON *local_config(void) { return lj_read(LOCAL_CONFIG, MAX_REQUEST_BODY); }
static int local_commit(cJSON *config) {
    cJSON *revision = lj_get(config, "revision");
    if (!cJSON_IsNumber(revision) || revision->valueint >= 2000000000) return -1;
    cJSON_SetNumberValue(revision, revision->valueint + 1);
    if (lj_write(LOCAL_CONFIG, config) != 0) return -1;
    if (unlink(LOCAL_JOURNAL "/pending") != 0) return -1;
    { int d = open(LOCAL_JOURNAL, O_RDONLY); if (d >= 0) { fsync(d); close(d); } }
    local_copy(LOCAL_CONFIG, LOCAL_ROOT "/last-good.json");
    return 0;
}
static int local_initialize(void) {
    cJSON *config; int lock;
    mkdir(LOCAL_ROOT, 0700); mkdir(LOCAL_OPS, 0700);
    lock = local_lock(); if (lock < 0) return -1;
    if (local_recover() != 0) { close(lock); return -1; }
    config = local_config();
    if (!config && access(LOCAL_CONFIG, F_OK) == 0) {
        config = lj_read(LOCAL_ROOT "/last-good.json", MAX_REQUEST_BODY);
        if (!config || lj_write(LOCAL_CONFIG, config) != 0) { cJSON_Delete(config); close(lock); return -1; }
    }
    if (!config) {
        config = lj_parse("{\"schemaVersion\":1,\"revision\":1,\"setupMode\":\"unselected\",\"layouts\":{},\"activities\":[]}");
        if (!config || !local_space(4096) || lj_write(LOCAL_CONFIG, config) != 0) { cJSON_Delete(config); close(lock); return -1; }
    }
    if (!local_validate_config(config)) { cJSON_Delete(config); close(lock); return -1; }
    cJSON_Delete(config); close(lock); return 0;
}
static int local_claim_code(void) {
    char code[33], hash[65]; cJSON *pairs;
    if (local_initialize() != 0) return 1;
    pairs = lj_read(LOCAL_PAIRS, 32768);
    if (access(LOCAL_PAIRS, F_OK) == 0 && !cJSON_IsArray(pairs)) { cJSON_Delete(pairs); return 1; }
    if (local_has_owner(pairs)) { cJSON_Delete(pairs); fprintf(stderr, "Already claimed; ownership was preserved.\n"); return 0; }
    cJSON_Delete(pairs);
    if (local_random(code, 16) != 0) return 1;
    local_hash(code, hash);
    if (write_file_atomic(LOCAL_ROOT "/claim.hash", hash, 64) != 0) return 1;
    printf("%s\n", code); return 0;
}
static int local_read_header(struct request *r, char *p, char *end) {
    struct { const char *name; char *out; size_t size; } fields[] = {
        {"Host:", r->host, sizeof(r->host)}, {"Origin:", r->origin, sizeof(r->origin)},
        {"Cookie:", r->cookie, sizeof(r->cookie)}, {"X-Harmony-CSRF:", r->csrf, sizeof(r->csrf)},
        {"X-Harmony-Revision:", r->revision, sizeof(r->revision)}};
    size_t i;
    if (strncasecmp(p, "Transfer-Encoding:", 18) == 0) return -1;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        size_t n = strlen(fields[i].name);
        if (strncasecmp(p, fields[i].name, n) == 0) {
            char *v = p + n;
            if (fields[i].out[0]) return -1;
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            if ((size_t)(end - v) >= fields[i].size) return -1;
            memcpy(fields[i].out, v, end - v); fields[i].out[end - v] = 0;
        }
    }
    return 0;
}
static int local_origin(int fd, const struct request *r) {
    struct sockaddr_in address; socklen_t n = sizeof(address); char host[128], origin[160];
    if (getsockname(fd, (struct sockaddr *)&address, &n) != 0) return 0;
    if (!strcmp(inet_ntoa(address.sin_addr), "192.168.76.1")) return 0;
    snprintf(host, sizeof(host), "%s:%u", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
    if (strcmp(r->host, host) != 0) return 0;
    snprintf(origin, sizeof(origin), "http://%s", host);
    return !r->origin[0] || strcmp(r->origin, origin) == 0;
}
static void local_token(const struct request *r, char token[65]) {
    const char *p; size_t n;
    token[0] = 0;
    if (strncmp(r->auth, "Bearer ", 7) == 0) p = r->auth + 7;
    else {
        p = r->cookie;
        while (*p) { while (*p == ';' || *p == ' ') p++; if (strncmp(p, "harmony=", 8) == 0) { p += 8; break; } p = strchr(p, ';'); if (!p) return; }
        if (!*p) return;
    }
    n = strcspn(p, "; \t\r\n"); if (n != 64) return;
    memcpy(token, p, 64); token[64] = 0;
}
static int local_controller(const struct request *r, struct local_controller *ctl) {
    char token[65], hash[65], internal[80]; cJSON *pairs, *p;
    memset(ctl, 0, sizeof(*ctl)); local_token(r, token); if (!token[0]) return 0;
    local_hash(token, hash);
    if (strncmp(r->auth, "Bearer ", 7) == 0 && read_text(LOCAL_ROOT "/internal.key", internal, sizeof(internal)) > 0) {
        chomp(internal);
        if (local_equal(token, internal)) { strcpy(ctl->id, "internal"); strcpy(ctl->role, "control"); strcpy(ctl->csrf, token); return 1; }
    }
    pairs = lj_read(LOCAL_PAIRS, 32768);
    cJSON_ArrayForEach(p, pairs) {
        if (local_equal(hash, lj_str(p, "tokenHash"))) {
            if (strcmp(lj_str(p, "role"), "pending") == 0 && lj_int(p, "expires", 0) < time(NULL)) break;
            snprintf(ctl->id, sizeof(ctl->id), "%s", lj_str(p, "id"));
            snprintf(ctl->role, sizeof(ctl->role), "%s", lj_str(p, "role"));
            snprintf(ctl->csrf, sizeof(ctl->csrf), "%s", lj_str(p, "csrf"));
            cJSON_Delete(pairs); return 1;
        }
    }
    cJSON_Delete(pairs); return 0;
}
static int local_auth(int fd, const struct request *r, struct local_controller *ctl, int owner) {
    if (!local_origin(fd, r)) { local_error(fd, "403 Forbidden", "Invalid Host or request origin."); return 0; }
    if (!local_controller(r, ctl) || strcmp(ctl->role, "pending") == 0) { local_error(fd, "401 Unauthorized", "Pair this controller first."); return 0; }
    if (owner && strcmp(ctl->role, "owner") != 0) { local_error(fd, "403 Forbidden", "Only the owner can change this setting."); return 0; }
    if (strcmp(r->method, "GET") != 0 && !local_equal(r->csrf, ctl->csrf)) { local_error(fd, "403 Forbidden", "Missing or invalid request token."); return 0; }
    return 1;
}
static void local_pair_reply(int fd, const char *token, const cJSON *pair) {
    char *raw; cJSON *out = cJSON_CreateObject(); FILE *f;
    cJSON_AddBoolToObject(out, "ok", 1); cJSON_AddStringToObject(out, "id", lj_str(pair, "id"));
    cJSON_AddStringToObject(out, "role", lj_str(pair, "role")); cJSON_AddStringToObject(out, "csrf", lj_str(pair, "csrf"));
    raw = cJSON_PrintUnformatted(out); f = fdopen(dup(fd), "w");
    if (f) { fprintf(f, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nSet-Cookie: harmony=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=31536000\r\n\r\n%s", token, raw ? raw : "{}"); fclose(f); }
    free(raw); cJSON_Delete(out);
}
static void local_pair(int fd, const struct request *r, int claim) {
    cJSON *body = lj_parse(r->body), *pairs, *pair; char token[65], hash[65], id[33], csrf[65], stored[80];
    int lock = local_lock(), button = !claim && cJSON_IsTrue(lj_get(body, "button")); const char *name = lj_str(body, "name"); struct stat attempt;
    if (!local_origin(fd, r)) { local_error(fd, "403 Forbidden", "Invalid request origin."); goto done; }
    if (lock < 0) { local_error(fd, "409 Conflict", "Another change is being saved."); goto done; }
    if (!body || !name[0] || strlen(name) > 64) { local_error(fd, "400 Bad Request", "Enter a controller name."); goto done; }
    pairs = lj_read(LOCAL_PAIRS, 32768);
    if (access(LOCAL_PAIRS, F_OK) == 0 && !cJSON_IsArray(pairs)) { cJSON_Delete(pairs); local_error(fd, "503 Service Unavailable", "Controller registry needs owner SSH recovery."); goto done; }
    if (!pairs) pairs = cJSON_CreateArray();
    if (claim) {
        if (local_has_owner(pairs)) { cJSON_Delete(pairs); local_error(fd, "409 Conflict", "This hub already has an owner."); goto done; }
        /* Rate limit survives HTTP workers and cannot be reset by a bad code. */
        if (stat(LOCAL_OPS "/claim-attempt", &attempt) == 0 && time(NULL) - attempt.st_mtime < 2) {
            cJSON_Delete(pairs); local_error(fd, "429 Too Many Requests", "Wait before trying another code."); goto done;
        }
        write_file_atomic(LOCAL_OPS "/claim-attempt", "", 0);
        local_hash(lj_str(body, "code"), hash);
        if (read_text(LOCAL_ROOT "/claim.hash", stored, sizeof(stored)) < 0 || !local_equal(hash, stored)) {
            cJSON_Delete(pairs); local_error(fd, "403 Forbidden", "The installer code is incorrect or has been used."); goto done;
        }
    } else if (!button && !local_has_owner(pairs)) {
        cJSON_Delete(pairs); local_error(fd, "409 Conflict", "Use the Pair button or installer code to claim the hub."); goto done;
    }
    if (button) {
        cJSON *window = local_pair_window();
        if (!local_button_available) { cJSON_Delete(window); cJSON_Delete(pairs); local_error(fd, "503 Service Unavailable", "The Pair button is unavailable. Use owner approval or the installer code."); goto done; }
        if (window) { cJSON_Delete(window); cJSON_Delete(pairs); local_error(fd, "409 Conflict", "Another browser is waiting for the Pair button. Wait up to 90 seconds, then try again."); goto done; }
    }
    { cJSON *p, *next; for (p = pairs->child; p; p = next) { next = p->next;
        if (strcmp(lj_str(p, "role"), "pending") == 0 && lj_int(p, "expires", 0) < time(NULL)) cJSON_Delete(cJSON_DetachItemViaPointer(pairs, p)); } }
    if (cJSON_GetArraySize(pairs) >= 16 || local_random(token, 32) || local_random(id, 16) || local_random(csrf, 32)) {
        cJSON_Delete(pairs); local_error(fd, "503 Service Unavailable", "Controller limit reached or random source unavailable."); goto done;
    }
    pair = cJSON_CreateObject(); local_hash(token, hash);
    cJSON_AddStringToObject(pair, "id", id); cJSON_AddStringToObject(pair, "name", name);
    cJSON_AddStringToObject(pair, "role", claim ? "owner" : "pending");
    cJSON_AddStringToObject(pair, "tokenHash", hash); cJSON_AddStringToObject(pair, "csrf", csrf);
    cJSON_AddNumberToObject(pair, "expires", time(NULL) + (button ? 90 : 600)); cJSON_AddItemToArray(pairs, pair);
    if (button && local_pair_window_start(id)) { cJSON_Delete(pairs); local_error(fd, "503 Service Unavailable", "Pairing could not start. Try again."); goto done; }
    if (!local_space(32768) || lj_write(LOCAL_PAIRS, pairs) != 0) { if (button) local_pair_window_clear(id); local_error(fd, "507 Insufficient Storage", "Unable to save controller pairing."); }
    else { if (claim) { unlink(LOCAL_ROOT "/claim.hash"); unlink(LOCAL_PAIR_WINDOW); } local_pair_reply(fd, token, pair); }
    cJSON_Delete(pairs);
done:
    if (lock >= 0) close(lock); cJSON_Delete(body);
}
static void local_pair_cancel(int fd, const struct request *r) {
    struct local_controller ctl; cJSON *pairs, *p; int lock;
    if (!local_origin(fd, r) || !local_controller(r, &ctl) || !strcmp(ctl.id, "internal") || !local_equal(r->csrf, ctl.csrf)) {
        local_error(fd, "403 Forbidden", "Only the requesting browser can cancel pairing."); return;
    }
    lock = local_lock(); if (lock < 0) { local_error(fd, "409 Conflict", "Another change is being saved."); return; }
    pairs = lj_read(LOCAL_PAIRS, 32768);
    cJSON_ArrayForEach(p, pairs) if (!strcmp(lj_str(p, "id"), ctl.id) && !strcmp(lj_str(p, "role"), "pending")) {
        cJSON_Delete(cJSON_DetachItemViaPointer(pairs, p)); break;
    }
    if (!cJSON_IsArray(pairs) || lj_write(LOCAL_PAIRS, pairs)) local_error(fd, "507 Insufficient Storage", "Pairing could not be cancelled.");
    else { local_pair_window_clear(ctl.id); send_text(fd, "200 OK", "{\"ok\":true}"); }
    cJSON_Delete(pairs); close(lock);
}
static void local_pair_upgrade(int fd, const struct request *r) {
    struct local_controller ctl; cJSON *window; int lock;
    if (!local_auth(fd, r, &ctl, 0)) return;
    if (strcmp(ctl.role, "control") || !strcmp(ctl.id, "internal")) { local_error(fd, "409 Conflict", "This browser already has full access or is not a browser controller."); return; }
    lock = local_lock(); if (lock < 0) { local_error(fd, "409 Conflict", "Another change is being saved."); return; }
    window = local_pair_window();
    if (!local_button_available) local_error(fd, "503 Service Unavailable", "The Pair button is unavailable.");
    else if (window) local_error(fd, "409 Conflict", "Another browser is waiting for the Pair button. Wait up to 90 seconds, then try again.");
    else if (local_pair_window_start(ctl.id)) local_error(fd, "503 Service Unavailable", "Pairing could not start. Try again.");
    else send_text(fd, "200 OK", "{\"ok\":true}");
    cJSON_Delete(window); close(lock);
}

/* Nonexclusive evdev observation leaves stock handheld/recovery behavior intact. */
struct local_button { double pressed, wall; char request[40]; int dropped; };
static void local_button_event(struct local_button *button, const struct input_event *event) {
    double now = local_milliseconds();
    if (event->type == EV_SYN && event->code == SYN_DROPPED) { memset(button, 0, sizeof(*button)); button->dropped = 1; return; }
    if (button->dropped) { if (event->type == EV_SYN && event->code == SYN_REPORT) button->dropped = 0; return; }
    if (event->type != EV_KEY || event->code != KEY_A) return;
    if (event->value == 1) {
        cJSON *window = local_pair_window();
        memset(button, 0, sizeof(*button)); button->pressed = now;
        button->wall = (double)event->time.tv_sec * 1000 + event->time.tv_usec / 1000;
        if (window && button->wall > cJSON_GetNumberValue(lj_get(window, "wall")))
            snprintf(button->request, sizeof(button->request), "%s", lj_str(window, "id"));
        cJSON_Delete(window);
    } else if (event->value == 0) {
        double duration = (double)event->time.tv_sec * 1000 + event->time.tv_usec / 1000 - button->wall;
        int lock;
        if (button->request[0] && now - button->pressed < 2000 && duration >= 50 && duration < 2000 && (lock = local_lock()) >= 0) {
            cJSON *window = local_pair_window(), *pairs = lj_read(LOCAL_PAIRS, 32768), *p;
            if (window && !strcmp(button->request, lj_str(window, "id"))) cJSON_ArrayForEach(p, pairs) {
                if (!strcmp(lj_str(p, "id"), button->request) &&
                    ((!strcmp(lj_str(p, "role"), "pending") && lj_int(p, "expires", 0) >= time(NULL)) || !strcmp(lj_str(p, "role"), "control"))) {
                    cJSON_ReplaceItemInObjectCaseSensitive(p, "role", cJSON_CreateString("owner"));
                    if (local_space(32768) && !lj_write(LOCAL_PAIRS, pairs)) {
                        unlink(LOCAL_PAIR_WINDOW); unlink(LOCAL_ROOT "/claim.hash");
                    }
                    break;
                }
            }
            cJSON_Delete(window); cJSON_Delete(pairs); close(lock);
        }
        memset(button, 0, sizeof(*button));
    }
}
static int local_legacy_authorize(int fd, const struct request *r) {
    struct local_controller ctl;
    if (strstr(r->path, "remotecentral")) { local_error(fd, "410 Gone", "Community searches run in the browser."); return 0; }
    /* Old write forms cannot bypass revisions, pairing or new update signatures. */
    if (strcmp(r->method, "GET") != 0) { local_error(fd, "410 Gone", "Use the versioned API for configuration and commands."); return 0; }
    if (strncmp(r->path, "/api/", 5) == 0 && strcmp(r->path, "/api/capture") == 0) {
        local_error(fd, "405 Method Not Allowed", "Learning requires an authenticated POST."); return 0;
    }
    return local_auth(fd, r, &ctl, strncmp(r->path, "/export/", 8) == 0);
}
static int local_inventory_complete = 1;
static void local_inventory_dir(cJSON *files, const char *path, int depth) {
    DIR *dir; struct dirent *e; char child[512]; struct stat st;
    if (depth > 8 || cJSON_GetArraySize(files) >= 512) { local_inventory_complete = 0; return; }
    if (!(dir = opendir(path))) return;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= sizeof(child) || lstat(child, &st)) continue;
        if (S_ISDIR(st.st_mode)) local_inventory_dir(files, child, depth + 1);
        else if (S_ISREG(st.st_mode)) {
            if (st.st_size > MAX_RESOURCE_FILE || cJSON_GetArraySize(files) >= 512) { local_inventory_complete = 0; continue; }
            cJSON *file = cJSON_CreateObject(); cJSON_AddStringToObject(file, "path", child); cJSON_AddNumberToObject(file, "size", st.st_size); cJSON_AddNumberToObject(file, "mode", st.st_mode); cJSON_AddItemToArray(files, file);
        }
    }
    closedir(dir);
}
static int local_inventory(void) {
    const char *dirs[] = {"/data/codex", "/data/resources", "/data/codexmqtt", "/pkg/codexmqtt", "/data/luaworks/provision", "/etc/dropbear", "/home/root/.ssh", "/opt/luaworks/tasks/codex"};
    const char *paths[] = {"/etc/init.d/rcS.local", "/opt/luaworks/tasks/connectserver/netservicestarter.lua",
        "/opt/luaworks/tasks/connectserver/transport/hbushttpserverconnector.lua", "/opt/luaworks/tasks/connectserver/transport/xmppserverconnector.lua",
        "/usr/sbin/dropbear", "/usr/sbin/dropbearkey", "/etc/wpa_supplicant.conf", "/home/root/.ssh/authorized_keys", "/etc/tdeenable"};
    cJSON *out = cJSON_CreateObject(), *files = cJSON_CreateArray(); size_t i; char *raw; struct stat st;
    cJSON_AddItemToObject(out, "files", files);
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) local_inventory_dir(files, dirs[i], 0);
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) if (lstat(paths[i], &st) == 0 && S_ISREG(st.st_mode)) {
        cJSON *file = cJSON_CreateObject(); cJSON_AddStringToObject(file, "path", paths[i]); cJSON_AddNumberToObject(file, "size", st.st_size); cJSON_AddNumberToObject(file, "mode", st.st_mode); cJSON_AddItemToArray(files, file);
    }
    cJSON_AddBoolToObject(out, "complete", local_inventory_complete);
    raw = cJSON_PrintUnformatted(out); if (raw) puts(raw); free(raw); cJSON_Delete(out); return 0;
}

static int local_trim_logs(void) {
    const char *paths[] = {"/cache/codex-init.log", "/cache/codex-bthid-keyboard.log", "/cache/codex-recovery.log", "/cache/codex-coordinator.log"};
    char block[65536], backup[256]; size_t i, n;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *f; struct stat st;
        if (stat(paths[i], &st) || st.st_size <= sizeof(block)) continue;
        f = fopen(paths[i], "r+"); if (!f) continue;
        if (!fseek(f, -(long)sizeof(block), SEEK_END)) {
            n = fread(block, 1, sizeof(block), f); snprintf(backup, sizeof(backup), "%s.1", paths[i]);
            if (n && !write_file_atomic(backup, block, n)) ftruncate(fileno(f), 0);
        }
        fclose(f);
    }
    return 0;
}

static cJSON *local_form_json(const cJSON *body, struct request *r) {
    cJSON *item; size_t size = 1, n = 0; char *form;
    cJSON_ArrayForEach(item, body) if (item->string) size += strlen(item->string) * 3 + (cJSON_IsString(item) ? strlen(item->valuestring) * 3 : 32) + 2;
    if (size > MAX_REQUEST_BODY) return NULL;
    form = malloc(size); if (!form) return NULL;
    cJSON_ArrayForEach(item, body) {
        const char *s; char number[32]; int part;
        if (!item->string) continue;
        if (cJSON_IsString(item)) s = item->valuestring;
        else if (cJSON_IsNumber(item)) { snprintf(number, sizeof(number), "%d", item->valueint); s = number; }
        else if (cJSON_IsBool(item)) s = cJSON_IsTrue(item) ? "1" : "0";
        else continue;
        if (n) form[n++] = '&';
        for (part = 0; part < 2; part++) {
            const unsigned char *p = (const unsigned char *)(part ? s : item->string);
            while (*p) { if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') form[n++] = *p;
                else { sprintf(form + n, "%%%02X", *p); n += 3; } p++; }
            if (!part) form[n++] = '=';
        }
    }
    form[n] = 0; memset(r, 0, sizeof(*r)); strcpy(r->method, "POST"); r->body = form; r->body_len = n;
    return (cJSON *)body;
}
static cJSON *local_call_renderer(void (*render)(int, const struct request *), const cJSON *body) {
    int pair[2]; char buf[16384]; ssize_t n, used = 0; struct request r; cJSON *out = NULL;
    if (!local_form_json(body, &r)) return NULL;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) { free(r.body); return NULL; }
    render(pair[0], &r); shutdown(pair[0], SHUT_WR);
    while ((n = read(pair[1], buf + used, sizeof(buf) - 1 - used)) > 0) { used += n; if (used >= sizeof(buf) - 1) break; }
    buf[used] = 0;
    { char *p = strstr(buf, "\r\n\r\n"); if (p) out = lj_parse(p + 4); }
    close(pair[0]); close(pair[1]); free(r.body); return out;
}
static int local_ctl_active(const char *id) {
    cJSON *pairs, *p; int ok = 0;
    if (strcmp(id, "internal") == 0) return 1;
    pairs = lj_read(LOCAL_PAIRS, 32768);
    cJSON_ArrayForEach(p, pairs) if (strcmp(lj_str(p, "id"), id) == 0 && strcmp(lj_str(p, "role"), "pending") != 0) ok = 1;
    cJSON_Delete(pairs); return ok;
}
static void local_op_path(const char *id, char path[256], const char *suffix) {
    snprintf(path, 256, LOCAL_OPS "/%s%s", id, suffix);
}
static int local_op_done(const cJSON *op) {
    const char *state = lj_str(op, "state");
    return strcmp(state, "completed") == 0 || strcmp(state, "failed") == 0 || strcmp(state, "cancelled") == 0;
}
static int local_op_trim(void) {
    DIR *dir = opendir(LOCAL_OPS); struct dirent *e; char path[256], oldest[128] = ""; int count = 0;
    if (!dir) return -1;
    while ((e = readdir(dir))) if (strstr(e->d_name, ".json") && strlen(e->d_name) < 110) {
        cJSON *op; count++; snprintf(path, sizeof(path), LOCAL_OPS "/%s", e->d_name); op = lj_read(path, 8192);
        if (op && local_op_done(op) && (!oldest[0] || strcmp(e->d_name, oldest) < 0)) snprintf(oldest, sizeof(oldest), "%s", e->d_name);
        cJSON_Delete(op);
    }
    closedir(dir);
    if (count < LOCAL_MAX_OPS) return 0;
    if (!oldest[0]) return -1;
    snprintf(path, sizeof(path), LOCAL_OPS "/%s", oldest); unlink(path);
    oldest[strlen(oldest) - 5] = 0;
    local_op_path(oldest, path, ".cancel"); unlink(path);
    local_op_path(oldest, path, ".heartbeat"); unlink(path);
    snprintf(path, sizeof(path), IR_CANCEL_PREFIX "%s", oldest); unlink(path); return 0;
}
static cJSON *local_enqueue(const cJSON *body, const struct local_controller *ctl, const char *kind) {
    char id[80], random[17], path[256], text[40]; long seq = 0; cJSON *op;
    if (local_op_trim() != 0 || local_random(random, 8) != 0) return NULL;
    if (read_text(LOCAL_OPS "/sequence", text, sizeof(text)) > 0) seq = atol(text);
    if (seq < 0 || seq >= 1999999999) return NULL;
    seq++; snprintf(text, sizeof(text), "%ld", seq);
    if (write_file_atomic(LOCAL_OPS "/sequence", text, strlen(text)) != 0) return NULL;
    snprintf(id, sizeof(id), "%010ld_%s", seq, random);
    op = cJSON_CreateObject(); cJSON_AddStringToObject(op, "id", id);
    cJSON_AddStringToObject(op, "controllerId", ctl->id); cJSON_AddStringToObject(op, "kind", kind);
    cJSON_AddStringToObject(op, "state", "queued"); cJSON_AddNumberToObject(op, "createdAt", time(NULL));
    cJSON_AddItemToObject(op, "request", cJSON_Duplicate(body, 1));
    local_op_path(id, path, ".json");
    if (lj_write(path, op) != 0) { cJSON_Delete(op); return NULL; }
    local_op_path(id, path, ".heartbeat"); renew_ir_hold(path); return op;
}
static void local_op_state(cJSON *op, const char *state, const cJSON *result) {
    char path[256];
    cJSON_ReplaceItemInObjectCaseSensitive(op, "state", cJSON_CreateString(state));
    if (result) { cJSON_DeleteItemFromObjectCaseSensitive(op, "result"); cJSON_AddItemToObject(op, "result", cJSON_Duplicate(result, 1)); }
    local_op_path(lj_str(op, "id"), path, ".json"); lj_write(path, op);
}
static void local_cancel(const cJSON *op);
static int local_coordinator(void) {
    int singleton; signal(SIGPIPE, SIG_IGN);
    mkdir(LOCAL_OPS, 0700); singleton = open(LOCAL_OPS "/coordinator.lock", O_CREAT | O_RDWR, 0600);
    if (singleton < 0 || flock(singleton, LOCK_EX | LOCK_NB) != 0) return 1;
    {
        DIR *dir = opendir(LOCAL_OPS); struct dirent *e; char path[256];
        if (dir) {
            while ((e = readdir(dir))) if (strstr(e->d_name, ".json")) {
                cJSON *op; snprintf(path, sizeof(path), LOCAL_OPS "/%s", e->d_name); op = lj_read(path, 8192);
                if (op && strcmp(lj_str(op, "kind"), "activity") && !strcmp(lj_str(op, "state"), "running")) {
                    cJSON *error = lj_parse("{\"ok\":false,\"error\":\"Command coordinator restarted\"}");
                    local_cancel(op); local_op_state(op, "failed", error); cJSON_Delete(error);
                }
                cJSON_Delete(op);
            }
            closedir(dir);
        }
    }
    while (1) {
        DIR *dir = opendir(LOCAL_OPS); struct dirent *entry;
        char chosen[128] = "", path[256]; cJSON *op = NULL, *body, *result = NULL;
        if (dir) {
            while ((entry = readdir(dir))) if (strstr(entry->d_name, ".json") && strlen(entry->d_name) < 110) {
                cJSON *item; snprintf(path, sizeof(path), LOCAL_OPS "/%s", entry->d_name); item = lj_read(path, 8192);
                if (item && strcmp(lj_str(item, "kind"), "activity") != 0 && strcmp(lj_str(item, "state"), "queued") == 0 &&
                    (!chosen[0] || strcmp(entry->d_name, chosen) < 0)) snprintf(chosen, sizeof(chosen), "%s", entry->d_name);
                cJSON_Delete(item);
            }
            closedir(dir);
        }
        if (!chosen[0]) { usleep(50000); continue; }
        snprintf(path, sizeof(path), LOCAL_OPS "/%s", chosen); op = lj_read(path, 8192);
        if (!op) { usleep(50000); continue; }
        body = lj_get(op, "request"); local_op_path(lj_str(op, "id"), path, ".cancel");
        if (access(path, F_OK) == 0 || !local_ctl_active(lj_str(op, "controllerId"))) local_op_state(op, "cancelled", NULL);
        else {
            const char *kind = lj_str(op, "kind");
            local_op_state(op, "running", NULL);
            if (strcmp(kind, "hold") == 0) {
                char timer[256], stamp[40]; struct timespec now; long long last = 0;
                local_op_path(lj_str(op, "id"), timer, ".heartbeat");
                if (read_text(timer, stamp, sizeof(stamp)) > 0) last = atoll(stamp);
                clock_gettime(CLOCK_MONOTONIC, &now);
                if ((long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 - last > 1200) {
                    local_op_state(op, "cancelled", NULL); cJSON_Delete(op); continue;
                }
                cJSON_DeleteItemFromObjectCaseSensitive(body, "runId"); cJSON_AddStringToObject(body, "runId", lj_str(op, "id"));
                cJSON_DeleteItemFromObjectCaseSensitive(body, "phase"); cJSON_AddStringToObject(body, "phase", "start");
                result = local_call_renderer(render_ir_hold_json, body);
            } else if (strcmp(kind, "tap") == 0) result = local_call_renderer(render_ir_send_json, body);
            else if (strcmp(kind, "bluetooth") == 0) {
                char msg[1024]; int lock = lock_ir(msg, sizeof(msg)), rc = -1;
                if (lock >= 0) { rc = send_bt_saved_command(lj_str(body, "deviceId"), lj_str(body, "command"), msg, sizeof(msg)); close(lock); }
                result = cJSON_CreateObject(); cJSON_AddBoolToObject(result, "ok", rc == 0); cJSON_AddStringToObject(result, rc == 0 ? "reply" : "error", msg);
            }
            if (access(path, F_OK) == 0) local_op_state(op, "cancelled", result);
            else local_op_state(op, result && cJSON_IsTrue(lj_get(result, "ok")) ? "completed" : "failed", result);
            local_op_path(lj_str(op, "id"), path, ".heartbeat"); unlink(path);
        }
        cJSON_Delete(result); cJSON_Delete(op);
    }
    return 0;
}
static void local_cancel(const cJSON *op) {
    char path[256]; local_op_path(lj_str(op, "id"), path, ".cancel"); write_file_atomic(path, "1", 1);
    snprintf(path, sizeof(path), IR_CANCEL_PREFIX "%s", lj_str(op, "id")); write_file_atomic(path, "1", 1);
}
static void local_cancel_controller(const char *id) {
    DIR *dir = opendir(LOCAL_OPS); struct dirent *e; char path[256];
    if (!dir) return;
    while ((e = readdir(dir))) if (strstr(e->d_name, ".json")) {
        cJSON *op; snprintf(path, sizeof(path), LOCAL_OPS "/%s", e->d_name); op = lj_read(path, 8192);
        if (op && strcmp(lj_str(op, "controllerId"), id) == 0 && !local_op_done(op)) local_cancel(op);
        cJSON_Delete(op);
    }
    closedir(dir);
}
static void local_session(int fd, const struct request *r) {
    struct local_controller ctl; cJSON *out = cJSON_CreateObject(), *config = local_config(), *pairs = lj_read(LOCAL_PAIRS, 32768);
    int authenticated = local_controller(r, &ctl);
    if (!local_origin(fd, r)) { local_error(fd, "403 Forbidden", "Invalid request origin."); goto done; }
    cJSON_AddBoolToObject(out, "ok", 1); cJSON_AddBoolToObject(out, "claimed", local_has_owner(pairs));
    cJSON_AddBoolToObject(out, "buttonAvailable", local_button_available);
    cJSON_AddStringToObject(out, "role", authenticated ? ctl.role : "unpaired");
    if (authenticated) {
        cJSON *window = local_pair_window();
        cJSON_AddStringToObject(out, "id", ctl.id); cJSON_AddStringToObject(out, "csrf", ctl.csrf);
        cJSON_AddBoolToObject(out, "buttonPending", window && !strcmp(lj_str(window, "id"), ctl.id));
        cJSON_Delete(window);
    }
    if (authenticated && strcmp(ctl.role, "pending") != 0) {
        cJSON_AddNumberToObject(out, "revision", lj_int(config, "revision", 0));
        cJSON_AddStringToObject(out, "setupMode", lj_str(config, "setupMode"));
    }
    cJSON_AddBoolToObject(out, "freshSetupVerified", 0); cJSON_AddStringToObject(out, "platform", "Harmony Hub / 4.15.600");
    lj_reply(fd, "200 OK", out);
done:
    cJSON_Delete(out); cJSON_Delete(config); cJSON_Delete(pairs);
}
static void local_controllers(int fd, const struct request *r, const struct local_controller *ctl, cJSON *body) {
    cJSON *pairs = lj_read(LOCAL_PAIRS, 32768), *p, *out = cJSON_CreateArray(); int lock = -1;
    if (strcmp(r->method, "GET") != 0) {
        lock = local_lock(); if (lock < 0) { local_error(fd, "409 Conflict", "Another change is being saved."); goto done; }
        cJSON_Delete(pairs); pairs = lj_read(LOCAL_PAIRS, 32768);
        cJSON_ArrayForEach(p, pairs) if (strcmp(lj_str(p, "id"), lj_str(body, "id")) == 0) {
            const char *action = lj_str(body, "action");
            if (strcmp(lj_str(p, "id"), ctl->id) == 0) { local_error(fd, "409 Conflict", "The owner cannot revoke their own controller."); goto done; }
            if (strcmp(action, "approve") == 0 && strcmp(lj_str(p, "role"), "pending") == 0 && lj_int(p, "expires", 0) >= time(NULL))
                cJSON_ReplaceItemInObjectCaseSensitive(p, "role", cJSON_CreateString("control"));
            else if (strcmp(action, "revoke") == 0) { local_cancel_controller(lj_str(p, "id")); cJSON_Delete(cJSON_DetachItemViaPointer(pairs, p)); }
            else { local_error(fd, "400 Bad Request", "Invalid approval or expired request."); goto done; }
            if (lj_write(LOCAL_PAIRS, pairs) != 0) { local_error(fd, "507 Insufficient Storage", "Pairing change could not be saved."); goto done; }
            local_pair_window_clear(lj_str(body, "id"));
            break;
        }
    }
    cJSON_ArrayForEach(p, pairs) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", lj_str(p, "id")); cJSON_AddStringToObject(item, "name", lj_str(p, "name"));
        cJSON_AddStringToObject(item, "role", lj_str(p, "role")); cJSON_AddNumberToObject(item, "expires", lj_int(p, "expires", 0));
        cJSON_AddItemToArray(out, item);
    }
    lj_reply(fd, "200 OK", out);
done:
    if (lock >= 0) close(lock); cJSON_Delete(pairs); cJSON_Delete(out);
}

static int local_validate_config(const cJSON *o) {
    cJSON *activities = lj_get(o, "activities"), *layouts = lj_get(o, "layouts"), *a, *b;
    cJSON *revision = lj_get(o, "revision"); const char *mode = lj_str(o, "setupMode");
    if (!cJSON_IsObject(o) || lj_int(o, "schemaVersion", 0) != 1 || !cJSON_IsArray(activities) ||
        cJSON_GetArraySize(activities) > 16 || !cJSON_IsObject(layouts) || !cJSON_IsNumber(revision) ||
        revision->valuedouble != revision->valueint || revision->valueint < 1 || revision->valueint >= 2000000000 ||
        (strcmp(mode, "unselected") && strcmp(mode, "keep") && strcmp(mode, "restore"))) return 0;
    cJSON_ArrayForEach(a, layouts) {
        if (!a->string || strlen(a->string) > 64 || !cJSON_IsArray(a) || cJSON_GetArraySize(a) > 80) return 0;
        cJSON_ArrayForEach(b, a) {
            cJSON *other;
            if (!cJSON_IsObject(b) || strlen(lj_str(b, "command")) > 127 || strlen(lj_str(b, "label")) > 64 ||
                !safe_label(lj_str(b, "command")) || !safe_run_id(lj_str(b, "slot")) || strlen(lj_str(b, "slot")) > 32) return 0;
            for (other = b->next; other; other = other->next) if (!strcmp(lj_str(b, "slot"), lj_str(other, "slot"))) return 0;
        }
    }
    cJSON_ArrayForEach(a, activities) {
        cJSON *steps = lj_get(a, "steps"), *exits = lj_get(a, "exitSteps"), *buttons = lj_get(a, "buttons"); int total = 0, pass;
        if (!safe_run_id(lj_str(a, "id")) || strlen(lj_str(a, "id")) > 64 || !safe_label(lj_str(a, "name")) || strlen(lj_str(a, "name")) > 64) return 0;
        for (b = a->next; b; b = b->next) if (strcmp(lj_str(a, "id"), lj_str(b, "id")) == 0) return 0;
        if (!cJSON_IsArray(steps) || cJSON_GetArraySize(steps) > 64 || !cJSON_IsArray(exits) || cJSON_GetArraySize(exits) > 64) return 0;
        if (buttons && (!cJSON_IsArray(buttons) || cJSON_GetArraySize(buttons) > 32)) return 0;
        cJSON_ArrayForEach(b, buttons) {
            cJSON *other;
            if (!safe_run_id(lj_str(b, "slot")) || strlen(lj_str(b, "slot")) > 32 ||
                !safe_label(lj_str(b, "deviceId")) || strlen(lj_str(b, "deviceId")) > 64 ||
                !safe_label(lj_str(b, "command")) || strlen(lj_str(b, "command")) > 127 || strlen(lj_str(b, "label")) > 64) return 0;
            for (other = b->next; other; other = other->next) if (!strcmp(lj_str(b, "slot"), lj_str(other, "slot"))) return 0;
        }
        for (pass = 0; pass < 2; pass++) cJSON_ArrayForEach(b, (pass ? exits : steps)) {
            int delay = lj_int(b, "delayMs", 0);
            if (delay < 0 || delay > 30000) return 0; total += delay;
            if (strcmp(lj_str(b, "kind"), "delay") != 0 &&
                (!safe_label(lj_str(b, "deviceId")) || strlen(lj_str(b, "deviceId")) > 64 ||
                 !safe_label(lj_str(b, "command")) || strlen(lj_str(b, "command")) > 127 ||
                 (strcmp(lj_str(b, "transport"), "ir") && strcmp(lj_str(b, "transport"), "bluetooth")))) return 0;
        }
        if (total > 120000) return 0;
    }
    return 1;
}
static void local_configuration_change(int fd, const struct request *r, cJSON *body) {
    int lock = local_lock(), ir_lock = -1, rc = -1; cJSON *config = NULL, *out = cJSON_CreateObject(); char msg[1024] = "", created[64] = "";
    const char *path = r->path;
    if (lock < 0) { local_error(fd, "409 Conflict", "Another configuration change is in progress."); goto done; }
    ir_lock = lock_ir(msg, sizeof(msg));
    if (ir_lock < 0) { local_error(fd, "409 Conflict", "Release the remote button before changing configuration."); goto done; }
    config = local_config();
    if (!config || lj_int(body, "revision", -1) != lj_int(config, "revision", 0)) {
        local_error(fd, "409 Conflict", "Configuration changed in another browser. Reload before saving."); goto done;
    }
    if (local_begin() != 0) { local_error(fd, "507 Insufficient Storage", "Not enough space for a recoverable change."); goto done; }
    local_transaction_active = 1;
    if (strcmp(path, "/api/v1/configuration") == 0) {
        cJSON *next = lj_get(body, "configuration");
        if (local_validate_config(next)) {
            cJSON_Delete(config); config = cJSON_Duplicate(next, 1);
            cJSON_ReplaceItemInObjectCaseSensitive(config, "revision", cJSON_CreateNumber(lj_int(body, "revision", 0))); rc = 0;
        } else snprintf(msg, sizeof(msg), "Invalid configuration, layout or activity sequence.");
    } else if (strcmp(path, "/api/v1/setup") == 0) {
        const char *mode = lj_str(body, "mode");
        if (strcmp(mode, "keep") == 0 || strcmp(mode, "restore") == 0) {
            cJSON_ReplaceItemInObjectCaseSensitive(config, "setupMode", cJSON_CreateString(mode)); rc = 0;
        } else snprintf(msg, sizeof(msg), "Fresh initialization has not passed the separate blank-hub recovery test. Keep or restore a known setup.");
    } else if (strcmp(path, "/api/v1/devices") == 0) {
        const char *action = lj_str(body, "action"), *id = lj_str(body, "deviceId");
        if (strcmp(lj_str(body, "transport"), "bluetooth") == 0) {
            if (!strcmp(action, "delete")) rc = delete_bt_device(id, msg, sizeof(msg));
            else if (bt_type_allowed(lj_str(body, "type"))) rc = upsert_bt_device(id, lj_str(body, "name"), lj_str(body, "type"), lj_str(body, "bdaddr"), msg, sizeof(msg));
        } else if (!strcmp(action, "create")) rc = create_ir_device_ex(lj_str(body, "name"), lj_str(body, "manufacturer"), lj_str(body, "model"), lj_str(body, "type"), msg, sizeof(msg), created, sizeof(created));
        else if (!strcmp(action, "delete")) rc = delete_ir_device(id, msg, sizeof(msg));
        else if (!strcmp(action, "update")) rc = update_ir_device(id, lj_str(body, "name"), lj_str(body, "manufacturer"), lj_str(body, "model"), lj_str(body, "type"), msg, sizeof(msg));
    } else if (strcmp(path, "/api/v1/commands/save") == 0) {
        if (!strcmp(lj_str(body, "transport"), "bluetooth"))
            rc = upsert_bt_command(lj_str(body, "deviceId"), lj_str(body, "oldName"), lj_str(body, "name"), lj_str(body, "script"), lj_int(body, "delayMs", 35), msg, sizeof(msg));
        else if (!strcmp(lj_str(body, "action"), "delete")) rc = delete_ir_command(lj_str(body, "deviceId"), lj_str(body, "name"), msg, sizeof(msg));
        else if (lj_str(body, "oldName")[0]) rc = update_ir_command(lj_str(body, "deviceId"), lj_str(body, "oldName"), lj_str(body, "name"), lj_str(body, "mode"), lj_str(body, "protocol"), lj_str(body, "nec"), lj_str(body, "keycode"), lj_str(body, "raw"), msg, sizeof(msg));
        else rc = add_ir_command(lj_str(body, "deviceId"), lj_str(body, "name"), lj_str(body, "mode"), lj_str(body, "protocol"), lj_str(body, "nec"), lj_str(body, "keycode"), lj_str(body, "raw"), msg, sizeof(msg));
    } else if (strcmp(path, "/api/v1/commands/import") == 0) {
        char *payload = strdup(lj_str(body, "payload"));
        if (payload) { rc = bulk_import_irdb_commands(lj_str(body, "deviceId"), payload, msg, sizeof(msg)); free(payload); }
    } else if (strcmp(path, "/api/v1/backups/restore") == 0) {
        cJSON *backup = lj_get(body, "backup"), *resources = lj_get(backup, "resources"), *next = lj_get(backup, "configuration"); size_t i;
        if (lj_int(backup, "schemaVersion", 0) != 1 || !local_validate_config(next) || !cJSON_IsObject(resources)) snprintf(msg, sizeof(msg), "Unsupported or malformed backup.");
        else {
            rc = 0;
            for (i = 1; i < sizeof(local_files) / sizeof(local_files[0]); i++) {
                const char *name = strrchr(local_files[i], '/') + 1; cJSON *v = lj_get(resources, name);
                const char *required[] = {"DevicesWithFeatures", "FunctionMaps", "Protocols"};
                if (!cJSON_IsObject(v) || (i <= 3 && !cJSON_IsArray(lj_get(v, required[i - 1])))) { rc = -1; break; }
            }
            if (rc == 0) {
                for (i = 1; i < sizeof(local_files) / sizeof(local_files[0]); i++) if (lj_write(local_files[i], lj_get(resources, strrchr(local_files[i], '/') + 1)) != 0) { rc = -1; break; }
                if (rc == 0) { cJSON_Delete(config); config = cJSON_Duplicate(next, 1); cJSON_ReplaceItemInObjectCaseSensitive(config, "revision", cJSON_CreateNumber(lj_int(body, "revision", 0))); request_resource_reload(); }
            }
        }
    }
    if (rc == 0 && local_commit(config) == 0) {
        cJSON_AddBoolToObject(out, "ok", 1); cJSON_AddNumberToObject(out, "revision", lj_int(config, "revision", 0));
        cJSON_AddStringToObject(out, "message", msg); if (created[0]) cJSON_AddStringToObject(out, "deviceId", created);
        lj_reply(fd, "200 OK", out);
    } else {
        if (local_recover() != 0) local_error(fd, "503 Service Unavailable", "Change failed; recovery needs owner SSH.");
        else local_error(fd, "400 Bad Request", msg[0] ? msg : "Change failed and was rolled back.");
    }
done:
    local_transaction_active = 0;
    if (local_reload_deferred) { local_reload_deferred = 0; request_resource_reload(); }
    if (ir_lock >= 0) close(ir_lock);
    if (lock >= 0) close(lock); cJSON_Delete(config); cJSON_Delete(out);
}
static void local_backup(int fd, int sensitive) {
    int lock = local_lock(); cJSON *out, *config, *resources; size_t i;
    if (lock < 0) { local_error(fd, "409 Conflict", "A configuration change is in progress. Try the backup again."); return; }
    out = cJSON_CreateObject(); config = local_config(); resources = cJSON_CreateObject();
    cJSON_AddNumberToObject(out, "schemaVersion", 1); cJSON_AddStringToObject(out, "kind", sensitive ? "sensitive-hub-backup" : "portable-setup");
    cJSON_AddItemToObject(out, "configuration", config); cJSON_AddItemToObject(out, "resources", resources);
    for (i = 1; i < sizeof(local_files) / sizeof(local_files[0]); i++) {
        cJSON *o = lj_read(local_files[i], MAX_RESOURCE_FILE);
        if (o) cJSON_AddItemToObject(resources, strrchr(local_files[i], '/') + 1, o);
        else cJSON_AddItemToObject(resources, strrchr(local_files[i], '/') + 1, cJSON_CreateObject());
    }
    if (sensitive) {
        char *wifi = read_file_alloc(WPA_CONFIG, 8192, NULL); cJSON *mqtt = lj_read(MQTT_CONFIG, 32768);
        cJSON_AddStringToObject(out, "wifi", wifi ? wifi : ""); free(wifi);
        if (mqtt) cJSON_AddItemToObject(out, "mqtt", mqtt);
        cJSON_AddStringToObject(out, "credentialRestore", "Wi-Fi and MQTT credentials require the SSH/USB recovery installer; browser restore changes portable setup only.");
    }
    close(lock); lj_reply(fd, "200 OK", out); cJSON_Delete(out);
}
static int local_asset(int fd, const struct request *r) {
    const char *name = NULL, *type = NULL; char path[256], header[768]; char *raw; size_t n;
    if (strcmp(r->method, "GET")) return 0;
    if (!strcmp(r->path, "/") || !strcmp(r->path, "/index.html")) { name = "index.html"; type = "text/html; charset=utf-8"; }
    else if (!strcmp(r->path, "/app.js")) { name = "app.js"; type = "text/javascript"; }
    else if (!strcmp(r->path, "/app.css")) { name = "app.css"; type = "text/css"; }
    else if (!strcmp(r->path, "/icons.svg")) { name = "icons.svg"; type = "image/svg+xml"; }
    else if (!strcmp(r->path, "/profiles.js")) { name = "profiles.js"; type = "text/javascript"; }
    if (!name) return 0;
    if (!local_origin(fd, r)) { local_error(fd, "403 Forbidden", "Invalid Host or origin."); return 1; }
    snprintf(path, sizeof(path), LOCAL_WWW "/%s", name); raw = read_file_alloc(path, MAX_REQUEST_BODY, &n);
    if (!raw) { local_error(fd, "503 Service Unavailable", "Local UI assets are missing; reinstall through SSH."); return 1; }
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self' https://raw.githubusercontent.com https://api.github.com; object-src 'none'; base-uri 'none'; frame-ancestors 'none'\r\nConnection: close\r\n\r\n", type, (unsigned long)n);
    send_all(fd, header, strlen(header)); send_all(fd, raw, n); free(raw); return 1;
}
static void local_operations(int fd, const struct request *r, cJSON *body, const struct local_controller *ctl) {
    char id[96], path[256]; cJSON *op;
    if (!strcmp(r->method, "GET")) query_value(r->path, "id", id, sizeof(id));
    else snprintf(id, sizeof(id), "%s", lj_str(body, "id"));
    if (!safe_run_id(id)) { local_error(fd, "400 Bad Request", "Invalid operation ID."); return; }
    local_op_path(id, path, ".json"); op = lj_read(path, 8192);
    if (!op || (strcmp(ctl->role, "owner") && strcmp(lj_str(op, "controllerId"), ctl->id) && strcmp(ctl->id, "internal"))) {
        cJSON_Delete(op); local_error(fd, "404 Not Found", "Operation is unavailable."); return;
    }
    if (!strcmp(r->method, "POST")) {
        const char *action = lj_str(body, "action");
        if (!strcmp(action, "cancel")) local_cancel(op);
        else if (!strcmp(action, "keepalive") && !local_op_done(op)) {
            local_op_path(id, path, ".heartbeat"); renew_ir_hold(path);
            snprintf(path, sizeof(path), IR_HOLD_PREFIX "%s", id);
            if (access(path, F_OK) == 0) renew_ir_hold(path);
        } else { cJSON_Delete(op); local_error(fd, "409 Conflict", "Operation is already stopped or action is invalid."); return; }
    }
    lj_reply(fd, "200 OK", op); cJSON_Delete(op);
}
static void local_activity_run(int fd, const cJSON *body, const struct local_controller *ctl) {
    int lock = local_lock(); cJSON *config = local_config(), *a, *request = NULL, *op;
    char current[96], path[256];
    if (lock < 0) { local_error(fd, "409 Conflict", "Another request is being queued."); goto done; }
    if (access(LOCAL_OPS "/core-ready", F_OK) != 0) { local_error(fd, "503 Service Unavailable", "The local activity service is not ready."); goto done; }
    if (read_text(LOCAL_OPS "/activity-current", current, sizeof(current)) > 0 && safe_run_id(current)) {
        local_op_path(current, path, ".json"); op = lj_read(path, 65536);
        if (op && !local_op_done(op)) { cJSON_Delete(op); local_error(fd, "409 Conflict", "Stop the current activity operation before switching."); goto done; }
        cJSON_Delete(op);
    }
    cJSON_ArrayForEach(a, lj_get(config, "activities")) if (!strcmp(lj_str(a, "id"), lj_str(body, "activityId"))) request = cJSON_Duplicate(a, 1);
    if (!strcmp(lj_str(body, "activityId"), "-1")) request = lj_parse("{\"id\":\"power-off\",\"name\":\"Power off\",\"steps\":[],\"exitSteps\":[],\"stopAll\":true}");
    if (!request) { local_error(fd, "404 Not Found", "Local activity not found. Native activities have not been converted."); goto done; }
    if (cJSON_IsTrue(lj_get(body, "fixState"))) cJSON_AddBoolToObject(request, "fixState", 1);
    { char *raw = cJSON_PrintUnformatted(request); int too_large = !raw || strlen(raw) > 7000; free(raw);
        if (too_large) { local_error(fd, "413 Payload Too Large", "Activity exceeds the runtime limit."); goto done; } }
    op = local_enqueue(request, ctl, "activity");
    if (!op) { local_error(fd, "429 Too Many Requests", "Operation queue is full."); goto done; }
    if (write_file_atomic(LOCAL_OPS "/activity-current", lj_str(op, "id"), strlen(lj_str(op, "id"))) != 0) {
        local_cancel(op); local_error(fd, "500 Internal Server Error", "Activity could not be queued.");
    } else lj_reply(fd, "202 Accepted", op);
    cJSON_Delete(op);
done:
    if (lock >= 0) close(lock); cJSON_Delete(config); cJSON_Delete(request);
}

#include "local_update.h"

static void local_mqtt(int fd, const struct request *r, const cJSON *body) {
    cJSON *config = lj_read(MQTT_CONFIG, 32768), *broker, *reply; int lock = -1;
    if (!config) config = lj_parse("{\"enabled\":false,\"baseTopic\":\"harmony/hub\",\"discoveryPrefix\":\"homeassistant\",\"clientId\":\"harmony-local\",\"haDiscovery\":true,\"broker\":{}}");
    broker = lj_get(config, "broker");
    if (!strcmp(r->method, "POST")) {
        lock = local_lock();
        if (lock < 0) { local_error(fd, "409 Conflict", "A configuration write is in progress."); goto done; }
        if (!cJSON_IsBool(lj_get(body, "enabled")) || strlen(lj_str(body, "host")) > 127 ||
            lj_int(body, "port", 0) < 1 || lj_int(body, "port", 0) > 65535 || strlen(lj_str(body, "password")) > 255 || strlen(lj_str(body, "username")) > 127) {
            local_error(fd, "400 Bad Request", "Invalid MQTT settings."); goto done;
        }
        if (cJSON_IsTrue(lj_get(body, "enabled"))) {
            struct in_addr address;
            if (inet_pton(AF_INET, lj_str(body, "host"), &address) != 1) { local_error(fd, "400 Bad Request", "Enter the broker IPv4 address so DNS cannot block the local remote."); goto done; }
        }
        cJSON_ReplaceItemInObjectCaseSensitive(config, "enabled", cJSON_CreateBool(cJSON_IsTrue(lj_get(body, "enabled"))));
        if (!broker) { broker = cJSON_CreateObject(); cJSON_AddItemToObject(config, "broker", broker); }
        cJSON_DeleteItemFromObjectCaseSensitive(broker, "host"); cJSON_AddStringToObject(broker, "host", lj_str(body, "host"));
        cJSON_DeleteItemFromObjectCaseSensitive(broker, "port"); cJSON_AddNumberToObject(broker, "port", lj_int(body, "port", 1883));
        cJSON_DeleteItemFromObjectCaseSensitive(broker, "username"); cJSON_AddStringToObject(broker, "username", lj_str(body, "username"));
        if (lj_str(body, "password")[0]) { cJSON_DeleteItemFromObjectCaseSensitive(broker, "password"); cJSON_AddStringToObject(broker, "password", lj_str(body, "password")); }
        if (!local_space(32768) || lj_write(MQTT_CONFIG, config)) { local_error(fd, "507 Insufficient Storage", "MQTT settings were not saved."); goto done; }
        trigger_mqtt_discover();
    }
    reply = cJSON_Duplicate(config, 1); cJSON_DeleteItemFromObjectCaseSensitive(lj_get(reply, "broker"), "password");
    cJSON_AddBoolToObject(reply, "ok", 1); lj_reply(fd, "200 OK", reply); cJSON_Delete(reply);
done:
    if (lock >= 0) close(lock); cJSON_Delete(config);
}
static void local_wifi(int fd, const struct request *r, const cJSON *body) {
    int lock = local_lock(); cJSON *reply = cJSON_CreateObject(); struct wifi_config config; char result[512];
    if (lock < 0) { local_error(fd, "409 Conflict", "Another network change is in progress."); goto done; }
    if (!strcmp(r->path, "/api/v1/network/confirm")) {
        if (access(LOCAL_ROOT "/wifi.pending", F_OK) || run_cmd("wpa_cli -i ath0 status 2>/dev/null", result, sizeof(result)) || !strstr(result, "wpa_state=COMPLETED")) {
            local_error(fd, "409 Conflict", "The new network is not connected yet."); goto done;
        }
        load_wifi(&config);
        { char *ssid = strstr(result, "\nssid="); size_t n;
            if (!ssid) { local_error(fd, "409 Conflict", "Cannot verify the new Wi-Fi name."); goto done; }
            ssid += 6; n = strcspn(ssid, "\r\n");
            if (strlen(config.ssid) != n || strncmp(config.ssid, ssid, n)) { local_error(fd, "409 Conflict", "The hub is not on the requested Wi-Fi network."); goto done; }
        }
        unlink(LOCAL_ROOT "/wifi.pending");
    } else {
        const char *ssid = lj_str(body, "ssid"), *password = lj_str(body, "password"); size_t i;
        if (!ssid[0] || strlen(ssid) > 32 || strlen(password) < 8 || strlen(password) > 63) { local_error(fd, "400 Bad Request", "Enter a Wi-Fi name and an 8-63 character WPA password."); goto done; }
        for (i = 0; ssid[i]; i++) if ((unsigned char)ssid[i] < 32) { local_error(fd, "400 Bad Request", "Wi-Fi name contains control characters."); goto done; }
        for (i = 0; password[i]; i++) if ((unsigned char)password[i] < 32) { local_error(fd, "400 Bad Request", "Wi-Fi password contains control characters."); goto done; }
        if (access(LOCAL_ROOT "/wifi.pending", F_OK) == 0) { local_error(fd, "409 Conflict", "Confirm or let the previous Wi-Fi change roll back first."); goto done; }
        if (access("/usr/sbin/wpa_cli", X_OK) && access("/usr/bin/wpa_cli", X_OK) && access("/sbin/wpa_cli", X_OK)) { local_error(fd, "503 Service Unavailable", "Live Wi-Fi reconfiguration is unavailable; use USB recovery."); goto done; }
        if (!local_space(32768) || local_copy(WPA_CONFIG, LOCAL_ROOT "/wifi.previous") || write_file_atomic(LOCAL_ROOT "/wifi.pending", "1", 1)) {
            local_error(fd, "507 Insufficient Storage", "Network rollback could not be prepared."); goto done;
        }
        memset(&config, 0, sizeof(config)); strcpy(config.ssid, ssid); strcpy(config.psk, password); config.hidden = cJSON_IsTrue(lj_get(body, "hidden"));
        if (save_wifi(&config)) { unlink(LOCAL_ROOT "/wifi.pending"); local_error(fd, "500 Internal Server Error", "Wi-Fi settings were not changed."); goto done; }
        { pid_t pid = fork(); if (pid == 0) { alarm(0); close(fd); close(lock); execl("/bin/sh", "sh", "/data/codex/maintenance.sh", "wifi-try", (char *)NULL); _exit(127); }
          if (pid < 0) { local_copy(LOCAL_ROOT "/wifi.previous", WPA_CONFIG); unlink(LOCAL_ROOT "/wifi.pending"); local_error(fd, "503 Service Unavailable", "Network recovery worker could not start; old settings restored."); goto done; } }
    }
    cJSON_AddBoolToObject(reply, "ok", 1); lj_reply(fd, "200 OK", reply);
done:
    if (lock >= 0) close(lock); cJSON_Delete(reply);
}
static int local_dispatch(int fd, const struct request *r) {
    struct local_controller ctl; cJSON *body = NULL, *out; int owner = 0, get = !strcmp(r->method, "GET");
    if (local_asset(fd, r)) return 1;
    if (strncmp(r->path, "/api/v1/", 8) != 0) return 0;
    if (strcmp(r->method, "POST") && !get) { local_error(fd, "405 Method Not Allowed", "Use GET or POST."); return 1; }
    if (!strcmp(r->path, "/api/v1/session") && get) { local_session(fd, r); return 1; }
    if (!strcmp(r->path, "/api/v1/controllers/cancel") && !get) { local_pair_cancel(fd, r); return 1; }
    if (!strcmp(r->path, "/api/v1/controllers/upgrade") && !get) { local_pair_upgrade(fd, r); return 1; }
    if (!get && (!strcmp(r->path, "/api/v1/controllers/claim") || !strcmp(r->path, "/api/v1/controllers/request"))) {
        local_pair(fd, r, !strcmp(r->path, "/api/v1/controllers/claim")); return 1;
    }
    owner = !(strncmp(r->path, "/api/v1/operations", 18) == 0 ||
        !strcmp(r->path, "/api/v1/commands/send") ||
        !strcmp(r->path, "/api/v1/activities/run") ||
        (get && (!strcmp(r->path, "/api/v1/devices") || !strcmp(r->path, "/api/v1/bluetooth/devices") || !strcmp(r->path, "/api/v1/configuration"))));
    if (!local_auth(fd, r, &ctl, owner)) return 1;
    if (!get) { body = lj_parse(r->body); if (!cJSON_IsObject(body)) { local_error(fd, "400 Bad Request", "Expected a valid JSON object without duplicate fields."); cJSON_Delete(body); return 1; } }
    if (!strcmp(r->path, "/api/v1/configuration") && get) {
        out = local_config(); lj_reply(fd, "200 OK", out); cJSON_Delete(out);
    } else if (!strcmp(r->path, "/api/v1/devices") && get) render_inventory_json(fd);
    else if (!strcmp(r->path, "/api/v1/bluetooth/devices") && get) send_bt_devices_download(fd);
    else if (!strcmp(r->path, "/api/v1/controllers")) local_controllers(fd, r, &ctl, body);
    else if (!strncmp(r->path, "/api/v1/operations", 18)) local_operations(fd, r, body, &ctl);
    else if (!strcmp(r->path, "/api/v1/activities/run") && !get) local_activity_run(fd, body, &ctl);
    else if (!strcmp(r->path, "/api/v1/commands/send") && !get) {
        const char *kind = !strcmp(lj_str(body, "transport"), "bluetooth") ? "bluetooth" : !strcmp(lj_str(body, "mode"), "hold") ? "hold" : "tap";
        int lock = local_lock();
        if (!safe_label(lj_str(body, "deviceId")) || strlen(lj_str(body, "deviceId")) > 64 || !safe_label(lj_str(body, "command")) || strlen(lj_str(body, "command")) > 127 || r->body_len > 2048)
            local_error(fd, "400 Bad Request", "Invalid saved command.");
        else if (lock < 0) local_error(fd, "409 Conflict", "Command queue is busy.");
        else {
            struct local_controller source = ctl;
            if (!strcmp(ctl.id, "internal") && lj_str(body, "parentId")[0]) {
                char path[256]; cJSON *parent = NULL;
                if (safe_run_id(lj_str(body, "parentId"))) { local_op_path(lj_str(body, "parentId"), path, ".json"); parent = lj_read(path, 8192); }
                if (!parent || local_op_done(parent) || !local_ctl_active(lj_str(parent, "controllerId"))) {
                    cJSON_Delete(parent); close(lock); cJSON_Delete(body); local_error(fd, "403 Forbidden", "Activity ownership expired."); return 1;
                }
                snprintf(source.id, sizeof(source.id), "%s", lj_str(parent, "controllerId")); cJSON_Delete(parent);
            }
            out = local_enqueue(body, &source, kind); if (out) { lj_reply(fd, "202 Accepted", out); cJSON_Delete(out); } else local_error(fd, "429 Too Many Requests", "Command queue is full.");
        }
        if (lock >= 0) close(lock);
    } else if ((!strcmp(r->path, "/api/v1/configuration") || !strcmp(r->path, "/api/v1/setup") || !strcmp(r->path, "/api/v1/devices") ||
        !strcmp(r->path, "/api/v1/commands/save") || !strcmp(r->path, "/api/v1/commands/import") || !strcmp(r->path, "/api/v1/backups/restore")) && !get) local_configuration_change(fd, r, body);
    else if (!strcmp(r->path, "/api/v1/backups/portable") && get) local_backup(fd, 0);
    else if (!strcmp(r->path, "/api/v1/backups/full") && get) local_backup(fd, 1);
    else if (!strcmp(r->path, "/api/v1/commands/learn") && !get) {
        char raw[4096]; int lock = lock_ir(raw, sizeof(raw));
        if (lock < 0) local_error(fd, "409 Conflict", raw);
        else { render_capture_json(fd); close(lock); }
    } else if (!strcmp(r->path, "/api/v1/bluetooth/pair") && !get) {
        const char *action = lj_str(body, "action");
        if (strcmp(action, "pairing_on") && strcmp(action, "pairing_off") && strcmp(action, "adapter_status") && strcmp(action, "connect") && strcmp(action, "status")) local_error(fd, "400 Bad Request", "Unsupported pairing action.");
        else { out = local_call_renderer(render_bluetooth_call_json, body); if (out) { lj_reply(fd, "200 OK", out); cJSON_Delete(out); } else local_error(fd, "502 Bad Gateway", "Bluetooth service did not respond."); }
    } else if (!strcmp(r->path, "/api/v1/activities/native") && get) send_file_download(fd, ACTIVITY_LIST, "native-activities.json", "application/json");
    else if (!strncmp(r->path, "/api/v1/updates/", 16) && !get) local_updates(fd, r, body);
    else if (!strcmp(r->path, "/api/v1/integrations/mqtt")) local_mqtt(fd, r, body);
    else if ((!strcmp(r->path, "/api/v1/network/wifi") || !strcmp(r->path, "/api/v1/network/confirm")) && !get) local_wifi(fd, r, body);
    else if (!strcmp(r->path, "/api/v1/maintenance/reboot") && !get) {
        out = lj_parse("{\"ok\":true}"); lj_reply(fd, "200 OK", out); cJSON_Delete(out);
        { pid_t pid = fork(); if (pid == 0) { close(fd); execl("/bin/sh", "sh", "-c", "sleep 2; /sbin/reboot", (char *)NULL); _exit(127); } }
    }
    else local_error(fd, "404 Not Found", "Unknown local API endpoint.");
    cJSON_Delete(body); return 1;
}

static int local_compat_dispatch(int fd, const struct request *r) {
    struct local_controller ctl; cJSON *body, *op; char device[80], command[160]; int lock;
    if (strcmp(r->method, "POST") || (strcmp(r->path, "/api/ir-send") && strcmp(r->path, "/api/bt-saved-command"))) return 0;
    if (!local_auth(fd, r, &ctl, 0)) return 1;
    form_value(r->body, "deviceId", device, sizeof(device)); form_value(r->body, "command", command, sizeof(command));
    if (!safe_label(device) || !safe_label(command)) { local_error(fd, "400 Bad Request", "Invalid saved command."); return 1; }
    lock = local_lock();
    if (lock < 0) { local_error(fd, "409 Conflict", "Command queue is busy."); return 1; }
    body = cJSON_CreateObject(); cJSON_AddStringToObject(body, "deviceId", device); cJSON_AddStringToObject(body, "command", command);
    cJSON_AddStringToObject(body, "mode", "tap");
    op = local_enqueue(body, &ctl, !strcmp(r->path, "/api/bt-saved-command") ? "bluetooth" : "tap");
    if (op) { lj_reply(fd, "202 Accepted", op); cJSON_Delete(op); }
    else local_error(fd, "429 Too Many Requests", "Command queue is full.");
    cJSON_Delete(body); close(lock); return 1;
}
