/*
 *  ARM/ARM64 Specific Low-Level ACPI Boot Support
 *
 *  Copyright (C) 2013, Al Stone <al.stone@linaro.org>
 *  Copyright (C) 2013, Graeme Gregory <graeme.gregory@linaro.org>
 *  Copyright (C) 2013, Hanjun Guo <hanjun.guo@linaro.org>
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/bootmem.h>
#include <linux/smp.h>

#include <asm/pgtable.h>
#include <asm/cputype.h>

/*
 * We never plan to use RSDT on arm/arm64 as its deprecated in spec but this
 * variable is still required by the ACPI core
 */
u32 acpi_rsdt_forced;

int acpi_noirq;			/* skip ACPI IRQ initialization */
int acpi_strict;
int acpi_disabled;
EXPORT_SYMBOL(acpi_disabled);

int acpi_pci_disabled;		/* skip ACPI PCI scan and IRQ initialization */
EXPORT_SYMBOL(acpi_pci_disabled);

/*
 * Local interrupt controller address,
 * GIC cpu interface base address on ARM/ARM64
 */
static u64 acpi_lapic_addr __initdata;

/* available_cpus here means enabled cpu in MADT */
static int available_cpus;

/* Map logic cpu id to physical GIC id (physical CPU id). */
int arm_cpu_to_apicid[NR_CPUS] = { [0 ... NR_CPUS-1] = -1 };
static int boot_cpu_apic_id = -1;

/* Parked Address in ACPI GIC structure */
static u64 parked_address[NR_CPUS];

#define BAD_MADT_ENTRY(entry, end) (					\
	(!entry) || (unsigned long)entry + sizeof(*entry) > end ||	\
	((struct acpi_subtable_header *)entry)->length < sizeof(*entry))

#define PREFIX			"ACPI: "

/* FIXME: this function should be moved to topology.c when it is ready */
void arch_fix_phys_package_id(int num, u32 slot)
{
	return;
}
EXPORT_SYMBOL_GPL(arch_fix_phys_package_id);

/*
 * Since we're on ARM, the default interrupt routing model
 * clearly has to be GIC.
 */
enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_GIC;

static unsigned int gsi_to_irq(unsigned int gsi)
{
	int irq = irq_find_mapping(NULL, gsi);

	return irq;
}

/*
 * __acpi_map_table() will be called before page_init(), so early_ioremap()
 * or early_memremap() should be called here.
 *
 * FIXME: early_io/memremap()/early_iounmap() are not upstream yet on ARM64,
 * just wait for Mark Salter's patchset accepted by mainline
 */
char *__init __acpi_map_table(unsigned long phys, unsigned long size)
{
	if (!phys || !size)
		return NULL;

	return early_memremap(phys, size);
}

void __init __acpi_unmap_table(char *map, unsigned long size)
{
	if (!map || !size)
		return;

	early_iounmap(map, size);
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

/*
 * GIC structures on ARM are somthing like Local APIC structures on x86,
 * which means GIC cpu interfaces for GICv2/v3. Every GIC structure in
 * MADT table represents a cpu in the system.
 *
 * GIC distributor structures are somthing like IOAPIC on x86. GIC can
 * be initialized with information in this structure.
 *
 * Please refer to chapter5.2.12.14/15 of ACPI 5.0
 */

/**
 * acpi_register_gic_cpu_interface - register a gic cpu interface and
 * generates a logic cpu number
 * @id: gic cpu interface id to register
 * @enabled: this cpu is enabled or not
 *
 * Returns the logic cpu number which maps to the gic cpu interface
 */
static int acpi_register_gic_cpu_interface(int id, u8 enabled)
{
	int cpu;

	if (id >= MAX_GIC_CPU_INTERFACE) {
		pr_info(PREFIX "skipped apicid that is too big\n");
		return -EINVAL;
	}

	total_cpus++;
	if (!enabled)
		return -EINVAL;

	if (available_cpus >=  NR_CPUS) {
		pr_warn(PREFIX "NR_CPUS limit of %d reached,"
		" Processor %d/0x%x ignored.\n", NR_CPUS, total_cpus, id);
		return -EINVAL;
	}

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

	return cpu;
}

static int __init
acpi_parse_gic(struct acpi_subtable_header *header, const unsigned long end)
{
	struct acpi_madt_generic_interrupt *processor = NULL;
	int cpu;

	processor = (struct acpi_madt_generic_interrupt *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/*
	 * As ACPI 5.0 said, the 64-bit physical address in GIC struct at
	 * which the processor can access this GIC. If provided, the
	 * ???Local Interruptp controller Address??? field in  the MADT
	 * is ignored by OSPM.
	 */
	if (processor->base_address)
		acpi_lapic_addr = processor->base_address;

	/*
	 * We need to register disabled CPU as well to permit
	 * counting disabled CPUs. This allows us to size
	 * cpus_possible_map more accurately, to permit
	 * to not preallocating memory for all NR_CPUS
	 * when we use CPU hotplug.
	 */
	cpu = acpi_register_gic_cpu_interface(processor->gic_id,
			processor->flags & ACPI_MADT_ENABLED);

	/*
	 * We need the parked address for SMP initialization with
	 * spin-table enable method
	 */
	if (cpu >= 0 && processor->parked_address)
		parked_address[cpu] = processor->parked_address;

	return 0;
}

/*
 * Hard code here, we can not get memory size from MADT (but FDT does),
 * this size can be refered from GICv2 spec
 */
#define GIC_DISTRIBUTOR_MEMORY_SIZE (SZ_4K)
#define GIC_CPU_INTERFACE_MEMORY_SIZE (SZ_8K)

/*
 * ACPI 5.0 only provides information of GICC and GICD, use them
 * to initialize GIC
 */
static int __init
acpi_parse_gic_distributor(struct acpi_subtable_header *header,
				const unsigned long end)
{
	struct acpi_madt_generic_distributor *distributor = NULL;
	void __iomem *dist_base, *cpu_base;

	if (!IS_ENABLED(CONFIG_ARM_GIC))
		return;

	distributor = (struct acpi_madt_generic_distributor *)header;

	if (BAD_MADT_ENTRY(distributor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* GIC is initialised after page_init(), no need for early_ioremap */
	dist_base = ioremap(distributor->base_address,
				GIC_DISTRIBUTOR_MEMORY_SIZE);
	if (!dist_base) {
		pr_warn(PREFIX "unable to map gic dist registers\n");
		return -ENOMEM;
	}

	/*
	 * acpi_lapic_addr is stored in acpi_parse_madt() or acpi_parse_gic(),
	 * so we can use it here for GIC init
	 */
	cpu_base = ioremap(acpi_lapic_addr, GIC_CPU_INTERFACE_MEMORY_SIZE);
	if (!cpu_base) {
		iounmap(dist_base);
		pr_warn(PREFIX "unable to map gic cpu registers\n");
		return -ENOMEM;
	}

	gic_init(distributor->gic_id, -1, dist_base, cpu_base);

	return 0;
}

/*
 * Parse GIC cpu interface related entries in MADT
 * returns 0 on success, < 0 on error
 */
static int __init acpi_parse_madt_gic_entries(void)
{
	int count;

	/*
	 * do a partial walk of MADT to determine how many CPUs
	 * we have including disabled CPUs
	 */
	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
				acpi_parse_gic, MAX_GIC_CPU_INTERFACE);

	if (!count) {
		pr_err(PREFIX "No GIC entries present\n");
		return -ENODEV;
	} else if (count < 0) {
		pr_err(PREFIX "Error parsing GIC entry\n");
		return count;
	}

#ifdef CONFIG_SMP
	if (available_cpus == 0) {
		pr_info(PREFIX "Found 0 CPUs; assuming 1\n");
		arm_cpu_to_apicid[available_cpus] =
			read_cpuid_mpidr() & MPIDR_HWID_BITMASK;
		available_cpus = 1;	/* We've got at least one of these */
	}
#endif

	/* Make boot-up look pretty */
	pr_info("%d CPUs available, %d CPUs total\n", available_cpus,
		total_cpus);

	return 0;
}

/* Parked Address in ACPI GIC structure can be used as cpu release addr */
int acpi_get_cpu_release_address(int cpu, u64 *release_address)
{
	if (!release_address || !parked_address[cpu])
		return -EINVAL;

	*release_address = parked_address[cpu];

	return 0;
}

/*
 * Parse GIC distributor related entries in MADT
 * returns 0 on success, < 0 on error
 */
static int __init acpi_parse_madt_gic_distributor_entries(void)
{
	int count;

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
			acpi_parse_gic_distributor, MAX_GIC_DISTRIBUTOR);

	if (!count) {
		pr_err(PREFIX "No GIC distributor entries present\n");
		return -ENODEV;
	} else if (count < 0) {
		pr_err(PREFIX "Error parsing GIC distributor entry\n");
		return count;
	}

	return 0;
}

int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
	*irq = gsi_to_irq(gsi);

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_gsi_to_irq);

int acpi_isa_irq_to_gsi(unsigned isa_irq, u32 *gsi)
{
	return -1;
}

int acpi_register_ioapic(acpi_handle handle, u64 phys_addr, u32 gsi_base)
{
	/* TBD */
	return -EINVAL;
}
EXPORT_SYMBOL(acpi_register_ioapic);

int acpi_unregister_ioapic(acpi_handle handle, u32 gsi_base)
{
	/* TBD */
	return -EINVAL;
}
EXPORT_SYMBOL(acpi_unregister_ioapic);

/*
 * success: return IRQ number (>0)
 * failure: return =< 0
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int trigger, int polarity)
{
	unsigned int irq;
	unsigned int irq_type;

	/*
	 * ACPI have no bindings to indicate SPI or PPI, so we
	 * use different mappings from DT in ACPI.
	 *
	 * For FDT
	 * PPI interrupt: in the range [0, 15];
	 * SPI interrupt: in the range [0, 987];
	 *
	 * For ACPI, GSI should be unique so using
	 * identity mapping for hwirq:
	 * PPI interrupt: in the range [16, 31];
	 * SPI interrupt: in the range [32, 1019];
	 */

	if (trigger == ACPI_EDGE_SENSITIVE &&
				polarity == ACPI_ACTIVE_LOW)
		irq_type = IRQ_TYPE_EDGE_FALLING;
	else if (trigger == ACPI_EDGE_SENSITIVE &&
				polarity == ACPI_ACTIVE_HIGH)
		irq_type = IRQ_TYPE_EDGE_RISING;
	else if (trigger == ACPI_LEVEL_SENSITIVE &&
				polarity == ACPI_ACTIVE_LOW)
		irq_type = IRQ_TYPE_LEVEL_LOW;
	else if (trigger == ACPI_LEVEL_SENSITIVE &&
				polarity == ACPI_ACTIVE_HIGH)
		irq_type = IRQ_TYPE_LEVEL_HIGH;
	else
		irq_type = IRQ_TYPE_NONE;

	/*
	 * Since only one GIC is supported in ACPI 5.0, we can
	 * create mapping refer to the default domain
	 */
	irq = irq_create_mapping(NULL, gsi);
	if (!irq)
		return irq;

	/* Set irq type if specified and different than the current one */
	if (irq_type != IRQ_TYPE_NONE &&
		irq_type != irq_get_trigger_type(irq))
		irq_set_irq_type(irq, irq_type);
	return irq;
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

void acpi_unregister_gsi(u32 gsi)
{
}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);

static int __init acpi_parse_fadt(struct acpi_table_header *table)
{
	return 0;
}

static void __init early_acpi_process_madt(void)
{
	acpi_table_parse(ACPI_SIG_MADT, acpi_parse_madt);
}

static void __init acpi_process_madt(void)
{
	int error;

	if (!acpi_table_parse(ACPI_SIG_MADT, acpi_parse_madt)) {

		/*
		 * Parse MADT GIC cpu interface entries
		 */
		error = acpi_parse_madt_gic_entries();
		if (!error)
			pr_info("Using ACPI for processor (GIC) configuration information\n");
	}

	return;
}

int __init acpi_gic_init(void)
{
	/*
	 * Parse MADT GIC distributor entries
	 */
	return acpi_parse_madt_gic_distributor_entries();
}

/*
 * To see PCSI is enabled or not.
 *
 * PSCI is not available for ACPI 5.0, return FALSE for now.
 *
 * FIXME: should we introduce early_param("psci", func) for test purpose?
 */
static bool acpi_psci_smp_available(int cpu)
{
	return FALSE;
}

static const char *enable_method[] = {
	"psci",
	"spin-table",
	NULL
};

const char *acpi_get_enable_method(int cpu)
{
	if (acpi_psci_smp_available(cpu))
		return enable_method[0];
	else
		return enable_method[1];
}

/*
 * acpi_boot_table_init() and acpi_boot_init()
 *  called from setup_arch(), always.
 *	1. checksums all tables
 *	2. enumerates lapics
 *	3. enumerates io-apics
 *
 * acpi_table_init() is separated to allow reading SRAT without
 * other side effects.
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
}

int __init early_acpi_boot_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		return -ENODEV;

	/* Get the boot CPU's GIC cpu interface id before MADT parsing */
	boot_cpu_apic_id = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;

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
		return -ENODEV;

	acpi_table_parse(ACPI_SIG_FADT, acpi_parse_fadt);

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	acpi_process_madt();

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

/*
 * acpi_suspend_lowlevel() - save kernel state and suspend.
 *
 * TBD when ARM/ARM64 starts to support suspend...
 */
int (*acpi_suspend_lowlevel)(void);
