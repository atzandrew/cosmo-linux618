# Wi-Fi (CONSYS) on the Cosmo: stock reference (W0)

Research only, no image. Sources: stock 4.4 tree
(`drivers/misc/mediatek/connectivity`, `arch/arm64/boot/dts/mediatek/mt6771.dts`,
`k71v1_64_bsp.dts`), the stock `vendor` partition (read with `debugfs -c`,
no mount), and live register reads under mainline 6.18 (S4b).

## Hardware

- D-die CONSYS in the MT6771 (WMT chip ID **0x0788**), A-die **MT6631**
  (gen3 Makefile `WLAN_CHIP_ID=MT6631` for `CONSYS_6771`).
- Stock config: `CONFIG_MTK_COMBO_CHIP_CONSYS_6771=y`, platform file
  `common/common_main/platform/mt6771.c`, Wi-Fi stack `wlan/core/gen3`,
  BTIF driver `drivers/misc/mediatek/btif`.

## Stock DT (mt6771.dts)

| Node | reg | IRQ (GIC SPI) | Clock / domain |
|---|---|---|---|
| `consys@18070000` (`mediatek,mt6771-consys`) | 0x18070000 (MCU cfg), 0x10007000/0x100 (TOPRGU), 0x10001000 (INFRACFG_AO), 0x10006000 (SPM) | 289 BGF_EINT, 291 WDT_EINT (level low) | `SCP_SYS_CONN` |
| `wifi@180f0000` (`mediatek,wifi`) | 0x180f0000/0x1100 (HIF), 0x10212180/0x6c (DMA), 0x10001000 (INFRACFG_AO), 0x180e0000/0x70 (CONN remap) | 290 HIF, 118 DMA | `INFRACFG_AO_CQ_DMA_CG`; `hardware-values` 0x788 / FW load+start 0x000a5800 |
| `btif@1100c000` | 0x1100c000/0x1000, TX DMA 0x11000a80/0x80, RX DMA 0x11000b00/0x80 | 114 BTIF, 139 TX DMA, 140 RX DMA | `INFRACFG_AO_BTIF_CG`, `INFRACFG_AO_AP_DMA_CG` |
| `consys-reserve-memory` | dynamic: size 0x200000, alignment 0x200000, alloc-ranges 0x40000000 + 0x80000000, no-map | | |

Board (`k71v1_64_bsp.dts`): `consys` pinctrl default is empty; GPS LNA on
GPIO155 (`WMT_SOC.cfg` has `wmt_gps_lna_enable=0`).

Note: the MT6797 BTIF DMA channels sit at 0x11000a00/0x11000a80; on the
MT6771 they are at 0x11000a80/0x11000b00.

## Stock power-on order (`mtk_wcn_consys_hw_reg_ctrl`, mt6771 ops)

`co_clock_type` = 0 (`consys_co_clock_type()` returns 0; `WMT_SOC.cfg`
`co_clock_flag=0`).

1. Reset EMI coredump area.
2. VCN18 on: `MT6358_LDO_VCN18_OP_EN` (0x1c5a) = 0x1, regulator 1.8 V;
   `MT6358_LDO_VCN33_OP_EN` (0x1d1e) = 0x1, regulator `vcn33_bt` **3.5 V**.
3. A-die pinmux (GPIO_MODE 0x10005000 + 0x3e0 / 0x3f0): pins 113-121 to
   mode 1 = `CONN_TOP_CLK/DATA, CONN_BT_CLK/DATA, CONN_WF_HB0-2,
   CONN_WB_PTA, CONN_HRST_B`. None of these pins is used in the mainline DT.
4. udelay(150).
5. Not co-clock: VCN28 HW0 op mode (0x1d8a bit 1 = 1, 0x1d90 bit 1 = 0),
   regulator VCN28 2.8 V on.
6. Assert CONN MCU SW reset: TOPRGU 0x10007018 |= BIT(12) | 0x88<<24.
7. CONN power domain on (`clk_prepare_enable(SCP_SYS_CONN)`); the
   non-API fallback documents the sequence: SPM 0x1000632c bits 2/3,
   acks 0x10006180/0x184 bit 1, clk_dis bit 4 = 0, ISO bit 1 = 0, rst
   bit 0 = 1, INFRA AXI prot 0x2a4 bits 13+14 released, then top2 ack
   0x1000632c bit 30.
8. udelay(10); poll chip ID 0x18070008 == **0x0788** (10 × 20 ms).
9. ROM desel: 0x18070600 = (0x17 << 16) | 0x2a4.
10. MCU ACR: 0x18070140 |= 0x3 (bypass at-speed MBIST).
11. AFE: 0x180b6038 = 0x000c1df0, 0x180b6054 = 0x110a2000.
12. Release CONN MCU SW reset; msleep(20).

EMI remap (at WMT init): INFRACFG_AO + 0x380 |=
`((base >> 21) & 0x1fff) | 0x2000`. Firmware sees the window at
0xf0080000 (`CONSYS_EMI_FW_PHY_BASE`). EMI MPU region 26 is set by stock
only with `CONFIG_MTK_EMI`; the gen3 Makefile has `CFG_ENABLE_EMI_MPU=0`
for 6771.

Stock does **not** use `vcn33_wifi` from the platform file (the
`consys_hw_wifi_vcn33_ctrl` body is `#if 0`).

## Mainline 6.18 mapping

- CONN domain: `mt8183-pm-domains.h` `MT8183_POWER_DOMAIN_CONN` has the
  same ctl 0x32c, sta 0x180/0x184 bit 1 and AXI prot BIT(13)|BIT(14) at
  INFRA 0x2a0/0x2a4/0x228. Only the top2 ack (bit 30) poll is not done by
  genpd; it can be read back afterwards.
- Regulators (`mt6358.dtsi`): `vcn18` fixed 1.8 V, `vcn28` fixed 2.8 V,
  `vcn33` 3.3-3.5 V (mainline merges VCN33_BT/WIFI into one regulator).
  Live stock under Gemian: vcn18 1.8, vcn28 2.8, vcn33_bt 3.5,
  vcn33_wifi 3.5 V (docs/power-stock.md).
- PMIC op-mode registers, live under mainline S4b: 0x1c5a = 0x0001,
  0x1d1e = 0x0001, 0x1d8a = 0x0001, 0x1c60/0x1d24/0x1d90 = 0. The
  VCN18/VCN33 values stock writes are already in place. Only the VCN28
  HW0 bit (0x1d8a bit 1, follow SRCLKEN in sleep) differs; not needed to
  power on.
- Pinmux: express step 3 as a pinctrl state on the consys node instead
  of a raw write.

## Firmware (stock `vendor/firmware`)

| File | Size | Used by |
|---|---|---|
| `ROMv4_be_patch_1_0_hdr.bin` | 228940 | WMT ROM patch (one per HW version) |
| `ROMv4_be_patch_1_1_hdr.bin` | 314120 | WMT ROM patch |
| `WIFI_RAM_CODE_6759` | 480432 | gen3: name = `WIFI_RAM_CODE` + chip ID, 0x6771 mapped to 0x6759 under `MT6631` (`gl_kal.c`) |
| `WMT_SOC.cfg` | 119 | `coex_wmt_ant_mode=1`, `wmt_gps_lna_pin=0`, `wmt_gps_lna_enable=0`, `co_clock_flag=0`, `bt_tssi_from_wifi=3`, `bt_tssi_target=3815` |

Stock finds the patches in userspace: the WMT core sends `srh_patch` to
`wmt_launcher`. ROMv4 patch addresses for 0x0788: `WMT_PATCH_ADDRESS_CMD`
[8..9] = 0x18 0x05, `WMT_PATCH_P_ADDRESS_CMD` [8..9] = 0x7c 0x0b
(`wmt_ic_soc.c`).

NVRAM: gen3 reads `/mnt/vendor/nvdata/APCFG/APRDEB/WIFI` (and
`WIFI_CUSTOM`). On this unit nvdata (mmcblk0p7) holds `WIFI` (1026 bytes),
`WIFI_CUSTOM` (6 bytes) and `BT_Addr`. Device-unique: copied from nvdata
at boot, never shipped in an image.

## Gemini port (gemini-nixos `mediatek-connectivity`) vs stock 4.4

- Based on the vendor 3.18 tree for MT6797/MT6630. No MT6631 code
  (0 hits; stock 4.4 gen3 has 50), no chip 0x0788 or 0x6771 paths, no
  ROMv4 patch addresses.
- Version gap: 31 k changed lines (whitespace ignored) in gen3 alone;
  `nic_cmd_event.h` (host/firmware command and event layout) has 1646 vs
  2173 lines. `WIFI_RAM_CODE_6759` is built for the 4.4 host code.
- Reusable: the mainline integration pattern (in-kernel answer to
  `srh_patch` with `request_firmware`, BTIF DMA transport, EMI window,
  glue for vendor-only APIs) and its 3.18 -> 6.6 API fixes, plus the
  6.6 -> 6.18 list in docs/hardware-map.md.

Consequence: port the stock 4.4 code (common_main, mt6771 platform
file, BTIF, gen3) to 6.18, using the Gemini port as the recipe.

## Old-kernel API in the stock code (common_main + gen3, 273 files)

`set_fs/get_fs` 20, `filp_open/vfs_read/vfs_write` 9 (firmware and NVRAM
file reads), wakelock 72, `struct timeval/timespec` etc. 43,
`ioremap_nocache` 30, `access_ok(VERIFY_*)` 4, old timers 8,
cfg80211 roam/scan/disconnect calls 14, wext 385 lines (can be compiled
out), MediaTek `KERNEL_*/aee_*` helpers.
