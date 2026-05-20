#==============================================================================
# CLI App Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := cli_apps

# Default target - build specific app if EXAMPLE is set, otherwise show help
ifdef EXAMPLE
.DEFAULT_GOAL := $(EXAMPLE)
else
.DEFAULT_GOAL := help
endif

# Include common definitions
include ../../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_BUILD_DIR := $(BUILD_DIR)/apps/cli

#==============================================================================
# All apps in this directory
#==============================================================================
APPS := cli_simple

#==============================================================================
# Source files for multi-file apps
#==============================================================================
# cli_simple is a multi-file app with all .c files in this directory
# Exclude test_harness.c unless HIL_TEST=1
ifeq ($(HIL_TEST),1)
  CLI_SRCS := $(wildcard *.c)
else
  CLI_SRCS := $(filter-out test_harness.c, $(wildcard *.c))
endif

CLI_OBJS := $(patsubst %.c,$(LOCAL_BUILD_DIR)/cli_simple/%.o,$(CLI_SRCS))

#==============================================================================
# Dependencies for each app
#==============================================================================
# Define explicit dependencies for each app
cli_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(LOG_C_LIB) $(PRINTF_LIB) $(UTILS_LIB)

# Add Unity library when HIL_TEST is enabled
ifeq ($(HIL_TEST),1)
  cli_simple_DEPS += $(UNITY_ARM_LIB)
endif

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean help $(APPS)

# Help target - show available apps
help:
	@echo "Usage: make EXAMPLE=<app_name>"
	@echo "       make all                 # Build all apps"
	@echo ""
	@echo "Available apps:"
	@for app in $(APPS); do \
		echo "  - $$app"; \
	done
	@echo ""
	@echo "Example: make EXAMPLE=cli_simple"

# Build all apps
all: $(APPS)

# Pattern rule for building the cli_simple app (multi-file)
cli_simple: $(LOCAL_BUILD_DIR)/cli_simple/cli_simple.bin
	@echo "App cli_simple built successfully!"

# Compile each .c file in this directory into an object file
$(LOCAL_BUILD_DIR)/cli_simple/%.o: %.c
	$(make-build-dir)
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -I. -o $@ $<

# Link all object files together
$(LOCAL_BUILD_DIR)/cli_simple/cli_simple.elf: $(CLI_OBJS) $(cli_simple_DEPS)
	$(make-build-dir)
	@echo "Linking cli_simple..."
	$(CC) $(LDFLAGS) -Wl,-Map=$(LOCAL_BUILD_DIR)/cli_simple/cli_simple.map,--cref -o $@ $(CLI_OBJS) -Wl,--start-group $(cli_simple_DEPS) -Wl,--end-group -lc -lm -lgcc
	@echo "Linking complete."

# Create binary from ELF
$(LOCAL_BUILD_DIR)/cli_simple/cli_simple.bin: $(LOCAL_BUILD_DIR)/cli_simple/cli_simple.elf
	$(CP) -O binary $< $@
	@echo "Created cli_simple.bin"
	@$(SZ) $<

# Clean all build artifacts
clean:
	@echo "Cleaning cli apps..."
	@rm -rf $(LOCAL_BUILD_DIR)
