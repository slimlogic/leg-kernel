/*******************************************************************************
 *
 * Module Name: prxface - Public interface to ACPI device properties
 *
 ******************************************************************************/

/*
 * Copyright (C) 2013, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <linux/export.h>
#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("prxface")

/*******************************************************************************
 *
 * FUNCTION:    acpi_pr_validate_parameters
 *
 * PARAMETERS:  device_handle   - Handle to a device
 *              buffer          - Pointer to a data buffer
 *              return_node     - Pointer to where the device node is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Checks validity of the parameters passed to
 *              acpi_get_device_properties().
 *
 ******************************************************************************/
static acpi_status
acpi_pr_validate_parameters(acpi_handle device_handle,
			    struct acpi_buffer *buffer,
			    struct acpi_namespace_node **return_node)
{
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_pr_validate_parameters);

	if (!device_handle) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	node = acpi_ns_validate_handle(device_handle);
	if (!node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (node->type != ACPI_TYPE_DEVICE) {
		return_ACPI_STATUS(AE_TYPE);
	}

	/* Make sure the buffer is valid */
	status = acpi_ut_validate_buffer(buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	*return_node = node;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_prp_method_data
 *
 * PARAMETERS:  node            - Device node
 *              ret_buffer      - Pointer to a buffer structure where the
 *                                resulting properties are stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to evaluate _PRP method for a given
 *              device. In case of success resulting package is stored in
 *              ret_buffer.
 *
 *              If the function fails an appropriate status will be returned and
 *              the contents of the callers buffer is undefined.
 *
 ******************************************************************************/
static acpi_status
acpi_pr_get_prp_method_data(struct acpi_namespace_node *node,
			    struct acpi_buffer *ret_buffer)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object **properties;
	acpi_status status;
	acpi_size size;
	int i;

	ACPI_FUNCTION_TRACE(acpi_pr_get_prp_method_data);

	status = acpi_ut_evaluate_object(node, METHOD_NAME__PRP,
					 ACPI_BTYPE_PACKAGE, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Validate that the returned package is in correct format:
	 *  1) Each property is a package itself holding { key, value }
	 *  2) Key is mandatory
	 *  3) Key must be string.
	 */
	properties = obj_desc->package.elements;
	for (i = 0; i < obj_desc->package.count; i++) {
		union acpi_operand_object **property;

		if (properties[i]->common.type != ACPI_TYPE_PACKAGE) {
			status = AE_BAD_DATA;
			goto out;
		}
		if (properties[i]->package.count < 1) {
			status = AE_BAD_DATA;
			goto out;
		}

		property = properties[i]->package.elements;
		if (property[0]->common.type != ACPI_TYPE_STRING) {
			status = AE_BAD_DATA;
			goto out;
		}
	}

	/* Copy the internal buffer to ret_buffer */
	status = acpi_ut_get_object_size(obj_desc, &size);
	if (ACPI_FAILURE(status)) {
		goto out;
	}

	status = acpi_ut_initialize_buffer(ret_buffer, size);
	if (ACPI_SUCCESS(status)) {
		status = acpi_ut_copy_iobject_to_eobject(obj_desc, ret_buffer);
	}

      out:
	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_properties
 *
 * PARAMETERS:  device_handle   - Handle to the device object for the
 *                                device we are querying
 *              ret_buffer      - Pointer to a buffer to receive the
 *                                properties for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get properties for
 *              a specific device The caller must first acquire a handle for the
 *              desired device The property data is placed in the buffer pointed
 *              to by the ret_buffer variable parameter.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of ret_buffer is undefined.
 *
 *              This function attempts to execute the _PRP method contained in
 *              the object indicated by the passed device_handle.
 *
 ******************************************************************************/
acpi_status
acpi_get_properties(acpi_handle device_handle, struct acpi_buffer *ret_buffer)
{
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_device_properties);

	status = acpi_pr_validate_parameters(device_handle, ret_buffer, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_pr_get_prp_method_data(node, ret_buffer);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_properties)
