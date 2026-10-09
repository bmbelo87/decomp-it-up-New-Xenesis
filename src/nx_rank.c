/*
 * nx_rank.c — ranking da NX (CRankManager, objeto 0x81db160), /SETTINGS/RANK.DAT.
 *
 * Arquivo de 0x2a4 bytes (0x805f300 Load / 0x805f410 Save):
 *   +0x000  u32 Adler-32 (0x805e760) dos 0x2a0 bytes seguintes
 *   +0x004  84 x { u32 score, char nome[4] }
 *            0..19   ranking do ARCADE (0x805f800 posição, 0x805f830 insere)
 *            20..83  recorde de cada local do WORLD TOUR, índice = número + 0x13
 *                    (0x805f8c0; comparado em 0x8076c59)
 * Padrão (0x805f4d0): tudo zerado com nome "NEX " / "CADE" (alternado) e o top 20
 * de PUMP 20000 até KOOO 1000. Arquivo ausente ou inválido -> padrão e grava.
 * Aqui o arquivo fica em <pasta do jogo>/SETTINGS/RANK.DAT (o original usa a raiz).
 */
#include "pumpy.h"

#define RK_COUNT 84
#define RK_SIZE  0x2a4

static uint8_t s_rk[RK_SIZE];
static bool    s_rkLoaded;

static uint32_t rkAdler(const uint8_t* d, int n) {
    uint32_t a = 1, b = 0;
    for (int i = 0; i < n; i++) { a = (a + d[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint8_t* ent(int i) { return s_rk + 4 + i * 8; }

static void rkPath(char* out, size_t n) { snprintf(out, n, "%s/SETTINGS/RANK.DAT", g_game.currentDirectory); }

/* 0x805f8c0 */
static void rkSet(int i, uint32_t score, const char* name) {
    if (i < 0 || i >= RK_COUNT) return;
    wr32(ent(i), score);
    memcpy(ent(i) + 4, name, 4);
}

static void rkDefaults(void) {   /* 0x805f4d0 */
    memset(s_rk, 0, sizeof(s_rk));
    for (int i = 0; i < RK_COUNT; i++) rkSet(i, 0, (i & 1) ? "CADE" : "NEX ");
    static const struct { uint32_t s; const char* n; } k_top[20] = {
        { 20000, "PUMP" }, { 19000, "ITUP" }, { 18000, "-NX-" }, { 17000, "BAEH" }, { 16000, "YUTE" },
        { 15000, "XERO" }, { 14000, "YAHP" }, { 13000, "DOL-" }, { 12000, "DZWS" }, { 11000, "CHOI" },
        { 10000, "LEE-" }, { 9000, "PARK" }, { 8000, "HAN-" }, { 7000, "XYZ." }, { 6000, "SOUL" },
        { 5000, "WOO-" }, { 4000, "SHIN" }, { 3000, "BLDG" }, { 2000, "MEMO" }, { 1000, "KOOO" },
    };
    for (int i = 0; i < 20; i++) rkSet(i, k_top[i].s, k_top[i].n);
}

void Rank_Save(void) {   /* 0x805f410 */
    wr32(s_rk, rkAdler(s_rk + 4, RK_SIZE - 4));
    char path[MAX_PATH];
    rkPath(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) { Log_Print("CRankManager::Save(): Can't open Rank file for writing.\n"); return; }
    if (fwrite(s_rk, 1, RK_SIZE, f) != RK_SIZE) Log_Print("CRankManager::Save(): Write Fail.\n");
    fclose(f);
}

void Rank_Load(void) {   /* 0x805f300 */
    char path[MAX_PATH];
    rkPath(path, sizeof(path));
    s_rkLoaded = true;
    FILE* f = fopen(path, "rb");
    if (!f) {
        Log_Print("CRankManager::Load(): Rank file is not found.\n");
        rkDefaults(); Rank_Save(); return;
    }
    size_t got = fread(s_rk, 1, RK_SIZE, f);
    fclose(f);
    if (got != RK_SIZE) { Log_Print("CRankManager::Load(): Rank file is invalid.\n"); rkDefaults(); Rank_Save(); return; }
    if (rd32(s_rk) != rkAdler(s_rk + 4, RK_SIZE - 4)) { Log_Print("CRankManager::Load(): Rank checksum is invaild\n"); rkDefaults(); Rank_Save(); return; }
    Log_Print("CRankManager::Load(): Success.\n");
}

static void rkEnsure(void) { if (!s_rkLoaded) Rank_Load(); }

/* SETUP > BOOKKEEPING > RESET RANKING (CLEAR HIGHSCORE RANKING DATA ?, 0x8088200):
 * volta para a tabela padrão (0x805f4d0) e grava — HIPÓTESE: o reset usa o mesmo padrão */
void Rank_Reset(void) { rkDefaults(); s_rkLoaded = true; Rank_Save(); }

/* 0x805f800: primeira posição do top 20 com score < valor; -1 se não entra */
int Rank_Find(uint32_t score) {
    rkEnsure();
    for (int i = 0; i < 20; i++) if ((int)score > (int)rd32(ent(i))) return i;
    return -1;
}

/* 0x805f830: desloca e insere no top 20 */
void Rank_Insert(uint32_t score, const char* name) {
    int pos = Rank_Find(score);
    if (pos < 0) return;
    for (int i = 19; i > pos; i--) memcpy(ent(i), ent(i - 1), 8);
    rkSet(pos, score, name);
}

/* recorde do local do WORLD TOUR (número 1..64) */
uint32_t Rank_LocScore(int number) { rkEnsure(); int i = number + 0x13; return (i >= 20 && i < RK_COUNT) ? rd32(ent(i)) : 0; }
void Rank_SetLoc(int number, uint32_t score, const char* name) { rkEnsure(); rkSet(number + 0x13, score, name); }
const char* Rank_LocName(int number) {
    static char n[5];
    rkEnsure();
    int i = number + 0x13;
    if (i < 20 || i >= RK_COUNT) return "    ";
    memcpy(n, ent(i) + 4, 4); n[4] = 0;
    return n;
}
