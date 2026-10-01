#!/usr/bin/env python3
"""
gen_zero_songs.py - gera src/zero_songs.c a partir da tabela de musicas do piu (Pump It Up Zero, ELF i386).

Tabela 0x08119040: 0x94 (148) registros x 0x4C bytes (contagem: 0x805a2f0 retorna 0x94)
  +0x00 u32 sequencia (0x805a4d0 devolve por id)
  +0x04 u32 id (hex -> "%X" nos nomes de arquivo)
  +0x08 s32 id base (-1 = a propria): Another reaproveita AUD/MOV/DAT da base (0x805a460, usado
        pelo CPlayEngine 0x8081c97 quando BGA/%X.MOV ou AUDIO/%03X.AUD do id nao existe)
  +0x0C/+0x10 artista KR/EN, +0x14/+0x18 titulo KR/EN (ponteiros, CP949)
  +0x1C double BPM
  +0x24 u32 canal: 0 BANYA, 1 K-POP, 2 POP, 3 REMIX, 4 ANOTHER
  +0x28..+0x38 5 x s32 niveis NORMAL HARD CRAZY FREESTYLE NIGHTMARE (-1 = nao existe)
  +0x3C byte desligada (reescrito por 0x805a2c0 com o vetor do SETUP em PIUZERO.INI+0xF38;
        padrao 0x805a290 = tudo ligado)
  +0x3D byte fixa (o SETUP nao deixa desligar)   +0x3E byte disponivel (0 nas Another; o
        construtor 0x805aa60 ainda trava C03/C17 e abre parte das Another por dificuldade)
  +0x40 s32 textura (-1)
  +0x44..+0x48 5 bytes de trava por dificuldade, +0x49 byte usado pela demo (0x805a310)

Os canais nao sao uma tabela no binario: a lista sai do campo +0x24 na ordem da tabela.

Uso:
  python tools/gen_zero_songs.py <piu> <saida.c>
"""
import struct
import sys

BASE = 0x08048000
SONG_VA = 0x08119040
SONG_COUNT = 0x94
SONG_SIZE = 0x4C
CHANNEL_COUNT = 5
CHANNEL_NAMES = ("BANYA", "K-POP", "POP", "REMIX", "ANOTHER")

# C44 "BANYA HARD REMIX": nenhuma referencia a 0xC44 no piu. Secoes do C44.STX
# com dados: 2, 4, 5 (Hard, Crazy, Freestyle, como as C41..C43); niveis tirados
# do TITLE/TC44E.PNZ (Crazy 18); Hard e Freestyle desconhecidos -> 0 ("?").
EXTRA_SONGS = [
    dict(id=0xC44, base=-1, ak="반야", ae="BanYa", tk="BANYA HARD REMIX", te="",
         bpm=0.0, ch=3, lv=(-1, 0, 18, 0, -1), vis=1, hid=0, arc=1,
         lock=bytes([1, 1, 1, 1, 1]), demo=0),
]


def read_cstr(b, va):
    if va == 0:
        return ""
    o = va - BASE
    return b[o:b.index(b"\0", o)].decode("cp949", "replace")


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
    so = SONG_VA - BASE
    songs = []
    for i in range(SONG_COUNT):
        o = so + i * SONG_SIZE
        seq, sid, base, ak, ae, tk, te = struct.unpack_from("<IIi4I", b, o)
        songs.append(dict(
            seq=seq, id=sid, base=base,
            ak=read_cstr(b, ak), ae=read_cstr(b, ae), tk=read_cstr(b, tk), te=read_cstr(b, te),
            bpm=struct.unpack_from("<d", b, o + 0x1C)[0],
            ch=struct.unpack_from("<I", b, o + 0x24)[0],
            lv=struct.unpack_from("<5i", b, o + 0x28),
            vis=b[o + 0x3C], hid=b[o + 0x3D], arc=b[o + 0x3E],
            lock=b[o + 0x44:o + 0x49], demo=b[o + 0x49]))
    # Extras deste projeto (nao estao na tabela do piu): musicas com todos os
    # arquivos no jogo (STEP.DAT, AUDIO, BGA, PREVIEW, TITLE, 90.DAT) mas sem
    # registro no executavel desta versao.
    for x in EXTRA_SONGS:
        songs.append(dict(x, seq=len(songs) + 1000))
    chans = [[s["id"] for s in songs if s["ch"] == c] for c in range(CHANNEL_COUNT)]
    cmax = max(len(c) for c in chans) + 1  # 0 termina a lista

    L = []
    L.append("/* GERADO por tools/gen_zero_songs.py a partir do piu (Pump It Up Zero) - nao editar a mao.")
    L.append(" *   g_exSongs     <- 0x08119040 (%d x 0x4C bytes)" % SONG_COUNT)
    L.append(" *   g_exChannels  <- campo +0x24 de cada registro, na ordem da tabela */")
    L.append('#include "pumpy.h"')
    L.append("")
    L.append("#if EX_SONG_COUNT != %d || EX_CHANNEL_COUNT != %d || EX_CHANNEL_MAX != %d" % (len(songs), CHANNEL_COUNT, cmax))
    L.append('#error "pumpy.h: EX_SONG_COUNT/EX_CHANNEL_COUNT/EX_CHANNEL_MAX fora do Zero (rode tools/gen_zero_songs.py)"')
    L.append("#endif")
    L.append("")
    L.append("const ExceedSong g_exSongs[EX_SONG_COUNT] = {")
    for i, s in enumerate(songs):
        L.append("    { 0x%X, %s, %s, %s, %s, %.4f, { %s }, %d, %d, { %s }, %s, %d, %d, %d, %d }, /* %d */" % (
            s["id"], c_escape(s["ak"]), c_escape(s["ae"]), c_escape(s["tk"]), c_escape(s["te"]), s["bpm"],
            ", ".join(str(x) for x in s["lv"]), s["vis"], s["hid"], ", ".join(str(x) for x in s["lock"]),
            ("0x%X" % s["base"]) if s["base"] >= 0 else "-1", s["ch"], s["seq"], s["demo"], s["arc"], i))
    L.append("};")
    L.append("")
    L.append("const int g_exChannels[EX_CHANNEL_COUNT][EX_CHANNEL_MAX] = {")
    for c, ids in enumerate(chans):
        L.append("    /* %s (%d) */ { %s }," % (CHANNEL_NAMES[c], len(ids),
                 ", ".join("0x%X" % x for x in ids + [0] * (cmax - len(ids)))))
    L.append("};")
    L.append("")
    open(sys.argv[2], "w", encoding="utf-8", newline="\n").write("\n".join(L))
    for c, ids in enumerate(chans):
        print("canal %d %-8s %3d musicas" % (c, CHANNEL_NAMES[c], len(ids)))
    print("EX_CHANNEL_MAX = %d; gravado: %s" % (cmax, sys.argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
