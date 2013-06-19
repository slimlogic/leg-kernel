/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 2.3.1
 *
 * Copyright (C) 2013 Linaro Ltd.
 *
 */

#include <linux/efi.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/cacheflush.h>
#include <asm/efi.h>
#include <asm/idmap.h>
#include <asm/tlbflush.h>

struct efi_memory_map memmap;

static efi_runtime_services_t *runtime;

static phys_addr_t efi_system_table;
static phys_addr_t efi_boot_mmap;
static u32 efi_boot_mmap_size;
static u32 efi_mmap_desc_size;
static u32 efi_mmap_desc_ver;

static unsigned long arm_efi_facility;

/* Default memory map descriptor information */
#define DESC_SIZE 48
#define DESC_VER   1

/* If you're planning to wire up a debugger and debug the UEFI side ... */
#undef KEEP_ALL_REGIONS
#define KEEP_BOOT_SERVICES_REGIONS

/*
 * Returns 1 if 'facility' is enabled, 0 otherwise.
 */
int efi_enabled(int facility)
{
	return test_bit(facility, &arm_efi_facility) != 0;
}
EXPORT_SYMBOL(efi_enabled);

static int __initdata uefi_debug;
static int __init uefi_debug_setup(char *str)
{
	uefi_debug = 1;

	return 0;
}
early_param("uefi_debug", uefi_debug_setup);

static int __init fdt_find_efi_params(unsigned long node, const char *uname,
				      int depth, void *data)
{
	unsigned long len;
	__be32 *prop;

	if (depth != 1 ||
	    (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0))
		return 0;

	pr_info("Getting EFI parameters from FDT.\n");

	prop = of_get_flat_dt_prop(node, "linux,efi-system-table", &len);
	if (!prop)
		return 0;
	efi_system_table = of_read_ulong(prop, len/4);

	prop = of_get_flat_dt_prop(node, "linux,efi-mmap", &len);
	if (!prop)
		return 0;
	efi_boot_mmap = (u32) prop;
	efi_boot_mmap_size = len;

	prop = of_get_flat_dt_prop(node, "linux,efi-mmap-desc-size", NULL);
	if (prop)
		efi_mmap_desc_size  = of_read_ulong(prop, 1);
	else
		efi_mmap_desc_size = DESC_SIZE;

	prop = of_get_flat_dt_prop(node, "linux,efi-mmap-desc-ver", NULL);
	if (prop)
		efi_mmap_desc_ver  = of_read_ulong(prop, 1);
	else
		efi_mmap_desc_ver = DESC_VER;

	if (uefi_debug) {
		pr_info("  EFI system table @ 0x%08x\n",
			(unsigned int) efi_system_table);
		pr_info("  EFI mmap @ 0x%08x\n",
			(unsigned int) efi_boot_mmap);
		pr_info("  EFI mmap size = 0x%08x\n",
			(unsigned int) efi_boot_mmap_size);
		pr_info("  EFI mmap descriptor size = 0x%08x\n",
			(unsigned int) efi_mmap_desc_size);
		pr_info("  EFI mmap descriptor version = 0x%08x\n",
			(unsigned int) efi_mmap_desc_ver);
	}

	return 1;
}

static int __init uefi_init(void)
{
	efi_char16_t *c16;
	char vendor[100] = "unknown";
	int i, retval;

	efi.systab = early_ioremap(efi_system_table,
				   sizeof(efi_system_table_t));

	/*
	 * Verify the EFI Table
	 */
	if (efi.systab == NULL)
		panic("Whoa! Can't find EFI system table.\n");
	if (efi.systab->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		panic("Whoa! EFI system table signature incorrect\n");
	if ((efi.systab->hdr.revision >> 16) == 0)
		pr_warn("Warning: EFI system table version %d.%02d, expected 1.00 or greater\n",
			efi.systab->hdr.revision >> 16,
			efi.systab->hdr.revision & 0xffff);

	/* Show what we know for posterity */
	c16 = (efi_char16_t *)early_ioremap(efi.systab->fw_vendor,
					    sizeof(vendor));
	if (c16) {
		for (i = 0; i < (int) sizeof(vendor) - 1 && *c16; ++i)
			vendor[i] = c16[i];
		vendor[i] = '\0';
	}

	pr_info("EFI v%u.%.02u by %s\n",
		efi.systab->hdr.revision >> 16,
		efi.systab->hdr.revision & 0xffff, vendor);

	retval = efi_config_init(NULL);
	if(retval == 0)
		set_bit(EFI_CONFIG_TABLES, &arm_efi_facility);

	pr_info(" retval=0x%08x\n", retval);

	early_iounmap(c16, sizeof(vendor));
	early_iounmap(efi.systab,  sizeof(efi_system_table_t));

	return retval;
}

static __init int is_discardable_region(efi_memory_desc_t *md)
{
#ifdef KEEP_ALL_REGIONS
	return 0;
#endif

	if (md->attribute & EFI_MEMORY_RUNTIME)
		return 0;

	switch (md->type) {
#ifdef KEEP_BOOT_SERVICES_REGIONS
	case EFI_BOOT_SERVICES_CODE:
	case EFI_BOOT_SERVICES_DATA:
#endif
	/* Keep tables around for any future kexec operations */
	case EFI_ACPI_RECLAIM_MEMORY:
		return 0;
	}

	return 1;
}

static __initdata struct {
	u32 type;
	const char *name;
}  memory_type_name_map[] = {
	{EFI_RESERVED_TYPE, "EFI reserved"},
	{EFI_LOADER_CODE, "EFI loader code"},
	{EFI_LOADER_DATA, "EFI loader data"},
	{EFI_BOOT_SERVICES_CODE, "EFI boot services code"},
	{EFI_BOOT_SERVICES_DATA, "EFI boot services data"},
	{EFI_RUNTIME_SERVICES_CODE, "EFI runtime services code"},
	{EFI_RUNTIME_SERVICES_DATA, "EFI runtime services data"},
	{EFI_CONVENTIONAL_MEMORY, "EFI conventional memory"},
	{EFI_UNUSABLE_MEMORY, "EFI unusable memory"},
	{EFI_ACPI_RECLAIM_MEMORY, "EFI ACPI reclaim memory"},
	{EFI_ACPI_MEMORY_NVS, "EFI ACPI memory nvs"},
	{EFI_MEMORY_MAPPED_IO, "EFI memory mapped I/O"},
	{EFI_MEMORY_MAPPED_IO_PORT_SPACE, "EFI memory mapped I/O port space"},
	{EFI_PAL_CODE, "EFI pal code"},
	{EFI_MAX_MEMORY_TYPE, NULL},
};

static __init void remove_sections(phys_addr_t addr, unsigned long size)
{
	unsigned long section_offset;
	unsigned long num_sections;

	section_offset = addr - (addr & SECTION_MASK);
	num_sections = size / SECTION_SIZE;
	if (size % SECTION_SIZE)
		num_sections++;

	memblock_remove(addr - section_offset, num_sections * SECTION_SIZE);
}

static __init int remove_regions(void)
{
	efi_memory_desc_t *md;
	int count = 0;
	void *p;

	memmap.phys_map = (void *) (u32) efi_boot_mmap;
	memmap.desc_size = efi_mmap_desc_size;
	memmap.desc_version = efi_mmap_desc_ver;
	memmap.map_end = (void *) memmap.phys_map + efi_boot_mmap_size;
	memmap.nr_map = 0;

	if (uefi_debug)
		pr_info("Processing EFI memory map:\n");

	for (p = memmap.phys_map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if (is_discardable_region(md))
			continue;

		if (uefi_debug)
			pr_info("  %8llu pages @ %016llx (%s)\n",
				md->num_pages, md->phys_addr,
				memory_type_name_map[md->type].name);

		if (md->type != EFI_MEMORY_MAPPED_IO) {
			remove_sections(md->phys_addr,
					md->num_pages * PAGE_SIZE);
			count++;
		}
		memmap.nr_map++;
	}

	if (uefi_debug)
		pr_info("%d regions preserved.\n", memmap.nr_map);

	return 0;
}

int __init efi_memblock_arm_reserve_range(void)
{
	/* Grab system table location out of FDT */
	of_scan_flat_dt(fdt_find_efi_params, NULL);

	if (!efi_system_table || !efi_boot_mmap || !efi_boot_mmap_size)
		return 0;

	set_bit(EFI_BOOT, &arm_efi_facility);

	uefi_init();

	remove_regions();

	return 0;
}

/*
 * Disable instrrupts, enable idmap and disable caches.
 */
static void __init phys_call_prologue(void)
{
	local_irq_disable();

	/* Take out a flat memory mapping. */
	setup_mm_for_reboot();

	/* Clean and invalidate caches */
	flush_cache_all();

	/* Turn off caching */
	cpu_proc_fin();

	/* Push out any further dirty data, and ensure cache is empty */
	flush_cache_all();
}

/*
 * Restore original memory map and re-enable interrupts.
 */
static void __init phys_call_epilogue(void)
{
	static struct mm_struct *mm = &init_mm;

	/* Restore original memory mapping */
	cpu_switch_mm(mm->pgd, mm);

	/* Flush branch predictor and TLBs */
	local_flush_bp_all();
#ifdef CONFIG_CPU_HAS_ASID
	local_flush_tlb_all();
#endif

	local_irq_enable();
}

/*
 * Memory map was previously extracted from flattened device tree for
 * reserving regions. Now we need to grab it from the unflattened tree
 * in order to access it for remapping purposes.
 */
static void __init *get_runtime_mmap(void)
{
	struct device_node *node;
	int len;
	void *map;

	node = of_find_node_by_path("/chosen");
	if (node != NULL) {
		map = (void *) of_get_property(node, "linux,efi-mmap", &len);

		if (len != efi_boot_mmap_size) {
			pr_info(" EFI mmap size mismatch!\n");
			map = NULL;
		}
	} else {
		map = NULL;
	}

	return map;
}

static int __init remap_region(efi_memory_desc_t *md, efi_memory_desc_t *entry)
{
	u64 va;
	u64 paddr;
	u64 size;

	*entry = *md;
	paddr = entry->phys_addr;
	size = entry->num_pages << EFI_PAGE_SHIFT;

	/*
	 * Map everything writeback-capable as coherent memory,
	 * anything else as device.
	 */
	if (md->attribute & EFI_MEMORY_WB)
		va = (u64)((u32)efi_remap(paddr, size) & 0xffffffffUL);
	else
		va = (u64)((u32)efi_ioremap(paddr, size) & 0xffffffffUL);
	if (!va)
		return 0;
	entry->virt_addr = va;

	if (uefi_debug)
		pr_info("  %016llx-%016llx => 0x%08x : (%s)\n",
			paddr, paddr + size - 1, (u32)va,
			md->attribute &  EFI_MEMORY_WB ? "WB" : "I/O");

	return 1;
}

static int __init remap_regions(void)
{
	void *p, *next;
	efi_memory_desc_t *md;

	memmap.phys_map = get_runtime_mmap();
	if (!memmap.phys_map)
		return 0;
	memmap.map_end = (void *)memmap.phys_map + efi_boot_mmap_size;

	/* Allocate space for the physical region map */
	memmap.map = kzalloc(memmap.nr_map * memmap.desc_size, GFP_KERNEL);
	if (!memmap.map)
		return 0;

	next = memmap.map;
	for (p = memmap.phys_map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if (is_discardable_region(md))
			continue;

		if (!remap_region(p, next))
			return 0;

		next += memmap.desc_size;
	}

	memmap.map_end = next;
	efi.memmap = &memmap;

	efi.systab = efi_lookup_mapped_addr(efi_system_table);
	if (efi.systab)
		set_bit(EFI_SYSTEM_TABLES, &arm_efi_facility);
	/*
	 * efi.systab->runtime is a 32-bit pointer to something guaranteed by
	 * the UEFI specification to be 1:1 mapped in a 4GB address space.
	 */
	runtime = efi_lookup_mapped_addr((u32)efi.systab->runtime);

	return 1;
}


/*
 * This function switches the EFI runtime services to virtual mode.
 * This operation must be performed only once in the system's lifetime,
 * including any kecec calls.
 *
 * This must be done with a 1:1 mapping. The current implementation
 * resolves this by disabling the MMU.
 */
efi_status_t  __init phys_set_virtual_address_map(u32 memory_map_size,
						  u32 descriptor_size,
						  u32 descriptor_version,
						  efi_memory_desc_t *dsc)
{
	efi_phys_call_t *phys_set_map;
	efi_status_t status;

	phys_call_prologue();

	phys_set_map = (void *)(unsigned long)virt_to_phys(efi_phys_call);

	/* Called with caches disabled, returns with caches enabled */
	status = phys_set_map(memory_map_size, descriptor_size,
			      descriptor_version, dsc,
			      efi.set_virtual_address_map);

	phys_call_epilogue();

	return status;
}

/*
 * Called explicitly from init/mm.c
 */
void __init efi_enter_virtual_mode(void)
{
	efi_status_t status;

	if (!efi_enabled(EFI_BOOT)) {
		pr_info("EFI services will not be available.\n");
		return;
	} else {
		pr_info("Remapping and enabling EFI services.\n");
	}

	/* Map the regions we memblock_remove:d earlier into kernel
	   address space */
	if (!remap_regions()) {
		pr_info("Failed to remap EFI regions - runtime services will not be available.\n");
		return;
	}

	/* Call SetVirtualAddressMap with the physical address of the map */
	efi.set_virtual_address_map =
		(efi_set_virtual_address_map_t *)
		runtime->set_virtual_address_map;
	memmap.phys_map =
		(efi_memory_desc_t *)(u32) __virt_to_phys((u32)memmap.map);

	status = phys_set_virtual_address_map(memmap.nr_map * memmap.desc_size,
					      memmap.desc_size,
					      memmap.desc_version,
					      memmap.phys_map);

	if (status != EFI_SUCCESS) {
		pr_info("Failed to set EFI virtual address map!\n");
		return;
	}

	/* Set up function pointers for efivars */
	efi.get_variable = (efi_get_variable_t *)runtime->get_variable;
	efi.get_next_variable =
		(efi_get_next_variable_t *)runtime->get_next_variable;
	efi.set_variable = (efi_set_variable_t *)runtime->set_variable;
	set_bit(EFI_RUNTIME_SERVICES, &arm_efi_facility);
}
