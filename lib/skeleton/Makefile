#==============================================================================
# Skeleton Library Makefile
#
# Minimal middleware library used to prove the lib/ build plumbing end-to-end.
# Mirrors the layout of drivers/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_skeleton

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/skeleton

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libskeleton.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the skeleton library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating skeleton library..."
	$(AR) rcs $@ $^
	@echo "Skeleton library created: $@"

# Compile sources.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling skeleton: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning skeleton..."
	@rm -rf $(LOCAL_BUILD_DIR)
