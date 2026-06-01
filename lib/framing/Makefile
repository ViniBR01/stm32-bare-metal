#==============================================================================
# Framing Library Makefile
#
# HDLC-style frame layer with byte-stuffing, CRC-16-CCITT, and a sliding-
# window-of-1 reliable layer. Pure C with no peripheral dependencies, so the
# same source compiles on the host (unit tests, tools/ota_send.py via Python
# round-trip checks) and on the target (bootloader OTA receiver).
#
# Plan 001 Phase 1.8 introduced this lib; Plan 002 Phase 2.2 reuses it.
# Mirrors lib/img/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_framing

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/framing

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libframing.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the framing library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating framing library..."
	$(AR) rcs $@ $^
	@echo "Framing library created: $@"

# Compile sources.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling framing: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning framing..."
	@rm -rf $(LOCAL_BUILD_DIR)
