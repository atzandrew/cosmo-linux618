// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cosmo Communicator sleep guard: refuse system sleep while the panel
 * bias is on.
 *
 * Sleeping with an LCD bias still applied can burn the panel. Stock 4.4
 * never does it: Android blanks the display first (lcm_suspend, then
 * lcm_suspend_power -> display_bias_disable). On mainline the NT36672
 * panel driver switches the MT6370 DSVPOS/DSVNEG rails off when DRM
 * suspends its CRTCs in the .prepare phase (mtk_drm_sys_prepare).
 *
 * This driver checks the result. Its .suspend callback runs after every
 * device's .prepare and reads both rails from the MT6370 (no regmap
 * cache). If either reads enabled, or cannot be read, the suspend fails
 * with -EBUSY and the PM core resumes everything again. Until the driver
 * has probed, a PM notifier refuses every suspend and hibernation.
 *
 * Read-only: nothing here switches a rail.
 */
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>

struct sleep_guard {
	struct regulator *vpos;
	struct regulator *vneg;
};

static bool guard_ready;

static char last_check[40] = "none";
module_param_string(last_check, last_check, sizeof(last_check), 0444);
MODULE_PARM_DESC(last_check, "result of the last suspend check");

static unsigned int refused;
module_param(refused, uint, 0444);
MODULE_PARM_DESC(refused, "suspends refused since boot");

static unsigned int passed;
module_param(passed, uint, 0444);
MODULE_PARM_DESC(passed, "suspend checks passed since boot");

static int sleep_guard_pm_notify(struct notifier_block *nb,
				 unsigned long event, void *unused)
{
	if (event != PM_SUSPEND_PREPARE && event != PM_HIBERNATION_PREPARE)
		return NOTIFY_DONE;
	if (READ_ONCE(guard_ready))
		return NOTIFY_DONE;

	pr_err("cosmo-sleep-guard: not probed, refusing sleep\n");
	strscpy(last_check, "refused: not probed", sizeof(last_check));
	refused++;
	return notifier_from_errno(-EBUSY);
}

static struct notifier_block sleep_guard_nb = {
	.notifier_call = sleep_guard_pm_notify,
};

static int sleep_guard_suspend(struct device *dev)
{
	struct sleep_guard *g = dev_get_drvdata(dev);
	int vpos = regulator_is_enabled(g->vpos);
	int vneg = regulator_is_enabled(g->vneg);

	if (vpos < 0 || vneg < 0) {
		snprintf(last_check, sizeof(last_check),
			 "refused: read error %d %d", vpos, vneg);
		refused++;
		dev_err(dev, "cannot read panel bias (%d %d), refusing sleep\n",
			vpos, vneg);
		return -EBUSY;
	}

	if (vpos || vneg) {
		snprintf(last_check, sizeof(last_check),
			 "refused: vpos %d vneg %d", vpos, vneg);
		refused++;
		dev_err(dev, "panel bias on (vpos %d vneg %d), refusing sleep\n",
			vpos, vneg);
		return -EBUSY;
	}

	strscpy(last_check, "pass: vpos 0 vneg 0", sizeof(last_check));
	passed++;
	dev_info(dev, "panel bias off (vpos 0 vneg 0)\n");
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(sleep_guard_pm_ops, sleep_guard_suspend, NULL);

static int sleep_guard_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sleep_guard *g;

	g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;

	/*
	 * Shared get (the panel owns the rails). A missing supply would give
	 * a dummy regulator, which reads enabled: sleep stays refused.
	 */
	g->vpos = devm_regulator_get(dev, "vpos");
	if (IS_ERR(g->vpos))
		return dev_err_probe(dev, PTR_ERR(g->vpos), "vpos supply\n");

	g->vneg = devm_regulator_get(dev, "vneg");
	if (IS_ERR(g->vneg))
		return dev_err_probe(dev, PTR_ERR(g->vneg), "vneg supply\n");

	platform_set_drvdata(pdev, g);
	WRITE_ONCE(guard_ready, true);
	dev_info(dev, "armed: vpos %d vneg %d\n",
		 regulator_is_enabled(g->vpos), regulator_is_enabled(g->vneg));
	return 0;
}

static const struct of_device_id sleep_guard_of_match[] = {
	{ .compatible = "planet,cosmo-sleep-guard" },
	{ }
};

static struct platform_driver sleep_guard_driver = {
	.probe = sleep_guard_probe,
	.driver = {
		.name = "cosmo-sleep-guard",
		.of_match_table = sleep_guard_of_match,
		.pm = pm_sleep_ptr(&sleep_guard_pm_ops),
		.suppress_bind_attrs = true,
	},
};

static int __init sleep_guard_init(void)
{
	int ret;

	ret = register_pm_notifier(&sleep_guard_nb);
	if (ret)
		return ret;
	ret = platform_driver_register(&sleep_guard_driver);
	/* On failure the notifier stays: sleep remains refused. */
	return ret;
}
device_initcall(sleep_guard_init);

MODULE_DESCRIPTION("Cosmo Communicator: refuse sleep while the panel bias is on");
MODULE_LICENSE("GPL");
