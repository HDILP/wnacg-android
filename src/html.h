#ifndef WNACG_HTML_H
#define WNACG_HTML_H

#include <stddef.h>
#include "net.h"

/* A single search result entry. */
typedef struct {
    long id;
    char *title;          /* cleaned title (filename-safe) */
    char *title_html;     /* raw title attribute text */
    char *cover;          /* full cover URL */
    char *additional;     /* e.g. "209張圖片 ..." */
} comic_entry;

typedef struct {
    comic_entry *items;
    int count;
    long current_page;
    long total_page;
} search_result;

/* Parse a search result HTML page (wnacg search/index.php output).
 * is_tag: 1 for tag searches (same f=tag endpoint, same layout; only the
 *         total-page detection differs), 0 for keyword search.
 * Returns 0 on success. Caller frees with free_search_result(). */
int parse_search(const char *html, int is_tag, search_result *out);
void free_search_result(search_result *s);

/* Parse the comic detail page and extract the embedded imglist JSON array.
 * The page contains a line:  var imglist = [ {url:..., caption:...}, ... ];
 * Extracted URLs are stored (with https: prefix added if missing).
 * The last fake entry (shoucang.jpg) is filtered out.
 * Returns 0 on success with out_urls/out_count populated (caller frees). */
int parse_imglist(const char *html, char ***out_urls, int *out_count);

/* Build a fallback image URL for a primary URL that failed to download:
 * rewrite the host to g_img_host (the reachable mirror, defaults to
 * webp.wnacgimg.date) and append the .w1280.webp transcoded-variant suffix
 * UNLESS already present. Returns a malloc'd string (caller frees), or NULL
 * on alloc failure. Used by download_image() to retry via the mirror. */
char *build_fallback_url(const char *url);

/* Sanitise a title into a filesystem-safe string (in place-ish, returns
 * malloc'd copy). Replaces illegal chars with '_'. */
char *filename_filter(const char *s);

#endif /* WNACG_HTML_H */
