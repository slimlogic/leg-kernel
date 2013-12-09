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

#ifdef CONFIG_OF

#include <linux/gufi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* Platform Device */

static int gufi_of_probe(struct platform_device *pdev)
{
	return -EINVAL;
}

static int gufi_of_remove(struct platform_device *pdev)
{
	return -EINVAL;
}

/* Platform Driver */

static struct of_device_id gufi_of_match[] = {
	{ .compatible = "gufi,of", },
	{ }
};
MODULE_DEVICE_TABLE(gufi, gufi_of_match);

static struct platform_driver gufi_of_driver = {
	.probe		= gufi_of_probe,
	.remove		= gufi_of_remove,
	.driver		= {
		.name	= "gufi-of-test",
		.owner	= THIS_MODULE,
		.of_match_table = gufi_of_match,
	},
};

static int __init gufi_of_init(void)
{
	return platform_driver_register(&gufi_of_driver);
}

static void __init gufi_of_exit(void)
{
	platform_driver_unregister(&gufi_of_driver);
}

module_init(gufi_of_init);
module_exit(gufi_of_exit);

MODULE_AUTHOR("Al Stone <al.stone@linaro.org>");
MODULE_DESCRIPTION("DT Test Driver for the Grand Unified Firmware Interface");
MODULE_LICENSE("GPL");

#endif	/* CONFIG_OF */

#endif	/* CONFIG_GUFI */
