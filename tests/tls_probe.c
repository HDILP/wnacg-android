/* tls_probe.c — host-side probe: raw HTTPS GET via net.c/tls.c stack. */
#include "net.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *url = (argc > 1) ? argv[1]
        : "https://img5.wnimg1.ru/data/3727/88/001.webp";
    http_response r;
    int rc = http_get(url, "https://www.wn09.shop/", NULL, 5, &r);
    printf("rc=%d status=%d body_len=%zu\n", rc, r.status, r.body_len);
    if (r.status == 200 && r.body_len > 16) {
        printf("first bytes: ");
        for (size_t i = 0; i < 16 && i < r.body_len; i++)
            printf("%02x ", (unsigned char)r.body[i]);
        printf("\n");
    }
    free_http_response(&r);
    return 0;
}
