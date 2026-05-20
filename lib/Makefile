#==============================================================================
# Lib Makefile — orchestrator for middleware libraries
#==============================================================================

# Module name for identification
MODULE_NAME := lib

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../Makefile.common

#==============================================================================
# Subdirectories — each is a single middleware library.
# Add a new lib by creating lib/<name>/{Makefile,inc,src} and listing it here.
#==============================================================================
SUBDIRS := skeleton img

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean $(SUBDIRS)

# Build every library in SUBDIRS.
all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

# Clean every library.
clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	@rm -rf $(BUILD_DIR)/lib
