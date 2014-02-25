/*
 * Grand Unified Firmware Interface
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

#include "acpi_protocol.h"

static acpi_status __gufi_acpi_find_first_callback(acpi_handle handle,
						   u32 lvl_not_used,
						   void *compatible_str,
						   void **return_value)
{
	struct acpi_device *device = NULL;
	char *compatible = (char *)compatible_str;
	const char *value;

	if (acpi_bus_get_device(handle, &device))
		return AE_NOT_FOUND;

	if (acpi_dev_get_property_string(device, "compatible", &value))
		return AE_NOT_FOUND;

	if (strcmp(value, compatible))
		return AE_NOT_FOUND;

	if (!*return_value)
		*return_value = device;

	return AE_OK;
}

static struct acpi_device *__gufi_find_first_acpi_node(const char *compatible)
{
	struct acpi_device *an = NULL;
	void *device = NULL;

	/*
	 * Traverse the namespace looking for a device with the right
	 * compatible key-value pair.  Will need to invoke the _PRP method
	 * to retrieve all key-value pairs and get the compatible property.
	 */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			    __gufi_acpi_find_first_callback, NULL,
			    (void *)compatible, &device);
	an = (struct acpi_device *)device;
	return an;
}

struct gufi_device_node *gufi_acpi_find_first_node(const char *name)
{
	struct device_node *dn = NULL;
	struct acpi_device *an = NULL;
	struct gufi_device_node *node;

	an = __gufi_find_first_acpi_node(name);
	node = gufi_look_for_node(dn, an);

	return node;
}

struct gufi_device_node *gufi_acpi_node_get(struct gufi_device_node *gdn)
{
	/* ACPI doesn't really do reference counting */
	return gdn;
}

void gufi_acpi_node_put(struct gufi_device_node *gdn)
{
	/* ACPI doesn't really do reference counting */
	return;
}

const struct gufi_device_id gufi_acpi_match_device(
		const struct gufi_device_id ids, const struct device *dev)
{
	struct gufi_device_id res;

	res.acpi_ids = acpi_match_device(ids.acpi_ids, dev);
	return res;
}

bool gufi_acpi_test_match(const struct gufi_device_id id)
{
	return id.acpi_ids != NULL;
}

int gufi_acpi_property_read_u32(const struct gufi_device_node *gdn,
		const char *propname, u32 *out_value)
{
	int res;
	acpi_handle handle = acpi_device_handle(gdn->an);
	struct acpi_dsm_entry entry;

	res = acpi_dsm_lookup_value(handle, propname, 0, &entry);
	if (res != 0)
		goto out;

	res = kstrtouint(entry.value, 0, out_value);
	if (res != 0)
		goto out;

out:
	kfree(entry.key);
	kfree(entry.value);

	return res;
}
