#!/usr/bin/env bash
# Build the Cosmo kernel: pinned upstream base + file-tree delta + config fragment.
#
#   bin/build-kernel.sh            # fetch base if needed, overlay delta, build
#   bin/build-kernel.sh dtbs       # just the DTB (quick check)
#
# Output: out/Image.gz, out/mt6771-cosmo-communicator.dtb, out/Image.gz-dtb
# (Image.gz with the DTB appended, as the stock 4.4 build produces it for LK).
# Boot-image packing is NOT done here yet: the LK boot contract is open (P0).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/kernel/BASE"

SRC="$ROOT/build/src"
OBJ="$ROOT/build/obj"
OUT="$ROOT/out"
DTB=mediatek/mt6771-cosmo-communicator.dtb
# Native gcc on an arm64 host (hydra); cross gcc elsewhere. Override with
# CROSS_COMPILE=... if needed.
if [[ "$(uname -m)" == aarch64 ]]; then
	CROSS_COMPILE="${CROSS_COMPILE-}"
else
	CROSS_COMPILE="${CROSS_COMPILE-aarch64-linux-gnu-}"
fi
JOBS="${JOBS:-$(nproc)}"
# LOCALVERSION= (set but empty) stops setlocalversion adding "-dirty": the
# delta always leaves the base tree modified. The release stays
# <version>-cosmo (CONFIG_LOCALVERSION in cosmo.config).
MK=(make -C "$SRC" O="$OBJ" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" LOCALVERSION= -j"$JOBS")

fetch_base() {
	if [[ ! -d "$SRC/.git" ]]; then
		mkdir -p "$(dirname "$SRC")"
		git clone --depth 1 --branch "$BASE_TAG" "$BASE_URL" "$SRC"
	fi
	local head
	head="$(git -C "$SRC" rev-parse HEAD)"
	if [[ "$head" != "$BASE_COMMIT" ]]; then
		echo "base mismatch: $SRC is $head, kernel/BASE pins $BASE_COMMIT" >&2
		exit 1
	fi
	# Reset to pristine base so deleted delta files never linger.
	git -C "$SRC" reset -q --hard "$BASE_COMMIT"
	git -C "$SRC" clean -qfdx
}

apply_delta() {
	cp -a "$ROOT/kernel/delta/." "$SRC/"
	echo "delta applied: $(git -C "$SRC" status --short | wc -l) files differ from $BASE_TAG"
}

configure() {
	mkdir -p "$OBJ"
	"${MK[@]}" defconfig
	"$SRC/scripts/kconfig/merge_config.sh" -m -O "$OBJ" \
		"$OBJ/.config" "$ROOT/kernel/config/cosmo.config"
	"${MK[@]}" olddefconfig
	# Fail loudly if the fragment asked for something Kconfig dropped.
	local missing=0
	while IFS= read -r line; do
		[[ "$line" =~ ^(#\ )?CONFIG_ ]] || continue
		grep -qxF "$line" "$OBJ/.config" || { echo "not applied: $line" >&2; missing=1; }
	done < <(sed 's/=n$/ is not set/; s/^\(CONFIG_[A-Z0-9_]*\) is not set$/# \1 is not set/' \
		"$ROOT/kernel/config/cosmo.config")
	return $missing
}

fetch_base
apply_delta
configure

mkdir -p "$OUT"
if [[ "${1:-}" == "dtbs" ]]; then
	"${MK[@]}" W=1 "$DTB"
	cp "$OBJ/arch/arm64/boot/dts/$DTB" "$OUT/"
	exit 0
fi

"${MK[@]}" Image.gz "$DTB" modules
cp "$OBJ/arch/arm64/boot/Image.gz" "$OUT/"
cp "$OBJ/arch/arm64/boot/dts/$DTB" "$OUT/"
cat "$OUT/Image.gz" "$OUT/$(basename "$DTB")" > "$OUT/Image.gz-dtb"
echo "built $(cat "$OBJ/include/config/kernel.release")"
sha256sum "$OUT/Image.gz-dtb"
