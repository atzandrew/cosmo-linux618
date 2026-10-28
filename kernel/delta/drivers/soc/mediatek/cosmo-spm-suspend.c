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
 * Boot-time setup as stock (boot_init, default on; cosmo_spm_suspend.
 * boot_init=0 on the command line skips it):
 *   - SPM IRQ0 (SPI 187) mapped through sysirq as LEVEL_LOW as in the
 *     stock DT, handler as stock spm_irq0_handler (SIP SPM_IRQ0_HANDLER).
 *     Without the polarity inverter the idle line reads as pending and
 *     ATF refuses CPU_SUSPEND (-95).
 *   - Start the SPM firmware and DVFSRC as stock spm_module_init ->
 *     spm_vcorefs_init in its VCORE-DVFS-disabled branch: SIP SPM_ARGS
 *     (SPMFW_IDX), VCOREFS CMD_0 (boot opp 0), VCOREFS CMD_1 (flag 0x8418:
 *     stock 0x8400 | DIS_VCORE_DVS | DIS_VCORE_DFS, so Vcore and the DRAM
 *     rate stay where they are), then stock dvfsrc_init (LP4X 3733
 *     branch). Without the running program the SPM sleep never wakes.
 *     Left out: CMD_3 (Vcore DVS table), SSPM QoS, mtk_rgu_cfg_dvfsrc.
 * "enable" refuses unless both succeeded.
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
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/psci.h>
#include <linux/suspend.h>

#define SIP_SPM_SUSPEND_ARGS	0xC200021A
#define SIP_SPM_FW_STATUS	0xC200021B
#define SIP_SPM_IRQ0_HANDLER	0xC200021C
#define SIP_SPM_VCOREFS_ARGS	0xC2000220
#define SIP_SPM_ARGS		0xC2000228
#define SPM_ARGS_SPMFW_IDX	0
#define VCOREFS_CMD_0		0
#define VCOREFS_CMD_1		1
#define SPMFW_IDX		0	/* SPMFW_LP4X_2CH_3733 (this unit: LPDDR4 3600) */
#define EXT_BUCK		0
#define BOOT_UP_OPP		0
#define VCOREFS_FLAG		0x8418	/* stock 0x8400 | DIS_VCORE_DVS | DIS_VCORE_DFS */

#define INFRACFG_AO_BASE	0x10001000
#define INFRA1_CG_STA		0x94
#define DVFSRC_CG		BIT(7)
#define DVFSRC_BASE		0x10012000
#define DVFSRC_LEVEL		0x0dc
#define PCM_REG15_DATA		0x13c
#define SPM_SW_RSV_5		0x61c
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
static bool boot_init = true;
module_param(boot_init, bool, 0444);
static bool boot_ready;
module_param(boot_ready, bool, 0444);
static uint irq0_count;
module_param(irq0_count, uint, 0444);
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

/*
 * Wake status as stock __spm_get_wakeup_status + __spm_output_wake_reason
 * (spm_v4/mtk_spm_internal.c): the firmware's backups, not live PCM_REG12.
 */
#define PCM_REG_DATA_INI	0x028	/* assert PC, 0 = no assert */
#define SPM_SRC_REQ		0x0b8
#define PCM_REG13_DATA		0x134
#define PCM_REG12_EXT_DATA	0x144
#define SPM_IRQ_STA		0x158
#define SPM_WAKEUP_STA		0x15c
#define SPM_WAKEUP_EXT_STA	0x160
#define SUBSYS_IDLE_STA		0x170
#define SRC_REQ_STA		0x17c
#define SPM_BSI_D0_SR		0x41c	/* backup of SPM_WAKEUP_MISC */
#define SPM_BSI_D1_SR		0x420	/* backup of PCM_TIMER_OUT */
#define SPM_BSI_D2_SR		0x424	/* PCM_EVENT_REG_STA */
#define SPM_SW_FLAG		0x600
#define SPM_SW_DEBUG		0x604
#define SPM_SW_RSV_0		0x608	/* backup of PCM_REG12_DATA */
#define SPM_SW_RSV_2		0x610
#define WDT_LATCH_SPARE0_FIX	0x780
#define WAKE_SRC_R12_PCM_TIMER	BIT(0)
#define WAKE_MISC_TWAM		BIT(18)
#define WAKE_MISC_PCM_TIMER	BIT(19)
#define WAKE_MISC_CPU_WAKE	BIT(20)

static void spm_print_wake_status(int ret)
{
	u32 assert_pc = readl(spm + PCM_REG_DATA_INI);
	u32 r12 = readl(spm + SPM_SW_RSV_0);
	u32 misc = readl(spm + SPM_BSI_D0_SR);
	const char *why = "unknown";

	if (assert_pc)
		why = "PCM ASSERT";
	else if ((r12 & WAKE_SRC_R12_PCM_TIMER) && (misc & WAKE_MISC_PCM_TIMER))
		why = (r12 & ~WAKE_SRC_R12_PCM_TIMER) ? "PCM_TIMER + other R12 bits" : "PCM_TIMER";
	else if (r12)
		why = "wake source (R12 bits)";

	pr_info("cosmo-spm-suspend: CPU_SUSPEND %d; wake up by %s; assert_pc 0x%x timer_out %u r13 0x%x debug_flag 0x%x 0x%x\n",
		ret, why, assert_pc, readl(spm + SPM_BSI_D1_SR),
		readl(spm + PCM_REG13_DATA), readl(spm + SPM_SW_DEBUG),
		readl(spm + WDT_LATCH_SPARE0_FIX));
	pr_info("cosmo-spm-suspend: r12 0x%x r12_ext 0x%x raw_sta 0x%x idle_sta 0x%x req_sta 0x%x event_reg 0x%x isr 0x%x\n",
		r12, readl(spm + PCM_REG12_EXT_DATA), readl(spm + SPM_WAKEUP_STA),
		readl(spm + SUBSYS_IDLE_STA), readl(spm + SRC_REQ_STA),
		readl(spm + SPM_BSI_D2_SR), readl(spm + SPM_IRQ_STA));
	pr_info("cosmo-spm-suspend: raw_ext_sta 0x%x wake_misc 0x%x pcm_flag 0x%x 0x%x req 0x%x\n",
		readl(spm + SPM_WAKEUP_EXT_STA), misc, readl(spm + SPM_SW_FLAG),
		readl(spm + SPM_SW_RSV_2), readl(spm + SPM_SRC_REQ));
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
	spm_print_wake_status(ret);
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

/* stock dvfsrc_init order, LP4X_2CH_3733 / LP4_2CH_2400 branch */
static const struct { u16 off; u32 val; } dvfsrc_seq[] __initconst = {
	{ 0x0e0, 0x00100000 }, { 0x0e4, 0x00210011 },	/* LEVEL_LABEL_0..3 */
	{ 0x0e8, 0x01100100 }, { 0x0ec, 0x01210111 },	/* 4..7 */
	{ 0x0f0, 0x02100200 }, { 0x0f4, 0x02210211 },	/* 8..11 */
	{ 0x0f8, 0x03210321 }, { 0x0fc, 0x03210321 },	/* 12..15 */
	{ 0x024, 0x00000032 },	/* EMI_QOS0 */
	{ 0x028, 0x00000066 },	/* EMI_QOS1 */
	{ 0x030, 0x000080f8 },	/* EMI_MD2SPM0 */
	{ 0x034, 0x00000000 },	/* EMI_MD2SPM1 */
	{ 0x068, 0x000080c0 },	/* VCORE_MD2SPM0 */
	{ 0x604, 0x0000001c },	/* RSRV_1 */
	{ 0x0d8, 0x00000013 },	/* TIMEOUT_NEXTREQ */
	{ 0x09c, 0x00000002 },	/* INT_EN */
	{ 0x00c, 0x00290209 },	/* EMI_REQUEST */
	{ 0x010, 0x00000000 },	/* EMI_REQUEST2 */
	{ 0x048, 0x00150000 },	/* VCORE_REQUEST */
	{ 0x180, 0x00000000 },	/* QOS_EN (stock !CONFIG_MTK_QOS_SUPPORT) */
	{ 0x300, 0x00400000 },	/* FORCE */
	{ 0x000, 0x0000c07b },	/* BASIC_CONTROL */
	{ 0x000, 0x0000017b },	/* BASIC_CONTROL */
};

static unsigned long sip(unsigned long id, unsigned long a1,
			 unsigned long a2, unsigned long a3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(id, a1, a2, a3, 0, 0, 0, 0, &res);
	return res.a0;
}

static irqreturn_t spm_irq0(int irq, void *data)
{
	sip(SIP_SPM_IRQ0_HANDLER, readl(spm + SPM_IRQ_STA), 0, 0);
	irq0_count++;
	return IRQ_HANDLED;
}

static int __init spm_irq0_init(void)
{
	struct of_phandle_args a = {
		.args_count = 3, .args = { 0, 187, IRQ_TYPE_LEVEL_LOW },
	};
	int virq, ret;

	a.np = of_find_compatible_node(NULL, NULL, "mediatek,mt6577-sysirq");
	if (!a.np)
		return -ENODEV;
	virq = irq_create_of_mapping(&a);
	of_node_put(a.np);
	if (!virq)
		return -EINVAL;
	ret = request_irq(virq, spm_irq0, 0, "spm-irq0", NULL);
	if (ret)
		irq_dispose_mapping(virq);
	return ret;
}

static int __init spm_dvfs_start(void)
{
	void __iomem *infra, *dvfsrc;
	unsigned long r0, r1, r2;
	int i, bad = 0, ret = 0;

	if (readl(spm + PCM_REG15_DATA)) {
		pr_err("cosmo-spm-suspend: PCM program already running\n");
		return -EBUSY;
	}
	infra = ioremap(INFRACFG_AO_BASE, 0x100);
	dvfsrc = ioremap(DVFSRC_BASE, 0x1000);
	if (!infra || !dvfsrc) {
		ret = -ENOMEM;
		goto out;
	}
	if (readl(infra + INFRA1_CG_STA) & DVFSRC_CG) {
		pr_err("cosmo-spm-suspend: DVFSRC clock off\n");
		ret = -ENODEV;
		goto out;
	}
	r0 = sip(SIP_SPM_ARGS, SPM_ARGS_SPMFW_IDX, SPMFW_IDX, EXT_BUCK);
	r1 = sip(SIP_SPM_VCOREFS_ARGS, VCOREFS_CMD_0, BOOT_UP_OPP, 0);
	r2 = sip(SIP_SPM_VCOREFS_ARGS, VCOREFS_CMD_1, VCOREFS_FLAG, 0);
	for (i = 0; i < ARRAY_SIZE(dvfsrc_seq); i++) {
		writel(dvfsrc_seq[i].val, dvfsrc + dvfsrc_seq[i].off);
		if (readl(dvfsrc + dvfsrc_seq[i].off) != dvfsrc_seq[i].val)
			bad++;
	}
	pr_info("cosmo-spm-suspend: SPM program started: SIP %lu/%lu/%lu, flag 0x%x, PCM_REG15 0x%08x SW_RSV_5 0x%08x, DVFSRC LEVEL 0x%08x, %d readback mismatches\n",
		r0, r1, r2, VCOREFS_FLAG, readl(spm + PCM_REG15_DATA),
		readl(spm + SPM_SW_RSV_5), readl(dvfsrc + DVFSRC_LEVEL), bad);
	if (r0 || r1 || r2 || !readl(spm + PCM_REG15_DATA))
		ret = -EIO;
out:
	if (dvfsrc)
		iounmap(dvfsrc);
	if (infra)
		iounmap(infra);
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
	if (on && !boot_ready) {
		pr_err("cosmo-spm-suspend: boot setup not done; refusing enable\n");
		return -ENODEV;
	}
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
	if (boot_init) {
		int ret = spm_irq0_init();

		if (ret)
			pr_err("cosmo-spm-suspend: SPM IRQ0 setup failed: %d\n", ret);
		else if (sip(SIP_SPM_FW_STATUS, 0, 0, 0) != 1)
			pr_err("cosmo-spm-suspend: SPM firmware not loaded\n");
		else if (!spm_dvfs_start())
			boot_ready = true;
	}
	gic_regs = min_t(int, ((readl(gicd + GICD_TYPER) & 0x1f) + 1), GIC_MAX_REGS);
	pr_info("cosmo-spm-suspend: ready (boot setup %s; write 1 to enable); GIC %d enable regs, SSPM slot tag 0x%08x\n",
		boot_ready ? "done" : "NOT done", gic_regs, readl(mbox + 4 * SSPM_SLOT));
	return 0;
}
late_initcall(cosmo_spm_suspend_init);
