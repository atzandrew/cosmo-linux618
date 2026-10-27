// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cosmo Communicator (MT6771): power a core down in the vendor ATF after
 * it has gone offline.
 *
 * With the vendor ATF (v1.3, plat/mediatek/mt6771), PSCI CPU_OFF alone
 * leaves the core in a state from which PSCI CPU_ON never brings it
 * back ("failed to come online"). Stock 4.4 follows every CPU death with
 * the SiP call MTK_SIP_POWER_DOWN_CORE(cpu) (hps_v3 mtk_hotplug_cb.c,
 * CPU_DEAD and CPU_DEAD_FROZEN). This driver makes the same call from a
 * cpuhp prepare-state teardown, which runs on the control CPU after the
 * dying CPU has been reported off. It covers sysfs hotplug and the
 * non-boot CPU shutdown in system suspend.
 *
 * Stock's cluster call (MTK_SIP_POWER_DOWN_CLUSTER) and ARMPLL handling
 * for an empty cluster are not done here.
 */
#include <linux/arm-smccc.h>
#include <linux/cpuhotplug.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>

#define MTK_SIP_POWER_DOWN_CORE	0xC2000218

static unsigned int calls;
module_param(calls, uint, 0444);
MODULE_PARM_DESC(calls, "POWER_DOWN_CORE calls since boot");

static unsigned int errors;
module_param(errors, uint, 0444);
MODULE_PARM_DESC(errors, "POWER_DOWN_CORE calls that returned non-zero");

static int cosmo_cpu_dead(unsigned int cpu)
{
	struct arm_smccc_res res;

	arm_smccc_smc(MTK_SIP_POWER_DOWN_CORE, cpu, 0, 0, 0, 0, 0, 0, &res);
	calls++;
	if (res.a0) {
		errors++;
		pr_warn("cosmo-cpu-hotplug: POWER_DOWN_CORE cpu%u returned 0x%lx\n",
			cpu, res.a0);
	}
	return 0;
}

static int __init cosmo_cpu_hotplug_init(void)
{
	int ret;

	if (!of_machine_is_compatible("planet,cosmo-communicator"))
		return 0;

	ret = cpuhp_setup_state_nocalls(CPUHP_BP_PREPARE_DYN,
					"soc/cosmo-cpu-hotplug:dead",
					NULL, cosmo_cpu_dead);
	if (ret < 0)
		return ret;
	pr_info("cosmo-cpu-hotplug: core power-down after CPU_OFF enabled\n");
	return 0;
}
device_initcall(cosmo_cpu_hotplug_init);

MODULE_DESCRIPTION("Cosmo Communicator: vendor ATF core power-down after CPU_OFF");
MODULE_LICENSE("GPL");
