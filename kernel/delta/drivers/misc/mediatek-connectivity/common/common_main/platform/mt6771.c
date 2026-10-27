/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
/*! \file
*    \brief  Declaration of library functions
*
*    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG "[WMT-CONSYS-HW]"

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include <connectivity_build_in_adapter.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/memblock.h>
#include <linux/platform_device.h>
#include "osal_typedef.h"
#include "mt6771.h"
#include "mtk_wcn_consys_hw.h"

#ifdef CONFIG_MTK_EMI
#include <mt_emi_api.h>
#endif

#if CONSYS_PMIC_CTRL_ENABLE
#include <linux/regulator/consumer.h>
#endif

#ifdef CONFIG_MTK_HIBERNATION
#include <mtk_hibernate_dpm.h>
#endif

#include <linux/of_reserved_mem.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/reset.h>
#include <linux/sizes.h>

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
static INT32 consys_clock_buffer_ctrl(MTK_WCN_BOOL enable);
static VOID consys_hw_reset_bit_set(MTK_WCN_BOOL enable);
static VOID consys_hw_spm_clk_gating_enable(VOID);
static INT32 consys_hw_power_ctrl(MTK_WCN_BOOL enable);
static INT32 consys_ahb_clock_ctrl(MTK_WCN_BOOL enable);
static INT32 polling_consys_chipid(VOID);
static VOID update_consys_rom_desel(VOID);
static VOID consys_hang_debug_info(VOID);
static VOID consys_acr_reg_setting(VOID);
static VOID consys_afe_reg_setting(VOID);
static INT32 consys_hw_vcn18_ctrl(MTK_WCN_BOOL enable);
static VOID consys_vcn28_hw_mode_ctrl(UINT32 enable);
static INT32 consys_hw_vcn28_ctrl(UINT32 enable);
static INT32 consys_hw_wifi_vcn33_ctrl(UINT32 enable);
static INT32 consys_hw_bt_vcn33_ctrl(UINT32 enable);
static UINT32 consys_soc_chipid_get(VOID);
static INT32 consys_emi_mpu_set_region_protection(VOID);
static UINT32 consys_emi_set_remapping_reg(VOID);
static INT32 bt_wifi_share_v33_spin_lock_init(VOID);
static INT32 consys_clk_get_from_dts(struct platform_device *pdev);
static INT32 consys_pmic_get_from_dts(struct platform_device *pdev);
static INT32 consys_read_irq_info_from_dts(struct platform_device *pdev, PINT32 irq_num, PUINT32 irq_flag);
static INT32 consys_read_reg_from_dts(struct platform_device *pdev);
static UINT32 consys_read_cpupcr(VOID);
static VOID force_trigger_assert_debug_pin(VOID);
static INT32 consys_co_clock_type(VOID);
static P_CONSYS_EMI_ADDR_INFO consys_soc_get_emi_phy_add(VOID);
static VOID consys_set_if_pinmux(MTK_WCN_BOOL enable);
static INT32 consys_emi_coredump_remapping(UINT8 __iomem **addr, UINT32 enable);
static INT32 consys_reset_emi_coredump(UINT8 __iomem *addr);

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
#if CONSYS_BT_WIFI_SHARE_V33
struct bt_wifi_v33_status gBtWifiV33;
#endif

/* CCF part */
struct clk *clk_scp_conn_main;	/*ctrl conn_power_on/off */
/* [port 6.18] mainline access paths (see consys_read_reg_from_dts) */
static struct reset_control *consys_mcu_rst;	/* TOPRGU SWSYSRST bit 12 */
static struct pinctrl *consys_if_pinctrl;
static struct pinctrl_state *consys_if_pins_on, *consys_if_pins_off;
static void __iomem *consys_infra_prot;	/* INFRACFG_AO, bus protect */
#define CONSYS_INFRA_PROT_STA1		0x228
#define CONSYS_INFRA_PROT_SET		0x2a0
#define CONSYS_INFRA_PROT_CLR		0x2a4
#define CONSYS_PWR_STATUS_OFFSET	0x180
#define CONSYS_PWR_STATUS_2ND_OFFSET	0x184
#define CONSYS_PWR_STA_MASK		(0x1 << 1)
#define CONSYS_MTCMOS_POLL		1000	/* x 10 us; stock polls forever */

/* PMIC part */
#if CONSYS_PMIC_CTRL_ENABLE
struct regulator *reg_VCN18;
struct regulator *reg_VCN28;
struct regulator *reg_VCN33_BT;
struct regulator *reg_VCN33_WIFI;
#endif

EMI_CTRL_STATE_OFFSET mtk_wcn_emi_state_off = {
	.emi_apmem_ctrl_state = EXP_APMEM_CTRL_STATE,
	.emi_apmem_ctrl_host_sync_state = EXP_APMEM_CTRL_HOST_SYNC_STATE,
	.emi_apmem_ctrl_host_sync_num = EXP_APMEM_CTRL_HOST_SYNC_NUM,
	.emi_apmem_ctrl_chip_sync_state = EXP_APMEM_CTRL_CHIP_SYNC_STATE,
	.emi_apmem_ctrl_chip_sync_num = EXP_APMEM_CTRL_CHIP_SYNC_NUM,
	.emi_apmem_ctrl_chip_sync_addr = EXP_APMEM_CTRL_CHIP_SYNC_ADDR,
	.emi_apmem_ctrl_chip_sync_len = EXP_APMEM_CTRL_CHIP_SYNC_LEN,
	.emi_apmem_ctrl_chip_print_buff_start = EXP_APMEM_CTRL_CHIP_PRINT_BUFF_START,
	.emi_apmem_ctrl_chip_print_buff_len = EXP_APMEM_CTRL_CHIP_PRINT_BUFF_LEN,
	.emi_apmem_ctrl_chip_print_buff_idx = EXP_APMEM_CTRL_CHIP_PRINT_BUFF_IDX,
	.emi_apmem_ctrl_chip_int_status = EXP_APMEM_CTRL_CHIP_INT_STATUS,
	.emi_apmem_ctrl_chip_paded_dump_end = EXP_APMEM_CTRL_CHIP_PAGED_DUMP_END,
	.emi_apmem_ctrl_host_outband_assert_w1 = EXP_APMEM_CTRL_HOST_OUTBAND_ASSERT_W1,
	.emi_apmem_ctrl_chip_page_dump_num = EXP_APMEM_CTRL_CHIP_PAGE_DUMP_NUM,
	.emi_apmem_ctrl_assert_flag = EXP_APMEM_CTRL_ASSERT_FLAG,
};

CONSYS_EMI_ADDR_INFO mtk_wcn_emi_addr_info = {
	.emi_phy_addr = CONSYS_EMI_FW_PHY_BASE,
	.paged_trace_off = CONSYS_EMI_PAGED_TRACE_OFFSET,
	.paged_dump_off = CONSYS_EMI_PAGED_DUMP_OFFSET,
	.full_dump_off = CONSYS_EMI_FULL_DUMP_OFFSET,
	.emi_remap_offset = CONSYS_EMI_MAPPING_OFFSET,
	.p_ecso = &mtk_wcn_emi_state_off,
	.emi_core_dump_offset = CONSYS_EMI_COREDUMP_OFFSET,
};

WMT_CONSYS_IC_OPS consys_ic_ops = {
	.consys_ic_clock_buffer_ctrl = consys_clock_buffer_ctrl,
	.consys_ic_hw_reset_bit_set = consys_hw_reset_bit_set,
	.consys_ic_hw_spm_clk_gating_enable = consys_hw_spm_clk_gating_enable,
	.consys_ic_hw_power_ctrl = consys_hw_power_ctrl,
	.consys_ic_ahb_clock_ctrl = consys_ahb_clock_ctrl,
	.polling_consys_ic_chipid = polling_consys_chipid,
	.update_consys_rom_desel_value = update_consys_rom_desel,
	.consys_hang_debug = consys_hang_debug_info,
	.consys_ic_acr_reg_setting = consys_acr_reg_setting,
	.consys_ic_afe_reg_setting = consys_afe_reg_setting,
	.consys_ic_hw_vcn18_ctrl = consys_hw_vcn18_ctrl,
	.consys_ic_vcn28_hw_mode_ctrl = consys_vcn28_hw_mode_ctrl,
	.consys_ic_hw_vcn28_ctrl = consys_hw_vcn28_ctrl,
	.consys_ic_hw_wifi_vcn33_ctrl = consys_hw_wifi_vcn33_ctrl,
	.consys_ic_hw_bt_vcn33_ctrl = consys_hw_bt_vcn33_ctrl,
	.consys_ic_soc_chipid_get = consys_soc_chipid_get,
	.consys_ic_emi_mpu_set_region_protection = consys_emi_mpu_set_region_protection,
	.consys_ic_emi_set_remapping_reg = consys_emi_set_remapping_reg,
	.ic_bt_wifi_share_v33_spin_lock_init = bt_wifi_share_v33_spin_lock_init,
	.consys_ic_clk_get_from_dts = consys_clk_get_from_dts,
	.consys_ic_pmic_get_from_dts = consys_pmic_get_from_dts,
	.consys_ic_read_irq_info_from_dts = consys_read_irq_info_from_dts,
	.consys_ic_read_reg_from_dts = consys_read_reg_from_dts,
	.consys_ic_read_cpupcr = consys_read_cpupcr,
	.ic_force_trigger_assert_debug_pin = force_trigger_assert_debug_pin,
	.consys_ic_co_clock_type = consys_co_clock_type,
	.consys_ic_soc_get_emi_phy_add = consys_soc_get_emi_phy_add,
	.consys_ic_set_if_pinmux = consys_set_if_pinmux,
	.consys_ic_emi_coredump_remapping = consys_emi_coredump_remapping,
	.consys_ic_reset_emi_coredump = consys_reset_emi_coredump,
};

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
UINT32 gJtagCtrl;

#if CONSYS_ENALBE_SET_JTAG
#define JTAG_ADDR1_BASE 0x10005000
#define JTAG_ADDR2_BASE 0x11E80000
#define JTAG_ADDR3_BASE 0x11D20000
#define AP2CONN_JTAG_2WIRE_OFFSET 0xF00
#endif

INT32 mtk_wcn_consys_jtag_set_for_mcu(VOID)
{
#if CONSYS_ENALBE_SET_JTAG
	INT32 ret = 0;
	UINT32 tmp = 0;
	PVOID addr = 0;
	PVOID remap_addr1 = 0;
	PVOID remap_addr2 = 0;
	PVOID remap_addr3 = 0;

	if (gJtagCtrl) {

		remap_addr1 = ioremap(JTAG_ADDR1_BASE, 0x1000);
		if (remap_addr1 == 0) {
			WMT_PLAT_PR_ERR("remap jtag_addr1 fail!\n");
			ret = -1;
			goto error;
		}

		remap_addr2 = ioremap(JTAG_ADDR2_BASE, 0x100);
		if (remap_addr2 == 0) {
			WMT_PLAT_PR_ERR("remap jtag_addr2 fail!\n");
			ret = -1;
			goto error;
		}

		remap_addr3 = ioremap(JTAG_ADDR3_BASE, 0x100);
		if (remap_addr3 == 0) {
			WMT_PLAT_PR_ERR("remap jtag_addr3 fail!\n");
			ret =  -1;
			goto error;
		}

		WMT_PLAT_PR_INFO("WCN jtag set for mcu start...\n");
		switch (gJtagCtrl) {
		case 1:
			/* 7-wire jtag pinmux setting*/
#if 1
			/* PAD AUX Function Selection */
			addr = remap_addr1 + 0x320;
			tmp = readl(addr);
			tmp = tmp & 0x0000000f;
			tmp = tmp | 0x33333330;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);

			/* PAD Driving Selection */
			addr = remap_addr2 + 0xA0;
			tmp = readl(addr);
			tmp = tmp & 0xfff00fff;
			tmp = tmp | 0x00077000;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);

			/* PAD PULL Selection */
			addr = remap_addr2 + 0x60;
			tmp = readl(addr);
			tmp = tmp & 0xfffe03ff;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);

			addr = remap_addr2 + 0x60;
			tmp = readl(addr);
			tmp = tmp & 0xfff80fff;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);

#else
			/* backup */
			/* PAD AUX Function Selection */
			addr = remap_addr1 + 0x310;
			tmp = readl(addr);
			tmp = tmp & 0xfffff000;
			tmp = tmp | 0x00000444;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);

			addr = remap_addr1 + 0x3C0;
			tmp = readl(addr);
			tmp = tmp & 0xf00ff00f;
			tmp = tmp | 0x04400440;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);

			/* PAD Driving Selection */
			addr = remap_addr3 + 0xA0;
			tmp = readl(addr);
			tmp = tmp & 0x0ffffff0;
			tmp = tmp | 0x70000007;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);

			addr = remap_addr3 + 0xB0;
			tmp = readl(addr);
			tmp = tmp & 0xfff0f0ff;
			tmp = tmp | 0x0007070f;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);

			/* PAD PULL Selection */
			addr = remap_addr3 + 0x60;
			tmp = readl(addr);
			tmp = tmp & 0xf333fffe;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);
#endif
			break;
		case 2:
			/* 2-wire jtag pinmux setting*/
#if 0
			CONSYS_SET_BIT(conn_reg.topckgen_base + AP2CONN_JTAG_2WIRE_OFFSET, 1 << 8);
			addr = remap_addr1 + 0x340;
			tmp = readl(addr);
			tmp = tmp & 0xfff88fff;
			tmp = tmp | 0x00034000;
			writel(tmp, addr);
			tmp = readl(addr);
			WMT_PLAT_PR_INFO("(RegAddr, RegVal):(0x%p, 0x%x)\n", addr, tmp);
#endif
			break;
		default:
			WMT_PLAT_PR_INFO("unsupported options!\n");
		}

	}
#endif

error:

	if (remap_addr1)
		iounmap(remap_addr1);
	if (remap_addr2)
		iounmap(remap_addr2);
	if (remap_addr3)
		iounmap(remap_addr3);

	return ret;
}

UINT32 mtk_wcn_consys_jtag_flag_ctrl(UINT32 en)
{
	WMT_PLAT_PR_INFO("%s jtag set for MCU\n", en ? "enable" : "disable");
	gJtagCtrl = en;
	return 0;
}

static INT32 consys_clk_get_from_dts(struct platform_device *pdev)
{
	/*
	 * [port 6.18] Stock powers CONN through the vendor clock driver
	 * (SCP_SYS_CONN = clk-mt6771-pg.c spm_mtcmos_ctrl_conn()). The mainline
	 * scpsys driver does not probe on this board, so consys_hw_power_ctrl()
	 * runs that MTCMOS sequence itself. No clock to get.
	 */
	return 0;
}

static INT32 consys_pmic_get_from_dts(struct platform_device *pdev)
{
#if CONSYS_PMIC_CTRL_ENABLE
	/*
	 * [port 6.18] mainline mt6358-regulator merges VCN33_BT and VCN33_WIFI
	 * into one "vcn33" regulator (two enable bits kept in sync). Stock only
	 * drives the BT side here (the WIFI-side control is compiled out).
	 */
	reg_VCN18 = devm_regulator_get(&pdev->dev, "vcn18");
	if (IS_ERR(reg_VCN18)) {
		WMT_PLAT_PR_ERR("Regulator_get VCN_1V8 fail\n");
		reg_VCN18 = NULL;
	}
	reg_VCN28 = devm_regulator_get(&pdev->dev, "vcn28");
	if (IS_ERR(reg_VCN28)) {
		WMT_PLAT_PR_ERR("Regulator_get VCN_2V8 fail\n");
		reg_VCN28 = NULL;
	}
	reg_VCN33_BT = devm_regulator_get(&pdev->dev, "vcn33");
	if (IS_ERR(reg_VCN33_BT)) {
		WMT_PLAT_PR_ERR("Regulator_get VCN33 fail\n");
		reg_VCN33_BT = NULL;
	}
	reg_VCN33_WIFI = NULL;
#endif
	return 0;
}

static INT32 consys_co_clock_type(VOID)
{
	return 0;
}

static INT32 consys_clock_buffer_ctrl(MTK_WCN_BOOL enable)
{
	/* [port 6.18] only called in co-clock mode; MT6771 is not co-clock
	 * (consys_co_clock_type() == 0, WMT_SOC.cfg co_clock_flag=0). */
	WMT_PLAT_PR_WARN("clock buffer ctrl(%d) not supported\n", enable);
	return 0;
}

static VOID consys_set_if_pinmux(MTK_WCN_BOOL enable)
{
	/*
	 * [port 6.18] Stock writes GPIO_MODE 0x100053e0/0x3f0 directly: pins
	 * GPIO113-121 to mode 1 (CONN_*) on, mode 0 off. The DT "on"/"off"
	 * pin states hold the same muxes; the pinctrl driver owns the registers.
	 */
	struct pinctrl_state *st = enable ? consys_if_pins_on : consys_if_pins_off;

	if (!consys_if_pinctrl || IS_ERR_OR_NULL(st)) {
		WMT_PLAT_PR_ERR("A-die pinmux states missing\n");
		return;
	}
	if (pinctrl_select_state(consys_if_pinctrl, st))
		WMT_PLAT_PR_ERR("A-die pinmux %s failed\n", enable ? "on" : "off");
}

static VOID consys_hw_reset_bit_set(MTK_WCN_BOOL enable)
{
	/*
	 * [port 6.18] TOPRGU 0x10007018 bit 12 with key 0x88 << 24, as stock,
	 * through the mtk_wdt reset controller (same read-modify-write, under
	 * the watchdog driver's lock).
	 */
	if (!consys_mcu_rst)
		return;
	if (enable)
		reset_control_assert(consys_mcu_rst);
	else
		reset_control_deassert(consys_mcu_rst);
}

static VOID consys_hw_spm_clk_gating_enable(VOID)
{
	/*turn on SPM clock gating enable PWRON_CONFG_EN 0x10006000 32'h0b160001 */
}

static VOID consys_conn_pwr_rmw(UINT32 clr, UINT32 set)
{
	UINT32 v = CONSYS_REG_READ(conn_reg.spm_base + CONSYS_TOP1_PWR_CTRL_OFFSET);

	CONSYS_REG_WRITE(conn_reg.spm_base + CONSYS_TOP1_PWR_CTRL_OFFSET, (v & ~clr) | set);
}

static MTK_WCN_BOOL consys_conn_pwr_sta(VOID)
{
	return (CONSYS_REG_READ(conn_reg.spm_base + CONSYS_PWR_STATUS_OFFSET) & CONSYS_PWR_STA_MASK) &&
	       (CONSYS_REG_READ(conn_reg.spm_base + CONSYS_PWR_STATUS_2ND_OFFSET) & CONSYS_PWR_STA_MASK);
}

/*
 * [port 6.18] stock clk-mt6771-pg.c spm_mtcmos_ctrl_conn(), the code behind
 * stock's clk_prepare_enable(SCP_SYS_CONN); polls bounded. Checked on glass
 * (W1a): chip ID 0x0788 after power-on, registers back to the boot values
 * after power-off.
 */
static INT32 consys_hw_power_ctrl(MTK_WCN_BOOL enable)
{
	INT32 i;

	if (enable) {
		consys_conn_pwr_rmw(0, CONSYS_SPM_PWR_ON_BIT);
		consys_conn_pwr_rmw(0, CONSYS_SPM_PWR_ON_S_BIT);
		for (i = 0; i < CONSYS_MTCMOS_POLL && !consys_conn_pwr_sta(); i++)
			udelay(10);
		if (i == CONSYS_MTCMOS_POLL) {
			WMT_PLAT_PR_ERR("CONN power ack timeout\n");
			return -1;
		}
		consys_conn_pwr_rmw(CONSYS_CLK_CTRL_BIT, 0);
		consys_conn_pwr_rmw(CONSYS_SPM_PWR_ISO_S_BIT, 0);
		consys_conn_pwr_rmw(0, CONSYS_SPM_PWR_RST_BIT);
		writel(CONSYS_PROT_MASK, consys_infra_prot + CONSYS_INFRA_PROT_CLR);
	} else {
		writel(CONSYS_PROT_MASK, consys_infra_prot + CONSYS_INFRA_PROT_SET);
		for (i = 0; i < CONSYS_MTCMOS_POLL; i++) {
			if ((readl(consys_infra_prot + CONSYS_INFRA_PROT_STA1) & CONSYS_PROT_MASK) ==
			    CONSYS_PROT_MASK)
				break;
			udelay(10);
		}
		if (i == CONSYS_MTCMOS_POLL) {
			/* cutting power with the bus unprotected can hang it */
			WMT_PLAT_PR_ERR("CONN bus protect ack timeout, CONN stays on\n");
			return -1;
		}
		consys_conn_pwr_rmw(0, CONSYS_SPM_PWR_ISO_S_BIT);
		consys_conn_pwr_rmw(0, CONSYS_CLK_CTRL_BIT);
		consys_conn_pwr_rmw(CONSYS_SPM_PWR_RST_BIT, 0);
		consys_conn_pwr_rmw(CONSYS_SPM_PWR_ON_BIT, 0);
		consys_conn_pwr_rmw(CONSYS_SPM_PWR_ON_S_BIT, 0);
		for (i = 0; i < CONSYS_MTCMOS_POLL; i++) {
			if (!(CONSYS_REG_READ(conn_reg.spm_base + CONSYS_PWR_STATUS_OFFSET) &
			      CONSYS_PWR_STA_MASK) &&
			    !(CONSYS_REG_READ(conn_reg.spm_base + CONSYS_PWR_STATUS_2ND_OFFSET) &
			      CONSYS_PWR_STA_MASK))
				break;
			udelay(10);
		}
		if (i == CONSYS_MTCMOS_POLL) {
			WMT_PLAT_PR_ERR("CONN power-off status timeout\n");
			return -1;
		}
	}
	return 0;
}

static INT32 consys_ahb_clock_ctrl(MTK_WCN_BOOL enable)
{
	return 0;
}

static INT32 polling_consys_chipid(VOID)
{
	UINT32 retry = 10;
	UINT32 consysHwChipId = 0;

	/*12.poll CONNSYS CHIP ID until chipid is returned  0x18070008 */
	while (retry-- > 0) {
		consysHwChipId = CONSYS_REG_READ(conn_reg.mcu_base + CONSYS_CHIP_ID_OFFSET);
		if (consysHwChipId == 0x0788) {
			WMT_PLAT_PR_INFO("retry(%d)consys chipId(0x%08x)\n", retry, consysHwChipId);
			break;
		}
		msleep(20);
	}

	if ((retry == 0) || (consysHwChipId == 0)) {
		WMT_PLAT_PR_ERR("Maybe has a consys power on issue,(0x%08x)\n", consysHwChipId);
		WMT_PLAT_PR_INFO("reg dump:CONSYS_CPU_SW_RST_REG(0x%x)\n",
				CONSYS_REG_READ(conn_reg.ap_rgu_base + CONSYS_CPU_SW_RST_OFFSET));
		WMT_PLAT_PR_INFO("reg dump:CONSYS_PWR_CONN_ACK_REG(0x%x)\n",
				CONSYS_REG_READ(conn_reg.spm_base + CONSYS_PWR_CONN_ACK_OFFSET));
		WMT_PLAT_PR_INFO("reg dump:CONSYS_PWR_CONN_ACK_S_REG(0x%x)\n",
				CONSYS_REG_READ(conn_reg.spm_base + CONSYS_PWR_CONN_ACK_S_OFFSET));
		WMT_PLAT_PR_INFO("reg dump:CONSYS_TOP1_PWR_CTRL_REG(0x%x)\n",
				CONSYS_REG_READ(conn_reg.spm_base + CONSYS_TOP1_PWR_CTRL_OFFSET));
	}

	return 0;
}

static VOID update_consys_rom_desel(VOID)
{
	CONSYS_REG_WRITE(conn_reg.mcu_base + CONSYS_ROM_DESEL_OFFSET, CONSYS_ROM_DESEL_MASK);
	WMT_PLAT_PR_INFO("Update consys rom desel value(0x%x)\n",
			CONSYS_REG_READ(conn_reg.mcu_base + CONSYS_ROM_DESEL_OFFSET));
}

static VOID consys_hang_debug_info(VOID)
{
	SIZE_T addr_1;
	SIZE_T addr_2;
	SIZE_T addr_3;

	UINT32 rv1, wv1[2], rv2[2], rv3, wv3 = 0;
	UINT32 i = 0;

	addr_1 = conn_reg.mcu_base + CONSYS_HANG_DBG_OFFSET_1;
	addr_2 = conn_reg.mcu_base + CONSYS_HANG_DBG_OFFSET_2;
	addr_3 = conn_reg.mcu_base + CONSYS_HANG_DBG_OFFSET_3;
	rv1 = CONSYS_REG_READ(addr_1);
	rv3 = CONSYS_REG_READ(addr_3);
	WMT_PLAT_PR_INFO("addr1:%zx:0x%x/addr2:%zx/addr3:%zx:0x%x\n",
			   addr_1, rv1, addr_2, addr_3, rv3);

	wv1[0] = (rv1 & 0xFF0000FF) | (0x0201 << 8);
	CONSYS_REG_WRITE(addr_1, wv1[0]);
	rv2[0] = CONSYS_REG_READ(addr_2);

	wv1[1] = (rv1 & 0xFF0000FF) | (0x0403 << 8);
	CONSYS_REG_WRITE(addr_1, wv1[1]);
	rv2[1] = CONSYS_REG_READ(addr_2);
	WMT_PLAT_PR_INFO("0x%x->addr1, addr2:0x%x/0x%x->addr1, addr2:0x%x\n",
			   wv1[0], rv2[0], wv1[1], rv2[1]);

	for (i = 0; i < 9; i++) {
		wv3 = (rv3 & 0xFFFFFF0F) | (i << 4);
		CONSYS_REG_WRITE(addr_3, wv3);

		wv1[0] = (rv1 & 0xFF0000FF) | (0x5251 << 8);
		CONSYS_REG_WRITE(addr_1, wv1[0]);
		rv2[0] = CONSYS_REG_READ(addr_2);

		wv1[1] = (rv1 & 0xFF0000FF) | (0x5453 << 8);
		CONSYS_REG_WRITE(addr_1, wv1[1]);
		rv2[1] = CONSYS_REG_READ(addr_2);
		WMT_PLAT_PR_INFO("0x%x->addr3,0x%x->addr1,addr2:0x%x/0x%x->addr1,addr2:0x%x\n",
				   wv3, wv1[0], rv2[0], wv1[1], rv2[1]);
	}

	WMT_PLAT_PR_INFO("apb0_dbg_prob\n");
	for (i = 0; i < 10; i++) {
		wv3 = (rv3 & 0xFFFF0FFF) | (i << 12);
		CONSYS_REG_WRITE(addr_3, wv3);

		wv1[0] = (rv1 & 0xFF0000FF) | (0x5655 << 8);
		CONSYS_REG_WRITE(addr_1, wv1[0]);
		rv2[0] = CONSYS_REG_READ(addr_2);

		wv1[1] = (rv1 & 0xFF0000FF) | (0x5857 << 8);
		CONSYS_REG_WRITE(addr_1, wv1[1]);
		rv2[1] = CONSYS_REG_READ(addr_2);
		WMT_PLAT_PR_INFO("0x%x->addr3,0x%x->addr1,addr2:0x%x/0x%x->addr1,addr2:0x%x\n",
				   wv3, wv1[0], rv2[0], wv1[1], rv2[1]);
	}

	WMT_PLAT_PR_INFO("apb1_dbg_prob\n");
	for (i = 0; i < 10; i++) {
		wv3 = (rv3 & 0xFF0FFFFF) | (i << 20);
		CONSYS_REG_WRITE(addr_3, wv3);

		wv1[0] = (rv1 & 0xFF0000FF) | (0x5A59 << 8);
		CONSYS_REG_WRITE(addr_1, wv1[0]);
		rv2[0] = CONSYS_REG_READ(addr_2);

		wv1[1] = (rv1 & 0xFF0000FF) | (0x5C5B << 8);
		CONSYS_REG_WRITE(addr_1, wv1[1]);
		rv2[1] = CONSYS_REG_READ(addr_2);
		WMT_PLAT_PR_INFO("0x%x->addr3,0x%x->addr1,addr2:0x%x/0x%x->addr1,addr2:0x%x\n",
				   wv3, wv1[0], rv2[0], wv1[1], rv2[1]);
	}
}

static VOID consys_acr_reg_setting(VOID)
{
	/*
	 * if bypass at-speed MBIST (default is at-speed) 0x18070140[1:0] 2'b11
	 */
	CONSYS_REG_WRITE(conn_reg.mcu_base + CONSYS_MCU_CFG_ACR_OFFSET,
			CONSYS_REG_READ(conn_reg.mcu_base + CONSYS_MCU_CFG_ACR_OFFSET) |
			CONSYS_MCU_CFG_ACR_MBIST_BIT);
}

static VOID consys_afe_reg_setting(VOID)
{
#if CONSYS_AFE_REG_SETTING
	UINT8 i = 0;
	UINT8 *consys_afe_reg_base = NULL;

	/*15.default no need,update ANA_WBG(AFE) CR if needed, CONSYS_AFE_REG */
	consys_afe_reg_base = ioremap_nocache(CONSYS_AFE_REG_BASE, 0x100);
	if (consys_afe_reg_base) {
		CONSYS_REG_WRITE(consys_afe_reg_base + CONSYS_AFE_RG_WBG_PLL_03_OFFSET,
				CONSYS_AFE_RG_WBG_PLL_03_VALUE);
		CONSYS_REG_WRITE(consys_afe_reg_base + CONSYS_AFE_RG_WBG_GPS_02_OFFSET,
				CONSYS_AFE_RG_WBG_GPS_02_VALUE);

		WMT_PLAT_PR_DBG("Dump AFE register\n");
		for (i = 0; i < 64; i++) {
			WMT_PLAT_PR_DBG("reg:0x%08x|val:0x%08x\n",
					CONSYS_AFE_REG_BASE + 4*i,
					CONSYS_REG_READ(consys_afe_reg_base + 4*i));
		}
		iounmap(consys_afe_reg_base);
	} else
		WMT_PLAT_PR_ERR("AFE base(0x%x) ioremap fail!\n", CONSYS_AFE_REG_BASE);
#endif
}

static INT32 consys_hw_vcn18_ctrl(MTK_WCN_BOOL enable)
{
#if CONSYS_PMIC_CTRL_ENABLE
	if (enable) {
		/*need PMIC driver provide new API protocol */
		/*1.AP power on VCN_1V8 LDO (with PMIC_WRAP API) VCN_1V8  */
		/* [port 6.18] VCN18_OP_EN (0x1c5a) already 0x1: no raw PMIC write */

		if (reg_VCN18) {
			regulator_set_voltage(reg_VCN18, 1800000, 1800000);
			if (regulator_enable(reg_VCN18))
				WMT_PLAT_PR_ERR("enable VCN18 fail\n");
			else
				WMT_PLAT_PR_DBG("enable VCN18 ok\n");
		}

		/* [port 6.18] VCN33_OP_EN (0x1d1e) already 0x1: no raw PMIC write */

		if (reg_VCN33_BT) {
			regulator_set_voltage(reg_VCN33_BT, 3500000, 3500000);
			if (regulator_enable(reg_VCN33_BT))
				WMT_PLAT_PR_ERR("WMT do BT PMIC on fail!\n");
		}

	} else {
		if (reg_VCN33_BT)
			regulator_disable(reg_VCN33_BT);

		/*AP power off MT6351L VCN_1V8 LDO */
		if (reg_VCN18) {
			if (regulator_disable(reg_VCN18))
				WMT_PLAT_PR_ERR("disable VCN_1V8 fail!\n");
			else
				WMT_PLAT_PR_DBG("disable VCN_1V8 ok\n");
		}
	}
#endif
	return 0;
}

static VOID consys_vcn28_hw_mode_ctrl(UINT32 enable)
{
	/*
	 * [port 6.18] Stock sets VCN28 HW0 op mode (PMIC 0x1d8a bit 1 = 1,
	 * 0x1d90 bit 1 = 0): VCN28 also follows SRCLKEN0, which only matters
	 * once the SW enable is off in sleep. Not written (no raw PMIC writes
	 * outside the regulator driver); VCN28 stays on by its SW enable.
	 */
}

static INT32 consys_hw_vcn28_ctrl(UINT32 enable)
{
#if CONSYS_PMIC_CTRL_ENABLE
	if (enable) {
		/*in co-clock mode,need to turn on vcn28 when fm on */
		if (reg_VCN28) {
			regulator_set_voltage(reg_VCN28, 2800000, 2800000);
			if (regulator_enable(reg_VCN28))
				WMT_PLAT_PR_ERR("WMT do VCN28 PMIC on fail!\n");
		}
		WMT_PLAT_PR_INFO("turn on vcn28 for fm/gps usage in co-clock mode\n");
	} else {
		/*in co-clock mode,need to turn off vcn28 when fm off */
		if (reg_VCN28)
			regulator_disable(reg_VCN28);
		WMT_PLAT_PR_INFO("turn off vcn28 for fm/gps usage in co-clock mode\n");
	}
#endif
	return 0;
}

static INT32 consys_hw_bt_vcn33_ctrl(UINT32 enable)
{
#if 0
#if CONSYS_BT_WIFI_SHARE_V33
	/* spin_lock_irqsave(&gBtWifiV33.lock,gBtWifiV33.flags); */
	if (enable) {
		if (gBtWifiV33.counter == 1) {
			gBtWifiV33.counter++;
			WMT_PLAT_PR_DBG("V33 has been enabled,counter(%d)\n", gBtWifiV33.counter);
		} else if (gBtWifiV33.counter == 2) {
			WMT_PLAT_PR_DBG("V33 has been enabled,counter(%d)\n", gBtWifiV33.counter);
		} else {
#if CONSYS_PMIC_CTRL_ENABLE
			/*do BT PMIC on,depenency PMIC API ready */
			/*switch BT PALDO control from SW mode to HW mode:0x416[5]-->0x1 */
			/* VOL_DEFAULT, VOL_3300, VOL_3400, VOL_3500, VOL_3600 */
			hwPowerOn(MT6351_POWER_LDO_VCN33_BT, VOL_3300 * 1000, "wcn_drv");
			mt6351_upmu_set_rg_vcn33_on_ctrl(1);
#endif
			WMT_PLAT_PR_INFO("WMT do BT/WIFI v3.3 on\n");
			gBtWifiV33.counter++;
		}

	} else {
		if (gBtWifiV33.counter == 1) {
			/*do BT PMIC off */
			/*switch BT PALDO control from HW mode to SW mode:0x416[5]-->0x0 */
#if CONSYS_PMIC_CTRL_ENABLE
			mt6351_upmu_set_rg_vcn33_on_ctrl(0);
			hwPowerDown(MT6351_POWER_LDO_VCN33_BT, "wcn_drv");
#endif
			WMT_PLAT_PR_INFO("WMT do BT/WIFI v3.3 off\n");
			gBtWifiV33.counter--;
		} else if (gBtWifiV33.counter == 2) {
			gBtWifiV33.counter--;
			WMT_PLAT_PR_DBG("V33 no need disabled,counter(%d)\n", gBtWifiV33.counter);
		} else {
			WMT_PLAT_PR_DBG("V33 has been disabled,counter(%d)\n", gBtWifiV33.counter);
		}

	}
	/* spin_unlock_irqrestore(&gBtWifiV33.lock,gBtWifiV33.flags); */
#else
	if (enable) {
		/*do BT PMIC on,depenency PMIC API ready */
		/*switch BT PALDO control from SW mode to HW mode:0x416[5]-->0x1 */
#if CONSYS_PMIC_CTRL_ENABLE
		/* VOL_DEFAULT, VOL_3300, VOL_3400, VOL_3500, VOL_3600 */
		if (reg_VCN33_BT) {
			regulator_set_voltage(reg_VCN33_BT, 3300000, 3300000);
			if (regulator_enable(reg_VCN33_BT))
				WMT_PLAT_PR_ERR("WMT do BT PMIC on fail!\n");
		}

		KERNEL_pmic_set_register_value(PMIC_RG_LDO_VCN33_HW0_OP_EN, 1);
		KERNEL_pmic_set_register_value(PMIC_RG_LDO_VCN33_HW0_OP_CFG, 0);
#endif
		WMT_PLAT_PR_DBG("WMT do BT PMIC on\n");
	} else {
		/*do BT PMIC off */
		/*switch BT PALDO control from HW mode to SW mode:0x416[5]-->0x0 */
#if CONSYS_PMIC_CTRL_ENABLE
		KERNEL_pmic_set_register_value(PMIC_RG_LDO_VCN33_HW0_OP_EN, 0);
		KERNEL_pmic_set_register_value(PMIC_RG_LDO_VCN33_HW0_OP_CFG, 0);
		if (reg_VCN33_BT)
			regulator_disable(reg_VCN33_BT);
#endif
		WMT_PLAT_PR_DBG("WMT do BT PMIC off\n");
	}
#endif

#endif
	return 0;
}

static INT32 consys_hw_wifi_vcn33_ctrl(UINT32 enable)
{
#if 0
#if CONSYS_BT_WIFI_SHARE_V33
	mtk_wcn_consys_hw_bt_paldo_ctrl(enable);
#else
	if (enable) {
		/*do WIFI PMIC on,depenency PMIC API ready */
		/*switch WIFI PALDO control from SW mode to HW mode:0x418[14]-->0x1 */
#if CONSYS_PMIC_CTRL_ENABLE
		if (reg_VCN33_WIFI) {
			regulator_set_voltage(reg_VCN33_WIFI, 3300000, 3300000);
			if (regulator_enable(reg_VCN33_WIFI))
				WMT_PLAT_PR_ERR("WMT do WIFI PMIC on fail!\n");
		}
		KERNEL_pmic_set_register_value(PMIC_RG_LDO_VCN33_HW0_OP_EN, 1);
		KERNEL_pmic_set_register_value(PMIC_RG_LDO_VCN33_HW0_OP_CFG, 0);
#endif
		WMT_PLAT_PR_DBG("WMT do WIFI PMIC on\n");
	} else {
		/*do WIFI PMIC off */
		/*switch WIFI PALDO control from HW mode to SW mode:0x418[14]-->0x0 */
#if CONSYS_PMIC_CTRL_ENABLE
		KERNEL_pmic_set_register_value(PMIC_RG_LDO_VCN33_HW0_OP_EN, 0);
		KERNEL_pmic_set_register_value(PMIC_RG_LDO_VCN33_HW0_OP_CFG, 0);
		if (reg_VCN33_WIFI)
			regulator_disable(reg_VCN33_WIFI);
#endif
		WMT_PLAT_PR_DBG("WMT do WIFI PMIC off\n");
	}

#endif

#endif
	return 0;
}

static INT32 consys_emi_mpu_set_region_protection(VOID)
{
#ifdef CONFIG_MTK_EMI
	struct emi_region_info_t region_info;

	/*set MPU for EMI share Memory */
	WMT_PLAT_PR_INFO("setting MPU for EMI share memory\n");

	region_info.start = gConEmiPhyBase + SZ_1M / 2 - SZ_64K;
	region_info.end = gConEmiPhyBase + gConEmiSize - 1;
	region_info.region = 26;
	SET_ACCESS_PERMISSION(region_info.apc, LOCK,
			FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN,
			FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN,
			NO_PROTECTION, FORBIDDEN, NO_PROTECTION);
	emi_mpu_set_protection(&region_info);
#endif
	return 0;
}

static UINT32 consys_emi_set_remapping_reg(VOID)
{
	UINT32 addrPhy = 0;

	/*consys to ap emi remapping register:10000380, cal remapping address */
	addrPhy = (gConEmiPhyBase >> 21) & 0x1FFF;

	/*enable consys to ap emi remapping bit13 */
	addrPhy = addrPhy | 0x2000;

	CONSYS_REG_WRITE(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET,
			CONSYS_REG_READ(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET) | addrPhy);

	WMT_PLAT_PR_INFO("CONSYS_EMI_MAPPING dump in restore cb(0x%08x)\n",
			CONSYS_REG_READ(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET));

	return 0;
}

static INT32 bt_wifi_share_v33_spin_lock_init(VOID)
{
#if CONSYS_BT_WIFI_SHARE_V33
	gBtWifiV33.counter = 0;
	spin_lock_init(&gBtWifiV33.lock);
#endif
	return 0;
}

static INT32 consys_read_irq_info_from_dts(struct platform_device *pdev, PINT32 irq_num, PUINT32 irq_flag)
{
	struct device_node *node;
	UINT32 irq_info[3] = { 0, 0, 0 };

	INT32 iret = -1;

	node = pdev->dev.of_node;
	if (node) {
		*irq_num = irq_of_parse_and_map(node, 0);
		/* get the interrupt line behaviour */
		if (of_property_read_u32_array(node, "interrupts", irq_info, ARRAY_SIZE(irq_info))) {
			WMT_PLAT_PR_ERR("get irq flags from DTS fail!!\n");
			return iret;
		}
		*irq_flag = irq_info[2];
		WMT_PLAT_PR_INFO("get irq id(%d) and irq trigger flag(%d) from DT\n", *irq_num,
				   *irq_flag);
	} else {
		WMT_PLAT_PR_ERR("[%s] can't find CONSYS compatible node\n", __func__);
		return iret;
	}

	return 0;
}

static INT32 consys_read_reg_from_dts(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *mem_np;
	struct reserved_mem *rmem;

	if (!node) {
		WMT_PLAT_PR_ERR("[%s] can't find CONSYS compatible node\n", __func__);
		return -1;
	}

	/* registers base address, stock order (vendor mt6771.dts consys node) */
	conn_reg.mcu_base = (SIZE_T) of_iomap(node, 0);
	conn_reg.ap_rgu_base = (SIZE_T) of_iomap(node, 1);
	conn_reg.topckgen_base = (SIZE_T) of_iomap(node, 2);
	conn_reg.spm_base = (SIZE_T) of_iomap(node, 3);
	consys_infra_prot = (void __iomem *)conn_reg.topckgen_base;
	if (!conn_reg.mcu_base || !conn_reg.ap_rgu_base || !conn_reg.topckgen_base ||
	    !conn_reg.spm_base) {
		WMT_PLAT_PR_ERR("CONSYS register map failed\n");
		return -1;
	}

	/*
	 * [port 6.18] stock sets gConEmiPhyBase/Size from a built-in
	 * RESERVEDMEM_OF_DECLARE hook (connectivity_build_in_adapter.c); a
	 * module looks the region up through memory-region instead.
	 */
	mem_np = of_parse_phandle(node, "memory-region", 0);
	rmem = mem_np ? of_reserved_mem_lookup(mem_np) : NULL;
	of_node_put(mem_np);
	if (!rmem) {
		WMT_PLAT_PR_ERR("CONSYS reserved memory not found\n");
		return -1;
	}
	gConEmiPhyBase = rmem->base;
	gConEmiSize = rmem->size;
	WMT_PLAT_PR_INFO("CONSYS EMI %pa size 0x%llx\n", &gConEmiPhyBase,
			 (unsigned long long)gConEmiSize);

	consys_mcu_rst = devm_reset_control_get_exclusive(&pdev->dev, "mcu");
	if (IS_ERR(consys_mcu_rst)) {
		WMT_PLAT_PR_ERR("CONN MCU reset line missing\n");
		consys_mcu_rst = NULL;
		return -1;
	}

	consys_if_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(consys_if_pinctrl)) {
		consys_if_pinctrl = NULL;
	} else {
		consys_if_pins_on = pinctrl_lookup_state(consys_if_pinctrl, "on");
		consys_if_pins_off = pinctrl_lookup_state(consys_if_pinctrl, "off");
	}
	if (!consys_if_pinctrl || IS_ERR(consys_if_pins_on) || IS_ERR(consys_if_pins_off)) {
		WMT_PLAT_PR_ERR("A-die pinmux states on/off missing\n");
		return -1;
	}

	return 0;
}

static VOID force_trigger_assert_debug_pin(VOID)
{
	CONSYS_REG_WRITE(conn_reg.topckgen_base + CONSYS_AP2CONN_OSC_EN_OFFSET,
			CONSYS_REG_READ(conn_reg.topckgen_base +
				CONSYS_AP2CONN_OSC_EN_OFFSET) & ~CONSYS_AP2CONN_WAKEUP_BIT);
	WMT_PLAT_PR_INFO("enable:dump CONSYS_AP2CONN_OSC_EN_REG(0x%x)\n",
			CONSYS_REG_READ(conn_reg.topckgen_base + CONSYS_AP2CONN_OSC_EN_OFFSET));
	usleep_range(64, 96);
	CONSYS_REG_WRITE(conn_reg.topckgen_base + CONSYS_AP2CONN_OSC_EN_OFFSET,
			CONSYS_REG_READ(conn_reg.topckgen_base +
				CONSYS_AP2CONN_OSC_EN_OFFSET) | CONSYS_AP2CONN_WAKEUP_BIT);
	WMT_PLAT_PR_INFO("disable:dump CONSYS_AP2CONN_OSC_EN_REG(0x%x)\n",
			CONSYS_REG_READ(conn_reg.topckgen_base + CONSYS_AP2CONN_OSC_EN_OFFSET));
}

static UINT32 consys_read_cpupcr(VOID)
{
	return CONSYS_REG_READ(conn_reg.mcu_base + CONSYS_CPUPCR_OFFSET);
}

static UINT32 consys_soc_chipid_get(VOID)
{
	return PLATFORM_SOC_CHIP;
}

static P_CONSYS_EMI_ADDR_INFO consys_soc_get_emi_phy_add(VOID)
{
	return &mtk_wcn_emi_addr_info;
}

P_WMT_CONSYS_IC_OPS mtk_wcn_get_consys_ic_ops(VOID)
{
	return &consys_ic_ops;
}

static INT32 consys_emi_coredump_remapping(UINT8 __iomem **addr, UINT32 enable)
{
	if (enable) {
		*addr = ioremap_nocache(gConEmiPhyBase + CONSYS_EMI_COREDUMP_OFFSET, CONSYS_EMI_MEM_SIZE);
		if (*addr) {
			WMT_PLAT_PR_INFO("COREDUMP EMI mapping OK virtual(0x%p) physical(0x%x)\n",
					   *addr, (UINT32) gConEmiPhyBase + CONSYS_EMI_COREDUMP_OFFSET);
			memset_io(*addr, 0, CONSYS_EMI_MEM_SIZE);
		} else {
			WMT_PLAT_PR_ERR("EMI mapping fail\n");
			return -1;
		}
	} else {
		if (*addr) {
			iounmap(*addr);
			*addr = NULL;
		}
	}
	return 0;
}

static INT32 consys_reset_emi_coredump(UINT8 __iomem *addr)
{
	if (!addr) {
		WMT_PLAT_PR_ERR("get virtual address fail\n");
		return -1;
	}
	WMT_PLAT_PR_INFO("Reset EMI(0xF0080000 ~ 0xF0080400) and (0xF0088400 ~ 0xF0090400)\n");
	/* reset 0xF0080000 ~ 0xF0080400 (1K) */
	memset_io(addr, 0, 0x400);
	/* reset 0xF0088400 ~ 0xF0090400 (32K)  */
	memset_io(addr + CONSYS_EMI_PAGED_DUMP_OFFSET, 0, 0x8000);
	return 0;
}
