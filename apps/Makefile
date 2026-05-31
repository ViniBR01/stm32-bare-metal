#==============================================================================
# Apps Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := apps

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../Makefile.common

#==============================================================================
# Subdirectories
#==============================================================================
SUBDIRS := basic cli bootloader

#==============================================================================
# App to build (passed from root makefile via the EXAMPLE variable, kept for
# backward-compatible command-line usage: `make EXAMPLE=cli_simple`).
#==============================================================================
EXAMPLE ?= blink_simple

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean $(SUBDIRS) $(EXAMPLE)

# Default target - build the specified app
all: $(EXAMPLE)

# Build specific app (delegate to appropriate subdirectory).
#
# Three layouts are supported by the probe:
#   1. dir/<app>.c                 — flat single-file app (apps/basic/*.c)
#   2. dir/<app>/main.c            — per-app subdir, EXAMPLE matches subdir
#                                    name (apps/bootloader/app_blinky_signed)
#   3. dir/<app>/Makefile          — same shape but EXAMPLE != subdir,
#                                    handled by the subdir's own Makefile
#                                    (e.g. apps/cli — EXAMPLE=cli_simple)
# We let the subdir Makefile do its own dispatch when neither (1) nor (2) hit.
$(EXAMPLE):
	@for dir in $(SUBDIRS); do \
		if [ -f "$$dir/$(EXAMPLE).c" ] || [ -f "$$dir/$(EXAMPLE)/main.c" ]; then \
			$(MAKE) -C $$dir EXAMPLE=$(EXAMPLE); \
			exit 0; \
		fi; \
	done; \
	for dir in $(SUBDIRS); do \
		if $(MAKE) -C $$dir -n $(EXAMPLE) >/dev/null 2>&1; then \
			$(MAKE) -C $$dir $(EXAMPLE); \
			exit 0; \
		fi; \
	done; \
	echo "Error: App '$(EXAMPLE)' not found in any subdirectory"; \
	exit 1

# Build all apps in all subdirectories
build-all:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir all; \
	done

# Clean all app build artifacts
clean:
	@echo "Cleaning apps..."
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	@rm -rf $(BUILD_DIR)/apps
