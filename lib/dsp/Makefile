#==============================================================================
# DSP Library Makefile
#
# q15 pulse shaping for the software modem (Plan 002 sub-track B0.4): the
# root-raised-cosine FIR (TX shaping + RX matched filter). The fixed-point
# conventions in inc/fixed.h remain header-only; this library builds the
# RRC sources in src/. Links libm for the sin/cos/sqrt used in tap design.
# Pure C; compiles unchanged on host and target. Mirrors lib/channel/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_dsp

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/dsp

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libdsp.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the dsp library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating dsp library..."
	$(AR) rcs $@ $^
	@echo "DSP library created: $@"

# Compile sources.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling dsp: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning dsp..."
	@rm -rf $(LOCAL_BUILD_DIR)
