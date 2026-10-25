#!/usr/bin/env python3
"""Pack an Android boot image, header v0, for the Cosmo's LK.

Defaults are the load addresses LK uses on this device (stock and Planet
Gemian boot images, docs/p0-findings.md): kernel 0x40080000, ramdisk
0x55000000, second 0x40f00000, tags/DTB 0x54000000, page 2048. The kernel
is Image.gz with the DTB appended, as LK expects.
"""
import argparse, hashlib, struct, sys

ap = argparse.ArgumentParser()
ap.add_argument("--kernel", required=True)
ap.add_argument("--ramdisk", required=True)
ap.add_argument("--cmdline", default="")
ap.add_argument("--name", default="")
ap.add_argument("-o", "--output", required=True)
ap.add_argument("--kernel-addr", type=lambda x: int(x, 0), default=0x40080000)
ap.add_argument("--ramdisk-addr", type=lambda x: int(x, 0), default=0x55000000)
ap.add_argument("--second-addr", type=lambda x: int(x, 0), default=0x40f00000)
ap.add_argument("--tags-addr", type=lambda x: int(x, 0), default=0x54000000)
ap.add_argument("--pagesize", type=int, default=2048)
ap.add_argument("--max-ramdisk", type=lambda x: int(x, 0), default=0x1000000,
                help="LK ramdisk window (ramdisk_addr_mb, 16 MiB)")
a = ap.parse_args()

k = open(a.kernel, "rb").read()
r = open(a.ramdisk, "rb").read()
cmd = a.cmdline.encode()
if len(cmd) > 511:
    sys.exit(f"cmdline is {len(cmd)} bytes; the v0 field holds 511")
if b"bootopt=" not in cmd:
    sys.exit("cmdline lacks bootopt=: LK parses it from this field (Gemini lesson)")
if len(r) > a.max_ramdisk:
    sys.exit(f"ramdisk {len(r)} bytes exceeds LK's window {a.max_ramdisk:#x}")

sha = hashlib.sha1()
for blob in (k, r, b""):
    sha.update(blob)
    sha.update(struct.pack("<I", len(blob)))
hdr = struct.pack("<8s10I16s512s32s",
                  b"ANDROID!", len(k), a.kernel_addr, len(r), a.ramdisk_addr,
                  0, a.second_addr, a.tags_addr, a.pagesize, 0, 0,
                  a.name.encode(), cmd, sha.digest().ljust(32, b"\0"))
hdr += bytes(1024)  # extra_cmdline (v0), unused

def pad(b):
    return b + bytes((-len(b)) % a.pagesize)

with open(a.output, "wb") as f:
    f.write(pad(hdr) + pad(k) + pad(r))
