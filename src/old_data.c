/* old_data.c — extra do port: "USE OLD DATA SONGS" (GAME OPTION, só com GAME STYLE = MODDED).
 *
 * O disco do NX traz áudio, intro, BGA e step de músicas que saíram da lista do arcade,
 * mas sem a capa (TITLE), a prévia (BGA/PREVIEW) e o disco da seleção (BGA/TEST.DAT).
 * Com a opção ligada, o que falta é lido da instalação de uma versão anterior.
 *
 * Onde procurar (pedido do usuário): duas pastas acima da pasta do jogo.
 *   <raiz>/18_nx/game        <- pasta do jogo (g_game.currentDirectory)
 *   <raiz>/17_zero/game      <- versões anteriores: pastas "NN_nome" com NN menor
 *   <raiz>/16_exc2/game         que o da versão atual, da mais próxima para a mais antiga
 * O número da versão atual vem do nome da pasta-mãe ("18_nx" -> 18). Vale a primeira
 * versão que tiver o arquivo.
 *
 * Nomes procurados em cada versão (confirmados nas instalações do Zero e do Exceed 2):
 *   capa    TITLE/T<ID>E.PNZ ou T<ID>H.PNZ (Zero, por idioma), TITLE/T<ID>.PNZ (Exceed 2)
 *   prévia  BGA/PREVIEW/<ID>.MOV, BGA/PREVIEW/ENG|HAN/<ID>.MOV (Zero)
 *   disco   <ID>.TGA dentro de BGA/90.DAT (Zero), BGA/90E.DAT ou 90H.DAT (Exceed 2)
 * Os arquivos do Zero são cifrados com as respostas do dongle do Zero; elas estão na
 * tabela de dog_table.c junto com as do NX (tools/gen_zero_dog.py).
 */
#include "pumpy.h"
#include "step.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#endif

#define OLD_MAX 16

static char s_root[OLD_MAX][MAX_PATH];   /* "<raiz>/NN_nome/game" */
static int  s_num[OLD_MAX];
static int  s_count = -1;                /* -1 = ainda não procurou */

static bool fileExists(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static bool dirExists(const char* path)
{
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* "18_nx" -> 18; -1 se o nome não começa com dígitos seguidos de '_' */
static int versionNum(const char* name)
{
    int n = 0, i = 0;
    while (name[i] >= '0' && name[i] <= '9') { n = n * 10 + (name[i] - '0'); i++; }
    if (i == 0 || name[i] != '_') return -1;
    return n;
}

static void addRoot(const char* base, const char* name, int curNum)
{
    int num = versionNum(name);
    if (num < 0 || num >= curNum || s_count >= OLD_MAX) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s/game", base, name);
    if (!dirExists(path)) return;
    /* ordem decrescente: a versão mais próxima primeiro */
    int at = s_count;
    while (at > 0 && s_num[at - 1] < num) {
        s_num[at] = s_num[at - 1];
        strcpy(s_root[at], s_root[at - 1]);
        at--;
    }
    s_num[at] = num;
    strncpy(s_root[at], path, MAX_PATH - 1);
    s_root[at][MAX_PATH - 1] = 0;
    s_count++;
}

static void scan(void)
{
    s_count = 0;

    /* <raiz>/<versão>/<jogo>: corta os dois últimos componentes */
    char dir[MAX_PATH];
    strncpy(dir, g_game.currentDirectory, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    size_t len = strlen(dir);
    while (len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\')) dir[--len] = 0;

    char* cut[2] = { NULL, NULL };
    for (int k = 0; k < 2; k++) {
        char* a = strrchr(dir, '/');
        char* b = strrchr(dir, '\\');
        char* s = (a > b) ? a : b;
        if (!s) { Log_Print("OLDDATA: '%s' não tem duas pastas acima\n", g_game.currentDirectory); return; }
        *s = 0;
        cut[k] = s + 1;
    }
    /* cut[0] = pasta do jogo, cut[1] = pasta da versão, dir = raiz */
    int curNum = versionNum(cut[1]);
    if (curNum < 0) {
        Log_Print("OLDDATA: pasta da versão '%s' não tem o formato NN_nome\n", cut[1]);
        return;
    }

#if defined(_WIN32)
    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) addRoot(dir, fd.cFileName, curNum);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR* d = opendir(dir[0] ? dir : "/");
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != NULL) addRoot(dir, e->d_name, curNum);
        closedir(d);
    }
#endif

    Log_Print("OLDDATA: versão atual %d ('%s'), %d versão(ões) anterior(es) em '%s'\n",
              curNum, cut[1], s_count, dir);
    for (int i = 0; i < s_count; i++) Log_Print("OLDDATA:   %d -> '%s'\n", s_num[i], s_root[i]);
}

bool OldData_On(void)
{
    if (!g_game.gameStyleModded || !g_game.useOldData) return false;
    if (s_count < 0) scan();
    return true;
}

/* Capa da tela de título. Uma tentativa por id; quem chama percorre a cadeia de recurso. */
static bool findTitle1(int id, char* out, size_t n)
{
    /* idioma como no Zero (piu 0x8081594): coreano -> H, senão E; o outro fica de reserva */
    const char* lang = g_game.svcLangOption == 0 ? "HE" : "EH";
    for (int r = 0; r < s_count; r++) {
        for (int k = 0; k < 2; k++) {
            snprintf(out, n, "%s/TITLE/T%X%c.PNZ", s_root[r], (unsigned)id, lang[k]);
            if (fileExists(out)) goto found;
        }
        snprintf(out, n, "%s/TITLE/T%X.PNZ", s_root[r], (unsigned)id);
        if (fileExists(out)) goto found;
        snprintf(out, n, "%s/TITLE/%03X.PNZ", s_root[r], (unsigned)id);
        if (fileExists(out)) goto found;
    }
    return false;
found:
    Log_Print("OLDDATA: capa %X -> '%s'\n", (unsigned)id, out);
    return true;
}

/* Prévia da seleção de músicas. */
static bool findPreview1(int id, char* out, size_t n)
{
    const char* lang[2];
    lang[0] = g_game.svcLangOption == 0 ? "HAN" : "ENG";
    lang[1] = g_game.svcLangOption == 0 ? "ENG" : "HAN";
    for (int r = 0; r < s_count; r++) {
        snprintf(out, n, "%s/BGA/PREVIEW/%X.MOV", s_root[r], (unsigned)id);
        if (fileExists(out)) goto found;
        for (int k = 0; k < 2; k++) {
            snprintf(out, n, "%s/BGA/PREVIEW/%s/%X.MOV", s_root[r], lang[k], (unsigned)id);
            if (fileExists(out)) goto found;
        }
    }
    return false;
found:
    Log_Print("OLDDATA: prévia %X -> '%s'\n", (unsigned)id, out);
    return true;
}

/* Arquivo solto de uma versão anterior. rel é o caminho dentro da pasta do jogo com um
 * "%X" para o id: "BGA/%X.MOV", "BGA/%X.DAT", "AUDIO/%X.AUD". */
static bool findFile1(int id, const char* rel, char* out, size_t n)
{
    for (int r = 0; r < s_count; r++) {
        char name[64];
        snprintf(name, sizeof(name), rel, (unsigned)id);
        snprintf(out, n, "%s/%s", s_root[r], name);
        if (fileExists(out)) {
            Log_Print("OLDDATA: %X -> '%s'\n", (unsigned)id, out);
            return true;
        }
    }
    return false;
}

/* As buscas tentam o id da música e depois a cadeia de recurso do NX (NX_ResId): a
 * DB52 (Novasonic Remix) tem capa, prévia e disco guardados como B52 nas versões antigas. */
bool OldData_FindTitle(int id, char* out, size_t n)
{
    if (!OldData_On()) return false;
    for (int g = 0; id != -1 && g < 8; g++, id = NX_ResId(id))
        if (findTitle1(id, out, n)) return true;
    return false;
}

bool OldData_FindPreview(int id, char* out, size_t n)
{
    if (!OldData_On()) return false;
    for (int g = 0; id != -1 && g < 8; g++, id = NX_ResId(id))
        if (findPreview1(id, out, n)) return true;
    return false;
}

bool OldData_FindFile(int id, const char* rel, char* out, size_t n)
{
    if (!OldData_On()) return false;
    for (int g = 0; id != -1 && g < 8; g++, id = NX_ResId(id))
        if (findFile1(id, rel, out, n)) return true;
    return false;
}

/* Discos da seleção: preenche tex[i] das músicas com want[i] != 0 e tex[i] < 0.
 * Abre cada pacote uma vez só. Fecha o pacote aberto no momento (RES_Open é único).
 * Depois das versões anteriores tenta o BGA/90.DAT do próprio NX, que ainda traz os
 * discos pequenos (128x128) de várias músicas que saíram do TEST.DAT. */
void OldData_LoadDiscs(int* tex, const uint8_t* want, int count)
{
    if (!OldData_On()) return;
    static const char* const k_pack[3] = { "90.DAT", "90E.DAT", "90H.DAT" };
    for (int r = 0; r <= s_count; r++) {   /* r == s_count: a própria pasta do jogo */
        const char* root = r < s_count ? s_root[r] : g_game.currentDirectory;
        for (int k = 0; k < 3; k++) {
            int missing = 0;
            for (int i = 0; i < count; i++) if (want[i] && tex[i] < 0) missing++;
            if (!missing) return;
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s/BGA/%s", root, k_pack[k]);
            if (!fileExists(path) || !RES_Open(path)) continue;
            int got = 0;
            for (int i = 0; i < count; i++) {
                if (!want[i] || tex[i] >= 0) continue;
                char name[16];
                /* era: só o id da própria música */
                for (int id = (int)g_songTable[i].id, g = 0; id != -1 && g < 8 && tex[i] < 0; g++, id = NX_ResId(id)) {
                    snprintf(name, sizeof(name), "%X.TGA", (unsigned)id);
                    tex[i] = loadTextureFromRES(name);
                }
                if (tex[i] >= 0) got++;
            }
            RES_Close();
            Log_Print("OLDDATA: %d disco(s) de '%s'\n", got, path);
        }
    }
}

/* Resumo para o GAME OPTION: quantas versões anteriores foram achadas e quantas das
 * músicas do USE OLD DATA SONGS têm o indispensável (capa, disco e áudio), contando o
 * que o próprio NX já traz. Prévia e BGA ficam de fora da conta: têm reserva (001.MOV)
 * e algumas músicas não os têm em versão nenhuma. Não depende de a opção estar ligada.
 * Calculado uma vez por execução (abre os pacotes de disco). */
void OldData_Status(int* roots, int* ok, int* total)
{
    static int s_ok = -1, s_total = 0;
    if (s_count < 0) scan();
    if (s_ok < 0) {
        static uint8_t title[SONG_TABLE_COUNT], audio[SONG_TABLE_COUNT], disc[SONG_TABLE_COUNT];
        char path[MAX_PATH];
        memset(title, 0, sizeof(title)); memset(audio, 0, sizeof(audio)); memset(disc, 0, sizeof(disc));
        for (int i = NX_SONG_OLD_BASE; i < SONG_TABLE_COUNT; i++) {
            for (int id = (int)g_songTable[i].id, g = 0; id != -1 && g < 8; g++, id = NX_ResId(id)) {
                if (!title[i]) {
                    snprintf(path, sizeof(path), "%s/TITLE/%03X.PNZ", g_game.currentDirectory, (unsigned)id);
                    title[i] = fileExists(path) || findTitle1(id, path, sizeof(path));
                }
                if (!audio[i]) {
                    snprintf(path, sizeof(path), "%s/AUDIO/%03X.AUD", g_game.currentDirectory, (unsigned)id);
                    audio[i] = fileExists(path) || findFile1(id, "AUDIO/%X.AUD", path, sizeof(path));
                }
            }
        }
        static const char* const k_pack[4] = { "TEST.DAT", "90.DAT", "90E.DAT", "90H.DAT" };
        static const char* const k_ext[6] = { "TGA", "png", "tga", "jpg", "bmp", "dds" };   /* como loadTextureFromRES */
        for (int r = 0; r <= s_count; r++) {
            const char* root = r < s_count ? s_root[r] : g_game.currentDirectory;
            for (int k = 0; k < 4; k++) {
                snprintf(path, sizeof(path), "%s/BGA/%s", root, k_pack[k]);
                if (!fileExists(path) || !RES_Open(path)) continue;
                for (int i = NX_SONG_OLD_BASE; i < SONG_TABLE_COUNT; i++)
                    for (int id = (int)g_songTable[i].id, g = 0; id != -1 && g < 8 && !disc[i]; g++, id = NX_ResId(id))
                        for (int e = 0; e < 6 && !disc[i]; e++) {
                            char name[24];
                            snprintf(name, sizeof(name), "%X.%s", (unsigned)id, k_ext[e]);
                            if (RES_Find(name) >= 0) disc[i] = 1;
                        }
                RES_Close();
            }
        }
        s_ok = 0; s_total = 0;
        for (int i = NX_SONG_OLD_BASE; i < SONG_TABLE_COUNT; i++) {
            s_total++;
            if (title[i] && audio[i] && disc[i]) s_ok++;
            else Log_Print("OLDDATA: %X incompleta (capa %d, disco %d, audio %d)\n",
                           (unsigned)g_songTable[i].id, title[i], disc[i], audio[i]);
        }
        Log_Print("OLDDATA: %d de %d músicas com capa, disco e áudio (%d versão(ões) anterior(es))\n",
                  s_ok, s_total, s_count);
    }
    *roots = s_count; *ok = s_ok; *total = s_total;
}

/* Modos que saíram do .SEE do NX e ainda existem no .STX de uma versão anterior
 * (conferido seção por seção: tools/import_old_data.py não cobre isto; ver docs/COMMANDS.md).
 * mode: 0 NORMAL, 1 HARD, 2 CRAZY, 3 FREESTYLE, 4 NIGHTMARE. Nível: tabela da versão em que
 * o modo existe (exceed2_songs.c para B05 e B12, exceed_songs.c para a 911). */
typedef struct { uint32_t id; int mode; int level; } OldMode;
static const OldMode k_oldModes[] = {
    { 0xB05, 2, 10 }, { 0xB05, 4, 11 },   /* Drunken in melody: CRAZY e NIGHTMARE do Exceed 2 */
    { 0xB12, 2, 11 }, { 0xB12, 4, 12 },   /* Too Late: CRAZY e NIGHTMARE do Exceed 2 */
    { 0x911, 3, 6 },                      /* Chicken Wing: FREESTYLE do Exceed */
};
#define OLD_MODE_COUNT ((int)(sizeof(k_oldModes) / sizeof(k_oldModes[0])))

/* Modos que têm notas no .SEE do próprio NX e nível -1 na tabela do piu, e que não são
 * cópia de outro chart (comparação das seções de todos os .SEE). Não dependem de versão
 * anterior: valem com GAME STYLE = MODDED. Nenhuma tabela traz o nível deles -> 99 ("??"),
 * pedido do usuário. */
static const OldMode k_hiddenModes[] = {
    { 0xD40, 2, 99 }, { 0xD40, 4, 99 },                       /* LOVELY full: CRAZY, NIGHTMARE */
    { 0xD41, 1, 99 },                                         /* Love is a Danger Zone 2: HARD */
    { 0xD42, 1, 99 },                                         /* Beat of the War 2: HARD */
    { 0xDB57, 4, 99 },                                        /* Canon D full: NIGHTMARE */
    { 0xD39, 3, 99 },                                         /* FIRE full: FREESTYLE */
    { 0xD36, 0, 99 },                                         /* BanYa-P Classic Mix: NORMAL */
    { 0xD735, 2, 99 },                                        /* Vook Another: CRAZY */
    { 0xD802, 0, 99 }, { 0xD802, 1, 99 },                     /* Bee Another: NORMAL, HARD */
    { 0xDD30, 0, 99 },                                        /* Final Audition EP.2-2 Another: NORMAL */
    { 0xDA14, 0, 99 }, { 0xDA14, 1, 99 }, { 0xDA14, 2, 99 },  /* Kiss me Another: N, H, C */
    { 0xD701, 0, 99 }, { 0xD701, 1, 99 },                     /* Dr. M Another: NORMAL, HARD */
    { 0xDD11, 2, 99 },                                        /* For You Another: CRAZY */
    { 0xDA11, 0, 99 }, { 0xDA11, 1, 99 },                     /* What do you really want? Another: N, H */
    { 0xDB06, 0, 99 }, { 0xDB06, 1, 99 }, { 0xDB06, 2, 99 },  /* Deja Vu Another: N, H, C */
    /* Iguais a um chart que não é música da lista (por isso tinham ficado de fora na 1ª
     * passada): C32 é um .SEE sem dono, AA601 e AA603 são missões do WORLD TOUR. */
    { 0xD41, 3, 99 },                                         /* Love is a Danger Zone 2 full: FREESTYLE (= C32) */
    { 0xD205, 1, 99 }, { 0xD205, 3, 99 },                     /* Extravaganza Another: HARD, FREESTYLE (= AA601) */
    { 0xD922, 0, 99 }, { 0xD922, 2, 99 }, { 0xD922, 3, 99 },  /* Final Audition Ep.1 Another: N, C, F (= AA603) */
    /* Chart guardado em outro arquivo (k_stepAlias) */
    { 0x919, 4, 99 },                                         /* Lazenca Save Us: NIGHTMARE (D901.SEE) */
};

/* Modo cujo chart está em outro .SEE do NX. D901.SEE só tem a seção NIGHTMARE, 705 notas,
 * em 85.1 BPM — o BPM da Lazenca Save Us (919), a única música da tabela nesse BPM — e
 * 1236 linhas contra as 614 da 919 (o dobro da divisão). Altamente provável que seja o
 * NIGHTMARE não lançado dela; nenhuma tabela liga os dois ids. */
typedef struct { uint32_t id; int mode; uint32_t fileId; } StepAlias;
static const StepAlias k_stepAlias[] = { { 0x919, 4, 0xD901 } };
#define HIDDEN_MODE_COUNT ((int)(sizeof(k_hiddenModes) / sizeof(k_hiddenModes[0])))

/* O .STX tem notas no modo pedido? (o do Zero ainda traz a B05 e a B12, mas já sem o
 * CRAZY e o NIGHTMARE: a seção existe com 16 linhas vazias) */
static bool stxHasMode(const char* path, int mode)
{
    static const int k_sec[5] = { 1, 2, 4, 5, 3 };   /* N H C F NM -> seção (Step_SelectChart) */
    static StepSong song;
    int notes = 0;
    if (!Step_LoadSong(path, &song)) return false;
    int ci = k_sec[mode];
    if (ci < song.chartCount) {
        const StepChart* c = &song.charts[ci];
        for (uint32_t r = 0; r < c->rowCount && notes <= 20; r++) {
            const StepRow* w = &c->rows[r];
            if (w->half1.dl || w->half1.ul || w->half1.cn || w->half1.ur || w->half1.dr ||
                w->half2.dl || w->half2.ul || w->half2.cn || w->half2.ur || w->half2.dr) notes++;
        }
    }
    Step_FreeSong(&song);
    return notes > 20;
}

/* .STX do id com o modo pedido, na versão anterior mais próxima que o tiver: arquivo solto
 * em STEP/ ou dentro do STEP.DAT (step.c abre os dois a partir do mesmo caminho
 * "<raiz>/STEP/<ID>.STX"). */
static bool findStx(int id, int mode, char* out, size_t n)
{
    for (int r = 0; r < s_count; r++) {
        snprintf(out, n, "%s/STEP/%X.STX", s_root[r], (unsigned)id);
        if (stxHasMode(out, mode)) return true;
    }
    return false;
}

/* Caminho do .STX para (música, modo) quando o modo é um dos recuperados e a opção
 * está ligada. */
bool OldData_FindStep(int id, int mode, char* out, size_t n)
{
    if (g_game.gameStyleModded)
        for (int k = 0; k < (int)(sizeof(k_stepAlias) / sizeof(k_stepAlias[0])); k++)
            if ((int)k_stepAlias[k].id == id && k_stepAlias[k].mode == mode) {
                snprintf(out, n, "%s/STEP/%03X.SEE", g_game.currentDirectory, (unsigned)k_stepAlias[k].fileId);
                return true;
            }
    if (!OldData_On()) return false;
    for (int k = 0; k < OLD_MODE_COUNT; k++)
        if ((int)k_oldModes[k].id == id && k_oldModes[k].mode == mode) return findStx(id, mode, out, n);
    return false;
}

/* Põe o nível dos modos recuperados na tabela (opção ligada e .STX achado) ou volta
 * para -1. Devolve true se algum nível mudou (o songDB precisa ser refeito). */
bool OldData_ApplyModes(void)
{
    static int8_t s_have[OLD_MODE_COUNT];   /* 0 = não conferido, 1 = tem .STX, -1 = não tem */
    bool on = OldData_On(), changed = false;
    for (int k = 0; k < OLD_MODE_COUNT; k++) {
        int want = -1;
        if (on) {
            if (!s_have[k]) {
                char p[MAX_PATH];
                s_have[k] = findStx((int)k_oldModes[k].id, k_oldModes[k].mode, p, sizeof(p)) ? 1 : -1;
                Log_Print("OLDDATA: modo %d da %X: .STX %s\n", k_oldModes[k].mode, (unsigned)k_oldModes[k].id,
                          s_have[k] > 0 ? "achado" : "não achado");
            }
            if (s_have[k] > 0) want = k_oldModes[k].level;
        }
        for (int i = 0; i < SONG_TABLE_COUNT; i++) {
            if (g_songTable[i].id != k_oldModes[k].id) continue;
            if (g_songTable[i].level[k_oldModes[k].mode] != want) {
                g_songTable[i].level[k_oldModes[k].mode] = want;
                changed = true;
            }
        }
    }
    /* modos escondidos no .SEE do NX: só dependem do GAME STYLE */
    for (int k = 0; k < HIDDEN_MODE_COUNT; k++) {
        int want = g_game.gameStyleModded ? k_hiddenModes[k].level : -1;
        for (int i = 0; i < SONG_TABLE_COUNT; i++) {
            if (g_songTable[i].id != k_hiddenModes[k].id) continue;
            if (g_songTable[i].level[k_hiddenModes[k].mode] != want) {
                g_songTable[i].level[k_hiddenModes[k].mode] = want;
                changed = true;
            }
        }
    }
    return changed;
}