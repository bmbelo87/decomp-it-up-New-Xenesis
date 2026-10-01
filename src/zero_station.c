/*
 * zero_station.c — CStation do Pump It Up Zero (piu, proc "STATION").
 *
 * O CTitle (0x8066fb0) carrega BGA/82.DAT e BGA/STATION.DAT e deixa o
 * BGA/CREDIT.MOV e o EFF_TITLE (WAVE/TITLE.WAV) tocando; a Station desenha o
 * mesmo vídeo de fundo (0x80a38f0).
 *
 *   0x80688a0 Begin  contador 20 (99 no modo evento), seleção pelo PIUZERO.INI
 *                    +0xED2 (1 ARCADE, 2 REMIX, outro EASY; o padrão do INI é
 *                    GAMESTATION=1), salva os sprites dos slots 3/4, 6/7, 10/11,
 *                    13/14 (EASY, ARCADE, MISSION, REMIX) e dos dígitos 0x5A..0x63.
 *   0x8068b20 fase 1 BG, top_down, in, setas, TIME; fim de "in" -> fase 2.
 *   0x8068cb0 fase 2 bg, top_down, hold/pan_left/pan_right, <ESTAÇÃO>_SEL depois
 *                    da confirmação, setas, TIME, 1PMISSION; contador < 1 ->
 *                    EFF_STATION_OUT e fase 3.
 *   0x80690c0 fase 3 bg, fade preto (c / 40), top_up, out; c >= 40 -> proc
 *                    (EASY "SELECTEZ", ARCADE/REMIX "SELECT", MISSION "SELECTMS").
 *   0x80694f0 entrada DL/DR (EFF_PUSH, pan), C (EFF_PUSH; 1º aperto toca a voz
 *                    da estação, 2º zera o contador), entrada tardia (EFF_JOIN).
 *   0x8069b30 qual sprite vai em cada slot. Com 2 jogadores (ou evento) a
 *                    MISSION sai da roda.
 *
 * Sons (SFX_TITLE.LUA / SFX_GLOBAL.LUA): EFF_PUSH 3-2, EFF_JOIN PUSHPANEL,
 * EFF_STATION_SELECT T2_01, EFF_ARCADE_STATION T2_02, EFF_REMIX_STATION T2_03,
 * EFF_MISSION_STATION T2_04, EFF_EASY_STATION T2_05, EFF_STATION_OUT SELECTED,
 * EFF_TIME_LIMIT TIME_LIMIT.
 *
 * EASY (CSelectEz) e MISSION (CSelectMission) ainda não existem no projeto:
 * por enquanto seguem para a Select do ARCADE.
 */
#include "pumpy.h"
#include "bga.h"
#include "movie.h"

#define ST_BGA 0

enum { ST_EASY = 0, ST_ARCADE = 1, ST_MISSION = 2, ST_REMIX = 3, ST_COUNT = 4 };

static int  g_phase;          /* 1 in, 2 principal, 3 saída */
static int  g_sel;            /* +0x68 */
static int  g_anim;           /* +0x64: 0 hold, 1 pan_left, 2 pan_right */
static bool g_confirmed;      /* +0x60 */
static int  g_timer;          /* +0x74 */
static int  g_prevTimer;      /* +0x78 */
static DWORD g_startMs;       /* +0x70 */
static int  g_fade;           /* +0x14 (fase 3) */
static int  g_fadeDraw;
static unsigned g_joined;

static BGALayerSrc g_item[ST_COUNT][2];   /* +0x18..+0x34 */
static BGALayerSrc g_digit[10];           /* +0x38..: slots 0x5A..0x63 */
static bool g_itemsOk;

enum { ZS_PUSH, ZS_JOIN, ZS_SELECT, ZS_ARCADE, ZS_REMIX, ZS_MISSION, ZS_EASY,
       ZS_OUT, ZS_TIME, ZS_COUNT };
static const char* const k_snd[ZS_COUNT] = {
    "3-2.WAV", "PUSHPANEL.WAV", "T2_01.WAV", "T2_02.WAV", "T2_03.WAV", "T2_04.WAV",
    "T2_05.WAV", "SELECTED.WAV", "TIME_LIMIT.WAV",
};
static int g_snd[ZS_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static void sfx(int k) { if (g_snd[k] >= 0) Audio_Play(g_snd[k], false); }

/* desenho adiado (ver station.c do Exceed2): guarda quadro e layout por cena */
#define ST_MAX_DRAW 16
static int  g_drawFrames[ST_MAX_DRAW];
static int  g_drawItems[ST_MAX_DRAW][4];
static int  g_drawTimer[ST_MAX_DRAW];
static int  g_drawCount;
static int  g_slotItem[4];
static const int k_slots[4] = { 3, 6, 10, 13 };

static void playScene(const char* name) {
    int f = BGA_SceneFrame(ST_BGA, name);
    if (f >= 0 && f < 30000 && g_drawCount < ST_MAX_DRAW) {
        g_drawFrames[g_drawCount] = f;
        memcpy(g_drawItems[g_drawCount], g_slotItem, sizeof(g_slotItem));
        g_drawTimer[g_drawCount] = g_timer;
        g_drawCount++;
    }
    BGA_ScenePlay(ST_BGA, name, false);
}

static bool missionOut(void) {
    /* 0x80694f0: evento (INI +0xED6 == 1, ainda não guardado) ou 2 jogadores */
    return (g_joined & 3) == 3;
}

static int stepSel(int s, int dir) {
    do {
        s += dir;
        if (s < 0) s = ST_COUNT - 1;
        if (s >= ST_COUNT) s = 0;
    } while (s == ST_MISSION && missionOut());
    return s;
}

/* 0x8069b30: esquerda = anterior, centro = atual, direita = próxima, trás =
 * a seguinte; durante o pan_left (anim 1) o layout ainda é o da anterior. */
static void setLayout(int anim) {
    int base = (anim == 1) ? stepSel(g_sel, -1) : g_sel;
    g_slotItem[0] = stepSel(base, -1);
    g_slotItem[1] = base;
    g_slotItem[2] = stepSel(base, +1);
    g_slotItem[3] = stepSel(g_slotItem[2], +1);
}

static void applyDraw(int i) {
    if (!g_itemsOk) return;
    for (int k = 0; k < 4; k++) {
        BGA_SetLayerSrc(ST_BGA, k_slots[k], &g_item[g_drawItems[i][k]][0]);
        BGA_SetLayerSrc(ST_BGA, k_slots[k] + 1, &g_item[g_drawItems[i][k]][1]);
    }
    /* 0x806a120: dezena no slot 0x57, unidade no 0x58 */
    int t = g_drawTimer[i] < 0 ? 0 : g_drawTimer[i];
    BGA_SetLayerSrc(ST_BGA, 0x58, &g_digit[t % 10]);
    BGA_SetLayerSrc(ST_BGA, 0x57, &g_digit[(t / 10) % 10]);
}

static const char* selScene(void) {
    static const char* const k[ST_COUNT] = { "EASY_SEL", "ARCADE_SEL", "MISSION_SEL", "REMIX_SEL" };
    return k[g_sel];
}

static void Station_Enter(void) {
    /* o CTitle já deixou o STATION.DAT carregado; se não, carrega aqui */
    if (g_game.bgaPicCount <= 0 || _stricmp(g_game.bgaPics[0].name, "STATION") != 0) {
        Resource_ClearBGA();
        if (!Resource_LoadBGAByName("STATION"))
            Log_Print("STATION: falha ao carregar BGA\\STATION.DAT\n");
    }
    BGA_Reset();

    g_itemsOk = true;
    for (int s = 0; s < ST_COUNT; s++) {
        g_itemsOk &= BGA_GetLayerSrc(ST_BGA, k_slots[s], &g_item[s][0]);
        g_itemsOk &= BGA_GetLayerSrc(ST_BGA, k_slots[s] + 1, &g_item[s][1]);
    }
    for (int d = 0; d < 10; d++) g_itemsOk &= BGA_GetLayerSrc(ST_BGA, 0x5A + d, &g_digit[d]);
    if (!g_itemsOk) Log_Print("STATION: slots do STATION.DAT incompletos\n");

    for (int k = 0; k < ZS_COUNT; k++)
        if (g_snd[k] < 0) g_snd[k] = Audio_LoadWaveFile(k_snd[k]);

    g_joined = Title_GetJoinedMask() & 3;
    if (g_joined == 0) g_joined = 1;
    g_sel = ST_ARCADE;           /* INI +0xED2 = GAMESTATION (padrão 1) */
    g_anim = 0;
    g_confirmed = false;
    g_timer = g_prevTimer = 20;
    g_fade = g_fadeDraw = 0;
    setLayout(0);
    g_startMs = timeGetTime();
    g_phase = 1;
}

static bool hit(int p, PadButton b) {
    return (g_joined & (1u << p)) && Input_IsPadHit(p, b);
}

/* 0x80694f0 */
static void playerInput(int p) {
    if (!(g_joined & (1u << p))) {
        if (!Input_IsPadHit(p, PAD_C) || !Coin_HasCredit()) return;
        sfx(ZS_JOIN);
        Coin_ConsumeCredit();
        g_joined |= 1u << p;
        Title_SetJoinedMask(g_joined);
        sfx(ZS_SELECT);   /* 0x8068140: EFF_STATION_SELECT ao entrar */
        if (g_sel == ST_MISSION && missionOut()) g_sel = ST_ARCADE;
        setLayout(g_anim);
        return;
    }
    if (hit(p, PAD_DL)) {
        sfx(ZS_PUSH);
        g_sel = stepSel(g_sel, -1);
        g_confirmed = false;
        g_anim = 2;
        BGA_SceneReset(ST_BGA, "arro_l");
        BGA_SceneReset(ST_BGA, "arr_left");
        BGA_SceneReset(ST_BGA, "pan_right");
        setLayout(g_anim);
    }
    if (hit(p, PAD_DR)) {
        sfx(ZS_PUSH);
        g_sel = stepSel(g_sel, +1);
        g_confirmed = false;
        g_anim = 1;
        BGA_SceneReset(ST_BGA, "arro_r");
        BGA_SceneReset(ST_BGA, "arr_right");
        BGA_SceneReset(ST_BGA, "pan_left");
        setLayout(g_anim);
    }
    if (hit(p, PAD_C)) {
        sfx(ZS_PUSH);
        if (g_confirmed) { g_timer = 0; return; }
        /* MISSION com 2 jogadores não confirma (mas a voz toca) */
        if (!(g_sel == ST_MISSION && missionOut())) g_confirmed = true;
        static const int k_voice[ST_COUNT] = { ZS_EASY, ZS_ARCADE, ZS_MISSION, ZS_REMIX };
        sfx(k_voice[g_sel]);
    }
}

static void updateTimer(void) {
    g_prevTimer = g_timer;
    if (g_timer > 0) g_timer = 20 - (int)((timeGetTime() - g_startMs) / 1000);
    if (g_timer != g_prevTimer && g_timer < 11 && g_timer > 0) sfx(ZS_TIME);
}

void Station_Update(float dt) {
    if (g_game.stateFrame == 1) {
        Station_Enter();
        sfx(ZS_SELECT);   /* EFF_STATION_SELECT */
    }
    Movie_Update(dt);
    g_drawCount = 0;

    if (g_phase == 1) {                             /* 0x8068b20 */
        playScene("BG");
        playScene("top_down");
        playScene("in");
        playScene("arro_l");
        playScene("arro_r");
        playScene("arr_left");
        playScene("arr_right");
        playScene("TIME");
        if (BGA_SceneDone(ST_BGA, "in")) {
            g_startMs = timeGetTime();
            g_phase = 2;
        }
    } else if (g_phase == 2) {                      /* 0x8068cb0 */
        static const char* const k_anim[3] = { "hold", "pan_left", "pan_right" };
        updateTimer();
        playScene("bg");
        playScene("top_down");
        const char* an = k_anim[g_anim];
        playScene(an);
        bool skipSel = false;
        if (g_anim != 0) {
            if (BGA_SceneDone(ST_BGA, an)) {
                g_anim = 0;
                setLayout(0);
                BGA_SceneReset(ST_BGA, selScene());
            }
            if (g_anim != 0) skipSel = true;
        }
        if (!skipSel && g_confirmed) playScene(selScene());
        playScene("arro_l");
        playScene("arro_r");
        playScene("arr_left");
        playScene("arr_right");
        playScene("TIME");
        if (g_sel == ST_MISSION && (g_joined & 3) == 3) playScene("1PMISSION");
        playerInput(0);
        playerInput(1);
        if (g_timer < 1) {
            sfx(ZS_OUT);
            if (g_sel == ST_MISSION && missionOut()) g_sel = ST_ARCADE;
            g_anim = 0;
            setLayout(0);
            g_fade = 0;
            g_phase = 3;
        }
    } else if (g_phase == 3) {                      /* 0x80690c0 */
        g_fadeDraw = g_fade;
        playScene("bg");
        playScene("top_up");
        playScene("out");
        if (g_fade >= 40) {
            /* ARCADE -> "SELECT"; REMIX -> "SELECT" (canal REMIX pela estação);
             * EASY / MISSION: telas ainda não feitas -> ARCADE */
            if (g_sel == ST_REMIX) ExSelect_SetStation(1, 3);
            else                   ExSelect_SetStation(0, 0);
            if (g_sel == ST_EASY || g_sel == ST_MISSION)
                Log_Print("STATION: %s ainda não existe, indo para o ARCADE\n",
                          g_sel == ST_EASY ? "EASY STATION" : "MISSION STATION");
            Movie_Close();
            BGM_Stop();
            Title_StopMusic();
            Title_SetJoinedMask(g_joined);
            Menu_ResetState();
            Game_ChangeState(STATE_EXSELECT);
            return;
        }
        g_fade++;
    }
}

void Station_Render(void) {
    Movie_Render();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < g_drawCount; i++) {
        /* 0x80690ef: o fade preto vem logo depois de "bg", antes do resto */
        if (g_phase == 3 && i == 1 && g_fadeDraw != 0) {
            glDisable(GL_TEXTURE_2D);
            glColor4f(0, 0, 0, (float)g_fadeDraw / 40.0f);
            glBegin(GL_QUADS);
            glVertex2f(0, 0); glVertex2f(640, 0); glVertex2f(640, 480); glVertex2f(0, 480);
            glEnd();
            glColor4f(1, 1, 1, 1);
        }
        applyDraw(i);
        BGA_DrawFrame(ST_BGA, g_drawFrames[i]);
    }
}
