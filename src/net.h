#ifndef WNACG_NET_H
#define WNACG_NET_H

#include <stddef.h>

#ifdef __ANDROID__
/* A standalone bionic executable cannot resolve the 'stdout'/'stderr' FILE*
 * pointers (they live in libc.so and are not exported for static linking), so
 * any printf/fprintf reference fails to link with "undefined reference to
 * 'stderr'/'stdout'". Route all formatted output through write() to the raw
 * file descriptors instead. Desktop builds keep real stdio. */
#include <unistd.h>
#include <stdarg.h>
static int wn_write_fd(int fd, const char *fmt, va_list ap) {
    char buf[4096];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) return -1;
    if ((size_t)n > sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    return (int)write(fd, buf, (size_t)n);
}
static int wn_out(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = wn_write_fd(1, fmt, ap); va_end(ap); return r; }
static int wn_err(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = wn_write_fd(2, fmt, ap); va_end(ap); return r; }
#define printf(...)        wn_out(__VA_ARGS__)
#define fprintf(s, ...)    wn_err(__VA_ARGS__)
#endif

/* Parsed URL. All pointers borrow from a single malloc'd buffer held by
 * the caller; we keep a copy here for clarity. */
typedef struct {
    int https;
    char *host;     /* e.g. www.wn09.shop */
    int port;       /* default 80 or 443 */
    char *path;     /* includes query, e.g. /search/index.php?q=x */
} parsed_url;

typedef struct {
    int status;         /* HTTP status code, 0 on transport failure */
    char *body;         /* malloc'd response body (NUL not guaranteed) */
    size_t body_len;
    int body_cap;
    char *location;     /* malloc'd redirect Location if 3xx, else NULL */
} http_response;

/* Parse a URL. Returns 0 on success, -1 on error. Caller frees nothing
 * special; parsed_url fields are malloc'd and must be freed with
 * free_parsed_url(). */
int parse_url(const char *url, parsed_url *out);
void free_parsed_url(parsed_url *p);

/* Perform an HTTP GET.
 * referer / cookie may be NULL.
 * If https is requested, a TLS connection is established via tls_connect
 * (see tls.h); on success the socket is wrapped.
 * Follows up to max_redirects (e.g. 5). On 3xx the Location is followed
 * automatically; final response is returned.
 * Returns 0 on success (response populated, possibly status!=200),
 * -1 on transport/TLS failure. */
int http_get(const char *url, const char *referer, const char *cookie,
             int max_redirects, http_response *out);

void free_http_response(http_response *r);

#endif /* WNACG_NET_H */
