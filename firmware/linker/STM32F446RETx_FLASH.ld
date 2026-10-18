/*
 * Linker script for STM32F446RE: 512 KiB flash, 128 KiB SRAM.
 *
 * Memory map (RM0390, section 2.3):
 *   FLASH  0x0800_0000  512 KiB   code, read-only data, initial values of .data
 *   RAM    0x2000_0000  128 KiB   SRAM1 (112 KiB) + SRAM2 (16 KiB), contiguous
 *
 * RAM layout, low to high:
 *   .data | .bss | .noinit | (free) ... | stack (grows down from _estack)
 *
 * The firmware never calls malloc (no heap is reserved). The ._stack_check
 * section only makes the link fail if static RAM use leaves less than
 * _Min_Stack_Size free for the stack.
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);    /* initial MSP: top of RAM */
_Min_Stack_Size = 0x2000;               /* 8 KiB reserved for the main stack */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
    /* Vector table must sit at the start of flash: the core fetches the
     * initial stack pointer and reset vector from 0x0800_0000 (aliased to 0). */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)          /* ARM/Thumb interworking glue */
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* Exception unwinding tables (pulled in by newlib even in C builds). */
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    /* Constructor/destructor tables walked by __libc_init_array. GCC emits
     * them as writable sections; READONLY keeps the flash segment R+X only. */
    .preinit_array (READONLY) :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array (READONLY) :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array (READONLY) :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* Load address of .data in flash; the startup code copies it to RAM. */
    _sidata = LOADADDR(.data);

    /* Initialised variables: live in RAM (VMA) but their initial values are
     * stored in flash (LMA). */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        *(.RamFunc)
        *(.RamFunc*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    /* Zero-initialised variables: the startup code clears this range. */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    /* Not touched by the startup code, so it keeps its contents across a
     * warm reset (watchdog, software reset, fault). Used to carry crash
     * information into the next boot. Contents are garbage after power-on,
     * so anything stored here must be validated (magic number + checksum). */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
    } > RAM

    /* newlib's _sbrk (from nosys.specs) uses `end` as the heap start. */
    PROVIDE(end = .);
    PROVIDE(_end = .);

    /* Link-time guard: fail if there's not enough RAM left for the stack. */
    ._stack_check (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
