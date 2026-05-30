#==============================================================================
# Basic Apps Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := basic_apps

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
LOCAL_BUILD_DIR := $(BUILD_DIR)/apps/basic

#==============================================================================
# All apps in this directory
#==============================================================================
APPS := blink_pwm \
		blink_simple \
		button_interrupt \
		button_simple \
		button_sleep \
		crc_basic \
		iwdg_basic \
		serial_echo \
		serial_simple \
		shift_register_simple \
		timer_interrupt

#==============================================================================
# Dependencies for each app
#==============================================================================
# Define explicit dependencies for each app
blink_pwm_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ)
blink_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ)
button_interrupt_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(PRINTF_LIB)
button_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ)
button_sleep_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(PRINTF_LIB)
crc_basic_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ)
iwdg_basic_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ)
serial_echo_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(LOG_C_LIB) $(PRINTF_LIB) $(UTILS_LIB)
serial_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(LOG_C_LIB) $(PRINTF_LIB)
shift_register_simple_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ) $(LOG_C_LIB) $(PRINTF_LIB)
timer_interrupt_DEPS := $(DRIVERS_LIB) $(STARTUP_OBJ)

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
	@echo "Example: make EXAMPLE=blink_simple"

# Build all apps
all: $(APPS)

# Default image-version baked into every signed app header.  Bumped manually
# as apps change; Phase 1.9 will tie this into the anti-rollback counter.
IMAGE_VERSION ?= 1

# Pattern rule for building apps.  Final target is the .signed.bin produced
# by tools/sign_image.py — that is what flashes at slot A and what the
# bootloader parses.  The unsigned .bin is kept as a debug aid.
define build-app
$(1): $(LOCAL_BUILD_DIR)/$(1)/$(1).signed.bin
	@echo "App $(1) built successfully!"

$(LOCAL_BUILD_DIR)/$(1)/$(1).o: $(1).c
	$$(make-build-dir)
	@echo "Compiling app: $(1).c"
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

$(LOCAL_BUILD_DIR)/$(1)/$(1).signed.bin: $(LOCAL_BUILD_DIR)/$(1)/$(1).bin $$(DEV_PRIV)
	@echo "Signing $(1).bin..."
	python3 $$(ROOT_DIR)/tools/sign_image.py \
		--priv-in $$(DEV_PRIV) \
		--in $$< \
		--out $$@ \
		--image-type app \
		--image-version $(IMAGE_VERSION)
endef

# Generate build rules for each app
$(foreach app,$(APPS),$(eval $(call build-app,$(app))))

# Clean all build artifacts
clean:
	@echo "Cleaning basic apps..."
	@rm -rf $(LOCAL_BUILD_DIR)
