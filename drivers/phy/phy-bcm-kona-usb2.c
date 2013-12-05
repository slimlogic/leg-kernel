/*
 * phy-bcm-kona-usb2.c - Broadcom Kona USB2 Phy Driver
 *
 * Copyright (C) 2013 Linaro Limited
 * Matt Porter <matt.porter@linaro.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/phy/phy.h>

#define OTGCTL_OTGSTAT2		(1 << 31)
#define OTGCTL_OTGSTAT1		(1 << 30)
#define OTGCTL_PRST_N_SW	(1 << 11)
#define OTGCTL_HRESET_N		(1 << 10)
#define OTGCTL_UTMI_LINE_STATE1	(1 << 9)
#define OTGCTL_UTMI_LINE_STATE0	(1 << 8)

#define P1CTL_SOFT_RESET	(1 << 1)
#define P1CTL_NON_DRIVING	(1 << 0)

struct bcm_kona_usb_phy_regs {
	u32 ctrl;
	u32 cfg;
	u32 p1ctl;
	u32 status;
	u32 bc_cfg;
	u32 tp_in;
	u32 tp_out;
	u32 phy_ctrl;
	u32 usbreg;
	u32 usbproben;
};

struct bcm_kona_usb {
	struct bcm_kona_usb_phy_regs *regs;
};

static void bcm_kona_usb_phy_power(struct bcm_kona_usb *phy, int on)
{
	u32 val;

	val = readl(&phy->regs->ctrl);
	if (on) {
		/* Configure and power PHY */
		val &= ~(OTGCTL_OTGSTAT2 | OTGCTL_OTGSTAT1 |
			 OTGCTL_UTMI_LINE_STATE1 | OTGCTL_UTMI_LINE_STATE0);
		val |= OTGCTL_PRST_N_SW | OTGCTL_HRESET_N;
		writel(val, &phy->regs->ctrl);

		/* Soft reset PHY */
		val = readl(&phy->regs->p1ctl);
		val &= ~P1CTL_NON_DRIVING;
		val |= P1CTL_SOFT_RESET;
		writel(val, &phy->regs->p1ctl);
		writel(val & ~P1CTL_SOFT_RESET, &phy->regs->p1ctl);
		/* Reset needs to be asserted for 2ms */
		mdelay(2);
		writel(val | P1CTL_SOFT_RESET, &phy->regs->p1ctl);
	} else {
		val &= ~(OTGCTL_PRST_N_SW | OTGCTL_HRESET_N);
		writel(val, &phy->regs->ctrl);
	}
}

static int bcm_kona_usb_phy_power_on(struct phy *gphy)
{
	struct bcm_kona_usb *phy = phy_get_drvdata(gphy);

	bcm_kona_usb_phy_power(phy, 1);

	return 0;
}

static int bcm_kona_usb_phy_power_off(struct phy *gphy)
{
	struct bcm_kona_usb *phy = phy_get_drvdata(gphy);

	bcm_kona_usb_phy_power(phy, 0);

	return 0;
}

static struct phy_ops ops = {
	.power_on	= bcm_kona_usb_phy_power_on,
	.power_off	= bcm_kona_usb_phy_power_off,
	.owner = THIS_MODULE,
};

static int bcm_kona_usb2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm_kona_usb *phy;
	struct resource *res;
	struct phy *gphy;
	struct phy_provider *phy_provider;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(phy->regs))
		return PTR_ERR(phy->regs);

	platform_set_drvdata(pdev, phy);

	phy_provider = devm_of_phy_provider_register(dev,
			of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	gphy = devm_phy_create(dev, &ops, NULL);
	if (IS_ERR(gphy))
		return PTR_ERR(gphy);

	/* The Kona PHY supports an 8-bit wide UTMI interface */
	phy_set_bus_width(gphy, 8);

	phy_set_drvdata(gphy, phy);

	return 0;
}

static const struct of_device_id bcm_kona_usb2_dt_ids[] = {
	{ .compatible = "brcm,kona-usb2-phy" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, bcm_kona_usb2_dt_ids);

static struct platform_driver bcm_kona_usb2_driver = {
	.probe		= bcm_kona_usb2_probe,
	.driver		= {
		.name	= "bcm-kona-usb2",
		.owner	= THIS_MODULE,
		.of_match_table = bcm_kona_usb2_dt_ids,
	},
};

module_platform_driver(bcm_kona_usb2_driver);

MODULE_ALIAS("platform:bcm-kona-usb2");
MODULE_AUTHOR("Matt Porter");
MODULE_DESCRIPTION("BCM Kona USB 2.0 PHY driver");
MODULE_LICENSE("GPL v2");
