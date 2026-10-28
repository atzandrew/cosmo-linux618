// SPDX-License-Identifier: GPL-2.0
/* Read-only: TOPRGU watchdog/request config, SPM PCM watchdog registers and
 * ATF's suspend pwr_ctrl defaults (SIP SPM_GET_PWR_CTRL_ARGS 0xC2000229,
 * stock mtk_spm_sleep.c:318; MT6771 enum indices from mtk_spm_internal.h). */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>

#define TOPRGU_BASE	0x10007000
#define SPM_BASE	0x10006000
#define SIP_GET_PWR_CTRL 0xC2000229
#define SPM_PWR_CTRL_SUSPEND 17

static unsigned long get_pwr(unsigned long idx)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SIP_GET_PWR_CTRL, SPM_PWR_CTRL_SUSPEND, idx, 0, 0, 0, 0, 0, &res);
	return res.a0;
}

static int __init cosmo_rgu_init(void)
{
	void __iomem *r = ioremap(TOPRGU_BASE, 0x100);
	void __iomem *s = ioremap(SPM_BASE, 0x100);

	if (!r || !s)
		goto out;
	pr_info("cosmo_rgu: WDT_MODE 0x%08x LENGTH 0x%08x STA 0x%08x NONRST_REG 0x%08x NONRST_REG2 0x%08x\n",
		readl(r + 0x00), readl(r + 0x04), readl(r + 0x0c),
		readl(r + 0x20), readl(r + 0x24));
	pr_info("cosmo_rgu: REQ_MODE 0x%08x REQ_IRQ_EN 0x%08x LATCH_CTL 0x%08x DEBUG_CTL 0x%08x\n",
		readl(r + 0x30), readl(r + 0x34), readl(r + 0x44), readl(r + 0x40));
	pr_info("cosmo_rgu: PCM_CON0 0x%08x PCM_CON1 0x%08x PCM_TIMER_VAL 0x%08x PCM_WDT_VAL 0x%08x\n",
		readl(s + 0x18), readl(s + 0x1c), readl(s + 0x30), readl(s + 0x34));
	pr_info("cosmo_rgu: ATF suspend pwr_ctrl: pcm_flags 0x%lx timer_val 0x%lx wake_src 0x%lx wdt_disable %lu\n",
		get_pwr(0), get_pwr(8), get_pwr(12), get_pwr(15));
out:
	if (r)
		iounmap(r);
	if (s)
		iounmap(s);
	return -EAGAIN; /* report only; never stays loaded */
}
module_init(cosmo_rgu_init);
MODULE_LICENSE("GPL");
