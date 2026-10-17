/*
 * movie.c — player dos .MOV (MOV2 do Exceed, MOV3 do Zero) com libmpeg2 nativa.
 *
 * Original (exceed.exe):
 *   0x4225A8  abre: "MOV2", pula hdr[0x88], chave 16 B, nome cifrado 32 B, byte G
 *   0x4226A0  lê 0x1000 B e desembaralha: b = bitrev(b) ^ bitrev(G) (0x4223C4/0x422420)
 *   0x422A68  decodifica até o quadro-alvo (tempo x fps) com a libmpeg2:
 *             estado 1 (SEQUENCE) -> mpeg2_convert(rgb16); 0 (BUFFER) -> mais dados;
 *             7/8 (SLICE/END) -> quadro pronto. No fim do arquivo volta a hdr[0x88]+0xC0.
 *   Quadro final RGB16 640x480.
 *
 * O port usa libmpeg2 estática vendorizada (terceiros/libmpeg2) cross-platform.
 */
#include "pumpy.h"
#include "movie.h"
#include "mpeg2.h"
#include "mpeg2convert.h"
#include <SDL.h>

#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif

/* Extra do port: tempo gasto no vídeo (ms), lido pelo detector de travadas (main.c) */
double g_movieMs = 0.0;

typedef struct {
    FILE*               f;
    mpeg2dec_t*         dec;
    const mpeg2_info_t* info;
    uint32_t            dataStart;     /* hdr[0x88] + 0xC0 */
    uint8_t             table[256];    /* bitrev(b) ^ bitrev(G) */
    uint8_t             buf[0x1000];
    double              fps;
    double              time;
    int                 target;        /* [+0x2c] no original */
    int                 decoded;       /* [+0x30] */
    bool                loop;
    bool                ended;
    GLuint              tex;
    bool                hasFrame;
    /* Extra do port: vídeo da música inteiro em memória (já desembaralhado),
     * carregado com o título (PNZ) na tela — ver Movie_Preload. */
    uint8_t*            mem;
    uint32_t            memSize;
    uint32_t            memPos;
} MovieState;

/* NX CSelect: fundo (BGA/BG.MOV) e prévia dentro do card tocam juntos
 * (0x8094450 e o objeto 0xa882300): duas instâncias, 0 = padrão. */
static MovieState  g_movs[2];
static MovieState* g_mov = &g_movs[0];

static uint8_t bitrev8(uint8_t b) {                                /* 0x4223C4 */
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) if (b & (1 << i)) r |= (uint8_t)(0x80 >> i);
    return r;
}

/* 0x4226D8: fps pelo frame_rate_code do sequence header */
static double movie_fps(const uint8_t* s) {
    static const double rates[8] = { 24000.0/1001, 24, 25, 30000.0/1001, 30, 50, 60000.0/1001, 60 };
    if (s[0] == 0 && s[1] == 0 && s[2] == 1 && s[3] == 0xB3) {
        int code = s[7] & 0x0F;
        if (code >= 1 && code <= 8) return rates[code - 1];
    }
    return 30000.0 / 1001;
}

static int movie_read(void) {                                      /* 0x4226A0 */
    if (g_mov->mem) {   /* pré-carregado: mesmos blocos de 0x1000, sem disco */
        uint32_t left = g_mov->memSize - g_mov->memPos;
        int n = (int)(left < sizeof(g_mov->buf) ? left : sizeof(g_mov->buf));
        memcpy(g_mov->buf, g_mov->mem + g_mov->memPos, (size_t)n);
        g_mov->memPos += (uint32_t)n;
        return n;
    }
    int n = (int)fread(g_mov->buf, 1, sizeof(g_mov->buf), g_mov->f);
    for (int i = 0; i < n; i++) g_mov->buf[i] = g_mov->table[g_mov->buf[i]];
    return n;
}

bool Movie_Open(const char* path, bool loop) {
    Movie_Close();

    char cleanPath[MAX_PATH];
    strncpy(cleanPath, path, sizeof(cleanPath) - 1);
    cleanPath[sizeof(cleanPath) - 1] = '\0';
#if !defined(_WIN32)
    for (char* p = cleanPath; *p; p++) {
        if (*p == '\\') *p = '/';
    }
#endif

    FILE* f = fopen(cleanPath, "rb");
    if (!f) { Log_Print("MOVIE: falha ao abrir '%s'\n", cleanPath); return false; }
    uint8_t hdr[0x8C];
    size_t hdrGot = fread(hdr, 1, sizeof(hdr), f);
    if (hdrGot >= 8 && hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 1 && hdr[3] == 0xB3) {
        /* Exceed2 (PIU32.EXE 0x4209f0): MPEG elementar puro, sem cabeçalho nem
         * embaralhamento — fps pelos primeiros 0x40 bytes (0x420e60) e blocos de
         * 0x1000 desde o offset 0 direto no mpeg2_buffer. */
        for (int b = 0; b < 256; b++) g_mov->table[b] = (uint8_t)b;
        g_mov->f = f;
        g_mov->dataStart = 0;
        g_mov->fps = movie_fps(hdr);
        fseek(f, 0, SEEK_SET);
        g_mov->dec = mpeg2_init();
        if (!g_mov->dec) { fclose(f); g_mov->f = NULL; return false; }
        g_mov->info = mpeg2_info(g_mov->dec);
        g_mov->loop = loop;
        g_mov->time = 0;
        g_mov->target = 0;
        g_mov->decoded = 0;
        g_mov->ended = false;
        g_mov->hasFrame = false;
        if (!g_mov->tex) glGenTextures(1, &g_mov->tex);
        Log_Print("MOVIE: '%s' aberto (MPEG puro, %.3f fps, loop=%d)\n", cleanPath, g_mov->fps, loop);
        Movie_Update(0.0f);
        return true;
    }

    /* Zero (piu 0x80a30e0): "MOV3" tem 16 B de chave extra antes do lixo de
     * hdr[0x88] bytes; o resto (chave, nome cifrado, G) segue o layout do MOV2
     * e o vídeo começa em 0xD0 + N. */
    int mov3 = hdrGot == sizeof(hdr) && memcmp(hdr, "MOV3", 4) == 0;
    if (hdrGot != sizeof(hdr) || (!mov3 && memcmp(hdr, "MOV2", 4) != 0)) {
        Log_Print("MOVIE: '%s' nao e MOV2/MOV3\n", cleanPath);
        fclose(f); return false;
    }
    uint32_t n = *(uint32_t*)(hdr + 0x88);
    uint8_t tail[0x34];
    fseek(f, (long)n + (mov3 ? 0x10 : 0), SEEK_CUR);
    if (fread(tail, 1, sizeof(tail), f) != sizeof(tail)) { fclose(f); return false; }
    uint8_t k = bitrev8(tail[0x30]);
    for (int b = 0; b < 256; b++) g_mov->table[b] = (uint8_t)(bitrev8((uint8_t)b) ^ k);

    g_mov->f = f;
    g_mov->dataStart = n + (mov3 ? 0xD0 : 0xC0);
    fseek(f, (long)g_mov->dataStart, SEEK_SET);
    uint8_t first[8];
    size_t got = fread(first, 1, sizeof(first), f);
    for (size_t i = 0; i < got; i++) first[i] = g_mov->table[first[i]];
    g_mov->fps = movie_fps(first);
    fseek(f, (long)g_mov->dataStart, SEEK_SET);

    g_mov->dec = mpeg2_init();
    if (!g_mov->dec) { fclose(f); g_mov->f = NULL; return false; }
    g_mov->info = mpeg2_info(g_mov->dec);
    g_mov->loop = loop;
    g_mov->time = 0;
    g_mov->target = 0;
    g_mov->decoded = 0;
    g_mov->ended = false;
    g_mov->hasFrame = false;
    if (!g_mov->tex) glGenTextures(1, &g_mov->tex);
    Log_Print("MOVIE: '%s' aberto (N=0x%X, %.3f fps, loop=%d)\n", cleanPath, n, g_mov->fps, loop);
    Movie_Update(0.0f);   /* NX 0x8094990: já sai com o 1º quadro (sem quadro preto na troca) */
    return true;
}

void Movie_Close(void) {
    if (g_mov->dec) { mpeg2_close(g_mov->dec); g_mov->dec = NULL; }
    if (g_mov->f) { fclose(g_mov->f); g_mov->f = NULL; }
    free(g_mov->mem); g_mov->mem = NULL; g_mov->memSize = g_mov->memPos = 0;
    g_mov->hasFrame = false;
}

/* Extra do port: lê o vídeo (dataStart..EOF) para a memória e desembaralha de
 * uma vez. Chamado no loading da música, com o PNZ na tela. */
bool Movie_Preload(void) {
    if (!g_mov->f || g_mov->mem) return g_mov->mem != NULL;
    long cur = ftell(g_mov->f);
    fseek(g_mov->f, 0, SEEK_END);
    long end = ftell(g_mov->f);
    if (cur < (long)g_mov->dataStart || end <= (long)g_mov->dataStart) {
        fseek(g_mov->f, cur, SEEK_SET); return false;
    }
    uint32_t size = (uint32_t)(end - (long)g_mov->dataStart);
    uint8_t* m = (uint8_t*)malloc(size);
    if (!m) { fseek(g_mov->f, cur, SEEK_SET); return false; }
    uint32_t t0 = timeGetTime();
    fseek(g_mov->f, (long)g_mov->dataStart, SEEK_SET);
    size_t got = fread(m, 1, size, g_mov->f);
    for (size_t i = 0; i < got; i++) m[i] = g_mov->table[m[i]];
    g_mov->mem = m;
    g_mov->memSize = (uint32_t)got;
    g_mov->memPos = (uint32_t)(cur - (long)g_mov->dataStart);
    if (g_mov->memPos > g_mov->memSize) g_mov->memPos = g_mov->memSize;
    Log_Print("MOVIE: pre-carregado %u bytes em %u ms (continua em %u)\n",
              (unsigned)got, timeGetTime() - t0, g_mov->memPos);
    return true;
}

void Movie_Prime(void) {
    if (!g_mov->f || g_mov->ended || g_mov->decoded > 0) return;
    g_mov->target = 0;
    Movie_Update(0.0f);
}

bool Movie_IsOpen(void) { return g_mov->f != NULL; }
void Movie_Select(int slot) { g_mov = &g_movs[(slot == 1) ? 1 : 0]; }
unsigned Movie_GLTexture(void) { return g_mov->hasFrame ? (unsigned)g_mov->tex : 0u; }
bool Movie_HasEnded(void) { return g_mov->ended; }
int  Movie_GetDecoded(void) { return g_mov->decoded; }

static void movie_upload(void) {
    if (!g_mov->info) return;
    const mpeg2_fbuf_t* fb = g_mov->info->display_fbuf;
    const mpeg2_sequence_t* sq = g_mov->info->sequence;
    if (!fb || !fb->buf[0] || !sq) return;
    glBindTexture(GL_TEXTURE_2D, g_mov->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    if (!g_mov->hasFrame) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }
    const void* pixels = fb->buf[0];
    if (!g_mov->hasFrame) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)sq->width, (GLsizei)sq->height, 0,
                     GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)sq->width, (GLsizei)sq->height,
                        GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    g_mov->hasFrame = true;
}

/* 0x422A68: decodifica até decoded > target */
void Movie_Update(float dt) {
    if (!g_mov->f || g_mov->ended) return;
    g_mov->time += dt;
    g_mov->target = (int)(g_mov->time * g_mov->fps);
    uint64_t hz0 = SDL_GetPerformanceCounter();
    bool newFrame = false;
    while (g_mov->decoded <= g_mov->target) {
        mpeg2_state_t st = mpeg2_parse(g_mov->dec);
        if (st == STATE_SEQUENCE) {
            mpeg2_convert(g_mov->dec, mpeg2convert_rgb16, NULL);
        } else if (st == STATE_BUFFER) {
            int n = movie_read();
            if (n <= 0) {
                if (!g_mov->loop) {
                    Log_Print("MOVIE: fim do arquivo (%d quadros)\n", g_mov->decoded);
                    g_mov->ended = true; break;
                }
                if (g_mov->mem) g_mov->memPos = 0;
                else fseek(g_mov->f, (long)g_mov->dataStart, SEEK_SET);
                n = movie_read();
                if (n <= 0) { g_mov->ended = true; break; }
            }
            mpeg2_buffer(g_mov->dec, g_mov->buf, g_mov->buf + n);
        } else if (st == STATE_SLICE || st == STATE_END) {
            g_mov->decoded++;
            newFrame = true;
        }
    }
    if (newFrame) movie_upload();
    g_movieMs += (double)(SDL_GetPerformanceCounter() - hz0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/* Tela cheia 640x480. Projeção Y-UP: linha 0 do quadro (topo) vai em y=480. */
void Movie_Render(void) {
    if (!g_mov->hasFrame) return;
    GLboolean blend = glIsEnabled(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, g_mov->tex);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 480);
    glTexCoord2f(1, 0); glVertex2f(640, 480);
    glTexCoord2f(1, 1); glVertex2f(640, 0);
    glTexCoord2f(0, 1); glVertex2f(0, 0);
    glEnd();
    if (blend) glEnable(GL_BLEND);
}

/* Zero CSelect (piu 0x806b6ba..0x806b70a): o quadro vai para uma textura e é
 * desenhado num retângulo, com glColor4f(c, c, c, alpha) e blend (fade da
 * prévia). Coordenadas Y-UP como o resto do render (y1 = topo). */
void Movie_RenderRect(float x0, float y0, float x1, float y1, float c, float alpha) {
    if (!g_mov->hasFrame) return;
    GLboolean blend = glIsEnabled(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, g_mov->tex);
    glColor4f(c, c, c, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x0, y1);
    glTexCoord2f(1, 0); glVertex2f(x1, y1);
    glTexCoord2f(1, 1); glVertex2f(x1, y0);
    glTexCoord2f(0, 1); glVertex2f(x0, y0);
    glEnd();
    glColor4f(1, 1, 1, 1);
    if (!blend) glDisable(GL_BLEND);
}
