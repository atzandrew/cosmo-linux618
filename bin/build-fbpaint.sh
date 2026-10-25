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
objcopy -O binary "$O/fbpaint.elf" "$O/Image.raw"
# Pad to the real kernel's compressed size with incompressible, seeded
# filler: LK died early (black screen, ~6 s loop) on the 100-byte gzip,
# but gets to the kernel jump with a 15 MB one (2026-10-25).
python3 - "$O/Image.raw" "$O/Image" "$ROOT/out/Image.gz" <<"PY"
import os, random, struct, sys
code = open(sys.argv[1], "rb").read()
target = os.path.getsize(sys.argv[3])
rng = random.Random(6771)
img = bytearray(code + rng.randbytes(target - len(code)))
size = (len(img) + 0xfffff) & ~0xfffff
struct.pack_into("<Q", img, 16, size)   # image_size
open(sys.argv[2], "wb").write(img)
PY
gzip -9nc "$O/Image" > "$O/Image.gz"
cat "$O/Image.gz" "$ROOT/out/mt6771-cosmo-communicator.dtb" > "$O/Image.gz-dtb"
python3 "$ROOT/bin/mkbootimg.py" --kernel "$O/Image.gz-dtb" \
	--ramdisk "$ROOT/out/initramfs.cpio.gz" \
	--cmdline "bootopt=64S3,32N2,64N2 log_buf_len=4M" -o "$O/boot.img"
sha256sum "$O/boot.img"
