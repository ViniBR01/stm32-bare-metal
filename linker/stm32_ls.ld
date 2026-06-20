/* 1. Entry Point */
/*
 * Standalone profile linker script — STM32F411RE.
 *
 * This is the `PROFILE=standalone` script (Makefile.common): the pre-bootloader
 * memory map.  The image occupies the full 512 KB flash based at 0x08000000
 * (sector 0), carries no img_header_t, and is NOT signed — it is flashed
 * directly with a debugger and runs from the chip's reset vector base.
 *
 * Because the vector table sits at 0x08000000 (the reset default for VTOR),
 * this script deliberately does NOT export _app_vector_base.  The shared
 * startup (startup/stm32f411_startup.c) declares that symbol weak and skips
 * the SCB->VTOR write when it resolves to 0, leaving VTOR at the correct
 * default.  This mirrors the bootloader_ls.ld behaviour.
 *
 * SLOT_BASE is forwarded by Makefile.common's LDFLAGS as an unused --defsym;
 * this script ignores it (it is only meaningful for the slot-aware app_ls.ld).
 *
 * For the bootloader memory map (signed apps in A/B slots) use the default
 * PROFILE=bootloader, which selects linker/app_ls.ld instead.
 */
ENTRY(Reset_Handler)

/* 2. Memory definitions */
MEMORY
{
	FLASH(rx):ORIGIN =0x08000000,LENGTH =512K
	SRAM(rwx):ORIGIN =0x20000000,LENGTH =128K
}

_estack = ORIGIN(SRAM)+LENGTH(SRAM);

/* 3. Indicate required heap and stack size */

__max_heap_size = 0x200;
__max_stack_size = 0x400;

/* Sections */
SECTIONS
{
    /* Vector table at the very start of FLASH */
    .isr_vector_tbl :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector_tbl))
        . = ALIGN(4);
    } > FLASH

    /* Code and read-only data */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        KEEP(*(.init))
        KEEP(*(.fini))
        *(.glue_7)         /* ARM-to-Thumb interwork glue */
        *(.glue_7t)        /* Thumb-to-ARM interwork glue */
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* ARM exception unwind tables (required by GCC 14+ libgcc) */
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

    /* Initialized data — LMA in FLASH, VMA in SRAM */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    /* Uninitialized data (zeroed by startup code) */
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

    /* _end symbol for _sbrk / malloc support */
    PROVIDE(_end = _ebss);
    PROVIDE(end = _ebss);

    /* Variables that must persist across soft resets (not zeroed by startup) */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
        _enoinit = .;
    } > SRAM

    /* Reserve space for heap and stack so the linker can detect overflow */
    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(_heap_start = .);
        . = . + __max_heap_size;
        PROVIDE(_heap_end = .);
        . = . + __max_stack_size;
        . = ALIGN(8);
    } > SRAM

    /* ARM build attributes */
    .ARM.attributes 0 : { *(.ARM.attributes) }

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}

/* ---- Link-time overflow checks ---- */
ASSERT(LENGTH(FLASH) >= (_etext - ORIGIN(FLASH)),
       "FLASH overflow!")
ASSERT(LENGTH(SRAM)  >= (_ebss - ORIGIN(SRAM)),
       "RAM overflow -- .data + .bss exceed SRAM!")
ASSERT(LENGTH(SRAM)  >= (_ebss + __max_heap_size + __max_stack_size - ORIGIN(SRAM)),
       "RAM overflow -- .data + .bss + heap + stack exceed SRAM!")
