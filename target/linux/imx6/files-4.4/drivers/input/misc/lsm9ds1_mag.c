/******************** (C) COPYRIGHT 2013 STMicroelectronics ********************
*
* File Name          : lsm9ds1_mag.c
* Authors            : MSH - C&I BU - Application Team
*		     : Giuseppe Barba (giuseppe.barba@st.com)
*		     : Matteo Dameno (matteo.dameno@st.com)
*		     : Denis Ciocca (denis.ciocca@st.com)
*		     : Both authors are willing to be considered the contact
*		     : and update points for the driver.
* Version            : V.1.0.0
* Date               : 2014/Feb/11
* Description        : LSM9DS1 magnetometer driver
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
********************************************************************************/
//#define DEBUG

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif

#include <linux/input/lsm9ds1.h>

#define I2C_AUTO_INCREMENT		(0x80)
#define MS_TO_NS(x)			(x*1000000L)

#define FUZZ				(0)
#define FLAT				(0)

/* Address registers */
#define REG_WHOAMI_ADDR		(0x0F)
#define CTRL_REG1_M			(0x20)
#define CTRL_REG2_M			(0x21)
#define CTRL_REG3_M			(0x22)
#define CTRL_REG4_M			(0x23)
#define CTRL_REG5_M			(0x24)
#define INT_CFG_M			(0x30)
#define INT_THS_L			(0x32)
#define INT_THS_H			(0x33)

#define REG_MAG_DATA_ADDR		(0x28) /** Mag. data low address register */

/* Sensitivity */
#define SENSITIVITY_MAG_4G		146 /**	ugauss/LSB	*/
#define SENSITIVITY_MAG_8G		292 /**	ugauss/LSB	*/
#define SENSITIVITY_MAG_12G		430 /**	ugauss/LSB	*/
#define SENSITIVITY_MAG_16G		584 /**	ugauss/LSB	*/

#define MAX_I2C_VAL			(0x7FFF)

/** Accelerometer range in ngauss */
#define MAG_MAX_POS			(MAX_I2C_VAL * SENSITIVITY_MAG_16G)
#define MAG_MAX_NEG			(-MAX_I2C_VAL * SENSITIVITY_MAG_16G)

/* Magnetic sensor mode */
#define CTRL_REG3_M_MD_MASK		(0x03)
#define CTRL_REG3_M_MD_OFF		(0x02)
#define CTRL_REG3_M_MD_CONTINUOUS	(0x00)
#define CTRL_REG3_M_MD_SINGLE		(0x01)

/* X and Y axis operative mode selection */
#define X_Y_PERFORMANCE_MASK		(0x60)
#define X_Y_LOW_PERFORMANCE		(0x00)
#define X_Y_MEDIUM_PERFORMANCE		(0x20)
#define X_Y_HIGH_PERFORMANCE		(0x40)
#define X_Y_ULTRA_HIGH_PERFORMANCE	(0x60)

/* Z axis operative mode selection */
#define Z_PERFORMANCE_MASK		(0x0c)
#define Z_LOW_PERFORMANCE		(0x00)
#define Z_MEDIUM_PERFORMANCE		(0x04)
#define Z_HIGH_PERFORMANCE		(0x08)
#define Z_ULTRA_HIGH_PERFORMANCE	(0x0c)

/* Default values loaded in probe function */
#define DEF_ZERO			(0x00)

#define WHOIAM_VALUE			(0x3D)
#define CTRL_REG1_M_DEF		(0x60)
#define CTRL_REG2_M_DEF		DEF_ZERO
#define CTRL_REG3_M_DEF		CTRL_REG3_M_MD_CONTINUOUS
#define CTRL_REG4_M_DEF		DEF_ZERO
#define CTRL_REG5_M_DEF		(0x40)
#define INT_CFG_M_DEF			DEF_ZERO
#define INT_THS_H_DEF			DEF_ZERO
#define INT_THS_L_DEF			DEF_ZERO



struct workqueue_struct *lsm9ds1_mag_workqueue = 0;


struct {
	unsigned int cutoff_us;
	u8 value;
} lsm9ds1_mag_odr_table[] = {
		{   12, LSM9DS1_MAG_ODR80 },
		{   25, LSM9DS1_MAG_ODR40 },
		{   50, LSM9DS1_MAG_ODR20 },
		{  100, LSM9DS1_MAG_ODR10 },
		{  200, LSM9DS1_MAG_ODR5 },
		{  400, LSM9DS1_MAG_ODR2_5 },
		{  800, LSM9DS1_MAG_ODR1_25 },
		{ 1600, LSM9DS1_MAG_ODR0_625 },
};

struct interrupt_enable {
	atomic_t enable;
	u8 address;
	u8 mask;
};

struct interrupt_value {
	int value;
	u8 address;
};

struct lsm9ds1_mag_status {
	struct i2c_client *client;
	struct lsm9ds1_mag_platform_data *pdata_mag;

	struct mutex lock;
	struct work_struct input_work_mag;

	struct hrtimer hr_timer_mag;
	ktime_t ktime_mag;

	struct input_dev *input_dev_mag;

	int hw_initialized;
	/* hw_working=-1 means not tested yet */
	int hw_working;

	atomic_t enabled_mag;

	int on_before_suspend;
	int use_smbus;

	u32 sensitivity_mag;

	u8 xy_mode;
	u8 z_mode;
};

static const struct lsm9ds1_mag_platform_data default_lsm9ds1_mag_pdata = {
	.poll_interval = 100,
	.min_interval = LSM9DS1_MAG_MIN_POLL_PERIOD_MS,
	.fs_range = LSM9DS1_MAG_FS_4G,
	.rot_matrix = {
		{1, 0, 0},
		{0, 1, 0},
		{0, 0, 1},
	},
};

struct reg_rw {
	u8 address;
	u8 default_value;
	u8 resume_value;
};

struct reg_r {
	u8 address;
	u8 value;
};

static struct status_registers {
	struct reg_r who_am_i;
	struct reg_rw ctrl_reg1_m;
	struct reg_rw ctrl_reg2_m;
	struct reg_rw ctrl_reg3_m;
	struct reg_rw ctrl_reg4_m;
	struct reg_rw ctrl_reg5_m;
	struct reg_rw int_cfg_m;
	struct reg_rw int_ths_l;
	struct reg_rw int_ths_h;
} status_registers = {
	.who_am_i = {.address = REG_WHOAMI_ADDR, .value = WHOIAM_VALUE,},
	.ctrl_reg1_m = {.address = CTRL_REG1_M, .default_value = CTRL_REG1_M_DEF,},
	.ctrl_reg2_m = {.address = CTRL_REG2_M, .default_value = CTRL_REG2_M_DEF,},
	.ctrl_reg3_m = {.address = CTRL_REG3_M, .default_value = CTRL_REG3_M_DEF,},
	.ctrl_reg4_m = {.address = CTRL_REG4_M, .default_value = CTRL_REG4_M_DEF,},
	.ctrl_reg5_m = {.address = CTRL_REG5_M, .default_value = CTRL_REG5_M_DEF,},
	.int_cfg_m = {.address = INT_CFG_M, .default_value = INT_CFG_M_DEF,},
	.int_ths_h = {.address = INT_THS_H, .default_value = INT_THS_H_DEF,},
	.int_ths_l = {.address = INT_THS_L, .default_value = INT_THS_L_DEF,},
};

static int lsm9ds1_i2c_read(struct lsm9ds1_mag_status *stat, u8 *buf, int len)
{
	int ret;
	u8 cmd = buf[0];
#ifdef DEBUG
	unsigned int ii;
#endif
	struct i2c_msg msgs[] = {
		{
			.addr = stat->client->addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = stat->client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		}
	};

	if (len > 1)
		cmd |= I2C_AUTO_INCREMENT;

	if (stat->use_smbus) {
		if (len == 1) {
			ret = i2c_smbus_read_byte_data(stat->client, cmd);
			buf[0] = ret & 0xff;
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_read_byte_data: ret=0x%02x, len:%d ,"
				"command=0x%02x, buf[0]=0x%02x\n",
				ret, len, cmd , buf[0]);
#endif
		} else if (len > 1) {
			ret = i2c_smbus_read_i2c_block_data(stat->client,
								cmd, len, buf);
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_read_i2c_block_data: ret:%d len:%d, "
				"command=0x%02x, ",
				ret, len, cmd);
			for (ii = 0; ii < len; ii++)
				printk(KERN_DEBUG "buf[%d]=0x%02x,",
								ii, buf[ii]);

			printk("\n");
#endif
		} else
			ret = -1;

		if (ret < 0) {
			dev_err(&stat->client->dev, "read transfer"
				" error: len:%d, command=0x%02x\n", len, cmd);
			return 0;
		}
		return len;
	}

	ret = i2c_transfer(stat->client->adapter, msgs, 2);

	return (ret == 2) ? 0 : 1;
}

static int lsm9ds1_i2c_write(struct lsm9ds1_mag_status *stat, u8 *buf,
									int len)
{
	int ret;
	u8 reg, value;
#ifdef DEBUG
	unsigned int ii;
#endif
	struct i2c_msg msg = {
		.addr = stat->client->addr,
		.flags = 0,
		.len = len + 1,
		.buf = buf,
	};

	reg = (I2C_AUTO_INCREMENT | buf[0]);
	value = buf[1];

	if (stat->use_smbus) {
		if (len == 1) {
			ret = i2c_smbus_write_byte_data(stat->client,
								reg, value);
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_write_byte_data: ret=%d, len:%d, "
				"command=0x%02x, value=0x%02x\n",
				ret, len, reg , value);
#endif
			return ret;
		} else if (len > 1) {
			ret = i2c_smbus_write_i2c_block_data(stat->client,
							reg, len, buf + 1);
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_write_i2c_block_data: ret=%d, "
				"len:%d, command=0x%02x, ",
				ret, len, reg);
			for (ii = 0; ii < (len + 1); ii++)
				printk(KERN_DEBUG "value[%d]=0x%02x,",
								ii, buf[ii]);

			printk("\n");
#endif
			return ret;
		}
	}

	ret = i2c_transfer(stat->client->adapter, &msg, 1);

	return (ret == 1) ? 0 : 1;
}

static int lsm9ds1_hw_init(struct lsm9ds1_mag_status *stat)
{
	int err = -1;
	u8 buf[6];

	pr_info("%s: hw init start\n", LSM9DS1_MAG_DEV_NAME);

	buf[0] = status_registers.who_am_i.address;
	err = lsm9ds1_i2c_read(stat, buf, 1);

	if (err < 0) {
		dev_warn(&stat->client->dev, "Error reading WHO_AM_I: is "
		" device available/working?\n");
		goto err_firstread;
	} else
		stat->hw_working = 1;

	if (buf[0] != status_registers.who_am_i.value) {
	dev_err(&stat->client->dev,
		"device unknown. Expected: 0x%02x,"
		" Replies: 0x%02x\n", status_registers.who_am_i.value, buf[0]);
		err = -1;
		goto err_unknown_device;
	}

	status_registers.ctrl_reg1_m.resume_value =
				status_registers.ctrl_reg1_m.default_value;
	status_registers.ctrl_reg2_m.resume_value =
				status_registers.ctrl_reg2_m.default_value;
	status_registers.ctrl_reg3_m.resume_value =
				status_registers.ctrl_reg3_m.default_value;
	status_registers.ctrl_reg4_m.resume_value =
				status_registers.ctrl_reg4_m.default_value;
	status_registers.ctrl_reg5_m.resume_value =
				status_registers.ctrl_reg5_m.default_value;
	status_registers.int_cfg_m.resume_value =
				status_registers.int_cfg_m.default_value;
	status_registers.int_ths_h.resume_value =
				status_registers.int_ths_h.default_value;
	status_registers.int_ths_l.resume_value =
				status_registers.int_ths_l.default_value;

	stat->xy_mode = X_Y_ULTRA_HIGH_PERFORMANCE;
	stat->z_mode = Z_ULTRA_HIGH_PERFORMANCE;
	stat->hw_initialized = 1;
	pr_info("%s: hw init done\n", LSM9DS1_MAG_DEV_NAME);

	return 0;

err_unknown_device:
err_firstread:
	stat->hw_working = 0;
	stat->hw_initialized = 0;
	return err;
}

static int lsm9ds1_mag_device_power_off(struct lsm9ds1_mag_status *stat)
{
	int err;
	u8 buf[2];

	buf[0] = status_registers.ctrl_reg3_m.address;
	buf[1] = ((CTRL_REG3_M_MD_MASK & CTRL_REG3_M_MD_OFF) |
		((~CTRL_REG3_M_MD_MASK) &
		status_registers.ctrl_reg3_m.resume_value));

	err = lsm9ds1_i2c_write(stat, buf, 1);
	if (err < 0)
		dev_err(&stat->client->dev, "magnetometer soft power off "
							"failed: %d\n", err);

	if (stat->pdata_mag->power_off)
		stat->pdata_mag->power_off();

	atomic_set(&stat->enabled_mag, 0);

	return 0;
}

static int lsm9ds1_mag_device_power_on(struct lsm9ds1_mag_status *stat)
{
	int err = -1;
	u8 buf[6];

	if (stat->pdata_mag->power_on) {
		err = stat->pdata_mag->power_on();
		if (err < 0) {
			dev_err(&stat->client->dev,
				"magnetometer power_on failed: %d\n", err);
			return err;
		}
	}

	buf[0] = status_registers.ctrl_reg1_m.address;
	buf[1] = status_registers.ctrl_reg1_m.resume_value;
	buf[2] = status_registers.ctrl_reg2_m.resume_value;
	buf[3] = status_registers.ctrl_reg3_m.resume_value;
	buf[4] = status_registers.ctrl_reg4_m.resume_value;
	buf[5] = status_registers.ctrl_reg5_m.resume_value;
	err = lsm9ds1_i2c_write(stat, buf, 5);
	if (err < 0)
		goto err_resume_state;

	buf[0] = status_registers.int_cfg_m.address;
	buf[1] = status_registers.int_cfg_m.resume_value;
	buf[2] = status_registers.int_ths_h.resume_value;
	buf[3] = status_registers.int_ths_l.resume_value;
	err = lsm9ds1_i2c_write(stat, buf, 5);
	if (err < 0)
		goto err_resume_state;

	atomic_set(&stat->enabled_mag, 1);

	return 0;

err_resume_state:
	atomic_set(&stat->enabled_mag, 0);
	dev_err(&stat->client->dev, "magnetometer hw power on error "
				"0x%02x,0x%02x: %d\n", buf[0], buf[1], err);
	return err;
}

static int lsm9ds1_mag_update_fs_range(struct lsm9ds1_mag_status *stat,
							u8 new_fs_range)
{
	int err = -1;
	u32 sensitivity;
	u8 updated_val;
	u8 buf[2];

	switch (new_fs_range) {
	case LSM9DS1_MAG_FS_4G:
		sensitivity = SENSITIVITY_MAG_4G;
		break;
	case LSM9DS1_MAG_FS_8G:
		sensitivity = SENSITIVITY_MAG_8G;
		break;
	case LSM9DS1_MAG_FS_12G:
		sensitivity = SENSITIVITY_MAG_12G;
		break;
	case LSM9DS1_MAG_FS_16G:
		sensitivity = SENSITIVITY_MAG_16G;
		break;
	default:
		dev_err(&stat->client->dev, "invalid magnetometer "
				"fs range requested: %u\n", new_fs_range);
		return -EINVAL;
	}

	buf[0] = status_registers.ctrl_reg2_m.address;
	err = lsm9ds1_i2c_read(stat, buf, 1);
	if (err < 0)
		goto error;

	status_registers.ctrl_reg2_m.resume_value = buf[0];
	updated_val = (LSM9DS1_MAG_FS_MASK & new_fs_range);
	buf[1] = updated_val;
	buf[0] = status_registers.ctrl_reg2_m.address;

	err = lsm9ds1_i2c_write(stat, buf, 1);
	if (err < 0)
		goto error;
	status_registers.ctrl_reg2_m.resume_value = updated_val;
	stat->sensitivity_mag = sensitivity;

	return err;

error:
	dev_err(&stat->client->dev, "update magnetometer fs range failed "
		"0x%02x,0x%02x: %d\n", buf[0], buf[1], err);
	return err;
}

static int lsm9ds1_mag_update_odr(struct lsm9ds1_mag_status *stat,
						unsigned int poll_interval_ms)
{
	int err = -1;
	u8 config[2];
	int i;

	for (i = ARRAY_SIZE(lsm9ds1_mag_odr_table) - 1; i >= 0; i--) {
		if ((lsm9ds1_mag_odr_table[i].cutoff_us <= poll_interval_ms)
								|| (i == 0))
			break;
	}

	config[1] = ((ODR_MAG_MASK & lsm9ds1_mag_odr_table[i].value) |
	      ((~ODR_MAG_MASK) & status_registers.ctrl_reg1_m.resume_value));

	if (atomic_read(&stat->enabled_mag)) {
		config[0] = status_registers.ctrl_reg1_m.address;
		err = lsm9ds1_i2c_write(stat, config, 1);
		if (err < 0)
			goto error;
	}
	status_registers.ctrl_reg1_m.resume_value = config[1];
	stat->ktime_mag = ktime_set(0, MS_TO_NS(poll_interval_ms));

	return err;

error:
	dev_err(&stat->client->dev, "update magnetometer odr failed "
			"0x%02x,0x%02x: %d\n", config[0], config[1], err);

	return err;
}

static int lsm9ds1_mag_update_operative_mode(struct lsm9ds1_mag_status *stat,
							int axis, u8 value)
{
	int err = -1;
	u8 config[2];
	u8 mask;
	u8 addr;

	if (axis == 0) {
		config[0] = CTRL_REG1_M;
		mask = X_Y_PERFORMANCE_MASK;
		addr = CTRL_REG1_M;
	} else {
		config[0] = CTRL_REG4_M;
		mask = Z_PERFORMANCE_MASK;
		addr = CTRL_REG4_M;
	}
	err = lsm9ds1_i2c_read(stat,config,1);
	if(err<0)
		goto error;
	config[1] = ((mask & value) |
		((~mask) & config[0]));

	config[0] = addr;
	err = lsm9ds1_i2c_write(stat,config,1);
	if (err < 0)
		goto error;
	if (axis == 0)
		stat->xy_mode = value;
	else
		stat->z_mode = value;

	return err;

error:
	dev_err(&stat->client->dev, "update operative mode failed "
			"0x%02x,0x%02x: %d\n", config[0], config[1], err);

	return err;
}

static int lsm9ds1_mag_validate_pdata(struct lsm9ds1_mag_status *stat)
{
	stat->pdata_mag->min_interval =
			    max((unsigned int)LSM9DS1_MAG_MIN_POLL_PERIOD_MS,
					    stat->pdata_mag->min_interval);
	stat->pdata_mag->poll_interval = max(stat->pdata_mag->poll_interval,
					     stat->pdata_mag->min_interval);

	return 0;
}

static int lsm9ds1_mag_enable(struct lsm9ds1_mag_status *stat)
{
	int err;

	if (!atomic_cmpxchg(&stat->enabled_mag, 0, 1)) {
		err = lsm9ds1_mag_device_power_on(stat);
		if (err < 0) {
			atomic_set(&stat->enabled_mag, 0);
			return err;
		}
		hrtimer_start(&stat->hr_timer_mag, stat->ktime_mag,
							HRTIMER_MODE_REL);
	}

	return 0;
}

static int lsm9ds1_mag_disable(struct lsm9ds1_mag_status *stat)
{
	if (atomic_cmpxchg(&stat->enabled_mag, 1, 0)) {
		cancel_work_sync(&stat->input_work_mag);
		hrtimer_cancel(&stat->hr_timer_mag);
		lsm9ds1_mag_device_power_off(stat);
	}

	return 0;
}

static void lsm9ds1_mag_input_cleanup(struct lsm9ds1_mag_status *stat)
{
	input_unregister_device(stat->input_dev_mag);
	input_free_device(stat->input_dev_mag);
}

static ssize_t attr_get_polling_rate_mag(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int val;
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);
	mutex_lock(&stat->lock);
	val = stat->pdata_mag->poll_interval;
	mutex_unlock(&stat->lock);
	return sprintf(buf, "%u\n", val);
}

static ssize_t attr_set_polling_rate_mag(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);
	unsigned long interval_ms;

	if (kstrtoul(buf, 10, &interval_ms))
		return -EINVAL;
	if (!interval_ms)
		return -EINVAL;
	interval_ms = max_t(unsigned int, (unsigned int)interval_ms,
						stat->pdata_mag->min_interval);
	mutex_lock(&stat->lock);
	stat->pdata_mag->poll_interval = (unsigned int)interval_ms;
	lsm9ds1_mag_update_odr(stat, interval_ms);
	mutex_unlock(&stat->lock);
	return size;
}

static ssize_t attr_get_enable_mag(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);
	int val = (int)atomic_read(&stat->enabled_mag);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_enable_mag(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val)
		lsm9ds1_mag_enable(stat);
	else
		lsm9ds1_mag_disable(stat);

	return size;
}

static ssize_t attr_get_range_mag(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u8 val;
	int range = 2;
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);

	mutex_lock(&stat->lock);
	val = stat->pdata_mag->fs_range;
	switch (val) {
	case LSM9DS1_MAG_FS_4G:
		range = 4;
		break;
	case LSM9DS1_MAG_FS_8G:
		range = 8;
		break;
	case LSM9DS1_MAG_FS_12G:
		range = 10;
		break;
	case LSM9DS1_MAG_FS_16G:
		range = 16;
		break;
	}
	mutex_unlock(&stat->lock);

	return sprintf(buf, "%d\n", range);
}

static ssize_t attr_set_range_mag(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);
	unsigned long val;
	u8 range;
	int err;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;
	switch (val) {
	case 4:
		range = LSM9DS1_MAG_FS_4G;
		break;
	case 8:
		range = LSM9DS1_MAG_FS_8G;
		break;
	case 10:
		range = LSM9DS1_MAG_FS_12G;
		break;
	case 16:
		range = LSM9DS1_MAG_FS_16G;
		break;
	default:
		dev_err(&stat->client->dev, "magnetometer invalid range "
					"request: %lu, discarded\n", val);
		return -EINVAL;
	}
	mutex_lock(&stat->lock);
	err = lsm9ds1_mag_update_fs_range(stat, range);
	if (err < 0) {
		mutex_unlock(&stat->lock);
		return err;
	}
	stat->pdata_mag->fs_range = range;
	mutex_unlock(&stat->lock);
	dev_info(&stat->client->dev, "magnetometer range set to:"
							" %lu g\n", val);

	return size;
}

static ssize_t attr_get_xy_mode(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	u8 val;
	char mode[13];
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);

	mutex_lock(&stat->lock);
	val = stat->xy_mode;
	switch (val) {
	case X_Y_HIGH_PERFORMANCE:
		strcpy(&(mode[0]), "high");
		break;
	case X_Y_LOW_PERFORMANCE:
		strcpy(&(mode[0]), "low");
		break;
	case X_Y_MEDIUM_PERFORMANCE:
		strcpy(&(mode[0]), "medium");
		break;
	case X_Y_ULTRA_HIGH_PERFORMANCE:
		strcpy(&(mode[0]), "ultra_high");
		break;
	}
	mutex_unlock(&stat->lock);
	return sprintf(buf, "%s\n", mode);
}

static ssize_t attr_set_xy_mode(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);
	u8 mode;
	int err;

	err = strncmp(buf, "high", 4);
	if(err==0) {
		mode = X_Y_HIGH_PERFORMANCE;
		goto valid;
	}
	err = strncmp(buf, "low", 3);
	if(err==0) {
		mode = X_Y_LOW_PERFORMANCE;
		goto valid;
	}
	err = strncmp(buf, "medium", 6);
	if(err==0) {
		mode = X_Y_MEDIUM_PERFORMANCE;
		goto valid;
	}
	err = strncmp(buf, "ultra_high", 10);
	if(err==0) {
		mode = X_Y_ULTRA_HIGH_PERFORMANCE;
		goto valid;
	}
	goto error;

valid:
	err = lsm9ds1_mag_update_operative_mode(stat,0,mode);
	if(err<0)
		goto error;

	dev_info(&stat->client->dev, "magnetometer x_y op. mode set to:"
							" %s", buf);
	return size;

error:
	dev_err(&stat->client->dev, "magnetometer invalid value "
					"request: %s, discarded\n", buf);
	return -EINVAL;
}

static ssize_t attr_get_z_mode(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	u8 val;
	char mode[13];
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);

	mutex_lock(&stat->lock);
	val = stat->z_mode;
	switch (val) {
	case Z_HIGH_PERFORMANCE:
		strcpy(&(mode[0]), "high");
		break;
	case Z_LOW_PERFORMANCE:
		strcpy(&(mode[0]), "low");
		break;
	case Z_MEDIUM_PERFORMANCE:
		strcpy(&(mode[0]), "medium");
		break;
	case Z_ULTRA_HIGH_PERFORMANCE:
		strcpy(&(mode[0]), "ultra_high");
		break;
	}
	mutex_unlock(&stat->lock);
	return sprintf(buf, "%s\n", mode);
}

static ssize_t attr_set_z_mode(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct lsm9ds1_mag_status *stat = dev_get_drvdata(dev);
	u8 mode;
	int err;

	err = strncmp(buf, "high", 4);
	if(err==0) {
		mode = Z_HIGH_PERFORMANCE;
		goto valid;
	}
	err = strncmp(buf, "low", 3);
	if(err==0) {
		mode = Z_LOW_PERFORMANCE;
		goto valid;
	}
	err = strncmp(buf, "medium", 6);
	if(err==0) {
		mode = Z_MEDIUM_PERFORMANCE;
		goto valid;
	}
	err = strncmp(buf, "ultra_high", 10);
	if(err==0) {
		mode = Z_ULTRA_HIGH_PERFORMANCE;
		goto valid;
	}
	goto error;

valid:
	err = lsm9ds1_mag_update_operative_mode(stat,1,mode);
	if(err<0)
		goto error;
	dev_info(&stat->client->dev, "magnetometer z op. mode set to:"
							" %s", buf);
	return size;

error:
	dev_err(&stat->client->dev, "magnetometer invalid value "
					"request: %s, discarded\n", buf);
	return -EINVAL;
}

#ifdef DEBUG
static int write_bit_on_register(struct lsm9ds1_mag_status *stat, u8 address,
					u8 *resume_value, u8 mask, int value)
{
	int err;
	u8 updated_val;
	u8 buf[2];
	u8 val = 0x00;

	buf[0] = address;
	err = lsm9ds1_i2c_read(stat, buf, 1);
	if (err < 0)
		return -1;

	if(resume_value != NULL)
		*resume_value = buf[0];

	if(mask == 0)
		updated_val = (u8)value;
	else {
		if(value>0)
			val = 0xFF;
		updated_val = (mask & val) | ((~mask) & buf[0]);
	}

	buf[1] = updated_val;
	buf[0] = address;

	err = lsm9ds1_i2c_write(stat, buf, 1);
	if (err < 0)
		return -1;

	if(resume_value != NULL)
		*resume_value = updated_val;

	return err;
}
#endif

static struct device_attribute attributes[] = {
	__ATTR(pollrate_ms, 0664, attr_get_polling_rate_mag,
						attr_set_polling_rate_mag),
	__ATTR(range, 0664, attr_get_range_mag, attr_set_range_mag),
	__ATTR(enable_device, 0664, attr_get_enable_mag, attr_set_enable_mag),
	__ATTR(x_y_opearative_mode, 0664, attr_get_xy_mode, attr_set_xy_mode),
	__ATTR(z_opearative_mode, 0664, attr_get_z_mode, attr_set_z_mode),

};

static int create_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto error;
	return 0;

error:
	for (; i >= 0; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s:Unable to create interface\n", __func__);
	return -1;
}

static void remove_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);
}

int lsm9ds1_mag_input_open(struct input_dev *input)
{
	struct lsm9ds1_mag_status *stat = input_get_drvdata(input);
	lsm9ds1_mag_enable(stat);

	return lsm9ds1_mag_enable(stat);

}

void lsm9ds1_mag_input_close(struct input_dev *dev)
{
	struct lsm9ds1_mag_status *stat = input_get_drvdata(dev);

	lsm9ds1_mag_disable(stat);
}

static int lsm9ds1_mag_get_data(struct lsm9ds1_mag_status *stat, int *xyz)
{
	int i, err = -1;
	u8 mag_data[6];
	s32 hw_d[3] = { 0 };

	mag_data[0] = (REG_MAG_DATA_ADDR);
	err = lsm9ds1_i2c_read(stat, mag_data, 6);
	if (err < 0)
		return err;

	hw_d[0] = ((s32)((s16)((mag_data[1] << 8) | (mag_data[0]))));
	hw_d[1] = ((s32)((s16)((mag_data[3] << 8) | (mag_data[2]))));
	hw_d[2] = ((s32)((s16)((mag_data[5] << 8) | (mag_data[4]))));

#ifdef DEBUG
	pr_debug("%s read x=0x%02x 0x%02x (regH regL), x=%d (dec) [LSB]\n",
		LSM9DS1_MAG_DEV_NAME, mag_data[1], mag_data[0], hw_d[0]);
	pr_debug("%s read y=0x%02x 0x%02x (regH regL), y=%d (dec) [LSB]\n",
		LSM9DS1_MAG_DEV_NAME, mag_data[3], mag_data[2], hw_d[1]);
	pr_debug("%s read z=0x%02x 0x%02x (regH regL), z=%d (dec) [LSB]\n",
		LSM9DS1_MAG_DEV_NAME, mag_data[5], mag_data[4], hw_d[2]);
#endif

	hw_d[0] = hw_d[0] * stat->sensitivity_mag;
	hw_d[1] = hw_d[1] * stat->sensitivity_mag;
	hw_d[2] = hw_d[2] * stat->sensitivity_mag;

	for (i = 0; i < 3; i++) {
		xyz[i] = stat->pdata_mag->rot_matrix[0][i] * hw_d[0] +
				stat->pdata_mag->rot_matrix[1][i] * hw_d[1] +
				stat->pdata_mag->rot_matrix[2][i] * hw_d[2];
	}

	return err;
}

static void lsm9ds1_mag_report_values(struct lsm9ds1_mag_status *stat,
								      int *xyz)
{
	input_report_abs(stat->input_dev_mag, ABS_X, xyz[0]);
	input_report_abs(stat->input_dev_mag, ABS_Y, xyz[1]);
	input_report_abs(stat->input_dev_mag, ABS_Z, xyz[2]);
	input_sync(stat->input_dev_mag);
}

static int lsm9ds1_mag_input_init(struct lsm9ds1_mag_status *stat)
{
	int err;

	stat->input_dev_mag = input_allocate_device();
	if (!stat->input_dev_mag) {
		err = -ENOMEM;
		dev_err(&stat->client->dev,
			"magnetometer input device allocation failed\n");
		goto err0;
	}

	stat->input_dev_mag->open = lsm9ds1_mag_input_open;
	stat->input_dev_mag->close = lsm9ds1_mag_input_close;
	stat->input_dev_mag->name = LSM9DS1_MAG_DEV_NAME;
	stat->input_dev_mag->id.bustype = BUS_I2C;
	stat->input_dev_mag->dev.parent = &stat->client->dev;

	input_set_drvdata(stat->input_dev_mag, stat);

	set_bit(EV_ABS, stat->input_dev_mag->evbit);

	input_set_abs_params(stat->input_dev_mag, ABS_X, MAG_MAX_NEG,
						MAG_MAX_POS, FUZZ, FLAT);
	input_set_abs_params(stat->input_dev_mag, ABS_Y, MAG_MAX_NEG,
						MAG_MAX_POS, FUZZ, FLAT);
	input_set_abs_params(stat->input_dev_mag, ABS_Z, MAG_MAX_NEG,
						MAG_MAX_POS, FUZZ, FLAT);

	err = input_register_device(stat->input_dev_mag);
	if (err) {
		dev_err(&stat->client->dev,
			"unable to register magnetometer input device %s\n",
				stat->input_dev_mag->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(stat->input_dev_mag);
err0:
	return err;
}

static void lsm9ds1_input_cleanup(struct lsm9ds1_mag_status *stat)
{
	input_unregister_device(stat->input_dev_mag);
	input_free_device(stat->input_dev_mag);
}

static void poll_function_work_mag(struct work_struct *input_work_mag)
{
	struct lsm9ds1_mag_status *stat;
	int xyz[3] = { 0 };
	int err;

	stat = container_of((struct work_struct *)input_work_mag,
			struct lsm9ds1_mag_status, input_work_mag);

	if(atomic_read(&stat->enabled_mag)) {
		err = lsm9ds1_mag_get_data(stat, xyz);
		if (err < 0)
			dev_err(&stat->client->dev, "get_magnetometer_data"
								" failed\n");
		else
			lsm9ds1_mag_report_values(stat, xyz);
	}

	hrtimer_start(&stat->hr_timer_mag, stat->ktime_mag, HRTIMER_MODE_REL);
}

enum hrtimer_restart poll_function_read_mag(struct hrtimer *timer)
{
	struct lsm9ds1_mag_status *stat;


	stat = container_of((struct hrtimer *)timer,
				struct lsm9ds1_mag_status, hr_timer_mag);

	queue_work(lsm9ds1_mag_workqueue, &stat->input_work_mag);
	return HRTIMER_NORESTART;
}

#ifdef CONFIG_OF
static const struct of_device_id lsm9ds1_mag_dt_id[] = {
	{.compatible = "st,lsm9ds1-mag",},
	{},
};
MODULE_DEVICE_TABLE(of, lsm9ds1_mag_dt_id);

static int lsm9ds1_mag_parse_dt(struct lsm9ds1_mag_status *stat,
                                        struct device* dev)
{
	struct device_node *dn;
	uint8_t i, j;
	uint32_t val;
	short vect[9];

	if (of_match_device(lsm9ds1_mag_dt_id, dev)) {
		dn = dev->of_node;
		stat->pdata_mag->of_node = dn;

		stat->pdata_mag->gpio_int_m = of_get_gpio(dn, 0);
		if (!gpio_is_valid(stat->pdata_mag->gpio_int_m)) {
			dev_err(dev, "failed to get gpio_int_m\n");

			stat->pdata_mag->gpio_int_m = LSM9DS1_INT_M_GPIO_DEF;
		}

		if (of_property_read_u16_array(dn, "rot-matrix", vect,
			      ARRAY_SIZE(vect)) >= 0) {
			for (j = 0; j < 3; j++) {
				for (i = 0; i < 3; i++) {
					stat->pdata_mag->rot_matrix[i][j] =
						(short)vect[3 * j + i];
				}
			}
		} else {
			for (j = 0; j < 3; j++) {
				for (i = 0; i < 3; i++) {
					stat->pdata_mag->rot_matrix[i][j] =
				default_lsm9ds1_mag_pdata.rot_matrix[i][j];
				}
			}
		}

		if (!of_property_read_u32(dn, "poll-interval", &val)) {
			stat->pdata_mag->poll_interval = val;
		} else {
			stat->pdata_mag->poll_interval =
				LSM9DS1_M_POLL_INTERVAL_DEF;
		}

		if (!of_property_read_u32(dn, "min-interval", &val)) {
			stat->pdata_mag->min_interval = val;
		} else {
			stat->pdata_mag->min_interval =
				LSM9DS1_MAG_MIN_POLL_PERIOD_MS;
		}

		if (!of_property_read_u32(dn, "fs-range", &val)) {
			stat->pdata_mag->fs_range = val;
		} else {
			stat->pdata_mag->fs_range = LSM9DS1_MAG_FS_4G;
		}
		return 0;
	}
	return -1;
}
#endif

static int lsm9ds1_mag_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct lsm9ds1_mag_status *stat;

	u32 smbus_func = I2C_FUNC_SMBUS_BYTE_DATA |
			I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK;

	int err = -1;
	dev_info(&client->dev, "probe start.\n");
	stat = kzalloc(sizeof(struct lsm9ds1_mag_status), GFP_KERNEL);
	if (stat == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
				"failed to allocate memory for module data: "
					"%d\n", err);
		goto exit_check_functionality_failed;
	}

	stat->use_smbus = 0;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev, "client not i2c capable\n");
		if (i2c_check_functionality(client->adapter, smbus_func)){
			stat->use_smbus = 1;
			dev_warn(&client->dev, "client using SMBUS\n");
		} else {
			err = -ENODEV;
			dev_err(&client->dev, "client nor SMBUS capable\n");
			goto exit_check_functionality_failed;
		}
	}

	if(lsm9ds1_mag_workqueue == 0)
		lsm9ds1_mag_workqueue = create_workqueue("lsm9ds1_workqueue");

	hrtimer_init(&stat->hr_timer_mag, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stat->hr_timer_mag.function = &poll_function_read_mag;

	mutex_init(&stat->lock);
	mutex_lock(&stat->lock);

	stat->client = client;
	i2c_set_clientdata(client, stat);

	stat->pdata_mag = kzalloc(sizeof(*stat->pdata_mag), GFP_KERNEL);
	if(stat->pdata_mag == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
			"failed to allocate memory for pdata: %d\n", err);
		goto err_mutexunlock;
	}

#ifdef CONFIG_OF
	lsm9ds1_mag_parse_dt(stat, &client->dev);
#else
	if (client->dev.platform_data == NULL) {
		memcpy(stat->pdata_mag, &default_lsm9ds1_mag_pdata,
						sizeof(*stat->pdata_mag));
		dev_info(&client->dev,
			"using default plaform_data for magnetometer\n");
	}
	else {
		memcpy(stat->pdata_mag, client->dev.platform_data,
						sizeof(*stat->pdata_mag));
	}
#endif

	err = lsm9ds1_mag_validate_pdata(stat);
	if (err < 0) {
		dev_err(&client->dev, "failed to validate platform data for "
							"magnetometer\n");
		goto exit_kfree_pdata;
	}

	if (stat->pdata_mag->init) {
		err = stat->pdata_mag->init();
		if (err < 0) {
			dev_err(&client->dev, "magnetometer init failed: "
								"%d\n", err);
			goto err_pdata_mag_init;
		}
	}

	err = lsm9ds1_hw_init(stat);
	if (err < 0) {
		dev_err(&client->dev, "hw init failed: %d\n", err);
		goto err_hw_init;
	}

	err = lsm9ds1_mag_device_power_on(stat);
	if (err < 0) {
		dev_err(&client->dev, "magnetometer power on failed: "
								"%d\n", err);
		goto err_pdata_init;
	}

	err = lsm9ds1_mag_update_fs_range(stat, stat->pdata_mag->fs_range);
	if (err < 0) {
		dev_err(&client->dev, "update_fs_range on magnetometer "
								"failed\n");
		goto  err_power_off_mag;
	}

	err = lsm9ds1_mag_update_odr(stat, stat->pdata_mag->poll_interval);
	if (err < 0) {
		dev_err(&client->dev, "update_odr on magnetometer failed\n");
		goto  err_power_off;
	}

	err = lsm9ds1_mag_input_init(stat);
	if (err < 0) {
		dev_err(&client->dev, "magnetometer input init failed\n");
		goto err_power_off;
	}

	err = create_sysfs_interfaces(&client->dev);
	if (err < 0) {
		dev_err(&client->dev,
		"device %s sysfs register failed\n", LSM9DS1_MAG_DEV_NAME);
		goto err_input_cleanup;
	}

	lsm9ds1_mag_device_power_off(stat);

	INIT_WORK(&stat->input_work_mag, poll_function_work_mag);

	mutex_unlock(&stat->lock);
	dev_info(&client->dev, "%s: probed\n", LSM9DS1_MAG_DEV_NAME);
	return 0;

err_input_cleanup:
	lsm9ds1_input_cleanup(stat);
err_power_off:
err_power_off_mag:
	lsm9ds1_mag_device_power_off(stat);
err_hw_init:
err_pdata_init:
err_pdata_mag_init:
	if (stat->pdata_mag->exit)
		stat->pdata_mag->exit();
exit_kfree_pdata:
	kfree(stat->pdata_mag);
err_mutexunlock:
	mutex_unlock(&stat->lock);
	kfree(stat);
	if(!lsm9ds1_mag_workqueue) {
		flush_workqueue(lsm9ds1_mag_workqueue);
		destroy_workqueue(lsm9ds1_mag_workqueue);
	}
exit_check_functionality_failed:
	pr_err("%s: Driver Init failed\n", LSM9DS1_MAG_DEV_NAME);
	return err;
}

static int lsm9ds1_mag_remove(struct i2c_client *client)
{
	struct lsm9ds1_mag_status *stat = i2c_get_clientdata(client);

	lsm9ds1_mag_disable(stat);
	lsm9ds1_mag_input_cleanup(stat);

	remove_sysfs_interfaces(&client->dev);

	if (stat->pdata_mag->exit)
		stat->pdata_mag->exit();

	if(!lsm9ds1_mag_workqueue) {
		flush_workqueue(lsm9ds1_mag_workqueue);
		destroy_workqueue(lsm9ds1_mag_workqueue);
	}

	kfree(stat->pdata_mag);
	kfree(stat);
	return 0;
}

static const struct i2c_device_id lsm9ds1_mag_id[]
				= { { LSM9DS1_MAG_DEV_NAME, 0 }, { }, };

MODULE_DEVICE_TABLE(i2c, lsm9ds1_mag_id);

static struct i2c_driver lsm9ds1_driver = {
	.driver = {
			.owner = THIS_MODULE,
			.name = LSM9DS1_MAG_DEV_NAME,
#ifdef CONFIG_OF
			.of_match_table =
				of_match_ptr(lsm9ds1_mag_dt_id),
#endif
		  },
	.probe = lsm9ds1_mag_probe,
	.remove = lsm9ds1_mag_remove,
	.id_table = lsm9ds1_mag_id,
};

static int __init lsm9ds1_init(void)
{
	pr_info("%s driver: init\n", LSM9DS1_MAG_DEV_NAME);
	return i2c_add_driver(&lsm9ds1_driver);
}

static void __exit lsm9ds1_exit(void)
{
	pr_info("%s driver exit\n", LSM9DS1_MAG_DEV_NAME);
	i2c_del_driver(&lsm9ds1_driver);
}

module_init(lsm9ds1_init);
module_exit(lsm9ds1_exit);

MODULE_DESCRIPTION("lsm9ds1 magnetometer driver");
MODULE_AUTHOR("Giuseppe Barba, Matteo Dameno, Denis Ciocca,"
							" STMicroelectronics");
MODULE_LICENSE("GPL");
