#!/usr/bin/env bash
# P0 dump: collect the boot contract from a Cosmo running stock Gemian 4.4.
#
#   sudo bash p0-dump.sh [outdir]       # default outdir: /tmp
#
# READ-ONLY. It never writes to any block device: partitions are only read
# (dd if=<partition> of=<file in outdir>), the partition table is printed
# with read-only tools (sgdisk -p, sfdisk -d), and parted is not used
# because it can offer to "fix" a GPT. Everything it writes goes to
# <outdir>/cosmo-p0-<date>/ and one tarball next to it.
#
# Answers AGENTS.md P0 questions 1-4: LK's DT overlay (odmdtbo), the LK
# framebuffer, LK's fdt fixups (reserved-memory, chosen), the boot image.
set -uo pipefail

if [[ $EUID -ne 0 ]]; then
	echo "run as root (dmesg, iomem addresses and partition reads need it)" >&2
	exit 1
fi

STAMP="$(date +%Y%m%d-%H%M%S)"
BASE="${1:-/tmp}"
NAME="cosmo-p0-$STAMP"
OUT="$BASE/$NAME"
mkdir -p "$OUT"/{dt,fb,part,bootimg,odmdtbo,logs}
LOG="$OUT/commands.log"

# run <outfile> <cmd...>: capture stdout+stderr, log the command and exit code.
run() {
	local f="$1"; shift
	"$@" >"$OUT/$f" 2>&1
	local rc=$?
	printf '%3d  %-28s %s\n' "$rc" "$f" "$*" >>"$LOG"
}
have() { command -v "$1" >/dev/null 2>&1; }

echo "collecting into $OUT"

# --- system ---
run uname.txt        uname -a
run version.txt      cat /proc/version
run cmdline.txt      cat /proc/cmdline
run iomem.txt        cat /proc/iomem
run meminfo.txt      cat /proc/meminfo
run cpuinfo.txt      cat /proc/cpuinfo
run mounts.txt       cat /proc/mounts
run modules.txt      cat /proc/modules
run logs/dmesg.txt   dmesg
# MediaTek kernels expose the preloader + LK log here: it may show what LK
# did with the odmdtbo overlay and the framebuffer (P0 Q1/Q2).
for f in /proc/pl_lk /proc/lk_env /proc/last_kmsg /sys/fs/pstore/*; do
	[[ -r "$f" ]] && run "logs/$(basename "$f").txt" cat "$f"
done

# --- device tree (as the kernel sees it, after LK's overlay and fixups) ---
run dt/tar.txt tar -C /sys/firmware/devicetree -cf "$OUT/dt/devicetree.tar" base
# Raw FDT blob exactly as LK handed it to the kernel.
[[ -r /sys/firmware/fdt ]] && run dt/fdt.dtb cat /sys/firmware/fdt
if have dtc; then
	run dt/devicetree-fs.dts dtc -I fs -O dts /proc/device-tree
	[[ -s "$OUT/dt/fdt.dtb" ]] && run dt/fdt.dts dtc -I dtb -O dts "$OUT/dt/fdt.dtb"
else
	echo "dtc not installed; decode dt/fdt.dtb or dt/devicetree.tar on the build host" >"$OUT/dt/NO-DTC"
fi
# Quick view of the nodes LK is expected to touch.
{
	for d in /proc/device-tree/chosen /proc/device-tree/reserved-memory/*; do
		[[ -d "$d" ]] || continue
		echo "== $d"
		for p in "$d"/*; do
			[[ -f "$p" ]] || continue
			# hex (big-endian cells) and a text view; one of them will make sense
			printf '  %-40s hex:%s\n' "$(basename "$p")" \
				"$(head -c 256 "$p" | od -An -tx1 | tr -d ' \n')"
			printf '  %-40s txt:%s\n' "" "$(head -c 256 "$p" | tr '\0' ' ' | tr -c '[:print:]' '.')"
		done
	done
	echo "== /proc/device-tree/memory*"
	for d in /proc/device-tree/memory*; do
		[[ -f "$d/reg" ]] && { printf '  %s reg ' "$d"; od -An -tx4 --endian=big "$d/reg" | tr -s ' \n' ' '; echo; }
	done
} >"$OUT/dt/chosen-reserved-summary.txt" 2>&1

# --- LK framebuffer (P0 Q2) ---
if [[ -d /sys/class/graphics/fb0 ]]; then
	for f in /sys/class/graphics/fb0/*; do
		[[ -f "$f" && -r "$f" ]] || continue
		printf '%s: %s\n' "$(basename "$f")" "$(cat "$f" 2>&1 | tr '\n' ' ')"
	done >"$OUT/fb/fb0-sysfs.txt"
fi
have fbset && run fb/fbset.txt fbset -i -fb /dev/fb0
# smem_start (physical address) is only in the FBIOGET_FSCREENINFO ioctl.
if have python3 && [[ -e /dev/fb0 ]]; then
	run fb/fb0-ioctl.txt python3 - <<'PY'
import fcntl, struct
FIX, VAR = 0x4602, 0x4600
with open("/dev/fb0", "rb") as fb:
    fix = fcntl.ioctl(fb, FIX, bytes(80))
    var = fcntl.ioctl(fb, VAR, bytes(160))
f = struct.unpack_from("@16sQIIIIHHHIQIIH", fix)
print("id          ", f[0].rstrip(b"\0").decode(errors="replace"))
print("smem_start   0x%x" % f[1])
print("smem_len     0x%x (%d)" % (f[2], f[2]))
print("type %d type_aux %d visual %d" % f[3:6])
print("line_length  %d (stride)" % f[9])
print("mmio_start   0x%x mmio_len 0x%x" % (f[10], f[11]))
v = struct.unpack_from("@8I", var)
print("xres %d yres %d xres_virtual %d yres_virtual %d xoffset %d yoffset %d" % v[:6])
print("bits_per_pixel %d grayscale %d" % v[6:8])
names = ["red", "green", "blue", "transp"]
for i, n in enumerate(names):
    off, length, msb = struct.unpack_from("@3I", var, 32 + 12 * i)
    print("%-6s offset %2d length %d" % (n, off, length))
print("raw_fix", fix.hex())
print("raw_var", var.hex())
PY
fi
grep -iE 'fb|lcm|videolfb|disp|framebuffer' "$OUT/logs/dmesg.txt" >"$OUT/fb/dmesg-fb.txt" 2>/dev/null
# MTK LK passes the framebuffer in /chosen as atag,videolfb-* properties.
ls /proc/device-tree/chosen 2>/dev/null | grep -i videolfb >"$OUT/fb/chosen-videolfb-props.txt"

# --- partitions (P0 Q4) ---
DISK=/dev/mmcblk0
run part/by-partlabel.txt ls -l /dev/disk/by-partlabel
run part/proc-partitions.txt cat /proc/partitions
run part/lsblk.txt lsblk -b -o NAME,SIZE,PARTLABEL,PARTUUID,FSTYPE,MOUNTPOINT
have sgdisk && run part/sgdisk-p.txt sgdisk -p "$DISK"
have sfdisk && run part/sfdisk-d.txt sfdisk -d "$DISK"
have blkid  && run part/blkid.txt blkid
for b in /sys/block/mmcblk0boot*; do
	[[ -e "$b/size" ]] && echo "$(basename "$b") sectors $(cat "$b/size") ro_lock $(cat "$b/force_ro" 2>/dev/null)"
done >"$OUT/part/boot-areas.txt"

# name -> device, from sysfs (works without udev's by-partlabel)
declare -A PARTDEV
for u in /sys/class/block/mmcblk0p*/uevent; do
	n="$(sed -n 's/^PARTNAME=//p' "$u")"
	d="$(sed -n 's/^DEVNAME=//p' "$u")"
	[[ -n "$n" ]] && PARTDEV[$n]="/dev/$d"
done
for n in "${!PARTDEV[@]}"; do echo "$n ${PARTDEV[$n]}"; done | sort >"$OUT/part/name-to-dev.txt"

# --- boot image headers: every partition that starts with ANDROID! ---
{
	for n in $(printf '%s\n' "${!PARTDEV[@]}" | sort); do
		dev="${PARTDEV[$n]}"
		case "$n" in userdata|cache|system|vendor|nvdata|nvram|protect1|protect2|persist|proinfo|metadata|frp|seccfg|sec1|otp|flashinfo|expdb) continue ;; esac
		magic="$(dd if="$dev" bs=8 count=1 status=none 2>/dev/null | tr -d '\\0')"
		[[ "$magic" == "ANDROID!" ]] || continue
		echo "== $n ($dev) size $(blockdev --getsize64 "$dev")"
		dd if="$dev" of="$OUT/bootimg/$n-header.bin" bs=4096 count=1 status=none
		echo "$n" >>"$OUT/bootimg/list.txt"
	done
} >"$OUT/bootimg/scan.txt" 2>&1
if have python3; then
	for h in "$OUT"/bootimg/*-header.bin; do
		[[ -e "$h" ]] || continue
		python3 - "$h" "$(cat /proc/cmdline)" >"${h%.bin}.txt" 2>&1 <<'PY'
import struct, sys
b = open(sys.argv[1], "rb").read()
running = sys.argv[2]
(magic, ksz, kaddr, rsz, raddr, ssz, saddr, tags, page, ver, osv) = struct.unpack_from("<8s10I", b)
name = b[48:64].rstrip(b"\0").decode(errors="replace")
cmd = b[64:576].rstrip(b"\0").decode(errors="replace")
print("magic         ", magic)
print("header_version", ver)
print("page_size      0x%x" % page)
print("kernel  size   0x%x  addr 0x%08x" % (ksz, kaddr))
print("ramdisk size   0x%x  addr 0x%08x" % (rsz, raddr))
print("second  size   0x%x  addr 0x%08x" % (ssz, saddr))
print("tags_addr      0x%08x" % tags)
print("os_version     0x%08x" % osv)
print("name          ", repr(name))
print("cmdline       ", repr(cmd))
extra = b[608:1632].rstrip(b"\0").decode(errors="replace")
if extra: print("extra_cmdline ", repr(extra))
if ver >= 1:
    rsz2, roff, hsz = struct.unpack_from("<IQI", b, 1632)
    print("recovery_dtbo size 0x%x off 0x%x header_size %d" % (rsz2, roff, hsz))
if ver >= 2:
    dsz, daddr = struct.unpack_from("<IQ", b, 1648)
    print("dtb size 0x%x addr 0x%x" % (dsz, daddr))
# a header cmdline that appears in the running cmdline marks the booted image
print("cmdline_in_running_cmdline", bool(cmd) and cmd in running)
PY
	done
fi

# --- odmdtbo (P0 Q1) ---
# Gemian-era layouts name it odmdtbo; the stock Android 9 GPT names it dtbo.
ODM="${PARTDEV[odmdtbo]:-${PARTDEV[dtbo]:-}}"
if [[ -n "$ODM" ]]; then
	sz="$(blockdev --getsize64 "$ODM")"
	echo "odmdtbo $ODM size $sz" >"$OUT/odmdtbo/info.txt"
	dd if="$ODM" bs=4096 count=1 status=none | od -Ax -tx1z >"$OUT/odmdtbo/head.hex"
	# Small partition with no user data: keep a full copy for offline study.
	if (( sz <= 64 * 1024 * 1024 )); then
		dd if="$ODM" of="$OUT/odmdtbo/odmdtbo.img" bs=1M status=none
		sha256sum "$OUT/odmdtbo/odmdtbo.img" >>"$OUT/odmdtbo/info.txt"
	fi
else
	echo "no partition named odmdtbo" >"$OUT/odmdtbo/info.txt"
fi

# --- pack ---
tar -C "$BASE" -czf "$BASE/$NAME.tar.gz" "$NAME"
sha256sum "$BASE/$NAME.tar.gz"
echo "done: $BASE/$NAME.tar.gz"
