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

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/types.h>

struct gufi_device_node {
	struct device_node *dn;
	struct acpi_device *an;
	struct kref kref;
	struct list_head entry;
};

#ifdef CONFIG_GUFI

/* Reference counting routines */
extern struct gufi_device_node *gufi_node_get(struct gufi_device_node *gdn);
extern void gufi_node_put(struct gufi_device_node *gdn);

/* Search for nodes in interesting ways */
extern struct gufi_device_node *gufi_look_for_node(struct device_node *dn,
						   struct acpi_device *an);
extern struct gufi_device_node *gufi_find_compatible_node(
						struct gufi_device_node *gdn,
						const char *type,
						const char *compatible);
extern struct gufi_device_node *gufi_find_node_by_phandle(phandle handle);

/* Tree walking routines */
extern struct gufi_device_node *gufi_get_next_parent(
						struct gufi_device_node *node);

/* Retrieve values for specific properties */
extern const void *gufi_get_property(const struct gufi_device_node *node,
				     const char *name,
				     int *lenp);
extern int gufi_property_read_string(struct gufi_device_node *np,
			             const char *propname,
			             const char **out_string);
extern int gufi_property_read_u32(const struct gufi_device_node *np,
				  const char *propname,
				  u32 *out_value);
extern int gufi_property_read_u32_array(const struct gufi_device_node *np,
				        const char *propname,
				        u32 *out_values,
				        size_t sz);

/* Addressing routines */
extern void __iomem *gufi_iomap(struct gufi_device_node *gdn, int index);

#else	/* CONFIG_GUFI */

/* Reference counting routines */
static inline
struct gufi_device_node *gufi_node_get(struct gufi_device_node *gdn)
{
	return NULL;
}
static inline void gufi_node_put(struct gufi_device_node *gdn) { return; }

/* Search for nodes in interesting ways */
static struct gufi_device_node *gufi_look_for_node(struct device_node *dn,
						   struct acpi_device *an)
{
	return NULL;
}

static inline struct gufi_device_node *gufi_find_compatible_node(
						struct gufi_device_node *gdn,
						const char *type,
						const char *compatible)
{
	return NULL;
}

static inline struct gufi_device_node *gufi_find_node_by_phandle(phandle handle)
{
	return NULL;
}

/* Tree walking routines */
static inline
struct gufi_device_node *gufi_get_next_parent(struct gufi_device_node *node)
{
	return NULL;
}

/* Retrieve values for specific properties */
static inline int gufi_property_read_u32_array(
					const struct gufi_device_node *np,
					const char *propname,
					u32 *out_values,
					size_t sz)
{
	return -ENOSYS;
}

static inline int gufi_property_read_string(struct gufi_device_node *np,
					    const char *propname,
					    const char **out_string)
{
	return -ENOSYS;
}

static inline const void *gufi_get_property(const struct device_node *node,
					    const char *name,
					    int *lenp)
{
	return NULL;
}


/* Addressing routines */
static inline void __iomem *gufi_iomap(struct gufi_device_node *gdn, int index)
{
	return NULL;
}

#endif	/* CONFIG_GUFI */

#endif	/*_LINUX_GUFI_H */
