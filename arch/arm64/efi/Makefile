#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2013, Red Hat, Inc.
# Author: Mark Salter <msalter@redhat.com>
#

ifeq ($(CONFIG_EFI),y)
efi_objs += efi.o
endif
ifeq ($(CONFIG_EFI_STUB),y)
efi_objs += efi-stub.o efi-entry.o
endif

libfdt_src	:= fdt_rw.c fdt_ro.c fdt_wip.c fdt.c
libfdt_hdrs	:= fdt.h libfdt.h libfdt_internal.h

libfdt_objs	:= $(addsuffix .o, $(basename $(libfdt_src)))

$(addprefix $(obj)/,$(libfdt_src) $(libfdt_hdrs)): $(obj)/%: $(srctree)/scripts/dtc/libfdt/%
	$(call cmd,shipped)

$(addprefix $(obj)/,$(libfdt_objs)): $(addprefix $(obj)/,$(libfdt_hdrs))

CFLAGS_efi-stub.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
ccflags-y := -I$(obj) -I$(srctree)/$(obj)

obj-y	+= $(libfdt_objs) $(efi_objs)

clean-files := $(libfdt_src) $(libfdt_hdrs)
