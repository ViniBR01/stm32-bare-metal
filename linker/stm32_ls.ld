/*1.Entry Point*/
ENTRY(Reset_Handler)

/*2.Memory definitions*/
MEMORY
{
	FLASH(rx):ORIGIN =0x08000000,LENGTH =512K
	SRAM(rwx):ORIGIN =0x20000000,LENGTH =128K
}

_estack = ORIGIN(SRAM)+LENGTH(SRAM);
 
/*3.Indicate required heap and stack size*/

__max_heap_size = 0x200;
__max_stack_size = 0x400;

/*Sections*/
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
        *(.rodata)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Initialized data */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    /* Uninitialized data */
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

}
