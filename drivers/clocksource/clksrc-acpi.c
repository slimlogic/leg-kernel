/*
 * Copyright (c) 2014, Hanjun Guo <hanjun.guo@linaro.org>
 * Copyright (c) 2014, Amit Daniel Kachhap <amit.daniel@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/clocksource.h>

extern struct acpi_device_id __clksrc_acpi_table[];

static const struct acpi_device_id __clksrc_acpi_table_sentinel
	__used __section(__clksrc_acpi_table_end);

void __init clocksource_acpi_init(void)
{
	const struct acpi_device_id *id;
	acpi_tbl_table_handler init_func;

	for (id = __clksrc_acpi_table; id->id[0]; id++) {
		init_func = (acpi_tbl_table_handler)id->driver_data;
		acpi_table_parse(id->id, init_func);
	}
}
