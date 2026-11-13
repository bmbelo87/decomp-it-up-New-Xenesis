import os
import sys
import struct
import zlib
from collections import Counter

sys.path.append(os.path.abspath('tools'))
import analyze_see_and_fire as a
import zero_decrypt

# 1. Carregar lista de musicas do jogo (de nx_songs.c)
songs = []
with open('src/nx_songs.c', 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        line = line.strip()
        # formato: { 0xD29, "...", "BanYa ", "FIRE ", "FIRE ", 136.0000, ...
        if line.startswith('{ 0x') and ',' in line:
            parts = line.split(',')
            try:
                sid = int(parts[0].replace('{', '').strip(), 16)
                artist = parts[2].strip().strip('"')
                title = parts[4].strip().strip('"')
                bpm = float(parts[5].strip())
                songs.append({'id': sid, 'artist': artist, 'title': title, 'bpm': bpm, 'raw': line})
            except Exception:
                pass

print(f"Total de musicas carregadas de nx_songs.c: {len(songs)}")

# Carregar STEP.DAT
step_path = os.path.abspath(r'..\game\STEP.DAT')
with open(step_path, 'rb') as f:
    step_data = f.read()

entries = {}
for name, content in zero_decrypt.respac2(step_data):
    if name.upper().endswith('.SEE'):
        entries[name.upper()] = content

# Carregar orfaos
import find_orphans
non_aa_orphans = [b for b, n, sz, _ in find_orphans.orphans if not b.startswith('AA')]

print(f"\n--- Comparando os {len(non_aa_orphans)} orfaos nao-AA com as musicas existentes ---")

matches = []
for b in sorted(non_aa_orphans):
    name = b + '.SEE'
    if name not in entries:
        continue
    content = entries[name]
    info = a.analyze_see(content)
    if not info:
        continue
    bpms = set()
    total_steps = 0
    secs = []
    delays = set()
    for si, sdata in sorted(info.items()):
        if sdata['total_steps'] > 0:
            total_steps += sdata['total_steps']
            secs.append(f"s{si}:{sdata['total_steps']}")
        for blk in sdata['blocks']:
            bpms.add(round(blk['bpm'], 1))
            delays.add(blk['delay'])
    
    # Procurar musicas candidatas pelo BPM
    candidate_songs = []
    for s in songs:
        # Se algum BPM do chart bate com o BPM da musica
        for cbpm in bpms:
            if abs(cbpm - s['bpm']) < 0.1:
                candidate_songs.append(s)
                break
    
    cand_str = ", ".join([f"{cs['title']} ({cs['id']:X}h {cs['bpm']}bpm)" for cs in candidate_songs[:3]])
    if len(candidate_songs) > 3:
        cand_str += f" ... (+{len(candidate_songs)-3})"
    
    print(f"{name:12s} ({total_steps:4d} steps) BPM={sorted(bpms)} Delays={sorted(delays)[:3]} -> Candidatos: {cand_str if cand_str else 'NENHUM'}")
