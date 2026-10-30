# CPU DVFS and CPU idle

Stock reference: Gemian 4.4 source (`drivers/misc/mediatek/base/power/
cpufreq_v1`, `eem_v2/mt6771`, `mcdi/mcdi_v1`, `drivers/cpuidle`) and the
values read on this unit (docs/stock-reference.md "CPU DVFS"). First
written 2026-10-29.

## Stock CPU DVFS path (Gemian 4.4 source, cpufreq_v1)

- mach/mt6771/mtk_cpufreq_platform.h:19-28: `CONFIG_MTK_TINYSYS_SSPM_SUPPORT`
  (=y in k71v1_64_bsp_defconfig:239) defines `CONFIG_HYBRID_CPU_DVFS` and
  `EEM_AP_SIDE`. So stock hands the PLL and PMIC writes to the SSPM: the
  kernel writes the per-cluster table into CSRAM 0x0011bc00 (dvfsp node,
  mt6771.dts:1337), sends IPI_DVFS_INIT_PTBL / IPI_DVFS_INIT /
  IPI_SET_CLUSTER_ON_OFF (mtk_cpufreq_hybrid.c:265-428, 515-556) and
  requests an OPP index per cluster in CSRAM (cpuhvfs_set_cluster_load_freq,
  OFFS_SCHED_S 0x3a4). CPU-CCI coupling happens inside SSPM firmware (not
  readable).
- Kernel-side sequence (used only without SSPM, and as the model for
  mainline): Vproc via regulator; Vsram write compiled out with the
  comment "HW auto-tracking" (mtk_cpufreq_platform.c set_cur_volt_sram_cpu).
  PLL: DDS in CON1 [21:0] + CHG bit 31, posdiv CON1 [26:24]; MCUCFG
  divider [21:17] (8 = /1, 10 = /2, 11 = /4) at 0x7a0 / 0x7a8 / 0x7c0.
- Vsram tracking on this unit, MT6358 read live under mainline (#40,
  regmap debugfs): VSRAM_PROC11/12_TRACKING_CON0..3 = 0x0006 0x1000 0x6340
  0x0000: tracking on, offset 16 steps = +100 mV, low bound 0x40 = 0.900 V,
  high bound 0x63 = 1.11875 V (LDO 0.5 V + 6.25 mV/step). Set by the
  bootloader. Vsram = clamp(Vproc + 100 mV, 0.900, 1.11875) reproduces
  every stock Vsram value (big OPP0 1.025 -> 1.11875, little OPP0 0.925 ->
  1.025). Mainline therefore drives Vproc only, as on MT8183 kukui (no
  sram-supply).
- This unit's base table is fy5tTbl (CPU_LEVEL_6, "V5_T",
  mtk_cpufreq_opp_pv_table.h:295-348): the only table with big 2106 MHz.
  Codes are 0.5 V + 6.25 mV/step. The voltages in stock-reference.md are
  after EEM: little top 1989 MHz base 1.000 V -> 0.925 V; big 793 MHz base
  0.675 V -> 0.70625 V (EEM moves both ways). CCI below 767 MHz runs with
  the MCUCFG divider at /2.

## Live clock state under mainline (#40, tools/cosmo-pwr/cosmo_cpuclk.c)

- LL: VCO 2.2 GHz /2, divider /1 -> 1.100 GHz. L: 2.4 GHz /2, /1 -> 1.200
  GHz. CCI: VCO 2.392 GHz /2 = ccipll 1.196 GHz, divider sel 10 (/2) ->
  **598 MHz**. All three Vproc/Vsram 0.800 / 0.900 V.
- clk_summary shows mcu_bus_sel 1.196 GHz: mainline clk-mt8183 has the
  MCUCFG mux (bits 10:9) but not the divider (bits 21:17). mtk-cci-devfreq
  would therefore get half of every CCI rate it sets until the divider is
  set to /1 (lower rate, not a voltage risk).

## Mainline vs stock

- mediatek-cpufreq (=y) and mtk-cci-devfreq (=m) already probe and stop at
  "failed to get proc regulator" (no proc-supply in our DTS).
- Order matches stock: voltage up before a rate rise, rate down before
  voltage down, reparent to the intermediate clock (armpll_div_pll1 =
  mainpll 1092 MHz) during the PLL change.
- While the CCI driver is not bound, mediatek-cpufreq never sets Vproc
  below the boot voltage (vproc_on_boot, mediatek-cpufreq.c:246).
- mt8183.dtsi tables: generic voltages, all higher than this unit's; big
  cluster uses the little list (has 1625/1677, lacks stock's 1651/2106);
  CCI has 463 MHz where stock has 481 MHz. Our DTS needs its own three
  tables.

## Stock CPU idle (MT6771)

- Driver cpuidle-mt67xx_acao_v3.c (drivers/cpuidle/Makefile:29): states
  WFI and "mcdi" (CPUs 0-3 residency 4500 us, 4-7 1500 us, exit 300 us,
  TIMER_STOP). mcdi_enter (mcdi_v1/mtk_mcdi.c:755) picks CPU off or
  cluster off; both end in arm_cpuidle_suspend with the DT states
  MCDI_CPU 0x00010001 / MCDI_CLUSTER 0x01010001 (mt6771.dts:154-180),
  the same PSCI params as mainline CPU_SLEEP / CLUSTER_SLEEP0.
- Stock also talks to the SSPM MCDI controller (mailbox 3): available CPU
  mask (mcdi_avail_cpu_cluster_update at init and on hotplug), cluster
  CAN_POWER_OFF before cluster-off, pause/ack around every hotplug.
  Mainline does none of this. Open: what the SSPM MCDI controller does by
  default (read mailbox 3 before any idle test).

## Stock EEM (= mainline SVS) bounds

- eem_v2/mt6771/mtk_eem.c eem_set_eem_volt (1480-1630): little (2L) and
  CCI are clamped to at most the base table (volt_tbl_orig); big (L) to
  at most base + 62.5 mV (MARGIN_ADD_OFF_VER4), never above 1.05 V.
- At or below 25 °C (INVERT_TEMP_VAL, mtk_eem_config.h:185; back above
  27 °C) EEM switches to its INIT2 table plus low_temp_off (0 for CPUs,
  LOW_TEMP_OFF_DEFAULT). The values in stock-reference.md are a warm
  snapshot; cold values may be higher, within the bounds above.

## Our table

Per OPP: the higher of the fy5tTbl base value and the value stock ran on
this unit. Base = 0.5 V + 6.25 mV x code.

| OPP | Little MHz | V | Big MHz | V | CCI MHz | V |
|---|---|---|---|---|---|---|
| 0 | 1989 | 1.00000 | 2106 | 1.05000 | 1196 | 1.00000 |
| 1 | 1924 | 0.97500 | 1989 | 1.02500 | 1144 | 0.97500 |
| 2 | 1846 | 0.95000 | 1924 | 1.00000 | 1092 | 0.95000 |
| 3 | 1781 | 0.92500 | 1846 | 0.97500 | 1027 | 0.92500 |
| 4 | 1716 | 0.89375 | 1781 | 0.95000 | 962 | 0.89375 |
| 5 | 1677 | 0.88125 | 1716 | 0.92500 | 923 | 0.88125 |
| 6 | 1625 | 0.85625 | 1651 | 0.90625 | 871 | 0.85625 |
| 7 | 1586 | 0.84375 | 1586 | 0.89375 | 845 | 0.84375 |
| 8 | 1508 | 0.81875 | 1508 | 0.86875 | 767 | 0.81875 |
| 9 | 1417 | 0.80000 | 1417 | 0.85625 | 689 | 0.80000 |
| 10 | 1326 | 0.76875 | 1326 | 0.83125 | 624 | 0.76875 |
| 11 | 1248 | 0.73750 | 1248 | 0.81250 | 546 | 0.73750 |
| 12 | 1131 | 0.70625 | 1131 | 0.78125 | 481 | 0.70625 |
| 13 | 1014 | 0.66875 | 1014 | 0.75625 | 403 | 0.66875 |
| 14 | 910 | 0.63750 | 910 | 0.73125 | 338 | 0.63750 |
| 15 | 793 | 0.60000 | 793 | 0.70625 | 273 | 0.60000 |

Each CPU OPP requires the CCI OPP with the same index (mainline passive
CCI governor). Stock's coupling is in SSPM firmware and not known.

## Steps

- C1: OPPs where the table needs at most 0.800 V (little 793-1417, big
  793-1131), both Vproc rails pinned at 0.800 V in the DTS, CCI without
  proc-supply (mtk-ccifreq unbound, CCI stays at LK's 598 MHz). Tests
  frequency switching only; no voltage change.
- C2: mtk-cci-devfreq first moves the divider /2 -> /1 at 598 MHz
  (ccipll 1196 -> 598 MHz, then the divider; the real rate never goes
  above 598 MHz), CCI intermediate clk26m instead of mainpll (1092 MHz
  undivided), CCI proc-supply on, pin still on.
- C3a / C3b: unpin vproc11, then vproc12, OPPs as C1 (down only, to
  stock's values; lowest 0.600 V = stock's floor).
- C4: full tables, constraints = each table's range.
- Idle (planned): read SSPM mailbox 3 (MCDI) first; CPU_SLEEP only
  before cluster states.
