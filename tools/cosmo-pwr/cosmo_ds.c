// SPDX-License-Identifier: GPL-2.0
/* Deep-sleep test DS1. Mainline suspend ends in PSCI SYSTEM_SUSPEND; this
 * module only adds stock's last step before WFI (mtk_spm_sleep.c:162):
 * SIP SPM_SUSPEND_ARGS(flags, 0, wake_s * 32768) from syscore suspend
 * (CPU0 only, IRQs off). If ATF runs its SPM suspend program, the SPM
 * timer wakes the board after wake_s and the PCM watchdog is armed.
 * Escape if it does not: keep_wdt=1 re-enables the TOPRGU watchdog
 * (mtk_wdt_suspend stopped it) so a sleep longer than its timeout resets.
 * Resume logs the SPM wake status (stock __spm_get_wakeup_status regs). */
#include <linux/module.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>

#define TOPRGU_BASE	0x10007000
#define SPM_BASE	0x10006000
#define SIP_SPM_SUSPEND_ARGS	0xC200021A
#define WDT_MODE_KEY	0x22000000
#define WDT_MODE_EN	BIT(0)

static uint flags = 0x80098;	/* stock slp_suspend_ops_enter */
module_param(flags, uint, 0644);
static uint wake_s = 10;
module_param(wake_s, uint, 0644);
static bool keep_wdt = true;
module_param(keep_wdt, bool, 0644);
static uint entered, resumed;
module_param(entered, uint, 0444);
module_param(resumed, uint, 0444);

static void __iomem *rgu, *spm;
static unsigned long sip_ret;
static u32 wdt_mode;

static void dump(const char *tag)
{
	pr_info("cosmo_ds: %s: R12 0x%08x R13 0x%08x WAKEUP_STA 0x%08x EXT 0x%08x MISC 0x%08x FSM 0x%08x SW_FLAG 0x%08x SW_DEBUG 0x%08x PCM_CON1 0x%08x TIMER 0x%08x WDT_VAL 0x%08x\n",
		tag, readl(spm + 0x130), readl(spm + 0x134), readl(spm + 0x15c),
		readl(spm + 0x160), readl(spm + 0x164), readl(spm + 0x178),
		readl(spm + 0x600), readl(spm + 0x604), readl(spm + 0x1c),
		readl(spm + 0x30), readl(spm + 0x34));
}

static int ds_suspend(void *unused)
{
	struct arm_smccc_res res;

	wdt_mode = readl(rgu);
	if (keep_wdt) {
		writel(0x1971, rgu + 0x08);	/* reload the counter first */
		writel(WDT_MODE_KEY | ((wdt_mode | WDT_MODE_EN) & 0xffff), rgu);
	}
	arm_smccc_smc(SIP_SPM_SUSPEND_ARGS, flags, 0, wake_s * 32768,
		      0, 0, 0, 0, &res);
	sip_ret = res.a0;
	entered++;
	return 0;
}

static void ds_resume(void *unused)
{
	if (keep_wdt)	/* back as mtk_wdt_suspend left it; mtk_wdt_resume restarts it */
		writel(WDT_MODE_KEY | (wdt_mode & 0xffff), rgu);
	resumed++;
	pr_info("cosmo_ds: SUSPEND_ARGS(0x%x, 0, %u s) -> 0x%lx, WDT_MODE before 0x%x, keep_wdt %d\n",
		flags, wake_s, sip_ret, wdt_mode, keep_wdt);
	dump("resume");
}

static const struct syscore_ops ds_ops = {
	.suspend = ds_suspend,
	.resume = ds_resume,
};

static struct syscore ds_sc = { .ops = &ds_ops };

static int __init cosmo_ds_init(void)
{
	rgu = ioremap(TOPRGU_BASE, 0x100);
	spm = ioremap(SPM_BASE, 0x1000);
	if (!rgu || !spm)
		return -ENOMEM;
	dump("load");
	register_syscore(&ds_sc);
	return 0;
}

static void __exit cosmo_ds_exit(void)
{
	unregister_syscore(&ds_sc);
	iounmap(rgu);
	iounmap(spm);
}
module_init(cosmo_ds_init);
module_exit(cosmo_ds_exit);
MODULE_LICENSE("GPL");
