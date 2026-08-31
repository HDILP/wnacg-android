#ifndef WNACG_TLS_H
#define WNACG_TLS_H

#include <stddef.h>

/* Establish a TLS client connection over an already-connected TCP socket
 * fd. hostname is used for SNI and (optionally) certificate name checks.
 *
 * IMPORTANT (Android 2.3 reality):
 *   The device's bundled CA store is ancient (2010) and the system SSL
 *   stack cannot do TLS 1.2. We use mbedTLS 3.6.2 which DOES modern TLS
 *   (1.2 + 1.3), but we intentionally do NOT verify the server certificate
 *   chain (MBEDTLS_SSL_VERIFY_NONE). This is vulnerable to active MITM.
 *   For a personal downloader on your own device this is a pragmatic
 *   choice; do not use on untrusted networks.
 *
 * Returns an opaque context pointer on success, NULL on failure.
 * Use tls_write_all / tls_recv / tls_close with that pointer. */
void *tls_connect(int fd, const char *hostname);

/* Returns the last mbedTLS error code if tls_connect failed, else 0.
 * Useful for diagnostics (see mbedtls/error.h MBEDTLS_ERR_*). */
int tls_last_error(void *ctx);

int tls_write_all(void *ctx, const char *buf, size_t len);
int tls_recv(void *ctx, unsigned char *buf, size_t len);
void tls_close(void *ctx);

#endif /* WNACG_TLS_H */
