/*
 * Copyright (C) 2014 Gateworks Corporation
 *
 * Written by Tim Harvey <tharvey@gateworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/mfd/gsc.h>

struct gsc_agent {
	int irq_base;
	int irq_num;
};

static struct gsc_agent *gsc_agent;

static struct irq_chip gsc_irq_chip = {
	.name		= "gsc",
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};

static irqreturn_t gsc_irq(int irq, void *data)
{
	struct gsc_agent *agent = data;
	u8 sts;

	gsc_i2c_read(GSC_IRQ_STATUS, &sts);
	pr_debug("gsc: irq%d status=0x%02x\n", irq, sts);

	while (sts) {
		irq = fls(sts);
		irq--;
		sts &= ~BIT(irq);

		handle_nested_irq(agent->irq_base + irq);
	}

	/* clear all status bits */
	gsc_i2c_write(GSC_IRQ_STATUS, 0);

	return IRQ_HANDLED;
}

int gsc_irq_init(struct device *dev, int irq_num)
{
	static struct irq_chip gsc_mod_irq_chip;
	int ret, i;
	int irq_base, irq_end, nr_irqs;
	struct device_node *node = dev->of_node;
	struct gsc_agent *agent;
	u8 reg;

	agent = devm_kzalloc(dev, sizeof *agent, GFP_KERNEL);
	if (!agent)
		return -ENOMEM;

	nr_irqs = 8;

	/* allocate 8 virq's for GSC status register bits */
	irq_base = irq_alloc_descs(-1, 0, nr_irqs, 0);
	if (irq_base< 0)
		return irq_base;

	irq_domain_add_legacy(node, nr_irqs, irq_base, 0,
			      &irq_domain_simple_ops, NULL);
	irq_end = irq_base + nr_irqs;

	/* clear all GSC interrupts */
	ret = gsc_i2c_read(GSC_IRQ_ENABLE, &reg);
	if (ret < 0) {
		dev_err(dev, "failed reading GSC_IRQ_ENABLE\n");
		return ret;
	}
	if (reg == 0)
		dev_warn(dev, "no GSC interrupts enabled\n");
	gsc_i2c_write(GSC_IRQ_STATUS, 0);

	/* install irq handler for each module */
	gsc_mod_irq_chip = dummy_irq_chip;
	gsc_mod_irq_chip.name = "gsc";

	gsc_irq_chip.irq_ack = dummy_irq_chip.irq_ack;
	agent->irq_base = irq_base;

	for (i = irq_base; i < irq_end; i++) {
		irq_set_chip_data(i, agent);
		irq_set_chip_and_handler(i, &gsc_mod_irq_chip,
					 handle_simple_irq);
		irq_set_nested_thread(i, 1);
	}
	gsc_i2c_write(GSC_IRQ_ENABLE, reg);

	/* install irq handler to demux the GSC IRQ */
	ret = devm_request_threaded_irq(dev, irq_num, NULL, gsc_irq,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"gsc", agent);
	if (ret < 0) {
		dev_err(dev, "could not claim irq%d: %d\n", irq_num, ret);
		goto fail_reqirq;
	}
	enable_irq_wake(irq_num);
	gsc_agent = agent;
	gsc_agent->irq_num = irq_num;

	return irq_base;

fail_reqirq:
	irq_free_descs(irq_base, nr_irqs);

	return ret;
}

int gsc_irq_exit(void)
{
	irq_free_descs(gsc_agent->irq_base, nr_irqs);
	return 0;
}
