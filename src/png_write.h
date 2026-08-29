#ifndef WNACG_PNG_WRITE_H
#define WNACG_PNG_WRITE_H

/* Write RGBA pixels (width*height*4 bytes, top-down, R,G,B,A) as a PNG file.
 * PNG is decodable on every Android version including 2.3 (unlike BMP, which
 * BitmapFactory cannot decode on API 9). Returns 0 on success, -1 on failure. */
int png_write_rgb(const char *path, const unsigned char *rgba, int w, int h);

#endif /* WNACG_PNG_WRITE_H */
