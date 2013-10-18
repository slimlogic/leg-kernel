#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2013, Red Hat, Inc.
# Author: Mark Salter <msalter@redhat.com>
#

libfdt		:= fdt_rw.c fdt_ro.c fdt_wip.c fdt.c
libfdt_hdrs	:= fdt.h libfdt.h libfdt_internal.h

libfdt_objs	:= $(addsuffix .o, $(basename $(libfdt)))

$(addprefix $(obj)/,$(libfdt) $(libfdt_hdrs)): $(obj)/%: $(srctree)/scripts/dtc/libfdt/%
	$(call cmd,shipped)

$(addprefix $(obj)/,$(libfdt_objs) efi-stub.o efi-entry.o): \
	$(addprefix $(obj)/,$(libfdt_hdrs))

CFLAGS_efi-stub.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
ccflags-y := -I$(obj) -I$(srctree)/$(obj)

obj-y	+= $(libfdt_objs) efi-stub.o efi-entry.o

clean-files := $(libfdt) $(libfdt_hdrs)
