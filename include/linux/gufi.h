/*
 * gufi.h - Grand Unified Firmware Interface
 *
 * Copyright (C) 2014 Al Stone <al.stone@linaro.org>
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

struct gufi_protocol {
	const char *name;
	struct list_head entry;

	struct gufi_device_node *(*find_first_node)(const char *name);
	struct gufi_device_node *(*node_get)(struct gufi_device_node *gdn);
	void (*node_put)(struct gufi_device_node *gdn);
};

struct gufi_device_id {
	const struct of_device_id *of_ids;
	const struct acpi_device_id *acpi_ids;
};

#define __GUFI_MATCH_INITIALIZER(ofs, acpis) {			\
	.of_ids = of_match_ptr(ofs),				\
	.acpi_ids = ACPI_PTR(acpis)				\
	}

#define DECLARE_GUFI_MATCH(name, ofs, acpis)			\
	struct gufi_device_id name = __GUFI_MATCH_INITIALIZER(ofs, acpis)

/* General GUFI functionality */
int gufi_init(void);
int gufi_register_protocol(struct gufi_protocol *prot);
void gufi_unregister_protocol(struct gufi_protocol *prot);

struct gufi_device_node *gufi_look_for_node(struct device_node *dn,
		struct acpi_device *an);

/* Reference counting functions */
struct gufi_device_node *gufi_node_get(struct gufi_device_node *gdn);
void gufi_node_put(struct gufi_device_node *gdn);

/* Functions returning configuration information */
struct gufi_device_node *gufi_find_first_node(const char *name);
const struct gufi_device_id gufi_match_device(const struct gufi_device_id ids,
		const struct device *dev);
bool gufi_test_match(const struct gufi_device_id id);

#endif	/*_LINUX_GUFI_H */
