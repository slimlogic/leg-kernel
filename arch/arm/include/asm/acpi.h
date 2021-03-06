/*
 *  Copyright (C) 2013, Al Stone <ahs3@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef _ASM_ARM_ACPI_H
#define _ASM_ARM_ACPI_H

#ifdef __KERNEL__

#include <acpi/pdc_arm.h>

#include <asm/cacheflush.h>

#include <linux/init.h>

#include <asm/div64.h>

#define COMPILER_DEPENDENT_INT64	long long
#define COMPILER_DEPENDENT_UINT64	unsigned long long

#define MAX_LOCAL_APIC 256
#define MAX_IO_APICS 64

/*
 * Calling conventions:
 *
 * ACPI_SYSTEM_XFACE        - Interfaces to host OS (handlers, threads)
 * ACPI_EXTERNAL_XFACE      - External ACPI interfaces
 * ACPI_INTERNAL_XFACE      - Internal ACPI interfaces
 * ACPI_INTERNAL_VAR_XFACE  - Internal variable-parameter list interfaces
 */
#define ACPI_SYSTEM_XFACE
#define ACPI_EXTERNAL_XFACE
#define ACPI_INTERNAL_XFACE
#define ACPI_INTERNAL_VAR_XFACE

/* Asm macros */
#define ACPI_ASM_MACROS
#define BREAKPOINT3
#define ACPI_DISABLE_IRQS() local_irq_disable()
#define ACPI_ENABLE_IRQS()  local_irq_enable()
#define ACPI_FLUSH_CPU_CACHE() flush_cache_all()

#define ACPI_DIV_64_BY_32(n_hi, n_lo, d32, q32, r32) {	\
	u64 value = (u64)n_hi << 32 | n_lo;			\
	r32 = do_div(value, d32);			\
	q32 = value;				\
	}

#define ACPI_SHIFT_RIGHT_64(n_hi, n_lo) \
	asm ("mov	r0, %2\n"                       \
	     "mov	r1, %3\n"                       \
             "and       r2, r0, #1\n"                   \
             "lsr       r0, r0, #1\n"                   \
             "lsr       r1, r1, #1\n"                   \
             "orr       r1, r1, r2, lsl #31\n"          \
             "mov       %0, r0\n"                       \
             "mov       %1, r1\n"                       \
	     : "=r"(n_hi), "=r"(n_lo)		/* output operands */   \
	     : "0"(n_hi), "1"(n_lo)     	/* input operands */    \
	     : "r0", "r1", "r2"       		/* clobbered registers */ \
            )

/* Blob handling macros */
#define	ACPI_BLOB_HEADER_SIZE	8

/* Basic configuration for ACPI */
#ifdef	CONFIG_ACPI
extern int acpi_disabled;
extern int acpi_noirq;
extern int acpi_pci_disabled;
extern int acpi_strict;

/* map logic cpu id to physical APIC id
 * APIC = GIC cpu interface on ARM
 */
extern volatile int arm_cpu_to_apicid[NR_CPUS];
extern int boot_cpu_apic_id;
#define cpu_physical_id(cpu) arm_cpu_to_apicid[cpu]

struct acpi_arm_root {
	phys_addr_t phys_address;
	unsigned long size;
};
extern struct acpi_arm_root acpi_arm_rsdp_info;

void arm_acpi_reserve_memory(void);

/* Low-level suspend routine. */
extern int (*acpi_suspend_lowlevel)(void);

extern void prefill_possible_map(void);

#define acpi_wakeup_address (0)

static inline void disable_acpi(void)
{
	acpi_disabled = 1;
	acpi_pci_disabled = 1;
	acpi_noirq = 1;
}

static inline bool arch_has_acpi_pdc(void)
{
	/* BOZO: replace x86 specific-ness here */
	return 0;	/* always false for now */
}

static inline void arch_acpi_set_pdc_bits(u32 *buf)
{
	/* BOZO: replace x86 specific-ness here */
}

static inline void acpi_noirq_set(void) { acpi_noirq = 1; }
static inline void acpi_disable_pci(void)
{
	acpi_pci_disabled = 1;
	acpi_noirq_set();
}

#else	/* !CONFIG_ACPI */
#define acpi_disabled 1		/* ACPI sometimes enabled on ARM */
#define acpi_noirq 1		/* ACPI sometimes enabled on ARM */
#define acpi_pci_disabled 1	/* ACPI PCI sometimes enabled on ARM */
#define acpi_strict 1		/* no ACPI spec workarounds on ARM */
#endif

#endif /*__KERNEL__*/

#endif /*_ASM_ARM_ACPI_H*/
