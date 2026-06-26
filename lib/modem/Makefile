#==============================================================================
# Modem Library Makefile
#
# BPSK symbol mapper/slicer for the software modem (Plan 002 sub-track B0).
# Pure C with no peripheral dependencies; shares the q15 fixed-point header in
# lib/dsp/inc. Compiles unchanged on host (unit tests) and target. Mirrors
# lib/framing/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_modem

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/modem

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
TARGET_LIB := $(LOCAL_BUILD_DIR)/libmodem.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the modem library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating modem library..."
	$(AR) rcs $@ $^
	@echo "Modem library created: $@"

# Compile sources.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling modem: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -I$(DSP_INC_DIR) -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning modem..."
	@rm -rf $(LOCAL_BUILD_DIR)
