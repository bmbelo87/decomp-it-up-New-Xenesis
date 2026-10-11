/*
 * old_save.c — save do Exceed2 (PIU32.EXE), arquivo ".\PIUEXCEED2.INI".
 *
 * Imagem crua de 2048 bytes ([0x484FE4]):
 *   0x422060 carrega (fread 0x800), 0x422190 grava (versão + checksum + fwrite),
 *   0x421DE0 padrão: tudo 0xFF, 0x421FC0 / 0x422020 / 0x421E30, zera +0x7F4,
 *            +0x7F8, +0x7FC, +0x522 e +0x526.
 *
 *   +0x074B + 4k  pontuação do ranking, posição k (u32, 20 posições)
 *   +0x079B + 4k  nome do ranking (4 bytes)      padrão "EXC2", 10000..500 (0x421E30)
 *   +0x0520/+0x521 bytes, +0x522/+0x526 u32 (0x4191EB)
 *   +0x0525       arcade (piu 0x8063743): Canon D FULL REMIX liberada. No PC esse
 *                 byte é o último do u32 +0x522 — ver Eeprom2_*Canon.
 *   +0x052A       versão da tabela de músicas (= [0x456398] = 105)
 *   +0x052B + i   música i desligada (0x412F00 -> +0x36 da tabela)
 *   +0x062B       "EX02" (0x32305845)
 *   +0x062F       checksum: Adler-32 dos 8 bytes em +0x7EC (0x423B40)
 *   +0x0633 + 2i  contagem de jogos da música i (u16, 0x4168FA)
 *   +0x07EC..0x7F3 configurações (0x421FC0 / 0x422020):
 *                 7EC=0 7ED=1 7EE=2 7EF=0 (idioma: 0 -> 90H) 7F0=1 7F1=0 7F2=5 7F3=1
 */
#include "pumpy.h"

#define OSV_SIZE        0x800
#define OSV_RANK_SCORE  0x74B
#define OSV_RANK_NAME   0x79B
#define OSV_SONGVER     0x52A
#define OSV_SONGOFF     0x52B
#define OSV_IDENT       0x62B
#define OSV_CHKSUM      0x62F
#define OSV_PLAYCNT     0x633
#define OSV_SETTINGS    0x7EC
#define OSV_IDENT_VAL   0x32305845u   /* "EX02" */
#define OSV_SONG_VER    105           /* [0x456398] */
/* Contagem de jogos: 2 bytes por música a partir de OSV_PLAYCNT. A imagem é a do Exceed 2
 * (0x800 bytes, tabela de 138 músicas); a tabela da NX cresceu (250 com o USE OLD DATA
 * SONGS) e o laço passava do fim da imagem (falha 0xc0000409 ao entrar no Enter Your Name).
 * Só as músicas que cabem antes do ranking (OSV_RANK_SCORE) têm contagem. */
#define OSV_PLAYCNT_MAX ((OSV_RANK_SCORE - OSV_PLAYCNT) / 2)   /* 140 */

static uint8_t g_oldSave[OSV_SIZE];
static bool    g_oldSaveLoaded = false;

static void put32(int off, uint32_t v) {
    g_oldSave[off] = (uint8_t)v; g_oldSave[off + 1] = (uint8_t)(v >> 8);
    g_oldSave[off + 2] = (uint8_t)(v >> 16); g_oldSave[off + 3] = (uint8_t)(v >> 24);
}
static uint32_t get32(int off) {
    return (uint32_t)g_oldSave[off] | ((uint32_t)g_oldSave[off + 1] << 8) |
           ((uint32_t)g_oldSave[off + 2] << 16) | ((uint32_t)g_oldSave[off + 3] << 24);
}

static uint32_t adler32(const uint8_t* d, int n) {   /* 0x423B40 */
    uint32_t a = 1, b = 0;
    for (int i = 0; i < n; i++) { a = (a + d[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

static void oldSavePath(char* out, size_t n) {
    snprintf(out, n, "%s/PIUEXCEED2.INI", g_game.currentDirectory);
}

/* 0x421DE0 */
static void oldSaveDefaults(void) {
    memset(g_oldSave, 0xFF, sizeof(g_oldSave));
    /* 0x421FC0 */
    put32(OSV_IDENT, OSV_IDENT_VAL);
    g_oldSave[0x7EC] = 0; g_oldSave[0x7ED] = 1; g_oldSave[0x7EE] = 2; g_oldSave[0x7F0] = 1;
    g_oldSave[0x7F1] = 0; g_oldSave[0x7EB] = 0; g_oldSave[0x7EF] = 0;
    g_oldSave[0x520] = 0; g_oldSave[0x521] = 0;
    /* 0x422020 */
    g_oldSave[0x7F2] = 5; g_oldSave[0x7F3] = 1; g_oldSave[0x7EB] = 0;
    /* 0x421E30: ranking padrão (0x410EA0(k, valor, "EXC2")) */
    for (int k = 0; k < 20; k++) {
        put32(OSV_RANK_SCORE + 4 * k, (uint32_t)(10000 - 500 * k));
        memcpy(&g_oldSave[OSV_RANK_NAME + 4 * k], "EXC2", 4);
    }
    /* era: i <= SONG_TABLE_COUNT, sem limite (estourava g_oldSave com a tabela da NX) */
    for (int i = 0; i <= SONG_TABLE_COUNT && i < OSV_PLAYCNT_MAX; i++) {          /* 0x421F94: contagem 0 */
        g_oldSave[OSV_PLAYCNT + 2 * i] = 0;
        g_oldSave[OSV_PLAYCNT + 2 * i + 1] = 0;
    }
    /* 0x421E05.. */
    put32(0x7F4, 0); put32(0x7F8, 0); put32(0x7FC, 0);
    put32(0x522, 0); put32(0x526, 0);
    put32(OSV_CHKSUM, adler32(&g_oldSave[OSV_SETTINGS], 8));
}

void OldSave_Save(void) {                                /* 0x422190 */
    put32(OSV_IDENT, OSV_IDENT_VAL);
    put32(OSV_CHKSUM, adler32(&g_oldSave[OSV_SETTINGS], 8));
    /* DESATIVADO (pedido do usuário): a NX guarda tudo em SETTINGS/ (PIUNX.INI,
     * RANK.DAT); o PIUEXCEED2.INI não é mais gravado. A imagem fica só na memória. */
#if 0
    char path[MAX_PATH];
    oldSavePath(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) { Log_Print("OLDSAVE: não foi possível gravar '%s'\n", path); return; }
    fwrite(g_oldSave, 1, OSV_SIZE, f);
    fclose(f);
#endif
}

/* 0x422060 */
void OldSave_Load(void) {
    if (g_oldSaveLoaded) return;
    g_oldSaveLoaded = true;
    bool dirty = false;
    /* DESATIVADO (pedido do usuário): o PIUEXCEED2.INI não é mais lido; a imagem
     * começa sempre nos valores padrão e vive só na memória. */
#if 0
    char path[MAX_PATH];
    oldSavePath(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (f && fread(g_oldSave, 1, OSV_SIZE, f) == OSV_SIZE) {
        fclose(f);
    } else {
        if (f) fclose(f);
        oldSaveDefaults();
        dirty = true;
    }
#endif
    oldSaveDefaults();
    if (get32(OSV_IDENT) != OSV_IDENT_VAL) {             /* 0x4220AF */
        Log_Print("OLDSAVE: Warning - SAVE DATA VERSION IS NOT MATCH\n");
        oldSaveDefaults();
        dirty = true;
    }
    if (get32(OSV_CHKSUM) != adler32(&g_oldSave[OSV_SETTINGS], 8)) {   /* 0x4220E9 */
        Log_Print("OLDSAVE: Warning - Save Data chksum error\n");
        oldSaveDefaults();
        dirty = true;
    }
    /* 0x422104: tabela de músicas mudou ou nunca foi gravada -> zera os desligamentos */
    if (g_oldSave[OSV_SONGVER] != OSV_SONG_VER || g_oldSave[OSV_SONGOFF] == 0xFF) {
        g_oldSave[OSV_SONGVER] = OSV_SONG_VER;
        memset(&g_oldSave[OSV_SONGOFF], 0, 256);            /* 0x412EF0 */
        dirty = true;
    }
    if (dirty) OldSave_Save();
    /* 0x42214E */
    if (g_oldSave[0x520] == 0xFF || g_oldSave[0x521] == 0xFF) {
        g_oldSave[0x520] = 0; g_oldSave[0x521] = 0;
        put32(0x522, 0); put32(0x526, 0);
    }
    /* era: Log_Print("OLDSAVE: '%s' carregado\n", path); */
    Log_Print("OLDSAVE: imagem padrão em memória (PIUEXCEED2.INI desativado)\n");
}

uint8_t* OldSave_Data(void) { OldSave_Load(); return g_oldSave; }

uint8_t OldSave_Language(void)  { OldSave_Load(); return g_oldSave[0x7EF]; }   /* 0x41335D */
bool    OldSave_SongOff(int i)  { OldSave_Load(); return i >= 0 && i < 256 && g_oldSave[OSV_SONGOFF + i] != 0; }

/* 0x4168FA: +1 na contagem de jogos da música (índice da tabela) */
void OldSave_CountPlay(int i) {
    OldSave_Load();
    if (i < 0 || i > SONG_TABLE_COUNT || i >= OSV_PLAYCNT_MAX) return;   /* era: sem o limite da imagem */
    int off = OSV_PLAYCNT + 2 * i;
    uint16_t v = (uint16_t)(g_oldSave[off] | (g_oldSave[off + 1] << 8));
    v++;
    g_oldSave[off] = (uint8_t)v; g_oldSave[off + 1] = (uint8_t)(v >> 8);
    OldSave_Save();
}

/* Arcade piu 0x8063743 / 0x806336E: +0x525 == 1 libera a B57. No PIU32 o byte
 * +0x525 é o mais alto do u32 +0x522 (0x4191EB); usado aqui como no arcade. */
bool OldSave_CanonUnlocked(void) { OldSave_Load(); return g_oldSave[0x525] == 1; }
void OldSave_SetCanonUnlocked(void) {
    OldSave_Load();
    g_oldSave[0x525] = 1;
    OldSave_Save();
}
