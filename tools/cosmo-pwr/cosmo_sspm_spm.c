// SPDX-License-Identifier: GPL-2.0
/* Sleep DS3 test: stock SSPM handshake around system suspend.
 * Stock spm_suspend_pre_process / post_process (mtk_spm_sleep_mt6771.c)
 * send IPI_ID_SPM_SUSPEND (polling) with struct spm_data, cmd SPM_SUSPEND
 * (0) before the sleep and SPM_RESUME (1) after; timestamp and option
 * fields are left 0 (no SSPM timesync, no GPS).
 * Mailbox (stock DT sspm@10440000, mt6771 sspm_ipi_*.h, sspm_mbox.c):
 * mbox1 data 0x10460000, slot 2, 8 words; IN_IRQ 0x10461000 (write
 * 1 << 1 to ring the SSPM), OUT_IRQ 0x10461004 (bit 1 = ack, write 1 to
 * clear); ack data is read back from slot 2. Runs from syscore suspend
 * and resume (CPU0, IRQs off), right before / after the platform enter. */
#include <linux/module.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#define MBOX1_DATA	0x10460000
#define MBOX1_CTRL	0x10461000
#define SLOT		2
#define LEN		8
#define IRQ_BIT		BIT(6 - 5)	/* IPI_ID_SPM_SUSPEND - mbox1 start */

static uint timeout_us = 500000;
module_param(timeout_us, uint, 0644);
static int sus_ret = 99, res_ret = 99;
static uint sus_ack = 0xdead, res_ack = 0xdead, sends;
module_param(sus_ret, int, 0444);
module_param(res_ret, int, 0444);
module_param(sus_ack, uint, 0444);
module_param(res_ack, uint, 0444);
module_param(sends, uint, 0444);

static void __iomem *data, *ctrl;

static int send(u32 cmd, u32 *ack)
{
	u32 v;
	int i, ret;

	if (readl(ctrl + 4) & IRQ_BIT)
		return -EBUSY;	/* previous ack not cleared */
	writel(cmd, data + 4 * SLOT);
	for (i = 1; i < LEN; i++)
		writel(0, data + 4 * (SLOT + i));
	writel(IRQ_BIT, ctrl);
	sends++;
	ret = readl_poll_timeout_atomic(ctrl + 4, v, v & IRQ_BIT, 1, timeout_us);
	if (ret)
		return ret;
	writel(IRQ_BIT, ctrl + 4);
	*ack = readl(data + 4 * SLOT);
	return 0;
}

static int sspm_suspend(void *unused)
{
	sus_ret = send(0, &sus_ack);	/* SPM_SUSPEND */
	return 0;	/* never block the suspend from here */
}

static void sspm_resume(void *unused)
{
	res_ret = send(1, &res_ack);	/* SPM_RESUME */
	pr_info("cosmo_sspm_spm: SPM_SUSPEND ret %d ack 0x%x, SPM_RESUME ret %d ack 0x%x\n",
		sus_ret, sus_ack, res_ret, res_ack);
}

static const struct syscore_ops ops = {
	.suspend = sspm_suspend,
	.resume = sspm_resume,
};
static struct syscore sc = { .ops = &ops };

static int __init cosmo_sspm_spm_init(void)
{
	data = ioremap(MBOX1_DATA, 0x100);
	ctrl = ioremap(MBOX1_CTRL, 8);
	if (!data || !ctrl)
		return -ENOMEM;
	if (readl(data + 4 * SLOT) != 0x4d010608)
		pr_warn("cosmo_sspm_spm: slot tag 0x%08x (boot value 0x4d010608)\n",
			readl(data + 4 * SLOT));
	register_syscore(&sc);
	pr_info("cosmo_sspm_spm: loaded\n");
	return 0;
}

static void __exit cosmo_sspm_spm_exit(void)
{
	unregister_syscore(&sc);
	iounmap(data);
	iounmap(ctrl);
}
module_init(cosmo_sspm_spm_init);
module_exit(cosmo_sspm_spm_exit);
MODULE_LICENSE("GPL");
