#ifndef LOCAL_UPDATE_STAGE
#define LOCAL_UPDATE_STAGE "/cache/harmony-update"
#endif
#ifndef LOCAL_UPDATE_BACKUP
#define LOCAL_UPDATE_BACKUP "/cache/harmony-rollback"
#endif
#ifndef LOCAL_UPDATE_VOLUME
#define LOCAL_UPDATE_VOLUME "/cache"
#endif
struct local_release_file { const char *name, *path; int mode; };
#ifndef LOCAL_CORE_FILE
#define LOCAL_CORE_FILE "/opt/luaworks/tasks/codex/localcore.lua"
#define LOCAL_MQTT_FILE "/pkg/codexmqtt/codexmqtt.lua"
#define LOCAL_INIT_FILE "/data/codex/init.sh"
#define LOCAL_MAINTENANCE_FILE "/data/codex/maintenance.sh"
#endif
static const struct local_release_file local_release_files[] = {
    {"codex_webui", CODEX_BIN_DIR "/codex_webui", 0755},
    {"codex_hbus", CODEX_BIN_DIR "/codex_hbus", 0755},
    {"codex_hal_ltcp", CODEX_BIN_DIR "/codex_hal_ltcp", 0755},
    {"codex_bthid_keyboard", CODEX_BIN_DIR "/codex_bthid_keyboard", 0755},
    {"codex_portal", CODEX_BIN_DIR "/codex_portal", 0755},
    {"localcore.lua", LOCAL_CORE_FILE, 0644},
    {"codexmqtt.lua", LOCAL_MQTT_FILE, 0644},
    {"index.html", LOCAL_WWW "/index.html", 0644},
    {"app.css", LOCAL_WWW "/app.css", 0644},
    {"app.js", LOCAL_WWW "/app.js", 0644},
    {"icons.svg", LOCAL_WWW "/icons.svg", 0644},
    {"profiles.js", LOCAL_WWW "/profiles.js", 0644},
    {"init.sh", LOCAL_INIT_FILE, 0755},
    {"maintenance.sh", LOCAL_MAINTENANCE_FILE, 0755},
    {"migrations.json", LOCAL_ROOT "/migrations.json", 0600}
};
static const struct local_release_file *local_release_file(const char *name) {
    size_t i; for (i = 0; i < sizeof(local_release_files) / sizeof(local_release_files[0]); i++)
        if (!strcmp(name, local_release_files[i].name)) return &local_release_files[i];
    return NULL;
}
static int local_file_hash(const char *path, unsigned char hash[64]) {
    FILE *f = fopen(path, "rb"); unsigned char block[4096]; size_t n; crypto_sha512_ctx ctx; int rc;
    if (!f) return -1; crypto_sha512_init(&ctx);
    while ((n = fread(block, 1, sizeof(block), f)) > 0) crypto_sha512_update(&ctx, block, n);
    rc = ferror(f) ? -1 : 0; fclose(f); crypto_sha512_final(&ctx, hash); return rc;
}
static cJSON *local_verify_manifest(const char *raw, const char *signature, int files) {
    unsigned char key[32], sig[64]; char trusted[80]; cJSON *manifest, *entry;
    size_t total = 0; int count = 0;
    if (!raw || strlen(raw) > 8192 || read_text(LOCAL_ROOT "/release.pub", trusted, sizeof(trusted)) <= 0) return NULL;
    chomp(trusted);
    if (local_hex(trusted, key, 32) || local_hex(signature, sig, 64) ||
        crypto_ed25519_check(sig, key, (const unsigned char *)raw, strlen(raw))) return NULL;
    manifest = lj_parse(raw);
    if (lj_int(manifest, "schemaVersion", 0) != 1 || strcmp(lj_str(manifest, "platform"), "pimento-4.15.600") ||
        !safe_run_id(lj_str(manifest, "version")) || !cJSON_IsArray(lj_get(manifest, "files"))) goto bad;
    cJSON_ArrayForEach(entry, lj_get(manifest, "files")) {
        const char *name = lj_str(entry, "name"); int size = lj_int(entry, "size", -1); cJSON *other;
        unsigned char expected[64], actual[64]; char path[256]; struct stat st;
        if (!local_release_file(name) || size <= 0 || size > 1500000 || local_hex(lj_str(entry, "sha512"), expected, 64)) goto bad;
        for (other = entry->next; other; other = other->next) if (!strcmp(name, lj_str(other, "name"))) goto bad;
        total += size; count++;
        if (files) {
            snprintf(path, sizeof(path), LOCAL_UPDATE_STAGE "/%s", name);
            if (stat(path, &st) || st.st_size != size || local_file_hash(path, actual) || crypto_verify64(expected, actual)) goto bad;
        }
    }
    if (count != sizeof(local_release_files) / sizeof(local_release_files[0]) || total > 2500000) goto bad;
    if (files) {
        cJSON *migration = lj_read(LOCAL_UPDATE_STAGE "/migrations.json", 4096);
        int supported = lj_int(migration, "from", 0) == 1 && lj_int(migration, "to", 0) == 1;
        cJSON_Delete(migration); if (!supported) goto bad;
    }
    return manifest;
bad:
    cJSON_Delete(manifest); return NULL;
}
static int local_rollback(void) {
    size_t i; char path[256]; int rc = 0;
    if (access(LOCAL_UPDATE_BACKUP "/ready", F_OK)) return -1;
    for (i = 0; i < sizeof(local_release_files) / sizeof(local_release_files[0]); i++) {
        snprintf(path, sizeof(path), LOCAL_UPDATE_BACKUP "/%s", local_release_files[i].name);
        if (local_copy(path, local_release_files[i].path) || chmod(local_release_files[i].path, local_release_files[i].mode)) rc = -1;
    }
    if (!rc) { unlink(LOCAL_ROOT "/update.pending"); write_file_atomic(LOCAL_ROOT "/update.result", "Rolled back after a failed health check", 37); sync(); }
    return rc;
}
static int local_health(void) {
    cJSON *config = local_config(); char id[64]; struct stat st;
    int ok = local_validate_config(config) && load_hub_id(id, sizeof(id)) &&
        stat(LOCAL_OPS "/core-ready", &st) == 0 && time(NULL) - st.st_mtime >= 0 && time(NULL) - st.st_mtime < 5;
    int lock = open(LOCAL_OPS "/coordinator.lock", O_RDWR);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) == 0) ok = 0;
    if (lock >= 0) close(lock);
#ifndef LOCAL_TEST_NO_REBOOT
    { struct sockaddr_in address; int fd = socket(AF_INET, SOCK_STREAM, 0);
      memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
      address.sin_port = htons(8080); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) ok = 0;
      if (fd >= 0) close(fd);
    }
#endif
    cJSON_Delete(config); return ok ? 0 : 1;
}
static int local_update_health(void) {
    char *raw = read_file_alloc(LOCAL_UPDATE_STAGE "/manifest.json", 8192, NULL), signature[160]; cJSON *manifest, *entry;
    int rc = local_health();
    read_text(LOCAL_UPDATE_STAGE "/signature", signature, sizeof(signature));
    manifest = local_verify_manifest(raw, signature, 0); free(raw);
    if (!manifest) return 1;
    cJSON_ArrayForEach(entry, lj_get(manifest, "files")) {
        unsigned char hash[64], expected[64]; const struct local_release_file *file = local_release_file(lj_str(entry, "name"));
        if (!file || local_file_hash(file->path, hash) || local_hex(lj_str(entry, "sha512"), expected, 64) || crypto_verify64(hash, expected)) rc = 1;
    }
    cJSON_Delete(manifest); return rc;
}
static void local_updates(int fd, const struct request *r, const cJSON *body) {
    int lock = local_lock(); cJSON *manifest = NULL, *reply = cJSON_CreateObject(); char *raw = NULL, signature[160];
    if (lock < 0) { local_error(fd, "409 Conflict", "An update or configuration change is already in progress."); goto done; }
    if (!strcmp(r->path, "/api/v1/updates/begin")) {
        const char *text = lj_str(body, "manifest"), *sig = lj_str(body, "signature"); struct statvfs space;
        manifest = local_verify_manifest(text, sig, 0);
        if (!manifest) { local_error(fd, "403 Forbidden", "Release signature, platform or manifest is invalid. A trusted release key must be installed through SSH."); goto done; }
        if (statvfs(LOCAL_UPDATE_VOLUME, &space) || (unsigned long long)space.f_bavail * space.f_frsize < 5000000 + LOCAL_RESERVE) {
            /* Require the actual bundle plus one installed rollback copy. */
            size_t need = LOCAL_RESERVE, i; cJSON *entry; struct stat st;
            cJSON_ArrayForEach(entry, lj_get(manifest, "files")) need += lj_int(entry, "size", 0);
            for (i = 0; i < sizeof(local_release_files) / sizeof(local_release_files[0]); i++)
                if (stat(local_release_files[i].path, &st) == 0) need += st.st_size;
            if (statvfs(LOCAL_UPDATE_VOLUME, &space) || (unsigned long long)space.f_bavail * space.f_frsize < need) { local_error(fd, "507 Insufficient Storage", "Not enough cache storage for staging and rollback."); goto done; }
        }
        if (access(LOCAL_ROOT "/update.pending", F_OK) == 0) { local_error(fd, "409 Conflict", "An activated release still needs a health check."); goto done; }
        mkdir(LOCAL_UPDATE_STAGE, 0700);
        { size_t i; char path[256]; for (i = 0; i < sizeof(local_release_files) / sizeof(local_release_files[0]); i++) { snprintf(path, sizeof(path), LOCAL_UPDATE_STAGE "/%s", local_release_files[i].name); unlink(path); } }
        if (write_file_atomic(LOCAL_UPDATE_STAGE "/manifest.json", text, strlen(text)) || write_file_atomic(LOCAL_UPDATE_STAGE "/signature", sig, strlen(sig))) {
            local_error(fd, "507 Insufficient Storage", "Could not stage the signed manifest."); goto done;
        }
    } else if (!strcmp(r->path, "/api/v1/updates/chunk")) {
        const char *name = lj_str(body, "name"), *hex = lj_str(body, "hex"); char path[256];
        FILE *file; long offset = lj_int(body, "offset", -1), size = -1; size_t n = strlen(hex), i; cJSON *entry;
        raw = read_file_alloc(LOCAL_UPDATE_STAGE "/manifest.json", 8192, NULL);
        read_text(LOCAL_UPDATE_STAGE "/signature", signature, sizeof(signature));
        manifest = local_verify_manifest(raw, signature, 0);
        if (!manifest || !local_release_file(name) || n == 0 || n > 32768 || n % 2) { local_error(fd, "400 Bad Request", "Invalid release chunk."); goto done; }
        cJSON_ArrayForEach(entry, lj_get(manifest, "files")) if (!strcmp(name, lj_str(entry, "name"))) size = lj_int(entry, "size", -1);
        snprintf(path, sizeof(path), LOCAL_UPDATE_STAGE "/%s", name);
        file = fopen(path, "ab");
        if (!file || fseek(file, 0, SEEK_END) || ftell(file) != offset || offset < 0 || offset + n / 2 > size) {
            if (file) fclose(file); local_error(fd, "409 Conflict", "Chunk offset or size does not match the signed manifest."); goto done;
        }
        for (i = 0; i < n; i++) if (hexval(hex[i]) < 0) { fclose(file); local_error(fd, "400 Bad Request", "Chunk is not hexadecimal."); goto done; }
        for (i = 0; i < n; i += 2) if (fputc((hexval(hex[i]) << 4) | hexval(hex[i + 1]), file) == EOF) break;
        { int failed = i != n || fflush(file) != 0 || fsync(fileno(file)) != 0; if (fclose(file) != 0) failed = 1;
            if (failed) { local_error(fd, "507 Insufficient Storage", "Release chunk could not be saved."); goto done; } }
    } else if (!strcmp(r->path, "/api/v1/updates/apply")) {
        size_t i; char path[256]; cJSON *entry; struct statvfs space; size_t need = LOCAL_RESERVE;
        raw = read_file_alloc(LOCAL_UPDATE_STAGE "/manifest.json", 8192, NULL);
        read_text(LOCAL_UPDATE_STAGE "/signature", signature, sizeof(signature)); manifest = local_verify_manifest(raw, signature, 1);
        if (!manifest) { local_error(fd, "403 Forbidden", "Signature, file hash or migration validation failed. Nothing was activated."); goto done; }
        cJSON_ArrayForEach(entry, lj_get(manifest, "files")) need += lj_int(entry, "size", 0);
        if (statvfs(LOCAL_ROOT, &space) || (unsigned long long)space.f_bavail * space.f_frsize < need) { local_error(fd, "507 Insufficient Storage", "Not enough writable storage for activation."); goto done; }
        mkdir(LOCAL_UPDATE_BACKUP, 0700); unlink(LOCAL_UPDATE_BACKUP "/ready");
        for (i = 0; i < sizeof(local_release_files) / sizeof(local_release_files[0]); i++) {
            snprintf(path, sizeof(path), LOCAL_UPDATE_BACKUP "/%s", local_release_files[i].name);
            if (local_copy(local_release_files[i].path, path)) { local_error(fd, "507 Insufficient Storage", "Rollback copy is incomplete. Nothing was activated."); goto done; }
        }
        if (write_file_atomic(LOCAL_UPDATE_BACKUP "/ready", "1", 1) || write_file_atomic(LOCAL_ROOT "/update.pending", lj_str(manifest, "version"), strlen(lj_str(manifest, "version")))) {
            local_error(fd, "507 Insufficient Storage", "Rollback journal could not be saved."); goto done;
        }
        for (i = 0; i < sizeof(local_release_files) / sizeof(local_release_files[0]); i++) {
            snprintf(path, sizeof(path), LOCAL_UPDATE_STAGE "/%s", local_release_files[i].name);
            if (local_copy(path, local_release_files[i].path) || chmod(local_release_files[i].path, local_release_files[i].mode)) {
                local_rollback(); local_error(fd, "500 Internal Server Error", "Activation failed; restored the previous release."); goto done;
            }
        }
        sync();
    } else { local_error(fd, "404 Not Found", "Unknown update endpoint."); goto done; }
    cJSON_AddBoolToObject(reply, "ok", 1); lj_reply(fd, "200 OK", reply);
    if (!strcmp(r->path, "/api/v1/updates/apply")) {
#ifndef LOCAL_TEST_NO_REBOOT
        pid_t pid = fork(); if (pid == 0) { close(fd); execl("/bin/sh", "sh", "-c", "sleep 2; /sbin/reboot", (char *)NULL); _exit(127); }
#endif
    }
done:
    if (lock >= 0) close(lock); cJSON_Delete(reply); cJSON_Delete(manifest); free(raw);
}
