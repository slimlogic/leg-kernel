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

#ifdef	CONFIG_GUFI

#include <linux/gufi.h>
#include <linux/init.h>

#include "of_protocol.h"
#include "acpi_protocol.h"

enum search_preference {
	SEARCH_ACPI_ONLY,
	SEARCH_DT_ONLY,
	SEARCH_ACPI_FIRST,
	SEARCH_DT_FIRST
};

#if IS_ENABLED(CONFIG_GUFI_ACPI_ONLY)
#define DEFAULT_PREFERENCE	SEARCH_ACPI_ONLY
#elif IS_ENABLED(CONFIG_GUFI_DT_ONLY)
#define DEFAULT_PREFERENCE	SEARCH_DT_ONLY
#elif IS_ENABLED(CONFIG_GUFI_ACPI_FIRST)
#define DEFAULT_PREFERENCE	SEARCH_ACPI_FIRST
#elif IS_ENABLED(CONFIG_GUFI_DT_FIRST)
#define DEFAULT_PREFERENCE	SEARCH_DT_FIRST
#else
#define DEFAULT_PREFERENCE	SEARCH_ACPI_FIRST
#endif

static enum search_preference howto_search = DEFAULT_PREFERENCE;

static LIST_HEAD(__protocols);
static LIST_HEAD(__gdn_list);

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
		list_add_tail(&tmp->entry, &__protocols);

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

	if (!tmp)
		list_del(&tmp->entry);

	return;
}
EXPORT_SYMBOL(gufi_unregister_protocol);

struct gufi_device_node *gufi_find_first_node(const char *name)
{
	struct gufi_device_node *result = NULL;
	struct gufi_protocol *p;

	list_for_each_entry(p, &__protocols, entry) {
		if (p->find_first_node)
			result = p->find_first_node(name);
			if (result)
				break;
	}

	return result;
}
EXPORT_SYMBOL(gufi_find_first_node);

static struct gufi_protocol acpi_protocol = {
	.name = "acpi",
	.find_first_node = gufi_acpi_find_first_node,
};

static struct gufi_protocol of_protocol = {
	.name = "of",
	.find_first_node = gufi_of_find_first_node,
};

static int  __init gufi_init(void)
{
	/*
	 * TODO: enable a kernel parameter that would allow
	 * this to be switched at boot time.
	 */

	switch (howto_search) {
	case SEARCH_ACPI_ONLY:
		gufi_register_protocol(&acpi_protocol);
		break;
		;;
	case SEARCH_DT_ONLY:
		gufi_register_protocol(&of_protocol);
		break;
		;;
	case SEARCH_ACPI_FIRST:
		gufi_register_protocol(&acpi_protocol);
		gufi_register_protocol(&of_protocol);
		break;
		;;
	case SEARCH_DT_FIRST:
		gufi_register_protocol(&of_protocol);
		gufi_register_protocol(&acpi_protocol);
		break;
		;;
	}

	return 0;
}
module_init(gufi_init);

#endif	/* CONFIG_GUFI */
