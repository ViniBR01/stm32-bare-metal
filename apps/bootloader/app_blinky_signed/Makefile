#==============================================================================
# app_blinky_signed Makefile — Plan 001 Phase 1.5
#
# Trivial slot-A app linked with linker/app_ls.ld at SLOT_BASE=0x08010000.
# After link + objcopy, tools/sign_image.py prepends the 140-byte img_header_t
# and writes app_blinky_signed.signed.bin.  That is the artifact CI/HIL flash
# at slot A; the unsigned .bin is kept as a debug aid only.
#==============================================================================

MODULE_NAME := app_blinky_signed

ifdef EXAMPLE
.DEFAULT_GOAL := $(EXAMPLE)
else
.DEFAULT_GOAL := help
endif

include ../../../Makefile.common

#==============================================================================
# Slot-A linker script
#==============================================================================
LDSCRIPT := $(LINKER_DIR)/app_ls.ld
LDFLAGS  += -Wl,--defsym=SLOT_BASE=$(SLOT_BASE)

LOCAL_BUILD_DIR := $(BUILD_DIR)/apps/bootloader/app_blinky_signed$(SLOT_SUFFIX)

OBJ  := $(LOCAL_BUILD_DIR)/main.o
ELF  := $(LOCAL_BUILD_DIR)/app_blinky_signed$(SLOT_SUFFIX).elf
BIN  := $(LOCAL_BUILD_DIR)/app_blinky_signed$(SLOT_SUFFIX).bin
SBIN := $(LOCAL_BUILD_DIR)/app_blinky_signed$(SLOT_SUFFIX).signed.bin

DEPS := $(BL_HANDSHAKE_LIB) $(IMG_LIB) $(FLASH_LIB) $(DRIVERS_LIB) $(STARTUP_OBJ)

APP_CFLAGS := -I$(LIB_DIR)/bl_handshake/inc -I$(LIB_DIR)/img/inc -I$(LIB_DIR)/flash/inc

#==============================================================================
# Image-version field baked into the signed header.  Bumped manually as the
# app changes; Phase 1.9 will tie this into the anti-rollback counter.
#==============================================================================
IMAGE_VERSION ?= 1

.PHONY: all clean help app_blinky_signed

help:
	@echo "Usage: make EXAMPLE=app_blinky_signed"

all: app_blinky_signed

app_blinky_signed: $(SBIN)
	@echo "App app_blinky_signed built ($(SBIN))."

$(OBJ): main.c
	$(make-build-dir)
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) $(APP_CFLAGS) -o $@ $<

$(ELF): $(OBJ) $(DEPS)
	$(make-build-dir)
	@echo "Linking app_blinky_signed.elf..."
	$(CC) $(LDFLAGS) -Wl,-Map=$(LOCAL_BUILD_DIR)/app_blinky_signed.map,--cref \
		-o $@ $(OBJ) -Wl,--start-group $(DEPS) -Wl,--end-group -lc -lm -lgcc
	@echo "Linking complete."

$(BIN): $(ELF)
	$(CP) -O binary $< $@
	@$(SZ) $<

$(SBIN): $(BIN) $(DEV_PRIV)
	@echo "Signing app_blinky_signed.bin..."
	python3 $(ROOT_DIR)/tools/sign_image.py \
		--priv-in $(DEV_PRIV) \
		--in $(BIN) \
		--out $(SBIN) \
		--image-type app \
		--image-version $(IMAGE_VERSION)

clean:
	@echo "Cleaning app_blinky_signed..."
	@rm -rf $(LOCAL_BUILD_DIR)
