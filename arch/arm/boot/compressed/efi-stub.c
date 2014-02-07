/*
 * linux/arch/arm/boot/compressed/efi-stub.c
 *
 * Copyright (C) 2013 Linaro Ltd;  <roy.franz@linaro.org>
 *
 * This file implements the EFI boot stub for the ARM kernel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/efi.h>
#include <libfdt.h>
#include <generated/compile.h>
#include <generated/utsrelease.h>
#include "efi-stub.h"

/*
 * The maximum uncompressed kernel size is 32 MBytes, so we will reserve
 * that for the decompressed kernel.  We have no easy way to tell what
 * the actuall size of code + data the uncompressed kernel will use.
 */
#define MAX_UNCOMP_KERNEL_SIZE	0x02000000

/*
 * The kernel zImage should be located between 32 Mbytes
 * and 128 MBytes from the base of DRAM.  The min
 * address leaves space for a maximal size uncompressed image,
 * and the max address is due to how the zImage decompressor
 * picks a destination address.
 */
#define ZIMAGE_OFFSET_LIMIT	0x08000000
#define MIN_ZIMAGE_OFFSET	MAX_UNCOMP_KERNEL_SIZE
#define MAX_FDT_OFFSET		ZIMAGE_OFFSET_LIMIT

/* Include shared EFI stub code, and required headers. */
#include "../../../../drivers/firmware/efi/efi-stub-helper.c"
#include "../../../../drivers/firmware/efi/fdt.c"
#include "../../../drivers/firmware/efi/arm-stub.c"

static efi_status_t handle_kernel_image(efi_system_table_t *sys_table,
					unsigned long *image_addr,
					unsigned long *image_size,
					unsigned long *reserve_addr,
					unsigned long *reserve_size,
					unsigned long dram_base,
					efi_loaded_image_t *image)
{
	unsigned long nr_pages;
	efi_status_t status;
	/* Use alloc_addr to tranlsate between types */
	efi_physical_addr_t alloc_addr;

	/*
	 * Verify that the DRAM base address is compatible the the ARM
	 * boot protocol, which determines the base of DRAM by masking
	 * off the low 24 bits of the address at which the zImage is
	 * loaded at.  These assumptions are made by the decompressor,
	 * before any memory map is available.
	 */
	if (dram_base & (ZIMAGE_OFFSET_LIMIT - 1)) {
		pr_efi_err(sys_table, "Invalid DRAM base address alignment.\n");
		return EFI_ERROR;
	}

	/*
	 * Reserve memory for the uncompressed kernel image. This is
	 * all that prevents any future allocations from conflicting
	 * with the kernel.  Since we can't tell from the compressed
	 * image how much DRAM the kernel actually uses (due to BSS
	 * size uncertainty) we allocate the maximum possible size.
	 * Do this very early, as prints can cause memory allocations
	 * that may conflict with this.
	 */
	alloc_addr = dram_base;
	*reserve_size = MAX_UNCOMP_KERNEL_SIZE;
	nr_pages = round_up(*reserve_size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
	status = efi_call_phys4(sys_table->boottime->allocate_pages,
				EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
				nr_pages, &alloc_addr);
	if (status != EFI_SUCCESS) {
		*reserve_size = 0;
		pr_efi_err(sys_table, "Unable to allocate memory for uncompressed kernel.\n");
		return status;
	}
	*reserve_addr = alloc_addr;

	/*
	 * Relocate the zImage, if required.  ARM doesn't have a
	 * preferred address, so we set it to 0, as we want to allocate
	 * as low in memory as possible.
	 */
	*image_size = image->image_size;
	status = efi_relocate_kernel(sys_table, image_addr, *image_size,
				     *image_size, 0, 0);
	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table, "Failed to relocate kernel.\n");
		efi_free(sys_table, *reserve_size, *reserve_addr);
		*reserve_size = 0;
		return status;
	}

	/*
	 * Check to see if we were able to allocate memory low enough
	 * in memory.  The kernel determines the base of DRAM from the
	 * address at which the zImage is loaded.
	 */
	if (*image_addr + *image_size > dram_base + ZIMAGE_OFFSET_LIMIT) {
		pr_efi_err(sys_table, "Failed to relocate kernel, no low memory available.\n");
		efi_free(sys_table, *reserve_size, *reserve_addr);
		*reserve_size = 0;
		efi_free(sys_table, *image_size, *image_addr);
		*image_size = 0;
		return EFI_ERROR;
	}
	return EFI_SUCCESS;
}
