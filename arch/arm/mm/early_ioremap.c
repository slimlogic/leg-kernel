/*
 * early_ioremap() support for ARM
 *
 * Based on existing support in arch/x86/mm/ioremap.c
 *
 * Restrictions: currently only functional before paging_init()
 */

#include <linux/init.h>
#include <linux/io.h>

#include <asm/fixmap.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include <asm/mach/map.h>

static int __initdata early_ioremap_debug;

static int __init early_ioremap_debug_setup(char *str)
{
	early_ioremap_debug = 1;

	return 0;
}
early_param("early_ioremap_debug", early_ioremap_debug_setup);

static pte_t __initdata bm_pte[PTRS_PER_PTE] __aligned(PTRS_PER_PTE * sizeof(pte_t));
static __initdata int after_paging_init;

static inline pmd_t * __init early_ioremap_pmd(unsigned long addr)
{
	unsigned int index = pgd_index(addr);
	pgd_t *pgd = cpu_get_pgd() + index;
	pud_t *pud = pud_offset(pgd, addr);
	pmd_t *pmd = pmd_offset(pud, addr);

	return pmd;
}

static inline pte_t * __init early_ioremap_pte(unsigned long addr)
{
	return &bm_pte[pte_index(addr)];
}

static unsigned long slot_virt[FIX_BTMAPS_SLOTS] __initdata;

void __init early_ioremap_init(void)
{
	pmd_t *pmd;
	int i;
	u64 desc;

	if (early_ioremap_debug)
		pr_info("early_ioremap_init()\n");

	for (i = 0; i < FIX_BTMAPS_SLOTS; i++) {
		slot_virt[i] = __fix_to_virt(FIX_BTMAP_BEGIN + NR_FIX_BTMAPS*i);
		if (early_ioremap_debug)
			pr_info("  %lu byte slot @ 0x%08x\n",
				NR_FIX_BTMAPS * PAGE_SIZE, (u32)slot_virt[i]);
	}

	pmd = early_ioremap_pmd(fix_to_virt(FIX_BTMAP_BEGIN));
	desc = *pmd;
	memset(bm_pte, 0, sizeof(bm_pte));

	pmd_populate_kernel(NULL, pmd, bm_pte);
	desc = *pmd;

	BUILD_BUG_ON((__fix_to_virt(FIX_BTMAP_BEGIN) >> PMD_SHIFT)
		     != (__fix_to_virt(FIX_BTMAP_END) >> PMD_SHIFT));

	if (pmd != early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END))) {
		WARN_ON(1);
		pr_warn("pmd %p != %p\n",
			pmd, early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END)));
		pr_warn("fix_to_virt(FIX_BTMAP_BEGIN): %08lx\n",
			fix_to_virt(FIX_BTMAP_BEGIN));
		pr_warn("fix_to_virt(FIX_BTMAP_END):   %08lx\n",
			fix_to_virt(FIX_BTMAP_END));
		pr_warn("FIX_BTMAP_END:       %lu\n", FIX_BTMAP_END);
		pr_warn("FIX_BTMAP_BEGIN:     %d\n",  FIX_BTMAP_BEGIN);
	}
}

void __init early_ioremap_reset(void)
{
	after_paging_init = 1;
}

static void __init __early_set_fixmap(unsigned long idx,
				      phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *pte;
	u64 desc;

	if (idx >= FIX_KMAP_END) {
		BUG();
		return;
	}
	pte = early_ioremap_pte(addr);

	if (pgprot_val(flags))
		set_pte_at(NULL, 0xfff00000, pte,
			   pfn_pte(phys >> PAGE_SHIFT, flags));
	else
		pte_clear(NULL, addr, pte);
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	desc = *pte;
}

static inline void __init early_set_fixmap(unsigned long idx,
					   phys_addr_t phys, pgprot_t prot)
{
	__early_set_fixmap(idx, phys, prot);
}

static inline void __init early_clear_fixmap(unsigned long idx)
{
	__early_set_fixmap(idx, 0, __pgprot(0));
}

static void __iomem *prev_map[FIX_BTMAPS_SLOTS] __initdata;
static unsigned long prev_size[FIX_BTMAPS_SLOTS] __initdata;

static void __init __iomem *
__early_remap(resource_size_t phys_addr, unsigned long size, pgprot_t prot)
{
	unsigned long offset;
	resource_size_t last_addr;
	unsigned int nrpages;
	unsigned long idx;
	int i, slot;

	slot = -1;
	for (i = 0; i < FIX_BTMAPS_SLOTS; i++) {
		if (!prev_map[i]) {
			slot = i;
			break;
		}
	}

	if (slot < 0) {
		pr_info("early_iomap(%08llx, %08lx) not found slot\n",
			(u64)phys_addr, size);
		WARN_ON(1);
		return NULL;
	}

	if (early_ioremap_debug) {
		pr_info("early_ioremap(%08llx, %08lx) [%d] => ",
			(u64)phys_addr, size, slot);
	}

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr) {
		WARN_ON(1);
		return NULL;
	}

	prev_size[slot] = size;
	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;

	/*
	 * Mappings have to fit in the FIX_BTMAP area.
	 */
	nrpages = size >> PAGE_SHIFT;
	if (nrpages > NR_FIX_BTMAPS) {
		WARN_ON(1);
		return NULL;
	}

	/*
	 * Ok, go for it..
	 */
	idx = FIX_BTMAP_BEGIN + slot * NR_FIX_BTMAPS;
	while (nrpages > 0) {
		early_set_fixmap(idx, phys_addr, prot);
		phys_addr += PAGE_SIZE;
		idx++;
		--nrpages;
	}
	if (early_ioremap_debug)
		pr_cont("%08lx + %08lx\n", offset, slot_virt[slot]);

	prev_map[slot] = (void __iomem *)(offset + slot_virt[slot]);
	return prev_map[slot];
}

/* Remap an IO device */
void __init __iomem *
early_remap(resource_size_t phys_addr, unsigned long size, u32 prot)
{
	if (after_paging_init) {
		WARN_ON(1);
		return NULL;
	}

	/*
	 * PAGE_KERNEL depends on not-yet-initialised variables.
	 * We don't care about coherency or executability of early_ioremap
	 * pages anyway.
	 */
	prot |= L_PTE_YOUNG | L_PTE_PRESENT;
	return __early_remap(phys_addr, size, prot);
}


void __init early_iounmap(void __iomem *addr, unsigned long size)
{
	unsigned long virt_addr;
	unsigned long offset;
	unsigned int nrpages;
	unsigned long idx;
	int i, slot;

	if (after_paging_init) {
		WARN_ON(1);
		return;
	}

	slot = -1;
	for (i = 0; i < FIX_BTMAPS_SLOTS; i++) {
		if (prev_map[i] == addr) {
			slot = i;
			break;
		}
	}

	if (slot < 0) {
		pr_info("early_iounmap(%p, %08lx) not found slot\n",
			addr, size);
		WARN_ON(1);
		return;
	}

	if (prev_size[slot] != size) {
		pr_info("early_iounmap(%p, %08lx) [%d] size not consistent %08lx\n",
			addr, size, slot, prev_size[slot]);
		WARN_ON(1);
		return;
	}

	if (early_ioremap_debug)
		pr_info("early_iounmap(%p, %08lx) [%d]\n", addr, size, slot);

	virt_addr = (unsigned long)addr;
	if (virt_addr < fix_to_virt(FIX_BTMAP_BEGIN)) {
		WARN_ON(1);
		return;
	}
	offset = virt_addr & ~PAGE_MASK;
	nrpages = PAGE_ALIGN(offset + size) >> PAGE_SHIFT;

	idx = FIX_BTMAP_BEGIN + slot * NR_FIX_BTMAPS;
	while (nrpages > 0) {
		early_clear_fixmap(idx);
		idx++;
		--nrpages;
	}
	prev_map[slot] = NULL;
}
