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
#include <linux/libfdt.h>
#include <asm/sections.h>

/*
 * temporary hack to provide cmdline when booting directly to kernel (no grub)
 */
#define CMDLINE_HACK

/* Error code returned to ASM code instead of valid FDT address. */
#define EFI_STUB_ERROR		(~0UL)

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

/*
 * AArch64 requires the DTB to be 8-byte aligned in the first 512MiB from
 * start of kernel and may not cross a 2MiB boundary. We set alignment to
 * equal max size so we know it won't cross a 2MiB boudary.
 */
#define MAX_DTB_SIZE	0x40000
#define DTB_ALIGN	MAX_DTB_SIZE
#define MAX_DTB_OFFSET	0x20000000

#define pr_efi(msg)     efi_printk(sys_table, "EFI stub: "msg)
#define pr_efi_err(msg) efi_printk(sys_table, "EFI stub: ERROR: "msg)

struct fdt_region {
	u64 base;
	u64 size;
};

/* Include shared EFI stub code */
#include "../../../drivers/firmware/efi/efi-stub-helper.c"

static unsigned long get_dram_base(efi_system_table_t *sys_table)
{
	efi_status_t status;
	unsigned long map_size, desc_size;
	unsigned long membase = ~0UL;
	efi_memory_desc_t *memory_map;
	int i;

	status = efi_get_memory_map(sys_table, &memory_map, &map_size,
				    &desc_size, NULL, NULL);
	if (status == EFI_SUCCESS) {
		for (i = 0; i < (map_size / sizeof(efi_memory_desc_t)); i++) {
			efi_memory_desc_t *desc;
			unsigned long m = (unsigned long)memory_map;

			desc = (efi_memory_desc_t *)(m + (i * desc_size));

			if (desc->num_pages == 0)
				break;

			if (desc->type == EFI_CONVENTIONAL_MEMORY) {
				unsigned long base = desc->phys_addr;

				base &= ~((unsigned long)(TEXT_OFFSET - 1));

				if (membase > base)
					membase = base;
			}
		}
	}
	return membase;
}

unsigned long efi_entry(void *handle, efi_system_table_t *sys_table,
			unsigned long *image_addr)
{
	efi_loaded_image_t *image;
	efi_status_t status;
	int err, image_allocated = 0;
	unsigned long image_size, mem_size;
	unsigned long dram_base;
	/* addr/point and size pairs for memory management*/
	u64 initrd_addr;
	u64 initrd_size = 0;
	u64 fdt_addr;  /* Original DTB */
	u64 fdt_size = 0;
	unsigned long new_fdt_size;
	char *cmdline_ptr;
	int cmdline_size = 0;
	unsigned long new_fdt_addr;
	unsigned long map_size, desc_size;
	unsigned long mmap_key;
	efi_memory_desc_t *memory_map;
	u32 desc_ver;
	efi_guid_t proto = LOADED_IMAGE_PROTOCOL_GUID;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		goto fail;

	pr_efi("Booting Linux Kernel...\n");

	/* get the command line from EFI, using the LOADED_IMAGE protocol */
	status = efi_call_phys3(sys_table->boottime->handle_protocol,
				handle, &proto, (void *)&image);
	if (status != EFI_SUCCESS) {
		pr_efi("Failed to get handle for LOADED_IMAGE_PROTOCOL\n");
		goto fail;
	}

	/*
	 * We are going to copy this into device tree, so we don't care where
	 * in memory it is.
	 */
	cmdline_ptr = efi_convert_cmdline_to_ascii(sys_table, image,
						   &cmdline_size);
	if (!cmdline_ptr) {
		pr_efi_err("converting command line to ascii.\n");
		goto fail;
	}
	if (*cmdline_ptr == 0) {
#ifdef CMDLINE_HACK
		int i;
		char *p;
		unsigned long new_addr;
		unsigned long cmdline_len;

		/* free the empty cmdline_ptr. */
		efi_free(sys_table, cmdline_size, (u64)cmdline_ptr);
		cmdline_len = 0;

		/* get cmdline from file */
		status = handle_cmdline_files(sys_table, image,
					      "cmd=cmdline\n", "cmd=", ~0UL,
					      (unsigned long *)&cmdline_ptr,
					      &cmdline_len);

		cmdline_size = cmdline_len;
		if (status != EFI_SUCCESS) {
			pr_efi("CMDLINE_HACK Failed to read cmdline file\n");
			goto fail;
		}

		for (i = 0, p = cmdline_ptr; i < cmdline_size; i++, p++)
			if (*p == '\n' || *p == '\0')
				break;
		/* i is length of string not counting terminating NULL or \n */
		status = efi_high_alloc(sys_table, i + 1, 0, &new_addr, ~0UL);
		if (status != EFI_SUCCESS) {
			pr_efi("CMDLINE_HACK alloc for new cmdline failed\n");
			goto fail;
		}
		p = (char *)new_addr;
		memcpy(p, cmdline_ptr, i);
		p[i] = 0;

		efi_free(sys_table, cmdline_size, (u64)cmdline_ptr);
		cmdline_size = i + 1;
		cmdline_ptr = p;
#endif
	}

	status = handle_cmdline_files(sys_table, image, cmdline_ptr, "dtb=",
				      ~0UL, (unsigned long *)&fdt_addr,
				      (unsigned long *)&fdt_size);
	if (status != EFI_SUCCESS)
		fdt_addr = 0;

	if (fdt_addr) {
		err = fdt_check_header((void *)fdt_addr);
		if (err != 0) {
			pr_efi_err("Device Tree header not valid\n");
			goto fail_free_dtb;
		}
		if (fdt_totalsize((void *)fdt_addr) > fdt_size) {
			pr_efi_err("Incomplete device tree.\n");
			goto fail_free_dtb;

		}
	}

	dram_base = get_dram_base(sys_table);
	if (dram_base == ~0UL) {
		pr_efi("Failed to get DRAM base\n");
		goto fail_free_dtb;
	}

	/* Relocate the image, if required. */
	image_size = image->image_size;
	if (*image_addr != (dram_base + TEXT_OFFSET)) {
		mem_size = image_size + (_end - __bss_start);
		status = efi_relocate_kernel(sys_table, image_addr, image_size,
					     mem_size, dram_base + TEXT_OFFSET,
					     PAGE_SIZE);
		if (status != EFI_SUCCESS) {
			pr_efi("Failed to relocate kernel\n");
			goto fail_free_dtb;
		}
		image_allocated = 1;
		if (*image_addr != (dram_base + TEXT_OFFSET)) {
			pr_efi("Failed to alloc kernel memory\n");
			goto fail_free_image;
		}
	}

	status = handle_cmdline_files(sys_table, image, cmdline_ptr, "initrd=",
				      dram_base + 0x20000000,
				      (unsigned long *)&initrd_addr,
				      (unsigned long *)&initrd_size);
	if (status != EFI_SUCCESS) {
		pr_efi("Error loading initrd\n");
		goto fail_free_image;
	}

	/* Estimate size of new FDT, and allocate memory for it. We
	 * will allocate a bigger buffer if this ends up being too
	 * small, so a rough guess is OK here.*/
	new_fdt_size = fdt_size + EFI_PAGE_SIZE;
	while (1) {
		status = efi_high_alloc(sys_table, new_fdt_size, DTB_ALIGN,
					&new_fdt_addr,
					dram_base + MAX_DTB_OFFSET);
		if (status != EFI_SUCCESS) {
			pr_efi_err("No memory for new devive tree.\n");
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
				    (void *)fdt_addr, (void *)new_fdt_addr,
				    new_fdt_size, cmdline_ptr,
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
			new_fdt_size += EFI_PAGE_SIZE;
		} else {
			pr_efi_err("Unable to constuct new device tree.\n");
			goto fail_free_mmap;
		}
	}

	/* Now we are ready to exit_boot_services.*/
	status = efi_call_phys2(sys_table->boottime->exit_boot_services,
				handle, mmap_key);

	if (status != EFI_SUCCESS) {
		pr_efi_err("Exit boot services failed.\n");
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

fail_free_image:
	if (image_allocated)
		efi_free(sys_table, mem_size, *image_addr);

fail_free_dtb:
	if (fdt_addr)
		efi_free(sys_table, fdt_size, fdt_addr);

	efi_free(sys_table, cmdline_size, (u64)cmdline_ptr);

fail:
	return EFI_STUB_ERROR;
}
