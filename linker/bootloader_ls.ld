/*
 * Bootloader linker script — STM32F411RE.
 *
 * Sector 0 only: 16 KB starting at 0x08000000.  This script intentionally
 * cannot reach the slot regions (sectors 4+ at 0x08010000 onwards) so a
 * stray pointer in the bootloader cannot scribble on app slots, and
 * conversely an over-large bootloader fails to link rather than silently
 * straddling sector 1.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH(rx) : ORIGIN = 0x08000000, LENGTH = 16K
    SRAM(rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(SRAM) + LENGTH(SRAM);

__max_heap_size  = 0x200;
__max_stack_size = 0x400;

SECTIONS
{
    .isr_vector_tbl :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector_tbl))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        KEEP(*(.init))
        KEEP(*(.fini))
        *(.glue_7)
        *(.glue_7t)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    PROVIDE(_end = _ebss);
    PROVIDE(end  = _ebss);

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
        _enoinit = .;
    } > SRAM

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(_heap_start = .);
        . = . + __max_heap_size;
        PROVIDE(_heap_end = .);
        . = . + __max_stack_size;
        . = ALIGN(8);
    } > SRAM

    .ARM.attributes 0 : { *(.ARM.attributes) }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}

/*
 * The bootloader build does NOT define _app_vector_base.  startup code uses a
 * weak alias that defaults to 0, so SCB->VTOR remains at the chip default
 * (0x08000000) — which is exactly where this image lives.
 */

ASSERT(LENGTH(FLASH) >= (_etext - ORIGIN(FLASH)),
       "Bootloader FLASH overflow — sector 0 is 16 KB.")
ASSERT(LENGTH(SRAM)  >= (_ebss - ORIGIN(SRAM)),
       "RAM overflow -- .data + .bss exceed SRAM!")
ASSERT(LENGTH(SRAM)  >= (_ebss + __max_heap_size + __max_stack_size - ORIGIN(SRAM)),
       "RAM overflow -- .data + .bss + heap + stack exceed SRAM!")
