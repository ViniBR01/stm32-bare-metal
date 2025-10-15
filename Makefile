#==============================================================================
# Project Name & Target
#==============================================================================
# To build a specific example, run: make EXAMPLE=<example_name>
# For example: make EXAMPLE=push_button_simple
EXAMPLE ?= blink_simple
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
AR      := arm-none-eabi-ar

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
-I./chip_headers/CMSIS/Device/ST/STM32F4xx/Include \
-I./3rd_party/printf \
-I./3rd_party/log_c/src

#==============================================================================
# Compiler and Linker Flags
#==============================================================================
# MCU-specific flags
MCU_FLAGS := -mcpu=cortex-m4 -mthumb

# C flags
CFLAGS := $(MCU_FLAGS) -c -std=gnu11 -Wall -g -O0 $(C_INCLUDES) -DSTM32F411xE -ffunction-sections -fdata-sections

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

# Add the 3rd-party log_c static library archive to link inputs. It will be
# created from 3rd_party/log_c/src/log_c.c into $(BUILD_DIR)/liblog_c.a
OBJS += $(BUILD_DIR)/liblog_c.a
# Add the static printf library archive to the link inputs. The archive
# will be created from 3rd_party/printf/printf.c into $(BUILD_DIR)/libprintf.a
OBJS += $(BUILD_DIR)/libprintf.a

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

# Compile the 3rd-party printf source into an object inside the build dir
# Build with flags to disable floating point, exponential and long-long support
CFLAGS_PRINTF := -DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL -DPRINTF_DISABLE_SUPPORT_LONG_LONG
$(BUILD_DIR)/printf.o: $(ROOT_DIR)/3rd_party/printf/printf.c
	@mkdir -p $(@D)
	@echo "Compiling 3rd-party printf (no float/exp/long long): $<"
	$(CC) $(CFLAGS) $(CFLAGS_PRINTF) -o $@ $<

# Archive the printf object into a static library
$(BUILD_DIR)/libprintf.a: $(BUILD_DIR)/printf.o
	@mkdir -p $(@D)
	@echo "Archiving $^ -> $@"
	@$(AR) rcs $@ $^

# Compile the 3rd-party log_c source into an object inside the build dir
$(BUILD_DIR)/log_c.o: $(ROOT_DIR)/3rd_party/log_c/src/log_c.c
	@mkdir -p $(@D)
	@echo "Compiling 3rd-party log_c: $<"
	$(CC) $(CFLAGS) -o $@ $<

# Archive the log_c object into a static library
$(BUILD_DIR)/liblog_c.a: $(BUILD_DIR)/log_c.o
	@mkdir -p $(@D)
	@echo "Archiving $^ -> $@"
	@$(AR) rcs $@ $^

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

flash: $(BUILD_DIR)/$(TARGET).elf
	@echo "Flashing $(TARGET).elf to target using OpenOCD..."
	openocd -f board/st_nucleo_f4.cfg -c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

debug: $(BUILD_DIR)/$(TARGET).elf
	./debug.sh $(BUILD_DIR)/$(TARGET).elf
