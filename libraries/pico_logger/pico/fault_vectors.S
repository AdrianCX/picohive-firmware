/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// Every exception the SDK would otherwise leave on a breakpoint goes to
// cm_backtrace instead, which logs the registers and call stack and restarts.
// These are strong, so they replace crt0's weak defaults in the vector table
// itself: a fault is caught from reset on, not only once logging is up.
//
// Same as cm_backtrace's own fault_handler/gcc/cmb_fault.S, with the vector
// names added - that file is not built.

.syntax unified
.thumb
.text

.global HardFault_Handler
.global isr_hardfault
.global isr_nmi
.global isr_svcall
.global isr_pendsv
.global isr_systick
.global isr_invalid
.type HardFault_Handler, %function
.type isr_hardfault, %function
.type isr_nmi, %function
.type isr_svcall, %function
.type isr_pendsv, %function
.type isr_systick, %function
.type isr_invalid, %function

HardFault_Handler:
isr_hardfault:
isr_nmi:
isr_svcall:
isr_pendsv:
isr_systick:
isr_invalid:
    LDR     r2, =core_dump_fault_regs
    STMIA   r2!, {r4-r7}
    MOV     r4, r8
    MOV     r5, r9
    MOV     r6, r10
    MOV     r7, r11
    STMIA   r2!, {r4-r7}

    MOV     r0, lr                  /* get lr */
    MOV     r1, sp                  /* get stack pointer (current is MSP) */
    BL      cm_backtrace_fault      /* restarts */

Fault_Loop:
    B       Fault_Loop

.global core_dump_capture
.type core_dump_capture, %function
.thumb_func
core_dump_capture:
    STR     r0, [r0, #0]
    STR     r1, [r0, #4]
    STR     r2, [r0, #8]
    STR     r3, [r0, #12]
    STR     r4, [r0, #16]
    STR     r5, [r0, #20]
    STR     r6, [r0, #24]
    STR     r7, [r0, #28]
    MOV     r1, r8
    STR     r1, [r0, #32]
    MOV     r1, r9
    STR     r1, [r0, #36]
    MOV     r1, r10
    STR     r1, [r0, #40]
    MOV     r1, r11
    STR     r1, [r0, #44]
    MOV     r1, r12
    STR     r1, [r0, #48]
    MOV     r1, sp
    STR     r1, [r0, #52]
    MOV     r1, lr
    STR     r1, [r0, #56]
    SUBS    r1, #1
    STR     r1, [r0, #60]
    MRS     r1, xpsr
    LDR     r2, =0x01000000
    ORRS    r1, r2
    STR     r1, [r0, #64]
    BX      lr

.ltorg
