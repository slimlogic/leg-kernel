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

#include <acpi/pdc_arm64.h>

#include <asm/cacheflush.h>

#include <linux/init.h>

#define COMPILER_DEPENDENT_INT64	long long
#define COMPILER_DEPENDENT_UINT64	unsigned long long

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

/* Blob handling macros */
#define	ACPI_BLOB_HEADER_SIZE	8

int __acpi_acquire_global_lock(unsigned int *lock);
int __acpi_release_global_lock(unsigned int *lock);

#define ACPI_ACQUIRE_GLOBAL_LOCK(facs, Acq) \
	((Acq) = __acpi_acquire_global_lock(&facs->global_lock))

#define ACPI_RELEASE_GLOBAL_LOCK(facs, Acq) \
	((Acq) = __acpi_release_global_lock(&facs->global_lock))

/* Basic configuration for ACPI */
/* BOZO: hardware reduced acpi only? */
#ifdef	CONFIG_ACPI
extern int acpi_disabled;
extern int acpi_noirq;
extern int acpi_pci_disabled;
extern int acpi_strict;

struct acpi_arm_root {
	phys_addr_t phys_address;
	unsigned long size;
};
extern struct acpi_arm_root acpi_arm_rsdp_info;

/* Low-level suspend routine. */
extern int acpi_suspend_lowlevel(void);

/* Physical address to resume after wakeup */
/* BOZO: was...
#define acpi_wakeup_address ((unsigned long)(real_mode_header->wakeup_start))
*/
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
