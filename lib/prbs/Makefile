#==============================================================================
# PRBS Library Makefile
#
# Maximal-length LFSR pseudo-random bit sequence generator + bit-error checker
# for the software modem (Plan 002 sub-track B0). Pure C with no peripheral
# dependencies, so the same source compiles on the host (unit tests) and on the
# target (apps/dsp/modem_sim). Mirrors lib/framing/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_prbs

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/prbs

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libprbs.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the PRBS library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating prbs library..."
	$(AR) rcs $@ $^
	@echo "PRBS library created: $@"

# Compile sources.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling prbs: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning prbs..."
	@rm -rf $(LOCAL_BUILD_DIR)
