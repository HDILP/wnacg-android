# Local build for testing on the dev machine (gcc). The Android NDK build
# is handled separately by build-android.sh / GitHub Actions.
#
# Usage:
#   make            # build ./wnacg (local test binary)
#   make test       # run parser tests against sample HTML
#   make clean

BS_DIR ?= thirdparty/bearssl-0.6
BS_LIB := $(BS_DIR)/build/libbearssl.a
BS_INC := $(BS_DIR)/inc

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE -DDEFAULT_API_DOMAIN='"www.wn09.shop"'
LDFLAGS := $(BS_LIB)

SRC := src/net.c src/tls.c src/html.c src/wnacg.c
HDR := src/net.h src/tls.h src/html.h

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
