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
- Result: try26: AW9524 probes with the AW9523 driver (chip ID 0x23,
  no error), GPIO109 high. Key walk: 2, 4, 5, 6, 7, P, Backspace and Esc
  still give nothing (col 7 and the fillers never fire); U still also
  reports (0,0); Fn = (4,3) (KEY_RIGHTALT in the Gemini map). The vendor
  DTS gives the AW9524 no interrupt line, so it is most likely extra
  matrix columns.

## 2026-10-25 — try27: AW9524 pins as matrix columns (pre-flash check)

- Change: col-gpios += AW9524 0..15 (columns 8..23; 8 x 24 matrix,
  row_shift 5, scan = row*32 + col). Unmapped positions still emit
  MSC_SCAN, which the capture decodes. matrix_keypad drives one column
  at a time while scanning (inactive columns are inputs), all columns
  low when idle: no two outputs at different levels.
- Risk: AW9524 pins become outputs (low when idle). If one of them is not
  a matrix line, it is held low while the keypad is idle. Standing
  approval. Rollback `try25-boot.img`.
- Image: `try27-boot.img` sha256 `33f2dc021d3d9fcb6c52f35f353c45699e64e5f62cdfeac191c6be2f6df9d622`.

## 2026-10-25 — keyboard: stop guessing, read the stock driver

- try27 (AW9524 pins as matrix columns) was flashed but not booted: the
  theory was wrong. Stock evidence:
  - The stock Gemian kernel (`boot-gemian-restore.img`, build
    `cosmo-linux-kernel-0.1+0~20210419192349.29~1.gbpe4734b`) holds the
    aw9523 keymap table (28-byte entries: name[12], code, state, row, col):
    8 rows (P0) x 7 columns (P1_0..P1_6), **identical to the Gemini PDA
    map**. The matrix wiring is the same; the dead keys (2, 4-7, P,
    Backspace) and the column-0 ghosting of (0,0) are a scan or electrical
    difference, not a keymap one.
  - The stock aw9524 driver registers `kbd_backlight` and `aw9524_led_proc`:
    the AW9524 is the keyboard backlight LED driver, not part of the matrix.
  - Esc is in neither table (stock input devices include `mtk-kpd`).
- Vendor sources: `drivers/misc/mediatek/aw9523/aw9523_key.c` and
  `aw9524/aw9524_key.c` in gemian/cosmo-linux-kernel-4.4 (in git; extract
  with `git show HEAD:<path>` from a sparse clone). Vendor aw9523 scan:
  P1 mask 0x7f, P1_7 kept as an output driven low; per column: P1_CONFIG
  read-modify-write, P1_OUTPUT low on that column, read P0_INPUT. Mainline
  pinctrl-aw9523 leaves P1_7 as an input.
- Next: read both vendor drivers in full and compare with mainline
  (pinctrl-aw9523 init + matrix_keypad scan) before the next image.
- Device restored to try25 (`10c28ea2…`).

## 2026-10-25 — keyboard: stock driver read, live register tests

- Both vendor drivers and the stock DTB/dtbo read; findings with receipts in
  `docs/keyboard-stock.md`. Stock EINT 12 is level-low; i2c4 400 kHz;
  AW9524 = cover RGB LEDs (`kbd_backlight` is SoC PWM1 + GPIO178).
- `tools/aw9523-stock-scan.c`: stock (A), stock with P1_7 an input (B) and
  the mainline register sequence (C) from userspace all read every key,
  including the dead ones. The real driver (D) lost 2, 3, 4 that run.
  Fault is in the kernel interrupt path, not wiring or scan order.
- No image flashed. Repo DTS still carries the try26/27 diagnostics; revert
  to the try25 keyboard node before the next build.

## 2026-10-25 — try28: AW9523 keyboard driver following the stock scan (pre-flash check)

- Change: new `drivers/input/keyboard/aw9523-keypad.c`
  (`awinic,aw9523-keypad`, CONFIG_KEYBOARD_AW9523=y) replaces
  pinctrl-aw9523 + gpio-matrix-keypad (CONFIG_PINCTRL_AW9523 and the
  matrix-keypad node dropped). Scan as vendor `aw9523_key.c`
  (docs/keyboard-stock.md): chip INT off while scanning, one P0 read per
  column, 10 ms rescans while keys are down, P1_7 output low. DT: EINT 12
  `IRQ_TYPE_LEVEL_LOW` and GPIO12 pull-up (stock dtbo fragment@59,
  `aw9523pin`), reset GPIO175 active-low (stock hwen_low = reset), keymap
  as before with (4,3) = KEY_LEFTMETA (stock). DTS reverted to the try25
  file first (try26/27 diagnostics removed). i2c4 stays at 100 kHz.
- New kernel config: explicit yes required (AGENTS.md rule 5).
- Risk flags: no PMIC keys, watchdog, reset path or display change. A
  driver fault means no keyboard; an oops panics (panic_on_oops) and the
  watchdog resets the board. Suspend/resume path not exercised.
- Escapes: power-key hold (~8 s) and watchdog reset, both observed on the
  try11+ kernel family, unchanged here. Rollback: `try25-boot.img`
  (10c28ea2…).
- Build: clean, no warnings; DTB W=1 clean.
- Image: `try28-boot.img` sha256 `e2196b542ff1648c26e9d70cee0c61bb68ff781b229d8b343775a7a3b5b9fd94`.
- Pass (fixed in advance): Debian back on SSH; `4-005b` bound to
  aw9523-keypad, "Integrated keyboard" input device; the key walk (2, 4,
  5, 6, 7, P, Backspace, U, S, J, 1, 3, Fn) gives every key once at its
  stock position, no (0,0) on column-0 keys; Ctrl+Shift+Z gives
  Ctrl+Shift+Z. Fail: any key missing or extra.
- Result: try28 **keyboard works**: `4-005b` bound to aw9523-keypad,
  "Integrated keyboard"; every key reported (key walk on the device). Fn emits
  KEY_LEFTMETA, which the headless console does not use as a modifier
  (Fn layer = userspace keymap, see below). Found: EINT 12 fired ~62/s
  while idle (626 in 10 s).
- INT stuck low (2026-10-25, driver unbound, `tools/aw9523-stock-scan reg`
  + pinctrl debugfs GPIO12 input bit): after the stock init GPIO12 stays
  low after a P0_INPUT read and goes high after a P1_INPUT read, with the
  P1 interrupt masked. The scan's port-1 writes latch INT; only a P1 read
  clears it. The vendor driver never reads P1.
- Fn layer in stock userspace: Gemian xkeyboard-config
  `symbols/planet_vndr/cosmo` maps `<LWIN>` (KEY_LEFTMETA) to
  ISO_Level3_Shift (Mod5); level 3 = Fn layer, level 4 = Fn+Shift.

## 2026-10-25 — try29: release the AW9523 INT line when idle (pre-flash check)

- Change: aw9523-keypad reads P1_INPUT before P0_INPUT when going idle.
  No config or DTS change: standing approval.
- Risk, escapes, rollback as try28 (rollback `try28-boot.img` e2196b54…,
  `try25-boot.img` 10c28ea2…).
- Image: `try29-boot.img` sha256 `f29ea6bb49dba72ebd06f76bc67eb3db63b8a2ddb0cd523f414b35d8deb7b050`.
- Pass: EINT 12 count stays flat while idle (10 s); every key still
  reported once. Fail: idle interrupts or a lost key.
- Result: try29 **PASS** for the driver: EINT 12 count 0 over 10 s idle
  (try28: ~620); keys reported (key walk on the device).
- Fn layer (rootfs, gemini-debian a5f630b, a2dd408): console-setup with
  xkb layout `cosmo`, variant `us` (trimmed Gemian planet_vndr/cosmo);
  Fn = KEY_LEFTMETA = AltGr on the console (dumpkeys: keycode 125 =
  AltGr). Installed live on the device (same files as the repo).
  console-setup also replaced the console font (Fixed 8x16); set to
  Terminus 16x32 like the kernel's fbcon font.
- OPEN: Fn works but the Fn-layer symbols do not match this unit's
  US/JPN silkscreen (key walk on the device). Next: walk the silkscreen key by key
  and correct the `cosmo(us)` symbols (no Kana group).
- OPEN: Esc = PMIC power key on stock (hal_kpd.c: KEY_ESC, KEY_POWER
  with Fn); mtk-pmic-keys stays disabled (AGENTS.md rule 5).

## 2026-10-26 — power-off: stock path read (no code, no flash)

- Stock power-off written up with receipts in `docs/poweroff.md`: the cut is
  MT6358 `PPCCTL0` 0xa08 bit 0 (`RG_PWRHOLD`) = 0 after the RTC/BBPU
  sequence; if power has not dropped after ~1.1 s (or a charger is
  present) stock does a watchdog reset with `WDT_MODE` bit 4 cleared.
- Why mainline reboots also unplugged: nothing in mainline writes
  `PPCCTL0`; PSCI SYSTEM_OFF is ignored; the watchdog reset that follows
  keeps LK's bit 4 ("bypass power key"), so LK boots Linux again.
- OPEN: device read of `WDT_MODE` and `PPCCTL0` (device not connected
  this session); pre-flash check for a power-off handler.
- Stock baseline (Gemian): power-off unplugged stays off, power key boots:
  PASS. Register values while running recorded in `docs/poweroff.md`.
- Found: RTC year encoding differs between mainline (base 1900, max 2027)
  and stock (base 1968): Gemian shows 2094 after mainline set the clock.

## 2026-10-26 — try30: MT6358 power-off handler (pre-flash check, NOT flashed)

- Change: new `drivers/power/reset/mt6358-poweroff.c` (+ Kconfig,
  Makefile), MFD cell `mt6358-pwrc` in `mt6397-core.c`, DT node
  `&pmic/pwrc` (`mediatek,toprgu = <&watchdog>`),
  `CONFIG_POWER_RESET_MT6358=y`. Sequence = vendor `hal_rtc_bbpu_pwdn()`
  in the power-off prepare stage, `RG_PWRHOLD` = 0 in the power-off stage
  (priority 192, before PSCI at 0), then after 1.1 s a TOPRGU reset with
  `WDT_MODE` bit 4 cleared (vendor `arch_reset(0, "charger")`).
  Receipts: `docs/poweroff.md`.
- Normal boot: probe reads `PPCCTL0` once; no writes. Reboot path
  unchanged (power-off handlers only).
- Image: `try30-boot.img` sha256
  `9ddfbc207f13dd9154126118d56e826ce60671720bc717025c97dff7b71c9202`
  (kernel Image.gz-dtb `bfc2552a…`). Rollback `try29-boot.img` f29ea6bb….
- PMIC/power path: not covered by the standing flash approval.
- Flashed 2026-10-26 (explicit OK): `mtk w boot,dtbo,userdata` = try30
  (9ddfbc20…, sector 1095680), no-op dtbo (c854cfe9…, 1177600), Debian
  rootfs `cosmo-debian-rootfs.img` (85ee0b3e…, 10158080).
- That rootfs image predates gemini-debian b148a20 (built 16:14, fix
  16:54 on 2026-10-25): usb0 unmanaged again. Recovered from the console
  (`nmcli device set usb0 managed yes`, temporary manual method), then
  installed the five b148a20 overlay files; usb0 back in shared mode.
  The image in gemini-debian `out/` still needs a rebuild.
- Gate: `mt6358-pwrc: power-off ready (PPCCTL0 0x1, TOPRGU mapped)`.
- Test A (USB unplugged, `systemctl poweroff` on the console): **PASS**.
  Stayed off; the power key (~4 s) booted Debian. Before try30 every
  power-off rebooted into Linux.
- Not yet shown: whether the power hold cut power directly or the 1.1 s
  fallback reset ran and LK powered off (both end "off" without a
  charger). The journal stops before the handler; LK's expdb ring had no
  entry for this boot yet and no watchdog boot reasons.
- LK also reads the RTC year with base 1968 (expdb: `Current RTC
  time:[2094/10/26 …]`), like the vendor kernel.
- OPEN: Test B (USB plugged in, expected LK charging mode / boot).

## 2026-10-26 — power foundation: stock capture, back to mainline, S1

- Stock reference (Gemian, read-only): docs/power-stock.md; plan:
  docs/power-plan.md. Charger values captured live on a PC port (CDP), two
  Anker wall chargers (DCP) and a USB-C PD laptop charger.
- Back to mainline: gemini-debian `DEVICE=cosmo` rootfs rebuilt on Hydra
  (a2dd408, includes the b148a20 usb0 fix; 3 GB image sha256 `9763f1c0…`,
  shrunk with `resize2fs -M` to 722 MB, `4b551b9e…`). Flashed (explicit
  OK): `mtk w boot,dtbo,userdata` = try30 (9ddfbc20…), no-op dtbo
  (c854cfe9…), rootfs. usb0 came up managed without manual steps.
- Efuse on try30 (`/sys/bus/nvmem/devices/nvmem0`, offset 0x180):
  `02954977 a21e314a a754aa20` = stock devinfo 100 / 101 / 102 (Gemian
  dmesg `[calibration] temp0=0xa21e314a, temp1=0x2954977, temp2=0xa754aa20`).
  The mt8183 V1 field layout in auxadc_thermal.c matches stock mtk_tc.c.
- S1 (measure only): `&i2c11` 400 kHz with `pmic@34` (`mediatek,mt6370`,
  GPIO10 level-low) and only the `adc` child; `MFD_MT6370`,
  `MEDIATEK_MT6370_ADC`, `MEDIATEK_MT6359_AUXADC` built in; charger,
  backlight, LEDs, regulators, TCPC drivers not built. Image `boot-s1.img`
  sha256 `91dcb53d95e3df88c003c289aa80f04b07179c45c96ca67f628894d6981517c2`
  (Image.gz-dtb `fa24a30a…`). Flashed (explicit OK): `mtk w boot`.
- S1 result: **PASS**.
  - Boot + usb0 OK; MT6370 core probed (`11-0034` regmap), `mt6370-irqs`
    on EINT 10: 0 interrupts after 120 s.
  - Charger registers (regmap debugfs, mainline offset +0x100) = LK's setup,
    untouched: AICR 0x113 = 0x22 (500 mA), CV 0x114 = 0x5a (4.35 V), ICHG
    0x117 = 0x10 (500 mA), IEOC 0x119 = 0x3c (250 mA), CHGCTRL2 0x112 =
    0x0b (TE off), timer 0x11c = 0x02 (on, **4 h**: the safety audit said
    12 h, corrected), WDT 0x11d = 0x52 (off), JEITA 0x120 = 0x00, IR comp
    0x12c = 0x40 (0).
  - VBAT: MT6370 `vbat` 818 x 5 mV = 4.090 V; MT6358 `bat_adc` 24765 x
    5400 / 32768 = 4.081 V.
  - Battery temperature: MT6358 `batt_temp` 907 x 3600 / 4096 = 797 mV,
    `bif_v` 2.809 V -> R = 24 k x 797 / (2809 - 797) = 9.5 kOhm -> about 26 C.
  - MT6370 `temp_jc` (38 - 20) x 2 = 36 C; `ibus` 400 mA, `ibat` 0 (PC
    port, LK's 500 mA input limit covers only the system load).
  - MT6358 `cdt_v` read times out (-ETIMEDOUT); not used.
  - Rootfs still carries the old `mt6359-auxadc.ko`: "Driver
    'mt6359-auxadc' is already registered" at boot, harmless.
  - Escape: power-key ~8 s hold reset the board and Debian booted again
    (observed on S1).

## 2026-10-26 — power S2: SoC thermal, critical trip

- Change: `MTK_THERMAL=y`, `MTK_SOC_THERMAL=y` (config only; thermal node and
  zones from `mt8183.dtsi`). Image `boot-s2.img` sha256
  `480893fc6d97e458cac45ba0300360aed424410934b4836c9a3ef08bd3b30bd2`.
  Flashed (explicit OK): `mtk w boot`.
- Stock comparison (vendor `thermal/mt6771/src/mtk_tc.c:97-118`): controller
  0 = TS_MCU5, TS_MCU4; controller 1 = TS_MCU1-3, TS_ABB; offsets 0x0 /
  0x100; AUXADC channel 11. Mainline mt8183 data (`msr` / `adcpnp` tables)
  places the sensors in the same slots. Difference: TSABB mux value 0 in
  mainline vs `TEMPADC_ABB` 0x10 in stock (`tscpu_settings.h:173-178`), so
  the sixth sensor reads TS1 again. mt8183 data has no APMIXED buffer write.
- Pre-flash prediction was wrong: mainline 6.18 `mtk_thermal_probe`
  registers only sensor 0 (`devm_thermal_of_zone_register(..., 0, ...)`),
  whose reading is the maximum over the bank (`mtk_read_temp`). Result: one
  zone, `cpu-thermal`, critical 115 C on the hottest sensor; the six
  per-sensor zones of mt8183.dtsi (100 C critical) are not bound. Check 2
  (7 zones) failed as written; check 3 (per-sensor spread) not measurable.
  Stock: 117 C hardware reset on the SoC sensors; its 100 C reset belongs
  to mtktsAP, a board thermistor, not the SoC sensors. 115 C kept.
- Readings: `cpu-thermal` 38.4 C idle after boot (stock tzts1-5 idle
  34-38 C). No trip events.
- Shutdown test: `systemd-run --on-active=90` writing 116000 to
  `thermal_zone0/emul_temp`, USB unplugged. Journal (boot -1): 17:01:12.715
  `cpu-thermal: critical temperature reached`, `reboot: HARDWARE PROTECTION
  shutdown (Temperature too high)`, orderly power-off finished 17:01:13.5;
  the board stayed off, the power key booted it. **PASS.**

## 2026-10-26 — power S3: MT6370 charger as stock (PARTIAL)

- Change: `CHARGER_MT6370=y`, `GENERIC_ADC_THERMAL=y`; whole-file delta
  `drivers/power/supply/mt6370-charger.c` (mainline + Cosmo policy); DTS:
  charger node under the MT6370 (`pmic@34`), `/cosmo-battery`
  (simple-battery: 4.40 V, IEOC 150 mA, ICHG max 2.0 A, 4220 mAh),
  `/thermal-sensor-battery` (generic-adc-thermal on MT6358 BAT_TEMP, stock
  10 k NTC table; values in mV x 4096 because the mt6359-auxadc scale is
  full-scale mV), zone `battery-thermal` (critical 60 C).
- Policy: CV, IEOC, ICHG ceiling from the battery node; TE on; timer 12 h;
  charger WDT off. Input/charge by port type: SDP 500 mA, CDP 1.5/1.5 A,
  DCP 3.2/2.0 A, other 500 mA (ICHG floor 900 mA, mainline workaround).
  Battery temperature polled every 10 s: stop >= 55 C or < -10 C, resume
  < 50 C / >= 0 C; unreadable temperature = charging off.
- Image `boot-s3.img` sha256
  `bd355939ce7f195511d01949da7e4b97e63fb32ffd66321c3ea5d9d6ff3aa26f`.
  Flashed (explicit OK): `mtk w boot`.
- Results against the pre-flash check:
  1. Boot + usb0: PASS.
  2. Probe line `CV 4400000 uV, IEOC 150000 uA, ICHG max 2000000 uA,
     timer 12 h`: PASS.
  3. Registers: CV 0x114 = 0x64, timer 0x11c = 0x82, IEOC 0x119 = 0x1c
     (150 mA), CHGCTRL2 0x112 = 0x1b (TE on): PASS.
  4. IBAT > 0: NOT SHOWN. `ibat` 0, `ibus` 400 mA at the 500 mA input
     limit (same as S1: the system load takes it all).
  5. Emulated 56 C: `Not charging`, health Overheat, CHG_EN 0; 49 C:
     charging resumed: PASS.
  6. Emulated -11 C: stopped, health Cold; 1 C: resumed: PASS.
  7. Wall charger 3.2 A / 2.0 A: FAIL. Anker PowerPort II, both USB-A
     ports, and the Dragon PC port all read USBSTATUS1 0x127 = 0x34
     (status 3 = SDP non-standard, DCDT bit set) -> SDP, 500 mA.
- Detection notes: at boot USB is already attached, `pwr_rdy is same(1)`,
  BC1.2 does not run (0x127 = 0x60 left from LK, status 6 = outside the
  enum; port type Unknown, 500 mA). After a replug BC1.2 runs and usb0
  comes back each time. Every port giving the same DCD timeout points at
  the SoC USB PHY staying attached (D+ pull-up from the gadget) during
  detection; stock detaches it (`Charger_Detect_Init`/`Release`). Stock
  maps non-standard to 2.05 A; mainline maps it to SDP. Kept at 500 mA
  until detection works.
- Rootfs carries old `generic-adc-thermal.ko`: "already registered" at
  boot, harmless (as `mt6359-auxadc` in S1).
- MT6358 `in_temp1_raw` read once by mistake: timed out (-ETIMEDOUT); no
  AUXADC reset followed. Skip that channel as well as `cdt_v`.
- Device left on S3. Next: S3b, BC1.2 with the PHY detached as stock.

## 2026-10-26 — power S3b: BC1.2 with the USB PHY handed over (PASS)

- Cause of the S3 failure, stock first: the vendor MT6370 driver wraps
  every BC1.2 run in `Charger_Detect_Init` / `Release`
  (`pmic/mt6370/mt6370_pmu_charger.c:735`), which on mt6771 is
  `mu3phy/mt6771/mtk-phy-asic.c:1197-1260`: set, then clear,
  `RG_USB20_BC11_SW_EN` (USBPHYACR6 bit 23). `ssusb_sif2` = 0x11f40000,
  U2 COM bank + 0x300, so the register is 0x11f40318 (mainline tphy uses
  the same layout and only clears the bit, `phy-mtk-tphy.c:857`). Live
  value on S3: 0x001004f2 (bit 23 = 0).
- Live check on S3 (before any build): bit set by hand through /dev/mem,
  auto-cleared by a script, then a plug into the Anker PowerPort II:
  USBSTATUS1 0x127 = 0x48 (DCP), DEVICE_TYPE 0x122 = 0xd4, AICR 0xfa,
  ICHG 0x4c, IBAT +1.3 A: all equal to the stock DCP dump. Without the bit
  the same port reads 0x34 (non-standard, DCD timeout).
- Change (delta `mt6370-charger.c`, DTS `cosmo,u2phy-bc11-reg =
  <0x11f40318>`): bit set before USBCHGEN is raised; cleared when the
  result is in, on detach, on error, and after 2 s at the latest
  ("BC1.2 result late" warning). A result of "under going" waits for the
  next attach_i (vendor behaviour). USBCHGEN is cleared before it is set,
  so a run always starts fresh. At probe, a pending attach_i from LK is
  flushed and the state reset to detached, so VBUS present at boot runs
  BC1.2 again. Non-standard = vendor NONSTANDARD_CHARGER, 2.05 A input /
  min(2.05 A, ceiling) charge.
- Image `boot-s3b.img` sha256
  `333bf519230433080dde0941014c03a2f3838b21b2856ff058034bd9766575fd`.
  Flashed (standing OK, user asked to queue it): `mtk w boot`.
- Results:
  1. Boot + usb0: PASS (`BC1.2 with USB PHY handover (0x11f40318)`).
  2. Cold boot on the Dragon PC port: stale LK result discarded
     (`port type 0`), fresh run `port type 1` (SDP), 0x127 = 0x20: PASS.
  3. Replug on Dragon: SDP again, usb0 back: PASS.
  4. Anker PowerPort II (high-power USB-A port): `port type 2: input
     3200000 uA, charge 2000000 uA`, 0x127 = 0x48, IBAT +1.3 A, IBUS
     1.8 A: PASS. S3 checks 4 and 7 are now met.
  5. 0x11f40318 read 0x009004f2 for ~0.4 s around each run and 0x001004f2
     after every one (boot, replug, wall, back to Dragon): PASS.
  6. Emulated battery temperature 56 / 49 / -11 / 1 C: stop / resume /
     stop / resume: PASS.
- Note: stock's PC-port dump was CDP (1.5 A); the Dragon port reads SDP
  under mainline. Stock's dump may have been taken on a different port.
- Device left on S3b.

## 2026-10-26 — power S4a: MT6358 fuel gauge, measurements only (COMPLETE)

- Stock first: `pmic/mt6358/v1/mt6358_gauge.c` `fgauge_read_current`
  (:882) and `fgauge_get_coulomb` (:1098). Handshake FGADC_CON1 (0xd0a)
  [3:0] = 0x1, wait LATCHDATA_ST (bit 15), read, 0x8, wait bit 15 = 0,
  0x0. Current 0xd8a (381.470 uA/LSB, raw > 32767 = discharge as 65535 -
  raw); CAR 0xd14/0xd16 (20 bits + sign, 108.507 uAh/LSB, discharge as
  0xfffff - car); CAR_TUNE 930 on both.
- Live, before the build (read only): FGADC_CON0 0x2129 (FG_ON; stock
  live 0x2529, bit 10 ZCV_DET_EN set by stock's gauge code), CAR about
  -0.1 mAh (counter reset at boot). LK's `/chosen` atags are ASCII:
  `atag,fg_swocv_v` "39527" (3.9527 V), `atag,fg_swocv_i` "-5835"
  (-583.5 mA). The stale latched current 0xf987 converts to -587 mA with
  the vendor formula, matching LK.
- Change: new `drivers/power/supply/mt6358-gauge.c` (+ delta Kconfig /
  Makefile, `CONFIG_BATTERY_MT6358_GAUGE=y`), MFD cell `mt6358-gauge` in
  the `mt6397-core.c` delta, DT node `gauge` under `&pmic` (batadc
  io-channel, monitored-battery, `power-supplies = <&mt6370_chg>`,
  `mediatek,car-tune = <930>`). One handshake per read for current and
  charge; the driver refuses to load if FG_ON is clear.
- Image `boot-s4a.img` sha256
  `4bcee6134a67469584d4b805d2235bdeb18493a53fce1395100d15ca4bba69e1`.
  Flashed (explicit OK): `mtk w boot`.
- Results:
  1. Boot + probe `FGADC_CON0 0x2129, car-tune 930: current -170642 uA,
     charge -302 uAh`: PASS.
  2. FGADC_CON0 0x2129 and FGADC_CON1 0x0000 after ~200 reads, no read
     errors, no AUXADC timeouts: PASS.
  3. Current vs MT6370 IBAT on the Anker PowerPort II: gauge 1.21 -> 1.04
     A, MT6370 1.30 -> 1.10 A; mean ratio 0.943 (1.014 without CAR_TUNE).
     Over the 5 % limit set in advance; accepted by the user with the
     vendor 0.93 kept (which reference is right is open).
  4. Voltage 4.0465 V vs MT6370 4.055 V: PASS.
  5. Charge counter over 780 s of charging: +246.7 mAh vs 243.1 mAh from
     the current samples (1.5 %): PASS.
  6. Temperature = battery-thermal: PASS.
  7. Status Charging (wall) / Discharging (unplugged) / Not charging
     (Dragon port, 500 mA does not cover the load): PASS.
- Finding: battery-thermal stepped from 24.5 C to 15.4 C about 3 min into
  the charge, rose slowly to 15.8 C over 2 min, then stepped back to
  25.0 C. Stock's BAT_TEMP path differs from ours: live VBIF pull-up
  voltage (`RBAT_PULL_UP_VOLT_BY_BIF`, `mtk_battery.c:1068`), an IR
  compensation with the gauge current (`force_get_tbat_internal`), a
  re-read filter on jumps over 100 mV (`pmic_auxadc.c:707-713`,
  `bat_temp_filter`) and a 5 C / 20 s jump warning. Not traced; the
  filter goes into S4b, the trace is a stretch goal.
- Device left on S4a.

## 2026-10-26 — power S4b: state of charge + BAT_TEMP guard (PASS)

- Stock first: the vendor computes the state of charge in a closed
  userspace daemon (`fuelgauged`), so the method is ours (docs/power-plan.md
  S4). The 25 C profile LK reads (`battery0_profile_t1`, 100 points of
  mAh x 10 / mV x 10 / R x 0.1 mOhm) gives 4276 mAh to 3.4 V; resampled
  every 5 % into `ocv-capacity-table-0` on `/cosmo-battery`;
  `factory-internal-resistance-micro-ohms` = 171000 (median R).
- Change, gauge (`mt6358-gauge.c`): start value = OCV table at V - I x R
  (median of 5 samples); then counted charge against
  `cosmo,charge-full-microamp-hours` = 4050000 (Gemini measurement, same
  cell); anchors: 100 % on charger status Full, 0 % below 3.40 V, at most
  3 % below 3.45 V; 99 % shown until the charger reports done; 20 s poll
  plus charger events. New values: capacity, charge_now, charge_full,
  status Full.
- Change, AUXADC (new delta `drivers/iio/adc/mt6359-auxadc.c`): vendor
  BAT_TEMP guard (`pmic_auxadc.c:707-713`): below 200 mV or more than
  100 mV from the last reading -> 5 re-reads, mean without min/max, with a
  "BAT_TEMP jump" log line.
- Image `boot-s4b.img` sha256
  `2dc5f307168fdd1824adf3636ee7002677f07b7ceca8f636f8fa5335fc3b9801`.
  Flashed (explicit OK): `mtk w boot`.
- Results:
  1. Start: `OCV 4124910 uV (V 4096636 uV, I -81596 uA)`, 78 %;
     predicted 79 % from V/I before the flash: PASS.
  2. Reboot over SSH: 78 % before, start 78 % after: PASS.
  3. Anker PowerPort II, 880 s: +281.4 mAh = +6.95 % of 4050 mAh; percent
     73 -> 80 in 1-point steps, never above 99: PASS. (A first attempt
     saw no VBUS for 17 min: the charger had come out of the wall socket.)
  4. Unplugged / on Dragon: 78 -> 74 % with -170 mAh counted (4.2 %),
     1-point steps: PASS.
  5. No "BAT_TEMP jump" line in ~1 h of normal use: PASS.
  6. Charger detection (SDP / DCP) and emulated 56 / 49 / -11 / 1 C stop /
     resume unchanged: PASS.
- Charge current on the wall charger (question during the test): stock
  ceiling on a 5 V DCP is 2.05 A charge / 3.2 A input; 3.0 A only with
  PE / PE4.0 / PD and only below 85 % / 80 % (`mt6771.dts:4115-4175`,
  `mtk_charger.c:1661`); MT6370 limits ICHG 5.0 A, AICR 3.25 A. Live at
  73-80 %: CHG_STAT1 0x1d0 = 0x80 (power good only: no MIVR, AICR or
  thermal regulation), VBUS 4.65 V, IBUS 1.6-1.8 A, battery current
  1.26 -> 1.07 A: the CV loop at 4.40 V sets it. On the Dragon port 0x1d0 =
  0xa0 (AICR loop at 500 mA).
- Device left on S4b.

## 2026-10-26 — Wi-Fi W1a: CONSYS power-on check (PASS)

- Research first: docs/wifi-stock.md (W0). Route: stock 4.4 gen3 on 6.18;
  WMT core choice at W1b.
- Change: DT `consys-reserve-memory` (stock node verbatim), `consys@18070000`
  (supplies vcn18/vcn28/vcn33, TOPRGU CONN MCU reset, A-die pinmux states
  GPIO113-121), module `mt6771-consys` loaded by hand. Kernel image
  unchanged except the DTB.
- Image `w1a-boot.img` sha256
  `03a69af48653f64d0559e5ff3f668c9df0aa94bf7762c58a2ce85081a4c76d3d`.
  Flashed (explicit OK): `mtk w boot`. Escapes as S4b (power key 8 s,
  watchdog); rollback `s4b-boot.img`.
- First load: `dev_pm_domain_attach_by_id` = -ETIMEDOUT. The scpsys power
  controller never probes on this board: `power-domain@7: failed to get clk
  at index 1` (DISP wants mmsys clocks, mmsys disabled since try22), so
  there is no genpd provider and every domain stays as LK left it. Module
  reworked (0c1f5d3, module only, no reflash) to run the stock CONN
  MTCMOS sequence (`clk-mt6771-pg.c spm_mtcmos_ctrl_conn()`) with bounded
  polls.
- Result (module a9c64a8e…):
  - At probe CONN off: CONN_PWR_CON 0x112, PWR_STATUS 0x000fff4c /
    0x00007f4c (bit 1 clear), PROT_STA1 0x001ff098 (bits 13+14 set).
    SPM POWERON_CONFIG_EN reads 0x3.
  - After CONN on: CONN_PWR_CON 0x10d, PWR_STATUS bit 1 set in both,
    PROT_STA1 bits 13+14 clear. Top2 ack (bit 30) not set (stock's clk path
    does not check it).
  - Chip ID **0x0788** on the first read. ROM desel reads back 0x001702a4.
    After the MCU reset release CPUPCR = 0x00001170, steady (ROM idle,
    waiting for the patch).
  - Power-off in stock order: CONN_PWR_CON, PWR_STATUS and PROT_STA1 back
    to the probe values exactly; vcn18/vcn28/vcn33 disabled, PMIC CON0
    0x1c58/0x1d1c/0x1d2a/0x1d88 = 0. USB SSH unaffected.
  - Pins GPIO113-121: raw mode 9 at boot (LK), 0 after the test (stock
    power-off writes 0).
- PASS. Open: scpsys probe (DISP domain clocks) before GPU/audio, then
  move CONN to genpd; mtk-pm-domains powers CONN on at registration (no
  KEEP_DEFAULT_OFF), unlike stock.
- Device left on W1a (module unloaded state: CONSYS off).

## 2026-10-27 Wi-Fi W1b: stock connectivity stack, wlan0 scans

- Image `boot-w1b.img` b8ee5fab… (Image.gz-dtb 354cf47f…, 41fa7e2).
  Pre-flash check: Image built-in config unchanged from W1a (only module
  options: NL80211_TESTMODE, MTK_WCN_*); no change to reset, watchdog,
  PMIC keys or display; escapes as W1a. Whole module tree replaced on the
  device (TESTMODE changes cfg80211's ops layout).
- Firmware from the stock vendor partition (p35) and the WIFI/WIFI_CUSTOM
  NVRAM test copy from nvdata (p7) dumped read-only with debugfs; hashes
  match docs/wifi-stock.md.
- First try: chip ID 0xffffffff, so WMT took the external combo-chip power
  path and oopsed on its missing pinctrl. Stock wmt_loader sets the SoC
  chip ID (COMBO_IOCTL_GET_SOC_CHIP_ID + SET_CHIP_ID) before module init;
  the in-kernel launcher now does the same (module only, no reflash).
- Second try: CONSYS on, chip 0x0788, patches 1_1 (seq 1, 00 80 07 f0) and
  1_0 (seq 2, 00 80 09 00) downloaded, WIFI_RAM_CODE_6759 runs, `wlan0`
  registered, `iw dev wlan0 scan` lists 10-14 BSSs. 2.4 GHz only so far.
- 6.18 fix: gen3 wrote netdev->dev_addr directly (6 places, one pointer
  reassignment in P2P); now eth_hw_addr_set(). dev_addr warning gone.
- PASS (wlan0 scanning). Open: 5 GHz missing from scans; Debian sets
  kernel.panic_on_oops=0 at runtime (cmdline asks 1): the first oops did
  not reset the board. Next: W2 (gemini-debian DEVICE=cosmo autoload),
  W3 (join + SSH over Wi-Fi).

## 2026-10-27 Wi-Fi W2 + W3: Wi-Fi at boot, SSH over Wi-Fi untethered

- W2 (gemini-debian e717114, installed on the device from that checkout,
  no reflash): cosmo-wifi.service copies the Wi-Fi/WMT firmware from the
  unit's `vendor` partition and the WIFI/WIFI_CUSTOM NVRAM record from
  `nvdata` (read-only, debugfs), loads btif -> wmt_drv -> wmt_chrdev_wifi
  -> wlan_drv_gen3 and powers Wi-Fi on. Tested from a cold boot with the
  firmware and NVRAM copies deleted first: all six files copied, wlan0 up
  with the NVRAM MAC 00:08:22:dc:88:fb. PASS.
- W3: joined the home WPA2 network with NetworkManager (connection saved
  on the device only), DHCP 192.168.0.27. With the USB cable unplugged,
  SSH from the flashing host over Wi-Fi works. PASS.
- Panic: panic_on_oops=1 on the cmdline was never a boot parameter (kernel
  lists it as unknown). Now oops=panic in bin/build-boot-img.sh (7582370,
  next image) and kernel.panic_on_oops=1 in the rootfs (gemini-debian
  ce4f4e4, live on the device). Oops -> reset not yet observed.
- Open: 5 GHz absent from scans; gen3 logs every scan to the console at
  loglevel 8 (dmesg -n 1 by hand for console work).

## 2026-10-27 — AW9524 LED identification controls (PARTIAL)

- Stock-first evidence: docs/leds-stock.md. Stock source and backed-up
  dtbo entry 0 agree on AW9524 at i2c3 0x5b, reset GPIO109, quarter-scale
  global current and four RGB triplets. Physical positions still unknown.
- Isolated branch `codex/kbd-lights`, based on d532f79. New module
  `leds-aw9524`, `CONFIG_LEDS_AW9524=m`; i2c3 enabled, 12 stock-controlled
  pins exposed as LED class devices using pin-based labels. All channels
  start dark, brightness capped at 64. Reset on failure/unbind/shutdown.
- PASS: DTB built with W=1; warnings are the existing vendor/LK nodes
  (memory name, odm LED nodes and i2c4 unit address). Driver object built
  with W=1. checkpatch: 0 errors, 0 warnings after using fsleep for the
  stock 5 ms reset delays.
- PASS: dt-doc-validate on the new binding and dt-validate restricted to
  awinic,aw9524-leds on the compiled board DTB, using an isolated dtschema
  environment. Hydra lacks Python development headers for pylibfdt;
  schema validation ran on the flashing host instead.
- Generated config comparison against the current main build: only
  CONFIG_LEDS_AW9524=m added; no built-in configuration differences.
- Baseline device: 6.18.58-cosmo, no LED class devices, i2c3 absent,
  kernel.panic_on_oops=1. Read-only SHA256 of the first 18018304 bytes
  of /dev/disk/by-partlabel/boot matches w1b-boot.img exactly:
  b8ee5fab4db99ca505d61cb7ddd55c33740f0cddfb6a730adbab82753a10377c.
- No LED writes or reboot performed. Runtime/visual results pending.
- Keyboard PWM: PARTIAL (source receipts only); GPIO178 HDMI sharing and
  FIFO support need resolving. MT6370 LEDs remain disabled. Charge-state
  policy pending physical LED identification. Fn layout and RTC are
  outside this LED-focused session.

### Pre-flash check — AW9524 identification

- Change: enable i2c3 and bind only the new AW9524 LED controller; the
  module starts dark. No changes to PMIC keys/TOP_RST_MISC, watchdog,
  reset path, display/panel, voltages, charger or connectivity code.
- New config option: module only, CONFIG_LEDS_AW9524=m. First boot is
  reserved for escape checks under AGENTS.md rule 5, before LED tests.
- Escapes carried: disabled MT6358 keys leaves the preloader's power-key
  reset intact; unchanged init-owned hardware watchdog and systemd
  handover. Both were observed on prior W1a/W1b lineage. Neither has yet
  been observed firing on the new image. Watchdog-test image adds
  cosmo.wdtest=1 and is expected to reset without a normal root boot.
- Rollback: byte-verified w1b-boot.img above. Gemian restore staged,
  SHA256 745615599521a72ca94dc8df350d4587021891e98d56dced0ec600c49ac6ba2b.
  Manual recovery readiness must be confirmed at test time.
- Preserve installed Wi-Fi modules and rootfs service (5 GHz fix in
  gemini-debian 99ccaf7); install only the new LED module for testing.
- Explicit flash/reboot approval pending. No flash performed.

### AW9524 build completion and staged images

- PASS: full `JOBS=6 bin/build-kernel.sh`, then incremental full rebuild
  with the final driver source. Build host suspended during the first
  build; after wake the build continued and completed. Release
  6.18.58-cosmo, module tree 80 MiB. Final standalone driver compile
  with W=1 also passed without warnings.
- Final build source matches the committed delta (6cd95d8).
  Module vermagic: `6.18.58-cosmo SMP preempt mod_unload aarch64`;
  OF alias awinic,aw9524-leds; no module dependencies.
- PASS: both images packed with bin/build-boot-img.sh, including its
  check that MT6358 keys remain disabled with no long-press-mode.
- Image.gz-dtb SHA256:
  `1cdfca4b5635f4403b5939f9c9a9d4b1ecfb46e98020a7d033941f5ac077270e`.
- Normal `codex-aw9524-boot.img` SHA256:
  `d5d4694e7f590a2762b529d352d976e1953f03750e2d8588e85856db20bd4aec`.
- Watchdog test `codex-aw9524-wdtest-boot.img` SHA256:
  `ed281ef51d98a9936d79b2cf2532cd2be68ef292b99b146235544abff7afc54d`.
  Adds cosmo.wdtest=1: init arms the watchdog and stops feeding it before
  root handoff, so SSH is not expected and reset should occur in 31–62 s.
- Installed-form `leds-aw9524.ko` SHA256:
  `73f79823a1647e13d5b351532ce7528d4112bf6373b7cde8d7afd0cc8564059a`.
- Flashing-host staging: `Build/codex-led-artifacts/6cd95d8/` because the
  active filesystem sandbox cannot write the usual cosmo-flash directory.
  The staging location needs to be made clear in the flash approval.
- No modules installed on the device; no LED writes, flash or reboot.
  Device remains on byte-verified W1b. Runtime LED tests and the two
  new-image escape observations remain pending explicit approval.

### AW9524 first flash approval

- Explicit approval received for the watchdog-test flash and reboot,
  including the written pre-flash check and manual recovery readiness.
- Rechecked main: d532f79, ancestor of the LED branch. All W1b kernel
  connectivity changes are included; branch diff touches only the AW9524
  implementation, board LED node, configuration and documentation.
  Existing cosmo-wifi.service is active; rootfs 5 GHz fix is preserved.
- All staged artifact SHA256 checks pass. Intended first image:
  codex-aw9524-wdtest-boot.img, release 6.18.58-cosmo,
  SHA256 ed281ef51d98a9936d79b2cf2532cd2be68ef292b99b146235544abff7afc54d.
- Flash pending USB connection; no write or reboot performed yet.

### AW9524 first flash attempt — USB sandbox prevents handshake

- USB connected; fresh hashes of both images/module passed. Approved
  `mtk w boot` launched for the watchdog-test image, then one approved
  `sudo systemctl reboot` over Wi-Fi.
- Host USB log: MediaTek preloader 0e8d:2000 enumerated, then normal Linux
  USB gadget returned. mtk failed to get USB configuration/handshake;
  it never reached a partition write and was stopped with Ctrl-C.
- Cause: active command sandbox hides /dev/bus/usb. lsusb enumerates IDs
  through sysfs, but /dev/bus does not exist inside the sandbox. Raw USB
  flashing must run from a normal host terminal or a session with USB
  device access; no attempt to bypass the sandbox was made.
- Current boot is the existing W1b rescue initramfs. USB telnet confirmed
  `last root boot never reported in: RESCUE initramfs (next boot tries again)`.
  Init owns/feeds the watchdog (31 s timeout). SSH/Wi-Fi are unavailable
  in rescue; USB telnet at 10.15.19.83 works.
- Fresh read-only boot-partition prefix hash still equals
  b8ee5fab4db99ca505d61cb7ddd55c33740f0cddfb6a730adbab82753a10377c.
  No boot-image change or LED writes occurred. New-image escape checks
  remain untested. Next reboot needs fresh approval under the task rule.

## 2026-10-27 — AW9524 rear LED tests and normal-image escape checks

- Watchdog-test image flashed externally after raw USB access proved
  unavailable in the command sandbox. Automatic reboot cycles were
  visually reported; host USB logs show gadget disconnect, preloader,
  then gadget again. Watchdog escape PASS by observation; exact reset
  timing and watchdog-image partition hash were not captured.
- Normal codex-aw9524-boot.img flashed externally with explicit approval.
  Rescue USB telnet read back the first 18018304 boot bytes:
  d5d4694e7f590a2762b529d352d976e1953f03750e2d8588e85856db20bd4aec.
  Release 6.18.58-cosmo; no cosmo.wdtest=1; uptime above 79 s with the
  init-owned 31 s watchdog fed normally.
- Rescue was selected by the existing incomplete-root-boot tripwire;
  its marker permitted Debian on the next boot. Approved power-key reset
  was physically performed and reported complete; USB SSH then reached
  Debian 13. Power-key escape PASS by observation. panic_on_oops=1;
  cosmo-boot-ok Result=success, ExecMainStatus=0.
- Only leds-aw9524.ko installed (SHA256
  73f79823a1647e13d5b351532ce7528d4112bf6373b7cde8d7afd0cc8564059a),
  depmod and modprobe. Existing Wi-Fi modules/service retained.
- Probe: `[102.712872] aw9524-leds 3-005b: AW9524 ID 0x23, LED current controls ready`.
  Twelve devices initially brightness=0, max_brightness=64.
- Physical mapping and colour tests: docs/leds-stock.md runtime section.
  Rightmost RGB confirmed, including purple from red=32/blue=32.
  Fingerprint top/bottom-left green and blue confirmed; red unobserved
  at 32 and 64 despite matching stock's red sink assignments.
  Independent red charging indicator remains unresolved.
- Reload test: LED devices removed/reappeared, all twelve returned zero;
  `[3515.796344] aw9524-leds 3-005b: AW9524 ID 0x23, LED current controls ready`.
  No AW9524/I2C errors in filtered log. End state: all twelve off,
  triggers none. No device reboot or flash during colour/reload tests.
- Status: rear controls PASS; full cover/charge LED identification PARTIAL.
  Keyboard backlight PARTIAL (source/DT/clock receipts only); Q-key LED
  not yet investigated. Fn layout/RTC outside this LED-focused session.
- Next: keyboard PWM1/GPIO178 and Q-key LED, stock-first; further stock
  fingerprint-red investigation remains separate. No stock restore.

## 2026-10-27 — stock FIFO keyboard backlight prepared

- Worktree codex/kbd-lights continued from 7566852; no merge/push.
  Dedicated leds-cosmo-kbd module and binding replace the general PWM
  provider exclusively. Stock PWM1 FIFO levels 0–5 and GPIO90 PWM_A;
  GPIO178 is only lowered when GPIO54 reads low. Detailed source
  receipts, scope limits and runtime pass/fail plan: docs/leds-stock.md.
- PASS: full JOBS=6 bin/build-kernel.sh; final driver W=1 object compile;
  strict checkpatch 0 errors/warnings/checks; git diff --check.
  DTB W=1 builds with existing vendor-node warnings; no new-node warning.
  Binding dt-doc-validate and filtered new-node dt-validate PASS.
- Kernel 6.18.58-cosmo. Requested config adds only LEDS_COSMO_KBD=m;
  LEDS_MT6370_RGB and REGULATOR_MT6370 remain disabled.
  No changes to display, charger, PMIC keys, watchdog, reset or Wi-Fi.
- Image.gz-dtb SHA256:
  b9747f9b89ede73ba9c42bc6be585f62cc1ad49595c6fcb7c91d6a304e366f7b.
- Normal codex-kbd-fifo-boot.img SHA256:
  15f4f4e55a22e7c6d56740649ab93c03a204c8c04dbc5b3cd008fe8ea90ccd91.
- codex-kbd-fifo-wdtest-boot.img SHA256:
  3352c437073df6c1d9d17144f3b48e5100e7b0d79b47cf446b575a5a721aa135.
- leds-cosmo-kbd.ko SHA256:
  63ce1d381febc2f86f25334b5e18ec116b88cf8ab0f31ab4b90f1f5588352352.
- Build logs: /tmp/codex-kbd-build.log, /tmp/codex-kbd-object-final.log.
  Both images packed with bin/build-boot-img.sh; PMIC-key safeguard PASS.
- Device read-only checks: boot-ok Result=success / ExecMainStatus=0;
  all four PWM channels disabled and unrequested. No keyboard PWM or
  GPIO178 write, module installation, flash or reboot performed.
- Q-key LED reported currently off. Stock AW9524 supports separate
  current sinks; its keyboard callback explicitly selects the SoC PWM1
  controller, not MT6370 PWM. Stock MT6370 indicator names do not prove
  Q-key placement. Physical controller identification remains pending.

### Written pre-flash check — approval pending

- Change: new keyboard module configuration and board PWM node; no
  keyboard module in the current rootfs, so keep it uninstalled until
  both escape checks have passed. First new-config boots only prove
  recovery, then install only the new module for keyboard testing.
- Risk flags: new kernel config YES; PMIC keys/watchdog/reset/display
  changes NO. MT6370 LED driver NO. HDMI operation untested; future HDMI
  driver needs shared GPIO ownership. No stock restore is planned.
- Both escapes retained: disabled mt6358keys leaves TOP_RST_MISC alone;
  init-owned watchdog retained. Watchdog test deliberately stops feeding
  via cosmo.wdtest=1. Power-key and watchdog observed on the prior normal
  AW9524 config, NOT yet observed on this new keyboard config.
- Known-good normal AW9524 image is staged and freshly hash-verified:
  d5d4694e7f590a2762b529d352d976e1953f03750e2d8588e85856db20bd4aec.
  Recovery must use that approved mainline image, never stock.
- Obtain explicit approval before EACH flash/reboot, including the
  watchdog-test image and subsequent normal image; confirm manual
  recovery readiness before the first flash. No approval inferred here.
- Current device remains normal codex-aw9524-boot.img, 6.18.58-cosmo.
  Keyboard runtime status PENDING; source/build checks PASS.

### Q-key follow-up: first AW9524 green channel

- User reported the Q-key LED off before testing. P0_1 alone was set
  to 32/64 with a 1000 ms on / 1000 ms off timer for 30 seconds.
  User observation: no light. Automatic cleanup selected trigger none
  and brightness zero; subsequent readback confirmed both.
- This does not identify the Q-key controller or prove AW9524 absence.
  No other channel was changed; no keyboard PWM/GPIO178 write occurred.
- Artifacts from 94fd128 copied to host staging
  Build/codex-kbd-artifacts/94fd128; all three hashes match Hydra.
  Flash/reboot approval remains pending.

### Normal keyboard-image handoff approved

- User explicitly waived the separate watchdog-test boot for this
  backlight-only change. Both recovery mechanisms remain enabled;
  no recovery, keyboard scanning or reset code was changed.
- User requested a staged image and command for manual flashing.
  No flash or reboot was performed by the agent.
- Normal image freshly verified on the flashing host:
  Build/codex-kbd-artifacts/94fd128/codex-kbd-fifo-boot.img
  SHA256 15f4f4e55a22e7c6d56740649ab93c03a204c8c04dbc5b3cd008fe8ea90ccd91.
- This authorization supersedes the pending watchdog-test sequence
  above. Next: manual normal-image flash, manual power-key boot,
  read-only boot identity check, then install only leds-cosmo-kbd.ko.
- Device last checked still on normal AW9524 image, boot-ok success.
  Keyboard runtime testing remains pending.

## 2026-10-27 — normal keyboard image boot and module probe

- User manually flashed and booted the normal keyboard image.
  USB SSH reached 6.18.58-cosmo; first 18018304 boot bytes SHA256:
  15f4f4e55a22e7c6d56740649ab93c03a204c8c04dbc5b3cd008fe8ea90ccd91.
  Live PWM node compatible planet,cosmo-kbd-backlight.
  Boot-ok Result=success, ExecMainStatus=0.
- Installed only leds-cosmo-kbd.ko, verified SHA256
  63ce1d381febc2f86f25334b5e18ec116b88cf8ab0f31ab4b90f1f5588352352,
  depmod then modprobe. Existing Wi-Fi and AW9524 modules retained.
- Probe PASS: [43.325284] cosmo-kbd-backlight 11006000.pwm:
  Stock PWM1 FIFO keyboard levels 0-5 ready.
  LED cosmo::kbd_backlight initially 0, maximum 5.
- First keyboard PWM/GPIO178 write: brightness 1 with automatic
  brightness-zero cleanup after 30 seconds. Sysfs readback 1.
  Visual result pending. No agent flash/reboot or MT6370 LED writes.

### Keyboard runtime PASS — visual levels, typing, off and reload

- User observed keyboard illumination at level 1; Q-key LED did not
  visibly light. Automatic 30 s cleanup returned brightness to 0.
- Six seconds each at 1–5 followed by 0: user confirmed progressively
  brighter illumination, then off, with typing still working.
- GPIO54 stayed low. GPIO178 high while lit, low when off. PWM top,
  channel 1 and HCLK each clock count 1 while on, 0 while off; rates
  unchanged. Driver unload/reload succeeded, returning brightness 0/max 5.
- No warning/oops in checked log; boot-ok Result=success, ExecMainStatus=0.
  Suspend and HDMI-high preservation remain untested.
- Current device: normal codex-kbd-fifo-boot.img, 6.18.58-cosmo,
  SHA256 15f4f4e55a22e7c6d56740649ab93c03a204c8c04dbc5b3cd008fe8ea90ccd91.
  Keyboard module installed/loaded, brightness 0. No agent flash/reboot.
- Q-key controller unresolved. No MT6370 LED enable or register writes.

## 2026-10-27 — MT6370 general indicator build, runtime pending

Enable/build authorized for the Q-key LED investigation. Upstream
leds-mt6370-rgb is modular; DT declares only ISNK1–3, default off,
4 mA maximum each, expected brightness range 0–1. ISNK4 is omitted to
preserve hardware-auto red charging indication. Physical mapping remains
unidentified; startup register effects and bounded runtime plan are in
docs/leds-stock.md. No target writes, install, flash or reboot performed.

Validation:

- Full bin/build-kernel.sh PASS, release 6.18.58-cosmo.
- Generated config diff: only LEDS_MT6370_RGB n -> m; multicolor class
  already m. REGULATOR_MT6370 and BACKLIGHT_MT6370 remain disabled.
- W=1 indicator object build PASS without warnings. W=1 DTB PASS with
  existing /memory, /odm LED and i2c4 unit-address warnings only.
- checkpatch 0 errors/0 warnings; git diff --check PASS.
- New indicator node dt-validate PASS against the pinned upstream
  indicator/common/multicolor schemas, isolated from unrelated schemas.
- Both boot pack operations passed the MT6358 keys-disabled guard.
- Indicator module vermagic: 6.18.58-cosmo SMP preempt mod_unload aarch64;
  dependency led-class-multicolor.

Artifact SHA256:

- codex-mt6370-indicator-boot.img:
  881b7ff0fbdafa935968ac9b5cc6c15b40ae1c68c84cd95713c2067214305448
- codex-mt6370-indicator-wdtest-boot.img:
  90a8d585f3860b5b85d208ab42a173a4034c22126d14322a2b40a6fcade3e00b
- leds-mt6370-rgb.ko:
  96c3268b26b5cb14b0b83733810965e4052cf0a3faa079402136d0622bbaf0b5
- led-class-multicolor.ko:
  1f60a5c729e9a57bfa76f6cb0f2c8bde0b0aae6eda9c47b8c9dd39bd9a7b394c

### Written pre-flash check — approval and runtime still pending

Change: enable modular MT6370 general indicator and declare three low-current
outputs. New configuration: yes. Panel/display/backlight/DSV/LDO, charger,
Wi-Fi, PMIC keys/TOP_RST_MISC, reset and watchdog code: unchanged.
Probe modifies only declared indicator mode/current/enable fields; ISNK4
charge-auto control is not requested. Physical wiring remains unknown.

Both images retain the power-key hardware reset and the armed watchdog.
The watchdog-test image adds cosmo.wdtest=1; normal image feeds normally.
Neither recovery mechanism has been observed on this new configuration.
First boot must establish recovery per AGENTS.md rule 5 before LED probing;
manual recovery readiness must be confirmed. Previous-config observations
do not count as new-config validation. No flash/reboot approval inferred
from the enable/build instruction. No stock restore planned.

Current device remains the normal keyboard FIFO image; keyboard/AW9524 and
Wi-Fi retained. Last verified boot image SHA256:
15f4f4e55a22e7c6d56740649ab93c03a204c8c04dbc5b3cd008fe8ea90ccd91.
Runtime indicator result: PENDING.


## 2026-10-27 — recovery-test policy correction for indicator work

The project owner narrowed the dedicated watchdog-test requirement to
changes that modify keyboard input. A new kernel configuration by itself,
keyboard backlight work, or indicator-only work does not require another
watchdog-test boot. AGENTS.md rule 5 and the indicator runtime plan now
reflect this policy. Both recovery mechanisms remain enabled; the written
pre-flash check and flash/reboot authorization requirements remain.

This supersedes the preceding indicator pre-flash check's requirement to
re-prove both escapes on the new configuration. Proceed directly with the
normal indicator image under the existing proceed authorization.

A watchdog-test flashing command was supplied before the correction.
Boot was reported, but neither live image identity nor an automatic reset
was confirmed: USB SSH and rescue telnet both timed out. Do not record a
new watchdog PASS or assume the current image is normal. Next step is the
normal indicator image flash and boot identity check. No agent-initiated
flash, reboot, module installation or LED register write occurred.


## 2026-10-27 — normal indicator image verified, module probe PASS

Normal boot partition first 18020352 bytes SHA256 matches
881b7ff0fbdafa935968ac9b5cc6c15b40ae1c68c84cd95713c2067214305448.
Kernel 6.18.58-cosmo, no cosmo.wdtest=1; boot-ok Result=success,
ExecMainStatus=0. DT has three 4000-uA indicator outputs.

Installed only leds-mt6370-rgb.ko and matching led-class-multicolor.ko,
with the previously installed dependency saved in /tmp before replacement.
Copied hashes match build artifacts (96c3268b... and 1f60a5c7...).
Depmod/modprobe succeeded; all three cosmo:mt6370:isnk1–3 class devices
initially brightness 0, maximum 1, trigger none. Charging status remained
Charging. Existing keyboard, AW9524 and Wi-Fi modules retained.
No agent flash/reboot. Physical mapping and visual darkness unconfirmed.

First bounded test: ISNK1 timer, 1000 ms on/off, 15 seconds, managed by
systemd-run cosmo-isnk1-test with EXIT cleanup (trigger none, brightness
0), independent of SSH lifetime. Trigger and delays read back correctly
while the unit was active. Visual observation and final cleanup verification
pending. No ISNK2/3 illumination requested.

ISNK1 cleanup verified: systemd unit completed successfully; all three
channels read brightness 0, trigger none. Charging remains Charging.
Physical observation pending.


### Persistent visual tests

The observer saw nothing during the first timed ISNK1 test and requested
that future tests remain running until visual confirmation arrives.
ISNK1 was restarted at brightness 1 with timer 1000/1000 ms; trigger and
delays verified. Left running for observation, ISNK2/3 off, charger status
Charging. Stop/switch after the next observation; no current increase.
The runtime plan now uses persistent tests in place of timed expiry.


### ISNK1 rear indicator observation; ISNK2 test active

Visual observation identifies ISNK1 as controlling a colour component of
the rear charging LED: the observer reports that LED changing colours.
Exact emitted colour was not specified; do not label it verified green.
This is not a Q-key LED mapping. Desired later policy: rear charging LED
should follow the Q-key LED behaviour once that indicator is identified.
No policy implementation yet.

ISNK1 stopped (trigger none, brightness 0). ISNK2 started at brightness 1,
timer 1000/1000 ms, left running for observation; ISNK3 remains off.
Charging still reports Charging. Next: collect location/colour observation,
then stop/switch the active test. No current increase or ISNK4 takeover.


### ISNK2 rear indicator colour observation; ISNK3 active

Observer reports the same rear charging LED changing from red to purple
with ISNK2 active. This is consistent with an added blue component and
matches the stock blue name; blue is inferred from mixed colour rather
than observed in isolation. Q-key indicator remains unidentified.

ISNK2 stopped (trigger none, brightness 0). ISNK3 started at brightness 1,
timer 1000/1000 ms, left running for observation. ISNK1/2 off. Trigger and
delays read back correctly; charging reports Charging. No ISNK4 change.
Next: collect ISNK3 physical location/colour observation, then stop/switch.


### ISNK3 negative observation; AW9524 P0_1 retest active

ISNK3 produced no visible blink by observation. This does not establish
that it is red or unused; ISNK4 is the separate stock red/charge output.
Stopped ISNK3, confirmed ISNK1–3 brightness 0 and Charging status.
Stock keyboard source aw9523_key.c:640–648 has matrix initialization but
no explicit Q/caps LED control found in targeted searches. Generic ODM
notification LED entries are disabled (k71v1_64_bsp.dts:34–70).

Retesting AW9524 P0_1 with persistent timer 1000/1000 ms, brightness 32/64,
same setting as earlier timed negative test. Trigger/delays verified;
left running until observation. No other AW9524 channel changed. Do not
claim either controller ruled out from negative observations alone.


### Caps-lock clue and primary-source internal LED mapping

Observer reports no P0_1 blink and supplies the caps-lock LED name.
P0_1 stopped and readback 0 confirmed. Primary Gemian project wiki:
https://github.com/gemian/gemian/wiki/LEDs
(raw https://raw.githubusercontent.com/wiki/gemian/gemian/LEDs.md)
explicitly maps the internal RGB LED near Tab/Esc to proc selectors
11x/12x/13x. Vendor aw9524_key.c:345–353 maps those to P0_0/P0_1/P0_2.
Thus the first triplet has documented internal-LED intent, although this
unit has not visibly responded in earlier tests. The caps-lock clue is
useful for finding userspace policy but is not alone a wiring receipt.

P0_0 red now persistently blinking 1000/1000 ms at 64/64, the previously
used test limit (not a current increase). Await observation. P0_1 off,
MT6370 ISNK1–3 off; automatic red charging indication not changed.
No keyboard input code change or raw I2C write performed.


## 2026-10-27 — internal LED closed; stock thermal policy captured

- AW9524 P0_2 dark on mainline (also with keyboard backlight on), on
  Gemian 4.4 at stock maximum, and on stock Android 9. Planet Settings
  never drives LED group 1. Details: docs/leds-stock.md.
- Device path for the test: Gemian restore (boot, stock dtbo, userdata),
  then stock Android boot (25 Oct dump of `boot`, sha256 d8be455c…),
  userdata head zeroed, `metadata` zeroed and a `boot-recovery
  --wipe_data` message in `para` (first boot hung until then).
- Captured stock Android thermal policy and GPU table into
  docs/stock-reference.md.


## 2026-10-27 — display D0 (read-only) and D1 build

Research (stock first): panel NT36672, LK LCM `aeon_nt36672_fhd_dsi_vdo_x800_datong`,
ID read by LK 0x80/0x70. The stock source file
`lcm/aeon_nt36672_fhd_dsi_vdo_x600_xinli` registers that name; its init
table (185 commands) is byte-identical to the stock kernel's and LK's
compiled tables. Stock kernel disassembly: power-on = reset 0, 20 ms, 1,
10 ms, 0, 10 ms, 1, 20 ms, then the table; suspend = 0x28, 50 ms, 0x10,
120 ms, +10 ms, then dsv_neg off, dsv_pos off. Bias = MT6370 DSV over I2C,
5.4 V each (no ENP/ENN GPIOs). Panel reset = LCM_RST pad (GPIO45).

D0, read-only (regmap debugfs window reads, LK picture lit):
DB_CTRL1 0x1b0 = 0x00, DB_CTRL2 0x1b1 = 0x7a (VPOS and VNEG enabled),
VBST 0x1b2 = 0xe2 (5.7 V), VPOS/VNEG 0x1b3/0x1b4 = 0x5c (5.4 V),
DB_STAT 0x1df = 0x00. BL_EN 0x1a0 = 0x7e, BL_BSTCTRL 0x1a1 = 0xed,
BL_PWM 0x1a2 = 0xb4, DIM 0x1a4/0x1a5 = 0x05/0x5f (level 766).
GPIO45 pad high. PASS.

D1 change (no panel write): MT6370 DSV regulators (5.4/5.4/5.7 V, equal
to LK, no consumer, kept on by regulator_ignore_unused) and MT6370
backlight (stock limits, max 768, default 766, I2C-only dimming; driver
delta writes the whole BL_PWM field so the PWM input is switched off).

Pre-flash check:
- Image `out/d1-boot.img` SHA256
  2836f713a4bb186df4e3412cc0620f2e595dfcc8d2a9e62d150417831ecc5a59.
- Risk: display-related (backlight register writes at probe). Expected
  visible effect: none, or a brightness step if LK's DISP_PWM duty was
  below 100 %.
- Escapes: power-key reset and watchdog unchanged from the observed
  kernel family (MT6358 keys still disabled; build-boot-img check passed).
- Rollback: codex-mt6370-indicator-boot.img (881b7ff0...).
- Rootfs modules unchanged (no MODVERSIONS; no loaded module uses the
  backlight class).
- Pass: no blank or flicker through boot; dsvpos/dsvneg read enabled at
  5.4 V; BL_PWM bit 7 clear; bl_power 4 turns the backlight off and 0
  back on; brightness steps visible. Fail: any blank, flicker, colour
  fault or probe error.

### D1 result: PASS

- Flashed with explicit approval; booted 6.18.58-cosmo #27 (fd3dc52).
- No probe errors. dsvpos/dsvneg `enabled` 5400000 uV, dsvbst 5700000 uV;
  DB_CTRL2 0x7a, DB_STAT 0x00 (unchanged from D0).
- BL_PWM 0xb4 -> 0x30 (PWM input off); BL_EN 0x7e, BL_BSTCTRL 0xed,
  DIM 05/5f unchanged; brightness 766 of 768.
- Observed on glass: 15-cycle loop 766 / 384 / 100 / bl_power 4 (off),
  every step visible, no flicker or colour fault, restored to 766.
- Not observed: brightness at the probe switch-over from PWM to I2C.

## 2026-10-27 — display D2: mainline pipeline + NT36672 panel (pre-flash check)

Change:
- mediatek-drm main path OVL0 -> OVL_2L0 -> RDMA0 -> COLOR -> CCORR -> AAL
  -> GAMMA -> DITHER -> DSI0, mipi-tx, mmsys, mutex enabled; second path
  (OVL_2L1, RDMA1, DPI) stays off. Built in (DRM_MEDIATEK, PHY_MTK_MIPI_DSI,
  DRM_PANEL_COSMO_NT36672 = y).
- scpsys power controller (&spm) disabled explicitly: with mmsys clocks
  present it would probe and power on every domain (CONN included). It
  never probed before (power-domain@7 clock missing), so state is
  unchanged. power-domains and iommus removed from the enabled display
  nodes: buffers come from CMA, scanned out by physical address as LK
  does (M4U/SMI stay disabled).
- New panel driver drivers/gpu/drm/panel/panel-cosmo-nt36672.c: stock
  init table (181 commands, generated from the table verified against the
  stock kernel and LK), then 0x11 + 120 ms, 0x29 + 10 ms. Commands < 0xB0
  as DCS, others generic, LP mode. prepare: reset low, dsvpos then dsvneg
  on, reset 0/20/1/10/0/10/1/20 ms, table. unprepare: 0x28 + 50 ms, 0x10 +
  120 ms, 10 ms, dsvneg then dsvpos off, reset left high (stock); logs
  "panel off, bias off" from the MT6370 enable bits, warns if still on.
  prepare_prev_first, backlight via drm_panel.
- GPIO45 to GPIO mode, output high (LK level) at probe.
- mtk_drm_drv.c delta: aperture_remove_all_conflicting_devices() after
  kms init, so fbcon moves from simpledrm. A bind failure before that
  keeps simpledrm and the LK picture.
- Boot cmdline loglevel=3 (600ae80).

Pre-flash check:
- Image `out/d2-boot.img` SHA256
  c2501c8f68263d8fa4e4819d01b99b1949f6014999f375b693d41b10ec825c34.
- Risk: first mainline panel write (panel rule: user present, one OK per
  panel write). Expected on glass: LK picture, one blank at the takeover
  (DSI/PHY re-init + panel reset and init), then the console on the new
  framebuffer. DSI data rate is the mainline value (pclk 160.439 MHz x 24
  / 4 = 962 Mbps per lane) instead of stock 1100 Mbps; burst mode.
- Escapes: power-key reset and watchdog unchanged from the observed kernel
  family; boot-image packer check passed. SSH over USB does not depend on
  the display.
- Rollback: d1-boot.img (2836f713...) or codex-mt6370-indicator-boot.img
  (881b7ff0...).
- Rootfs: new modules.builtin* copied and depmod run, so the old
  mediatek-drm/mipi-dsi/backlight modules are not loaded over the
  built-in drivers. Other modules unchanged.
- Pass (boot): console readable and upright after one blank; dmesg
  "panel on", no DSI errors. Pass (colour): red/green/blue/white bands
  appear in that order and colour. Pass (blank): fb blank -> dark,
  "panel off, bias off", dsvpos/dsvneg read disabled; unblank -> picture
  back, "panel on", bias enabled. Fail: flicker, colour fault, lines,
  bias reading on after blank, or any DSI error. Stop at the first fault.

### D2 result: FAIL (hang), rolled back to D1

- Flashed with explicit approval. On glass: LK picture, then blank as
  Linux started (simpledrm removed), backlight on, no picture after.
  Host USB log: gadget up 15:23:15, gone 15:23:37, up 15:24:00 (reboot
  loop, likely panic=10), then a hang with no reset: the display drivers
  were built in, so the bind ran before init armed the watchdog. Only the
  power-key reset escaped. No log survived (raw `cache` holds only the old
  Android ext4; the Debian boot writes no log there).
- Rolled back with `mtk w boot d1-boot.img`; D1 boots as before.
- Lesson: a built-in driver that can hang runs before the watchdog escape.

### D2b: same code, display stack as modules (pre-flash check)

- DRM_MEDIATEK, PHY_MTK_MIPI_DSI, DRM_PANEL_COSMO_NT36672 = m. Cmdline adds
  `modprobe.blacklist=mediatek_drm` (alias autoload blocked; explicit
  modprobe works). Image `out/d2b-boot.img` SHA256
  6cced53fce234a1bca4b2340cc9177bfa7cc229cd6978ef7673849b81af616bf.
- Installed on the rootfs from this build, then depmod: mediatek-drm.ko
  (6d0d7d0e...), phy-mtk-mipi-dsi-drv.ko (1022547b...),
  panel-cosmo-nt36672.ko (19ad49ec...), drm_display_helper.ko,
  drm_dma_helper.ko. Other modules unchanged.
- Plan: boot to SSH with LK picture (simpledrm), `dmesg -w` streamed to
  the host, then `modprobe mediatek_drm` by hand. Watchdog armed by then;
  power-key reset as the second escape.
