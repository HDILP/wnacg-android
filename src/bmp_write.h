#ifndef WNACG_BMP_WRITE_H
#define WNACG_BMP_WRITE_H

/* Write RGBA pixels (width*height*4 bytes, top-down, R,G,B,A) as a 24-bit
 * uncompressed BMP file (no zlib needed). Returns 0 on success, -1 on failure.
 * BMP stores rows bottom-up in BGR order, with each row padded to 4 bytes. */
int bmp_write_rgb(const char *path, const unsigned char *rgba, int w, int h);

#endif /* WNACG_BMP_WRITE_H */
