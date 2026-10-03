/*
 * movie.c — player dos .MOV (MOV2 do Exceed, MOV3 do Zero).
 *
 * Original (exceed.exe):
 *   0x4225A8  abre: "MOV2", pula hdr[0x88], chave 16 B, nome cifrado 32 B, byte G
 *   0x4226A0  lê 0x1000 B e desembaralha: b = bitrev(b) ^ bitrev(G) (0x4223C4/0x422420)
 *   0x422A68  decodifica até o quadro-alvo (tempo x fps) com a libmpeg2:
 *             estado 1 (SEQUENCE) -> mpeg2_convert(rgb16); 0 (BUFFER) -> mais dados;
 *             7/8 (SLICE/END) -> quadro pronto. No fim do arquivo volta a hdr[0x88]+0xC0.
 *   Quadro final RGB16 640x480.
 *
 * O port usa a própria MPEG2.dll do jogo (mesma API), carregada com LoadLibrary.
 */
#include "pumpy.h"
#include "movie.h"
#include <SDL.h>

/* Extra do port: tempo gasto no vídeo (ms), lido pelo detector de travadas (main.c) */
double g_movieMs = 0.0;

#if defined(_WIN32)
#include <windows.h>

#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif

/* libmpeg2 0.3/0.4 — só os campos lidos */
typedef struct { uint8_t* buf[3]; void* id; } mp2_fbuf;
typedef struct {
    unsigned int width, height, chroma_width, chroma_height;
} mp2_sequence;
typedef struct {
    const mp2_sequence* sequence;
    const void* gop;
    const void* current_picture;
    const void* current_picture_2nd;
    const mp2_fbuf* current_fbuf;
    const void* display_picture;
    const void* display_picture_2nd;
    const mp2_fbuf* display_fbuf;
} mp2_info;

typedef void* (*fn_init)(uint32_t accel);
typedef void  (*fn_close)(void* dec);
typedef const mp2_info* (*fn_info)(void* dec);
typedef int   (*fn_parse)(void* dec);
typedef void  (*fn_buffer)(void* dec, uint8_t* start, uint8_t* end);
typedef int   (*fn_convert)(void* dec, void* conv, void* arg);

static HMODULE    g_mpegDll;
static fn_init    p_init;
static fn_close   p_close;
static fn_info    p_info;
static fn_parse   p_parse;
static fn_buffer  p_buffer;
static fn_convert p_convert;
static void*      p_rgb16;

typedef struct {
    FILE*    f;
    void*    dec;
    const mp2_info* info;
    uint32_t dataStart;     /* hdr[0x88] + 0xC0 */
    uint8_t  table[256];    /* bitrev(b) ^ bitrev(G) */
    uint8_t  buf[0x1000];
    double   fps;
    double   time;
    int      target;        /* [+0x2c] no original */
    int      decoded;       /* [+0x30] */
    bool     loop;
    bool     ended;
    GLuint   tex;
    bool     hasFrame;
    /* Extra do port: vídeo da música inteiro em memória (já desembaralhado),
     * carregado com o título (PNZ) na tela — ver Movie_Preload. */
    uint8_t* mem;
    uint32_t memSize;
    uint32_t memPos;
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

#ifdef PUMPY_LIBMPEG2
/* Build x64: o MPEG2.dll dos jogos é i386 e não carrega num processo de 64
 * bits. Usa a libmpeg2 (mesma API) linkada estaticamente; ela não traz a
 * libmpeg2convert, então o quadro sai em YUV 4:2:0 e é convertido para RGB565
 * aqui (mesmo formato final do mpeg2convert_rgb16 do original). */
#include <mpeg2dec/mpeg2.h>
static uint16_t* g_rgb16;
static size_t    g_rgb16Size;

static void movie_yuv_to_rgb565(const mp2_fbuf* fb, const mp2_sequence* sq) {
    unsigned w = sq->width, h = sq->height;
    size_t need = (size_t)w * h;
    if (need > g_rgb16Size) {
        uint16_t* p = (uint16_t*)realloc(g_rgb16, need * sizeof(uint16_t));
        if (!p) return;
        g_rgb16 = p; g_rgb16Size = need;
    }
    unsigned cw = sq->chroma_width, ch = sq->chroma_height;
    unsigned sx = cw ? w / cw : 2, sy = ch ? h / ch : 2;
    if (!sx) sx = 1;
    if (!sy) sy = 1;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t* Y = fb->buf[0] + (size_t)y * w;
        const uint8_t* U = fb->buf[1] + (size_t)(y / sy) * cw;
        const uint8_t* V = fb->buf[2] + (size_t)(y / sy) * cw;
        uint16_t* out = g_rgb16 + (size_t)y * w;
        for (unsigned x = 0; x < w; x++) {
            /* ITU-R BT.601, faixa 16..235 (a mesma da libmpeg2convert) */
            int c = ((int)Y[x] - 16) * 298;
            int d = (int)U[x / sx] - 128, e = (int)V[x / sx] - 128;
            int r = (c + 409 * e + 128) >> 8;
            int g = (c - 100 * d - 208 * e + 128) >> 8;
            int b = (c + 516 * d + 128) >> 8;
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
            out[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

static bool movie_load_dll(void) {
    if (p_init) return true;
    p_init    = (fn_init)   (void*)mpeg2_init;
    p_close   = (fn_close)  (void*)mpeg2_close;
    p_info    = (fn_info)   (void*)mpeg2_info;
    p_parse   = (fn_parse)  (void*)mpeg2_parse;
    p_buffer  = (fn_buffer) (void*)mpeg2_buffer;
    p_convert = NULL;   /* sem libmpeg2convert: conversão em movie_upload */
    p_rgb16   = NULL;
    return true;
}
#else
static bool movie_load_dll(void) {
    if (g_mpegDll) return true;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\MPEG2.dll", g_game.currentDirectory);
    g_mpegDll = LoadLibraryA(path);
    if (!g_mpegDll) { Log_Print("MOVIE: falha ao carregar '%s'\n", path); return false; }
    p_init    = (fn_init)   GetProcAddress(g_mpegDll, "mpeg2_init");
    p_close   = (fn_close)  GetProcAddress(g_mpegDll, "mpeg2_close");
    p_info    = (fn_info)   GetProcAddress(g_mpegDll, "mpeg2_info");
    p_parse   = (fn_parse)  GetProcAddress(g_mpegDll, "mpeg2_parse");
    p_buffer  = (fn_buffer) GetProcAddress(g_mpegDll, "mpeg2_buffer");
    p_convert = (fn_convert)GetProcAddress(g_mpegDll, "mpeg2_convert");
    p_rgb16   = (void*)     GetProcAddress(g_mpegDll, "mpeg2convert_rgb16");
    if (!p_init || !p_close || !p_info || !p_parse || !p_buffer || !p_convert || !p_rgb16) {
        Log_Print("MOVIE: MPEG2.dll sem as funcoes esperadas\n");
        FreeLibrary(g_mpegDll); g_mpegDll = NULL;
        return false;
    }
    return true;
}
#endif

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
    if (!movie_load_dll()) return false;

    FILE* f = fopen(path, "rb");
    if (!f) { Log_Print("MOVIE: falha ao abrir '%s'\n", path); return false; }
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
        g_mov->dec = p_init(0);
        if (!g_mov->dec) { fclose(f); g_mov->f = NULL; return false; }
        g_mov->info = p_info(g_mov->dec);
        g_mov->loop = loop;
        g_mov->time = 0;
        g_mov->target = 0;
        g_mov->decoded = 0;
        g_mov->ended = false;
        g_mov->hasFrame = false;
        if (!g_mov->tex) glGenTextures(1, &g_mov->tex);
        Log_Print("MOVIE: '%s' aberto (MPEG puro, %.3f fps, loop=%d)\n", path, g_mov->fps, loop);
        Movie_Update(0.0f);
        return true;
    }
    /* Zero (piu 0x80a30e0): "MOV3" tem 16 B de chave extra antes do lixo de
     * hdr[0x88] bytes; o resto (chave, nome cifrado, G) segue o layout do MOV2
     * e o vídeo começa em 0xD0 + N. O nome só é conferido pelo original. */
    int mov3 = hdrGot == sizeof(hdr) && memcmp(hdr, "MOV3", 4) == 0;
    if (hdrGot != sizeof(hdr) || (!mov3 && memcmp(hdr, "MOV2", 4) != 0)) {
        Log_Print("MOVIE: '%s' nao e MOV2/MOV3\n", path);
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

    g_mov->dec = p_init(0);
    if (!g_mov->dec) { fclose(f); g_mov->f = NULL; return false; }
    g_mov->info = p_info(g_mov->dec);
    g_mov->loop = loop;
    g_mov->time = 0;
    g_mov->target = 0;
    g_mov->decoded = 0;
    g_mov->ended = false;
    g_mov->hasFrame = false;
    if (!g_mov->tex) glGenTextures(1, &g_mov->tex);
    Log_Print("MOVIE: '%s' aberto (N=0x%X, %.3f fps, loop=%d)\n", path, n, g_mov->fps, loop);
    Movie_Update(0.0f);   /* NX 0x8094990: já sai com o 1º quadro (sem quadro preto na troca) */
    return true;
}

void Movie_Close(void) {
    if (g_mov->dec) { p_close(g_mov->dec); g_mov->dec = NULL; }
    if (g_mov->f) { fclose(g_mov->f); g_mov->f = NULL; }
    free(g_mov->mem); g_mov->mem = NULL; g_mov->memSize = g_mov->memPos = 0;
    g_mov->hasFrame = false;
}

/* Extra do port: lê o vídeo (dataStart..EOF) para a memória e desembaralha de
 * uma vez. Chamado no loading da música, com o PNZ na tela. O Movie_Open da
 * NX já decodificou o 1º quadro lendo alguns blocos do disco: a leitura em
 * memória continua exatamente de onde o arquivo parou (sem repetir blocos). */
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

/* Extra do port (paridade com Exceed/Exceed2/Zero): na NX o Movie_Open já sai
 * com o 1º quadro (0x8094990), então aqui não há nada a fazer. */
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
    const mp2_fbuf* fb = g_mov->info->display_fbuf;
    const mp2_sequence* sq = g_mov->info->sequence;
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
#ifdef PUMPY_LIBMPEG2
    movie_yuv_to_rgb565(fb, sq);
    if (!g_rgb16) return;
    pixels = g_rgb16;
#endif
    /* Extra do port: aloca a textura só no 1º quadro do vídeo e depois só
     * reescreve os pixels. Recriar a textura a cada quadro obriga o driver a
     * realocar memória de vídeo e pode travar um frame.
     * era: glTexImage2D em todo quadro. */
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
        int st = p_parse(g_mov->dec);
        if (st == 1) {
            if (p_convert) p_convert(g_mov->dec, p_rgb16, NULL);
        } else if (st == 0) {
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
            p_buffer(g_mov->dec, g_mov->buf, g_mov->buf + n);
        } else if (st == 7 || st == 8) {
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
    /* Restaura o blend no fim: o gameplay desenha por cima contando com o
     * estado que estava ligado (sem isso os sprites saem com fundo). */
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

#else /* sem MPEG2.dll fora do Windows */

bool Movie_Open(const char* path, bool loop) {
    (void)loop;
    Log_Print("MOVIE: '%s' ignorado (MPEG2.dll so no Windows)\n", path);
    return false;
}
void Movie_Close(void) {}
bool Movie_IsOpen(void) { return false; }
void Movie_Select(int slot) { (void)slot; }
unsigned Movie_GLTexture(void) { return 0u; }
bool Movie_HasEnded(void) { return true; }
int  Movie_GetDecoded(void) { return 0; }
void Movie_Update(float dt) { (void)dt; }
void Movie_Render(void) {}
bool Movie_Preload(void) { return false; }
void Movie_Prime(void) {}
void Movie_RenderRect(float x0, float y0, float x1, float y1, float c, float alpha) {
    (void)x0; (void)y0; (void)x1; (void)y1; (void)c; (void)alpha;
}

#endif
