import os
import sys
import struct
import zlib
from collections import Counter

sys.path.append(os.path.abspath('tools'))
import analyze_see_and_fire as a

for name in ['D29', 'AA132', 'AA271', 'AA512']:
    fname, data = a.see_entries[name]
    offs = struct.unpack_from('<9I', data, 0xFC)
    print(f"*** {name} ***")
    for si, so in enumerate(offs):
        if not so:
            continue
        # 200 block counts
        block_counts = struct.unpack_from('<200I', data, so + 4)
        n_blocks = sum(block_counts)
        if n_blocks == 0:
            n_blocks = 1
        pos = so + 0x324
        total_lines = 0
        notes = []
        for bi in range(n_blocks):
            if pos + 4 > len(data):
                break
            sz, = struct.unpack_from('<I', data, pos)
            pos += 4
            blk = a.see_block(data[pos:pos+sz])
            pos += sz
            bpm, meter, speed, delay = struct.unpack_from('<fIIi', blk, 0)
            nl, = struct.unpack_from('<I', blk, 128) if len(blk) >= 132 else (0,)
            lines = min(nl, (len(blk)-132)//13)
            for li in range(lines):
                row = blk[132+li*13 : 132+li*13+10]
                if any(row):
                    notes.append((total_lines + li, [b for b in row]))
            total_lines += lines
        if notes:
            p1_active = sum(1 for line, r in notes if any(r[:5]))
            p2_active = sum(1 for line, r in notes if any(r[5:]))
            is_double = any(any(r[5:]) for line, r in notes)
            mode_type = "DOUBLE" if is_double else "SINGLE"
            print(f"  Secao {si}: total_lines={total_lines}, rows com nota={len(notes)}, p1={p1_active}, p2={p2_active}, tipo={mode_type}")
            print(f"    Primeira nota na linha {notes[0][0]}, ultima nota na linha {notes[-1][0]}")
            # Contagem dos tipos de nota
            note_types = Counter(b for line, r in notes for b in r if b)
            print(f"    Tipos de nota: {dict(note_types)}")
