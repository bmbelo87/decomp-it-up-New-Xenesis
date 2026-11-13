import os
import sys
import struct
import zlib
from collections import Counter

sys.path.append(os.path.abspath('tools'))
import zero_decrypt

# Carregar o blowfish do piu
PIU_PATH = os.path.abspath(r'..\game\piu')
b_piu = open(PIU_PATH, 'rb').read()

def off(va):
    # .data da NX
    return va - 0x08137000 + 0xEF000

init = struct.unpack_from('<1042I', b_piu, off(0x811FB00))
key = b_piu[off(0x8141E30):off(0x8141E30) + 24]
P = list(init[:18])
S = [list(init[18 + 256 * i:18 + 256 * (i + 1)]) for i in range(4)]
F = lambda x: ((((S[0][x >> 24] + S[1][(x >> 16) & 255]) & 0xFFFFFFFF) ^ S[2][(x >> 8) & 255]) + S[3][x & 255]) & 0xFFFFFFFF

def enc(l, r):
    for i in range(16):
        l ^= P[i]; r ^= F(l); l, r = r, l
    l, r = r, l
    return l ^ P[17], r ^ P[16]

def dec(l, r):
    for i in range(17, 1, -1):
        l ^= P[i]; r ^= F(l); l, r = r, l
    l, r = r, l
    return l ^ P[0], r ^ P[1]

j = 0
for i in range(18):
    d = 0
    for _ in range(4):
        d = (d << 8) | key[j % 24]
        j += 1
    P[i] ^= d

l = r = 0
for i in range(0, 18, 2):
    l, r = enc(l, r)
    P[i], P[i + 1] = l, r

for s in range(4):
    for i in range(0, 256, 2):
        l, r = enc(l, r)
        S[s][i], S[s][i + 1] = l, r

def see_block(z):
    blk = bytearray(zlib.decompress(z))
    for i in range(0, len(blk) - 7, 0x18):
        struct.pack_into('<2I', blk, i, *dec(*struct.unpack_from('<2I', blk, i)))
    return blk

def analyze_see(raw_data):
    """Retorna informacoes das secoes e blocos de um .SEE"""
    if len(raw_data) < 0x120:
        return None
    offs = struct.unpack_from('<9I', raw_data, 0xFC)
    sec_names = ['NORMAL', 'HARD', 'CRAZY', 'FREESTYLE', 'NIGHTMARE', 'SEC5', 'SEC6', 'SEC7', 'SEC8']
    res = {}
    for si, so in enumerate(offs):
        if not so or so + 0x328 > len(raw_data):
            continue
        # header da secao tem contadores de blocos: 50 inteiros
        block_counts = struct.unpack_from('<50I', raw_data, so + 4)
        n_blocks = max(1, sum(block_counts))
        pos = so + 0x324
        blocks_info = []
        total_steps = 0
        notes_counter = Counter()
        for k in range(n_blocks):
            if pos + 4 > len(raw_data):
                break
            sz, = struct.unpack_from('<I', raw_data, pos)
            pos += 4
            if pos + sz > len(raw_data):
                break
            try:
                blk = see_block(raw_data[pos:pos + sz])
            except Exception as e:
                pos += sz
                continue
            pos += sz
            bpm, meter, speed, delay = struct.unpack_from('<fIIi', blk, 0)
            num_lines = struct.unpack_from('<I', blk, 128)[0] if len(blk) >= 132 else 0
            # contar notas
            lines_data = []
            if len(blk) >= 132:
                actual_lines = min(num_lines, (len(blk) - 132) // 13)
                for row_idx in range(actual_lines):
                    row = blk[132 + row_idx * 13: 132 + row_idx * 13 + 10]
                    # paineis ativos
                    active = [p for p, b in enumerate(row) if b in (1, 2)]
                    if active:
                        total_steps += 1
                        for p in active:
                            notes_counter[p] += 1
            blocks_info.append({
                'bpm': bpm, 'meter': meter, 'speed': speed, 'delay': delay,
                'num_lines': num_lines, 'steps': total_steps
            })
        res[si] = {
            'name': sec_names[si] if si < len(sec_names) else f'SEC{si}',
            'n_blocks': n_blocks,
            'blocks': blocks_info,
            'total_steps': total_steps,
            'notes': dict(notes_counter)
        }
    return res

# Ler STEP.DAT
step_path = os.path.abspath(r'..\game\STEP.DAT')
with open(step_path, 'rb') as f:
    step_data = f.read()

see_entries = {}
for name, content in zero_decrypt.respac2(step_data):
    base, ext = os.path.splitext(name)
    if ext.upper() == '.SEE':
        see_entries[base.upper()] = (name, content)

print(f"Total .SEE carregados: {len(see_entries)}")

# Analisar D29 e os AA relacionados
for target in ['D29', 'AA132', 'AA271', 'AA512']:
    if target in see_entries:
        fname, data = see_entries[target]
        info = analyze_see(data)
        print(f"\n=================== {fname} ({len(data)} bytes) ===================")
        for si, sdata in sorted(info.items()):
            print(f"  Secao {si} ({sdata['name']}): {sdata['total_steps']} steps, {sdata['n_blocks']} blocos")
            for bi, b in enumerate(sdata['blocks'][:3]):
                print(f"    bloco {bi}: BPM={b['bpm']:.1f}, delay={b['delay']}, lines={b['num_lines']}")
            if len(sdata['blocks']) > 3:
                print(f"    ... e mais {len(sdata['blocks'])-3} blocos")
    else:
        print(f"Target {target} NAO encontrado!")
