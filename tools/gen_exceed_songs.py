#!/usr/bin/env python3
"""
gen_exceed_songs.py - gera src/exceed_songs.c a partir das tabelas do exceed.exe.

Tabelas lidas (VA -> offset de arquivo = VA - 0x400000, strings em .data1):
  0x004551E0  105 registros x 56 bytes (id, 4 strings, u32, double BPM,
              5 x s32 niveis, byte visivel, byte oculta, 2 bytes)
  0x004568E0  int canal[3][50], ordem de exibicao, 0 termina a lista

Uso:
  python tools/gen_exceed_songs.py <exceed.exe> <saida.c>
"""
import struct
import sys

SONG_TABLE = 0x551E0
SONG_COUNT = 105
SONG_SIZE = 56
CHANNEL_TABLE = 0x568E0
DATA1_VA = 0x1282000
DATA1_RAW = 0x64000


def va_to_off(va):
    if va >= DATA1_VA:
        return va - DATA1_VA + DATA1_RAW
    return va - 0x400000


def read_cstr(b, va):
    o = va_to_off(va)
    e = b.index(b"\0", o)
    return b[o:e].decode("cp949", "replace")


def c_escape(s):
    out = []
    for ch in s.encode("utf-8"):
        if ch in (0x22, 0x5C):
            out.append("\\" + chr(ch))
        elif 32 <= ch < 127:
            out.append(chr(ch))
        else:
            out.append("\\%03o" % ch)
    return '"' + "".join(out) + '"'


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    b = open(sys.argv[1], "rb").read()
    lines = []
    lines.append("/* GERADO por tools/gen_exceed_songs.py a partir do exceed.exe - nao editar a mao.")
    lines.append(" *   g_exSongs     <- 0x004551E0 (105 x 56 bytes)")
    lines.append(" *   g_exChannels  <- 0x004568E0 (int[3][50]) */")
    lines.append('#include "pumpy.h"')
    lines.append("")
    lines.append("const ExceedSong g_exSongs[EX_SONG_COUNT] = {")
    for i in range(SONG_COUNT):
        o = SONG_TABLE + i * SONG_SIZE
        r = struct.unpack_from("<6Id5iBBH", b, o)
        sid, ak, ae, tk, te, u14, bpm = r[0], r[1], r[2], r[3], r[4], r[5], r[6]
        lv = r[7:12]
        vis, hid = r[12], r[13]
        lines.append("    { 0x%X, %s, %s, %s, %s, %.4f, { %s }, %d, %d }, /* %d */" % (
            sid, c_escape(read_cstr(b, ak)), c_escape(read_cstr(b, ae)),
            c_escape(read_cstr(b, tk)), c_escape(read_cstr(b, te)), bpm,
            ", ".join(str(x) for x in lv), vis, hid, i))
        print("  %3d  %X  bpm=%-6g niveis=%s oculta=%d" % (i, sid, bpm, list(lv), hid))
    lines.append("};")
    lines.append("")
    lines.append("const int g_exChannels[EX_CHANNEL_COUNT][EX_CHANNEL_MAX] = {")
    for ch in range(3):
        ids = struct.unpack_from("<50I", b, CHANNEL_TABLE + ch * 200)
        n = next((k for k, x in enumerate(ids) if x == 0), 50)
        print("canal %d: %d musicas" % (ch, n))
        row = ", ".join("0x%X" % x for x in ids)
        lines.append("    { %s }," % row)
    lines.append("};")
    lines.append("")
    open(sys.argv[2], "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print("escrito: %s" % sys.argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
