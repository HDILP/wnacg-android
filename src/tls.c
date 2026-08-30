#include "tls.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

/* Custom entropy from /dev/urandom for Android 2.3 & POSIX */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    ssize_t n = read(fd, output, len);
    close(fd);
    if (n <= 0) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = (size_t)n;
    return 0;
}

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    int fd;
    int last_err;
} tls_ctx;

static int g_last_err = 0;

int tls_last_error(void *ctx) {
    if (ctx) return ((tls_ctx *)ctx)->last_err;
    return g_last_err;
}

/* Custom BIO send/recv callbacks for raw socket fd */
static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    ssize_t ret = write(fd, buf, len);
    if (ret < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    return (int)ret;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    ssize_t ret = read(fd, buf, len);
    if (ret < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (ret == 0) return 0; /* EOF */
    return (int)ret;
}

void *tls_connect(int fd, const char *hostname) {
    tls_ctx *c = (tls_ctx *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = fd;

    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_ctr_drbg_init(&c->ctr_drbg);
    mbedtls_entropy_init(&c->entropy);

    const char *pers = "wnacg_client";
    int ret = mbedtls_ctr_drbg_seed(&c->ctr_drbg, mbedtls_entropy_func, &c->entropy,
                                    (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        g_last_err = c->last_err = ret;
        goto fail;
    }

    ret = mbedtls_ssl_config_defaults(&c->conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        g_last_err = c->last_err = ret;
        goto fail;
    }

    /* Skip certificate verification: Android 2.3 CA store is ancient (2010) */
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->ctr_drbg);

    ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (ret != 0) {
        g_last_err = c->last_err = ret;
        goto fail;
    }

    if (hostname && *hostname) {
        ret = mbedtls_ssl_set_hostname(&c->ssl, hostname);
        if (ret != 0) {
            g_last_err = c->last_err = ret;
            goto fail;
        }
    }

    mbedtls_ssl_set_bio(&c->ssl, (void *)(intptr_t)fd, bio_send, bio_recv, NULL);

    /* Perform TLS handshake (TLS 1.2 or TLS 1.3 negotiated automatically) */
    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            g_last_err = c->last_err = ret;
            goto fail;
        }
    }

    return c;

fail:
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_ctr_drbg_free(&c->ctr_drbg);
    mbedtls_entropy_free(&c->entropy);
    free(c);
    return NULL;
}

int tls_write_all(void *ctx, const char *buf, size_t len) {
    if (!ctx) return 0;
    tls_ctx *c = (tls_ctx *)ctx;
    size_t written = 0;
    while (written < len) {
        int ret = mbedtls_ssl_write(&c->ssl, (const unsigned char *)buf + written, len - written);
        if (ret > 0) {
            written += (size_t)ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
            continue;
        } else {
            c->last_err = ret;
            return 0;
        }
    }
    return 1;
}

int tls_recv(void *ctx, unsigned char *buf, size_t len) {
    if (!ctx) return -1;
    tls_ctx *c = (tls_ctx *)ctx;
    for (;;) {
        int ret = mbedtls_ssl_read(&c->ssl, buf, len);
        if (ret > 0) return ret;
        if (ret == 0) return 0; /* EOF */
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        c->last_err = ret;
        return -1;
    }
}

void tls_close(void *ctx) {
    if (!ctx) return;
    tls_ctx *c = (tls_ctx *)ctx;
    /* best effort notify */
    mbedtls_ssl_close_notify(&c->ssl);
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_ctr_drbg_free(&c->ctr_drbg);
    mbedtls_entropy_free(&c->entropy);
    free(c);
}
