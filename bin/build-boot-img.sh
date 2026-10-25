#!/usr/bin/env bash
# Pack out/boot.img: out/Image.gz-dtb (bin/build-kernel.sh) + a busybox
# initramfs from initramfs/ + the boot cmdline below.
#
#   bin/build-boot-img.sh
#
# BUSYBOX=/path/to/static/busybox overrides the host's /usr/bin/busybox
# (must be a static aarch64 binary).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/out"
BUSYBOX="${BUSYBOX:-/usr/bin/busybox}"

# Header cmdline. LK puts its own arguments around it (docs/p0-findings.md):
#  - bootopt= is parsed by LK itself and must stay (Gemini lesson).
#  - console=tty0 again so the framebuffer is the preferred console (LK
#    lists ttyS0 last).
#  - *_ignore_unused: keep LK's display clocks, power and PMIC rails on.
#  - cpuidle.off=1 until the vendor ATF's PSCI suspend is checked.
#  - panic=0: keep a panic on glass (the screen is the only console).
#  - g_ether.*: fixed MACs for the bring-up USB network (init gives
#    the Cosmo 10.15.19.83 and hands the host .84 by DHCP).
CMDLINE="bootopt=64S3,32N2,64N2 log_buf_len=4M console=tty0 clk_ignore_unused pd_ignore_unused regulator_ignore_unused cpuidle.off=1 fbcon=rotate:3 fbcon=font:TER16x32 consoleblank=0 loglevel=8 panic=0 panic_on_oops=1 softlockup_panic=1 g_ether.dev_addr=42:00:15:19:83:01 g_ether.host_addr=42:00:15:19:83:00"

file -L "$BUSYBOX" | grep -q "aarch64.*statically linked" \
	|| { echo "$BUSYBOX is not a static aarch64 binary" >&2; exit 1; }
[[ -f "$OUT/Image.gz-dtb" ]] || { echo "run bin/build-kernel.sh first" >&2; exit 1; }

# AGENTS.md rule 5: never pack an image that can strand the device. The
# DTB must keep the MT6358 power-key long-press reset enabled.
DTB="$OUT/mt6771-cosmo-communicator.dtb"
DTC="$ROOT/build/obj/scripts/dtc/dtc"
if ! "$DTC" -q -I dtb -O dts "$DTB" 2>/dev/null \
	| grep -q "mediatek,long-press-mode = <0x01>"; then
	echo "REFUSING: $DTB lacks mediatek,long-press-mode = <1> (AGENTS.md rule 5)" >&2
	exit 1
fi

RD="$(mktemp -d)"
trap 'rm -rf "$RD"' EXIT
mkdir -p "$RD"/{bin,dev,proc,sys,tmp}
cp "$BUSYBOX" "$RD/bin/busybox"
cp "$ROOT/initramfs/init" "$RD/init"
chmod 755 "$RD/init" "$RD/bin/busybox"
( cd "$RD" && find . | LC_ALL=C sort | cpio -o -H newc --owner=0:0 --quiet ) \
	| gzip -9n > "$OUT/initramfs.cpio.gz"

python3 "$ROOT/bin/mkbootimg.py" --kernel "$OUT/Image.gz-dtb" \
	--ramdisk "$OUT/initramfs.cpio.gz" --cmdline "$CMDLINE" \
	-o "$OUT/boot.img"

echo "busybox: $("$BUSYBOX" | head -1)"
sha256sum "$OUT/initramfs.cpio.gz" "$OUT/boot.img"
