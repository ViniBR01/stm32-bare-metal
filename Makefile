#==============================================================================
# Project Name & Target
#==============================================================================
# To build a specific example, run: make EXAMPLE=<example_name>
# For example: make EXAMPLE=push_button_simple
EXAMPLE ?= blink_led_simple
TARGET = $(EXAMPLE)

#==============================================================================
# Directories
#==============================================================================
ROOT_DIR      := .
DRIVERS_DIR   := $(ROOT_DIR)/drivers
EXAMPLES_DIR  := $(ROOT_DIR)/examples
STARTUP_DIR   := $(ROOT_DIR)/startup
LINKER_DIR    := $(ROOT_DIR)/linker
BUILD_DIR     := $(ROOT_DIR)/build

#==============================================================================
# Toolchain
#==============================================================================
CC      := arm-none-eabi-gcc
CP      := arm-none-eabi-objcopy
OD      := arm-none-eabi-objdump
SZ      := arm-none-eabi-size

#==============================================================================
# Source Files
#==============================================================================
# C source files from drivers and the selected example
C_SOURCES := $(wildcard $(DRIVERS_DIR)/src/*.c)
C_SOURCES += $(EXAMPLES_DIR)/basic/$(EXAMPLE).c

# Assembly/startup source files
S_SOURCES := $(STARTUP_DIR)/stm32f411_startup.c

#==============================================================================
# Include Directories
#==============================================================================
C_INCLUDES := \
-I$(DRIVERS_DIR)/inc \
-I./chip_headers/CMSIS/Include \
-I./chip_headers/CMSIS/Device/ST/STM32F4xx/Include

#==============================================================================
# Compiler and Linker Flags
#==============================================================================
# MCU-specific flags
MCU_FLAGS := -mcpu=cortex-m4 -mthumb

# C flags
CFLAGS := $(MCU_FLAGS) -c -std=gnu11 -Wall -g -O0 $(C_INCLUDES) -DSTM32F411xE

# Linker script
LDSCRIPT := $(LINKER_DIR)/stm32_ls.ld

# Linker flags
LDFLAGS := $(MCU_FLAGS) -nostdlib -T $(LDSCRIPT) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

#==============================================================================
# Build Rules
#==============================================================================
# Object files
OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJS += $(addprefix $(BUILD_DIR)/,$(notdir $(S_SOURCES:.c=.o)))

.PHONY: all clean openocd

all: $(BUILD_DIR)/$(TARGET).bin

# Rule to build object files from C sources
$(BUILD_DIR)/%.o: $(DRIVERS_DIR)/src/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/%.o: $(EXAMPLES_DIR)/basic/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -o $@ $<

# Rule to build object files from startup file
$(BUILD_DIR)/stm32f411_startup.o: $(STARTUP_DIR)/stm32f411_startup.c
	@mkdir -p $(@D)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -o $@ $<

# Rule to link the executable
$(BUILD_DIR)/$(TARGET).elf: $(OBJS)
	@echo "Linking..."
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	@echo "Linking complete."

# Rule to create the binary file
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CP) -O binary $< $@
	@echo "Created $(TARGET).bin"
	@$(SZ) $<

clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned build directory."

openocd: all
	@echo "Starting OpenOCD on localhost port 3333..."
	openocd -f board/st_nucleo_f4.cfg
