// SPDX-License-Identifier: GPL-2.0
/*
 * Cosmo Communicator: system suspend the stock 4.4 way (spm_v4).
 *
 * Mainline suspends with PSCI SYSTEM_SUSPEND; the MT6771 vendor ATF does
 * not run its SPM suspend program for that call. This driver replaces the
 * platform enter with stock's sequence (mtk_sleep.c slp_suspend_ops_enter,
 * mtk_spm_sleep.c spm_go_to_sleep, mtk_spm_sleep_mt6771.c):
 *   checks: SPM firmware loaded (SIP 0xC200021B); no DIS/MFG/ISP/VEN/CAM/
 *           VPU/VDE MTCMOS on (stock subsys_if_on BUG()s; here: refuse)
 *   STEP_GIC:  mask every SPI except the SPM IRQ0 (SPI 187)
 *   STEP_SSPM: SSPM IPI_ID_SPM_SUSPEND, cmd SPM_SUSPEND (polling)
 *   SIP SPM_SUSPEND_ARGS(pcm_flags, pcm_flags1, timer); ATF arms the 30 s
 *           PCM watchdog itself
 *   STEP_UART: UART0 sleep request + ack
 *   PSCI CPU_SUSPEND 0x01010005
 *   then the reverse: UART wake, wake status, SSPM SPM_RESUME, GIC restore.
 * Not done: CIRQ (stock clones edge IRQs into it so they are not lost in
 * sleep; no CIRQ node in this DT), DLPT/IMIX PMIC sampling.
 *
 * Nothing changes until "enable" is written (1 = replace the suspend
 * ops; reboot to go back). keep_wdt re-enables the TOPRGU watchdog for
 * the sleep (mtk_wdt_suspend stopped it) as a second escape while the
 * path is tested. Does nothing on other machines.
 */
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/psci.h>
#include <linux/suspend.h>

#define SIP_SPM_SUSPEND_ARGS	0xC200021A
#define SIP_SPM_FW_STATUS	0xC200021B
#define STOCK_SUSPEND_STATE	0x01010005	/* stock DT idle state "SUSPEND" */

#define TOPRGU_BASE		0x10007000
#define WDT_MODE_KEY		0x22000000
#define WDT_MODE_EN		BIT(0)
#define WDT_RST_RELOAD		0x1971

#define SPM_BASE		0x10006000
#define PWR_STATUS		0x180
#define PWR_STATUS_2ND		0x184
/* stock subsys_if_on() BUG()s on these (clk-mt6771-pg.c): DIS, MFG,
 * ISP, MFG_CORE0, MFG_CORE1, VEN, MFG_2D, MFG_ASYNC, CAM, VPU*, VDE */
#define SUBSYS_MUST_BE_OFF	(BIT(3) | BIT(4) | BIT(5) | BIT(7) | BIT(20) | \
				 BIT(21) | BIT(22) | BIT(23) | GENMASK(29, 25) | BIT(31))

#define GICD_BASE		0x0c000000
#define GICD_TYPER		0x004
#define GICD_ISENABLER		0x100
#define GICD_ICENABLER		0x180
#define SPM_IRQ0_INTID		(32 + 187)	/* stock DT sleep@10006000 */
#define GIC_MAX_REGS		32

#define SSPM_MBOX1_DATA		0x10460000	/* stock DT sspm mbox1_base */
#define SSPM_MBOX1_CTRL		0x10461000	/* IN_IRQ +0, OUT_IRQ +4 */
#define SSPM_SLOT		2		/* IPI_ID_SPM_SUSPEND */
#define SSPM_LEN		8		/* SPM_D_LEN */
#define SSPM_IRQ		BIT(6 - 5)	/* id 6 - mbox1 first id 5 */
#define SPM_CMD_SUSPEND		0
#define SPM_CMD_RESUME		1

#define UART0_BASE		0x11002000
#define UART_SLEEP_REQ		(0x2d << 2)
#define UART_SLEEP_ACK		(0x2e << 2)

#define STEP_GIC		BIT(0)
#define STEP_SSPM		BIT(1)
#define STEP_UART		BIT(2)

static uint flags = 0x80098;	/* stock slp_spm_flags */
module_param(flags, uint, 0644);
static uint flags1 = 0x300;	/* stock __sync_big_buck_ctrl_pcm_flag: BIG_BUCK_OFF|ON */
module_param(flags1, uint, 0644);
static uint wake_s = 10;	/* SPM timer wake */
module_param(wake_s, uint, 0644);
static uint steps = STEP_GIC | STEP_SSPM | STEP_UART;
module_param(steps, uint, 0644);
static bool keep_wdt = true;
module_param(keep_wdt, bool, 0644);
static uint entered, returned;
module_param(entered, uint, 0444);
module_param(returned, uint, 0444);
static int last_ret;
module_param(last_ret, int, 0444);

static void __iomem *rgu, *spm, *gicd, *mbox, *mbox_ctrl, *uart;
static u32 gic_saved[GIC_MAX_REGS];
static int gic_regs;

static void gic_mask_for_sleep(void)
{
	int i;

	for (i = 1; i < gic_regs; i++) {	/* SPIs only; SGI/PPI untouched */
		gic_saved[i] = readl(gicd + GICD_ISENABLER + 4 * i);
		writel(~0U, gicd + GICD_ICENABLER + 4 * i);
	}
	writel(BIT(SPM_IRQ0_INTID % 32),
	       gicd + GICD_ISENABLER + 4 * (SPM_IRQ0_INTID / 32));
}

static void gic_restore(void)
{
	int i;

	for (i = 1; i < gic_regs; i++) {
		writel(~0U, gicd + GICD_ICENABLER + 4 * i);
		writel(gic_saved[i], gicd + GICD_ISENABLER + 4 * i);
	}
}

static int sspm_send(u32 cmd, u32 *ack)
{
	u32 v;
	int i, ret;

	if (readl(mbox_ctrl + 4) & SSPM_IRQ)
		return -EBUSY;
	writel(cmd, mbox + 4 * SSPM_SLOT);
	for (i = 1; i < SSPM_LEN; i++)
		writel(0, mbox + 4 * (SSPM_SLOT + i));
	writel(SSPM_IRQ, mbox_ctrl);
	ret = readl_poll_timeout_atomic(mbox_ctrl + 4, v, v & SSPM_IRQ, 1, 500000);
	if (ret)
		return ret;
	writel(SSPM_IRQ, mbox_ctrl + 4);
	*ack = readl(mbox + 4 * SSPM_SLOT);
	return 0;
}

static int uart_sleep(bool sleep)
{
	u32 req = readl(uart + UART_SLEEP_REQ);
	int i;

	writel(sleep ? req | 1 : req & ~1, uart + UART_SLEEP_REQ);
	for (i = 0; i < 50; i++) {	/* stock MTK_UART_WAIT_ACK_TIMES x 10 us */
		if (!!(readl(uart + UART_SLEEP_ACK) & 1) == sleep)
			return 0;
		udelay(10);
	}
	writel(req, uart + UART_SLEEP_REQ);
	return -EBUSY;
}

static int cosmo_spm_enter(suspend_state_t state)
{
	struct arm_smccc_res res;
	u32 mode = readl(rgu), pwr, sus_ack = ~0, res_ack = ~0;
	int ret, sus_ret = 1, res_ret = 1, uart_ret = 1;

	arm_smccc_smc(SIP_SPM_FW_STATUS, 0, 0, 0, 0, 0, 0, 0, &res);
	pwr = readl(spm + PWR_STATUS) & readl(spm + PWR_STATUS_2ND);
	if (res.a0 != 1 || (pwr & SUBSYS_MUST_BE_OFF)) {
		pr_err("cosmo-spm-suspend: refused: SPM firmware %lu, PWR_STATUS 0x%08x\n",
		       res.a0, pwr);
		return -EPERM;
	}
	if (keep_wdt) {
		writel(WDT_RST_RELOAD, rgu + 0x08);
		writel(WDT_MODE_KEY | ((mode | WDT_MODE_EN) & 0xffff), rgu);
	}
	if (steps & STEP_GIC)
		gic_mask_for_sleep();
	if (steps & STEP_SSPM)
		sus_ret = sspm_send(SPM_CMD_SUSPEND, &sus_ack);
	arm_smccc_smc(SIP_SPM_SUSPEND_ARGS, flags, flags1, wake_s * 32768,
		      0, 0, 0, 0, &res);
	if (steps & STEP_UART)
		uart_ret = uart_sleep(true);
	entered++;
	if (uart_ret >= 0)
		ret = psci_cpu_suspend_enter(STOCK_SUSPEND_STATE);
	else
		ret = -EBUSY;	/* stock: WR_UART_BUSY, no WFI */
	returned++;
	last_ret = ret;
	if ((steps & STEP_UART) && uart_ret == 0)
		uart_sleep(false);
	pr_info("cosmo-spm-suspend: CPU_SUSPEND %d; R12 0x%08x R13 0x%08x WAKEUP_STA 0x%08x EXT 0x%08x FSM 0x%08x SW_DEBUG 0x%08x\n",
		ret, readl(spm + 0x130), readl(spm + 0x134), readl(spm + 0x15c),
		readl(spm + 0x160), readl(spm + 0x178), readl(spm + 0x604));
	if (steps & STEP_SSPM)
		res_ret = sspm_send(SPM_CMD_RESUME, &res_ack);
	if (steps & STEP_GIC)
		gic_restore();
	if (keep_wdt)
		writel(WDT_MODE_KEY | (mode & 0xffff), rgu);
	pr_info("cosmo-spm-suspend: steps 0x%x flags 0x%x/0x%x; SSPM suspend %d/0x%x resume %d/0x%x; UART %d\n",
		steps, flags, flags1, sus_ret, sus_ack, res_ret, res_ack, uart_ret);
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
	if (!uart)
		return -ENODEV;
	if (on && !enabled) {
		suspend_set_ops(&cosmo_spm_ops);
		enabled = true;
		pr_info("cosmo-spm-suspend: stock suspend path enabled (flags 0x%x/0x%x, wake %u s, steps 0x%x, keep_wdt %d)\n",
			flags, flags1, wake_s, steps, keep_wdt);
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
	gicd = ioremap(GICD_BASE, 0x1000);
	mbox = ioremap(SSPM_MBOX1_DATA, 0x100);
	mbox_ctrl = ioremap(SSPM_MBOX1_CTRL, 8);
	uart = ioremap(UART0_BASE, 0x100);
	if (!rgu || !spm || !gicd || !mbox || !mbox_ctrl || !uart) {
		uart = NULL;
		return -ENOMEM;
	}
	gic_regs = min_t(int, ((readl(gicd + GICD_TYPER) & 0x1f) + 1), GIC_MAX_REGS);
	pr_info("cosmo-spm-suspend: ready (write 1 to enable); GIC %d enable regs, SSPM slot tag 0x%08x\n",
		gic_regs, readl(mbox + 4 * SSPM_SLOT));
	return 0;
}
late_initcall(cosmo_spm_suspend_init);
