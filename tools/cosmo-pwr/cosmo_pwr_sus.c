// SPDX-License-Identifier: GPL-2.0
/* Read-only: log SPM PWR_STATUS in the noirq suspend and resume phases
 * and when CPU1 goes offline during suspend (after every noirq callback). */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/cpuhotplug.h>
#include <linux/suspend.h>

static void __iomem *b;
static struct platform_device *pdev;

static void show(const char *when)
{
	u32 s = readl(b + 0x180) & readl(b + 0x184);

	pr_info("cosmo_pwr_sus: %s PWR_STATUS&2ND 0x%08x DIS %u CONN %u MFG %u\n",
		when, s, !!(s & BIT(3)), !!(s & BIT(1)), !!(s & BIT(4)));
}
static int sus_noirq(struct device *d) { show("suspend_noirq"); return 0; }
static int res_noirq(struct device *d) { show("resume_noirq"); return 0; }
static const struct dev_pm_ops ops = {
	.suspend_noirq = sus_noirq, .resume_noirq = res_noirq,
};
static int hp_state;
static int cpu_dead(unsigned int cpu)
{
	if (cpu == 1 && pm_suspend_target_state != PM_SUSPEND_ON)
		show("cpu1 dead");
	return 0;
}

static struct platform_driver drv = {
	.driver = { .name = "cosmo-pwr-sus", .pm = &ops },
};

static int __init init_(void)
{
	int ret;

	b = ioremap(0x10006000, 0x1000);
	if (!b)
		return -ENOMEM;
	ret = platform_driver_register(&drv);
	if (ret)
		goto err;
	pdev = platform_device_register_simple("cosmo-pwr-sus", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		platform_driver_unregister(&drv);
		goto err;
	}
	hp_state = cpuhp_setup_state_nocalls(CPUHP_BP_PREPARE_DYN, "cosmo/pwr:dead", NULL, cpu_dead);
	if (hp_state < 0) {
		platform_device_unregister(pdev);
		platform_driver_unregister(&drv);
		ret = hp_state;
		goto err;
	}
	show("load");
	return 0;
err:
	iounmap(b);
	return ret;
}
static void __exit exit_(void)
{
	cpuhp_remove_state_nocalls(hp_state);
	platform_device_unregister(pdev);
	platform_driver_unregister(&drv);
	iounmap(b);
}
module_init(init_);
module_exit(exit_);
MODULE_LICENSE("GPL");
