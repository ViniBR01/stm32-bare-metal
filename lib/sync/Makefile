#==============================================================================
# Sync Library Makefile
#
# RX synchronisation loops for the software modem (Plan 002 sub-track B0.5):
# AGC, Mueller-and-Muller symbol-timing recovery, decision-directed Costas
# phase recovery, and Barker-13 preamble frame sync. Operates on the complex
# cq15 sample path; reuses the q15/complex/NCO primitives in lib/dsp and the
# BPSK slicer in lib/modem. Loop state/gains are float (scalar control math);
# only the signal is q15. Pure C; compiles on host and target. Links libm.
# Mirrors lib/dsp/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_sync

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/sync

# Complex/NCO primitives live in lib/dsp/inc; BPSK slicer in lib/modem/inc.
DSP_INC_DIR   := ../dsp/inc
MODEM_INC_DIR := ../modem/inc

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libsync.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the sync library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating sync library..."
	$(AR) rcs $@ $^
	@echo "Sync library created: $@"

# Compile sources.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling sync: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -I$(DSP_INC_DIR) -I$(MODEM_INC_DIR) -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning sync..."
	@rm -rf $(LOCAL_BUILD_DIR)
