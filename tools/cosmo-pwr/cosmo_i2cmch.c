// SPDX-License-Identifier: GPL-2.0
/*
 * Test: restore the MT6771 multi-channel I2C secure settings the way stock
 * i2c-mtk.c mt_i2c_resume_noirq() does for buses with ch_offset_default
 * (i2c4 on the Cosmo: AW9523 keyboard). With the bus clocks on:
 *   SIP I2C_SEC_WRITE(id, V2_OFFSET_ROLLBACK 0xf98, 0)       rollback off
 *   SIP I2C_SEC_WRITE(id, V2_OFFSET_MULTI_DMA 0xf8c, 2)      shadow reg mode
 * Mainline i2c-mt65xx never makes these calls; LK sets them at boot.
 * Does nothing unless go=1. Never stays loaded (-EAGAIN).
 */
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>

#define SIP_I2C_SEC_WRITE	0xC20002A0
#define V2_OFFSET_MULTI_DMA	0xf8c
#define V2_OFFSET_ROLLBACK	0xf98
#define I2C_SHADOW_REG_MODE	0x2

static bool go;
module_param(go, bool, 0444);
static uint bus = 4;	/* stock DT id and mainline adapter number */
module_param(bus, uint, 0444);

static int __init cosmo_i2cmch_init(void)
{
	struct i2c_adapter *adap;
	struct arm_smccc_res r1, r2;
	struct clk *clk[3] = { };
	int i;

	if (!go)
		return -EAGAIN;
	adap = i2c_get_adapter(bus);
	if (!adap || !adap->dev.parent || !adap->dev.parent->of_node)
		return -ENODEV;
	for (i = 0; i < 3; i++) {
		clk[i] = of_clk_get(adap->dev.parent->of_node, i);
		if (IS_ERR(clk[i]) || clk_prepare_enable(clk[i])) {
			if (!IS_ERR(clk[i]))
				clk_put(clk[i]);
			clk[i] = NULL;
		}
	}
	arm_smccc_smc(SIP_I2C_SEC_WRITE, bus, V2_OFFSET_ROLLBACK, 0, 0, 0, 0, 0, &r1);
	arm_smccc_smc(SIP_I2C_SEC_WRITE, bus, V2_OFFSET_MULTI_DMA,
		      I2C_SHADOW_REG_MODE, 0, 0, 0, 0, &r2);
	pr_info("cosmo_i2cmch: bus %u (%s): clocks %d%d%d; ROLLBACK=0 -> 0x%lx, MULTI_DMA=2 -> 0x%lx\n",
		bus, dev_name(adap->dev.parent), !!clk[0], !!clk[1], !!clk[2],
		r1.a0, r2.a0);
	for (i = 0; i < 3; i++) {
		if (clk[i]) {
			clk_disable_unprepare(clk[i]);
			clk_put(clk[i]);
		}
	}
	i2c_put_adapter(adap);
	return -EAGAIN;
}
module_init(cosmo_i2cmch_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo test: stock multi-channel I2C resume SIP writes");
