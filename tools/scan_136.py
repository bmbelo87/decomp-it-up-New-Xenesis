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

print(f"Total de .SEE no STEP.DAT: {len(entries)}")
print("Listando charts com BPM 136.0...")

for name, content in sorted(entries.items()):
    info = a.analyze_see(content)
    if not info:
        continue
    bpms = set()
    total_steps = 0
    sec_summary = []
    for si, sdata in sorted(info.items()):
        if sdata['total_steps'] > 0:
            total_steps += sdata['total_steps']
            sec_summary.append(f"s{si}({sdata['name']}):{sdata['total_steps']}")
        for b in sdata['blocks']:
            bpms.add(round(b['bpm'], 1))
    if 136.0 in bpms:
        print(f"{name:15s} size={len(content):6d} steps={total_steps:4d} BPMs={sorted(bpms)} secoes=[{', '.join(sec_summary)}]")
