/*
 * nx_credit.c — faixa de crédito da NX (CCreditManager, piu 0x804e324.. = NX2
 * creditmanager.cpp), recursos do BGA/CREDIT.DAT.
 *
 * Draw (todo quadro, menos no SETUP; NX2 main.cpp:604):
 *   GAME MODE = EVENT            -> cena "EVENT"
 *   moedas por crédito = 0       -> cena "freeplay"
 *   senão, crédito < 10          -> cena "creidit _0"  (crédito, resto de moedas, moedas por crédito)
 *          crédito 10..99        -> cena "creidit _00" (dois dígitos de crédito)
 * DrawDemoPlay (durante a demo; NX2 playengine.cpp:260): "DEMOPLAY_1P" e "DEMOPLAY_2P".
 *
 * As cenas do credit.bga são estáticas (os keyframes de cada slot têm a mesma
 * posição), então os sprites são desenhados direto, sem o sistema de BGA: ele é
 * limpo inteiro a cada troca de tela (Resource_ClearBGA) e esta faixa fica por cima
 * de todas. As texturas também são apagadas na limpeza; g_texGeneration avisa
 * quando é preciso recarregar.
 *
 * Posições (credit.bga, BGA3: x,y = centro da extensão do SPR, Y para baixo):
 *   slot 0  credit01.spr (322,470)   slot 1..3  dígitos (339|359|378, 468)
 *   slot 4  credit02.spr (322,470)   slot 5..8  dígitos (333|345|367|387, 468)
 *   slot 9  freeplay.spr (322,469)   slot 10    event.spr (320,467)
 *   slot 11 demoplay.spr (128,440) escala 0.8   slot 12 demoplay.spr (512,440) escala 0.8
 * Cor (keyframes +0x2C..+0x38, r g b a): crédito e dígitos (1,1,1); freeplay e event
 *   (1,1,0.5); demoplay (1,1,0) = amarelo. Sem animação: os dois keyframes de cada
 *   slot são iguais, e o NX2 só chama DrawScene.
 * Recortes: os "T tex x y w h u1 v1 u2 v2" dos .spr do CREDIT.DAT.
 */
#include "pumpy.h"

extern unsigned g_texGeneration;   /* texture.c: +1 a cada Texture_Shutdown */
extern bool g_exDemo;

static int      s_texText = -1;    /* CTEXTS.TGA   */
static int      s_texCred = -1;    /* credit01.tga */
static unsigned s_gen = 0xFFFFFFFFu;
static bool     s_failed;

typedef struct { int x, y, w, h, u1, v1, u2, v2; } CrTile;

/* credit01.spr e credit02.spr: 3 tiles cada (desenhados do último para o primeiro) */
static const CrTile k_cred01[3] = { { 25, 5, 76, 11, 1, 26, 77, 37 }, { 122, 5, 43, 11, 96, 26, 139, 37 }, { 0, 0, 193, 24, 0, 0, 193, 24 } };
static const CrTile k_cred02[3] = { { 20, 5, 76, 11, 1, 26, 77, 37 }, { 130, 5, 43, 11, 96, 26, 139, 37 }, { 0, 0, 193, 24, 0, 0, 193, 24 } };
/* cr0..cr9.spr: 12x10, v 41..51 */
static const int k_digU[10] = { 3, 18, 33, 48, 63, 79, 94, 109, 124, 139 };
static const CrTile k_free = { 0, 0, 96, 13, 0, 0, 96, 13 };      /* freeplay.spr */
static const CrTile k_event = { 0, 0, 84, 21, 3, 48, 87, 69 };    /* event.spr    */
static const CrTile k_demo = { 0, 0, 220, 32, 1, 14, 221, 46 };   /* demoplay.spr */

static bool crLoad(void) {
    if (s_gen == g_texGeneration && !s_failed) return s_texText >= 0 && s_texCred >= 0;
    if (s_gen == g_texGeneration && s_failed) return false;
    s_gen = g_texGeneration;
    s_texText = s_texCred = -1;
    s_failed = false;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/BGA/CREDIT.DAT", g_game.currentDirectory);
    if (!RES_Open(path)) { s_failed = true; return false; }
    s_texText = loadTextureFromRES("ctexts.tga");
    s_texCred = loadTextureFromRES("credit01.tga");
    RES_Close();
    if (s_texText < 0 || s_texCred < 0) { s_failed = true; return false; }
    return true;
}

/* tile de um SPR cujo centro da extensão (extW x extH) está em (cx, cy), com escala */
static void crTile(int tex, const CrTile* t, float cx, float cy, float extW, float extH, float sc, float blue) {
    float ox = cx - extW * 0.5f * sc, oy = cy - extH * 0.5f * sc;
    /* Texture_DrawUV: x,y = canto de cima-esquerda (Y para baixo), UV em pixels, V=0 no topo */
    Texture_DrawUV(tex, ox + (float)t->x * sc, oy + (float)t->y * sc, (float)t->w * sc, (float)t->h * sc,
                   (float)t->u1, (float)t->v1, (float)t->u2, (float)t->v2, 1.0f, 1.0f, blue, 1.0f);
}

static void crDigit(int d, float cx, float cy) {
    if (d < 0) d = 0;
    if (d > 9) d = 9;
    CrTile t = { 0, 0, 12, 10, k_digU[d], 41, k_digU[d] + 12, 51 };
    crTile(s_texCred, &t, cx, cy, 12.0f, 10.0f, 1.0f, 1.0f);
}

void NxCredit_Draw(void) {
    if (g_game.state == STATE_SERVICE_MENU) return;
    if (!crLoad()) return;

    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, 640, 0, 480, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    /* sem teste de profundidade: o gameplay pode deixar ligado e as setas ficavam por
     * cima do DEMO PLAY (no NX2 o IconDraw vem depois das setas, playengine.cpp:2208) */
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (g_exDemo && g_game.state == STATE_GAMEPLAY) {   /* DrawDemoPlay */
        crTile(s_texText, &k_demo, 128.0f, 440.0f, 220.0f, 32.0f, 0.8f, 0.0f);
        crTile(s_texText, &k_demo, 512.0f, 440.0f, 220.0f, 32.0f, 0.8f, 0.0f);
    }

    if (g_game.svcGameMode == 1) {                      /* EVENT */
        crTile(s_texText, &k_event, 320.0f, 467.0f, 84.0f, 21.0f, 1.0f, 0.5f);
    } else if (g_game.svcCoin1 == 0) {                  /* FREE PLAY */
        crTile(s_texText, &k_free, 322.0f, 469.0f, 96.0f, 13.0f, 1.0f, 0.5f);
    } else {
        int credit = g_game.svcCoinTotal / g_game.svcCoin1;
        int tail = g_game.svcCoinTotal % g_game.svcCoin1;
        if (credit > 99) credit = 99;
        const CrTile* base = (credit < 10) ? k_cred01 : k_cred02;
        for (int t = 2; t >= 0; t--) crTile(s_texCred, &base[t], 322.0f, 470.0f, 193.0f, 24.0f, 1.0f, 1.0f);
        if (credit < 10) {                              /* "creidit _0" */
            crDigit(credit, 339.0f, 468.0f);
            crDigit(tail, 359.0f, 468.0f);
            crDigit(g_game.svcCoin1, 378.0f, 468.0f);
        } else {                                        /* "creidit _00" */
            crDigit(credit / 10, 333.0f, 468.0f);
            crDigit(credit % 10, 345.0f, 468.0f);
            crDigit(tail, 367.0f, 468.0f);
            crDigit(g_game.svcCoin1, 387.0f, 468.0f);
        }
    }

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glPopAttrib();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
}
