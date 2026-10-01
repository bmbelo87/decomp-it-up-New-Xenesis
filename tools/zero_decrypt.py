#!/usr/bin/env python3
"""Decifra arquivos do Pump It Up Zero (PC/Linux): ENC2 (.AUD/.PNZ), RESPAC2 (.DAT).

Algoritmos herdados do Exceed2 (PIU32.EXE), ver src/resource.c.
Uso: zero_decrypt.py [--dog chave.key] <arquivo|pasta> <saida>
(padrao: tools/data/nx_dog.key, dump do nxhook do pumptools)
"""
import os, struct, sys, zlib

# Tabela do MicroDog 3.4 (dump do zerohook/pumptools): a partir de 0xE0,
# entradas de 0x4C = { u32 resposta, u32 senha 0x66BB66BB, u32 len, dados[64] }.
DOG_KEY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "nx_dog.key")
_dog = None

def dog_convert(data):  # piu 0x80a6058 (servico 4 = Convert)
    global _dog
    if _dog is None:
        raw = open(DOG_KEY, "rb").read(); _dog = {}
        for p in range(0xE0, len(raw) - 0x4B, 0x4C):
            resp, _, n = struct.unpack_from("<III", raw, p)
            _dog.setdefault(raw[p + 12:p + 12 + n], resp)
    r = _dog.get(bytes(data))
    if r is None: raise KeyError("dados sem resposta na tabela do dongle: " + bytes(data).hex())
    return r

def x2_key(data, resp=None):  # piu 0x80a3dc0 (= PIU32.EXE 0x420610)
    """out[i] = in[i] ^ k[i&3], k[j] = (k[j]+i) ^ 0x1C; k = resposta do dongle (big-endian)."""
    if resp is None: resp = dog_convert(data)
    k = bytearray(struct.pack(">I", resp)); out = bytearray(len(data))
    for i, b in enumerate(data):
        out[i] = b ^ k[i & 3]
        k[i & 3] = ((k[i & 3] + i) ^ 0x1C) & 0xFF
    return bytes(out)

BITREV = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))

def enc2(buf):
    size, skip = struct.unpack_from("<II", buf, 0x84)
    off = 0x8C + 0x10 + skip
    seed = struct.unpack_from("<I", buf, off)[0]
    K = x2_key(buf[0x8C:0x9C])
    T = buf[off + 4: off + 4 + 0x400]
    table = bytes(T[j] ^ K[j & 15] for j in range(0x400))
    src = buf[off + 4 + 0x400:][:size]
    rot = (table * ((len(src) >> 10) + 2))[seed & 0x3FF:][:len(src)]
    dec = bytes(a ^ b for a, b in zip(src.translate(BITREV), rot))
    ok = zlib.adler32(dec) == seed
    return dec, ok

def respac2(d):
    n = struct.unpack_from("<I", d, 0x0C)[0]
    hdr = 0x28  # piu 0x809a050: le 8 + 0x20 bytes de cabecalho, indice sempre em 0x28
    G = x2_key(d[0x18:0x28]); crypt = d[9] != 0
    idx = bytes(d[hdr + i] ^ ((0x5C + 0xC1 * i) & 0xFF) for i in range(n * 0x12C))
    base = hdr + n * 0x12C + 0x118
    for e in range(n):
        ent = idx[e * 0x12C:(e + 1) * 0x12C]
        name = ent[:0x100].split(b"\0")[0].decode("latin1")
        raw, cs = struct.unpack_from("<II", ent, 0x10C)
        off = struct.unpack_from("<I", ent, 0x128)[0]
        blob = bytearray(d[base + off: base + off + cs])
        if crypt:
            k = bytearray(a ^ b for a, b in zip(ent[0x118:0x128], G))
            for i in range(cs):
                blob[i] ^= k[i & 15]; k[i & 15] = (k[i & 15] + 0x54) & 0xFF
        yield name, zlib.decompress(bytes(blob))

def mov3(path, outdir):  # piu 0x80a30e0 (abre) / 0x80a3380 (le)
    """hdr[0x8C] "MOV3"; A[16]; lixo[hdr+0x88]; B[16]; nome[32]; G; video em 0xD0+N.
    k = 0x80a3dc0(A) ^ B decifra o nome (+0x54); byte = bitrev(b) ^ bitrev(G)."""
    with open(path, "rb") as f:
        hdr = f.read(0x8C)
        n = struct.unpack_from("<I", hdr, 0x88)[0]
        A = f.read(16); f.seek(n, 1); B = f.read(16); enc = bytearray(f.read(32))
        k = bytearray(a ^ b for a, b in zip(x2_key(A), B))
        for i in range(32):
            enc[i] ^= k[i & 15]; k[i & 15] = (k[i & 15] + 0x54) & 0xFF
        name = bytes(enc).split(bytes(1))[0].decode("latin1")
        g = BITREV[f.read(4)[0]]
        table = bytes(BITREV[i] ^ g for i in range(256))
        f.seek(0xD0 + n)
        out = os.path.join(outdir, os.path.splitext(os.path.basename(path))[0] + os.path.splitext(name)[1].lower())
        with open(out, "wb") as o:
            while True:
                blk = f.read(1 << 20)
                if not blk: break
                o.write(blk.translate(table))
    head = open(out, "rb").read(4)
    print(f"{os.path.basename(path)}: MOV3 nome interno {name} -> {head.hex()}")

ENC1_TABLE = os.path.join(os.path.dirname(DOG_KEY), "zero_enc1_table.bin")  # piu 0x8103940

def enc1(buf):  # piu 0x80a42e0 / 0x80a4010
    """hdr[0x86]; tamanho = u32@0x7E ^ 0xCCBB; pula u32@0x82; u32 semente (Adler-32);
    saida[i] = bitrev(src[i]) ^ T[(semente+i) & 0x3FF], T estatica de 1 KB."""
    T = open(ENC1_TABLE, "rb").read()
    size = struct.unpack_from("<I", buf, 0x7E)[0] ^ 0xCCBB
    off = 0x86 + struct.unpack_from("<I", buf, 0x82)[0]
    seed = struct.unpack_from("<I", buf, off)[0]
    src = buf[off + 4:off + 4 + size]
    rot = (T * ((len(src) >> 10) + 2))[seed & 0x3FF:][:len(src)]
    dec = bytes(a ^ b for a, b in zip(src.translate(BITREV), rot))
    return dec, zlib.adler32(dec) == seed

def guess_ext(b):
    for m, e in ((b"RIFF", ".wav"), (b"OggS", ".ogg"), (b"ID3", ".mp3"), (b"\x89PNG", ".png"),
                 (b"\xff\xd8", ".jpg"), (b"BM", ".bmp")):
        if b.startswith(m): return e
    if len(b) > 1 and b[0] == 0xFF and b[1] & 0xE0 == 0xE0: return ".mp3"
    return ".bin"

def process(path, outdir):
    with open(path, "rb") as f: magic = f.read(4)
    if magic == b"MOV3": return mov3(path, outdir)
    d = open(path, "rb").read(); base = os.path.basename(path)
    if d[:4] in (b"ENC1", b"ENC2"):
        dec, ok = (enc1 if d[:4] == b"ENC1" else enc2)(d)
        out = os.path.join(outdir, base + guess_ext(dec))
        open(out, "wb").write(dec)
        print(f"{base}: {d[:4].decode()} {len(dec)} B adler={'OK' if ok else 'FALHOU'} -> {dec[:4]!r}")
    elif d[:8] == b"RESPAC2\x1a":
        sub = os.path.join(outdir, base + "_x"); os.makedirs(sub, exist_ok=True)
        c = 0
        for name, data in respac2(d):
            p = os.path.join(sub, name.replace("\\", "/")); os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(data); c += 1
        print(f"{base}: RESPAC2 {c} entradas")
    else:
        print(f"{base}: formato desconhecido {d[:8]!r}")

if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--dog"]: DOG_KEY = args[1]; args = args[2:]  # ex.: tools/data/zero_dog.key para a Zero
    src, out = args[0], args[1]; os.makedirs(out, exist_ok=True)
    files = [os.path.join(r, f) for r, _, fs in os.walk(src) for f in fs] if os.path.isdir(src) else [src]
    for f in sorted(files):
        try: process(f, out)
        except Exception as ex: print(f"{os.path.basename(f)}: ERRO {ex}")
