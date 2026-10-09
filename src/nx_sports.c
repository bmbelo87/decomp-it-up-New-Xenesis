/*
 * nx_sports.c — tela de kcal do fim do crédito (CSports, piu NX).
 *
 * Classe CSports (RTTI "7CSports"): Begin 0x8089900, intro 0x8089e00,
 * quadro 0x8089eb0, End 0x808a300, saída (fade) 0x808a220. Proc "SPORTS",
 * passo 14 do sequenciador 0x805fb90:
 *   - fim do crédito (passo 12): SPORTS, ou GAMEOVER direto com GAME MODE
 *     = EVENT (EEPROM +0xECE, [0x9e3dbce] == 1). WORLD tem fluxo próprio.
 *   - depois do STAGE BREAK (passo 19): SPORTS se [0x81f8910] > 0.
 *   - saída (0x805ffc3): ARCADE -> NAMEINPUT ([0x81f8900]) ou GAMEOVER;
 *     SPECIAL -> GAMEOVER; TRAINING -> PLAYMOVIE CONGRATULATIONS -> GAMEOVER.
 *
 * Valores (CDanceGrade, 0x80735c9..0x80737a3), por jogador e estágio:
 *   t    = [play+0xd2c8] / 60000 (minutos tocados)
 *   L    = nível do chart ([jogador+0], 1..30, 0x807b3cc)
 *   m    = 0.931 se o modo for CRAZY (código 3 de "nhcdmp" -> 0 1 3 9 10 4)
 *   e/c  = (totalstep - miss) / totalstep  ([+0x10], [+0x3c])
 *   kcal = (0.475 L + 7.414 + m) t e / c   -> [jogador+0x49c + 4 st] (0x80715b0)
 *   VO2  = (1.335 L + 19.829 + m) t e / c  -> [jogador+0x4ac + 4 st] (0x80715d0)
 *   Zerados no início do 1º estágio (0x8070c92).
 *
 * Tela: BGA/BG.MOV em loop + BGA/SPORTS.DAT. Cenas "start" (+0xb4), "round
 * start" ou "training round start" no TRAINING (+0x4c), "kcal data position"
 * (+0xcc). Títulos pelo idioma (0x8089aba): "OOQP"[lang] -> slots 5 e 6,
 * "RKIK"[lang] -> 71, +1 -> 72. Dígitos 0..9 = slots 84..93.
 * Barras (0x808a6b0): kcal do estágio, cresce 1 por quadro, altura x7.27
 * (10..220), em Translate(x, 162) + Rotate(ang, 0,1,0); peças bar_01 (base),
 * bar_02 esticada de y=8 até h, bar_03 em h. P1 = slots 13..15, P2 = bar_R
 * (94..96). Totais (0x808a3f0): kcal e VO2 x1000 em "%06d", um dígito por slot
 * (tabela 0x8114850). 20 s (+0xac); CENTER encerra depois de 4 s; saída com
 * fade preto de 40 quadros.
 */
#include "pumpy.h"
#include "bga.h"
#include "movie.h"

#define NS_BGA 0

float g_nxKcal[2][4];   /* +0x49c */
float g_nxVo2[2][4];    /* +0x4ac */

double Gameplay_SongTimeSec(void);

static int   s_phase;          /* [0x81608b0]: 1 intro, 2 quadro, 3 saída, 4 CONGRATULATIONS */
static GameState s_next;
static uint32_t s_t0;
static int   s_left;           /* +0xac */
static float s_fade;           /* +0x48 */
static float s_bar[2][4];      /* 0xa882460 / 0xa882470 */
static float s_kcalTot[2], s_vo2Tot[2];   /* +0x94 / +0x9c */
static int   s_stages;         /* [0x81f8910] */
static BGALayerSrc s_dig[10];
static BGALayerSrc s_barSrc[2][3];
static bool  s_ok;
static const char* s_round;

/* chamado no início da nota de cada estágio */
void NxSports_Store(int st) {
    if (st < 0 || st > 3) return;
    if (st == 0) { memset(g_nxKcal, 0, sizeof(g_nxKcal)); memset(g_nxVo2, 0, sizeof(g_nxVo2)); }
    const GameplayStats* s = &g_game.stats;
    float t = (float)(Gameplay_SongTimeSec() * 1000.0 / 60000.0);
    int L = g_game.selectedDifficulty;   /* HIPÓTESE: o mesmo nível para os dois jogadores */
    if (L == 0) L = 1;
    if (L >= 31) L = 30;
    bool crazy = g_game.selectedModeIndex >= 0 &&
                 !strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "CRAZY");
    float m = crazy ? 0.931f : 0.0f;
    for (int p = 0; p < 2; p++) {
        if (!(g_game.activePlayerMask & (1 << p))) continue;
        int c = (int)(s->perfectCount[p] + s->greatCount[p] + s->goodCount[p] + s->badCount[p] + s->missCount[p]);
        if (c <= 0) continue;
        float e = (float)(c - (int)s->missCount[p]);
        g_nxKcal[p][st] = (0.475f * L + 7.414f + m) * t * e / (float)c;
        g_nxVo2[p][st]  = (1.335f * L + 19.829f + m) * t * e / (float)c;
        Log_Print("SPORTS: P%d estágio %d kcal %.3f VO2 %.3f (L %d t %.2f %d/%d)\n",
                  p + 1, st, g_nxKcal[p][st], g_nxVo2[p][st], L, t, (int)e, c);
    }
}

/* estado depois da nota/stage break: passa pela SPORTS quando o original passa */
GameState NxSports_Route(GameState ns, bool fromStageBreak, int stage) {
    if (!g_exceedSongIds || g_game.nxGameMode == 2) return ns;
    if (ns != STATE_NAMEINPUT && ns != STATE_GAMEOVER_ENTER) return ns;
    if (g_game.svcGameMode == 1) return ns;            /* EVENT: [0x9e3dbce] == 1 */
    if (fromStageBreak && stage <= 0) return ns;       /* 0x806005a */
    s_next = ns;
    s_stages = stage < 0 ? 0 : (stage > 3 ? 3 : stage);
    /* o estágio do STAGE BREAK não passou pela nota: no original continua com o
     * zero do início do crédito (0x8070c92), assim como os seguintes */
    if (fromStageBreak)
        for (int p = 0; p < 2; p++) for (int k = s_stages; k < 4; k++) g_nxKcal[p][k] = g_nxVo2[p][k] = 0.0f;
    return STATE_NX_SPORTS;
}

static void numbers(int p) {   /* 0x808a3f0 */
    static const int k_slot[2][12] = {
        { 55, 56, 57, 59, 60, 61, 64, 65, 66, 68, 69, 70 },
        { 39, 40, 41, 43, 44, 45, 48, 49, 50, 52, 53, 54 },
    };
    char buf[16];
    int v[2] = { (int)(s_kcalTot[p] * 1000.0f), (int)(1000.0f * s_vo2Tot[p]) };
    for (int k = 0; k < 2; k++) {
        snprintf(buf, sizeof(buf), "%06d", v[k]);
        for (int i = 0; i < 6; i++) {
            int d = buf[5 - i] - '0';
            if (d < 0 || d > 9) continue;
            BGA_SetLayerSrc(NS_BGA, k_slot[p][k * 6 + i], &s_dig[d]);
        }
    }
}

static void quadTile(const BGALayerSrc* src, float y0, float y1, float dy) {
    if (!src->isSPR || src->sprTileStart < 0) return;
    SPRTileDef* t = &g_game.sprTiles[src->sprTileStart];
    if (t->texId < 0 || t->texId >= MAX_TEXTURES || !g_game.textures[t->texId].inUse) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_game.textures[t->texId].id);
    float x1 = (float)t->srcX, x2 = (float)(t->srcX + t->srcW);
    float lo = y0 + dy, hi = y1 + dy;
    glBegin(GL_QUADS);   /* V do topo da textura no y de cima (Y para cima) */
    glTexCoord2f(t->u1, t->v1); glVertex2f(x1, hi);
    glTexCoord2f(t->u1, t->v2); glVertex2f(x1, lo);
    glTexCoord2f(t->u2, t->v2); glVertex2f(x2, lo);
    glTexCoord2f(t->u2, t->v1); glVertex2f(x2, hi);
    glEnd();
}

static void bar(int p, float x, float h, float ang) {   /* 0x808a6b0 */
    if (h < 10.0f) h = 10.0f;
    if (h > 220.0f) h = 220.0f;
    glPushMatrix();
    glTranslatef(x, 162.0f, 0.0f);
    glRotatef(ang, 0.0f, 1.0f, 0.0f);
    SPRTileDef* t0 = &g_game.sprTiles[s_barSrc[p][0].sprTileStart];
    SPRTileDef* t2 = &g_game.sprTiles[s_barSrc[p][2].sprTileStart];
    quadTile(&s_barSrc[p][0], (float)t0->srcY, (float)(t0->srcY + t0->srcH), 0.0f);   /* 0x805ac90 */
    quadTile(&s_barSrc[p][1], 8.0f, h, 0.0f);                                          /* 0x805a810 */
    quadTile(&s_barSrc[p][2], (float)t2->srcY, (float)(t2->srcY + t2->srcH), h);       /* 0x805ad80 */
    glPopMatrix();
}

void NxSports_Enter(void) {   /* 0x8089900 */
    memset(s_bar, 0, sizeof(s_bar));
    for (int p = 0; p < 2; p++) {
        s_kcalTot[p] = s_vo2Tot[p] = 0.0f;
        if (!(g_game.activePlayerMask & (1 << p))) continue;
        for (int k = 0; k <= s_stages; k++) { s_kcalTot[p] += g_nxKcal[p][k]; s_vo2Tot[p] += g_nxVo2[p][k]; }
    }
    BGM_Stop();
    Movie_Close();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/BGA/BG.MOV", g_game.currentDirectory);
    Movie_Open(path, true);
    Resource_ClearBGA();
    s_ok = Resource_LoadBGAByName("SPORTS");
    BGA_Reset();
    if (!s_ok) Log_Print("SPORTS: falha ao carregar BGA\\SPORTS.DAT\n");

    if (s_ok) {
        int lang = g_game.svcLangOption & 3;
        BGALayerSrc a, b, c;
        if (BGA_GetLayerSrc(NS_BGA, "OOQP"[lang], &a)) { BGA_SetLayerSrc(NS_BGA, 5, &a); BGA_SetLayerSrc(NS_BGA, 6, &a); }
        if (BGA_GetLayerSrc(NS_BGA, "RKIK"[lang], &b)) BGA_SetLayerSrc(NS_BGA, 0x47, &b);
        if (BGA_GetLayerSrc(NS_BGA, "RKIK"[lang] + 1, &c)) BGA_SetLayerSrc(NS_BGA, 0x48, &c);
        for (int i = 0; i < 10; i++) s_ok &= BGA_GetLayerSrc(NS_BGA, 0x54 + i, &s_dig[i]);
        for (int i = 0; i < 3; i++) {
            s_ok &= BGA_GetLayerSrc(NS_BGA, 0xd + i, &s_barSrc[0][i]);
            s_ok &= BGA_GetLayerSrc(NS_BGA, 0x5e + i, &s_barSrc[1][i]);
        }
        if (!s_ok) Log_Print("SPORTS: alguma camada de origem não existe\n");
    }
    s_round = g_game.nxGameMode == 3 ? "training round start" : "round start";
    BGA_SceneReset(NS_BGA, "start");
    BGA_SceneReset(NS_BGA, s_round);
    BGA_SceneReset(NS_BGA, "kcal data position");
    s_phase = 1;
    s_fade = 0.0f;
    s_left = 20;
    s_t0 = timeGetTime();
    Log_Print("SPORTS: Begin, estágios 0..%d, kcal P1 %.3f P2 %.3f\n", s_stages, s_kcalTot[0], s_kcalTot[1]);
}

static void leave(void) {   /* 0x805ffc3 */
    Movie_Close();
    Resource_ClearBGA();
    if (g_game.nxGameMode == 3) {   /* TRAINING: PLAYMOVIE CONGRATULATIONS */
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/BGA/CONGRATULATIONS.MOV", g_game.currentDirectory);
        Movie_Open(path, false);
        snprintf(path, sizeof(path), "%s/AUDIO/CONGRATULATIONS.AUD", g_game.currentDirectory);
        if (BGM_LoadAUDDirect(path)) BGM_Play(false);
        s_phase = 4;
        return;
    }
    Game_ChangeState(g_game.nxGameMode == 0 ? s_next : STATE_GAMEOVER_ENTER);
}

void NxSports_Update(float dt) {
    if (g_game.stateFrame == 1) NxSports_Enter();
    Movie_Update(dt);
    if (s_phase == 4) {   /* PLAYMOVIE: sai com o vídeo e o áudio no fim (0x80784d0) */
        if ((!Movie_IsOpen() || Movie_HasEnded()) && !BGM_IsPlaying()) {
            Movie_Close();
            Game_ChangeState(STATE_GAMEOVER_ENTER);
        }
        return;
    }
    if (s_phase == 1) {   /* 0x8089e00 */
        if (BGA_SceneDone(NS_BGA, "start")) s_phase = 2;
        return;
    }
    if (s_phase == 2) {   /* 0x8089eb0 */
        s_left = 20 - (int)((timeGetTime() - s_t0) / 1000);
        for (int p = 0; p < 2; p++) {
            if (!(g_game.activePlayerMask & (1 << p))) continue;
            for (int k = 0; k <= s_stages; k++) {
                if (g_nxKcal[p][k] == 0.0f) continue;
                s_bar[p][k] += 1.0f;
                if (s_bar[p][k] >= g_nxKcal[p][k]) s_bar[p][k] = g_nxKcal[p][k];
            }
        }
        if (s_left <= 16) {   /* 0x808a18a: CENTER encerra */
            if ((g_game.activePlayerMask & 1) && Input_IsPadHit(0, PAD_C)) s_left = 0;
            if ((g_game.activePlayerMask & 2) && Input_IsPadHit(1, PAD_C)) s_left = 0;
        }
        if (s_left <= 0) s_phase = 3;
        return;
    }
    /* 0x808a220: fade preto, depois o próximo passo */
    if ((int)s_fade > 0x27) { leave(); return; }
    s_fade += 1.0f;
}

void NxSports_Render(void) {
    if (s_phase == 4) { Movie_Render(); return; }
    Movie_Render();
    if (!s_ok) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
    BGA_ScenePlay(NS_BGA, "start", true);
    if (s_phase >= 2) {
        for (int p = 0; p < 2; p++) if (g_game.activePlayerMask & (1 << p)) numbers(p);
        BGA_ScenePlay(NS_BGA, s_round, true);
        BGA_ScenePlay(NS_BGA, "kcal data position", true);

        /* 0x8089ecd: colunas x e ângulo; deslocamento por jogador (0x8089fa1) */
        static const int k_x[4] = { 0x95, 0x101, 0x188, 0x1f1 };
        static const int k_ang[4] = { 0, 0, 180, 180 };
        int both = (g_game.activePlayerMask & 3) == 3;
        int off[2] = { both ? 0 : 15, both ? 0x21 : 15 };
        glColor4f(1, 1, 1, 1);
        for (int p = 0; p < 2; p++) {
            if (!(g_game.activePlayerMask & (1 << p))) continue;
            for (int k = 0; k <= s_stages; k++) {
                if (g_nxKcal[p][k] == 0.0f) continue;
                bar(p, (float)(k_x[k] + off[p]), s_bar[p][k] * 7.27f, (float)k_ang[k]);
            }
        }
    }
    if (s_phase == 3 && (int)s_fade != 0) {
        Render_Rect(0.0f, 0.0f, 640.0f, 480.0f, 0, 0, 0, (uint8_t)(s_fade / 40.0f * 255.0f > 255.0f ? 255 : s_fade / 40.0f * 255.0f));
        glColor4f(1, 1, 1, 1);
    }
}
