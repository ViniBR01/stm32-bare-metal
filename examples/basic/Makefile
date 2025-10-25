#==============================================================================
# Basic Examples Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := basic_examples

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
LOCAL_BUILD_DIR := $(BUILD_DIR)/examples/basic

#==============================================================================
# All examples in this directory
#==============================================================================
EXAMPLES := blink_simple button_interrupt button_simple button_sleep serial_echo serial_simple

#==============================================================================
# Dependencies for each example
#==============================================================================
# Define explicit dependencies for each example
blink_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ)
button_interrupt_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(PRINTF_LIB)
button_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ)
button_sleep_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(PRINTF_LIB)
serial_echo_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(LOG_C_LIB) $(PRINTF_LIB) $(UTILS_LIB)
serial_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(LOG_C_LIB) $(PRINTF_LIB)

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
	@echo "Example: make EXAMPLE=blink_simple"

# Build all examples
all: $(EXAMPLES)

# Pattern rule for building examples
define build-example
$(1): $(LOCAL_BUILD_DIR)/$(1)/$(1).bin
	@echo "Example $(1) built successfully!"

$(LOCAL_BUILD_DIR)/$(1)/$(1).o: $(1).c
	$$(make-build-dir)
	@echo "Compiling example: $(1).c"
	$$(CC) $$(CFLAGS) -o $$@ $$<

$(LOCAL_BUILD_DIR)/$(1)/$(1).elf: $(LOCAL_BUILD_DIR)/$(1)/$(1).o $$($(1)_DEPS)
	$$(make-build-dir)
	@echo "Linking $(1)..."
	$$(CC) $$(LDFLAGS) -Wl,-Map=$(LOCAL_BUILD_DIR)/$(1)/$(1).map,--cref -o $$@ $(LOCAL_BUILD_DIR)/$(1)/$(1).o -Wl,--start-group $$($(1)_DEPS) -Wl,--end-group
	@echo "Linking complete."

$(LOCAL_BUILD_DIR)/$(1)/$(1).bin: $(LOCAL_BUILD_DIR)/$(1)/$(1).elf
	$$(CP) -O binary $$< $$@
	@echo "Created $(1).bin"
	@$$(SZ) $$<
endef

# Generate build rules for each example
$(foreach example,$(EXAMPLES),$(eval $(call build-example,$(example))))

# Clean all build artifacts
clean:
	@echo "Cleaning basic examples..."
	@rm -rf $(LOCAL_BUILD_DIR)
