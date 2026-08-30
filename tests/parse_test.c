/* Local parser test: reads sample HTML files, runs parse_search /
 * parse_imglist, and asserts expected outputs. No network. */
#include "html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    fread(buf, 1, n, f); buf[n] = '\0'; fclose(f);
    return buf;
}

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("  FAIL: %s\n", msg); failures++; } else { printf("  ok: %s\n", msg); } } while(0)

int main(void) {
    /* --- search --- */
    char *shtml = read_file("tests/sample_search.html");
    search_result s;
    parse_search(shtml, 0, &s);
    printf("[search]\n");
    CHECK(s.count == 2, "two results parsed");
    CHECK(s.total_page == 50, "total_page==50 (1200/24)");
    CHECK(s.items[0].id == 257351, "first id==257351");
    CHECK(strcmp(s.items[0].title, "百合百合 第1卷") == 0, "first title cleaned");
    CHECK(strncmp(s.items[0].cover, "https://", 8) == 0, "cover has https prefix");
    CHECK(strstr(s.items[0].additional, "209") != NULL, "additional has 209");
    free_search_result(&s);

    /* --- tag search total_page path (is_tag=1) --- */
    char taghtml[2048];
    snprintf(taghtml, sizeof(taghtml),
        "<div class=\"f_left paginator\"><a href=\"/x/1.html\">1</a>"
        "<a href=\"/x/3.html\">3</a></div>");
    search_result ts;
    parse_search(taghtml, 1, &ts);
    printf("[tag total page]\n");
    CHECK(ts.total_page == 3, "tag total_page==3 (last <a>)");

    /* --- imglist --- */
    char *ghtml = read_file("tests/sample_gallery.html");
    char **urls = NULL; int n = 0;
    parse_imglist(ghtml, &urls, &n);
    printf("[imglist]\n");
    CHECK(n == 4, "4 real images (shoucang.jpg filtered)");
    CHECK(strstr(urls[0], "https://") != NULL, "url0 has https prefix");
    CHECK(strstr(urls[2], "webp.wnacgimg.date/data/257351/02.jpg.w1280.webp") != NULL,
          "img host overridden to webp.wnacgimg.date + .w1280.webp variant appended");
    for (int i = 0; i < n; i++) free(urls[i]);
    free(urls);

    /* filename_filter */
    char *fn = filename_filter("a/b:c*?\"<>|d");
    CHECK(strcmp(fn, "a_b_c______d") == 0, "filename_filter replaces illegal chars");
    free(fn);

    free(shtml); free(ghtml);
    printf("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return failures ? 1 : 0;
}
