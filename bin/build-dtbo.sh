#!/usr/bin/env bash
# Build out/dtbo.img (the `dtbo` partition) from dtbo/cosmo-noop.dts, using
# the dtc from the kernel build (run bin/build-kernel.sh first), and check
# that it applies to the board DTB the way LK applies it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/out"
DTC="$ROOT/build/obj/scripts/dtc/dtc"
[[ -x "$DTC" ]] || { echo "no $DTC: run bin/build-kernel.sh first" >&2; exit 1; }
"$DTC" -q -I dts -O dtb -o "$OUT/cosmo-noop.dtbo" "$ROOT/dtbo/cosmo-noop.dts"
python3 "$ROOT/bin/mkdtboimg.py" "$OUT/cosmo-noop.dtbo" -o "$OUT/dtbo.img"
sha256sum "$OUT/dtbo.img"
