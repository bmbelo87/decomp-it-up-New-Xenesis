#!/usr/bin/env python3
"""Valida .SEE (NX): decifra (Blowfish ECB, 8 bytes a cada 0x18, piu 0x8067e30) todos os blocos e
mostra BPM/compasso/delay. Uso: see_check.py <pasta com .SEE> [piu]"""
import os, struct, sys, zlib
piu = sys.argv[2] if len(sys.argv) > 2 else "F:/18_nx/game/piu"
b = open(piu, "rb").read()
o = lambda va: va - 0x08137000 + 0xEF000  # .data da NX
init = struct.unpack_from("<1042I", b, o(0x811FB00)); key = b[o(0x8141E30):o(0x8141E30) + 24]
P = list(init[:18]); S = [list(init[18 + 256 * i:18 + 256 * (i + 1)]) for i in range(4)]
F = lambda x: ((((S[0][x >> 24] + S[1][(x >> 16) & 255]) & 0xFFFFFFFF) ^ S[2][(x >> 8) & 255]) + S[3][x & 255]) & 0xFFFFFFFF
def enc(l, r):
    for i in range(16): l ^= P[i]; r ^= F(l); l, r = r, l
    l, r = r, l; return l ^ P[17], r ^ P[16]
def dec(l, r):
    for i in range(17, 1, -1): l ^= P[i]; r ^= F(l); l, r = r, l
    l, r = r, l; return l ^ P[0], r ^ P[1]
j = 0
for i in range(18):
    d = 0
    for _ in range(4): d = (d << 8) | key[j % 24]; j += 1
    P[i] ^= d
l = r = 0
for i in range(0, 18, 2): l, r = enc(l, r); P[i], P[i + 1] = l, r
for s in range(4):
    for i in range(0, 256, 2): l, r = enc(l, r); S[s][i], S[s][i + 1] = l, r
def see_block(z):
    blk = bytearray(zlib.decompress(z))
    for i in range(0, len(blk) - 7, 0x18):
        struct.pack_into("<2I", blk, i, *dec(*struct.unpack_from("<2I", blk, i)))
    return blk
bad = odd = nblk = 0
for fn in sorted(os.listdir(sys.argv[1])):
    if not fn.upper().endswith(".SEE"): continue
    d = open(os.path.join(sys.argv[1], fn), "rb").read()
    for si, so in enumerate(struct.unpack_from("<9I", d, 0xFC)):
        if not so: continue
        n = max(1, sum(struct.unpack_from("<50I", d, so + 4)))
        pos = so + 0x324
        for k in range(n):
            sz = struct.unpack_from("<I", d, pos)[0]; pos += 4
            blk = see_block(d[pos:pos + sz]); pos += sz; nblk += 1
            bpm, m, sp, dl = struct.unpack_from("<fIIi", blk, 0)
            if not (0 < bpm < 2000 and 0 < m and 0 < sp <= 256): bad += 1; print("RUIM", fn, si, k, bpm, m, sp, dl)
            if dl % 10: odd += 1
print("blocos", nblk, "invalidos", bad, "delay nao multiplo de 10:", odd)
