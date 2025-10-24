#==============================================================================
# Startup Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := startup

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_BUILD_DIR := $(BUILD_DIR)/startup

#==============================================================================
# Source files
#==============================================================================
LOCAL_SRCS := stm32f411_startup.c
LOCAL_OBJS := $(patsubst %.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

#==============================================================================
# Target object
#==============================================================================
TARGET_OBJ := $(STARTUP_OBJ)

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_OBJ)

# Build the startup object
$(TARGET_OBJ): $(LOCAL_SRCS)
	$(make-build-dir)
	@echo "Compiling startup code: $<"
	$(CC) $(CFLAGS) -o $@ $<
	@echo "Startup object created: $@"

# Clean local build artifacts
clean:
	@echo "Cleaning startup..."
	@rm -rf $(LOCAL_BUILD_DIR)
