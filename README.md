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
  - Used for CLI and direct printf usage
  - No dependencies on standard C library
  - Configurable features to minimize code size
  - Located in `3rd_party/printf/`

- **log_c**
  - Minimal logging library for embedded C applications
  - **Self-contained**: No printf dependency (~1.8KB compiled size)
  - Multiple log levels (critical, error, warning, info, debug)
  - Compile-time filtering and callback-based backend
  - Internal formatting (supports %d, %u, %x, %s, %c)
  - Located in `3rd_party/log_c/`

### Internal Libraries
- **String Utilities**
  - Custom string manipulation functions optimized for embedded use
  - Includes safe string operations and formatting helpers
  - Located in `utils/`

All dependencies are included in the repository or fetched via git submodules during the initial setup.

## Logging System

The project uses the `log_c` library for structured logging, with a custom platform integration layer that makes it easy to use on STM32.

### Quick Start

```c
#include "log_platform.h"
#include "log_c.h"

int main(void) {
    // Initialize logging with UART backend (one line!)
    log_platform_init_uart();
    
    // Use logging macros
    loginfo("System initialized");
    logdebug("Debug information");
    logerror("Error occurred");
    
    // ... rest of your code
}
```

### Log Levels

The library supports five log levels (plus OFF):
- `logcritical()` - Unrecoverable errors
- `logerror()` - Error conditions
- `logwarning()` - Warning conditions
- `loginfo()` - Informational messages (default level)
- `logdebug()` - Debug-level messages

### Compile-Time Log Level Configuration

Control which log levels are compiled into your binary using the `LOG_LEVEL` build variable:

```sh
# Build with default level (INFO - includes critical, error, warning, info)
make EXAMPLE=serial_simple

# Build with DEBUG level (includes all messages)
make EXAMPLE=serial_simple LOG_LEVEL=LOG_LEVEL_DEBUG

# Build with only critical and error messages
make EXAMPLE=serial_simple LOG_LEVEL=LOG_LEVEL_ERROR
```

Valid values:
- `LOG_LEVEL_OFF` - Disable all logging
- `LOG_LEVEL_CRITICAL` - Only critical messages
- `LOG_LEVEL_ERROR` - Critical and error messages
- `LOG_LEVEL_WARNING` - Critical, error, and warning messages
- `LOG_LEVEL_INFO` - All except debug (default)
- `LOG_LEVEL_DEBUG` - All messages

Higher levels include all lower levels. Messages above the compile-time level are completely eliminated from the binary, saving code space.

### Thread and Interrupt Safety

The logging system is designed to be safe for use in both main code and interrupt handlers:

- **Thread-safe**: The platform layer uses a singleton pattern with static allocation
- **Interrupt-safe**: UART writes are atomic and blocking
- **No dynamic allocation**: All state is statically allocated
- **Reentrant**: Safe to call from multiple contexts

You can safely call logging functions from:
- Main application code
- RTOS tasks (if using an RTOS)
- Interrupt handlers (though excessive logging in ISRs is not recommended)

### Custom Backends

For advanced use cases, you can redirect log output to custom destinations:

```c
#include "log_platform.h"

void my_custom_putchar(char c) {
    // Send to SPI, I2C, flash, network, etc.
    spi_send_byte(c);
}

int main(void) {
    // Initialize with custom backend
    log_platform_init_custom(my_custom_putchar);
    
    // Logs now go to your custom function
    loginfo("This goes to SPI");
}
```

### Logging vs Printf

**When to use logging macros** (`loginfo`, `logerror`, etc.):
- Application-level informational messages
- Error reporting and diagnostics
- Debug traces during development
- Messages that should be filtered by level

**When to use printf directly**:
- User interface output (CLI prompts, menus)
- Data output that must always appear regardless of log level
- Formatted data dumps

The `serial_simple` example demonstrates proper logging usage.

### Platform Integration Architecture

The logging system uses a layered architecture:

```
Application Code
    ↓ calls log_platform_init_uart()
Platform Integration Layer (log_platform.c)
    ↓ registers callback
log_c library (self-contained, no printf)
    ↓ calls output callback
UART Driver (or custom backend)
```

This design:
- **Self-contained log_c**: No printf dependency, smaller code size (~1.8KB vs ~4-6KB)
- **Callback-based API**: Clean, explicit backend registration (no weak symbols)
- **Platform layer**: Provides simple initialization for STM32 users
- **Future-ready**: Singleton pattern enables runtime configuration
- **Proper abstraction**: Demonstrates clean layering in embedded systems

### Code Size Benefits

With the self-contained log_c implementation:
- **log_c library**: ~1.8KB (includes internal formatting)
- **Total savings**: ~3-4KB compared to printf-based approach
- **printf library**: Still available for CLI and direct printf usage

## License

MIT License. See [LICENSE](LICENSE) for details.

---

**Note:** This project is for educational purposes and demonstrates low-level embedded programming on STM32 microcontrollers.
