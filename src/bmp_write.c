/* bmp_write.c — minimal 24-bit uncompressed BMP writer (no zlib needed).
 * Shared by the Android cover path (webp_bmp.c) and the host round-trip test.
 */
#include "bmp_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u16(unsigned char **p, unsigned v) {
    *(*p)++ = (unsigned char)(v & 0xff);
    *(*p)++ = (unsigned char)((v >> 8) & 0xff);
}
static void put_u32(unsigned char **p, unsigned long v) {
    *(*p)++ = (unsigned char)(v & 0xff);
    *(*p)++ = (unsigned char)((v >> 8) & 0xff);
    *(*p)++ = (unsigned char)((v >> 16) & 0xff);
    *(*p)++ = (unsigned char)((v >> 24) & 0xff);
}

int bmp_write_rgb(const char *path, const unsigned char *rgba, int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    int stride = ((w * 3 + 3) / 4) * 4;       /* 4-byte row padding */
    long pix_size = (long)stride * h;
    long file_size = 54 + pix_size;

    unsigned char *buf = (unsigned char *)malloc((size_t)file_size);
    if (!buf) return -1;
    unsigned char *p = buf;

    /* BITMAPFILEHEADER */
    *p++ = 'B'; *p++ = 'M';
    put_u32(&p, (unsigned long)file_size);
    put_u16(&p, 0); put_u16(&p, 0);
    put_u32(&p, 54);

    /* BITMAPINFOHEADER */
    put_u32(&p, 40);
    put_u32(&p, (unsigned long)(int)w);
    put_u32(&p, (unsigned long)(int)h);
    put_u16(&p, 1); put_u16(&p, 24);
    put_u32(&p, 0); put_u32(&p, (unsigned long)pix_size);
    put_u32(&p, 2835); put_u32(&p, 2835);
    put_u32(&p, 0); put_u32(&p, 0);

    /* pixel data: BMP is bottom-up, BGR */
    for (int y = h - 1; y >= 0; y--) {
        const unsigned char *row = rgba + (long)y * w * 4;
        for (int x = 0; x < w; x++) {
            const unsigned char *px = row + x * 4;
            *p++ = px[2]; *p++ = px[1]; *p++ = px[0];
        }
        int pad = stride - w * 3;
        while (pad-- > 0) *p++ = 0;
    }

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return -1; }
    size_t wrote = fwrite(buf, 1, (size_t)file_size, f);
    fclose(f);
    free(buf);
    return (wrote == (size_t)file_size) ? 0 : -1;
}
