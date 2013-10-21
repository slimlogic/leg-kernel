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
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>

/* shouldn't need this, but some firmware images are broken... */
#define KEEP_BOOT_SERVICES_REGIONS

#define efi_early_remap(a, b) \
	((__force void *)early_ioremap((a), (b)))
#define efi_early_unmap(a, b) \
	early_iounmap((void __iomem *)(a), (b))

struct efi_memory_map memmap;

static efi_runtime_services_t *runtime;

static u64 efi_system_table;

static unsigned long arm_efi_facility;

/* Default memory map descriptor information */
#define DESC_SIZE 48
#define DESC_VER   1

/*
 * Returns 1 if 'facility' is enabled, 0 otherwise.
 */
int efi_enabled(int facility)
{
	return test_bit(facility, &arm_efi_facility) != 0;
}
EXPORT_SYMBOL(efi_enabled);

static int uefi_debug __initdata;
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
	if (!prop) {
		pr_info("No EFI system table in FDT\n");
		return 0;
	}
	efi_system_table = of_read_ulong(prop, len/4);

	prop = of_get_flat_dt_prop(node, "linux,efi-mmap", &len);
	if (!prop || !len) {
		pr_info("No EFI memmap in FDT\n");
		return 0;
	}
	memmap.map = (void *)prop;
	memmap.map_end = memmap.map + len;
	memmap.phys_map = (void *)virt_to_phys(memmap.map);

	prop = of_get_flat_dt_prop(node, "linux,efi-mmap-desc-size", &len);
	if (prop)
		memmap.desc_size = of_read_ulong(prop, len/4);
	else
		memmap.desc_size = DESC_SIZE;

	prop = of_get_flat_dt_prop(node, "linux,efi-mmap-desc-ver", &len);
	if (prop)
		memmap.desc_version = of_read_ulong(prop, len/4);
	else
		memmap.desc_version = DESC_VER;

	if (uefi_debug) {
		pr_info("  EFI system table @ %p\n", (void *)efi_system_table);
		pr_info("  EFI mmap @ %p-%p\n", memmap.phys_map,
			memmap.map_end);
		pr_info("  EFI mmap descriptor size = 0x%lx\n",
			memmap.desc_size);
		pr_info("  EFI mmap descriptor version = 0x%lx\n",
			memmap.desc_version);
	}

	return 1;
}

static void * __init __efi_alloc(u64 size)
{
	phys_addr_t p;
	void *v;

	size = PAGE_ALIGN(size);
	p = memblock_alloc(size, PAGE_SIZE);
	if (!p)
		return NULL;
	v = phys_to_virt(p);
	memset(v, 0, size);
	return v;
}

#define PGD_END  (&swapper_pg_dir[sizeof(idmap_pg_dir)/sizeof(pgd_t)])
#ifndef CONFIG_SMP
#define PTE_FLAGS	(PTE_TYPE_PAGE | PTE_AF)
#define PMD_FLAGS	(PMD_TYPE_SECT | PMD_SECT_AF)
#else
#define PTE_FLAGS	(PTE_TYPE_PAGE | PTE_AF | PTE_SHARED)
#define PMD_FLAGS	(PMD_TYPE_SECT | PMD_SECT_AF | PMD_SECT_S)
#endif

#ifdef CONFIG_ARM64_64K_PAGES
#define MM_MMUFLAGS	(PTE_ATTRINDX(MT_NORMAL) | PTE_FLAGS)
#else
#define MM_MMUFLAGS	(PMD_ATTRINDX(MT_NORMAL) | PMD_FLAGS)
#endif

#ifdef CONFIG_ARM64_64K_PAGES
static void __init __memory_idmap(unsigned long addr, unsigned long len)
{
	unsigned long end, next, p;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/* page align it */
	len = PAGE_ALIGN(len + (addr & ~PAGE_MASK));
	addr &= PAGE_MASK;

	end = addr + len;
	pgd = &idmap_pg_dir[pgd_index(addr)];

	do {
		next = pgd_addr_end(addr, end);
		if (pgd >= PGD_END)
			continue;

		pud = pud_offset(pgd, addr);
		pmd = pmd_offset(pud, addr);

		if (pmd_none(*pmd)) {
			pte = __efi_alloc(PAGE_SIZE);
			if (!pte)
				continue;
			set_pmd(pmd, __pmd(__pa(pte) | PMD_TYPE_TABLE));
		}

		for (p = addr; p < next; p += PAGE_SIZE) {
			pte = pte_offset_kernel(pmd, p);
			if (pte_none(*pte))
				set_pte(pte, __pte(p | MM_MMUFLAGS));
		}
	} while (pgd++, addr = next, addr != end);
}
#else
static void __init __memory_idmap(unsigned long addr, unsigned long len)
{
	unsigned long end, next, p;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	/* section align it */
	len = ALIGN(len + (addr & ~SECTION_MASK), SECTION_SIZE);
	addr &= SECTION_MASK;

	end = addr + len;
	pgd = &idmap_pg_dir[pgd_index(addr)];

	do {
		next = pgd_addr_end(addr, end);
		if (pgd >= PGD_END)
			continue;

		pud = pud_offset(pgd, addr);
		if (pud_none(*pud)) {
			pmd = __efi_alloc(PAGE_SIZE);
			if (!pmd)
				continue;
			set_pud(pud, __pud(__pa(pmd) | PMD_TYPE_TABLE));
		}

		for (p = addr; p < next; p += SECTION_SIZE) {
			pmd = pmd_offset(pud, p);

			if (pmd_none(*pmd))
				set_pmd(pmd, __pmd(p | MM_MMUFLAGS));
		}
	} while (pgd++, addr = next, addr != end);
}
#endif

static void __init efi_setup_idmap(void)
{
	struct memblock_region *r;
	efi_memory_desc_t *md;
	unsigned long next, end;

	for_each_memblock(memory, r)
		__memory_idmap(r->base, r->size);

	next = (unsigned long)memmap.map;
	end = (unsigned long)memmap.map_end;

	for (; next < end; next += memmap.desc_size) {
		md = (efi_memory_desc_t *)next;

		if (md->num_pages == 0)
			break;

		if (md->type == EFI_MEMORY_MAPPED_IO)
			continue;

		__memory_idmap(md->phys_addr, md->num_pages << EFI_PAGE_SHIFT);
	}
}

static int __init uefi_init(void)
{
	efi_char16_t *c16;
	char vendor[100] = "unknown";
	int i, retval;

	efi.systab = efi_early_remap(efi_system_table,
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
	c16 = efi_early_remap(efi.systab->fw_vendor,
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
	if (retval == 0)
		set_bit(EFI_CONFIG_TABLES, &arm_efi_facility);

	efi_early_unmap(c16, sizeof(vendor));
	efi_early_unmap(efi.systab,  sizeof(efi_system_table_t));

	return retval;
}

static __init int is_discardable_region(efi_memory_desc_t *md)
{
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
	{EFI_RESERVED_TYPE, "Reserved"},
	{EFI_LOADER_CODE, "Loader Code"},
	{EFI_LOADER_DATA, "Loader Data"},
	{EFI_BOOT_SERVICES_CODE, "Boot Services Code"},
	{EFI_BOOT_SERVICES_DATA, "Boot Services Data"},
	{EFI_RUNTIME_SERVICES_CODE, "Runtime Services Code"},
	{EFI_RUNTIME_SERVICES_DATA, "Runtime Services Data"},
	{EFI_CONVENTIONAL_MEMORY, "Conventional Memory"},
	{EFI_UNUSABLE_MEMORY, "Unusable Memory"},
	{EFI_ACPI_RECLAIM_MEMORY, "ACPI Reclaim Memory"},
	{EFI_ACPI_MEMORY_NVS, "ACPI Memory NVS"},
	{EFI_MEMORY_MAPPED_IO, "Memory Mapped I/O"},
	{EFI_MEMORY_MAPPED_IO_PORT_SPACE, "Memory Mapped I/O Port Space"},
	{EFI_PAL_CODE, "EFI PAL Code"},
	{EFI_MAX_MEMORY_TYPE, NULL},
};

static __init int reserve_regions(void)
{
	void *map;
	efi_memory_desc_t md;
	unsigned long mapsize;
	int num;
	u64 paddr;
	u64 npages;

	memmap.nr_map = 0;

	if (uefi_debug)
		pr_info("Processing EFI memory map:\n");

	mapsize = memmap.map_end - memmap.map;

	for (map = memmap.map, num = mapsize/memmap.desc_size;
	     num-- > 0;
	     map += memmap.desc_size) {

		memcpy(&md, map, sizeof(md));

		/* Some UEFI firmware images terminate with a NULL entry */
		if (md.num_pages == 0)
			break;

		if (is_discardable_region(&md))
			continue;

		if (md.type != EFI_MEMORY_MAPPED_IO) {
			paddr = md.phys_addr;
			npages = md.num_pages;
			memrange_efi_to_native(&paddr, &npages);
			memblock_reserve(paddr, npages << PAGE_SHIFT);
			if (uefi_debug)
				pr_info("  0x%012llx-0x%012llx (%s)\n",
					paddr, npages << PAGE_SHIFT,
					memory_type_name_map[md.type].name);
		}
		memmap.nr_map++;
	}

	if (uefi_debug)
		pr_info("%d EFI regions reserved.\n", memmap.nr_map);

	return 0;
}

void __init efi_init(void)
{
	/* Grab system table location out of FDT */
	if (!of_scan_flat_dt(fdt_find_efi_params, NULL))
		return;

	set_bit(EFI_BOOT, &arm_efi_facility);
	set_bit(EFI_64BIT, &arm_efi_facility);

	uefi_init();

	reserve_regions();
}

static int __init remap_region(efi_memory_desc_t *md, efi_memory_desc_t *entry)
{
	u64 va;
	u64 paddr;
	u64 npages;
	u64 size;

	*entry = *md;
	paddr = entry->phys_addr;
	npages = entry->num_pages;

	memrange_efi_to_native(&paddr, &npages);

	size = npages << PAGE_SHIFT;

	/*
	 * Map everything writeback-capable as coherent memory,
	 * anything else as device.
	 */
	if (md->attribute & EFI_MEMORY_WB) {
		if (memblock_is_memory(paddr))
			va = (u64)phys_to_virt(paddr);
		else
			va = (__force u64)efi_remap(paddr, size);
	} else
		va = (__force u64)efi_ioremap(paddr, size);
	if (!va)
		return 0;
	entry->virt_addr = va;

	if (uefi_debug)
		pr_info("  %p-%p => %p : (%s)\n",
			(void *)paddr, (void *)paddr + size - 1, (void *)va,
			md->attribute & EFI_MEMORY_WB ? "WB" : "I/O");
	return 1;
}

static int __init remap_regions(void)
{
	void *map, *next;
	efi_memory_desc_t md;
	unsigned long mapsize;
	int num;
	u64 addr;

	mapsize = memmap.map_end - memmap.map;
	map = memmap.map;

	/* Allocate space for the physical region map */
	memmap.map = __efi_alloc(memmap.nr_map * memmap.desc_size);
	if (!memmap.map)
		return 0;

	memmap.phys_map = (void *)virt_to_phys(memmap.map);

	next = memmap.map;
	for (num = mapsize/memmap.desc_size;
	     num-- > 0;
	     map += memmap.desc_size) {

		memcpy(&md, map, sizeof(md));

		if (is_discardable_region(&md))
			continue;

		if (!remap_region(&md, next))
			return 0;

		next += memmap.desc_size;
	}

	memmap.map_end = next;
	efi.memmap = &memmap;

	efi.systab = (__force void *)efi_lookup_mapped_addr(efi_system_table);
	if (efi.systab)
		set_bit(EFI_SYSTEM_TABLES, &arm_efi_facility);

	/*
	 * efi.systab->runtime is a pointer to something guaranteed by
	 * the UEFI specification to be 1:1 mapped in a 4GB address space.
	 */
	addr = (u64)efi.systab->runtime;
	runtime = (__force void *)efi_lookup_mapped_addr(addr);

	return 1;
}


/*
 * Called from setup_arch with interrupts disabled.
 */
void __init efi_enter_virtual_mode(void)
{
	efi_status_t status;

	if (!efi_enabled(EFI_BOOT)) {
		pr_info("EFI services will not be available.\n");
		return;
	}
	pr_info("Remapping and enabling EFI services.\n");

	/* Map the regions we reserved earlier */
	if (!remap_regions()) {
		pr_info("Failed to remap EFI regions - runtime services will not be available.\n");
		return;
	}

	/* Call SetVirtualAddressMap with the physical address of the map */
	efi.set_virtual_address_map = runtime->set_virtual_address_map;

	/* boot time idmap_pg_dir is incomplete, so fill in missing parts */
	efi_setup_idmap();

	cpu_switch_mm(idmap_pg_dir, &init_mm);
	flush_tlb_all();
	flush_cache_all();

	status = efi.set_virtual_address_map(memmap.nr_map * memmap.desc_size,
					      memmap.desc_size,
					      memmap.desc_version,
					      memmap.phys_map);
	cpu_set_reserved_ttbr0();
	flush_tlb_all();
	flush_cache_all();

	if (status != EFI_SUCCESS) {
		pr_info("Failed to set EFI virtual address map! [%lx]\n",
			status);
		return;
	}

	pr_info("EFI Virtual address map set\n");

	/* Set up function pointers for efivars */
	efi.get_variable = (efi_get_variable_t *)runtime->get_variable;
	efi.get_next_variable =
		(efi_get_next_variable_t *)runtime->get_next_variable;
	efi.set_variable = (efi_set_variable_t *)runtime->set_variable;
	set_bit(EFI_RUNTIME_SERVICES, &arm_efi_facility);
}
