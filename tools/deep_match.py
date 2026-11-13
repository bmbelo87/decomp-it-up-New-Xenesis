import os
import sys
import struct
import zlib
from collections import Counter

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

# Carregar músicas existentes de nx_songs.c
songs = []
with open('src/nx_songs.c', 'r', encoding='latin1') as f:
    for line in f:
        line = line.strip()
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

def get_chart_signature(raw_data):
    info = a.analyze_see(raw_data)
    if not info:
        return None
    valid_secs = {}
    for si, sdata in info.items():
        if sdata['total_steps'] > 0 and len(sdata['blocks']) > 0:
            first_blk = sdata['blocks'][0]
            valid_secs[si] = {
                'name': sdata['name'],
                'steps': sdata['total_steps'],
                'bpm': first_blk['bpm'],
                'delay': first_blk['delay'],
                'meter': first_blk['meter'],
                'speed': first_blk['speed'],
                'lines': first_blk['num_lines'],
                'all_bpms': sorted(list(set(round(b['bpm'], 1) for b in sdata['blocks']))),
                'n_blocks': len(sdata['blocks'])
            }
    return valid_secs

import find_orphans
non_aa_orphans = [b for b, n, sz, _ in find_orphans.orphans if not b.startswith('AA')]

# 1. Pre-calcular assinaturas das musicas existentes que tem .SEE
print("Pré-calculando assinaturas das músicas existentes...")
known_signatures = {}
for s in songs:
    target_see = f"{s['id']:X}.SEE"
    if target_see in entries:
        sig = get_chart_signature(entries[target_see])
        if sig:
            t_sec = next(iter(sig.values()))
            all_bpms = set()
            for si, sec in sig.items():
                all_bpms.update(sec['all_bpms'])
            known_signatures[s['id']] = {
                'song': s,
                'target_see': target_see,
                't_bpm': t_sec['bpm'],
                't_delay': t_sec['delay'],
                'all_bpms': all_bpms,
                'sig': sig
            }

print(f"Músicas conhecidas com assinatura: {len(known_signatures)}")
print(f"Analisando os {len(non_aa_orphans)} órfãos não-AA...")

results = []
for b in sorted(non_aa_orphans):
    name = b + '.SEE'
    if name not in entries:
        continue
    raw = entries[name]
    sig = get_chart_signature(raw)
    if not sig:
        results.append((name, len(raw), "Sem passos válidos", []))
        continue
    
    any_sec = next(iter(sig.values()))
    main_bpm = any_sec['bpm']
    main_delay = any_sec['delay']
    all_bpms = set()
    total_steps = 0
    for si, s in sig.items():
        all_bpms.update(s['all_bpms'])
        total_steps += s['steps']
    
    # Comparar com o cache
    matches = []
    for sid, k in known_signatures.items():
        s = k['song']
        bpm_match = abs(k['t_bpm'] - main_bpm) < 0.1
        bpm_half = abs(k['t_bpm']*2 - main_bpm) < 0.1 or abs(k['t_bpm']/2 - main_bpm) < 0.1
        delay_match = (k['t_delay'] == main_delay)
        bpms_intersect = len(set(all_bpms).intersection(k['all_bpms']))
        
        score = 0
        if bpm_match: score += 6
        elif bpm_half: score += 2
        if delay_match: score += 6
        if bpms_intersect > 0: score += bpms_intersect * 3
        if abs(s['bpm'] - main_bpm) < 0.1: score += 3
        
        # Bônus especial se o nome tem relação óbvia (ex: C25 -> C05, A101 -> A10, 2516 -> 516)
        base_name = b.replace('-', '').replace('_', '')
        sid_hex = f"{sid:X}"
        if sid_hex in base_name or base_name in sid_hex:
            score += 4
        
        if score >= 9:
            matches.append({
                'song': s,
                'score': score,
                'target_see': k['target_see'],
                't_bpm': k['t_bpm'],
                't_delay': k['t_delay']
            })
    
    matches.sort(key=lambda x: x['score'], reverse=True)
    results.append((name, len(raw), total_steps, sig, matches[:3]))

print("\n" + "="*80)
print("RESULTADOS DA CORRELAÇÃO DE CHARTS ÓRFÃOS:")
print("="*80)

for item in results:
    if len(item) == 4:
        name, sz, msg, _ = item
        print(f"\n{name:12s} ({sz:6d} bytes): {msg}")
        continue
    name, sz, steps, sig, matches = item
    sec_str = ", ".join([f"{s['name']}:{s['steps']}" for si, s in sig.items()])
    bpms_str = str(sorted(list(set(b for si, s in sig.items() for b in s['all_bpms']))))
    delays_str = str(sorted(list(set(s['delay'] for si, s in sig.items()))))
    print(f"\n{name:12s} ({sz:6d}b, {steps:4d} steps) BPM={bpms_str} Delay={delays_str}")
    print(f"  Seções ativas: {sec_str}")
    if matches:
        print("  MATCHES ENCONTRADOS:")
        for m in matches:
            s = m['song']
            print(f"    -> [{m['target_see']}] \"{s['title']}\" ({s['artist'].strip()}) [ID 0x{s['id']:X}] | BPM: {m['t_bpm']}, Delay: {m['t_delay']} (score: {m['score']})")
    else:
        print("  Nenhum match direto com músicas existentes.")
