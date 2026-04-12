#==============================================================================
# Third-party Libraries Makefile
#==============================================================================

# Include common definitions
include ../Makefile.common

#==============================================================================
# Local directories
#==============================================================================
LOCAL_BUILD_DIR := $(BUILD_DIR)/3rd_party

PRINTF_SRC_DIR := $(THIRD_PARTY_DIR)/printf
LOG_C_SRC_DIR := $(THIRD_PARTY_DIR)/log_c/src
UNITY_SRC_DIR := $(THIRD_PARTY_DIR)/unity/src

#==============================================================================
# Source files
#==============================================================================
PRINTF_SRC := $(PRINTF_SRC_DIR)/printf.c
PRINTF_OBJ := $(LOCAL_BUILD_DIR)/printf.o
PRINTF_LIB_OUT := $(LOCAL_BUILD_DIR)/libprintf.a

LOG_C_SRC := $(LOG_C_SRC_DIR)/log_c.c
LOG_C_OBJ := $(LOCAL_BUILD_DIR)/log_c.o
LOG_C_LIB_OUT := $(LOCAL_BUILD_DIR)/liblog_c.a

UNITY_SRC := $(UNITY_SRC_DIR)/unity.c
UNITY_OBJ := $(LOCAL_BUILD_DIR)/unity.o
UNITY_LIB_OUT := $(LOCAL_BUILD_DIR)/libunity_arm.a

#==============================================================================
# Build rules
#==============================================================================
.PHONY: all clean unity_arm

# Default target builds printf and log_c (Unity only when HIL_TEST=1)
all: $(PRINTF_LIB_OUT) $(LOG_C_LIB_OUT)
ifeq ($(HIL_TEST),1)
	@$(MAKE) unity_arm
endif

unity_arm: $(UNITY_LIB_OUT)

# Printf library
$(PRINTF_OBJ): $(PRINTF_SRC)
	$(make-build-dir)
	@echo "Compiling printf for ARM target..."
	$(CC) $(CFLAGS_BASE) -I$(PRINTF_SRC_DIR) -o $@ $<

$(PRINTF_LIB_OUT): $(PRINTF_OBJ)
	$(make-build-dir)
	@echo "Creating printf library..."
	$(AR) rcs $@ $^

# Log_c library
$(LOG_C_OBJ): $(LOG_C_SRC)
	$(make-build-dir)
	@echo "Compiling log_c for ARM target..."
	$(CC) $(CFLAGS_BASE) -I$(LOG_C_SRC_DIR) -o $@ $<

$(LOG_C_LIB_OUT): $(LOG_C_OBJ)
	$(make-build-dir)
	@echo "Creating log_c library..."
	$(AR) rcs $@ $^

# Unity library (only built when HIL_TEST=1)
$(UNITY_OBJ): $(UNITY_SRC)
	$(make-build-dir)
	@echo "Compiling Unity for ARM target..."
	$(CC) $(CFLAGS_BASE) -I$(UNITY_SRC_DIR) -o $@ $<

$(UNITY_LIB_OUT): $(UNITY_OBJ)
	$(make-build-dir)
	@echo "Creating Unity ARM library..."
	$(AR) rcs $@ $^
	@echo "Unity ARM library created: $@"

# Clean
clean:
	@echo "Cleaning 3rd_party build artifacts..."
	@rm -rf $(LOCAL_BUILD_DIR)
