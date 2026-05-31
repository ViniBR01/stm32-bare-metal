#==============================================================================
# Bootloader Apps Makefile — Plan 001 Phase 1.5+
#
# Two apps live here:
#   - loader              : the bootloader itself (sector 0, 16 KB)
#   - app_blinky_signed   : trivial signed slot-A app proving the boot chain
#
# Each is a multi-file app in its own subdirectory with its own Makefile.
# We just delegate based on EXAMPLE.
#==============================================================================

MODULE_NAME := bootloader_apps

ifdef EXAMPLE
.DEFAULT_GOAL := $(EXAMPLE)
else
.DEFAULT_GOAL := help
endif

include ../../Makefile.common

APPS := bootloader app_blinky_signed

.PHONY: all clean help $(APPS)

help:
	@echo "Usage: make EXAMPLE=<app_name>  (one of: $(APPS))"

all: $(APPS)

# `EXAMPLE=bootloader` builds the loader subdir (we keep the directory name
# generic so Phase 1.6+ can add `loader_v2/` etc. without colliding with the
# flag user-facing name).
bootloader:
	$(MAKE) -C loader

app_blinky_signed:
	$(MAKE) -C app_blinky_signed

clean:
	@echo "Cleaning bootloader apps..."
	$(MAKE) -C loader clean
	$(MAKE) -C app_blinky_signed clean
