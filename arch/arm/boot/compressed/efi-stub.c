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
#include "efi-stub.h"

/* EFI function call wrappers.  These are not required for
 * ARM, but wrappers are required for X86 to convert between
 * ABIs.  These wrappers are provided to allow code sharing
 * between X86 and ARM.  Since these wrappers directly invoke the
 * EFI function pointer, the function pointer type must be properly
 * defined, which is not the case for X86  One advantage of this is
 * it allows for type checking of arguments, which is not
 * possible with the X86 wrappers.
 */
#define efi_call_phys0(f)			f()
#define efi_call_phys1(f, a1)			f(a1)
#define efi_call_phys2(f, a1, a2)		f(a1, a2)
#define efi_call_phys3(f, a1, a2, a3)		f(a1, a2, a3)
#define efi_call_phys4(f, a1, a2, a3, a4)	f(a1, a2, a3, a4)
#define efi_call_phys5(f, a1, a2, a3, a4, a5)	f(a1, a2, a3, a4, a5)

/* The maximum uncompressed kernel size is 32 MBytes, so we will reserve
 * that for the decompressed kernel.  We have no easy way to tell what
 * the actuall size of code + data the uncompressed kernel will use.
 */
#define MAX_UNCOMP_KERNEL_SIZE	0x02000000

/* The kernel zImage should be located between 32 Mbytes
 * and 128 MBytes from the base of DRAM.  The min
 * address leaves space for a maximal size uncompressed image,
 * and the max address is due to how the zImage decompressor
 * picks a destination address.
 */
#define ZIMAGE_OFFSET_LIMIT	0x08000000
#define MIN_ZIMAGE_OFFSET	MAX_UNCOMP_KERNEL_SIZE

#define PRINTK_PREFIX		"EFIstub: "

struct fdt_region {
	u64 base;
	u64 size;
};


/* Include shared EFI stub code */
#include "../../../../drivers/firmware/efi/efi-stub-helper.c"


int efi_entry(void *handle, efi_system_table_t *sys_table,
	      unsigned long *zimage_addr)
{
	efi_loaded_image_t *image;
	int status;
	unsigned long nr_pages;
	const struct fdt_region *region;

	void *fdt;
	int err;
	int node;
	unsigned long zimage_size = 0;
	unsigned long dram_base;
	/* addr/point and size pairs for memory management*/
	unsigned long initrd_addr;
	unsigned long initrd_size = 0;
	unsigned long fdt_addr;
	unsigned long fdt_size = 0;
	efi_physical_addr_t kernel_reserve_addr;
	unsigned long kernel_reserve_size = 0;
	char *cmdline_ptr;
	int cmdline_size = 0;

	unsigned long map_size, desc_size;
	u32 desc_ver;
	unsigned long mmap_key;
	efi_memory_desc_t *memory_map;

	unsigned long new_fdt_size;
	unsigned long new_fdt_addr;

	efi_guid_t proto = LOADED_IMAGE_PROTOCOL_GUID;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		goto fail;

	efi_printk(sys_table, PRINTK_PREFIX"Booting Linux using EFI stub.\n");


	/* get the command line from EFI, using the LOADED_IMAGE protocol */
	status = efi_call_phys3(sys_table->boottime->handle_protocol,
				handle, &proto, (void *)&image);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Failed to get handle for LOADED_IMAGE_PROTOCOL.\n");
		goto fail;
	}

	/* We are going to copy the command line into the device tree,
	 * so this memory just needs to not conflict with boot protocol
	 * requirements.
	 */
	cmdline_ptr = efi_convert_cmdline_to_ascii(sys_table, image,
						   &cmdline_size);
	if (!cmdline_ptr) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Unable to allocate memory for command line.\n");
		goto fail;
	}

	/* We first load the device tree, as we need to get the base address of
	 * DRAM from the device tree.  The zImage, device tree, and initrd
	 * have address restrictions that are relative to the base of DRAM.
	 */
	status = handle_cmdline_files(sys_table, image, cmdline_ptr, "dtb=",
				      0xffffffff, &fdt_addr, &fdt_size);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Unable to load device tree blob.\n");
		goto fail_free_cmdline;
	}

	err = fdt_check_header((void *)fdt_addr);
	if (err != 0) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Device tree header not valid.\n");
		goto fail_free_fdt;
	}
	if (fdt_totalsize((void *)fdt_addr) > fdt_size) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Incomplete device tree.\n");
		goto fail_free_fdt;

	}


	/* Look up the base of DRAM from the device tree. */
	fdt = (void *)fdt_addr;
	node = fdt_subnode_offset(fdt, 0, "memory");
	region = fdt_getprop(fdt, node, "reg", NULL);
	if (region) {
		dram_base = fdt64_to_cpu(region->base);
	} else {
		/* There is no way to get amount or addresses of physical
		 * memory installed using EFI calls.  If the device tree
		 * we read from disk doesn't have this, there is no way
		 * for us to construct this informaion.
		 */
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: No 'memory' node in device tree.\n");
		goto fail_free_fdt;
	}

	/* Reserve memory for the uncompressed kernel image. This is
	 * all that prevents any future allocations from conflicting
	 * with the kernel.  Since we can't tell from the compressed
	 * image how much DRAM the kernel actually uses (due to BSS
	 * size uncertainty) we allocate the maximum possible size.
	 */
	kernel_reserve_addr = dram_base;
	kernel_reserve_size = MAX_UNCOMP_KERNEL_SIZE;
	nr_pages = round_up(kernel_reserve_size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
	status = efi_call_phys4(sys_table->boottime->allocate_pages,
				EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
				nr_pages, &kernel_reserve_addr);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Unable to allocate memory for uncompressed kernel.\n");
		goto fail_free_fdt;
	}

	/* Relocate the zImage, if required.  ARM doesn't have a
	 * preferred address, so we set it to 0, as we want to allocate
	 * as low in memory as possible.
	 */
	zimage_size = image->image_size;
	status = efi_relocate_kernel(sys_table, zimage_addr, zimage_size,
				     zimage_size, 0, 0);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Failed to relocate kernel.\n");
		goto fail_free_kernel_reserve;
	}

	/* Check to see if we were able to allocate memory low enough
	 * in memory.
	 */
	if (*zimage_addr + zimage_size > dram_base + ZIMAGE_OFFSET_LIMIT) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Failed to relocate kernel, no low memory available.\n");
		goto fail_free_zimage;
	}
	status = handle_cmdline_files(sys_table, image, cmdline_ptr, "initrd=",
				      dram_base + ZIMAGE_OFFSET_LIMIT,
				      &initrd_addr, &initrd_size);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Unable to load initrd.\n");
		goto fail_free_zimage;
	}

	/* Estimate size of new FDT, and allocate memory for it. We
	 * will allocate a bigger buffer if this ends up being too
	 * small, so a rough guess is OK here.*/
	new_fdt_size = fdt_size + cmdline_size + 0x800;
	while (1) {
		status = efi_high_alloc(sys_table, new_fdt_size, 0,
					&new_fdt_addr,
					dram_base + ZIMAGE_OFFSET_LIMIT);
		if (status != EFI_SUCCESS) {
			efi_printk(sys_table, PRINTK_PREFIX"ERROR: Unable to allocate memory for new device tree.\n");
			goto fail_free_initrd;
		}

		/* Now that we have done our final memory allocation (and free)
		 * we can get the memory map key  needed for
		 * exit_boot_services().
		 */
		status = efi_get_memory_map(sys_table, &memory_map, &map_size,
					    &desc_size, &desc_ver, &mmap_key);
		if (status != EFI_SUCCESS)
			goto fail_free_new_fdt;

		status = update_fdt(sys_table,
				    fdt, (void *)new_fdt_addr, new_fdt_size,
				    cmdline_ptr,
				    initrd_addr, initrd_size,
				    memory_map, map_size, desc_size, desc_ver);

		/* Succeeding the first time is the expected case. */
		if (status == EFI_SUCCESS)
			break;

		if (status == EFI_BUFFER_TOO_SMALL) {
			/* We need to allocate more space for the new
			 * device tree, so free existing buffer that is
			 * too small.  Also free memory map, as we will need
			 * to get new one that reflects the free/alloc we do
			 * on the device tree buffer. */
			efi_free(sys_table, new_fdt_size, new_fdt_addr);
			efi_call_phys1(sys_table->boottime->free_pool,
				       memory_map);
			new_fdt_size += new_fdt_size / 4;
		} else {
			efi_printk(sys_table, PRINTK_PREFIX"ERROR: Unable to constuct new device tree.\n");
			goto fail_free_mmap;
		}
	}

	/* Now we are ready to exit_boot_services.*/
	status = efi_call_phys2(sys_table->boottime->exit_boot_services,
				handle, mmap_key);

	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, PRINTK_PREFIX"ERROR: Exit boot services failed.\n");
		goto fail_free_mmap;
	}


	/* Now we need to return the FDT address to the calling
	 * assembly to this can be used as part of normal boot.
	 */
	return new_fdt_addr;

fail_free_mmap:
	efi_call_phys1(sys_table->boottime->free_pool, memory_map);

fail_free_new_fdt:
	efi_free(sys_table, new_fdt_size, new_fdt_addr);

fail_free_initrd:
	efi_free(sys_table, initrd_size, initrd_addr);

fail_free_zimage:
	efi_free(sys_table, zimage_size, *zimage_addr);

fail_free_kernel_reserve:
	efi_free(sys_table, kernel_reserve_size, kernel_reserve_addr);

fail_free_fdt:
	efi_free(sys_table, fdt_size, fdt_addr);

fail_free_cmdline:
	efi_free(sys_table, cmdline_size, (u32)cmdline_ptr);

fail:
	return EFI_STUB_ERROR;
}
