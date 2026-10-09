/*
 * nx_world.c — WORLD TOUR do Pump It Up NX (CSelectWorld, piu NX).
 *
 * Classe CSelectWorld (RTTI "12CSelectWorld", vtable 0x8113f88):
 *   Begin 0x80816d0, intro 0x8082390, quadro 0x80827f0, End 0x8083180, saída 0x8083170.
 *   Entra pela Station com o modo [0x81f8998] = 2 (g_game.nxGameMode).
 *
 * Recursos (Begin):
 *   [+4]  BGA/ARCADE_SPECIAL.DAT  carrossel (screen2 *), molduras por região, cadeado
 *   [+8]  BGA/COMMON.DAT          faixa/nome da região, nome do local (slot 0x38), tempo
 *   [+c]  BGA/WORLDTOUR01.DAT     mapa: "world tour start", "<região> hold", transições,
 *                                 rótulo do cartão (slot 0x23 + local)
 *   [+10] BGA/WORLDTOUR02.DAT     nome do local (slot = local)
 *   [+14] BGA/ARRO.DAT            setas
 *   BGA/WT.MOV                    fundo, somado à cor do mapa da região (unidade 1 do GL)
 *   BGA/WF.DAT                    discos f-a%02d / f-n / f-s / f-e .tga (0x8084be0)
 *
 * Tabelas (nx_world_data.c): 64 locais {nome, região, número} e 190 missões.
 * O local escolhido vale para o crédito todo: o stage n joga "RUN AA%02d%d"
 * (número, n) — nos stages 2 e 3 o Begin já começa a missão (0x8081776).
 *
 * Fase 1 (não feito ainda): itens de missão, condição/CWorldGrade, recordes
 * (MRANK.DAT: desbloqueio por recorde e cena "hi-score"), modificadores da missão.
 */
#include "pumpy.h"
#include "bga.h"
#include "movie.h"
#include <ctype.h>

enum { NW_AS = 0, NW_CM = 1, NW_W1 = 2, NW_W2 = 3, NW_AR = 4 };

/* SFX_SELECT.LUA / SFX_GLOBAL.LUA (mesmos arquivos da Select) */
enum { WS_MOVE, WS_MOVE_ACC, WS_CHANNEL, WS_SELECT, WS_WRONG, WS_START, WS_COUNT };
static const char* const k_wsnd[WS_COUNT] = {
    "3-2.WAV", "ACC.WAV", "CHGMOD.WAV", "3-2.WAV", "WRONG.WAV", "START.WAV",
};
static int s_snd[WS_COUNT] = { -1, -1, -1, -1, -1, -1 };
static void snd(int k) { if (s_snd[k] >= 0) Audio_Play(s_snd[k], false); }

/* ---- estado (campos do objeto) ---- */
static int  s_cur;                 /* +0x57c: local (0..63), lembrado no crédito */
static int  s_region, s_regionPrev;/* +0x458 / +0x45c */
static int  s_regionOld;           /* +0x460: região antes da troca */
static int  s_move;                /* +0x464: 0 parado, 1 L move, 2 R move, 3 início */
static bool s_confirmed;           /* +0x450 */
static bool s_trans;               /* +0x451: transição do mapa em andamento */
static bool s_regionChanged;       /* +0x452 */
static int  s_acc;                 /* +0x454 */
static int  s_time;                /* +0x46c */
static uint32_t s_t0;              /* +0x468 */
static uint32_t s_transT;          /* +0x578 */
static bool s_intro;               /* fase 0x8082390 */
static bool s_started;
static unsigned s_joined;
static uint8_t s_open[NX_WORLD_STAGES];
static int  s_tex[NX_WORLD_STAGES];/* +0x474 */
static const char* s_transScene;   /* +0x438 */
static const char* s_screen;       /* +0x44c */
static const char* s_arro[4];      /* +0x43c DL, +0x440 DR, +0x444 UL, +0x448 UR */
static uint32_t s_holdT[2][4];
static bool s_autoStart;           /* stages 2 e 3: missão começa no 1º quadro */
static const char* s_cond = "1";  /* condição da missão atual */
NxMods g_nxMods;

/* 0x806b6ea: 2º campo da condição. Dígitos somam a velocidade (4 = x1); letras de
 * "vnwfmrujdax^!" ligam flags (0x810f520) e põem ícone do COMMAND.DAT (0x810f560);
 * "k" + um de "xgo12mcpnr" escolhe a skin ([0x81f8990]; r = sorteio) com ícone
 * 0x810f4c0; "s" -> 0x100 na velocidade (RV, ícone 0x20); "e" -> 0x200 (EW, ícone 0x13).
 * Depois o ícone da velocidade pela soma (0x806b8b8): 2 -> 0x26, >7 -> 0xb, >0xb -> 0xc,
 * >0xf -> 0xd, >0x13 -> 0x27, >0x17 -> 0x28, >0x1f -> 0xe. */
static void parseMods(const char* m)
{
    static const char k_let[] = "vnwfmrujdax^!";
    static const unsigned k_flag[13] = { 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x80, 0x100, 0x200, 0x400, 0x1000, 0x2000, 0x4000 };
    static const int k_icon[13] = { 35, 28, 21, 20, 24, 31, 34, 22, 18, 15, 36, 29, -1 };
    static const char k_skin[] = "xgo12mcpnr";
    static const int k_skinIcon[10] = { -1, 23, 30, 51, 51, 48, 16, 17, -1, -1 };
    NxMods* x = &g_nxMods;
    memset(x, 0, sizeof(*x));
    x->skin = -1;
    #define ADDICON(i) do { if ((i) >= 0 && x->nIcons < 4) x->icons[x->nIcons++] = (i); } while (0)
    int sum = 0;
    for (const char* c = m; *c && *c != ' '; c++) {
        char ch = (char)tolower((unsigned char)*c);
        if (ch >= '0' && ch <= '9') { sum += ch - '0'; continue; }
        if (ch == 'k' && c[1]) {
            char k = (char)tolower((unsigned char)*++c);
            const char* f = strchr(k_skin, k);
            if (f && *f) {
                int i = (int)(f - k_skin);
                if (i == 9) i = rand() % 9;   /* "r": sorteio (0x806b898) */
                x->skin = i;
                ADDICON(k_skinIcon[i]);
            }
            continue;
        }
        if (ch == 's') { x->flags |= 0x10000; ADDICON(0x20); continue; }
        if (ch == 'e') { x->flags |= 0x20000; ADDICON(0x13); continue; }
        const char* f = strchr(k_let, ch);
        if (f && *f) { int i = (int)(f - k_let); x->flags |= k_flag[i]; ADDICON(k_icon[i]); }
    }
    x->units = sum;
    if (sum > 0x1f) ADDICON(0xe);
    else if (sum > 0x17) ADDICON(0x28);
    else if (sum > 0x13) ADDICON(0x27);
    else if (sum > 0xf) ADDICON(0xd);
    else if (sum > 0xb) ADDICON(0xc);
    else if (sum > 7) ADDICON(0xb);
    else if (sum == 2) ADDICON(0x26);
    #undef ADDICON
}
static int  s_lastCost;            /* corações da última missão (para o CONTINUE) */
static float s_tint[4][3];         /* cor do centro do mapa de cada região */

static BGALayerSrc s_frame[4];     /* +0x3c..+0x48: ARCADE 0x13/0x1b/0x1f/0x17 */
static BGALayerSrc s_regText[4];   /* +0x6c..+0x78: ARCADE 0x46..0x49 */
static BGALayerSrc s_regBar[4][2]; /* +0x7c: COMMON pares de 0x8113fc0 (canais 8..11) */
static BGALayerSrc s_lock;         /* +0x354: ARCADE 0x45 */
static BGALayerSrc s_label[NX_WORLD_STAGES];   /* +0x10c: WORLDTOUR01 0x23 + i */
static BGALayerSrc s_name[NX_WORLD_STAGES];    /* +0x20c: WORLDTOUR02 i */
static BGALayerSrc s_digit[10];    /* +0x32c: COMMON 0x52, 0x59..0x61 */
static bool s_ok;

#define NPOS 20
static int s_posSlot[NPOS], s_posTile[NPOS], s_posCount;

static const char* const k_hold[4] = { "asia hold", "north america hold", "south america hold", "europe hold" };

static int ri(int region) { int r = region - 8; return r < 0 ? 0 : r > 3 ? 3 : r; }
static int wrap(int i) { i %= NX_WORLD_STAGES; return i < 0 ? i + NX_WORLD_STAGES : i; }
static int regionOf(int i) { return g_nxWorldStages[wrap(i)].region; }
/* 0x8084d90: aberto de fábrica/pelo contador, ou com recorde no RANK.DAT (0x81db1fc + número * 8) */
static bool selectable(int i) { i = wrap(i); return s_open[i] != 0 || Rank_LocScore(g_nxWorldStages[i].number) != 0; }

/* Recorde do local (objeto +0x35c, 0x8084f10 / 0x8084f60 / 0x8084fe0) */
void NxText_DrawCentered(const char* str, int x, int y);
static bool s_hs;                  /* +0x370 */
static int  s_wfTex[2] = { -1, -1 };
static const int8_t k_wfLin[64] = { 0,0,0,0,0,0,0,1,1,1,1,1,1,1,2,2,2,2,2,2,2,3,3,3,3,3,3,3,4,4,4,4,4,4,4,5,5,5,5,5,5,5,6,6,6,6,6,6,6,0,0,0,0,0,0,0,1,1,1,0,10,0,0,0 };
static const int8_t k_wfCol[64] = { 0,1,2,3,4,5,6,0,1,2,3,4,5,6,0,1,2,3,4,5,6,0,1,2,3,4,5,6,0,1,2,3,4,5,6,0,1,2,3,4,5,6,0,1,2,3,4,5,6,0,1,2,3,4,5,6,0,1,2,0,10,0,0,0 };
static const char k_wfChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789~!@#$%^&*()_-+=\\:;/?";

/* 0x8064a20: WFONT, células 71x68 (linha 0x8140300, coluna 0x8140340), textura = índice / 49,
 * avanço 56.8 a partir de x - 113.6 */
static void wfontPrint(float x, float y, const char* s)
{
    x -= 113.6f;
    for (; *s; s++, x += 56.8f) {
        const char* f = strchr(k_wfChars, *s);
        if (!f || !*s) continue;
        int v = (int)(f - k_wfChars);
        int t = s_wfTex[v / 49 > 1 ? 1 : v / 49];
        if (t < 0) continue;
        if (!g_game.textures[t].inUse) continue;
        glEnable(GL_TEXTURE_2D);   /* era: só Texture_Bind (o estado podia estar sem textura) */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, 1);
        glBindTexture(GL_TEXTURE_2D, g_game.textures[t].id);
        float th = (float)Texture_GetHeight(t);
        if (th <= 0.0f) th = 512.0f;
        float u0 = k_wfCol[v] * 71.0f / 512.0f, v0 = k_wfLin[v] * 68.0f / th;
        float u1 = u0 + 71.0f / 512.0f, v1 = v0 + 68.0f / th;
        float yt = 480.0f - y;
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(x, yt);
        glTexCoord2f(u1, v0); glVertex2f(x + 71.0f, yt);
        glTexCoord2f(u1, v1); glVertex2f(x + 71.0f, yt - 68.0f);
        glTexCoord2f(u0, v1); glVertex2f(x, yt - 68.0f);
        glEnd();
    }
}

static BGALayerSrc s_hsLabel;      /* ARCADE slot 5 (0x808311d) */

/* ---------------------------------------------------------------------------
 * Carrossel (0x8083ce0)
 * ------------------------------------------------------------------------- */
static int posTile(int slot) {
    for (int i = 0; i < s_posCount; i++) if (s_posSlot[i] == slot) return s_posTile[i];
    return -1;
}

static void makePosCopy(int slot) {
    BGALayerSrc src;
    if (s_posCount >= NPOS || !BGA_GetLayerSrc(NW_AS, slot, &src) || !src.isSPR || src.sprTileCount < 1) return;
    if (g_game.sprTileCount >= MAX_SPR_TILES) return;
    int t = g_game.sprTileCount++;
    g_game.sprTiles[t] = g_game.sprTiles[src.sprTileStart];
    src.sprTileStart = t;
    src.sprTileCount = 1;
    BGA_SetLayerSrc(NW_AS, slot, &src);
    s_posSlot[s_posCount] = slot;
    s_posTile[s_posCount] = t;
    s_posCount++;
}

/* P = disco do local, P+2 = rótulo (ou cadeado), P-1 = moldura da região */
static void fillCard(int pos, int idx) {
    idx = wrap(idx);
    int t = posTile(pos);
    if (t >= 0 && s_tex[idx] != -1) g_game.sprTiles[t].texId = s_tex[idx];
    BGA_SetLayerSrc(NW_AS, pos + 2, &s_label[idx]);
    BGA_SetLayerSrc(NW_AS, pos - 1, &s_frame[ri(regionOf(idx))]);
    if (!selectable(idx)) BGA_SetLayerSrc(NW_AS, pos + 2, &s_lock);
}

static void carousel(int dir) {
    int c = s_cur;
    if (!s_ok) return;
    if (dir == 0) {
        fillCard(15, c); fillCard(7, c + 1); fillCard(11, c - 1);
    } else if (dir == 1) {
        fillCard(56, c - 1); fillCard(40, c - 1); fillCard(52, c); fillCard(44, c + 1); fillCard(48, c - 2);
    } else if (dir == 2) {
        fillCard(36, c + 1); fillCard(20, c + 1); fillCard(28, c + 2); fillCard(24, c - 1); fillCard(32, c);
    }
}

/* 0x8084740: faixa e nome da região (canais 8..11) */
static void regionUpdate(void) {
    s_regionPrev = s_region;
    s_region = regionOf(s_cur);
    if (s_region == s_regionPrev || !s_ok) return;
    int r = ri(s_region);
    BGA_SetLayerSrc(NW_CM, 1, &s_regBar[r][0]);
    BGA_SetLayerSrc(NW_CM, 4, &s_regBar[r][1]);
    BGA_SetLayerSrc(NW_CM, 5, &s_regBar[r][1]);
    BGA_SetLayerSrc(NW_CM, 0x63, &s_regBar[r][1]);
    BGA_SetLayerSrc(NW_CM, 0x31, &s_regText[r]);
    BGA_SceneReset(NW_CM, "channel text start");
    if (s_regionPrev != 0) snd(WS_CHANNEL);
}

/* 0x8083aa0: cena de transição do mapa entre a região antiga e a nova */
static void startTransition(void) {
    static const char* const k_tr[4][4] = {
        /* para:  A                     N                              S                              E */
        { NULL,                  "asia_north america",          NULL,                          "asia_europe" },          /* de A */
        { "north america_asia",  NULL,                          "north america_south america", NULL },                   /* de N */
        { NULL,                  "south america_north america", NULL,                          "sout america_europe" },  /* de S */
        { "europe_asia",         NULL,                          "eurpe_south america",         NULL },                   /* de E */
    };
    s_transScene = k_tr[ri(s_regionOld)][ri(regionOf(s_cur))];
    BGA_SceneReset(NW_AS, "screen2 start");
    BGA_SceneReset(NW_AS, "screen2 end");
    if (s_transScene) BGA_SceneReset(NW_W1, s_transScene);
    s_transT = timeGetTime();
}

/* ---------------------------------------------------------------------------
 * Início da missão (0x8084b10)
 * ------------------------------------------------------------------------- */
static void startMission(void) {
    if (s_started) return;
    s_started = true;
    snd(WS_START);
    int stage = 3 - g_game.stageCount;          /* [0x81f8910] */
    if (stage < 0) stage = 0;
    int num = g_nxWorldStages[s_cur].number;
    int id = 0xAA000 + ((num / 10) << 8) + ((num % 10) << 4) + (stage + 1);   /* "AA%02d%d" */
    /* 0x8084b8b: números 0x19 e 0x2b custam 3 corações, os outros 2 */
    s_lastCost = (num == 0x19 || num == 0x2b) ? 3 : 2;
    g_game.nxHearts -= s_lastCost;
    const NxWorldMission* m = NULL;
    for (int i = 0; i < NX_WORLD_MISSIONS; i++)
        if ((int)g_nxWorldMissions[i].id == id) { m = &g_nxWorldMissions[i]; break; }
    Movie_Close();
    BGM_Stop();
    /* "-<modo> <mods>  <condição>": a condição é o 3º campo (o RUN passa a string inteira) */
    s_cond = "1";
    memset(&g_nxMods, 0, sizeof(g_nxMods));
    g_nxMods.skin = -1;
    if (m) {
        const char* mm = m->cond;   /* "-X mods cond": 2º campo */
        while (*mm && *mm != ' ') mm++;
        while (*mm == ' ') mm++;
        if (*mm && *mm != '.') parseMods(mm);
        Log_Print("WORLD: mods \"%.8s\" -> flags 0x%X velocidade %d skin %d ícones %d\n", mm,
                  g_nxMods.flags, g_nxMods.units, g_nxMods.skin, g_nxMods.nIcons);
        const char* c = m->cond;
        for (int f = 0; f < 2 && *c; f++) {
            while (*c && *c != ' ') c++;
            while (*c == ' ') c++;
        }
        if (*c) s_cond = c;
    }
    int diff = -1;
    if (m && m->cond[0] == '-') {   /* letra do modo: N H C D M -> NORMAL HARD CRAZY DOUBLE NIGHTMARE */
        switch (m->cond[1]) {
        case 'N': diff = 0; break;
        case 'H': diff = 1; break;
        case 'C': diff = 2; break;
        case 'D': diff = 3; break;
        case 'M': diff = 4; break;
        }
    }
    Log_Print("WORLD: %s (%d) stage %d -> %X \"%s\", corações %d\n", g_nxWorldStages[s_cur].name, num,
              stage + 1, (unsigned)id, m ? m->cond : "?", g_game.nxHearts);
    if (!m || diff < 0 || !ExSelect_StartMission(id, diff, m->level, s_joined)) {
        Log_Print("WORLD: missão %X não existe\n", (unsigned)id);
        Game_ChangeState(STATE_GAMEOVER_ENTER);
    }
}

/* ---------------------------------------------------------------------------
 * Begin (0x80816d0)
 * ------------------------------------------------------------------------- */
void NxWorld_Enter(void) {
    Title_StopMusic();
    Movie_Close();
    BGM_Stop();
    s_joined = Title_GetJoinedMask() & 3;
    if (s_joined == 0) s_joined = 1;
    s_started = false;

    /* 0x80816f0..0x808176d: locais abertos. O original abre também os que têm
     * recorde (0x81db204); aqui só os de fábrica e os do contador [0x9e3dc27]
     * (EEPROM +0xF27) / 10000 * 32, na ordem (i + 4) / 4 + ((i + 4) & 3) * 16. */
    memcpy(s_open, g_nxWorldOpen, sizeof(s_open));
    /* 0x80816f0: recorde do número i + 1 abre o índice (i + 4) / 4 + ((i + 4) & 3) * 16
     * (o local seguinte da mesma região) */
    for (int i = 0; i <= 0x3b; i++) {
        if (Rank_LocScore(i + 1) == 0) continue;
        int j = i + 4;
        s_open[j / 4 + (j & 3) * 16] = 1;
    }
    int n = (int)(Eeprom_Get32(0xF27) / 10000) * 32;
    for (int i = 0; i < n && i != 0x40; i++) {
        int j = i + 4;
        int k = j / 4 + (j & 3) * 16;
        if (k >= 0 && k < NX_WORLD_STAGES) s_open[k] = 1;
    }

    /* 0x808176f: stages 2 e 3 começam a missão do mesmo local direto (no 1º quadro,
     * fora do Game_ChangeState) */
    s_autoStart = g_game.stageCount < 3;
    if (s_autoStart) return;

    s_cur = 0;
    Resource_ClearBGA();
    static const char* const k_bga[5] = { "ARCADE_SPECIAL", "COMMON", "WORLDTOUR01", "WORLDTOUR02", "ARRO" };
    for (int i = 0; i < 5; i++)
        if (!Resource_LoadBGAByName(k_bga[i]))
            Log_Print("WORLD: falha ao carregar BGA\\%s.DAT\n", k_bga[i]);
    BGA_Reset();

    s_ok = g_game.bgaPicCount >= 5;
    static const int k_frame[4] = { 0x13, 0x1b, 0x1f, 0x17 };
    static const int k_bar[4][2] = { { 1, 65 }, { 8, 80 }, { 7, 66 }, { 9, 81 } };   /* 0x8113fc0 [16..23] */
    for (int r = 0; r < 4 && s_ok; r++) {
        s_ok &= BGA_GetLayerSrc(NW_AS, k_frame[r], &s_frame[r]);
        s_ok &= BGA_GetLayerSrc(NW_AS, 0x46 + r, &s_regText[r]);
        s_ok &= BGA_GetLayerSrc(NW_CM, k_bar[r][0], &s_regBar[r][0]);
        s_ok &= BGA_GetLayerSrc(NW_CM, k_bar[r][1], &s_regBar[r][1]);
    }
    if (s_ok) s_ok &= BGA_GetLayerSrc(NW_AS, 0x45, &s_lock);
    for (int i = 0; i < NX_WORLD_STAGES && s_ok; i++) {
        s_ok &= BGA_GetLayerSrc(NW_W1, 0x23 + i, &s_label[i]);
        s_ok &= BGA_GetLayerSrc(NW_W2, i, &s_name[i]);
    }
    if (s_ok) {
        s_ok &= BGA_GetLayerSrc(NW_CM, 0x52, &s_digit[0]);
        for (int i = 1; i < 10; i++) s_ok &= BGA_GetLayerSrc(NW_CM, 0x58 + i, &s_digit[i]);
    }
    if (!s_ok) Log_Print("WORLD: alguma camada de origem não existe\n");

    /* 0x8082085..0x8082159: setas Prev/Next do ARRO */
    static const int k_arro[8][2] = { { 0, 0x12 }, { 0xa, 0x17 }, { 2, 0x13 }, { 0xc, 0x18 },
                                      { 4, 0x14 }, { 0xe, 0x19 }, { 6, 0x15 }, { 0x10, 0x1a } };
    for (int i = 0; i < 8; i++) {
        BGALayerSrc a;
        if (BGA_GetLayerSrc(NW_AR, k_arro[i][1], &a)) BGA_SetLayerSrc(NW_AR, k_arro[i][0], &a);
    }

    s_hs = false;
    BGA_GetLayerSrc(NW_AS, 5, &s_hsLabel);
    {
        char wp[MAX_PATH];
        snprintf(wp, sizeof(wp), "%s/BGA/WFONT.DAT", g_game.currentDirectory);
        s_wfTex[0] = s_wfTex[1] = -1;
        if (RES_Open(wp)) {
            s_wfTex[0] = loadTextureFromRES("wfont01.tga");
            s_wfTex[1] = loadTextureFromRES("wfont02.tga");
            RES_Close();
        }
        Log_Print("WORLD: WFONT tex %d %d\n", s_wfTex[0], s_wfTex[1]);
    }

    /* cor do centro do mapa de cada região (WORLDTOUR01 slots 1, 2, 4, 3), usada no fundo */
    static const int k_map[4] = { 1, 2, 4, 3 };
    for (int r = 0; r < 4; r++) {
        s_tint[r][0] = s_tint[r][1] = s_tint[r][2] = 0.0f;
        BGALayerSrc m;
        if (!BGA_GetLayerSrc(NW_W1, k_map[r], &m)) continue;
        int tex = m.isSPR && m.sprTileCount > 0 ? g_game.sprTiles[m.sprTileStart].texId : m.texId;
        int w = tex >= 0 ? Texture_GetWidth(tex) : 0, h = tex >= 0 ? Texture_GetHeight(tex) : 0;
        if (w <= 0 || h <= 0) continue;
        unsigned char* px = (unsigned char*)malloc((size_t)w * h * 4);
        if (!px) continue;
        Texture_Bind(tex);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        const unsigned char* c = px + ((h / 2) * w + w / 2) * 4;   /* glMultiTexCoord2f(0.5, 0.5) */
        for (int k = 0; k < 3; k++) s_tint[r][k] = c[k] / 255.0f;
        free(px);
    }

    /* um objeto position.spr por slot */
    s_posCount = 0;
    static const int k_pos[] = { 15, 7, 11, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56 };
    for (size_t i = 0; i < sizeof(k_pos) / sizeof(k_pos[0]); i++) makePosCopy(k_pos[i]);

    /* 0x8084be0: discos do BGA/WF.DAT */
    char path[MAX_PATH];
    for (int i = 0; i < NX_WORLD_STAGES; i++) s_tex[i] = -1;
    snprintf(path, sizeof(path), "%s/BGA/WF.DAT", g_game.currentDirectory);
    if (RES_Open(path)) {
        static const char k_reg[4] = { 'a', 'n', 's', 'e' };
        for (int r = 0; r < 4; r++)
            for (int i = 0; i < 16; i++) {
                char nm[16];
                snprintf(nm, sizeof(nm), "f-%c%02d.tga", k_reg[r], i + 1);
                s_tex[r * 16 + i] = loadTextureFromRES(nm);
            }
        RES_Close();
    } else Log_Print("WORLD: '%s' não abriu\n", path);

    for (int k = 0; k < WS_COUNT; k++)
        if (s_snd[k] < 0) s_snd[k] = Audio_LoadWaveFile(k_wsnd[k]);

    snprintf(path, sizeof(path), "%s/BGA/WT.MOV", g_game.currentDirectory);
    Movie_Open(path, true);

    static const char* const k_reset[][2] = {
        { "", "channel start" }, { "", "channel text start" }, { "", "single mode text start" },
    };
    for (size_t i = 0; i < sizeof(k_reset) / sizeof(k_reset[0]); i++) BGA_SceneReset(NW_CM, k_reset[i][1]);
    BGA_SceneReset(NW_W1, "world tour start");
    BGA_SceneReset(NW_AS, "screen2 start");
    BGA_SceneReset(NW_AR, "arro start");

    s_region = s_regionPrev = 0;
    s_regionOld = 0;
    s_move = 0;
    s_confirmed = s_trans = s_regionChanged = false;
    s_acc = 0x1f4;
    s_intro = true;
    s_transScene = NULL;
    s_screen = "screen2 hold";
    s_arro[0] = "arroUL click"; s_arro[1] = "arroUR click";
    s_arro[2] = "arroHL click"; s_arro[3] = "arroHR click";
    s_time = 0x5a;
    s_t0 = timeGetTime();
    carousel(0);
    regionUpdate();
    s_move = 3;   /* 0x8082339 */
    Log_Print("WORLD: Begin, %d locais abertos, regiao %d\n", n + 4, s_region);
}

/* ---------------------------------------------------------------------------
 * Entrada (0x8083250)
 * ------------------------------------------------------------------------- */
static bool repeatHit(int p, int k, PadButton b, bool* rep) {
    uint32_t now = timeGetTime();
    *rep = false;
    if (Input_IsPadHit(p, b)) { s_holdT[p][k] = now; return true; }
    if (!Input_IsPadDown(p, b) || now - s_holdT[p][k] <= 0x320) return false;
    *rep = true;
    if (s_acc <= 0x2cf) s_acc += 0x1e;
    s_holdT[p][k] = now - (uint32_t)s_acc;
    return true;
}

static void moveTo(int dir, int p, int k, bool rep) {   /* 0x8083400 (+1) / 0x8083700 (-1) */
    s_confirmed = false;
    s_move = dir;
    s_hs = false;   /* 0x808347a: 0x8084fe0(0) */
    BGA_SceneReset(NW_AS, dir == 1 ? "screen2 L move" : "screen2 R move");
    BGA_SceneReset(NW_CM, "single mode text start");
    s_cur = wrap(s_cur + (dir == 1 ? 1 : -1));   /* 0x80848e0 / 0x8084900 */
    carousel(dir);
    snd(s_acc > 0x289 ? WS_MOVE_ACC : WS_MOVE);
    if (regionOf(s_cur) != s_region) {   /* 0x80834b8: troca de região -> transição ao parar */
        s_regionChanged = true;
        s_acc = 0x1f4;
        s_regionOld = s_region;
    }
    if (rep) s_holdT[p][k] = timeGetTime() - (uint32_t)s_acc;
}

static void playerInput(int p) {
    if (!(s_joined & (1u << p))) return;
    bool rep;
    if (Input_IsPadHit(p, PAD_C)) {   /* 0x80839e8 */
        if (s_confirmed) { s_time = 0; snd(WS_SELECT); }
        else if (selectable(s_cur)) {
            s_confirmed = true;
            if (s_move != 0) {
                s_move = 0;
                if (s_regionChanged) { s_trans = true; s_regionChanged = false; startTransition(); }
                carousel(0);
                regionUpdate();
            }
            BGA_SceneReset(NW_CM, "center step");
            snd(WS_SELECT);
        } else snd(WS_WRONG);
    }
    if (s_trans) return;
    if (repeatHit(p, 0, PAD_UR, &rep)) { BGA_SceneReset(NW_AR, "arroHR click"); s_arro[3] = rep && s_acc > 0x2e3 ? "arroHR click hold" : "arroHR click"; moveTo(1, p, 0, rep); }
    if (repeatHit(p, 1, PAD_DR, &rep)) { BGA_SceneReset(NW_AR, "arroUR click"); s_arro[1] = rep && s_acc > 0x2e3 ? "arroUR click hold" : "arroUR click"; moveTo(1, p, 1, rep); }
    if (repeatHit(p, 2, PAD_UL, &rep)) { BGA_SceneReset(NW_AR, "arroHL click"); s_arro[2] = rep && s_acc > 0x2e3 ? "arroHL click hold" : "arroHL click"; moveTo(2, p, 2, rep); }
    if (repeatHit(p, 3, PAD_DL, &rep)) { BGA_SceneReset(NW_AR, "arroUL click"); s_arro[0] = rep && s_acc > 0x2e3 ? "arroUL click hold" : "arroUL click"; moveTo(2, p, 3, rep); }
}

/* ---------------------------------------------------------------------------
 * Quadro (0x8082390 intro / 0x80827f0)
 * ------------------------------------------------------------------------- */
void NxWorld_Update(float dt) {
    if (s_autoStart) { s_autoStart = false; startMission(); return; }
    if (s_started) return;
    Movie_Update(dt);
    s_time = 0x5a - (int)((timeGetTime() - s_t0) / 1000);
    if (s_time < 0) s_time = 0;

    bool any = false;
    for (int p = 0; p < 2; p++) {
        if (!(s_joined & (1u << p))) continue;
        static const PadButton k_b[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
        for (int k = 0; k < 5; k++) if (Input_IsPadHit(p, k_b[k])) any = true;
    }
    if (s_intro) {   /* 0x80823ca: qualquer painel pula a abertura */
        if (any || (BGA_SceneDone(NW_W1, "world tour start") && BGA_SceneDone(NW_AR, "arro start"))) s_intro = false;
        else return;
    }

    playerInput(0);
    playerInput(1);

    /* 0x8082a11: DL/DR segurado -> carrossel e região todo quadro */
    bool held = false;
    for (int p = 0; p < 2; p++)
        if ((s_joined & (1u << p)) && (Input_IsPadDown(p, PAD_DL) || Input_IsPadDown(p, PAD_DR) ||
                                       Input_IsPadDown(p, PAD_UL) || Input_IsPadDown(p, PAD_UR))) held = true;
    if (held) { carousel(s_move); regionUpdate(); }
    else s_acc = 0x1f4;

    /* 0x80829c1: cena do carrossel */
    if (!s_trans) s_screen = s_move == 1 ? "screen2 L move" : s_move == 2 ? "screen2 R move" : "screen2 hold";
    if (s_confirmed) s_screen = "screen2 click";

    /* 0x8083027: movimento terminou */
    if (!s_trans && s_move != 0 && !held && BGA_SceneDone(NW_AS, s_screen)) {
        s_move = 0;
        if (s_regionChanged) { s_trans = true; s_regionChanged = false; startTransition(); }
        carousel(0);
        regionUpdate();
        /* 0x8083094: local liberado com recorde -> texto do recorde, "hi-score start",
         * rótulo central (slot 0x11) = ARCADE slot 5 */
        if (!s_trans && selectable(s_cur) && Rank_LocScore(g_nxWorldStages[s_cur].number) != 0) {
            s_hs = true;
            BGA_SceneReset(NW_W1, "hi-score start");
            BGA_SetLayerSrc(NW_AS, 0x11, &s_hsLabel);
        }
    }

    /* 0x8082d57: transição do mapa: "screen2 end" -> cena -> 1,5 s -> "screen2 start" */
    if (s_trans && BGA_SceneDone(NW_AS, "screen2 end") &&
        (!s_transScene || BGA_SceneDone(NW_W1, s_transScene)) && timeGetTime() - s_transT > 0x5db) {
        s_move = 3;
        if (BGA_SceneDone(NW_AS, "screen2 start")) s_trans = false;
    }

    if (s_time <= 0) {   /* 0x8082c91 */
        if (!selectable(s_cur)) {   /* 0x8084dd0: sorteia um local aberto */
            int guard = 0;
            do s_cur = wrap(rand() % NX_WORLD_STAGES - 1); while (!selectable(s_cur) && ++guard < 1000);
        }
        startMission();
    }
}

void NxWorld_Render(void) {
    if (s_started || s_autoStart || g_game.bgaPicCount < 5) return;
    /* fundo: WT.MOV + cor do mapa da região (combine ADD da unidade 1, 0x8082848) */
    Movie_Render();
    glEnable(GL_BLEND);
    {
        const float* c = s_tint[ri(regionOf(s_cur))];
        glDisable(GL_TEXTURE_2D);
        glBlendFunc(GL_ONE, GL_ONE);
        glColor4f(c[0], c[1], c[2], 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(640, 0); glVertex2f(640, 480); glVertex2f(0, 480);
        glEnd();
        glEnable(GL_TEXTURE_2D);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);

    /* nome do local no COMMON (0x80832a9) */
    if (s_ok) BGA_SetLayerSrc(NW_CM, 0x38, &s_name[s_cur]);

    if (s_intro) {   /* 0x80825cf..0x8082728 */
        BGA_ScenePlay(NW_W1, "world tour start", true);
        BGA_ScenePlay(NW_CM, "channel start", true);
        if (BGA_SceneDone(NW_W1, "world tour start")) BGA_ScenePlay(NW_CM, "channel text start", true);
        if (BGA_SceneDone(NW_CM, "channel start")) {
            BGA_ScenePlay(NW_AS, "screen2 start", true);
            BGA_ScenePlay(NW_CM, "single mode text start", true);
            if (BGA_SceneDone(NW_AS, "screen2 start")) BGA_ScenePlay(NW_AR, "arro start", true);
        }
        return;
    }

    /* mapa e carrossel */
    if (!s_trans) {
        BGA_ScenePlay(NW_W1, k_hold[ri(s_regionChanged ? s_regionOld : s_region)], true);
        BGA_ScenePlay(NW_AS, s_screen, true);
    } else if (!BGA_SceneDone(NW_AS, "screen2 end")) {
        BGA_ScenePlay(NW_W1, k_hold[ri(s_regionOld)], true);
        BGA_ScenePlay(NW_AS, "screen2 end", true);
    } else {
        if (s_transScene && !BGA_SceneDone(NW_W1, s_transScene)) BGA_ScenePlay(NW_W1, s_transScene, true);
        else BGA_ScenePlay(NW_W1, k_hold[ri(s_region)], true);
        if (s_move == 3) BGA_ScenePlay(NW_AS, "screen2 start", true);
    }

    /* região, nome do local, tempo */
    BGA_ScenePlay(NW_CM, "channel start", true);
    BGA_ScenePlay(NW_CM, "channel text start", true);
    BGA_ScenePlay(NW_CM, "single mode text start", true);
    if (s_ok) {
        BGA_SetLayerSrc(NW_CM, 0x58, &s_digit[s_time % 10]);
        BGA_SetLayerSrc(NW_CM, 0x57, &s_digit[(s_time / 10) % 10]);
    }
    BGA_ScenePlay(NW_CM, "time position", true);
    if (s_confirmed) BGA_ScenePlay(NW_CM, "center step", true);

    /* setas (0x8082c25) */
    for (int i = 0; i < 4; i++) BGA_ScenePlay(NW_AR, s_arro[i], true);

    /* recorde (0x8082abd / 0x8084f60) */
    if (s_hs) {
        int num = g_nxWorldStages[s_cur].number;
        BGA_ScenePlay(NW_W1, "hi-score start", true);
        glColor4f(1, 1, 1, 1);
        wfontPrint(320.0f, 210.0f, Rank_LocName(num));
        char sc[32];
        snprintf(sc, sizeof(sc), "Score: %d", (int)Rank_LocScore(num));
        NxText_DrawCentered(sc, 0x140, 0x16f);
    }
    glColor4f(1, 1, 1, 1);
}

/* CONTINUE aceito (0x8072d57 -> 0x805fb90 com 0): repete a mesma missão.
 * Hipótese: o stage e os corações voltam ao que eram antes da missão perdida. */
void NxWorld_Retry(void) {
    g_game.stageCount++;
    g_game.nxHearts += s_lastCost;
    s_started = false;
    startMission();
}

const char* NxWorld_Cond(void) { return s_cond; }

int NxWorld_Loc(void) { return s_cur; }

/* 0x80758bd..0x807597c: na 1ª conclusão do local (recorde ainda 0) e fora das liberadas
 * pelo contador, a música de 0x8110340[número - 1], se ela estiver ligada no Service Menu:
 * +0x34 da música = (EEPROM +0xF38 + índice != 0) (0x8061bb0, lista do SETUP 0x8088ef0).
 * era: "ainda não liberada" (hipótese). */
int NxClear_UnlockSong(void) {
    int num = g_nxWorldStages[s_cur].number;
    int n = (int)(Eeprom_Get32(0xF27) / 10000) * 32;
    if (n > 0x40) n = 0x40;
    if (Rank_LocScore(num) != 0 || num - 1 < n) return -1;
    uint32_t id = g_nxWorldUnlock[num - 1];
    if (!id) return -1;
    for (int i = 0; i < EX_SONG_COUNT; i++)
        if (g_exSongs[i].id == id) return (Eeprom_Get32(0xF38 + i) & 0xFF) != 0 ? -1 : i;   /* desligada no SETUP */
    return -1;
}
