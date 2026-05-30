/*
 * App linker script — STM32F411RE, slot-aware.
 *
 * Apps live inside an A/B slot at SLOT_BASE (default 0x08010000 = sector 4).
 * `sign_image.py` prepends a 140-byte img_header_t to the raw .bin produced by
 * objcopy.  The signed image is flashed at SLOT_BASE, so the app payload —
 * vector table, .text, .rodata, etc. — physically starts at
 * SLOT_BASE + sizeof(img_header_t).  To make the linker's symbolic addresses
 * match the post-flash layout, we set FLASH ORIGIN to that same offset.
 *
 * SLOT_BASE is supplied at link time:
 *   $(CC) -Wl,--defsym=SLOT_BASE=0x08010000 ...
 * and may be re-targeted to the slot-B base once Phase 1.7 lands.
 *
 * The linker also publishes _app_vector_base = ORIGIN(FLASH) so the shared
 * startup code can update SCB->VTOR before any interrupt fires.  The
 * bootloader build deliberately leaves _app_vector_base undefined; startup
 * declares it weak and skips the VTOR write when the address is zero.
 */

ENTRY(Reset_Handler)

/* sizeof(img_header_t).  Pinned in lib/img/inc/img_header.h with a
 * _Static_assert and validated by the round-trip test.  If the on-flash
 * format ever changes, both spots must move together. */
__img_header_size = 0x8C;

/* Default = slot A.  Override with --defsym=SLOT_BASE=... for slot B. */
PROVIDE(SLOT_BASE = 0x08010000);

/* End of usable slot region.  Slots A and B are each 192 KB
 * (sectors 4+5+6 = 64K + 128K + 0; slot B will live in sectors 7).
 * For Phase 1.5 only slot A exists, so we cap at 192 KB to avoid silently
 * landing a future slot-B build inside slot A's region. */
__slot_size_bytes = 192K;

MEMORY
{
    FLASH(rx) : ORIGIN = SLOT_BASE + __img_header_size,
                LENGTH = __slot_size_bytes - __img_header_size
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
        _app_vector_base = .;
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

ASSERT(LENGTH(FLASH) >= (_etext - ORIGIN(FLASH)),
       "App FLASH overflow -- payload exceeds slot region.")
ASSERT(LENGTH(SRAM)  >= (_ebss - ORIGIN(SRAM)),
       "RAM overflow -- .data + .bss exceed SRAM!")
ASSERT(LENGTH(SRAM)  >= (_ebss + __max_heap_size + __max_stack_size - ORIGIN(SRAM)),
       "RAM overflow -- .data + .bss + heap + stack exceed SRAM!")
