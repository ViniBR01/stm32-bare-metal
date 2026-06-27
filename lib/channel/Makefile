#==============================================================================
# Channel Library Makefile
#
# Software AWGN channel ("emulated wireless link") for the software modem
# (Plan 002 sub-track B0): deterministic PRNG, Box-Muller Gaussian noise, and
# the Eb/N0 -> sigma mapping. Shares the q15 fixed-point header in lib/dsp/inc.
# Pure C; links libm for sqrt/erfc/pow. Compiles on host and target. Mirrors
# lib/framing/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_channel

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/channel

# Shared fixed-point conventions live in lib/dsp/inc.
DSP_INC_DIR := ../dsp/inc

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libchannel.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the channel library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating channel library..."
	$(AR) rcs $@ $^
	@echo "Channel library created: $@"

# Compile sources.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling channel: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -I$(DSP_INC_DIR) -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning channel..."
	@rm -rf $(LOCAL_BUILD_DIR)
