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

# Pattern rule for building apps.  Output paths carry $(PROFILE_SUFFIX) so
# slot-A / slot-B / standalone builds of the same app never clobber each
# other.  The final artifact is the .signed.bin for bootloader profiles
# (SIGN_IMAGE=1) or the raw .bin for the standalone profile (SIGN_IMAGE=0).
#
# $(1) = app name.  $(2) is precomputed by the caller as the suffixed stem
# ($(1)$(PROFILE_SUFFIX)) so the directory and file names stay in lock-step.
define build-app
ifeq ($(SIGN_IMAGE),1)
$(1): $(LOCAL_BUILD_DIR)/$(2)/$(2).signed.bin
else
$(1): $(LOCAL_BUILD_DIR)/$(2)/$(2).bin
endif
	@echo "App $(1) built successfully!"

$(LOCAL_BUILD_DIR)/$(2)/$(2).o: $(1).c
	$$(make-build-dir)
	@echo "Compiling app: $(1).c"
	$$(CC) $$(CFLAGS) -o $$@ $$<

$(LOCAL_BUILD_DIR)/$(2)/$(2).elf: $(LOCAL_BUILD_DIR)/$(2)/$(2).o $$($(1)_DEPS)
	$$(make-build-dir)
	@echo "Linking $(2)..."
	$$(CC) $$(LDFLAGS) -Wl,-Map=$(LOCAL_BUILD_DIR)/$(2)/$(2).map,--cref -o $$@ $(LOCAL_BUILD_DIR)/$(2)/$(2).o -Wl,--start-group $$($(1)_DEPS) -Wl,--end-group
	@echo "Linking complete."

$(LOCAL_BUILD_DIR)/$(2)/$(2).bin: $(LOCAL_BUILD_DIR)/$(2)/$(2).elf
	$$(CP) -O binary $$< $$@
	@echo "Created $(2).bin"
	@$$(SZ) $$<

$(LOCAL_BUILD_DIR)/$(2)/$(2).signed.bin: $(LOCAL_BUILD_DIR)/$(2)/$(2).bin $$(DEV_PRIV)
	@echo "Signing $(2).bin..."
	python3 $$(ROOT_DIR)/tools/sign_image.py \
		--priv-in $$(DEV_PRIV) \
		--in $$< \
		--out $$@ \
		--image-type app \
		--image-version $(IMAGE_VERSION)
endef

# Generate build rules for each app.  Second arg is the profile-suffixed stem.
$(foreach app,$(APPS),$(eval $(call build-app,$(app),$(app)$(PROFILE_SUFFIX))))

# Clean all build artifacts
clean:
	@echo "Cleaning basic apps..."
	@rm -rf $(LOCAL_BUILD_DIR)
