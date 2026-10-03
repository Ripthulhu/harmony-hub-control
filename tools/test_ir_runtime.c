/* Build with the vendored JSON/crypto sources; see docs/BUILD.md. */
#define main webui_main
#include "../payload/source/codex_webui.c"
#undef main
#include <assert.h>

int main(int argc, char **argv) {
    char out[4096];
    if (argc > 1 && strcmp(argv[1], "--helper-check") == 0) {
        int rc = run_cmd("printf 'helper reply'", out, sizeof(out));
        printf("helper exit=%d output=%s\n", rc, out);
        printf("IR success parse=%d\n", ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":200,\"msg\":\"OK\"}"));
        return 0;
    }
    assert(ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":200}"));
    assert(ir_reply_ok("{\"code\": 200, \"cmd\": \"harmony.engine?holdaction\"}"));
    assert(!ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":565,\"msg\":\"Device not found\"}"));
    assert(!ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":500}"));
    assert(!ir_reply_ok("{\"cmd\":\"other\",\"code\":200}"));
    assert(!ir_reply_ok(""));
    assert(!ir_reply_ok("connect: Connection refused"));
    assert(!ir_reply_ok("{\"nested\":{\"code\":200,\"cmd\":\"harmony.engine?holdaction\"}}"));
    assert(!ir_reply_ok("{\"code\":500,\"code\":200,\"cmd\":\"harmony.engine?holdaction\"}"));
    assert(!ir_reply_ok("{\"code\":200,\"cmd\":\"harmony.engine?holdaction\"} trailing"));
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
        cJSON *request = lj_parse("{\"phase\":\"keepalive\",\"runId\":\"unit-no-start\"}");
        cJSON *result = execute_ir_hold(request);
        assert(cJSON_IsTrue(lj_get(result, "ok")));
        cJSON_Delete(result); cJSON_Delete(request);
        assert(access(IR_HOLD_PREFIX "unit-no-start", F_OK) != 0);
    }
    puts("IR response, locking, run ID and hold checks passed");
    return 0;
}
