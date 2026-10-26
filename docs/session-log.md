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

## 2026-10-25 — try11 escape test: BOTH ESCAPES OBSERVED

- Flashed no-op `dtbo` + `boot-wdtest.img` (`3083ddf3…8a5a`). Each reset
  shows on Dragon as two preloader windows ~6 s apart.
- **Watchdog (timer) escape, observed twice, no buttons:** boot 1 reset by
  itself (preloader 11:36:45); boot 2 (from 11:36:54) reset by itself at
  11:38:23, ~80 s after init armed the watchdog (longer than the 31 s
  timeout; probably the two-stage WDT mode, not investigated).
- **Power-key escape, observed:** boot 3 (from 11:38:31), power held as
  soon as ESCAPE TEST showed: reset at 11:38:57, ~26 s into the boot,
  far ahead of the ~80 s watchdog. Stock `TOP_RST_MISC` untouched by Linux.
- Paused here. Next (needs explicit yes): flash the normal `try11-boot.img`
  (`425937dc…f65d`).

## 2026-10-25 — try11–try17: USB networking works

- try11 normal image (`425937dc…f65d`): stable past 80 s with init feeding
  the watchdog. mtu3 probe failed: `clks of sts1 are not stable!`, -110.
- RAM readback of ramoops after a power-key reset: blank (94 % 0xff, no
  `DBGC`). Not a usable log channel after a cold reset.
- try12 (`e9ebbd57…`): init prints a one-page USB report after the usb0
  wait (kernel console quieted).
- try13 (`e9307882…`, `be6649b`): tphy sets U3 PHYA REG1 bit 29
  (`RG_SSUSB_VUSB09_ON`, vendor phy_init_soc). Same failure.
- Gemian (stock 4.4) reference, read-only: `regulator ext_pmic2 enable` at
  0.23 s, `ldo_va09_status` 1 (900 mV), PMIC STRUP_CON5 (0xa1e) = 0x322.
  `cache` (p37) is not mounted under Gemian.
- try14 (`2f4c2b1c…`, `c424a1d`): mt6358-regulator sets STRUP_CON5 bits
  1 and 5 (read-modify-write, stock value). Log showed `before 0x300`:
  VA09 was already on from LK (status bits 8/9 set), so not the cause.
  New log channel: init writes dmesg to the raw `cache` partition
  (name-checked, 4 MiB) at ~22 s and ~60 s; read from Dragon with
  `mtk ro 4747952128 4194304 <file>` after a power-key reset. Works.
- try15 (`7822bc11…`, `d1c750c`): mtu3 clears PW_CTRL1 HOST_PDN and
  PW_CTRL3 PCIE_PDN (vendor order) and dumps IPPC on failure:
  want 0x10501, STS1 0x1000d0f. SYSPLL, REF and SYS125 up; only the U3 MAC
  clock (bit 16) missing. Clock gates checked against vendor clk-mt6771:
  identical to mainline mt8183 (infra2 bits 1 and 11).
- try16 (`23b5c0e7…`, `b347c41`): `mediatek,u3p-dis-msk = <1>` (port is
  USB 2 only). UDC `11201000.usb` registers, g_ether ready, D+ pulled up,
  but the host never enumerated.
- try17 (`e94d8c34…`, `5bbf1e4`): tphy sets the U2PHYDTM1 force enables
  for VBUSVALID/AVALID/SESSEND (bits 13/12/10), as the vendor init does.
  **Host enumerates** `0525:a4a1` "Ethernet Gadget" (cdc_ether), DHCP
  gives the host 10.15.19.84, `telnet 10.15.19.83` gives a root shell,
  `current_speed` high-speed.
- Which of the try13–try15 changes are still needed is not yet tested
  (VUSB09_ON bit, VA09 write, HOST/PCIE_PDN clears). Candidates for
  removal one at a time now that there is a shell and log channel.

## 2026-10-25 — try18: kexec test loop

- try18 (`88b01b35…`, `c93f401`): mtk_wdt stop() is a no-op while
  `kexec_in_progress` (watchdog stays armed across kexec); initramfs has
  kexec-tools 2.0.32 (dynamic, with its libraries) and `/bin/kx`.
- Flashed hands-free: mtk queued on Dragon, `reboot -f` over telnet. The
  watchdog restart (priority 130) resets the board and mtk catches the
  preloader. After the write the board stays in download mode until a
  manual power-key reset. `mtk reset` powers the board OFF: do not use.
- PSCI: CPU_OFF works (`psci: CPU7 killed`); CPU_ON of the same CPU in
  the same kernel fails ("failed to come online"). A kexec kernel still
  brings all 8 CPUs up.
- Transfer: the Dragon firewall blocks connections from the Cosmo; push
  instead: on the Cosmo `nc -l -p 9000 > /tmp/f &`, on Dragon
  `cat f > /dev/tcp/10.15.19.83/9000`.
- **Watchdog escape across kexec, OBSERVED:** `kx /tmp/Image
  /tmp/initrd.cpio.gz cosmo.wdtest=1` at 13:53:21; board reset by itself
  at 13:54:23 (jump ~25 s, then the 31 s timeout), no buttons.
- **Normal kexec, OBSERVED:** `kx` at 14:15:48; new kernel back on USB at
  14:16:27 without the preloader, 8 CPUs online. One cycle is ~40 s.

## 2026-10-25 — try19: USB fix trimmed to two changes

- try19 (`bcde573f…`, `1129a80`): without the VUSB09_ON write, the VA09
  STRUP write and the HOST/PCIE_PDN clears. Cold boot (flash + power-key
  reset): host enumerates at 14:23:45, `udc: 11201000.usb`, 8 CPUs.
- U3 PHYA_REG1 reads 0x60000007 at Linux start without our write: bit 29
  (VUSB09_ON) is already set before Linux, as VA09 is (STRUP_CON5 0x300).
- Needed for USB device mode on the Cosmo: `mediatek,u3p-dis-msk = <1>`
  (DTS) and the U2PHYDTM1 FORCE_VBUSVALID/AVALID/SESSEND bits (tphy).
  Still carried, diagnostics only: the mtu3 IPPC dump on clock failure and
  the tphy `cosmo: u3 ...` line.

## 2026-10-25 — try20: Debian 13 (trixie) on userdata

- gemini-debian `DEVICE=cosmo` (f1b052c, 7bd7183, b148a20) built a
  headless trixie image (424 MB used; shrunk with `resize2fs -M` to 833 MB
  for the flash, grows to the 110 GB userdata on first boot). Flashed with
  the no-op dtbo and try20 (`dd2cc047…`, a5d992a); the Gemian userdata is
  replaced.
- First boot: Debian came up, but NetworkManager left usb0 unmanaged
  (its 85-nm-unmanaged.rules unmanages DEVTYPE=gadget) and the first
  cosmo-boot-ok confirmed the boot anyway, so the tripwire did not fire.
  Recovered with a rescue boot image (try20 + `cosmo.noroot`), read the
  persistent journal from the rescue shell, fixed overlay-cosmo (udev
  rule, `firewall-backend=none`, boot-ok waits for an IPv4 address on
  usb0) and copied the fixed files into the image from the rescue shell.
- Debian 13 up: SSH `atzero@10.15.19.83` (the host gets DHCP from
  NetworkManager shared mode), `systemctl is-system-running` = running,
  8 CPUs, root 110 GB.
- **Watchdog handover OBSERVED:** the journal shows systemd "Using hardware
  watchdog 'mtk-wdt' ... hardware timeout of 30s" at 3.2 s, after init
  closed its fd ("watchdog did not stop!").
- **Tripwire OBSERVED:** flag set to COSMO-TRY from Debian, `systemctl
  reboot` -> initramfs "last root boot never reported in: RESCUE
  initramfs"; `reboot -f` from rescue -> Debian again (flag RSC, then OK).
- `systemctl reboot` and `reboot -f` both reset through the watchdog.
- The Debian clock starts at the last saved time (no RTC read/set yet, no
  NTP over USB).

## 2026-10-25 — try21: hide the LK orange-state text (pre-flash check)

- Tidy first (c15cb8b): mtu3_plat.c delta removed (it held only the IPPC
  dump; stock driver again), tphy U3 register print dropped, U2 VBUS
  force bits kept. AGENTS.md rule 5 wording was already current.
- Change (21fbeb9): built-in `mtk-lk-overlay-clear` driver + DT node
  `lk-overlay@7e878000` (reg 0x7e878000 +0x8e6200 = 4320 x 2160). It fills
  OVL layer 3's buffer with zero (alpha 0) once at probe. The buffer is
  inside LK's no-map `mblock-7-framebuffer` (0x7df70000 +0x1f90000, read
  from the running Debian's /proc/device-tree), past layer 0
  (0x7df70000 +0x8f7000). Memory write only: no OVL/RDMA/DSI register and
  no panel command, so AGENTS.md rule 3 is not touched. It counts as a
  display change under rule 5, so it needs an explicit yes.
- Risk: wrong address = a stripe of garbage or nothing on screen until
  the next boot; no register state changes. Watchdog, PMIC keys, reset
  path and USB init are unchanged from try20 (apart from the removed
  debug prints).
- Escapes: power-key hold (~8 s) and watchdog, both OBSERVED on this
  kernel family (try11+, try20). Rollback: `~/cosmo-flash/try20-boot.img`
  (dd2cc047…), rescue `try20-rescue-boot.img`.
- Image: `try21-boot.img` sha256 `81c7db49a740c775123e59dd0fe705bd6f28cdec8a5e8c145a8dd9880a38614f`
  (boot only; no-op dtbo and Debian userdata stay).
- Pass: orange text gone from the console, console text intact, dmesg
  shows `mtk-lk-overlay-clear ... cleared`, Debian back on SSH.
- Result: flashed (`mtk w boot`, sector 1095680), reset by hand.
  **Orange-state text gone on glass**, console intact. dmesg at 0.197 s:
  `mtk-lk-overlay-clear 7e878000.lk-overlay: cleared [mem
  0x7e878000-0x7f15e1ff]`. Debian `running`, 8 CPUs, USB network and
  SSH back. PASS.

## 2026-10-25 — RTC year offset; try22: unbrought-up drivers kept off (pre-flash check)

- RTC: `mt6397-rtc` read 2086-10-25 00:24 while UTC was 2026-10-26 00:24
  (Gemian/stock left the time in the vendor year encoding; mainline uses
  base 1900 + `start_secs` 1968-01-02). At boot the kernel set 2086 and
  systemd reset the clock to its build epoch (2026-04-13). Rewritten from
  Linux with `timedatectl set-time` (NTP off for the call, back on):
  `/sys/class/rtc/rtc0` = 2026-10-26 00:26, matching. Debian has no
  `hwclock` (util-linux-extra). Survives-reboot check: next boot.
- NTP: the Cosmo has no route beyond usb0 and Dragon runs only a time
  client, so no NTP for now; the RTC carries time across boots.
- try21 dmesg: the Debian module set autoloads drivers for every enabled
  mt8183.dtsi node: mediatek-drm (no CRTC, components missing) with OVL,
  RDMA, COLOR, CCORR, AAL, GAMMA, mutex, mmsys bound; `mediatek-disp-pwm`
  bound to the backlight PWM (1100e000); `mediatek-mipi-tx` bound to the
  DSI PHY; panfrost bound to the GPU; WARN traces from mtk-mdp3
  (`__get_pdev_by_id`, `mdp_probe`, devm_kfree) and mtk_scp `scp_get`
  (from vcodec). Display unaffected on glass, but this is outside rule 3.
- Change (8eace9c): disable &svs, &pwm0 (disp-pwm), &mipi_tx0, &gpu,
  &mmsys, OVL/RDMA/COLOR/CCORR/AAL/GAMMA/DITHER/mutex, the six mdp3
  nodes, vcodec dec/enc and the JPEG encoder. Thermal stays on.
- Risk: drivers that were bound simply do not probe; none of them had
  written display state (no CRTC; disp-pwm and mipi-tx write only on
  enable). LK's scanout, backlight and clocks stay as LK left them
  (clk/pd/regulator_ignore_unused). Display-adjacent, so explicit yes.
  Watchdog, PMIC keys, reset path unchanged.
- Escapes: power-key hold and watchdog, OBSERVED on this kernel family.
  Rollback: `try21-boot.img` (81c7db49…).
- Image: `try22-boot.img` sha256 `5ad4ed103cd06b086390f4b9108005f88fa06df3e734845bde5b2d962131de6a`.
- Pass: console and backlight as before, no orange text, dmesg free of
  the mdp3/scp WARNs and of mediatek-drm/disp-pwm/mipi-tx/panfrost
  binds, RTC reads the correct UTC after the reboot, Debian on SSH.
- Result: try22 flashed and booted. 0 WARN traces; mediatek-drm,
  panfrost and disp-pwm not bound; `systemctl is-system-running` =
  running; RTC after the reboot = 2026-10-26 00:34:39 UTC, matching
  Dragon (RTC fix holds). PASS.

## 2026-10-25 — try23: built-in keyboard (pre-flash check)

- Live pin state (try22, debugfs `pinctrl_paris/pins`): GPIO50/51 and
  GPIO105/106 in mode 1 (SCL3/SDA3, SCL4/SDA4) with pull-ups; GPIO12
  (AW9523 INT) GPIO input, pull-up, high; GPIO175 (AW9523 HWEN) and
  GPIO109 (AW9524 HWEN) GPIO outputs driven **low**: both expanders held
  in reset after LK. (This debugfs prints mode + 8.)
- Vendor `k71v1_64_bsp.dts`: aw9523_key HWEN GPIO175, INT GPIO12;
  aw9524_key HWEN GPIO109. Stock I2C: 4-005b aw9523 "Integrated
  keyboard", 3-005b aw9524. The vendor driver sources
  (`drivers/misc/mediatek/aw952x`) are not in the gemian 4.4 tree.
- Change: `&i2c4` on at 100 kHz with `awinic,aw9523-pinctrl` @5b
  (INT GPIO12 falling edge, reset GPIO175 active-low); `gpio-matrix-keypad`
  8 rows (P0) x 7 cols (P1), keymap from the Gemini PDA, interrupt
  driven. CONFIG_PINCTRL_AW9523=y, CONFIG_KEYBOARD_MATRIX=y. AW9524
  untouched (i2c3 stays off, GPIO109 stays low).
- Risk: I2C traffic on i2c4 (also carries camera parts, all unpowered)
  and GPIO175 driven high (vendor running state). No reset, watchdog,
  PMIC-key or display change: standing approval.
- Escapes: power-key hold and watchdog, OBSERVED. Rollback:
  `try22-boot.img` (5ad4ed10…).
- Image: `try23-boot.img` sha256 `e7b7c5dcc81918b59d44bb79bc19de01ea48ed4c28deaed8ee6d593929474b80`.
- Pass: aw9523 probes, an input device appears, key presses give events
  (`/dev/input/event*`), Debian back on SSH.
- Result: try23 booted (Debian up), but `aw9523-pinctrl 4-005b: Failed to
  read 2: -110`, probe failed; keypad deferred on it; GPIO175 still low.
  Cause: pinctrl-aw9523 treats `reset-gpios` as "1 = run" (requests it
  GPIOD_OUT_HIGH, regmap reads the registers back, then pulses 0 -> 1).
  Described active-low, logical 1 drove GPIO175 low, so the chip sat in
  reset during the readback. FAIL.

## 2026-10-25 — try24: AW9523 reset polarity (pre-flash check)

- Change: `reset-gpios = <&pio 175 GPIO_ACTIVE_HIGH>`. Nothing else.
- Risk, escapes, rollback as try23 (standing approval; rollback
  `try22-boot.img`).
- Image: `try24-boot.img` sha256 `db62c800fae595b7f4ef043c3d10104b5a85b8916bd835b1c92077b6725fdc3f`.
- Pass: as try23.
- Result: try24 booted; GPIO175 now high (chip out of reset), but the
  readback still fails (-110, ~4 s). Manual rebind: same. `/proc/interrupts`
  shows **0** interrupts for 11008000.i2c, so the controller never
  completes a transfer. Clock gates match the vendor (infra0 bit 12,
  infra1 bit 18, infra2 bit 21). Vendor mt6771.dts gives i2c2 and i2c4
  `ch_offset_default = <0x100>` / `ccu-ch-offset = <0x200>`: multi-channel
  controllers, AP channel at +0x100. FAIL.
- Also seen (since try22): scpsys (`10006000.syscon:power-controller`)
  defers forever, with mfgcfg and the AFE waiting on it: the mt8183 DISP
  power domain takes mmsys clocks and &mmsys is now disabled. Power
  domains stay as LK left them (pd_ignore_unused). To fix before GPU or
  audio bring-up.

## 2026-10-25 — try25: i2c4 at the AP channel offset (pre-flash check)

- Change: `&i2c4` reg = 0x11008100 +0x100 (AP channel), DMA reg as before.
- Risk, escapes, rollback as try23 (standing approval; rollback
  `try22-boot.img`).
- Image: `try25-boot.img` sha256 `10c28ea28be42bc0a6f09b048f7d6e8308031155be30db6f3ff6b9248193afa6`.
- Pass: i2c4 interrupts > 0, aw9523 probes, keypad input device appears.
- Result: try25 **keyboard alive**: aw9523 probes, `input0` "keyboard",
  matrix IRQs count. One gpiolib WARN ("Ignoring 3 default trigger":
  pinctrl-aw9523 sets girq->default_type with an OF irqchip; harmless).
  Key walk (raw evdev capture, scan = row*8+col): letters, Tab, Enter,
  shifts, Ctrl, Alts, Space, arrows, comma, dot, apostrophe, 1, 3, 8, 9,
  0 match the Gemini map. Never seen: 2, 4, 5, 6, 7 (Gemini (0,1),
  (0,3)-(0,6)), Backspace (6,2), P (6,3). Every column-0 key (U, S, J,
  comma, apostrophe) also reports (0,0) "1". PARTIAL.

## 2026-10-25 — try26: keyboard wiring diagnostic (pre-flash check)

- Change: matrix 8 x 8 (col 7 = AW9523 P1_7 added); the 12 positions the
  Gemini map leaves empty get KEY_F13..F24 so every position is
  identifiable; AW9524 on `&i2c3` (base 0x1100f000, no channel offset)
  probed with the AW9523 driver, reset GPIO109 active-high (vendor
  hwen high). The driver sets all its pins to inputs, or stops at a
  chip-ID mismatch (logged).
- Risk: AW9524 leaves reset (stock running state); i2c3 also carries
  FUSB301A, PN544 NFC and SiI9022 HDMI, none described. Standing approval.
- Escapes as before. Rollback: `try25-boot.img` (10c28ea2…).
- Image: `try26-boot.img` sha256 `15f843f1c1fda1d2ca6390a71a61656d64bd1193d973497075521e38f8a21025`.
