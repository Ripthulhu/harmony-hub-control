#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const char page[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
    "<!doctype html><html lang='en'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Harmony Recovery</title><body><h1>USB Network Recovery</h1>"
    "<p>This recovery network is not encrypted. It cannot accept Wi-Fi passwords.</p>"
    "<p>Connect the hub to your computer over USB and choose Provision Wi-Fi in the Harmony Hub Tool.</p>"
    "<p>A factory reset removes local settings and may remove this installation.</p></body></html>";

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    struct sockaddr_in address; int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    if (fd < 0) return 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_port = htons(80); inet_pton(AF_INET, "192.168.76.1", &address.sin_addr);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 4)) return 1;
    while (1) {
        char request[1024]; int client = accept(fd, NULL, NULL); ssize_t n;
        struct timeval timeout = {2, 0};
        if (client < 0) continue;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        n = recv(client, request, sizeof(request), 0);
        if (n >= 4 && !memcmp(request, "GET ", 4)) send(client, page, sizeof(page) - 1, 0);
        else { const char error[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; send(client, error, sizeof(error) - 1, 0); }
        close(client);
    }
}
