#include "pumpy.h"
#include <direct.h>

/* Persistência no formato da NX (piu, Linux): "/SETTINGS/PIUNX.INI".
 *
 * Imagem crua de 4096 bytes em [0x9e3cd00] (offset = endereço - 0x9e3cd00):
 *   0x8061950 carrega (fread 0x1000), 0x8061ac0 grava (versão + checksum + tempo + fwrite),
 *   0x8061730 padrão (tudo 0xFF, 0x8061830 e 0x80618d0, zera contadores).
 *
 *   +0xB9D  "NX10" (0x3031584E)                                 0x9e3d89d
 *   +0xBA1  checksum Adler-32 dos 8 bytes de +0xECE (0x805e760)  0x9e3d8a1
 *   +0xBA5  tempo acumulado (u32, somado a cada gravação)         0x9e3d8a5
 *   +0xBA9  LANGUAGE         (SETUP id 9;  0 KR 1 EN 2 ES 3 CHT)
 *   +0xBAA  STATISTICS: 200 x u32, jogos por música ("[%04d]", 0x8088961)
 *   +0xECA  DEFAULT STATION  (id 16, u32; a CStation lê em 0x808ad7c)
 *   +0xECE  GAME MODE        (id 11; 0 NORMAL 1 EVENT)          -+
 *   +0xECF  LEVEL            (id 12; 0 EASY 1 NORMAL 2 HARD)     |
 *   +0xED0  STAGE BREAK      (id 13; 0 OFF, 1..4)                |
 *   +0xED1  DEMO SOUND       (id 14; 0 OFF 1 ON)                 | checksum
 *   +0xED2  SHOW HELP        (id 15; 0 OFF 1 ON)                 | (8 bytes)
 *   +0xED3  MERCY TICKET     (id 17)                             |
 *   +0xED4  SCORE PER TICKET (id 18)                             |
 *   +0xED5  CREDIT LIMIT     (id 33)                            -+
 *   +0xED6  COIN1 SETTING    (id 31; 0 FREE PLAY, n moedas = 1 crédito)
 *   +0xED7  COIN2 SETTING    (id 32)
 *   +0xED8..+0xF0E  zerados no padrão (55 bytes, uso não identificado)
 *   +0xF0F/+0xF13/+0xF17  u32 não identificados (F13 padrão = max(90, 6*[0x81db100][0x16]))
 *   +0xF1B  bookkeeping COIN 1  (u32, 0x804e65f)
 *   +0xF1F  bookkeeping COIN 2  (u32, 0x804e758)
 *   +0xF23  bookkeeping SERVICE (u32, 0x804e743)
 *   +0xF27  moedas acumuladas para o próximo crédito (u32, 0x804e67f) — provável
 *   +0xF2B  créditos (u32, limite 99: 0x804dca3)
 *   +0xF2F/+0xF33  TICKET 1 / TICKET 2 (u32)
 *   +0xF38  200 bytes por música (0x8061bb0: música ligada = byte != 0)
 *
 * IDs e nomes: SCRIPT/SETUP_EN.LUA; o índice do switch de 0x8085703 é id - 9.
 * Campos sem equivalente em g_game ficam na imagem e são preservados ao regravar.
 *
 * Diferenças deliberadas:
 *  - Arquivo em <pasta do jogo>/SETTINGS/PIUNX.INI (o original usa a raiz "/").
 *  - COIN1 padrão = 0 (FREE PLAY). O original usa 1 ou 5 conforme /SCRIPT/KEY.LUA;
 *    um arquivo novo deixaria o jogo sem crédito num PC sem moedeiro.
 *  - 0x8061a86 zera 0x100 bytes a partir de +0xF38 (passa 0x38 bytes do fim da
 *    imagem, sobre globais 0x9e3dd00..); aqui só os 0xC8 que cabem.
 *
 * Conversão para g_game (que segue a convenção do Prex3):
 *  - svcDemoSound: 0 = ON (NX: 1 = ON)
 *  - svcCoinTotal: acumulador em moedas = créditos * COIN1 + moedas pendentes */

#define EEP_SIZE       0x1000
#define EEP_IDENT      0xB9D
#define EEP_CHKSUM     0xBA1
#define EEP_TIME       0xBA5
#define EEP_SETTINGS   0xECE
#define EEP_SETLEN     8
#define EEP_IDENT_VAL  0x3031584Eu   /* "NX10" */

#define O_LANG       0xBA9
#define O_STATS      0xBAA
#define O_STATION    0xECA
#define O_MODE       0xECE
#define O_LEVEL      0xECF
#define O_STAGEBRK   0xED0
#define O_DEMO       0xED1
#define O_HELP       0xED2
#define O_MERCY      0xED3
#define O_SCORETKT   0xED4
#define O_CREDLIMIT  0xED5
#define O_COIN1      0xED6
#define O_COIN2      0xED7
#define O_UNK_ED8    0xED8
#define O_UNK_F0F    0xF0F
#define O_UNK_F13    0xF13
#define O_UNK_F17    0xF17
#define O_COIN1TOT   0xF1B
#define O_COIN2TOT   0xF1F
#define O_SVCTOT     0xF23
#define O_COINS      0xF27
#define O_CREDITS    0xF2B
#define O_TICKET1    0xF2F
#define O_TICKET2    0xF33
#define O_SONGS      0xF38
#define SONG_COUNT   0xC8

#define EEP_DEFAULT_COIN1  0   /* FREE PLAY (o original vem com 1 ou 5) */

static uint8_t  g_eep[EEP_SIZE];
static uint32_t g_eepTime;   /* timeGetTime da última gravação (tempo de +0xBA5) */

static uint32_t eepGet32(int off) {
    return (uint32_t)g_eep[off] | ((uint32_t)g_eep[off + 1] << 8) |
           ((uint32_t)g_eep[off + 2] << 16) | ((uint32_t)g_eep[off + 3] << 24);
}

static void eepPut32(int off, uint32_t v) {
    g_eep[off]     = (uint8_t)(v);
    g_eep[off + 1] = (uint8_t)(v >> 8);
    g_eep[off + 2] = (uint8_t)(v >> 16);
    g_eep[off + 3] = (uint8_t)(v >> 24);
}

/* Adler-32 com valor inicial 1 (piu 0x805e760: módulo 0xFFF1). */
static uint32_t eepAdler32(const uint8_t* d, int n) {
    uint32_t a = 1, b = 0;
    for (int i = 0; i < n; i++) {
        a = (a + d[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void eepStamp(void) {
    eepPut32(EEP_IDENT, EEP_IDENT_VAL);
    eepPut32(EEP_CHKSUM, eepAdler32(&g_eep[EEP_SETTINGS], EEP_SETLEN));
}

/* piu 0x8061730: tudo 0xFF, 0x8061830 (GAME SETTING), 0x80618d0 (COIN SETTING), contadores zerados. */
static void eepDefaults(void) {
    memset(g_eep, 0xFF, sizeof(g_eep));
    /* 0x8061830 */
    g_eep[O_MODE]      = 0;
    g_eep[O_LEVEL]     = 1;
    g_eep[O_STAGEBRK]  = 2;
    g_eep[O_DEMO]      = 1;
    g_eep[O_HELP]      = 1;
    g_eep[O_LANG]      = 1;   /* sete [0x9e3dd08]: KEY.LUA ausente -> 1 (ENGLISH) */
    g_eep[O_MERCY]     = 0;
    g_eep[O_SCORETKT]  = 0;
    g_eep[O_CREDLIMIT] = 0;
    /* 0x80618d0 */
    g_eep[O_COIN1]     = EEP_DEFAULT_COIN1;
    g_eep[O_COIN2]     = 1;
    /* 0x8061766.. */
    for (int i = 0; i < 200; i++) eepPut32(O_STATS + 4 * i, 0);
    eepPut32(O_STATION, 0);
    eepPut32(O_COIN1TOT, 0);
    eepPut32(O_COIN2TOT, 0);
    eepPut32(O_SVCTOT, 0);
    eepPut32(O_COINS, 0);
    eepPut32(O_CREDITS, 0);
    eepPut32(O_TICKET1, 0);
    eepPut32(O_TICKET2, 0);
    eepPut32(EEP_TIME, 0);
    memset(&g_eep[O_UNK_ED8], 0, 0x37);
    eepPut32(O_UNK_F0F, 0);
    eepPut32(O_UNK_F17, 0);
    eepPut32(O_UNK_F13, 90);   /* max(0x5a, 6 * [0x81db100][0x16]); tabela não lida -> 0x5a */
    eepStamp();
}

static void eepPath(char* out, size_t n) {
    snprintf(out, n, "%s/SETTINGS/PIUNX.INI", g_game.currentDirectory);
}

static void eepToGame(void) {
    g_game.svcGameMode      = g_eep[O_MODE];
    g_game.optionDifficulty = g_eep[O_LEVEL];
    g_game.optionToggle1    = g_eep[O_STAGEBRK];
    g_game.svcLangOption    = g_eep[O_LANG];
    g_game.svcDemoSound     = g_eep[O_DEMO] ? 0 : 1;      /* NX 1=ON -> projeto 0=ON */
    g_game.optionToggle2    = g_eep[O_HELP];
    g_game.svcCoin1         = g_eep[O_COIN1];
    g_game.svcCoin2         = g_eep[O_COIN2];
    g_game.svcCoin1Total    = (int)eepGet32(O_COIN1TOT);
    g_game.svcCoin2Total    = (int)eepGet32(O_COIN2TOT);
    g_game.svcServiceTotal  = (int)eepGet32(O_SVCTOT);
    g_game.svcCoinTotal     = (int)(eepGet32(O_CREDITS) * g_game.svcCoin1 + eepGet32(O_COINS));
}

static void eepFromGame(void) {
    g_eep[O_MODE]     = (uint8_t)g_game.svcGameMode;
    g_eep[O_LEVEL]    = (uint8_t)g_game.optionDifficulty;
    g_eep[O_STAGEBRK] = (uint8_t)g_game.optionToggle1;
    g_eep[O_LANG]     = (uint8_t)g_game.svcLangOption;
    g_eep[O_DEMO]     = g_game.svcDemoSound == 0 ? 1 : 0;
    g_eep[O_HELP]     = (uint8_t)g_game.optionToggle2;
    g_eep[O_COIN1]    = (uint8_t)g_game.svcCoin1;
    g_eep[O_COIN2]    = (uint8_t)g_game.svcCoin2;
    eepPut32(O_COIN1TOT, (uint32_t)g_game.svcCoin1Total);
    eepPut32(O_COIN2TOT, (uint32_t)g_game.svcCoin2Total);
    eepPut32(O_SVCTOT,   (uint32_t)g_game.svcServiceTotal);
    if (g_game.svcCoin1 > 0) {
        eepPut32(O_CREDITS, (uint32_t)(g_game.svcCoinTotal / g_game.svcCoin1));
        eepPut32(O_COINS,   (uint32_t)(g_game.svcCoinTotal % g_game.svcCoin1));
    } else {
        eepPut32(O_CREDITS, 0);
        eepPut32(O_COINS,   0);
    }
}

/* Grava a imagem (piu 0x8061ac0): versão, checksum, +0xBA5 += tempo decorrido, fwrite 0x1000. */
void Eeprom_Save(void) {
    char path[MAX_PATH], dir[MAX_PATH];
    eepFromGame();
    eepStamp();
    uint32_t now = timeGetTime();
    if (g_eepTime) eepPut32(EEP_TIME, eepGet32(EEP_TIME) + (now - g_eepTime));
    g_eepTime = now;
    snprintf(dir, sizeof(dir), "%s/SETTINGS", g_game.currentDirectory);
    _mkdir(dir);
    eepPath(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) {
        Log_Print("EEPROM: cannot write '%s'\n", path);
        return;
    }
    fwrite(g_eep, 1, sizeof(g_eep), f);
    fclose(f);
}

/* Migração (extra deste projeto): importa as opções do pumpprex3.ini antigo
 * (imagem do PUMPY.EXE: "X3.1" em +0x68C, opções em +0x7E8, mesma convenção de g_game). */
static bool eepImportPrex3(void) {
    char path[MAX_PATH];
    uint8_t old[0x800];
    snprintf(path, sizeof(path), "%s/pumpprex3.ini", g_game.currentDirectory);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(old, 1, sizeof(old), f);
    fclose(f);
#define OLD32(o) ((uint32_t)old[o] | ((uint32_t)old[(o) + 1] << 8) | \
                  ((uint32_t)old[(o) + 2] << 16) | ((uint32_t)old[(o) + 3] << 24))
    if (got != sizeof(old) || OLD32(0x68C) != 0x312E3358u) return false;
    g_game.svcGameMode      = old[0x7E8];
    g_game.optionDifficulty = old[0x7E9];
    g_game.optionToggle1    = old[0x7EA];
    g_game.svcLangOption    = old[0x7EB];
    g_game.svcDemoSound     = old[0x7EC];
    g_game.optionToggle2    = old[0x7ED];
    g_game.svcCoin1         = old[0x7EE];
    g_game.svcCoin2         = old[0x7EF];
    g_game.svcCoin1Total    = (int)OLD32(0x7F0);
    g_game.svcCoin2Total    = (int)OLD32(0x7F4);
    g_game.svcCoinTotal     = (int)OLD32(0x7F8);
    g_game.svcServiceTotal  = (int)OLD32(0x7FC);
#undef OLD32
    Log_Print("EEPROM: opções importadas de %s\n", path);
    return true;
}

/* Lê e valida a imagem (piu 0x8061950) e aplica em g_game.
 * Retorno: 1 = arquivo válido; 0 = inválido (resetado e regravado, como o original);
 *          -1 = ausente (padrão, ou importado do pumpprex3.ini; já gravado). */
int Eeprom_Load(void) {
    char path[MAX_PATH];
    eepPath(path, sizeof(path));
    Log_Print("EEPROM: loading %s\n", path);
    g_eepTime = timeGetTime();

    FILE* f = fopen(path, "rb");
    if (!f) {                       /* 0x8061aa5: padrão e grava */
        eepDefaults();
        eepToGame();
        eepImportPrex3();
        Eeprom_Save();
        return -1;
    }
    size_t got = fread(g_eep, 1, sizeof(g_eep), f);
    fclose(f);

    bool dirty = false;
    int ret = 1;
    if (got != sizeof(g_eep) || eepGet32(EEP_IDENT) != EEP_IDENT_VAL) {
        Log_Print("Warning - SAVE DATA VERSION IS NOT MATCH\n");
        eepDefaults();
        dirty = true; ret = 0;
    } else if (eepGet32(EEP_CHKSUM) != eepAdler32(&g_eep[EEP_SETTINGS], EEP_SETLEN)) {
        Log_Print("Warning - Save Data chksum error\n");
        eepDefaults();
        dirty = true; ret = 0;
    }
    if (g_eep[O_SONGS] == 0xFF) {   /* 0x8061a86 */
        memset(&g_eep[O_SONGS], 0, SONG_COUNT);
        dirty = true;
    }
    eepToGame();
    if (dirty) Eeprom_Save();       /* 0x8061a76 */
    if (g_eep[O_MERCY] == 0xFF || g_eep[O_SCORETKT] == 0xFF) {   /* 0x8061a56, sem regravar */
        g_eep[O_MERCY] = 0;
        g_eep[O_SCORETKT] = 0;
        eepPut32(O_TICKET1, 0);
        eepPut32(O_TICKET2, 0);
    }
    return ret;
}

#if 0   /* DESATIVADO (02/10/2026): imagem do Prex3 (pumpprex3.ini, 2048 bytes, PUMPY.EXE),
         * substituída pela da NX acima. Preservado; tem comentários internos, por isso #if 0. */

/* Persistência no formato do PUMPY.EXE (EEPROM do gabinete).
 *
 * O original guarda uma imagem binária de 2048 bytes em "c:\pumpprex3.ini"
 * (fread(...,1,0x800,...) em 0x4067a0; defaults em 0x405150; gravação em 0x405190).
 * Aqui o arquivo é "pumpprex3.ini" na pasta do jogo. Layout (offset = endereço - 0xd38858):
 *
 *   +0x68C  ident  0x312E3358 ("X3.1")           0xd38ee4
 *   +0x690  checksum Adler-32 dos 8 bytes de +0x7E8   0xd38ee8   (0x4195d0 -> 0x419570)
 *   +0x7E8  GAME MODE   (0=NORMAL, 1=EVENT)       0xd39040
 *   +0x7E9  LEVEL       (0=EASY 1=NORMAL 2=HARD)  0xd39041
 *   +0x7EA  STAGE BREAK (0=OFF, 1..4 stages)      0xd39042  default 2
 *   +0x7EB  LANGUAGE                              0xd39043  default 1
 *   +0x7EC  DEMO SOUND  (0=ON)                    0xd39044
 *   +0x7ED  SHOW HELP                             0xd39045
 *   +0x7EE  COIN 1 (moedas por crédito)           0xd39046  default 5
 *   +0x7EF  COIN 2                                0xd39047  default 1
 *   +0x7F0  bookkeeping COIN 1  (u32)             0xd39048  (tela BOOKKEEPING 0x406360)
 *   +0x7F4  bookkeeping COIN 2  (u32)             0xd3904c
 *   +0x7F8  total de moedas     (u32)             0xd39050
 *   +0x7FC  bookkeeping SERVICE (u32)             0xd39054
 *
 * O restante da imagem (ranking etc.) fica como está: é preservado ao regravar.
 *
 * Diferença deliberada: o default do COIN 1 é 0 (FREE PLAY) em vez de 5, senão
 * um arquivo novo deixaria o jogo sem crédito num PC sem moedeiro. */

#define EEP_SIZE       0x800
#define EEP_IDENT      0x68C
#define EEP_CHKSUM     0x690
#define EEP_SETTINGS   0x7E8
#define EEP_SETLEN     8
#define EEP_IDENT_VAL  0x312E3358u   /* "X3.1" */

#define O_MODE      0x7E8
#define O_LEVEL     0x7E9
#define O_STAGEBRK  0x7EA
#define O_LANG      0x7EB
#define O_DEMO      0x7EC
#define O_HELP      0x7ED
#define O_COIN1     0x7EE
#define O_COIN2     0x7EF
#define O_COIN1TOT  0x7F0
#define O_COIN2TOT  0x7F4
#define O_COINTOT   0x7F8
#define O_SVCTOT    0x7FC

#define EEP_DEFAULT_COIN1  0   /* FREE PLAY (o original vem com 5) */

static uint8_t g_eep[EEP_SIZE];

static uint32_t eepGet32(int off) {
    return (uint32_t)g_eep[off] | ((uint32_t)g_eep[off + 1] << 8) |
           ((uint32_t)g_eep[off + 2] << 16) | ((uint32_t)g_eep[off + 3] << 24);
}

static void eepPut32(int off, uint32_t v) {
    g_eep[off]     = (uint8_t)(v);
    g_eep[off + 1] = (uint8_t)(v >> 8);
    g_eep[off + 2] = (uint8_t)(v >> 16);
    g_eep[off + 3] = (uint8_t)(v >> 24);
}

/* Adler-32 com valor inicial 1 (PUMPY.EXE 0x419570: módulo 0xFFF1). */
static uint32_t eepAdler32(const uint8_t* d, int n) {
    uint32_t a = 1, b = 0;
    for (int i = 0; i < n; i++) {
        a = (a + d[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void eepStamp(void) {
    eepPut32(EEP_IDENT, EEP_IDENT_VAL);
    eepPut32(EEP_CHKSUM, eepAdler32(&g_eep[EEP_SETTINGS], EEP_SETLEN));
}

/* PUMPY.EXE 0x405150: tudo 0xFF, depois os defaults de 0x404f40/0x404fa0 e os totais zerados. */
static void eepDefaults(void) {
    memset(g_eep, 0xFF, sizeof(g_eep));
    g_eep[O_MODE]     = 0;
    g_eep[O_LEVEL]    = 1;
    g_eep[O_STAGEBRK] = 2;
    g_eep[O_LANG]     = 1;
    g_eep[O_DEMO]     = 0;
    g_eep[O_HELP]     = 0;
    g_eep[O_COIN1]    = EEP_DEFAULT_COIN1;
    g_eep[O_COIN2]    = 1;
    eepPut32(O_COIN1TOT, 0);
    eepPut32(O_COIN2TOT, 0);
    eepPut32(O_COINTOT, 0);
    eepPut32(O_SVCTOT, 0);
    eepStamp();
}

static void eepPath(char* out, size_t n) {
    snprintf(out, n, "%s/pumpprex3.ini", g_game.currentDirectory);
}

static void eepToGame(void) {
    g_game.svcGameMode     = g_eep[O_MODE];
    g_game.optionDifficulty = g_eep[O_LEVEL];
    g_game.optionToggle1   = g_eep[O_STAGEBRK];
    g_game.svcLangOption   = g_eep[O_LANG];
    g_game.svcDemoSound    = g_eep[O_DEMO];
    g_game.optionToggle2   = g_eep[O_HELP];
    g_game.svcCoin1        = g_eep[O_COIN1];
    g_game.svcCoin2        = g_eep[O_COIN2];
    g_game.svcCoin1Total   = (int)eepGet32(O_COIN1TOT);
    g_game.svcCoin2Total   = (int)eepGet32(O_COIN2TOT);
    g_game.svcCoinTotal    = (int)eepGet32(O_COINTOT);
    g_game.svcServiceTotal = (int)eepGet32(O_SVCTOT);
}

static void eepFromGame(void) {
    g_eep[O_MODE]     = (uint8_t)g_game.svcGameMode;
    g_eep[O_LEVEL]    = (uint8_t)g_game.optionDifficulty;
    g_eep[O_STAGEBRK] = (uint8_t)g_game.optionToggle1;
    g_eep[O_LANG]     = (uint8_t)g_game.svcLangOption;
    g_eep[O_DEMO]     = (uint8_t)g_game.svcDemoSound;
    g_eep[O_HELP]     = (uint8_t)g_game.optionToggle2;
    g_eep[O_COIN1]    = (uint8_t)g_game.svcCoin1;
    g_eep[O_COIN2]    = (uint8_t)g_game.svcCoin2;
    eepPut32(O_COIN1TOT, (uint32_t)g_game.svcCoin1Total);
    eepPut32(O_COIN2TOT, (uint32_t)g_game.svcCoin2Total);
    eepPut32(O_COINTOT,  (uint32_t)g_game.svcCoinTotal);
    eepPut32(O_SVCTOT,   (uint32_t)g_game.svcServiceTotal);
    eepStamp();
}

/* Grava a imagem (PUMPY.EXE 0x405190). */
void Eeprom_Save(void) {
    char path[MAX_PATH];
    eepFromGame();
    eepPath(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) {
        Log_Print("EEPROM: cannot write '%s'\n", path);
        return;
    }
    fwrite(g_eep, 1, sizeof(g_eep), f);
    fclose(f);
}

/* Lê e valida a imagem (PUMPY.EXE 0x4067a0) e aplica em g_game.
 * Retorno: 1 = arquivo válido; 0 = arquivo inválido (resetado, com o aviso do original);
 *          -1 = arquivo ausente (defaults em g_game; quem chamou decide migrar/gravar). */
int Eeprom_Load(void) {
    char path[MAX_PATH];
    eepPath(path, sizeof(path));
    Log_Print("EEPROM: loading %s\n", path);

    FILE* f = fopen(path, "rb");
    if (!f) {
        eepDefaults();
        eepToGame();
        return -1;
    }
    size_t got = fread(g_eep, 1, sizeof(g_eep), f);
    fclose(f);

    if (got != sizeof(g_eep) || eepGet32(EEP_IDENT) != EEP_IDENT_VAL) {
        Log_Print("Warning - ident mismatch\n");
        eepDefaults();
        eepToGame();
        Eeprom_Save();               /* o original reseta e regrava neste caso */
        return 0;
    }
    if (eepGet32(EEP_CHKSUM) != eepAdler32(&g_eep[EEP_SETTINGS], EEP_SETLEN)) {
        Log_Print("Warning - EEPROM chksum err\n");
        eepDefaults();               /* e não regrava, como o original */
        eepToGame();
        return 0;
    }
    eepToGame();
    return 1;
}

#endif
