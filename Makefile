#==============================================================================
# Root Makefile - STM32 Bare Metal Project
#==============================================================================

# Include common definitions
include Makefile.common

#==============================================================================
# Default target
#==============================================================================
# To build a specific app, run: make EXAMPLE=<app_name>
# For example: make EXAMPLE=blink_simple
EXAMPLE ?= cli_simple

# Default goal
.DEFAULT_GOAL := $(EXAMPLE)

#==============================================================================
# Board / probe selection (shared by flash, flash-bootloader, serial, debug)
#==============================================================================
# BOARD names a registered role from scripts/boards.json ('dev' or 'ci').
# Manual commands default to the development board so they never disturb the
# CI board (reserved for the HIL runner — see CLAUDE.md).  Override per-command:
#
#   make flash BOARD=ci          # target the CI board explicitly
#   make flash HLA_SERIAL=066...  # pin a raw ST-LINK serial (wins over BOARD)
#
# HLA_SERIAL, when non-empty, overrides BOARD.  STLINK_SERIAL is the resolved
# value handed to OpenOCD (hla_serial / adapter serial) and to the serial-port
# finder.  Resolution defers to scripts/boards.py so the Makefile, the Python
# scripts, and the wiki all read one source of truth.
BOARD      ?= dev
HLA_SERIAL ?=
STLINK_SERIAL := $(if $(HLA_SERIAL),$(HLA_SERIAL),$(shell python3 -c \
  "import sys; sys.path.insert(0, 'scripts'); import boards; print(boards.BOARD_REGISTRY.get('$(BOARD)', ''))"))

#==============================================================================
# Subdirectories with makefiles
#==============================================================================
SUBDIRS := startup utils drivers 3rd_party lib apps

#==============================================================================
# All apps list (for 'all' target)
#==============================================================================
ALL_APPS := \
	blink_simple \
	bootloader \
	app_blinky_signed \
	button_interrupt \
	button_simple \
	button_sleep \
	crc_basic \
	iwdg_basic \
	serial_simple \
	cli_simple

# Per-app build rule target list.  A single rule (below) covers every app in
# $(ALL_APPS) plus the default goal $(EXAMPLE).  We deliberately do NOT add a
# separate $(EXAMPLE): rule — when EXAMPLE names an app already in $(ALL_APPS)
# (e.g. the default cli_simple) that would define two recipes for the same
# target and make would emit an "overriding recipe" warning.  $(EXAMPLE) is
# appended only when it is not already in the list, so custom EXAMPLE values
# still build.
APP_TARGETS := $(ALL_APPS) $(filter-out $(ALL_APPS),$(EXAMPLE))

#==============================================================================
# Phony targets
#==============================================================================
.PHONY: all clean test keys flash-bootloader sanitize-board $(SUBDIRS) flash debug openocd serial help $(APP_TARGETS)

#==============================================================================
# Build all apps
#==============================================================================
all:
	@echo "Building all apps..."
	@for app in $(ALL_APPS); do \
		echo "========================================"; \
		echo "Building $$app..."; \
		echo "========================================"; \
		$(MAKE) EXAMPLE=$$app || exit 1; \
	done
	@echo "========================================";
	@echo "All apps built successfully!"

#==============================================================================
# Build specific app
#==============================================================================
$(APP_TARGETS): startup utils drivers 3rd_party lib keys
	@echo "Building app: $@"
	$(MAKE) -C apps EXAMPLE=$@

#==============================================================================
# Build subdirectories
#==============================================================================
startup:
	$(MAKE) -C startup

drivers:
	$(MAKE) -C drivers

3rd_party:
	$(MAKE) -C 3rd_party all

utils: 3rd_party
	$(MAKE) -C utils

lib: 3rd_party drivers
	$(MAKE) -C lib

apps: startup utils drivers 3rd_party lib keys
	$(MAKE) -C apps EXAMPLE=$(EXAMPLE)

#==============================================================================
# Plan 001 dev keypair generation
#
# Produces $(DEV_PRIV) (the signing private key) and $(BL_PUBKEY_C) (the
# matching pubkey C source linked into the bootloader).  The seed is fixed,
# so re-running `make keys` is idempotent and reproducible across hosts —
# crucial for CI builds that regenerate from a clean checkout.
#
# Outputs land under build/keys/, already gitignored via the build/ rule.
#==============================================================================
keys: $(DEV_PRIV) $(BL_PUBKEY_C)

$(DEV_PRIV) $(BL_PUBKEY_C):
	@mkdir -p $(KEYS_DIR)
	@python3 tools/keygen.py \
		--seed "$(KEY_SEED)" \
		--priv-out $(DEV_PRIV) \
		--pub-out $(BL_PUBKEY_C)

#==============================================================================
# Host unit tests (no cross-compiler required)
#==============================================================================
test:
	$(MAKE) -C tests run

#==============================================================================
# Clean all build artifacts
#==============================================================================
clean:
	@echo "Cleaning all build artifacts..."
	@rm -rf $(BUILD_DIR)
	@$(MAKE) -C tests clean
	@echo "Clean complete."

#==============================================================================
# Flash target (delegates to apps)
#
# Programs the selected app's final image at its slot base address via OpenOCD,
# pinned to the chosen board's ST-LINK probe.
#
#   - For bootloader profiles (the default) the final artifact is the
#     .signed.bin and SLOT_BASE is slot A (0x08010000) or slot B (0x08040000).
#     The signed image is loaded *through* the slot the bootloader jumps into.
#   - For PROFILE=standalone the final artifact is the raw .bin and SLOT_BASE
#     is 0x08000000.
#
# Probe selection follows BOARD / HLA_SERIAL (resolved to STLINK_SERIAL at the
# top of this file); defaults to the dev board.  Examples:
#
#   make flash                          # cli_simple, dev board, slot A
#   make flash SLOT=B                   # slot B image at 0x08040000
#   make flash EXAMPLE=iwdg_basic       # a different app, slot A
#   make flash BOARD=ci                 # target the CI board
#   make flash EXAMPLE=blink_pwm PROFILE=standalone  # raw image at 0x08000000
#
# Refuses the bootloader app — use `make flash-bootloader` for sector 0.
#==============================================================================
# Final artifact extension is profile-dependent: bootloader profiles sign the
# image; standalone stops at the raw bin.
ifeq ($(SIGN_IMAGE),1)
  FLASH_EXT := signed.bin
else
  FLASH_EXT := bin
endif

flash: $(EXAMPLE)
	@if [ "$(EXAMPLE)" = "bootloader" ]; then \
		echo "Refusing to flash the bootloader via 'make flash'."; \
		echo "Use 'make flash-bootloader' instead — it makes the intent explicit"; \
		echo "and is the only path documented in bootloader-skeleton.md."; \
		exit 1; \
	fi
	@IMG=$$(find $(BUILD_DIR)/apps -name $(EXAMPLE)$(PROFILE_SUFFIX).$(FLASH_EXT) | head -1); \
	if [ -z "$$IMG" ]; then \
		echo "Error: no $(EXAMPLE)$(PROFILE_SUFFIX).$(FLASH_EXT) found under $(BUILD_DIR)/apps"; \
		exit 1; \
	fi; \
	echo "Flashing $$IMG at $(SLOT_BASE) on board '$(BOARD)' (ST-LINK $(STLINK_SERIAL))..."; \
	openocd -f board/st_nucleo_f4.cfg \
		$(if $(STLINK_SERIAL),-c "hla_serial $(STLINK_SERIAL)") \
		-c "program $$IMG $(SLOT_BASE) verify reset exit"

#==============================================================================
# flash-bootloader — explicit, manual-only sector-0 programming.
#
# Sector 0 contains the bootloader.  CI never invokes this; it is a one-time
# step the operator performs on each NUCLEO board before that board can run
# slot-A app images.  See docs/wiki/plans/001-bootloader/bootloader-skeleton.md
# for the full procedure (including OpenOCD-based recovery if a bad image
# bricks the chain).
#
# Delegates to scripts/flash_bootloader.py so we get the post-flash readback
# sanity check, the STM32_BARE_METAL_CI=1 guard, and optional ST-LINK pinning
# via `BOARD=ci|dev` or `HLA_SERIAL=...` for free.  Pass extra args with
# `BOOTLOADER_FLASH_ARGS="--skip-build"` etc.
#==============================================================================
# BOARD / HLA_SERIAL are defined once at the top of this Makefile and shared
# by every probe-driving target.  flash-bootloader forwards them to the Python
# flasher unchanged.
BOOTLOADER_FLASH_ARGS ?=

flash-bootloader: bootloader
	@echo "Programming bootloader to sector 0 (0x08000000)."
	@echo "This is a manual operation; the HIL CI runner must NEVER call this."
	@python3 scripts/flash_bootloader.py --skip-build \
		$(if $(BOARD),--board $(BOARD)) \
		$(if $(HLA_SERIAL),--hla-serial $(HLA_SERIAL)) \
		$(BOOTLOADER_FLASH_ARGS)

#==============================================================================
# sanitize-board — erase the slot metadata sectors to reset the rollback floor.
#
# The Phase 1.9 bootloader derives a rollback floor from the highest
# monotonic_counter committed across both metadata sectors.  After an
# anti-rollback or OTA HIL run the floor is left elevated (e.g. 2), so a plain
# `make flash` (which signs at the default IMAGE_VERSION=1) is then rejected on
# every slot — the board logs `rollback ver=1 < floor=N` and stops at
# `both slots failed verify`.  Erasing the metadata sectors resets floor=0 so
# the next boot accepts a freshly flashed image.
#
# Delegates to scripts/sanitize_board.py (the same step CI runs at the start of
# each HIL job); honors the shared BOARD / HLA_SERIAL knobs.
#==============================================================================
sanitize-board:
	@python3 scripts/sanitize_board.py \
		$(if $(BOARD),--board $(BOARD)) \
		$(if $(HLA_SERIAL),--hla-serial $(HLA_SERIAL))
#==============================================================================
# Debug target
#
# Flashes nothing; attaches GDB to the running target via OpenOCD, pinned to
# the selected board's ST-LINK probe (BOARD / HLA_SERIAL -> STLINK_SERIAL).
#==============================================================================
debug: $(EXAMPLE)
	./debug.sh $(shell find $(BUILD_DIR)/apps -name $(EXAMPLE)$(PROFILE_SUFFIX).elf) $(STLINK_SERIAL)

#==============================================================================
# OpenOCD target
#==============================================================================
openocd:
	@echo "Starting OpenOCD on localhost port 3333..."
	openocd -f board/st_nucleo_f4.cfg

#==============================================================================
# Serial connection target
#
# Resolves the serial port for the selected board (BOARD / HLA_SERIAL ->
# STLINK_SERIAL) by reusing run_hil_tests.find_serial_port, which prefers the
# stable /dev/serial/by-id symlink for the chosen ST-LINK and falls back to a
# /dev/cu.usbmodem* / /dev/ttyACM* glob on dev hosts without the symlink
# (e.g. macOS).  Override the board with `make serial BOARD=ci`.
#==============================================================================
BAUD_RATE ?= 115200

serial:
	@echo "Connecting to board '$(BOARD)' serial port at $(BAUD_RATE) baud..."
	@SERIAL_PORT=$$(python3 -c "import sys, os, contextlib; sys.path.insert(0, 'scripts'); \
import run_hil_tests as h; \
f = open(os.devnull, 'w'); \
ctx = contextlib.redirect_stdout(f); ctx.__enter__(); \
p = h.find_serial_port(hla_serial='$(STLINK_SERIAL)'); \
ctx.__exit__(None, None, None); \
print(p or '')" 2>/dev/null); \
	if [ -z "$$SERIAL_PORT" ]; then \
		echo "Error: No serial device found. Is the board connected?"; \
		exit 1; \
	fi; \
	echo "Found device: $$SERIAL_PORT"; \
	picocom -b $(BAUD_RATE) $$SERIAL_PORT

#==============================================================================
# Help target
#==============================================================================
help:
	@echo "STM32 Bare Metal Build System"
	@echo "============================="
	@echo ""
	@echo "Usage:"
	@echo "  make                    - Build default app (cli_simple)"
	@echo "  make EXAMPLE=<name>     - Build specific app"
	@echo "  make all                - Build all apps"
	@echo "  make clean              - Clean all build artifacts"
	@echo "  make test               - Run host unit tests (no board needed)"
	@echo "  make flash              - Flash current app to its slot via OpenOCD"
	@echo "  make debug              - Debug current app"
	@echo "  make openocd            - Start OpenOCD server"
	@echo "  make serial             - Connect to serial port (115200 baud)"
	@echo "  make serial BAUD_RATE=9600 - Connect with custom baud rate"
	@echo "  make sanitize-board     - Erase slot metadata (reset rollback floor)"
	@echo ""
	@echo "Board / slot / profile knobs (flash, serial, debug):"
	@echo "  BOARD=dev|ci            - Target a registered board (default: dev)"
	@echo "                            Roles + serials live in scripts/boards.json"
	@echo "  HLA_SERIAL=<serial>     - Pin a raw ST-LINK serial (overrides BOARD)"
	@echo "  SLOT=A|B                - Slot for bootloader profile (default: A)"
	@echo "  PROFILE=bootloader|standalone - Memory map (default: bootloader)"
	@echo ""
	@echo "Examples:"
	@echo "  make flash                          # cli_simple, dev board, slot A"
	@echo "  make flash SLOT=B                   # slot B image at 0x08040000"
	@echo "  make flash BOARD=ci                 # target the CI board"
	@echo "  make flash EXAMPLE=blink_pwm PROFILE=standalone  # raw image at 0x08000000"
	@echo "  make serial BOARD=dev               # console on the dev board"
	@echo ""
	@echo "Available apps:"
	@for app in $(ALL_APPS); do \
		echo "  - $$app"; \
	done
