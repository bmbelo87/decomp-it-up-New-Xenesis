#!/usr/bin/env python3
"""Lista slots (nome, nº de keyframes, 1º quadro, x/y) e cenas de um .bga BGA3.
Uso: bga3_dump.py <arquivo.bga> [--kf]"""
import struct, sys
d = open(sys.argv[1], "rb").read(); kf = "--kf" in sys.argv
assert d[:4] == b"BGA3", "nao e BGA3"
pos = 16
for slot in range(100):
    name = d[pos:pos + 64].split(b"\0")[0].decode("latin1"); n = max(0, struct.unpack_from("<i", d, pos + 64)[0]); pos += 0x44
    if name and n:
        r = [struct.unpack_from("<hhhh6f", d, pos + i * 0x64) for i in range(n)]
        a = [struct.unpack_from("<f", d, pos + i * 0x64 + 0x38)[0] for i in range(n)]
        print("slot %2d %-16s kf=%-3d quadros %d..%d  x=%.0f y=%.0f" % (slot, name, n, r[0][0], r[-1][0], r[0][4], r[0][5]))
        if kf:
            for i, k in enumerate(r): print("      f=%d g=%d x=%.1f y=%.1f hx=%.1f hy=%.1f sx=%.2f sy=%.2f a=%.2f" % (k[0], k[1], k[4], k[5], k[6], k[7], k[8], k[9], a[i]))
    pos += n * 0x64
if d[pos:pos + 6] == b"SCENE1":
    pos += 6; n = struct.unpack_from("<I", d, pos)[0]; pos += 4
    for i in range(n):
        nm = d[pos + 4:pos + 0x44].split(b"\0")[0].decode("latin1")
        s, e, lp = struct.unpack_from("<hhh", d, pos + 0x44); mode = struct.unpack_from("<i", d, pos + 0x4C)[0]
        print("cena %-28s %4d..%-4d volta=%d modo=%d" % (nm, s, e, lp, mode)); pos += 0x50
