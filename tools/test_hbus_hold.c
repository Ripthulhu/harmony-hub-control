/* gcc -O0 tools/test_hbus_hold.c -o /tmp/test-hold && /tmp/test-hold */
#define main hbus_main
#include "../payload/source/codex_hbus.c"
#undef main
#include <assert.h>
#include <sys/wait.h>
#define MAX_JSON_SIZE 16383
#define LTCP_FRAME_SIZE 64
#include "../payload/source/ltcp_send.h"

static char lease[] = "/tmp/codex-test-hold-XXXXXX";
static char cancel[128];

static void renew(long long stamp) {
    FILE *f = fopen(lease, "w");
    assert(f);
    fprintf(f, "%lld\n", stamp);
    fclose(f);
}

static void frame(int fd, const char *status) {
    unsigned char hdr[2], mask[4], text[4096];
    size_t len, i;
    assert(read_exact(fd, hdr, 2) == 0 && hdr[0] == 0x81 && (hdr[1] & 128));
    len = hdr[1] & 127;
    if (len == 126) {
        assert(read_exact(fd, hdr, 2) == 0);
        len = hdr[0] * 256 + hdr[1];
    }
    assert(len < sizeof(text));
    assert(read_exact(fd, mask, 4) == 0 && read_exact(fd, text, len) == 0);
    for (i = 0; i < len; ++i) text[i] ^= mask[i & 3];
    text[len] = 0;
    char expected[64];
    snprintf(expected, sizeof(expected), "\"status\":\"%s\"", status);
    assert(strstr((char *)text, expected));
    assert(strstr((char *)text, "\"timestamp\":0,\"action\":\"test\"}}}"));
}

static void hold_cycle(int disconnect) {
    int pair[2], status;
    renew(millis());
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(pair[1]);
        int rc = run_hold(pair[0], "123", "harmony.engine?holdaction",
                          "{\"action\":\"test\"}", lease, cancel);
        close(pair[0]);
        _exit(rc);
    }
    close(pair[0]);
    long long started = millis();
    frame(pair[1], "press");
    if (disconnect) {
        for (int i = 0; i < 5; i++) frame(pair[1], "hold");
        frame(pair[1], "release");
        assert(millis() - started >= 1100 && millis() - started < 2000);
    } else {
        for (int i = 0; i < 8; i++) {
            frame(pair[1], "hold");
            renew(millis());
        }
        FILE *f = fopen(cancel, "w");
        assert(f); fclose(f);
        frame(pair[1], "release");
    }
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(pair[1]);
    unlink(cancel);
}

int main(void) {
    int fd = mkstemp(lease), pair[2];
    assert(fd >= 0);close(fd);
    snprintf(cancel, sizeof(cancel), "%s.cancel", lease);
    renew(millis());
    assert(hold_alive(lease, cancel, millis()));
    assert(!hold_alive(lease, cancel, millis()-30000));
    renew(millis()-1201);assert(!hold_alive(lease,cancel,millis()));
    renew(millis()+1000);assert(!hold_alive(lease,cancel,millis()));
    hold_cycle(0);
    hold_cycle(1);
    renew(millis());
    FILE *f=fopen(cancel,"w");assert(f);fclose(f);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    assert(run_hold(pair[0],"123","harmony.engine?holdaction","{}",lease,cancel)==0);
    close(pair[0]);char byte;
    assert(read(pair[1],&byte,1)==0);close(pair[1]);

    /* The hub only replies on errors for press/hold/release. */
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    const char error[]="{\"cmd\":\"harmony.engine?holdaction\",\"code\": 565}";
    unsigned char header[]={0x81,sizeof(error)-1};
    assert(write_all(pair[0],header,2)==0 && write_all(pair[0],error,sizeof(error)-1)==0);
    assert(recv_ws_text(pair[1],1)==-1);
    close(pair[0]);close(pair[1]);

    /* Both HAL clients now share this exact padded LTCP framing. */
    for (int len=2;len<=130;len+=64) {
        unsigned char payload[130], wire[192];
        memset(payload,'x',sizeof(payload));
        assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
        assert(send_ltcp_command(pair[0],payload,len)==0);
        size_t prefix=len>63 ? 9 : 8;
        size_t size=((len+prefix+63)/64)*64;
        assert(read_exact(pair[1],wire,size)==0);
        assert(wire[0]==0xff && wire[1]==8 && wire[6]==1);
        assert(wire[7]==(len>63 ? (0xc0|(len>>8)) : (0x80|len)));
        if(len>63)assert(wire[8]==(len&255));
        assert(memcmp(wire+prefix,payload,len)==0);
        for(size_t i=prefix+len;i<size;i++)assert(wire[i]==0);
        close(pair[0]);close(pair[1]);
    }
    unlink(lease);unlink(cancel);
    puts("IR hold lease, cancellation, timeout, error reply, and shared LTCP checks passed");
    return 0;
}
