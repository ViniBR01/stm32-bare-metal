# stm32-bare-metal

This repository contains toolchain setup and bare-metal programming examples for the STM32 NUCLEO-F411RE evaluation board. It demonstrates direct register-level programming without using vendor libraries or HAL, providing a minimal and educational approach to embedded development.

## Features

- **Direct register access:** No vendor libraries or HAL; all peripheral access is via memory-mapped registers.
- **Blink LED example:** Minimal example toggling the onboard LED (PA5).
- **Push Button example:** Reads the user button and toggles the LED accordingly.
- **Custom linker script:** Simple linker script for STM32F411RE.
- **Startup code:** Custom startup file with vector table and reset handler.
- **Makefile-based build:** Easy build and clean commands for multiple examples.
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
   ```

2. **Build the firmware:**
   ```sh
   make
   ```
   or
   ```
   make EXAMPLE=<your_example>
   ```
   This will produce the selected binary:
   - `blink_led_simple.elf` (LED blink example)
   - `push_button_simple.elf` (Push button example)

3. **Clean build files:**
   ```sh
   make clean
   ```

## Flashing and Debugging

1. **Connect your NUCLEO board via USB.**

2. **Start OpenOCD:**
   ```sh
   make openocd
   ```
   or manually:
   ```sh
   openocd -f board/st_nucleo_f4.cfg
   ```

3. **In another terminal, flash using GDB:**
   ```sh
   cd build/
   arm-none-eabi-gdb
   ```
   Then in GDB:
   ```
   target remote localhost:3333
   monitor reset init
   monitor flash write_image erase <your_example>.elf
   monitor reset init
   monitor resume
   ```

## File Structure

- `drivers/inc/` - Header files for low-level drivers (GPIO, etc.)
- `drivers/src/` - Source files for low-level drivers
- `examples/basic/blink_led_simple.c` - Blink LED example source
- `examples/basic/push_button_simple.c` - Push button example source
- `startup/stm32f411_startup.c` - Startup code and vector table
- `linker/stm32_ls.ld` - Linker script for STM32F411RE
- `chip_headers/` - CMSIS and device header files
- `build/` - Output directory for compiled binaries and map files (created after build)
- `Makefile` - Build instructions for all examples
- `board/st_nucleo_f4.cfg` - OpenOCD configuration file

## License

MIT License. See [LICENSE](LICENSE) for details.

---

**Note:** This project is for educational purposes and demonstrates low-level embedded programming on STM32 microcontrollers.
