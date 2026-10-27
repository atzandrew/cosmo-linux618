# Stock reference values (Gemian 4.4 on this unit)

Read 2026-10-25 from Planet Gemian 4.4 (`4.4.146`) on the Cosmo, as an
unprivileged user (sysfs, procfs, dmesg). Used as the target for mainline
bring-up. Unit-specific where noted: MediaTek bins the CPU voltages per chip
(PTP-OD), so these tables are this unit's.

## CPU DVFS (`/proc/cpufreq/MT_CPU_DVFS_*/cpufreq_oppidx`)

Driver `mt-cpufreq`, governor `interactive`. Voltages are Vproc; the vendor
table prints them in 10 µV units (92500 = 0.925 V).

| OPP | Little (CPU0-3) kHz | V | Big (CPU4-7) kHz | V | CCI kHz | V |
|---|---|---|---|---|---|---|
| 0 | 1989000 | 0.92500 | 2106000 | 1.02500 | 1196000 | 0.92500 |
| 1 | 1924000 | 0.91250 | 1989000 | 0.98125 | 1144000 | 0.91250 |
| 2 | 1846000 | 0.88750 | 1924000 | 0.96875 | 1092000 | 0.89375 |
| 3 | 1781000 | 0.87500 | 1846000 | 0.95625 | 1027000 | 0.87500 |
| 4 | 1716000 | 0.86250 | 1781000 | 0.93750 | 962000 | 0.85625 |
| 5 | 1677000 | 0.85625 | 1716000 | 0.92500 | 923000 | 0.85000 |
| 6 | 1625000 | 0.83750 | 1651000 | 0.90625 | 871000 | 0.82500 |
| 7 | 1586000 | 0.83125 | 1586000 | 0.89375 | 845000 | 0.81875 |
| 8 | 1508000 | 0.81250 | 1508000 | 0.86875 | 767000 | 0.80000 |
| 9 | 1417000 | 0.79375 | 1417000 | 0.85625 | 689000 | 0.77500 |
| 10 | 1326000 | 0.76875 | 1326000 | 0.83125 | 624000 | 0.75625 |
| 11 | 1248000 | 0.73750 | 1248000 | 0.81250 | 546000 | 0.73125 |
| 12 | 1131000 | 0.70625 | 1131000 | 0.78125 | 481000 | 0.70625 |
| 13 | 1014000 | 0.66875 | 1014000 | 0.75625 | 403000 | 0.66875 |
| 14 | 910000 | 0.63750 | 910000 | 0.73125 | 338000 | 0.63750 |
| 15 | 793000 | 0.60000 | 793000 | 0.70625 | 273000 | 0.60000 |

Supplies: big = MT6358 `vproc11` + `vsram_proc11`; little and CCI = `vproc12`
+ `vsram_proc12` (at OPP0: big Vproc 1.025 V / Vsram 1.11875 V, little Vproc
0.925 V / Vsram 1.025 V). No external CPU buck: `spm_module_init
is_ext_buck_exist() 0x0`. Other rails at idle: vcore 0.800 V, vsram_others
0.900 V, vgpu 0.625 V, vsram_gpu 0.850 V.

Our kernel (no cpufreq yet): armpll_ll 1.1 GHz, armpll_l 1.2 GHz, ccipll
1.196 GHz, as LK leaves them.

## eMMC

Stock runs HS400 (`timing<10> mode<3>`) at 192 MHz from a 384 MHz source.
Card `hDEaP3`, 116 GiB. Our kernel: HS200 at 192 MHz, 169 MB/s sequential
read.

## I2C / SPI devices (bound driver in brackets)

| Bus-addr | Device | Stock driver | Notes |
|---|---|---|---|
| 0-0053 | solomon_touch | — | not fitted on this unit |
| 0-0062 | cap_touch | NVT-ts | Novatek touch (NT36672) |
| 1-000c | msensor | — | via SCP sensor hub |
| 1-0048 | alsps | — | STK3x1x, via SCP sensor hub |
| 1-0077 | barometer | — | via SCP sensor hub |
| 2-0033/0040/0054/0072 | main camera, CCU, EEPROM, AF | vendor camera | |
| 3-0025 | fusb301a | FUSB301_1 | second USB-C |
| 3-0028 | nfc | pn544 | NXP PN544 |
| 3-0039 / 3-0050 | sii9022_hdmi / siiedid | sii902xA (platform) | HDMI |
| 3-005b | aw9524_key | AW9524keyboard | keyboard expander |
| 4-0010/0011/0035/0036/0043/0051/0054/0072 | cameras 2 and sub, CCU, EEPROMs, AF | vendor camera | |
| 4-005b | aw9523_key | Integrated | keyboard expander |
| 5-0034 | subpmic_pmu | mt6370_pmu | MT6370 |
| 5-004e | usb_type_c | usb_type_c | MT6370 TCPC |
| spi accelgyro | accelgyrosensor | — | via SCP sensor hub |
| spi cs_finger | cs_finger | chipsailing | fingerprint |
| spi aw6302 | aw6302 | aw6302 | registers `cs_finger`; proc `AEON_AW6302` |

Sensor chips (accel, gyro, mag, baro, ALS) are driven by the SCP
sensor-hub firmware (`accel_hub`, `mag_hub`, `alsps_hub`, `gyro_hub`), so
their part numbers are not visible from Linux; identify them by chip-ID
registers. `rt5509_driver_init` runs but no RT5509 is bound: the speaker
amp may not be an RT5509 (or not fitted). Input devices: `ACCDET`,
`mtk-kpd`, `cf-keys`, `Integrated keyboard`, `mtk-tpd`; lid via
`mtk-hall`. Sound card: `mt-snd-card`.

## GPU DVFS (source, not read live)

`drivers/misc/mediatek/base/power/mt6771/mtk_gpufreq_core.h`. Two segment
tables, selected by efuse: SEG1 900 MHz max (Vgpu 0.900 V, Vsram_gpu
1.000 V at OPP0; 300 MHz / 0.625 V at OPP15) and SEG2 800 MHz max. Live
`/proc/gpufreq` is blocked by SELinux on stock Android. This unit's CPU
table (big 2106 MHz) is the P70 bin, which pairs with SEG1 (inferred, not
read). Idle vgpu 0.625 V matches SEG1 OPP15.

## Thermal policy (stock Android 9, `/vendor/etc/.tp/thermal.conf`)

Read with adb 2026-10-27 from Cosmo-9.0-Planet-02032021-V25. MediaTek
obfuscates the file: each byte is shifted by (position in line mod 10),
wrapping within 32..122. Active policy (`init.svc.thermal` running).

| Zone | Trips (°C → cooler) |
|---|---|
| tzcpu | 117 → system reset; 57 → cpu_adaptive_0; 56 → cpu_adaptive_1 (only while mtktsAP ≥ 69) |
| tzbts (board/AP NTC) | 100 → reset; 90 → shutdown; 53 → camera limit; 47/47.001 → abcct_lcmoff; 45/45.001 → abcct (charge current) |
| tzbtspa (PA NTC) | 120 → shutdown; 110 → modem off; 100 → no IMS; 68 → adaptive modem TX |
| tzbattery | 60 → reset |
| tzpmic | 136 → reset |
| tzpa, tzcharger, tzcharger2, tzwmt | 120 → reset/shutdown |
| tzts1..5 | 150 → shutdown (disabled, 0 trips) |

Notable parameters: `clctm` = `2 80000 57000 44000 47000 70000 51000 ...`
(CPU thermal-target table), `clabcct` target 46 °C (screen on) / 48 °C
(screen off), `clatm_setting` 0: `2000 10 15 1 635 0 250 0`, 1:
`1000 10 15 1 400 0 220 0`. Alternative policies `.thermal_policy_00/03/05/08`
and `.ht120.mtc` exist; `thermal.off.conf` equals `.ht120.mtc`.
