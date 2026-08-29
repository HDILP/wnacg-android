/* webp_bmp.c — WebP-aware image downloader that re-encodes WebP covers to
 * a 24-bit BMP so Android 2.3 (API < 14, no WebP decoder in BitmapFactory)
 * can show them. Non-WebP images pass through untouched.
 *
 * libwebp decode needs no zlib; the BMP writer below is uncompressed, so this
 * file pulls in zero extra dependencies beyond libwebp.
 */
#include "webp_bmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"          /* http_get / http_response / free_http_response */
#include "webp/decode.h"  /* WebPDecodeRGBA */

/* Minimal little-endian writers (BMP is little-endian on disk). */
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

/* Write RGBA (width*height*4 bytes, top-down) as a 24-bit BMP file. */
static int write_bmp(const char *path, const unsigned char *rgba,
                     int w, int h) {
    /* row stride padded to a multiple of 4 bytes */
    int stride = ((w * 3 + 3) / 4) * 4;
    long pix_size = (long)stride * h;
    long file_size = 54 + pix_size;

    unsigned char *buf = (unsigned char *)malloc(file_size);
    if (!buf) return -1;
    unsigned char *p = buf;

    /* BITMAPFILEHEADER */
    *p++ = 'B'; *p++ = 'M';
    put_u32(&p, (unsigned long)file_size);
    put_u16(&p, 0); put_u16(&p, 0);
    put_u32(&p, 54); /* pixel data offset */

    /* BITMAPINFOHEADER */
    put_u32(&p, 40);                 /* header size */
    put_u32(&p, (unsigned long)(int)w);
    put_u32(&p, (unsigned long)(int)h);
    put_u16(&p, 1);                  /* planes */
    put_u16(&p, 24);                 /* bits per pixel */
    put_u32(&p, 0);                  /* no compression */
    put_u32(&p, (unsigned long)pix_size);
    put_u32(&p, 2835); put_u32(&p, 2835); /* ~72 DPI */
    put_u32(&p, 0); put_u32(&p, 0);

    /* pixel data: BMP is bottom-up, BGR */
    for (int y = h - 1; y >= 0; y--) {
        const unsigned char *row = rgba + (long)y * w * 4;
        for (int x = 0; x < w; x++) {
            const unsigned char *px = row + x * 4;
            *p++ = px[2]; /* B */
            *p++ = px[1]; /* G */
            *p++ = px[0]; /* R */
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

static int is_webp(const unsigned char *data, long len) {
    /* RIFF....WEBP */
    return len >= 12 && data[0] == 'R' && data[1] == 'I' &&
           data[2] == 'F' && data[3] == 'F' &&
           data[8] == 'W' && data[9] == 'E' &&
           data[10] == 'B' && data[11] == 'P';
}

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

    if (is_webp((const unsigned char *)r.body, r.body_len)) {
        int w = 0, h = 0;
        uint8_t *rgba = WebPDecodeRGBA((const uint8_t *)r.body,
                                       (size_t)r.body_len, &w, &h);
        free_http_response(&r);
        if (!rgba || w <= 0 || h <= 0) {
            fprintf(stderr, "  [!] WebP decode failed: %s\n", url);
            return -1;
        }
        int rc = write_bmp(out_path, rgba, w, h);
        free(rgba);
        if (rc != 0) {
            fprintf(stderr, "  [!] BMP write failed: %s\n", out_path);
            return -1;
        }
        fprintf(stderr, "  [i] WebP -> BMP %dx%d: %s\n", w, h, out_path);
        return 0;
    }

    /* pass-through for PNG/JPEG */
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
