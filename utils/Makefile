#==============================================================================
# Utils Library Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := utils

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_SRC_DIR := src
LOCAL_INC_DIR := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/utils

#==============================================================================
# Source files
#==============================================================================
SOURCES := $(wildcard $(LOCAL_SRC_DIR)/*.c)
OBJECTS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(SOURCES))

#==============================================================================
# Target library
#==============================================================================
TARGET_LIB := $(LOCAL_BUILD_DIR)/libutils.a

#==============================================================================
# Additional includes
#==============================================================================
LOCAL_INCLUDES := -I$(LOCAL_INC_DIR)
override CFLAGS += $(LOCAL_INCLUDES)

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGET_LIB)

# Build object files
$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -o $@ $<

# Build static library
$(TARGET_LIB): $(OBJECTS)
	$(make-build-dir)
	@echo "Creating utils library..."
	$(AR) rcs $@ $^
	@echo "Utils library created: $@"

clean:
	@echo "Cleaning utils..."
	@rm -rf $(LOCAL_BUILD_DIR)
