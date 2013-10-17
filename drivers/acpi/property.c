/*
 * Device Tree style properties from ACPI devices.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Darren Hart <dvhart@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/export.h>

#include "internal.h"

struct acpi_dev_property_lookup {
	const char *name;
	acpi_object_type type;
	const union acpi_object *obj;
};

void acpi_init_properties(struct acpi_device *adev)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER };

	if (ACPI_SUCCESS(acpi_get_properties(adev->handle, &buf)))
		adev->properties = buf.pointer;
}

void acpi_free_properties(struct acpi_device *adev)
{
	ACPI_FREE(adev->properties);
	adev->properties = NULL;
}

/**
 * acpi_dev_get_properties - get properties from a device
 * @adev: device to get properties from
 * @callback: callback that is called for each found property
 * @data: data passed to @callback
 *
 * Function goes over device properties and for each property @callback is
 * called. If @callback returns non-zero the iteration is terminated and
 * that return value is returned from this function.
 */
int acpi_dev_get_properties(struct acpi_device *adev,
			    int (*callback)(const union acpi_object *, void *),
			    void *data)
{
	const union acpi_object *property;
	int i, ret;

	if (!adev)
		return -EINVAL;
	if (!adev->properties)
		return -ENODATA;

	for (i = 0; i < adev->properties->package.count; i++) {
		property = &adev->properties->package.elements[i];
		ret = callback(property, data);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_properties);

/*
 * Returns 0 if the property didn̈́'t match, 1 if it did and -EINVAL if the
 * value found is not of expected type.
 */
static int acpi_dev_find_property(const union acpi_object *pkg, void *data)
{
	const union acpi_object *obj, *name = &pkg->package.elements[0];
	struct acpi_dev_property_lookup *lookup = data;

	if (strcmp(lookup->name, name->string.pointer))
		return 0;

	obj = pkg->package.count > 1 ? &pkg->package.elements[1] : NULL;

	if (lookup->type == ACPI_TYPE_ANY ||
	    (obj && lookup->type == obj->type)) {
		lookup->obj = obj;
		return 1;
	}

	return -EINVAL;
}

/**
 * acpi_dev_get_property - return an ACPI property with given name
 * @adev: ACPI device to get property
 * @name: name of the property
 * @type: expected type or %ACPI_TYPE_ANY if caller doesn't care
 * @obj: property value is placed here if not %NULL
 *
 * Function looks up a property with @name and returns the resulting ACPI
 * object in @obj if found. The returned object should not be released by
 * the caller, it is released automatically by the ACPI core when @adev is
 * removed.
 */
int acpi_dev_get_property(struct acpi_device *adev, const char *name,
			  acpi_object_type type, const union acpi_object **obj)
{
	struct acpi_dev_property_lookup lookup = {
		.name = name,
		.type = type,
	};
	int ret;

	ret = acpi_dev_get_properties(adev, acpi_dev_find_property, &lookup);
	if (ret == 1) {
		if (obj)
			*obj = lookup.obj;
		return 0;
	}
	return ret ? ret : -ENODATA;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property);

/**
 * acpi_dev_get_property_u64 - find and read 64-bit integer property
 * @adev: ACPI device to get property
 * @name: name of the property
 * @value: value of the property is placed here.
 *
 * Search for a property with the @name and if find, place the value to
 * @value. Returns %0 on success, %-ENODATA if the property is not found,
 * and %-EINVAL if the property is not in correct format.
 *
 * A sample ASL might look like this:
 * Package () { "property", 0x0000ffffffff0000 }
 */
int acpi_dev_get_property_u64(struct acpi_device *adev, const char *name,
			      u64 *value)
{
	const union acpi_object *obj;
	int ret;

	ret = acpi_dev_get_property(adev, name, ACPI_TYPE_INTEGER, &obj);
	if (!ret)
		*value = (u64)obj->integer.value;
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_u64);

/**
 * acpi_dev_get_property_u32 - find and read 32-bit integer property
 * @adev: ACPI device to get property
 * @name: name of the property
 * @value: value of the property is placed here.
 *
 * Search for a property with the @name and if find, place the value to
 * @value. Returns %0 on success, %-ENODATA if the property is not found,
 * and %-EINVAL if the property is not in correct format.
 *
 * A sample ASL might look like this:
 * Package () { "property", 0x0ffffff0 }
 */
int acpi_dev_get_property_u32(struct acpi_device *adev, const char *name,
			      u32 *value)
{
	u64 tmp;
	int ret;

	ret = acpi_dev_get_property_u64(adev, name, &tmp);
	if (!ret)
		*value = tmp;
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_u32);

/**
 * acpi_dev_get_property_u16 - find and read 16-bit integer property
 * @adev: ACPI device to get property
 * @name: name of the property
 * @value: value of the property is placed here.
 *
 * Search for a property with the @name and if find, place the value to
 * @value. Returns %0 on success, %-ENODATA if the property is not found,
 * and %-EINVAL if the property is not in correct format.
 *
 * A sample ASL might look like this:
 * Package () { "property", 0x0ff0 }
 */
int acpi_dev_get_property_u16(struct acpi_device *adev, const char *name,
			      u16 *value)
{
	u64 tmp;
	int ret;

	ret = acpi_dev_get_property_u64(adev, name, &tmp);
	if (!ret)
		*value = tmp;
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_u16);

/**
 * acpi_dev_get_property_u8 - find and read 8-bit integer property
 * @adev: ACPI device to get property
 * @name: name of the property
 * @value: value of the property is placed here.
 *
 * Search for a property with the @name and if find, place the value to
 * @value. Returns %0 on success, %-ENODATA if the property is not found,
 * and %-EINVAL if the property is not in correct format.
 *
 * A sample ASL might look like this:
 * Package () { "property", 0x3c }
 */
int acpi_dev_get_property_u8(struct acpi_device *adev, const char *name,
			     u8 *value)
{
	u64 tmp;
	int ret;

	ret = acpi_dev_get_property_u64(adev, name, &tmp);
	if (!ret)
		*value = tmp;
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_u8);

static int acpi_dev_get_property_array(struct acpi_device *adev,
				       const char *name, acpi_object_type type,
				       const union acpi_object **ret_obj)
{
	const union acpi_object *obj;
	int ret, i;

	ret = acpi_dev_get_property(adev, name, ACPI_TYPE_PACKAGE, &obj);
	if (ret)
		return ret;

	/* Check that all elements are of correct type */
	for (i = 0; i < obj->package.count; i++)
		if (obj->package.elements[i].type != type)
			return -EINVAL;

	*ret_obj = obj;
	return 0;
}

/**
 * acpi_dev_get_property_array_u64 - find and read array of u64 from a property
 * @adev: ACPI device to get property
 * @name: name of the property
 * @values: array where the data is placed
 * @nvalues: number of elements in @values array
 *
 * Copies integer properties array with @name into @values and returns
 * number of items in the actual array or %-ENODATA if the property doesn't
 * exists, %-EINVAL if the array format is invalid. @values and @nvalues
 * can be set to %NULL and %0 respectively. In that case the function
 * returns number of items in the array but doesn't touch @values.
 *
 * A sample ASL might look like this:
 * Package () { "property", Package () { 1, 2, ... } }
 */
int acpi_dev_get_property_array_u64(struct acpi_device *adev, const char *name,
				    u64 *values, size_t nvalues)
{
	const union acpi_object *obj;
	int ret;

	ret = acpi_dev_get_property_array(adev, name, ACPI_TYPE_INTEGER, &obj);
	if (ret)
		return ret;

	if (values) {
		int i;

		for (i = 0; i < obj->package.count && i < nvalues; i++)
			values[i] = obj->package.elements[i].integer.value;
	}

	return obj->package.count;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_array_u64);

/**
 * acpi_dev_get_property_array_u32 - find and read array of u32 from a property
 * @adev: ACPI device to get property
 * @name: name of the property
 * @values: array where the data is placed
 * @nvalues: number of elements in @values array
 *
 * Copies integer properties array with @name into @values and returns
 * number of items in the actual array or %-ENODATA if the property doesn't
 * exists, %-EINVAL if the array format is invalid. @values and @nvalues
 * can be set to %NULL and %0 respectively. In that case the function
 * returns number of items in the array but doesn't touch @values.
 *
 * A sample ASL might look like this:
 * Package () { "property", Package () { 1, 2, ... } }
 */
int acpi_dev_get_property_array_u32(struct acpi_device *adev, const char *name,
				    u32 *values, size_t nvalues)
{
	const union acpi_object *obj;
	int ret;

	ret = acpi_dev_get_property_array(adev, name, ACPI_TYPE_INTEGER, &obj);
	if (ret)
		return ret;

	if (values) {
		int i;

		for (i = 0; i < obj->package.count && i < nvalues; i++)
			values[i] = obj->package.elements[i].integer.value;
	}

	return obj->package.count;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_array_u32);

/**
 * acpi_dev_get_property_array_u16 - find and read array of u16 from a property
 * @adev: ACPI device to get property
 * @name: name of the property
 * @values: array where the data is placed
 * @nvalues: number of elements in @values array
 *
 * Copies integer properties array with @name into @values and returns
 * number of items in the actual array or %-ENODATA if the property doesn't
 * exists, %-EINVAL if the array format is invalid. @values and @nvalues
 * can be set to %NULL and %0 respectively. In that case the function
 * returns number of items in the array but doesn't touch @values.
 *
 * A sample ASL might look like this:
 * Package () { "property", Package () { 1, 2, ... } }
 */
int acpi_dev_get_property_array_u16(struct acpi_device *adev, const char *name,
				    u16 *values, size_t nvalues)
{
	const union acpi_object *obj;
	int ret;

	ret = acpi_dev_get_property_array(adev, name, ACPI_TYPE_INTEGER, &obj);
	if (ret)
		return ret;

	if (values) {
		int i;

		for (i = 0; i < obj->package.count && i < nvalues; i++)
			values[i] = obj->package.elements[i].integer.value;
	}

	return obj->package.count;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_array_u16);

/**
 * acpi_dev_get_property_array_u8 - find and read array of u8 from a property
 * @adev: ACPI device to get property
 * @name: name of the property
 * @values: array where the data is placed. Caller allocated can be %NULL.
 * @nvalues: number of items in @values array
 *
 * Copies integer properties array with @name into @values and returns
 * number of items in the actual array or %-ENODATA if the property doesn't
 * exists, %-EINVAL if the array format is invalid. @values and @nvalues
 * can be set to %NULL and %0 respectively. In that case the function
 * returns number of items in the array but doesn't touch @values.
 *
 * Function treats ACPI types package and buffer the same. It first looks
 * for a package and then falls back to a buffer.
 *
 * A sample ASL might look like this if package is used:
 * Package () { "property", Package () { 1, 2, ... } }
 *
 * And like this if buffer is used:
 * Package () { "property", Buffer () { 1, 2, ... } }
 */
int acpi_dev_get_property_array_u8(struct acpi_device *adev, const char *name,
				   u8 *values, size_t nvalues)
{
	const union acpi_object *obj;
	int ret, i;

	ret = acpi_dev_get_property_array(adev, name, ACPI_TYPE_INTEGER, &obj);
	if (!ret) {
		if (values) {
			const union acpi_object *elements;

			elements = obj->package.elements;
			for (i = 0; i < obj->package.count && i < nvalues; i++)
				values[i] = elements[i].integer.value;
		}
		return obj->package.count;
	}

	ret = acpi_dev_get_property(adev, name, ACPI_TYPE_BUFFER, &obj);
	if (ret)
		return ret;

	if (values) {
		for (i = 0; i < obj->buffer.length && i < nvalues; i++)
			values[i] = obj->buffer.pointer[i];
	}

	return obj->buffer.length;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_array_u8);

/**
 * acpi_dev_get_property_string - returns string property value
 * @adev: ACPI device to get property
 * @name: name of the property
 * @value: pointer to the returned string
 *
 * Finds property with @name, and places pointer to the string value to
 * @value. The memory pointed by @value should not be released by the
 * called but it will be released when the corresponding ACPI device object
 * is removed.
 *
 * A sample ASL might look like this:
 * Package () { "property", "my string property value" }
 */
int acpi_dev_get_property_string(struct acpi_device *adev, const char *name,
				 const char **value)
{
	const union acpi_object *obj;
	int ret;

	ret = acpi_dev_get_property(adev, name, ACPI_TYPE_STRING, &obj);
	if (!ret)
		*value = obj->string.pointer;
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_string);

/**
 * acpi_dev_get_property_array_string - find and read an array of strings
 * @adev: ACPI device to get property
 * @name: name of the property
 * @values: array where strings are placed
 * @nvalues: number of items in @values array
 *
 * Finds property with @name, verifies that it contains an array of strings
 * and if so, fills in @values with pointers to those strings. Note that
 * the caller shouldn't try to release those pointers. They are owned by
 * the ACPI device @adev.
 *
 * String pointers will remain valid as long as the corresponding ACPI
 * device object exists.
 *
 * A sample ASL might look like this:
 * Package () {
 *	"property",
 *	Package () { "my first string", "my second string" }
 * }
 */
int acpi_dev_get_property_array_string(struct acpi_device *adev,
				       const char *name, const char **values,
				       size_t nvalues)
{
	const union acpi_object *obj;
	int ret;

	ret = acpi_dev_get_property_array(adev, name, ACPI_TYPE_STRING, &obj);
	if (ret)
		return ret;

	if (values) {
		const union acpi_object *elements = obj->package.elements;
		int i;

		for (i = 0; i < obj->package.count && i < nvalues; i++)
			values[i] = elements[i].string.pointer;
	}

	return obj->package.count;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_array_string);

/**
 * acpi_dev_get_property_reference - returns handle to the referenced object
 * @adev: ACPI device to get property
 * @name: name of the property
 * @obj_handle: pointer to acpi_handle where the found ACPI handle is placed
 *
 * Function finds property with @name, verififies that it is an object
 * reference and if so, returns the ACPI handle of the referenced object in
 * @obj_handle. Returns %0 in case of success, %-ENODATA if the property
 * doesn't exists or doesn't have a value, and %-EINVAL if the property
 * value is not a reference.
 *
 * A sample ASL might look like this:
 * Package () { "property", \_SB.PCI0.LPC }
 */
int acpi_dev_get_property_reference(struct acpi_device *adev, const char *name,
				    acpi_handle *obj_handle)
{
	const union acpi_object *obj;
	int ret;

	ret = acpi_dev_get_property(adev, name, ACPI_TYPE_LOCAL_REFERENCE,
				    &obj);
	if (!ret)
		*obj_handle = obj->reference.handle;
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_reference);
