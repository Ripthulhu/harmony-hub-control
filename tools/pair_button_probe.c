/* Read-only, bounded observation. Never grab the input device or write GPIOs. */
#include <linux/input.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    struct timespec start, now;
    char previous[128] = "";
    if (fd < 0) { perror("event0"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    clock_gettime(CLOCK_MONOTONIC, &start);
    puts("Observing input and button status for 120 seconds; no actions will be taken.");
    do {
        struct input_event event;
        char status[128] = "";
        FILE *f = fopen("/sys/class/input/input0/tde", "r");
        if (f) { fgets(status, sizeof(status), f); fclose(f); }
        if (strcmp(status, previous)) { printf("status: %s", status); strcpy(previous, status); }
        while (read(fd, &event, sizeof(event)) == sizeof(event))
            printf("event: %ld.%06ld type=%u code=%u value=%d\n",
                   (long)event.time.tv_sec, (long)event.time.tv_usec, event.type, event.code, event.value);
        usleep(20000);
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while (now.tv_sec - start.tv_sec < 120);
    close(fd);
    return 0;
}
