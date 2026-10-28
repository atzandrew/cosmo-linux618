// SPDX-License-Identifier: GPL-2.0
/* Fake hang for the in-sleep watchdog (W1). Makes the stock "legacy" suspend
 * setup call while awake: SIP SPM_SUSPEND_ARGS(flags, 0, timer), then
 * SIP SPM_ARGS(SUSPEND). In this ATF that call arms the SPM PCM watchdog
 * (PCM_WDT_VAL = PCM_TIMER_VAL + 30 s, PCM_CON1 bit 8) and kicks the SPM
 * firmware into its suspend program, which waits for this CPU's WFI.
 * The CPU then spins (no WFI), so the SPM never sleeps and never wakes:
 * the PCM watchdog should request a TOPRGU reset ~timer_s + 30 s later.
 * If nothing happens within spin_s, it calls SPM_ARGS(SUSPEND_FINISH)
 * (disarms the PCM watchdog) and restores the TOPRGU watchdog.
 * toprgu_off=1 clears the TOPRGU WDT_MODE enable bit for the spin, as
 * mainline mtk_wdt_suspend does in a real suspend. */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>

#define TOPRGU_BASE	0x10007000
#define SPM_BASE	0x10006000
#define SIP_SPM_SUSPEND_ARGS	0xC200021A
#define SIP_SPM_ARGS		0xC2000228
#define SPM_ARGS_SUSPEND	2
#define SPM_ARGS_SUSPEND_FINISH	3
#define WDT_MODE_KEY	0x22000000
#define WDT_MODE_EN	BIT(0)

static int cpu = 3;
module_param(cpu, int, 0444);
static uint timer_s = 2;
module_param(timer_s, uint, 0444);
static uint spin_s = 60;
module_param(spin_s, uint, 0444);
static uint flags = 0x80098;	/* stock slp_suspend_ops_enter */
module_param(flags, uint, 0444);
static bool toprgu_off;
module_param(toprgu_off, bool, 0444);
static bool go;
module_param(go, bool, 0444);

static void __iomem *rgu, *spm;

static unsigned long sip(unsigned long id, unsigned long a1,
			 unsigned long a2, unsigned long a3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(id, a1, a2, a3, 0, 0, 0, 0, &res);
	return res.a0;
}

static void dump(const char *tag)
{
	pr_info("cosmo_pcmwdt: %s: PCM_CON1 0x%08x TIMER 0x%08x WDT_VAL 0x%08x CPU_WAKEUP_EVENT 0x%08x WDT_MODE 0x%08x\n",
		tag, readl(spm + 0x1c), readl(spm + 0x30), readl(spm + 0x34),
		readl(spm + 0xb0), readl(rgu + 0x00));
}

static int fake_hang(void *unused)
{
	u32 mode = readl(rgu + 0x00);
	unsigned long r;
	uint i;

	preempt_disable();
	dump("before");
	if (toprgu_off)
		writel(WDT_MODE_KEY | (mode & ~WDT_MODE_EN & 0xffff), rgu + 0x00);
	r = sip(SIP_SPM_SUSPEND_ARGS, flags, 0, timer_s * 32768);
	pr_info("cosmo_pcmwdt: SUSPEND_ARGS(0x%x, 0, %u s) -> 0x%lx\n", flags, timer_s, r);
	r = sip(SIP_SPM_ARGS, SPM_ARGS_SUSPEND, 0, 0);
	dump("armed");
	pr_info("cosmo_pcmwdt: SPM_ARGS(SUSPEND) -> 0x%lx on cpu%d; expect reset ~%u s from now\n",
		r, smp_processor_id(), timer_s + 30);
	for (i = 1; i <= spin_s; i++) {
		mdelay(1000);
		if (i % 5 == 0)
			pr_info("cosmo_pcmwdt: t+%u s still running\n", i);
	}
	r = sip(SIP_SPM_ARGS, SPM_ARGS_SUSPEND_FINISH, 0, 0);
	dump("finish");
	if (toprgu_off) {
		writel(WDT_MODE_KEY | (mode & 0xffff), rgu + 0x00);
		writel(0x1971, rgu + 0x08);	/* WDT_RST reload */
	}
	dump("restored");
	pr_info("cosmo_pcmwdt: NO RESET after %u s (FAIL)\n", spin_s);
	preempt_enable();
	return 0;
}

static int __init cosmo_pcmwdt_init(void)
{
	struct task_struct *t;

	rgu = ioremap(TOPRGU_BASE, 0x100);
	spm = ioremap(SPM_BASE, 0x100);
	if (!rgu || !spm)
		return -ENOMEM;
	if (!go) {
		dump("read-only (go=0)");
		iounmap(rgu);
		iounmap(spm);
		return -EAGAIN;
	}
	t = kthread_create(fake_hang, NULL, "cosmo_pcmwdt");
	if (IS_ERR(t))
		return PTR_ERR(t);
	kthread_bind(t, cpu);
	wake_up_process(t);
	return 0;	/* stays loaded; rmmod only after the thread finished */
}
module_init(cosmo_pcmwdt_init);
MODULE_LICENSE("GPL");
