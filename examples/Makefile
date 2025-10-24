#==============================================================================
# Examples Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := examples

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../Makefile.common

#==============================================================================
# Subdirectories
#==============================================================================
SUBDIRS := basic

#==============================================================================
# Example to build (passed from root makefile)
#==============================================================================
EXAMPLE ?= blink_simple

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean $(SUBDIRS) $(EXAMPLE)

# Default target - build the specified example
all: $(EXAMPLE)

# Build specific example (delegate to appropriate subdirectory)
$(EXAMPLE):
	@# Determine which subdirectory contains the example
	@for dir in $(SUBDIRS); do \
		if [ -f "$$dir/$(EXAMPLE).c" ]; then \
			$(MAKE) -C $$dir EXAMPLE=$(EXAMPLE); \
			exit 0; \
		fi; \
	done; \
	echo "Error: Example '$(EXAMPLE)' not found in any subdirectory"; \
	exit 1

# Build all examples in all subdirectories
build-all:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir all; \
	done

# Clean all example build artifacts
clean:
	@echo "Cleaning examples..."
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	@rm -rf $(BUILD_DIR)/examples
