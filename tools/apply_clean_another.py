import os
import sys

nx_songs_path = 'src/nx_songs.c'
with open(nx_songs_path, 'r', encoding='latin1') as f:
    text = f.read()

# 1. Atualizar o assert inicial
text = text.replace(
    "#if SONG_TABLE_COUNT != 277",
    "#if SONG_TABLE_COUNT != 276"
)

# 2. Localizar e substituir o bloco de novas entradas
old_block_marker = "    /* 14 novos charts"
old_block_end = "    { 0xC32, "
idx_start = text.find(old_block_marker)
if idx_start == -1:
    print("ERRO: Bloco anterior nao encontrado")
    sys.exit(1)

idx_end = text.find("\n};", idx_start)
if idx_end == -1:
    print("ERRO: Final de g_songTable nao encontrado")
    sys.exit(1)

clean_entries = """    /* 13 novos charts órfãos com níveis EXCLUSIVOS no canal ANOTHER: */
    { 0xAA271, "\\353\\260\\230\\354\\225\\274 ", "BanYa ", "FIRE(NM 12) ", "FIRE ", 136.0000, { -1, -1, -1, -1, 12 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xD29, 6, 265, 0, 1, "136", 95 }, /* 263 */
    { 0xAA512, "\\353\\260\\230\\354\\225\\274 ", "BanYa ", "FIRE(NM 19) ", "FIRE ", 136.0000, { -1, -1, -1, -1, 19 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xD29, 6, 266, 0, 1, "136", 95 }, /* 264 */
    { 0xC23, "Yahpp ", "Yahpp ", "\\355\\202\\244\\353\\251\\224\\353\\235\\274(NM 23) ", "Chimera ", 210.0000, { -1, -1, -1, -1, 23 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xD03, 6, 267, 0, 1, "210", 109 }, /* 265 */
    { 0xC26, "\\355\\201\\254\\353\\236\\230\\354\\211\\254 ", "Crash ", "\\353\\213\\210\\352\\260\\200\\354\\247\\204\\354\\247\\234\\353\\241\\234\\354\\233\\220\\355\\225\\230\\353\\212\\224\\352\\262\\214\\353\\255\\220\\354\\225\\274(H/NM) ", "What do you really want? ", 110.0000, { -1, 6, -1, -1, 20 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xA11, 6, 268, 0, 1, "110", 101 }, /* 266 */
    { 0xDA25, "DVS ", "DVS ", "Bust Back(C/NM) ", "Bust Back ", 94.0000, { -1, -1, 12, -1, 15 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xD25, 6, 269, 0, 1, "94", 100 }, /* 267 */
    { 0xA101, "\\353\\263\\264\\354\\225\\204 ", "BoA ", "\\353\\260\\234\\353\\240\\214\\355\\213\\260(NM) ", "Valenti ", 104.0000, { -1, -1, -1, -1, 14 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xA10, 6, 270, 0, 1, "104", 107 }, /* 268 */
    { 0xB902, "Yahpp ", "Yahpp ", "\\355\\214\\214\\354\\227\\2412-1(C/NM) ", "Final Audition EP.2-1 ", 170.0000, { -1, -1, 20, -1, 22 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xD28, 6, 271, 0, 1, "170", 98 }, /* 269 */
    { 0xB501, "\\353\\260\\230\\354\\225\\274 ", "BanYa ", "\\355\\225\\230\\354\\235\\264\\353\\260\\224\\354\\235\\264(NM) ", "Hi Bi ", 145.0000, { -1, -1, -1, -1, 19 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xB17, 6, 272, 0, 1, "145", 95 }, /* 270 */
    { 0xC28, "\\353\\260\\230\\354\\225\\274 ", "BanYa ", "\\352\\267\\270\\353\\205\\200\\353\\212\\224\\355\\224\\274\\354\\236\\220\\353\\245\\274\\354\\242\\213\\354\\225\\204\\355\\225\\264(NM) ", "She Likes Pizza ", 229.6000, { -1, -1, -1, -1, 19 }, 1, 1, { 1, 1, 1, 1, 1 }, 0x310, 6, 273, 0, 1, "230", 91 }, /* 271 */
    { 0xC25, "\\353\\260\\230\\354\\225\\274 ", "BanYa ", "\\355\\214\\254\\355\\205\\200(NM 18) ", "Phantom ", 136.0000, { -1, -1, -1, -1, 18 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xC05, 6, 274, 0, 1, "136", 97 }, /* 272 */
    { 0xC182, "\\354\\241\\260PD & B.E.G ", "Cho PD & B.E.G ", "Hold The Line(Normal) ", "Hold The Line ", 126.0000, { 6, -1, -1, -1, -1 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xD22, 6, 275, 0, 1, "126", 100 }, /* 273 */
    { 0x2516, "\\353\\205\\270\\353\\260\\224\\354\\206\\214\\353\\213\\211 ", "NOVASONIC ", "Slam(Crazy 12) ", "Slam ", 132.0000, { -1, -1, 12, -1, -1 }, 1, 1, { 1, 1, 1, 1, 1 }, 0x516, 6, 276, 0, 1, "132", 98 }, /* 274 */
    { 0xC32, "Yahpp ", "Yahpp ", "\\353\\237\\254\\353\\270\\214 \\354\\235\\264\\354\\212\\210 \\354\\226\\264 \\353\\211\\207\\354\\240\\200 \\354\\241\\264 2 Remix(NM) ", "Love is a Danger Zone 2 Remix ", 162.0000, { -1, -1, -1, -1, 21 }, 1, 1, { 1, 1, 1, 1, 1 }, 0xD41, 6, 277, 0, 1, "162", 140 }, /* 275 */"""

text = text[:idx_start] + clean_entries + text[idx_end:]

# 3. Atualizar g_songChannels[6] removendo FFC04
old_ch_marker = "/* ANOTHER (37 + 14 = 51) */"
idx_ch = text.find(old_ch_marker)
if idx_ch == -1:
    old_ch_marker = "/* ANOTHER"
    idx_ch = text.find(old_ch_marker)

idx_ch_end = text.find("};", idx_ch)

new_ch = """/* ANOTHER (37 + 13 = 50) */ { 0xDB18, 0xDC09, 0xD205, 0xDC04, 0xDA02, 0xD704, 0xDC03, 0xDC11, 0xD814, 0xD902, 0xD405, 0xD735, 0xD112, 0xDB13, 0xD301, 0xD818, 0xD204, 0xDA01, 0xDD07, 0xDD03, 0xD505, 0xD802, 0xD922, 0xDD28, 0xDD30, 0xDA14, 0xDD01, 0xD701, 0xDD11, 0xDC05, 0xDA11, 0xDB17, 0xD310, 0xD736, 0xDA18, 0xDB06, 0xDD16, 0xD809,
      /* 13 novos Another com charts exclusivos: */
      0xAA271, 0xAA512, 0xC23, 0xC26, 0xDA25, 0xA101, 0xB902, 0xB501, 0xC28, 0xC25, 0xC182, 0x2516, 0xC32, 0 }"""

text = text[:idx_ch] + new_ch + text[idx_ch_end:]

with open(nx_songs_path, 'w', encoding='latin1') as f:
    f.write(text)

print("nx_songs.c limpo e atualizado com sucesso!")
