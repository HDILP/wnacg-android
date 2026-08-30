/* Image-CDN host runtime override.
 *
 * Kept in its own translation unit so that both the full binary (wnacg.c)
 * and the standalone parser unit test (tests/parse_test.c, which compiles
 * html.c WITHOUT wnacg.c) link the symbol. html.c references g_img_host from
 * extract_fast_img_host(); if the definition lived only in wnacg.c the test
 * binary would fail with "undefined reference to g_img_host".
 *
 * See net.h for the declaration + design note. */
#include <stdlib.h>

#include "net.h"

#ifndef DEFAULT_IMG_HOST
#define DEFAULT_IMG_HOST "webp.wnacgimg.date"
#endif

const char *g_img_host = DEFAULT_IMG_HOST;

void init_img_host(void) {
    const char *e = getenv("WNACG_IMG_HOST");
    if (e && *e) g_img_host = e;
}
