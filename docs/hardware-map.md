# Cosmo Communicator — hardware map, kernel target and plan

Written 2026-10-24. Inputs: `gemian/cosmo-linux-kernel-4.4` @ a741a2e8 (v4.4.146, defconfig `k71v1_64_bsp_defconfig`, DTS `k71v1_64_bsp.dts` + `mt6771.dts`), `atzandrew/gemini-nixos` (kernel model + delta), `atzandrew/gemini-debian`, and mainline stable `v6.6.157` / `v6.18.58` (gregkh/linux mirror). Nothing has been built or flashed yet.

## Decision: target 6.18 LTS

**The headline finding: the MT6771 is, for kernel purposes, an MT8183** — the Chromebook twin of the same die, which mainline supports extensively. Measured, not assumed:

| Check | Result |
|---|---|
| DT nodes in mainline `mt8183.dtsi` (v6.18.58) at the same base address and same function as Cosmo's vendor `mt6771.dts` | **97 of 108** (CPUs, GIC, topckgen/infracfg/pericfg/apmixed, scpsys, pwrap, WDT, all I2C/SPI/UART, MSDC, XHCI, AFE, mipi-tx, Mali, the whole disp/mdp pipeline, DSI, SMI/M4U, vdec/venc, camsys) |
| Pinctrl: vendor `pinctrl-mtk-mt6771.h` vs mainline `pinctrl-mtk-mt8183.h` | 192 pins each, 840 pin-function entries each, **0 differences** |
| PLL register offsets (`clk-mt6771.c` vs `clk-mt8183-apmixedsys.c`) | match |
| "mt6771" anywhere in mainline (6.6 or 6.18) | 0 files — so the board rides on `mt8183` compatibles |

That flips the project's shape compared with the Gemini: the MT6797 had almost nothing upstream (hence its 545-file delta), whereas almost all Cosmo SoC support comes from upstream MT8183 code. So the kernel to pick is the one with the best MT8183 support and the longest life, and the delta is mostly board DT + a few vendor drivers.

Why 6.18 over 6.6:

1. **Upstream MT8183 work since 6.6** (diff line counts 6.6.157 → 6.18.58): `mt8183.dtsi` 498, mediatek DRM 3,583, panfrost 1,351, mediatek pmdomain 1,673, soc/mediatek 3,960 (dvfsrc, svs, mmsys), mediatek thermal 738, `mt6358-regulator` 528, mt8183 ASoC 849, cpufreq 106. New in 6.18: `interconnect/mediatek/mt8183.c` (EMI bandwidth via DVFSRC), panfrost-on-mt8183 in DRM CI, `nt36672e` panel.
2. **Support window:** 6.18 is projected to Dec 2028, 6.6 to Dec 2027 (Feb/Mar 2026 LTS extensions).
3. **Drivers the Gemini had to carry are upstream in 6.18:** `pinctrl-aw9523` (Gemini ships its own `gpio-aw9523b`).
4. **Cost is small and reusable:** the vendor code to forward-port from the Gemini 6.6 delta is the CONSYS stack (`drivers/misc/mediatek-connectivity`, 131 .c files). API churn hits only a handful of spots (`del_timer` ×5, `from_timer` ×2, cfg80211 `set_wiphy_params`/`get/set_tx_power` gaining `radio_idx`, `no_llseek` ×1). That port also pre-pays a future Gemini bump to 6.18.

What 6.6 would buy: one shared base with the Gemini today, flash-proven toolchain. Worth it only if keeping a single kernel tree across both devices *now* outweighs the above. Recommendation stays 6.18.

## Cosmo hardware map (from the 4.4 tree)

| Block | Cosmo part | Mainline 6.18 status |
|---|---|---|
| SoC | MT6771 (Helio P70: 4×A73 + 4×A53) | via `mt8183.dtsi` |
| PMIC | MT6358 | ✅ mfd, regulators, RTC, keys, codec (same as Kukui Chromebooks) |
| Charger/PMU | MT6370 (charger, backlight, flash LED, RGB LED, LDO, DSV, TCPC) | ✅ all sub-drivers incl. `tcpci_mt6370` |
| 2nd USB-C | FUSB302 | ✅ `fusb302` |
| Fuel gauge | MT6358 gauge (vendor `GAUGE_VERSION=30`) | ❌ none — start voltage-derived like Gemini |
| Panel | SSD2092 (or NT36672 variant), 1080×2160, rot 270 | Gemini's `panel-solomon-ssd2092` + init table. **Cosmo's live SSD2092 init table is byte-identical to the Gemini's** (184/184 non-gamma commands; the two 0xC3 gamma packets are the ones the Gemini splits) |
| Touch | SSD20xx or NT36xxx | SSD2092 from Gemini delta; `novatek-nvt-ts` (nt36672a) upstream |
| Keyboard | AW9523 + AW9524 expanders | `pinctrl-aw9523` upstream; AW9524 to check |
| GPU | Mali-G72 MP3 (Bifrost, vendor r14p0) | ✅ panfrost, `mediatek,mt8183b-mali` |
| Wi-Fi/BT/GPS/FM | CONSYS_6771, WLAN **gen3** | Port Gemini's gen3 + BTIF/`hci_stp` (vendor gen3 Makefile already lists CONSYS_6771) |
| Audio | MT6358 codec + RT5509 amp, ACCDET | AFE+codec ✅; RT5509 no driver ❌ |
| Sensors | BMI160 (SPI), STK3x1x, mag/baro via SCP sensorhub | BMI160 ✅ (iio); others later |
| Out of scope for first pass | modem, cameras, fingerprint (CS358/AW6302), cover display, NFC, GPS, IR TX | — |

## Plan

**P0 — Safety and receipts (before any flash).** Full eMMC dump with mtkclient (preloader, LK, nvram/nvdata/protect* for IMEI and radio calibration). From running Gemian 4.4, capture `/proc/device-tree`, `dmesg`, `/proc/iomem`, `/proc/cmdline`, boot.img header and LK's framebuffer address/format. Document the boot contract (Image.gz + appended DTB as the 4.4 build does; which partition LK boots for Linux; any LK cmdline must-keeps — the Gemini's `bootopt=` lesson). Find a debug UART if reachable.

**P1 — Skeleton kernel.** New repo, same model as gemini-nixos: hash-pinned v6.18.y tarball + plain file-tree delta, no patches. `mt6771-cosmo-communicator.dts` including `mt8183.dtsi`: memory, UART, `simple-framebuffer` on LK's buffer (no DSI touch — the panel-burn rule), eMMC, pwrap + MT6358, power key. Goal: console on glass + initramfs shell, then USB gadget CDC-ECM (`mtu3`) for SSH.

**P2 — Platform.** eMMC speed modes, cpufreq (check mt8183 OPPs against P70 bins from the vendor tables), thermal, MT6370 charger/backlight/LEDs, battery (voltage-derived first), reboot/poweroff, Type-C (MT6370 TCPC + FUSB302).

**P3 — Display and input.** Mainline mediatek-drm DSI with the Gemini SSD2092 panel driver (identify SSD2092 vs NT36672 by panel ID first), touch, keyboard.

**P4 — GPU.** panfrost on G72; Mesa from Debian trixie.

**P5 — Connectivity.** Forward-port Gemini's gen3 Wi-Fi + BT to 6.18 with an mt6771 platform file.

**P6 — Audio.** MT8183 AFE + MT6358 machine driver; RT5509 driver port.

**P7 — Distro.** Reuse the gemini-debian builder (trixie + Plasma 6) with a device switch.

## Sources

- [Phoronix: 6.18 / 6.12 / 6.6 LTS periods extended](https://phoronix.com/news/Linux-6.18-LTS-6.12-6.6-Extend)
- [It's FOSS: LTS support extension](https://itsfoss.com/news/linux-lts-kernel-support-extension/)
- [gemian/cosmo-linux-kernel-4.4](https://github.com/gemian/cosmo-linux-kernel-4.4)
- [gregkh/linux stable mirror](https://github.com/gregkh/linux)
