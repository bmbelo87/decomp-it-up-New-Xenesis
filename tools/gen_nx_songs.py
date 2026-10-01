#!/usr/bin/env python3
"""
gen_nx_songs.py - gera src/nx_songs.c a partir da tabela de musicas do piu (Pump It Up NX, ELF i386).

Tabela 0x0813c200: 0xC0 (192) registros x 0x48 bytes (contagem: 0x8061be0 retorna 0xC0)
  +0x00 u32 sequencia (0x8061e00 busca por ela)
  +0x04 u32 id (hex -> "%03X" nos nomes de arquivo; 0x8061d90/0x8061dc0 buscam por ele)
  +0x08/+0x0C artista KR/EN, +0x10/+0x14 titulo KR/EN (ponteiros, agora UTF-8)
  +0x18 ponteiro para o BPM em texto ("101~138", "???")
  +0x1C u32 canal (0x8061e30 sorteia so musicas com (canal & 4) == 0)
  +0x20..+0x30 5 x s32 niveis (-1 = nao existe); 0x8061e30 indexa pelo modo:
        0 -> [0], 1 -> [1], 3 -> [2], 9 -> [4], outro -> [3]
  +0x34 byte, +0x35 byte, +0x36 byte (reescrito por 0x8061f50: alguma dificuldade aberta)
  +0x38 s32 (-1)
  +0x3C..+0x40 5 bytes: dificuldade aberta (0x8061ed0 / 0x8061f50)
  +0x41 byte: usado pela demo "RUN %03X -n -demo" (0x8061c00)
  +0x44 u32 (~85..283; hipotese: duracao em segundos — os FULL SONG tem 198..283)

Mapa de recursos 0x0813f860: pares { u32 id_recurso, u32 id_chart }, termina com id_chart 0.
  0x8061d10(id_chart) devolve id_recurso (-1 se nao achar); 0x8061d50 reescreve.
  Ex.: DB18 -> B18 (Another usa AUD/MOV/PNZ da B18), AA011 -> 103 (World Tour).

Uso:
  python tools/gen_nx_songs.py <piu> <saida.c>
"""
import struct
import sys

SONG_VA = 0x0813C200
SONG_COUNT = 0xC0
SONG_SIZE = 0x48
RESMAP_VA = 0x0813F860
# Hipotese pelos ids: 0 novas da NX, 1 K-POP, 2 POP, 3 BANYA, 4 FULL SONG, 5 REMIX, 6 ANOTHER
CHANNEL_NAMES = ("NX", "K-POP", "POP", "BANYA", "FULLSONG", "REMIX", "ANOTHER")
CHANNEL_COUNT = len(CHANNEL_NAMES)


def load_segs(b):
    phoff, = struct.unpack_from("<I", b, 0x1C)
    phnum, = struct.unpack_from("<H", b, 0x2C)
    segs = []
    for i in range(phnum):
        t, off, va, _, fsz, _, _, _ = struct.unpack_from("<8I", b, phoff + i * 32)
        if t == 1:
            segs.append((va, off, fsz))
    return segs


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    b = open(sys.argv[1], "rb").read()
    segs = load_segs(b)

    def off(va):
        for s, o, n in segs:
            if s <= va < s + n:
                return va - s + o
        raise ValueError("VA fora do arquivo: 0x%08X" % va)

    def cstr(va):
        if va == 0:
            return ""
        o = off(va)
        return b[o:b.index(b"\0", o)].decode("utf-8", "replace")

    resmap = []
    i = 0
    while True:
        res, chart = struct.unpack_from("<II", b, off(RESMAP_VA + i * 8))
        if chart == 0:
            break
        resmap.append((res, chart))
        i += 1
    res_of = {}
    for res, chart in resmap:
        res_of.setdefault(chart, res)  # 0x8061d10 devolve o primeiro par

    songs = []
    for i in range(SONG_COUNT):
        o = off(SONG_VA + i * SONG_SIZE)
        seq, sid, ak, ae, tk, te, bpm, ch = struct.unpack_from("<II6I", b, o)
        bpm_txt = cstr(bpm).strip()
        try:
            bpm_num = float(bpm_txt.split("~")[0])
        except ValueError:
            bpm_num = 0.0
        res = res_of.get(sid, -1)
        songs.append(dict(
            seq=seq, id=sid, ak=cstr(ak), ae=cstr(ae), tk=cstr(tk), te=cstr(te),
            bpm=bpm_num, bpmText=bpm_txt, ch=ch,
            lv=struct.unpack_from("<5i", b, o + 0x20),
            vis=b[o + 0x34], hid=b[o + 0x35], arc=b[o + 0x36],
            lock=b[o + 0x3C:o + 0x41], demo=b[o + 0x41],
            length=struct.unpack_from("<I", b, o + 0x44)[0],
            # Ids < 0x100 no mapa (D31..D43, B51...) nao sao musicas; ficam so em g_nxResMap.
            base=res if res >= 0x100 else -1))
    chans = [[s["id"] for s in songs if s["ch"] == c] for c in range(CHANNEL_COUNT)]
    cmax = max(len(c) for c in chans) + 1  # 0 termina a lista

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

    L = []
    L.append("/* GERADO por tools/gen_nx_songs.py a partir do piu (Pump It Up NX) - nao editar a mao.")
    L.append(" *   g_exSongs     <- 0x0813c200 (%d x 0x48 bytes)" % SONG_COUNT)
    L.append(" *   g_exChannels  <- campo +0x1C de cada registro, na ordem da tabela")
    L.append(" *   g_nxResMap    <- 0x0813f860 (%d pares recurso/chart, 0x8061d10) */" % len(resmap))
    L.append('#include "pumpy.h"')
    L.append("")
    L.append("#if EX_SONG_COUNT != %d || EX_CHANNEL_COUNT != %d || EX_CHANNEL_MAX != %d" % (len(songs), CHANNEL_COUNT, cmax))
    L.append('#error "pumpy.h: EX_SONG_COUNT/EX_CHANNEL_COUNT/EX_CHANNEL_MAX fora da NX (rode tools/gen_nx_songs.py)"')
    L.append("#endif")
    L.append("")
    L.append("const ExceedSong g_exSongs[EX_SONG_COUNT] = {")
    for i, s in enumerate(songs):
        L.append("    { 0x%X, %s, %s, %s, %s, %.4f, { %s }, %d, %d, { %s }, %s, %d, %d, %d, %d, %s, %d }, /* %d */" % (
            s["id"], c_escape(s["ak"]), c_escape(s["ae"]), c_escape(s["tk"]), c_escape(s["te"]), s["bpm"],
            ", ".join(str(x) for x in s["lv"]), s["vis"], s["hid"], ", ".join(str(x) for x in s["lock"]),
            ("0x%X" % s["base"]) if s["base"] >= 0 else "-1", s["ch"], s["seq"], s["demo"], s["arc"],
            c_escape(s["bpmText"]), s["length"], i))
    L.append("};")
    L.append("")
    L.append("const int g_exChannels[EX_CHANNEL_COUNT][EX_CHANNEL_MAX] = {")
    for c, ids in enumerate(chans):
        L.append("    /* %s (%d) */ { %s }," % (CHANNEL_NAMES[c], len(ids),
                 ", ".join("0x%X" % x for x in ids + [0] * (cmax - len(ids)))))
    L.append("};")
    L.append("")
    L.append("const uint32_t g_nxResMap[NX_RESMAP_COUNT][2] = {")
    for k in range(0, len(resmap), 6):
        L.append("    " + " ".join("{ 0x%X, 0x%X }," % p for p in resmap[k:k + 6]))
    L.append("};")
    L.append("")
    L.append("#if NX_RESMAP_COUNT != %d" % len(resmap))
    L.append('#error "pumpy.h: NX_RESMAP_COUNT fora da NX"')
    L.append("#endif")
    L.append("")
    open(sys.argv[2], "w", encoding="utf-8", newline="\n").write("\n".join(L))
    for c, ids in enumerate(chans):
        print("canal %d %-8s %3d musicas" % (c, CHANNEL_NAMES[c], len(ids)))
    print("EX_CHANNEL_MAX = %d; NX_RESMAP_COUNT = %d; gravado: %s" % (cmax, len(resmap), sys.argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
