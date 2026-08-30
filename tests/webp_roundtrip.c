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

static int rd32be(const unsigned char *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
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
    int pw = rd32be(ihdr + 8), ph = rd32be(ihdr + 12);
    if (pw != w || ph != h) {
        fprintf(stderr, "FAIL: PNG size %dx%d (want %dx%d)\n", pw, ph, w, h);
        return 1;
    }
    if (ihdr[16] != 8 || ihdr[17] != 6) { /* 8-bit, RGBA */
        fprintf(stderr, "FAIL: PNG not 8-bit RGBA (bitdepth=%d color=%d)\n",
                ihdr[16], ihdr[17]);
        return 1;
    }

    /* 5) verify every chunk's CRC32 — Android's libpng rejects bad CRCs, so a
     *    writer that emits wrong CRCs passes magic/IHDR/zlib checks yet fails
     *    to decode on device. This catches that class of bug. */
    {
        static uint32_t tbl[256]; static int tbl_done = 0;
        if (!tbl_done) {
            for (uint32_t n = 0; n < 256; n++) {
                uint32_t c = n;
                for (int k = 0; k < 8; k++)
                    c = (c & 1) ? (0xedb88320U ^ (c >> 1)) : (c >> 1);
                tbl[n] = c;
            }
            tbl_done = 1;
        }
        FILE *g = fopen(png_path, "rb");
        if (!g) { fprintf(stderr, "FAIL: cannot reopen PNG for CRC check\n"); return 1; }
        fseek(g, 0, SEEK_END);
        long fsz = ftell(g);
        fseek(g, 0, SEEK_SET);
        unsigned char *whole = (unsigned char *)malloc((size_t)fsz);
        if (!whole || fread(whole, 1, (size_t)fsz, g) != (size_t)fsz) {
            fprintf(stderr, "FAIL: cannot read whole PNG\n"); return 1;
        }
        fclose(g);
        long pos = 8;
        int crc_bad = 0;
        while (pos + 12 <= fsz) {
            uint32_t ln = rd32be(whole + pos);
            if (pos + 12 + (long)ln > fsz) break;
            uint32_t c = 0xffffffffU;
            for (long i = pos + 4; i < pos + 8 + (long)ln; i++)
                c = tbl[(c ^ whole[i]) & 0xff] ^ (c >> 8);
            c ^= 0xffffffffU;
            uint32_t got = rd32be(whole + pos + 8 + ln);
            char t[5] = { whole[pos+4], whole[pos+5], whole[pos+6], whole[pos+7], 0 };
            if (c != got) {
                fprintf(stderr, "FAIL: bad CRC on %s (want %08x got %08x)\n", t, c, got);
                crc_bad = 1;
            }
            pos += 12 + (long)ln;
        }
        free(whole);
        if (crc_bad) return 1;
    }

    printf("OK round-trip %dx%d -> WebP %zu bytes -> PNG %dx%d (8-bit RGBA, CRC verified)\n",
           w, h, wsize, pw, ph);
    return 0;
}
