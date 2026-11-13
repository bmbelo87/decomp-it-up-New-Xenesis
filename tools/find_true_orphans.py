import os
import sys
import re

# 1. Carregar todos os IDs/nomes referenciados em src/nx_world_data.c
world_missions = set()
with open('src/nx_world_data.c', 'r', encoding='latin1') as f:
    text = f.read()
    # 0xAA011, etc.
    for m in re.findall(r'0x([0-9A-Fa-f]+)', text):
        val = int(m, 16)
        world_missions.add(f"{val:X}".upper())
        world_missions.add(m.upper())

# 2. Carregar todos os IDs/nomes referenciados em src/nx_training_data.c
training_charts = set()
with open('src/nx_training_data.c', 'r', encoding='latin1') as f:
    text = f.read()
    for m in re.findall(r'0x([0-9A-Fa-f]+)', text):
        val = int(m, 16)
        training_charts.add(f"{val:X}".upper())
        training_charts.add(m.upper())

# 3. Carregar todos os IDs em src/nx_songs.c
song_ids = set()
resmap_pairs = []
with open('src/nx_songs.c', 'r', encoding='latin1') as f:
    text = f.read()
    for m in re.findall(r'0x([0-9A-Fa-f]+)', text):
        val = int(m, 16)
        song_ids.add(f"{val:X}".upper())
    for r1, r2 in re.findall(r'\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*\}', text):
        resmap_pairs.append((f"{int(r1,16):X}".upper(), f"{int(r2,16):X}".upper()))

# 4. Ler todas as entradas do STEP.DAT
sys.path.append(os.path.abspath('tools'))
import zero_decrypt

step_path = os.path.abspath(r'..\game\STEP.DAT')
with open(step_path, 'rb') as f:
    step_data = f.read()

see_files = []
for name, content in zero_decrypt.respac2(step_data):
    if name.upper().endswith('.SEE'):
        base = os.path.splitext(name)[0].upper()
        see_files.append((base, name, len(content)))

print(f"Total de .SEE no STEP.DAT: {len(see_files)}")
print(f"IDs em nx_world_data.c: {len(world_missions)}")
print(f"IDs em nx_training_data.c: {len(training_charts)}")
print(f"IDs em nx_songs.c: {len(song_ids)}")
print(f"Pares em g_nxResMap: {len(resmap_pairs)}")

# Classificar cada .SEE do STEP.DAT
in_world_tour = []
in_training = []
in_arcade_songs = []
in_resmap = []
true_orphans = []

for base, name, sz in see_files:
    # Checar se é missão do World Tour
    is_wt = base in world_missions or any(p[1] == base for p in resmap_pairs if p[1].startswith('AA'))
    # Checar se é Training
    is_tr = base in training_charts or any(p[1] == base for p in resmap_pairs if p[1].startswith('DDD'))
    # Checar se é música normal da tabela
    is_arc = base in song_ids
    # Checar se está em resmap
    is_rm = any(p[0] == base or p[1] == base for p in resmap_pairs)
    
    if is_wt:
        in_world_tour.append((base, name, sz))
    elif is_tr:
        in_training.append((base, name, sz))
    elif is_arc:
        in_arcade_songs.append((base, name, sz))
    else:
        true_orphans.append((base, name, sz))

print("\n" + "="*80)
print("CLASSIFICAÇÃO DOS 553 .SEE DO STEP.DAT:")
print("="*80)
print(f"1. Usados no WORLD TOUR (nx_world_data.c): {len(in_world_tour)} charts")
print(f"2. Usados no TRAINING (nx_training_data.c): {len(in_training)} charts")
print(f"3. Usados no ARCADE / Músicas principais (nx_songs.c): {len(in_arcade_songs)} charts")
print(f"4. VERDADEIROS ÓRFÃOS (NÃO usados em World Tour, Training ou Arcade): {len(true_orphans)} charts")

print("\nLISTA DOS VERDADEIROS ÓRFÃOS:")
for b, n, sz in true_orphans:
    print(f"  {n:15s} ({sz:6d} bytes)")
