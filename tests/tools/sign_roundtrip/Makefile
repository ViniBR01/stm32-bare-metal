#==============================================================================
# Cross-language round-trip test for the Phase 1.4 host signing tools.
#
# Generates a keypair + signed image with python3 tools/{keygen,sign_image}.py,
# then compiles a Unity test that parses the image with lib/img and verifies it
# with lib/crypto. Asserts: any drift between the Python signer and the C
# parser fails this suite, breaking host-tests on the next CI run.
#
# Both python invocations and the C compile fan in to test_roundtrip.out, so
# the standard `make run` flow self-bootstraps the fixtures every build.
#==============================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I../../../lib/img/inc \
          -Ibuild \
          -I../../../lib/crypto/inc \
          -I../../../3rd_party/sha256 \
          -I../../../3rd_party/micro-ecc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

# Match lib/crypto/Makefile and tests/lib/crypto/Makefile so the host build
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
IMG_SRC        = ../../../lib/img/src/img_header.c

KEYGEN     = ../../../tools/keygen.py
SIGNER     = ../../../tools/sign_image.py
FIXTURE    = fixture_payload.bin

# Deterministic seed pins the fixtures to the same keypair every build, so
# test_roundtrip.c can embed a known image_version without checking against
# random data. The seed is test-only — never used as a production key.
SEED = stm32-bare-metal/sign-roundtrip/v1

BUILD_DIR    = build
PRIV_KEY     = $(BUILD_DIR)/test_priv.pem
PUBKEY_C     = $(BUILD_DIR)/test_pubkey.c
SIGNED_IMAGE = $(BUILD_DIR)/test_image.signed.bin

.PHONY: all run clean

all: test_roundtrip.out

run: all
	./test_roundtrip.out

# Generate the keypair. keygen.py writes both files; the C source includes
# `extern const uint8_t bootloader_pubkey[64]` which test_roundtrip.c links
# against to drive crypto_ecdsa_p256_verify().
$(PRIV_KEY) $(PUBKEY_C): $(KEYGEN)
	@mkdir -p $(BUILD_DIR)
	python3 $(KEYGEN) --priv-out $(PRIV_KEY) --pub-out $(PUBKEY_C) --seed "$(SEED)"

# Sign the committed fixture payload with the generated private key.
$(SIGNED_IMAGE): $(SIGNER) $(PRIV_KEY) $(FIXTURE)
	python3 $(SIGNER) \
	    --priv-in $(PRIV_KEY) \
	    --in $(FIXTURE) \
	    --out $(SIGNED_IMAGE) \
	    --image-type app \
	    --image-version 7

test_roundtrip.out: test_roundtrip.c $(PUBKEY_C) $(SIGNED_IMAGE) \
                    $(IMG_SRC) $(CRYPTO_SRC) $(SHA256_SRC) $(MICRO_ECC_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $(MICRO_ECC_CONFIG) \
	    -DSIGNED_IMAGE_PATH=\"$(SIGNED_IMAGE)\" \
	    test_roundtrip.c $(PUBKEY_C) $(IMG_SRC) $(CRYPTO_SRC) \
	    $(SHA256_SRC) $(MICRO_ECC_SRC) $(UNITY_SRC) \
	    -o $@

clean:
	rm -f *.out *.gcda *.gcno
	rm -rf $(BUILD_DIR)
