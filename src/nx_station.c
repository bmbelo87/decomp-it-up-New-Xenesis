/*
 * nx_station.c — Station Select do Pump It Up NX (CStation, piu NX).
 *
 * Classe CStation (RTTI "8CStation", vtable 0x081148e8): Begin 0x808a7f0,
 * quadro 0x808b300 (+ saída 0x808b5d0), End 0x808b770, entrada 0x808ba00.
 * Não carrega BGA próprio: usa os objetos que a CTitle (0x808c610) deixou —
 * [+4] = BGA/COMMON.DAT (cards "screen *", canal, nome, tempo) e
 * [+8] = BGA/ARRO.DAT (setas; slots 0/2/4/6 recebem as "Prev./Next" 18..21).
 *
 * Estações (+0x12c) e modo de jogo [0x81f8998] (tabela 0x8114978 = {3, 0, 2, 1}):
 *   0 TRAINING (text_tra) -> 3     1 ARCADE (text_arc) -> 0
 *   2 WORLD TOUR (text_wor) -> 2   3 SPECIAL ZONE (text_spe) -> 1
 * Com 2 jogadores TRAINING e WORLD dão EFF_WRONG e saem como ARCADE (0x808b6e0).
 *
 * SPECIAL ZONE (modo 1) usa a mesma CSelect (nx_select.c) com g_game.nxGameMode = 1.
 * Diferenças conhecidas: TRAINING (CSelectEz) e WORLD TOUR (CSelectWorld) ainda não
 * existem no projeto -> vão para a CSelect do arcade (modo 0).
 */
#include "pumpy.h"
#include "bga.h"
#include "movie.h"

static int NS_COMMON = 0, NS_ARRO = 1;   /* índices dos BGAs (os da CTitle, se existirem) */

static int findBGA(const char* name) {
    for (int i = 0; i < g_game.bgaPicCount; i++)
        if (_stricmp(g_game.bgaPics[i].name, name) == 0) return i;
    return -1;
}

/* SFX_GLOBAL.LUA / SFX_SELECT.LUA */
enum { SS_PUSH, SS_SELECT, SS_WRONG, SS_JOIN, SS_START, SS_STATION, SS_TIME, SS_COUNT };
static const char* const k_snd[SS_COUNT] = {
    "3-2.WAV", "3-2.WAV", "WRONG.WAV", "PUSHPANEL.WAV", "START.WAV", "T2_01.WAV", "TIME_LIMIT.WAV",
};
static int s_snd[SS_COUNT] = { -1, -1, -1, -1, -1, -1, -1 };
static void snd(int k) { if (s_snd[k] >= 0) Audio_Play(s_snd[k], false); }

static int  s_st = 1;            /* +0x12c, lembrado ([0x9e3dbca]) */
static int  s_anim, s_animPrev;  /* +0x124 / +0x128: 0 parado, 1 L move, 2 R move */
static bool s_confirmed;         /* +0x120 */
static unsigned s_joined;
static int  s_time, s_timePrev;  /* +0x138 / +0x13c: 0x19 - segundos */
static uint32_t s_t0;            /* +0x134 */
static int  s_phase;             /* 0 entrada, 1 seleção, 2 saída */
static int  s_exitFrames;        /* +0xc (float no original) */

static BGALayerSrc s_card[4];    /* +0x38..+0x44: COMMON 37, 40, 38, 39 */
static BGALayerSrc s_text[4];    /* +0x48..+0x54: COMMON 53, 52, 54, 55 */
static BGALayerSrc s_bar[4];     /* +0x58/+0x60/+0x68/+0x70: COMMON 9, 1, 8, 7 (channel4/1/3/2, 0x808ac60..0x808ad1d) */
static BGALayerSrc s_name[4];    /* +0x5c/+0x64/+0x6c/+0x74: COMMON 81, 65, 80, 66 */
static BGALayerSrc s_digit[10];  /* +0x10..: COMMON 82, 89..97 */
static BGALayerSrc s_only[2];    /* +0x78 / +0x7c: COMMON 79 (T-only), 78 (W-only) */
static BGALayerSrc s_arro[4];    /* +0x84..+0x90: ARRO 18..21 */

/* 0x808af2a / 0x808bafc: 2 jogadores ou EVENT -> cards de TRAINING e WORLD viram "only" */
static bool singleOnly(void) { return (s_joined & 3) == 3 || g_game.svcGameMode == 1; }
static void onlyCards(void) {
    if (!singleOnly()) return;
    s_card[0] = s_only[0];
    s_card[2] = s_only[1];
}

static int st(int k) { return ((s_st + k) % 4 + 4) % 4; }

/* 0x808b810 */
static void applyStation(void) {
    BGA_SetLayerSrc(NS_COMMON, 0x34, &s_text[s_st]);
    BGA_SetLayerSrc(NS_COMMON, 1, &s_bar[s_st]);
    BGA_SetLayerSrc(NS_COMMON, 4, &s_name[s_st]);
    BGA_SetLayerSrc(NS_COMMON, 5, &s_name[s_st]);
}

/* 0x808bf90(this, animação) */
static void cards(int anim) {
    if (anim == 0) {
        BGA_SetLayerSrc(NS_COMMON, 0x23, &s_card[st(0)]);
        BGA_SetLayerSrc(NS_COMMON, 0x21, &s_card[st(1)]);
        BGA_SetLayerSrc(NS_COMMON, 0x22, &s_card[st(3)]);
    } else if (anim == 2) {   /* "screen R move" */
        BGA_SetLayerSrc(NS_COMMON, 0x28, &s_card[st(1)]);
        BGA_SetLayerSrc(NS_COMMON, 0x24, &s_card[st(1)]);
        BGA_SetLayerSrc(NS_COMMON, 0x26, &s_card[st(2)]);
        BGA_SetLayerSrc(NS_COMMON, 0x25, &s_card[st(3)]);
        BGA_SetLayerSrc(NS_COMMON, 0x27, &s_card[st(0)]);
    } else {                  /* "screen L move" */
        BGA_SetLayerSrc(NS_COMMON, 0x2d, &s_card[st(3)]);
        BGA_SetLayerSrc(NS_COMMON, 0x29, &s_card[st(3)]);
        BGA_SetLayerSrc(NS_COMMON, 0x2c, &s_card[st(0)]);
        BGA_SetLayerSrc(NS_COMMON, 0x2a, &s_card[st(1)]);
        BGA_SetLayerSrc(NS_COMMON, 0x2b, &s_card[st(2)]);
    }
}

void NxStation_Enter(void) {
    /* A CStation não carrega BGA: usa os objetos da CTitle (0x808c661 / 0x808c67c).
     * Só carrega aqui se não vier da Title (ex.: PUMPY_AUTOSTATE). O EFF_TITLE segue. */
    Movie_Select(1); Movie_Close(); Movie_Select(0);
    BGM_Stop();
    NS_COMMON = findBGA("COMMON");
    NS_ARRO = findBGA("ARRO");
    if (NS_COMMON < 0 || NS_ARRO < 0) {
        Resource_ClearBGA();
        if (!Resource_LoadBGAByName("COMMON")) Log_Print("NXSTATION: falha ao carregar BGA\\COMMON.DAT\n");
        if (!Resource_LoadBGAByName("ARRO"))   Log_Print("NXSTATION: falha ao carregar BGA\\ARRO.DAT\n");
        NS_COMMON = findBGA("COMMON");
        NS_ARRO = findBGA("ARRO");
    }
    BGA_Reset();

    bool ok = true;
    static const int k_card[4] = { 37, 40, 38, 39 }, k_text[4] = { 53, 52, 54, 55 };
    static const int k_bar[4] = { 9, 1, 8, 7 }, k_name[4] = { 81, 65, 80, 66 };
    for (int i = 0; i < 4; i++) {
        ok &= BGA_GetLayerSrc(NS_COMMON, k_card[i], &s_card[i]);
        ok &= BGA_GetLayerSrc(NS_COMMON, k_text[i], &s_text[i]);
        ok &= BGA_GetLayerSrc(NS_COMMON, k_bar[i], &s_bar[i]);
        ok &= BGA_GetLayerSrc(NS_COMMON, k_name[i], &s_name[i]);
        ok &= BGA_GetLayerSrc(NS_ARRO, 18 + i, &s_arro[i]);
    }
    ok &= BGA_GetLayerSrc(NS_COMMON, 79, &s_only[0]);
    ok &= BGA_GetLayerSrc(NS_COMMON, 78, &s_only[1]);
    ok &= BGA_GetLayerSrc(NS_COMMON, 82, &s_digit[0]);
    for (int i = 1; i < 10; i++) ok &= BGA_GetLayerSrc(NS_COMMON, 88 + i, &s_digit[i]);
    if (!ok) Log_Print("NXSTATION: alguma camada de origem não existe\n");

    /* setas "Prev./Next": ARRO 0/2/4/6 <- 18..21 (0x808ab68..0x808abc2) */
    for (int i = 0; i < 4; i++) BGA_SetLayerSrc(NS_ARRO, i * 2, &s_arro[i]);

    for (int k = 0; k < SS_COUNT; k++)
        if (s_snd[k] < 0) s_snd[k] = Audio_LoadWaveFile(k_snd[k]);

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/BGA/BG.MOV", g_game.currentDirectory);
    Movie_Open(path, true);

    s_joined = Title_GetJoinedMask() & 3;
    if (s_joined == 0) s_joined = 1;
    /* era: if (s_st < 0 || s_st > 3) s_st = 1;  (lembrava a última escolha) */
    /* 0x808ad7c: todo Begin parte do DEFAULT STATION do Service Menu ([0x9e3dbca],
     * EEPROM +0xECA): 1, 2, 3 ou 0 para qualquer outro valor */
    {
        uint32_t def = Eeprom_Get32(0xECA);
        s_st = (def >= 1 && def <= 3) ? (int)def : 0;
    }
    s_anim = s_animPrev = 0;
    s_confirmed = false;
    s_phase = 0;
    s_exitFrames = 0;
    s_time = s_timePrev = 0x19;
    s_t0 = timeGetTime();
    static const char* const k_c[] = { "channel start", "channel text start", "screen start", "name text start" };
    for (size_t i = 0; i < sizeof(k_c) / sizeof(k_c[0]); i++) BGA_SceneReset(NS_COMMON, k_c[i]);
    BGA_SceneReset(NS_ARRO, "arro start");
    applyStation();
    onlyCards();
    cards(0);
    snd(SS_STATION);
    Log_Print("NXSTATION: estação %d, P%s\n", s_st, (s_joined & 3) == 3 ? "1+P2" : (s_joined & 2) ? "2" : "1");
}

static void move(int dir) {   /* +1: UR/DR (L move), -1: UL/DL (R move) */
    snd(SS_PUSH);
    s_confirmed = false;
    s_animPrev = s_anim;
    s_anim = dir > 0 ? 1 : 2;
    s_st = st(dir);
    BGA_SceneReset(NS_COMMON, dir > 0 ? "screen L move" : "screen R move");
    if (s_animPrev != s_anim) {
        BGA_SceneReset(NS_COMMON, "name text end");
        BGA_SceneReset(NS_COMMON, "name text start");
    }
    cards(s_anim);
}

static void playerInput(int p) {   /* 0x808ba00 */
    bool joined = (s_joined & (1u << p)) != 0;
    if (!joined) {
        if (Input_IsPadHit(p, PAD_C) && Coin_HasCredit()) {
            Coin_ConsumeCredit();
            s_joined |= 1u << p;
            snd(SS_JOIN);
            onlyCards();
            cards(s_anim);
        }
        return;
    }
    if (Input_IsPadHit(p, PAD_UR)) { BGA_SceneReset(NS_ARRO, "arroHR click"); move(+1); }
    if (Input_IsPadHit(p, PAD_DR)) { BGA_SceneReset(NS_ARRO, "arroUR click"); move(+1); }
    if (Input_IsPadHit(p, PAD_UL)) { BGA_SceneReset(NS_ARRO, "arroHL click"); move(-1); }
    if (Input_IsPadHit(p, PAD_DL)) { BGA_SceneReset(NS_ARRO, "arroUL click"); move(-1); }
    if (Input_IsPadHit(p, PAD_C)) {
        if (s_confirmed) { s_time = 0; snd(SS_SELECT); return; }
        /* 0x808bbe3: TRAINING/WORLD com 2 jogadores -> EFF_WRONG */
        /* era: tocava EFF_WRONG mas confirmava e saía como ARCADE.
         * 0x808bbec..0x808bc37: com 2 jogadores ou EVENT só toca o EFF_WRONG, sem confirmar. */
        if ((s_st == 0 || s_st == 2) && singleOnly()) { snd(SS_WRONG); return; }
        snd(SS_PUSH);
        s_confirmed = true;
        if (s_anim != 0) { s_anim = 0; applyStation(); cards(0); }
        BGA_SceneReset(NS_COMMON, "screen click");
        BGA_SceneReset(NS_COMMON, "center step");
    }
}

static void leave(void) {   /* 0x808b68e */
    int st0 = s_st;
    if ((s_joined & 3) == 3 && (st0 == 0 || st0 == 2)) st0 = 1;
    static const int k_mode[4] = { 3, 0, 2, 1 };   /* 0x8114978 */
    if (0)
        Log_Print("NXSTATION: modo %d (estação %d) ainda não existe, indo para o ARCADE\n", k_mode[st0], st0);
    Movie_Close();
    BGM_Stop();
    Title_SetJoinedMask(s_joined);
    Menu_ResetState();
    /* 0x808b6b7: [0x81f8998] = tabela[estação]. TRAINING (3) ainda não existe aqui. */
    g_game.nxGameMode = k_mode[st0];
    Game_ChangeState(STATE_EXSELECT);
}

void Station_Update(float dt) {
    Movie_Update(dt);
    if (s_phase == 2) {
        if (++s_exitFrames > 0x27) leave();
        return;
    }
    if (s_phase == 0 && BGA_SceneDone(NS_COMMON, "screen start")) { s_phase = 1; s_t0 = timeGetTime(); }
    if (s_phase == 1) {
        s_timePrev = s_time;
        if (s_time > 0) s_time = 0x19 - (int)((timeGetTime() - s_t0) / 1000);
        if (s_time < 0) s_time = 0;
        if (s_time != s_timePrev && s_time > 0 && s_time < 11) snd(SS_TIME);
        playerInput(0);
        playerInput(1);
        if (s_anim != 0 && BGA_SceneDone(NS_COMMON, s_anim == 1 ? "screen L move" : "screen R move")) {
            s_anim = 0;
            applyStation();
            cards(0);
        }
        if (s_time <= 0) {   /* 0x808b4e8 */
            snd(SS_START);
            s_phase = 2;
            s_exitFrames = 0;
            static const char* const k_end[] = { "screen end", "channel text end", "channel end" };
            for (size_t i = 0; i < 3; i++) BGA_SceneReset(NS_COMMON, k_end[i]);
            BGA_SceneReset(NS_ARRO, "arro end");
        }
    }
}

void Station_Render(void) {
    if (NS_COMMON < 0 || NS_ARRO < 0) return;
    Movie_Render();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (s_phase == 2) {   /* 0x808b5d0 */
        BGA_ScenePlay(NS_COMMON, "screen end", true);
        BGA_ScenePlay(NS_COMMON, "channel text end", true);
        BGA_ScenePlay(NS_ARRO, "arro end", true);
        BGA_ScenePlay(NS_COMMON, "channel end", true);
        /* escurece até preto antes da próxima tela */
        float a = (float)s_exitFrames / 0x27;
        if (a > 1.0f) a = 1.0f;
        glDisable(GL_TEXTURE_2D);
        glColor4f(0, 0, 0, a);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(640, 0); glVertex2f(640, 480); glVertex2f(0, 480);
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        return;
    }
    /* canal */
    BGA_ScenePlay(NS_COMMON, BGA_SceneDone(NS_COMMON, "channel start") ? "channel hold" : "channel start", true);
    BGA_ScenePlay(NS_COMMON, BGA_SceneDone(NS_COMMON, "channel text start") ? "channel text hold" : "channel text start", true);
    /* setas do ARRO */
    if (!BGA_SceneDone(NS_ARRO, "arro start")) BGA_ScenePlay(NS_ARRO, "arro start", true);
    else {
        BGA_ScenePlay(NS_ARRO, "arroHL click", true);
        BGA_ScenePlay(NS_ARRO, "arroHR click", true);
        BGA_ScenePlay(NS_ARRO, "arroUL click", true);
        BGA_ScenePlay(NS_ARRO, "arroUR click", true);
    }
    /* tempo (0x808c510) */
    BGA_SetLayerSrc(NS_COMMON, 0x58, &s_digit[s_time % 10]);
    BGA_SetLayerSrc(NS_COMMON, 0x57, &s_digit[(s_time / 10) % 10]);
    BGA_ScenePlay(NS_COMMON, "time position", true);
    /* nome e cards (0x808b445..0x808b4a5 / 0x808b520): primeiro o texto do nome
     * ([+0xe0] start parado, [+0xe8] end na troca), depois o card ([+0xb0] hold,
     * [+0xa8] L move, [+0xac] R move); confirmado, "screen click" [+0xb8] e
     * "center step" [+0x11c] por cima do hold. */
    if (s_phase == 0) {
        BGA_ScenePlay(NS_COMMON, "name text start", true);
        BGA_ScenePlay(NS_COMMON, "screen start", true);
    } else {
        BGA_ScenePlay(NS_COMMON, s_anim != 0 ? "name text end" : "name text start", true);
        BGA_ScenePlay(NS_COMMON, s_anim == 1 ? "screen L move" : s_anim == 2 ? "screen R move" : "screen hold", true);
        if (s_confirmed) {
            BGA_ScenePlay(NS_COMMON, "screen click", true);
            BGA_ScenePlay(NS_COMMON, "center step", true);
        }
    }
    /* era: start -> "name text hold" depois dos cards; "screen click" no lugar do hold */
    glColor4f(1, 1, 1, 1);
}
