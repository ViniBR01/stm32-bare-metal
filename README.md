# stm32-bare-metal

This repository contains toolchain setup and bare-metal programming examples for the STM32 NUCLEO-F411RE evaluation board. It demonstrates direct register-level programming without using vendor libraries or HAL, providing a minimal and educational approach to embedded development.

## Features

- **Direct register access:** No vendor libraries or HAL; all peripheral access is via memory-mapped registers.
- **Blink LED example:** Minimal example toggling the onboard LED (PA5).
- **Custom linker script:** Simple linker script for STM32F411RE.
- **Startup code:** Custom startup file with vector table and reset handler.
- **Makefile-based build:** Easy build and clean commands.
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
   This will produce `blink_led.elf`, the main firmware binary.

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
   arm-none-eabi-gdb blink_led.elf
   ```
   Then in GDB:
   ```
   target remote localhost:3333
   load
   monitor reset init
   continue
   ```

## File Structure

- `main.c` - Main application (LED blink example)
- `stm32f411_startup.c` - Startup code and vector table
- `stm32_ls.ld` - Linker script
- `Makefile` - Build instructions
- `board/st_nucleo_f4.cfg` - OpenOCD configuration

## License

MIT License. See [LICENSE](LICENSE) for details.

---

**Note:** This project is for educational purposes and demonstrates low-level embedded programming on STM32 microcontrollers.
