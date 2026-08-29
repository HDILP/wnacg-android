#ifndef WNACG_WEBP_BMP_H
#define WNACG_WEBP_BMP_H

/* Download `url` into `out_path`.
 *
 * - If the payload is WebP, decode it with libwebp and re-serialize as a
 *   24-bit BMP (no compression, no zlib needed) so old Android (API < 14,
 *   whose BitmapFactory has no WebP decoder) can still display the cover.
 * - Otherwise write the bytes through unchanged (PNG/JPEG as-is).
 *
 * Returns 0 on success, non-zero on failure. The caller is responsible for
 * the network fetch; this only decides the on-disk encoding.
 */
int save_image_auto(const char *url, const char *out_path);

#endif /* WNACG_WEBP_BMP_H */
