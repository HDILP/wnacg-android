/* zip_parse_test.c — unit test for parse_zip_download against a captured
 * /download-index-aid-<id>.html page (tests/sample_download_index.html). */
#include "zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* zip.c references the extern global g_api_domain (net.h). Provide it at file
 * scope so this test links without pulling in wnacg.c (which has its own main). */
const char *g_api_domain = "www.wn10.shop";

static char *rf(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    b[n] = 0;
    fclose(f);
    return b;
}

int main(void) {
    char *html = rf("tests/sample_download_index.html");
    if (!html) { fprintf(stderr, "FAIL: cannot read sample\n"); return 1; }

    zip_download z;
    if (parse_zip_download(html, &z) != 0) {
        fprintf(stderr, "FAIL: parse_zip_download\n");
        free(html);
        return 1;
    }
    int bad = 0;
    if (z.worker_api && strcmp(z.worker_api, "https://d1.wcdn.date/api/generate-link") == 0)
        printf("ok: worker_api = %s\n", z.worker_api);
    else { printf("FAIL: worker_api = %s\n", z.worker_api ? z.worker_api : "(null)"); bad = 1; }

    if (z.file_key && strncmp(z.file_key, "down/", 5) == 0 && strstr(z.file_key, ".zip"))
        printf("ok: file_key = %s\n", z.file_key);
    else { printf("FAIL: file_key = %s\n", z.file_key ? z.file_key : "(null)"); bad = 1; }

    if (z.file_name && strstr(z.file_name, ".zip"))
        printf("ok: file_name = %s\n", z.file_name);
    else { printf("FAIL: file_name = %s\n", z.file_name ? z.file_name : "(null)"); bad = 1; }

    if (z.server2 && strncmp(z.server2, "https://", 8) == 0 &&
        strstr(z.server2, ".zip") && !strstr(z.server2, "&nbsp;"))
        printf("ok: server2 = %s\n", z.server2);
    else { printf("FAIL: server2 = %s\n", z.server2 ? z.server2 : "(null)"); bad = 1; }

    if (z.file_name && strstr(z.file_name, "&nbsp;") == NULL)
        printf("ok: file_name entity decoded (&nbsp; gone): %s\n", z.file_name);
    else { printf("FAIL: file_name still has &nbsp;: %s\n", z.file_name ? z.file_name : "(null)"); bad = 1; }

    free_zip_download(&z);
    free(html);
    if (bad) return 1;
    printf("ALL ZIP PARSE TESTS PASSED\n");
    return 0;
}
