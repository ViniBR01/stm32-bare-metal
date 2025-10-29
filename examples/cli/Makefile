#==============================================================================
# Cli Examples Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := cli_examples

# Default target - build specific example if EXAMPLE is set, otherwise show help
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
LOCAL_BUILD_DIR := $(BUILD_DIR)/examples/cli

#==============================================================================
# All examples in this directory
#==============================================================================
EXAMPLES := cli_simple

#==============================================================================
# Source files for multi-file examples
#==============================================================================
# cli_simple is a multi-file example with all .c files in this directory
CLI_SRCS := $(wildcard *.c)
CLI_OBJS := $(patsubst %.c,$(LOCAL_BUILD_DIR)/cli_simple/%.o,$(CLI_SRCS))

#==============================================================================
# Dependencies for each example
#==============================================================================
# Define explicit dependencies for each example
cli_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(LOG_C_LIB) $(PRINTF_LIB) $(UTILS_LIB)

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean help $(EXAMPLES)

# Help target - show available examples
help:
	@echo "Usage: make EXAMPLE=<example_name>"
	@echo "       make all                 # Build all examples"
	@echo ""
	@echo "Available examples:"
	@for example in $(EXAMPLES); do \
		echo "  - $$example"; \
	done
	@echo ""
	@echo "Example: make EXAMPLE=cli_simple"

# Build all examples
all: $(EXAMPLES)

# Pattern rule for building the cli_simple example (multi-file)
cli_simple: $(LOCAL_BUILD_DIR)/cli_simple/cli_simple.bin
	@echo "Example cli_simple built successfully!"

# Compile each .c file in this directory into an object file
$(LOCAL_BUILD_DIR)/cli_simple/%.o: %.c
	$(make-build-dir)
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -I. -o $@ $<

# Link all object files together
$(LOCAL_BUILD_DIR)/cli_simple/cli_simple.elf: $(CLI_OBJS) $(cli_simple_DEPS)
	$(make-build-dir)
	@echo "Linking cli_simple..."
	$(CC) $(LDFLAGS) -Wl,-Map=$(LOCAL_BUILD_DIR)/cli_simple/cli_simple.map,--cref -o $@ $(CLI_OBJS) -Wl,--start-group $(cli_simple_DEPS) -Wl,--end-group
	@echo "Linking complete."

# Create binary from ELF
$(LOCAL_BUILD_DIR)/cli_simple/cli_simple.bin: $(LOCAL_BUILD_DIR)/cli_simple/cli_simple.elf
	$(CP) -O binary $< $@
	@echo "Created cli_simple.bin"
	@$(SZ) $<

# Clean all build artifacts
clean:
	@echo "Cleaning cli examples..."
	@rm -rf $(LOCAL_BUILD_DIR)
