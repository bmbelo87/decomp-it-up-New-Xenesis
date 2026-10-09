/*
 * nx_text.c — texto TrueType da NX (classe de fonte do piu, FreeType).
 *
 * Original (piu NX), objeto 0xa880e60 da Select:
 *   0x8078e7a  iniciado com "MICROGBE.TTF"; 0x8091da0: tamanho +0x2c = min(0x18, 0x80) = 24
 *   0x8091160  glifo: FT_Set_Pixel_Sizes(face, 0, 24) (0x80c0ff0), FT_Load_Glyph(0x8a),
 *              FT_Render_Glyph normal (anti-alias), textura LUMINANCE_ALPHA com filtro LINEAR.
 *              Guarda +0x10 bitmap_top, +0x14 bitmap_left, +0x18 largura, +0x1c linhas.
 *   0x8092680  avanço = bitmap_left + largura (NÃO o advance da fonte); se <= 0
 *              (espaço), avanço = tamanho / 2 = 12. Sem kerning.
 *   0x8092840  glTranslatef(x, 480 - y); cada glifo com topo em -(24 - bitmap_top):
 *              y é o topo da caixa de 24 px (linha de base em y + 24, Y para baixo).
 *   0x8091070  texto em UTF-8 (1 a 3 bytes) -> códigos.
 *   0x8090de0  define o texto: x = 0xbe (190), y = 0x16f (367), rolagem desligada.
 *   0x807bb87  a Select liga a rolagem (+0x80c = 1) quando a prévia termina de carregar.
 *   0x8090fa0  desenha: máscara de stencil (0x80926a0) com o trapézio 0x8144fa0
 *              (187,119) (149,79) (489,79) (446,119) em Y para cima, cor branca 0x8144f88.
 *              Com rolagem: x -= 3 por desenho; com x <= 0x94 e x + largura < -60
 *              volta para x = 0x1e9 (489).
 *
 * Diferenças: rasterização com stb_truetype (pedido do usuário) em vez de FreeType;
 * sem stencil, o trapézio é recortado na geometria (uma faixa por linha de pixel).
 * A rolagem anda 3 px a cada 1/60 s (o original anda 3 px por quadro desenhado).
 */
#include "pumpy.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define NXT_SIZE  24               /* +0x2c */
#define NXT_TEX_W 2048
#define NXT_TEX_H 32               /* caixa de 24 px + descendentes */
#define NXT_MARGIN 16              /* folga à esquerda: bitmap_left pode ser negativo (ex.: 'W') */

static unsigned char* s_ttf;
static stbtt_fontinfo s_font;
static float  s_scale;
static GLuint s_tex;
static int    s_width;             /* +0x810 */
static bool   s_has;               /* +0x830 != -1 */
static bool   s_scroll;            /* +0x80c */
static int    s_x = 0xbe;          /* +0x81c */
static float  s_acc;

static bool nxtInit(void) {
    if (s_ttf) return true;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/MICROGBE.TTF", g_game.currentDirectory);
    FILE* f = fopen(path, "rb");
    if (!f) { Log_Print("NXTEXT: '%s' não abriu\n", path); return false; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    s_ttf = (unsigned char*)malloc((size_t)n);
    if (!s_ttf || fread(s_ttf, 1, (size_t)n, f) != (size_t)n ||
        !stbtt_InitFont(&s_font, s_ttf, stbtt_GetFontOffsetForIndex(s_ttf, 0))) {
        fclose(f);
        free(s_ttf); s_ttf = NULL;
        Log_Print("NXTEXT: '%s' inválido\n", path);
        return false;
    }
    fclose(f);
    s_scale = stbtt_ScaleForMappingEmToPixels(&s_font, (float)NXT_SIZE);   /* FT_Set_Pixel_Sizes(0, 24) */
    glGenTextures(1, &s_tex);
    return true;
}

/* 0x8091070: UTF-8 de 1 a 3 bytes */
static int nextCode(const unsigned char** pp) {
    const unsigned char* p = *pp;
    int c = *p;
    if (c < 0x80)      { *pp = p + 1; return c; }
    if (c <= 0xdf && p[1]) { *pp = p + 2; return ((c & 0x1f) << 6) | (p[1] & 0x3f); }
    if (c <= 0xef && p[1] && p[2]) { *pp = p + 3; return ((c & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f); }
    *pp = p + 1;
    return -1;   /* ignorado */
}

/* rasteriza uma linha (até len bytes) na textura tex; devolve a largura */
static int rasterize(const char* str, size_t len, GLuint tex) {
    unsigned char* bmp = (unsigned char*)calloc(NXT_TEX_W * NXT_TEX_H, 1);
    if (!bmp) return -1;
    int pen = 0;
    const unsigned char* p = (const unsigned char*)str;
    const unsigned char* end = p + len;
    while (p < end && *p) {
        int c = nextCode(&p);
        if (c < 0) continue;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        int g = stbtt_FindGlyphIndex(&s_font, c);
        if (g) stbtt_GetGlyphBitmapBox(&s_font, g, s_scale, s_scale, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;
        /* topo do glifo = 24 - bitmap_top = 24 + y0 (y0 = -bitmap_top) */
        int gx = NXT_MARGIN + pen + x0, gy = NXT_SIZE + y0;
        if (g && w > 0 && h > 0 && gx >= 0 && gy >= 0 && gx + w <= NXT_TEX_W && gy + h <= NXT_TEX_H)
            stbtt_MakeGlyphBitmap(&s_font, bmp + gy * NXT_TEX_W + gx, w, h, NXT_TEX_W, s_scale, s_scale, g);
        int adv = x0 + w;                        /* 0x8092680: left + largura */
        pen += (g && adv > 0) ? adv : NXT_SIZE / 2;   /* senão tamanho / 2 */
    }
    int width = pen > NXT_TEX_W - NXT_MARGIN ? NXT_TEX_W - NXT_MARGIN : pen;
    unsigned char* la = (unsigned char*)malloc(NXT_TEX_W * NXT_TEX_H * 2);
    if (la) {
        for (int i = 0; i < NXT_TEX_W * NXT_TEX_H; i++) { la[i * 2] = 255; la[i * 2 + 1] = bmp[i]; }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   /* 0x2601 */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, NXT_TEX_W, NXT_TEX_H, 0,
                     GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la);
        free(la);
    } else width = -1;
    free(bmp);
    return width;
}

/* 0x8090de0 */
void NxText_Set(const char* str) {
    s_has = false;
    s_scroll = false;   /* +0x80c = 0 */
    s_x = 0xbe;         /* +0x81c = 190 */
    s_acc = 0.0f;
    if (!str || !nxtInit()) return;
    s_width = rasterize(str, strlen(str), s_tex);
    s_has = s_width >= 0;
}

/* 0x8090c40: texto em várias linhas ('@' quebra), linha i em y + 30 * i (0x8090d1e),
 * desenhado por 0x8090e80 sem máscara. Usado pelo objetivo da missão (x 0x55, y 0xc2). */
#define NXT_LINES 6
static GLuint s_lineTex[NXT_LINES];
static int    s_lineW[NXT_LINES], s_lineCount;

void NxText_SetBlock(const char* str) {
    s_lineCount = 0;
    if (!str || !nxtInit()) return;
    if (!s_lineTex[0]) glGenTextures(NXT_LINES, s_lineTex);
    const char* p = str;
    while (s_lineCount < NXT_LINES) {
        const char* at = strchr(p, '@');
        size_t n = at ? (size_t)(at - p) : strlen(p);
        int w = rasterize(p, n, s_lineTex[s_lineCount]);
        if (w < 0) break;
        s_lineW[s_lineCount++] = w;
        if (!at) break;
        p = at + 1;
    }
}

void NxText_DrawBlock(int x, int y) {
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);   /* 0x8144f80 */
    for (int i = 0; i < s_lineCount; i++) {
        float x0 = (float)x - NXT_MARGIN, w = (float)(s_lineW[i] + NXT_MARGIN);
        float t = 480.0f - (float)(y + 30 * i), b = t - NXT_TEX_H;
        glBindTexture(GL_TEXTURE_2D, s_lineTex[i]);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);                 glVertex2f(x0, t);
        glTexCoord2f(w / NXT_TEX_W, 0);     glVertex2f(x0 + w, t);
        glTexCoord2f(w / NXT_TEX_W, 1);     glVertex2f(x0 + w, b);
        glTexCoord2f(0, 1);                 glVertex2f(x0, b);
        glEnd();
    }
    glPopAttrib();
}

/* 0x807bb8c: +0x80c = 1 */
void NxText_StartScroll(void) { s_scroll = true; }

/* trapézio 0x8144fa0 (Y para cima): esquerda (149,79)->(187,119), direita (489,79)->(446,119) */
static float leftEdge(float y)  { return 149.0f + (187.0f - 149.0f) * (y - 79.0f) / 40.0f; }
static float rightEdge(float y) { return 489.0f + (446.0f - 489.0f) * (y - 79.0f) / 40.0f; }

/* 0x8091030 (dentro do desenho no original): um passo por 1/60 s */
void NxText_Update(float dt) {
    if (!s_has || !s_scroll) return;
    s_acc += dt * 60.0f;
    while (s_acc >= 1.0f) {
        s_acc -= 1.0f;
        s_x -= 3;
        if (s_x <= 0x94 && s_x + s_width < -0x3c) s_x = 0x1e9;
    }
}

/* 0x8090fa0 */
void NxText_Draw(void) {
    if (!s_has) return;
    const float x = (float)s_x, yTop = 480.0f - 367.0f;   /* glTranslatef(x, 480 - y) */
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   /* 0x80926f3: 0x302/0x303 */
    glColor4f(1, 1, 1, 1);                               /* 0x8144f88 */
    glBegin(GL_QUADS);
    for (int r = 0; r < NXT_TEX_H; r++) {                /* uma faixa por linha de pixel */
        float t = yTop - r, b = t - 1.0f, ym = t - 0.5f;
        if (ym > 119.0f || ym < 79.0f) continue;
        float l = leftEdge(ym), rr = rightEdge(ym);
        float a = x - NXT_MARGIN > l ? x - NXT_MARGIN : l;
        float e = x + s_width < rr ? x + s_width : rr;
        if (e <= a) continue;
        float u0 = (a - x + NXT_MARGIN) / NXT_TEX_W, u1 = (e - x + NXT_MARGIN) / NXT_TEX_W;
        float v0 = (float)r / NXT_TEX_H, v1 = (float)(r + 1) / NXT_TEX_H;
        glTexCoord2f(u0, v0); glVertex2f(a, t);
        glTexCoord2f(u1, v0); glVertex2f(e, t);
        glTexCoord2f(u1, v1); glVertex2f(e, b);
        glTexCoord2f(u0, v1); glVertex2f(a, b);
    }
    glEnd();
    glPopAttrib();
}

/* 0x8090f10: desenha o texto do objeto centrado em x (x - largura / 2), y no topo
 * da caixa, sem a máscara (0x80926a0 com 0). Usado pelo "Score: %d" do recorde
 * na World Select (0x8084fd9, x 0x140, y 0x16f). Textura própria, refeita só
 * quando o texto muda. */
static GLuint s_cTex;
static int    s_cW = -1;
static char   s_cStr[64];

void NxText_DrawCentered(const char* str, int x, int y) {
    if (!str || !nxtInit()) return;
    if (!s_cTex) glGenTextures(1, &s_cTex);
    if (s_cW < 0 || strcmp(str, s_cStr) != 0) {
        snprintf(s_cStr, sizeof(s_cStr), "%s", str);
        s_cW = rasterize(str, strlen(str), s_cTex);
    }
    if (s_cW < 0) return;
    float x0 = (float)x - s_cW / 2.0f - NXT_MARGIN, w = (float)(s_cW + NXT_MARGIN);
    float t = 480.0f - (float)y, b = t - NXT_TEX_H;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s_cTex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);   /* 0x8144f84 */
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);             glVertex2f(x0, t);
    glTexCoord2f(w / NXT_TEX_W, 0); glVertex2f(x0 + w, t);
    glTexCoord2f(w / NXT_TEX_W, 1); glVertex2f(x0 + w, b);
    glTexCoord2f(0, 1);             glVertex2f(x0, b);
    glEnd();
    glPopAttrib();
}
