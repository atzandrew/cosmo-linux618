# Power foundation: plan (2026-10-26)

Stock reference: docs/power-stock.md. Mainline v6.18.58.

## What mainline gives us

| Block | Mainline driver | Writes at probe | Gap vs stock |
|---|---|---|---|
| MT6370 core | `mfd/mt6370.c` (`mediatek,mt6370`, 0x34; TCPC as dummy client 0x4e) | IRQ masks (regmap-irq, CHG_MASK1..), reads vendor ID | none |
| MT6370 ADC | `iio/adc/mt6370-adc.c` | `CHG_ADC` (0x121) per conversion, 0 at probe | none (vbus, ibus, vbat, ibat, ts_bat, temp_jc) |
| Charger | `power/supply/mt6370-charger.c` | ICHG := 900 mA, IINLMTSEL := AICR, ILIM off, USBCHGEN off; BC1.2 on attach | **no DT currents/CV, no AICR by charger type, no temperature stop, no WDT/timer handling**: LK's AICR 500 mA / CV 4.35 V / 12 h timer stay |
| Battery temp | `iio/adc/mt6359-auxadc.c` (`mediatek,mt6358-auxadc`, bat_temp, channel 3 as stock) + NTC conversion | AUXADC requests | needs the stock conversion: 10 k NTC, 24 k pull-up to VBIF28 (2.8 V), stock R-T table |
| Battery level | none for MT6358 | | **no gauge**: needs a port of the coulomb-counter approach (cell data: same cell as the Gemini) |
| SoC thermal | `thermal/mediatek/auxadc_thermal.c` (mt8183 data) | thermal controller + APMIXED TS buffer | no HW reset at 117 C; no cpufreq yet, so only critical (shutdown) trips work |
| Backlight | `video/backlight/mt6370-backlight.c` | BLED block | display rule: ask first |
| Type-C | `usb/typec/tcpm/tcpci_mt6370.c` + TCPM | TCPC init | later |

Stays OFF: `REGULATOR_MT6370` (DSV / LDO; panel bias area), `LEDS_MT6370_*`, flash.

## Stages (each its own image, pre-flash check, explicit yes)

### S1: measure only
MFD + ADC + MT6358 AUXADC + efuse nvmem (`MTK_EFUSE`). No charger driver.
- Checks, all against today's Gemian values: MT6370 VBAT/IBAT vs gauge;
  bat_temp vs 26 C; temp_jc vs mtktscharger 38 C; efuse 0x180..0x18b ==
  `0x02954977 0xa21e314a 0xa754aa20` (stock devinfo 100/101/102).
- Writes: IRQ masks, ADC control. Charger setup untouched (verify CHGCTRL
  registers unchanged before/after via regmap debugfs).

**S1 result 2026-10-26: PASS** (`boot-s1.img` 91dcb53d…, docs/session-log.md).
Efuse check also PASS (read on try30, before S1).

### S2: SoC thermal, critical shutdown only
`MTK_SOC_THERMAL`, only if the efuse check passed. mt8183.dtsi zones; critical
trip -> orderly power-off (power-off works since try30).
- Pass: zone temps plausible (idle ~35-40 C as tzts1-5 under Gemian);
  emulated critical temperature (`THERMAL_EMULATION`, `emul_temp`) powers
  the Cosmo off cleanly.

**S2 result 2026-10-26: PASS** (`boot-s2.img` 480893fc…, docs/session-log.md).
Mainline 6.18 registers one zone only, `cpu-thermal` = the hottest of the six
sensors, critical 115 C (stock: 117 C hardware reset on the same sensors).
The other `mt8183.dtsi` zones are not bound. TSABB is read with mux 0
(= TS1); stock uses 0x10 (`tscpu_settings.h:178`).

### S3: charger as stock
Enable `CHARGER_MT6370` plus a small whole-file delta of
`mt6370-charger.c` (pattern: mt6358-poweroff.c), values from DT:
- CV 4.40 V (cell label + stock); IEOC 150 mA; safety timer kept 12 h on.
- Input/charge current by BC1.2 type, stock table: SDP 500/500, CDP
  1500/1500, DCP 3200/2050, other 2050/2050 mA.
- Battery temperature stop (stock `mtk_charger.c:1325`): off below -10 C or
  at >= 55 C, back on at >= 0 C / below 50 C; poll 10 s. No current caps:
  stock's bcct 550/350/150 mA coolers are not bound on Gemian; its heat
  limiting is a PID loop on the board thermistor (docs/power-stock.md),
  left for later. A battery thermal zone gives a critical power-off at 60 C.
- Charger WDT: off, as LK leaves it (the chip's 12 h safety timer and
  thermal regulation stay on).
- Pass/fail: per charger type, registers equal stock's (docs/power-stock.md)
  within one step; heat test by emulated battery temperature stops charging.

**S3 result 2026-10-26: PARTIAL** (`boot-s3.img` bd355939…,
docs/session-log.md). CV/IEOC/TE/timer and the temperature stop/resume
work. BC1.2 detection does not: every port (PC host and two wall-charger
ports) reads "non-standard, DCD timeout", so the input stays at 500 mA.
Likely cause: detection runs with the SoC USB PHY still attached (stock
detaches it with `Charger_Detect_Init`/`Release`). Open as S3b.

### S4: battery level
New module `mt6358-gauge` (power_supply, type Battery) reading the gauge
as stock does (docs/power-stock.md "MT6358 fuel gauge hardware"). Writes:
only the FGADC_CON1 read handshake; never FG_ON, CAR reset or calibration.
- Measurements: current (CUR_CON0), charge (CAR), voltage (MT6358 AUXADC
  BATADC), temperature (AUXADC channel 3, stock conversion).
- State of charge: start from LK's boot OCV (`/chosen/atag,fg_swocv_v`)
  through the 25 C curve, then count charge; re-anchor at full (taper
  below IEOC at CV); learn capacity (start 4050 mAh, the value measured on
  the same cell in the Gemini, bounded); low-voltage caps at 3.45 / 3.4 V.
- Pass/fail: current within 5 % of the MT6370 IBAT reading and of a USB
  meter; charge counted over a known charge session within 5 %; percent
  never jumps by more than 2 points without a full/empty anchor.

### S5 backlight, S6 Type-C
After the display/USB plans; not covered by this plan.

## Escapes for every stage
Power-key 8 s reset and the init-owned watchdog, both observed on the image
before it is left alone. Rollback: try30 boot image.

## Precondition
Gemian stays installed until the stock reads for S3/S4 are done. Then
mainline Debian is reinstalled: rebuild the gemini-debian cosmo rootfs on
Hydra (`DEVICE=cosmo`), flash it with try30.
