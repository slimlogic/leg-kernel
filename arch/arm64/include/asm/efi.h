#ifndef _ASM_ARM64_EFI_H
#define _ASM_ARM64_EFI_H

#include <asm/io.h>

#ifdef CONFIG_EFI
extern void efi_init(void);
#else
#define efi_init()
#endif

#define efi_remap(cookie, size) __ioremap((cookie), (size), PAGE_KERNEL_EXEC)
#define efi_ioremap(cookie, size) __ioremap((cookie), (size), \
					    __pgprot(PROT_DEVICE_nGnRE))
#define efi_unmap(cookie) __iounmap((cookie))
#define efi_iounmap(cookie) __iounmap((cookie))

#endif /* _ASM_ARM64_EFI_H */
