# Session log

## 2026-10-24 — target decision + P1 skeleton

- Decided on **6.18 LTS** over 6.6: MT6771 ≡ MT8183 (97/108 mt8183.dtsi nodes
  at identical addresses; pinctrl 192 pins / 840 functions / 0 diffs; PLL
  offsets match). Rationale and hardware map: `docs/hardware-map.md`.
- Created repo skeleton: `kernel/BASE` pins v6.18.58
  (`2926d88bb7fc4608fd942453d929c39e4e5ec006`); delta = DTS Makefile line +
  `mt6771-cosmo-communicator.dts` (UART0, eMMC HS200, MT6358, reserved LK
  regions copied from vendor `mt6771.dts`, `scp_mem_reserved` sized as vendor
  `scp_share`).
- Verified: DTB compiles clean with `W=1` against v6.18.58 (host dtc, no
  cross-compiler in that sandbox); `bin/build-kernel.sh dtbs` ran end to end;
  every symbol in `cosmo.config` exists in 6.18. Fix found by the script's
  config check: `DRM=m` in arm64 defconfig capped simpledrm at `m` → set
  `DRM=y`, dropped `FB_SIMPLE` (mutually exclusive with simpledrm).
- Found P0 blocker: LK applies a board DT overlay from `odmdtbo` to the
  appended DTB (see AGENTS.md open question 1).
- Nothing built for or flashed to the device.

## 2026-10-24 — first full build (hydra) + P0 prep

- Build host hydra: Fedora Asahi Remix 44, aarch64, native gcc 16.2.1
  20260819, GNU ld 2.46.1. No cross compiler needed: `bin/build-kernel.sh`
  now leaves `CROSS_COMPILE` empty when `uname -m` is aarch64. Packages
  added: `flex bison openssl-devel elfutils-libelf-devel` (Fedora names,
  README updated).
- First full `bin/build-kernel.sh` (Image.gz + DTB + modules): rc 0,
  13m19s wall on 8 cores. Config check: **0 "not applied"** lines.
- Fix: release came out `6.18.58-cosmo-dirty`. Cause: in 6.18
  `scripts/setlocalversion` still runs the dirty check when
  `LOCALVERSION_AUTO` is off and `LOCALVERSION` is unset, and the copied
  delta always leaves the base tree modified. The script now passes
  `LOCALVERSION=` to make; the rebuild (incremental, 24 s) has 0 warnings
  in its log.
- Result (receipt: `out/build.log`, `strings` on Image.gz):
  - release `6.18.58-cosmo`; banner
    `Linux version 6.18.58-cosmo (atzero@Hydra) (gcc (GCC) 16.2.1 …)`
  - `Image.gz-dtb` 15,081,746 bytes, sha256
    `a09422501fe500636b245b9c2ffe4d72dc8df429460e515518673c838f50756f`
  - `Image.gz` 15,033,393 bytes, sha256 `084743e78e92…590043d`;
    DTB 48,353 bytes, sha256 `7a863bb7f72c…d1c7cdbdb1d7ce4e6ee0`
- Note for P0: full arm64 defconfig gives a 15 MB Image.gz. Check it plus a
  ramdisk fits the boot partition size LK uses; trim the config if not.
- P0 prep: `bin/p0-dump.sh` (read-only collector; assumes Gemian 4.4 with
  root — the Cosmo currently runs stock Android, so either install Gemian
  first or adapt) and `docs/p0-backup.md` (scope: device-unique partitions
  only — nvram/nvdata/nvcfg/protect1/2/proinfo/persist/seccfg + GPT; the
  rest is restorable from the stock Planet image). Neither run yet.
- Nothing flashed.

## 2026-10-25 — P0 backup of device-unique partitions

- Flashing host Dragon, mtkclient V2.1.4, stock Planet Android on the Cosmo.
  First try (reboot with USB in) reached preloader but the handshake failed
  and Android booted. Second try caught preloader (`0e8d:2000`) and
  connected. Receipt (mtk log): HW code `0x788`, **SLA disabled, mem
  read/write auth disabled**, DA anti-rollback patched by mtkclient, DA
  extensions loaded.
- Command: `mtk r gpt,proinfo,nvram,nvcfg,nvdata,protect1,protect2,protect_f,protect_s,persist,seccfg <files>`.
  Read: proinfo 3 MiB, nvram 64, nvcfg 32, nvdata 64, protect1 8,
  protect2 9.5, persist 48, seccfg 8, plus the first 0x16000 bytes (GPT).
  `protect_f`/`protect_s` do not exist (expected; older names). No file is
  blank (distinct byte values 22–256 per file).
- `SHA256SUMS` sha256 `05ecc2a2cab4d9e46f65aa41932c00ac93fc0ab4c68b5fff0df45cb36d6e7540`.
  Two copies: flashing host and build host, `~/cosmo-backup/20261025/`;
  `sha256sum -c` OK on the second copy. Not in any repo.
- GPT (40 partitions, from `gpt.bin`), boot-relevant ones:

  | # | name | start LBA | size |
  |---|---|---|---|
  | 2 | recovery | 2112 | 32 MiB |
  | 28 | lk | 1091584 | 1 MiB |
  | 29 | lk2 | 1093632 | 1 MiB |
  | 30 | boot | 1095680 | 32 MiB |
  | 31 | logo | 1161216 | 8 MiB |
  | 32 | dtbo | 1177600 | 8 MiB |
  | 38 | userdata | 10158080 | 114261 MiB |

  Full order: boot_para recovery para expdb frp nvcfg nvdata metadata
  protect1 protect2 seccfg persist sec1 proinfo md1img md1dsp spmfw scp1
  scp2 sspm_1 sspm_2 cam_vpu1-3 gz1 gz2 nvram lk lk2 boot logo dtbo tee1
  tee2 vendor system cache userdata otp flashinfo.
- Findings: **no `odmdtbo`**; the overlay partition is `dtbo` (AGENTS.md
  Q1 updated, `bin/p0-dump.sh` falls back to `dtbo`). `boot` is 32 MiB, so
  the 15 MB Image.gz-dtb fits with room for a ramdisk.
- Nothing written to the device.

## 2026-10-25 — P0 offline: lk, boot, dtbo, expdb read and analysed

- Read-only mtk run (`mtk r lk,lk2,boot,dtbo,expdb …`); first attempt
  exited at once ("Please disconnect, start mtkclient and reconnect": the
  Cosmo was still in preloader from the previous run), second caught
  preloader after a reboot. All five read, no errors. Dumps (with
  SHA256SUMS) on the flashing and build hosts under `~/cosmo-p0/20261025/`,
  not in the repo.
- Results in `docs/p0-findings.md`. Headlines: boot = Android header v1,
  `Image.gz`+appended DTB at 0x40080000, DTB window 0x54000000/512 KiB,
  ramdisk window 16 MiB; LK applies the overlay from `dtbo` (3 identical
  entries, 280 label-based fragments) and its failure path on our DTB is
  unknown; LK fb at 0x7df70000; panel NT36672; DRAM 6 GiB; LK `sbc_en = 0`
  but `lock_state 0x4` and `verifiedbootstate=green`.
- Nothing written to the device.

## 2026-10-25 — Gemian on the device, bootloader unlocked, live P0 dump

- Flashed Planet `cosmo-customos-installer-v4.zip` (sha256
  `6fc0a22e933bd36c7aabd5139d6e402f8487d91effc50e803939966195f2887f`):
  `mtk w boot,userdata linux-cosmo-boot.img,gemian-cosmocom-kde-plasma.img`.
  `boot` written (sector 1095680); the `userdata` write dropped at 3.2 %
  ("Device disconnected" after ~18 min in DA mode since the previous read).
  Retry `mtk w userdata …` caught a later preloader window and completed
  (sector 10158080, count 234006464). Android is gone; system/vendor/
  recovery and all backed-up partitions untouched.
- First boot: **red state** and a reboot loop (LK rejects the unsigned
  image while locked). `mtk da seccfg unlock`: "Detected V4 Lockstate",
  "Successfully wrote seccfg". Next boot: orange state, Gemian 4.4.146 up,
  SSH as `cosmo` over Wi-Fi.
- `bin/p0-dump.sh` run as root on Gemian: all steps rc 0 except a
  harmless `tar` "file shrank" on `memory/mblock_info`. Results in
  `docs/p0-findings.md` ("Live system"): fb stride 4352 / a8b8g8r8, LK
  `mblock-*` reserved-memory list, `lock_state = 0x3`, `boot state: orange`.
- Fix: `bin/p0-dump.sh` boot-magic read no longer warns about NUL bytes.

## 2026-10-25 — first-boot image prepared (not yet flashed)

- DTS for the LK contract (`docs/p0-findings.md`): `/memory` (exact path,
  LK rewrites it), `simple-framebuffer` in `/chosen` (0x7df70000,
  1080x2160, stride 4352, a8b8g8r8, DISP power domain), ramoops at the
  vendor pstore region with Gemian 4.4\x27s layout, vendor nodes LK reads
  (`/gpio@10005000` with `gpio_init_default`, `mediatek,scp`,
  `mediatek,lk_charger`, `reserve-memory-sspm_share`, scp_share sharing
  the mainline `scp_mem_reserved` pool). LK\x27s `mblock-*` carve-outs are
  deliberately not duplicated (Gemini: duplicate no-map hung early boot).
  M4U/SMI/larbs disabled for first boots. `dtbs W=1`: only the intended
  `/memory` unit-address warning.
- Config: fbcon rotation + TER16x32 font, ramoops/pstore console built in.
- New: `bin/mkbootimg.py` (header v0, LK addresses, refuses a cmdline
  without `bootopt=` or a ramdisk over 16 MiB), `bin/build-boot-img.sh`,
  `initramfs/init` (busybox; prints status to tty0 + kmsg, dmesg to pmsg;
  no eMMC writes). Busybox: hydra `/usr/bin/busybox` 1.37.0 (static).
- Build: `6.18.58-cosmo`, `Image.gz-dtb` sha256 `78dfae0955a0…9c6ff`;
  `boot.img` 15,990,784 bytes, sha256
  `15c6fb1d665b510424c52b3b9720c20e666a77a5ddb8c3f7b56357d865e15289`.
- Offline: header fields match the working Planet Gemian image apart
  from sizes and cmdline. `fdtoverlay` of the stock `dtbo` onto our DTB
  fails (`FDT_ERR_BADOFFSET`, no `__symbols__`), as expected: the first
  boot tests what LK does then.

## 2026-10-25 — first boot attempt: LK asserts on the overlay

- Flashed `boot` = `boot.img` sha256 `15c6fb1d…5289` (`mtk w boot`, sector
  1095680, OK). Result: black screen, no vibration, preloader every ~23 s
  (USB log 09:18:40, 09:19:05, 09:19:30).
- `mtk r expdb` (read-only) → LK log for these boots:
  `Kernel(1) zimage_size:0xe694ee,dtb_addr:…(dtb_size:0xd89d)` (ours),
  `real mdtbo index=0`, then
  `ERROR: ufdt_overlay_do_fixups():No node __symbols__ in main dtb.`,
  `ufdt_apply_overlay() failed!`,
  `panic … ASSERT at (app/mt_boot/fdt_op.c:212): 0`. AGENTS.md Q1 answered.
- Fix: `DTC_FLAGS_mt6771-cosmo-communicator := -@` (delta Makefile) and a
  no-op overlay for `dtbo` (`dtbo/cosmo-noop.dts`, one fragment on `/`,
  marker `planet,lk-dtbo = "cosmo-noop"`), packed as a DT table like stock
  (3 entries → one blob at offset 128, page 2048, v0) by
  `bin/mkdtboimg.py` / `bin/build-dtbo.sh`. Offline: `fdtoverlay` applies
  it to the new DTB (70,557 bytes) cleanly.
- New images: `Image.gz-dtb` `d6441ae0…dc95`, `boot.img`
  `14364cd4eaa3df393ab6b606921318203951569db19a6fa85bb8d30b7ae566de`,
  `dtbo.img` `c854cfe933e3e1142b7c04f62bdbd3e50cc8de6e075be0c7d605bfa13cfdfb6c`.
- Consequence: with the no-op `dtbo`, Gemian\x27s vendor DTB loses its
  overlay; restoring Gemian = stock `dtbo` + Gemian `boot`.

## 2026-10-25 — second attempt: overlay passes, LK asserts on bootargs

- Flashed `dtbo` = no-op table (`c854cfe9…6c`) and `boot` =
  `14364cd4…66de` (`mtk w dtbo,boot`, sectors 1177600 / 1095680). Loop
  again (~25 s). `expdb` is a **ring buffer**: the newest LK entries are
  not at the end; match entries by `zimage_size`.
- LK log for this image (`zimage_size:0xe6cfee`, `dtbo_size: 179`):
  `Overlay takes 5 ms` (the no-op overlay applies), then
  `panic … ASSERT at (app/mt_boot/bootargs.c:96): fdt_bootargs`: LK
  needs `/chosen/bootargs` (vendor value = the stock cmdline head).
- Fix: `bootargs = "console=tty0 console=ttyS0,921600n1"`. Also added, from
  the next LK steps on a good boot (Gemian `pl_lk`): `lk-i2c5` container
  (`id = <5>`) with `subpmic_pmu@34`, `mt6370_pmu_dts` (bled/charger;
  `intr_gpio` phandle written as `&pio`), `battery` (bat_gm30), `/odm/led@0-6`
  (led@6 = LCD backlight). Vendor i2c5 @ 0x11017000 = mainline `&i2c11`.
- New `boot.img` sha256
  `adeadd9bc0947358b7fa6d23a76e34a79b5f249fd497c83b7fff8abc964cf455`
  (DTB 80,543 bytes; no-op overlay applies offline).

## 2026-10-25 — third attempt: LK hands over, screen stays on orange

- Flashed `boot` = `adeadd9b…f455`. Result: vibration + orange-state
  screen ("will boot in 5 seconds"), then the screen stays on that frame;
  **no reset loop** (no preloader on USB for >60 s after 09:30:16). So
  LK now completes (the bootargs/vendor-data fix worked) and most likely
  jumped to the kernel.
- Open: kernel running but not visible, or stopped early. LK\x27s display
  has two OVL layers on at handoff (Gemian `pl_lk`): layer 0 = fb
  0x7df70000 pitch 4352 (our simplefb), layer 3 = DAL 0x7e878000 pitch
  4320, aen=1 alpha=255, above it. If layer 3 covers layer 0, fbcon is
  invisible.
- Test image: second `simple-framebuffer` on the layer-3 buffer; initramfs
  writes a noise band into rows 1400-2100 of every fb after fb0.
  `boot.img` sha256
  `2943617bb5712da8070dcc8e77ca209b2ab9c17dd9784d9285a82920d6dd8c01`.

## 2026-10-25 — fourth attempt: still the orange frame; RAM read via mtk

- Flashed `boot` = `2943617b…8c01` (second simplefb + noise band).
  Result: orange frame unchanged, no text, no noise band, no reset.
- No reset for minutes although LK arms its WDT ("mtk_wdt_mode_config LK
  mode value=5d"): either LK disarms it before the jump, or the kernel is
  alive and pinging it (mtk_wdt sets WDOG_HW_RUNNING).
- `mtk da peek 0x54410000 0xe0000` after a 10 s power-key reset: 94 %
  0xff, 8 distinct byte values, no ramoops `DBGC` signature: RAM does not
  survive a power-key reset (or ramoops never initialised; not
  distinguishable from this).
- Next image: cmdline `panic=5 panic_on_oops=1 softlockup_panic=1`
  (was `panic=0`); initramfs warm-reboots (`reboot -f`) 15 s after its
  status output, so the console survives in ramoops for `mtk da peek`.
  `boot.img` sha256
  `0f1571391437b1e5fa98d622ecc197513239141cd7f2e789da3c692e76588161`.

## 2026-10-25 — fifth attempt: no self-reboot

- Flashed `boot` = `0f157139…8161` (panic=5, init `reboot -f` after 15 s).
  Orange frame; no reset in >60 s (preloader 09:40:54, nothing after).
- `expdb` holds no LK entry for try3–5: LK only persists its log there when
  LK itself crashes (or the vendor kernel saves it), so a clean LK handoff
  leaves no record. Consistent with LK jumping to our kernel.
- Two readings left: (a) the kernel hangs before init; (b) init runs but
  `reboot -f`/panic reboot do not work (mainline restarts via PSCI
  SYSTEM_RESET first; the vendor ATF may not implement it).
- Next: init opens `/dev/watchdog` and never feeds it (mtk_wdt, ~31 s), a
  reset path independent of PSCI and the display.

## 2026-10-25 — sixth attempt: init never runs

- Flashed `boot` = `0d795468…5af6` (init arms `/dev/watchdog`, never feeds
  it). Orange frame, no reset for >2 min: **init never ran**. With LK
  finishing cleanly (try3–5), Linux hangs early or the handoff fails.
- Next: `fbpaint` probe (`fbpaint/fbpaint.S`, `bin/build-fbpaint.sh`)
  in place of the kernel: paints LK layer 0 green and layer 3 blue, then
  parks. `out/fbpaint/boot.img` sha256 printed by the script.
- fbpaint v1 (100-byte gzip): black screen, vibration, preloader every ~6 s:
  LK dies before the logo. `expdb` had no usable entry for it. Treated as
  a probe artifact (only the kernel size differs from try6, which reaches
  the orange frame); v2 pads the probe to the real `Image.gz` size with
  seeded incompressible filler.
- fbpaint v2 (padded, `boot.img` `4f060ddc…02a7`): **green, then a wipe
  to red**. Handoff and display work; layer 0 visible, layer 3 transparent
  above it; format is a8r8g8b8 (details in `docs/p0-findings.md`). So the
  Linux image hangs before init, before any console.
- Next (try7): simplefb format a8r8g8b8, no `power-domains` on it (binds as
  soon as simpledrm registers), layer-3 fb + noise test removed, cmdline
  `initcall_debug panic=0`. `boot.img` sha256
  `c6def8d1711dfdbfb39f2b1c8646b8bf21901ff0da8dca834583563b31444a76`.

## 2026-10-25 — try7: **Linux boots on glass**

- `boot.img` `c6def8d1…4a76` (simplefb a8r8g8b8 without power-domains,
  initcall_debug). On glass: kernel console scrolls (initcall_debug), then
  shutdown messages from the init\x27s bring-up self-reboot/watchdog.
  First mainline 6.18 kernel output on the Cosmo.
- Why earlier images showed nothing: the console had `power-domains =
  <&spm DISP>`, so simpledrm waited for the scpsys power-domain driver;
  without it the console binds as soon as simpledrm registers. (Whether
  scpsys probes at all is to be read from the log.)
- try8: init stays up (no self-reboot, no unfed watchdog) and prints its
  status every 30 s; `initcall_debug` dropped. `boot.img` sha256
  `f1b993a6b711834cf5a9fdbc44b95031732e29d91ab3a18dc76d453b7993e020`.
- try7 photo (`20261025_095831.jpg`, kept outside the repo): 8 Tux logos
  (8 CPUs online), console upright in landscape with `fbcon=rotate:3`,
  init ran (its `reboot -f` shutdown sequence at 26.7 s). The sequence
  ends at `PM: Calling printk_kthreads_shutdown` and the board never
  resets: **mainline restart (PSCI SYSTEM_RESET first) does not work with
  the vendor ATF**. So try5/try6 "no restart" were a broken reset, not a
  hang before init. TODO: restart via the mtk-wdt restart handler.
- LK\x27s orange-state text ("Your device has been unlocked…", "Orange
  State") stays on top of the console: it is drawn on OVL layer 3, which
  LK leaves enabled. TODO: clear layer 3 to transparent at boot.

## 2026-10-25 — try8 status screen; try9 = PMIC IRQ, domain supplies, USB net

- try8 (`f1b993a6…e020`) photo `20261025_101108.jpg` (outside the repo):
  init alive (30 s ticks to >180 s), MemTotal 5617908 kB, drm card0
  (simpledrm), **0 eMMC partitions**. Errors: `mt6397 1000d000.pwrap:pmic:
  error -ENXIO: IRQ index 0 not found`; `mtk-power-controller …
  power-domain@2: failed to get power supply` (the whole scpsys power
  controller fails, so anything with power-domains defers, e.g.
  13000000.syscon); `mtk-socinfo … error -ENOENT` (no socinfo efuse data,
  harmless); `cpu0: failed to get proc regulator`; cpuidle not registered
  (cpuidle.off=1). `regulator: Not disabling unused regulators`.
- try9: `&pmic` interrupt = pio 182 level-high (vendor `pmic_irq`
  `<0xb6 0x04>`, same as Kukui); `&mfg_async`/`&mfg` domain-supply as
  Kukui; `&ssusb` peripheral, high-speed, vusb33 from MT6358; config
  `USB_ETH=y` (CDC ECM, no RNDIS); cmdline g_ether MACs
  42:00:15:19:83:01/00; init: usb0 10.15.19.83, udhcpd hands .84, busybox
  telnetd. `boot.img` sha256
  `eee30d39bc6ad5696afbe01cec62d46dd5e4438e1dc01615d12d6c2d13e8308c`.

## 2026-10-25 — try9 result; try10 = mmsys for the DISP domain, diagnostics

- try9 (`eee30d39…308c`) photo `20261025_102718.jpg`: **eMMC up**
  (`mmcblk0`, `mmcblk0p1`–`p40`, `mmcblk0boot0/1`): the PMIC IRQ fix
  brought up MT6358 and its regulators. Power controller now defers at
  `power-domain@7: failed to get clk at index 1` (DISP domain clocks come
  from the mmsys driver; `MTK_MMSYS` was `m`). USB: `no usb0 after 20 s`,
  `udc:` empty (mtu3 never registered a UDC; reason not on screen).
- try10: `MTK_CMDQ=y`, `MTK_CMDQ_MBOX=y`, `MTK_MMSYS=y` (MMSYS is capped
  by CMDQ in Kconfig); init mounts debugfs and prints `devices_deferred`
  and the usb/mtu3/phy/power-controller/mmsys dmesg lines. `boot.img`
  sha256 `fcedf1f10e9fb92309b3920c7e69b508d5d550f4095f7664ff82b84e4bf4005c`.
- try9 stayed up and **a 20 s power-key hold did not reset it**:
  mtk-pmic-keys writes LP_DISABLE to the MT6358 long-press reset when
  `mediatek,long-press-mode` is absent, and PSCI reset does not work
  either. try10 now sets `&mt6358keys { mediatek,long-press-mode = <1>; }`.
- **Rule added (AGENTS.md rule 5):** never flash an image that can strand
  the device; drain-to-reboot is an absolute failure state. Build guard:
  `bin/build-boot-img.sh` refuses a DTB without
  `mediatek,long-press-mode = <1>`.
- try9 could not be escaped: 20 s+ power holds, unplugged 30-40 s holds and
  power+volume combos all failed (MT6358 long-press reset disabled by
  mtk-pmic-keys; no input, USB or working reboot in that image). The board
  is left to run the battery down; the PMIC UVLO and the pack protection
  end it in hardware. On the next USB connection Dragon restores Gemian
  (stock `dtbo` + Gemian `boot`) so the battery recharges under the vendor
  charger driver before bring-up continues with try10 (long-press fixed).
- Recovery: battery connector unplugged (back cover pries off by hand
  from the hinge side; connector under a screwed plastic plate on the
  right-hand board, no battery removal needed). On reconnect with USB on
  Dragon, mtk caught the preloader (10:59:21) and restored stock `dtbo`
  (`532e6060…3d5a`, sector 1177600) + Gemian `boot` (`74561559…2ba2b`,
  sector 1095680). Next: boot Gemian, recharge under the vendor charger.

## 2026-10-25 — try10 stranded the board again

- Flashed no-op `dtbo` + try10 (`5c8b0493…e98c`). Boot as before, `no usb0
  after 20 s`. **The 10 s power-key hold did not reset it**: the
  `mediatek,long-press-mode = <1>` fix was never verified before use.
  Evidence pointing at the cause: stock preloader log
  `[pmic_wdt_set] TOP_RST_MISC=0x1325` (bit 5 set, bit 6 clear) versus
  mainline mt6358_regs (PWRKEY_RST = bit 6, HOMEKEY_RST = bit 5). To be
  confirmed against the vendor MT6358 register definitions.
- No timer failsafe was in the image (the unfed-watchdog signal from try6
  was removed in try8; try6 also never produced a watchdog reset, so that
  path is unproven too).
- AGENTS.md rule 5 amended: two observed-working escapes, escape-only
  first boot, no standing approvals. Recovery: battery connector + Gemian
  restore (queued).
- Root cause of both strandings found offline (vendor `upmu_hw.h`): mainline
  `mtk-pmic-keys` has the wrong MT6358 bit layout and clears
  `RG_PWRKEY_RST_EN` (bit 9). Details: `docs/p0-findings.md`. Reference:
  stock long-press reset = ~8 s (measured under Gemian). After try10 the
  board did reset after 4-5 long holds, probably not via the PMIC (its
  enable was cleared); MT6370 manual reset suspected, unproven.
- Nothing built or flashed. Next image only after review: mt6358keys
  disabled, timer failsafe added, escape-only first boot.
- Group A fixes in source (nothing built for the device): `&mt6358keys`
  disabled (stock TOP_RST_MISC kept); build guard now requires that and
  forbids `long-press-mode`; cmdline `panic=10` instead of `panic=0`.
  Still open before any flash: driver audit against stock, watchdog
  failsafe (LK handoff state + init feeding), escape-test image.
- Safety audit written (`docs/safety-audit.md`). Fixes in source:
  delta `drivers/watchdog/mtk_wdt.c` (restart priority 130, above PSCI);
  init owns `/dev/watchdog` and feeds it only from its own loops;
  `cosmo.wdtest=1` escape test. Open: compare MT6358 fixed-voltage
  regulators with stock register values (Gemian, read-only, needs root).
- try11 built (not flashed): `boot.img`
  `425937dc07dd38a026b7f463398ebd974bff7f7e29706870a3ca5fd0dee8f65d`,
  escape-test `boot-wdtest.img` (adds `cosmo.wdtest=1`)
  `3083ddf30f6c11e7e8c342ad0c15eb13c324bfd0275d237860f43f49e8808a5a`.
  Delta: 3 files (DTS, DTS Makefile, `mtk_wdt.c`).
