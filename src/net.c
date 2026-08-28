#include "net.h"
#include "tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __ANDROID__
/* A standalone bionic executable cannot resolve 'stderr' from the shared libc,
   so link fails with "undefined reference to 'stderr'". Route diagnostics to
   stdout, which the Java wrapper reads into the on-screen log anyway. */
#define stderr stdout
#endif

/* ---------- URL parsing ---------- */

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

int parse_url(const char *url, parsed_url *out) {
    memset(out, 0, sizeof(*out));
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        out->https = 1;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        out->https = 0;
        p += 7;
    } else {
        return -1;
    }
    /* host */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    size_t host_len;
    if (colon && (!slash || colon < slash)) {
        host_len = (size_t)(colon - p);
    } else if (slash) {
        host_len = (size_t)(slash - p);
    } else {
        host_len = strlen(p);
    }
    char *host = malloc(host_len + 1);
    if (!host) return -1;
    memcpy(host, p, host_len);
    host[host_len] = '\0';
    out->host = host;

    /* port */
    if (colon && (!slash || colon < slash)) {
        int port = atoi(colon + 1);
        out->port = port > 0 ? port : (out->https ? 443 : 80);
        p = colon + 1;
        const char *sl = strchr(p, '/');
        p = sl ? sl : p + strlen(p);
    } else {
        out->port = out->https ? 443 : 80;
    }

    /* path (default "/") */
    const char *path = slash ? slash : "/";
    out->path = xstrdup(path);
    if (!out->path) { free(out->host); return -1; }
    return 0;
}

void free_parsed_url(parsed_url *p) {
    free(p->host);
    free(p->path);
    p->host = p->path = NULL;
}

/* ---------- low-level socket ---------- */

static int tcp_connect(const char *host, int port) {
    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        /* 8s connect timeout-ish (blocking, acceptable for a CLI) */
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    if (res) freeaddrinfo(res);
    return fd;
}

/* ---------- HTTP response parsing ---------- */

/* Append raw bytes to response body, growing as needed. */
static int body_append(http_response *r, const char *buf, size_t len) {
    if (r->body_len + len + 1 > (size_t)r->body_cap) {
        size_t ncap = r->body_cap ? r->body_cap : 4096;
        while (r->body_len + len + 1 > ncap) ncap *= 2;
        char *nb = realloc(r->body, ncap);
        if (!nb) return -1;
        r->body = nb;
        r->body_cap = ncap;
    }
    memcpy(r->body + r->body_len, buf, len);
    r->body_len += len;
    r->body[r->body_len] = '\0';
    return 0;
}

/* Read exactly one HTTP response from a buffered reader.
 * reader returns bytes; for plain sockets we use recv. We implement a
 * simple line-based header parser then body (content-length or chunked). */
typedef struct {
    int fd;
    void *tls_ctx;   /* if non-NULL, read via TLS instead of raw recv */
    char *buf;
    size_t len;     /* bytes held */
    size_t pos;     /* read position */
} conn_reader;

static int reader_fill(conn_reader *cr) {
    if (cr->pos >= cr->len) {
        cr->pos = cr->len = 0;
    }
    /* compact */
    if (cr->pos > 0 && cr->len > cr->pos) {
        memmove(cr->buf, cr->buf + cr->pos, cr->len - cr->pos);
        cr->len -= cr->pos;
        cr->pos = 0;
    }
    if (cr->len >= 65536) return 0;
    ssize_t n;
    if (cr->tls_ctx) {
        n = tls_recv(cr->tls_ctx, (unsigned char *)cr->buf + cr->len,
                     65536 - cr->len);
        if (n < 0) {
            return -1;   /* TLS error / connection closed */
        }
        /* n == 0 means clean close: treat as EOF */
    } else {
        n = recv(cr->fd, cr->buf + cr->len, 65536 - cr->len, 0);
        if (n <= 0) return -1;
    }
    cr->len += (size_t)n;
    return 0;
}

static int reader_getc(conn_reader *cr) {
    if (cr->pos >= cr->len) {
        if (reader_fill(cr) != 0) return -1;
        if (cr->pos >= cr->len) return -1;
    }
    return (unsigned char)cr->buf[cr->pos++];
}

/* Read one line (up to CRLF). Stores into line (NUL-terminated), returns
 * length excluding CRLF, or -1 on EOF. Strips trailing CRLF / LF. */
static int reader_readline(conn_reader *cr, char *line, size_t cap) {
    size_t i = 0;
    int got = 0;
    int c;
    while ((c = reader_getc(cr)) >= 0) {
        got = 1;
        if (c == '\n') {
            if (i > 0 && line[i - 1] == '\r') i--;
            line[i] = '\0';
            return (int)i;
        }
        if (i + 1 < cap) line[i++] = (char)c;
    }
    if (!got) return -1;
    line[i] = '\0';
    return (int)i;
}

/* Read exactly n bytes into dst. */
static int reader_readn(conn_reader *cr, char *dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        if (cr->pos >= cr->len) {
            if (reader_fill(cr) != 0) return -1;
            if (cr->pos >= cr->len) return -1;
        }
        size_t avail = cr->len - cr->pos;
        size_t take = avail < (n - got) ? avail : (n - got);
        memcpy(dst + got, cr->buf + cr->pos, take);
        cr->pos += take;
        got += take;
    }
    return 0;
}

/* Parse headers. Returns 0 on success. Sets content_length (-1 if unknown),
 * chunked flag, and location (malloc'd, may be NULL). */
static int parse_headers(conn_reader *cr, int *status_out,
                         long *content_length, int *chunked,
                         char **location) {
    char line[8192];
    *content_length = -1;
    *chunked = 0;
    *location = NULL;
    /* status line */
    int n = reader_readline(cr, line, sizeof(line));
    if (n < 0) return -1;
    /* "HTTP/1.1 200 OK" */
    int st = 0;
    sscanf(line, "HTTP/%*s %d", &st);
    *status_out = st;
    /* headers */
    for (;;) {
        n = reader_readline(cr, line, sizeof(line));
        if (n < 0) return -1;
        if (n == 0) break; /* end of headers */
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *key = line;
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        size_t klen = strlen(key);
        /* case-insensitive compare */
        int is_cl = (strncasecmp(key, "Content-Length", klen) == 0);
        int is_te = (strncasecmp(key, "Transfer-Encoding", klen) == 0);
        int is_loc = (strncasecmp(key, "Location", klen) == 0);
        if (is_cl) {
            *content_length = atol(val);
        } else if (is_te && strncasecmp(val, "chunked", 7) == 0) {
            *chunked = 1;
        } else if (is_loc) {
            free(*location);
            *location = xstrdup(val);
        }
    }
    return 0;
}

static int read_chunked_body(conn_reader *cr, http_response *r) {
    for (;;) {
        char line[256];
        int n = reader_readline(cr, line, sizeof(line));
        if (n < 0) return -1;
        /* parse chunk size (hex), ignore extensions after ';' */
        char *semi = strchr(line, ';');
        if (semi) *semi = '\0';
        long size = strtol(line, NULL, 16);
        if (size <= 0) {
            /* last chunk; read trailing CRLF */
            reader_readline(cr, line, sizeof(line));
            break;
        }
        /* read 'size' bytes */
        char *chunk = malloc(size);
        if (!chunk) return -1;
        if (reader_readn(cr, chunk, (size_t)size) != 0) {
            free(chunk); return -1;
        }
        body_append(r, chunk, (size_t)size);
        free(chunk);
        /* trailing CRLF after chunk */
        reader_readline(cr, line, sizeof(line));
    }
    return 0;
}

static int read_fixed_body(conn_reader *cr, http_response *r, long len) {
    if (len < 0) {
        /* unknown: read until EOF */
        for (;;) {
            if (cr->pos >= cr->len) {
                if (reader_fill(cr) != 0) break;
                if (cr->pos >= cr->len) break;
            }
            size_t avail = cr->len - cr->pos;
            if (body_append(r, cr->buf + cr->pos, avail) != 0) return -1;
            cr->pos += avail;
        }
        return 0;
    }
    long remaining = len;
    while (remaining > 0) {
        if (cr->pos >= cr->len) {
            if (reader_fill(cr) != 0) return -1;
            if (cr->pos >= cr->len) return -1;
        }
        size_t avail = cr->len - cr->pos;
        size_t take = avail < (size_t)remaining ? avail : (size_t)remaining;
        if (body_append(r, cr->buf + cr->pos, take) != 0) return -1;
        cr->pos += take;
        remaining -= (long)take;
    }
    return 0;
}

/* ---------- main GET ---------- */

int http_get(const char *url, const char *referer, const char *cookie,
             int max_redirects, http_response *out) {
    memset(out, 0, sizeof(*out));
    parsed_url pu;
    if (parse_url(url, &pu) != 0) return -1;

    int redirects_left = max_redirects;
    char *working_url = xstrdup(url);

    for (;;) {
        parsed_url cur;
        if (parse_url(working_url, &cur) != 0) { free(working_url); free_parsed_url(&pu); return -1; }

        int fd = tcp_connect(cur.host, cur.port);
        if (fd < 0) {
            free_parsed_url(&cur);
            free(working_url);
            free_parsed_url(&pu);
            return -1;
        }
        /* bounded receive so a misbehaving server can't block forever;
         * 60s is generous for a single page over a 2G/3G link. We rely on
         * "Connection: close" for clean EOF in the normal case. */
        struct timeval _tv = { .tv_sec = 60, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &_tv, sizeof(_tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &_tv, sizeof(_tv));

        void *tls_ctx = NULL;
        int tls_ok = 0;
        if (cur.https) {
            tls_ctx = tls_connect(fd, cur.host);
            if (!tls_ctx) {
                fprintf(stderr, "[net] TLS握手失败 (BearSSL err=0x%04x)\n",
                        tls_last_error(NULL));
                close(fd);
                free_parsed_url(&cur);
                free(working_url);
                free_parsed_url(&pu);
                return -1;
            }
            tls_ok = 1;
        }

        /* Build request */
        char req[8192];
        char extra[4096];
        extra[0] = '\0';
        if (referer) {
            int _n = (int)strlen(extra);
            snprintf(extra + _n, sizeof(extra) - _n, "Referer: %s\r\n", referer);
        }
        if (cookie) {
            int _n = (int)strlen(extra);
            snprintf(extra + _n, sizeof(extra) - _n, "Cookie: %s\r\n", cookie);
        }
        int rl = snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (wnacg-android)\r\n"
            "Accept: */*\r\n"
            "%s"
            "Connection: close\r\n\r\n",
            cur.path, cur.host, extra);

        int send_ok;
        if (tls_ok) {
            send_ok = tls_write_all(tls_ctx, req, (size_t)rl);
        } else {
            ssize_t w = write(fd, req, (size_t)rl);
            send_ok = (w == (ssize_t)rl);
        }
        if (!send_ok) {
            if (tls_ok) tls_close(tls_ctx);
            else close(fd);
            free_parsed_url(&cur);
            free(working_url);
            free_parsed_url(&pu);
            return -1;
        }

        /* Read response. For TLS we read through tls_recv; for plain we
         * use a conn_reader over the socket fd. */
        conn_reader cr;
        cr.fd = fd;
        cr.tls_ctx = tls_ok ? tls_ctx : NULL;
        cr.buf = malloc(65536);
        cr.len = cr.pos = 0;

        /* Stream the TLS/raw response through the reader; parse_headers and
         * body readers stop at the correct length boundary, so we do NOT need
         * the server to close the connection (keep-alive safe). */

        int status = 0;
        long cl = -1;
        int chunked = 0;
        char *location = NULL;
        if (parse_headers(&cr, &status, &cl, &chunked, &location) != 0) {
            free(cr.buf);
            free(location);
            if (tls_ok) tls_close(tls_ctx); else close(fd);
            free_parsed_url(&cur);
            free(working_url);
            free_parsed_url(&pu);
            return -1;
        }

        /* free previous body */
        free(out->body);
        out->body = NULL;
        out->body_len = 0;
        out->body_cap = 0;
        free(out->location);
        out->location = NULL;

        if (chunked) {
            if (read_chunked_body(&cr, out) != 0) { /* best effort */ }
        } else {
            if (read_fixed_body(&cr, out, cl) != 0) { /* best effort */ }
        }
        out->status = status;

        if (tls_ok) tls_close(tls_ctx); else close(fd);
        free(cr.buf);

        /* redirect? */
        if ((status == 301 || status == 302 || status == 303 ||
             status == 307 || status == 308) && location && redirects_left > 0) {
            /* resolve relative location */
            char *next = NULL;
            if (strncmp(location, "http", 4) == 0) {
                next = xstrdup(location);
            } else if (location[0] == '/') {
                /* absolute path on same host */
                size_t hl = strlen(cur.host);
                size_t need = hl + 8 + strlen(location) + 16;
                next = malloc(need);
                snprintf(next, need, "%s://%s:%d%s",
                         cur.https ? "https" : "http", cur.host, cur.port, location);
            } else {
                /* relative to current path dir */
                const char *slash = strrchr(cur.path, '/');
                size_t dirlen = slash ? (size_t)(slash - cur.path) : 0;
                size_t need = strlen(cur.host) + 8 + dirlen + 1 + strlen(location) + 16;
                next = malloc(need);
                snprintf(next, need, "%s://%s:%d%.*s/%s",
                         cur.https ? "https" : "http", cur.host, cur.port,
                         (int)dirlen, cur.path, location);
            }
            free(location);
            free(working_url);
            working_url = next;
            free_parsed_url(&cur);
            redirects_left--;
            continue;
        }

        free(location);
        free_parsed_url(&cur);
        break;
    }

    free(working_url);
    free_parsed_url(&pu);
    return 0;
}

void free_http_response(http_response *r) {
    free(r->body);
    free(r->location);
    r->body = NULL;
    r->location = NULL;
    r->body_len = r->body_cap = 0;
}
