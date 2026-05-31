#==============================================================================
# Slot-aware Flash Middleware Library Makefile
#
# Wraps drivers/src/flash.c with slot-erase / metadata-commit primitives and
# a sector-range validator that refuses to touch the bootloader sector.
# Mirrors lib/img/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := lib_flash

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/flash

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libflash.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build the slot-aware flash library.
$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating flash library..."
	$(AR) rcs $@ $^
	@echo "Flash library created: $@"

# Compile sources.  -I$(LIB_DIR)/img/inc pulls img_slot_metadata_t in.
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling flash: $<"
	$(CC) $(CFLAGS) -I$(LOCAL_INC_DIR) -I$(LIB_DIR)/img/inc -o $@ $<

# Clean local build artifacts.
clean:
	@echo "Cleaning flash..."
	@rm -rf $(LOCAL_BUILD_DIR)
