# stm32-bare-metal

This repository contains toolchain setup and bare-metal programming examples for the STM32 NUCLEO-F411RE evaluation board. It demonstrates direct register-level programming without using vendor libraries, HAL or IDE, providing a minimal and educational approach to embedded development.

## Features

- **Direct register access:** No vendor libraries or HAL; all peripheral access is via memory-mapped registers.
- **Multiple examples:**
  - LED blinking (simple)
  - Button polling and interrupt-based handling
  - UART serial communication (simple output and echo)
  - Sleep mode with button wake-up
- **Modular build system:** Organized Makefile structure with common definitions and per-module builds.
- **Third-party libraries:** Integrated printf and logging capabilities.
- **Utility functions:** String manipulation utilities for embedded use.
- **Custom linker script:** Simple linker script for STM32F411RE.
- **Startup code:** Custom startup file with vector table and reset handler.
- **OpenOCD integration:** Flash and debug support via OpenOCD.

## Prerequisites

- **Linux OS** (tested on Ubuntu)
- **ARM GCC Toolchain:**  
  Install with  
  ```sh
  sudo apt install gcc-arm-none-eabi
  ```
- **OpenOCD:**  
  Install with  
  ```sh
  sudo apt install openocd
  ```
- **STM32 NUCLEO-F411RE board** connected via USB

## Building the Project

1. **Clone the repository:**
   ```sh
   git clone https://github.com/vinibr01/stm32-bare-metal.git
   cd stm32-bare-metal
   git submodule update --init --recursive
   ```

2. **Build the firmware:**
   ```sh
   make
   ```
   This builds the default example (`blink_simple`).
   
   To build a specific example:
   ```sh
   make EXAMPLE=<example_name>
   ```
   
   To build all examples:
   ```sh
   make all
   ```
   
   Available examples:
   - `blink_simple` - LED blink example
   - `button_simple` - Push button polling example
   - `button_interrupt` - Push button with interrupt handling
   - `button_sleep` - Sleep mode with EXTI wakeup
   - `serial_simple` - UART serial output example  
   - `serial_echo` - UART echo example

3. **Clean build files:**
   ```sh
   make clean
   ```

4. **Get help:**
   ```sh
   make help
   ```
   This displays all available make targets and examples.

## Flashing and Debugging

1. **Connect your NUCLEO board via USB.**

2. **Flash directly using OpenOCD:**
   ```sh
   make flash EXAMPLE=<example_name>
   ```
   Or flash the default example:
   ```sh
   make flash
   ```

3. **Alternatively, flash using GDB:**

   a. **Start OpenOCD:**
   ```sh
   make openocd
   ```
   or manually:
   ```sh
   openocd -f board/st_nucleo_f4.cfg
   ```

   b. **In another terminal, use GDB:**
   ```sh
   cd build/examples/basic/<example_name>/
   arm-none-eabi-gdb <example_name>.elf
   ```
   Then in GDB:
   ```
   target remote localhost:3333
   monitor reset init
   monitor flash write_image erase <example_name>.elf
   monitor reset init
   monitor resume
   ```

## Debugging

The project includes a convenient debug script for GDB debugging:

```sh
make debug EXAMPLE=<example_name>
```

This will:
- Start OpenOCD in the background
- Launch GDB with the specified example
- Automatically connect to the target
- Load the symbol table

## Serial Communication

1. **Connect and flash your NUCLEO board with a serial example:**
   ```sh
   make flash EXAMPLE=serial_simple  # For output only
   # or
   make flash EXAMPLE=serial_echo    # For echo functionality
   ```

2. **Open a terminal on the host PC:**
   ```sh
   picocom -b 115200 /dev/tty*  # Use the device name created when the board is connected
   ```

## Dependencies

The project includes the following third-party components:

### Core Dependencies
- **CMSIS** (Cortex Microcontroller Software Interface Standard)
  - ARM's standardized hardware abstraction layer for Cortex-M processors
  - Provides core CPU definitions, peripheral access, and startup code templates
  - Located in `chip_headers/CMSIS/`

- **STM32F4xx Device Headers**
  - ST Microelectronics' device-specific register definitions for STM32F4 series
  - Memory-mapped register addresses and bit field definitions
  - Located in `chip_headers/CMSIS/Device/ST/STM32F4xx/`

### Third-Party Libraries
- **printf** ([mpaland/printf](https://github.com/mpaland/printf))
  - Lightweight printf/sprintf implementation for embedded systems
  - No dependencies on standard C library
  - Configurable features to minimize code size
  - Located in `3rd_party/printf/`

- **log_c**
  - Minimal logging library for embedded C applications
  - Multiple log levels (critical, error, warning, info, debug)
  - Compile-time filtering and custom backend support
  - Located in `3rd_party/log_c/`

### Internal Libraries
- **String Utilities**
  - Custom string manipulation functions optimized for embedded use
  - Includes safe string operations and formatting helpers
  - Located in `utils/`

All dependencies are included in the repository or fetched via git submodules during the initial setup.

## License

MIT License. See [LICENSE](LICENSE) for details.

---

**Note:** This project is for educational purposes and demonstrates low-level embedded programming on STM32 microcontrollers.
