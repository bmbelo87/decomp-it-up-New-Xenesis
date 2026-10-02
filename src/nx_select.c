/*
 * nx_select.c — tela de seleção de música do Pump It Up NX (CSelect, piu NX).
 *
 * Classe CSelect (RTTI "7CSelect", vtable 0x081105a8): Begin 0x8079710,
 * quadro 0x807aee0, End 0x807bd40. Detalhes em docs/NX_SELECT.md.
 *
 * Recursos (Begin):
 *   BGA/COMMON.DAT         [+0x32c]  setas, faixa/nome do canal, dificuldade, tempo
 *   BGA/LEVEL.DAT          [+0x330]  estrelas / hell do nível
 *   BGA/ARCADE_SPECIAL.DAT [+0x334]  carrossel (screen2 *), corações
 *   BGA/COMMAND.DAT        [+0x338]  ícones de modificador (ainda não usados aqui)
 *   BGA/BG.MOV                       fundo (0x8094450)
 *   BGA/TEST.DAT                     discos "%03X.TGA" (0x8062210)
 *   /SCRIPT/UI/SFX_SELECT.LUA        EFF_* (ver k_sfx)
 *
 * Diferenças conhecidas do original:
 *   - só o modo arcade ([0x81f8998] == 0): sem corações/bonus do modo especial;
 *   - ícones dos códigos: um por posição (o original guarda subícones);
 *   - o texto "artista- título- BPM" (0x807c760, FreeType NXTW.TTF em 0x8090fa0)
 *     sai com a fonte do projeto, posição aproximada;
 *   - prévia carregada no mesmo quadro (o original usa uma thread).
 */
#include "pumpy.h"
#include "bga.h"
#include "movie.h"
#include "testbga.h"

enum { NB_COMMON = 0, NB_LEVEL = 1, NB_ARCADE = 2, NB_COMMAND = 3 };

int  Texture_Wrap(int slot, unsigned glId, int w, int h);
void Texture_Unwrap(int slot);

/* ---------------------------------------------------------------------------
 * Sons (SFX_SELECT.LUA da NX + SFX_GLOBAL.LUA)
 * ------------------------------------------------------------------------- */
enum { SFX_HIDDEN, SFX_MOVE, SFX_MOVE_ACC, SFX_CHANNEL, SFX_MODE, SFX_SELECT, SFX_JOIN,
       SFX_WRONG, SFX_START, SFX_TIME_LIMIT, SFX_COUNT };
static const char* const k_sfx[SFX_COUNT] = {
    "2-1.WAV", "3-2.WAV", "ACC.WAV", "CHGMOD.WAV", "13-1.WAV", "3-2.WAV",
    "PUSHPANEL.WAV", "WRONG.WAV", "START.WAV", "TIME_LIMIT.WAV",
};
static int s_sfx[SFX_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static void sfx(int k) { if (s_sfx[k] >= 0) Audio_Play(s_sfx[k], false); }

/* ---------------------------------------------------------------------------
 * Estado (campos do objeto CSelect)
 * ------------------------------------------------------------------------- */
static int  s_list[EX_SONG_COUNT];   /* [this+8]: índices em g_exSongs */
static int  s_count;                 /* +0x20 */
static int  s_cursor;                /* +0x14 */
static int  s_lastId = -1;           /* +0x18, lembrado entre stages */
static int  s_prevCh = -1;           /* +0xc / +0x10 */
static int  s_diff[2];               /* +0x8c */
static int  s_diffSaved[2];          /* +0x84 */
static int  s_dir;                   /* +0xa4: 0 parado, 1 DR (L move), 2 DL (R move) */
static bool s_ready;                 /* +0x25 */
static bool s_previewOn;             /* +0x26 */
static unsigned s_joined;            /* [0x81f8904] bits 0/1 */
static int  s_time;                  /* +0x9c: 0x5a - segundos */
static uint32_t s_timeTick;          /* [+0x33c] */
static bool s_started;
static bool s_lockOn;              /* "lock click" disparado */
static int  s_lvNew[2], s_lvOld[2];  /* +0x36c / +0x374 */
static const char* s_lvScene[2];     /* +0x360 / +0x364 */
static const char* s_arroDL;         /* +0x30 */
static const char* s_arroDR;         /* +0x34 */
static int  s_acc;                   /* +0x1a0: aceleração do DL/DR */
static uint32_t s_holdT[2][2];
static int  s_discTex[EX_SONG_COUNT];/* registro +0x38 */
static int  s_movieTex = -1;         /* textura do vídeo da prévia (slot de textura) */
static int  s_prevTimeSnd = -1;

/* fontes de camada guardadas no Begin */
static BGALayerSrc s_chFrame[7];     /* +0x1ac: ARCADE 19,31,27,23,19,31,27 */
static BGALayerSrc s_chText[7];      /* +0x1dc: ARCADE 61,63,62,64,66,68,67 */
static BGALayerSrc s_chBar[7][2];    /* +0x20c: COMMON pares de 0x8110520 / 0x81104c0 */
static BGALayerSrc s_lock2, s_lock;  /* +0x324 (ARCADE 69), ARCADE 5 */
static BGALayerSrc s_movieSpr;       /* +0x1a8: ARCADE 99 (movie.spr) */
static BGALayerSrc s_lvSrc[7];       /* +0x2cc..+0x2e4: LEVEL 8,9,10,1,2,3,56 */
static BGALayerSrc s_diffText[5];    /* +0x2e8: COMMON 61,67,69,68,56 */
static BGALayerSrc s_digit[10];      /* +0x2fc: COMMON 82, 89..97 */
static bool s_srcOk;

/* cópias do tile de position.spr: um objeto por slot (o original troca obj+0x10) */
#define NPOS 20
static int s_posSlot[NPOS], s_posTile[NPOS], s_posCount;
static int s_posOrigTile = -1;      /* tile original do position.spr */

static bool twoPlayers(void) { return (s_joined & 3) == 3; }
static const ExceedSong* cur(void) { return s_count > 0 ? &g_exSongs[s_list[s_cursor]] : NULL; }
static int wrap(int i) { if (s_count <= 0) return 0; i %= s_count; return i < 0 ? i + s_count : i; }

/* ---------------------------------------------------------------------------
 * Códigos de modificador (0x804d240 + tabela 0x8143ca0, efeitos em 0x807c050)
 *   botões: 7 DL, 8 UL, 9 C, 10 UR, 11 DR (histórico de 25, compara o fim)
 *   estado por jogador (0x9e3dd20 + p*0x504): +0x494 flags, +0x498 velocidade
 *   ícones: COMMAND.DAT, 5 posições por jogador (slots p*5+k+1, cenas
 *   "%dp-%dcommand"); ícone = slot do COMMAND (11 2X .. 36 X)
 * ------------------------------------------------------------------------- */
enum { NC_DL = 7, NC_UL = 8, NC_C = 9, NC_UR = 10, NC_DR = 11 };
typedef struct { int n; uint8_t b[11]; } NxCode;
static const NxCode k_nxCodes[19] = {
    { 9, { 11,11,11,7,11,8,10,7,9 } },    /*  0 skin 2 (OR)   */
    { 9, { 8,10,7,9,7,11,11,10,10 } },    /*  1 skin 6 (CANO) */
    { 9, { 11,11,11,7,11,8,10,7,8 } },    /*  2 skin 7 (CARD) */
    { 9, { 11,11,11,7,11,8,10,7,10 } },   /*  3 skin 1 (HATO) */
    { 8, { 7,9,7,9,7,9,10,9 } },          /*  4 0x80 nos dois (UA) */
    { 10, { 7,8,9,11,10,7,8,9,11,10 } },  /*  5 0x2000 nos dois (NX) */
    { 11, { 8,10,9,7,11,11,7,9,10,8,9 } },/*  6 0x100 (RG) */
    { 9, { 8,10,8,10,8,10,8,10,9 } },     /*  7 RV (vel. 0x100) */
    { 9, { 8,10,8,10,7,11,7,11,9 } },     /*  8 0x20 (RS) */
    { 9, { 7,10,7,10,11,8,11,8,9 } },     /*  9 0x1000 nos dois (X) */
    { 9, { 11,7,10,8,11,10,7,8,9 } },     /* 10 EW (vel. 0x200) */
    { 9, { 8,7,10,11,11,8,10,7,9 } },     /* 11 0x08 (FD) */
    { 9, { 7,7,11,11,8,8,10,10,9 } },     /* 12 0x400 (AC) */
    { 9, { 11,11,7,7,10,10,8,8,9 } },     /* 13 0x200 (DC) */
    { 9, { 11,7,10,8,11,7,10,8,9 } },     /* 14 0x10 (M) */
    { 9, { 7,8,11,7,10,11,8,10,9 } },     /* 15 0x04 (FL) */
    { 5, { 8,10,8,10,9 } },               /* 16 UL UR UL UR C: x2 x3 x4 x8 */
    { 5, { 8,10,7,11,9 } },               /* 17 UL UR DL DR C: V -> NS -> desliga */
    { 6, { 7,11,7,11,7,11 } },            /* 18 DL DR DL DR DL DR: limpa */
};
#define NX_HIST 25
static uint8_t  s_hist[2][NX_HIST];
static int      s_histLen[2];
static unsigned s_nxFlags[2] = { 0, 0 };   /* +0x494 */
static unsigned s_nxSpeed[2] = { 4, 4 };   /* +0x498: 4 x1, 8 x2, 0xc x3, 0x10 x4, 0x20 x8, 0x100 RV, 0x200 EW */
static int      s_nxSkinIcon = -1;         /* posição 3 (skin, nos dois) */
static int      s_posIcon[2][5];           /* ícone mostrado em cada posição (-1 nenhum) */
static BGALayerSrc s_cmdIcon[37];          /* COMMAND slots 11..36 */
static bool     s_cmdOk;

static int pushCode(int p, int button) {   /* 0x804d200 + 0x804d240 */
    if (s_histLen[p] == NX_HIST) { memmove(s_hist[p], s_hist[p] + 1, NX_HIST - 1); s_histLen[p]--; }
    s_hist[p][s_histLen[p]++] = (uint8_t)button;
    for (int k = 0; k < 19; k++) {
        int n = k_nxCodes[k].n;
        if (s_histLen[p] >= n && memcmp(s_hist[p] + s_histLen[p] - n, k_nxCodes[k].b, (size_t)n) == 0) {
            s_histLen[p] = 0;
            return k;
        }
    }
    /* Extra do port (não existe no original): TestBGA, DL DL DL DL DR DR DR DR C,
     * mesmo código do Prex3 (song_select.c). Starfield no lugar do fundo. */
    static const uint8_t k_testBGA[9] = { NC_DL, NC_DL, NC_DL, NC_DL, NC_DR, NC_DR, NC_DR, NC_DR, NC_C };
    if (s_histLen[p] >= 9 && memcmp(s_hist[p] + s_histLen[p] - 9, k_testBGA, 9) == 0) {
        s_histLen[p] = 0;
        g_game.cmdTestBGA[p] = true;
        InitS();
        Log_Print("NXSELECT P%d: TestBGA ON\n", p + 1);
    }
    return -1;
}

/* Ícone de cada posição a partir do estado. O original guarda subícones por
 * posição (0x808e1d0, capacidades 2,2,1,3,2); aqui mostra o que estiver ligado,
 * por prioridade (hipótese de exibição). */
static void cmdIcons(void) {
    for (int p = 0; p < 2; p++) {
        unsigned f = s_nxFlags[p], v = s_nxSpeed[p];
        int ic[5] = { -1, -1, -1, -1, -1 };
        if (v == 8) ic[0] = 11; else if (v == 0xc) ic[0] = 12; else if (v == 0x10) ic[0] = 13;
        else if (v == 0x20) ic[0] = 14; else if (v == 0x100) ic[0] = 32; else if (v == 0x200) ic[0] = 19;
        else if (f & 0x200) ic[0] = 18; else if (f & 0x400) ic[0] = 15;
        if (f & 1) ic[1] = 35; else if (f & 2) ic[1] = 28; else if (f & 4) ic[1] = 21; else if (f & 8) ic[1] = 20;
        ic[2] = s_nxSkinIcon;
        if (f & 0x1000) ic[3] = 36; else if (f & 0x2000) ic[3] = 29; else if (f & 0x80) ic[3] = 34;
        if (f & 0x20) ic[4] = 31; else if (f & 0x10) ic[4] = 24; else if (f & 0x100) ic[4] = 22;
        for (int k = 0; k < 5; k++) {
            if (ic[k] == s_posIcon[p][k]) continue;
            s_posIcon[p][k] = ic[k];
            if (ic[k] < 0 || !s_cmdOk) continue;
            BGA_SetLayerSrc(NB_COMMAND, p * 5 + k + 1, &s_cmdIcon[ic[k]]);
            char sc[24];
            snprintf(sc, sizeof(sc), "%dp-%dcommand", p + 1, k + 1);
            BGA_SceneReset(NB_COMMAND, sc);   /* 0x8058b90: entra de novo */
        }
    }
}

static void globalFlag(unsigned bit) {   /* liga/desliga nos dois jogadores */
    if (s_nxFlags[0] & bit) { s_nxFlags[0] &= ~bit; s_nxFlags[1] &= ~bit; }
    else                    { s_nxFlags[0] |= bit;  s_nxFlags[1] |= bit; }
}

static void applyCode(int p, int code) {   /* 0x807c050 */
    unsigned* f = &s_nxFlags[p];
    unsigned* v = &s_nxSpeed[p];
    static const int k_skin[4] = { 2, 6, 7, 1 }, k_skinIcon[4] = { 30, 16, 17, 23 };
    switch (code) {
    case 0: case 1: case 2: case 3:   /* NOTESKIN = n (0x8050200) */
        Zero_SetSkinIndex(k_skin[code]);
        s_nxSkinIcon = k_skinIcon[code];
        break;
    case 4:  globalFlag(0x80); break;
    case 5:  globalFlag(0x2000); break;
    case 6:  *f ^= 0x100; break;
    case 7:  *v = (*v & 0x100) ? 4 : 0x100; break;
    case 8:  *f = (*f & 0x20) ? (*f & ~0x30u) : ((*f & ~0x30u) | 0x20); break;
    case 9:  globalFlag(0x1000); break;
    case 10: *v = (*v & 0x200) ? 4 : 0x200; break;
    case 11: *f ^= 8; break;
    case 12: *f = (*f & 0x400) ? (*f & ~0x600u) : ((*f & ~0x600u) | 0x400); break;
    case 13: *f = (*f & 0x200) ? (*f & ~0x600u) : ((*f & ~0x600u) | 0x200); break;
    case 14: *f = (*f & 0x10) ? (*f & ~0x30u) : ((*f & ~0x30u) | 0x10); break;
    case 15: *f = (*f & 4) ? (*f & ~7u) : ((*f & ~7u) | 4); break;
    case 16: /* 0x807c66b: >0x1f desliga, >0xf 8X, >0xb 4X, >7 3X, senão 2X */
        if ((*v & 0xff) > 0x1f) *v = 4;
        else if ((*v & 0xff) > 0xf) *v = 0x20;
        else if ((*v & 0xff) > 0xb) *v = 0x10;
        else if ((*v & 0xff) > 7) *v = 0xc;
        else *v = 8;
        break;
    case 17: /* 0x807c6f6 */
        if (*f & 2) *f &= ~7u;
        else if (*f & 1) *f = (*f & ~7u) | 2;
        else *f = (*f & ~7u) | 1;
        break;
    case 18: /* 0x807c08a: zera o jogador e os 0x80/0x1000/0x2000 dos dois.
              * O original volta o NOTESKIN para "0" (SKIN00); aqui volta para a
              * skin padrão do projeto (SKIN08, informado pelo usuário). */
        *f = 0; *v = 4;
        s_nxFlags[0] &= ~0x3080u; s_nxFlags[1] &= ~0x3080u;
        Zero_SetSkinIndex(8);
        s_nxSkinIcon = -1;
        break;
    default: return;
    }
    cmdIcons();
    Log_Print("NXSELECT: P%d código %d -> flags 0x%X vel 0x%X\n", p + 1, code, s_nxFlags[p], s_nxSpeed[p]);
}

/* passa os modificadores para o gameplay (só os que o projeto já implementa) */
static void cmdToGame(void) {
    for (int p = 0; p < 2; p++) {
        unsigned f = s_nxFlags[p], v = s_nxSpeed[p];
        g_game.cmdVanish[p]     = (f & 1) != 0;
        g_game.cmdNonStep[p]    = (f & 2) != 0;
        g_game.cmdMirror[p]     = (f & 0x10) != 0;
        g_game.cmdRandomStep[p] = (f & 0x20) != 0;
        g_game.cmdEarthworm[p]  = (v == 0x200);
        g_game.cmdFlash[p]       = (f & 0x04) != 0;
        g_game.cmdFreedom[p]     = (f & 0x08) != 0;
        g_game.cmdGradeRev[p]    = (f & 0x100) != 0;
        g_game.cmdDecel[p]       = (f & 0x200) != 0;
        g_game.cmdAccel[p]       = (f & 0x400) != 0;
        g_game.cmdUnderAttack[p] = (f & 0x80) != 0;
        g_game.cmdXMode[p]       = (f & 0x1000) != 0;
        g_game.cmdNXMode[p]      = (f & 0x2000) != 0;
    }
}

/* ---------------------------------------------------------------------------
 * Lista (0x8062400): canais 0..3 com algum nível (2P: só N/H/C contam)
 * ------------------------------------------------------------------------- */
static void buildList(void) {
    s_count = 0;
    for (int i = 0; i < EX_SONG_COUNT; i++) {
        const ExceedSong* e = &g_exSongs[i];
        if (e->channel & 4) continue;
        bool ok = e->level[0] >= 0 || e->level[1] >= 0 || e->level[2] >= 0;
        if (!ok && !twoPlayers()) ok = e->level[3] >= 0 || e->level[4] >= 0;
        if (ok) s_list[s_count++] = i;
    }
}

/* ---------------------------------------------------------------------------
 * Camadas do position.spr
 * ------------------------------------------------------------------------- */
static int posTile(int slot) {
    for (int i = 0; i < s_posCount; i++) if (s_posSlot[i] == slot) return s_posTile[i];
    return -1;
}

static void makePosCopy(int slot) {
    BGALayerSrc src;
    if (s_posCount >= NPOS || !BGA_GetLayerSrc(NB_ARCADE, slot, &src) || !src.isSPR || src.sprTileCount < 1) return;
    if (g_game.sprTileCount >= MAX_SPR_TILES) return;
    int t = g_game.sprTileCount++;
    if (s_posOrigTile < 0) s_posOrigTile = src.sprTileStart;
    g_game.sprTiles[t] = g_game.sprTiles[src.sprTileStart];
    src.sprTileStart = t;
    src.sprTileCount = 1;
    BGA_SetLayerSrc(NB_ARCADE, slot, &src);
    s_posSlot[s_posCount] = slot;
    s_posTile[s_posCount] = t;
    s_posCount++;
}

/* 0x807d760 (bloco repetido de 0x807d820): position P = disco do vizinho,
 * moldura P-1 = cor do canal, lock P+2 = lock2 se indisponível, senão lock.spr */
static void fillCard(int pos, int listIdx) {
    if (s_count <= 0) return;
    const ExceedSong* e = &g_exSongs[s_list[wrap(listIdx)]];
    int t = posTile(pos);
    int tex = s_discTex[s_list[wrap(listIdx)]];
    if (t >= 0 && tex >= 0) g_game.sprTiles[t].texId = tex;
    int ch = e->channel;
    if (ch < 0 || ch > 6) ch = 0;
    BGA_SetLayerSrc(NB_ARCADE, pos - 1, &s_chFrame[ch]);
    BGA_SetLayerSrc(NB_ARCADE, pos + 2, e->avail ? &s_lock : &s_lock2);
}

/* 0x807d820(this, direção) */
static void carousel(int dir) {
    int c = s_cursor;
    if (dir == 0) {
        fillCard(15, c); fillCard(7, c + 1); fillCard(11, c - 1);
    } else if (dir == 1) {   /* DR: cena "screen2 L move" */
        fillCard(56, c - 1); fillCard(40, c - 1); fillCard(52, c); fillCard(44, c + 1); fillCard(48, c - 2);
    } else {                 /* DL: cena "screen2 R move" */
        fillCard(36, c + 1); fillCard(20, c + 1); fillCard(28, c + 2); fillCard(24, c - 1); fillCard(32, c);
    }
}

/* ---------------------------------------------------------------------------
 * Canal (0x807d5a0)
 * ------------------------------------------------------------------------- */
static void channelUpdate(bool force) {
    const ExceedSong* e = cur();
    if (!e) return;
    int ch = e->channel;
    if (ch < 0 || ch > 6) ch = 0;
    if (ch == s_prevCh && !force) return;
    bool changed = (s_prevCh != -1 && ch != s_prevCh);
    s_prevCh = ch;
    BGA_SetLayerSrc(NB_COMMON, 1, &s_chBar[ch][0]);
    BGA_SetLayerSrc(NB_COMMON, 4, &s_chBar[ch][1]);
    BGA_SetLayerSrc(NB_COMMON, 5, &s_chBar[ch][1]);
    BGA_SetLayerSrc(NB_COMMON, 0x63, &s_chBar[ch][1]);
    BGA_SetLayerSrc(NB_COMMON, 0x31, &s_chText[ch]);
    BGA_SceneReset(NB_COMMON, "channel text start");
    if (changed) sfx(SFX_CHANNEL);
}

/* ---------------------------------------------------------------------------
 * Nível (0x807c820 / 0x807c970)
 * ------------------------------------------------------------------------- */
static void levelSlots(int level, int base) {
    if (level <= 14) {
        int full = level / 2, half = level % 2;
        for (int i = 0; i < 8; i++) {
            const BGALayerSrc* s = &s_lvSrc[2];
            if (i < full) s = &s_lvSrc[0];
            else if (half) { s = &s_lvSrc[1]; half = 0; }
            BGA_SetLayerSrc(NB_LEVEL, base + i, s);
        }
    } else if (level <= 23) {
        int lv = level >= 16 ? level - 1 : level;
        int k = lv - 14;
        for (int i = 0; i < 8; i++) {
            BGA_SetLayerSrc(NB_LEVEL, base + i, k > 0 ? &s_lvSrc[3] : &s_lvSrc[5]);
            if (k > 0) k--;
        }
    } else {
        for (int i = 0; i < 8; i++) BGA_SetLayerSrc(NB_LEVEL, base + i, &s_lvSrc[6]);
    }
}

static void levelUpdate(int p) {
    const ExceedSong* e = cur();
    if (!e) return;
    static const char* const k_pos[3]  = { "single level position", "1p level position", "2p level position" };
    static const char* const k_up[3]   = { "single star-hell", "1p star-hell", "2p star-hell" };
    static const char* const k_down[3] = { "single hell-star", "1p hell-star", "2p hell-star" };
    static const int k_hell[3] = { 0, 0x13, 0x25 }, k_star[3] = { 8, 0x1b, 0x2d };
    int m = twoPlayers() ? 1 + p : 0;
    int lv = e->level[s_diff[p]];
    if (lv < 0) lv = 0;
    s_lvOld[p] = s_lvNew[p];
    s_lvNew[p] = lv;
    int o = s_lvOld[p];
    if ((o <= 23 && lv > 23) || (o <= 14 && lv > 14)) {
        s_lvScene[p] = k_up[m];
        levelSlots(o, k_star[m]); levelSlots(lv, k_hell[m]);
    } else if ((o > 23 && lv <= 23) || (o > 14 && lv <= 14)) {
        s_lvScene[p] = k_down[m];
        levelSlots(o, k_hell[m]); levelSlots(lv, k_star[m]);
    } else {
        s_lvScene[p] = k_pos[m];
        levelSlots(lv, k_star[m]);
    }
    BGA_SceneReset(NB_LEVEL, s_lvScene[p]);
}

/* 0x807cce0: texto da dificuldade */
static void diffText(int p, bool sceneReset) {
    int d = s_diff[p];
    if (d < 0 || d > 4) d = 0;
    if (!twoPlayers()) {
        BGA_SetLayerSrc(NB_COMMON, 0x38, &s_diffText[d]);
        if (sceneReset) BGA_SceneReset(NB_COMMON, "single mode text start");
    } else {
        BGA_SetLayerSrc(NB_COMMON, p == 0 ? 0x3e : 0x3d, &s_diffText[d]);
        if (sceneReset) BGA_SceneReset(NB_COMMON, p == 0 ? "1p mode text start" : "2p mode text start");
    }
}

static bool diffOk(const ExceedSong* e, int d) {
    if (d < 0 || d > 4 || e->level[d] < 0 || !e->lock[d]) return false;
    return !twoPlayers() || d <= 2;
}

/* 0x807e7a0 (+1) / 0x807e710 (-1) */
static int stepDiff(int p, int dir) {
    const ExceedSong* e = cur();
    int n = twoPlayers() ? 3 : 5;
    int d0 = s_diff[p];
    for (int i = 1; i < n; i++) {
        int d = ((d0 + dir * i) % n + n) % n;
        if (diffOk(e, d)) return d;
    }
    return d0;
}

/* 0x807e5f0: depois de trocar de música */
static void fixDiff(void) {
    const ExceedSong* e = cur();
    if (!e) return;
    int np = twoPlayers() ? 2 : 1, n = twoPlayers() ? 3 : 5;
    for (int p = 0; p < np; p++) {
        if (diffOk(e, s_diffSaved[p])) s_diff[p] = s_diffSaved[p];
        else if (!diffOk(e, s_diff[p])) {
            s_diff[p] = 0;
            for (int d = 0; d < n; d++) if (diffOk(e, d)) { s_diff[p] = d; break; }
        }
        diffText(p, false);
    }
}

static void levelsAll(void) {
    levelUpdate(0);
    if (twoPlayers()) levelUpdate(1);
}

/* ---------------------------------------------------------------------------
 * Prévia (0x8079160 / 0x80791f1): vídeo dentro do card central
 * ------------------------------------------------------------------------- */
static bool nxFind(int id, const char* fmt, char* out, size_t sz) {
    for (int g = 0; id != -1 && g < 8; g++) {
        snprintf(out, sz, fmt, g_game.currentDirectory, (unsigned)id);
        FILE* f = fopen(out, "rb");
        if (f) { fclose(f); return true; }
        id = NX_ResId(id);
    }
    return false;
}

static void previewStop(void) {
    if (!s_previewOn) return;
    s_previewOn = false;
    Movie_Select(1); Movie_Close(); Movie_Select(0);
    BGM_Stop();
    if (s_movieTex >= 0) { Texture_Unwrap(s_movieTex); s_movieTex = -1; }
    /* card central volta ao position.spr (o disco é reposto pelo carrossel) */
    int t = posTile(15);
    BGALayerSrc src;
    if (t >= 0 && s_posOrigTile >= 0 && BGA_GetLayerSrc(NB_ARCADE, 15, &src)) {
        g_game.sprTiles[t] = g_game.sprTiles[s_posOrigTile];
        src.sprTileStart = t; src.sprTileCount = 1; src.isSPR = 1;
        BGA_SetLayerSrc(NB_ARCADE, 15, &src);
        if (cur() && s_discTex[s_list[s_cursor]] >= 0) g_game.sprTiles[t].texId = s_discTex[s_list[s_cursor]];
    }
}

static void previewStart(void) {
    const ExceedSong* e = cur();
    if (!e) return;
    char mov[MAX_PATH], aud[MAX_PATH];
    if (!nxFind((int)e->id, "%s/BGA/PREVIEW/%03X.MOV", mov, sizeof(mov)))
        snprintf(mov, sizeof(mov), "%s/BGA/PREVIEW/001.MOV", g_game.currentDirectory);
    Movie_Select(1);
    Movie_Open(mov, true);
    Movie_Select(0);
    if (nxFind((int)e->id, "%s/AUDIO/INTRO/%03X.AUD", aud, sizeof(aud)) && BGM_LoadAUDDirect(aud))
        BGM_Play(false);
    s_previewOn = true;
}

/* ---------------------------------------------------------------------------
 * Begin (0x8079710)
 * ------------------------------------------------------------------------- */
void NxSelect_Enter(void) {
    Title_StopMusic();
    Movie_Select(1); Movie_Close(); Movie_Select(0);
    Movie_Close();
    BGM_Stop();
    Resource_ClearBGA();
    static const char* const k_bga[4] = { "COMMON", "LEVEL", "ARCADE_SPECIAL", "COMMAND" };
    for (int i = 0; i < 4; i++)
        if (!Resource_LoadBGAByName(k_bga[i]))
            Log_Print("NXSELECT: falha ao carregar BGA\\%s.DAT\n", k_bga[i]);
    BGA_Reset();

    s_srcOk = true;
    static const int k_frame[7] = { 19, 31, 27, 23, 19, 31, 27 };
    static const int k_text[7]  = { 61, 63, 62, 64, 66, 68, 67 };
    static const int k_bar1P[7][2] = { {1,4}, {7,15}, {8,16}, {9,17}, {1,4}, {7,15}, {8,16} };
    static const int k_bar2P[7][2] = { {10,18}, {11,19}, {12,20}, {13,21}, {10,18}, {11,19}, {12,20} };
    s_joined = Title_GetJoinedMask() & 3;
    if (s_joined == 0) s_joined = 1;
    for (int c = 0; c < 7; c++) {
        s_srcOk &= BGA_GetLayerSrc(NB_ARCADE, k_frame[c], &s_chFrame[c]);
        s_srcOk &= BGA_GetLayerSrc(NB_ARCADE, k_text[c], &s_chText[c]);
        const int* b = twoPlayers() ? k_bar2P[c] : k_bar1P[c];
        s_srcOk &= BGA_GetLayerSrc(NB_COMMON, b[0], &s_chBar[c][0]);
        s_srcOk &= BGA_GetLayerSrc(NB_COMMON, b[1], &s_chBar[c][1]);
    }
    s_srcOk &= BGA_GetLayerSrc(NB_ARCADE, 69, &s_lock2);
    s_srcOk &= BGA_GetLayerSrc(NB_ARCADE, 5, &s_lock);
    s_srcOk &= BGA_GetLayerSrc(NB_ARCADE, 99, &s_movieSpr);
    static const int k_lv[7] = { 8, 9, 10, 1, 2, 3, 56 };
    for (int i = 0; i < 7; i++) s_srcOk &= BGA_GetLayerSrc(NB_LEVEL, k_lv[i], &s_lvSrc[i]);
    static const int k_dt[5] = { 61, 67, 69, 68, 56 };
    for (int i = 0; i < 5; i++) s_srcOk &= BGA_GetLayerSrc(NB_COMMON, k_dt[i], &s_diffText[i]);
    s_srcOk &= BGA_GetLayerSrc(NB_COMMON, 82, &s_digit[0]);
    for (int i = 1; i < 10; i++) s_srcOk &= BGA_GetLayerSrc(NB_COMMON, 88 + i, &s_digit[i]);
    if (!s_srcOk) Log_Print("NXSELECT: alguma camada de origem não existe\n");
    s_cmdOk = true;
    for (int i = 11; i <= 36; i++) s_cmdOk &= BGA_GetLayerSrc(NB_COMMAND, i, &s_cmdIcon[i]);
    if (!s_cmdOk) Log_Print("NXSELECT: ícones do COMMAND.DAT faltando\n");

    /* um objeto position.spr por slot */
    s_posCount = 0;
    s_posOrigTile = -1;
    static const int k_pos[] = { 15, 7, 11, 4, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56 };
    for (size_t i = 0; i < sizeof(k_pos) / sizeof(k_pos[0]); i++) makePosCopy(k_pos[i]);

    /* discos (0x8062210) */
    char path[MAX_PATH];
    for (int i = 0; i < EX_SONG_COUNT; i++) s_discTex[i] = -1;
    snprintf(path, sizeof(path), "%s/BGA/TEST.DAT", g_game.currentDirectory);
    if (RES_Open(path)) {
        for (int i = 0; i < EX_SONG_COUNT; i++) {
            if (g_exSongs[i].channel & 4) continue;
            char name[16];
            for (int id = (int)g_exSongs[i].id, g = 0; id != -1 && g < 8 && s_discTex[i] < 0; g++, id = NX_ResId(id)) {
                snprintf(name, sizeof(name), "%03X.TGA", (unsigned)id);
                s_discTex[i] = loadTextureFromRES(name);
            }
        }
        RES_Close();
    }

    for (int k = 0; k < SFX_COUNT; k++)
        if (s_sfx[k] < 0) s_sfx[k] = Audio_LoadWaveFile(k_sfx[k]);

    buildList();
    s_cursor = 0;
    for (int i = 0; i < s_count; i++)
        if ((int)g_exSongs[s_list[i]].id == s_lastId) s_cursor = i;
    bool newGame = (g_game.stageCount == 3 && !g_game.isBonusSong);
    if (newGame) {
        s_diff[0] = s_diff[1] = 0;
        s_nxFlags[0] = s_nxFlags[1] = 0;
        s_nxSpeed[0] = s_nxSpeed[1] = 4;
    }
    s_histLen[0] = s_histLen[1] = 0;
    for (int p = 0; p < 2; p++) for (int k = 0; k < 5; k++) s_posIcon[p][k] = -1;
    cmdIcons();
    s_diffSaved[0] = s_diff[0];
    s_diffSaved[1] = s_diff[1];

    snprintf(path, sizeof(path), "%s/BGA/BG.MOV", g_game.currentDirectory);
    Movie_Open(path, true);

    static const char* const k_reset[][2] = {
        { "", "channel start" }, { "", "arro start" }, { "", "channel text start" },
        { "", "single mode text start" }, { "", "1p mode text start" }, { "", "2p mode text start" },
        { "", "time position" },
    };
    for (size_t i = 0; i < sizeof(k_reset) / sizeof(k_reset[0]); i++) BGA_SceneReset(NB_COMMON, k_reset[i][1]);
    BGA_SceneReset(NB_ARCADE, "screen2 start");
    BGA_SceneReset(NB_ARCADE, "screen2 hold");

    s_dir = 0;
    s_ready = false;
    s_previewOn = false;
    s_started = false;
    s_lockOn = false;
    s_prevCh = -1;
    s_acc = 0x1f4;
    s_arroDL = "arroUL click";
    s_arroDR = "arroUR click";
    s_lvNew[0] = s_lvNew[1] = 0;
    s_time = 0x5a;
    s_prevTimeSnd = -1;
    s_timeTick = timeGetTime();
    fixDiff();
    carousel(0);
    channelUpdate(true);
    levelsAll();
    Log_Print("NXSELECT: %d músicas, cursor %d (%03X), P%s\n", s_count, s_cursor,
              cur() ? (unsigned)cur()->id : 0u, twoPlayers() ? "1+P2" : (s_joined & 2) ? "2" : "1");
}

/* ---------------------------------------------------------------------------
 * Entrada (0x807cdb0)
 * ------------------------------------------------------------------------- */
static void moveTo(int dir) {   /* 0x807cf80 (DR, dir 1) / 0x807d140 (DL, dir 2) */
    s_ready = false;
    previewStop();
    s_dir = dir;
    BGA_SceneReset(NB_ARCADE, dir == 1 ? "screen2 L move" : "screen2 R move");
    BGA_SceneReset(NB_COMMON, dir == 1 ? "arroUR click" : "arroUL click");
    s_cursor = wrap(s_cursor + (dir == 1 ? 1 : -1));
    s_lastId = cur() ? (int)cur()->id : -1;
    carousel(dir);
    sfx(s_acc > 0x289 ? SFX_MOVE_ACC : SFX_MOVE);
}

static bool repeatHit(int p, int k, PadButton b) {
    uint32_t now = timeGetTime();
    if (Input_IsPadHit(p, b)) { s_holdT[p][k] = now; return true; }
    if (!Input_IsPadDown(p, b) || now - s_holdT[p][k] <= 0x320) return false;
    /* segurando: próxima repetição em 800 - aceleração; acelera 0x1e por vez */
    if (s_acc <= 0x2cf) s_acc += 0x1e;
    s_holdT[p][k] = now - (uint32_t)s_acc;
    return true;
}

static void startGame(void) {
    if (s_started) return;
    const ExceedSong* e = cur();
    if (!e) return;
    s_started = true;
    sfx(SFX_START);
    previewStop();
    Movie_Close();
    BGM_Stop();
    static const char* const k_arg[5] = { "-n", "-h", "-c", "-d", "-m" };
    Log_Print("NXSELECT: RUN %X %s %s\n", (unsigned)e->id, k_arg[s_diff[0]],
              twoPlayers() ? k_arg[s_diff[1]] : "");
    int speed[2] = { 1, 1 };
    bool rv[2] = { false, false };
    for (int p = 0; p < 2; p++) {
        unsigned v = s_nxSpeed[p];
        speed[p] = v == 8 ? 2 : v == 0xc ? 3 : v == 0x10 ? 4 : v == 0x20 ? 8 : 1;
        rv[p] = (v == 0x100);
    }
    if (!ExSelect_StartZero((int)e->id, s_diff[0], s_joined, speed, rv))
        s_started = false;
    else
        cmdToGame();
}

static void playerInput(int p) {
    bool joined = (s_joined & (1u << p)) != 0;
    if (joined) {   /* cada painel vai para o histórico de códigos (0x804d200) */
        static const PadButton k_pad[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
        for (int k = 0; k < 5; k++)
            if (Input_IsPadHit(p, k_pad[k])) {
                int code = pushCode(p, NC_DL + k);
                if (code >= 0) { s_ready = false; sfx(SFX_HIDDEN); applyCode(p, code); }
            }
    }
    if (Input_IsPadHit(p, PAD_C)) {
        if (!joined) {   /* 0x807e420 */
            if (Coin_HasCredit()) { Coin_ConsumeCredit(); s_joined |= 1u << p; sfx(SFX_JOIN); buildList(); fixDiff(); carousel(0); levelsAll(); }
            return;
        }
        const ExceedSong* e = cur();
        if (s_ready) { s_time = 0; sfx(SFX_SELECT); }
        else if (e && e->avail) {
            s_ready = true;
            if (s_dir != 0) { s_dir = 0; fixDiff(); carousel(0); channelUpdate(false); levelsAll(); }
            BGA_SceneReset(NB_COMMON, "center step");
            BGA_SceneReset(NB_ARCADE, "screen2 click");
            sfx(SFX_SELECT);
        } else {
            sfx(SFX_WRONG);
            BGA_SceneReset(NB_ARCADE, "lock click");
            s_lockOn = true;
        }
    }
    if (!joined) return;
    if (repeatHit(p, 0, PAD_DR)) { s_arroDR = "arroUR click hold"; moveTo(1); }
    if (repeatHit(p, 1, PAD_DL)) { s_arroDL = "arroUL click hold"; moveTo(2); }
    if (Input_IsPadHit(p, PAD_UR) || Input_IsPadHit(p, PAD_UL)) {
        bool up = Input_IsPadHit(p, PAD_UR);
        int q = twoPlayers() ? p : 0;
        s_ready = false;
        BGA_SceneReset(NB_COMMON, up ? "arroHR click" : "arroHL click");
        s_diff[q] = stepDiff(q, up ? 1 : -1);
        s_diffSaved[q] = s_diff[q];
        diffText(q, true);
        levelUpdate(q);
        sfx(SFX_MODE);
    }
}

/* ---------------------------------------------------------------------------
 * Quadro (0x807aee0)
 * ------------------------------------------------------------------------- */
void NxSelect_Update(float dt) {
    if (s_started) return;
    int el = (int)((timeGetTime() - s_timeTick) / 1000);
    if (s_time > 0) s_time = 0x5a - el;
    if (s_time < 0) s_time = 0;
    if (s_time > 0 && s_time < 11 && s_time != s_prevTimeSnd) { s_prevTimeSnd = s_time; sfx(SFX_TIME_LIMIT); }

    Movie_Update(dt);
    if (s_previewOn) {
        Movie_Select(1);
        Movie_Update(dt);
        unsigned gl = Movie_GLTexture();
        Movie_Select(0);
        if (gl) {   /* 0x807bb00: card central passa a mostrar o vídeo */
            s_movieTex = Texture_Wrap(s_movieTex, gl, 256, 192);
            BGALayerSrc mv = s_movieSpr;
            int t = posTile(15);
            if (t >= 0 && s_movieSpr.sprTileCount > 0 && s_movieTex >= 0) {
                g_game.sprTiles[t] = g_game.sprTiles[s_movieSpr.sprTileStart];
                g_game.sprTiles[t].texId = s_movieTex;
                /* movie.spr foi feito para a movie.tga 256x256, com o vídeo 256x192 nas
                 * linhas de cima; nossa textura do vídeo tem só 256x192 -> reescala V */
                g_game.sprTiles[t].v1 *= 256.0f / 192.0f;
                g_game.sprTiles[t].v2 *= 256.0f / 192.0f;
                mv.sprTileStart = t;
                BGA_SetLayerSrc(NB_ARCADE, 15, &mv);
            }
        }
    }

    for (int p = 0; p < 2; p++) {
        if (!Input_IsPadDown(p, PAD_DL)) s_arroDL = "arroUL click";
        if (!Input_IsPadDown(p, PAD_DR)) s_arroDR = "arroUR click";
    }
    bool held = false;
    for (int p = 0; p < 2; p++)
        if ((s_joined & (1u << p)) && (Input_IsPadDown(p, PAD_DL) || Input_IsPadDown(p, PAD_DR))) held = true;
    if (!held) s_acc = 0x1f4;

    playerInput(0);
    playerInput(1);

    /* fim do movimento: volta ao carrossel parado (0x807bc38) */
    if (s_dir != 0 && BGA_SceneDone(NB_ARCADE, s_dir == 1 ? "screen2 L move" : "screen2 R move") && !held) {
        s_dir = 0;
        fixDiff();
        carousel(0);
        channelUpdate(false);
        levelsAll();
    }
    /* parado e disponível: pede a prévia (0x807bbc1) */
    if (s_dir == 0 && !held && !s_previewOn && cur() && cur()->avail && BGA_SceneDone(NB_ARCADE, "screen2 start"))
        previewStart();

    if (s_time <= 0) startGame();
}

void NxSelect_Render(void) {
    if (g_game.bgaPicCount < 4) return;
    Movie_Render();   /* BGA/BG.MOV */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* setas */
    BGA_ScenePlay(NB_COMMON, "arroHL click", true);
    BGA_ScenePlay(NB_COMMON, "arroHR click", true);
    BGA_ScenePlay(NB_COMMON, s_arroDL, true);
    BGA_ScenePlay(NB_COMMON, s_arroDR, true);

    /* canal */
    if (!BGA_SceneDone(NB_COMMON, "channel start")) BGA_ScenePlay(NB_COMMON, "channel start", true);
    else BGA_ScenePlay(NB_COMMON, "channel hold", true);
    BGA_ScenePlay(NB_COMMON, "channel text start", true);

    /* tempo (0x807e8e0) */
    BGA_SetLayerSrc(NB_COMMON, 0x58, &s_digit[s_time % 10]);
    BGA_SetLayerSrc(NB_COMMON, 0x57, &s_digit[(s_time / 10) % 10]);
    BGA_ScenePlay(NB_COMMON, "time position", true);

    /* carrossel */
    const char* sc;
    if (s_dir == 1) sc = "screen2 L move";
    else if (s_dir == 2) sc = "screen2 R move";
    else if (!BGA_SceneDone(NB_ARCADE, "screen2 start")) sc = "screen2 start";
    else sc = s_ready ? "screen2 click" : "screen2 hold";
    BGA_ScenePlay(NB_ARCADE, sc, true);
    /* só depois de um CENTER em música bloqueada (a cena nunca resetada começa em cur=0) */
    if (s_lockOn) {
        if (BGA_SceneDone(NB_ARCADE, "lock click")) s_lockOn = false;
        else BGA_ScenePlay(NB_ARCADE, "lock click", true);
    }

    /* ícones dos códigos (COMMAND.DAT) */
    if (s_cmdOk)
        for (int p = 0; p < 2; p++) {
            if (!(s_joined & (1u << p))) continue;
            for (int k = 0; k < 5; k++) {
                if (s_posIcon[p][k] < 0) continue;
                char sc[24];
                snprintf(sc, sizeof(sc), "%dp-%dcommand", p + 1, k + 1);
                BGA_ScenePlay(NB_COMMAND, sc, true);
            }
        }

    /* nível e dificuldade */
    if (s_dir == 0) {
        int np = twoPlayers() ? 2 : 1;
        for (int p = 0; p < np; p++) {
            /* 0x807b6f2..0x807b7db: nível > 14 -> "hell effect hold" ANTES da cena do
             * nível; só fica de fora enquanto a transição estrela->caveira
             * ("star-hell") não terminou. Sem cor/alpha extra: o pisca e o aditivo
             * vêm dos keyframes (levelef1/2.spr, blend 1).
             * era: desenhado depois da cena do nível e só com ela terminada. */
            const char* up = twoPlayers() ? (p ? "2p star-hell" : "1p star-hell") : "single star-hell";
            if (s_lvNew[p] > 14 && !(s_lvOld[p] <= 14 && s_lvOld[p] != s_lvNew[p] && !BGA_SceneDone(NB_LEVEL, up)))
                BGA_ScenePlay(NB_LEVEL, twoPlayers() ? (p ? "2p hell effect hold" : "1p hell effect hold")
                                                     : "single hell effect hold", true);
            if (s_lvScene[p]) BGA_ScenePlay(NB_LEVEL, s_lvScene[p], true);
        }
        if (twoPlayers()) {
            BGA_ScenePlay(NB_COMMON, "1p mode text start", true);
            BGA_ScenePlay(NB_COMMON, "2p mode text start", true);
        } else {
            BGA_ScenePlay(NB_COMMON, "single mode text start", true);
        }
        /* 0x807c760: "artista- título- BPM:bpm" */
        const ExceedSong* e = cur();
        if (e) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s- %s- BPM:%s", e->artistEn, e->titleEn, e->bpmText ? e->bpmText : "");
            Font_DrawStringCentered(320, 405, buf, 1, 1, 1, 1);
        }
    }
    if (s_ready) BGA_ScenePlay(NB_COMMON, "center step", true);
    glColor4f(1, 1, 1, 1);
}
