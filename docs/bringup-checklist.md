# Cosmo bring-up checklist

Every hardware block the Cosmo needs under mainline 6.18, with where it sits,
what mainline offers, and what is done. Written 2026-10-25 from the stock
device trees (Gemian 4.4 `boot` appended DTB and the stock `dtbo` overlay,
decompiled), the vendor 4.4 source, live stock values (`docs/stock-reference.md`)
and the bring-up so far; status updated 2026-10-30. Plan phases
(P1–P7) are the ones in `docs/hardware-map.md`.

Status: ✅ working on our kernel · 🔶 partly / interim · ⬜ not started ·
➖ out of scope for now. Mainline: **yes** = driver upstream, **port** = vendor
or Gemini code to carry, **new** = no driver anywhere usable, **check** = not
yet confirmed.

## Core platform

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | Boot via LK | `boot` (Image.gz + DTB), no-op `dtbo` | — | LK contract in `docs/p0-findings.md` | P1 |
| ✅ | CPUs, GIC, timers | MT6771 = MT8183 die, 4×A53 + 4×A73 | yes | all 8 online and taking load; core power-down idle since I2 | P1 |
| ✅ | eMMC | msdc0, 8-bit, HS200 | yes (`mtk-sd`) | 169 MB/s; stock runs HS400 at 192 MHz: enable it | P1/P2 |
| ✅ | Watchdog / reboot | TOPRGU `mtk_wdt` | yes + delta | restart priority 130; stays armed across kexec | P1 |
| ✅ | Power-key long-press reset | MT6358 `TOP_RST_MISC` | (hardware) | preloader value 0x1325 kept; Linux never writes it | P1 |
| ✅ | USB device (gadget) | mtu3 `11201000`, t-phy | yes + delta | `u3p-dis-msk`, U2 force bits; CDC-ECM + telnet | P1 |
| ✅ | kexec | — | yes | test loop, ~40 s per kernel | P1 |
| ✅ | Framebuffer console | mediatek-drm fbdev (was LK buffer `0x7df70000` + simplefb) | yes | since display D2 | P1 |
| ✅ | Power key / keys as input | MT6358 `mt6358keys` | **fix** | corrected driver that never touches the reset enable (pk2); hold 3 s = power off | P2 |
| ⬜ | Keypad block | `kp@10010000` | check (`mt6779-keypad`?) | which keys it carries on the Cosmo | P2 |
| ✅ | cpufreq / DVFS | big: vproc11+vsram_proc11, little+CCI: vproc12+vsram_proc12 (no external buck) | yes (mt8183) + delta | full stock tables for this unit, CCI devfreq (C1-C4, `docs/cpufreq.md`) | P2 |
| 🔶 | SVS, thermal | — | yes (mt8183) | thermal done (`cpu-thermal` critical 115 C, battery 60 C; sensor re-init after deep sleep, TH1); SVS open (cold-margin table, `docs/cpufreq.md`) | P2 |
| 🔶 | cpuidle / suspend | SPM, SSPM MCDI | delta | deep sleep via SPM, ~85 mA = stock on this unit (DS7); core power-down idle with stock's MCDI mailbox init (I2); cluster power-down not done | P2 |
| ✅ | CPU hotplug (CPU_ON after CPU_OFF) | vendor ATF | firmware | works since 2026-10-27; MCDI mask updated around hotplug (I2) | P2 |
| ✅ | RTC | MT6358 RTC | yes (`rtc-mt6397`) + delta | vendor year encoding, base 1968 (rtc1) | P2 |
| ⬜ | Regulators audit | MT6358 fixed LDOs | yes | compare voltages with stock (open safety-audit item) | P2 |
| ⬜ | microSD | msdc1, 4-bit, card-detect GPIO | yes (`mtk-sd`) | stock `status = okay` | P2 |
| ⬜ | Debug UART | UART0 GPIO95/96 | yes | no reachable pads found | — |

## Power

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | Charger | MT6370 `subpmic_pmu@34` (i2c5) | yes + delta | as stock: BC1.2 with the PHY handover, 3.2 A / 2.0 A on a wall charger, battery temperature stop (power S3/S3b); PE/PD fast charge with Type-C | P2 |
| ✅ | Battery level | MT6358 gauge | **new** (`mt6358-gauge`) | coulomb counter read as stock; percent from an OCV start + counted charge (power S4a/S4b) | P2 |
| ⬜ | Main USB-C port role / PD | MT6370 TCPC `usb_type_c@4e` (i2c5) | yes (`tcpci_mt6370`) | needed for host mode and correct charging | P2 |
| ⬜ | Second USB-C port | **FUSB301A** `@25` (i2c3) | **new** | not the FUSB302 the hardware map assumed; no mainline driver | P2 |
| ⬜ | USB host mode | xhci in the same SSUSB block | yes | needs role switch from the TCPC | P2 |
| ✅ | Power off | MT6358 | **new** (`mt6358-poweroff`) | vendor RTC sequence + RG_PWRHOLD (docs/poweroff.md) | P2 |

## Display and input

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | Panel (DSI) | NT36672 on this unit (SSD2092 on others), 1080×2160 | **port** | display D2; panel off in deep sleep, back on at wake | P3 |
| ✅ | Backlight | MT6370 backlight (`mt6370_pmu_bled`) | yes (`mt6370-backlight`) | fb0 blank does not turn it off; use brightness | P3 |
| ✅ | Display pipeline | OVL/RDMA/DSI (mt8183) | yes (mediatek-drm) | replaces simplefb (D2) | P3 |
| ⬜ | Touch | `cap_touch@62` (i2c0, Novatek) or `solomon_touch@53` | yes / port | `novatek-nvt-ts` for NT36672; SSD20xx from the Gemini delta | P3 |
| ✅ | Keyboard | AW9523 `@5b` (i2c4) + AW9524 `@5b` (i2c3) | **new** (stock scan) | driver follows the stock scan; works after deep sleep (DS8) | P3 |
| ✅ | Keyboard backlight | GPIO178 + PWM1 FIFO | **new** (`planet,cosmo-kbd-backlight`) | as stock (`docs/leds-stock.md`) | P3 |
| ⬜ | Lid (hall sensor) | `hall-eint` GPIO (stock `mtk-hall`) | yes (`gpio-keys` SW_LID) | DT only | P3 |
| ⬜ | Cover display | external screen | check | controller not identified yet | later |

## Graphics, media

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ⬜ | GPU | Mali-G72 MP3 | yes (panfrost) | Mesa from trixie | P4 |
| ⬜ | HDMI out | SiI9022 `@39` + EDID `@50` (i2c3) | yes (`sii902x` bridge) | needs a display output path to feed it | later |
| ⬜ | Video decode/encode | vdec/venc | yes (mt8183) | | later |
| ➖ | Cameras | main `@40` (i2c2), main2 `@10` + sub `@35` (i2c4), AF/EEPROMs | **new** | sensor models unknown | — |
| ➖ | Flash LED | MT6370 `fled1/2` | yes (`leds-mt6370-flash`) | only useful with cameras | — |

## Audio

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ⬜ | SoC audio + codec | MT8183 AFE + MT6358 codec | yes | needs a Cosmo machine driver (or simple-card) | P6 |
| ⬜ | Speaker amp | unknown | check | stock runs `rt5509_driver_init` but binds no RT5509: may not be fitted; check the MT6358 speaker path | P6 |
| ⬜ | Headset jack | MT6358 ACCDET | check | | P6 |

## Connectivity

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | Wi-Fi | CONSYS `18070000`, WLAN gen3 | **port** | stock connectivity stack, Wi-Fi at boot (W1-W3, `docs/wifi-stock.md`); deep sleep with Wi-Fi on not supported | P5 |
| ⬜ | Bluetooth | CONSYS (BTIF / `hci_stp`) | **port** | same stack | P5 |
| ➖ | GPS, FM | CONSYS | port | | — |
| ➖ | Modem / SIM | MD1 | — | | — |
| ➖ | NFC | NXP PN544 `@28` (i2c3) | yes (`pn544_i2c`) | | — |
| ➖ | IR transmitter | `irtx@1100d000` (PWM) | yes (`pwm-ir-tx`) | | later |

## Sensors and misc

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ⬜ | Accelerometer/gyro | `accelgyro@0` (spi1), BMI160 | yes (`bmi160_spi`) | screen rotation | later |
| ⬜ | Light/proximity | STK3x1x `@48` (i2c1) | check (`stk3310`) | | later |
| ⬜ | Magnetometer | `@0c` (i2c1) | check | stock drives it from the SCP sensor hub; identify by chip-ID register | later |
| ⬜ | Barometer | `@77` (i2c1) | check | SCP sensor hub on stock; identify by chip-ID register | later |
| ⬜ | Vibrator | MT6358 VIBR LDO | yes (`regulator-haptic`) | | later |
| ✅ | Notification LED | MT6370 RGB + AW9524 rear LEDs | yes / delta | `docs/leds-stock.md` | later |
| ➖ | Fingerprint | `cs_finger` (spi0) | **new** | | — |
| ➖ | `aw6302` (spi2) | AEON driver, registers `cs_finger` | **new** | fingerprint-related, not the cover display | — |

## Distro

| | Item | Notes | Phase |
|---|---|---|---|
| 🔶 | Root filesystem | headless Debian trixie on userdata (gemini-debian `DEVICE=cosmo`); desktop after display/input | P7 |
| ✅ | Boot flow | initramfs → Debian on userdata; systemd takes the watchdog; tripwire to the rescue initramfs (both observed 2026-10-25) | P7 |
| 🔶 | Clock | RTC read at boot (rtc1); NTP not checked | P2 |
| ⬜ | Defconfig trim | drop unused arm64 defconfig drivers | P7 |
