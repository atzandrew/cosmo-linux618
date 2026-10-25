#!/usr/bin/env bash
# Build out/fbpaint/boot.img: the fbpaint probe (fbpaint/fbpaint.S) packed
# like a kernel (Image header, gzip, board DTB appended) for LK. Needs a
# native arm64 toolchain and out/ from bin/build-kernel.sh + build-boot-img.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
O="$ROOT/out/fbpaint"
mkdir -p "$O"
gcc -c -o "$O/fbpaint.o" "$ROOT/fbpaint/fbpaint.S"
ld -Ttext=0 -e _start -o "$O/fbpaint.elf" "$O/fbpaint.o"
objcopy -O binary "$O/fbpaint.elf" "$O/Image"
gzip -9nc "$O/Image" > "$O/Image.gz"
cat "$O/Image.gz" "$ROOT/out/mt6771-cosmo-communicator.dtb" > "$O/Image.gz-dtb"
python3 "$ROOT/bin/mkbootimg.py" --kernel "$O/Image.gz-dtb" \
	--ramdisk "$ROOT/out/initramfs.cpio.gz" \
	--cmdline "bootopt=64S3,32N2,64N2 log_buf_len=4M" -o "$O/boot.img"
sha256sum "$O/boot.img"
