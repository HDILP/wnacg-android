/* webp_bmp.c — WebP-aware image downloader.
 *
 * On Android (API < 14 has no WebP decoder in BitmapFactory) the native binary
 * re-encodes WebP covers to a PNG (PNG decodes on EVERY Android version
 * including 2.3; BMP does NOT decode on API 9). Non-WebP images pass through
 * untouched.
 *
 * The PNG writer (png_write.c) uses zlib, which the NDK provides as libz — but
 * ONLY on Android. On the host build (parser unit tests, no libwebp/libz
 * linked) the WebP decode path is compiled out and save_image_auto is a plain
 * pass-through.
 */
#include "webp_bmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"          /* http_get / http_response / free_http_response */
#include "png_write.h"    /* png_write_rgb */

#ifdef __ANDROID__
#include "webp/decode.h"  /* WebPDecodeRGBA (only present in the Android build) */

static int is_webp(const unsigned char *data, long len) {
    return len >= 12 && data[0] == 'R' && data[1] == 'I' &&
           data[2] == 'F' && data[3] == 'F' &&
           data[8] == 'W' && data[9] == 'E' &&
           data[10] == 'B' && data[11] == 'P';
}
#endif /* __ANDROID__ */

int save_image_auto(const char *url, const char *out_path) {
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

#ifdef __ANDROID__
    if (is_webp((const unsigned char *)r.body, r.body_len)) {
        int w = 0, h = 0;
        uint8_t *rgba = WebPDecodeRGBA((const uint8_t *)r.body,
                                       (size_t)r.body_len, &w, &h);
        free_http_response(&r);
        if (!rgba || w <= 0 || h <= 0) {
            fprintf(stderr, "  [!] WebP decode failed: %s\n", url);
            return -1;
        }
        int rc = png_write_rgb(out_path, rgba, w, h);
        free(rgba);
        if (rc != 0) {
            fprintf(stderr, "  [!] PNG write failed: %s\n", out_path);
            return -1;
        }
        fprintf(stderr, "  [i] WebP -> PNG %dx%d: %s\n", w, h, out_path);
        return 0;
    }
#endif /* __ANDROID__ */

    /* pass-through for PNG/JPEG (and on host: also WebP, unmodified) */
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        free_http_response(&r);
        fprintf(stderr, "  [!] cannot open %s\n", out_path);
        return -1;
    }
    fwrite(r.body, 1, r.body_len, f);
    fclose(f);
    free_http_response(&r);
    return 0;
}
