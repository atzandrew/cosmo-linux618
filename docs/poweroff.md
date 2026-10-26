# Power-off: stock 4.4 path and the mainline gap

Written 2026-10-26. Reference: vendor kernel `gemian/cosmo-linux-kernel-4.4`
@ a741a2e (paths below are relative to it unless marked *mainline*),
mainline v6.18.58, Cosmo LK image from the P0 backup (strings only).
No code yet; this is the stock reference for a power-off handler.

## Symptom (mainline)

`systemctl poweroff` reboots back into Linux, with or without USB.

## Stock config (k71v1_64_bsp_defconfig, the Cosmo build: `debian/rules:16`)

| Option | Value | Line |
|---|---|---|
| `CONFIG_MTK_PMIC_CHIP_MT6358` | y | 135 |
| `CONFIG_MTK_RTC` | y | 237 |
| `CONFIG_MTK_SMART_BATTERY` | y | 344 |
| `CONFIG_MTK_KERNEL_POWER_OFF_CHARGING` | y | 346 |
| `CONFIG_POWER_EXT` | not set | |
| `CONFIG_MTK_WATCHDOG_COMMON_V2` | y | 357 |

So both `#ifdef CONFIG_MTK_SMART_BATTERY` blocks below are built, and the
`!CONFIG_POWER_EXT` (phone) branch of `mt_power_off` runs.

## Who owns `pm_power_off`

- `drivers/firmware/psci.c:526-528`: PSCI sets `pm_power_off =
  psci_sys_poweroff` from `setup_arch` (`arch/arm64/kernel/setup.c:342`).
- `drivers/misc/mediatek/rtc/mtk_rtc_common.c:858-862`: `rtc_arch_init`
  (`arch_initcall`, later) overwrites it with `mt_power_off`. Stock never
  calls PSCI SYSTEM_OFF.
- `arch/arm64/kernel/process.c:129-137`: `machine_power_off` stops the
  other CPUs and calls `pm_power_off`.

## `mt_power_off` (`mtk_rtc_common.c:428-459`)

1. `rtc_bbpu_power_down()` (`:407-426`): reads charger presence
   (`mtk_chr_is_charger_exist`), then, under `rtc_lock`,
   `hal_rtc_bbpu_pwdn(charger_status)`. The MT6358 HAL ignores the argument.
2. Loop: `mdelay(100)`; if a charger is present, **or after 11 passes
   (~1.1 s) without one**, `arch_reset(0, "charger")`.

So stock expects the PMIC to cut power within ~1 s. With a charger (USB
counts) the PMIC stays up (charger-in is a power-on source) and the
fallback reset hands over to LK; without one the same fallback runs if
power has not dropped.

## `hal_rtc_bbpu_pwdn` (`rtc/mt6358/mtk_rtc_hal.c:249-279`)

All RTC registers go through the PMIC wrapper (`mtk_rtc_hal_common.c:46-57`);
`rtc_write_trigger` = write `RTC_WRTGR` = 1, then poll `RTC_BBPU.CBUSY`
clear, 500 ms timeout (`:59-79`).

| Step | Source | Registers (MT6358 absolute address) |
|---|---|---|
| 1 `rtc_disable_2sec_reboot` | `:235-242` | `RTC_AL_SEC` 0x5a0: clear `BBPU_2SEC_EN` (bit 8), `BBPU_AUTO_PDN_SEL` (bit 6); trigger |
| 2 `rtc_enable_k_eosc` | `:185-233` | `SCK_TOP_CKPDN_CON0_CLR` bit 2 (EOSC32 clock on); `SRCLKEN_IN0/IN1_HW_MODE` = 1; `RG_RTC_EOSC32_CK_PDN` = 0; `EOSC_CALI_TD` = 6 (default `rtc_eosc_cali_td` 8); `XO_EN32K_MAN` (DCXO_CW02 bit 0) = 0; `RTC_BBPU` \|= KEY\|RELOAD, trigger; `RTC_AL_YEA` 0x5ac: set bits 8, 10, clear bit 9 (`K_EOSC_RSV_0/2`, `~RSV_1`), trigger; `RTC_OSC32CON` 0x5ae \|= `EMBCK_SRC_SEL` (bit 8) via `rtc_xosc_write(…, true)` (unlock1, unlock2, value, each with CBUSY wait, then BBPU KEY\|RELOAD + trigger; `mtk_rtc_hal_common.c:98-115`) |
| 3 32 k export off | `:256-261` | if `RTC_PDN1` 0x5b4 has no `RTC_GPIO_USER` bits (8-12): `RTC_CON` 0x5c4 \|= `F32KOB` (bit 5), trigger |
| 4 BBPU | `:264-265` | `RTC_BBPU` 0x588 = `KEY (0x43<<8) \| CLR (bit 1) \| PWREN (bit 0)` |
| 5 alarm mask | `:267-268` | `RTC_AL_MASK` 0x590 = `DOW` (bit 4) only; trigger |
| 6 spare/alarm clear | `:171-183` | poll `RTC_BBPU.CLR` until 0, 500 ms timeout |
| 7 shutdown delay | `pmic/mt6358/v1/pmic.c:73-78` | `TOP_TMA_KEY` 0x3a8 = 0x9CA7; `PSEQ_ELR11` 0xa62 bit 10 (`RG_SDN_DLY_ENB`) = 0; `TOP_TMA_KEY` = 0 |
| 8 reload | `:274-275` | `RTC_BBPU` \|= KEY\|RELOAD; trigger |
| 9 **power hold off** | `:278` → `:244-247` → `pmic/mt6358/v1/pmic_initial_setting.c:79-96` | `PPCCTL0` 0xa08 bit 0 (`RG_PWRHOLD`) = 0: this is the actual cut |

Addresses: `include/mt-plat/mt6771/include/mach/upmu_hw.h` (e.g. `RTC_BBPU`
:289, `RTC_WRTGR` :318, `TMA_KEY` :3480, `RG_PWRHOLD` :5319-5321,
`RG_SDN_DLY_ENB` :5883-5885); bit names `rtc/mt6358/mtk_rtc_hw.h`.
`RTC_WRTGR` 0x5c2 = RTC base 0x588 + 0x3a (mainline `RTC_WRTGR_MT6358`).

Steps 1-8 do not write `RTC_PDN1`/`RTC_PDN2`/`RTC_SPAR*` (LK boot flags);
step 3 only reads `PDN1`. `PWREN` = 1 keeps a pending RTC alarm able to
power the device on.

## The fallback reset: `arch_reset(0, "charger")`

- `drivers/watchdog/mediatek/wdk/wd_api.c:630-671`: for `"charger"`,
  `reboot` stays 0 (no `WD_SW_RESET_BYPASS_PWR_KEY`), then
  `wd_sw_reset(0)`.
- `wdt_v2/mtk_wdt_v2.c:428-437` (`wdt_arch_reset`): clears
  `MTK_WDT_MODE_AUTO_RESTART` (bit 4); sets it again only for a bypass
  reset. Bit 4 is MediaTek's "bypass power key" flag.
- Outside that path stock **always** sets bit 4 (`:205-207`, `:1189-1191`:
  "Because HW reboot always need reboot to kernel, we set it always"), so
  a watchdog timeout boots straight back to the kernel; only a deliberate
  `arch_reset` without bypass lets the boot chain decide.
- Preloader (MT6797 vendor preloader `platform.c:1093-1097`, sister SoC,
  lead only): bit 4 set → `BR_WDT_BY_PASS_PWK`, else `BR_WDT`.
- LK (Cosmo image strings): its own power-off (`mt_power_off new`,
  `rtc_bbpu_power_down`, `[PMIC]POWER_HOLD OFF`) and power-off-charging
  checks: `Kernel Power Off Charging with Charger/Usb`, `… without
  Charger/Usb`, `Unplugged Usb/Charger in Kernel Charging Mode Before
  Jumping to Kernel, Power Off`, `[BATTERY] No Charger, Power OFF !`.
  The control flow (which boot reasons reach these) is not read; strings
  only.

## Why mainline reboots, also unplugged

- *mainline* `drivers/firmware/psci/psci.c:334`: power-off is PSCI
  SYSTEM_OFF; the vendor ATF does not act on it (same as SYSTEM_RESET,
  session-log 2026-10-25 try7). Nothing in mainline writes MT6358
  `PPCCTL0` (no `PWRHOLD`/`PPCCTL` anywhere in `drivers/` or
  `include/linux/mfd/`); `mt6323-poweroff` writes only `BBPU_KEY` +
  WRTGR at the MT6323 offset.
- After the failed power-off the kernel ends in a panic (`panic=10`) or a
  watchdog timeout. LK hands over `WDT_MODE` = 0x5d (bit 4 set,
  safety-audit.md), and *mainline* `mtk_wdt.c` start/restart only
  read-modify-write `WDT_MODE`, so bit 4 stays set: the reset is a
  "bypass power key" reboot and LK boots Linux again.

## Power-on after a successful power-off (stock)

- Power key: preloader `BR_POWER_KEY` (lead, MT6797 `platform.c:1102-1106`).
- Charger/USB plug-in: `BR_USB` → LK power-off-charging mode, which boots
  the kernel in charger mode; under our kernel that is a normal boot.
- RTC alarm (`PWREN`), only if an alarm is set; mainline sets none
  (safety-audit.md).

## Open before code

- Confirm on the device (read-only): `WDT_MODE` (0x10007000) bit 4 under
  mainline; `PPCCTL0` 0xa08 = 1 while running.
- Stock behaviour with USB plugged in (expected: KPOC boot) for the
  comparison test.

## Stock register values while running (Gemian 4.4, 2026-10-26)

Read-only via `/sys/devices/platform/mt-pmic/pmic_access` (address-only
write = read: `drivers/misc/mediatek/pmic/common/upmu_debugfs.c:94-123`).

| Register | Value | Note |
|---|---|---|
| `PPCCTL0` 0xa08 | 0x0001 | `RG_PWRHOLD` = 1 (power-off clears it) |
| `PSEQ_ELR11` 0xa62 | 0xa403 | bit 10 `RG_SDN_DLY_ENB` = 1 (power-off clears it) |
| `TOP_TMA_KEY` 0x3a8 | 0x0000 | locked |
| `TOP_RST_MISC` 0x14c | 0x0221 | bit 9 `RG_PWRKEY_RST_EN` set; differs from the preloader's 0x1325 (safety-audit.md): the vendor kernel rewrites it |
| `RTC_BBPU` 0x588 | 0x0001 | `PWREN` |
| `RTC_IRQ_EN` 0x58c | 0x000c | alarm IRQ off |
| `RTC_AL_MASK` 0x590 | 0x0010 | DOW only (same as the power-off write) |
| `RTC_AL_SEC` 0x5a0 | 0x25f4 | `2SEC_EN` (bit 8), `AUTO_PDN_SEL` (bit 6) set; power-off clears both |
| `RTC_AL_YEA` 0x5ac | 0xc502 | `K_EOSC_RSV_0/2` set, `RSV_1` clear (already the power-off pattern) |
| `RTC_OSC32CON` 0x5ae | 0xde6e | `EMBCK_SRC_SEL` (bit 8) clear; power-off sets it |
| `RTC_PDN1` 0x5b4 | 0x0000 | no `RTC_GPIO_USER` bits: power-off sets `F32KOB` |
| `RTC_PDN2` 0x5b6 | 0x0201 | `PWRON_ALARM` clear |
| `RTC_CON` 0x5c4 | 0x0486 | `F32KOB` (bit 5) clear: 32 k exported while running |

`WDT_MODE` is not readable here (`CONFIG_DEVMEM` not set in the vendor kernel).

## Stock baseline test (Gemian 4.4, 2026-10-26)

- Pass/fail fixed before the run: USB unplugged, shut down from Gemian;
  pass = stays off for 2+ minutes, then the power key boots Gemian.
- Result: **PASS** (device observed by the user). The following Plasma
  login failed once; one more reboot fixed it (see the RTC year note).
- Unplugging USB from Dragon before the test rebooted the running
  device once (`boot_reason=0`, no WDT/exception record; battery gauge
  91 % but 3.65 V under boot load). Cause not proven; a battery dip with
  the 2 s reboot enabled (`RTC_AL_SEC` bit 8) fits.
- Register dump after the cycle (one extra reboot in between,
  `boot_reason=4` = `BR_WDT_BY_PASS_PWK`,
  `mt-plat/mtk_boot_reason.h:17-26`): identical to the table above, so
  boot restores the running values.

## Side finding: RTC year encoding differs (open)

- Mainline `rtc-mt6397.c:80,122`: year register = `tm_year` (years since
  1900); range capped at 2027-12-31 (`:292-295`).
- Stock `mtk_rtc_common.c:82,97,196`: year register + 68 (base 1968).
- After mainline Debian set the clock, Gemian reads 2094 (same day/time).
  LK's interpretation is not yet checked. Needs a decision before
  dual-booting or before 2028.
