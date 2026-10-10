/*
 * zero_select.c — tela de seleção de música do Pump It Up Zero (CSelect, piu).
 *
 * Classe CSelect (RTTI "7CSelect", vtable 0x081003c8): Begin 0x806a7a0, quadro
 * 0x806b530 (lógica e desenho juntos), End 0x806e240. A lista de músicas é um
 * objeto embutido em +4 (vtable 0x081003ec): 0x805af80 monta o canal, 0x805ae20
 * carrega os discos do BGA/90.DAT.
 *
 * Recursos (Begin):
 *   BGA/SELECT.DAT  [+0x90]  cenas SET, LEFT/RIGHT(_EFFECT), dificuldades,
 *                            canais, 2PLAY SET, 1P2P/1PVS2P, STEFFECT, ANOMARK...
 *   BGA/ICONDD.DAT  [+0x94]  ícones de modificador por jogador ("1p 2x_in"...)
 *   BGA/ICONALL.DAT [+0x98]  ícone grande do código digitado ("2X", "RV", "hc"...)
 *   BGA/SELECT3.DAT [+0x9C]  TIME, 1P_LV/2P_LV, READY e os dígitos (slots)
 *   /SCRIPT/UI/ZERO/SFX_SELECT.LUA + SFX_GLOBAL.LUA: EFF_* -> WAVE (ver k_sfx)
 *
 * Quadro (0x806b530), na ordem do original:
 *   prévia (fade de 0,1 por quadro na troca) -> contador (60 s) -> roda de
 *   discos em perspectiva (0x806eae0) -> vídeo da prévia em 0..640 x 175..415
 *   -> ícones -> dígitos -> cenas do SELECT -> ICONALL -> SELECT3 -> prévia
 *   nova após 30 quadros parado -> giro da roda -> códigos -> entrada.
 *
 * Diferenças conhecidas do original:
 *   - prévia e áudio carregam no mesmo quadro (o original usa duas threads);
 *   - com 2 jogadores o gameplay do projeto usa a dificuldade do P1 para os dois;
 *   - códigos 0..5 (skins / grade reverse) dependem de desbloqueios do
 *     PIUZERO.INI (0x805a250, +0x28..+0x2F) que o projeto ainda não guarda;
 *   - só 2X/3X/4X/8X e RV chegam ao gameplay; os demais modificadores são
 *     guardados e mostrados, mas o gameplay ainda não os aplica.
 */
#include "pumpy.h"
#include "bga.h"
#include "movie.h"
#include <math.h>

enum { ZB_SELECT = 0, ZB_ICONDD = 1, ZB_ICONALL = 2, ZB_SELECT3 = 3 };

/* ---------------------------------------------------------------------------
 * Desbloqueio (estado de execução da tabela 0x08119040)
 *   +0x3E disponível e +0x44..+0x48 dificuldade aberta. O construtor da lista
 *   (0x805aa60) trava C03, C17 e as Another (0x805a600(id, 0)) e reabre parte
 *   das Another por dificuldade (0x805a680). Desbloqueios por pontos e missões
 *   (0x805a680 a partir de 0x0804f8xx) ainda não existem no projeto.
 * ------------------------------------------------------------------------- */
static bool    s_unlockInit;
static uint8_t s_avail[EX_SONG_COUNT];
static uint8_t s_open[EX_SONG_COUNT][5];

static int songIndex(int id) {
    for (int i = 0; i < EX_SONG_COUNT; i++)
        if ((int)g_exSongs[i].id == id) return i;
    return -1;
}

/* 0x805a600(id, on): as cinco dificuldades e o +0x3E juntos */
static void unlockSong(int id, bool on) {
    int i = songIndex(id);
    if (i < 0) return;
    for (int d = 0; d < 5; d++) s_open[i][d] = on ? 1 : 0;
    s_avail[i] = on ? 1 : 0;
}

/* 0x805a680(id, n, h, c, d, nm): -1 não mexe; +0x3E = alguma aberta */
static void unlockModes(int id, int n, int h, int c, int d, int nm) {
    int i = songIndex(id);
    if (i < 0) return;
    const int v[5] = { n, h, c, d, nm };
    bool any = false;
    for (int k = 0; k < 5; k++) {
        if (v[k] != -1) s_open[i][k] = (v[k] == 1);
        if (s_open[i][k]) any = true;
    }
    s_avail[i] = any;
}

static void initUnlocks(void) {
    if (s_unlockInit) return;
    s_unlockInit = true;
    for (int i = 0; i < EX_SONG_COUNT; i++) {
        s_avail[i] = g_exSongs[i].avail;
        for (int d = 0; d < 5; d++) s_open[i][d] = g_exSongs[i].lock[d];
    }
    /* 0x805aa60 */
    static const int k_locked[] = {
        0xC03, 0xC17, 0xC1112, 0xC1204, 0xC1205, 0xC1301, 0xC1405, 0xC1704, 0xC1735,
        0xC1809, 0xC1814, 0xC1818, 0xC1A01, 0xC1A02, 0xC1B13, 0xC1B18, 0xC1C03,
        0xC1C04, 0xC1C09, 0xC1C11,
    };
    for (size_t k = 0; k < sizeof(k_locked) / sizeof(k_locked[0]); k++)
        unlockSong(k_locked[k], false);
    unlockModes(0xC1301, 0, 0, 1, 0, 0);
    unlockModes(0xC1A01, 0, 0, 0, 0, 1);
    unlockModes(0xC1809, 0, 0, 0, 1, 0);
    unlockModes(0xC1818, 0, 0, 0, 0, 1);
    unlockModes(0xC1B18, 0, 0, 1, 0, 0);
    unlockModes(0xC1C09, 0, 0, 0, 1, 0);
}

/* ---------------------------------------------------------------------------
 * Sons: EFF_* de SFX_SELECT.LUA (UI/ZERO) e SFX_GLOBAL.LUA, tocados por
 * 0x804f220(nome). EFF_SELECT_START não está em nenhuma tabela: sem som.
 * ------------------------------------------------------------------------- */
enum { SFX_MOVE, SFX_MOVE_ACC, SFX_CHANNEL, SFX_MODE, SFX_SELECT, SFX_JOIN,
       SFX_HIDDEN, SFX_START, SFX_TIME_LIMIT, SFX_COUNT };
static const char* const k_sfx[SFX_COUNT] = {
    "3-2.WAV",        /* EFF_MOVE */
    "10-2.WAV",       /* EFF_MOVE_ACC */
    "CHGMOD.WAV",     /* EFF_CHANNEL */
    "13-1.WAV",       /* EFF_MODE */
    "3-2.WAV",        /* EFF_SELECT */
    "PUSHPANEL.WAV",  /* EFF_JOIN */
    "2-1.WAV",        /* EFF_HIDDEN_SELECTED */
    "START.WAV",      /* EFF_START (SFX_GLOBAL) */
    "TIME_LIMIT.WAV", /* EFF_TIME_LIMIT (SFX_GLOBAL) */
};
static int s_sfx[SFX_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static void sfx(int k) { if (s_sfx[k] >= 0) Audio_Play(s_sfx[k], false); }

/* ---------------------------------------------------------------------------
 * Estado (campos do objeto CSelect)
 * ------------------------------------------------------------------------- */
static int  s_list[EX_SONG_COUNT];     /* [+8]: cópia dos registros do canal */
static int  s_count;                   /* lista +0x4C */
static int  s_ch;                      /* +0x10 (lista +0xC) */
static int  s_cursor[EX_CHANNEL_COUNT];/* +0x14 (lista +0x10) */
static int  s_lastId[EX_CHANNEL_COUNT];/* +0x28 (lista +0x24): música lembrada por canal */
static bool s_listInit;
static int  s_diff[2];                 /* +0x74 / +0x78 */
static int  s_diffSaved[2];            /* +0x7C / +0x80 */
static const char* s_diffScene[2];     /* +0x68 / +0x6C */
static const char* s_moveScene;        /* +0x60: LEFT / RIGHT */
static const char* s_moveFx;           /* +0x64: LEFT_EFFECT / RIGHT_EFFECT */
static const char* s_chScene;          /* +0x70 */
static int  s_time, s_timePrev;        /* +0x84 / +0x88 */
static uint32_t s_timeTick;            /* [+0x134]: cronômetro de 60 s */
static float s_idle;                   /* +0x138: quadros desde o último movimento */
static bool s_anotherSel;              /* +0x58 (1 no Begin) */
static bool s_vs;                      /* +0x59: 1PVS2P */
static bool s_ready;                   /* +0x5A */
static bool s_previewDone;             /* +0x5B */
static int  s_spinDir;                 /* +0x5C: 0 parado, 1 direita, 2 esquerda */
static int  s_spin;                    /* [0x08627ccc] 0..15 */
static int  s_repeat;                  /* [0x08627cc8] repetições do DL/DR */
static float s_previewA;               /* [0x08627cd0] alfa da prévia */
static int  s_previewPending = -1;     /* id esperando o fade */
static const char* s_bigIcon;          /* [0x0811cb28]: cena do ICONALL */
static const char* s_icon[2][5];       /* [0x0811ca00]: cena atual por grupo */
static unsigned s_mods[2];             /* +0x1F0 de cada jogador */
static unsigned s_joined;              /* [obj 0x98c]+4 bits 0/1 */
static int  s_station = 1;             /* [obj 0x98c]+0xCC: 0 EASY 1 ARCADE 2 MISSION 3 REMIX */
static bool s_anotherChannel;          /* [obj 0x98c]+0x108 == 1 (ANOTHERCHANNEL) */
static bool s_started;
static int  s_frame;
static int  s_bannerTex[EX_SONG_COUNT];
static BGALayerSrc s_digTime[11], s_dig2P[11], s_dig1P[11];
static bool s_digOk;

/* histórico de códigos por jogador (objetos +0x140 / +0x144, 0x804f300) */
#define CODE_HIST 25
static uint8_t s_hist[2][CODE_HIST];
static int     s_histLen[2];

static const char* const k_chScene[5] = { "BANYA", "KPOP", "POP", "REMIX", "ANOTHER" };
static const char* const k_diff1P[5] = { "NORMAL", "HARD", "CRAZY", "FREESTYLE", "NIGHTMARE" };
/* 0x0811be44 / be4c / be54: com 2 jogadores só N, H, C (NM = Normal) */
static const char* const k_diff2P[3][2] = { { "NM1", "NM2" }, { "HD1", "HD2" }, { "CRZ1", "CRZ2" } };

/* 0x0811bde0[estação][canal]: música inicial de cada canal na 1ª música do
 * crédito (só ARCADE e REMIX têm valores). */
static const int k_startSong[4][5] = {
    { -1, -1, -1, -1, -1 },
    { 0xC04, 0xC09, 0xC21, -1, 0xC1112 },
    { -1, -1, -1, -1, -1 },
    { -1, -1, -1, 0xC42, -1 },
};

/* ICONDD (0x0811ca40): k * 2 + jogador */
static const char* const k_iconName[29][2] = {
    { "in_1p", "in_2p" },
    { "1p 2x_in", "2x_in" }, { "1p 2x_out", "2x_out" },
    { "1p 3x_in", "3x_in" }, { "1p 3x_out", "3x_out" },
    { "1p 4x_in", "4x_in" }, { "1p 4x_out", "4x_out" },
    { "1p 8x_in", "8x_in" }, { "1p 8x_out", "8x_out" },
    { "1p_RV_in", "RV_in" }, { "1p_RV_out", "RV_out" },
    { "1p ew_in", "ew_in" }, { "1p ew_out", "ew_out" },
    { "1p rs_in", "rs_in" }, { "1p rs_out", "rs_out" },
    { "1p ac_in", "ac_in" }, { "1p ac_out", "ac_out" },
    { "1p dc_in", "dc_in" }, { "1p dc_out", "dc_out" },
    { "1p x_in", "x_in" },   { "1p x_out", "x_out" },
    { "1p fd_in", "fd_in" }, { "1p fd_out", "fd_out" },
    { "1p mr_in", "mr_in" }, { "1p mr_out", "mr_out" },
    { "1p v_in", "v_in" },   { "1p v_out", "v_out" },
    { "1p ns_in", "ns_in" }, { "1p ns_out", "ns_out" },
};
enum { IC_IN, IC_2X, IC_2X_O, IC_3X, IC_3X_O, IC_4X, IC_4X_O, IC_8X, IC_8X_O, IC_RV, IC_RV_O,
       IC_EW, IC_EW_O, IC_RS, IC_RS_O, IC_AC, IC_AC_O, IC_DC, IC_DC_O, IC_X, IC_X_O,
       IC_FD, IC_FD_O, IC_MR, IC_MR_O, IC_V, IC_V_O, IC_NS, IC_NS_O };
#define ICON(k, p) k_iconName[k][p]

/* +0x1F0 do jogador */
#define ZM_2X    0x00002u
#define ZM_3X    0x00004u
#define ZM_4X    0x00008u
#define ZM_8X    0x00010u
#define ZM_V     0x00020u
#define ZM_MR    0x00040u
#define ZM_RS    0x00080u
#define ZM_NS    0x00100u
#define ZM_RV    0x00400u
#define ZM_FD    0x00800u
#define ZM_EW    0x01000u
#define ZM_DC    0x04000u
#define ZM_AC    0x08000u
#define ZM_GR    0x10000u   /* código 5 (grade reverse?), travado */
#define ZM_X     0x20000u
#define ZM_SPEED_GROUP 0x0141Eu   /* máscara 0xffffebe1: 2X 3X 4X 8X RV EW */

/* Tabelas de códigos (0x0811c3a0 P1 / 0x0811c960 P2; o jogador está em cada passo) */
enum { ZB_UL = 8, ZB_UR = 10, ZB_C = 9, ZB_DL = 7, ZB_DR = 11 };
typedef struct { int n; uint8_t b[11]; } ZeroCode;
static const ZeroCode k_codes[18] = {
    { 9, { ZB_UL, ZB_UR, ZB_DL, ZB_C, ZB_DL, ZB_DR, ZB_DR, ZB_UR, ZB_DL } },  /* 0 skin 3 */
    { 9, { ZB_UL, ZB_UR, ZB_DL, ZB_C, ZB_DL, ZB_DR, ZB_DR, ZB_UR, ZB_DR } },  /* 1 skin 5 */
    { 9, { ZB_UL, ZB_UR, ZB_DL, ZB_C, ZB_DL, ZB_DR, ZB_DR, ZB_UR, ZB_C } },   /* 2 skin 2 */
    { 9, { ZB_UL, ZB_UR, ZB_DL, ZB_C, ZB_DL, ZB_DR, ZB_DR, ZB_UR, ZB_UL } },  /* 3 skin 4 */
    { 9, { ZB_UL, ZB_UR, ZB_DL, ZB_C, ZB_DL, ZB_DR, ZB_DR, ZB_UR, ZB_UR } },  /* 4 skin 6 */
    { 11, { ZB_UL, ZB_UR, ZB_C, ZB_DL, ZB_DR, ZB_DR, ZB_DL, ZB_C, ZB_UR, ZB_UL, ZB_C } }, /* 5 0x10000 */
    { 9, { ZB_UL, ZB_UR, ZB_UL, ZB_UR, ZB_UL, ZB_UR, ZB_UL, ZB_UR, ZB_C } },  /* 6 RV */
    { 9, { ZB_UL, ZB_UR, ZB_UL, ZB_UR, ZB_DL, ZB_DR, ZB_DL, ZB_DR, ZB_C } },  /* 7 RS */
    { 9, { ZB_DL, ZB_UR, ZB_DL, ZB_UR, ZB_DR, ZB_UL, ZB_DR, ZB_UL, ZB_C } },  /* 8 X */
    { 9, { ZB_DR, ZB_DL, ZB_UR, ZB_UL, ZB_DR, ZB_UR, ZB_DL, ZB_UL, ZB_C } },  /* 9 EW */
    { 9, { ZB_UL, ZB_DL, ZB_UR, ZB_DR, ZB_DR, ZB_UL, ZB_UR, ZB_DL, ZB_C } },  /* 10 FD */
    { 9, { ZB_DL, ZB_DL, ZB_DR, ZB_DR, ZB_UL, ZB_UL, ZB_UR, ZB_UR, ZB_C } },  /* 11 AC */
    { 9, { ZB_DR, ZB_DR, ZB_DL, ZB_DL, ZB_UR, ZB_UR, ZB_UL, ZB_UL, ZB_C } },  /* 12 DC */
    { 9, { ZB_DR, ZB_DL, ZB_UR, ZB_UL, ZB_DR, ZB_DL, ZB_UR, ZB_UL, ZB_C } },  /* 13 MR */
    { 9, { ZB_DL, ZB_UR, ZB_C, ZB_DL, ZB_DR, ZB_UL, ZB_C, ZB_DR, ZB_C } },    /* 14 1PVS2P */
    { 5, { ZB_UL, ZB_UR, ZB_UL, ZB_UR, ZB_C } },                             /* 15 velocidade */
    { 5, { ZB_UL, ZB_UR, ZB_DL, ZB_DR, ZB_C } },                             /* 16 V / NS */
    { 6, { ZB_DL, ZB_DR, ZB_DL, ZB_DR, ZB_DL, ZB_DR } },                      /* 17 limpa */
};

/* [obj 0x98c]+0x5C/+0x90: skin escolhida pelos códigos 0..4 (travados no INI);
 * sem desbloqueio fica a 0 (BGA/SKIN00.DAT, 0x8080a1f) */
/* era: static int s_skin = 8; (uma skin para os dois)
 * NX2 CPlayer::m_CurrentSkin: skin por jogador. NX: padrão SKIN08 (informado
 * pelo usuário); SKIN09..12 só existem na NX2, então o limite fica em 8. */
static int s_skin[2] = { 8, 8 };
int Zero_SkinIndex(void) { return s_skin[0]; }
int Zero_SkinIndexP(int p) { return s_skin[(p == 1) ? 1 : 0]; }
void Zero_SetSkinIndex(int n) { if (n >= 0 && n <= 8) s_skin[0] = s_skin[1] = n; }   /* console /skin */
void Zero_SetSkinIndexP(int p, int n) { if (n >= 0 && n <= 8) s_skin[(p == 1) ? 1 : 0] = n; }

static bool twoPlayers(void) { return (s_joined & 3) == 3; }

static const ExceedSong* curSong(void) {
    if (s_count <= 0) return NULL;
    return &g_exSongs[s_list[s_cursor[s_ch]]];
}
static int curSongIdx(void) { return s_count > 0 ? s_list[s_cursor[s_ch]] : -1; }

/* nível existe e dificuldade aberta (+0x28 + d*4 != -1 && +0x44 + d) */
static bool modeOk(int si, int d) {
    return si >= 0 && d >= 0 && d < 5 && g_exSongs[si].level[d] != -1 && s_open[si][d];
}

/* 0x805af80(lista, canal, modo, força): canal == +0x24, +0x3E != 0, +0x3C == 0
 * (SETUP: tudo ligado). Modo 2 (dois jogadores) exige N, H ou C. */
static void buildList(int ch) {
    int two = twoPlayers();
    s_count = 0;
    for (int i = 0; i < EX_SONG_COUNT; i++) {
        const ExceedSong* e = &g_exSongs[i];
        if (e->channel != ch || !s_avail[i]) continue;
        if (two && e->level[0] == -1 && e->level[1] == -1 && e->level[2] == -1) continue;
        if (s_lastId[ch] == (int)e->id) s_cursor[ch] = s_count;
        s_list[s_count++] = i;
    }
    if (s_cursor[ch] >= s_count) s_cursor[ch] = 0;
}

/* 0x806d200(jogador, dificuldade): grava e reinicia a cena */
static void setDiff(int p, int d) {
    if (!twoPlayers()) {
        if (d < 0 || d > 4) d = 0;
        s_diff[0] = d;
        s_diffScene[0] = k_diff1P[d];
        BGA_SceneReset(ZB_SELECT, s_diffScene[0]);
        return;
    }
    if (d < 0 || d > 2) d = 0;
    s_diff[p] = d;
    s_diffScene[p] = k_diff2P[d][p];
    BGA_SceneReset(ZB_SELECT, s_diffScene[p]);
}

/* 0x806e450: a dificuldade de cada jogador existe na música */
static bool diffOk(int si) {
    if (!twoPlayers()) return modeOk(si, s_diff[0]);
    return modeOk(si, s_diff[0]) && modeOk(si, s_diff[1]);
}

/* 0x806e530: dificuldade inválida -> primeira que existe */
static void fixDiff(int si) {
    if (twoPlayers()) {
        for (int p = 0; p < 2; p++) {
            if (modeOk(si, s_diff[p])) continue;
            int d = 0;
            if (!modeOk(si, 0)) d = modeOk(si, 1) ? 1 : modeOk(si, 2) ? 2 : 0;
            setDiff(p, d);
        }
        return;
    }
    if (modeOk(si, s_diff[0])) return;
    int d = 0;
    for (int k = 0; k < 5; k++) if (modeOk(si, k)) { d = k; break; }
    setDiff(0, d);
}

/* 0x806e740: próxima dificuldade (UL) */
static int nextDiff(int p, int si) {
    int last = twoPlayers() ? 3 : 5;
    int cur = s_diff[twoPlayers() ? p : 0];
    for (int k = 1; k <= last; k++) {
        int d = (cur + k) % last;
        if (modeOk(si, d)) return d;
    }
    return cur;
}

static void requestPreview(int id) {
    s_previewPending = id;
}

/* 0x806a2f0: BGA/PREVIEW/%X.MOV -> PREVIEW/<ENG|HAN>/%X.MOV -> a da base ->
 * BGA/000P.MOV; AUDIO/D%X.AUD -> o da base. O idioma vem do PIUZERO.INI +0xBB1
 * (0 -> HAN); o projeto ainda não guarda essa opção e usa ENG. */
/* NX 0x80791f1: tenta "fmt" com o id e segue 0x8061d10 (recurso do recurso)
 * enquanto o arquivo não existir; devolve false se a corrente acabar. */
static bool nxFindChain(int id, const char* fmt, char* out, size_t outSize) {
    for (int guard = 0; id != -1 && guard < 8; guard++) {
        snprintf(out, outSize, fmt, g_game.currentDirectory, (unsigned)id);
        FILE* f = fopen(out, "rb");
        if (f) { fclose(f); return true; }
        id = NX_ResId(id);
    }
    return false;
}

static void openPreview(int id) {
    char mov[MAX_PATH], aud[MAX_PATH];
    /* era (Zero 0x806a2f0): PREVIEW/%X.MOV -> PREVIEW/<ENG|HAN>/%X.MOV -> base ->
     * 000P.MOV; AUDIO/D%X.AUD -> base.
     * NX 0x80791f1: PREVIEW/%03X.MOV pela corrente -> PREVIEW/001.MOV;
     * AUDIO/INTRO/%03X.AUD pela corrente. */
    if (!nxFindChain(id, "%s/BGA/PREVIEW/%03X.MOV", mov, sizeof(mov)))
        snprintf(mov, sizeof(mov), "%s/BGA/PREVIEW/001.MOV", g_game.currentDirectory);
    nxFindChain(id, "%s/AUDIO/INTRO/%03X.AUD", aud, sizeof(aud));

    Movie_Close();
    Movie_Open(mov, true);
    BGM_Stop();
    if (BGM_LoadAUDDirect(aud)) BGM_Play(false);
    Log_Print("ZSELECT: prévia %X -> '%s' + '%s'\n", (unsigned)id, mov, aud);
}

/* 0x806d3a0: troca de canal */
static void setChannel(int ch) {
    if (ch < 0 || ch >= EX_CHANNEL_COUNT) ch = 0;
    Log_Print("ZSELECT: canal %d\n", ch);
    s_ch = ch;
    s_chScene = k_chScene[ch];
    buildList(ch);
    s_previewDone = false;
    BGA_SceneReset(ZB_SELECT, s_chScene);
}

static void resetHistory(int p) { s_histLen[p] = 0; }

/* 0x804f370 + 0x804f3b0: guarda o passo e testa as sequências na ordem */
static int pushCode(int p, int button) {
    if (s_histLen[p] == CODE_HIST) {
        memmove(s_hist[p], s_hist[p] + 1, CODE_HIST - 1);
        s_histLen[p]--;
    }
    s_hist[p][s_histLen[p]++] = (uint8_t)button;
    for (int k = 0; k < 18; k++) {
        int n = k_codes[k].n;
        if (s_histLen[p] < n) continue;
        if (memcmp(s_hist[p] + s_histLen[p] - n, k_codes[k].b, (size_t)n) == 0) {
            resetHistory(p);   /* 0x804f430 */
            return k;
        }
    }
    return -1;
}

/* 0x806c740: aplica o código */
static void applyCode(int p, int code) {
    unsigned m = s_mods[p];
    const char* big = NULL;
    int slot = -1;
    switch (code) {
    case 0: case 1: case 2: case 3: case 4: case 5:
        /* skins / 0x10000: exigem o desbloqueio em PIUZERO.INI (0x805a250) */
        Log_Print("ZSELECT: código %d (travado no INI)\n", code);
        return;
    case 6:
        if (m & ZM_RV) m &= ~ZM_SPEED_GROUP;
        else { m = (m & ~ZM_SPEED_GROUP) | ZM_RV; big = "RV"; }
        slot = 0;
        break;
    case 7:
        if (!(m & ZM_RS)) big = "RS";
        m ^= ZM_RS;
        slot = 1;
        break;
    case 8:   /* X: global, liga/desliga nos dois */
        if (!(m & ZM_X)) { s_mods[0] |= ZM_X; s_mods[1] |= ZM_X; big = "X"; }
        else             { s_mods[0] &= ~ZM_X; s_mods[1] &= ~ZM_X; }
        m = s_mods[p];
        slot = 2;
        break;
    case 9:
        if (m & ZM_EW) m &= ~ZM_SPEED_GROUP;
        else { m = (m & ~ZM_SPEED_GROUP) | ZM_EW; big = "EW"; }
        slot = 0;
        break;
    case 10:
        if (m & ZM_FD) m &= ~ZM_FD;
        else { m |= ZM_FD; big = "FD"; }
        slot = 2;
        break;
    case 11:
        if (m & ZM_AC) m &= ~(ZM_AC | ZM_DC);
        else { m = (m & ~(ZM_AC | ZM_DC)) | ZM_AC; big = "AC"; }
        slot = 2;
        break;
    case 12:
        if (m & ZM_DC) m &= ~(ZM_AC | ZM_DC);
        else { m = (m & ~(ZM_AC | ZM_DC)) | ZM_DC; big = "DC"; }
        slot = 2;
        break;
    case 13:
        if (!(m & ZM_MR)) big = "MR";
        m ^= ZM_MR;
        slot = 3;
        break;
    case 14:   /* 1P x 2P: só com os dois jogadores */
        if (!twoPlayers()) return;
        if (s_vs) { s_vs = false; BGA_SceneReset(ZB_SELECT, "1P2P"); return; }
        s_vs = true;
        BGA_SceneReset(ZB_SELECT, "1PVS2P");
        big = "hc";
        break;
    case 15:   /* 2X -> 3X -> 4X -> 8X -> RV -> desliga */
        if (m & ZM_RV)       m &= ~ZM_SPEED_GROUP;
        else if (m & ZM_8X) { m = (m & ~ZM_SPEED_GROUP) | ZM_RV; big = "RV"; }
        else if (m & ZM_4X) { m = (m & ~ZM_SPEED_GROUP) | ZM_8X; big = "8X"; }
        else if (m & ZM_3X) { m = (m & ~ZM_SPEED_GROUP) | ZM_4X; big = "4X"; }
        else if (m & ZM_2X) { m = (m & ~ZM_SPEED_GROUP) | ZM_3X; big = "3X"; }
        else                { m = (m & ~ZM_SPEED_GROUP) | ZM_2X; big = "2X"; }
        slot = 0;
        break;
    case 16:   /* V -> NS -> desliga */
        if (m & ZM_NS)      m &= ~(ZM_V | ZM_NS | 0x200u);
        else if (m & ZM_V) { m = (m & ~(ZM_V | ZM_NS | 0x200u)) | ZM_NS; big = "NS"; }
        else               { m = (m & ~(ZM_V | ZM_NS | 0x200u)) | ZM_V; big = "V"; }
        slot = 4;
        break;
    case 17:   /* limpa o jogador e o X dos dois */
        m = 0;
        s_mods[0] &= ~ZM_X;
        s_mods[1] &= ~ZM_X;
        break;
    default:
        return;
    }
    s_mods[p] = m;
    if (slot >= 0 && s_icon[p][slot]) BGA_SceneReset(ZB_ICONDD, s_icon[p][slot]);
    if (big) {
        s_bigIcon = big;
        BGA_SceneReset(ZB_ICONALL, big);
    }
    Log_Print("ZSELECT: P%d código %d -> mods 0x%05X\n", p + 1, code, s_mods[p]);
}

/* 0x806c360: escolhe a cena de cada grupo de ícones (entrada/saída) */
static void updateIcons(int p) {
    if (!(s_joined & (1u << p))) return;
    unsigned m = s_mods[p];
    const char** c = s_icon[p];
    /* grupo 0: 2X 3X 4X 8X RV EW */
    static const struct { int in, out; unsigned bit; } g0[6] = {
        { IC_2X, IC_2X_O, ZM_2X }, { IC_3X, IC_3X_O, ZM_3X }, { IC_4X, IC_4X_O, ZM_4X },
        { IC_8X, IC_8X_O, ZM_8X }, { IC_RV, IC_RV_O, ZM_RV }, { IC_EW, IC_EW_O, ZM_EW },
    };
    int k;
    for (k = 0; k < 6; k++) if (c[0] == ICON(g0[k].in, p)) break;
    if (k < 6) {
        if (!(m & g0[k].bit)) c[0] = ICON(g0[k].out, p);
    } else {
        for (k = 0; k < 6; k++) if (m & g0[k].bit) c[0] = ICON(g0[k].in, p);
    }
    /* grupo 1: RS */
    if (c[1] == ICON(IC_RS, p)) { if (!(m & ZM_RS)) c[1] = ICON(IC_RS_O, p); }
    else if (m & ZM_RS) c[1] = ICON(IC_RS, p);
    /* grupo 2: AC DC X FD */
    static const struct { int in, out; unsigned bit; } g2[4] = {
        { IC_AC, IC_AC_O, ZM_AC }, { IC_DC, IC_DC_O, ZM_DC }, { IC_X, IC_X_O, ZM_X }, { IC_FD, IC_FD_O, ZM_FD },
    };
    for (k = 0; k < 4; k++) if (c[2] == ICON(g2[k].in, p)) break;
    if (k < 4) {
        if (!(m & g2[k].bit)) c[2] = ICON(g2[k].out, p);
    } else {
        for (k = 0; k < 4; k++) if (m & g2[k].bit) c[2] = ICON(g2[k].in, p);
    }
    /* grupo 3: MR */
    if (c[3] == ICON(IC_MR, p)) { if (!(m & ZM_MR)) c[3] = ICON(IC_MR_O, p); }
    else if (m & ZM_MR) c[3] = ICON(IC_MR, p);
    /* grupo 4: V NS */
    if (c[4] == ICON(IC_V, p)) { if (!(m & ZM_V)) c[4] = ICON(IC_V_O, p); }
    else if (c[4] == ICON(IC_NS, p)) { if (!(m & ZM_NS)) c[4] = ICON(IC_NS_O, p); }
    else {
        if (m & ZM_V) c[4] = ICON(IC_V, p);
        if (m & ZM_NS) c[4] = ICON(IC_NS, p);
    }
}

/* 0x806c2b0 */
static void drawIcons(int p) {
    if (!(s_joined & (1u << p))) return;
    BGA_ScenePlay(ZB_ICONDD, ICON(IC_IN, p), true);
    for (int k = 0; k < 5; k++)
        if (s_icon[p][k]) BGA_ScenePlay(ZB_ICONDD, s_icon[p][k], true);
}

/* 0x806ce90: troca o sprite dos slots de dígito do SELECT3 */
static void setDigit(int slot, const BGALayerSrc* src) {
    BGA_SetLayerSrc(ZB_SELECT3, slot, src);
}
static void levelDigits(int lv, int slotTens, int slotOnes, const BGALayerSrc* dig) {
    if (lv <= 0) {   /* 0 (ou sem nível) -> "?" (dígito 10) */
        setDigit(slotOnes, &dig[10]);
        setDigit(slotTens, &dig[10]);
        return;
    }
    setDigit(slotOnes, &dig[lv % 10]);
    setDigit(slotTens, &dig[(lv / 10) % 10]);
}
static void updateDigits(void) {
    if (!s_digOk) return;
    int si = curSongIdx();
    if (twoPlayers()) {
        levelDigits(si >= 0 ? g_exSongs[si].level[s_diff[0]] : 0, 0x35, 0x36, s_dig2P);
        levelDigits(si >= 0 ? g_exSongs[si].level[s_diff[1]] : 0, 0x37, 0x38, s_dig2P);
    } else {
        levelDigits(si >= 0 ? g_exSongs[si].level[s_diff[0]] : 0, 0x10, 0x11, s_dig1P);
    }
    int t = s_time < 0 ? 0 : s_time;
    setDigit(0x13, &s_digTime[t % 10]);
    setDigit(0x12, &s_digTime[(t / 10) % 10]);
}

/* 0x80a7690(fov): perspectiva com o plano z = 0 em 1:1 com 640x480 */
static void zeroPerspective(float fov) {
    const double W = 640.0, H = 480.0;
    double eye = (H * 0.5) / tan(fov * 0.5 * 3.14159265358979 / 180.0);
    double n = 0.1, f = 5000.0;
    double top = n * tan(fov * 0.5 * 3.14159265358979 / 180.0), right = top * (W / H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, n, f);          /* gluPerspective */
    glTranslatef(-(float)(W * 0.5), -(float)(H * 0.5), 0.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -(float)eye);              /* gluLookAt(0,0,eye, 0,0,0) */
}

static void zeroOrtho(void) {   /* 0x80a75f0 */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 640, 0, 480, -500, 500);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* 0x806eae0(giro, deslocamento do índice, x, y): 7 discos (-3..3), 20° entre
 * eles, num arco de raio 174 inclinado -75° em X. Constantes 0x0811cb2c..38 e
 * 0x08100430..0x08100540. */
static const float k_spinTab[8] = { 0.0f, 0.3878f, 1.38775f, 2.7552f, 4.2448f, 5.61225f, 6.6122f, 7.0f };
static void drawWheel(int spin, int idxOff, float px, float py) {
    if (s_count <= 0) return;
    const float R = -174.0f, discW = 43.8003006f;
    glPushMatrix();
    zeroPerspective(15.0f);
    glTranslatef(px + 320.0f, (25.0f - (0.5f * R + 200.0f)) + py, 0.0f);
    glRotatef(-75.0f, 1.0f, 0.0f, 0.0f);
    int off = (s_spinDir == 1) ? idxOff - 1 : idxOff;
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int k = -3; k < 4; k++) {
        int c = s_cursor[s_ch] + k + off;
        while (c < 0) c += s_count;
        while (c >= s_count) c -= s_count;
        int tex = s_bannerTex[s_list[c]];
        if (tex >= 0) Texture_Bind(tex);
        else glBindTexture(GL_TEXTURE_2D, 0);
        glColor4f(1, 1, 1, 1);
        glPushMatrix();
        glRotatef((float)k * -20.0f + k_spinTab[(spin / 2) & 7] * 2.857f, 0.0f, 0.0f, 1.0f);
        glTranslatef(0.0f, R + 700.0f, 0.0f);
        glRotatef(26.8f, 1.0f, 0.0f, 0.0f);
        glBegin(GL_QUADS);   /* 0x80a8780(0,0, -2w,3w, 1,1, 2w,0) */
        glTexCoord2f(0, 0); glVertex2f(discW * -2.0f, discW * 3.0f);
        glTexCoord2f(0, 1); glVertex2f(discW * -2.0f, 0.0f);
        glTexCoord2f(1, 1); glVertex2f(discW * 2.0f, 0.0f);
        glTexCoord2f(1, 0); glVertex2f(discW * 2.0f, discW * 3.0f);
        glEnd();
        glPopMatrix();
    }
    glPopMatrix();
    zeroOrtho();
}

/* ---------------------------------------------------------------------------
 * Begin (0x806a7a0)
 * ------------------------------------------------------------------------- */
void ZeroSelect_Enter(void) {
    initUnlocks();
    Title_StopMusic();
    Movie_Close();
    BGM_Stop();
    Resource_ClearBGA();
    static const char* const k_bga[4] = { "SELECT", "ICONDD", "ICONALL", "SELECT3" };
    for (int i = 0; i < 4; i++)
        if (!Resource_LoadBGAByName(k_bga[i]))
            Log_Print("ZSELECT: falha ao carregar BGA\\%s.DAT\n", k_bga[i]);
    BGA_Reset();

    /* dígitos: slots 0x14.. (tempo), 0x3A.. (2P) e 0x46.. (1P) do SELECT3, 11 cada */
    s_digOk = true;
    for (int d = 0; d < 11; d++) {
        s_digOk &= BGA_GetLayerSrc(ZB_SELECT3, 0x14 + d, &s_digTime[d]);
        s_digOk &= BGA_GetLayerSrc(ZB_SELECT3, 0x3A + d, &s_dig2P[d]);
        s_digOk &= BGA_GetLayerSrc(ZB_SELECT3, 0x46 + d, &s_dig1P[d]);
    }
    if (!s_digOk) Log_Print("ZSELECT: slots de dígito do SELECT3 incompletos\n");

    /* era (Zero 0x805ae20): discos "%X.TGA" do BGA/90.DAT, com a base como reserva.
     * NX 0x8062210: "%03X.TGA" do BGA/TEST.DAT -> registro +0x38; pula as músicas
     * com (canal & 4); sem o arquivo, tenta o recurso de 0x813f860 (baseId). */
    char path[MAX_PATH];
    for (int i = 0; i < EX_SONG_COUNT; i++) s_bannerTex[i] = -1;
    snprintf(path, sizeof(path), "%s/BGA/TEST.DAT", g_game.currentDirectory);
    if (RES_Open(path)) {
        int ok = 0;
        for (int i = 0; i < EX_SONG_COUNT; i++) {
            char name[16];
            if (g_exSongs[i].channel & 4) continue;
            snprintf(name, sizeof(name), "%03X.TGA", (unsigned)g_exSongs[i].id);
            s_bannerTex[i] = loadTextureFromRES(name);
            if (s_bannerTex[i] < 0 && g_exSongs[i].baseId != -1) {
                snprintf(name, sizeof(name), "%03X.TGA", (unsigned)g_exSongs[i].baseId);
                s_bannerTex[i] = loadTextureFromRES(name);
            }
            if (s_bannerTex[i] >= 0) ok++;
        }
        RES_Close();
        Log_Print("ZSELECT: %d/%d discos do TEST.DAT\n", ok, EX_SONG_COUNT);
    } else {
        Log_Print("ZSELECT: falha ao abrir '%s'\n", path);
    }

    for (int k = 0; k < SFX_COUNT; k++)
        if (s_sfx[k] < 0) s_sfx[k] = Audio_LoadWaveFile(k_sfx[k]);

    s_joined = Title_GetJoinedMask() & 3;
    if (s_joined == 0) s_joined = 1;
    /* estação: station.c (Exceed2) guarda 1 para REMIX; no Zero REMIX = 3 */
    extern int g_exStation;
    s_station = (g_exStation == 1) ? 3 : 1;

    bool newGame = (g_game.stageCount == 3 && !g_game.isBonusSong);
    if (newGame || !s_listInit) {
        s_listInit = true;
        s_mods[0] = s_mods[1] = 0;
        for (int c = 0; c < EX_CHANNEL_COUNT; c++) {
            s_cursor[c] = 0;
            /* +8 do objeto de jogo == 0 (1ª música): músicas iniciais */
            s_lastId[c] = k_startSong[s_station][c];
        }
        s_ch = 0;
        for (int c = 0; c < EX_CHANNEL_COUNT; c++)
            if (s_lastId[c] != -1) {
                int si = songIndex(s_lastId[c]);
                if (si >= 0) s_ch = g_exSongs[si].channel;
                break;
            }
        s_diff[0] = s_diff[1] = 0;
    }
    for (int p = 0; p < 2; p++) {
        for (int k = 0; k < 5; k++) s_icon[p][k] = NULL;   /* memset(0x0811ca00, -1) */
        resetHistory(p);
    }
    s_bigIcon = NULL;
    s_anotherSel = true;
    s_anotherChannel = false;   /* ANOTHERCHANNEL do INI: padrão "0" */

    /* RIGHT começa no fim (0x80a1ac0(SELECT, RIGHT, 15)) */
    s_moveScene = "RIGHT";
    s_moveFx = "RIGHT_EFFECT";
    BGA_SceneSetOffset(ZB_SELECT, "RIGHT", 0xF);
    setChannel(s_ch);
    if (twoPlayers()) { setDiff(0, s_diff[0]); setDiff(1, s_diff[1]); }
    else setDiff(0, s_diff[0]);
    fixDiff(curSongIdx());

    s_time = s_timePrev = 60;
    s_timeTick = timeGetTime();
    s_idle = 0;
    s_vs = false;
    s_ready = false;
    s_spinDir = 0;
    s_spin = 0;
    s_repeat = 0;
    s_previewA = 0.0f;   /* [0x08627cd0] parte de 0: a 1ª prévia abre sem fade */
    s_previewPending = -1;
    s_started = false;
    s_frame = 0;
    s_diffSaved[0] = s_diff[0];
    s_diffSaved[1] = s_diff[1];
    const ExceedSong* e = curSong();
    if (e) { s_lastId[s_ch] = (int)e->id; requestPreview((int)e->id); }
    s_previewDone = true;
    Log_Print("ZSELECT: estação %d, canal %d (%d músicas), P%s\n", s_station, s_ch, s_count,
              twoPlayers() ? "1+P2" : (s_joined & 2) ? "2" : "1");
}

/* 0x806df10: jogador entra durante a Select */
static void lateJoin(int p) {
    if (!Coin_HasCredit()) return;
    sfx(SFX_JOIN);
    Coin_ConsumeCredit();
    s_joined |= (1u << p);
    if (!twoPlayers()) return;
    int keepId = curSong() ? (int)curSong()->id : -1;
    buildList(s_ch);
    s_cursor[s_ch] = 0;
    bool found = false;
    for (int i = 0; i < s_count; i++)
        if ((int)g_exSongs[s_list[i]].id == keepId) { s_cursor[s_ch] = i; found = true; }
    setDiff(0, 0);
    setDiff(1, 0);
    if (!diffOk(curSongIdx())) fixDiff(curSongIdx());
    s_diffSaved[0] = s_diff[0];
    s_diffSaved[1] = s_diff[1];
    if (!found && curSong()) requestPreview((int)curSong()->id);
}

/* troca de música (DL/DR): 0x806d4d0, blocos dos botões 7 e 11 */
static void move(int dir) {
    sfx(s_repeat < 0x1D && s_repeat <= 5 ? SFX_MOVE : SFX_MOVE_ACC);
    s_ready = false;
    s_previewDone = false;
    if (s_count > 0) {
        int c = s_cursor[s_ch] + (dir > 0 ? 1 : -1);
        if (c < 0) c = s_count - 1;
        if (c >= s_count) c = 0;
        s_cursor[s_ch] = c;
        s_lastId[s_ch] = (int)g_exSongs[s_list[c]].id;
    }
    /* volta à dificuldade guardada (+0x7C/+0x80) */
    for (int p = 0; p < (twoPlayers() ? 2 : 1); p++)
        if (s_diff[p] != s_diffSaved[p]) setDiff(p, s_diffSaved[p]);
    if (!diffOk(curSongIdx())) fixDiff(curSongIdx());
    s_idle = 0;
    s_moveScene = dir > 0 ? "RIGHT" : "LEFT";
    s_moveFx = dir > 0 ? "RIGHT_EFFECT" : "LEFT_EFFECT";
    BGA_SceneReset(ZB_SELECT, s_moveScene);
    BGA_SceneReset(ZB_SELECT, s_moveFx);
    s_repeat++;
    Log_Print("ZSELECT: move %+d -> cursor %d (%X)\n", dir, s_cursor[s_ch],
              curSong() ? (unsigned)curSong()->id : 0u);
    s_spinDir = dir > 0 ? 1 : 2;
    s_spin = dir > 0 ? 0 : 0xF;
}

/* repetição (0x806d4d0 + 0x805c770): age no aperto ou com o botão segurado há
 * mais de 800 ms; ao agir segurando, o relógio do botão volta para "agora -
 * atraso" (500 até 5 repetições, 600 até 15, 700 até 28, depois 750), então
 * as repetições seguintes vêm a cada 300 / 200 / 100 / 50 ms. */
static uint32_t s_holdStart[2][2];
static bool repeatHit(int p, int k, PadButton b) {
    uint32_t now = timeGetTime();
    if (Input_IsPadHit(p, b)) {
        s_holdStart[p][k] = now;
        return true;
    }
    if (!Input_IsPadDown(p, b)) return false;
    if (now - s_holdStart[p][k] <= 800) return false;
    uint32_t delay = s_repeat < 6 ? 500 : s_repeat < 16 ? 600 : s_repeat < 0x1D ? 700 : 0x2EE;
    s_holdStart[p][k] = now - delay;
    return true;
}

static void startGame(void) {
    if (s_started) return;
    const ExceedSong* e = curSong();
    if (!e) return;
    s_started = true;
    sfx(SFX_START);
    int speed[2];
    bool rv[2];
    for (int p = 0; p < 2; p++) {
        unsigned m = s_mods[p];
        speed[p] = (m & ZM_8X) ? 8 : (m & ZM_4X) ? 4 : (m & ZM_3X) ? 3 : (m & ZM_2X) ? 2 : 1;
        rv[p] = (m & ZM_RV) != 0;
    }
    static const char* const k_arg[5] = { "-n", "-h", "-c", "-d", "-nm" };
    Log_Print("ZSELECT: RUN %X %s %s\n", (unsigned)e->id, k_arg[s_diff[0]],
              twoPlayers() ? k_arg[s_diff[1]] : " ");
    Movie_Close();
    BGM_Stop();
    g_nxDiffIdx[0] = g_nxDiffIdx[1] = s_diff[0];   /* chart do P2 diferente é só do NX */
    if (!ExSelect_StartZero((int)e->id, s_diff[0], s_joined, speed, rv))
        s_started = false;
}

/* 0x806d4d0: entrada de um jogador */
static void playerInput(int p) {
    bool joined = (s_joined & (1u << p)) != 0;
    static const PadButton k_pad[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
    if (joined) {
        for (int k = 0; k < 5; k++)
            if (Input_IsPadHit(p, k_pad[k])) {
                int code = pushCode(p, 7 + k);
                if (code >= 0) {
                    s_ready = false;
                    sfx(SFX_HIDDEN);
                    applyCode(p, code);
                }
            }
    }
    if (Input_IsPadHit(p, PAD_C)) {
        if (!joined) { lateJoin(p); return; }
        sfx(SFX_SELECT);
        if (!s_ready || !diffOk(curSongIdx())) {
            s_ready = true;
            BGA_SceneReset(ZB_SELECT3, "READY");
        } else {
            s_time = 0;
        }
    }
    if (!joined) return;
    if (repeatHit(p, 0, PAD_DL)) move(-1);
    if (repeatHit(p, 1, PAD_DR)) move(+1);
    if (Input_IsPadHit(p, PAD_UL)) {
        s_ready = false;
        sfx(SFX_MODE);
        int q = twoPlayers() ? p : 0;
        setDiff(q, nextDiff(q, curSongIdx()));
        s_diffSaved[q] = s_diff[q];
    }
    if (Input_IsPadHit(p, PAD_UR)) {
        s_idle = 15.0f;
        s_ready = false;
        sfx(SFX_CHANNEL);
        int ch = 0;
        switch (s_ch) {
        case 0: ch = 1; break;
        case 1: ch = 2; break;
        case 2: ch = (s_anotherSel && s_anotherChannel) ? 4 : 0; break;
        case 3: ch = 3; break;     /* REMIX STATION: canal fixo */
        default: ch = 0; break;
        }
        if (ch == s_ch) return;
        for (int q = 0; q < (twoPlayers() ? 2 : 1); q++)
            if (s_diff[q] != s_diffSaved[q]) setDiff(q, s_diffSaved[q]);
        setChannel(ch);
        if (!diffOk(curSongIdx())) fixDiff(curSongIdx());
    }
}

void ZeroSelect_Update(float dt) {
    if (s_started) return;
    s_frame++;

    /* prévia: fade de 0,1 por quadro e troca quando chega a 0 */
    if (s_previewPending >= 0) {
        s_previewA -= 0.1f;
        if (s_previewA <= 0.0f) {
            s_previewA = 1.0f;
            openPreview(s_previewPending);
            s_previewPending = -1;
        }
    } else if (Movie_IsOpen()) {
        Movie_Update(dt);
    }

    /* 60 - segundos */
    s_timePrev = s_time;
    if (s_time > 0) s_time = 60 - (int)((timeGetTime() - s_timeTick) / 1000);
    if (s_time != s_timePrev && s_time < 11 && s_time > 0) sfx(SFX_TIME_LIMIT);

    /* 30 quadros parado: prévia da música do cursor */
    if ((int)s_idle > 0x1D && !s_previewDone) {
        s_previewDone = true;
        const ExceedSong* e = curSong();
        if (e) requestPreview((int)e->id);
    }

    /* giro da roda */
    if (s_spinDir == 1) { if (++s_spin >= 0x10) { s_spinDir = 0; s_spin = 0; } }
    else if (s_spinDir == 2) { if (--s_spin < 1) { s_spinDir = 0; s_spin = 0; } }

    s_idle += 1.0f;

    playerInput(0);
    playerInput(1);
    /* ninguém segurando DL/DR: zera as repetições */
    bool hold = false;
    for (int p = 0; p < 2; p++)
        if ((s_joined & (1u << p)) && (Input_IsPadDown(p, PAD_DL) || Input_IsPadDown(p, PAD_DR)))
            hold = true;
    if (!hold) s_repeat = 0;

    if (s_time < 1) startGame();
}

void ZeroSelect_Render(void) {
    if (g_game.bgaPicCount < 4) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* SET: entrada da roda (0x806b641..0x806b6b5) */
    int setOff = BGA_SceneOffset(ZB_SELECT, "SET");
    int idxOff = 0;
    float py = 0.0f;
    if (setOff < 0x1E) {
        idxOff = -1;
        if (setOff == 0xF) { s_spinDir = 1; idxOff = 0; s_spin = 0; }
        else if (setOff > 0xF) idxOff = 0;
        py = (float)setOff * 1.6666666f - 50.0f;
    }
    drawWheel(s_spin, idxOff, 0.0f, py);

    zeroOrtho();
    Movie_RenderRect(0.0f, 175.0f, 640.0f, 415.0f, 1.0f, s_previewA);

    if (s_ch == EX_CH_ANOTHER && s_anotherChannel) {
        BGA_SetColor4(ZB_SELECT, 1, 1, 1, s_previewA);
        BGA_ScenePlay(ZB_SELECT, "ANOMARK", true);
        BGA_SetColor4(ZB_SELECT, 1, 1, 1, 1);
    }
    updateIcons(0);
    updateIcons(1);
    drawIcons(0);
    drawIcons(1);
    updateDigits();

    if (BGA_SceneOffset(ZB_SELECT, "SET") > 0x1C)
        BGA_ScenePlayAt(ZB_SELECT, "RIGHT", s_spin == 0xF ? 14 : s_spin);
    if (twoPlayers()) {
        BGA_ScenePlay(ZB_SELECT, "2PLAY SET", true);
        BGA_ScenePlay(ZB_SELECT, s_diffScene[0], true);
        BGA_ScenePlay(ZB_SELECT, s_diffScene[1], true);
    }
    BGA_ScenePlay(ZB_SELECT, "SET", true);
    if (BGA_SceneOffset(ZB_SELECT, "SET") > 0x1C) {
        BGA_ScenePlay(ZB_SELECT, s_moveFx, true);
        BGA_ScenePlay(ZB_SELECT, twoPlayers() ? (s_vs ? "1PVS2P" : "1P2P") : s_diffScene[0], true);
        BGA_ScenePlay(ZB_SELECT, s_chScene, true);
        BGA_ScenePlay(ZB_SELECT, "STEFFECT", true);
        BGA_ScenePlay(ZB_SELECT, "2PLEFT", true);
        BGA_ScenePlay(ZB_SELECT, "2PRIGHT", true);
    }
    if (s_bigIcon) BGA_ScenePlay(ZB_ICONALL, s_bigIcon, true);
    BGA_ScenePlay(ZB_SELECT3, "TIME", true);
    BGA_ScenePlay(ZB_SELECT3, twoPlayers() ? "2P_LV" : "1P_LV", true);
    if (s_ready) BGA_ScenePlay(ZB_SELECT3, "READY", true);
}
