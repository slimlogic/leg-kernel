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

/*
 * We never plan to use RSDT on arm/arm64 as its deprecated in spec but this
 * variable is still required by the ACPI core
 */
u32 acpi_rsdt_forced;

int acpi_disabled;
EXPORT_SYMBOL(acpi_disabled);

/* available_cpus here means enabled cpu in MADT */
int available_cpus;

/* Map logic cpu id to physical APIC id.
 * APIC = GIC cpu interface on ARM
 */
volatile int arm_cpu_to_apicid[NR_CPUS] = { [0 ... NR_CPUS-1] = -1 };
int boot_cpu_apic_id = -1;

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
static u64 acpi_lapic_addr __initdata;

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

static int __init acpi_parse_madt(struct acpi_table_header *table)
{
	struct acpi_table_madt *madt = NULL;

	madt = (struct acpi_table_madt *)table;
	if (!madt) {
		pr_warn(PREFIX "Unable to map MADT\n");
		return -ENODEV;
	}

	if (madt->address) {
		acpi_lapic_addr = (u64) madt->address;

		pr_info(PREFIX "Local APIC address 0x%08x\n", madt->address);
	}

	return 0;
}

/* Local APIC = GIC cpu interface on ARM */
static void __cpuinit acpi_register_lapic(int id, u8 enabled)
{
	int cpu;

	if (id >= MAX_LOCAL_APIC) {
		pr_info(PREFIX "skipped apicid that is too big\n");
		return;
	}

	total_cpus++;
	if (!enabled)
		return;

	available_cpus++;

	/* allocate a logic cpu id for the new comer */
	if (boot_cpu_apic_id == id) {
		/*
		 * boot_cpu_init() already hold bit 0 in cpu_present_mask
		 * for BSP, no need to allocte again.
		 */
		cpu = 0;
	} else {
		cpu = cpumask_next_zero(-1, cpu_present_mask);
	}

	/* map the logic cpu id to APIC id */
	arm_cpu_to_apicid[cpu] = id;

	set_cpu_present(cpu, true);
	set_cpu_possible(cpu, true);
}

static int __init
acpi_parse_gic(struct acpi_subtable_header *header, const unsigned long end)
{
	struct acpi_madt_generic_interrupt *processor = NULL;

	processor = (struct acpi_madt_generic_interrupt *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/*
	 * We need to register disabled CPU as well to permit
	 * counting disabled CPUs. This allows us to size
	 * cpus_possible_map more accurately, to permit
	 * to not preallocating memory for all NR_CPUS
	 * when we use CPU hotplug.
	 */
	acpi_register_lapic(processor->gic_id,
			    processor->flags & ACPI_MADT_ENABLED);

	return 0;
}

static int __init
acpi_parse_gic_distributor(struct acpi_subtable_header *header,
				const unsigned long end)
{
	struct acpi_madt_generic_distributor *distributor = NULL;

	distributor = (struct acpi_madt_generic_distributor *)header;

	if (BAD_MADT_ENTRY(distributor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TODO: handle with the base_address and irq_base for irq system */

	return 0;
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

void __init acpi_set_irq_model_gic(void)
{
	acpi_irq_model = ACPI_IRQ_MODEL_GIC;
	__acpi_register_gsi = acpi_register_gsi_ioapic;
	acpi_ioapic = 1;
}

static int __initdata setup_possible_cpus = -1;
static int __init _setup_possible_cpus(char *str)
{
	get_option(&str, &setup_possible_cpus);
	return 0;
}
early_param("possible_cpus", _setup_possible_cpus);

/*
 * cpu_possible_mask should be static, it cannot change as cpu's
 * are onlined, or offlined. The reason is per-cpu data-structures
 * are allocated by some modules at init time, and dont expect to
 * do this dynamically on cpu arrival/departure.
 * cpu_present_mask on the other hand can change dynamically.
 * In case when cpu_hotplug is not compiled, then we resort to current
 * behaviour, which is cpu_possible == cpu_present.
 * - Ashok Raj
 *
 * Three ways to find out the number of additional hotplug CPUs:
 * - If the BIOS specified disabled CPUs in ACPI/mptables use that.
 * - The user can overwrite it with possible_cpus=NUM
 * - Otherwise don't reserve additional CPUs.
 * We do this because additional CPUs waste a lot of memory.
 * -AK
 */
__init void prefill_possible_map(void)
{
	int i;
	int possible, disabled_cpus;

	disabled_cpus = total_cpus - available_cpus;

	if (setup_possible_cpus == -1) {
		if (disabled_cpus > 0)
			setup_possible_cpus = disabled_cpus;
		else
			setup_possible_cpus = 0;
	}

	possible = available_cpus + setup_possible_cpus;

	pr_info("SMP: the system is limited to %d CPUs\n", nr_cpu_ids);

	/*
	 * On armv8 foundation model --cores=4 lets nr_cpu_ids=4, so we can't
	 * get possible map correctly when more than 4 APIC entries in MADT.
	 */
	if (possible > nr_cpu_ids)
		possible = nr_cpu_ids;

	pr_info("SMP: Allowing %d CPUs, %d hotplug CPUs\n",
		possible, max((possible - available_cpus), 0));

	for (i = 0; i < possible; i++)
		set_cpu_possible(i, true);
	for (; i < NR_CPUS; i++)
		set_cpu_possible(i, false);
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
	acpi_register_lapic(physid, ACPI_MADT_ENABLED);

	/*
	 * If acpi_register_lapic successfully generates a new logical cpu
	 * number, then the following will get us exactly what was mapped
	 */
	cpumask_andnot(new_map, cpu_present_mask, tmp_map);
	if (cpumask_empty(new_map)) {
		pr_err("Unable to map lapic to logical cpu number\n");
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
	arm_cpu_to_apicid[cpu] = -1;
	set_cpu_present(cpu, false);
	available_cpus--;

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

/* Local APIC = GIC cpu interface on ARM */
static int __init acpi_parse_madt_lapic_entries(void)
{
	int count;

	/*
	 * do a partial walk of MADT to determine how many CPUs
	 * we have including disabled CPUs
	 */
	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
				      acpi_parse_gic, MAX_LOCAL_APIC);

	if (!count) {
		pr_err(PREFIX "No LAPIC entries present\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return -ENODEV;
	} else if (count < 0) {
		pr_err(PREFIX "Error parsing LAPIC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

#ifdef CONFIG_SMP
	if (available_cpus == 0) {
		pr_info(PREFIX "Found 0 CPUS; assuming 1\n");
		/* FIXME: should be the real GIC id read from hardware */
		arm_cpu_to_apicid[available_cpus] = 0;
		available_cpus = 1;	/* We've got at least one of these */
	}
#endif
	/* Make boot-up look pretty */
	pr_info("%d CPUs available, %d CPUs total\n", available_cpus,
	       total_cpus);

	return 0;
}

static int __init acpi_parse_fadt(struct acpi_table_header *table)
{
	return 0;
}

/*
 * Parse IOAPIC related entries in MADT
 * returns 0 on success, < 0 on error
 * IOAPIC = GIC distributor on ARM
 */
static int __init acpi_parse_madt_ioapic_entries(void)
{
	int count;

	/*
	 * ACPI interpreter is required to complete interrupt setup,
	 * so if it is off, don't enumerate the io-apics with ACPI.
	 * If MPS is present, it will handle them,
	 * otherwise the system will stay in PIC mode
	 */
	if (acpi_disabled || acpi_noirq)
		return -ENODEV;

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
			acpi_parse_gic_distributor, MAX_IO_APICS);

	if (!count) {
		pr_err(PREFIX "No IOAPIC entries present\n");
		return -ENODEV;
	} else if (count < 0) {
		pr_err(PREFIX "Error parsing IOAPIC entry\n");
		return count;
	}

	return 0;
}

static void __init early_acpi_process_madt(void)
{
	/* should I introduce CONFIG_ARM_LOCAL_APIC like x86 does? */
	acpi_table_parse(ACPI_SIG_MADT, acpi_parse_madt);
}

static void __init acpi_process_madt(void)
{
	/* should I introduce CONFIG_ARM_LOCAL_APIC like x86 does? */
	int error;

	if (!acpi_table_parse(ACPI_SIG_MADT, acpi_parse_madt)) {

		/*
		 * Parse MADT LAPIC entries
		 */
		error = acpi_parse_madt_lapic_entries();
		if (!error) {
			acpi_lapic = 1;

			/*
			 * Parse MADT IO-APIC entries
			 */
			error = acpi_parse_madt_ioapic_entries();
			if (!error)
				acpi_set_irq_model_gic();
		}
	}

	/*
	 * ACPI supports both logical (e.g. Hyper-Threading) and physical
	 * processors, where MPS only supports physical.
	 */
	if (acpi_lapic && acpi_ioapic)
		pr_info("Using ACPI (MADT) for SMP configuration "
		       "information\n");
	else if (acpi_lapic)
		pr_info("Using ACPI for processor (LAPIC) "
		       "configuration information\n");

	return;
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
	/* acpi=strict disables out-of-spec workarounds */
	else if (strcmp(arg, "strict") == 0) {
		acpi_strict = 1;
	}
	return 0;
}
early_param("acpi", parse_acpi);

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
