/*
 * arch/arm/kernel/uefi_phys.S
 *
 * Copyright (C) 2013  Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/assembler.h>
#include <asm/cp15.h>
#include <linux/linkage.h>
#define PAR_MASK 0xfff

	.text
@ uefi_phys_call(*f, virt_phys_offset, a, b, c, d, ...)
	.align  5
	.pushsection    .idmap.text, "ax"
ENTRY(uefi_phys_call)
	@ Save physical context
	mov	r12, sp
	ldr	sp, =tmpstack
	stmfd	sp, {r4-r5, r12, lr}	@ push is redefined by asm/assembler.h

	mov	r4, r1

	@ Extract function pointer (don't write lr again before call)
	mov	lr, r0

	@ Shift arguments down
	mov	r0, r2
	mov	r1, r3
	ldr	r2, [r12], #4
	ldr	r3, [r12], #4

	@ Convert sp to physical
	sub	r12, r12, r4
	mov	sp, r12

	@ Disable MMU
	ldr	r5, =(CR_M)
	update_sctlr	r12, , r5
	isb

	@ Make call
	blx	lr

	@ Enable MMU + Caches
	ldr	r4, =(CR_I | CR_C | CR_M)
	update_sctlr	r12, r4
	isb

	ldr	sp, =tmpstack_top
	ldmfd	sp, {r4-r5, r12, lr}

	@ Restore virtual sp and return
	mov	sp, r12
	bx	lr

	.align	3
tmpstack_top:
	.long	0	@ r4
	.long	0	@ r5
	.long	0	@ r12
	.long	0	@ lr
tmpstack:
ENDPROC(uefi_phys_call)
	.popsection
