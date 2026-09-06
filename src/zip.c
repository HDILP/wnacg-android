/* zip.c — whole-archive (.zip) download from the site's packaged download
 * page (/download-index-aid-<id>.html).
 *
 * The page is a JS-driven downloader: its CONFIG block carries the file key,
 * display name and a Worker API endpoint that mints a signed one-shot URL
 * (Server 1); a static backup <a> (Server 2) points at the direct file.
 * We parse the page server-side (no browser needed) and expose two things:
 *   - the resolved URLs themselves (ziplink), for copy-to-browser use;
 *   - a stream-to-disk download of either line (zip).
 */
#include "zip.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

static char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

/* Decode the handful of HTML entities the page actually uses (&#39; &quot;
 * &amp; &lt; &gt; &nbsp;) plus numeric ones. In-place-ish: returns malloc'd. */
static char *html_unescape(const char *s) {
    if (!s) return xstrdup("");
    size_t n = strlen(s);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n && s[i];) {
        if (s[i] == '&') {
            const char *semi = strchr(s + i, ';');
            size_t dist = semi ? (size_t)(semi - (s + i)) : 0;
            if (dist >= 2 && dist <= 12) {
                size_t len = dist - 1;  /* entity-name length (excl & and ;) */
                if (len >= 2 && s[i + 1] == '#') {
                    /* numeric: &#39; / &#x27; */
                    long cp = 0;
                    if (len >= 3 && (s[i + 2] == 'x' || s[i + 2] == 'X')) {
                        cp = strtol(s + i + 3, NULL, 16);
                    } else {
                        cp = strtol(s + i + 2, NULL, 10);
                    }
                    if (cp > 0 && cp < 0x80) { out[j++] = (char)cp; i = (size_t)(semi - s) + 1; continue; }
                } else {
                    static const struct { const char *e; char c; } tab[] = {
                        { "amp", '&' }, { "lt", '<' }, { "gt", '>' },
                        { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' },
                    };
                    for (size_t k = 0; k < sizeof(tab)/sizeof(tab[0]); k++) {
                        if (strlen(tab[k].e) == len &&
                            strncmp(s + i + 1, tab[k].e, len) == 0) {
                            out[j++] = tab[k].c;
                            i = (size_t)(semi - s) + 1;
                            goto next_entity;
                        }
                    }
                }
            }
        }
        out[j++] = s[i++];
next_entity:;
    }
    out[j] = '\0';
    return out;
}

/* Extract the value of `key: "..."` inside a JS const CONFIG = {...} block. */
static char *extract_config(const char *html, const char *key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "%s:", key);
    const char *p = strstr(html, pat);
    if (!p) return NULL;
    const char *q = strchr(p, '"');
    if (!q) return NULL;
    const char *qe = strchr(q + 1, '"');
    if (!qe) return NULL;
    return xstrndup(q + 1, (size_t)(qe - q - 1));
}

/* True if a JS string literal at `p` starts with "//" or a scheme. */
static int looks_absolute(const char *u) {
    return strncmp(u, "//", 2) == 0 || strncmp(u, "http://", 7) == 0 ||
           strncmp(u, "https://", 8) == 0;
}

int parse_zip_download(const char *html, zip_download *out) {
    memset(out, 0, sizeof(*out));
    /* CONFIG values (may legitimately be absent on a non-download page). */
    out->worker_api = extract_config(html, "WORKER_API");
    out->file_key = extract_config(html, "FILE_KEY");
    char *raw_name = extract_config(html, "FILE_NAME");
    out->file_name = html_unescape(raw_name ? raw_name : "");
    free(raw_name);

    /* Server 2: the direct backup link is in an <a class="ads" href="...">.
     * href is protocol-relative ("//dl1.wn01.download/down/...zip?..."). */
    const char *a = strstr(html, "class=\"ads\"");
    if (!a) a = strstr(html, "class='ads'");
    if (a) {
        const char *href = strstr(a, "href=\"");
        if (!href) href = strstr(a, "href='");
        if (href) {
            href += 6; /* past href=" */
            char quote = href[-1] == '\'' ? '\'' : '"';
            const char *end = strchr(href, quote);
            if (end) {
                char *u = xstrndup(href, (size_t)(end - href));
                if (looks_absolute(u)) {
                    size_t need = strlen(u) + 9; /* https: prefix room */
                    char *abs = malloc(need);
                    if (abs) {
                        if (strncmp(u, "//", 2) == 0) snprintf(abs, need, "https:%s", u);
                        else snprintf(abs, need, "%s", u);
                        /* keep only the base file URL, drop ?n= display name —
                         * the server redirects to the file regardless, and the
                         * name param is just cosmetic for browsers. */
                        char *q = strchr(abs, '?');
                        if (q) *q = '\0';
                        out->server2 = abs;
                    }
                }
                free(u);
            }
        }
    }

    if (!out->file_key && !out->server2) return -1;
    return 0;
}

void free_zip_download(zip_download *z) {
    if (!z) return;
    free(z->worker_api);
    free(z->file_key);
    free(z->file_name);
    free(z->server2);
    memset(z, 0, sizeof(*z));
}

/* Minimal JSON string literal escape for building the POST body. */
static char *json_escape(const char *s) {
    if (!s) return xstrdup("");
    size_t n = strlen(s);
    char *out = malloc(n * 2 + 3);
    if (!out) return NULL;
    size_t j = 0;
    out[j++] = '"';
    for (size_t i = 0; i < n && s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { out[j++] = '\\'; out[j++] = (char)c; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't'; }
        else if (c < 0x20) { /* skip control chars */ }
        else out[j++] = (char)c;
    }
    out[j++] = '"';
    out[j] = '\0';
    return out;
}

/* Pull the `"url"` value out of the Worker's JSON response:
 *   {"success":true,"url":"https://d1.wcdn.date/download?key=...&sign=..."} */
static char *extract_json_url(const char *json) {
    const char *p = strstr(json, "\"url\"");
    if (!p) p = strstr(json, "\"url\":");
    if (!p) return NULL;
    const char *colon = strchr(p, ':');
    if (!colon) return NULL;
    const char *q = strchr(colon, '"');
    if (!q) return NULL;
    const char *qe = strchr(q + 1, '"');
    if (!qe) return NULL;
    return xstrndup(q + 1, (size_t)(qe - q - 1));
}

char *zip_generate_link(const char *api_url, const char *file_key,
                        const char *file_name, const char *referer) {
    if (!api_url || !file_key) return NULL;
    char *k = json_escape(file_key);
    char *n = json_escape(file_name ? file_name : "");
    if (!k || !n) { free(k); free(n); return NULL; }
    size_t bodycap = strlen(k) + strlen(n) + 64;
    char *body = malloc(bodycap);
    if (!body) { free(k); free(n); return NULL; }
    snprintf(body, bodycap, "{\"file_key\":%s,\"file_name\":%s}", k, n);
    free(k);
    free(n);

    http_response r;
    if (http_post_json(api_url, referer, body, 5, &r) != 0) {
        fprintf(stderr, "[zip] generate-link 请求失败\n");
        free(body);
        return NULL;
    }
    free(body);
    if (r.status != 200 || !r.body) {
        /* Cloudflare challenges HTTP/1.1 (returns an interstitial) even though
         * browsers reach it over HTTP/2. Body is usually a "Just a moment..."
         * challenge page. Nothing we can do from this HTTP/1.1-only client —
         * tell the caller to fall back to the direct backup link. */
        fprintf(stderr, "[zip] generate-link HTTP %d (Server1 需 HTTP/2, 换 2 试试)\n", r.status);
        free_http_response(&r);
        return NULL;
    }
    char *url = extract_json_url(r.body);
    if (!url) {
        fprintf(stderr, "[zip] generate-link 响应无 url 字段: %.200s\n", r.body);
        free_http_response(&r);
        return NULL;
    }
    free_http_response(&r);
    return url;
}

/* Fetch the download-index page and parse it. Returns 0 with z filled. */
static int fetch_zip_page(long id, zip_download *z) {
    char url[256];
    snprintf(url, sizeof(url), "https://%s/download-index-aid-%ld.html",
             g_api_domain, id);
    char referer[128];
    snprintf(referer, sizeof(referer), "https://%s/", g_api_domain);
    http_response r;
    if (http_get(url, referer, NULL, 5, &r) != 0) {
        fprintf(stderr, "[zip] 下载页请求失败: %s\n", url);
        return -1;
    }
    if (r.status != 200 || !r.body) {
        fprintf(stderr, "[zip] 下载页 HTTP %d (可能无 zip 打包或 ID 不存在)\n", r.status);
        free_http_response(&r);
        return -1;
    }
    int rc = parse_zip_download(r.body, z);
    free_http_response(&r);
    if (rc != 0) {
        fprintf(stderr, "[zip] 页面里没找到打包下载配置\n");
        return -1;
    }
    return 0;
}

/* Save an http body (already fetched whole) to out_path. Creates parent dirs
 * (single level) so `zip <id> 2 /sdcard/wnacg` works without pre-creating. */
static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    return mkdir(path, 0755);
}
static int body_to_file(const http_response *r, const char *out_path) {
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", out_path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        ensure_dir(dir);
    }
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "[zip] 无法写入 %s\n", out_path);
        return -1;
    }
    size_t written = fwrite(r->body, 1, r->body_len, f);
    fclose(f);
    if (written != r->body_len) {
        fprintf(stderr, "[zip] 写入不完整\n");
        return -1;
    }
    return 0;
}

int cmd_zip(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "用法: %s zip <漫画ID> [1|2] [保存目录]\n"
                        "  1 = Worker 签名链接 (默认), 2 = 备用直链\n", argv[0]);
        return 1;
    }
    long id = atol(argv[2]);
    int server = (argc >= 4) ? atoi(argv[3]) : 1;
    const char *dir = (argc >= 5) ? argv[4] : ".";
    if (server != 1 && server != 2) server = 1;

    zip_download z;
    if (fetch_zip_page(id, &z) != 0) return 1;

    /* resolve the actual download URL */
    char *dl = NULL;
    if (server == 2) {
        /* 备用直链: 稳定, 支持断点, 我们的 HTTP/1.1 客户端直接可用 */
        if (z.server2) {
            dl = xstrdup(z.server2);
            printf("[zip] Server2 直链\n");
        } else {
            fprintf(stderr, "[zip] 页面没有 Server2 直链\n");
            free_zip_download(&z);
            return 1;
        }
    } else {
        /* Server1: Cloudflare Worker 只回 HTTP/2; 本客户端是 HTTP/1.1,
         * 大概率被 challenge (403)。能拿到签名链就用, 拿不到自动回落直链。 */
        char referer[256];
        snprintf(referer, sizeof(referer),
                 "https://%s/download-index-aid-%ld.html", g_api_domain, id);
        dl = zip_generate_link(z.worker_api, z.file_key, z.file_name, referer);
        if (dl) {
            printf("[zip] Server1 签名链接已生成\n");
        } else if (z.server2) {
            dl = xstrdup(z.server2);
            printf("[zip] Server1 不可用, 自动回落 Server2 直链\n");
        } else {
            fprintf(stderr, "[zip] Server1 失败且无 Server2 直链\n");
            free_zip_download(&z);
            return 1;
        }
    }

    /* pick output filename: <dir>/<id>.zip */
    char out[1200];
    snprintf(out, sizeof(out), "%s/%ld.zip", dir, id);

    printf("开始下载 zip 到 %s\n", out);
    fflush(stdout);

    char referer[128];
    snprintf(referer, sizeof(referer), "https://%s/", g_api_domain);
    http_response r;
    if (http_get(dl, referer, NULL, 5, &r) != 0) {
        fprintf(stderr, "[zip] 下载失败\n");
        free(dl);
        free_zip_download(&z);
        return 1;
    }
    if (r.status != 200 || !r.body || r.body_len == 0) {
        fprintf(stderr, "[zip] 下载 HTTP %d (可能签名过期/限额, 换 2 试试)\n", r.status);
        free_http_response(&r);
        free(dl);
        free_zip_download(&z);
        return 1;
    }
    int ok = body_to_file(&r, out);
    printf("完成: zip %s (%zu 字节)\n", ok == 0 ? "成功" : "失败", r.body_len);
    free_http_response(&r);
    free(dl);
    free_zip_download(&z);
    return ok;
}

int cmd_ziplink(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "用法: %s ziplink <漫画ID>\n", argv[0]);
        return 1;
    }
    long id = atol(argv[2]);
    zip_download z;
    if (fetch_zip_page(id, &z) != 0) return 1;

    int rc = 1;
    char referer[256];
    snprintf(referer, sizeof(referer),
             "https://%s/download-index-aid-%ld.html", g_api_domain, id);

    if (z.server2) {
        printf("ZIP2 %s\n", z.server2);
    } else {
        printf("ZIP2 无\n");
    }
    if (z.worker_api && z.file_key) {
        char *url = zip_generate_link(z.worker_api, z.file_key, z.file_name, referer);
        if (url) {
            printf("ZIP1 %s\n", url);
            free(url);
            rc = 0;
        } else {
            printf("ZIP1 无\n");
        }
    } else {
        printf("ZIP1 无\n");
    }
    free_zip_download(&z);
    return rc;
}
