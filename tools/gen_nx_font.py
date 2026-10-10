#!/usr/bin/env python3
"""Gera src/nx_font_data.c com a fonte bitmap do SETUP (Service Menu) do piu (NX).

Fonte do objeto 0x9e0cf58 (construtor 0x8059800, estilo [+4] = 1 = sem efeito),
desenhada por 0x805a560 -> 0x8059e40, sem textura (glDisable(GL_TEXTURE_2D)),
um GL_POINTS por bit aceso:
  ASCII  (0x8059870): 8x16, 16 bytes por caractere em 0x8138460 (256 caracteres),
                      ponto em (x + coluna, y + 16 - linha), avanço 8.
  Hangul (0x8059980): KS X 1001 (CP949, 2 bytes) -> índice (hi*0x5E + lo - 0x4141)
                      -> johab pela tabela 0x80fb540 (2350 sílabas) -> 8x4x4:
    cho/jung/jong = (code>>10)&31, (code>>5)&31, code&31, mapeados por
    0x80fc7a0 / 0x80fc7c0 / 0x80fc7e0;
    variante da inicial: 0x80fc800[jung] sem final, 0x80fc816[jung] com final;
    variante da medial: 0 se inicial <= 1 ou == 16, senão 1; +2 com final;
    variante da final: 0x80fc82c[jung];
    bitmaps 16x16 (32 bytes): inicial 0x8139460 + (cho + v*20)*32,
    medial 0x813a860 + (jung + v*22)*32, final 0x813b360 + (jong + v*28)*32 (OR).
Uso: gen_nx_font.py <piu> <saida.c>
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
    def blob(a, n): return b[off(a):off(a) + n]

    def arr_u8(name, data, per_line=16, dims=""):
        L = ["const uint8_t %s%s = {" % (name, dims)]
        for i in range(0, len(data), per_line):
            L.append("    " + ", ".join("0x%02X" % x for x in data[i:i + per_line]) + ",")
        L.append("};")
        return L

    def arr_s8(name, data):
        return ["const int8_t %s[%d] = { %s };" % (name, len(data), ", ".join(str(struct.unpack('b', bytes([x]))[0]) for x in data))]

    L = ["/* Gerado por tools/gen_nx_font.py a partir do piu (NX) — não editar à mão.",
         " * Fonte bitmap do SETUP: ASCII 8x16 (0x8138460) e hangul johab 8x4x4",
         " * (0x8139460 / 0x813a860 / 0x813b360, tabelas 0x80fc7a0..0x80fc82c, KS 0x80fb540). */",
         '#include <stdint.h>', ""]
    L += arr_u8("g_nxFontAsc", blob(0x8138460, 256 * 16), dims="[256 * 16]"); L.append("")
    L += arr_u8("g_nxFontCho", blob(0x8139460, 160 * 32), dims="[160 * 32]"); L.append("")
    L += arr_u8("g_nxFontJung", blob(0x813a860, 88 * 32), dims="[88 * 32]"); L.append("")
    L += arr_u8("g_nxFontJong", blob(0x813b360, 112 * 32), dims="[112 * 32]"); L.append("")
    for name, a, n in (("g_nxFontMapCho", 0x80fc7a0, 32), ("g_nxFontMapJung", 0x80fc7c0, 32),
                       ("g_nxFontMapJong", 0x80fc7e0, 32), ("g_nxFontVarCho0", 0x80fc800, 22),
                       ("g_nxFontVarCho1", 0x80fc816, 22), ("g_nxFontVarJong", 0x80fc82c, 22)):
        L += arr_s8(name, blob(a, n))
    L.append("")
    ks = struct.unpack_from("<2350H", b, off(0x80fb540))
    L.append("const uint16_t g_nxFontKs[2350] = {")
    for i in range(0, 2350, 12):
        L.append("    " + ", ".join("0x%04X" % x for x in ks[i:i + 12]) + ",")
    L += ["};", ""]
    open(sys.argv[2], "w", encoding="utf-8", newline="\r\n").write("\n".join(L))
    print("ok")

main()
