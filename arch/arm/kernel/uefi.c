/*
 * Based on Unified Extensible Firmware Interface Specification version 2.3.1
 *
 * Copyright (C) 2013-2014  Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/efi.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/cacheflush.h>
#include <asm/idmap.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>
#include <asm/uefi.h>

struct efi_memory_map memmap;

static efi_runtime_services_t *runtime;

static phys_addr_t uefi_system_table;
static phys_addr_t uefi_boot_mmap;
static u32 uefi_boot_mmap_size;
static u32 uefi_mmap_desc_size;
static u32 uefi_mmap_desc_ver;

/*
 * If you want to wire up a debugger and debug the UEFI side, set to 0.
 */
#define DISCARD_UNUSED_REGIONS 1

/*
 * If you need to (temporarily) support buggy firmware, set to 0.
 */
#define DISCARD_BOOT_SERVICES_REGIONS 1

static int uefi_debug __initdata;
static int __init uefi_debug_setup(char *str)
{
	uefi_debug = 1;

	return 0;
}
early_param("uefi_debug", uefi_debug_setup);

static int __init uefi_systab_init(void)
{
	efi_char16_t *c16;
	char vendor[100] = "unknown";
	int i, retval;

	efi.systab = early_memremap(uefi_system_table,
				    sizeof(efi_system_table_t));

	/*
	 * Verify the UEFI System Table
	 */
	if (efi.systab == NULL)
		panic("Whoa! Can't find UEFI system table.\n");
	if (efi.systab->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		panic("Whoa! UEFI system table signature incorrect\n");
	if ((efi.systab->hdr.revision >> 16) == 0)
		pr_warn("Warning: UEFI system table version %d.%02d, expected 2.30 or greater\n",
			efi.systab->hdr.revision >> 16,
			efi.systab->hdr.revision & 0xffff);

	/* Show what we know for posterity */
	c16 = early_memremap(efi.systab->fw_vendor, sizeof(vendor));
	if (c16) {
		for (i = 0; i < (int) sizeof(vendor) - 1 && *c16; ++i)
			vendor[i] = c16[i];
		vendor[i] = '\0';
	}

	pr_info("UEFI v%u.%.02u by %s\n",
		efi.systab->hdr.revision >> 16,
		efi.systab->hdr.revision & 0xffff, vendor);

	retval = efi_config_init(NULL);
	if (retval == 0)
		set_bit(EFI_CONFIG_TABLES, &efi.flags);

	early_memunmap(c16, sizeof(vendor));
	early_memunmap(efi.systab,  sizeof(efi_system_table_t));

	return retval;
}

static __init int is_discardable_region(efi_memory_desc_t *md)
{
	if (md->attribute & EFI_MEMORY_RUNTIME)
		return 0;

	switch (md->type) {
	case EFI_CONVENTIONAL_MEMORY:
		return 1;
	case EFI_BOOT_SERVICES_CODE:
	case EFI_BOOT_SERVICES_DATA:
		return DISCARD_BOOT_SERVICES_REGIONS;
	/* Keep tables around for any future kexec operations */
	case EFI_ACPI_MEMORY_NVS:
	case EFI_ACPI_RECLAIM_MEMORY:
		return 0;
	/* Preserve */
	case EFI_RESERVED_TYPE:
		return 0;
	}

	return DISCARD_UNUSED_REGIONS;
}

static __initdata struct {
	u32 type;
	const char *name;
}  memory_type_name_map[] = {
	{EFI_RESERVED_TYPE, "reserved"},
	{EFI_LOADER_CODE, "loader code"},
	{EFI_LOADER_DATA, "loader data"},
	{EFI_BOOT_SERVICES_CODE, "boot services code"},
	{EFI_BOOT_SERVICES_DATA, "boot services data"},
	{EFI_RUNTIME_SERVICES_CODE, "runtime services code"},
	{EFI_RUNTIME_SERVICES_DATA, "runtime services data"},
	{EFI_CONVENTIONAL_MEMORY, "conventional memory"},
	{EFI_UNUSABLE_MEMORY, "unusable memory"},
	{EFI_ACPI_RECLAIM_MEMORY, "ACPI reclaim memory"},
	{EFI_ACPI_MEMORY_NVS, "ACPI memory nvs"},
	{EFI_MEMORY_MAPPED_IO, "memory mapped I/O"},
	{EFI_MEMORY_MAPPED_IO_PORT_SPACE, "memory mapped I/O port space"},
	{EFI_PAL_CODE, "pal code"},
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

static void memmap_init(void)
{
	efi_memory_desc_t *md;
	int i = 0;

	if (uefi_debug)
		pr_info("Processing UEFI memory map:\n");

	memmap.map = early_memremap(uefi_boot_mmap, uefi_boot_mmap_size);
	if (!memmap.map)
		return;

	memmap.map_end = memmap.map + uefi_boot_mmap_size;
	memmap.nr_map = 0;

	for_each_efi_memory_desc(&memmap, md) {
		pr_info("  %8llu pages @ %016llx (%s)\n",
			md->num_pages, md->phys_addr,
			memory_type_name_map[md->type].name);
		if (md->attribute & EFI_MEMORY_WB) {
			if (is_discardable_region(md)) {
				arm_add_memory(md->phys_addr,
					       md->num_pages * EFI_PAGE_SIZE);
				i++;
			}
		}
		memmap.nr_map++;
	}

	if (uefi_debug)
		pr_info("%d memory regions added.\n", i);

	remove_sections(uefi_boot_mmap, uefi_boot_mmap_size);

	early_memunmap(memmap.map, uefi_boot_mmap_size);

	set_bit(EFI_MEMMAP, &efi.flags);
}

void __init uefi_init(void)
{
	struct efi_fdt_params params;

	uefi_debug = 1;

	/* Grab UEFI information placed in FDT by stub */
	if (!efi_get_fdt_params(&params, uefi_debug))
		return;

	uefi_system_table = params.system_table;

	uefi_boot_mmap = params.mmap;
	uefi_boot_mmap_size = params.mmap_size;
	uefi_mmap_desc_size = params.desc_size;
	uefi_mmap_desc_ver = params.desc_ver;
	memmap.desc_size = uefi_mmap_desc_size;
	memmap.map_end = memmap.map + params.mmap_size;
	if (uefi_boot_mmap > UINT_MAX) {
		pr_err("UEFI memory map located above 4GB - unusable!");
		return;
	}

	if (uefi_systab_init() < 0)
		return;

	memmap_init();

	set_bit(EFI_BOOT, &efi.flags);
}

/*
 * Disable instrrupts, enable idmap and disable caches.
 */
static void __init phys_call_prologue(void)
{
	local_irq_disable();

	outer_disable();

	idmap_prepare();
}

/*
 * Restore original memory map and re-enable interrupts.
 */
static void __init phys_call_epilogue(void)
{
	static struct mm_struct *mm = &init_mm;

	/* Restore original memory mapping */
	cpu_switch_mm(mm->pgd, mm);

	local_flush_bp_all();
	local_flush_tlb_all();

	outer_resume();

	local_irq_enable();
}

static int __init remap_region(efi_memory_desc_t *md, int entry)
{
	efi_memory_desc_t *region;
	u32 va;
	u64 paddr;
	u64 size;

	region = memmap.map + entry * memmap.desc_size;
	*region = *md;
	paddr = region->phys_addr;
	size = region->num_pages << EFI_PAGE_SHIFT;

	/*
	 * Map everything writeback-capable as coherent memory,
	 * anything else as device.
	 */
	if (md->attribute & EFI_MEMORY_WB)
		va = (u32)uefi_remap(paddr, size);
	else
		va = (u32)uefi_ioremap(paddr, size);
	if (!va)
		return 0;
	region->virt_addr = va;

	if (uefi_debug)
		pr_info("  %016llx-%016llx => 0x%08x : (%s)\n",
			paddr, paddr + size - 1, va,
			md->attribute &  EFI_MEMORY_WB ? "WB" : "I/O");

	return 1;
}

static int __init remap_regions(void)
{
	void *p;
	efi_memory_desc_t *md;
	int mapped_regions;

	memmap.phys_map = uefi_remap(uefi_boot_mmap, uefi_boot_mmap_size);
	if (!memmap.phys_map)
		return 0;

	memmap.map_end = memmap.phys_map + uefi_boot_mmap_size;
	memmap.desc_size = uefi_mmap_desc_size;
	memmap.desc_version = uefi_mmap_desc_ver;

	/* Allocate space for the physical region map */
	memmap.map = kzalloc(memmap.nr_map * memmap.desc_size, GFP_ATOMIC);
	if (!memmap.map)
		return 0;

	mapped_regions = 0;
	for (p = memmap.phys_map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if (is_discardable_region(md))
			continue;

		if (!remap_region(p, mapped_regions++))
			return 0;
	}

	memmap.map_end = memmap.map + mapped_regions * memmap.desc_size;
	efi.memmap = &memmap;

	uefi_unmap(memmap.phys_map);
	memmap.phys_map = efi_lookup_mapped_addr(uefi_boot_mmap);
	efi.systab = efi_lookup_mapped_addr(uefi_system_table);
	if (efi.systab)
		set_bit(EFI_SYSTEM_TABLES, &efi.flags);
	/*
	 * efi.systab->runtime is a 32-bit pointer to something guaranteed by
	 * the UEFI specification to be 1:1 mapped in a 4GB address space.
	 */
	runtime = efi_lookup_mapped_addr((u32)efi.systab->runtime);

	return 1;
}


/*
 * This function switches the UEFI runtime services to virtual mode.
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
	uefi_phys_call_t *phys_set_map;
	efi_status_t status;

	phys_call_prologue();

	phys_set_map = (void *)(unsigned long)virt_to_phys(uefi_phys_call);

	/* Called with caches disabled, returns with caches enabled */
	status = phys_set_map(efi.set_virtual_address_map,
			      PAGE_OFFSET - PHYS_OFFSET,
			      memory_map_size, descriptor_size,
			      descriptor_version, dsc);

	phys_call_epilogue();

	return status;
}

/*
 * Called explicitly from init/mm.c
 */
void __init efi_enter_virtual_mode(void)
{
	efi_status_t status;
	u32 mmap_phys_addr;

	if (!efi_enabled(EFI_BOOT)) {
		pr_info("UEFI services will not be available.\n");
		return;
	}

	pr_info("Remapping and enabling UEFI services.\n");

	/* Map the regions we memblock_remove:d earlier into kernel
	   address space */
	if (!remap_regions()) {
		pr_info("Failed to remap UEFI regions - runtime services will not be available.\n");
		return;
	}

	/* Call SetVirtualAddressMap with the physical address of the map */
	efi.set_virtual_address_map = runtime->set_virtual_address_map;

	/*
	 * __virt_to_phys() takes an unsigned long and returns a phys_addr_t
	 * memmap.phys_map is a void *
	 * The gymnastics below makes this compile validly with/without LPAE.
	 */
	mmap_phys_addr = __virt_to_phys((u32)memmap.map);
	memmap.phys_map = (void *)mmap_phys_addr;

	status = phys_set_virtual_address_map(memmap.nr_map * memmap.desc_size,
					      memmap.desc_size,
					      memmap.desc_version,
					      memmap.phys_map);
	if (status != EFI_SUCCESS) {
		pr_info("Failed to set UEFI virtual address map!\n");
		return;
	}

	/* Set up function pointers for efivars */
	efi.get_variable = runtime->get_variable;
	efi.get_next_variable = runtime->get_next_variable;
	efi.set_variable = runtime->set_variable;
	efi.set_virtual_address_map = NULL;

	set_bit(EFI_RUNTIME_SERVICES, &efi.flags);
}
