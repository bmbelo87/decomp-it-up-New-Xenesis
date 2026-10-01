#!/usr/bin/env python3
"""Disassembler minimo (capstone) para o piu (ELF i386).
Uso: piudis.py <piu> <va_inicio> [n_instr=80]   -- para no primeiro 'ret' se n_instr for 0
Anota referencias a strings do .rodata."""
import sys, struct, capstone
sys.stdout.reconfigure(encoding="utf-8")
b = open(sys.argv[1], "rb").read()
# cabecalhos de programa: mapeia VA -> offset
phoff, = struct.unpack_from("<I", b, 0x1C); phnum, = struct.unpack_from("<H", b, 0x2C)
segs = []
for i in range(phnum):
    t, off, va, _, fsz, _, _, _ = struct.unpack_from("<8I", b, phoff + i * 32)
    if t == 1: segs.append((va, off, fsz))
def off(va):
    for s, o, n in segs:
        if s <= va < s + n: return va - s + o
def cstr(va):
    o = off(va)
    if o is None or not (0x20 <= b[o] < 0x7F or b[o] >= 0xC0): return None
    e = b.find(b"\0", o)
    s = b[o:e]
    return s.decode("utf-8", "replace") if 2 <= len(s) < 120 else None
va = int(sys.argv[2], 16); n = int(sys.argv[3]) if len(sys.argv) > 3 else 80
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
o = off(va); k = 0
for ins in md.disasm(b[o:o + max(0x4000, n * 16)], va):
    note = ""
    for tok in ins.op_str.replace(",", " ").replace("[", " ").replace("]", " ").split():
        if tok.startswith("0x") and len(tok) >= 9:
            s = cstr(int(tok, 16))
            if s: note = "  ; " + repr(s)
    print("%08x  %-7s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
    k += 1
    if (n and k >= n) or (not n and ins.mnemonic == "ret"): break
