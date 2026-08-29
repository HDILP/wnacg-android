/* png_write.c — minimal RGBA->PNG writer (no external image libs, uses zlib).
 *
 * PNG is decodable on every Android version including 2.3, so covers re-encoded
 * from WebP use PNG (BMP would not decode on API 9). We emit a true-color 8-bit
 * RGBA PNG: a single IDAT with zlib-compressed filtered scanlines.
 */
#include "png_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t crc_table[256];
static int crc_table_done = 0;

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xedb88320U ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
    crc_table_done = 1;
}

static uint32_t crc32_buf(const unsigned char *buf, size_t len) {
    uint32_t c = 0xffffffffU;
    for (size_t i = 0; i < len; i++)
        c = crc_table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffU;
}

/* chunk = length(4 BE) + type(4) + data + crc(4) over type+data */
static int write_chunk(FILE *f, const char *type,
                       const unsigned char *data, uint32_t len) {
    unsigned char hdr[8];
    hdr[0] = (unsigned char)((len >> 24) & 0xff);
    hdr[1] = (unsigned char)((len >> 16) & 0xff);
    hdr[2] = (unsigned char)((len >> 8) & 0xff);
    hdr[3] = (unsigned char)(len & 0xff);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return -1;
    if (len && fwrite(data, 1, len, f) != len) return -1;
    uint32_t crc = crc32_buf((const unsigned char *)type, 4);
    /* continue crc over data */
    for (uint32_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    crc ^= 0xffffffffU;
    unsigned char crcb[4] = {
        (unsigned char)((crc >> 24) & 0xff),
        (unsigned char)((crc >> 16) & 0xff),
        (unsigned char)((crc >> 8) & 0xff),
        (unsigned char)(crc & 0xff),
    };
    if (fwrite(crcb, 1, 4, f) != 4) return -1;
    return 0;
}

int png_write_rgb(const char *path, const unsigned char *rgba, int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    if (!crc_table_done) crc_init();

    /* build raw filtered scanlines: each row prefixed with filter byte 0 */
    long raw = (long)w * 4 + 1;          /* 1 filter byte + w*4 bytes */
    unsigned char *buf = (unsigned char *)malloc(raw * h);
    if (!buf) return -1;
    for (int y = 0; y < h; y++) {
        const unsigned char *src = rgba + (long)y * w * 4;
        unsigned char *dst = buf + (long)y * raw;
        *dst++ = 0;                       /* filter type 0 (none) */
        for (int x = 0; x < w; x++) {
            *dst++ = src[0];             /* R */
            *dst++ = src[1];             /* G */
            *dst++ = src[2];             /* B */
            *dst++ = src[3];             /* A */
        }
    }

    uLongf comp_cap = compressBound((uLong)(raw * h));
    unsigned char *comp = (unsigned char *)malloc(comp_cap);
    if (!comp) { free(buf); return -1; }
    uLongf comp_len = comp_cap;
    int zr = compress2(comp, &comp_len, buf, (uLong)(raw * h), Z_BEST_COMPRESSION);
    free(buf);
    if (zr != Z_OK) { free(comp); return -1; }

    FILE *f = fopen(path, "wb");
    if (!f) { free(comp); return -1; }

    /* signature */
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    int ok = (fwrite(sig, 1, 8, f) == 8) ? 0 : -1;

    /* IHDR */
    unsigned char ihdr[13];
    ihdr[0] = (unsigned char)((w >> 24) & 0xff);
    ihdr[1] = (unsigned char)((w >> 16) & 0xff);
    ihdr[2] = (unsigned char)((w >> 8) & 0xff);
    ihdr[3] = (unsigned char)(w & 0xff);
    ihdr[4] = (unsigned char)((h >> 24) & 0xff);
    ihdr[5] = (unsigned char)((h >> 16) & 0xff);
    ihdr[6] = (unsigned char)((h >> 8) & 0xff);
    ihdr[7] = (unsigned char)(h & 0xff);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 6;    /* color type: RGBA */
    ihdr[10] = 0;   /* compression */
    ihdr[11] = 0;   /* filter */
    ihdr[12] = 0;   /* interlace */
    if (ok == 0) ok = write_chunk(f, "IHDR", ihdr, 13);
    if (ok == 0) ok = write_chunk(f, "IDAT", comp, (uint32_t)comp_len);
    if (ok == 0) ok = write_chunk(f, "IEND", NULL, 0);

    free(comp);
    fclose(f);
    return ok;
}
