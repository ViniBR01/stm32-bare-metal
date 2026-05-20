CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I../../../lib/crypto/inc \
          -I../../../3rd_party/sha256 \
          -I../../../3rd_party/micro-ecc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

# micro-ecc configuration must match lib/crypto/Makefile so the host build
# exercises the same code paths as the ARM build.
MICRO_ECC_CONFIG = \
  -DuECC_PLATFORM=0 \
  -DuECC_SUPPORTS_secp160r1=0 \
  -DuECC_SUPPORTS_secp192r1=0 \
  -DuECC_SUPPORTS_secp224r1=0 \
  -DuECC_SUPPORTS_secp256r1=1 \
  -DuECC_SUPPORTS_secp256k1=0 \
  -DuECC_SUPPORT_COMPRESSED_POINT=0 \
  -DuECC_SQUARE_FUNC=0

UNITY_SRC      = ../../../3rd_party/unity/src/unity.c
SHA256_SRC     = ../../../3rd_party/sha256/sha256.c
MICRO_ECC_SRC  = ../../../3rd_party/micro-ecc/uECC.c
CRYPTO_SRC     = ../../../lib/crypto/src/crypto.c

.PHONY: all run clean

all: test_crypto.out

run: all
	./test_crypto.out

test_crypto.out: test_crypto.c $(CRYPTO_SRC) $(SHA256_SRC) $(MICRO_ECC_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $(MICRO_ECC_CONFIG) $^ -o $@

clean:
	rm -f *.out *.gcda *.gcno
