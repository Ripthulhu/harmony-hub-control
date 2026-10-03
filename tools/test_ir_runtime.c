/* gcc -O0 tools/test_ir_runtime.c -o /tmp/test-ir && /tmp/test-ir */
#define main webui_main
#include "../payload/source/codex_webui.c"
#undef main
#include <assert.h>

static void batch_reply(const char *body, char *out, size_t size) {
    int pair[2];
    struct request req = {0};
    req.body = (char *)body;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    render_ir_batch_send_json(pair[0], &req);
    close(pair[0]);
    ssize_t n = read(pair[1], out, size - 1);
    assert(n > 0);
    out[n] = 0;
    close(pair[1]);
}

int main(int argc, char **argv) {
    char out[4096];
    if (argc > 1 && strcmp(argv[1], "--helper-check") == 0) {
        int rc = run_cmd("printf 'helper reply'", out, sizeof(out));
        printf("helper exit=%d output=%s\n", rc, out);
        printf("IR success parse=%d\n", ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":200,\"msg\":\"OK\"}"));
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--preview") == 0) {
        struct ir_inventory *inv = calloc(1, sizeof(*inv));
        const char *names[] = {"Power", "Input", "Home", "Settings", "Up", "Down", "Left", "Right", "OK", "Volume Up", "Volume Down", "Channel Up", "Channel Down", "Mute", "Back", "Play", "Pause", "Rewind", "Forward", "Red", "Green", "Yellow", "Blue", "HDMI 1", "Guide"};
        inv->device_count = 1;
        strcpy(inv->devices[0].id, "preview");
        strcpy(inv->devices[0].name, "Living room TV");
        strcpy(inv->devices[0].manufacturer, "LG");
        strcpy(inv->devices[0].model, "C5");
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            strcpy(inv->devices[0].commands[i].name, names[i]);
            inv->devices[0].command_count++;
        }
        page_head(stdout, "Harmony layout preview");
        fputs("<section data-view='control' class='section active'><div class='section-head'><h2>Remote</h2><button data-next-step='device'>Add device</button></div>", stdout);
        ir_render_device_workspace(stdout, &inv->devices[0], 1);
        fputs("</section><section data-view='ir' class='section'><div class='section-head'><h2>Devices</h2><button data-next-step='device'>Add device</button></div>", stdout);
        ir_setup_flow(stdout, inv);
        ir_render_device_editor(stdout, &inv->devices[0], 1);
        fputs("</section>", stdout);
        bluetooth_panel(stdout);
        page_end(stdout);
        free(inv);
        return 0;
    }
    assert(ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":200}"));
    assert(ir_reply_ok("{\"code\": 200, \"cmd\": \"harmony.engine?holdaction\"}"));
    assert(!ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":565,\"msg\":\"Device not found\"}"));
    assert(!ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":500}"));
    assert(!ir_reply_ok("{\"cmd\":\"other\",\"code\":200}"));
    assert(!ir_reply_ok(""));
    assert(!ir_reply_ok("connect: Connection refused"));
    assert(run_cmd("printf 'helper reply'", out, sizeof(out)) == 0);
    assert(strcmp(out, "helper reply") == 0);
    assert(run_cmd("exit 7", out, sizeof(out)) != 0);
    int lock = lock_ir(out, sizeof(out));
    assert(lock >= 0);
    assert(send_ir_command_action_ex("unit-test", "Volume Up", "test", "", out, sizeof(out)) != 0);
    assert(strstr(out, "Another IR command"));
    close(lock);
    assert(safe_run_id("hold-abc-123"));
    assert(!safe_run_id("../hold"));
    assert(!safe_run_id(""));
    {
        int pair[2];
        struct request req = {0};
        req.body = "phase=keepalive&runId=unit-no-start";
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        render_ir_hold_json(pair[0], &req);
        assert(read(pair[1], out, sizeof(out)) > 0);
        close(pair[1]);
        assert(access(IR_HOLD_PREFIX "unit-no-start", F_OK) != 0);
    }
    assert(ir_alias_match("volup", "volumeup|volup"));
    assert(!ir_alias_match("poweroff", "power|powertoggle"));
    assert(!ir_alias_match("inputhdmi1", "input|source"));
    batch_reply("deviceId=unit-test&commands=One%0ATwo&dryRun=1", out, sizeof(out));
    assert(strstr(out, "\"attempted\":2") && strstr(out, "\"sent\":0") && strstr(out, "\"failed\":0"));
    /* Missing hub ID must count as failure, never as a transmitted command. */
    assert(access("/data/codex/hub_id", F_OK) != 0);
    batch_reply("deviceId=unit-test&commands=One%0ATwo&dryRun=0", out, sizeof(out));
    assert(strstr(out, "\"attempted\":2") && strstr(out, "\"sent\":0") && strstr(out, "\"failed\":2"));
    puts("IR response and batch checks passed");
    return 0;
}
