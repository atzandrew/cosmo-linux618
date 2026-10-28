// SPDX-License-Identifier: GPL-2.0
/*
 * Read-only: SPM firmware state, SPM PMIC command table, DVFSRC and TOPRGU
 * registers that stock Gemian 4.4 sets up at boot through spm_module_init ->
 * spm_vcorefs_init (VcoreFS DVFS start, dvfsrc_init, mtk_rgu_cfg_dvfsrc).
 * Compared against stock values from this unit's Gemian dmesg.
 *
 * Offsets: stock mtk_spm_reg_mt6771.h, mtk_dvfsrc_reg_mt6771.h,
 * mtk_sleep_reg_md_reg_mt6771.h, wdt_v2/mtk_wdt.h. Bases: stock mt6771.dts.
 *
 * Only readl(); no writes, no SMC calls. Each group can be turned off.
 * Planned runs: (1) dvfsrc=0 (spm, cmd, rgu); (2) spm=0 cmd=0 rgu=0 dvfsrc=1.
 * Run under systemd-inhibit --what=sleep: enable=N on cosmo_spm_suspend
 * only switches the stock path off, it does not block system suspend.
 * DVFSRC needs infra_dvfsrc (INFRACFG_AO 0x10001094 bit 7 clear = clock on);
 * the module checks that bit first and skips DVFSRC if the clock is off.
 * The md group (0x1000f000) has never been read on mainline: off by default.
 * Never stays loaded (init returns -EAGAIN).
 */
#include <linux/module.h>
#include <linux/io.h>

static bool spm = true;
module_param(spm, bool, 0444);
static bool cmd = true;
module_param(cmd, bool, 0444);
static bool dvfsrc = true;
module_param(dvfsrc, bool, 0444);
static bool rgu = true;
module_param(rgu, bool, 0444);
static bool md;
module_param(md, bool, 0444);
/* Log each address before reading it, so a bus hang can be narrowed down
 * (last lines may still be lost on the way out). */
static bool trace = true;
module_param(trace, bool, 0444);

#define INFRACFG_AO_BASE	0x10001000
#define INFRA1_CG_STA		0x94
#define DVFSRC_CG_BIT		7
#define SPM_BASE		0x10006000
#define TOPRGU_BASE		0x10007000
#define SLEEP_REG_MD_BASE	0x1000f000
#define DVFSRC_BASE		0x10012000

struct reg {
	u16 off;
	const char *name;
};

static const struct reg spm_regs[] = {
	{ 0x000, "POWERON_CONFIG_EN" },
	{ 0x018, "PCM_CON0" },
	{ 0x01c, "PCM_CON1" },
	{ 0x028, "PCM_REG_DATA_INI" },
	{ 0x02c, "PCM_PWR_IO_EN" },
	{ 0x0b8, "SPM_SRC_REQ" },
	{ 0x0bc, "SPM_SRC_MASK" },
	{ 0x0c0, "SPM_SRC2_MASK" },
	{ 0x134, "PCM_REG13_DATA" },
	{ 0x13c, "PCM_REG15_DATA" },	/* stock: != 0 when the PCM program runs */
	{ 0x178, "PCM_FSM_STA" },
	{ 0x1c8, "SPM_DVFS_STA" },
	{ 0x400, "SPM_DVFS_CON" },
	{ 0x43c, "MD2SPM_DVFS_CON" },
	{ 0x600, "SPM_SW_FLAG" },
	{ 0x604, "SPM_SW_DEBUG" },
	{ 0x608, "SPM_SW_RSV_0" },
	{ 0x60c, "SPM_SW_RSV_1" },
	{ 0x610, "SPM_SW_RSV_2" },
	{ 0x614, "SPM_SW_RSV_3" },
	{ 0x618, "SPM_SW_RSV_4" },
	{ 0x61c, "SPM_SW_RSV_5" },	/* stock after DVFS start: 0x80000001 */
	{ 0x690, "DVFSRC_EVENT_MASK_CON" },
	{ 0x694, "DVFSRC_EVENT_FORCE_ON" },
	{ 0x698, "DVFSRC_EVENT_SEL" },
	{ 0x69c, "SPM_DVFS_EVENT_STA" },
	{ 0x6a4, "SPM_DVFS_LEVEL" },
	{ 0x6b0, "SPM_DFS_LEVEL" },
	{ 0x6b4, "SPM_DVS_LEVEL" },
	{ 0x6b8, "SPM_DVFS_MISC" },
};

/* SPM_DVFS_CMD0..15: PMIC wrap command table (addr << 16 | wdata). */
#define SPM_DVFS_CMD0		0x710
#define SPM_DVFS_CMD_NUM	16

static const struct reg dvfsrc_regs[] = {
	{ 0x000, "BASIC_CONTROL" },	/* stock: 0x017b */
	{ 0x004, "SW_REQ" },
	{ 0x008, "SW_REQ2" },
	{ 0x00c, "EMI_REQUEST" },	/* stock: 0x00290209 */
	{ 0x010, "EMI_REQUEST2" },
	{ 0x014, "EMI_REQUEST3" },
	{ 0x024, "EMI_QOS0" },		/* stock (3733 branch): 0x32 */
	{ 0x028, "EMI_QOS1" },		/* 0x66 */
	{ 0x030, "EMI_MD2SPM0" },	/* 0x80f8 */
	{ 0x034, "EMI_MD2SPM1" },	/* 0x0 */
	{ 0x048, "VCORE_REQUEST" },	/* 0x00150000 */
	{ 0x04c, "VCORE_REQUEST2" },
	{ 0x068, "VCORE_MD2SPM0" },	/* 0x80c0 */
	{ 0x080, "MD_REQUEST" },
	{ 0x098, "INT" },
	{ 0x09c, "INT_EN" },		/* 0x2 */
	{ 0x0d8, "TIMEOUT_NEXTREQ" },	/* 0x13 */
	{ 0x0dc, "LEVEL" },		/* stock at boot: 0x10000 */
	{ 0x0e0, "LEVEL_LABEL_0_1" },	/* 0x00100000 */
	{ 0x0e4, "LEVEL_LABEL_2_3" },	/* 0x00210011 */
	{ 0x0e8, "LEVEL_LABEL_4_5" },	/* 0x01100100 */
	{ 0x0ec, "LEVEL_LABEL_6_7" },	/* 0x01210111 */
	{ 0x0f0, "LEVEL_LABEL_8_9" },	/* 0x02100200 */
	{ 0x0f4, "LEVEL_LABEL_10_11" },	/* 0x02210211 */
	{ 0x0f8, "LEVEL_LABEL_12_13" },	/* 0x03210321 */
	{ 0x0fc, "LEVEL_LABEL_14_15" },	/* 0x03210321 */
	{ 0x180, "QOS_EN" },		/* 0x407f or 0 */
	{ 0x300, "FORCE" },		/* 0x00400000 */
	{ 0x304, "SEC_SW_REQ" },
	{ 0x600, "RSRV_0" },
	{ 0x604, "RSRV_1" },		/* 0x1c */
};

static const struct reg rgu_regs[] = {
	{ 0x030, "REQ_MODE" },
	{ 0x034, "REQ_IRQ_EN" },
	{ 0x040, "DEBUG_CTL" },
	{ 0x044, "LATCH_CTL" },		/* stock after rgu_cfg_dvfsrc: 0x3f71 */
	{ 0x0a0, "DEBUG_CTL2" },	/* stock: 0x2ff */
};

static const struct reg md_regs[] = {
	{ 0x000, "SPM_DVFS_CMD16" },
	{ 0x004, "SPM_DVFS_CMD17" },
	{ 0x008, "SPM_DVFS_CMD18" },
	{ 0x00c, "SPM_DVFS_CMD19" },
	{ 0x010, "SPM_DVFS_CMD20" },
	{ 0x014, "SPM_DVFS_CMD21" },
	{ 0x018, "SPM_DVFS_CMD22" },
	{ 0x01c, "SPM_DVFS_CMD23" },
};

static void dump(const char *grp, phys_addr_t base, const struct reg *r, int n)
{
	void __iomem *b = ioremap(base, 0x1000);
	int i;

	if (!b) {
		pr_err("cosmo_vcfs: %s: ioremap 0x%pa failed\n", grp, &base);
		return;
	}
	pr_info("cosmo_vcfs: %s: start (base 0x%pa)\n", grp, &base);
	for (i = 0; i < n; i++) {
		if (trace)
			pr_info("cosmo_vcfs: %s reading 0x%03x\n", grp, r[i].off);
		pr_info("cosmo_vcfs: %s %-22s 0x%03x = 0x%08x\n",
			grp, r[i].name, r[i].off, readl(b + r[i].off));
	}
	pr_info("cosmo_vcfs: %s: done\n", grp);
	iounmap(b);
}

static void dump_cmd(void)
{
	void __iomem *b = ioremap(SPM_BASE, 0x1000);
	int i;
	u32 v;

	if (!b)
		return;
	for (i = 0; i < SPM_DVFS_CMD_NUM; i++) {
		if (trace)
			pr_info("cosmo_vcfs: cmd reading 0x%03x\n", SPM_DVFS_CMD0 + 4 * i);
		v = readl(b + SPM_DVFS_CMD0 + 4 * i);
		pr_info("cosmo_vcfs: cmd SPM_DVFS_CMD%-2d 0x%03x = 0x%08x (pmic 0x%04x <- 0x%04x)\n",
			i, SPM_DVFS_CMD0 + 4 * i, v, v >> 16, v & 0xffff);
	}
	iounmap(b);
}

static bool dvfsrc_clock_on(void)
{
	void __iomem *b = ioremap(INFRACFG_AO_BASE, 0x1000);
	u32 sta;

	if (!b)
		return false;
	if (trace)
		pr_info("cosmo_vcfs: infra reading 0x094\n");
	sta = readl(b + INFRA1_CG_STA);
	iounmap(b);
	pr_info("cosmo_vcfs: infra INFRA1_CG_STA 0x094 = 0x%08x (bit %d %s)\n",
		sta, DVFSRC_CG_BIT,
		sta & BIT(DVFSRC_CG_BIT) ? "set: dvfsrc clock OFF" : "clear: dvfsrc clock on");
	return !(sta & BIT(DVFSRC_CG_BIT));
}

static int __init cosmo_vcfs_init(void)
{
	if (spm)
		dump("spm", SPM_BASE, spm_regs, ARRAY_SIZE(spm_regs));
	if (cmd)
		dump_cmd();
	if (dvfsrc) {
		if (dvfsrc_clock_on())
			dump("dvfsrc", DVFSRC_BASE, dvfsrc_regs, ARRAY_SIZE(dvfsrc_regs));
		else
			pr_info("cosmo_vcfs: dvfsrc skipped (clock off)\n");
	}
	if (rgu)
		dump("rgu", TOPRGU_BASE, rgu_regs, ARRAY_SIZE(rgu_regs));
	if (md)
		dump("md", SLEEP_REG_MD_BASE, md_regs, ARRAY_SIZE(md_regs));
	return -EAGAIN; /* report only; never stays loaded */
}
module_init(cosmo_vcfs_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo read-only VcoreFS/DVFSRC/SPM boot-state dump");
