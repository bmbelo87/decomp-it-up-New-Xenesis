import os
import sys
import struct
import zlib

sys.path.append(os.path.abspath('tools'))
import analyze_see_and_fire as a
import zero_decrypt

step_path = os.path.abspath(r'..\game\STEP.DAT')
with open(step_path, 'rb') as f:
    step_data = f.read()

entries = {}
for name, content in zero_decrypt.respac2(step_data):
    if name.upper().endswith('.SEE'):
        entries[name.upper()] = content

# Arquivos que aparecem como orfaos (sem mídias proprias)
with open('tools/find_orphans.py') as f:
    pass

import find_orphans
non_aa_orphans = [b for b, n, sz, _ in find_orphans.orphans if not b.startswith('AA')]

print(f"Total de orfaos nao-AA: {len(non_aa_orphans)}")
print(f"{'Nome':12s} {'Tamanho':7s} {'BPMs':30s} {'Secoes com passos'}")
print("-" * 75)

for b in sorted(non_aa_orphans):
    name = b + '.SEE'
    if name not in entries:
        continue
    content = entries[name]
    info = a.analyze_see(content)
    if not info:
        continue
    bpms = set()
    secs = []
    total_steps = 0
    for si, sdata in sorted(info.items()):
        if sdata['total_steps'] > 0:
            total_steps += sdata['total_steps']
            secs.append(f"s{si}({sdata['name']}):{sdata['total_steps']}")
        for blk in sdata['blocks']:
            bpms.add(round(blk['bpm'], 1))
    bpm_str = str(sorted(bpms))
    if len(bpm_str) > 30:
        bpm_str = bpm_str[:27] + "..."
    print(f"{name:12s} {len(content):6d}b {bpm_str:30s} {', '.join(secs)}")
