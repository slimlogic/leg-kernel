/*
 * gufi.h - Grand Unified Firmware Interface
 *
 * Copyright (C) 2013 Al Stone <al.stone@linaro.org>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef _LINUX_GUFI_H
#define _LINUX_GUFI_H

#ifdef CONFIG_GUFI

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/types.h>

struct gufi_device_node {
	struct device_node *dn;
	struct acpi_device_id *id;
	struct kref kref;
	struct list_head entry;
};

/* Reference counting routines */
extern struct gufi_device_node *gufi_node_get(struct gufi_device_node *gdn);
extern void gufi_node_put(struct gufi_device_node *gdn);

/* Search for nodes in interesting ways */
extern struct gufi_device_node *gufi_find_compatible_node(
						struct gufi_device_node *gdn,
						const char *type,
						const char *compatible);

/* Addressing routines */
extern void __iomem *gufi_iomap(struct gufi_device_node *gdn, int index);

#else	/* CONFIG_GUFI */

/* Reference counting routines */
struct gufi_device_node *gufi_node_get(struct gufi_device_node *gdn)
{
	return NULL;
}
void gufi_node_put(struct gufi_device_node *gdn) { return; }

/* Search for nodes in interesting ways */
struct gufi_device_node *gufi_find_compatible_node(
						struct gufi_device_node *gdn,
						const char *type,
						const char *compatible)
{
	return NULL;
}

/* Addressing routines */
void __iomem *gufi_iomap(struct gufi_device_node *gdn, int index)
{
	return NULL;
}

#endif	/* CONFIG_GUFI */

#endif	/*_LINUX_GUFI_H */
