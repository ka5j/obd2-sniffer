/*
 * Fault exception entry (Cortex-M4).
 *
 * All assembly in the firmware is kept in this file (MISRA C:2012
 * Directive 4.3: assembly language shall be encapsulated and isolated).
 *
 * Each fault handler loads its fault type into r1 and jumps to a shared
 * entry routine, which:
 *   1. finds the stack frame the CPU pushed on exception entry. Bit 2 of the
 *      EXC_RETURN value in LR says which stack was in use: 0 = main stack
 *      (MSP), 1 = process stack (PSP);
 *   2. switches to a dedicated fault stack, so the capture code can run even
 *      if the fault was a stack overflow;
 *   3. calls fault_capture(frame, type) in fault.c, which records everything
 *      and resets the chip. It never returns.
 *
 * The type numbers must match FAULT_TYPE_* in lib/include/fault_record.h
 * (checked by static assertions in fault.c).
 */

    .syntax unified
    .cpu cortex-m4
    .thumb

    .section .text.fault_entry, "ax", %progbits

    .global HardFault_Handler
    .type HardFault_Handler, %function
HardFault_Handler:
    movs r1, #1
    b fault_entry
    .size HardFault_Handler, . - HardFault_Handler

    .global MemManage_Handler
    .type MemManage_Handler, %function
MemManage_Handler:
    movs r1, #2
    b fault_entry
    .size MemManage_Handler, . - MemManage_Handler

    .global BusFault_Handler
    .type BusFault_Handler, %function
BusFault_Handler:
    movs r1, #3
    b fault_entry
    .size BusFault_Handler, . - BusFault_Handler

    .global UsageFault_Handler
    .type UsageFault_Handler, %function
UsageFault_Handler:
    movs r1, #4
    b fault_entry
    .size UsageFault_Handler, . - UsageFault_Handler

    .type fault_entry, %function
fault_entry:
    tst lr, #4                  /* which stack holds the exception frame? */
    ite eq
    mrseq r0, msp
    mrsne r0, psp
    ldr r2, =fault_stack_end    /* switch to the dedicated fault stack */
    mov sp, r2
    b fault_capture             /* fault_capture(frame = r0, type = r1) */
    .size fault_entry, . - fault_entry

    /* 512-byte fault stack, 8-byte aligned as the AAPCS requires. */
    .section .bss.fault_stack, "aw", %nobits
    .balign 8
fault_stack:
    .space 512
fault_stack_end:
