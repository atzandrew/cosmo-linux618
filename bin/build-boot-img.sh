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
#  - panic=0: stay on the panic screen instead of rebooting.
CMDLINE="bootopt=64S3,32N2,64N2 log_buf_len=4M console=tty0 clk_ignore_unused pd_ignore_unused regulator_ignore_unused cpuidle.off=1 fbcon=rotate:3 fbcon=font:TER16x32 consoleblank=0 loglevel=8 panic=0"

file -L "$BUSYBOX" | grep -q "aarch64.*statically linked" \
	|| { echo "$BUSYBOX is not a static aarch64 binary" >&2; exit 1; }
[[ -f "$OUT/Image.gz-dtb" ]] || { echo "run bin/build-kernel.sh first" >&2; exit 1; }

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
