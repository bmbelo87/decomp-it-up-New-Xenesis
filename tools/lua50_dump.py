#!/usr/bin/env python3
"""Desmonta bytecode Lua 5.0 (os .LUA do Pump It Up Zero) e reconstroi atribuicoes simples.

Uso: lua50_dump.py <arquivo.LUA> [-v]
Imprime as instrucoes (-v) e, para cada SETTABLE/SETGLOBAL com constantes, a atribuicao.
"""
import struct, sys

OPS = ["MOVE","LOADK","LOADBOOL","LOADNIL","GETUPVAL","GETGLOBAL","GETTABLE","SETGLOBAL","SETUPVAL",
       "SETTABLE","NEWTABLE","SELF","ADD","SUB","MUL","DIV","POW","UNM","NOT","CONCAT","JMP","EQ","LT",
       "LE","TEST","CALL","TAILCALL","RETURN","FORLOOP","TFORLOOP","TFORPREP","SETLIST","SETLISTO",
       "CLOSE","CLOSURE"]
MAXSTACK = 250  # RK: >= MAXSTACK -> constante

class R:
    def __init__(s, b): s.b, s.p = b, 0
    def u8(s): v = s.b[s.p]; s.p += 1; return v
    def i32(s): v = struct.unpack_from("<i", s.b, s.p)[0]; s.p += 4; return v
    def u32(s): v = struct.unpack_from("<I", s.b, s.p)[0]; s.p += 4; return v
    def num(s): v = struct.unpack_from("<d", s.b, s.p)[0]; s.p += 8; return v
    def str(s):
        n = s.u32()
        if n == 0: return None
        v = s.b[s.p:s.p + n - 1]; s.p += n
        return v.decode("cp949", "replace")

def func(r):
    f = {"src": r.str(), "line": r.i32(), "nups": r.u8(), "npar": r.u8(), "va": r.u8(), "maxst": r.u8()}
    n = r.i32(); r.p += 4 * n                      # lineinfo
    n = r.i32()
    for _ in range(n): r.str(); r.i32(); r.i32()   # locvars
    n = r.i32()
    for _ in range(n): r.str()                     # upvalues
    n = r.i32(); k = []
    for _ in range(n):
        t = r.u8()
        if t == 3: k.append(r.num())
        elif t == 4: k.append(r.str())
        elif t == 0: k.append(None)
        else: raise ValueError("tipo de constante %d" % t)
    f["k"] = k
    n = r.i32(); f["p"] = [func(r) for _ in range(n)]
    n = r.i32(); f["code"] = [r.u32() for _ in range(n)]
    return f

def dec(i):
    op = i & 0x3F; c = (i >> 6) & 0x1FF; b = (i >> 15) & 0x1FF; a = (i >> 24) & 0xFF
    bx = (i >> 6) & 0x3FFFF
    return op, a, b, c, bx

def show(f, verbose, depth=0):
    k = f["k"]; reg = {}
    rk = lambda x: repr(k[x - MAXSTACK]) if x >= MAXSTACK else ("R%d=%s" % (x, reg.get(x, "?")))
    ind = "  " * depth
    for pc, ins in enumerate(f["code"]):
        op, a, b, c, bx = dec(ins)
        name = OPS[op] if op < len(OPS) else "OP%d" % op
        if verbose: print(ind + "%4d %-9s A=%d B=%d C=%d Bx=%d" % (pc, name, a, b, c, bx))
        if name == "LOADK": reg[a] = repr(k[bx])
        elif name == "GETGLOBAL": reg[a] = str(k[bx])
        elif name == "NEWTABLE": reg[a] = "{}"
        elif name == "MOVE": reg[a] = reg.get(b, "?")
        elif name == "GETTABLE": reg[a] = "%s[%s]" % (reg.get(b, "?"), rk(c))
        elif name == "SETTABLE": print(ind + "%s[%s] = %s" % (reg.get(a, "R%d" % a), rk(b), rk(c)))
        elif name == "SETGLOBAL": print(ind + "%s = %s" % (k[bx], reg.get(a, "R%d" % a)))
        elif name == "LOADBOOL": reg[a] = "true" if b else "false"
        elif name == "LOADNIL":
            for x in range(a, b + 1): reg[x] = "nil"
        elif name == "CLOSURE": reg[a] = "function#%d" % bx
    for i, p in enumerate(f["p"]):
        print(ind + "-- function#%d (linha %d, %d params)" % (i, p["line"], p["npar"]))
        show(p, verbose, depth + 1)

def main():
    b = open(sys.argv[1], "rb").read()
    if b[:4] != b"\x1bLua" or b[4] != 0x50: raise SystemExit("nao e Lua 5.0")
    r = R(b); r.p = 4 + 1 + 1 + 5 + 1 + 8   # versao, endian, sizes(int,size_t,instr,...) , opbits, number size, teste
    # cabecalho 5.0: version(1) endian(1) int(1) size_t(1) instr(1) SIZE_OP(1) SIZE_A(1) SIZE_B(1) SIZE_C(1) number(1) test(8)
    r.p = 4 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 8
    show(func(r), "-v" in sys.argv)

if __name__ == "__main__":
    main()
