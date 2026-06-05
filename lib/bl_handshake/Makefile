#==============================================================================
# bl_handshake Library Makefile — Plan 001 Phase 1.9
#
# Tiny app-side helper that resets a slot's fail_count after a clean boot.
# Reuses lib/img (parser + finalize) and lib/flash (commit).  Linked into
# every app that wants to participate in the rollback-on-crash protocol.
#==============================================================================

MODULE_NAME := lib_bl_handshake

.DEFAULT_GOAL := all

include ../../Makefile.common

LOCAL_SRC_DIR   := src
LOCAL_INC_DIR   := inc
LOCAL_BUILD_DIR := $(BUILD_DIR)/lib/bl_handshake

LIB_INC_FLAGS := -I$(LOCAL_INC_DIR) \
                 -I$(LIB_DIR)/img/inc \
                 -I$(LIB_DIR)/flash/inc

LOCAL_SRCS := $(wildcard $(LOCAL_SRC_DIR)/*.c)
LOCAL_OBJS := $(patsubst $(LOCAL_SRC_DIR)/%.c,$(LOCAL_BUILD_DIR)/%.o,$(LOCAL_SRCS))

TARGET_LIB := $(LOCAL_BUILD_DIR)/libbl_handshake.a

.PHONY: all clean

all: $(TARGET_LIB)

$(TARGET_LIB): $(LOCAL_OBJS)
	$(make-build-dir)
	@echo "Creating bl_handshake library..."
	$(AR) rcs $@ $^
	@echo "bl_handshake library created: $@"

$(LOCAL_BUILD_DIR)/%.o: $(LOCAL_SRC_DIR)/%.c
	$(make-build-dir)
	@echo "Compiling bl_handshake: $<"
	$(CC) $(CFLAGS) $(LIB_INC_FLAGS) -o $@ $<

clean:
	@echo "Cleaning bl_handshake..."
	@rm -rf $(LOCAL_BUILD_DIR)
