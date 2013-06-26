/*
 *  boot.c - Architecture-Specific Low-Level ACPI Boot Support
 *
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
 *  Copyright (C) 2013, Al Stone <al.stone@linaro.org> (ARM version)
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

/*
 * BOZO: this needs to be done right....
 */
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/acpi_pmtmr.h>
#include <linux/efi.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <linux/pci.h>

#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/acpi.h>

static int __initdata acpi_force;
u32 acpi_rsdt_forced;
int acpi_disabled;
EXPORT_SYMBOL(acpi_disabled);

#define BAD_MADT_ENTRY(entry, end) (					    \
		(!entry) || (unsigned long)entry + sizeof(*entry) > end ||  \
		((struct acpi_subtable_header *)entry)->length < sizeof(*entry))

#define PREFIX			"ACPI: "

int acpi_noirq;				/* skip ACPI IRQ initialization */
int acpi_pci_disabled;		/* skip ACPI PCI scan and IRQ initialization */
EXPORT_SYMBOL(acpi_pci_disabled);

int acpi_lapic;
int acpi_ioapic;
int acpi_strict;

u8 acpi_sci_flags __initdata;
int acpi_sci_override_gsi __initdata;
int acpi_skip_timer_override __initdata;
int acpi_use_timer_override __initdata;
int acpi_fix_pin2_polarity __initdata;

struct acpi_arm_root acpi_arm_rsdp_info;     /* info about RSDP from FDT */

/*
 * Boot-time Configuration
 */

/*
 * The default interrupt routing model is PIC (8259).  This gets
 * overridden if IOAPICs are enumerated (below).
 *
 * Since we're on ARM, it clearly has to be GIC.
 */
enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_GIC;

static unsigned int gsi_to_irq(unsigned int gsi)
{
	int irq = irq_create_mapping(NULL, gsi);

	return irq;
}

/*
 * BOZO: is it reasonable to just reserve the memory space?  Or are there
 * other restrictions needed?  Or does it need copying to some other place?
 */
char *__init __acpi_map_table(unsigned long phys, unsigned long size)
{
	if (!phys || !size)
		return NULL;

	/* we're already in memory so we cannot io_remap the entry */
	return phys_to_virt(phys);
}

void __init __acpi_unmap_table(char *map, unsigned long size)
{
	if (!map || !size)
		return;

	/* we're already in memory so we cannot io_remap the entry;
	 * since we're not io_remap'ing, unmap'ing is especially
	 * pointless
	 */
	return;
}

/*
 * acpi_pic_sci_set_trigger()
 *
 * use ELCR to set PIC-mode trigger type for SCI
 *
 * If a PIC-mode SCI is not recognized or gives spurious IRQ7's
 * it may require Edge Trigger -- use "acpi_sci=edge"
 *
 * Port 0x4d0-4d1 are ECLR1 and ECLR2, the Edge/Level Control Registers
 * for the 8259 PIC.  bit[n] = 1 means irq[n] is Level, otherwise Edge.
 * ECLR1 is IRQs 0-7 (IRQ 0, 1, 2 must be 0)
 * ECLR2 is IRQs 8-15 (IRQ 8, 13 must be 0)
 */

void __init acpi_pic_sci_set_trigger(unsigned int irq, u16 trigger)
{
	unsigned int mask = 1 << irq;
	unsigned int old, new;

	/* Real old ELCR mask */
	old = inb(0x4d0) | (inb(0x4d1) << 8);

	/*
	 * If we use ACPI to set PCI IRQs, then we should clear ELCR
	 * since we will set it correctly as we enable the PCI irq
	 * routing.
	 */
	new = acpi_noirq ? old : 0;

	/*
	 * Update SCI information in the ELCR, it isn't in the PCI
	 * routing tables..
	 */
	switch (trigger) {
	case 1:		/* Edge - clear */
		new &= ~mask;
		break;
	case 3:		/* Level - set */
		new |= mask;
		break;
	}

	if (old == new)
		return;

	printk(PREFIX "setting ELCR to %04x (from %04x)\n", new, old);
	outb(new, 0x4d0);
	outb(new >> 8, 0x4d1);
}

int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
	*irq = gsi_to_irq(gsi);

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_gsi_to_irq);

static int acpi_register_gsi_pic(struct device *dev, u32 gsi,
				 int trigger, int polarity)
{
#ifdef CONFIG_PCI
	/*
	 * Make sure all (legacy) PCI IRQs are set as level-triggered.
	 */
	if (trigger == ACPI_LEVEL_SENSITIVE)
		eisa_set_level_irq(gsi);
#endif

	return gsi;
}

static int acpi_register_gsi_ioapic(struct device *dev, u32 gsi,
				    int trigger, int polarity)
{
	return gsi;
}

int (*__acpi_register_gsi)(struct device *dev, u32 gsi,
			   int trigger, int polarity) = acpi_register_gsi_pic;

/*
 * success: return IRQ number (>=0)
 * failure: return < 0
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int trigger, int polarity)
{
	unsigned int irq;
	unsigned int plat_gsi = gsi;

	plat_gsi = (*__acpi_register_gsi)(dev, gsi, trigger, polarity);

	irq = gsi_to_irq(plat_gsi);

	return irq;
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

void acpi_unregister_gsi(u32 gsi)
{
}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);

void __init acpi_set_irq_model_pic(void)
{
	acpi_irq_model = ACPI_IRQ_MODEL_PIC;
	__acpi_register_gsi = acpi_register_gsi_pic;
	acpi_ioapic = 0;
}

void __init acpi_set_irq_model_ioapic(void)
{
	acpi_irq_model = ACPI_IRQ_MODEL_IOAPIC;
	__acpi_register_gsi = acpi_register_gsi_ioapic;
	acpi_ioapic = 1;
}

/*
 *  ACPI based hotplug support for CPU
 */
#ifdef CONFIG_ACPI_HOTPLUG_CPU
#include <acpi/processor.h>

static void __cpuinit acpi_map_cpu2node(acpi_handle handle, int cpu, int physid)
{
#ifdef CONFIG_ACPI_NUMA
	int nid;

	nid = acpi_get_node(handle);
	if (nid == -1 || !node_online(nid))
		return;
	set_apicid_to_node(physid, nid);
	numa_set_node(cpu, nid);
#endif
}

static int __cpuinit _acpi_map_lsapic(acpi_handle handle, int *pcpu)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct acpi_madt_local_apic *lapic;
	cpumask_var_t tmp_map, new_map;
	u8 physid;
	int cpu;
	int retval = -ENOMEM;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_MAT", NULL, &buffer)))
		return -EINVAL;

	if (!buffer.length || !buffer.pointer)
		return -EINVAL;

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length < sizeof(*lapic)) {
		kfree(buffer.pointer);
		return -EINVAL;
	}

	lapic = (struct acpi_madt_local_apic *)obj->buffer.pointer;

	if (lapic->header.type != ACPI_MADT_TYPE_LOCAL_APIC ||
	    !(lapic->lapic_flags & ACPI_MADT_ENABLED)) {
		kfree(buffer.pointer);
		return -EINVAL;
	}

	physid = lapic->id;

	kfree(buffer.pointer);
	buffer.length = ACPI_ALLOCATE_BUFFER;
	buffer.pointer = NULL;
	lapic = NULL;

	if (!alloc_cpumask_var(&tmp_map, GFP_KERNEL))
		goto out;

	if (!alloc_cpumask_var(&new_map, GFP_KERNEL))
		goto free_tmp_map;

	cpumask_copy(tmp_map, cpu_present_mask);
#ifdef CONFIG_X86
	/* BOZO: ?? */
	acpi_register_lapic(physid, ACPI_MADT_ENABLED);
#endif

	/*
	 * If acpi_register_lapic successfully generates a new logical cpu
	 * number, then the following will get us exactly what was mapped
	 */
	cpumask_andnot(new_map, cpu_present_mask, tmp_map);
	if (cpumask_empty(new_map)) {
		printk("Unable to map lapic to logical cpu number\n");
		retval = -EINVAL;
		goto free_new_map;
	}

	acpi_processor_set_pdc(handle);

	cpu = cpumask_first(new_map);
	acpi_map_cpu2node(handle, cpu, physid);

	*pcpu = cpu;
	retval = 0;

free_new_map:
	free_cpumask_var(new_map);
free_tmp_map:
	free_cpumask_var(tmp_map);
out:
	return retval;
}

/* wrapper to silence section mismatch warning */
int __ref acpi_map_lsapic(acpi_handle handle, int *pcpu)
{
	return _acpi_map_lsapic(handle, pcpu);
}
EXPORT_SYMBOL(acpi_map_lsapic);

int acpi_unmap_lsapic(int cpu)
{
#ifdef CONFIG_X86
	/* BOZO: ??? */
	per_cpu(x86_cpu_to_apicid, cpu) = -1;
	set_cpu_present(cpu, false);
	num_processors--;
#endif

	return 0;
}
EXPORT_SYMBOL(acpi_unmap_lsapic);
#endif				/* CONFIG_ACPI_HOTPLUG_CPU */

static int __init acpi_parse_sbf(struct acpi_table_header *table)
{
	struct acpi_table_boot *sb;

	sb = (struct acpi_table_boot *)table;
	if (!sb) {
		printk(KERN_WARNING PREFIX "Unable to map SBF\n");
		return -ENODEV;
	}

	return 0;
}

#ifdef CONFIG_HPET_TIMER
#include <asm/hpet.h>

static struct __initdata resource * hpet_res;

static int __init acpi_parse_hpet(struct acpi_table_header *table)
{
	struct acpi_table_hpet *hpet_tbl;

	hpet_tbl = (struct acpi_table_hpet *)table;
	if (!hpet_tbl) {
		printk(KERN_WARNING PREFIX "Unable to map HPET\n");
		return -ENODEV;
	}

	if (hpet_tbl->address.space_id != ACPI_SPACE_MEM) {
		printk(KERN_WARNING PREFIX "HPET timers must be located in "
		       "memory.\n");
		return -1;
	}

	hpet_address = hpet_tbl->address.address;
	hpet_blockid = hpet_tbl->sequence;

	/*
	 * Some broken BIOSes advertise HPET at 0x0. We really do not
	 * want to allocate a resource there.
	 */
	if (!hpet_address) {
		printk(KERN_WARNING PREFIX
		       "HPET id: %#x base: %#lx is invalid\n",
		       hpet_tbl->id, hpet_address);
		return 0;
	}
	printk(KERN_INFO PREFIX "HPET id: %#x base: %#lx\n",
	       hpet_tbl->id, hpet_address);

	/*
	 * Allocate and initialize the HPET firmware resource for adding into
	 * the resource tree during the lateinit timeframe.
	 */
#define HPET_RESOURCE_NAME_SIZE 9
	hpet_res = alloc_bootmem(sizeof(*hpet_res) + HPET_RESOURCE_NAME_SIZE);

	hpet_res->name = (void *)&hpet_res[1];
	hpet_res->flags = IORESOURCE_MEM;
	snprintf((char *)hpet_res->name, HPET_RESOURCE_NAME_SIZE, "HPET %u",
		 hpet_tbl->sequence);

	hpet_res->start = hpet_address;
	hpet_res->end = hpet_address + (1 * 1024) - 1;

	return 0;
}

/*
 * hpet_insert_resource inserts the HPET resources used into the resource
 * tree.
 */
static __init int hpet_insert_resource(void)
{
	if (!hpet_res)
		return 1;

	return insert_resource(&iomem_resource, hpet_res);
}

late_initcall(hpet_insert_resource);

#else
#define	acpi_parse_hpet	NULL
#endif

static int __init acpi_parse_fadt(struct acpi_table_header *table)
{
	return 0;
}

static void __init early_acpi_process_madt(void)
{
}

static void __init acpi_process_madt(void)
{
	return;
}

void set_checksum(u8 *start, int len, u8 *cksum)
{
	u8 newsum, oldsum;
	u8 *p;

	newsum = 0;
	for (p = (u8 *)start; p < (u8 *)(start + len); p++)
		newsum += *p;

	oldsum = *cksum;
	newsum = (u8)(newsum - oldsum);

	*cksum = (u8)(0 - newsum);
}

void __init acpi_arm_blob_relocate(void)
{
	/*
	 * Fortunately, there are only a few tables that need to
	 * have their offsets converted to actual addresses.
	 *
	 * NB: all values in the blob are little-endian.
	 */

	struct acpi_table_rsdp *rp;
	struct acpi_table_xsdt *xp;
	struct acpi_table_fadt *fp;
	phys_addr_t paddress;
	void *vaddress;
	u32 entries;
	u32 ii;
	u64 *tmp;

	if (!acpi_arm_rsdp_info.phys_address && !acpi_arm_rsdp_info.size) {
		printk(KERN_ERR "(E) ACPI: failed to find rsdp info\n");
		return;
	}

	paddress = acpi_arm_rsdp_info.phys_address;
	paddress += ACPI_BLOB_HEADER_SIZE;
	vaddress = phys_to_virt(paddress);

	/* fixups for the rsdp */
	rp = (struct acpi_table_rsdp *)vaddress;
	if (rp->rsdt_physical_address)
		rp->rsdt_physical_address += paddress;
	if (rp->xsdt_physical_address)
		rp->xsdt_physical_address += paddress;
	set_checksum((u8 *)rp, rp->length, &(rp->checksum));

	/* fixups for the xsdt */
	vaddress = phys_to_virt(rp->xsdt_physical_address);
	xp = (struct acpi_table_xsdt *)vaddress;
	entries = xp->header.length - sizeof (struct acpi_table_header);
	entries /= 8;		/* length is in bytes */
	tmp = (u64 *)(&(xp->table_offset_entry[0]));
	for (ii = 0; ii < entries; ii++)
		*tmp++ += paddress;
	set_checksum((u8 *)xp, xp->header.length, &(xp->header.checksum));

	/* fixups for the fadt */
	vaddress = phys_to_virt(xp->table_offset_entry[0]);
	fp = (struct acpi_table_fadt *)vaddress;
	if (fp->facs)
		fp->facs += paddress;
	if (fp->dsdt)
		fp->dsdt += paddress;
	if (fp->Xfacs)
		fp->Xfacs += paddress;
	if (fp->Xdsdt)
		fp->Xdsdt += paddress;

	/* Always fix up the checksums since we've changed bits. */
	set_checksum((u8 *)fp, fp->header.length, &(fp->header.checksum));
}

/*
 * ========== OLD COMMENTS FROM x86 =================================
 * acpi_boot_table_init() and acpi_boot_init()
 *  called from setup_arch(), always.
 *	1. checksums all tables
 *	2. enumerates lapics
 *	3. enumerates io-apics
 *
 * acpi_table_init() is separate to allow reading SRAT without
 * other side effects.
 *
 * side effects of acpi_boot_init:
 *	acpi_lapic = 1 if LAPIC found
 *	acpi_ioapic = 1 if IOAPIC found
 *	if (acpi_lapic && acpi_ioapic) smp_found_config = 1;
 *	if acpi_blacklisted() acpi_disabled = 1;
 *	acpi_irq_model=...
 *	...
 * ==================================================================
 *
 * We have to approach this a little different on ARMv7.  We are
 * passed in an ACPI blob and we really have no idea where in RAM
 * it will be located.  So, what should have been the physical
 * addresses of other tables cannot really be hardcoded into the
 * tables.  What we will do is put an offset in the blob that is
 * the offset from the beginning of the RSDP structure.  However,
 * what that means is that we have to unpack the blob and do a
 * bit of fixup work on the offsets to turn them into kernel
 * virtual addresses so we can pass them on for later use.
 */

void __init acpi_boot_table_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		return;

	/*
	 * Fix up the addresses in the ACPI we've loaded
	 * in.  The blob has them as offsets and we need
	 * actual addresses.
	 */
	acpi_arm_blob_relocate();

	/*
	 * Initialize the ACPI boot-time table parser.
	 */
	if (acpi_table_init()) {
		disable_acpi();
		return;
	}

	acpi_table_parse(ACPI_SIG_BOOT, acpi_parse_sbf);
}

int __init early_acpi_boot_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		return 1;

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	early_acpi_process_madt();

	return 0;
}

int __init acpi_boot_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		return 1;

	acpi_table_parse(ACPI_SIG_BOOT, acpi_parse_sbf);

	/*
	 * set sci_int and PM timer address
	 */
	acpi_table_parse(ACPI_SIG_FADT, acpi_parse_fadt);

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	acpi_process_madt();

	acpi_table_parse(ACPI_SIG_HPET, acpi_parse_hpet);

	return 0;
}

static int __init parse_acpi(char *arg)
{
	if (!arg)
		return -EINVAL;

	/* "acpi=off" disables both ACPI table parsing and interpreter */
	if (strcmp(arg, "off") == 0) {
		disable_acpi();
	}
	/* acpi=force to over-ride black-list */
	else if (strcmp(arg, "force") == 0) {
		acpi_force = 1;
		acpi_disabled = 0;
	}
	/* acpi=strict disables out-of-spec workarounds */
	else if (strcmp(arg, "strict") == 0) {
		acpi_strict = 1;
	}
	/* acpi=rsdt use RSDT instead of XSDT */
	else if (strcmp(arg, "rsdt") == 0) {
		acpi_rsdt_forced = 1;
	}
	/* "acpi=noirq" disables ACPI interrupt routing */
	else if (strcmp(arg, "noirq") == 0) {
		acpi_noirq_set();
	}
	/* "acpi=copy_dsdt" copys DSDT */
	else if (strcmp(arg, "copy_dsdt") == 0) {
		acpi_gbl_copy_dsdt_locally = 1;
	} else {
		/* Core will printk when we return error. */
		return -EINVAL;
	}
	return 0;
}
early_param("acpi", parse_acpi);

/* FIXME: Using pci= for an ACPI parameter is a travesty. */
static int __init parse_pci(char *arg)
{
	if (arg && strcmp(arg, "noacpi") == 0)
		acpi_disable_pci();
	return 0;
}
early_param("pci", parse_pci);

int __init acpi_mps_check(void)
{
	return 0;
}

static int __init setup_acpi_sci(char *s)
{
	if (!s)
		return -EINVAL;
	if (!strcmp(s, "edge"))
		acpi_sci_flags =  ACPI_MADT_TRIGGER_EDGE |
			(acpi_sci_flags & ~ACPI_MADT_TRIGGER_MASK);
	else if (!strcmp(s, "level"))
		acpi_sci_flags = ACPI_MADT_TRIGGER_LEVEL |
			(acpi_sci_flags & ~ACPI_MADT_TRIGGER_MASK);
	else if (!strcmp(s, "high"))
		acpi_sci_flags = ACPI_MADT_POLARITY_ACTIVE_HIGH |
			(acpi_sci_flags & ~ACPI_MADT_POLARITY_MASK);
	else if (!strcmp(s, "low"))
		acpi_sci_flags = ACPI_MADT_POLARITY_ACTIVE_LOW |
			(acpi_sci_flags & ~ACPI_MADT_POLARITY_MASK);
	else
		return -EINVAL;
	return 0;
}
early_param("acpi_sci", setup_acpi_sci);

int __acpi_acquire_global_lock(unsigned int *lock)
{
	unsigned int old, new, val;
	do {
		old = *lock;
		new = (((old & ~0x3) + 2) + ((old >> 1) & 0x1));
		val = cmpxchg(lock, old, new);
	} while (unlikely (val != old));
	return (new < 3) ? -1 : 0;
}

int __acpi_release_global_lock(unsigned int *lock)
{
	unsigned int old, new, val;
	do {
		old = *lock;
		new = old & ~0x3;
		val = cmpxchg(lock, old, new);
	} while (unlikely (val != old));
	return old & 0x1;
}

