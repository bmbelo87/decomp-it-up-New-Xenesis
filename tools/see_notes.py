#!/usr/bin/env python3
"""Histograma dos bytes de nota das seções de um .SEE (NX), decifrando como o piu.

Formato (step.c / piu 0x80717b0): offsets das 9 seções em 0xFC; cada seção tem
0x324 bytes de cabeçalho + u32 tamanho do 1º bloco zlib; o bloco descomprimido é
decifrado com Blowfish (8 bytes a cada 0x18, chave 0x8141e30) e tem as linhas de
13 bytes a partir de 132 (contagem em 128).

Uso: see_notes.py <piu> <arquivo.SEE | pasta> [prefixo]
"""
import sys, os, struct, zlib
from collections import Counter

def load_bf(piu):
    b = open(piu, "rb").read()
    phoff, = struct.unpack_from("<I", b, 0x1C); phnum, = struct.unpack_from("<H", b, 0x2C)
    segs = [struct.unpack_from("<8I", b, phoff + i * 32) for i in range(phnum)]
    def off(va):
        for t, o, v, _, fs, _, _, _ in segs:
            if t == 1 and v <= va < v + fs: return va - v + o
    init = list(struct.unpack_from("<1042I", b, off(0x811FB00)))
    key = b[off(0x8141E30):off(0x8141E30) + 24]
    P = init[:18]; S = [init[18 + 256 * k:18 + 256 * (k + 1)] for k in range(4)]
    M = 0xffffffff
    def F(x): return ((((S[0][x >> 24] + S[1][(x >> 16) & 255]) & M) ^ S[2][(x >> 8) & 255]) + S[3][x & 255]) & M
    def enc(l, r):
        for i in range(16):
            l ^= P[i]; r ^= F(l); l, r = r, l
        l, r = r, l
        r ^= P[16]; l ^= P[17]
        return l, r
    j = 0
    for i in range(18):
        d = 0
        for _ in range(4): d = (d << 8) | key[j % 24]; j += 1
        P[i] ^= d
    l = r = 0
    for i in range(0, 18, 2): l, r = enc(l, r); P[i], P[i + 1] = l, r
    for s in range(4):
        for i in range(0, 256, 2): l, r = enc(l, r); S[s][i], S[s][i + 1] = l, r
    def dec(l, r):
        for i in range(17, 1, -1):
            l ^= P[i]; r ^= F(l); l, r = r, l
        l, r = r, l
        r ^= P[1]; l ^= P[0]
        return l, r
    return dec

def sections(path, dec):
    b = open(path, "rb").read()
    offs = struct.unpack_from("<9I", b, 0xFC)
    for si, o in enumerate(offs):
        if not o or o + 0x328 > len(b): continue
        cs, = struct.unpack_from("<I", b, o + 0x324)
        try: d = bytearray(zlib.decompressobj().decompress(b[o + 0x328:o + 0x328 + cs]))
        except Exception: continue
        for i in range(0, len(d) - 7, 0x18):
            l, r = struct.unpack_from("<II", d, i)
            l, r = dec(l, r)
            struct.pack_into("<II", d, i, l, r)
        if len(d) < 132: continue
        n, = struct.unpack_from("<I", d, 128)
        n = min(n, (len(d) - 132) // 13)
        yield si, [bytes(d[132 + k * 13:132 + k * 13 + 13]) for k in range(n)]

def main():
    if len(sys.argv) < 3: print(__doc__); sys.exit(1)
    dec = load_bf(sys.argv[1])
    p = sys.argv[2]; pre = sys.argv[3] if len(sys.argv) > 3 else ""
    files = [os.path.join(p, f) for f in sorted(os.listdir(p)) if f.startswith(pre) and f.upper().endswith(".SEE")] if os.path.isdir(p) else [p]
    total = Counter()
    for f in files:
        for si, rows in sections(f, dec):
            c = Counter(x for r in rows for x in r[:10] if x)
            if len(files) == 1: print(os.path.basename(f), "seção", si, "linhas", len(rows), dict(sorted(c.items())))
            if len(rows) > 2: total.update(c)
    print("total:", {hex(k): v for k, v in sorted(total.items())})

main()
