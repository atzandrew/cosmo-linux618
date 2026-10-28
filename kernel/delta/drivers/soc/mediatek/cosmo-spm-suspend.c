// SPDX-License-Identifier: GPL-2.0
/*
 * Cosmo Communicator: system suspend through the vendor ATF SPM path.
 *
 * Mainline suspends with PSCI SYSTEM_SUSPEND. The MT6771 vendor ATF does
 * not run its SPM suspend program for that call (no SPM timer wake, no
 * PCM watchdog). Stock 4.4 (spm_v4 mtk_spm_sleep.c spm_go_to_sleep)
 * instead makes SIP SPM_SUSPEND_ARGS(pcm_flags, pcm_flags1, timer) and
 * then PSCI CPU_SUSPEND with power state 0x01010005 on the last CPU.
 * ATF's suspend setup then arms the 30 s SPM PCM watchdog itself.
 *
 * Nothing changes until "enable" is written (1 = replace the suspend
 * ops; reboot to go back). keep_wdt re-enables the TOPRGU watchdog for
 * the sleep (mtk_wdt_suspend stopped it) as a second escape while the
 * path is tested. Does nothing on other machines.
 */
#include <linux/arm-smccc.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/psci.h>
#include <linux/suspend.h>

#define SIP_SPM_SUSPEND_ARGS	0xC200021A
#define STOCK_SUSPEND_STATE	0x01010005	/* stock DT idle state "SUSPEND" */
#define TOPRGU_BASE		0x10007000
#define SPM_BASE		0x10006000
#define WDT_MODE_KEY		0x22000000
#define WDT_MODE_EN		BIT(0)
#define WDT_RST_RELOAD		0x1971

static uint flags = 0x80098;	/* stock slp_suspend_ops_enter */
module_param(flags, uint, 0644);
static uint wake_s = 10;	/* SPM timer wake */
module_param(wake_s, uint, 0644);
static bool keep_wdt = true;
module_param(keep_wdt, bool, 0644);
static uint entered, returned;
module_param(entered, uint, 0444);
module_param(returned, uint, 0444);
static int last_ret;
module_param(last_ret, int, 0444);

static void __iomem *rgu, *spm;

static int cosmo_spm_enter(suspend_state_t state)
{
	struct arm_smccc_res res;
	u32 mode = readl(rgu);
	int ret;

	if (keep_wdt) {
		writel(WDT_RST_RELOAD, rgu + 0x08);
		writel(WDT_MODE_KEY | ((mode | WDT_MODE_EN) & 0xffff), rgu);
	}
	arm_smccc_smc(SIP_SPM_SUSPEND_ARGS, flags, 0, wake_s * 32768,
		      0, 0, 0, 0, &res);
	entered++;
	ret = psci_cpu_suspend_enter(STOCK_SUSPEND_STATE);
	returned++;
	last_ret = ret;
	if (keep_wdt)
		writel(WDT_MODE_KEY | (mode & 0xffff), rgu);
	pr_info("cosmo-spm-suspend: SUSPEND_ARGS -> 0x%lx, CPU_SUSPEND -> %d, R12 0x%08x R13 0x%08x WAKEUP_STA 0x%08x EXT 0x%08x FSM 0x%08x SW_DEBUG 0x%08x\n",
		res.a0, ret, readl(spm + 0x130), readl(spm + 0x134),
		readl(spm + 0x15c), readl(spm + 0x160), readl(spm + 0x178),
		readl(spm + 0x604));
	return ret;
}

static const struct platform_suspend_ops cosmo_spm_ops = {
	.valid = suspend_valid_only_mem,
	.enter = cosmo_spm_enter,
};

static bool enabled;

static int enable_set(const char *val, const struct kernel_param *kp)
{
	bool on;
	int ret = kstrtobool(val, &on);

	if (ret)
		return ret;
	if (!rgu || !spm)
		return -ENODEV;
	if (on && !enabled) {
		suspend_set_ops(&cosmo_spm_ops);
		enabled = true;
		pr_info("cosmo-spm-suspend: stock SPM suspend path enabled (flags 0x%x, wake %u s, keep_wdt %d)\n",
			flags, wake_s, keep_wdt);
	}
	return 0;
}

static const struct kernel_param_ops enable_ops = {
	.set = enable_set,
	.get = param_get_bool,
};
module_param_cb(enable, &enable_ops, &enabled, 0644);

static int __init cosmo_spm_suspend_init(void)
{
	if (!of_machine_is_compatible("planet,cosmo-communicator"))
		return 0;
	rgu = ioremap(TOPRGU_BASE, 0x100);
	spm = ioremap(SPM_BASE, 0x1000);
	if (!rgu || !spm)
		return -ENOMEM;
	pr_info("cosmo-spm-suspend: ready (write 1 to enable)\n");
	return 0;
}
late_initcall(cosmo_spm_suspend_init);
