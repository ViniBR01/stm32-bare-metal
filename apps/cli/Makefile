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
#
# PROFILE_SUFFIX (_a for slot A, _b for slot B, _standalone for the
# no-bootloader map) is threaded through every output path so different
# profile/slot builds of the same app never clobber each other.
#==============================================================================
LOCAL_BUILD_DIR := $(BUILD_DIR)/apps/cli/cli_simple$(PROFILE_SUFFIX)

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

CLI_OBJS := $(patsubst %.c,$(LOCAL_BUILD_DIR)/%.o,$(CLI_SRCS))

#==============================================================================
# Output artifacts (profile-suffixed)
#==============================================================================
ELF  := $(LOCAL_BUILD_DIR)/cli_simple$(PROFILE_SUFFIX).elf
BIN  := $(LOCAL_BUILD_DIR)/cli_simple$(PROFILE_SUFFIX).bin
SBIN := $(LOCAL_BUILD_DIR)/cli_simple$(PROFILE_SUFFIX).signed.bin

# Final artifact depends on the profile: bootloader profiles sign the image,
# standalone profiles stop at the raw .bin.
ifeq ($(SIGN_IMAGE),1)
  FINAL := $(SBIN)
else
  FINAL := $(BIN)
endif

#==============================================================================
# Dependencies for each app
#==============================================================================
# Define explicit dependencies for each app
cli_simple_DEPS := $(BL_HANDSHAKE_LIB) $(IMG_LIB) $(FLASH_LIB) $(DRIVERS_LIB) $(STARTUP_OBJ) $(LOG_C_LIB) $(PRINTF_LIB) $(UTILS_LIB)

# Phase 1.9: bl_handshake header lookup needs the lib/ tree on the
# include path.  Picked up by the per-source compile rule below.
CLI_LIB_INCS := -I$(LIB_DIR)/bl_handshake/inc -I$(LIB_DIR)/img/inc -I$(LIB_DIR)/flash/inc

# Add Unity library when HIL_TEST is enabled.  The Tier 9 software-modem BER
# test (test_harness.c) links the Plan 002 B0 modem libs; they are pulled in
# only under HIL_TEST so the production cli_simple image stays free of the
# libm-heavy float path.
ifeq ($(HIL_TEST),1)
  cli_simple_DEPS += $(UNITY_ARM_LIB) $(PRBS_LIB) $(MODEM_LIB) $(CHANNEL_LIB) $(DSP_LIB) $(SYNC_LIB)
  CLI_LIB_INCS += -I$(LIB_DIR)/prbs/inc -I$(LIB_DIR)/modem/inc -I$(LIB_DIR)/channel/inc -I$(LIB_DIR)/dsp/inc -I$(LIB_DIR)/sync/inc
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

#==============================================================================
# Image-version field stamped into the signed header.
#==============================================================================
IMAGE_VERSION ?= 1

# Build all apps
all: $(APPS)

# cli_simple builds to its profile-appropriate final artifact (signed bin for
# bootloader profiles, raw bin for standalone).
cli_simple: $(FINAL)
	@echo "App cli_simple built successfully! ($(FINAL))"

# Compile each .c file in this directory into an object file
$(LOCAL_BUILD_DIR)/%.o: %.c
	$(make-build-dir)
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -I. $(CLI_LIB_INCS) -o $@ $<

# Link all object files together
$(ELF): $(CLI_OBJS) $(cli_simple_DEPS)
	$(make-build-dir)
	@echo "Linking cli_simple..."
	$(CC) $(LDFLAGS) -Wl,-Map=$(LOCAL_BUILD_DIR)/cli_simple$(PROFILE_SUFFIX).map,--cref -o $@ $(CLI_OBJS) -Wl,--start-group $(cli_simple_DEPS) -Wl,--end-group -lc -lm -lgcc
	@echo "Linking complete."

# Create binary from ELF
$(BIN): $(ELF)
	$(CP) -O binary $< $@
	@echo "Created $(notdir $(BIN))"
	@$(SZ) $<

# Sign the bin so the bootloader will accept it (bootloader profiles only).
$(SBIN): $(BIN) $(DEV_PRIV)
	@echo "Signing $(notdir $(BIN))..."
	python3 $(ROOT_DIR)/tools/sign_image.py \
		--priv-in $(DEV_PRIV) \
		--in $< \
		--out $@ \
		--image-type app \
		--image-version $(IMAGE_VERSION)

# Clean all build artifacts
clean:
	@echo "Cleaning cli apps..."
	@rm -rf $(BUILD_DIR)/apps/cli
