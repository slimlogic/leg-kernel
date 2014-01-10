/*
 * Flash mappings described by the ACPI
 *
 * Copyright (C) 2006 MontaVista Software Inc.
 * Author: Vitaly Wool <vwool@ru.mvista.com>
 * Copyright (C) 2007 David Gibson, IBM Corporation.
 *
 * Revised to handle ACPI style flash binding by:
 *   Copyright (C) 2013 Tomasz Nowicki <tomasz.nowicki@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/concat.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

struct acpi_flash_list {
	struct mtd_info *mtd;
	struct map_info map;
	struct resource *res;
};

struct acpi_flash {
	struct mtd_info		*cmtd;
	int list_size; /* number of elements in acpi_flash_list */
	struct acpi_flash_list	list[0];
};

static const char * const rom_probe_types[] = {
	"cfi_probe", "jedec_probe", "map_rom" };

/* Helper function to handle probing of the obsolete "direct-mapped"
 * compatible binding, which has an extra "probe-type" property
 * describing the type of flash probe necessary. */
static struct mtd_info *obsolete_probe(struct platform_device *dev,
				       struct map_info *map)
{
	struct acpi_dsm_entry entry;
	struct mtd_info *mtd;
	acpi_handle handler;
	char *acpi_probe;
	int i, err, len;

	handler = ACPI_HANDLE(&dev->dev);
	dev_warn(&dev->dev, "ACPI uses obsolete \"direct-mapped\" flash "
		 "binding\n");

	err = acpi_dsm_lookup_value(handler, "probe-type", 0, &entry);
	if (err || entry.value == NULL) {
		for (i = 0; i < ARRAY_SIZE(rom_probe_types); i++) {
			mtd = do_map_probe(rom_probe_types[i], map);
			if (mtd)
				return mtd;
		}
		return NULL;
	}

	len = strlen(entry.value) + 1;
	acpi_probe = devm_kzalloc(&dev->dev, len, GFP_KERNEL);
	strncpy(acpi_probe, entry.value, len);
	kfree(entry.key);
	kfree(entry.value);

	if (strcmp(acpi_probe, "CFI") == 0) {
		return do_map_probe("cfi_probe", map);
	} else if (strcmp(acpi_probe, "JEDEC") == 0) {
		return do_map_probe("jedec_probe", map);
	} else {
		if (strcmp(acpi_probe, "ROM") != 0)
			dev_warn(&dev->dev, "obsolete_probe: don't know probe "
				 "type '%s', mapping as rom\n", acpi_probe);
		return do_map_probe("mtd_rom", map);
	}
}

/* When partitions are set we look for a linux,part-probe property which
   specifies the list of partition probers to use. If none is given then the
   default is use. These take precedence over other device tree
   information. */
static const char * const part_probe_types_def[] = {
	"cmdlinepart", "RedBoot", NULL };

static const char * const *acpi_get_part_probes(struct device *dev)
{
	struct acpi_dsm_entry entry;
	const char **res;
	int cplen, err, len;
	unsigned int count = 0, i;
	char *cp = NULL;

	acpi_handle handler = ACPI_HANDLE(dev);

	/* Get space separated strings */
	err = acpi_dsm_lookup_value(handler, "linux,part-probe", 0, &entry);
	if (err || entry.value == NULL)
			return part_probe_types_def;

	len = strlen(entry.value) + 1;
	cp = devm_kzalloc(dev, len, GFP_KERNEL);
	strncpy(cp, entry.value, len);
	kfree(entry.key);
	kfree(entry.value);

	cplen = strlen(cp);
	for (i = 0; i != cplen; i++)
		if (cp[i] == ' ')
			count++;

	/* Create the table with references to strings */
	res = kzalloc((count + 1) * sizeof(char *), GFP_KERNEL);
	for (i = 0; i < count + 1; i++) {
		res[i] = cp;
		cp = strnchr(cp, cplen, ' ');
		if (cp == NULL)
			break;

		*cp++ = '\0';
		cplen = strlen(cp);
	}
	return res;
}

static void acpi_free_probes(const char * const *probes)
{
	if (probes != part_probe_types_def)
		kfree(probes);
}

static const struct acpi_device_id acpi_flash_match[];
static int acpi_flash_remove(struct platform_device *dev);

static int acpi_flash_probe(struct platform_device *dev)
{
	const char * const *part_probe_types;
	const struct acpi_device_id *id;
	resource_size_t res_size;
	struct acpi_flash *info;
	const char *probe_type;
	struct resource *res;
	acpi_handle handler;
	int err = 0, i;
	int bank_width = 0, map_indirect = 0;
	struct mtd_info **mtd_list = NULL;
	char *mtd_name = NULL;
	int len, count = 0;
	struct acpi_dsm_entry entry;

	handler = ACPI_HANDLE(&dev->dev);

	id = acpi_match_device(acpi_flash_match, &dev->dev);
	if (!id)
		return -ENODEV;

	probe_type = (const char *)id->driver_data;

	err = acpi_dsm_lookup_value(handler, "linux,mtd-name", 0, &entry);
	if (err == 0 && entry.value) {
		len = strlen(entry.value) + 1;
		mtd_name = devm_kzalloc(&dev->dev, len, GFP_KERNEL);
		strncpy(mtd_name, entry.value, len);
		kfree(entry.key);
		kfree(entry.value);
	}

	err = acpi_dsm_lookup_value(handler, "no-unaligned-direct-access",
				0, &entry);
	if (err == 0 && kstrtoint(entry.value, 0, &map_indirect) == 0) {
		kfree(entry.key);
		kfree(entry.value);
	}

	while (platform_get_resource(dev, IORESOURCE_MEM, count))
		count++;

	if (!count) {
		dev_err(&dev->dev, "No resources found for %s device\n",
				dev_name(&dev->dev));
		err = -ENXIO;
		goto err_flash_remove;
	}

	info = devm_kzalloc(&dev->dev,
			    sizeof(struct acpi_flash) +
			    sizeof(struct acpi_flash_list) * count, GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto err_flash_remove;
	}

	dev_set_drvdata(&dev->dev, info);

	mtd_list = kzalloc(sizeof(*mtd_list) * count, GFP_KERNEL);
	if (!mtd_list)
		goto err_flash_remove;

	for (i = 0; i < count; i++) {
		res = platform_get_resource(dev, IORESOURCE_MEM, i);
		dev_dbg(&dev->dev, "resource[%d]: address 0x%lx size 0x%lx\n",
			i, (long)res->start, (long)resource_size(res));

		res_size = resource_size(res);
		info->list[i].res = request_mem_region(res->start, res_size,
						       dev_name(&dev->dev));
		if (!info->list[i].res) {
			err = -EBUSY;
			goto err_out;
		}

		/* Mandatory property */
		err = acpi_dsm_lookup_value(handler, "bank-width", 0, &entry);
		if (err || kstrtoint(entry.value, 0, &bank_width) != 0) {
			dev_err(&dev->dev,
				"Can't get bank width from DSDT\n");
			goto err_out;
		}
		kfree(entry.key);
		kfree(entry.value);

		info->list[i].map.name = mtd_name ?: dev_name(&dev->dev);
		info->list[i].map.phys = res->start;
		info->list[i].map.size = res_size;
		info->list[i].map.bankwidth = bank_width;
		info->list[i].map.virt = ioremap(info->list[i].map.phys,
						 info->list[i].map.size);
		if (!info->list[i].map.virt) {
			dev_err(&dev->dev, "Failed to ioremap() flash region\n");
			err = -ENOMEM;
			goto err_out;
		}

		simple_map_init(&info->list[i].map);

		/*
		 * On some platforms (e.g. MPC5200) a direct 1:1 mapping
		 * may cause problems with JFFS2 usage, as the local bus (LPB)
		 * doesn't support unaligned accesses as implemented in the
		 * JFFS2 code via memcpy(). By setting NO_XIP, the
		 * flash will not be exposed directly to the MTD users
		 * (e.g. JFFS2) any more.
		 */
		if (map_indirect)
			info->list[i].map.phys = NO_XIP;

		if (probe_type) {
			info->list[i].mtd = do_map_probe(probe_type,
							 &info->list[i].map);
		} else {
			info->list[i].mtd = obsolete_probe(dev,
							   &info->list[i].map);
		}

		if (!info->list[i].mtd) {
			dev_err(&dev->dev, "do_map_probe() failed\n");
			err = -ENXIO;
			goto err_out;
		} else
			info->list_size++;

		info->list[i].mtd->owner = THIS_MODULE;
		info->list[i].mtd->dev.parent = &dev->dev;
		mtd_list[i] = info->list[i].mtd;
	}

	info->cmtd = NULL;
	if (info->list_size == 1) {
		info->cmtd = info->list[0].mtd;
	} else if (info->list_size > 1) {
		/*
		 * We detected multiple devices. Concatenate them together.
		 */
		info->cmtd = mtd_concat_create(mtd_list, info->list_size,
					       dev_name(&dev->dev));
	}
	if (info->cmtd == NULL) {
		err = -ENXIO;
		goto err_out;
	}

	part_probe_types = acpi_get_part_probes(&dev->dev);
	mtd_device_parse_register(info->cmtd, part_probe_types, NULL,
			NULL, 0);
	acpi_free_probes(part_probe_types);

	kfree(mtd_list);

	return 0;

err_out:
	kfree(mtd_list);
err_flash_remove:
	acpi_flash_remove(dev);

	return err;
}

static int acpi_flash_remove(struct platform_device *dev)
{
	struct acpi_flash *info;
	int i;

	info = dev_get_drvdata(&dev->dev);
	if (!info)
		return 0;
	dev_set_drvdata(&dev->dev, NULL);

	if (info->cmtd != info->list[0].mtd) {
		mtd_device_unregister(info->cmtd);
		mtd_concat_destroy(info->cmtd);
	}

	if (info->cmtd)
		mtd_device_unregister(info->cmtd);

	for (i = 0; i < info->list_size; i++) {
		if (info->list[i].mtd)
			map_destroy(info->list[i].mtd);

		if (info->list[i].map.virt)
			iounmap(info->list[i].map.virt);

		if (info->list[i].res) {
			release_resource(info->list[i].res);
			kfree(info->list[i].res);
		}
	}

	return 0;
}

static const struct acpi_device_id acpi_flash_match[] = {
	{ "LNRO0015", (unsigned long)"cfi_probe"},
	{ "LNRO0016", (unsigned long)"jedec_probe"},
	{ "LNRO0017", (unsigned long)"map_ram"},
	{ "LNRO0018", (unsigned long)"direct-mapped"},
	{},
};
MODULE_DEVICE_TABLE(acpi, acpi_flash_match);

static struct platform_driver acpi_flash_driver = {
	.driver = {
		.name = "acpi-flash",
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(acpi_flash_match),
	},
	.probe		= acpi_flash_probe,
	.remove		= acpi_flash_remove,
};

module_platform_driver(acpi_flash_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tomasz Nowicki <tomasz.nowicki@linaro.org>");
MODULE_DESCRIPTION("ACPI based MTD map driver");
