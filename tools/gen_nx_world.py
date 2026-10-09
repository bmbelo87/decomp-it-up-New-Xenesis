#!/usr/bin/env python3
"""Gera src/nx_world_data.c a partir do piu (Pump It Up NX, ELF i386).

Tabelas (CSelectWorld, WORLD TOUR):
  0x8144220  u8[64]  locais abertos de fábrica (0x8081717 / 0x8084d90)
  0x8144260  64 x {char* nome, u32 região (8 Ásia, 9 Am. Norte, 10 Am. Sul, 11 Europa), u32 número}
             (0x8144264 / 0x8144268; "RUN AA%02d%d" em 0x8084b4c usa o número)
  0x8144560  missões {u32 id 0xAAnnk, u32 nível, char* "-<modo> <mods> <condição>"}, fim em id 0
             (0x806bd94: id em +0, nível em +4 (0x806bdd7), condição em +8 (0x806be1d))
  0x8110340  u32[64] música liberada por local (índice = número - 1, 0 = nenhuma):
             MISSIONCLEAR 0x8075906, SPECIAL ZONE 0x807aaf0 (0x8061ed0(id, 1))
  Objetivo da missão (tela 0x806bee0), 192 textos por idioma [0x9e3d8a9],
  índice = local * 3 + stage, '@' = quebra de linha:
             0 0x8142340, 1 0x8142640, 2 0x8142040, 3 0x8142c40

Uso: gen_nx_world.py <piu> <saida.c>
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
    def cs(a):
        o = off(a); e = b.find(b"\0", o)
        return b[o:e].decode("latin1")
    def q(s): return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

    opened = list(b[off(0x8144220):off(0x8144220) + 64])
    stages = []
    for k in range(64):
        a = 0x8144260 + 12 * k
        stages.append((cs(rd(a)), rd(a + 4), rd(a + 8)))
    missions = []
    a = 0x8144560
    while rd(a):
        missions.append((cs(rd(a + 8)), rd(a), rd(a + 4)))
        a += 12
    print("locais: %d, missões: %d (fim em 0x%x)" % (len(stages), len(missions), a))

    L = ["/* Gerado por tools/gen_nx_world.py a partir do piu (NX) — não editar à mão.",
         " *   g_nxWorldOpen     <- 0x8144220 (locais abertos de fábrica)",
         " *   g_nxWorldStages   <- 0x8144260 (64 x {nome, região, número})",
         " *   g_nxWorldMissions <- 0x8144560 (%d x {condição, id, nível}) */" % len(missions),
         '#include "pumpy.h"', "",
         "const uint8_t g_nxWorldOpen[NX_WORLD_STAGES] = {"]
    for i in range(0, 64, 16):
        L.append("    " + ", ".join(str(x) for x in opened[i:i + 16]) + ",")
    L += ["};", "", "const NxWorldStage g_nxWorldStages[NX_WORLD_STAGES] = {"]
    for n, r, num in stages:
        L.append("    { %s, %d, %d }," % (q(n), r, num))
    L += ["};", "", "const NxWorldMission g_nxWorldMissions[NX_WORLD_MISSIONS] = {"]
    for c, i, lv in missions:
        L.append("    { %s, 0x%X, %d }," % (q(c), i, lv))
    L += ["};", "", "const uint32_t g_nxWorldUnlock[NX_WORLD_STAGES] = {"]
    unl = [rd(0x8110340 + 4 * k) for k in range(64)]
    for i in range(0, 64, 8):
        L.append("    " + ", ".join("0x%X" % x for x in unl[i:i + 8]) + ",")
    L += ["};", "", "/* bytes originais (UTF-8 no coreano) */",
          "const char* const g_nxWorldDesc[4][NX_WORLD_STAGES * 3] = {"]
    def esc(raw):
        o = []
        for c in raw:
            if c == 0x22 or c == 0x5c: o.append("\\" + chr(c))
            elif 32 <= c < 127: o.append(chr(c))
            else: o.append("\\%03o" % c)
        return '"' + "".join(o) + '"'
    for base in (0x8142340, 0x8142640, 0x8142040, 0x8142c40):
        L.append("    {")
        for k in range(192):
            o = off(rd(base + 4 * k)); e = b.find(b"\0", o)
            L.append("        %s," % esc(b[o:e]))
        L.append("    },")
    L += ["};", ""]
    open(sys.argv[2], "w", encoding="utf-8", newline="\r\n").write("\n".join(L))

main()
