#!/usr/bin/env python3
"""Gera src/nx_training_data.c a partir do piu (Pump It Up NX, ELF i386).

TRAINING (CSelectEz, vtable 0x8112728): 20 lições x 3 partes.
  0x8143d30  u32[64] chart da parte: índice = lição * 3 + parte (lição 1..20, parte 1..3),
             "RUN T%02d%d" -> 0x806b68e
  0x8143e40  u32[64] nível do jogador na parte (mesmo índice; limitado a 30, 0x808137c)
  Texto da lição (0x80810e0), 20 por idioma [0x9e3d8a9]:
             0 0x8144040, 1 0x81440a0, 2 0x8144100, 3 0x8144160
  Explicação da lição na tela antes da música (0x806c470, '@' quebra a linha),
             índice = lição (1..20): 0 0x8141ebc, 1 0x8141f1c, 2 0x8141e5c, 3 0x8141f7c
Uso: gen_nx_training.py <piu> <saida.c>
"""
import sys, struct

def main():
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(1)
    b = open(sys.argv[1], "rb").read()
    phoff, = struct.unpack_from("<I", b, 0x1C); phnum, = struct.unpack_from("<H", b, 0x2C)
    segs = []
    for i in range(phnum):
        t, o, va, _, fsz, _, _, _ = struct.unpack_from("<8I", b, phoff + i * 32)
        if t == 1: segs.append((va, o, fsz))
    def off(a):
        for va, o, sz in segs:
            if va <= a < va + sz: return a - va + o
        raise ValueError(hex(a))
    def rd(a): return struct.unpack_from("<I", b, off(a))[0]
    def esc(raw):
        o = []
        for c in raw:
            if c == 0x22 or c == 0x5c: o.append("\\" + chr(c))
            elif 32 <= c < 127: o.append(chr(c))
            else: o.append("\\%03o" % c)
        return '"' + "".join(o) + '"'

    L = ["/* Gerado por tools/gen_nx_training.py a partir do piu (NX) — não editar à mão.",
         " *   g_nxTrainChart <- 0x8143d30, g_nxTrainLevel <- 0x8143e40 (índice lição*3 + parte)",
         " *   g_nxTrainDesc  <- 0x8144040 / 0x81440a0 / 0x8144100 / 0x8144160 */",
         '#include "pumpy.h"', ""]
    for name, base in (("g_nxTrainChart", 0x8143d30), ("g_nxTrainLevel", 0x8143e40)):
        v = [rd(base + 4 * k) for k in range(64)]
        L.append("const uint32_t %s[64] = {" % name)
        for i in range(0, 64, 8):
            L.append("    " + ", ".join("0x%X" % x for x in v[i:i + 8]) + ",")
        L += ["};", ""]
    L.append("const char* const g_nxTrainDesc[4][20] = {")
    for base in (0x8144040, 0x81440a0, 0x8144100, 0x8144160):
        L.append("    {")
        for k in range(20):
            o = off(rd(base + 4 * k)); e = b.find(b"\0", o)
            L.append("        %s," % esc(b[o:e]))
        L.append("    },")
    L += ["};", "", "const char* const g_nxTrainIntro[4][21] = {"]
    for base in (0x8141ebc, 0x8141f1c, 0x8141e5c, 0x8141f7c):
        L.append("    {")
        for k in range(21):
            a = rd(base + 4 * k)
            try:
                o = off(a); e = b.find(b"\0", o); L.append("        %s," % esc(b[o:e]))
            except ValueError:
                L.append('        "",')
        L.append("    },")
    L += ["};", ""]
    open(sys.argv[2], "w", encoding="utf-8", newline="\r\n").write("\n".join(L))
    print("ok")

main()
