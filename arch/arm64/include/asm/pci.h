#ifndef __ASMARM64_PCI_H
#define __ASMARM64_PCI_H

#ifdef __KERNEL__

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

/*
 * The PCI address space does equal the physical memory address space.
 * The networking and block device layers use this boolean for bounce
 * buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     (1)

#endif /* __KERNEL__ */

#endif /* __ASMARM64_PCI_H */
