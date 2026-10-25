#!/usr/bin/env python3
"""Pack DT overlays into an Android DT table image (the `dtbo` partition).

Layout as the stock Cosmo `dtbo` (docs/p0-findings.md): magic 0xd7b7ab1e,
header 32 bytes, entry 32 bytes, page size 2048, version 0. LK picks the
entry by PCB_ID (index 0 on this unit) and errors if the index is beyond
the entry count, so by default every one of --entries slots points at the
same blob, as stock does with three.
"""
import argparse, struct

ap = argparse.ArgumentParser()
ap.add_argument("overlay", help="compiled overlay (.dtbo)")
ap.add_argument("-o", "--output", required=True)
ap.add_argument("--entries", type=int, default=3)
ap.add_argument("--pagesize", type=int, default=2048)
a = ap.parse_args()

blob = open(a.overlay, "rb").read()
if blob[:4] != b"\xd0\x0d\xfe\xed":
    raise SystemExit("overlay is not an FDT blob")
hdr_size, ent_size = 32, 32
off = hdr_size + ent_size * a.entries
total = off + len(blob)
hdr = struct.pack(">8I", 0xd7b7ab1e, total, hdr_size, ent_size, a.entries,
                  hdr_size, a.pagesize, 0)
ents = b"".join(struct.pack(">8I", len(blob), off, i, 0, 0, 0, 0, 0)
                for i in range(a.entries))
open(a.output, "wb").write(hdr + ents + blob)
