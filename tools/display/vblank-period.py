#!/usr/bin/python3 -I
# Wait for N vblanks on a DRM card and print the period from the kernel
# vblank timestamps and sequence numbers. Usage: vblank-period.py /dev/dri/card2 600
import ctypes, os, sys

class Req(ctypes.Structure):
    _fields_ = [("type", ctypes.c_uint), ("sequence", ctypes.c_uint), ("signal", ctypes.c_ulong)]

class Rep(ctypes.Structure):
    _fields_ = [("type", ctypes.c_uint), ("sequence", ctypes.c_uint),
                ("tval_sec", ctypes.c_long), ("tval_usec", ctypes.c_long)]

class VBlank(ctypes.Union):
    _fields_ = [("request", Req), ("reply", Rep)]

drm = ctypes.CDLL("libdrm.so.2")
fd = os.open(sys.argv[1], os.O_RDWR)
n = int(sys.argv[2])
ts, seq = [], []
for _ in range(n):
    v = VBlank()
    v.request.type = 1  # DRM_VBLANK_RELATIVE
    v.request.sequence = 1
    if drm.drmWaitVBlank(fd, ctypes.byref(v)) != 0:
        sys.exit("drmWaitVBlank failed")
    ts.append(v.reply.tval_sec * 1000000 + v.reply.tval_usec)
    seq.append(v.reply.sequence)
d = [b - a for a, b in zip(ts, ts[1:])]
s = [b - a for a, b in zip(seq, seq[1:])]
span = ts[-1] - ts[0]
print("waits %d, span %d us, mean wait %.2f us (%.4f Hz)" % (n, span, span / (n - 1), 1e6 / (span / (n - 1))))
print("seq steps:", {k: s.count(k) for k in sorted(set(s))})
short = [x for x in d if x < 12000]
longw = [x for x in d if x > 20000]
print("waits < 12 ms: %d, > 20 ms: %d, min %d max %d" % (len(short), len(longw), min(d), max(d)))
print("first 12 waits:", d[:12])
