#==============================================================================
# Crypto Library Makefile
#
# Wraps two vendored single-file primitives:
#   - 3rd_party/sha256/      (B-Con, public domain)
#   - 3rd_party/micro-ecc/   (kmackay, BSD 2-Clause)
#
# We compile the vendored .c files directly into libcrypto.a — both upstreams
# are tiny and only have one consumer (this library), so a separate top-level
# 3rd_party/ static lib would be ceremony for nothing.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_crypto

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR   := src
LOCAL_INC_DIR   := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/crypto

SHA256_DIR   := $(THIRD_PARTY_DIR)/sha256
MICRO_ECC_DIR := $(THIRD_PARTY_DIR)/micro-ecc

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

# Vendored objects (compiled into the same archive)
SHA256_OBJ    := $(LOCAL_BUILD_DIR)/3rd_party/sha256/sha256.o
MICRO_ECC_OBJ := $(LOCAL_BUILD_DIR)/3rd_party/micro-ecc/uECC.o

ALL_OBJS := $(LOCAL_OBJS) $(SHA256_OBJ) $(MICRO_ECC_OBJ)

#==============================================================================
# micro-ecc configuration — secp256r1 verify only, portable C path.
# uECC_PLATFORM=0 is uECC_arch_other (no inline asm).
#==============================================================================
MICRO_ECC_CONFIG := \
	-DuECC_PLATFORM=0 \
	-DuECC_SUPPORTS_secp160r1=0 \
	-DuECC_SUPPORTS_secp192r1=0 \
	-DuECC_SUPPORTS_secp224r1=0 \
	-DuECC_SUPPORTS_secp256r1=1 \
	-DuECC_SUPPORTS_secp256k1=0 \
	-DuECC_SUPPORT_COMPRESSED_POINT=0 \
	-DuECC_SQUARE_FUNC=0

# Suppress -Wunused-function for the unused default_RNG path in
# 3rd_party/micro-ecc/platform-specific.inc (only triggers on POSIX hosts;
# harmless either way).
MICRO_ECC_CFLAGS := $(MICRO_ECC_CONFIG) -Wno-unused-function

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libcrypto.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

$(TARGET_LIB): $(ALL_OBJS)
	$(make-build-dir)
	@echo "Creating crypto library..."
	$(AR) rcs $@ $^
	@echo "Crypto library created: $@"

# Wrappers in lib/crypto/src/
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling crypto: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -I$(SHA256_DIR) -I$(MICRO_ECC_DIR) -o $@ $<

# Vendored SHA-256
$(SHA256_OBJ): $(SHA256_DIR)/sha256.c $(SHA256_DIR)/sha256.h
	$(make-build-dir)
	@echo "Compiling crypto: $<"
	$(CC) $(CFLAGS) -I$(SHA256_DIR) -o $@ $<

# Vendored micro-ecc
$(MICRO_ECC_OBJ): $(MICRO_ECC_DIR)/uECC.c $(MICRO_ECC_DIR)/uECC.h $(MICRO_ECC_DIR)/uECC_vli.h $(MICRO_ECC_DIR)/types.h
	$(make-build-dir)
	@echo "Compiling crypto: $<"
	$(CC) $(CFLAGS) $(MICRO_ECC_CFLAGS) -I$(MICRO_ECC_DIR) -o $@ $<

clean:
	@echo "Cleaning crypto..."
	@rm -rf $(LOCAL_BUILD_DIR)
