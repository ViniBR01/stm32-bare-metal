#==============================================================================
# Root Makefile - STM32 Bare Metal Project
#==============================================================================

# Include common definitions
include Makefile.common

#==============================================================================
# Default target
#==============================================================================
# To build a specific app, run: make EXAMPLE=<app_name>
# For example: make EXAMPLE=blink_simple
EXAMPLE ?= cli_simple

# Default goal
.DEFAULT_GOAL := $(EXAMPLE)

#==============================================================================
# Subdirectories with makefiles
#==============================================================================
SUBDIRS := startup utils drivers 3rd_party lib apps

#==============================================================================
# All apps list (for 'all' target)
#==============================================================================
ALL_APPS := \
	blink_simple \
	button_interrupt \
	button_simple \
	button_sleep \
	crc_basic \
	iwdg_basic \
	serial_simple \
	cli_simple

#==============================================================================
# Phony targets
#==============================================================================
.PHONY: all clean test $(SUBDIRS) flash debug openocd serial help $(EXAMPLE) $(ALL_APPS)

#==============================================================================
# Build all apps
#==============================================================================
all:
	@echo "Building all apps..."
	@for app in $(ALL_APPS); do \
		echo "========================================"; \
		echo "Building $$app..."; \
		echo "========================================"; \
		$(MAKE) EXAMPLE=$$app || exit 1; \
	done
	@echo "========================================";
	@echo "All apps built successfully!"

#==============================================================================
# Build specific app
#==============================================================================
$(EXAMPLE): startup utils drivers 3rd_party lib
	@echo "Building app: $(EXAMPLE)"
	$(MAKE) -C apps EXAMPLE=$(EXAMPLE)

# Individual app targets
$(ALL_APPS): startup utils drivers 3rd_party lib
	@echo "Building app: $@"
	$(MAKE) -C apps EXAMPLE=$@

#==============================================================================
# Build subdirectories
#==============================================================================
startup:
	$(MAKE) -C startup

drivers:
	$(MAKE) -C drivers

3rd_party:
	$(MAKE) -C 3rd_party all

utils: 3rd_party
	$(MAKE) -C utils

lib: 3rd_party drivers
	$(MAKE) -C lib

apps: startup utils drivers 3rd_party lib
	$(MAKE) -C apps EXAMPLE=$(EXAMPLE)

#==============================================================================
# Host unit tests (no cross-compiler required)
#==============================================================================
test:
	$(MAKE) -C tests run

#==============================================================================
# Clean all build artifacts
#==============================================================================
clean:
	@echo "Cleaning all build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete."

#==============================================================================
# Flash target (delegates to apps)
#==============================================================================
flash: $(EXAMPLE)
	@echo "Flashing $(EXAMPLE).elf to target using OpenOCD..."
	openocd -f board/st_nucleo_f4.cfg -c "program $(shell find $(BUILD_DIR)/apps -name $(EXAMPLE).elf) verify reset exit"
#==============================================================================
# Debug target
#==============================================================================
debug: $(EXAMPLE)
	./debug.sh $(shell find $(BUILD_DIR)/apps -name $(EXAMPLE).elf)

#==============================================================================
# OpenOCD target
#==============================================================================
openocd:
	@echo "Starting OpenOCD on localhost port 3333..."
	openocd -f board/st_nucleo_f4.cfg

#==============================================================================
# Serial connection target
#==============================================================================
BAUD_RATE ?= 115200

serial:
	@echo "Connecting to serial port at $(BAUD_RATE) baud..."
	@SERIAL_PORT=$$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1); \
	if [ -z "$$SERIAL_PORT" ]; then \
		echo "Error: No serial device found. Is the board connected?"; \
		exit 1; \
	fi; \
	echo "Found device: $$SERIAL_PORT"; \
	picocom -b $(BAUD_RATE) $$SERIAL_PORT

#==============================================================================
# Help target
#==============================================================================
help:
	@echo "STM32 Bare Metal Build System"
	@echo "============================="
	@echo ""
	@echo "Usage:"
	@echo "  make                    - Build default app (cli_simple)"
	@echo "  make EXAMPLE=<name>     - Build specific app"
	@echo "  make all                - Build all apps"
	@echo "  make clean              - Clean all build artifacts"
	@echo "  make test               - Run host unit tests (no board needed)"
	@echo "  make flash              - Flash current app to target"
	@echo "  make debug              - Debug current app"
	@echo "  make openocd            - Start OpenOCD server"
	@echo "  make serial             - Connect to serial port (115200 baud)"
	@echo "  make serial BAUD_RATE=9600 - Connect with custom baud rate"
	@echo ""
	@echo "Available apps:"
	@for app in $(ALL_APPS); do \
		echo "  - $$app"; \
	done
