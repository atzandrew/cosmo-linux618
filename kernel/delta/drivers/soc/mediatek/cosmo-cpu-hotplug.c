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
 *
 * CPU idle (MCDI). A core powered down in idle (PSCI CPU_SUSPEND
 * 0x00010001) is powered back on by the SSPM MCDI controller, which only
 * does so for CPUs in its AVAIL_CPU_MASK (SSPM mailbox 3 slot 7, 0 after
 * boot: the core never wakes). As stock mcdi_v1 (mtk_mcdi.c,
 * mtk_mcdi_governor.c): after SMP bring-up write the online mask and
 * CPU_ISOLATION_MASK 0; around every hotplug keep all CPUs out of the
 * deep state and awake (stock mcdi_pause: WFI only + wake all CPUs; here
 * a CPU latency QoS request of 0 + wake_up_all_idle_cpus()), then write
 * the new online mask and resume.
 */
#include <linux/arm-smccc.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/sched.h>

#define MTK_SIP_POWER_DOWN_CORE	0xC2000218

static unsigned int calls;
module_param(calls, uint, 0444);
MODULE_PARM_DESC(calls, "POWER_DOWN_CORE calls since boot");

static unsigned int errors;
module_param(errors, uint, 0444);
MODULE_PARM_DESC(errors, "POWER_DOWN_CORE calls that returned non-zero");

#define MCDI_MBOX_BASE		0x10480000	/* SSPM mailbox 3 */
#define MCDI_AVAIL_CPU_MASK	(4 * 7)
#define MCDI_CPU_ISOLATION_MASK	(4 * 12)

static void __iomem *mcdi_mbox;
static struct pm_qos_request mcdi_pause_req;

static unsigned int mcdi_mask_writes;
module_param(mcdi_mask_writes, uint, 0444);
MODULE_PARM_DESC(mcdi_mask_writes, "AVAIL_CPU_MASK writes since boot");

static void cosmo_mcdi_avail_update(void)
{
	u32 mask = 0;
	int cpu;

	for_each_online_cpu(cpu)
		mask |= BIT(cpu);
	writel(mask, mcdi_mbox + MCDI_AVAIL_CPU_MASK);
	mcdi_mask_writes++;
}

/* stock mcdi_pause(true): no deep idle, every CPU awake */
static void cosmo_mcdi_pause(void)
{
	cpu_latency_qos_update_request(&mcdi_pause_req, 0);
	wake_up_all_idle_cpus();
}

static void cosmo_mcdi_resume(void)
{
	cosmo_mcdi_avail_update();
	cpu_latency_qos_update_request(&mcdi_pause_req, PM_QOS_DEFAULT_VALUE);
}

/* BP prepare state: before a CPU comes up (control CPU) */
static int cosmo_cpu_prepare(unsigned int cpu)
{
	cosmo_mcdi_pause();
	return 0;
}

/* AP online state: on the new CPU once it is online / before it goes down */
static int cosmo_cpu_online(unsigned int cpu)
{
	cosmo_mcdi_resume();
	return 0;
}

static int cosmo_cpu_down_prepare(unsigned int cpu)
{
	cosmo_mcdi_pause();
	return 0;
}

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
	cosmo_mcdi_resume();
	return 0;
}

/*
 * After SMP bring-up (all CPUs online) and before cpuidle-psci registers
 * its states (device_initcall): stock's MCDI init values.
 */
static int __init cosmo_mcdi_init(void)
{
	if (!of_machine_is_compatible("planet,cosmo-communicator"))
		return 0;

	mcdi_mbox = ioremap(MCDI_MBOX_BASE, 0x100);
	if (!mcdi_mbox)
		return -ENOMEM;
	cpu_latency_qos_add_request(&mcdi_pause_req, PM_QOS_DEFAULT_VALUE);
	writel(0, mcdi_mbox + MCDI_CPU_ISOLATION_MASK);
	cosmo_mcdi_avail_update();
	pr_info("cosmo-cpu-hotplug: MCDI avail mask 0x%x, isolation 0\n",
		readl(mcdi_mbox + MCDI_AVAIL_CPU_MASK));
	return 0;
}
arch_initcall(cosmo_mcdi_init);

static int __init cosmo_cpu_hotplug_init(void)
{
	int ret;

	if (!of_machine_is_compatible("planet,cosmo-communicator"))
		return 0;

	if (!mcdi_mbox)
		return -ENODEV;

	ret = cpuhp_setup_state_nocalls(CPUHP_BP_PREPARE_DYN,
					"soc/cosmo-cpu-hotplug:dead",
					cosmo_cpu_prepare, cosmo_cpu_dead);
	if (ret < 0)
		return ret;
	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"soc/cosmo-cpu-hotplug:mcdi",
					cosmo_cpu_online, cosmo_cpu_down_prepare);
	if (ret < 0)
		return ret;
	pr_info("cosmo-cpu-hotplug: core power-down after CPU_OFF enabled\n");
	return 0;
}
device_initcall(cosmo_cpu_hotplug_init);

MODULE_DESCRIPTION("Cosmo Communicator: vendor ATF core power-down after CPU_OFF");
MODULE_LICENSE("GPL");
