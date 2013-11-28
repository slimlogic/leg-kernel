#ifndef _ASM_ARM_EFI_H
#define _ASM_ARM_EFI_H

#ifdef CONFIG_EFI
#include <asm/mach/map.h>

extern void uefi_init(void);

typedef efi_status_t uefi_phys_call_t(efi_set_virtual_address_map_t *f,
				      u32 virt_phys_offset,
				      u32 memory_map_size,
				      u32 descriptor_size,
				      u32 descriptor_version,
				      efi_memory_desc_t *dsc);

extern efi_status_t uefi_phys_call(u32, u32, u32, efi_memory_desc_t *,
				   efi_set_virtual_address_map_t *);

#define uefi_remap(cookie, size) __arm_ioremap((cookie), (size), MT_MEMORY_RWX)
#define uefi_ioremap(cookie, size) __arm_ioremap((cookie), (size), MT_DEVICE)
#define uefi_unmap(cookie) __arm_iounmap((cookie))
#define uefi_iounmap(cookie) __arm_iounmap((cookie))

#else
#define uefi_init()
#endif /* CONFIG_EFI */

#endif /* _ASM_ARM_EFI_H */
