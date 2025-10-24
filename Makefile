#==============================================================================
# Root Makefile - STM32 Bare Metal Project
#==============================================================================

# Include common definitions
include Makefile.common

#==============================================================================
# Default target
#==============================================================================
# To build a specific example, run: make EXAMPLE=<example_name>
# For example: make EXAMPLE=blink_simple
EXAMPLE ?= blink_simple

# Default goal
.DEFAULT_GOAL := $(EXAMPLE)

#==============================================================================
# Subdirectories with makefiles
#==============================================================================
SUBDIRS := startup drivers 3rd_party examples

#==============================================================================
# All examples list (for 'all' target)
#==============================================================================
ALL_EXAMPLES := blink_simple button_interrupt button_simple button_sleep serial_echo serial_simple

#==============================================================================
# Phony targets
#==============================================================================
.PHONY: all clean $(SUBDIRS) flash debug openocd help $(EXAMPLE) $(ALL_EXAMPLES)

#==============================================================================
# Build all examples
#==============================================================================
all:
	@echo "Building all examples..."
	@for example in $(ALL_EXAMPLES); do \
		echo "========================================"; \
		echo "Building $$example..."; \
		echo "========================================"; \
		$(MAKE) EXAMPLE=$$example || exit 1; \
	done
	@echo "========================================"; \
	@echo "All examples built successfully!"

#==============================================================================
# Build specific example
#==============================================================================
$(EXAMPLE): startup drivers 3rd_party
	@echo "Building example: $(EXAMPLE)"
	$(MAKE) -C examples EXAMPLE=$(EXAMPLE)

#==============================================================================
# Build subdirectories
#==============================================================================
startup:
	$(MAKE) -C startup

drivers:
	$(MAKE) -C drivers

3rd_party:
	$(MAKE) -C 3rd_party

examples: startup drivers 3rd_party
	$(MAKE) -C examples EXAMPLE=$(EXAMPLE)

#==============================================================================
# Clean all build artifacts
#==============================================================================
clean:
	@echo "Cleaning all build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete."

#==============================================================================
# Flash target (delegates to examples)
#==============================================================================
flash: $(EXAMPLE)
	@echo "Flashing $(EXAMPLE).elf to target using OpenOCD..."
	openocd -f board/st_nucleo_f4.cfg -c "program $(BUILD_DIR)/examples/basic/$(EXAMPLE)/$(EXAMPLE).elf verify reset exit"

#==============================================================================
# Debug target
#==============================================================================
debug: $(EXAMPLE)
	./debug.sh $(BUILD_DIR)/examples/basic/$(EXAMPLE)/$(EXAMPLE).elf

#==============================================================================
# OpenOCD target
#==============================================================================
openocd:
	@echo "Starting OpenOCD on localhost port 3333..."
	openocd -f board/st_nucleo_f4.cfg

#==============================================================================
# Help target
#==============================================================================
help:
	@echo "STM32 Bare Metal Build System"
	@echo "============================="
	@echo ""
	@echo "Usage:"
	@echo "  make                    - Build default example (blink_simple)"
	@echo "  make EXAMPLE=<name>     - Build specific example"
	@echo "  make all                - Build all examples"
	@echo "  make clean              - Clean all build artifacts"
	@echo "  make flash              - Flash current example to target"
	@echo "  make debug              - Debug current example"
	@echo "  make openocd            - Start OpenOCD server"
	@echo ""
	@echo "Available examples:"
	@for example in $(ALL_EXAMPLES); do \
		echo "  - $$example"; \
	done