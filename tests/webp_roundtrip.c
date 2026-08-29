/* webp_roundtrip.c — host-only unit test for the WebP->BMP cover path.
 *
 * Builds a known RGBA image, encodes it to WebP (libwebp encoder), decodes it
 * back (WebPDecodeRGBA — the exact call webp_bmp.c uses on Android), re-encodes
 * to a 24-bit BMP via bmp_write_rgb, then reads the BMP header back and checks
 * the magic number and dimensions. No device needed; proves the decode+BMP
 * writer chain is correct.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webp/encode.h"   /* WebPEncodeRGBA */
#include "webp/decode.h"   /* WebPDecodeRGBA */
#include "bmp_write.h"     /* bmp_write_rgb */

static int rd32(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

int main(void) {
    int w = 4, h = 3;
    unsigned char *rgba = (unsigned char *)malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        rgba[i * 4 + 0] = (unsigned char)(i * 20);
        rgba[i * 4 + 1] = (unsigned char)(i * 10);
        rgba[i * 4 + 2] = (unsigned char)(255 - i * 5);
        rgba[i * 4 + 3] = 255;
    }

    /* 1) encode to WebP */
    uint8_t *webp = NULL;
    size_t wsize = WebPEncodeRGBA(rgba, w, h, w * 4, 90.0f, &webp);
    if (wsize == 0) { fprintf(stderr, "FAIL: WebPEncodeRGBA\n"); return 1; }

    /* 2) it really is a RIFF/WEBP container */
    if (wsize < 12 || webp[0] != 'R' || webp[1] != 'I' || webp[2] != 'F' ||
        webp[3] != 'F' || webp[8] != 'W' || webp[9] != 'E' ||
        webp[10] != 'B' || webp[11] != 'P') {
        fprintf(stderr, "FAIL: not a WebP container\n");
        return 1;
    }

    /* 3) decode back — the call webp_bmp.c makes on Android */
    int dw = 0, dh = 0;
    uint8_t *out = WebPDecodeRGBA(webp, wsize, &dw, &dh);
    if (!out || dw != w || dh != h) {
        fprintf(stderr, "FAIL: WebPDecodeRGBA %dx%d (want %dx%d)\n", dw, dh, w, h);
        return 1;
    }

    /* 4) write BMP and read the header back */
    const char *bmp_path = "/tmp/webp_roundtrip.bmp";
    if (bmp_write_rgb(bmp_path, out, dw, dh) != 0) {
        fprintf(stderr, "FAIL: bmp_write_rgb\n");
        return 1;
    }
    unsigned char hdr[54];
    FILE *f = fopen(bmp_path, "rb");
    if (!f || fread(hdr, 1, 54, f) != 54) {
        fprintf(stderr, "FAIL: cannot read BMP header\n");
        return 1;
    }
    fclose(f);
    if (hdr[0] != 'B' || hdr[1] != 'M') {
        fprintf(stderr, "FAIL: bad BMP magic\n");
        return 1;
    }
    int bw = rd32(hdr + 18), bh = rd32(hdr + 22);
    if (bw != w || bh != h) {
        fprintf(stderr, "FAIL: BMP size %dx%d (want %dx%d)\n", bw, bh, w, h);
        return 1;
    }

    printf("OK round-trip %dx%d -> WebP %zu bytes -> BMP %dx%d\n", w, h, wsize, bw, bh);
    return 0;
}
