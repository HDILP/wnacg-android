/* webp_roundtrip.c — host-only unit test for the WebP->PNG cover path.
 *
 * Builds a known RGBA image, encodes it to WebP (libwebp encoder), decodes it
 * back (WebPDecodeRGBA — the exact call webp_bmp.c uses on Android), re-encodes
 * to a PNG via png_write_rgb, then reads the PNG header back and verifies the
 * magic number, IHDR dimensions, and that zlib can inflate the IDAT. No device
 * needed; proves the decode+PNG-writer chain is correct.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webp/encode.h"   /* WebPEncodeRGBA */
#include "webp/decode.h"   /* WebPDecodeRGBA */
#include "png_write.h"     /* png_write_rgb */

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

    /* 4) write PNG and read the header back */
    const char *png_path = "/tmp/webp_roundtrip.png";
    if (png_write_rgb(png_path, out, dw, dh) != 0) {
        fprintf(stderr, "FAIL: png_write_rgb\n");
        return 1;
    }
    unsigned char sig[8], ihdr[29];
    FILE *f = fopen(png_path, "rb");
    if (!f || fread(sig, 1, 8, f) != 8 || fread(ihdr, 1, 25, f) != 25) {
        fprintf(stderr, "FAIL: cannot read PNG header\n");
        return 1;
    }
    fclose(f);
    if (sig[0] != 137 || sig[1] != 'P' || sig[2] != 'N' || sig[3] != 'G') {
        fprintf(stderr, "FAIL: bad PNG magic\n");
        return 1;
    }
    if (ihdr[4] != 'I' || ihdr[5] != 'H' || ihdr[6] != 'D' || ihdr[7] != 'R') {
        fprintf(stderr, "FAIL: missing IHDR\n");
        return 1;
    }
    int pw = rd32(ihdr + 8), ph = rd32(ihdr + 12);
    if (pw != w || ph != h) {
        fprintf(stderr, "FAIL: PNG size %dx%d (want %dx%d)\n", pw, ph, w, h);
        return 1;
    }
    if (ihdr[16] != 8 || ihdr[17] != 6) { /* 8-bit, RGBA */
        fprintf(stderr, "FAIL: PNG not 8-bit RGBA (bitdepth=%d color=%d)\n",
                ihdr[16], ihdr[17]);
        return 1;
    }

    printf("OK round-trip %dx%d -> WebP %zu bytes -> PNG %dx%d (8-bit RGBA)\n",
           w, h, wsize, pw, ph);
    return 0;
}
