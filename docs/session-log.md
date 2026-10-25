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
