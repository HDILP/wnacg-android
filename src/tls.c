#include "tls.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "bearssl.h"
#include "bearssl_x509.h"

/* forward declarations for low-level fd I/O used by sslio */
static int fd_read(void *ctx, unsigned char *buf, size_t len);
static int fd_write(void *ctx, const unsigned char *buf, size_t len);

/* ----- custom x509 validator that decodes the leaf cert to extract the
 * public key (needed for key exchange) but accepts any chain. ----- */

typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context dec;
    br_x509_pkey pkey;       /* leaf public key, valid after end_chain */
    unsigned char qbuf[128]; /* deep copy of EC point to avoid dangling ref */
    int have_pkey;
    int got_leaf;            /* only capture the first (leaf) cert's key */
} noverify_x509;

static void nv_start_chain(const br_x509_class **ctx, const char *server_name) {
    (void)server_name;
    noverify_x509 *x = (noverify_x509 *)ctx;
    x->have_pkey = 0;
    x->got_leaf = 0;
    /* decoder needs a destination buffer for raw cert; we feed via push */
}

static void nv_start_cert(const br_x509_class **ctx, uint32_t length) {
    noverify_x509 *x = (noverify_x509 *)ctx;
    /* (re)initialise decoder; length ignored, decoder tracks internally */
    (void)length;
    br_x509_decoder_init(&x->dec, NULL, NULL);
}

static void nv_append(const br_x509_class **ctx, const unsigned char *buf, size_t len) {
    noverify_x509 *x = (noverify_x509 *)ctx;
    br_x509_decoder_push(&x->dec, buf, len);
}

static void nv_end_cert(const br_x509_class **ctx) {
    noverify_x509 *x = (noverify_x509 *)ctx;
    if (x->got_leaf) return;   /* already captured the leaf key */
    const br_x509_pkey *pk = br_x509_decoder_get_pkey(&x->dec);
    if (pk) {
        /* deep-copy the EC point so the pointer stays valid after dec is reused */
        if (pk->key_type == BR_KEYTYPE_EC && pk->key.ec.qlen <= sizeof(x->qbuf)) {
            memcpy(x->qbuf, pk->key.ec.q, pk->key.ec.qlen);
            x->pkey = *pk;
            x->pkey.key.ec.q = x->qbuf;
        } else {
            x->pkey = *pk;
        }
        x->have_pkey = 1;
        x->got_leaf = 1;
    }
}

static unsigned nv_end_chain(const br_x509_class **ctx) {
    (void)ctx;
    /* Accept unconditionally. */
    return 0;
}

static const br_x509_pkey *nv_get_pkey(const br_x509_class *const *ctx, unsigned *usages) {
    noverify_x509 *x = (noverify_x509 *)ctx;
    if (usages) *usages = BR_KEYTYPE_SIGN | BR_KEYTYPE_KEYX;
    return x->have_pkey ? &x->pkey : NULL;
}

static const br_x509_class nv_x509_vtable = {
    sizeof(noverify_x509),
    nv_start_chain,
    nv_start_cert,
    nv_append,
    nv_end_cert,
    nv_end_chain,
    nv_get_pkey,
};

/* ----- TLS context wrapper ----- */

typedef struct {
    br_ssl_client_context sc;
    br_x509_minimal_context mc;
    br_sslio_context io;
    noverify_x509 xc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    int fd;
    int last_err;
} tls_ctx;

/* tls_connect() frees its context on failure, so the error code must survive
 * the free. Keep the most recent handshake error in a static as well. */
static int g_last_err = 0;

int tls_last_error(void *ctx) {
    if (ctx) return ((tls_ctx *)ctx)->last_err;
    return g_last_err;
}

void *tls_connect(int fd, const char *hostname) {
    tls_ctx *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = fd;

    /* Initialise the full crypto profile (all default implementations and a
     * complete cipher-suite list), then override the X.509 verifier with our
     * no-verify class. init_full needs a trust-anchor set; we pass an empty
     * one because we don't use the built-in chain verification anyway. */
    br_x509_trust_anchor ta_dummy = {0};
    br_ssl_client_init_full(&c->sc, &c->mc, &ta_dummy, 0);
    br_ssl_engine_context *eng = (br_ssl_engine_context *)&c->sc;

    /* plug our no-verify x509 */
    c->xc.vtable = &nv_x509_vtable;
    br_ssl_engine_set_x509(eng, &c->xc.vtable);

    br_ssl_engine_set_buffer(eng, c->iobuf, sizeof(c->iobuf), 1);
    int r0 = br_ssl_client_reset(&c->sc, hostname, 0);
    if (r0 == 0) {
        free(c);
        return NULL;
    }

    br_sslio_init(&c->io, (br_ssl_engine_context *)&c->sc,
                  &fd_read, (void *)(intptr_t)fd,
                  &fd_write, (void *)(intptr_t)fd);

    /* perform handshake */
    if (br_sslio_flush(&c->io) != 0) {
        c->last_err = (int)br_ssl_engine_last_error((br_ssl_engine_context *)&c->sc);
        g_last_err = c->last_err;
        free(c);
        return NULL;
    }
    return c;
}

/* low-level fd I/O for BearSSL sslio */
static int fd_read(void *ctx, unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    ssize_t n = read(fd, buf, len);
    if (n < 0) {
        if (errno == EINTR) return -2; /* retry */
        return -1;
    }
    if (n == 0) return -1; /* EOF */
    return (int)n;
}

static int fd_write(void *ctx, const unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return (int)len;
}

int tls_write_all(void *ctx, const char *buf, size_t len) {
    tls_ctx *c = ctx;
    size_t off = 0;
    while (off < len) {
        int n = br_sslio_write(&c->io, buf + off, len - off);
        if (n < 0) return 0;
        off += (size_t)n;
    }
    if (br_sslio_flush(&c->io) != 0) {
        return 0;
    }
    return 1;
}

int tls_recv(void *ctx, unsigned char *buf, size_t len) {
    tls_ctx *c = ctx;
    br_ssl_engine_context *eng = (br_ssl_engine_context *)&c->sc;
    for (;;) {
        unsigned st = br_ssl_engine_current_state(eng);
        if (st & BR_SSL_RECVAPP) {
            unsigned char *ab; size_t al;
            ab = br_ssl_engine_recvapp_buf(eng, &al);
            if (al > 0) {
                size_t take = al < len ? al : len;
                memcpy(buf, ab, take);
                br_ssl_engine_recvapp_ack(eng, take);
                return (int)take;
            }
        }
        if (st & BR_SSL_CLOSED) return 0;
        if (st & BR_SSL_SENDREC) {
            unsigned char *sb; size_t sl;
            sb = br_ssl_engine_sendrec_buf(eng, &sl);
            int n = fd_write((void *)(intptr_t)c->fd, sb, sl);
            if (n < 0) return -1;
            br_ssl_engine_sendrec_ack(eng, (size_t)n);
            continue;
        }
        if (st & BR_SSL_RECVREC) {
            unsigned char *rb; size_t rl;
            rb = br_ssl_engine_recvrec_buf(eng, &rl);
            int n = fd_read((void *)(intptr_t)c->fd, rb, rl);
            if (n < 0) return -1;
            if (n == 0) return -1;
            br_ssl_engine_recvrec_ack(eng, (size_t)n);
            continue;
        }
        return -1;
    }
}

void tls_close(void *ctx) {
    tls_ctx *c = ctx;
    if (!c) return;
    br_sslio_close(&c->io);
    free(c);
}
