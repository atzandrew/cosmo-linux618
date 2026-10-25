# AGENTS.md — cosmo-linux618: mainline Linux 6.18 LTS on the Cosmo Communicator

Read this first, then `docs/hardware-map.md` and `docs/session-log.md`.

Sister projects (same owner, same methods — reuse their code and lessons):
- `atzandrew/gemini-nixos` — Gemini PDA (MT6797) on 6.6. Its kernel delta
  (`devices/planet-geminipda/kernel/delta/`) holds the SSD2092 panel driver,
  the CONSYS gen3 Wi-Fi + BTIF/`hci_stp` Bluetooth port, AW9523 GPIO, power
  reset/off drivers. Its `docs/` are the best reference for LK behaviour.
- `atzandrew/gemini-debian` — Debian 13 + Plasma 6 rootfs builder for the Gemini.

## Status (2026-10-24)

Phase **P1 skeleton**: full kernel builds on hydra (6.18.58-cosmo, 2026-10-24); nothing flashed to the device yet.
- `kernel/BASE` pins v6.18.58 (`2926d88bb7fc`).
- `kernel/delta/` = DTS Makefile line + `mt6771-cosmo-communicator.dts`
  (CPUs/GIC/timer via mt8183.dtsi, UART0, eMMC at HS200, MT6358).
- DTB compiles clean with `W=1` against v6.18.58 (checked in a sandbox
  with host dtc). Full Image.gz-dtb build: see docs/session-log.md.

## Why this works: MT6771 == MT8183

The Cosmo's MT6771 shares MT8183's register map (97 of 108 `mt8183.dtsi`
nodes at identical addresses), pin table (192 pins, 840 functions, 0 diffs)
and PLL offsets. Mainline has no `mt6771` anything; the board rides on
`mt8183.dtsi` and MT8183 drivers. Full map + per-block mainline status:
`docs/hardware-map.md`.

## Rules

1. **Base + file-tree delta, no patch files.** Upstream is pinned in
   `kernel/BASE`; every changed or added kernel file lives at its kernel-relative
   path under `kernel/delta/` as a whole file. `bin/build-kernel.sh` resets the
   base to pristine, copies the delta over, merges `kernel/config/cosmo.config`,
   and fails if Kconfig drops any requested option.
2. **Record before you forget.** Every fact learned from source, a build or the
   device goes in a doc with a date and a receipt (`file:line`, command output,
   commit). Every session adds an entry to `docs/session-log.md`; every flash
   logs kernel release + image sha256.
3. **Panel rule (inherited from the Gemini).** LK initialises the panel. Do not
   enable the mediatek DSI/display pipeline or send panel init until the panel
   ID and init path are confirmed; first boots use LK's framebuffer
   (simpledrm on a `simple-framebuffer` node). A bad DSI init can damage the
   panel.
4. **Never flash something you cannot identify or undo.** P0 backups first.
5. **Never flash an image that can strand the device.** A hung boot must
   always be resettable without software help. Needing to drain the battery
   or open the case to reboot is an absolute failure state (it happened
   twice on 2026-10-25, try9 and try10; see `docs/session-log.md`).
   - **Power-key reset:** the preloader sets MT6358 `TOP_RST_MISC` (0x14c)
     to 0x1325 (`RG_PWRKEY_RST_EN`, bit 9: ~8 s hold resets). Mainline
     `mtk-pmic-keys` uses the MT6397 bit layout for this register and
     clears the reset enable whatever the DT says (`long-press-mode`
     included). So `&mt6358keys` stays **disabled** and nothing in Linux
     writes 0x14c; `bin/build-boot-img.sh` refuses a DTB where the keys
     node is enabled or `long-press-mode` appears.
   - **Watchdog (timer failsafe):** LK leaves the SoC watchdog running;
     init owns `/dev/watchdog` and feeds it only from its own loop, so a
     stuck init or kernel resets the board in ~31-62 s. `cosmo.wdtest=1`
     arms it without feeding (escape test). The watchdog stays armed across
     kexec (`mtk_wdt` stop is skipped while `kexec_in_progress`).
   - **Reboot:** PSCI `SYSTEM_RESET` never returns with the vendor ATF; the
     `mtk_wdt` restart handler runs first (priority 130), so `reboot -f`
     resets through the watchdog.
   - Status (2026-10-25): power-key reset, watchdog reset, watchdog reset
     across kexec and `reboot -f` all **observed** on the try11+ kernels.
   Rules:
   - An escape counts only after it has been **seen to fire** on the
     device. Configured or read-from-source is not an escape.
   - Every experimental image carries **both** escapes above.
   - The first boot of any new kernel config does nothing but prove both
     escapes, with the Gemian restore staged and the manual recovery
     (unplug the battery connector) ready.
   - Never remove a failsafe to make a test more convenient.
   - Any change touching the PMIC keys/`TOP_RST_MISC`, the watchdog, the
     reset path, the display/panel, or a new kernel config needs an
     explicit yes to a written pre-flash check (change, risk flags, escapes
     and whether each has been observed) before it is flashed. Other debug
     images may go under a standing approval; the pre-flash check is still
     written into `docs/session-log.md`.
   - kexec inherits hardware state from the previous kernel: a test of
     whether some power-up step is needed must be a cold boot (flash, then
     a power-key reset).
   - After an mtk write the board stays in download mode until it is reset
     by hand. `mtk reset` powers the board off: do not use it.

## Open questions blocking first boot (P0 — answer from the device)

Offline answers from the stock firmware (2026-10-25): `docs/p0-findings.md`.
Live answers from Planet Gemian 4.4 on the device (2026-10-25), same doc.
Short form: Q1 overlay comes from `dtbo` (no `odmdtbo`), fails on our DTB,
LK **asserts** on failure (fdt_op.c:212): our DTB needs `-@` and `dtbo` needs the no-op overlay; Q2 **answered** (0x7df70000,
1080×2160, stride 4352, a8b8g8r8); Q3 **answered** (LK adds `mblock-*`
reserved nodes; carry them in our DTS); Q4 answered (boot, header v1,
Image.gz+DTB, keep `bootopt=`). Panel is **NT36672**, not SSD2092.
Bootloader **unlocked** 2026-10-25 (`mtk da seccfg unlock`; orange state);
the device now runs Planet Gemian from `boot` + `userdata`.

1. **LK applies a DT overlay.** The stock 4.4 build appends only the base
   `mt6771.dtb` to `Image.gz`; the board DT (`k71v1_64_bsp.dts`, `/plugin/`)
   is built as an overlay and LK applies it from the `odmdtbo` partition
   (defconfig: `BUILD_ARM64_DTB_OVERLAY_IMAGE_NAMES`). LK will try to apply
   that vendor overlay to *our* DTB. On the stock-Android unit (GPT read
   2026-10-25) there is **no `odmdtbo` partition**; the overlay partition
   is the Android-9 `dtbo` (8 MiB). Check which one LK actually reads. Find out what LK does when the apply
   fails (boot anyway? halt?), and whether we need a neutral overlay or an
   LK-side switch. The Gemini (MT6797) did not have this step.
2. **LK framebuffer** address, size, stride and format, for the
   `simple-framebuffer` node. Dump from Gemian 4.4 (`/proc/iomem`,
   `/sys/class/graphics/fb0/*`, `dmesg | grep -i fb`).
3. **LK fdt fixups:** which `/reserved-memory` nodes (ATF, TEE, SSPM, SCP)
   and `/chosen` properties LK injects. Dump `/proc/device-tree` on 4.4.
4. **Boot image:** which partition Gemian boots, header version, kernel/ramdisk
   offsets, and any cmdline LK requires (the Gemini needed `bootopt=` kept).
5. **Console path:** is UART0 (GPIO95/96, 921600) reachable on any pad?
   Otherwise the first sign of life is the framebuffer.
6. First boots should pass `clk_ignore_unused pd_ignore_unused
   regulator_ignore_unused` so the kernel does not turn off LK's display
   clocks/power or MT6358 rails it doesn't know about yet.

## Where things live

| Path | What |
|---|---|
| `kernel/BASE` | upstream pin (tag, commit, URL) |
| `kernel/delta/` | changed/added kernel files, kernel-relative |
| `kernel/config/cosmo.config` | config fragment over arm64 defconfig |
| `bin/build-kernel.sh` | build (`dtbs` arg = DTB only) → `out/` |
| `bin/p0-dump.sh` | read-only P0 collector, run as root on the Cosmo under Gemian 4.4 |
| `docs/p0-backup.md` | mtkclient backup of the device-unique partitions (before any flash) |
| `docs/hardware-map.md` | SoC/board map, mainline status, plan P0–P7 |
| `docs/session-log.md` | dated log of every session |
| `build/`, `out/` | build tree and artifacts (gitignored) |
