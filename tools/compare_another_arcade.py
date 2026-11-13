import os
import sys
import struct
import zlib

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

def extract_section_rows(data, si):
    """Extrai todas as linhas decifradas de uma seção específica de um .SEE"""
    if len(data) < 0x120:
        return []
    offs = struct.unpack_from('<9I', data, 0xFC)
    if si >= len(offs) or not offs[si]:
        return []
    so = offs[si]
    if so + 0x328 > len(data):
        return []
    block_counts = struct.unpack_from('<200I', data, so + 4)
    n_blocks = max(1, sum(block_counts))
    pos = so + 0x324
    all_rows = []
    for bi in range(n_blocks):
        if pos + 4 > len(data):
            break
        sz, = struct.unpack_from('<I', data, pos)
        pos += 4
        if pos + sz > len(data):
            break
        blk = a.see_block(data[pos:pos+sz])
        pos += sz
        if len(blk) >= 132:
            num_lines, = struct.unpack_from('<I', blk, 128)
            actual_lines = min(num_lines, (len(blk) - 132) // 13)
            for li in range(actual_lines):
                # Pegar os 10 bytes dos paineis (half1 e half2)
                row = bytes(blk[132 + li * 13 : 132 + li * 13 + 10])
                all_rows.append(row)
    return all_rows

# Lista de pares (Nome do arquivo Another, Nome do arquivo Arcade base, Título)
pairs = [
    ('AA271', 'D29',  'FIRE'),
    ('AA512', 'D29',  'FIRE'),
    ('FFC04', 'C04',  'Love is a Danger Zone pt. 2'),
    ('C23',   'D03',  'Chimera'),
    ('C26',   'A11',  'What do you really want?'),
    ('A-25',  'D25',  'Bust Back'),
    ('A101',  'A10',  'Valenti'),
    ('B902',  'D28',  'Final Audition EP.2-1'),
    ('B501',  'B17',  'Hi Bi'),
    ('C28',   '310',  'She Likes Pizza'),
    ('C25',   'C05',  'Phantom'),
    ('C182',  'D22',  'Hold The Line'),
    ('2516',  '516',  'Slam'),
    ('C32',   'D41',  'Love is a Danger Zone 2 Remix'),
]

# Nome das seções e mapeamento no TrackEntry.level[5]
# 0: Normal / Practice
# 1: Hard / Normal
# 2: Crazy / Hard
# 3: Nightmare
# 4: Crazy
# 5: Freestyle / Double
sec_to_mode = {
    0: 'NORMAL(s0)',
    1: 'HARD(s1)',
    2: 'CRAZY(s2)',
    3: 'NIGHTMARE(s3)',
    4: 'CRAZY(s4)',
    5: 'FREESTYLE(s5)',
}

print("="*90)
print("COMPARAÇÃO SEÇÃO POR SEÇÃO: ANOTHER vs ARCADE")
print("="*90)

for an_name, arc_name, title in pairs:
    an_raw = entries.get(an_name + '.SEE')
    arc_raw = entries.get(arc_name + '.SEE')
    if not an_raw or not arc_raw:
        print(f"Erro ao carregar {an_name} ou {arc_name}")
        continue
    
    print(f"\n--- {an_name}.SEE vs {arc_name}.SEE ({title}) ---")
    diff_secs = []
    same_secs = []
    
    for si in range(6):
        an_rows = extract_section_rows(an_raw, si)
        arc_rows = extract_section_rows(arc_raw, si)
        
        an_notes = sum(1 for r in an_rows if any(r))
        arc_notes = sum(1 for r in arc_rows if any(r))
        
        mode_label = sec_to_mode.get(si, f"SEC{si}")
        
        if an_notes == 0:
            continue
            
        if an_notes > 0 and arc_notes == 0:
            diff_secs.append((si, mode_label, an_notes, arc_notes, "INÉDITO (Não existe no Arcade)"))
        elif an_rows == arc_rows:
            same_secs.append((si, mode_label, an_notes, arc_notes, "IDÊNTICO ao Arcade"))
        else:
            diff_secs.append((si, mode_label, an_notes, arc_notes, "DIFERENTE do Arcade"))
    
    for si, ml, an_n, arc_n, status in same_secs:
        print(f"  [=] Seção {si} ({ml}): {an_n} notas -> {status}")
    for si, ml, an_n, arc_n, status in diff_secs:
        print(f"  [+] Seção {si} ({ml}): {an_n} notas (Arcade tinha {arc_n}) -> {status}")
