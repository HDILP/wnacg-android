# Local build for testing on the dev machine (gcc). The Android NDK build
# is handled separately by build-android.sh / GitHub Actions.
#
# Usage:
#   make            # build ./wnacg (local test binary)
#   make test       # run parser tests against sample HTML
#   make clean
#
# NOTE: the host build does NOT link libwebp. webp_bmp.c compiles out the
# WebP->BMP decode path under #ifndef __ANDROID__, so the host binary is a
# plain pass-through for covers (used only for parser unit tests). The Android
# build (build-android.sh) links libwebp and performs the real WebP decoding.

BS_DIR ?= thirdparty/bearssl-0.6
BS_LIB := $(BS_DIR)/build/libbearssl.a
BS_INC := $(BS_DIR)/inc

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE -DDEFAULT_API_DOMAIN="www.wn09.shop"
LDFLAGS := $(BS_LIB)

SRC := src/net.c src/tls.c src/html.c src/wnacg.c src/webp_bmp.c
HDR := src/net.h src/tls.h src/html.h src/webp_bmp.h

.PHONY: all test clean

all: wnacg

$(BS_LIB):
	$(MAKE) -C $(BS_DIR) CC=$(CC)

wnacg: $(SRC) $(HDR) $(BS_LIB)
	$(CC) $(CFLAGS) -I$(BS_INC) $(SRC) -o $@ $(LDFLAGS)

# Parser unit tests against sample HTML (no network). The authoritative test
# entry is build.sh test (builds ./wnacg then compiles+runs tests/parse_test.c);
# this target is kept for convenience and uses the same driver.
test: wnacg
	./build.sh test

clean:
	rm -f wnacg
