#!/usr/bin/env python3
"""Converte .MOV (formato MOV2 do Pump It Up Exceed) em .m2v (MPEG-2 puro).

Formato (conferido no exceed.exe, 0x4225A8 / 0x4226A0):
  0x00      "MOV2"
  0x88      u32 N (lixo de tamanho variavel)
  0x8C+N    chave de 16 bytes
  0x9C+N    32 bytes cifrados (cifra do RESPACK) = nome original ("INTRO.M2V")
  0xBC+N    byte G
  0xC0+N    stream MPEG-2: byte = bitrev(b) ^ bitrev(G)   (0x4223C4 / 0x422420)

Uso: mov2_decode.py <arquivo.MOV | pasta> <pasta de saida> [-r]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from respack_extract import decrypt  # noqa: E402

REV = bytes(int("{:08b}".format(b)[::-1], 2) for b in range(256))


def convert(path, outdir):
    with open(path, "rb") as f:
        hdr = f.read(0x8C)
        if hdr[:4] != b"MOV2":
            raise ValueError("magic invalido")
        n = struct.unpack_from("<I", hdr, 0x88)[0]
        f.seek(0x8C + n)
        key = f.read(16)
        name = decrypt(f.read(32), key).split(b"\0", 1)[0].decode("latin-1")
        g = f.read(4)[0]
        k = REV[g]
        table = bytes(REV[b] ^ k for b in range(256))
        base = os.path.splitext(os.path.basename(path))[0]
        dst = os.path.join(outdir, base + ".m2v")
        total = 0
        with open(dst, "wb") as o:
            while True:
                blk = f.read(1 << 20)
                if not blk:
                    break
                o.write(blk.translate(table))
                total += len(blk)
    with open(dst, "rb") as chk:
        ok = chk.read(4) == b"\x00\x00\x01\xb3"
    print("%-14s nome interno %-14s G=0x%02X %10d bytes %s"
          % (os.path.basename(path), name, g, total, "ok" if ok else "SEM SEQUENCE HEADER"))
    return ok


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    files = [src]
    if os.path.isdir(src):
        walk = os.walk(src) if "-r" in sys.argv else [(src, [], os.listdir(src))]
        files = [os.path.join(r, f) for r, _, fs in walk for f in fs if f.upper().endswith(".MOV")]
    bad = 0
    for p in sorted(files):
        try:
            if not convert(p, out):
                bad += 1
        except Exception as ex:
            bad += 1
            print("%s: ERRO %s" % (p, ex))
    print("%d arquivo(s), %d com problema" % (len(files), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
