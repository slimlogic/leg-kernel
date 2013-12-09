/*
 * Grand Unified Firmware Interface -- test driver
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

#ifdef CONFIG_GUFI

#ifdef CONFIG_ACPI

#include <linux/gufi.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>

/* Platform Device */

static int gufi_acpi_probe(struct platform_device *pdev)
{
	pr_debug("entering gufi_acpi_probe\n");
	return -EINVAL;
}

static int gufi_acpi_remove(struct platform_device *pdev)
{
	return -EINVAL;
}

/* Platform Driver */

static struct acpi_device_id gufi_acpi_match[] = {
	{ "LNRO000B", },
	{ }
};
MODULE_DEVICE_TABLE(gufi, gufi_acpi_match);

static struct platform_driver gufi_acpi_driver = {
	.probe		= gufi_acpi_probe,
	.remove		= gufi_acpi_remove,
	.driver		= {
		.name	= "gufi-acpi-test",
		.owner	= THIS_MODULE,
		.acpi_match_table = gufi_acpi_match,
	},
};

static int __init gufi_acpi_init(void)
{
	return platform_driver_register(&gufi_acpi_driver);
}

static void __init gufi_acpi_exit(void)
{
	platform_driver_unregister(&gufi_acpi_driver);
}

module_init(gufi_acpi_init);
module_exit(gufi_acpi_exit);

MODULE_AUTHOR("Al Stone <al.stone@linaro.org>");
MODULE_DESCRIPTION("DT Test Driver for the Grand Unified Firmware Interface");
MODULE_LICENSE("GPL");

#endif	/* CONFIG_OF */

#endif	/* CONFIG_GUFI */
