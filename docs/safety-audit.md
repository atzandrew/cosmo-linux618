# Safety audit: what mainline does to stock firmware state

Written 2026-10-25 after two boots that could not be reset (AGENTS.md
rule 5). Reference = stock firmware behaviour (preloader/LK logs, vendor
kernel `gemian/cosmo-linux-kernel-4.4` @ a741a2e). Scope: built-in drivers
of the current config that write to the PMIC, the watchdog/reset unit
(TOPRGU) or the charger. Mainline source: v6.18.58.

| Area | Stock | Mainline (as built) | Status |
|---|---|---|---|
| Power-key long-press reset (MT6358 `TOP_RST_MISC` 0x14c) | preloader writes `0x1325`: `RG_PWRKEY_RST_EN` (bit 9) on, ~8 s hold (measured under Gemian) | `mtk-pmic-keys` uses the MT6397 layout and clears bits 9:8 (both key-reset enables) and bit 5 (`RG_WDTRSTB_DEB`) whatever the DT says (`mtk-pmic-keys.c:109-119`, `:159-202`) | **Fixed:** `&mt6358keys` disabled; only writer of 0x14c in mainline; build guard enforces it |
| SoC watchdog at handoff | LK arms it (`mtk_wdt_mode_config LK mode value=5d`: EN, EXRST, IRQ, AUTO_START, DUAL) and resets on LK hangs (`lk_wdt_dump(): watchdog timeout in LK` 10 s after an LK assert). Vendor kernel keeps feeding it from a kernel thread | `mtk_wdt` sees EN, sets `WDOG_HW_RUNNING`, timeout 31 s; the watchdog core feeds it while the kernel runs and until userspace opens it (`mtk_wdt.c:288-298`) | OK, but a kernel that is alive and stuck is fed forever: **init must own the watchdog** (below) |
| Reboot / panic restart | vendor kernel resets through the watchdog (TOPRGU SWRST) | PSCI `SYSTEM_RESET` handler (priority 129, `psci.c:327-329`) runs before `mtk_wdt_restart` (priority 128, `mtk_wdt.c:439`); the vendor ATF never resets, and `watchdog_stop_on_reboot` (`mtk_wdt.c:445`) has already stopped the watchdog: **reboot hangs for good** (try5–try7; also why try6's unfed watchdog never fired: init's `reboot -f` stopped it) | **Fix:** delta `mtk_wdt.c` with restart priority 130 (watchdog reset first, as stock) |
| RTC (MT6358 RTC, LK boot flags in spare regs) | LK reads `spar0/spar1`, PDN bits for boot mode | `rtc-mt6397` probe writes nothing; writes only on alarm IRQ / set time / set alarm (`rtc-mt6397.c:40-56,197-240`) | OK. Do not set the RTC alarm until the spare-register use is mapped |
| MT6358 regulators | vendor driver; voltages from PMIC defaults / LK | `mt6358.dtsi` regulators with min == max get `apply_uV` (`of_regulator.c:111`): vrf12 1.2, vio18 1.8, vcamio 1.8, vcn18 1.8, vfe28 2.8, vcn28 2.8, vxo22 2.2, vaux18 1.8, vbif28 2.8, vio28 2.8, va12 1.2, vrf18 1.8, vaud28 2.8 V. Always-on: vcore, vproc11/12, vs1, vs2, vmodem, vio18, vusb, vsram_*, vxo22, va12 | **Checked 2026-10-26:** every fixed rail matches this unit under stock 4.4 (Gemian `/sys/class/regulator`, docs/power-stock.md). `regulator_ignore_unused` keeps unused rails on |
| eMMC rails | VEMC ~3.0 V, VIO18 1.8 V | `mmc` sets vmmc (VEMC) from the card OCR and power-cycles it at probe | Standard (Kukui); within eMMC VCC spec. Low risk |
| Charger (MT6370) | vendor kernel drives it (currents, JEITA, watchdog) | **no driver**: LK's setup stays (read under S1, 2026-10-26: ICHG 500 mA, AICR 500 mA, CV 4.35 V, IEOC 250 mA, TE off, safety timer 4 h on, JEITA off, MT6370 WDT off, IR compensation off) | Hardware-limited and conservative. No software temperature supervision under our kernel: avoid long charge sessions on mainline until the charger/thermal work (P2) |
| Thermal / DVFS | vendor thermal + DVFS | thermal, SVS, cpufreq not active (modules / no CPU supply) | CPUs stay at LK's clock; no software thermal protection. Idle bring-up only; no heavy loads |
| PMIC wrapper | initialised by preloader | `pwrap` re-inits only if not initialised (`PWRAP_INIT_DONE2`) | OK |
| Display | LK owns the pipe | no DSI/DRM_MEDIATEK; mmsys/CMDQ only register clocks/mailbox; `clk/pd_ignore_unused` | OK (panel rule) |

## Watchdog failsafe (implemented in initramfs/init)

Init opens `/dev/watchdog` as its first action after mounting /dev and
feeds it only from its own main loop. From then on the kernel no longer
feeds it, so a stuck init or a hung kernel resets the board within ~31 s,
like stock. A panic (`panic_on_oops`, `softlockup_panic`) stops all
feeding; `panic=10` then restarts through the watchdog (priority 130).
Escape test: cmdline `cosmo.wdtest=1` makes init arm the watchdog and
never feed it.
