#include "html.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* g_img_host is declared in net.h (included via html.h). */

/* ---------- small string helpers ---------- */

static char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

/* Strip ALL whitespace (space/tab/CR/LF) from a string in place. The gallery
 * HTML sometimes folds image URLs across lines or leaks stray spaces into the
 * imglist JSON values; a URL with embedded whitespace fails host resolution in
 * http_get, so we scrub it before the URL is ever used. */
static void strip_whitespace(char *s) {
    if (!s) return;
    size_t j = 0;
    for (size_t i = 0; s[i]; i++) {
        if (!isspace((unsigned char)s[i])) s[j++] = s[i];
    }
    s[j] = '\0';
}

/* Find first occurrence of needle in haystack[hlen], starting at off.
 * Returns pointer into haystack or NULL. */
static const char *find_from(const char *h, size_t hlen, size_t off,
                             const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || off + nlen > hlen) return NULL;
    for (size_t i = off; i + nlen <= hlen; i++) {
        if (memcmp(h + i, needle, nlen) == 0) return h + i;
    }
    return NULL;
}

/* Extract attribute value for `attr="..."` searching forward from *pos.
 * On success sets *pos past the value and returns malloc'd value (caller frees).
 * Returns NULL if not found. */
static char *extract_attr(const char *h, size_t hlen, size_t *pos,
                          const char *attr) {
    char pat[64];
    snprintf(pat, sizeof(pat), " %s=\"", attr);
    const char *p = find_from(h, hlen, *pos, pat);
    if (!p) {
        /* try without leading space (start of tag) */
        snprintf(pat, sizeof(pat), "%s=\"", attr);
        p = find_from(h, hlen, *pos, pat);
        if (!p) return NULL;
    }
    const char *vstart = p + strlen(pat);
    const char *vend = find_from(h, hlen, (size_t)(vstart - h), "\"");
    if (!vend) return NULL;
    *pos = (size_t)(vend - h) + 1;
    return xstrndup(vstart, (size_t)(vend - vstart));
}

/* Extract text between '>' and '</a>' from a position; returns malloc'd text. */
static char *extract_tag_text(const char *h, size_t hlen, size_t *pos,
                              const char *close_tag) {
    const char *gt = find_from(h, hlen, *pos, ">");
    if (!gt) return NULL;
    const char *tstart = gt + 1;
    const char *tend = find_from(h, hlen, (size_t)(tstart - h), close_tag);
    if (!tend) return NULL;
    /* trim trailing spaces */
    const char *e = tend;
    while (e > tstart && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' ||
                          e[-1] == '\n')) e--;
    *pos = (size_t)(tend - h) + strlen(close_tag);
    return xstrndup(tstart, (size_t)(e - tstart));
}

char *filename_filter(const char *s) {
    if (!s) return xstrdup("");
    size_t n = strlen(s);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == '\n' ||
            c == '\r' || c == '\t') {
            out[j++] = '_';
        } else {
            out[j++] = (char)c;
        }
    }
    if (j == 0) { out[j++] = '_'; }
    out[j] = '\0';
    return out;
}

/* ---------- search parsing ---------- */

static int parse_one_li(const char *h, size_t hlen, size_t li_start,
                        size_t li_end, comic_entry *out) {
    (void)li_end;
    memset(out, 0, sizeof(*out));
    size_t pos = li_start;
    /* id from href */
    const char *href = find_from(h, hlen, pos, "/photos-index-aid-");
    if (!href) return -1;
    const char *idstart = href + strlen("/photos-index-aid-");
    const char *idend = find_from(h, hlen, (size_t)(idstart - h), ".html");
    if (!idend) return -1;
    out->id = strtol(idstart, NULL, 10);

    /* title: prefer the <a title="..."> attribute of the result link,
     * then strip any inline <em>...</em> tags. This avoids the cover
     * <img alt=...> which carries the same text but no useful title. */
    size_t tpos = (size_t)(href - h);
    char *title_attr = extract_attr(h, hlen, &tpos, "title");
    if (title_attr) {
        /* strip <em> and </em> tags */
        char *clean = malloc(strlen(title_attr) + 1);
        if (clean) {
            size_t j = 0;
            for (size_t i = 0; title_attr[i]; i++) {
                if (strncmp(title_attr + i, "<em>", 4) == 0) { i += 3; continue; }
                if (strncmp(title_attr + i, "</em>", 5) == 0) { i += 4; continue; }
                clean[j++] = title_attr[i];
            }
            clean[j] = '\0';
            out->title = filename_filter(clean);
            free(clean);
        } else {
            out->title = xstrdup("");
        }
        free(title_attr);
    } else {
        out->title = xstrdup("");
    }

    /* cover image src within block */
    size_t ipos = li_start;
    char *src = extract_attr(h, hlen, &ipos, "src");
    if (src) {
        if (strncmp(src, "//", 2) == 0) {
            size_t need = strlen(src) + 7;
            char *cov = malloc(need);
            snprintf(cov, need, "https:%s", src);
            free(src);
            out->cover = cov;
        } else if (strncmp(src, "http", 4) == 0) {
            out->cover = src;
        } else {
            size_t need = strlen(src) + 9;
            char *cov = malloc(need);
            snprintf(cov, need, "https://%s", src);
            free(src);
            out->cover = cov;
        }
    }
    /* additional info: find info_col div text */
    const char *ic = find_from(h, hlen, li_start, "info_col");
    if (ic) {
        size_t icpos = (size_t)(ic - h);
        char *info = extract_tag_text(h, hlen, &icpos, "</div>");
        if (info) {
            size_t sl = 0, el = strlen(info);
            while (sl < el && isspace((unsigned char)info[sl])) sl++;
            while (el > sl && isspace((unsigned char)info[el - 1])) el--;
            out->additional = xstrndup(info + sl, el - sl);
            free(info);
            /* strip embedded CR/LF (the info_col text contains a line break
             * between e.g. "32張圖片，" and "創建於...") so it renders on one
             * clean line in the terminal/TextView. */
            for (size_t k = 0; out->additional[k]; k++) {
                if (out->additional[k] == '\r' || out->additional[k] == '\n')
                    out->additional[k] = ' ';
            }
        }
    }
    return 0;
}

int parse_search(const char *html, int is_tag, search_result *out) {
    memset(out, 0, sizeof(*out));
    size_t hlen = strlen(html);

    /* Collect <li ... gallary_item ...> blocks. We scan for "gallary_item". */
    size_t cap = 16;
    out->items = calloc(cap, sizeof(comic_entry));
    if (!out->items) return -1;

    size_t pos = 0;
    const char *marker;
    while ((marker = find_from(html, hlen, pos, "gallary_item")) != NULL) {
        /* find enclosing <li ...> start */
        const char *li_open = html;
        /* walk back to a '<' that begins an <li */
        const char *p = marker;
        int found = 0;
        while (p > html) {
            p--;
            if (*p == '<' && (p[1] == 'l' || p[1] == 'L') &&
                (p[2] == 'i' || p[2] == 'I') &&
                (p[3] == ' ' || p[3] == '>' || p[3] == '\t' || p[3] == '\n')) {
                li_open = p;
                found = 1;
                break;
            }
        }
        if (!found) { pos = (size_t)(marker - html) + 12; continue; }
        /* find matching </li> */
        const char *li_close = find_from(html, hlen, (size_t)(li_open - html),
                                         "</li>");
        if (!li_close) { pos = (size_t)(marker - html) + 12; continue; }
        size_t li_end = (size_t)(li_close - html) + strlen("</li>");

        comic_entry e;
        if (parse_one_li(html, hlen, (size_t)(li_open - html), li_end, &e) == 0) {
            if (out->count >= (int)cap) {
                cap *= 2;
                out->items = realloc(out->items, cap * sizeof(comic_entry));
            }
            out->items[out->count++] = e;
        }
        pos = li_end;
    }

    /* current page: .thispage text */
    out->current_page = 1;
    const char *tp = find_from(html, hlen, 0, "thispage");
    if (tp) {
        size_t tppos = (size_t)(tp - html);
        char *txt = extract_tag_text(html, hlen, &tppos, "</span>");
        if (txt) {
            out->current_page = strtol(txt, NULL, 10);
            if (out->current_page <= 0) out->current_page = 1;
            free(txt);
        }
    }

    /* total page */
    if (is_tag) {
        out->total_page = 1;
        /* last <a> inside .f_left.paginator */
        const char *pag = find_from(html, hlen, 0, "f_left");
        if (pag) {
            size_t pstart = (size_t)(pag - html);
            /* find paginator block end by next </div> after f_left */
            const char *pend = find_from(html, hlen, pstart, "</div>");
            size_t pend_pos = pend ? (size_t)(pend - html) : hlen;
            /* scan <a ...>NUM</a> and take max numeric */
            size_t apos = pstart;
            long best = 1;
            const char *a;
            while ((a = find_from(html, hlen, apos, "<a ")) != NULL &&
                   (size_t)(a - html) < pend_pos) {
                size_t ap = (size_t)(a - html);
                char *txt = extract_tag_text(html, hlen, &ap, "</a>");
                if (txt) {
                    long v = strtol(txt, NULL, 10);
                    if (v > best) best = v;
                    free(txt);
                }
                apos = ap;
            }
            out->total_page = best;
        }
    } else {
        /* total count in #bodywrap .result > b — a <b> element inside the
         * result div. Find the first "<b>" after the result block. */
        const char *res = find_from(html, hlen, 0, "class=\"result\"");
        if (!res) res = find_from(html, hlen, 0, "result");
        long total = 0;
        if (res) {
            size_t rpos = (size_t)(res - html);
            const char *btag = find_from(html, hlen, rpos, "<b>");
            if (btag) {
                size_t bpos = (size_t)(btag - html); /* at '<' of <b> */
                char *txt = extract_tag_text(html, hlen, &bpos, "</b>");
                if (txt) {
                    /* strip commas */
                    char clean[64];
                    int ci = 0;
                    for (int i = 0; txt[i] && ci < 63; i++)
                        if (txt[i] != ',') clean[ci++] = txt[i];
                    clean[ci] = '\0';
                    total = strtol(clean, NULL, 10);
                    free(txt);
                }
            }
        }
        out->total_page = (total + 23) / 24;
        if (out->total_page <= 0) out->total_page = 1;
    }
    return 0;
}

void free_search_result(search_result *s) {
    for (int i = 0; i < s->count; i++) {
        free(s->items[i].title);
        free(s->items[i].title_html);
        free(s->items[i].cover);
        free(s->items[i].additional);
    }
    free(s->items);
    s->items = NULL;
    s->count = 0;
}

static int is_fake_img(const char *url) {
    return strstr(url, "shoucang.jpg") != NULL;
}

/* Rewrite the host part of an image URL to g_img_host. The gallery HTML may
 * hardcode the unreachable img5.wnimg1.ru / img5.wnimg.ru (or use the
 * fast_img_host JS variable, already substituted to g_img_host upstream), so
 * we normalise the host unconditionally. Keeps scheme (https: or //) and path.
 * Returns a malloc'd string (caller frees); on alloc failure returns NULL. */
static char *rewrite_img_host(const char *url) {
    const char *host_start;
    if (strncmp(url, "https://", 8) == 0) { host_start = url + 8; }
    else if (strncmp(url, "http://", 7) == 0) { host_start = url + 7; }
    else if (strncmp(url, "//", 2) == 0) { host_start = url + 2; }
    else return xstrdup(url); /* no scheme, leave as-is */

    const char *path = strchr(host_start, '/');
    size_t host_len = path ? (size_t)(path - host_start) : strlen(host_start);
    size_t new_len = strlen(g_img_host);
    size_t pre = (size_t)(host_start - url);
    size_t post = strlen(host_start) - host_len; /* includes leading '/' or 0 */
    char *out = malloc(pre + new_len + post + 1);
    if (!out) return NULL;
    memcpy(out, url, pre);
    memcpy(out + pre, g_img_host, new_len);
    memcpy(out + pre + new_len, host_start + host_len, post + 1); /* copies NUL */
    return out;
}

/* Public fallback builder: rewrite host to g_img_host, then append the
 * .w1280.webp variant suffix (unless already present). Used by download_image
 * to retry a failed primary URL via the reachable mirror. */
char *build_fallback_url(const char *url) {
    char *ru = rewrite_img_host(url);
    if (!ru) return NULL;
    size_t ul = strlen(ru);
    static const char SUF[] = ".w1280.webp"; /* 11 chars */
    if (ul >= 11 && strcmp(ru + ul - 11, SUF) == 0) {
        return ru; /* already a variant URL */
    }
    char *vu = malloc(ul + 12); /* 11 chars + NUL */
    if (!vu) { free(ru); return NULL; }
    memcpy(vu, ru, ul);
    memcpy(vu + ul, SUF, 12);   /* 11 chars + NUL */
    free(ru);
    return vu;
}

/* Extract the page-declared image host: `var fast_img_host='...';`. On the
 * real site the imglist sits inside document.writeln("..."), so the value is
 * written as `fast_img_host=\"\"` — i.e. an EMPTY string with its quotes
 * escaped as \". The gallery URLs are then `fast_img_host+\"//img5.wnimg1.ru/
 * data/.../NNN.webp\"`, i.e. they carry their own full host, so an empty
 * fast_img_host is correct and expected. This is used ONLY as the PRIMARY
 * download target; when that host is unreachable (2.3 BearSSL / Cloudflare TLS
 * fingerprint), download_image() falls back to g_img_host + .w1280.webp via
 * build_fallback_url(). We must NOT substitute g_img_host here, or the
 * "fallback" would become a hardcoded override. Defaults to "" when the page
 * omits the variable (URLs already carry their own host). */
static char *extract_fast_img_host(const char *html, size_t hlen) {
    const char *k = find_from(html, hlen, 0, "fast_img_host");
    if (k) {
        const char *eq = k + 13;
        while (eq < html + hlen && *eq != '=') eq++;
        if (eq < html + hlen) {
            eq++; /* past '=' */
            const char *semi = eq;
            while (semi < html + hlen && *semi != ';' && *semi != '\n') semi++;
            char *raw = xstrndup(eq, (size_t)(semi - eq));
            if (raw) {
                /* unescape \" -> " (document.writeln escaping) */
                size_t j = 0;
                for (size_t i = 0; raw[i]; i++) {
                    if (raw[i] == '\\' && raw[i + 1] == '"') { raw[j++] = '"'; i++; }
                    else raw[j++] = raw[i];
                }
                raw[j] = '\0';
                /* strip one surrounding quote pair ('...' or "...") */
                size_t n = strlen(raw);
                if (n >= 2 && ((raw[0] == '"' && raw[n - 1] == '"') ||
                               (raw[0] == '\'' && raw[n - 1] == '\''))) {
                    memmove(raw, raw + 1, n - 2);
                    raw[n - 2] = '\0';
                }
                strip_whitespace(raw);
                return raw;
            }
        }
    }
    return xstrdup("");
}

int parse_imglist(const char *html, char ***out_urls, int *out_count) {
    *out_urls = NULL;
    *out_count = 0;
    size_t hlen = strlen(html);

    char *img_host = extract_fast_img_host(html, hlen);

    const char *line = find_from(html, hlen, 0, "var imglist = ");
    if (!line) { free(img_host); return -1; }

    const char *start = strchr(line, '[');
    if (!start) { free(img_host); return -1; }
    /* find matching ']' — Rust uses rfind(']') */
    const char *end = NULL;
    for (const char *p = line; *p; p++) if (*p == ']') end = p;
    if (!end || end < start) { free(img_host); return -1; }

    size_t json_len = (size_t)(end - start + 1);
    char *json = xstrndup(start, json_len);
    if (!json) { free(img_host); return -1; }

    /* normalise into valid JSON */
    /* url: -> "url":  and  caption: -> "caption": */
    char *norm = malloc(json_len * 2 + 1);
    if (!norm) { free(json); return -1; }
    size_t oi = 0;
    for (size_t i = 0; i < json_len; i++) {
        if (json[i] == 'u' && strncmp(json + i, "url:", 4) == 0) {
            norm[oi++] = '"'; norm[oi++] = 'u'; norm[oi++] = 'r';
            norm[oi++] = 'l'; norm[oi++] = '"'; norm[oi++] = ':'; i += 3;
        } else if (json[i] == 'c' && strncmp(json + i, "caption:", 8) == 0) {
            norm[oi++] = '"'; norm[oi++] = 'c'; norm[oi++] = 'a';
            norm[oi++] = 'p'; norm[oi++] = 't'; norm[oi++] = 'i';
            norm[oi++] = 'o'; norm[oi++] = 'n'; norm[oi++] = '"'; norm[oi++] = ':';
            i += 7;
        } else if (json[i] == 'f' && strncmp(json + i, "fast_img_host", 13) == 0) {
            /* Substitute the JS variable `fast_img_host` with its value.
             * The original is:  fast_img_host + "/path"
             * We emit:  "<value>/path"   (value may be empty for the real
             * site where the literal already carries "//host/path").
             * The literal's opening quote may be escaped as \" — skip it. */
            size_t hl = strlen(img_host);
            if (oi + hl + 1 > json_len * 2) break;
            norm[oi++] = '"';               /* opening quote of the value */
            memcpy(norm + oi, img_host, hl);
            oi += hl;
            size_t j = i + 13;
            while (j < json_len && (json[j] == '+' || json[j] == ' ' ||
                                    json[j] == '\t')) j++;
            /* skip the literal's own opening quote (" or \") */
            if (j < json_len && json[j] == '\\' && json[j + 1] == '"') j += 2;
            else if (j < json_len && (json[j] == '"' || json[j] == '\'')) j++;
            i = j - 1; /* loop's i++ lands on the first char of the literal */
        } else if (json[i] == '\\' && json[i + 1] == '"') {
            /* \" -> " */
            norm[oi++] = '"'; i += 1;
        } else {
            norm[oi++] = json[i];
        }
    }
    norm[oi] = '\0';
    free(json);

    /* now parse array of {url, caption}. Simple scanner. */
    char **urls = malloc(16 * sizeof(char *));
    int count = 0, cap = 16;
    if (!urls) { free(norm); return -1; }
    const char *p = norm;
    while (*p) {
        const char *uo = strstr(p, "\"url\"");
        if (!uo) break;
        const char *colon = strchr(uo, ':');
        if (!colon) break;
        const char *q = strchr(colon, '"');
        if (!q) break;
        q++; /* past opening quote */
        const char *qe = strchr(q, '"');
        if (!qe) break;
        size_t ulen = (size_t)(qe - q);
        char *url = xstrndup(q, ulen);
        if (url) {
            strip_whitespace(url);
            if (!is_fake_img(url)) {
                /* prepend https: if starts with // */
                if (strncmp(url, "//", 2) == 0) {
                    size_t need = ulen + 7;
                    char *hu = malloc(need);
                    snprintf(hu, need, "https:%s", url);
                    free(url);
                    url = hu;
                }
                if (count >= cap) {
                    cap *= 2;
                    urls = realloc(urls, cap * sizeof(char *));
                }
                /* Keep the site's ORIGINAL image URL as the primary target.
                 * The download path falls back to g_img_host + .w1280.webp
                 * variant ONLY if this primary host is unreachable (see
                 * build_fallback_url in wnacg.c's download_image). */
                urls[count++] = url;
            } else {
                free(url);
            }
        }
        p = qe + 1;
    }
    free(norm);
    free(img_host);

    *out_urls = urls;
    *out_count = count;
    return 0;
}
