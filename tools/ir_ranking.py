#!/usr/bin/env python3
"""
ir_ranking.py - "servidor" do Internet Ranking do Exceed.

O site original da Andamiro recebia a senha "XXXX-XXXX-XXXX-XXXX" mostrada
no fim do jogo. Este script faz o papel dele: decodifica, valida o checksum
e guarda num ranking local (JSON). Tabelas lidas direto do exceed.exe:
  0x0045C1A0  mixtable[64][72]
  0x004551E0  105 músicas x 56 bytes (id em +0, título en em +0x10)

Uso:
  python tools/ir_ranking.py decode  <senha>            [--exe caminho]
  python tools/ir_ranking.py submit  <nome> <senha>     [--db ranking.json]
  python tools/ir_ranking.py list                       [--db ranking.json]
  python tools/ir_ranking.py encode  <score> <c1> <c2> <c3> <c4>   (teste)
"""
import argparse
import json
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEF_EXE = os.path.normpath(os.path.join(HERE, '..', '..', 'game', 'exceed.exe'))
DEF_DB = os.path.join(HERE, 'ir_ranking.json')

MIX_OFF = 0x5C1A0
SONG_OFF, SONG_SIZE, SONG_COUNT = 0x551E0, 56, 105
DATA1_VA, DATA1_RAW = 0x1282000, 0x64000
MIX_POS = (10, 24, 33, 36, 49, 56, 62, 75)
MASKS = (0x0F, 0xF0, 0xAA, 0x33, 0xCC, 0x66, 0x99, 0x55)
CH_DEC = '0123TMSPBCLRAKDFIHGJ687O54EQV9UNWXYZ'
CH_ENC1 = '0123TMSPBCLRAKDFIHGJ687O54EQV9UN'
CH_ENC2 = 'WXYZTMSPBCLRAKDFIHGJ687O54EQV9UN'


class Exe:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        self.mix = [self.b[MIX_OFF + r * 72:MIX_OFF + (r + 1) * 72] for r in range(64)]
        self.songs = []
        for i in range(SONG_COUNT):
            rec = self.b[SONG_OFF + i * SONG_SIZE:SONG_OFF + (i + 1) * SONG_SIZE]
            sid = struct.unpack_from('<I', rec, 0)[0]
            self.songs.append((sid, self._str(struct.unpack_from('<I', rec, 0x10)[0]) or
                               self._str(struct.unpack_from('<I', rec, 0x0C)[0])))

    def _str(self, va):
        if not va:
            return ''
        off = va - DATA1_VA + DATA1_RAW if va >= DATA1_VA else va - 0x400000
        end = self.b.index(b'\0', off)
        raw = self.b[off:end]
        try:
            return raw.decode('ascii')
        except UnicodeDecodeError:
            return raw.decode('cp949', 'replace')

    def song(self, idx):
        if idx < len(self.songs):
            sid, name = self.songs[idx]
            return '%X %s' % (sid, name)
        return '? (índice %d fora da tabela)' % idx


def checksum(score, course):
    return (score + sum(course)) & 0xFFF          # 0x4222C7..0x4222E5


def encode(exe, score, course, mixpattern=None):
    mp = random.randrange(64) if mixpattern is None else mixpattern
    chk = checksum(score, course)
    code = list(struct.pack('<I', score)) + list(course) + [chk & 0xFF]
    for i in range(4):
        if chk & (0x100 << i):
            code[4 + i] |= 0x80
    code = [c ^ MASKS[mp % 8] for c in code]
    bits = [(code[i // 8] >> (7 - i % 8)) & 1 for i in range(72)]
    bits2 = [bits[exe.mix[mp][i]] for i in range(72)]
    for k, p in enumerate(MIX_POS):
        bits2.insert(p, (mp >> (7 - k)) & 1)
    out = ''
    for i in range(0, 80, 5):
        v = int(''.join(map(str, bits2[i:i + 5])), 2)
        out += random.choice((CH_ENC1, CH_ENC2))[v]
        if len(out) % 5 == 4 and len(out) < 19:
            out += '-'
    return out


def decode(exe, code):
    code = code.strip().upper()
    bits = []
    for ch in code:
        p = CH_DEC.find(ch)
        if p < 0:
            continue
        p %= 32
        bits += [(p >> b) & 1 for b in range(4, -1, -1)]
    if len(bits) != 80:
        raise ValueError('senha precisa ter 16 caracteres válidos (tem %d)' % (len(bits) // 5))
    mp = 0
    for k, p in enumerate(MIX_POS):
        mp |= bits[p] << (7 - k)
    for k, p in enumerate(MIX_POS):
        del bits[p - k]
    if mp >= 64:
        raise ValueError('mixpattern inválido')
    bits2 = [0] * 72
    for i in range(72):
        bits2[exe.mix[mp][i]] = bits[i]
    cb = [int(''.join(map(str, bits2[i:i + 8])), 2) ^ MASKS[mp % 8] for i in range(0, 72, 8)]
    score = struct.unpack('<I', bytes(cb[0:4]))[0]
    chk = cb[8]
    for i in range(4):
        chk |= (cb[4 + i] & 0x80) << (1 + i)
    course = [c & 0x7F for c in cb[4:8]]
    return score, course, chk, chk == checksum(score, course)


def load_db(path):
    if os.path.exists(path):
        return json.load(open(path, encoding='utf-8'))
    return []


def main():
    ap = argparse.ArgumentParser(description='Internet Ranking do Exceed')
    ap.add_argument('--exe', default=DEF_EXE)
    ap.add_argument('--db', default=DEF_DB)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('decode'); s.add_argument('senha')
    s = sub.add_parser('submit'); s.add_argument('nome'); s.add_argument('senha')
    sub.add_parser('list')
    s = sub.add_parser('encode'); s.add_argument('score', type=int); s.add_argument('cursos', type=int, nargs=4)
    a = ap.parse_args()
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    exe = Exe(a.exe)

    if a.cmd == 'encode':
        print(encode(exe, a.score, a.cursos))
        return 0

    if a.cmd in ('decode', 'submit'):
        score, course, chk, ok = decode(exe, a.senha)
        print('score   : %d' % score)
        for i, c in enumerate(course):
            print('estágio %d: %s' % (i + 1, exe.song(c)))
        print('checksum: %d (%s)' % (chk, 'válido' if ok else 'INVÁLIDO'))
        if a.cmd == 'decode':
            return 0 if ok else 1
        if not ok:
            print('não registrado: checksum inválido')
            return 1
        db = load_db(a.db)
        code = a.senha.strip().upper()
        if any(e['senha'] == code for e in db):
            print('senha já registrada')
            return 1
        db.append({'nome': a.nome.upper()[:8], 'score': score, 'cursos': course, 'senha': code})
        db.sort(key=lambda e: -e['score'])
        json.dump(db, open(a.db, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        pos = next(i for i, e in enumerate(db) if e['senha'] == code) + 1
        print('registrado em %dº lugar' % pos)
        return 0

    if a.cmd == 'list':
        for i, e in enumerate(load_db(a.db)):
            songs = ' / '.join(exe.song(c).split(' ', 1)[1] for c in e['cursos'])
            print('%2d. %-8s %9d  %s' % (i + 1, e['nome'], e['score'], songs))
        return 0


if __name__ == '__main__':
    sys.exit(main())
