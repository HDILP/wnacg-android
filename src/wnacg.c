#include "net.h"
#include "tls.h"
#include "html.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef DEFAULT_API_DOMAIN
#define DEFAULT_API_DOMAIN "www.wn09.shop"
#endif

/* Percent-encode a string for use in a URL query (RFC 3986 unsafe set). */
static void url_encode(const char *src, char *dst, size_t dstcap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dstcap; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            dst[j++] = (char)c;
        } else {
            if (j + 3 >= dstcap) break;
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0xF];
            dst[j++] = hex[c & 0xF];
        }
    }
    dst[j] = '\0';
}

/* Download a single image URL to the given path. Returns 0 on success. */
static int download_image(const char *url, const char *out_path) {
    http_response r;
    if (http_get(url, "https://" DEFAULT_API_DOMAIN "/", NULL, 5, &r) != 0) {
        fprintf(stderr, "  [!] network error: %s\n", url);
        return -1;
    }
    if (r.status != 200 || r.body_len == 0) {
        fprintf(stderr, "  [!] HTTP %d empty: %s\n", r.status, url);
        free_http_response(&r);
        return -1;
    }
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "  [!] cannot open %s\n", out_path);
        free_http_response(&r);
        return -1;
    }
    fwrite(r.body, 1, r.body_len, f);
    fclose(f);
    free_http_response(&r);
    return 0;
}

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    return mkdir(path, 0755);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "wnacg-android — 移植自 lanyeeee/wnacg-downloader 的安卓2.3下载器\n\n"
        "用法:\n"
        "  %s search <关键词> [页码]           搜索漫画\n"
        "  %s tag <标签> [页码]               按标签搜索\n"
        "  %s download <漫画ID> [保存目录]    下载整本漫画到目录(单线程)\n"
        "  %s detail <漫画ID>                 打印漫画详情(图数/标签)\n\n"
        "示例:\n"
        "  %s search 百合\n"
        "  %s download 257351 /sdcard/wnacg\n"
        "  说明: search/tag 支持多关键词, 用空格分隔 (如: %s search 百合 汉化);\n"
        "  最后一个纯数字参数视为页码 (如: %s search 百合 汉化 2).\n",
        argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

int cmd_search(int argc, char **argv, int is_tag) {
    if (argc < 3) { usage(argv[0]); return 1; }
    /* Join all tokens after the command into one keyword, EXCEPT a trailing
     * pure-number token which is taken as the page number. This lets users
     * type `search 百合 汉化` (multi-word query) and `search 百合 汉化 2`
     * (query + page). */
    int page = 1;
    int last = argc - 1;
    if (last >= 2) {
        /* is argv[last] a bare positive integer? */
        const char *p = argv[last];
        int isnum = 1;
        for (; *p; p++) if (*p < '0' || *p > '9') { isnum = 0; break; }
        if (isnum && *argv[last] != '\0' && atoi(argv[last]) >= 1) {
            page = atoi(argv[last]);
            last--;   // don't include it in the keyword
        }
    }
    /* assemble keyword from argv[2..last] */
    size_t kcap = 256;
    for (int i = 2; i <= last; i++) kcap += strlen(argv[i]) + 1;
    char *kw = malloc(kcap);
    if (!kw) { fprintf(stderr, "内存不足\n"); return 1; }
    kw[0] = '\0';
    for (int i = 2; i <= last; i++) {
        if (i > 2) strcat(kw, " ");
        strcat(kw, argv[i]);
    }
    if (kw[0] == '\0') { free(kw); usage(argv[0]); return 1; }

    char url[1024];
    char qenc[2048];
    url_encode(kw, qenc, sizeof(qenc));
    if (is_tag) {
        /* NOTE: the legacy route /albums-index-page-N-tag-X.html is dead on the
         * current mobile site (returns 200 but zero items). The working tag
         * endpoint is the search page with f=tag. It returns the same
         * gallary_item list layout as a normal search, so parsing is identical.
         * Caveat: the server only serves page 1 for f=tag (p>=2 returns empty);
         * we surface that to the user below. */
        snprintf(url, sizeof(url),
                 "https://" DEFAULT_API_DOMAIN "/search/index.php?q=%s&syn=yes&f=tag&s=create_time_DESC&p=%d",
                 qenc, page);
    } else {
        snprintf(url, sizeof(url),
                 "https://" DEFAULT_API_DOMAIN "/search/index.php?q=%s&syn=yes&f=_all&s=create_time_DESC&p=%d",
                 qenc, page);
    }

    http_response r;
    if (http_get(url, "https://" DEFAULT_API_DOMAIN "/", NULL, 5, &r) != 0) {
        fprintf(stderr, "搜索请求失败(网络/TLS错误)\n");
        return 1;
    }
    if (r.status != 200) {
        fprintf(stderr, "搜索返回 HTTP %d\n", r.status);
        free_http_response(&r);
        return 1;
    }

    search_result s;
    if (parse_search(r.body, is_tag, &s) != 0) {
        fprintf(stderr, "解析搜索结果失败\n");
        free_http_response(&r);
        return 1;
    }
    free_http_response(&r);

    /* ---- 输出排版 (手机 TextView 友好: 分隔线 + 每行一项信息) ---- */
    printf("\n");
    printf("%s 「%s」  第 %ld/%ld 页 · 共 %d 条\n",
           is_tag ? "标签" : "搜索", kw, s.current_page, s.total_page, s.count);
    if (is_tag) {
        printf("[!] tag 模式仅第 1 页有结果 (站点限制, 翻页可能为空)\n");
    }
    printf("────────────────────────────\n");

    for (int i = 0; i < s.count; i++) {
        comic_entry *e = &s.items[i];
        const char *show_title = (e->title_html && *e->title_html)
                                  ? e->title_html : (e->title ? e->title : "(无标题)");
        printf("\n%d. %s\n", i + 1, show_title);
        printf("   ID: %ld", e->id);
        if (e->additional && *e->additional)
            printf("    %s", e->additional);
        printf("\n   下载: download %ld\n", e->id);
    }
    printf("\n────────────────────────────\n");
    free_search_result(&s);
    return 0;
}

int cmd_detail(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    long id = atol(argv[2]);
    char url[256];
    snprintf(url, sizeof(url),
             "https://" DEFAULT_API_DOMAIN "/photos-gallery-aid-%ld.html", id);

    http_response r;
    if (http_get(url, "https://" DEFAULT_API_DOMAIN "/", NULL, 5, &r) != 0) {
        fprintf(stderr, "详情请求失败\n");
        return 1;
    }
    char **urls = NULL;
    int n = 0;
    if (r.status == 200) {
        parse_imglist(r.body, &urls, &n);
    }
    printf("漫画ID %ld: 图片 %d 张\n", id, n);
    free_http_response(&r);
    for (int i = 0; i < n; i++) free(urls[i]);
    free(urls);
    return 0;
}

int cmd_download(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    long id = atol(argv[2]);
    const char *base_dir = (argc >= 4) ? argv[3] : ".";
    ensure_dir(base_dir);

    char gallery_url[256];
    snprintf(gallery_url, sizeof(gallery_url),
             "https://" DEFAULT_API_DOMAIN "/photos-gallery-aid-%ld.html", id);

    http_response r;
    if (http_get(gallery_url, "https://" DEFAULT_API_DOMAIN "/", NULL, 5, &r) != 0) {
        fprintf(stderr, "获取漫画页失败\n");
        return 1;
    }
    if (r.status != 200) {
        fprintf(stderr, "漫画页 HTTP %d (可能ID不存在或被删)\n", r.status);
        free_http_response(&r);
        return 1;
    }

    char **urls = NULL;
    int n = 0;
    parse_imglist(r.body, &urls, &n);
    free_http_response(&r);

    if (n == 0) {
        fprintf(stderr, "未解析到任何图片URL\n");
        return 1;
    }

    /* directory name = comic id */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/%ld", base_dir, id);
    ensure_dir(dir);

    printf("开始下载 漫画 %ld: %d 张到 %s\n", id, n, dir);
    int ok = 0, fail = 0;
    for (int i = 0; i < n; i++) {
        char num[16];
        snprintf(num, sizeof(num), "%04d", i + 1);
        /* keep original extension if present */
        const char *dot = strrchr(urls[i], '.');
        const char *ext = (dot && strlen(dot) <= 6) ? dot : "";
        char outp[1100];
        snprintf(outp, sizeof(outp), "%s/%s%s", dir, num, ext);
        printf("  (%d/%d) %s\n", i + 1, n, num);
        if (download_image(urls[i], outp) == 0) ok++;
        else fail++;
    }
    printf("完成: 成功 %d, 失败 %d\n", ok, fail);
    for (int i = 0; i < n; i++) free(urls[i]);
    free(urls);
    return fail ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    const char *cmd = argv[1];
    if (strcmp(cmd, "search") == 0)   return cmd_search(argc, argv, 0);
    if (strcmp(cmd, "tag") == 0)      return cmd_search(argc, argv, 1);
    if (strcmp(cmd, "download") == 0)  return cmd_download(argc, argv);
    if (strcmp(cmd, "detail") == 0)   return cmd_detail(argc, argv);
    usage(argv[0]);
    return 1;
}
