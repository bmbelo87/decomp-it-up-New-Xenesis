#!/usr/bin/env python3
"""Extrator de RESPACK (BGA\\*.DAT do Pump It Up Exceed).

Formato (conferido no exceed.exe):
  0x00  "RESPACK\\x1A"
  0x0C  u32 numero de entradas
  indice em 0x18 (ou 0x28 se o DWORD em 0x18 for zero), entradas de 0x12C bytes,
  cifrado com XOR: chave 0x5C no inicio do indice, +0xC1 a cada byte.
  entrada +0x000 nome, +0x110 tamanho comprimido, +0x118 chave (16 bytes),
          +0x128 offset do recurso.
  dados em header + n*0x12C + offset + 0x118; chave derivada por 0x411DBC,
  XOR por 0x423234 (k[i%16] += 0x54 a cada uso) e depois zlib.

Uso: respack_extract.py <arquivo.DAT | pasta> <saida> [-r]
"""
import os
import struct
import sys
import zlib

ENTRY = 0x12C
T0 = bytes.fromhex("f078f9fd1c20c202")  # 0x411dc4
T1 = bytes.fromhex("fffefcf8f0e0c07f")  # 0x411f94


class KeyGen:
    """Emula 0x411DBC -> 0x411F06 e as auxiliares (estado global em 0x1284ab0)."""

    def __init__(self):
        self.g = 0

    def f(self, c, s):  # 0x411df3
        c &= 0xFF
        if c == 0:
            return T0[s & 0xFF]
        a = self.f((c - 1) & 7, (s - 1) & 7)
        r = ((a << 1) | (((a >> 7) ^ (a >> 6)) & 1)) & 0xFF
        if (s & 0xFF) == 7:
            r ^= self.f(c, 0)
        return r

    def step0(self):  # 0x411dcc
        dl = 0
        for b in range(8):
            if (self.g >> b) & 1:
                dl ^= T1[b]
        self.g = dl

    def stepk(self, k):  # 0x411e67
        r = 0
        for b in range(8):
            if (self.g >> b) & 1:
                r ^= self.f(k, b)
        self.g = r & 0xFF

    def h(self, x):  # 0x411ead
        r = 0
        for c in range(8):
            if c == 0:
                self.step0()
            r ^= ((self.g >> c) & 1) << c
            if not (x >> c) & 1:
                self.stepk(c)
        return r & 0xFF

    def derive(self, key):  # 0x411f06
        n = len(key)
        out = bytearray(n)
        self.g = 0xFC
        prev = self.h(~key[0] & 0xFF)
        for i in range(1, n):
            a = self.h(~key[i] & 0xFF)
            out[i - 1] = ((prev >> 3) | (a << 5)) & 0xFF
            prev = a
        a = self.h(0)
        out[n - 1] = ((a << 5) | (prev >> 3)) & 0xFF
        return bytes(out)


def decrypt(data, key16):  # 0x423234
    k = bytearray(KeyGen().derive(key16))
    out = bytearray(data)
    for i in range(len(out)):
        j = i % 16
        out[i] ^= k[j]
        k[j] = (k[j] + 0x54) & 0xFF
    return bytes(out)


def parse(path):
    d = open(path, "rb").read()
    if d[:8] != b"RESPACK\x1a":
        raise ValueError("magic invalido")
    n = struct.unpack_from("<I", d, 0x0C)[0]
    hdr = 0x28 if struct.unpack_from("<I", d, 0x18)[0] == 0 else 0x18
    raw = d[hdr:hdr + n * ENTRY]
    idx = bytes(b ^ ((0x5C + 0xC1 * i) & 0xFF) for i, b in enumerate(raw))
    base = hdr + n * ENTRY + 0x118
    items = []
    for e in range(n):
        ent = idx[e * ENTRY:(e + 1) * ENTRY]
        name = ent[:0x110].split(b"\0", 1)[0].decode("latin-1")
        csize = struct.unpack_from("<I", ent, 0x110)[0]
        key = ent[0x118:0x128]
        off = struct.unpack_from("<I", ent, 0x128)[0]
        blob = d[base + off:base + off + csize]
        items.append((name, zlib.decompress(decrypt(blob, key))))
    return hdr, items


def extract(path, outdir):
    hdr, items = parse(path)
    dst = os.path.join(outdir, os.path.splitext(os.path.basename(path))[0])
    os.makedirs(dst, exist_ok=True)
    for name, data in items:
        with open(os.path.join(dst, os.path.basename(name)), "wb") as f:
            f.write(data)
    print("%s: header 0x%X, %d entradas -> %s" % (path, hdr, len(items), dst))
    for name, data in items:
        print("   %-40s %8d" % (name, len(data)))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, out = sys.argv[1], sys.argv[2]
    files = [src]
    if os.path.isdir(src):
        walk = os.walk(src) if "-r" in sys.argv else [(src, [], os.listdir(src))]
        files = [os.path.join(r, f) for r, _, fs in walk for f in fs if f.upper().endswith(".DAT")]
    bad = 0
    for p in sorted(files):
        try:
            extract(p, out)
        except Exception as ex:
            bad += 1
            print("%s: ERRO %s" % (p, ex))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
