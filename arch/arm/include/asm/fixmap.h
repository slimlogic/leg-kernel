#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#include <linux/bug.h>

/*
 * Nothing too fancy for now.
 *
 * On ARM we already have well known fixed virtual addresses imposed by
 * the architecture such as the vector page which is located at 0xffff0000,
 * therefore a second level page table is already allocated covering
 * 0xfff00000 upwards.
 *
 * The cache flushing code in proc-xscale.S uses the virtual area between
 * 0xfffe0000 and 0xfffeffff.
 */

#define FIXADDR_START		0xfff00000UL
#define FIXADDR_END		0xfffe0000UL
#define FIXADDR_TOP		(FIXADDR_END - PAGE_SIZE)

enum fixed_addresses {
	FIX_KMAP_BEGIN,
	FIX_KMAP_END = (FIXADDR_TOP - FIXADDR_START) >> PAGE_SHIFT,
	__end_of_fixed_addresses
/*
 * 224 temporary boot-time mappings, used by early_ioremap(),
 * before ioremap() is functional.
 *
 * (P)re-using the FIXADDR region, which is used for highmem
 * later on, and statically aligned to 1MB.
 */
#define NR_FIX_BTMAPS		32
#define FIX_BTMAPS_SLOTS	7
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)
#define FIX_BTMAP_END		FIX_KMAP_BEGIN
#define FIX_BTMAP_BEGIN		(FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1)
};

#define FIXMAP_PAGE_NORMAL (L_PTE_MT_WRITEBACK | L_PTE_YOUNG | L_PTE_PRESENT)
#define FIXMAP_PAGE_IO (L_PTE_MT_DEV_NONSHARED | L_PTE_YOUNG | L_PTE_PRESENT)

extern void __early_set_fixmap(enum fixed_addresses idx,
			       phys_addr_t phys, pgprot_t flags);

#include <asm-generic/fixmap.h>

#endif /* _ASM_FIXMAP_H */
