/*
 * Copyright (C) 2014 Gateworks Corporation
 *
 * Written by Tim Harvey <tharvey@gateworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

#include <linux/mfd/gsc.h>

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
        "(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int gsc_wdt_start(struct watchdog_device *wdt)
{
	u8 reg = (1 << GSC_CTRL_1_WDT_ENABLE);

	dev_dbg(wdt->dev, "%s timeout=%d\n", __func__, wdt->timeout);

	return gsc_i2c_update(GSC_CTRL_1, reg, reg);
}

static int gsc_wdt_stop(struct watchdog_device *wdt)
{
	u8 reg = (1 << GSC_CTRL_1_WDT_ENABLE);

	dev_dbg(wdt->dev, "%s\n", __func__);

	return gsc_i2c_update(GSC_CTRL_1, reg, 0);
}

static int gsc_wdt_set_timeout(struct watchdog_device *wdt,
			       unsigned int timeout)
{
	u8 long_sel = 0;

	dev_dbg(wdt->dev, "%s: %d\n", __func__, timeout);

	switch (timeout) {
	case 60:
		long_sel = (1 << GSC_CTRL_1_WDT_TIME);
	case 30:
		gsc_i2c_update(GSC_CTRL_1,
			       (1 << GSC_CTRL_1_WDT_TIME),
			       (long_sel << GSC_CTRL_1_WDT_TIME));
		wdt->timeout = timeout;
		return 0;
	}

	return -EINVAL;
}

static const struct watchdog_info gsc_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "GSC Watchdog"
};

static const struct watchdog_ops gsc_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= gsc_wdt_start,
	.stop		= gsc_wdt_stop,
	.set_timeout	= gsc_wdt_set_timeout,
};

static int gsc_wdt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct watchdog_device *wdt;
	u8 reg;

	if (gsc_get_fwver() < 44) {
		dev_err(&pdev->dev, "firmware v44 or newer required "
			"for watchdog functionality\n");
		return -EINVAL;
	}

	/* ensure WD bit enabled */
	if (gsc_i2c_read(GSC_CTRL_1, &reg)) {
		dev_err(&pdev->dev, "failed reading GSC_CTRL_1\n");
		return -EIO;
	}
	if (!(reg & (1 << GSC_CTRL_1_WDT_ENABLE))) {
		dev_err(&pdev->dev, "GSC WDT_ENABLE not enabled"
			" - must be manually enabled\n");
		return -EINVAL;
	}

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->info		= &gsc_wdt_info;
	wdt->ops		= &gsc_wdt_ops;
	wdt->status		= 0;
	wdt->timeout		= 30;
	wdt->min_timeout	= 30;
	wdt->max_timeout	= 60;

	watchdog_set_nowayout(wdt, nowayout);
	platform_set_drvdata(pdev, wdt);

	ret = watchdog_register_device(wdt);
	if (ret) {
		platform_set_drvdata(pdev, NULL);
		return ret;
	}
	dev_info(&pdev->dev, "registerted watchdog (nowayout=%d)\n",
		 nowayout);

	return 0;
}

static int gsc_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(wdt);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int gsc_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct watchdog_device *wdt = platform_get_drvdata(pdev);
	if (watchdog_active(wdt))
		return gsc_wdt_stop(wdt);

	return 0;
}

static int gsc_wdt_resume(struct platform_device *pdev)
{
	struct watchdog_device *wdt = platform_get_drvdata(pdev);
	if (watchdog_active(wdt))
		return gsc_wdt_start(wdt);

	return 0;
}
#else
#define gsc_wdt_suspend        NULL
#define gsc_wdt_resume         NULL
#endif

static const struct of_device_id gsc_wdt_dt_ids[] = {
	{ .compatible = "gw,gsc_wdt", },
	{ /* sentinel */ }
};

static struct platform_driver gsc_wdt_driver = {
	.probe		= gsc_wdt_probe,
	.remove		= gsc_wdt_remove,
	.suspend	= gsc_wdt_suspend,
	.resume		= gsc_wdt_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "gsc_wdt",
		.of_match_table = gsc_wdt_dt_ids,
	},
};

module_platform_driver(gsc_wdt_driver);

MODULE_AUTHOR("Tim Harvey <tharvey@gateworks.com>");
MODULE_DESCRIPTION("Watchdog driver for GSC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gsc_wdt");
