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
#elif IS_ENABLED(CONFIG_GUFI_DT_ONLY)
#define DEFAULT_PREFERENCE	SEARCH_DT_ONLY
#elif IS_ENABLED(CONFIG_GUFI_ACPI_FIRST)
#define DEFAULT_PREFERENCE	SEARCH_ACPI_FIRST
#elif IS_ENABLED(CONFIG_GUFI_DT_FIRST)
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

/* Utility routines */

/**
 * __gufi_get_mem32fixed - get the first Memory32Fixed resource descriptor
 * @res:	the acpi_resource provided to this callback function
 * @data:	a struct acpi_resouce_fixed_memory32 to be filled out
 *
 * Returns AE_OK if found, AE_ERROR otherwise
 */
static acpi_status __gufi_get_mem32fixed(struct acpi_resource *res, void *data)
{
	if (!res)
		return AE_ERROR;

	if (res->type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
		return AE_OK;
	}

	return AE_ERROR;
}

/**
 * __gufi_look_for_acpi	- All gufi_device_nodes a kept in a list.
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
 * __gufi_look_for_dt	- All gufi_device_nodes a kept in a list.
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


/* Reference counting routines */

/*
 * NOTE: ACPI does not do reference counting the same way as DT; it is
 * handled by very low level routines instead (acpi_ut_add_reference() and
 * acpi_ut_remove_reference()) and is done for the user inside the ACPICA
 * code instead of being done outside the code explicitly as it is for the
 * DT code.
 */

/**
 * gufi_node_get - Increment the reference count for a node
 * @gdn:	Node that needs the reference count incremented; NULL
 * 		is handled for caller safety.
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
EXPORT_SYMBOL(gufi_node_release);

/**
 * gufi_node_put - Decrement the reference count for a node
 * @gdn:	Node that needs the reference count decremented; NULL
 * 		is handled for caller safety.
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


/* Tree walking routines */

/**
 * __gufi_acpi_next_parent -- iterate to the node's parent in ACPI
 * @gdn:	node to get parent of
 *
 * Returns pointer to next parent.
 */
struct acpi_device *__gufi_acpi_get_next_parent(struct gufi_device_node *gdn)
{
	acpi_status status;
	acpi_handle parent;
	struct acpi_device *device;

	if (!gdn || !gdn->an)
		return NULL;

	status = acpi_get_parent(ACPI_HANDLE(&gdn->an->dev), &parent);
	if (ACPI_FAILURE(status))
		return NULL;
	if (acpi_bus_get_device(parent, &device))
		return NULL;

	return device;
}

/**
 * gufi_get_next_parent - Iterate to a node's parent
 * @node:	Node to get parent of
 *
 *  This is like of_get_parent() except that it drops the
 *  refcount on the passed node, making it suitable for iterating
 *  through a node's parents.
 *
 * Returns a node pointer with refcount incremented, use
 * gufi_node_put() on it when done.
 */
struct gufi_device_node *gufi_get_next_parent(struct gufi_device_node *gdn)
{
	enum search_preference search = search_first;
	struct device_node *dn;
	struct acpi_device *an;

	run_in_order(search,
		     an = __gufi_acpi_get_next_parent(gdn),
		     dn = of_get_next_parent(gdn->dn)
		    );
	return NULL;
}
EXPORT_SYMBOL(gufi_get_next_parent);


/* Search for nodes in interesting ways */

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
	enum search_preference search = search_first;

	pr_debug("GUFI: entering gufi_look_for_node\n");
	pr_debug("GUFI: gufi_look_for_node: dn = 0x%p\n", dn);
	pr_debug("GUFI: gufi_look_for_node: an = 0x%p\n", an);

	run_in_order(search,
		     ga = __gufi_look_for_acpi(an),
		     gd = __gufi_look_for_dt(dn)
		    );
	pr_debug("GUFI: gufi_look_for_node: ga = 0x%p\n", ga);
	pr_debug("GUFI: gufi_look_for_node: gd = 0x%p\n", gd);
	if (ga || gd )
		return ga ? ga : gd;

	if (!(ga || gd)) {
		gdn = kzalloc(sizeof(struct gufi_device_node), GFP_KERNEL);
		if (gdn) {
			pr_debug("GUFI: gufi_look_for_node: gdn = 0x%p\n", gdn);
			gdn->dn = dn;
			gdn->an = an;
			//kref_init(&gdn->kref);
			list_add_tail(&gdn->entry, &__gdn_list);
		}
	}

	pr_debug("GUFI: leaving gufi_look_for_node\n");

	return gdn;
}
EXPORT_SYMBOL(gufi_look_for_node);

/**
 * __gufi_acpi_find_callback -
 */
static acpi_status __gufi_acpi_find_callback(acpi_handle handle,
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

/**
 * __gufi_find_acpi_compatible - Emulate the DT of_find_compatible_node
 * 			      using ACPI
 * @gdn:	The node to start searching from or NULL, the node
 * 		you pass will not be searched, only the next one
 *		will; typically, you pass what the previous call
 *		returned.
 * @type:	The type string to match "device_type" or NULL to ignore
 * @compatible:	The string to match to one of the tokens in the device
 *		"compatible" list.
 *
 * Return an acpi_device_id pointer.
 */
static struct acpi_device *__gufi_find_acpi_compatible(
					struct gufi_device_node *gdn,
					const char *type,
					const char *compatible)
{
	struct acpi_device *an = NULL;
	void *device = NULL;
	acpi_handle handle;

	pr_debug("GUFI: entering __gufi_find_acpi_compatible\n");

	/*
	 * Traverse the namespace looking for a device with the right
	 * compatible key-value pair.  Will need to invoke the _PRP method
	 * to retrieve all key-value pairs and get the compatible property.
	 */
	if (gdn)
		handle = gdn->an ? gdn->an->handle : 0;
	else
		handle = ACPI_ROOT_OBJECT;
	acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, ACPI_UINT32_MAX,
			    __gufi_acpi_find_callback, NULL,
			    (void *)compatible, &device);

	an = (struct acpi_device *)device;

	pr_debug("GUFI: leaving __gufi_find_acpi_compatible\n");

	return an;
}

/**
 * gufi_find_compatible_node - Find a node based on type and one of the
 *                             tokens in its "compatible" property, or
 *                             by a token returned from a _PRP method
 * @gdn:	The node to start searching from or NULL, the node
 * 		you pass will not be searched, only the next one
 * 		will; typically, you pass what the previous call
 * 		returned. gufi_node_put() will be called on it
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
	struct acpi_device *an = NULL;
	struct gufi_device_node *node;

	pr_debug("GUFI: entering gufi_find_compatible_node\n");

	dn = gdn ? gdn->dn : NULL;
	run_in_order(search,
		     an = __gufi_find_acpi_compatible(gdn, type, compatible),
		     dn = of_find_compatible_node(dn, type, compatible)
		    );
	node = gufi_look_for_node(dn, an);
	gufi_node_put(node);

	pr_debug("GUFI: leaving gufi_find_compatible_node\n");

	return node;
}
EXPORT_SYMBOL(gufi_find_compatible_node);

/**
 * __gufi_acpi_find_phandle -
 */
static acpi_status __gufi_acpi_find_phandle(acpi_handle handle,
					    u32 lvl_not_used,
					    void *phandle_in,
					    void **return_value)
{
	struct acpi_device *device = NULL;
	phandle handle_wanted = *(phandle *)phandle_in;
	phandle current_handle;

	current_handle = (phandle)((unsigned long)handle & ACPI_UINT32_MAX);
	if (current_handle != handle_wanted)
		return AE_NOT_FOUND;

	if (acpi_bus_get_device(handle, &device))
		return AE_NOT_FOUND;

	if (!*return_value)
		*return_value = device;

	return AE_OK;
}
/**
 * __gufi_acpi_find_by_phandle - find an ACPI node by "phandle"; we let
 * 				 the lower 32-bits of the acpi_handle
 * 				 stand in for the DT phandle value
 * @handle:	a DT phandle
 *
 * Returns a pointer to the ACPI node.
 */
static struct acpi_device *__gufi_acpi_find_by_phandle(phandle handle)
{
	phandle tmp = handle;
	void *device;

	acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			    __gufi_acpi_find_phandle, NULL,
			    (void *)&tmp, &device);
	return (struct acpi_device *)device;
}

/**
 * gufi_find_node_by_phandle - Find a node given a phandle
 * @handle:	phandle of the node to find
 *
 * Returns a node pointer with refcount incremented, use
 * gufi_node_put() on it when done.
 */
struct gufi_device_node *gufi_find_node_by_phandle(phandle handle)
{
	enum search_preference search = search_first;
	struct gufi_device_node *gdn;
	struct device_node *dn;
	struct acpi_device *an;

	run_in_order(search,
		     an = __gufi_acpi_find_by_phandle(handle),
		     dn = of_find_node_by_phandle(handle)
		    );
	gdn = gufi_look_for_node(dn, an);
	gufi_node_put(gdn);
	return gdn;
}
EXPORT_SYMBOL(gufi_find_node_by_phandle);


/* Retrieve values for specific properties */

/**
 * __gufi_acpi_get_property - helper function for calling ACPI properly in
 * 			      order to get a pointer to any property value
 * @gdn:	device node from which to get the property
 * @name:	name of the property to get
 *
 * Returns the value of the property of the node with the given name.
 */
const void *__gufi_acpi_get_property(const struct gufi_device_node *gdn,
				     const char *name,
				     int *lenp)
{
	const union acpi_object *obj;

	pr_debug("GUFI: entering __gufi_acpi_get_property\n");

	if  (!gdn || !gdn->an || !gdn->an->handle)
		return NULL;

	pr_debug("GUFI: __gufi_acpi_get_property: gdn = 0x%p\n", gdn);
	pr_debug("GUFI: __gufi_acpi_get_property: gdn->an = 0x%p\n", gdn->an);
	pr_debug("GUFI: __gufi_acpi_get_property: gdn->an->device_type = 0x%0x\n", gdn->an->device_type);
	pr_debug("GUFI: __gufi_acpi_get_property: gdn->an->handle = 0x%p\n", gdn->an->handle);
	pr_debug("GUFI: __gufi_acpi_get_property: gdn->an->properties = 0x%p\n", gdn->an->properties);
	pr_debug("GUFI: __gufi_acpi_get_property: name = %s\n", name);
	if (acpi_dev_get_property(gdn->an, name, ACPI_TYPE_ANY, &obj))
		return NULL;
	else
		return (const void *)obj;
}

/**
 * gufi_get_property - Find a pointer to a node property
 * @gdn:	device node from which to get the property
 * @name:	name of the property to get
 *
 * Returns the value of the property of the node with the given name.
 */
const void *gufi_get_property(const struct gufi_device_node *gdn,
			      const char *name,
			      int *lenp)
{
	enum search_preference search = search_first;
	struct device_node *dn;
	const void *ptr;

	dn = gdn ? gdn->dn : NULL;
	run_in_order(search,
		     ptr = __gufi_acpi_get_property(gdn, name, lenp),
		     ptr = of_get_property(gdn->dn, name, lenp)
		    );
	return ptr;
}
EXPORT_SYMBOL(gufi_get_property);

/**
 * gufi_property_read_string - Find and read a string from a property
 * @gdn:	device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_string:	pointer to null terminated return string, modified only if
 *		return value is 0.
 *
 * Search for a property in a device tree node and retrieve a null
 * terminated string value (pointer to data, not a copy). Returns 0 on
 * success, -EINVAL if the property does not exist, -ENODATA if property
 * does not have a value, and -EILSEQ if the string is not null-terminated
 * within the length of the property data.
 *
 * The out_string pointer is modified only if a valid string can be decoded.
 */
int gufi_property_read_string(struct gufi_device_node *gdn,
			      const char *propname,
			      const char **out_string)
{
	enum search_preference search = search_first;
	struct device_node *dn;
	struct acpi_device *an;

	if (gdn) {
		dn = gdn->dn;
		an = gdn->an;
	} else {
		return -EINVAL;
	}
	run_in_order(search,
		     return acpi_dev_get_property_string(an, propname, \
		     					 out_string),
		     return of_property_read_string(dn, propname, out_string)
		    );
	return -ENOSYS;
}
EXPORT_SYMBOL(gufi_property_read_string);

/**
 * gufi_property_read_u32 - Find and read a 32 bit integer from a property.
 *
 * @gdn:	device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_value:	pointer to return value, modified only if return value is 0.
 *
 * Search for a property in a device node and read a 32-bit value from
 * it. Returns 0 on success, -EINVAL if the property does not exist,
 * -ENODATA if property does not have a value, and -EOVERFLOW if the
 * property data isn't large enough.
 *
 * The out_value is modified only if a valid u32 value can be decoded.
 */
int gufi_property_read_u32(const struct gufi_device_node *gdn,
			   const char *propname, u32 *out_value)
{
	enum search_preference search = search_first;
	struct device_node *dn;
	struct acpi_device *an;

	if (gdn) {
		dn = gdn->dn;
		an = gdn->an;
	} else {
		return -EINVAL;
	}

	run_in_order(search,
		     return acpi_dev_get_property_u32(an, propname, out_value),
		     return of_property_read_u32(dn, propname, out_value)
		    );
	return -ENOSYS;
}
EXPORT_SYMBOL(gufi_property_read_u32);

/**
 * gufi_property_read_u32_array - Find and read an array of 32 bit integers
 * from a property.
 *
 * @gdn:	device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device node and read 32-bit value(s) from
 * it. Returns 0 on success, -EINVAL if the property does not exist,
 * -ENODATA if property does not have a value, and -EOVERFLOW if the
 * property data isn't large enough.
 *
 * The out_values is modified only if a valid u32 value can be decoded.
 */
int gufi_property_read_u32_array(const struct gufi_device_node *gdn,
			         const char *propname, u32 *out_values,
			         size_t sz)
{
	enum search_preference search = search_first;
	struct device_node *dn;
	struct acpi_device *an;

	if (gdn) {
		dn = gdn->dn;
		an = gdn->an;
	} else {
		return -EINVAL;
	}


	run_in_order(search,
		     return acpi_dev_get_property_array_u32(an, propname, \
		     				            out_values, sz),
		     return of_property_read_u32_array(dn, propname, \
		     				       out_values, sz)
		    );
	return -ENOSYS;
}
EXPORT_SYMBOL(gufi_property_read_u32_array);



/* Addressing routines */

/**
 * __gufi_acpi_iomap - Maps the memory mapped IO for a given device_node
 * @gdn:	the device whose io range will be mapped
 *
 * Returns a pointer to the mapped memory
 */
void __iomem *__gufi_acpi_iomap(struct gufi_device_node *gdn, int index)
{
	acpi_status status;
	struct acpi_resource_fixed_memory32 data;

	pr_debug("GUFI: entering __gufi_acpi_iomap\n");

	if (!gdn || !gdn->an)
		return NULL;
	pr_debug("GUFI: __gufi_acpi_iomap: gdn = 0x%p\n", gdn);
	pr_debug("GUFI: __gufi_acpi_iomap: gdn->an = 0x%p\n", gdn->an);

	/* TODO: use acpi_get_current_resources() instead? */
	memset(&data, 0, sizeof(struct acpi_resource_fixed_memory32));
	status = acpi_walk_resources(gdn->an->handle, METHOD_NAME__CRS,
				     __gufi_get_mem32fixed, &data);
	if (ACPI_SUCCESS(status))
		return acpi_os_map_memory((acpi_physical_address)data.address,
					  (acpi_size)data.address_length);

	pr_debug("GUFI: leaving __gufi_acpi_iomap\n");

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

	pr_debug("GUFI: entering gufi_iomap\n");

	if (!gdn)
		return NULL;

	run_in_order(search,
		     ptr = __gufi_acpi_iomap(gdn, index),
		     ptr = of_iomap(gdn->dn, index)
		    );

	pr_debug("GUFI: leaving gufi_iomap\n");

	return ptr;
}

