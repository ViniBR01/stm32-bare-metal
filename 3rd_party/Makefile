#==============================================================================
# 3rd Party Libraries Makefile
#==============================================================================

# Module name for identification
MODULE_NAME := 3rd_party

# Default target
.DEFAULT_GOAL := all

# Include common definitions
include ../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_BUILD_DIR := $(BUILD_DIR)/3rd_party

#==============================================================================
# Printf library configuration
#==============================================================================
PRINTF_SRC := printf/printf.c
PRINTF_OBJ := $(LOCAL_BUILD_DIR)/printf.o
PRINTF_FLAGS := -DPRINTF_DISABLE_SUPPORT_FLOAT \
                -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL \
                -DPRINTF_DISABLE_SUPPORT_LONG_LONG

#==============================================================================
# Log_c library configuration
#==============================================================================
LOG_C_SRC := log_c/src/log_c.c
LOG_C_OBJ := $(LOCAL_BUILD_DIR)/log_c.o

#==============================================================================
# Target libraries
#==============================================================================
TARGETS := $(PRINTF_LIB) $(LOG_C_LIB)

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean

all: $(TARGETS)

# Build printf library
$(PRINTF_LIB): $(PRINTF_OBJ)
	$(make-build-dir)
	@echo "Creating printf library..."
	$(AR) rcs $@ $^
	@echo "Printf library created: $@"

$(PRINTF_OBJ): $(PRINTF_SRC)
	$(make-build-dir)
	@echo "Compiling 3rd-party printf (no float/exp/long long): $<"
	$(CC) $(CFLAGS) $(PRINTF_FLAGS) -o $@ $<

# Build log_c library
$(LOG_C_LIB): $(LOG_C_OBJ)
	$(make-build-dir)
	@echo "Creating log_c library..."
	$(AR) rcs $@ $^
	@echo "Log_c library created: $@"

$(LOG_C_OBJ): $(LOG_C_SRC)
	$(make-build-dir)
	@echo "Compiling 3rd-party log_c: $<"
	$(CC) $(CFLAGS) -o $@ $<

# Clean all 3rd party build artifacts
clean:
	@echo "Cleaning 3rd party libraries..."
	@rm -rf $(LOCAL_BUILD_DIR)
	@rm -f $(TARGETS)
