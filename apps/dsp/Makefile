#==============================================================================
# DSP Apps Makefile
#
# Plan 002 sub-track B0 — software BPSK modem. modem_sim is a single-file
# interactive CLI app that links the modem middleware (lib/prbs, lib/modem,
# lib/channel) and the q15 header (lib/dsp). Mirrors apps/cli/Makefile.
#==============================================================================

# Module name for identification
MODULE_NAME := dsp_apps

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
# PROFILE_SUFFIX (_a slot A, _b slot B, _standalone) keeps profile/slot builds
# of the same app from clobbering each other.
#==============================================================================
LOCAL_BUILD_DIR := $(BUILD_DIR)/apps/dsp/modem_sim$(PROFILE_SUFFIX)

#==============================================================================
# All apps in this directory
#==============================================================================
APPS := modem_sim

#==============================================================================
# Source files (single-file app: all .c in this directory)
#==============================================================================
MODEM_SIM_SRCS := $(wildcard *.c)
MODEM_SIM_OBJS := $(patsubst %.c,$(LOCAL_BUILD_DIR)/%.o,$(MODEM_SIM_SRCS))

#==============================================================================
# Output artifacts (profile-suffixed)
#==============================================================================
ELF  := $(LOCAL_BUILD_DIR)/modem_sim$(PROFILE_SUFFIX).elf
BIN  := $(LOCAL_BUILD_DIR)/modem_sim$(PROFILE_SUFFIX).bin
SBIN := $(LOCAL_BUILD_DIR)/modem_sim$(PROFILE_SUFFIX).signed.bin

# Bootloader profiles sign the image; standalone stops at the raw .bin.
ifeq ($(SIGN_IMAGE),1)
  FINAL := $(SBIN)
else
  FINAL := $(BIN)
endif

#==============================================================================
# Dependencies — modem libs are linked unconditionally (the point of the app)
#==============================================================================
modem_sim_DEPS := $(STARTUP_OBJ) $(DRIVERS_LIB) $(LOG_C_LIB) $(PRINTF_LIB) \
                  $(UTILS_LIB) $(BL_HANDSHAKE_LIB) $(IMG_LIB) $(FLASH_LIB) \
                  $(PRBS_LIB) $(MODEM_LIB) $(CHANNEL_LIB)

# Header lookup for the middleware libs (and bl_handshake/img/flash used by
# the bootloader handshake in main()).
DSP_LIB_INCS := -I$(LIB_DIR)/prbs/inc -I$(LIB_DIR)/modem/inc \
                -I$(LIB_DIR)/channel/inc -I$(LIB_DIR)/dsp/inc \
                -I$(LIB_DIR)/bl_handshake/inc -I$(LIB_DIR)/img/inc \
                -I$(LIB_DIR)/flash/inc

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean help $(APPS)

help:
	@echo "Usage: make EXAMPLE=<app_name>"
	@echo "       make all                 # Build all apps"
	@echo ""
	@echo "Available apps:"
	@for app in $(APPS); do \
		echo "  - $$app"; \
	done
	@echo ""
	@echo "Example: make EXAMPLE=modem_sim"

# Image-version field stamped into the signed header.
IMAGE_VERSION ?= 1

all: $(APPS)

modem_sim: $(FINAL)
	@echo "App modem_sim built successfully! ($(FINAL))"

# Compile each .c file in this directory into an object file.
$(LOCAL_BUILD_DIR)/%.o: %.c
	$(make-build-dir)
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -I. $(DSP_LIB_INCS) -o $@ $<

# Link all object files with the dependency libraries.
$(ELF): $(MODEM_SIM_OBJS) $(modem_sim_DEPS)
	$(make-build-dir)
	@echo "Linking modem_sim..."
	$(CC) $(LDFLAGS) -Wl,-Map=$(LOCAL_BUILD_DIR)/modem_sim$(PROFILE_SUFFIX).map,--cref -o $@ $(MODEM_SIM_OBJS) -Wl,--start-group $(modem_sim_DEPS) -Wl,--end-group -lc -lm -lgcc
	@echo "Linking complete."

# Create binary from ELF.
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

clean:
	@echo "Cleaning dsp apps..."
	@rm -rf $(BUILD_DIR)/apps/dsp
