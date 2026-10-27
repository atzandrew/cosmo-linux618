// SPDX-License-Identifier: GPL-2.0
/*
 * Hotplug test: after a CPU is dead, make the call stock 4.4 makes in
 * CPU_DEAD (hps_v3 mtk_hotplug_cb.c): SIP MTK_SIP_POWER_DOWN_CORE(cpu).
 * Only CPUs in 'mask' are touched. Cluster calls are not made.
 */
#include <linux/module.h>
#include <linux/cpuhotplug.h>
#include <linux/arm-smccc.h>

#define MTK_SIP_POWER_DOWN_CORE 0xC2000218

static unsigned int mask = 0x80;
module_param(mask, uint, 0444);

static int hp_state;

static int cosmo_hp_dead(unsigned int cpu)
{
	struct arm_smccc_res res;

	if (!(mask & BIT(cpu)))
		return 0;
	arm_smccc_smc(MTK_SIP_POWER_DOWN_CORE, cpu, 0, 0, 0, 0, 0, 0, &res);
	pr_info("cosmo_hp: POWER_DOWN_CORE cpu%u -> 0x%lx\n", cpu, res.a0);
	return 0;
}

static int __init cosmo_hp_init(void)
{
	hp_state = cpuhp_setup_state_nocalls(CPUHP_BP_PREPARE_DYN,
					     "cosmo/hp:dead", NULL, cosmo_hp_dead);
	if (hp_state < 0)
		return hp_state;
	pr_info("cosmo_hp: loaded, mask 0x%x\n", mask);
	return 0;
}

static void __exit cosmo_hp_exit(void)
{
	cpuhp_remove_state_nocalls(hp_state);
}

module_init(cosmo_hp_init);
module_exit(cosmo_hp_exit);
MODULE_LICENSE("GPL");
