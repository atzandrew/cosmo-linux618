# Cosmo bring-up checklist

Every hardware block the Cosmo needs under mainline 6.18, with where it sits,
what mainline offers, and what is done. Written 2026-10-25 from the stock
device trees (Gemian 4.4 `boot` appended DTB and the stock `dtbo` overlay,
decompiled), the vendor 4.4 source, live stock values (`docs/stock-reference.md`)
and the bring-up so far; status updated 2026-11-03. Plan phases
(P1–P7) are the ones in `docs/hardware-map.md`; the Phase column also
carries priorities set 2026-11-04 (**critical**, medium, nice to have, low,
dropped).

Status: ✅ working on our kernel · 🔶 partly / interim · ⬜ not started ·
➖ out of scope for now. Mainline: **yes** = driver upstream, **port** = vendor
or Gemini code to carry, **new** = no driver anywhere usable, **check** = not
yet confirmed.

## Core platform

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | Boot via LK | `boot` (Image.gz + DTB), no-op `dtbo` | — | LK contract in `docs/p0-findings.md` | P1 |
| ✅ | CPUs, GIC, timers | MT6771 = MT8183 die, 4×A53 + 4×A73 | yes | all 8 online and taking load; core power-down idle since I2 | P1 |
| ✅ | eMMC | msdc0, 8-bit, HS400 | yes (`mtk-sd`) | HS400 at 192 MHz as stock (H1): 290 MB/s direct read (HS200 was 171); DS DLY3 20, DLY1 tuned to 11; kept across deep sleep | P1/P2 |
| ✅ | Watchdog / reboot | TOPRGU `mtk_wdt` | yes + delta | restart priority 130; stays armed across kexec | P1 |
| ✅ | Power-key long-press reset | MT6358 `TOP_RST_MISC` | (hardware) | preloader value 0x1325 kept; Linux never writes it | P1 |
| ✅ | USB device (gadget) | mtu3 `11201000`, t-phy | yes + delta | `u3p-dis-msk`, U2 force bits; CDC-ECM + telnet | P1 |
| ✅ | kexec | — | yes | test loop, ~40 s per kernel | P1 |
| ✅ | Framebuffer console | mediatek-drm fbdev (was LK buffer `0x7df70000` + simplefb) | yes | since display D2 | P1 |
| ✅ | Power key / keys as input | MT6358 `mt6358keys` | **fix** | corrected driver that never touches the reset enable (pk2); hold 3 s = power off; power key = keyboard Esc and the silver key (KEY_ESC); home key = fingerprint side key (KEY_PROG1, stock KEY_VOLUMEUP) | P2 |
| ⬜ | Keypad block | `kp@10010000` | check (`mt6779-keypad`?) | which keys it carries on the Cosmo | P2 |
| ✅ | cpufreq / DVFS | big: vproc11+vsram_proc11, little+CCI: vproc12+vsram_proc12 (no external buck) | yes (mt8183) + delta | full stock tables for this unit, CCI devfreq (C1-C4, `docs/cpufreq.md`) | P2 |
| 🔶 | SVS, thermal | — | yes (mt8183) | thermal done (`cpu-thermal` critical 115 C, battery 60 C; sensor re-init after deep sleep, TH1); SVS open (cold-margin table, `docs/cpufreq.md`) | P2 |
| 🔶 | cpuidle / suspend | SPM, SSPM MCDI | delta | deep sleep via SPM, ~85 mA = stock on this unit (DS7), enabled at boot by the rootfs; wakes on Esc and the power key; Wi-Fi is switched off around sleep (Wi-Fi-on sleep wakes the AP via CONN2AP); the SSUSB IP loses power in deep sleep and is re-initialised on wake (c1a161b, 4f0717e); core power-down idle with stock's MCDI mailbox init (I2); cluster power-down not done | P2 |
| ✅ | CPU hotplug (CPU_ON after CPU_OFF) | vendor ATF | firmware | works since 2026-10-27; MCDI mask updated around hotplug (I2) | P2 |
| ✅ | RTC | MT6358 RTC | yes (`rtc-mt6397`) + delta | vendor year encoding, base 1968 (rtc1) | P2 |
| ✅ | Regulators audit | MT6358 + MT6370 rails | yes | every rail matches stock (voltage and on/off) after R1: VEMC pinned to stock's 3.0 V (was 3.3 V); VUSB/VMODEM/Vsram differences explained (`docs/power-stock.md`) | **critical** |
| ✅ | microSD | msdc1, 4-bit, card-detect GPIO3 (high = card in) | yes (`mtk-sd`) | SDR104 at 192 MHz, VMCH 3.0 V, VMC 1.8 V as stock; mount works; hot-plug and deep sleep with a card in pass (data unchanged) | P2 |
| ➖ | Debug UART | UART0 GPIO95/96 | yes | no reachable pads found | dropped |

## Power

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | Charger | MT6370 `subpmic_pmu@34` (i2c5) | yes + delta | as stock: BC1.2 with the PHY handover, 3.2 A / 2.0 A on a wall charger, battery temperature stop (power S3/S3b); PD PPS fast charge on the left port (P2d, ~5.8 V); Pump Express not done | P2 |
| ✅ | Battery level | MT6358 gauge | **new** (`mt6358-gauge`) | coulomb counter read as stock; percent from an OCV start + counted charge (power S4a/S4b) | P2 |
| 🔶 | Main USB-C port role / PD | MT6370 TCPC `usb_type_c@4e` (i2c5) | yes (`tcpci_mt6370`) | TCPM as sink: PD contracts and PPS (P1b, P2d); not linked to the USB role switch yet, so left host mode is selected by hand | P2 |
| 🔶 | Second USB-C port | **FUSB301A** `@25` (i2c3) + STM32 data switch | **new** (`cosmo-fusb301a`) | automatic host on attach (U4); needs the rootfs `cosmo-codi` service to set the STM32 switch; a mouse keeps working across deep sleep (reset on resume, ~0.5 s) | P2 |
| 🔶 | USB host mode | xhci in the same SSUSB block, USB 2 only | yes + delta | both ports (U2, U4); left port: role by hand, MT6370 OTG boost for VBUS, boost dead after system sleep; right port: VBUS from GPIO178 | P2 |
| ✅ | Power off | MT6358 | **new** (`mt6358-poweroff`) | vendor RTC sequence + RG_PWRHOLD (docs/poweroff.md) | P2 |

## Display and input

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| 🔶 | Panel (DSI) | NT36672 on this unit (SSD2092 on others), 1080×2160 | **port** | NT36672: display D2, panel off in deep sleep, back on at wake; SSD2092 units not supported | P3 |
| ✅ | Backlight | MT6370 backlight (`mt6370_pmu_bled`) | yes (`mt6370-backlight`) | fb0 blank does not turn it off; use brightness | P3 |
| ✅ | Display pipeline | OVL/RDMA/DSI (mt8183) | yes (mediatek-drm) | replaces simplefb (D2) | P3 |
| 🔶 | Touch | `cap_touch@62` (i2c0, Novatek) or `solomon_touch@53` | yes / port | NT36672 units: mainline `novatek-nvt-ts` (T1b); SSD2092/Solomon units not supported | P3 |
| ✅ | Keyboard | AW9523 `@5b` (i2c4) + AW9524 `@5b` (i2c3) | **new** (stock scan) | driver follows the stock scan; works after deep sleep (DS8) | P3 |
| ✅ | Keyboard backlight | GPIO178 + PWM1 FIFO | **new** (`planet,cosmo-kbd-backlight`) | as stock (`docs/leds-stock.md`) | P3 |
| ✅ | Lid (hall sensor) | GPIO8 / EINT 8, low = closed | yes (`gpio-keys` SW_LID) | DT only; wakes deep sleep on lid open only; logind/Plasma act on it; rootfs `cosmo-codi` turns the keyboard off while the lid is shut | P3 |
| 🔶 | Cover display | STM32 on UART1 (`ttyS1`), GPIO77 reset | — (userspace) | rootfs `cosmo-codi` service shows time and battery, lights on lid close and on the fingerprint side key; silver key arrives as an STM32 request and is passed on as KEY_PROG2 (uinput); protocol decoded from stock Android | later |

## Graphics, media

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | GPU | Mali-G72 MP3 | yes (panfrost) | Mesa from trixie, GLES 3.1; stock segment-4 OPP table 300-900 MHz from efuse (G2, G3); Plasma Wayland runs on it; performance pass (benchmarks, Mesa, devfreq, thermal) open | P4, **high** |
| ⬜ | HDMI out | SiI9022 `@39` + EDID `@50` (i2c3) | yes (`sii902x` bridge) | needs a display output path to feed it | low |
| ⬜ | Video decode/encode | vdec/venc | yes (mt8183) | | later |
| ➖ | Cameras | main `@40` (i2c2), main2 `@10` + sub `@35` (i2c4), AF/EEPROMs | **new** | sensor models unknown | — |
| ➖ | Flash LED | MT6370 `fled1/2` | yes (`leds-mt6370-flash`) | only useful with cameras | — |

## Audio

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | SoC audio + codec | MT8183 AFE + MT6358 codec | yes + delta | playback works (A1); AFE clock-gate fix in `mt8183-afe-pcm.c`; microphone not done; hiss while a stream is open | P6 |
| ✅ | Speaker amp | two GPIO amps (GPIO153, GPIO111) | yes (DT) | fed from the MT6358 headphone buffers as stock (A2); no RT5509 fitted | P6 |
| 🔶 | Headset jack | MT6358 ACCDET | check | headphone playback works; plug detection (ACCDET) and headset mic not done | P6 |

## Connectivity

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ✅ | Wi-Fi | CONSYS `18070000`, WLAN gen3 | **port** | stock connectivity stack, Wi-Fi at boot (W1-W3, `docs/wifi-stock.md`); deep sleep with Wi-Fi on not supported | P5 |
| ✅ | Bluetooth | CONSYS (BTIF / `hci_stp`) | **port** | hci0 BR/EDR + LE, BLE mouse (B1), nvdata address (B2a), loaded at boot (B2b), A2DP audio (B2c), reconnects after sleep (B3); `docs/bluetooth-stock.md` | **critical** |
| ➖ | GPS, FM | CONSYS | port | | — |
| ➖ | Modem / SIM | MD1 | — | | — |
| ➖ | NFC | NXP PN544 `@28` (i2c3) | yes (`pn544_i2c`) | | — |
| ➖ | IR transmitter | `irtx@1100d000` (PWM) | yes (`pwm-ir-tx`) | | later |

## Sensors and misc

| | Block | Part / location | Mainline | Notes | Phase |
|---|---|---|---|---|---|
| ⬜ | Accelerometer/gyro | `accelgyro@0` (spi1), BMI160 | yes (`bmi160_spi`) | screen rotation | low |
| ⬜ | Light/proximity | STK3x1x `@48` (i2c1) | check (`stk3310`) | | low |
| ⬜ | Magnetometer | `@0c` (i2c1) | check | stock drives it from the SCP sensor hub; identify by chip-ID register | low |
| ⬜ | Barometer | `@77` (i2c1) | check | SCP sensor hub on stock; identify by chip-ID register | low |
| ⬜ | Vibrator | MT6358 VIBR LDO | yes (`regulator-haptic`) | | nice to have |
| ✅ | Notification LED | MT6370 RGB + AW9524 rear LEDs | yes / delta | `docs/leds-stock.md` | later |
| ➖ | Fingerprint | `cs_finger` (spi0) | **new** | | — |
| ➖ | `aw6302` (spi2) | AEON driver, registers `cs_finger` | **new** | fingerprint-related, not the cover display | — |

## Distro

| | Item | Notes | Phase |
|---|---|---|---|
| ✅ | Root filesystem | Debian trixie on userdata (gemini-debian `DEVICE=cosmo`): Plasma Wayland, greetd + tuigreet login, Cosmo services (sleep, Wi-Fi sleep hook, `cosmo-codi`) | P7 |
| ✅ | Boot flow | initramfs → Debian on userdata; systemd takes the watchdog; tripwire to the rescue initramfs (both observed 2026-10-25) | P7 |
| 🔶 | Clock | RTC read at boot (rtc1); NTP not checked | P2 |
| ✅ | Defconfig trim | Cosmo-only `kernel/config/cosmo_defconfig` + `cosmo.config` (K1): Image.gz 15.2 -> 8.1 MB, 363 modules, clean build 4 min | medium |
