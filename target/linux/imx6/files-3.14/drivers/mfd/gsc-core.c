/*
 * Copyright (C) 2014 Gateworks Corporation
 *
 * Written by Tim Harvey <tharvey@gateworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/gsc.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "gsc-core.h"

#define I2C_RETRIES	3

/*
 * The Gateworks System Controller (GSC) is a family of a multi-function
 * "Power Management and System Companion Device" chips originally designed for
 * use in Gateworks Single Board Computers. The control interface is I2C,
 * at 100kbps, with an interrupt.
 *
 * This driver core provides genirq support for the interrupts emitted,
 * by the various modules, and exports register access primitives.
 *
 */

#define DRIVER_NAME			"gsc"

/*----------------------------------------------------------------------*/

struct gsc {
	struct i2c_client *client;
	struct device *dev;
	unsigned int fwver;
	unsigned short fwcrc;
	unsigned char ready;
	struct mutex io_lock;
	struct platform_device *gsc;
	struct platform_device *wdt;
	struct platform_device *input;
};

static struct gsc *gsc_priv;

/**
 * gsc_i2c_write - Writes a register to GSC with retries
 * @reg: register address
 * @val: value to write
 *
 * Returns the result of operation - 0 is success
 */
int __gsc_i2c_write(u8 reg, u8 val)
{
	int retry, ret;

	for (retry = 0; retry < I2C_RETRIES; retry++) {
		ret = i2c_smbus_write_byte_data(gsc_priv->client, reg, val);
		/*
		 * -EAGAIN returned when the i2c host controller is busy
		 * -EIO returned when i2c device is busy
		 */
		if (ret != -EAGAIN && ret != -EIO)
			break;
	}
	if (ret < 0) {
		dev_err(&gsc_priv->client->dev, ">> 0x%02x %d\n", reg, ret);
		return ret;
	}
	dev_dbg(&gsc_priv->client->dev, ">> 0x%02x=0x%02x (%d)\n", reg,
		val, retry);

	return 0;
}

/**
 * gsc_i2c_read - Reads register from GSC with retries
 * @reg: register address
 * @val: value to write
 *
 * Returns result of operation
 */
static int __gsc_i2c_read(u8 reg, u8 *val)
{
	int retry, ret;

	for (retry = 0; retry < I2C_RETRIES; retry++) {
		ret = i2c_smbus_read_byte_data(gsc_priv->client, reg);
		/*
		 * -EAGAIN returned when the i2c host controller is busy
		 * -EIO returned when i2c device is busy
		 */
		if (ret != -EAGAIN && ret != -EIO)
			break;
	}
	if (ret < 0) {
		dev_err(&gsc_priv->client->dev, "<< 0x%02x %d\n", reg, ret);
		return ret;
	}

	*val = ret & 0xff;
	dev_dbg(&gsc_priv->client->dev, "<< 0x%02x=0x%02x (%d)\n", reg,
		*val, retry);

	return 0;
}

/**
 * gsc_i2c_update - set bits
 * @reg: register address
 * @mask: bits to clear
 * @set; bits to set
 *
 * Returns result of operation
 */
static int __gsc_i2c_update(u8 reg, u8 mask, u8 set)
{
	u8 rval;
	int ret;

	ret = __gsc_i2c_read(reg, &rval);
	if (ret)
		return ret;
	rval &= ~mask;
	ret = __gsc_i2c_write(reg, rval | set);
	return ret;
}

/* Exported Functions */

/**
 * gsc_i2c_write - Writes a register to GSC
 * @reg: register address
 * @val: value to write
 *
 * Returns the result of operation - 0 is success
 */
int gsc_i2c_write(u8 reg, u8 val)
{
	struct device *dev = &gsc_priv->client->dev;
	int ret = 0;

	if (unlikely(!gsc_priv || !gsc_priv->ready)) {
		pr_err("%s: not initialized\n", DRIVER_NAME);
		return -EPERM;
	}

	mutex_lock(&gsc_priv->io_lock);
	ret = __gsc_i2c_write(reg, val);
	if (ret < 0)
		dev_err(dev, ">> 0x%02x %d\n", reg, ret);
	else
		dev_dbg(dev, ">> 0x%02x=0x%02x\n", reg, val);
	mutex_unlock(&gsc_priv->io_lock);

	return ret;
}
EXPORT_SYMBOL(gsc_i2c_write);

/**
 * gsc_i2c_read - Reads register from GSC
 * @reg: register address
 * @val: value to write
 *
 * Returns result of operation
 */
int gsc_i2c_read(u8 reg, u8 *val)
{
	struct device *dev = &gsc_priv->client->dev;
	int ret = 0;

	if (unlikely(!gsc_priv || !gsc_priv->ready)) {
		pr_err("%s: not initialized\n", DRIVER_NAME);
		return -EPERM;
	}

	mutex_lock(&gsc_priv->io_lock);
	ret = __gsc_i2c_read(reg, val);
	if (ret < 0)
		dev_err(dev, "<< 0x%02x %d\n", reg, ret);
	else
		dev_dbg(dev, "<< 0x%02x=0x%02x\n", reg, *val);
	mutex_unlock(&gsc_priv->io_lock);

	return ret;
}
EXPORT_SYMBOL(gsc_i2c_read);

/**
 * gsc_i2c_update - read/write register from GSC
 * @reg: register address
 * @mask: bits to clear
 * @val: bits to set
 *
 * Returns result of operation
 */
int gsc_i2c_update(u8 reg, u8 mask, u8 val)
{
	int ret = 0;

	if (unlikely(!gsc_priv || !gsc_priv->ready)) {
		pr_err("%s: not initialized\n", DRIVER_NAME);
		return -EPERM;
	}

	mutex_lock(&gsc_priv->io_lock);
	ret = __gsc_i2c_update(reg, mask, val);
	mutex_unlock(&gsc_priv->io_lock);

	return ret;
}
EXPORT_SYMBOL(gsc_i2c_update);

/*----------------------------------------------------------------------*/

/**
 * gsc_get_fwver - API to get GSC FW version.
 */
int gsc_get_fwver(void)
{
	if (unlikely(!gsc_priv || !gsc_priv->ready)) {
		pr_err("%s: not initialized\n", DRIVER_NAME);
		return -EPERM;
	}

	return gsc_priv->fwver;
}
EXPORT_SYMBOL_GPL(gsc_get_fwver);

/*
 * gsc_powerdown - API to use GSC to power down board for a specific time
 *
 * secs - number of seconds to remain powered off
 */
int gsc_powerdown(unsigned long secs)
{
	int ret;

	if (!gsc_priv)
		return -ENODEV;

	dev_info(&gsc_priv->client->dev, "GSC powerdown for %ld seconds\n",
		 secs);
	mutex_lock(&gsc_priv->io_lock);
	ret = __gsc_i2c_write(GSC_TIME_ADD + 0, secs & 0xff);
	if (ret)
		goto done;
	ret = __gsc_i2c_write(GSC_TIME_ADD + 1, (secs >> 8) & 0xff);
	if (ret)
		goto done;
	ret = __gsc_i2c_write(GSC_TIME_ADD + 2, (secs >> 16) & 0xff);
	if (ret)
		goto done;
	ret = __gsc_i2c_write(GSC_TIME_ADD + 3, (secs >> 24) & 0xff);
	if (ret)
		goto done;
	ret = __gsc_i2c_update(GSC_CTRL_1, 0, 1 << GSC_CTRL_1_LATCH_SLEEP_ADD);
	if (ret)
		goto done;
	ret = __gsc_i2c_update(GSC_CTRL_1, 0, 1 << GSC_CTRL_1_ACTIVATE_SLEEP |
			       1 << GSC_CTRL_1_SLEEP_ENABLE);
	if (ret)
		goto done;

	ret = 0;

done:
	mutex_unlock(&gsc_priv->io_lock);

	return ret;
}

/*----------------------------------------------------------------------*/


/*
 * sysfs hooks
 */
static ssize_t gsc_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct gsc *gsc = dev_get_drvdata(dev);
	const char *name = attr->attr.name;
	int rz = 0;

	if (strcasecmp(name, "fw_version") == 0)
		rz = sprintf(buf, "%d\n", gsc->fwver);
	else if (strcasecmp(name, "fw_crc") == 0)
		rz = sprintf(buf, "0x%04x\n", gsc->fwcrc);

	return rz;
}

static ssize_t gsc_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	const char *name = attr->attr.name;
	int ret;

	if (strcasecmp(name, "powerdown") == 0) {
		long value;

		ret = strict_strtol(buf, 0, &value);
		if (ret == 0)
			gsc_powerdown(value);
	} else
		printk(KERN_ERR "invalid name '%s\n", name);

	return count;
}


/*
 * Create a group of attributes so that we can create and destroy them all
 * at once.
 */
static struct device_attribute attr_fwver =
	__ATTR(fw_version, 0440, gsc_show, NULL);
static struct device_attribute attr_fwcrc =
	__ATTR(fw_crc, 0440, gsc_show, NULL);
static struct device_attribute attr_pwrdown =
	__ATTR(powerdown, 0220, NULL, gsc_store);

static struct attribute *gsc_attrs[] = {
	&attr_fwver.attr,
	&attr_fwcrc.attr,
	&attr_pwrdown.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = gsc_attrs,
};

static int
gsc_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct gsc *gsc;
	int ret, irq_base = 0;
	u8 reg;

	if (client->irq <= 0) {
		dev_err(dev, "%s: no irq specified\n", DRIVER_NAME);
		return -EINVAL;
	}
	if (gsc_priv) {
		dev_err(dev, "already initialized\n");
		return -EBUSY;
	}

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0) {
		dev_dbg(dev, "can't talk I2C?\n");
		return -EIO;
	}


	gsc = devm_kzalloc(dev, sizeof(struct gsc), GFP_KERNEL);
	if (!gsc)
		ret = -ENOMEM;
	gsc_priv = gsc;

	mutex_init(&gsc->io_lock);
	gsc->client = client;

	if (__gsc_i2c_read(GSC_FW_VER, &reg))
		return -EIO;
	gsc->fwver = reg;
	__gsc_i2c_read(GSC_FW_CRC, &reg);
	gsc->fwcrc = reg << 8;
	__gsc_i2c_read(GSC_FW_CRC+1, &reg);
	gsc->fwcrc |= reg;
	gsc->ready = true;

	irq_base = gsc_irq_init(&client->dev, client->irq);
	if (irq_base < 0)
		return irq_base;
	dev_info(dev, "Gateworks System Controller: fw v%02d crc=0x%04x irq%d\n",
		 gsc->fwver, gsc->fwcrc, client->irq);

	i2c_set_clientdata(client, gsc);
	gsc->dev = &client->dev;

	ret = sysfs_create_group(&client->dev.kobj, &attr_group);
	if (ret)
		dev_err(dev, "failed to create sysfs attrs\n");

	if (client->dev.of_node) {
		printk("Populating platform devices\n");
		ret = of_platform_populate(client->dev.of_node, NULL, NULL,
					   &client->dev);
	}

	return ret;
}

static int gsc_remove(struct i2c_client *client)
{
	struct gsc *gsc = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &attr_group);
	mfd_remove_devices(gsc->dev);
	gsc_priv = NULL;

	return 0;
}

static const struct i2c_device_id gsc_i2c_ids[] = {
	{ "gsc", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, gsc_i2c_ids);

static const struct of_device_id gsc_dt_ids[] = {
	{ .compatible = "gw,gsc", },
	{ }
};
MODULE_DEVICE_TABLE(of, gsc_dt_ids);

static struct i2c_driver gsc_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = gsc_dt_ids,
	},
	.probe		= gsc_probe,
	.remove		= gsc_remove,
	.id_table	= gsc_i2c_ids,
};

static int __init gsc_init(void)
{
        return i2c_add_driver(&gsc_driver);
}
/* register after i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(gsc_init);

static void __exit gsc_exit(void)
{
        i2c_del_driver(&gsc_driver);
}
module_exit(gsc_exit);

MODULE_AUTHOR("Tim Harvey <tharvey@gateworks.com>");
MODULE_DESCRIPTION("I2C Core interface for GSC");
MODULE_LICENSE("GPL v2");
