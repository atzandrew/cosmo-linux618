// SPDX-License-Identifier: GPL-2.0
/*
 * Start the SPM allinone firmware the way stock Gemian 4.4 does at boot, in
 * stock's "VCORE DVFS disabled" branch (mtk_spm_vcorefs_mt6771.c
 * spm_vcorefs_init, VMODEM_VCORE_COBUCK = 1 in mtk_spm_vcore_dvfs_mt6771.h):
 *
 *   spm_module_init: SIP SPM_ARGS(SPMFW_IDX, __spm_get_dram_type(), ext_buck)
 *   spm_dvfsfw_init: SIP VCOREFS_ARGS(CMD_0, curr_opp, 0)
 *   spm_go_to_vcorefs: SIP VCOREFS_ARGS(CMD_1, flag, 0)
 *       flag = RUN_COMMON_SCENARIO | DIS_VCORE_DVS | DIS_VCORE_DFS
 *              (| DISABLE_MMSYS_DVFS, as in stock's 0x8400 on this unit)
 *   dvfsrc_init (LP4X 3733 / LP4 2400 branch; this unit: LPDDR4 3600, idx 0)
 *
 * With DVS and DFS disabled the SPM keeps Vcore and the DRAM rate where they
 * are. Left out on purpose: CMD_3 (Vcore DVS table; unused without DVS),
 * QOS_EN 0x407f / EMI_REQUEST3 (need the SSPM QoS setup; QOS_EN stays 0 as in
 * stock's !CONFIG_MTK_QOS_SUPPORT case), mtk_rgu_cfg_dvfsrc (TOPRGU change).
 *
 * Values: this unit's Gemian dmesg (__spmfw_idx 0x0, ext_buck 0x0, boot opp 0)
 * and stock dvfsrc_init. Does nothing unless go=1. steps bitmask:
 * 1 SPMFW_IDX, 2 CMD_0, 4 CMD_1, 8 DVFSRC. Refuses if the SPM firmware is not
 * loaded, the PCM program already runs (PCM_REG15 != 0) or the DVFSRC clock
 * is off. Never stays loaded (init returns -EAGAIN); the state stays until
 * reboot.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>

static bool go;
module_param(go, bool, 0444);
static uint steps = 0xf;
module_param(steps, uint, 0444);
static uint flag = 0x8418;
module_param(flag, uint, 0444);

#define SIP_SPM_FW_STATUS	0xC200021B
#define SIP_SPM_VCOREFS_ARGS	0xC2000220
#define SIP_SPM_ARGS		0xC2000228
#define SPM_ARGS_SPMFW_IDX	0
#define VCOREFS_SMC_CMD_0	0
#define VCOREFS_SMC_CMD_1	1
#define SPMFW_IDX		0	/* SPMFW_LP4X_2CH_3733 */
#define EXT_BUCK		0
#define BOOT_UP_OPP		0

#define SPM_FLAG_DIS_VCORE_DVS	(1U << 3)
#define SPM_FLAG_DIS_VCORE_DFS	(1U << 4)

#define INFRACFG_AO_BASE	0x10001000
#define INFRA1_CG_STA		0x94
#define DVFSRC_CG_BIT		7
#define SPM_BASE		0x10006000
#define PCM_REG15_DATA		0x13c
#define SPM_SW_RSV_5		0x61c
#define DVFSRC_BASE		0x10012000

struct wr {
	u16 off;
	u32 val;
	const char *name;
};

/* stock dvfsrc_init order, LP4X_2CH_3733 / LP4_2CH_2400 branch */
static const struct wr dvfsrc_seq[] = {
	{ 0x0e0, 0x00100000, "LEVEL_LABEL_0_1" },
	{ 0x0e4, 0x00210011, "LEVEL_LABEL_2_3" },
	{ 0x0e8, 0x01100100, "LEVEL_LABEL_4_5" },
	{ 0x0ec, 0x01210111, "LEVEL_LABEL_6_7" },
	{ 0x0f0, 0x02100200, "LEVEL_LABEL_8_9" },
	{ 0x0f4, 0x02210211, "LEVEL_LABEL_10_11" },
	{ 0x0f8, 0x03210321, "LEVEL_LABEL_12_13" },
	{ 0x0fc, 0x03210321, "LEVEL_LABEL_14_15" },
	{ 0x024, 0x00000032, "EMI_QOS0" },
	{ 0x028, 0x00000066, "EMI_QOS1" },
	{ 0x030, 0x000080f8, "EMI_MD2SPM0" },
	{ 0x034, 0x00000000, "EMI_MD2SPM1" },
	{ 0x068, 0x000080c0, "VCORE_MD2SPM0" },
	{ 0x604, 0x0000001c, "RSRV_1" },
	{ 0x0d8, 0x00000013, "TIMEOUT_NEXTREQ" },
	{ 0x09c, 0x00000002, "INT_EN" },
	{ 0x00c, 0x00290209, "EMI_REQUEST" },
	{ 0x010, 0x00000000, "EMI_REQUEST2" },
	{ 0x048, 0x00150000, "VCORE_REQUEST" },
	{ 0x180, 0x00000000, "QOS_EN" },
	{ 0x300, 0x00400000, "FORCE" },
	{ 0x000, 0x0000c07b, "BASIC_CONTROL" },
	{ 0x000, 0x0000017b, "BASIC_CONTROL" },
};

static unsigned long sip(unsigned long id, unsigned long a1,
			 unsigned long a2, unsigned long a3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(id, a1, a2, a3, 0, 0, 0, 0, &res);
	return res.a0;
}

static int __init cosmo_vcfs_start_init(void)
{
	void __iomem *infra, *spm, *dvfsrc;
	unsigned long r;
	int i, ret = -EAGAIN;

	pr_info("cosmo_vcfs_start: go=%d steps=0x%x flag=0x%x\n", go, steps, flag);
	if (!go) {
		pr_info("cosmo_vcfs_start: go=0, nothing done\n");
		return -EAGAIN;
	}
	if (!(flag & SPM_FLAG_DIS_VCORE_DVS) || !(flag & SPM_FLAG_DIS_VCORE_DFS)) {
		pr_err("cosmo_vcfs_start: flag 0x%x enables DVS/DFS; refused\n", flag);
		return -EINVAL;
	}

	infra = ioremap(INFRACFG_AO_BASE, 0x1000);
	spm = ioremap(SPM_BASE, 0x1000);
	dvfsrc = ioremap(DVFSRC_BASE, 0x1000);
	if (!infra || !spm || !dvfsrc) {
		ret = -ENOMEM;
		goto out;
	}

	r = sip(SIP_SPM_FW_STATUS, 0, 0, 0);
	pr_info("cosmo_vcfs_start: SPM firmware status %lu, PCM_REG15 0x%08x, SW_RSV_5 0x%08x\n",
		r, readl(spm + PCM_REG15_DATA), readl(spm + SPM_SW_RSV_5));
	if (r != 1) {
		pr_err("cosmo_vcfs_start: SPM firmware not loaded; refused\n");
		ret = -ENODEV;
		goto out;
	}
	if (readl(spm + PCM_REG15_DATA)) {
		pr_err("cosmo_vcfs_start: PCM program already running; refused\n");
		ret = -EBUSY;
		goto out;
	}
	if ((steps & 8) && (readl(infra + INFRA1_CG_STA) & BIT(DVFSRC_CG_BIT))) {
		pr_err("cosmo_vcfs_start: dvfsrc clock off; refused\n");
		ret = -ENODEV;
		goto out;
	}

	if (steps & 1) {
		r = sip(SIP_SPM_ARGS, SPM_ARGS_SPMFW_IDX, SPMFW_IDX, EXT_BUCK);
		pr_info("cosmo_vcfs_start: SPM_ARGS(SPMFW_IDX, %d, %d) -> 0x%lx\n",
			SPMFW_IDX, EXT_BUCK, r);
	}
	if (steps & 2) {
		r = sip(SIP_SPM_VCOREFS_ARGS, VCOREFS_SMC_CMD_0, BOOT_UP_OPP, 0);
		pr_info("cosmo_vcfs_start: VCOREFS(CMD_0, opp %d) -> 0x%lx\n",
			BOOT_UP_OPP, r);
	}
	if (steps & 4) {
		r = sip(SIP_SPM_VCOREFS_ARGS, VCOREFS_SMC_CMD_1, flag, 0);
		pr_info("cosmo_vcfs_start: VCOREFS(CMD_1, flag 0x%x) -> 0x%lx; PCM_REG15 0x%08x SW_RSV_5 0x%08x\n",
			flag, r, readl(spm + PCM_REG15_DATA), readl(spm + SPM_SW_RSV_5));
	}
	if (steps & 8) {
		for (i = 0; i < ARRAY_SIZE(dvfsrc_seq); i++) {
			writel(dvfsrc_seq[i].val, dvfsrc + dvfsrc_seq[i].off);
			pr_info("cosmo_vcfs_start: dvfsrc %-17s 0x%03x <- 0x%08x (now 0x%08x)\n",
				dvfsrc_seq[i].name, dvfsrc_seq[i].off, dvfsrc_seq[i].val,
				readl(dvfsrc + dvfsrc_seq[i].off));
		}
		pr_info("cosmo_vcfs_start: dvfsrc LEVEL 0x%08x\n", readl(dvfsrc + 0x0dc));
	}
	pr_info("cosmo_vcfs_start: done; PCM_REG15 0x%08x SW_RSV_5 0x%08x\n",
		readl(spm + PCM_REG15_DATA), readl(spm + SPM_SW_RSV_5));
out:
	if (dvfsrc)
		iounmap(dvfsrc);
	if (spm)
		iounmap(spm);
	if (infra)
		iounmap(infra);
	return ret;
}
module_init(cosmo_vcfs_start_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo: start SPM firmware + DVFSRC as stock (DVS/DFS disabled)");
