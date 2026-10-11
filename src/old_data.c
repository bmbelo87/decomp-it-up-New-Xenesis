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