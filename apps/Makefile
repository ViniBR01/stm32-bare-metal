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
SUBDIRS := basic cli

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

# Build specific app (delegate to appropriate subdirectory)
$(EXAMPLE):
	@# Determine which subdirectory contains the app
	@for dir in $(SUBDIRS); do \
		if [ -f "$$dir/$(EXAMPLE).c" ]; then \
			$(MAKE) -C $$dir EXAMPLE=$(EXAMPLE); \
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
