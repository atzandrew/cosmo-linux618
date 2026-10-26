# Power foundation: how stock 4.4 drives charger, Type-C, battery, thermal and backlight

Written 2026-10-26. Reference: vendor kernel `gemian/cosmo-linux-kernel-4.4`
@ a741a2e (paths relative to it unless marked *mainline*), mainline v6.18.58.
Live values: the same unit under Gemian (stock 4.4.146, `/proc/config.gz`
matches `k71v1_64_bsp_defconfig` for every option below), USB connected to a
PC port, battery 91 %, 26 C. No code yet; this is the stock reference for the
MT6370 / battery / thermal work.

## Build (k71v1_64_bsp_defconfig)

| Option | Value | Line | Built |
|---|---|---|---|
| `CONFIG_MFD_MT6370_PMU` | y | 138 | `pmic/mt6370/mt6370_pmu_{i2c,regmap,irq,subdevs,core}.c` |
| `CONFIG_MT6370_PMU_CHARGER` | y | 139 | `mt6370_pmu_charger.c` (GAUGE_VERSION 30 branch of `pmic/mt6370/Makefile`) |
| `CONFIG_MT6370_PMU_BLED` | y | 141 | `mt6370_pmu_bled.c` |
| `CONFIG_TCPC_MT6370` | y | 245 | `usb_c/tcpc/tcpc_mt6370.c` + PD stack (`CONFIG_USB_POWER_DELIVERY`) |
| `CONFIG_USB_C_SWITCH_FUSB302` | y | 242 | `usb_c/fusb302/usb_typec.c` (second port, FUSB301A) |
| `CONFIG_MTK_SMART_BATTERY` + `CONFIG_MTK_GAUGE_VERSION=30` | y / 30 | 344, 347 | `power/mediatek/battery/` (GM3.0) + `power/mediatek/charger/` (`mtk_charger.c`, `mtk_switch_charging.c`, PE / PE2.0 / PE4.0) |
| `CONFIG_THERMAL` | y | 351 | `misc/mediatek/thermal/` (mt6771 + common zones/coolers) |
| `CONFIG_RT_REGMAP` | y (selected) | | debugfs `/sys/kernel/debug/rt-regmap/` |

All paths below under `drivers/misc/mediatek/` unless they start with
`drivers/power/` or `arch/`.

## MT6370 charger

I2C5 @ 0x34 (`subpmic_pmu@34`, live DT), IRQ GPIO10 (`k71v1_64_bsp.dts:884`).

### What stock programs, and when

DT `arch/arm64/boot/dts/mediatek/mt6370.dtsi:35-63` (the live DT matches it
value for value). `mt6370_chg_init_setting()` (`pmic/mt6370/mt6370_pmu_charger.c:3868`)
runs once at probe:

| Setting | DT | Register (field) | Live value | Decoded |
|---|---|---|---|---|
| Input limit select | | CHGCTRL2 0x12 [3:2] | 0x1b | 2 = AICR (`:3877`) |
| Charge enable / TE | `enable_te` | CHGCTRL2 [0], [4] | 0x1b | CHG_EN 1, TE_EN 1 |
| HW ILIM | | CHGCTRL3 0x13 [0] | 0x72 | off (`:3886`) |
| AICR | 3.2 A | CHGCTRL3 [7:2] | 0x72 | **1.5 A** now: the algorithm lowered it for a charging-host port (see below) |
| CV (VOREG) | 4.4 V | CHGCTRL4 0x14 [7:1] | 0x64 | **4.40 V** |
| MIVR | 4.4 V | CHGCTRL6 0x16 [7:1] | 0x0b | 4.4 V |
| ICHG | 2.05 A | CHGCTRL7 0x17 [7:2] | 0x38 | **1.5 A** now (algorithm, as AICR) |
| IEOC | 150 mA | CHGCTRL9 0x19 [7:4] | 0x1c | 150 mA |
| Safety timer | 12 h, enabled (`:3925`) | CHGCTRL12 0x1c [7:5], [1] | 0x82 | 12 h, TMR_EN 1 |
| Charger WDT | `enable_wdt` | CHGCTRL13 0x1d [7] | 0xd2 | WDT_EN 1 |
| JEITA (chip) | disabled (`:3937`) | CHGCTRL16 0x20 [4] | 0x00 | off |
| IR compensation | 25 mOhm / 32 mV | CHGCTRL18 0x2c | 0x49 | 25 mOhm, 32 mV |
| DC WDT | 4 s | | | `mt6370_set_dc_wdt` (`:1318`) |

Decode formulas: stock `pmic/mt6370/inc/mt6370_pmu_charger.h:26-97`
(ICHG 100 mA + n x 100 mA, AICR 100 mA + n x 50 mA, VOREG 3.9 V + n x 10 mV,
MIVR 3.9 V + n x 100 mV, IEOC 100 mA + n x 50 mA, WT_FC 4 h + n x 2 h).
*Mainline* `drivers/power/supply/mt6370-charger.c:132-137` uses the same
fields (its ICHG range starts at 0x08 = 900 mA; 0x38 decodes to 1.5 A in
both).

- WDT: init turns it **off** (`:3931`, "prevent 1mA power consumption");
  `mt6370_plug_in()` (`:2742`) turns it on when a charger is attached,
  `mt6370_plug_out()` (`:2715`) off again. The algorithm kicks it each
  10 s poll (`mt6370_kick_wdt`, `:2525`).
- Charger type detection (BC1.2) is done by the MT6370 (`CONFIG_MT6370_PMU_CHARGER_TYPE_DETECT=y`
  in the live config); init first disables it (`:3955`).

### Charger algorithm (`drivers/power/mediatek/charger/`)

DT `mt6771.dts:4106-4190` (`charger`, live DT identical; board override
`k71v1_64_bsp.dts:1029` only changes the dual-charger currents).

- `mtk_charger.c` thread, poll every `CHARGING_INTERVAL` = 10 s
  (`mtk_charger_intf.h:42`, `mtk_charger.c:2569`).
- Current by charger type, `swchg_select_charging_current_limit()`
  (`mtk_switch_charging.c:106`):

  | Type | Input (AICR) | Charge (ICHG) |
  |---|---|---|
  | SDP (PC USB) | 500 mA | 500 mA |
  | CDP (charging host) | 1.5 A | 1.5 A |
  | DCP (standard charger) | 3.2 A | 2.05 A, then PE / PE2.0 |
  | Non-standard | 2.05 A | 2.05 A |
  | Apple 1 A / 2.1 A | 650 / 800 mA | same |

  The live 1.5 A / 1.5 A matches the CDP row.
- `battery_cv` 4.4 V; `max_charger_voltage` 6.5 V (VBUS above it stops
  charging, `mtk_charger.c:1195-1222`).
- SW JEITA: **off** (`enable_sw_jeita` commented out). Instead,
  `charger_check_status()` (`mtk_charger.c:1325-1380`) stops charging below
  -10 C or at/above 55 C, and resumes above -10 C / below 50 C.
- Battery cell (label): 3.85 V nominal, 4220 mAh, charge voltage 4.4 V. So
  CV 4.4 V is the cell rating; LK's 4.35 V is more conservative.

### Live check on a wall charger (DCP)

Same day, Anker wall charger (USB-A port), detected as "Standard Charger"
(type 4); battery 97 %, 4.41 V (CV phase), +1.39 A, 28 C; VBUS 4.66 V (no
Pump Express step-up on this charger). Registers that differ from the PC
port dump:

| Reg | PC port (CDP) | Wall (DCP) | Decoded (DCP) |
|---|---|---|---|
| CHGCTRL3 0x13 | 0x72 | 0xfa | AICR 3.2 A (+ AICR_EN) |
| CHGCTRL6 0x16 | 0x0b | 0x0f | MIVR 4.6 V |
| CHGCTRL7 0x17 | 0x38 | 0x4c | ICHG 2.0 A (DT 2.05 A rounded down to the 100 mA step) |
| 0x22, 0x27, 0x4d, 0xb1 | 0x50, 0x60, 0xbf, 0x32 | 0xd4, 0x48, 0xbb, 0x7a | not decoded |
| status 0xd0, 0xd4, 0xd6, 0xe0 | 0xa0, 0x10, 0x00, 0xaf | 0x80, 0x14, 0x41, 0xef | |

CV stays 4.40 V, IEOC 150 mA, timer and TE as on the PC port.

### Live check on an Anker PowerPort mini (5 V / 2.4 A, Apple-compatible)

Detected as "Standard Charger" (type 4), not as an Apple charger: the
charger registers equal the DCP column above (AICR 3.2 A, ICHG 2.0 A,
MIVR 4.6 V, CV 4.40 V). Battery 97 %, 4.45 V, +1.59 A.

### Live check on a USB-C PD laptop charger

Same day, laptop USB-C PD charger; battery 97 %, 4.25 V, +0.69 A, 28 C.
TCPC log: contract 5 V / 1.0 A, 5 V / 1.5 A, then 11 V / 2.5 A and 7.65 V /
2.5 A (programmable steps), then back to 5 V / 2.5 A within 2 s: above
`pe40_stop_battery_soc` (80 %) stock leaves the higher voltages.

| Reg | Wall (DCP) | PD | Decoded (PD) |
|---|---|---|---|
| CHGCTRL2 0x12 | 0x1b | 0x0b | TE_EN **0** (termination off), CHG_EN 1 |
| CHGCTRL3 0x13 | 0xfa | 0xc2 | AICR 2.5 A = PD contract current |
| CHGCTRL7 0x17 | 0x4c | 0x74 | ICHG **3.0 A** |

ICHG 3.0 A is `pd_charger_current`, hard-coded at 3 A
(`drivers/power/mediatek/charger/mtk_charger.c:1661`) and used on the PD
path (`mtk_switch_charging.c:144-170`), above the 2.05 A that the DT gives
for a wall charger. CV stays 4.40 V, MIVR 4.6 V.

### Pump Express on the Anker (USB-A) charger

No step-up during the DCP check: `ta_stop_battery_soc` = 85 % (DT) and the
battery was at 97 %. The same charger gave 9 V on the Gemini PDA.

### Thermal limits on charge current

`thermal/common/coolers/mtk_cooler_bcct.c`; battery zone trips are set from
userspace (see Thermal). Live `/proc/driver/thermal/clbcct`:
`mtk-cl-bcct00` 550 mA, `01` 350 mA, `02` 150 mA (kernel defaults
1000 / 500 / 0 mA, `:366-368`, are overwritten). Live `clabcct`: a PID
loop with a battery target of 46 C, maximum 2000 mA.

## Type-C

### MT6370 TCPC (port 0)

I2C5 @ 0x4e (`usb_type_c@4e`), IRQ GPIO41 (`k71v1_64_bsp.dts:889`).
DT `mt6370_pd.dtsi`: `role_def` 4 = Try.SNK, `rp_level` 0 (default),
`vconn_supply` 1 (always), PD sink PDOs 5 V / 2 A fixed and
`0xC0761E3C`, source PDO 5 V / 0.5 A; `dpm_caps` include DR/PR swap and DP
alt mode.

Alerts are write-1-to-clear (`tcpc_mt6370.c:620-646`). Live registers
(rt-regmap `mt6370-4e/regs`, all reads, cache disabled `tcpc_mt6370.c:367`):

| Reg | Live | Meaning |
|---|---|---|
| VID / PID / DID 0x00-0x05 | 29cf / 5081 / 2486 | Richtek MT6370 TCPC |
| ROLE_CTRL 0x1a | 0x0a | CC1 = CC2 = Rd: sink |
| CC_STATUS 0x1d | 0x11 | |
| POWER_STATUS 0x1e | 0x0c | VBUS present, VBUS detect on |
| MT6370 vendor 0x80-0xa3 | raw below | |

### FUSB301A (second port)

`fusb301a@25` on I2C3, IRQ GPIO166 (`mt6771.dts:2026`), driver
`usb_c/fusb302/usb_typec.c` ("FUSB301_1", `:595`). It drives the HDMI path:
GPIO177 = switch enable, GPIO176 = switch select (`usb_typec.c:175-176,
338-339`), together with the SiI9022 HDMI bridge.

## Battery level

- Stock uses the MT6358 coulomb-counter gauge (GM3.0, `power/mediatek/battery/`).
  The kernel part only measures; the state-of-charge algorithm runs in
  **userspace**: `/vendor/bin/fuelgauged` in the Android container (live `ps`).
  `mtk_battery_recovery.c` is only the recovery-mode fallback.
- Profile: `bat_setting/mt6771_battery_prop.dtsi` + `mt6771_battery_table.dtsi`
  (100-point OCV tables per temperature). R_FG 10 mOhm (`:19`),
  CAR_TUNE 93 (`:23`), shutdown voltage 3.4 V (`:35`), pseudo-100 98 %.
- Battery NTC: 10 k, pull-up 24 k to 2.8 V, BIF_NTC_R 16 k
  (`include/mt-plat/mt6771/include/mach/mtk_battery_table.h:199-212`).
- Live `power_supply/battery`: capacity 91 %, 4.224 V, +1.16 A,
  `charge_full` 2946000 uAh, 26.0 C. The 2946 mAh does not match the
  4220 mAh cell: the profile looks like the generic MediaTek one.
- Same cell as the Gemini PDA (label 3.85 V / 4220 mAh / 4.4 V). Measured
  there under stock (gemini-nixos `f475d5f`): about 4050 mAh usable from
  empty to full at 4.4 V, resting voltage 4.34 V when full. To be confirmed
  on this unit.
- Battery temperature path: MT6358 AUXADC channel 3 (`BAT TEMP`, 12 bit,
  divider 2, `pmic/mt6358/v1/pmic_auxadc.c:351`), converted as a plain
  divider R_NTC = 24 k x V / (VBIF28 - V) (`power/mediatek/battery/mtk_battery.c:1047-1100`;
  BIF_NTC_R is not used on this path). Live: AUXADC_ADC3 (0x108e) =
  0x837b -> 891 counts = 783 mV -> 9.3 kOhm -> about 26.5-27 C on the 10 k
  table; stock reported 27.0 C at the same time.
- *Mainline*: no MT6358 gauge driver. Available: `mt6359-auxadc.c`
  (`mediatek,mt6358-auxadc`: batadc, bat_temp, vbif ...) and the MT6370 ADC
  (`mt6370-adc.c`: vbat, ibat, ts_bat, temp_jc).

## MT6358 fuel gauge hardware (for a mainline gauge)

Stock HAL: `pmic/mt6358/v1/mt6358_gauge.c`; register map
`include/mt-plat/mt6771/include/mach/upmu_hw.h:526-585,6573-6884`.

- Enabled before Linux: FGADC_CON0 (0xd08) = 0x2529 live (FG_ON, FG_CAL 2,
  AUTOCALRATE 2, SON_SLP_EN, ZCV_DET_EN). The kernel never turns it on or
  resets it in normal use.
- Read handshake (`fgauge_read_current` `:882`, `fgauge_get_coulomb`
  `:1098`): FGADC_CON1 (0xd0a) [3:0] := 0x1 (SW_READ_PRE), wait for
  LATCHDATA_ST (bit 15) = 1, read, [3:0] := 0x8 (SW_CLEAR), wait for bit 15 =
  0, [3:0] := 0x0. These are the only writes needed to read.
- Current: FG_CURRENT_OUT = FGADC_CUR_CON0 (0xd8a), 16-bit two's complement,
  + = charging; 381.470 uA per LSB (`UNIT_FGCURRENT`, `:47`).
- Charge (CAR): 20 bits + sign: `(CAR_CON0 >> 11) | (CAR_CON1 & 0x7fff) << 5`,
  sign = CAR_CON1 bit 15 (0xd14 / 0xd16); 108.507 uAh per LSB
  (`UNIT_FGCAR` = 190.735 uAs x 2^11 / 3600, `:49`).
- Calibration: R_FG 10 mOhm (r_fg_value 100 = no scaling), CAR_TUNE 930 =
  x 0.930 on current and charge. Gemian log: "Get car_tune_value from DT:
  930", "Get r_fg_value from DT: 100"; no runtime override
  (`Set_CARTUNE_TO_KERNEL`, `power/mediatek/battery/mtk_battery.c:3097,3350`) seen.
- Check against the live dump: CUR_CON0 0x0b8e = 2958 -> 2958 x 381.47 uA x
  0.93 = 1.049 A (stock reported 1.003 A); CAR 0x00c8:0x8823 -> 6417 LSB ->
  +648 mAh.
- LK hands a boot-time reading to the kernel in `/chosen`: `atag,fg_swocv_v`
  (38619 = 3.8619 V) and `atag,fg_swocv_i` (-716 = -71.6 mA) on this boot.
- Cell curve: the 25 C profile LK reads (our DTS `battery` node,
  `battery0_profile_t1`, mAh x 10 / mV x 10 / R) gives 4276 mAh to 3.4 V,
  starts at 4.365 V, and is within 20 mV of the Gemini PDA vendor 25 C curve
  at every 10 % step (same cell). The kernel profile's 2946 mAh
  (`charge_full`) is not this cell.

## Thermal

Kernel defaults are in `thermal/mt6771/inc/tz*_initcfg.h`; the live trips
come from `/vendor/bin/thermal` (Android container) reading
`/vendor/etc/.tp/thermal.conf` (obfuscated). Live values
(`/sys/class/thermal`, `/proc/driver/thermal/tz*`):

| Zone | Live trips (C) and cooler | Kernel default |
|---|---|---|
| mtktscpu | 117 sysrst, 57 cpu_adaptive_0, 56 cpu_adaptive_1; poll 200 ms | 117 sysrst, 95, 85, 65 (`tzcpu_initcfg.h`) |
| mtktsbattery | 60 sysrst | 49/48/46 bcct (`tzbatt_initcfg.h`) |
| mtktsAP (bts) | 100 sysrst, 90 shutdown, 53 cam, 47/45 abcct | |
| mtktscharger, charger2 | 120 sysrst | |
| mtktspmic | 136 | |
| mt6358tsbuck1-3 | 150 sysrst | |
| mtktspa, mtktswmt | 120 | |
| tzts1-5 (SoC sensors) | none (raw) | |

- Hardware protection: `tscpu_config_all_tc_hw_protect(trip_temp[0], ...)`
  (`thermal/common/thermal_zones/mtk_ts_cpu.c:469,2598`,
  `thermal/mt6771/src/mtk_tc.c:1278`) arms the thermal controller's HW reset
  at trip 0 = 117 C.
- *Mainline* `drivers/thermal/mediatek/auxadc_thermal.c` has mt8183 data
  (same thermal controller generation) but does not arm a HW reset.
- Live temperatures (idle, charging): CPU 37.5, battery 26, charger 38,
  PMIC 36.9, buck1-3 35 C.

## Backlight (MT6370 BLED)

DT `mt6370.dtsi:132-197` (live identical): 4 channels (`chan_en` 0xF),
OVP 29 V, OCP 1.5 A, PWM input on (`use_pwm`, fsample 24 MHz, deglitch
100 ns, hysteresis on), ramp 3 (750 us), flash ramp 500 us, linear mapping,
`max_bled_brightness` 768 of 2047, current scale 0 (30 mA full scale).
Init: `mt6370_pmu_bled.c:408-437`; brightness:
`bright = max x 256 / 255 x level >> 8`, written to BLDIM2 / BLDIM1
(`:301-319`).

Live: BLEN 0xa0 = 0x7e, BLBSTCTRL 0xa1 = 0xed, BLPWM 0xa2 = 0xb4 (PWM on),
BLCTRL 0xa3 = 0x30, BLDIM2/1 0xa4/0xa5 = 0x07 / 0x5f (about 760 of 768),
BLMODECTRL 0xad = 0x00. The panel brightness is therefore the SoC DISP_PWM
duty times this dim level.

## MT6358 regulators (closes the safety-audit regulator item)

Live `/sys/class/regulator/*/microvolts`. Every fixed rail in *mainline*
`mt6358.dtsi` matches: vrf12 1.2, vio18 1.8, vcamio 1.8, vcn18 1.8,
vfe28 2.8, vcn28 2.8, vxo22 2.2, vaux18 1.8, vbif28 2.8, vio28 2.8,
va12 1.2, vrf18 1.8, vaud28 2.8 V. Bucks under Gemian at idle: vcore 0.8,
vproc11 1.025, vproc12 0.925, vgpu 0.625, vmodem 0.65, vs1 2.0, vs2 1.35,
vdram1 1.125, vsram_proc11 1.11875, vsram_proc12 1.025, vsram_gpu 0.85,
vsram_others 0.9 V. Full list below.

## How the values were read (no writes)

- MT6370 PMU: per-register read-only debugfs files
  `/sys/kernel/debug/rt-regmap/mt6370_pmu/reg0xXX` (`S_IRUGO`, handler
  `eachreg_read`, `rt-regmap/rt-regmap.c:1816,1890`). Skipped 0x0b
  (IRQIND) and 0xc0-0xcf (IRQ events): the IRQ handler reads those to clear
  them (`pmic/mt6370/mt6370_pmu_irq.c:245,250`). Most charger registers are
  `RT_VOLATILE` (read from the chip, `mt6370_pmu_regmap.c`).
- MT6370 TCPC: `rt-regmap/mt6370-4e/regs` (`rt_show_regs`, block reads
  only, `rt-regmap.c:1299`); all TCPC IRQ registers are write-1-to-clear.
- Everything else: sysfs, `/proc/driver/thermal`, `/proc/device-tree`.

## Raw dumps (2026-10-26, Gemian 4.4.146)

MT6370 PMU (`reg:value`):

```
reg0x00:0xe6 reg0x01:0x16 reg0x02:0x00 reg0x03:0x00 reg0x04:0x00 
reg0x07:0x00 reg0x08:0x69 reg0x09:0xc3 reg0x0a:0x3c reg0x0b:skipped 
reg0x0c:0x00 reg0x0d:0x00 reg0x0e:0x00 reg0x0f:0xf0 reg0x10:0x04 
reg0x11:0x10 reg0x12:0x1b reg0x13:0x72 reg0x14:0x64 reg0x15:0x67 
reg0x16:0x0b reg0x17:0x38 reg0x18:0xa1 reg0x19:0x1c reg0x1a:0x58 
reg0x1b:0x2c reg0x1c:0x82 reg0x1d:0xd2 reg0x1e:0x05 reg0x1f:0x06 
reg0x20:0x00 reg0x21:0x10 reg0x22:0x50 reg0x23:0x00 reg0x24:0x21 
reg0x25:0xa2 reg0x26:0x00 reg0x27:0x60 reg0x28:0x00 reg0x29:0x00 
reg0x2a:0x60 reg0x2b:0x00 reg0x2c:0x49 reg0x2d:0x58 reg0x2e:0xe1 
reg0x2f:0x24 reg0x30:0x00 reg0x31:0x00 reg0x33:0x00 reg0x35:0x00 
reg0x36:0x00 reg0x37:0x00 reg0x38:0x00 reg0x3e:0x00 reg0x4a:0x60 
reg0x4b:0x00 reg0x4c:0x00 reg0x4d:0xbf reg0x4e:0x00 reg0x4f:0x00 
reg0x50:0x00 reg0x51:0x00 reg0x52:0x03 reg0x53:0x02 reg0x60:0x01 
reg0x61:0x00 reg0x62:0xa0 reg0x70:0x81 reg0x72:0x00 reg0x73:0x0b 
reg0x74:0x58 reg0x75:0x16 reg0x76:0x00 reg0x78:0x48 reg0x79:0x0e 
reg0x7a:0x37 reg0x7b:0x37 reg0x7c:0x01 reg0x7e:0x00 reg0x80:0x48 
reg0x81:0x09 reg0x82:0x40 reg0x83:0x40 reg0x84:0x40 reg0x85:0x0f 
reg0x86:0x08 reg0x87:0x08 reg0x88:0x08 reg0x89:0x52 reg0x8a:0x25 
reg0x8b:0x11 reg0x8c:0x52 reg0x8d:0x25 reg0x8e:0x11 reg0x8f:0x52 
reg0x90:0x25 reg0x91:0x11 reg0x92:0xc0 reg0x93:0x07 reg0x94:0x52 
reg0x95:0x25 reg0x96:0x11 reg0x97:0xff reg0x98:0x00 reg0x99:0x00 
reg0x9a:0x00 reg0x9f:0x00 reg0xa0:0x7e reg0xa1:0xed reg0xa2:0xb4 
reg0xa3:0x30 reg0xa4:0x07 reg0xa5:0x5f reg0xa6:0x00 reg0xa7:0x08 
reg0xa8:0x8c reg0xa9:0x80 reg0xaa:0xff reg0xab:0x00 reg0xad:0x00 
reg0xb0:0x00 reg0xb1:0x32 reg0xb2:0xe2 reg0xb3:0x5c reg0xb4:0x5c 
reg0xc0:skipped reg0xc1:skipped reg0xc2:skipped reg0xc3:skipped 
reg0xc4:skipped reg0xc5:skipped reg0xc6:skipped reg0xc7:skipped 
reg0xc8:skipped reg0xc9:skipped reg0xca:skipped reg0xcb:skipped 
reg0xcc:skipped reg0xcd:skipped reg0xce:skipped reg0xcf:skipped 
reg0xd0:0xa0 reg0xd1:0x00 reg0xd2:0x00 reg0xd3:0x00 reg0xd4:0x10 
reg0xd5:0x01 reg0xd6:0x00 reg0xd7:0x00 reg0xd8:0x08 reg0xd9:0x00 
reg0xda:0x00 reg0xdb:0x00 reg0xdc:0x00 reg0xdd:0x00 reg0xde:0x00 
reg0xdf:0x00 reg0xe0:0xaf reg0xe1:0x7f reg0xe2:0xff reg0xe3:0xf7 
reg0xe4:0xf6 reg0xe5:0xff reg0xe6:0x7e reg0xe7:0xff reg0xe8:0xef 
reg0xe9:0x37 reg0xea:0xff reg0xeb:0x1f reg0xec:0x7f reg0xed:0x00 
reg0xee:0xbf reg0xef:0x07
```

MT6370 TCPC (16-bit registers are little-endian byte pairs):

```
reg0x00:0xcf,29 reg0x02:0x81,50 reg0x04:0x86,24 reg0x06:0x11,00 
reg0x08:0x10,30 reg0x0a:0x10,10 reg0x10:0x00,00 reg0x12:0x7f,06 
reg0x14:0x04 reg0x15:0x82 reg0x19:0x00 reg0x1a:0x0a reg0x1b:0x00 
reg0x1c:0x10 reg0x1d:0x11 reg0x1e:0x0c reg0x1f:0x00 reg0x23:0x00 
reg0x2e:0x02 reg0x2f:0x00 reg0x30:0x00 reg0x31:0x00 reg0x32:0x00,00 
reg0x34:0x00 reg0x50:0x05 reg0x51:0x00 reg0x52:0x00,00 reg0x54:0x00 
reg0x80:0x71 reg0x82:0x70 reg0x87:0x8c reg0x88:0x10 reg0x90:0x07 
reg0x93:0x81 reg0x97:0x80 reg0x98:0x00 reg0x99:0x03 reg0x9a:0x01 
reg0x9b:0x3a reg0x9c:0x01 reg0x9d:0x04 reg0x9e:0x8f reg0xa0:0x00 
reg0xa1:0x05 reg0xa2:0x04 reg0xa3:0x4a
```

MT6358 regulators (`name uV state`):

```
dsv_neg 5400000 disabled; dsv_pos 5400000 disabled; irtx_ldo 3400000 
disabled; va09 900000 enabled; va12 1200000 enabled; vaud28 2800000 
enabled; vaux18 1800000 enabled; vbif28 2800000 enabled; vcama1 2800000 
disabled; vcama2 2800000 disabled; vcamd 1200000 disabled; vcamio 1800000 
disabled; vcn18 1800000 enabled; vcn28 2800000 enabled; vcn33_bt 3500000 
enabled; vcn33_wifi 3500000 enabled; vcore 800000 enabled; vdram1 1125000 
enabled; vdram2 600000 enabled; vefuse 1800000 disabled; vemc 3000000 
enabled; vfe28 2800000 disabled; vgpu 625000 enabled; vibr 2800000 
disabled; vio18 1800000 enabled; vio28 2800000 enabled; vldo28 2800000 
disabled; vmc 3000000 disabled; vmch 3000000 disabled; vmodem 650000 
enabled; vpa 500000 disabled; vproc11 1025000 enabled; vproc12 925000 
enabled; vrf12 1200000 disabled; vrf18 1800000 disabled; vs1 2000000 
enabled; vs2 1350000 enabled; vsim1 3000000 disabled; vsim2 3000000 
disabled; vsram_gpu 850000 enabled; vsram_others 900000 enabled; 
vsram_proc11 1118750 enabled; vsram_proc12 1025000 enabled; vusb 3000000 
enabled; vxo22 2200000 enabled
```

MT6358 fuel gauge and AUXADC (`addr value`, read through `pmic_access`
with a bare address: `store_pmic_access` -> `pmic_read_interface`,
`pmic/common/upmu_debugfs.c`; battery 95 %, 4.314 V, +1.0 A, 27.0 C):

```
0x0c80 0x4222 0x0c82 0x1210 0x0c84 0xc00 0x0c86 0x0 0x0c88 0x0 0x0c8a 0x504 
0x0c8c 0x3 0x0c8e 0x412 0x0c90 0x0 0x0d08 0x2529 0x0d0a 0x0 0x0d0c 0x0 
0x0d0e 0x308 0x0d10 0x80 0x0d12 0x1 0x0d14 0x8823 0x0d16 0xc8 0x0d18 0x0 
0x0d1a 0x0 0x0d1c 0x0 0x0d1e 0x0 0x0d20 0x82d8 0x0d22 0xc4 0x0d24 0x9253 
0x0d26 0xcb 0x0d28 0x113c 0x0d2a 0xffff 0x0d2c 0x5c73 0x0d2e 0xfadc 0x0d30 
0x1ad5 0x0d32 0x8000 0x0d34 0xfdf 0x0d36 0x0 0x0d38 0x1d16 0x0d3a 0x0 
0x0d3c 0xf8e 0x0d3e 0x0 0x0d40 0x0 0x0d42 0x0 0x0d44 0x1f 0x0d46 0x14 
0x0d48 0xff 0x0d4a 0x3 0x0d4c 0x1f 0x0d4e 0x14 0x0d50 0xff 0x0d52 0x0 
0x0d54 0x0 0x0d56 0x5443 0x0d58 0x95 0x0d5a 0x6 0x0d5c 0x0 0x0d5e 0x0 
0x0d60 0x0 0x0d62 0x979d 0x0d64 0x3 0x0d88 0xad9 0x0d8a 0xb8e 0x0d8c 0xc680 
0x0d8e 0xcf28 0x0d90 0xd60 0x0d92 0x55 0x0d94 0x0 0x0d96 0x400 0x0d98 0x0 
0x0e0a 0x0 0x1088 0xe628 0x108a 0x0 0x108c 0x8478 0x108e 0x837b 0x1090 
0x862f 0x1092 0x0 0x1094 0x0 0x1096 0xb6a3 0x1098 0x0 0x109a 0xe99a 0x109c 
0x0 0x109e 0x8c7c 0x10a0 0x0 0x10a2 0x8cad
```
