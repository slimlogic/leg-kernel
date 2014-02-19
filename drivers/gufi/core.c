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

#include <linux/gufi.h>
#include <linux/init.h>
#include <linux/spinlock.h>

/* TODO: move OF and ACPI functionality to separate files and get rid of this */
#include <linux/of_device.h>
#include <linux/acpi.h>

#include "of_protocol.h"
#include "acpi_protocol.h"

/*
 * TODO list:
 *
 * (1) how do we sync up with the ACPI interpreter being enabled?
 *     or do we? i think we will have to so that we can execute methods
 *     properly.  but, does that delay all the other drivers from getting
 *     initialized?
 *
 */

static LIST_HEAD(__protocols);
static LIST_HEAD(__gdn_list);

static DEFINE_SPINLOCK(__gdn_list_lock);

/***********************************************************************
 *
 * Functions to get different mechanisms registered for finding
 * configuration information for a device.
 *
 ***********************************************************************/

int gufi_register_protocol(struct gufi_protocol *prot)
{
	struct gufi_protocol *p;
	struct gufi_protocol *tmp = NULL;

	if (!prot)
		return -ENODEV;
	pr_debug("%s: registering %s\n", __func__, prot->name);

	list_for_each_entry(p, &__protocols, entry) {
		if (!strcmp(prot->name, p->name)) {
			tmp = p;
			break;
		}
	}

	if (!tmp)
		list_add_tail(&prot->entry, &__protocols);

	return 0;
}
EXPORT_SYMBOL(gufi_register_protocol);

void gufi_unregister_protocol(struct gufi_protocol *prot)
{
	struct gufi_protocol *p;
	struct gufi_protocol *tmp = NULL;

	if (!prot)
		return;
	pr_debug("%s: unregistering %s\n", __func__, prot->name);

	list_for_each_entry(p, &__protocols, entry) {
		if (!strcmp(prot->name, p->name)) {
			tmp = p;
			break;
		}
	}

	if (tmp)
		list_del(&tmp->entry);

	return;
}
EXPORT_SYMBOL(gufi_unregister_protocol);

/***********************************************************************
 *
 * General utility functions
 *
 ***********************************************************************/

/**
 * __gufi_look_for_acpi	- All gufi_device_nodes are kept in a list.
 * 			  Given an acpi_device, search the list for
 * 			  a matching node.
 * @an:	struct acpi_device to look for
 *
 * Returns a pointer to the node found, if any.
 */
static struct gufi_device_node *__gufi_look_for_acpi(struct acpi_device *an)
{
	struct gufi_device_node *pos;

	if (!an)
		return NULL;

	list_for_each_entry(pos, &__gdn_list, entry) {
		if (pos->an == an) {
			return pos;
		}
	}

	return NULL;
}

/**
 * __gufi_look_for_dt	- All gufi_device_nodes are kept in a list.
 * 			  Given an DT device node, search the list for
 * 			  a matching node.
 * @dn:	struct device_node to look for
 *
 * Returns a pointer to the node found, if any.
 */
static struct gufi_device_node *__gufi_look_for_dt(struct device_node *dn)
{
	struct gufi_device_node *pos;

	if (!dn)
		return NULL;

	list_for_each_entry(pos, &__gdn_list, entry) {
		if (pos->dn == dn) {
			return pos;
		}
	}

	return NULL;
}

/**
 * gufi_look_for_node	- All gufi_device_nodes a kept in a list.
 * 			  Given either a device_node or acpi_device
 * 			  (or both), search the list for a matching
 * 			  node.  If there is no node, make one and
 * 			  add it to the list.
 * @dn:	struct device_node to look for
 * @an:	struct acpi_device to look for
 *
 * Returns a pointer to the node found, if any, or creates a new node
 * and returns the address to it.
 */
struct gufi_device_node *gufi_look_for_node(struct device_node *dn,
					    struct acpi_device *an)
{
	struct gufi_device_node *gdn = NULL;
	struct gufi_device_node *ga = NULL;
	struct gufi_device_node *gd = NULL;
	unsigned long lock_flags;

	pr_debug("GUFI: entering gufi_look_for_node\n");
	pr_debug("GUFI: gufi_look_for_node: dn = 0x%p\n", dn);
	pr_debug("GUFI: gufi_look_for_node: an = 0x%p\n", an);

	spin_lock_irqsave(&__gdn_list_lock, lock_flags);

	ga = __gufi_look_for_acpi(an);
	gd = __gufi_look_for_dt(dn);
	pr_debug("GUFI: gufi_look_for_node: ga = 0x%p\n", ga);
	pr_debug("GUFI: gufi_look_for_node: gd = 0x%p\n", gd);

	if ((ga || gd) && (ga == gd)) {
		spin_unlock_irqrestore(&__gdn_list_lock, lock_flags);
		return ga; /* because ga == gd */
	}

	if (ga && !gd) {
		if (dn)
			ga->dn = dn;
		spin_unlock_irqrestore(&__gdn_list_lock, lock_flags);
		return ga;
	}

	if (gd && !ga) {
		if (an)
			gd->an = an;
		spin_unlock_irqrestore(&__gdn_list_lock, lock_flags);
		return gd;
	}

	if (!(ga || gd)) {
		gdn = kzalloc(sizeof(struct gufi_device_node), GFP_KERNEL);
		if (gdn) {
			gdn->dn = dn;
			gdn->an = an;
			pr_debug("GUFI: gufi_look_for_node: gdn = 0x%p\n", gdn);
			pr_debug("GUFI: gufi_look_for_node: gdn->an = 0x%p\n", gdn->an);
			pr_debug("GUFI: gufi_look_for_node: gdn->dn = 0x%p\n", gdn->dn);
			kref_init(&gdn->kref);
			list_add_tail(&gdn->entry, &__gdn_list);
		}
	}
	spin_unlock_irqrestore(&__gdn_list_lock, lock_flags);

	pr_debug("GUFI: leaving gufi_look_for_node\n");

	return gdn;
}
EXPORT_SYMBOL(gufi_look_for_node);


/***********************************************************************
 *
 * Functions that implement the elements of the GUFI API
 *
 ***********************************************************************/

struct gufi_device_node *gufi_find_first_node(const char *name)
{
	struct gufi_device_node *result = NULL;
	struct gufi_protocol *p;

	list_for_each_entry(p, &__protocols, entry) {
		if (p->find_first_node) {
			result = p->find_first_node(name);
			if (result)
				break;
		}
	}

	return result;
}
EXPORT_SYMBOL(gufi_find_first_node);

const struct gufi_device_id gufi_match_device(const struct gufi_device_id ids,
		const struct device *dev)
{
	struct gufi_device_id res = { NULL, NULL };

	if (acpi_disabled)
		res.of_ids = of_match_device(ids.of_ids, dev);
	else
		res.acpi_ids = acpi_match_device(ids.acpi_ids, dev);

	return res;
}
EXPORT_SYMBOL(gufi_match_device);

bool gufi_test_match(const struct gufi_device_id id)
{
	return id.of_ids != NULL || id.acpi_ids != NULL;
}
EXPORT_SYMBOL(gufi_test_match);

int gufi_property_read_u32(const struct gufi_device_node *gdn,
		const char *propname, u32 *out_value)
{
	int res;

	if (!gdn || !propname || !out_value)
		return -EINVAL;

	if (acpi_disabled) {
		res = of_property_read_u32(gdn->dn, propname, out_value);
	} else {
		acpi_handle handle = acpi_device_handle(gdn->an);
		struct acpi_dsm_entry entry;

		res = acpi_dsm_lookup_value(handle, propname, 0, &entry);
		if (res != 0)
			return -ENODATA;

		if (kstrtouint(entry.value, 0, out_value) != 0)
			return -EINVAL;

		kfree(entry.key);
		kfree(entry.value);
	}

	return res;
}
EXPORT_SYMBOL(gufi_property_read_u32);

struct gufi_device_node *gufi_node_get(struct gufi_device_node *gdn)
{
	struct gufi_device_node *result = NULL;
	struct gufi_protocol *p;

	list_for_each_entry(p, &__protocols, entry) {
		if (p->node_get)
			result = p->node_get(gdn);
	}
	kref_get(&gdn->kref);

	return result;
}
EXPORT_SYMBOL(gufi_node_get);

static void gufi_node_release(struct kref *kref)
{
	struct gufi_device_node *gdn;

	gdn = container_of(kref, struct gufi_device_node, kref);
	kfree(gdn);
}

void gufi_node_put(struct gufi_device_node *gdn)
{
	struct gufi_protocol *p;

	list_for_each_entry(p, &__protocols, entry) {
		if (p->node_put)
			p->node_put(gdn);
	}
	kref_put(&gdn->kref, gufi_node_release);

	return;
}
EXPORT_SYMBOL(gufi_node_put);

static struct gufi_protocol acpi_protocol = {
	.name = "ACPI",
	.find_first_node = gufi_acpi_find_first_node,
	.node_get = gufi_acpi_node_get,
	.node_put = gufi_acpi_node_put,
};

static struct gufi_protocol of_protocol = {
	.name = "OF",
	.find_first_node = gufi_of_find_first_node,
	.node_get = gufi_of_node_get,
	.node_put = gufi_of_node_put,
};

int __init gufi_init(void)
{
	if (acpi_disabled)
		gufi_register_protocol(&of_protocol);
	else
		gufi_register_protocol(&acpi_protocol);

	return 0;
}
EXPORT_SYMBOL(gufi_init);
