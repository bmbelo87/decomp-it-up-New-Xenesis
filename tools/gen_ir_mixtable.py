#!/usr/bin/env python3
"""
gen_ir_mixtable.py - gera src/ir_mixtable.c a partir do exceed.exe.

Tabela lida (VA -> offset de arquivo = VA - 0x400000):
  0x0045C1A0  unsigned char mixtable[64][72] (Internet Ranking).
              Referenciada por Encode 0x4215DC e Decode 0x422059.
              Idêntica à do ir_password.cpp do src do Exceed (snapshot 2003-12-29).

Uso:
  python tools/gen_ir_mixtable.py <exceed.exe> <saida.c>
"""
import sys

MIX_OFF = 0x5C1A0
ROWS, COLS = 64, 72


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    exe = open(sys.argv[1], 'rb').read()
    tbl = exe[MIX_OFF:MIX_OFF + ROWS * COLS]
    for r in range(ROWS):
        if sorted(tbl[r * COLS:(r + 1) * COLS]) != list(range(COLS)):
            raise SystemExit('linha %d nao e permutacao de 0..71 - offset errado?' % r)
    out = ['/* GERADO por tools/gen_ir_mixtable.py a partir do exceed.exe - nao editar a mao.',
           ' *   g_irMixTable <- 0x0045C1A0 (64 x 72 bytes) */',
           '#include "ir_password.h"', '',
           'const unsigned char g_irMixTable[IR_MIX_ROWS][IR_MIX_COLS] = {']
    for r in range(ROWS):
        row = tbl[r * COLS:(r + 1) * COLS]
        out.append('    { ' + ', '.join('%2d' % v for v in row) + ' },')
    out.append('};')
    open(sys.argv[2], 'w', newline='\n').write('\n'.join(out) + '\n')
    print('ok: %s' % sys.argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main())
