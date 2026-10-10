/*
 * nx_font.c — fonte bitmap do SETUP do NX (objeto 0x9e0cf58, piu).
 *
 * 0x805a560 -> 0x8059e40: glDisable(GL_TEXTURE_2D), texto em CP949;
 *   byte < 0x80: ASCII 8x16 (0x8059870), avanço 8;
 *   byte >= 0x80: KS de 2 bytes -> johab (0x80fb540) -> hangul 16x16 8x4x4
 *                 (0x8059980), avanço 16.
 * Cada bit aceso vira um ponto em (x + coluna, y + 16 - linha), Y para cima.
 * O original usa GL_POINTS de 1 px numa tela 640x480; aqui cada ponto é um
 * quadrado 1x1 em coordenadas lógicas, que fica igual em qualquer escala da janela.
 * Dados em nx_font_data.c (tools/gen_nx_font.py).
 */
#include "pumpy.h"

extern const uint8_t g_nxFontAsc[256 * 16];
extern const uint8_t g_nxFontCho[160 * 32];
extern const uint8_t g_nxFontJung[88 * 32];
extern const uint8_t g_nxFontJong[112 * 32];
extern const int8_t  g_nxFontMapCho[32], g_nxFontMapJung[32], g_nxFontMapJong[32];
extern const int8_t  g_nxFontVarCho0[22], g_nxFontVarCho1[22], g_nxFontVarJong[22];
extern const uint16_t g_nxFontKs[2350];

static void dot(float x, float y)
{
    glVertex2f(x - 0.5f, y - 0.5f);
    glVertex2f(x + 0.5f, y - 0.5f);
    glVertex2f(x + 0.5f, y + 0.5f);
    glVertex2f(x - 0.5f, y + 0.5f);
}

static void ascii(float x, float y, unsigned char c)   /* 0x8059870 */
{
    const uint8_t* g = &g_nxFontAsc[c * 16];
    for (int r = 0; r < 16; r++)
        for (int b = 0; b < 8; b++)
            if (g[r] & (0x80 >> b)) dot(x + (float)b, y + 16.0f - (float)r);
}

static void hangul(float x, float y, uint16_t code)    /* 0x8059980 */
{
    int cho  = g_nxFontMapCho[(code >> 10) & 31];
    int jung = g_nxFontMapJung[(code >> 5) & 31];
    int jong = g_nxFontMapJong[code & 31];
    if (jung < 0 || jung > 21) jung = 0;
    int vCho  = jong ? g_nxFontVarCho1[jung] : g_nxFontVarCho0[jung];
    int vJung = ((cho <= 1 || cho == 16) ? 0 : 1) + (jong ? 2 : 0);
    int vJong = g_nxFontVarJong[jung];
    uint8_t g[32] = { 0 };
    if (cho)  { const uint8_t* s = &g_nxFontCho[(cho + vCho * 20) * 32];    for (int i = 0; i < 32; i++) g[i] = s[i]; }
    if (jung) { const uint8_t* s = &g_nxFontJung[(jung + vJung * 22) * 32]; for (int i = 0; i < 32; i++) g[i] |= s[i]; }
    if (jong) { const uint8_t* s = &g_nxFontJong[(jong + vJong * 28) * 32]; for (int i = 0; i < 32; i++) g[i] |= s[i]; }
    for (int r = 0; r < 16; r++)
        for (int half = 0; half < 2; half++)
            for (int b = 0; b < 8; b++)
                if (g[r * 2 + half] & (0x80 >> b)) dot(x + (float)(half * 8 + b), y + 16.0f - (float)r);
}

/* (x, y) = canto de baixo à esquerda, Y para cima (mesma convenção do svcText) */
void NxFont_Draw(float x, float y, const char* s)       /* 0x805a560 / 0x8059e40 */
{
    if (!s) return;
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        if (*p < 0x80) { ascii(x, y, *p); x += 8.0f; p++; continue; }
        if (!p[1]) break;
        int k = p[0] * 0x5E + p[1] - 0x4141;
        if (k >= 0 && k < 2350) hangul(x, y, g_nxFontKs[k]);
        x += 16.0f;
        p += 2;
    }
    glEnd();
    glEnable(GL_TEXTURE_2D);
}
