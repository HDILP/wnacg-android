# Local build for testing on the dev machine (gcc). The Android NDK build
# is handled separately by build-android.sh / GitHub Actions.
#
# Usage:
#   make            # build ./wnacg (local test binary)
#   make test       # run parser tests against sample HTML
#   make clean
#
# NOTE: the host build does NOT link libwebp. webp_bmp.c compiles out the
# WebP->PNG decode path under #ifndef __ANDROID__, so the host binary is a
# plain pass-through for covers (used only for parser unit tests). The Android
# build (build-android.sh) links libwebp and performs the real WebP decoding.
#
# TLS: mbedTLS 3.6.2 (same tree as the Android build). build-mbedtls-host.sh
# fetches the source on demand and produces the static libs below.

MBEDTLS_DIR := thirdparty/mbedtls
MBEDTLS_LIB := $(MBEDTLS_DIR)/build-host/library/libmbedtls.a
MBEDTLS_X509 := $(MBEDTLS_DIR)/build-host/library/libmbedx509.a
MBEDTLS_CRYPTO := $(MBEDTLS_DIR)/build-host/library/libmbedcrypto.a
MBEDTLS_INC := $(MBEDTLS_DIR)/include

CC ?= gcc
# Quote escaping must mirror build.sh: gcc must receive -D...="..." (the shell
# strips one layer), so the macro expands to a C string literal.
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE \
	-DDEFAULT_API_DOMAIN=\"www.wn09.shop\" \
	-DMBEDTLS_USER_CONFIG_FILE=\"mbedtls/mbedtls_user_config.h\"
LDFLAGS := $(MBEDTLS_LIB) $(MBEDTLS_X509) $(MBEDTLS_CRYPTO)

SRC := src/net.c src/tls.c src/html.c src/wnacg.c src/webp_bmp.c src/img_host.c
HDR := src/net.h src/tls.h src/html.h src/webp_bmp.h

.PHONY: all test clean

all: wnacg

$(MBEDTLS_LIB):
	bash build-mbedtls-host.sh

wnacg: $(SRC) $(HDR) $(MBEDTLS_LIB)
	$(CC) $(CFLAGS) -I$(MBEDTLS_INC) -Isrc $(SRC) -o $@ $(LDFLAGS)

# Parser unit tests against sample HTML (no network). The authoritative test
# entry is build.sh test (builds ./wnacg then compiles+runs tests/parse_test.c);
# this target is kept for convenience and uses the same driver.
test: wnacg
	./build.sh test

clean:
	rm -f wnacg
