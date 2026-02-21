/* 1. Entry Point */
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
