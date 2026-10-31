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
#  - cpuidle on since I1a (docs/session-log.md): core power-down only.
#  - cma=256M: the display scans out physical buffers (no M4U), so
#    every scanout buffer (9.3 MB at 1080x2160) comes from CMA; the
#    32 MB default could not hold a GPU client's buffers (GPU G2b).
#  - loglevel=3: only critical messages on glass (driver debug output
#    filled the console); oops and panic still print in full because
#    the kernel goes verbose when it dies. Everything is in dmesg.
#  - panic=10 + oops=panic (the plain
#    panic_on_oops= form is not a boot parameter and was
#    ignored until 2026-10-27): an oops becomes a panic; a
#    panic stays on glass until the reset. PSCI reboot does not work with
#    the vendor ATF, so the hardware watchdog (fed only while healthy) is
#    what actually resets the board (AGENTS.md rule 5).
#  - g_ether.*: fixed MACs for the bring-up USB network (init gives
#    the Cosmo 10.15.19.83 and hands the host .84 by DHCP).
CMDLINE="bootopt=64S3,32N2,64N2 log_buf_len=4M cma=256M console=tty0 clk_ignore_unused pd_ignore_unused regulator_ignore_unused fbcon=rotate:3 fbcon=font:TER16x32 consoleblank=0 loglevel=3 panic=10 oops=panic g_ether.dev_addr=42:00:15:19:83:01 g_ether.host_addr=42:00:15:19:83:00"

# EXTRA_CMDLINE is appended (e.g. cosmo.wdtest=1 for the watchdog escape
# test); OUTNAME names the image (default boot.img).
CMDLINE="$CMDLINE${EXTRA_CMDLINE:+ $EXTRA_CMDLINE}"
OUTNAME="${OUTNAME:-boot.img}"

file -L "$BUSYBOX" | grep -q "aarch64.*statically linked" \
	|| { echo "$BUSYBOX is not a static aarch64 binary" >&2; exit 1; }
[[ -f "$OUT/Image.gz-dtb" ]] || { echo "run bin/build-kernel.sh first" >&2; exit 1; }

# AGENTS.md rule 5: never pack an image that can strand the device.
# Mainline mtk-pmic-keys used the MT6397 TOP_RST_MISC layout on the MT6358
# and cleared the power-key reset enable. The keys node may be on only with
# the fixed driver (kernel/delta) and the stock long-press setting (mode 1,
# time 1 = stock 0x1225 bits 13:8, one-key reset); otherwise disabled
# with no long-press property.
DTB="$OUT/mt6771-cosmo-communicator.dtb"
DTC="$ROOT/build/obj/scripts/dtc/dtc"
DTS_TEXT="$("$DTC" -q -I dtb -O dts "$DTB")"
KEYS_NODE="$(awk '/compatible = "mediatek,mt6358-keys"/{f=1} f{print} f&&/^\t*};$/&&++n==3{exit}' <<<"$DTS_TEXT")"
KEYS_STATUS="$(grep -m1 'status =' <<<"$KEYS_NODE" || true)"
KDRV="$ROOT/kernel/delta/drivers/input/keyboard/mtk-pmic-keys.c"
if [[ "$KEYS_STATUS" == *'"disabled"'* ]]; then
	if [[ "$DTS_TEXT" == *"long-press-mode"* ]]; then
		echo "REFUSING: long-press-mode set while mt6358-keys is disabled (AGENTS.md rule 5)" >&2
		exit 1
	fi
else
	if ! grep -q 'mediatek,long-press-mode = <0x01>;' <<<"$KEYS_NODE" \
		|| ! grep -q 'power-off-time-sec = <0x01>;' <<<"$KEYS_NODE" \
		|| ! grep -qE 'MTK_PMIC_MT6358_PWRKEY_RST[[:space:]]+BIT\(9\)' "$KDRV" \
		|| ! grep -qE 'MTK_PMIC_MT6358_HOMEKEY_RST[[:space:]]+BIT\(8\)' "$KDRV" \
		|| ! grep -qE 'MTK_PMIC_MT6358_RST_DU_MASK[[:space:]]+GENMASK\(13, 12\)' "$KDRV" \
		|| ! grep -q '.rst_lprst_mask = MTK_PMIC_MT6358_RST_DU_MASK,' "$KDRV"; then
		echo "REFUSING: mt6358-keys on without the MT6358 driver fix and stock long-press values (AGENTS.md rule 5)" >&2
		exit 1
	fi
fi

RD="$(mktemp -d)"
trap 'rm -rf "$RD"' EXIT
mkdir -p "$RD"/{bin,dev,proc,sys,tmp}
cp "$BUSYBOX" "$RD/bin/busybox"
cp "$ROOT/initramfs/init" "$RD/init"
chmod 755 "$RD/init" "$RD/bin/busybox"
# kexec (dynamic, from the build host) + its libraries, and the kx helper:
# lets a running kernel boot the next test kernel without a flash.
KEXEC="${KEXEC:-/usr/sbin/kexec}"
if [[ -x "$KEXEC" ]]; then
	cp -L "$KEXEC" "$RD/bin/kexec"
	for lib in $(ldd "$KEXEC" | awk '/=>/ {print $3} /ld-linux/ {print $1}'); do
		mkdir -p "$RD$(dirname "$lib")"
		cp -L "$lib" "$RD$lib"
	done
	cp "$ROOT/initramfs/kx" "$RD/bin/kx"
	chmod 755 "$RD/bin/kexec" "$RD/bin/kx"
fi
( cd "$RD" && find . | LC_ALL=C sort | cpio -o -H newc --owner=0:0 --quiet ) \
	| gzip -9n > "$OUT/initramfs.cpio.gz"

python3 "$ROOT/bin/mkbootimg.py" --kernel "$OUT/Image.gz-dtb" \
	--ramdisk "$OUT/initramfs.cpio.gz" --cmdline "$CMDLINE" \
	-o "$OUT/$OUTNAME"

echo "busybox: $("$BUSYBOX" | head -1)"
sha256sum "$OUT/initramfs.cpio.gz" "$OUT/$OUTNAME"
