/*
 * A hwmon driver for the Gateworks System Controller 
 * Copyright (C) 2009-14 Gateworks Corporation
 *
 * Author: Chris Lang <clang@gateworks.com>
 *
 * Based on lm75.c
 * Copyright (C) 1998-99 Frodo Looijaard <frodol@dds.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License,
 * as published by the Free Software Foundation - version 2.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/slab.h>

#define DRV_VERSION "0.2"
#define I2C_RETRIES     3

enum chips { gsc };

/* AD7418 registers */
#define GSC_REG_TEMP_IN		0x00
#define GSC_REG_VIN		0x02
#define GSC_REG_3P3		0x05
#define GSC_REG_BAT		0x08
#define GSC_REG_5P0		0x0b
#define GSC_REG_CORE		0x0e
#define GSC_REG_CPU1		0x11
#define GSC_REG_CPU2		0x14
#define GSC_REG_DRAM		0x17
#define GSC_REG_EXT_BAT		0x1a
#define GSC_REG_IO1		0x1d
#define GSC_REG_IO2 		0x20
#define GSC_REG_PCIE		0x23
#define GSC_REG_CURRENT		0x26
#define GSC_FAN_0		0x2C
#define GSC_FAN_1		0x2E
#define GSC_FAN_2		0x30
#define GSC_FAN_3		0x32
#define GSC_FAN_4		0x34
#define GSC_FAN_5		0x36

struct gsc_sensor_info {
	const char* name;
	int reg;
};

static const struct gsc_sensor_info gsc_sensors[] = {
	{"temp", GSC_REG_TEMP_IN},
	{"vin", GSC_REG_VIN},
	{"3p3", GSC_REG_3P3},
	{"bat", GSC_REG_BAT},
	{"5p0", GSC_REG_5P0},
	{"core", GSC_REG_CORE},
	{"cpu1", GSC_REG_CPU1},
	{"cpu2", GSC_REG_CPU2},
	{"dram", GSC_REG_DRAM},
	{"ext_bat", GSC_REG_EXT_BAT},
	{"io1", GSC_REG_IO1},
	{"io2", GSC_REG_IO2},
	{"pci2", GSC_REG_PCIE},
	{"current", GSC_REG_CURRENT},
	{"fan_point0", GSC_FAN_0},
	{"fan_point1", GSC_FAN_1},
	{"fan_point2", GSC_FAN_2},
	{"fan_point3", GSC_FAN_3},
	{"fan_point4", GSC_FAN_4},
	{"fan_point5", GSC_FAN_5},
};

struct gsc_data {
	struct i2c_client	*client;
	struct attribute_group	attrs;
	enum chips		type;
};


/**
 * gsc_i2c_write - Writes a register to GSC with retries
 * @client: i2c client
 * @reg: register address
 * @val: value to write
 *
 * Returns the result of operation - 0 is success
 */
static int gsc_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
	int retry, ret;

	for (retry = 0; retry < I2C_RETRIES; retry++) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		/*
		 * -EAGAIN returned when the i2c host controller is busy
		 * -EIO returned when i2c device is busy
		 */
		if (ret != -EAGAIN && ret != -EIO)
			break;
	}
	if (ret < 0) {
		dev_err(&client->dev, ">> 0x%02x %d\n", reg, ret);
		return ret;
	}
	dev_dbg(&client->dev, ">> 0x%02x=0x%02x (%d)\n", reg,
		val, retry);

	return 0;
}

/**
 * gsc_i2c_read - Reads register from GSC with retries
 * @client: i2c client
 * @reg: register address
 * @val: value to write
 *
 * Returns 0 on success
 */
static int gsc_i2c_read(struct i2c_client *client, u8 reg, u8 *val)
{
	int retry, ret;

	for (retry = 0; retry < I2C_RETRIES; retry++) {
		ret = i2c_smbus_read_byte_data(client, reg);
		/*
		 * -EAGAIN returned when the i2c host controller is busy
		 * -EIO returned when i2c device is busy
		 */
		if (ret != -EAGAIN && ret != -EIO)
			break;
	}
	if (ret < 0) {
		dev_err(&client->dev, "<< 0x%02x %d\n", reg, ret);
		return ret;
	}

	*val = ret & 0xff;
	dev_dbg(&client->dev, "<< 0x%02x=0x%02x (%d)\n", reg,
		*val, retry);

	return 0;
}

/* All registers are word-sized, except for the configuration registers.
 * AD7418 uses a high-byte first convention. Do NOT use those functions to
 * access the configuration registers CONF and CONF2, as they are byte-sized.
 */
static inline int gsc_read(struct i2c_client *client, u8 reg)
{
	unsigned int adc, i, err;
	u8 b;

	if (reg == GSC_REG_TEMP_IN || reg > GSC_REG_CURRENT)
		i = 2;
	else
		i = 3;

	adc = 0;
	while (i-- > 0) {
		err = gsc_i2c_read(client, reg + i, &b);
		if (err)
			return err;
		adc |= (b << (8*i));
	}
	if (reg == GSC_REG_TEMP_IN && adc > 0x8000)
		adc -= 0xffff;
	return adc;
}

static inline int gsc_write(struct i2c_client *client, u8 reg, u16 value)
{
	gsc_i2c_write(client, reg, value & 0xff);
	gsc_i2c_write(client, reg + 1, ((value >> 8) & 0xff));
	return 1;
}

static ssize_t show_adc(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct gsc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n",
		       gsc_read(data->client, gsc_sensors[attr->index].reg));
}

static ssize_t show_label(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sprintf(buf, "%s\n", gsc_sensors[attr->index].name);
}

static ssize_t store_fan(struct device *dev,
			 struct device_attribute *devattr,
			 const char *buf, size_t count)
{
	u16 val;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct gsc_data *data = dev_get_drvdata(dev);

	val = simple_strtoul(buf, NULL, 10);
	gsc_write(data->client, gsc_sensors[attr->index].reg, val);
	return count;
}

static SENSOR_DEVICE_ATTR(temp0_input, S_IRUGO, show_adc, NULL, 0);
static SENSOR_DEVICE_ATTR(temp0_label, S_IRUGO, show_label, NULL, 0);

static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, show_adc, NULL, 1);
static SENSOR_DEVICE_ATTR(in0_label, S_IRUGO, show_label, NULL, 1);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, show_adc, NULL, 2);
static SENSOR_DEVICE_ATTR(in1_label, S_IRUGO, show_label, NULL, 2);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, show_adc, NULL, 3);
static SENSOR_DEVICE_ATTR(in2_label, S_IRUGO, show_label, NULL, 3);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, show_adc, NULL, 4);
static SENSOR_DEVICE_ATTR(in3_label, S_IRUGO, show_label, NULL, 4);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, show_adc, NULL, 5);
static SENSOR_DEVICE_ATTR(in4_label, S_IRUGO, show_label, NULL, 5);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, show_adc, NULL, 6);
static SENSOR_DEVICE_ATTR(in5_label, S_IRUGO, show_label, NULL, 6);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, show_adc, NULL, 7);
static SENSOR_DEVICE_ATTR(in6_label, S_IRUGO, show_label, NULL, 7);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, show_adc, NULL, 8);
static SENSOR_DEVICE_ATTR(in7_label, S_IRUGO, show_label, NULL, 8);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, show_adc, NULL, 9);
static SENSOR_DEVICE_ATTR(in8_label, S_IRUGO, show_label, NULL, 9);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, show_adc, NULL, 10);
static SENSOR_DEVICE_ATTR(in9_label, S_IRUGO, show_label, NULL, 10);
static SENSOR_DEVICE_ATTR(in10_input, S_IRUGO, show_adc, NULL, 11);
static SENSOR_DEVICE_ATTR(in10_label, S_IRUGO, show_label, NULL, 11);
static SENSOR_DEVICE_ATTR(in11_input, S_IRUGO, show_adc, NULL, 12);
static SENSOR_DEVICE_ATTR(in11_label, S_IRUGO, show_label, NULL, 12);
static SENSOR_DEVICE_ATTR(in12_input, S_IRUGO, show_adc, NULL, 13);
static SENSOR_DEVICE_ATTR(in12_label, S_IRUGO, show_label, NULL, 13);

static SENSOR_DEVICE_ATTR(fan0_point0, S_IRUGO | S_IWUSR, show_adc, store_fan,
			  14);
static SENSOR_DEVICE_ATTR(fan0_point1, S_IRUGO | S_IWUSR, show_adc, store_fan,
			  15);
static SENSOR_DEVICE_ATTR(fan0_point2, S_IRUGO | S_IWUSR, show_adc, store_fan,
			  16);
static SENSOR_DEVICE_ATTR(fan0_point3, S_IRUGO | S_IWUSR, show_adc, store_fan,
			  17);
static SENSOR_DEVICE_ATTR(fan0_point4, S_IRUGO | S_IWUSR, show_adc, store_fan,
			  18);
static SENSOR_DEVICE_ATTR(fan0_point5, S_IRUGO | S_IWUSR, show_adc, store_fan,
			  19);

static struct attribute *gsc_attributes[] = {
	&sensor_dev_attr_temp0_input.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in12_input.dev_attr.attr,

	&sensor_dev_attr_temp0_label.dev_attr.attr,
	&sensor_dev_attr_in0_label.dev_attr.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,
	&sensor_dev_attr_in2_label.dev_attr.attr,
	&sensor_dev_attr_in3_label.dev_attr.attr,
	&sensor_dev_attr_in4_label.dev_attr.attr,
	&sensor_dev_attr_in5_label.dev_attr.attr,
	&sensor_dev_attr_in6_label.dev_attr.attr,
	&sensor_dev_attr_in7_label.dev_attr.attr,
	&sensor_dev_attr_in8_label.dev_attr.attr,
	&sensor_dev_attr_in9_label.dev_attr.attr,
	&sensor_dev_attr_in10_label.dev_attr.attr,
	&sensor_dev_attr_in11_label.dev_attr.attr,
	&sensor_dev_attr_in12_label.dev_attr.attr,

	&sensor_dev_attr_fan0_point0.dev_attr.attr,
	&sensor_dev_attr_fan0_point1.dev_attr.attr,
	&sensor_dev_attr_fan0_point2.dev_attr.attr,
	&sensor_dev_attr_fan0_point3.dev_attr.attr,
	&sensor_dev_attr_fan0_point4.dev_attr.attr,
	&sensor_dev_attr_fan0_point5.dev_attr.attr,
	NULL
};

static const struct attribute_group gsc_group = {
	.attrs = gsc_attributes,
};
__ATTRIBUTE_GROUPS(gsc);

static int gsc_probe(struct i2c_client *client,
		     const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct gsc_data *data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		return -EOPNOTSUPP;
	}

	data = devm_kzalloc(dev, sizeof(struct gsc_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);

	data->type = id->driver_data;

	switch (data->type) {
	case 0:
		data->attrs.attrs = gsc_attributes;
		break;
	}

	dev_info(&client->dev, "%s chip found\n", client->name);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   gsc_groups);
	if (IS_ERR(hwmon_dev)) {
		kfree(data);
		return PTR_ERR(hwmon_dev);
	}

	return 0;
}

static int gsc_remove(struct i2c_client *client)
{
	struct gsc_data *data = i2c_get_clientdata(client);
	kfree(data);
	return 0;
}

static const struct i2c_device_id gsc_id[] = {
	{ "gsc_hwmon", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gsc_id);

static struct i2c_driver gsc_driver = {
	.driver = {
		.name	= "gsc_hwmon",
	},
	.probe		= gsc_probe,
	.remove		= gsc_remove,
	.id_table	= gsc_id,
};

module_i2c_driver(gsc_driver);

MODULE_AUTHOR("Chris Lang <clang@gateworks.com>");
MODULE_DESCRIPTION("GSC HWMON driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
