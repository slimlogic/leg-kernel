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

#ifndef _GUFI_ACPI_PROTOCOL_H
#define _GUFI_ACPI_PROTOCOL_H

#ifdef CONFIG_GUFI

#include <linux/gufi.h>
#include <linux/of.h>

extern struct gufi_device_node *gufi_acpi_find_first_node(const char *name);
extern struct gufi_device_node *gufi_acpi_node_get(struct gufi_device_node *gdn);
extern void gufi_acpi_node_put(struct gufi_device_node *gdn);

#endif	/* CONFIG_GUFI */

#endif	/*_GUFI_ACPI_PROTOCOL_H */
