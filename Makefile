# Local build for testing on the dev machine (gcc). The Android NDK build
# is handled separately by build-android.sh / GitHub Actions.
#
# Usage:
#   make            # build ./wnacg (local test binary)
#   make test       # run parser tests against sample HTML
#   make clean
#
# NOTE: building requires libwebp, which build-webp.sh fetches + cross-compiles.
# On the CI runner that runs automatically; locally you need network access to
# GitHub. See build-android.sh for the "ci only" convention.

BS_DIR ?= thirdparty/bearssl-0.6
BS_LIB := $(BS_DIR)/build/libbearssl.a
BS_INC := $(BS_DIR)/inc

# libwebp (decode only) — built by build-webp.sh into
# thirdparty/libwebp/build-android/libwebp.a. Source is fetched on demand by
# build-webp.sh (CI only; the dev box cannot reach GitHub directly).
WEBP_INC ?= thirdparty/libwebp/src
WEBP_LIB ?= thirdparty/libwebp/build-android/libwebp.a

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE -DDEFAULT_API_DOMAIN="www.wn09.shop"
LDFLAGS := $(BS_LIB) $(WEBP_LIB)

SRC := src/net.c src/tls.c src/html.c src/wnacg.c src/webp_bmp.c
HDR := src/net.h src/tls.h src/html.h src/webp_bmp.h

.PHONY: all test clean

all: wnacg

$(BS_LIB):
	$(MAKE) -C $(BS_DIR) CC=$(CC)

$(WEBP_LIB):
	bash build-webp.sh

wnacg: $(SRC) $(HDR) $(BS_LIB) $(WEBP_LIB)
	$(CC) $(CFLAGS) -I$(BS_INC) -I$(WEBP_INC) $(SRC) -o $@ $(LDFLAGS)

# Parser unit tests against sample HTML (no network). The authoritative test
# entry is build.sh test (builds ./wnacg then compiles+runs tests/parse_test.c);
# this target is kept for convenience and uses the same driver.
test: wnacg
	./build.sh test

clean:
	rm -f wnacg
