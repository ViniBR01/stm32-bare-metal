#==============================================================================
# Image Header / Slot Metadata Library Makefile
#
# Defines on-flash image header and slot metadata structures plus a pure-C
# parser. Host-validated, target-usable. Mirrors lib/skeleton/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_img

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/img

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libimg.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the img library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating img library..."
	$(AR) rcs $@ $^
	@echo "Img library created: $@"

# Compile sources.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling img: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning img..."
	@rm -rf $(LOCAL_BUILD_DIR)
