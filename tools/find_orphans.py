import os
import sys
import glob

sys.path.append(os.path.abspath('tools'))
import zero_decrypt

game_dir = os.path.abspath(r'..\game')

media_bases = set()
files_by_ext = {}
all_game_files = {}

for root, dirs, files in os.walk(game_dir):
    for f in files:
        base, ext = os.path.splitext(f)
        ext = ext.upper()
        base_u = base.upper()
        files_by_ext.setdefault(ext, set()).add(base_u)
        all_game_files.setdefault(base_u, []).append(os.path.join(root, f))
        if ext in ('.AUD', '.WAV', '.DAT', '.MOV', '.PNZ'):
            if f.upper() != 'STEP.DAT':
                media_bases.add(base_u)

print(f"Mídias encontradas no jogo: {len(media_bases)}")
print(f"AUD: {len(files_by_ext.get('.AUD', set()))}")
print(f"MOV: {len(files_by_ext.get('.MOV', set()))}")
print(f"PNZ: {len(files_by_ext.get('.PNZ', set()))}")
print(f"DAT: {len(files_by_ext.get('.DAT', set()))}")

step_path = os.path.join(game_dir, 'STEP.DAT')
with open(step_path, 'rb') as f:
    step_data = f.read()

see_entries = {}
for name, content in zero_decrypt.respac2(step_data):
    base, ext = os.path.splitext(name)
    if ext.upper() == '.SEE':
        see_entries[base.upper()] = (name, len(content), content)

print(f"Total de .SEE no STEP.DAT: {len(see_entries)}")

orphans = []
for base, (name, sz, content) in sorted(see_entries.items()):
    has_aud = base in files_by_ext.get('.AUD', set()) or base in files_by_ext.get('.WAV', set())
    has_bga = (
        base in files_by_ext.get('.DAT', set()) or
        base in files_by_ext.get('.MOV', set()) or
        base in files_by_ext.get('.PNZ', set())
    )
    if not has_aud and not has_bga:
        orphans.append((base, name, sz, content))

print(f"\nTotal de .SEE órfãos (sem AUD, WAV, DAT, MOV, PNZ): {len(orphans)}")
for b, n, sz, _ in orphans:
    print(f"  {n:15s} ({sz:6d} bytes)")
