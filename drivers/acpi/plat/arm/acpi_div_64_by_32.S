/*
 * Copyright (c) 2013, Al Stone <ahs3@redhat.com>
 *
 * __acpi_arm_div64_by_32: perform integer division of a 64-bit value
 *                         a 32-bit value
 *
 * The algorithm is borrowed from the GMP library, but has been redone
 * here in order to put this implementation under a GPLv2 license.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef __ARM_ARCH_7A__

#include <linux/linkage.h>

/*
 * This needs to be called in the following manner:
 *      n_lo    => r0           # these are the low 32 bits of the dividend
 *      n_hi    => r1           # the high 32 bits of the dividend
 *      d32     => r2           # the 32-bit divisor
 *
 * The result is:
 *      q32     <= r0           # the 32-bit quotient
 *      r32     <= r1           # the 32-bit remainder
 *
 * This should be consistent with the normal ARMv7 calling conventions.
 *
 */

ENTRY(__arm_acpi_div_64_by_32)
        mov     r12, #32                // loop counter
        cmp     r2, #0x80000000         // check divisor MSB and clear carry
        bcs     bigdiv

loop:   adcs    r1, r1, r1              // handle each bit
        adc     r0, r0, r0
        cmp     r0, r2
        subcs   r0, r0, r2
        sub     r12, r12, #1
        teq     r12, #0
        bne     loop

        mov     r3, r0                  // stash the remainder for a tic
        adc     r0, r1, r1              // quotient: add in last carry
        mov     r1, r3                  // remainder (now in right register)
        mov     pc, lr

bigdiv: stmfd   sp!, { r8, lr }         // clear some scratch space

        and     r8, r1, #1              // save LSB of dividend
        mov     lr, r0, lsl #31
        orrs    r1, lr, r1, lsr #1      // r1 = lower part >> 1 bit
        mov     r0, r0, lsr #1          // r0 = higher part >> 1 bit

        and     lr, r2, #1              // save LSB of divisor
        movs    r2, r2, lsr #1          // r2 = floor(divisor / 2)
        adc     r2, r2, #0              // r2 = ceil(divisor / 2)

loop2:  adcs    r1, r1, r1              // handle each bit
        adc     r0, r0, r0
        cmp     r0, r2
        subcs   r0, r0, r2
        sub     r12, r12, #1
        teq     r12, #0
        bne     loop2

        adc     r1, r1, r1              // shift and add last carry
        add     r0, r8, r0, lsl #1      // shift in remaining dividend LSB
        tst     lr, lr
        beq     evendiv

        rsb     r2, lr, r2, lsl #1      // restore divisor value
        adds    r0, r0, r1              // adjust for omitted divisor LSB
        addcs   r1, r1, #1              // adjust quotient if a carry results
        subcs   r0, r0, r2              // adjust remainder, if carry
        cmp     r0, r2
        subcs   r0, r0, #1              // adjust remainder
        addcs   r1, r1, #1              // adjust quotient

evendiv:
        mov     r3, r0                  // stash the remainder for a tic
        mov     r0, r1                  // quotient
        mov     r1, r3                  // remainder

        ldmfd   sp!, { r8, pc }         // restore the registers used

ENDPROC(__arm_acpi_div_64_by_32)

#else           /* ! __ARM_ARCH_7A__ */
#error  __arm_acpi_div_64_by_32 not defined for this architecture
#endif

