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
    MOV     r0, lr                  /* get lr */
    MOV     r1, sp                  /* get stack pointer (current is MSP) */
    BL      cm_backtrace_fault      /* restarts */

Fault_Loop:
    B       Fault_Loop
