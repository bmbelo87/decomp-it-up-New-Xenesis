/*
 * nx_training.c — TRAINING do Pump It Up NX (CSelectEz, piu NX).
 *
 * Classe CSelectEz (RTTI "9CSelectEz", vtable 0x8112728):
 *   Begin 0x807ee40, intro 0x807fc90, quadro 0x807ff60, End 0x8080400.
 *   Entra pela Station com o modo [0x81f8998] = 3 (g_game.nxGameMode).
 *
 * Recursos: [+4] ARCADE_SPECIAL.DAT (carrossel), [+8] COMMON.DAT, [+c] TRAINING.DAT
 * (cenas "lesson *", "part effect", rótulos tr_L-P no slot 6 + L*3 + P), [+10]
 * COMMAND.DAT, [+14] ARRO.DAT, BGA/TRAINING.MOV em loop; discos t-M01..20.tga do
 * TRT_KR.DAT / TRT_EN.DAT / TRT.DAT pelo idioma (0x80813d0).
 *
 * 20 lições x 3 partes. Cartão (0x8080b30): disco da lição no slot P e a marca da
 * parte escolhida dela (ARCADE 0x55/0x56/0x57 = +0x18c) no slot P+1. DL/DR trocam a
 * lição (cursor = lição, circular); UL/UR trocam a parte (EFF_MODE, "part effect",
 * nome da parte no slot 4 do TRAINING). Stage 2 e 3 (0x807ee64): a parte avança;
 * depois da 3ª, a lição seguinte.
 * Início (0x80812f0): EFF_START, "RUN T%02d%d -%c" (lição + 1, parte + 1, 'n' até a
 * lição 16, 'd' nas 17..20), chart 0x8143d30[(L+1)*3 + P+1], nível 0x8143e40[L*3+P]
 * (até 30), 2 corações.
 *
 * Não feito: textos fora do inglês/coreano
 * com a NXTW, cena SPORTS.DAT "training round" no gameplay.
 */
#include "pumpy.h"
#include "bga.h"
#include "movie.h"

enum { NT_AS = 0, NT_CM = 1, NT_TR = 2, NT_CMD = 3, NT_AR = 4 };
#define NT_LESSONS 20

extern const uint32_t g_nxTrainChart[64];
extern const uint32_t g_nxTrainLevel[64];
extern const char* const g_nxTrainDesc[4][20];
void NxText_Set(const char* str);
void NxText_StartScroll(void);
void NxText_Update(float dt);
void NxText_Draw(void);

/* SFX_SELECT.LUA */
enum { TS_MOVE, TS_MODE, TS_SELECT, TS_START, TS_HIDDEN, TS_COUNT };
static const char* const k_tsnd[TS_COUNT] = { "3-2.WAV", "13-1.WAV", "3-2.WAV", "START.WAV", "2-1.WAV" };
static int s_snd[TS_COUNT] = { -1, -1, -1, -1, -1 };

/* códigos de comando (nx_select.c) */
void NxCmd_Begin(void);
int  NxCmd_Push(int p, int button);
void NxCmd_Draw(unsigned joined);
void NxCmd_ToGame(void);
static void snd(int k) { if (s_snd[k] >= 0) Audio_Play(s_snd[k], false); }

int g_nxTrainN;
static int  s_lesson;               /* +0x314 (lembrado no crédito) */
static int  s_part[NT_LESSONS];     /* +0x274 */
static int  s_move;                 /* 0 parado, 1 L move, 2 R move */
static bool s_confirmed, s_started, s_autoStart;
static int  s_time;
static uint32_t s_t0;
static unsigned s_joined;
static int  s_tex[NT_LESSONS];      /* +0x2c4 */
static bool s_ok, s_partFx;
static const char* s_arro[4];
static BGALayerSrc s_mark[3];       /* +0x18c: ARCADE 0x55..0x57 */
static BGALayerSrc s_label[NT_LESSONS * 3];   /* +0x74: TRAINING 6 + L*3 + P */
static BGALayerSrc s_digit[10];

#define NPOS 20
static int s_posSlot[NPOS], s_posTile[NPOS], s_posCount;

static int wrap(int i) { i %= NT_LESSONS; return i < 0 ? i + NT_LESSONS : i; }

static int posTile(int slot) {
    for (int i = 0; i < s_posCount; i++) if (s_posSlot[i] == slot) return s_posTile[i];
    return -1;
}
static void makePosCopy(int slot) {
    BGALayerSrc src;
    if (s_posCount >= NPOS || !BGA_GetLayerSrc(NT_AS, slot, &src) || !src.isSPR || src.sprTileCount < 1) return;
    if (g_game.sprTileCount >= MAX_SPR_TILES) return;
    int t = g_game.sprTileCount++;
    g_game.sprTiles[t] = g_game.sprTiles[src.sprTileStart];
    src.sprTileStart = t; src.sprTileCount = 1;
    BGA_SetLayerSrc(NT_AS, slot, &src);
    s_posSlot[s_posCount] = slot; s_posTile[s_posCount] = t; s_posCount++;
}

/* 0x8080b30: disco no slot P, marca da parte da lição no P+1 */
static void fillCard(int pos, int idx) {
    idx = wrap(idx);
    int t = posTile(pos);
    if (t >= 0 && s_tex[idx] >= 0) g_game.sprTiles[t].texId = s_tex[idx];
    if (s_ok) BGA_SetLayerSrc(NT_AS, pos + 1, &s_mark[s_part[idx]]);
}
static void carousel(int dir) {
    int c = s_lesson;
    if (dir == 0) { fillCard(15, c); fillCard(7, c + 1); fillCard(11, c - 1); }
    else if (dir == 1) { fillCard(56, c - 1); fillCard(40, c - 1); fillCard(52, c); fillCard(44, c + 1); fillCard(48, c - 2); }
    else { fillCard(36, c + 1); fillCard(20, c + 1); fillCard(28, c + 2); fillCard(24, c - 1); fillCard(32, c); }
}

static void partLabel(void) {   /* slot 4 do TRAINING = nome da parte (0x807fbba) */
    if (s_ok) BGA_SetLayerSrc(NT_TR, 4, &s_label[s_lesson * 3 + s_part[s_lesson]]);
}

static void lessonText(void) {   /* 0x80810e0 */
    int lang = g_game.svcLangOption;
    if (lang < 0 || lang > 3) lang = 1;
    NxText_Set(g_nxTrainDesc[lang][s_lesson]);
    NxText_StartScroll();
}

/* ---------------------------------------------------------------------------
 * Início (0x80812f0)
 * ------------------------------------------------------------------------- */
static void startLesson(void) {
    if (s_started) return;
    s_started = true;
    snd(TS_START);
    int L = s_lesson, P = s_part[L];
    uint32_t id = g_nxTrainChart[(L + 1) * 3 + (P + 1)];
    int level = (int)g_nxTrainLevel[L * 3 + P];
    if (level > 30) level = 30;
    int diff = L < 16 ? 0 : 3;   /* 'n' / 'd' */
    g_nxTrainN = (L + 1) * 10 + (P + 1);
    g_game.nxHearts -= 2;        /* 0x80813be */
    Movie_Close();
    BGM_Stop();
    memset(&g_nxMods, 0, sizeof(g_nxMods));
    g_nxMods.skin = -1;
    Log_Print("TRAINING: RUN T%02d%d -%c -> %X (nível %d), corações %d\n", L + 1, P + 1, diff ? 'd' : 'n',
              (unsigned)id, level, g_game.nxHearts);
    if (!id || !ExSelect_StartMission((int)id, diff, level, s_joined)) {
        Log_Print("TRAINING: chart %X não existe\n", (unsigned)id);
        Game_ChangeState(STATE_GAMEOVER_ENTER);
    } else
        NxCmd_ToGame();   /* +0x494/+0x498 dos códigos (0x806b918 só soma os da missão) */
}

/* ---------------------------------------------------------------------------
 * Begin (0x807ee40)
 * ------------------------------------------------------------------------- */
void NxTraining_Enter(void) {
    Title_StopMusic();
    Movie_Close();
    BGM_Stop();
    s_joined = Title_GetJoinedMask() & 3;
    if (s_joined == 0) s_joined = 1;
    s_started = false;

    if (g_game.stageCount == 3) {           /* 0x807fc05: 1º stage */
        s_lesson = 0;
        memset(s_part, 0, sizeof(s_part));
    } else {                                /* 0x807ee64: próxima parte / lição */
        if (s_part[s_lesson] <= 1) s_part[s_lesson]++;
        else { s_part[s_lesson] = 0; s_lesson = wrap(s_lesson + 1); }
    }

    Resource_ClearBGA();
    static const char* const k_bga[5] = { "ARCADE_SPECIAL", "COMMON", "TRAINING", "COMMAND", "ARRO" };
    for (int i = 0; i < 5; i++)
        if (!Resource_LoadBGAByName(k_bga[i])) Log_Print("TRAINING: falha ao carregar BGA\\%s.DAT\n", k_bga[i]);
    BGA_Reset();
    s_ok = g_game.bgaPicCount >= 5;

    if (s_ok) {
        for (int i = 0; i < 3; i++) s_ok &= BGA_GetLayerSrc(NT_AS, 0x55 + i, &s_mark[i]);
        for (int i = 0; i < NT_LESSONS * 3 && s_ok; i++) s_ok &= BGA_GetLayerSrc(NT_TR, 6 + i, &s_label[i]);
        s_ok &= BGA_GetLayerSrc(NT_CM, 0x52, &s_digit[0]);
        for (int i = 1; i < 10; i++) s_ok &= BGA_GetLayerSrc(NT_CM, 0x58 + i, &s_digit[i]);
        /* molduras: ARCADE 0x17 em todas (0x807f63e..0x807f7b2) */
        BGALayerSrc fr;
        if (BGA_GetLayerSrc(NT_AS, 0x17, &fr)) {
            static const int k_fr[13] = { 0xe, 6, 0xa, 0x37, 0x27, 0x33, 0x2b, 0x2f, 0x23, 0x13, 0x1b, 0x17, 0x1f };
            for (int i = 0; i < 13; i++) BGA_SetLayerSrc(NT_AS, k_fr[i], &fr);
        }
        /* 0x807f8a3..0x807f901: painel de cima = COMMON slot 9 nos slots 0 e 1 do TRAINING
         * (o channel4.spr não existe dentro do TRAINING.DAT) */
        BGALayerSrc pn;
        if (BGA_GetLayerSrc(NT_CM, 9, &pn)) { BGA_SetLayerSrc(NT_TR, 0, &pn); BGA_SetLayerSrc(NT_TR, 1, &pn); }
        /* setas Prev/Next (0x807f43a..0x807f61f) */
        static const int k_arro[8] = { 0x1b, 0x1d, 0x1c, 0x1e, 0x14, 0x19, 0x15, 0x1a };
        for (int i = 0; i < 8; i++) { BGALayerSrc a; if (BGA_GetLayerSrc(NT_AR, k_arro[i], &a)) BGA_SetLayerSrc(NT_AR, i, &a); }
    }
    if (!s_ok) Log_Print("TRAINING: alguma camada de origem não existe\n");

    s_posCount = 0;
    static const int k_pos[] = { 15, 7, 11, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56 };
    for (size_t i = 0; i < sizeof(k_pos) / sizeof(k_pos[0]); i++) makePosCopy(k_pos[i]);

    /* 0x80813d0: discos pelo idioma */
    char path[MAX_PATH];
    const char* dat = g_game.svcLangOption == 0 ? "TRT_KR" : g_game.svcLangOption == 1 ? "TRT_EN" : "TRT";
    for (int i = 0; i < NT_LESSONS; i++) s_tex[i] = -1;
    snprintf(path, sizeof(path), "%s/BGA/%s.DAT", g_game.currentDirectory, dat);
    if (RES_Open(path)) {
        for (int i = 0; i < NT_LESSONS; i++) {
            char nm[16];
            snprintf(nm, sizeof(nm), "t-M%02d.tga", i + 1);
            s_tex[i] = loadTextureFromRES(nm);
        }
        RES_Close();
    } else Log_Print("TRAINING: '%s' não abriu\n", path);

    for (int k = 0; k < TS_COUNT; k++) if (s_snd[k] < 0) s_snd[k] = Audio_LoadWaveFile(k_tsnd[k]);

    snprintf(path, sizeof(path), "%s/BGA/TRAINING.MOV", g_game.currentDirectory);
    Movie_Open(path, true);

    BGA_SceneReset(NT_TR, "lesson start");
    BGA_SceneReset(NT_TR, "lesson font star");
    BGA_SceneReset(NT_CM, "channel start");
    BGA_SceneReset(NT_CM, "channel text start");
    BGA_SceneReset(NT_AS, "screen2 start");
    BGA_SceneReset(NT_AR, "arro start");

    s_move = 0;
    s_confirmed = false;
    s_partFx = false;
    s_arro[0] = "arroUL click"; s_arro[1] = "arroUR click"; s_arro[2] = "arroHL click"; s_arro[3] = "arroHR click";
    s_time = 0x5a;
    s_t0 = timeGetTime();
    carousel(0);
    partLabel();
    lessonText();
    NxCmd_Begin();
    Log_Print("TRAINING: Begin, lição %d parte %d\n", s_lesson + 1, s_part[s_lesson] + 1);
}

/* ---------------------------------------------------------------------------
 * Entrada (0x8080500)
 * ------------------------------------------------------------------------- */
static void moveLesson(int dir) {   /* 0x8081090 / 0x80810b0 */
    s_confirmed = false;
    s_move = dir;
    BGA_SceneReset(NT_AS, dir == 1 ? "screen2 L move" : "screen2 R move");
    s_lesson = wrap(s_lesson + (dir == 1 ? 1 : -1));
    carousel(dir);
    partLabel();
    lessonText();
    BGA_SceneReset(NT_TR, "lesson font star");
    snd(TS_MOVE);
}

static void movePart(int d) {      /* 0x8081030 / 0x8081060 */
    s_confirmed = false;
    s_part[s_lesson] = (s_part[s_lesson] + d + 3) % 3;
    partLabel();
    carousel(0);
    BGA_SceneReset(NT_TR, "part effect");
    s_partFx = true;
    snd(TS_MODE);
}

static void playerInput(int p) {
    if (!(s_joined & (1u << p))) return;
    /* 0x8080a29..0x8080aad: cada painel vai para o histórico (0x804d200);
     * 0x8080198..0x80801fb: código achado -> +0x1bc = 0 e 0x807c050. O som
     * EFF_HIDDEN_SELECTED só existe no bloco do P1 (0x80801ba); o P2 fica mudo. */
    static const PadButton k_pad[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
    for (int k = 0; k < 5; k++)
        if (Input_IsPadHit(p, k_pad[k]) && NxCmd_Push(p, 7 + k) >= 0) {
            s_confirmed = false;
            /* era: if (p == 0) snd(TS_HIDDEN);  (0x80801ba só no P1) — pedido: toca para P1 e P2 */
            snd(TS_HIDDEN);
        }
    if (Input_IsPadHit(p, PAD_C)) {
        if (s_confirmed) { s_time = 0; snd(TS_SELECT); }
        else {
            s_confirmed = true;
            if (s_move) { s_move = 0; carousel(0); }
            BGA_SceneReset(NT_CM, "center step");
            BGA_SceneReset(NT_AS, "screen2 click");
            snd(TS_SELECT);
        }
    }
    if (Input_IsPadHit(p, PAD_DR)) { BGA_SceneReset(NT_AR, "arroUR click"); moveLesson(1); }
    if (Input_IsPadHit(p, PAD_DL)) { BGA_SceneReset(NT_AR, "arroUL click"); moveLesson(2); }
    if (Input_IsPadHit(p, PAD_UR)) { BGA_SceneReset(NT_AR, "arroHR click"); movePart(1); }
    if (Input_IsPadHit(p, PAD_UL)) { BGA_SceneReset(NT_AR, "arroHL click"); movePart(-1); }
}

/* ---------------------------------------------------------------------------
 * Quadro (0x807ff60)
 * ------------------------------------------------------------------------- */
void NxTraining_Update(float dt) {
    if (s_started) return;
    Movie_Update(dt);
    NxText_Update(dt);
    s_time = s_confirmed && s_time == 0 ? 0 : 0x5a - (int)((timeGetTime() - s_t0) / 1000);
    if (s_time < 0) s_time = 0;
    playerInput(0);
    playerInput(1);
    if (s_move != 0 && BGA_SceneDone(NT_AS, s_move == 1 ? "screen2 L move" : "screen2 R move")) {
        s_move = 0;
        carousel(0);
    }
    if (s_time <= 0) startLesson();
}

void NxTraining_Render(void) {
    if (s_started || g_game.bgaPicCount < 5) return;
    Movie_Render();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);

    /* lição: entrada e repouso, nome animado, efeito da parte */
    if (!BGA_SceneDone(NT_TR, "lesson start")) BGA_ScenePlay(NT_TR, "lesson start", true);
    else BGA_ScenePlay(NT_TR, "lesson hold", true);
    BGA_ScenePlay(NT_TR, "lesson font star", true);
    if (s_partFx) { BGA_ScenePlay(NT_TR, "part effect", true); if (BGA_SceneDone(NT_TR, "part effect")) s_partFx = false; }

    /* carrossel */
    const char* sc = s_move == 1 ? "screen2 L move" : s_move == 2 ? "screen2 R move" :
                     !BGA_SceneDone(NT_AS, "screen2 start") ? "screen2 start" :
                     s_confirmed ? "screen2 click" : "screen2 hold";
    BGA_ScenePlay(NT_AS, sc, true);

    /* era: painel de cima pelo COMMON "channel start"/"channel hold" (cor azul errada);
     * no original ele é o slot 0/1 do TRAINING nas cenas "lesson start/hold".
    if (!BGA_SceneDone(NT_CM, "channel start")) BGA_ScenePlay(NT_CM, "channel start", true);
    else BGA_ScenePlay(NT_CM, "channel hold", true);
    */
    /* faixa, tempo, setas */
    BGA_ScenePlay(NT_CM, "channel text start", true);
    if (s_ok) {
        BGA_SetLayerSrc(NT_CM, 0x58, &s_digit[s_time % 10]);
        BGA_SetLayerSrc(NT_CM, 0x57, &s_digit[(s_time / 10) % 10]);
    }
    BGA_ScenePlay(NT_CM, "time position", true);
    if (s_confirmed) BGA_ScenePlay(NT_CM, "center step", true);
    NxCmd_Draw(s_joined);   /* ícones dos códigos (COMMAND.DAT, 0x807bf40) */
    if (!BGA_SceneDone(NT_AR, "arro start")) BGA_ScenePlay(NT_AR, "arro start", true);
    for (int i = 0; i < 4; i++) BGA_ScenePlay(NT_AR, s_arro[i], true);

    NxText_Draw();   /* 0x808012d */
    glColor4f(1, 1, 1, 1);
}
