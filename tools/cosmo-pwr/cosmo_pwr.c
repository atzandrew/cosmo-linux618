// SPDX-License-Identifier: GPL-2.0
/* Read-only: SPM PWR_STATUS/PWR_STATUS_2ND (MTCMOS state, bit names from
 * stock clk-mt6771-pg.c) and SIP SPM_FIRMWARE_STATUS (0xC200021B). */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>

#define SPM_BASE 0x10006000
static const char * const names[32] = {
	[0] = "MD1", [1] = "CONN", [2] = "DPY", [3] = "DIS", [4] = "MFG",
	[5] = "ISP", [6] = "IFR", [7] = "MFG_CORE0", [8] = "MP0_CPUTOP",
	[9] = "MP0_CPU0", [10] = "MP0_CPU1", [11] = "MP0_CPU2", [12] = "MP0_CPU3",
	[20] = "MFG_CORE1", [21] = "VEN", [22] = "MFG_2D", [23] = "MFG_ASYNC",
	[24] = "AUDIO", [25] = "CAM", [26] = "VPU_TOP", [27] = "VPU_CORE0",
	[28] = "VPU_CORE1", [29] = "VPU_CORE2", [31] = "VDE",
};

static int __init cosmo_pwr_init(void)
{
	struct arm_smccc_res res;
	void __iomem *b = ioremap(SPM_BASE, 0x1000);
	u32 s1, s2;
	int i;
	char buf[400];
	int n = 0;

	if (!b)
		return -ENOMEM;
	s1 = readl(b + 0x180);
	s2 = readl(b + 0x184);
	for (i = 0; i < 32; i++)
		if ((s1 & s2 & BIT(i)) && n < sizeof(buf) - 16)
			n += scnprintf(buf + n, sizeof(buf) - n, " %s",
				       names[i] ? names[i] : "?");
	buf[n] = 0;
	pr_info("cosmo_pwr: PWR_STATUS 0x%08x 2ND 0x%08x on:%s\n", s1, s2, buf);
	pr_info("cosmo_pwr: DIS_PWR_CON 0x%08x MFG_PWR_CON 0x%08x CONN 0x%08x AUDIO 0x%08x\n",
		readl(b + 0x30c), readl(b + 0x338), readl(b + 0x32c), readl(b + 0x314));
	iounmap(b);
	arm_smccc_smc(0xC200021B, 0, 0, 0, 0, 0, 0, 0, &res);
	pr_info("cosmo_pwr: SPM_FIRMWARE_STATUS -> 0x%lx\n", res.a0);
	return -EAGAIN; /* report only; never stays loaded */
}
module_init(cosmo_pwr_init);
MODULE_LICENSE("GPL");
