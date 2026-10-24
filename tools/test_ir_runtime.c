/* Build with the vendored JSON/crypto sources; see docs/BUILD.md. */
#define main webui_main
#include "../payload/source/codex_webui.c"
#undef main
#include <assert.h>

/* Real HAL ir.cap replies (codex_hal_ltcp --hex) from a TCL Google TV remote. */
static const char *CAPTURE_GLOBOPLAY_HEX =
    "7b226964223a312c22636f6465223a3230307d00070f8b00981f3b01e109a801e109a801e109a801e109a801e109a801e109a801e109a801e105dc01e109a801e109a801e105da01e109a801e105dc01e105da01e105da01e105dc01e105da01e105da01e105dc01e109a801e105da01e105dc01e109a801e105da01e180000000800000008000000080000000800000008000000011e2\n";
static const char *CAPTURE_VOLUME_UP_HEX =
    "7b226964223a312c22636f6465223a3230307d000a0f8d00981f3b01e309a801e309a801e409a801e409a801e405dc01e305da01e309a801e305dc01e309a801e309a801e309a801e309a801e305da01e405dc01e305da01e305da01e409a801e409a901e305da01e309a801e305da01e405dc01e305da01e305da01e422410f8d1f3b01e309a801e309a801e309a801e309a801e305da01e405dc01e309a801e305da01e309a801e409a801e409a801e409a901e305da01e305da01e405dc01e305da01e309a801e309a801e305dc01e309a801e305da01e305da01e405dc01e305da01e3224d0f8d1f3b01e409a801e409a801e409a901e309a801e305da01e305da01e409a801e405dc01e309a801e309a801e309a801e309a801e305da01e405dc01e305da01e305dc01e309a801e309a801e305da01e309a801e305dc01e305da01e305da01e305dc01e322900f8d1f3b01e309a801e309a801e309a801e309a801e305da01e405dc01e309a801e305da01e309a801e309a801e409a801e409a801e405dc01e305da01e305da01e405dc01e309a801e309a801e305da01e409a801e405dc01e305da01e305dc01e305da01e380000000800000008000000080000000800000008000000012b1\n";
static const char *CAPTURE_NOISE_HEX =
    "7b226964223a312c22636f6465223a3230307d0004006d0005090901450d1101e307d201e4061501e3090f00a8049f01e3800000008000000080000000800000008000000080000000116d\n";

static void test_ir_capture_decoding(void) {
    char raw[4096];
    assert(hal_capture_reply_to_raw(CAPTURE_GLOBOPLAY_HEX, raw, sizeof(raw)) == 0);
    /* One RCA frame at 38 kHz; the last mark has no period, so it gets the default gap. */
    assert(strcmp(raw, "F9470PF8BSFB0P1E1S7C7P1E1S7C7P1E1S7C7P1E1S7C7P1E1S7C7P1E1S7C7P1E1S7C7P1E1S3FB"
                       "P1E1S7C7P1E1S7C7P1E1S3F9P1E1S7C7P1E1S3FBP1E1S3F9P1E1S3F9P1E1S3FBP1E1S3F9P1E1S3F9"
                       "P1E1S3FBP1E1S7C7P1E1S3F9P1E1S3FBP1E1S7C7P1E1S3F9P1E1S4E20") == 0);
    assert(hal_capture_reply_to_raw(CAPTURE_VOLUME_UP_HEX, raw, sizeof(raw)) == 0);
    /* Four repeated frames collapse to the first, keeping the real inter-frame gap. */
    assert(strcmp(raw, "F9470PF8DSFAEP1E3S7C5P1E3S7C5P1E4S7C4P1E4S7C4P1E4S3F8P1E3S3F7P1E3S7C5P1E3S3F9"
                       "P1E3S7C5P1E3S7C5P1E3S7C5P1E3S7C5P1E3S3F7P1E4S3F8P1E3S3F7P1E3S3F7P1E4S7C4P1E4S7C5"
                       "P1E3S3F7P1E3S7C5P1E3S3F7P1E4S3F8P1E3S3F7P1E3S3F7P1E4S205D") == 0);
    assert(hal_capture_reply_to_raw(CAPTURE_NOISE_HEX, raw, sizeof(raw)) != 0);
    assert(hal_capture_reply_to_raw("7b226964223a312c22636f6465223a3230307d\n", raw, sizeof(raw)) != 0);
    assert(hal_capture_reply_to_raw("7b226964223a312c22636f6465223a3530307d000a0f8d00981f3b01e309a8\n", raw, sizeof(raw)) != 0);
    assert(hal_capture_reply_to_raw("connect_hal: Connection refused\n", raw, sizeof(raw)) != 0);
    assert(hal_capture_reply_to_raw("", raw, sizeof(raw)) != 0);
    assert(hal_capture_reply_to_raw(CAPTURE_GLOBOPLAY_HEX, raw, 40) != 0);
}

int main(int argc, char **argv) {
    char out[4096];
    if (argc > 1 && strcmp(argv[1], "--helper-check") == 0) {
        int rc = run_cmd("printf 'helper reply'", out, sizeof(out));
        printf("helper exit=%d output=%s\n", rc, out);
        printf("IR success parse=%d\n", ir_reply_ok("{\"cmd\":\"harmony.engine?holdaction\",\"code\":200,\"msg\":\"OK\"}"));
        return 0;
    }
    test_ir_capture_decoding();
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
