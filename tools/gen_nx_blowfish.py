#!/usr/bin/env python3
"""Gera src/nx_blowfish.inc a partir do piu (NX): tabela inicial do Blowfish (0x811fb00,
18 P + 4x256 S, copiada por 0x80a0b90) e a chave de 24 bytes dos .SEE (0x8141e30, 0x8067e30).
Uso: gen_nx_blowfish.py <piu> <saida.inc>"""
import struct, sys
b = open(sys.argv[1], "rb").read()
phoff, = struct.unpack_from("<I", b, 0x1C); phnum, = struct.unpack_from("<H", b, 0x2C)
segs = [struct.unpack_from("<8I", b, phoff + i * 32) for i in range(phnum)]
def off(va):
    for t, o, v, _, fs, _, _, _ in segs:
        if t == 1 and v <= va < v + fs: return va - v + o
init = struct.unpack_from("<1042I", b, off(0x811FB00))
key = b[off(0x8141E30):off(0x8141E30) + 24]
assert init[0] == 0x243F6A88, "tabela inesperada"
L = ["/* GERADO por tools/gen_nx_blowfish.py a partir do piu (NX) - nao editar. */",
     "/* piu 0x811fb00: estado inicial do Blowfish (P[18] e S[4][256]) */",
     "static const uint32_t k_nxBfInit[1042] = {"]
for i in range(0, 1042, 6):
    L.append("    " + " ".join("0x%08Xu," % x for x in init[i:i + 6]))
L += ["};", "/* piu 0x8141e30: chave dos .SEE (BF_set_key(ks, 0x18, chave) em 0x8067e30) */",
      "static const uint8_t k_nxSeeKey[24] = { " + ", ".join("0x%02X" % x for x in key) + " };", ""]
open(sys.argv[2], "w", encoding="utf-8", newline="\n").write("\n".join(L))
print("gravado", sys.argv[2])
