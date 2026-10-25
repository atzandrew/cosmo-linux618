# P0 findings — boot contract

Source: read-only mtkclient dump of `lk`, `lk2`, `boot`, `dtbo`, `expdb`
from the Cosmo on stock Planet Android, 2026-10-25 (`docs/session-log.md`).
`expdb` holds LK's own boot log from several earlier boots; quotes below
are from it (`strings expdb.bin`) unless marked otherwise. Dumps live
outside the repo (`~/cosmo-p0/20261025/` on the flashing and build hosts).

## Boot image (`boot`, 32 MiB)

`boot.bin` header, decoded with a struct parse:

| Field | Value |
|---|---|
| magic / header version | `ANDROID!` / **1** (header_size 1648, no recovery dtbo) |
| page size | 0x800 |
| kernel | 0x91a47c bytes at **0x40080000**: `Image.gz` (gzip `1f8b`) + appended DTB at kernel offset 0x8f6462, DTB size 0x2401a |
| ramdisk | size **0**, addr 0x55000000 (system-as-root; LK adds `skip_initramfs`) |
| tags (DTB) addr | 0x54000000 |
| os_version | 0x12000151 |
| cmdline | `bootopt=64S3,32N2,64N2 buildvariant=user veritykeyid=id:…` |

Stock kernel banner (`strings` on the extracted Image): `Linux version
4.4.146 (server@server) (… clang version 6.0.2 …) #2 SMP PREEMPT Thu Feb 4
17:08:34 CST 2021`.

LK side (log):
- `ZBOOT FDT bldr_load_dtb from boot` → `Kernel(1) zimage_size:0x91a47c,dtb_addr:…(dtb_size:0x2401a)`:
  LK takes the DTB appended to `Image.gz`, as the 4.4 build does.
- `[LK] fdt setup addr:0x54000000`; reserved window
  `dtb_kernel_addr_mb` 0x54000000 size **0x80000 (512 KiB)**: the merged
  DTB must fit (`Error: merged size %d > DTB_MAX_SIZE!` in LK strings).
- `kernel_addr_mb` 0x40080000 size 0xc800000 (200 MiB);
  `ramdisk_addr_mb` 0x55000000 size **0x1000000 (16 MiB)**: an initramfs
  larger than 16 MiB would overrun. Our `Image.gz-dtb` (15 MB) fits `boot`.

Final cmdline LK hands the kernel (log, `cmdline:`; serial number and
other unit IDs omitted here):

```
console=tty0 console=ttyS0,921600n1 vmalloc=496M slub_max_order=0 slub_debug=OFZPU
androidboot.hardware=mt6771 firmware_class.path=/vendor/firmware loop.max_part=7
has_battery_removed=0 maxcpus=8 androidboot.bootpartition=boot
androidboot.verifiedbootstate=green androidboot.bootpartition=boot skip_initramfs
rootwait ro init=/init root=/dev/dm-0 dm="system none ro,0 1 android-verity PARTUUID=… "
androidboot.veritymode=enforcing <header cmdline> androidboot.meta_log_disable=0
androidboot.dtbo_idx=0 printk.disable_uart=1 bootprof.pl_t=… bootprof.lk_t=…
bootprof.logo_t=… boot_reason=0 androidboot.serialno=… androidboot.bootreason=reboot
gpt=1 usb2jtag_mode=0 mrdump_ddrsv=yes mrdump_cb=0x11e000,0x2000
```

Notes: the header cmdline sits *after* LK's `root=/dev/dm-0 … init=/init`,
so a `root=`/`init=` in our header wins. `printk.disable_uart=1` is
vendor-only (mainline ignores it). Keep `bootopt=` (Gemini lesson).

## DT overlay (AGENTS.md P0 Q1)

- There is **no `odmdtbo` partition** on this unit. LK's strings name both
  `odmdtbo` and the Android DT-table path (`app/mt_boot/odm_mdtbo.c`); the
  log shows it uses `dtbo`:
  `PCB_ID: EVB (MTK0754), dtb idx = 0` (from `pin111 = 0, pin175 = 0`) →
  `real mdtbo index=0` → `hw_mdtbo_index: 0, dtbo_offset: 128, dtbo_size: 54231`
  → `[PROFILE] ::: lvl(3) Overlay takes 17 ms` → `early load dtb takes 54 ms`.
- `dtbo.bin`: Android DT table (magic `d7b7ab1e`, version 0), 3 entries
  (id 0/1/2) that all point to the **same** 54,231-byte overlay at
  offset 0x80.
- The overlay: 280 fragments, targets by label through `__fixups__`
  (`&pio` ×12, `&touch` ×3, `&i2c1`, `&spi0-2`, `&scp`, `&reserved_memory`,
  `&tcpc_pd`, …). The vendor base DTB carries `__symbols__`.
- **Consequence:** applied to our mainline DTB, the fixups cannot resolve
  (no `__symbols__`, different labels), so `ufdt_apply_overlay()` fails.
  **Answered 2026-10-25 (first boot of our image, LK log read from
  `expdb`):** LK panics:
  `ERROR: ufdt_overlay_do_fixups():No node __symbols__ in main dtb.` →
  `ufdt_apply_overlay() failed!` →
  `panic (caller 0x5602c2d3): ASSERT at (app/mt_boot/fdt_op.c:212): 0`,
  then the LK watchdog resets (~23 s loop, black screen, kernel never
  starts). Fix: base DTB built with `-@` and a no-op overlay in `dtbo`
  (`dtbo/cosmo-noop.dts`, `bin/build-dtbo.sh`). Earlier text kept for the
  record: what LK does next was not answered by the log. The failure paths exist in LK strings (`ufdt_apply_overlay()
  failed!`, `load overlay dtbo failed !`); the control flow needs LK
  disassembly or a test.
- LK also *reads* the merged DTB for its own use before booting:
  `[GPIO] Found /gpio@10005000 at offset 9052` / `Found perperty …, len 5376`
  (pin setup from the DT), then `mt6370_bled_probe` and the SCP firmware
  load (`[SCP](overlay)firmware size …`). Missing nodes there must not
  stop LK; to confirm.

Candidate fix, not decided: write a `dtbo` that holds a valid DT table with
a no-op overlay (one fragment with `target-path = "/"`, empty body), checked
against our DTB offline with `fdtoverlay` before any flash. That is a
partition write: ask first.

## LK framebuffer (P0 Q2)

Log (`videolfb` lines):

| | |
|---|---|
| fb_base | **0x7df70000** |
| vram | 33,095,680 bytes (0x1f90000) |
| LCM | `aeon_nt36672_fhd_dsi_vdo_x800_datong`, 1080×2160, DSI0 burst video mode, 4 lanes |
| rotation | `MTK_LCM_PHYSICAL_ROTATION = 270` |

LK passes it to the kernel as `/chosen` `atag,videolfb-fb_base_h/_l`,
`atag,videolfb-vramSize`, `atag,videolfb-lcmname` (LK strings). Stride and
format: see "Live system" below (stride 4352, a8b8g8r8).

## Panel: NT36672, not SSD2092

LK probes `NT36672 lcm_compare_id()` and logs `disp_lcm_probe SUCCESS` with
the NT36672 driver in every logged boot. **This unit has the Novatek
NT36672 panel**, so `docs/hardware-map.md`'s SSD2092 assumption does not
hold here; P3 targets NT36672 (touch: `novatek-nvt-ts`). Two different
timing sets appear across boots (vbp 33 / PLL 550 MHz and vbp 15 / PLL
440 MHz), likely from different firmware versions; record the one from the
current boot before P3.

## Memory (P0 Q3, partial)

- `total_dram_size: 0x0000000180000000` = **6 GiB** (matches the DTS
  `memory` node).
- LK `mblock` reservations: `atf-reserved` 0x54600000 +0x40000,
  `pl-bootarg` 0x54800000 +0x200000, `SSPM-reserved` 0x7ff00000 +0xc0000,
  `log_store` 0x7ffc0000 +0x40000, `tee-reserved` 0xedb00000 +0x2440000,
  `dramc-rk0` 0xbffff000 +0x1000, `dramc-rk1` 0x1bffff000 +0x1000, plus
  LK's own `lk_addr_mb`/`scratch_addr_mb`/`dtb_kernel_addr_mb` (transient).
- Whether LK writes these into the DTB's `/reserved-memory` still needs a
  running-system DT dump; until then, the DTS should carry them itself.

## Security state

- BROM (mtk log): HW code 0x788, SLA disabled, memory read/write auth
  disabled.
- LK: `[SBC] sbc_en = 0`, `[SEC_POLICY] lock_state(default) = 0x4`,
  images `logo` and `dtbo` "auth pass", `androidboot.verifiedbootstate=green`.
- **Answered 2026-10-25:** locked LK enforces signatures. Planet's Linux
  boot image gave **red state** and a reboot loop. Unlocked with
  `mtk da seccfg unlock` (seccfg V4, rewritten via the SoC's HACC engine;
  original in the backup). Now `[SEC_POLICY] lock_state = 0x3`,
  `boot state: orange`, `androidboot.verifiedbootstate=orange`: LK shows
  the orange warning and boots unsigned images.

## Live system: Planet Gemian 4.4 (2026-10-25)

Planet `cosmo-customos-installer-v4.zip` (sha256 `6fc0a22e…2887f`):
`linux-cosmo-boot.img` → `boot`, `gemian-cosmocom-kde-plasma.img` (ext4) →
`userdata` (no `linux` partition here; its initramfs `scripts/geminipda`
tries `linux` then `userdata` and boots a partition that has
`/etc/os-release`). Its DTB is byte-identical to the stock Android one.
Data from `bin/p0-dump.sh` run as root (tarball outside the repo,
`~/cosmo-p0/gemian/`).

**Framebuffer (P0 Q2, answered)** — `FBIOGET_FSCREENINFO`/`VSCREENINFO` on
`/dev/fb0` (`mtkfb`):

| | |
|---|---|
| base | 0x7df70000 (= `smem_start`; LK reserves 0x7df70000 +0x1f90000 as `mblock-7-framebuffer`, no-map) |
| visible | 1080×2160, virtual 1088×4320 (double buffer) |
| stride | **4352** bytes (1088 px × 4) |
| format | 32 bpp, R offset 0, G 8, B 16, A 24 → simple-framebuffer `a8b8g8r8` |

So `simple-framebuffer`: `reg = <0 0x7df70000 0 (4352*2160)>`, width 1080,
height 2160, stride 4352, format "a8b8g8r8". The kernel had panned to
yoffset 2160 (second buffer) at dump time; at LK handoff, check whether
LK scans out the first buffer.

**DT that LK hands over (P0 Q3, answered)** — `/sys/firmware/fdt`:
- The `dtbo` overlay is merged (the result has `__symbols__`, touch nodes).
- `/memory`: `reg = <0 0x40000000 1 0x80000000>` (6 GiB), plus LK
  properties `orig_dram_info`, `tee_reserved_mem`, `mblock_info`.
- LK adds **`mblock-N-*` nodes to `/reserved-memory`**:

| node | base | size | no-map |
|---|---|---|---|
| mblock-1-dramc-rk0 | 0xbffff000 | 0x1000 | yes |
| mblock-2-dramc-rk1 | 0x1bffff000 | 0x1000 | yes |
| mblock-3-log_store | 0x7ffc0000 | 0x40000 | no |
| mblock-4-atf-reserved | 0x54600000 | 0x40000 | yes |
| mblock-5-SSPM-reserved | 0x7ff00000 | 0xc0000 | yes |
| mblock-6-tee-reserved | 0xedb00000 | 0x2440000 | yes |
| mblock-7-framebuffer | 0x7df70000 | 0x1f90000 | yes |
| mblock-8-vpu_binary | 0x9d5f0000 | 0x2a10000 | yes |
| mblock-9-SPM-reserved | 0x77ff0000 | 0x10000 | yes |
| mblock-10-SCP-reserved | 0x9cf00000 | 0x600000 | yes |
| mblock-11-ccci | 0x8c000000 | 0x100000 | yes |
| mblock-12-ccci | 0x66000000 | 0x10000000 | no |
| mblock-13-ccci | 0x8a000000 | 0x1600000 | yes |

  The rest of `/reserved-memory` (pstore, ram_console, minirdump, scp/sspm
  share, consys, zone-movable CMA) is from the vendor DTS. Whether LK adds
  the mblock nodes to *our* DTB is unknown; the safe route is to carry
  ATF/TEE/SSPM/SPM/framebuffer/dramc as fixed no-map nodes in our DTS.
  `/proc/iomem` System RAM holes match the no-map regions.
- `/chosen`: `bootargs`, `linux,initrd-start/-end`, and MTK `atag,*`
  blobs. The binary `atag,videolfb` blob holds the live values (lcm name
  `aeon_nt36672_fhd_…`); the separate `atag,videolfb-lcmname`
  (`nt35595_…`) and `-vramSize` (0x17bb000) properties are stale vendor
  defaults, do not trust them.

**cmdline when unlocked**: LK swaps the verity `root=/dev/dm-0 dm=…` for
`root=PARTUUID=4d2d1290-…` (the stock `system` partition) and sets
`verifiedbootstate=orange`; our header cmdline comes after it, so our
`root=` wins.

Still open: LK behaviour when the overlay fails on our DTB (Q1), UART pads
(Q5).

## Display at handoff, proven on glass (2026-10-25, fbpaint probe)

`fbpaint/fbpaint.S` packed as a kernel: screen turned green (layer 0
filled), then a top-down wipe to red (layer 3 filled).
- LK jumps to the payload; LK's display path keeps scanning out.
- OVL layer 0 = 0x7df70000, stride 4352: visible.
- OVL layer 3 = 0x7e878000, stride 4320: above layer 0 but transparent
  as LK leaves it; it covers layer 0 once opaque pixels are written.
  Same arrangement as the Gemini (gemini-nixos docs/display.md: layer 0 =
  LK fb, layer 3 = page 1).
- Pixel format: 0xffff0000 showed **red**, so memory order is B,G,R,A =
  DRM `a8r8g8b8` (LK calls it eBGRA8888). The Gemian fbdev var info
  (red at bit 0) is wrong. Write alpha 0xff.
- A ~100-byte gzip "kernel" makes LK die before the logo (black, ~6 s
  loop); a payload padded to a normal kernel size boots.
