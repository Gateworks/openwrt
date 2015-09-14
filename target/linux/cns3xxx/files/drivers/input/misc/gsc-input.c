/*
 * Copyright (C) 2014 Gateworks Corporation
 *
 * Written by Tim Harvey <tharvey@gateworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include <linux/mfd/gsc.h>

#define DRIVER_NAME "gsc_input"

static irqreturn_t gsc_input_irq(int irq, void *data)
{
	struct input_dev *input = (struct input_dev *)data;
	int key;
	u8 sts;
	struct irq_desc *desc = irq_to_desc(irq);

	gsc_i2c_read(GSC_IRQ_STATUS, &sts);
	dev_dbg(&input->dev, "irq%d status=0x%02x\n", irq, sts);

	while (sts) {
		irq = fls(sts);
		irq--;
		sts &= ~BIT(irq);

		switch(irq) {
		/* user button press and release within 700ms */
		case GSC_IRQ_PB:
			key = BTN_0;
			break;
		/* user eeprom section has been erased */
		case GSC_IRQ_KEY_ERASED:
			key = BTN_1;
			break;
		/* user eeprom write-protect violation */
		case GSC_IRQ_EEPROM_WP:
			key = BTN_2;
			break;
		/* gpio change event */
		case GSC_IRQ_GPIO:
			key = BTN_3;
			break;
		/* tamper event */
		case GSC_IRQ_TAMPER:
			key = BTN_4;
			break;
		/* user button held down for more than 700ms */
		case GSC_IRQ_SWITCH_HOLD:
			key = BTN_5;
			break;
		default:
			key = 0;
			break;
		}

		if (desc->action->name) {
			dev_dbg(&input->dev, "bit%d: key=0x%03x %s\n", irq,
				key, desc->action->name);
			input_report_key(input, key, 1);
			input_report_key(input, key, 0);
			input_sync(input);
		}
	}

	return IRQ_HANDLED;
}

static int gsc_input_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	int err, i, irq;
	struct resource r;
	static char *irq_names[] = {
		"button",
		"key-erased",
		"eeprom-wp",
		NULL,
		NULL,
		"tamper",
		NULL,
		"button-held"
	};
	u8 reg;

	if (gsc_i2c_read(GSC_CTRL_0, &reg)) {
		dev_err(&pdev->dev, "failed reading GSC_CTRL_0\n");
		return -EIO;
	}

	input = devm_input_allocate_device(&pdev->dev);
	if (!input) {
		dev_err(&pdev->dev, "Can't allocate input device\n");
		return -ENOMEM;
	}

	input->name = DRIVER_NAME;
	input->dev.parent = &pdev->dev;

	input_set_capability(input, EV_KEY, BTN_0);
	input_set_capability(input, EV_KEY, BTN_1);
	input_set_capability(input, EV_KEY, BTN_2);
	input_set_capability(input, EV_KEY, BTN_3);
	input_set_capability(input, EV_KEY, BTN_4);
	input_set_capability(input, EV_KEY, BTN_5);

	platform_set_drvdata(pdev, input);

	for (i = 0; i < 8; i++) {
		if (pdev->dev.of_node) {
			irq = of_irq_to_resource(pdev->dev.of_node, i, &r);
			if (!irq)
				break;
		} else {
			if (!irq_names[i])
				continue;
			r.name = irq_names[i];
			irq = 160 + i;
		}
		err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						gsc_input_irq, 0, r.name,
						input);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to claim irq%d: %d\n", irq,
				err);
			return err;
		}
		dev_dbg(&pdev->dev, "registered irq%d: %s\n", irq, r.name);
	}

	err = input_register_device(input);
	if (err) {
		dev_err(&pdev->dev, "Can't register input device: %d\n", err);
		return err;
	}

	dev_info(&pdev->dev, "input driver installed: CTRL_0=0x%02x\n", reg);

	return 0;
}

static int gsc_input_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "input driver removed\n");
	return 0;
}

static const struct of_device_id gsc_input_dt_ids[] = {
	{ .compatible = "gw,gsc_input", },
	{ }
};

static struct platform_driver gsc_input_driver = {
	.probe		= gsc_input_probe,
	.remove		= gsc_input_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = gsc_input_dt_ids,
	},
};

module_platform_driver(gsc_input_driver);

MODULE_AUTHOR("Tim Harvey <tharvey@gateworks.com>");
MODULE_DESCRIPTION("Input driver for GSC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
