/*
 * Grand Unified Firmware Interface
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

#ifdef	CONFIG_GUFI

#include <linux/gufi.h>
#include <linux/of_address.h>

enum search_preference {
	SEARCH_ACPI_ONLY = 0,
	SEARCH_DT_ONLY,
	SEARCH_ACPI_FIRST,
	SEARCH_DT_FIRST
};

#if IS_ENABLED(CONFIG_GUFI_ACPI_ONLY)
#define DEFAULT_PREFERENCE	SEARCH_ACPI_ONLY
#elif IS_ENABLED(CONFIG_GUFI_DT_ONLY
#define DEFAULT_PREFERENCE	SEARCH_DT_ONLY
#elif IS_ENABLED(CONFIG_GUFI_ACPI_FIRST
#define DEFAULT_PREFERENCE	SEARCH_ACPI_FIRST
#elif IS_ENABLED(CONFIG_GUFI_DT_FIRST
#define DEFAULT_PREFERENCE	SEARCH_DT_FIRST
#else
#define DEFAULT_PREFERENCE	SEARCH_ACPI_FIRST
#endif

static enum search_preference search_first = DEFAULT_PREFERENCE;

static LIST_HEAD(__gdn_list);

/*
 * This macro may be a bit long, but the idea is to simplify the code
 * for most of the other functions defined since each function will need
 * to employ the same logic.  Without this macro, each function would also
 * have to duplicate code (one instance for each switch) which could make
 * it harder to maintain over time (e.g., correcting the second switch
 * but inadvertently missing the first one).  Or, should some new format
 * other than ACPI or Device Tree be defined, adding might be simpler
 * with this approach.
 *
 * s => the enum search_preference variable being used to track which
 * 	searches have been done
 * sacpi => expression to use for ACPI functionality
 * sdt => expression to use for DT functionality
 *
 */
#define run_in_order(s, sacpi, sdt) \
	switch (s) { 							\
	case SEARCH_ACPI_ONLY:						\
		sacpi;							\
		break;							\
		;;							\
	case SEARCH_DT_ONLY:						\
		sdt;							\
		break;							\
		;;							\
	case SEARCH_ACPI_FIRST:						\
		sacpi;							\
		break;							\
		;;							\
	case SEARCH_DT_FIRST:						\
		sdt;							\
		break;							\
		;;							\
	default:							\
		pr_err("%s: internal error (1)", __func__);		\
	}								\
	if (s >= SEARCH_ACPI_FIRST) {					\
		s++;							\
		s = s > SEARCH_DT_FIRST ? SEARCH_ACPI_FIRST : s;	\
		switch (s) { 						\
		case SEARCH_ACPI_ONLY:					\
		case SEARCH_DT_ONLY:					\
			break;						\
			;;						\
		case SEARCH_ACPI_FIRST:					\
			sacpi;						\
			break;						\
			;;						\
		case SEARCH_DT_FIRST:					\
			sdt;						\
			break;						\
			;;						\
		default:						\
			pr_err("%s: internal error (2)", __func__);	\
		}							\
	}



/* Reference counting routines */

/**
 * gufi_node_get - Increment the reference count for a node
 * @gdn:	Node that needs the reference count incremented; NULL
 * 		is handled for caller safety.
 *
 * NOTE: ACPI does not do reference counting.
 *
 * Returns the node incremented.
 */
struct gufi_device_node *gufi_node_get(struct gufi_device_node *gdn)
{
	if (gdn) {
		if (gdn->dn)
			of_node_get(gdn->dn);
		kref_get(&gdn->kref);
	}
	return gdn;
}
EXPORT_SYMBOL(gufi_node_get);

/**
 * gufi_node_release - Release node resources for re-use
 * @kref:	kref element of the node being released
 *
 * Used as a destructor by gufi_node_put().  This is mostly a
 * placeholder for now.
 *
 */
static void gufi_node_release(struct kref *kref)
{
	struct gufi_device_node *gdn;

	gdn = container_of(kref, struct gufi_device_node, kref);

	kfree(gdn);
}

/**
 * gufi_node_put - Decrement the reference count for a node
 * @gdn:	Node that needs the reference count decremented; NULL
 * 		is handled for caller safety.
 *
 * NOTE: ACPI does not do reference counting.
 *
 */
void gufi_node_put(struct gufi_device_node *gdn)
{
	if (gdn) {
		if (gdn->dn)
			of_node_put(gdn->dn);
		kref_put(&gdn->kref, gufi_node_release);
	}
}
EXPORT_SYMBOL(gufi_node_put);


/* Search for nodes in interesting ways */

/**
 * __gufi_look_for_node	- All gufi_device_nodes a kept in a list.
 * 			  Given either a device_node or acpi_device_id
 * 			  (or both), search the list for a matching
 * 			  node.  If there is no node, make one and
 * 			  add it to the list.
 * @dn:	struct device_node to look for
 * @id:	struct acpi_device_id to look for
 *
 * Returns a pointer to the node found, if any, or creates a new node
 * and returns the address to it.
 */
static struct gufi_device_node *__gufi_look_for_node(struct device_node *dn,
						     struct acpi_device_id *id)
{
	struct gufi_device_node *pos;
	struct gufi_device_node *node = NULL;

	list_for_each_entry(pos, &__gdn_list, entry) {
		if (pos->dn == dn && pos->id == id) {
			node = pos;
			break;
		}
	}

	if (!node) {
		node = kzalloc(sizeof(struct gufi_device_node), GFP_KERNEL);
		if (node) {
			node->dn = dn;
			node->id = id;
			list_add_tail(&node->entry, &__gdn_list);
		}
	}
	return node;
}

/**
 * __gufi_find_acpi_compatible - Emulate the DT of_find_compatible_node
 * 			      using ACPI
 * @gdn:	The node to start searching from or NULL, the node
 * 		you pass will not be searched, only the next one
 *		will; typically, you pass what the previous call
 *		returned. of_node_put() will be called on it
 * @type:	The type string to match "device_type" or NULL to ignore
 * @compatible:	The string to match to one of the tokens in the device
 *		"compatible" list.
 *
 * Return an acpi_device_id pointer.
 */
static struct acpi_device_id *__gufi_find_acpi_compatible(
					struct gufi_device_node *gdn,
					const char *type,
					const char *compatible)
{
	struct acpi_device_id *id = NULL;

	/*
	 * TODO: traverse the namespace looking for a device with the right
	 * keys; the values of the keys are irrelevant here.  Will need to
	 * invoke the _PRP method to retrieve all key-value pairs.
	 */
	return id;
}

/**
 * gufi_find_compatible_node - Find a node based on type and one of the
 *                             tokens in its "compatible" property, or
 *                             by a token returned from a _DSM or _PRP
 *                             method
 * @gdn:	The node to start searching from or NULL, the node
 * 		you pass will not be searched, only the next one
 * 		will; typically, you pass what the previous call
 * 		returned. of_node_put() will be called on it
 * @type:	The type string to match "device_type" or NULL to ignore
 * @compatible:	The string to match to one of the tokens in the device
 * 		"compatible" list.
 *
 * Returns a node pointer with reference count incremented; use
 * gufi_node_put() on it when done.
 */
struct gufi_device_node *gufi_find_compatible_node(
						struct gufi_device_node *gdn,
						const char *type,
						const char *compatible)
{
	enum search_preference search = search_first;
	struct device_node *dn = NULL;
	struct acpi_device_id *id = NULL;
	struct gufi_device_node *node;

	run_in_order(search,
		     dn = of_find_compatible_node(gdn->dn, type, compatible),
		     id = __gufi_find_acpi_compatible(gdn, type, compatible)
		    );
	node = __gufi_look_for_node(dn, id);
	gufi_node_put(node);
	return node;
}


/* Addressing routines */

/**
 * __gufi_acpi_iomap - Maps the memory mapped IO for a given device_node
 * @gdn:	the device whose io range will be mapped
 *
 * Returns a pointer to the mapped memory
 */
void __iomem *__gufi_acpi_iomap(struct gufi_device_node *gdn)
{
	/*
	 * TODO: do I actually have to do an ioremap() here or has
	 * this already been taken care of?
	 */
	return NULL;
}

/**
 * gufi_iomap - Maps the memory mapped IO for a given device_node
 * @gdn:	the device whose io range will be mapped
 * @index:	index of the io range
 *
 * Returns a pointer to the mapped memory
 */
void __iomem *gufi_iomap(struct gufi_device_node *gdn, int index)
{
	enum search_preference search = search_first;
	void __iomem *ptr;

	run_in_order(search,
		     ptr = of_iomap(gdn->dn, index),
		     ptr = __gufi_acpi_iomap(gdn)
		    );
	return ptr;
}

#endif	/* CONFIG_GUFI */
