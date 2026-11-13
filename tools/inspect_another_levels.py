import os
import sys

sys.path.append('tools')
import analyze_see_and_fire as a
import zero_decrypt

step_path = os.path.abspath(r'..\game\STEP.DAT')
with open(step_path, 'rb') as f:
    step_data = f.read()

entries = {}
for name, content in zero_decrypt.respac2(step_data):
    if name.upper().endswith('.SEE'):
        entries[name.upper()] = content

charts_info = [
    ('AA271', 0xD29, 'BanYa', 'FIRE', 'FIRE(NM 12)', 136.0, 95),
    ('AA512', 0xD29, 'BanYa', 'FIRE', 'FIRE(NM 19)', 136.0, 95),
    ('FFC04', 0xC04, 'BanYa', 'Love is a Danger Zone pt. 2', 'Love is a Danger Zone pt. 2(Another)', 162.0, 102),
    ('C23',   0xD03, 'Yahpp', 'Chimera', 'Chimera(Another)', 210.0, 109),
    ('C26',   0xA11, 'Crash', 'What do you really want?', 'What do you really want?(Another)', 110.0, 101),
    ('A-25',  0xD25, 'DVS', 'Bust Back', 'Bust Back(Another)', 94.0, 100),
    ('A101',  0xA10, 'BoA', 'Valenti', 'Valenti(Another)', 104.0, 107),
    ('B902',  0xD28, 'Yahpp', 'Final Audition EP.2-1', 'Final Audition EP.2-1(Another 2)', 170.0, 98),
    ('B501',  0xB17, 'BanYa', 'Hi Bi', 'Hi Bi(Another 2)', 145.0, 95),
    ('C28',   0x310, 'BanYa', 'She Likes Pizza', 'She Likes Pizza(Another 2)', 229.6, 91),
    ('C25',   0xC05, 'BanYa', 'Phantom', 'Phantom(Another 2)', 136.0, 97),
    ('C182',  0xD22, 'Cho PD & B.E.G', 'Hold The Line', 'Hold The Line(Another)', 126.0, 100),
    ('2516',  0x516, 'NOVASONIC', 'Slam', 'Slam(Another)', 132.0, 98),
    ('C32',   0xD41, 'Yahpp', 'Love is a Danger Zone 2 Remix', 'Love is a Danger Zone 2 Remix(Another)', 162.0, 140),
]

for cf, base, art, tit_orig, tit_an, bpm, dur in charts_info:
    raw = entries.get(cf + '.SEE')
    if not raw:
        print(f"ERRO: {cf} nao encontrado!")
        continue
    sig = a.analyze_see(raw)
    levels = [-1, -1, -1, -1, -1] # Normal, Hard, Crazy, Freestyle, Nightmare
    # Mapeamento do step.c: 0=Practice/Normal, 1=Normal/Hard, 2=Hard/Crazy, 3=Nightmare/Freestyle, 4=Crazy/Nightmare, 5=Double
    # Vamos ver quais seções têm steps
    active_sec_strs = []
    for si, s in sorted(sig.items()):
        if s['total_steps'] > 0:
            active_sec_strs.append(f"s{si}:{s['total_steps']}")
    print(f"{cf:7s} (Base 0x{base:X}) BPM={bpm} -> {active_sec_strs}")
