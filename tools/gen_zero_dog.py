#!/usr/bin/env python3
"""Gera src/dog_table.c a partir de tools/data/nx_dog.key (dump MicroDog 3.4 do nxhook do pumptools).
So as consultas de 16 bytes (as unicas usadas pelo piu 0x80a3dc0), ordenadas para bsearch."""
import os, struct
here = os.path.dirname(os.path.abspath(__file__))
# nx_dog.key primeiro; zero_dog.key entra para o "USE OLD DATA SONGS" (capas, previas e discos
# lidos da pasta do Zero). Consulta repetida com resposta diferente aborta a geracao.
tab = {}
for key in ("nx_dog.key", "zero_dog.key"):
    raw = open(os.path.join(here, "data", key), "rb").read()
    for p in range(0xE0, len(raw) - 0x4B, 0x4C):
        resp, _, n = struct.unpack_from("<III", raw, p)
        if n != 16: continue
        q = raw[p + 12:p + 28]
        if q in tab and tab[q] != resp: raise SystemExit("conflito em %s: %s" % (key, q.hex()))
        tab.setdefault(q, resp)
    print(key, "->", len(tab), "entradas acumuladas")
out = ["/* Gerado por tools/gen_zero_dog.py (nx_dog.key + zero_dog.key) — nao editar. */",
       "/* Respostas do MicroDog 3.4 (servico Convert, piu 0x80a6058) para consultas de 16 bytes. */",
       "#include <stdint.h>", "#include <string.h>", "#include <stdlib.h>", "",
       "typedef struct { uint8_t q[16]; uint32_t r; } DogEntry;", "",
       "static const DogEntry g_dogTable[%d] = {" % len(tab)]
for q in sorted(tab):
    out.append("    {{%s}, 0x%08Xu}," % (",".join("0x%02X" % b for b in q), tab[q]))
out += ["};", "",
        "static int zd_cmp(const void* a, const void* b) { return memcmp(a, ((const DogEntry*)b)->q, 16); }", "",
        "/* Retorna 1 e preenche *resp se a consulta estiver na tabela. */",
        "int Dog_Convert16(const uint8_t* q, uint32_t* resp) {",
        "    const DogEntry* e = (const DogEntry*)bsearch(q, g_dogTable, sizeof(g_dogTable) / sizeof(g_dogTable[0]),",
        "                                                       sizeof(g_dogTable[0]), zd_cmp);",
        "    if (!e) return 0;", "    *resp = e->r;", "    return 1;", "}", ""]
open(os.path.join(here, "..", "src", "dog_table.c"), "w", encoding="utf-8", newline="\n").write("\n".join(out))
print(len(tab), "entradas")
