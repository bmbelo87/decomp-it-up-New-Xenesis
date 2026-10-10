/*
 * highscore.c — CHighscore do Exceed (vtable 0x1282B88), parte da atração.
 *
 * Begin 0x412648: com crédito vai para TITLE; senão carrega BGA\HS.DAT,
 *   a fonte do BGA\BFONT.DAT (0x41E58C) e aponta para o ranking da EEPROM
 *   ([0xA69038] = eeprom.bin, 2048 bytes):
 *     +0x74B + 4k  pontuação da posição k (u32)
 *     +0x79B + 4k  nome da posição k (4 caracteres)
 *   20 posições.
 * Entrada 0x412894 (t = [+0x10] de 0 a 120): HS.DAT inteiro no quadro t e as
 *   posições 15..20 entrando com fade.
 * Update 0x412B0C: a lista rola até a 1ª posição; fade a partir de t=1200 e
 *   IDLE com t > 1260. Com crédito, TITLE.
 * Coordenadas do original em Y-UP (espaço do GL do projeto): entram direto.
 */
#include "pumpy.h"
#include "bga.h"

/* DESATIVADO: tela do Exceed (BGA\HS.DAT + bfont.tga, que não existem na NX).
 * A versão da NX está depois do #endif. */
#if 0
#define HS_BGA 0

static uint8_t g_eeprom[2048];
static int g_bfontTex = -1;     /* [0x568F90]: bfont.tga do BFONT.DAT */
static int g_t;                 /* [+0x10] */
static int g_phase;             /* 0 = entrada (0x412894), 1 = Update (0x412B0C) */
static float g_color;           /* [+0x6C] */

static uint32_t hsScore(int k) {
    uint32_t v;
    memcpy(&v, g_eeprom + 0x74B + 4 * k, 4);
    return v;
}
static const uint8_t* hsName(int k) { return g_eeprom + 0x79B + 4 * k; }

/* 0x41E63C: índice na tabela 0x456FAC; 0x3B = não desenha */
static int glyphIndex(int ch) {
    static const char k_set[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789~!@#$%^&*()_-+=\\:;/? ";
    for (int i = 0; k_set[i]; i++)
        if ((unsigned char)k_set[i] == (unsigned)ch) return i;
    return 0x3B;
}

/* 0x41E6EC(x, y, ch, w, h): bfont.tga em grade 8x8, y na base */
static void drawGlyph(float x, float y, int ch, float w, float h) {
    int idx = glyphIndex(ch);
    if (idx == 0x3B) return;
    float u0 = (float)(idx % 8) * 0.125f, u1 = u0 + 0.125f;
    float v0 = (float)(idx / 8) * 0.125f, v1 = v0 + 0.125f;
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x,     y + h);
    glTexCoord2f(u0, v1); glVertex2f(x,     y);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y);
    glTexCoord2f(u1, v0); glVertex2f(x + w, y + h);
    glEnd();
}

/* 0x41E9E0(x, y, w, h, passo, fmt, ...) */
static void drawText(float x, float y, const char* s) {
    if (g_bfontTex < 0 || !g_game.textures[g_bfontTex].inUse) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_game.textures[g_bfontTex].id);
    for (; *s; s++, x += 35.0f)
        drawGlyph(x, y, (unsigned char)*s, 38.0f, 48.0f);
}

/* Uma linha: posição (%02d) em x=50, nome em 142, pontuação (%08d) em 306 */
static void drawRow(int k, float y) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d", k + 1);
    drawText(50.0f, y, buf);
    const uint8_t* n = hsName(k);
    snprintf(buf, sizeof(buf), "%c%c%c%c", n[0], n[1], n[2], n[3]);
    drawText(142.0f, y, buf);
    snprintf(buf, sizeof(buf), "%08u", (unsigned)hsScore(k));
    drawText(306.0f, y, buf);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   /* 0x412FE4 */
}

void Highscore_Enter(void) {
    char path[MAX_PATH];
    /* era (Exceed2): ranking da imagem do old_save.c (PIUEXCEED2.INI, +0x74B/+0x79B):
     *     memcpy(g_eeprom, OldSave_Data(), sizeof(g_eeprom));
     * NX: o ranking é o top 20 do SETTINGS/RANK.DAT (nx_rank.c), o mesmo que a
     * NAMEINPUT grava. Antes esta tela mostrava a tabela do Exceed2, que a NX nunca
     * atualizava. Os campos +0x74B/+0x79B continuam sendo o formato lido abaixo. */
    memset(g_eeprom, 0, sizeof(g_eeprom));
    for (int k = 0; k < 20; k++) {
        uint32_t sc = Rank_TopScore(k);
        memcpy(g_eeprom + 0x74B + 4 * k, &sc, 4);
        memcpy(g_eeprom + 0x79B + 4 * k, Rank_TopName(k), 4);
    }

    snprintf(path, sizeof(path), "%s/BGA/BFONT.DAT", g_game.currentDirectory);
    g_bfontTex = -1;
    if (RES_Open(path)) {
        g_bfontTex = loadTextureFromRES("bfont.tga");
        RES_Close();
    }
    if (g_bfontTex < 0) Log_Print("HIGHSCORE: bfont.tga do BFONT.DAT não carregou\n");

    g_t = 0;
    g_phase = 0;
    g_color = 1.0f;
    BGA_SetColor(HS_BGA, 1.0f, 1.0f);
}

void Highscore_Update(float dt) {
    (void)dt;
    if (g_game.stateFrame == 1) Highscore_Enter();

    if (Attract_HasCredit()) {                  /* 0x412B1A / 0x4128C1 (era: Coin_HasCredit()) */
        Game_ChangeState(STATE_CREDIT);
        return;
    }
    if (g_phase == 0) {
        if (g_t >= 0x78) {                      /* 0x412AB5 */
            g_phase = 1;
            g_t = 0;
        }
        g_t++;
        return;
    }
    if (g_t > 0x4EC) {                          /* 0x413037 */
        Attract_Idle();
        return;
    }
    g_t++;
}

void Highscore_Render(void) {
    if (g_game.bgaPicCount <= 0) return;
    int t = g_t;

    if (g_phase == 0) {
        BGA_SetEventFrame(HS_BGA, t);           /* 0x4128F9 */
        for (int i = 0; i < 6; i++) {
            int s = i * 7 + 0x18;
            float a;
            if (t < s)          a = 0.0f;
            else if (t < s + 4) a = (float)(t - s) / 4.0f;
            else                a = 1.0f;
            glColor4f(1.0f, 1.0f, 1.0f, a);
            drawRow(14 + i, (float)i * -60.0f + 305.0f);
        }
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }

    BGA_DrawSlot(HS_BGA, t % 90 + 0x78, 0);     /* 0x412B42 */

    float yBase;
    int first;
    bool stopped = false;
    if (t <= 0x78) {
        yBase = 1145.0f;
        first = 14;
    } else {
        yBase = (float)(t - 0x78) * -1.3333334f + 1145.0f;
        first = 14 - (t - 0x5C) / 45;
        if (first <= 0 && yBase <= 305.0f) {    /* 0x41306C */
            yBase = 305.0f;
            first = 0;
            stopped = true;
        }
        if (first < 0) first = 0;
    }

    static const int k_rowSlots[7] = { 8, 0x0D, 0x11, 0x15, 0x19, 0x1D, 0x21 };
    if (t > 0x78 && !stopped) {
        int f = (t - 0x78) % 45 + 300;
        BGA_DrawSlot(HS_BGA, f, 8);
        BGA_DrawSlot(HS_BGA, f, 9);
        for (int i = 1; i < 7; i++) BGA_DrawSlot(HS_BGA, f, k_rowSlots[i]);
    } else {
        for (int i = 0; i < 7; i++) BGA_DrawSlot(HS_BGA, 300, k_rowSlots[i]);
    }
    BGA_DrawSlot(HS_BGA, t % 40 + 0x78, 0x25);
    BGA_DrawSlot(HS_BGA, t % 40 + 0x78, 0x2B);
    BGA_DrawSlot(HS_BGA, t % 61 + 0x1E, 0x2C);

    glColor4f(g_color, g_color, g_color, 1.0f);
    for (int k = first; k < 20; k++)
        drawRow(k, (float)k * -60.0f + yBase);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    if (t > 0x4B0) {                            /* 0x412FFB: fade de saída */
        g_color = (float)(t - 0x4B0) / -60.0f + 1.0f;
        BGA_SetColor(HS_BGA, g_color, 1.0f);
    }
}
#endif

/* ---------------------------------------------------------------------------
 * NX CHighScore (piu 0x80746a6.., "10CHighScore") = NX2 ProcModules/highscore.cpp.
 *
 * Begin: BGA/HIGHSCORE.DAT + BGA/HS.MOV em loop; fonte CBoldFont (NX2 bfont.cpp):
 *   uma textura por caractere, BFONT%02d.TGA do BGA/BFONT.DAT, índice = posição
 *   na tabela cnvTable.
 * Run:
 *   título: cena "hs_font" e, quando termina, "font_glow";
 *   lista:  cena "in" (entrada) -> espera 2000 ms -> cena "scroll" repetida, uma
 *           posição por volta, de 14 até a 1ª -> "in" parada por 5000 ms -> fade
 *           preto de 300 ms -> próxima tela da atração.
 *   linhas: posição "%02d" em x=30 (35x45, passo 35), nome em x=142 e pontuação
 *           "%08d" em x=390 (26x38, passo 26), 57 px entre linhas.
 * Coordenadas do original em Y para cima (espaço do GL do projeto): entram direto.
 * Os tempos em ms viraram quadros de 60 Hz (120 / 300 / 18).
 * O ranking é o top 20 do SETTINGS/RANK.DAT (nx_rank.c).
 * ------------------------------------------------------------------------- */
#include "movie.h"

#define NHS_BGA 0
#define NHS_WAIT_SCROLL 120   /* WAIT_BEFORE_SCROLL  2000 ms */
#define NHS_WAIT_FADE   300   /* WAIT_BEFORE_FADEOUT 5000 ms */
#define NHS_FADE        18    /* FADEOUT_DURATION     300 ms */
#define NHS_FIRST       14    /* SCROLL_ITEM_START_NUM */

enum { NHS_IN, NHS_ING, NHS_STOP };

static int  s_bf[59];           /* m_BFontTexID */
static int  s_scroll;           /* m_ScrollState */
static int  s_top;              /* m_iTopNumber */
static int  s_wait;             /* m_pScrollTimer em quadros; -1 = parado */
static bool s_done;             /* fim: o Update troca de tela */

/* NX2 bfont.cpp cnvTable / _CnvCharToValue: ' ' não desenha */
static int nhsGlyph(int ch) {
    static const char k_tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789~!@#$%^&*()_-+=\\:;/?";
    if (ch == ' ') return -1;
    for (int i = 0; k_tab[i]; i++)
        if ((unsigned char)k_tab[i] == (unsigned)ch) return i;
    return -1;
}

/* CBoldFont::PrintfEx(x, y, w, h, passo, texto): y = base do caractere (Y para cima),
 * textura inteira com o topo da imagem em y + h */
static void nhsText(float x, float y, float w, float h, float step, const char* s) {
    glEnable(GL_TEXTURE_2D);
    for (; *s; s++, x += step) {
        int g = nhsGlyph((unsigned char)*s);
        if (g < 0 || g >= 59 || s_bf[g] < 0 || !g_game.textures[s_bf[g]].inUse) continue;
        glBindTexture(GL_TEXTURE_2D, g_game.textures[s_bf[g]].id);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x,     y + h);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x,     y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y + h);
        glEnd();
    }
}

/* linha da posição 'number' (1..20): número, nome e pontuação */
static void nhsRow(int number, float yNum, float yText) {
    char buf[16];
    if (number < 1 || number > 20) return;   /* o original lê fora da tabela; aqui não desenha */
    snprintf(buf, sizeof(buf), "%02d", number);
    nhsText(30.0f, yNum, 35.0f, 45.0f, 35.0f, buf);
    const char* n = Rank_TopName(number - 1);
    snprintf(buf, sizeof(buf), "%c%c%c%c", n[0], n[1], n[2], n[3]);
    nhsText(142.0f, yText, 26.0f, 38.0f, 26.0f, buf);
    snprintf(buf, sizeof(buf), "%08u", (unsigned)Rank_TopScore(number - 1));
    nhsText(390.0f, yText, 26.0f, 38.0f, 26.0f, buf);
}

void Highscore_Enter(void) {
    char path[MAX_PATH];
    for (int i = 0; i < 59; i++) s_bf[i] = -1;
    snprintf(path, sizeof(path), "%s/BGA/BFONT.DAT", g_game.currentDirectory);
    if (RES_Open(path)) {
        for (int a = 0; a < 59; a++) {
            if (a == 56) continue;            /* ' ' */
            char bfn[32];
            snprintf(bfn, sizeof(bfn), "bfont%02d.tga", a);
            s_bf[a] = loadTextureFromRES(bfn);
        }
        RES_Close();
    } else {
        Log_Print("HIGHSCORE: BGA/BFONT.DAT não abriu\n");
    }
    snprintf(path, sizeof(path), "%s/BGA/HS.MOV", g_game.currentDirectory);
    Movie_Close();
    if (!Movie_Open(path, true)) Log_Print("HIGHSCORE: '%s' não abriu\n", path);
    Rank_Load();

    s_scroll = NHS_IN;
    s_top = NHS_FIRST;
    s_wait = -1;
    s_done = false;
    BGA_SceneReset(NHS_BGA, "hs_font");
    BGA_SceneReset(NHS_BGA, "font_glow");
    BGA_SceneReset(NHS_BGA, "in");
    BGA_SceneReset(NHS_BGA, "scroll");
}

void Highscore_Update(float dt) {
    if (g_game.stateFrame == 1) Highscore_Enter();

    if (Attract_HasCredit()) {                  /* CHighScore::Run: GetCoin() -> Next(true) */
        Movie_Close();
        Game_ChangeState(STATE_CREDIT);
        return;
    }
    if (s_done) {
        Movie_Close();
        Attract_Idle();
        return;
    }
    Movie_Update(dt);
}

void Highscore_Render(void) {
    extern bool g_renderTick;
    Movie_Render();
    if (g_game.stateFrame < 1) return;          /* antes do Enter */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    bool haveBga = g_game.bgaPicCount > 0;
    if (haveBga) {
        if (BGA_SceneDone(NHS_BGA, "hs_font")) BGA_ScenePlay(NHS_BGA, "font_glow", true);
        else                                   BGA_ScenePlay(NHS_BGA, "hs_font", true);
        BGA_ScenePlay(NHS_BGA, (s_scroll == NHS_ING) ? "scroll" : "in", true);
    }

    if (s_scroll == NHS_IN && (!haveBga || BGA_SceneDone(NHS_BGA, "in")) && g_renderTick) {
        if (s_wait < 0) s_wait = 0;
        else if (s_wait >= NHS_WAIT_SCROLL) s_scroll = NHS_ING;
        else s_wait++;
    }

    float scrollY = 0.0f;
    if (s_scroll == NHS_ING) {
        if (!haveBga || BGA_SceneDone(NHS_BGA, "scroll")) {
            if (g_renderTick) {
                s_top--;
                if (s_top <= 1) {               /* chegou à 1ª posição: para */
                    s_top = 1;
                    s_scroll = NHS_STOP;
                    s_wait = 0;
                }
            }
        } else {
            scrollY = (float)BGA_SceneOffset(NHS_BGA, "scroll") / 46.0f * 57.0f;
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (s_scroll == NHS_IN) {
        int tick = haveBga ? BGA_SceneOffset(NHS_BGA, "in") : 1000;
        int num = (tick < 4) ? 0 : tick / 4;
        float a = (float)(tick % 4) / 3.0f;
        if (num > 7) { num = 7; a = 1.0f; }     /* da 8ª linha em diante fica fora da tela */
        for (int i = 0; i < num; i++) {
            glColor4f(1.0f, 1.0f, 1.0f, (i == num - 1) ? a : 1.0f);
            nhsRow(i + s_top, 350.0f - 57.0f * (float)i - scrollY, 352.0f - 57.0f * (float)i - scrollY);
        }
    } else {
        for (int i = -1; i < 7; i++) {
            float y;
            if (i == -1) {                      /* linha que entra por cima, com fade */
                float a = (scrollY > 50.0f) ? (scrollY - 50.0f) / 50.0f : 0.0f;
                if (a < 0.0f) a = 0.0f;
                if (a > 1.0f) a = 1.0f;
                y = 352.0f;
                glColor4f(1.0f, 1.0f, 1.0f, a);
            } else {
                y = 352.0f - 57.0f * (float)i - scrollY;
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            }
            nhsRow(i + s_top, y, y);
        }
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    if (s_scroll == NHS_STOP) {
        if (s_wait >= NHS_WAIT_FADE) {
            float a = 1.0f - (float)((NHS_WAIT_FADE + NHS_FADE) - s_wait) / (float)NHS_FADE;
            if (a > 1.0f) a = 1.0f;
            if (a < 0.0f) a = 0.0f;
            glDisable(GL_TEXTURE_2D);
            glColor4f(0.0f, 0.0f, 0.0f, a);
            glBegin(GL_QUADS);
            glVertex2f(0.0f, 0.0f); glVertex2f(640.0f, 0.0f);
            glVertex2f(640.0f, 480.0f); glVertex2f(0.0f, 480.0f);
            glEnd();
            glEnable(GL_TEXTURE_2D);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            if (s_wait >= NHS_WAIT_FADE + NHS_FADE) s_done = true;
        }
        if (g_renderTick) s_wait++;
    }
}
