/*
 * Copyright (C) 2014 Gateworks Corporation
 *
 * Written by Tim Harvey <tharvey@gateworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __GSC_H_
#define __GSC_H_

struct gsc_platform_data {
	struct pca953x_platform_data *pca953x_data;
};

/* Register offsets */
#define GSC_CTRL_0	0x00
#define GSC_CTRL_1	0x01
#define GSC_TIME	0x02
#define GSC_TIME_ADD	0x06
#define GSC_IRQ_STATUS	0x0A
#define GSC_IRQ_ENABLE	0x0B
#define GSC_FW_CRC	0x0C
#define GSC_FW_VER	0x0E
#define GSC_WP		0x0F

/* Bit definitions */
#define GSC_CTRL_0_PB_HARD_RESET	0
#define GSC_CTRL_0_PB_CLEAR_SECURE_KEY	1
#define GSC_CTRL_0_PB_SOFT_POWER_DOWN	2
#define GSC_CTRL_0_PB_BOOT_ALTERNATE	3
#define GSC_CTRL_0_PERFORM_CRC		4
#define GSC_CTRL_0_TAMPER_DETECT	5
#define GSC_CTRL_0_SWITCH_HOLD		6

#define GSC_CTRL_1_SLEEP_ENABLE		0
#define GSC_CTRL_1_ACTIVATE_SLEEP	1
#define GSC_CTRL_1_LATCH_SLEEP_ADD	2
#define GSC_CTRL_1_SLEEP_NOWAKEPB	3
#define GSC_CTRL_1_WDT_TIME		4
#define GSC_CTRL_1_WDT_ENABLE		5
#define GSC_CTRL_1_SWITCH_BOOT_ENABLE	6
#define GSC_CTRL_1_SWITCH_BOOT_CLEAR	7

#define GSC_IRQ_PB			0
#define GSC_IRQ_KEY_ERASED		1
#define GSC_IRQ_EEPROM_WP		2
#define GSC_IRQ_GPIO			4
#define GSC_IRQ_TAMPER			5
#define GSC_IRQ_ALT_BOOT		6
#define GSC_IRQ_SWITCH_HOLD		7

/*
 * Read and write single 8-bit registers
 */
int gsc_i2c_write(u8 reg, u8 val);
int gsc_i2c_read(u8 reg, u8 *val);
int gsc_i2c_update(u8 reg, u8 valmask, u8 val);

int gsc_get_fwver(void);
int gsc_powerdown(unsigned long seconds);

#endif
