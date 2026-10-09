/* service_menu.c — Menu de serviço (SETUP MENU)
 *
 * Reconstrução das funções ServiceMenu_* do PUMPY.EXE.
 *
 * Mapa de origem (endereços do original):
 *   ServiceMenu_Enter                  0x00404ee0
 *   ServiceMenu_Exit                   0x004066d0
 *   ServiceMenu_UpdateRender           0x004066e0
 *   ServiceMenu_RenderMain             0x004052e0
 *   ServiceMenu_RenderFooter           0x004051e0
 *   ServiceMenu_RenderGradientBar      0x00404d40
 *   ServiceMenu_RenderEEPROMOverlay    0x00404e00
 *   ServiceMenu_RenderIOTest           0x004054c0
 *   ServiceMenu_RenderEEPROMTest       0x004056d0
 *   ServiceMenu_RenderScreenTest       0x00405840
 *   ServiceMenu_RenderGameOption       0x00405910
 *   ServiceMenu_RenderCoinOption       0x00405d80
 *   ServiceMenu_RenderSoundTest        0x00406030
 *   ServiceMenu_RenderClearBookkeeping 0x00406210
 *   ServiceMenu_RenderBookkeeping      0x00406360
 *   ServiceMenu_RenderStatistics       0x004064e0
 *   Render_DrawGrid                    0x00403950
 *
 * Convenção de coordenadas: o original projeta em Y-UP (glOrtho 0..480 de baixo
 * pra cima), igual ao Render_SetOrtho daqui. Portanto todas as coordenadas
 * abaixo são as do binário, sem conversão.
 *
 * Botões: o original lê dois bits de input —
 *   0x10000 = TEST BUTTON    (MOVE   / percorre)
 *   0x20000 = SERVICE BUTTON (SELECT / confirma)
 *   0x40000 = CLEAR BUTTON, 0x100000 = COIN1, 0x200000 = COIN2 (só no I/O TEST)
 * No PC seguimos a mesma ordem da botoeira listada pelo I/O TEST:
 *   F1 = TEST | F2 = SERVICE | F3 = CLEAR | F4 = COIN1 | F5 = COIN2
 * O F1 abre o menu de qualquer tela e, já dentro dele, percorre a lista (MOVE);
 * o F2 confirma a opção selecionada (SELECT). Como o mesmo F1 que abre também
 * seria lido como MOVE no primeiro frame, g_svcSkipInput engole esse press.
 *
 * Desvios deliberados em relação ao original (documentados):
 *   1. O original desenha literalmente as format strings ("%s", "%d STAGE",
 *      "1 CREDITS / %d COIN", "SERVICE : %d", "%02d. %-24s [%04d]") sem passar
 *      argumento nenhum — código inacabado no binário. Aqui elas são formatadas
 *      com o valor real, que é a intenção evidente.
 *   2. I/O TEST e EEPROM TEST são adaptados ao PC: o I/O TEST reflete o estado
 *      real do teclado e o EEPROM TEST grava/lê o arquivo de configuração local,
 *      que faz o papel da EEPROM do gabinete.
 *   3. Textos coreanos foram omitidos. ServiceMenu_Enter força g_nLanguage = 1
 *      (inglês) no original, então os ramos coreanos são inalcançáveis na prática.
 */

#include "pumpy.h"
#include "movie.h"
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- cores ---
 * Extraídas de PUMPY.EXE. A paleta de 0x442270 a 0x4422b8 tem 7 entradas e é
 * usada tanto pelas barras do SCREEN TEST quanto como cor de destaque.
 */
static const float SVC_HIGHLIGHT[3]  = { 1.0f, 0.0f, 0.0f }; /* 0x442270 */
static const float SVC_EEPROMDATA[3] = { 1.0f, 1.0f, 0.0f }; /* 0x44227c */
static const float SVC_SUCCESS[3]    = { 0.0f, 1.0f, 0.0f }; /* 0x442288 */
static const float SVC_NORMAL[3]     = { 1.0f, 1.0f, 1.0f }; /* 0x4422b8 */
static const float SVC_SCREENTEST[3] = { 0.0f, 0.0f, 0.0f }; /* 0x4422c4 */

static const float SVC_PALETTE[7][3] = {
    { 1.0f, 0.0f, 0.0f },  /* 0x442270 vermelho  */
    { 1.0f, 1.0f, 0.0f },  /* 0x44227c amarelo   */
    { 0.0f, 1.0f, 0.0f },  /* 0x442288 verde     */
    { 0.0f, 1.0f, 1.0f },  /* 0x442294 ciano     */
    { 0.0f, 0.0f, 1.0f },  /* 0x4422a0 azul      */
    { 1.0f, 0.0f, 1.0f },  /* 0x4422ac magenta   */
    { 1.0f, 1.0f, 1.0f },  /* 0x4422b8 branco    */
};

/* ------------------------------------------------------------- input bits -*/
#define SVC_BIT_TEST     0x10000   /* MOVE   */
#define SVC_BIT_SERVICE  0x20000   /* SELECT */
#define SVC_BIT_CLEAR    0x40000
#define SVC_BIT_COIN1    0x100000
#define SVC_BIT_COIN2    0x200000

/* ---------------------------------------------------------------- estado --
 * Equivalentes dos globais do original:
 *   g_svcPage      <- g_nServiceMenuPage   @0x00d387f8
 *   g_svcOption    <- g_nServiceMenuOption
 *   g_svcCursor    <- g_nServiceMenuCursor
 *   g_svcSubCursor <- g_nSubMenuCursor
 *   g_svcAudioIdx  <- DAT_004422d4 (init -1)
 *   g_svcSeIdx     <- DAT_004422d0 (init -1)
 */
/* GRAPHICS SETTINGS / BUTTON CONFIG: 11 itens no SETUP MENU; páginas 11 e 12. */
#define SVC_MAIN_COUNT 11
#define SVC_PAGE_GRAPHICS 11
#define SVC_PAGE_BUTTON_CONFIG 12
static int  g_svcPage;
static int  g_svcOption;
static int  g_svcCursor;
static int  g_svcSubCursor;
static int  g_svcEepromDone;
static int  g_svcEepromResult;
static int  g_svcAudioIdx  = -1;
static int  g_svcSeIdx     = -1;
static int  g_svcSkipInput;          /* engole o F2 que abriu o menu */
static char g_svcEepromBuf[64];

/* Padrão gravado/lido pelo EEPROM TEST — PUMPY.EXE 0x4422e4 */
static const char SVC_EEPROM_PATTERN[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123";

/* --------------------------------------------------------------- helpers -*/

/* Snapshot do input do frame, capturado em ServiceMenu_Update (fase de update)
 * e consumido pelas páginas durante o render. Ver o comentário em
 * ServiceMenu_Update para o motivo. */
static uint32_t g_svcHitBits;
static uint32_t g_svcHeldBits;

static uint32_t svcBitsHit(void)  { return g_svcHitBits;  }
static uint32_t svcBitsHeld(void) { return g_svcHeldBits; }

/* NX: o texto do setup usa a grade ASCII do SCOREFONT.TGA (BGA/SCOREFONT.DAT,
 * carregado em 0x8060c20). Grade medida na imagem: 0x20..0x7F em 3 linhas de
 * 32 colunas, célula 8x15 px a partir do topo (V=0 = topo, sem inversão).
 * (x,y) = canto de baixo à esquerda, Y para cima, avanço 8 px — mesma convenção
 * do Font_DrawText que era usado. A célula 8x15 é medida, não confirmada no
 * assembly da rotina de texto do setup. */
static int g_svcFontTex = -2;   /* -2 = ainda não tentou */

static void svcText(float x, float y, const char* s)
{
    /* era: Font_DrawText(x, y, s); */
    if (g_svcFontTex == -2) {
        char path[MAX_PATH];
        g_svcFontTex = -1;
        snprintf(path, sizeof(path), "%s/BGA/SCOREFONT.DAT", g_game.currentDirectory);
        if (RES_Open(path)) {
            g_svcFontTex = loadTextureFromRES("SCOREFONT.TGA");
            RES_Close();
        }
        if (g_svcFontTex < 0) Log_Print("SVC: SCOREFONT.TGA não carregou, usando font.tga\n");
    }
    if (g_svcFontTex < 0 || !s) { Font_DrawText(x, y, s); return; }

    Texture_Bind(g_svcFontTex);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    for (; *s; s++, x += 8.0f) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 0x20 || ch >= 0x80) continue;
        int idx = ch - 0x20;
        float u0 = (float)((idx % 32) * 8) / 256.0f, u1 = u0 + 8.0f / 256.0f;
        float vt = (float)((idx / 32) * 15) / 256.0f, vb = vt + 15.0f / 256.0f;
        glTexCoord2f(u0, vb); glVertex2f(x, y);
        glTexCoord2f(u1, vb); glVertex2f(x + 8.0f, y);
        glTexCoord2f(u1, vt); glVertex2f(x + 8.0f, y + 15.0f);
        glTexCoord2f(u0, vt); glVertex2f(x, y + 15.0f);
    }
    glEnd();
}

static void svcColor(const float* c)
{
    glColor3fv(c);
}

/* Aplica highlight quando o índice bate com o cursor (padrão do original) */
static void svcColorFor(int idx, int cursor)
{
    glColor3fv(idx == cursor ? SVC_HIGHLIGHT : SVC_NORMAL);
}

/* ------------------------------------------------- Render_DrawGrid 0x403950
 * Malha de 16px + moldura de 2px. Usado pelo SCREEN TEST para conferir
 * alinhamento e overscan do monitor.
 */
static void svcDrawGrid(void)
{
    int i;
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_LINES);
    for (i = 0; i < 640; i += 16) {          /* verticais   */
        glVertex2f((float)i, 0.0f);
        glVertex2f((float)i, 480.0f);
    }
    for (i = 0; i < 480; i += 16) {          /* horizontais */
        glVertex2f(0.0f,   (float)i);
        glVertex2f(640.0f, (float)i);
    }
    /* moldura — linhas duplas nas quatro bordas */
    glVertex2f(0.0f, 0.0f);     glVertex2f(639.0f, 0.0f);
    glVertex2f(0.0f, 1.0f);     glVertex2f(639.0f, 1.0f);
    glVertex2f(0.0f, 478.0f);   glVertex2f(640.0f, 478.0f);
    glVertex2f(0.0f, 479.0f);   glVertex2f(640.0f, 479.0f);
    glVertex2f(0.0f, 0.0f);     glVertex2f(0.0f,   479.0f);
    glVertex2f(1.0f, 0.0f);     glVertex2f(1.0f,   479.0f);
    glVertex2f(638.0f, 0.0f);   glVertex2f(638.0f, 479.0f);
    glVertex2f(639.0f, 0.0f);   glVertex2f(639.0f, 479.0f);
    glEnd();

    glEnable(GL_TEXTURE_2D);
}

/* ------------------------------------- ServiceMenu_RenderGradientBar 0x404d40
 * 7 barras de 256x32 a partir de (64, 384), descendo 32 a cada uma. Cada barra
 * é um degradê do preto (esquerda) até a cor da paleta (direita).
 */
static void svcRenderGradientBar(void)
{
    int i;
    glShadeModel(GL_SMOOTH);
    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(64.0f, 384.0f, 0.0f);
    for (i = 0; i < 7; i++) {
        glBegin(GL_QUADS);
        glColor3fv(SVC_SCREENTEST);
        glVertex2i(0, 0);
        glVertex2i(0, 32);
        glColor3fv(SVC_PALETTE[i]);
        glVertex2i(256, 32);
        glVertex2i(256, 0);
        glEnd();
        glTranslatef(0.0f, -32.0f, 0.0f);
    }
    glPopMatrix();
    glEnable(GL_TEXTURE_2D);
    glShadeModel(GL_FLAT);
}

/* ---------------------------------- ServiceMenu_RenderEEPROMOverlay 0x404e00
 * Hexágono de cores (TRIANGLE_FAN) centrado em (480, 304), raio ~112.
 * Apesar do nome no original, é parte do teste visual de cor.
 */
static void svcRenderEEPROMOverlay(void)
{
    glShadeModel(GL_SMOOTH);
    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(480.0f, 304.0f, 0.0f);
    glBegin(GL_TRIANGLE_FAN);
    glColor3fv(SVC_NORMAL);       glVertex2i(  0,    0);
    glColor3fv(SVC_PALETTE[0]);   glVertex2i(  0,  112);
    glColor3fv(SVC_PALETTE[1]);   glVertex2i( 96,   48);
    glColor3fv(SVC_PALETTE[2]);   glVertex2i( 96,  -48);
    glColor3fv(SVC_PALETTE[3]);   glVertex2i(  0, -112);
    glColor3fv(SVC_PALETTE[4]);   glVertex2i(-96,  -48);
    glColor3fv(SVC_PALETTE[5]);   glVertex2i(-96,   48);
    glColor3fv(SVC_PALETTE[0]);   glVertex2i(  0,  112);
    glEnd();
    glPopMatrix();
    glEnable(GL_TEXTURE_2D);
    glShadeModel(GL_FLAT);
}

/* -------------------------------------- ServiceMenu_RenderFooter 0x004051e0 */
static void svcRenderFooter(void)
{
    char buf[64];
    svcColor(SVC_NORMAL);

    (void)buf;
    /* era (Prex3): "PUMP IT UP (PREX 3 / 3)" em (0,16) e "1999-2003 ANDAMIRO CO., LTD." em (0,0) */
    /* NX 0x80864e0: três linhas em x = 48 (Y para cima) */
    svcText(48.0f, 64.0f, "PUMP IT UP: NX");
    svcText(48.0f, 48.0f, "(C) 1999-2006 ANDAMIRO CO., LTD.");
    svcText(48.0f, 32.0f, "(BUILD:1.08)");

    /* NX 0x808656f (tabela 0x81144f0): topo, SOUND, BOOKKEEPING e confirmações =
     * MOVE/SELECT; I/O e SCREEN = EXIT; STATISTICS = MOVE PAGE/EXIT; as páginas de
     * lista do Lua (GAME/COIN SETTING) não desenham botões */
    switch (g_svcPage) {
    case 4: case 5:
        break;
    case 0: case 6: case 7: case 10: case 13: case SVC_PAGE_GRAPHICS: case SVC_PAGE_BUTTON_CONFIG:
        svcText(236.0f, 36.0f, "MOVE   - TEST    BUTTON");
        svcText(236.0f, 16.0f, "SELECT - SERVICE BUTTON");
        break;
    case 1: case 3:
        svcText(236.0f, 16.0f, "EXIT   - SERVICE BUTTON");
        break;
    case 8:
        svcText(236.0f, 36.0f, "MOVE PAGE - TEST    BUTTON");
        svcText(236.0f, 16.0f, "EXIT      - SERVICE BUTTON");
        break;
    default:
        break;   /* página 2 (EEPROM TEST) não desenha rodapé de botões */
    }
}

/* ---------------------------------------- ServiceMenu_RenderMain 0x004052e0 */
/* static const char* SVC_MAIN_ITEMS[9] = {
    "I/O TEST", "EEPROM TEST", "SCREEN TEST", "GAME OPTION", "COIN OPTION",
    "SOUND TEST", "BOOKEEPING", "STATISTICS", "EXIT"
}; */
/* "BOOKEEPING" com um K só — typo presente no binário original, preservado.
 * "GRAPHICS SETTINGS" (página 11) e "BUTTON CONFIG" (página 12) são extras deste port. */
static const char* SVC_MAIN_ITEMS[SVC_MAIN_COUNT] = {
    "I/O TEST", "EEPROM TEST", "SCREEN TEST", "GAME OPTION", "COIN OPTION",
    "SOUND TEST", "BOOKEEPING", "STATISTICS", "GRAPHICS SETTINGS", "BUTTON CONFIG", "EXIT"
};

/* NX: SETUP_MENU_TOP de /SCRIPT/SETUP_COMMON.LUA + nomes de SETUP_EN.LUA.
 * EEPROM TEST não existe no NX; LANGUAGE troca o valor no próprio topo
 * (KOREAN / ENGLISH / SPANISH / CHINESE(T), padrão ENGLISH, EEPROM +0xBA9).
 * GRAPHICS SETTINGS e BUTTON CONFIG continuam como extras do port. */
#define SVC_TOP_BLANK  (-1)
#define SVC_TOP_LANG   (-2)
static const struct { const char* name; int page; } SVC_TOP[] = {
    { "I/O TEST",          1 },
    { "SCREEN TEST",       3 },
    { "GAME SETTING",      4 },
    { "COIN SETTING",      5 },
    { "SOUND TEST",        6 },
    { "BOOKKEEPING",       7 },
    { "STATISTICS",        8 },
    /* { "RESTRICTION", ? },  comentado no SETUP_COMMON.LUA */
    { "LANGUAGE",          SVC_TOP_LANG },
    { "GRAPHICS SETTINGS", SVC_PAGE_GRAPHICS },       /* extra do port */
    { "BUTTON CONFIG",     SVC_PAGE_BUTTON_CONFIG },  /* extra do port */
    { "",                  SVC_TOP_BLANK },
    { "EXIT",              9 },
};
#define SVC_TOP_COUNT ((int)(sizeof(SVC_TOP) / sizeof(SVC_TOP[0])))
static const char* SVC_LANG_NX[4] = { "KOREAN", "ENGLISH", "SPANISH", "CHINESE(T)" };

static void svcRenderMain(void)
{
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "SETUP MENU");

    /* itens a partir de Y=352 descendo 20 */
    for (i = 0; i < SVC_TOP_COUNT; i++) {
        if (SVC_TOP[i].page == SVC_TOP_BLANK) continue;
        svcColorFor(i, g_svcOption);
        svcText(276.0f, (float)(352 - i * 20), SVC_TOP[i].name);
        if (SVC_TOP[i].page == SVC_TOP_LANG) {
            if (g_game.svcLangOption == 1) svcColor(SVC_SUCCESS);
            svcText(404.0f, (float)(352 - i * 20), SVC_LANG_NX[g_game.svcLangOption & 3]);
        }
    }

    /* NX 0x8086290: teste da trava a cada 60 quadros -> OK/Err, em (276,132);
     * sem dispensador de tickets ([0x9e40060] == 0), o aviso em (196,112) e
     * "NOT CONNECTED" em vermelho em (356,112). No PC não há trava nem dispensador. */
    {
        static int lockFrame, lockOk;
        char lb[48];
        if (lockFrame++ % 60 == 0) lockOk++;
        snprintf(lb, sizeof(lb), "Lock OK = %d Err = %d", lockOk, 0);
        svcColor(SVC_NORMAL);
        /* era: svcText(276.0f, 132.0f, lb); + aviso do dispensador em (196,112)/(356,112)
         * Pedido do usuário: sem o aviso do Ticket Dispenser; o Lock OK vai para a posição dele */
        svcText(196.0f, 112.0f, lb);
        /* svcText(196.0f, 112.0f, "Ticket Dispenser is NOT CONNECTED.");
        svcColor(SVC_HIGHLIGHT);
        svcText(356.0f, 112.0f, "NOT CONNECTED"); */
    }

    if (hit & SVC_BIT_TEST) {
        do {
            g_svcOption++;
            if (g_svcOption > SVC_TOP_COUNT - 1) g_svcOption = 0;
        } while (SVC_TOP[g_svcOption].page == SVC_TOP_BLANK);
    }
    if (hit & SVC_BIT_SERVICE) {
        int pg = SVC_TOP[g_svcOption].page;
        if (pg == SVC_TOP_LANG) {
            g_game.svcLangOption = (g_game.svcLangOption + 1) & 3;
            GameOption_Save();
        } else if (pg != SVC_TOP_BLANK) {
            g_svcPage       = pg;
            g_svcCursor     = 0;
            g_svcEepromDone = 0;
        }
    }
}
/* era (Prex3): itens SVC_MAIN_ITEMS com EEPROM TEST e "Lock OK = 0 Err = 0" */

/* -------------------------------------- ServiceMenu_RenderIOTest 0x004054c0 */
static void svcRenderIOTest(void)
{
    static const struct { uint32_t bit; float y; const char* label; } lines[5] = {
        /* NX 0x80870e0: rótulos com ":" no texto e o valor em " %s" */
        { SVC_BIT_TEST,    400.0f, "1. TEST BUTTON    :" },
        { SVC_BIT_SERVICE, 384.0f, "2. SERVICE BUTTON :" },
        { SVC_BIT_CLEAR,   368.0f, "3. CLEAR BUTTON   :" },
        { SVC_BIT_COIN1,   352.0f, "4. COIN 1         :" },
        { SVC_BIT_COIN2,   336.0f, "5. COIN 2         :" },
    };
    char buf[64];
    int i;
    uint32_t held = svcBitsHeld();
    uint32_t hit  = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "I/O TEST");

    for (i = 0; i < 5; i++) {
        int on = ((held | hit) & lines[i].bit) != 0;
        svcColor(on ? SVC_HIGHLIGHT : SVC_NORMAL);
        sprintf(buf, "%s %s", lines[i].label, on ? "ON" : "OFF");
        svcText(260.0f, lines[i].y, buf);
    }

    if (hit & SVC_BIT_SERVICE) g_svcPage = 0;
}

/* ---------------------------------- ServiceMenu_RenderEEPROMTest 0x004056d0
 * Adaptação: a EEPROM do gabinete é substituída pelo arquivo de configuração
 * local. Grava o padrão, lê de volta e compara.
 */
static void svcRenderEEPROMTest(void)
{
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "EEPROM TEST");

    if (!g_svcEepromDone) {
        FILE* f = fopen("eeprom.dat", "wb");
        memset(g_svcEepromBuf, 0, sizeof(g_svcEepromBuf));
        if (f) {
            fwrite(SVC_EEPROM_PATTERN, 1, strlen(SVC_EEPROM_PATTERN), f);
            fclose(f);
            f = fopen("eeprom.dat", "rb");
            if (f) {
                size_t n = fread(g_svcEepromBuf, 1, sizeof(g_svcEepromBuf) - 1, f);
                g_svcEepromBuf[n] = '\0';
                fclose(f);
            }
        }
        g_svcEepromResult = (strcmp(g_svcEepromBuf, SVC_EEPROM_PATTERN) == 0);
        g_svcEepromDone   = 1;
        Log_Print("ServiceMenu: EEPROM test result=%d\n", g_svcEepromResult);
    }

    svcText(120.0f, 340.0f, "WRITE  ...");
    svcText(120.0f, 300.0f, "READ   ...");
    svcText(120.0f, 260.0f, "RESULT ...");

    svcColor(SVC_EEPROMDATA);
    svcText(220.0f, 340.0f, SVC_EEPROM_PATTERN);
    svcText(220.0f, 300.0f, g_svcEepromBuf);

    if (g_svcEepromResult) {
        svcColor(SVC_SUCCESS);
        svcText(220.0f, 260.0f, "SUCCESS");
    } else {
        svcColor(SVC_HIGHLIGHT);
        svcText(220.0f, 260.0f, "FAIL");
    }

    if (hit & SVC_BIT_SERVICE) g_svcPage = 0;
}

/* ---------------------------------- ServiceMenu_RenderScreenTest 0x00405840 */
static void svcRenderScreenTest(void)
{
    uint32_t hit = svcBitsHit();

    svcDrawGrid();
    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "SCREEN TEST");
    svcRenderGradientBar();
    svcRenderEEPROMOverlay();

    if (hit & SVC_BIT_SERVICE) g_svcPage = 0;
}

/* ---------------------------------- ServiceMenu_RenderGameOption 0x00405910 */
#if 0
static const char* SVC_GAMEOPT_ITEMS[9] = {
    "GAME MODE", "LEVEL", "STAGE BREAK", "LANGUAGE", "DEMO SOUND",
    "SHOW HELP", "DEFAULT SETTING", "SAVE AND EXIT", "EXIT"
};
static const char* SVC_LEVEL_NAMES[3]  = { "1. EASY", "2. NORMAL", "3. HARD" };
static const char* SVC_LANG_NAMES[4]   = { "KOREAN", "ENGLISH", "PORTUGUESE", "SPANISH" };

/* Espelha os defaults de GameOption_Load (game_option.c) */
static void svcGameOptionReset(void)
{
    g_game.optionDifficulty = 1;    /* NORMAL          */
    g_game.optionToggle1    = 1;    /* STAGE BREAK on  */
    g_game.optionToggle2    = 0;    /* SHOW HELP off   */
    g_game.svcGameMode      = 0;    /* NORMAL          */
    g_game.svcDemoSound     = 0;
    g_game.svcLangOption    = 1;    /* ENGLISH         */
}

static void svcRenderGameOption(void)
{
    char buf[64];
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "GAME OPTION");

    for (i = 0; i < 9; i++) {
        float y = (float)(352 - i * 20);
        svcColorFor(i, g_svcCursor);
        svcText(196.0f, y, SVC_GAMEOPT_ITEMS[i]);

        switch (i) {
        case 0:  /* GAME MODE — verde quando NORMAL */
            if (g_game.svcGameMode == 0) {
                svcColor(SVC_SUCCESS);
                svcText(404.0f, y, "NORMAL");
            } else {
                svcText(404.0f, y, "EVENT");
            }
            break;
        case 1:  /* LEVEL — verde quando NORMAL (1) */
            if (g_game.optionDifficulty == 1) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, SVC_LEVEL_NAMES[g_game.optionDifficulty % 3]);
            break;
        case 2:  /* STAGE BREAK — 0=OFF, 1..4 = "%d STAGE"; verde quando 2
                  * Usa optionToggle1, que é o campo realmente lido pelo jogo
                  * (gameplay.c e game_option.c). Os leitores tratam como
                  * booleano, e 1..4 continuam sendo "ligado" para eles. */
            if (g_game.optionToggle1 == 2) svcColor(SVC_SUCCESS);
            if (g_game.optionToggle1 != 0) {
                sprintf(buf, "%d STAGE", g_game.optionToggle1);
                svcText(404.0f, y, buf);
            } else {
                svcText(404.0f, y, "OFF");
            }
            break;
        case 3:  /* LANGUAGE — verde quando ENGLISH (1) */
            if (g_game.svcLangOption == 1) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, SVC_LANG_NAMES[g_game.svcLangOption & 3]);
            break;
        case 4:  /* DEMO SOUND — verde quando ligado (0 no original) */
            if (g_game.svcDemoSound == 0) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, g_game.svcDemoSound == 0 ? "ON" : "OFF");
            break;
        case 5:  /* SHOW HELP — optionToggle2 é o campo lido por menu.c */
            if (g_game.optionToggle2 == 0) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, g_game.optionToggle2 ? "ON" : "OFF");
            break;
        default:
            break;
        }
    }

    if (hit & SVC_BIT_TEST) {
        g_svcCursor++;
        if (g_svcCursor > 8) g_svcCursor = 0;
    }
    if (hit & SVC_BIT_SERVICE) {
        switch (g_svcCursor) {
        case 0:
            g_game.svcGameMode = !g_game.svcGameMode;
            break;
        case 1:
            g_game.optionDifficulty++;
            if (g_game.optionDifficulty > 2) g_game.optionDifficulty = 0;
            break;
        case 2:
            g_game.optionToggle1++;
            if (g_game.optionToggle1 > 4) g_game.optionToggle1 = 0;
            break;
        case 3:
            g_game.svcLangOption++;
            if (g_game.svcLangOption >= 4) g_game.svcLangOption = 1;
            break;
        case 4:
            g_game.svcDemoSound = !g_game.svcDemoSound;
            break;
        case 5:
            g_game.optionToggle2 = !g_game.optionToggle2;
            break;
        case 6:
            svcGameOptionReset();
            break;
        case 7:
            GameOption_Save();
            /* fall-through: SAVE AND EXIT salva e cai no EXIT, como no original */
        case 8:
            g_svcPage = 0;
            break;
        default:
            break;
        }
    }

    /* O original força ENGLISH quando LANGUAGE cai em 0 (coreano) */
    if (g_game.svcLangOption == 0) g_game.svcLangOption = 1;
}

#endif /* era (Prex3): GAME OPTION com LANGUAGE, SAVE AND EXIT e STAGE BREAK "%d STAGE" */

/* NX: SETUP_GAMESETTING de /SCRIPT/SETUP_COMMON.LUA (valores e padrões).
 * EEPROM: GAME MODE +0xECE, LEVEL +0xECF, STAGE BREAK +0xED0, DEMO SOUND +0xED1,
 * SHOW HELP +0xED2, DEFAULT STATION +0xECA (u32), MERCY TICKET +0xED3,
 * SCORE PER TICKET +0xED4. "SAVE AND EXIT" está comentado no script: o EXIT grava.
 * MERCY TICKET: a lista do script usa "SETUP_VALU_1" (erro de digitação, nil), e
 * o GetMenuValuesNum para no nil -> só "OFF" existe no original. */
/* era: 11 itens; "UNLOCK SPECIAL ZONE" (índice 8) é extra do port, não existe no NX */
static const char* SVC_GAMEOPT_ITEMS[12] = {
    "GAME MODE", "LEVEL", "STAGE BREAK", "DEMO SOUND", "SHOW HELP",
    "DEFAULT STATION", "MERCY TICKET", "SCORE PER TICKET", "UNLOCK SPECIAL ZONE", "", "DEFAULT SETTING", "EXIT"
};
static const char* SVC_LEVEL_NAMES[3]  = { "1. EASY", "2. NORMAL", "3. HARD" };
static const char* SVC_STAGEBRK_NX[5]  = { "OFF", "1ST STAGE", "2ND STAGE", "3RD STAGE", "4TH STAGE" };
static const char* SVC_STATION_NX[4]   = { "TRAINING STATION", "ARCADE STATION", "WORLD TOUR", "SPECIAL ZONE" };
static const char* SVC_SCORETKT_NX[7]  = { "OFF", "100000", "200000", "300000", "400000", "500000", "1000000" };
#define SVC_MERCY_COUNT 1

/* padrões do script (3º campo, base 1) */
static void svcGameOptionReset(void)
{
    g_game.svcGameMode      = 0;    /* NORMAL     */
    g_game.optionDifficulty = 1;    /* 2. NORMAL  */
    g_game.optionToggle1    = 2;    /* 2ND STAGE  */
    g_game.svcDemoSound     = 0;    /* ON (0 = ligado no projeto) */
    g_game.optionToggle2    = 1;    /* SHOW HELP ON */
    Eeprom_Set32(0xECA, 0);         /* TRAINING STATION */
    Eeprom_Set8(0xED3, 0);          /* MERCY TICKET OFF */
    Eeprom_Set8(0xED4, 0);          /* SCORE PER TICKET OFF */
    g_game.nxUnlockSpecial  = false;  /* extra do port */
}

static void svcRenderGameOption(void)
{
    int i;
    uint32_t hit = svcBitsHit();
    uint32_t st = Eeprom_Get32(0xECA);
    int mercy = Eeprom_Get8(0xED3), stkt = Eeprom_Get8(0xED4);
    if (st > 3) st = 0;
    if (mercy >= SVC_MERCY_COUNT) mercy = 0;
    if (stkt > 6) stkt = 0;

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "GAME OPTION");

    for (i = 0; i < 12; i++) {
        float y = (float)(352 - i * 20);
        if (i == 9) continue;   /* MENU_BLANK */
        svcColorFor(i, g_svcCursor);
        svcText(196.0f, y, SVC_GAMEOPT_ITEMS[i]);

        switch (i) {   /* verde = valor padrão, como no original */
        case 0:
            if (g_game.svcGameMode == 0) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, g_game.svcGameMode ? "EVENT" : "NORMAL");
            break;
        case 1:
            if (g_game.optionDifficulty == 1) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, SVC_LEVEL_NAMES[g_game.optionDifficulty % 3]);
            break;
        case 2:
            if (g_game.optionToggle1 == 2) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, SVC_STAGEBRK_NX[g_game.optionToggle1 % 5]);
            break;
        case 3:
            if (g_game.svcDemoSound == 0) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, g_game.svcDemoSound == 0 ? "ON" : "OFF");
            break;
        case 4:
            if (g_game.optionToggle2) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, g_game.optionToggle2 ? "ON" : "OFF");
            break;
        case 5:
            if (st == 0) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, SVC_STATION_NX[st]);
            break;
        case 6:
            if (mercy == 0) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, "OFF");
            break;
        case 7:
            if (stkt == 0) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, SVC_SCORETKT_NX[stkt]);
            break;
        case 8:   /* extra do port */
            if (!g_game.nxUnlockSpecial) svcColor(SVC_SUCCESS);
            svcText(404.0f, y, g_game.nxUnlockSpecial ? "ON" : "OFF");
            break;
        default:
            break;
        }
    }

    if (hit & SVC_BIT_TEST) {
        do {
            g_svcCursor++;
            if (g_svcCursor > 11) g_svcCursor = 0;
        } while (g_svcCursor == 9);
    }
    if (hit & SVC_BIT_SERVICE) {
        switch (g_svcCursor) {
        case 0: g_game.svcGameMode = !g_game.svcGameMode; break;
        case 1: g_game.optionDifficulty = (g_game.optionDifficulty + 1) % 3; break;
        case 2: g_game.optionToggle1 = (g_game.optionToggle1 + 1) % 5; break;
        case 3: g_game.svcDemoSound = !g_game.svcDemoSound; break;
        case 4: g_game.optionToggle2 = !g_game.optionToggle2; break;
        case 5: Eeprom_Set32(0xECA, (st + 1) % 4); break;
        case 6: Eeprom_Set8(0xED3, (uint8_t)((mercy + 1) % SVC_MERCY_COUNT)); break;
        case 7: Eeprom_Set8(0xED4, (uint8_t)((stkt + 1) % 7)); break;
        case 8: g_game.nxUnlockSpecial = !g_game.nxUnlockSpecial; break;
        case 10: svcGameOptionReset(); break;
        case 11:
            GameOption_Save();
            g_svcPage = 0;
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------ GRAPHICS SETTINGS (extra do port)
 * Mesmo padrão da GAME OPTION: TEST move, SERVICE altera. As mudanças valem na
 * hora (Window_ApplyGraphics); SAVE AND EXIT grava no PUMPY.INI. */
static const char* SVC_GFX_ITEMS[8] = {
    "FULLSCREEN", "RESOLUTION", "VSYNC", "TEXTURE FILTER",
    "SHOW FPS", "ASPECT", "SAVE AND EXIT", "EXIT"
};
/* DESATIVADO (30/09/2026): item UPSCALE (XBRZ), entre ASPECT e SAVE AND EXIT.
static const char* SVC_GFX_ITEMS[9] = {
    "FULLSCREEN", "RESOLUTION", "VSYNC", "TEXTURE FILTER",
    "SHOW FPS", "ASPECT", "UPSCALE (XBRZ)", "SAVE AND EXIT", "EXIT"
};
static const char* SVC_GFX_UPS[5] = { "OFF", "OFF", "2X", "3X", "4X" };
*/
static const char* SVC_GFX_RES[5] = { "640x480", "800x600", "1024x768", "1280x960", "1600x1200" };

static void svcRenderGraphics(void)
{
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "GRAPHICS SETTINGS");

    for (i = 0; i < 8; i++) {   /* era 9 com o UPSCALE */
        float y = (float)(352 - i * 20);
        svcColorFor(i, g_svcCursor);
        svcText(196.0f, y, SVC_GFX_ITEMS[i]);
        switch (i) {
        case 0: svcText(404.0f, y, g_game.isFullscreen ? "ON" : "OFF"); break;
        case 1: svcText(404.0f, y, SVC_GFX_RES[(g_game.gfxResIdx >= 0 && g_game.gfxResIdx <= 4) ? g_game.gfxResIdx : 2]); break;
        case 2: svcText(404.0f, y, g_game.vsync ? "ON" : "OFF"); break;
        case 3: svcText(404.0f, y, g_game.gfxTexFilter ? "SHARP" : "SMOOTH"); break;
        case 4: svcText(404.0f, y, g_game.gfxShowFps ? "ON" : "OFF"); break;
        case 5: svcText(404.0f, y, g_game.gfxAspect ? "STRETCH" : "4:3"); break;
        /* case 6: svcText(404.0f, y, SVC_GFX_UPS[(g_game.gfxUpscale >= 2 && g_game.gfxUpscale <= 4) ? g_game.gfxUpscale : 0]); break; */
        default: break;
        }
    }

    if (hit & SVC_BIT_TEST) {
        g_svcCursor++;
        if (g_svcCursor > 7) g_svcCursor = 0;   /* era 8 com o UPSCALE */
    }
    if (hit & SVC_BIT_SERVICE) {
        switch (g_svcCursor) {
        case 0: g_game.isFullscreen = !g_game.isFullscreen; Window_ApplyGraphics(); break;
        case 1: g_game.gfxResIdx = (g_game.gfxResIdx + 1) % 5; Window_ApplyGraphics(); break;
        case 2: g_game.vsync = !g_game.vsync; Window_ApplyGraphics(); break;
        case 3: g_game.gfxTexFilter = !g_game.gfxTexFilter; Window_ApplyGraphics(); break;
        case 4: g_game.gfxShowFps = !g_game.gfxShowFps; break;
        case 5: g_game.gfxAspect = !g_game.gfxAspect; Window_ApplyGraphics(); break;
        /* case 6: g_game.gfxUpscale = (g_game.gfxUpscale < 2) ? 2 : (g_game.gfxUpscale >= 4 ? 0 : g_game.gfxUpscale + 1); break;
         * (UPSCALE desativado: SAVE AND EXIT/EXIT voltam a ser 6/7) */
        case 6:
            GameOption_Save();
            /* fall-through: SAVE AND EXIT salva e sai, como na GAME OPTION */
        case 7:
            g_svcPage = 0;
            break;
        default: break;
        }
    }
}

/* ------------------------------------------- BUTTON CONFIG (extra do port)
 * Configuração de botões do pad para P1 e P2 (teclado e joysticks/tapetes USB).
 * MOVE (F1 / DOWN) percorre; SELECT (F2 / ENTER) entra em modo de escuta para
 * associar nova tecla/botão ou executa a ação selecionada.
 * SAVE AND EXIT salva piukey.cfg (teclado) e PUMPY.INI (joystick).
 */
static void svcRenderButtonConfig(void)
{
    uint32_t hit = svcBitsHit();
    bool listening = Input_IsListening();
    int i;
    char buf[128];

    svcColor(SVC_NORMAL);
    svcText(260.0f, 440.0f, "BUTTON CONFIG");

    if (listening) {
        svcColor(SVC_HIGHLIGHT);
        svcText(120.0f, 415.0f, "PRESS ANY KEY OR JOYSTICK BUTTON (ESC: CANCEL)");
    } else {
        svcColor(SVC_NORMAL);
        svcText(140.0f, 415.0f, "SELECT TO BIND KEY/JOY (DOWN: MOVE, ENTER: SELECT)");
    }

    static const struct { int p; PadButton b; const char* label; } kRows[10] = {
        { 0, PAD_UL, "P1 7 (UP-LEFT)   " },
        { 0, PAD_UR, "P1 9 (UP-RIGHT)  " },
        { 0, PAD_C,  "P1 5 (CENTER)    " },
        { 0, PAD_DL, "P1 1 (DOWN-LEFT) " },
        { 0, PAD_DR, "P1 3 (DOWN-RIGHT)" },
        { 1, PAD_UL, "P2 7 (UP-LEFT)   " },
        { 1, PAD_UR, "P2 9 (UP-RIGHT)  " },
        { 1, PAD_C,  "P2 5 (CENTER)    " },
        { 1, PAD_DL, "P2 1 (DOWN-LEFT) " },
        { 1, PAD_DR, "P2 3 (DOWN-RIGHT)" },
    };

    static const char* const kActions[4] = {
        "CLEAR JOYSTICK BINDS",
        "RESTORE DEFAULTS",
        "SAVE AND EXIT",
        "EXIT"
    };

    /* Desenha as 10 linhas de botões do pad */
    for (i = 0; i < 10; i++) {
        float y = (i < 5) ? (385.0f - (float)i * 20.0f) : (375.0f - (float)i * 20.0f);
        bool isCurrent = (g_svcCursor == i);

        if (isCurrent && listening) {
            svcColor(SVC_HIGHLIGHT);
        } else {
            svcColorFor(i, g_svcCursor);
        }

        /* Nome do botão */
        svcText(120.0f, y, kRows[i].label);

        /* Tecla e Joystick */
        char keyName[32], joyName[32];
        Input_GetButtonKeyName(kRows[i].p, kRows[i].b, keyName, sizeof(keyName));
        Input_GetButtonJoyName(kRows[i].p, kRows[i].b, joyName, sizeof(joyName));

        if (isCurrent && listening) {
            svcText(310.0f, y, "[PRESS KEY / JOY]");
        } else {
            snprintf(buf, sizeof(buf), "KEY: %-10s JOY: %s", keyName, joyName);
            svcText(310.0f, y, buf);
        }
    }

    /* Desenha os 4 itens de ação */
    for (i = 0; i < 4; i++) {
        int idx = 10 + i;
        float y = 165.0f - (float)i * 20.0f;
        svcColorFor(idx, g_svcCursor);
        svcText(120.0f, y, kActions[i]);
    }

    if (!listening) {
        if (hit & SVC_BIT_TEST) {
            g_svcCursor++;
            if (g_svcCursor > 13) g_svcCursor = 0;
        }
        if (hit & SVC_BIT_SERVICE) {
            if (g_svcCursor >= 0 && g_svcCursor < 10) {
                Input_StartListen(kRows[g_svcCursor].p, kRows[g_svcCursor].b);
            } else if (g_svcCursor == 10) {
                Input_ClearJoyBindings();
            } else if (g_svcCursor == 11) {
                Input_RestoreDefaultConfig();
            } else if (g_svcCursor == 12) {
                Input_SaveKeyConfig();
                Input_SaveJoyConfig();
                g_svcPage = 0;
            } else if (g_svcCursor == 13) {
                /* Recarrega configurações originais descartando alterações não salvas */
                Input_LoadKeyConfig();
                Input_LoadJoyConfig();
                g_svcPage = 0;
            }
        }
    }
}

/* ---------------------------------- ServiceMenu_RenderCoinOption 0x00405d80 */
#if 0
static const char* SVC_COINOPT_ITEMS[5] = {
    "COIN1 SETTING", "COIN2 SETTING", "DEFAULT SETTING", "SAVE AND EXIT", "EXIT"
};

static void svcRenderCoinOption(void)
{
    char buf[64];
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "COIN OPTION");

    for (i = 0; i < 5; i++) {
        svcColorFor(i, g_svcCursor);
        svcText(196.0f, (float)(352 - i * 20), SVC_COINOPT_ITEMS[i]);
    }

    svcColorFor(0, g_svcCursor);
    if (g_game.svcCoin1 == 0) {
        svcText(404.0f, 352.0f, "FREE PLAY");
    } else {
        sprintf(buf, "1 CREDITS / %d COIN", g_game.svcCoin1);
        svcText(404.0f, 352.0f, buf);
    }

    svcColorFor(1, g_svcCursor);
    if (g_game.svcCoin2 == 0) {
        svcText(404.0f, 332.0f, "FREE PLAY");
    } else {
        sprintf(buf, "%d CREDITS / 1 COIN", g_game.svcCoin2);
        svcText(404.0f, 332.0f, buf);
    }

    if (hit & SVC_BIT_TEST) {
        g_svcCursor++;
        if (g_svcCursor > 4) g_svcCursor = 0;
    }
    if (hit & SVC_BIT_SERVICE) {
        switch (g_svcCursor) {
        case 0:
            g_game.svcCoin1++;
            if (g_game.svcCoin1 > 10) g_game.svcCoin1 = 0;
            break;
        case 1:
            g_game.svcCoin2++;
            if (g_game.svcCoin2 > 9) g_game.svcCoin2 = 1;
            break;
        case 2:
            svcGameOptionReset();
            g_game.svcCoin1 = 1;
            g_game.svcCoin2 = 1;
            break;
        case 3:
            GameOption_Save();
            /* fall-through: SAVE AND EXIT salva e cai no EXIT, como no original */
        case 4:
            g_svcPage = 0;
            break;
        default:
            break;
        }
    }
}

#endif /* era (Prex3): COIN OPTION sem CREDIT LIMIT, DEFAULT zerava a GAME OPTION */

/* NX: SETUP_COINSETTING de /SCRIPT/SETUP_COMMON.LUA. COIN1 +0xED6 (0 FREE PLAY,
 * n = 1 CREDIT / n COINS), COIN2 +0xED7 (n = n CREDITS / 1 COIN), CREDIT LIMIT
 * +0xED5 (0 OFF, 1..10). Padrões: COIN1 = 5, COIN2 = 1, CREDIT LIMIT = OFF.
 * "SAVE AND EXIT" comentado no script: o EXIT grava. */
static const char* SVC_COINOPT_ITEMS[6] = {
    "COIN1 SETTING", "COIN2 SETTING", "CREDIT LIMIT", "", "DEFAULT SETTING", "EXIT"
};

static void svcRenderCoinOption(void)
{
    char buf[64];
    int i;
    uint32_t hit = svcBitsHit();
    int lim = Eeprom_Get8(0xED5);
    if (lim > 10) lim = 0;

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "COIN OPTION");

    for (i = 0; i < 6; i++) {
        if (i == 3) continue;   /* MENU_BLANK */
        svcColorFor(i, g_svcCursor);
        svcText(196.0f, (float)(352 - i * 20), SVC_COINOPT_ITEMS[i]);
    }

    svcColorFor(0, g_svcCursor);
    if (g_game.svcCoin1 == 5) svcColor(SVC_SUCCESS);
    if (g_game.svcCoin1 == 0) snprintf(buf, sizeof(buf), "FREE PLAY");
    else snprintf(buf, sizeof(buf), "1 CREDIT / %d COIN%s", g_game.svcCoin1, g_game.svcCoin1 > 1 ? "S" : "");
    svcText(404.0f, 352.0f, buf);

    svcColorFor(1, g_svcCursor);
    if (g_game.svcCoin2 == 1) svcColor(SVC_SUCCESS);
    if (g_game.svcCoin2 == 0) snprintf(buf, sizeof(buf), "FREE PLAY");
    else snprintf(buf, sizeof(buf), "%d CREDIT%s / 1 COIN", g_game.svcCoin2, g_game.svcCoin2 > 1 ? "S" : "");
    svcText(404.0f, 332.0f, buf);

    svcColorFor(2, g_svcCursor);
    if (lim == 0) svcColor(SVC_SUCCESS);
    if (lim == 0) snprintf(buf, sizeof(buf), "OFF");
    else snprintf(buf, sizeof(buf), "%d", lim);
    svcText(404.0f, 312.0f, buf);

    if (hit & SVC_BIT_TEST) {
        do {
            g_svcCursor++;
            if (g_svcCursor > 5) g_svcCursor = 0;
        } while (g_svcCursor == 3);
    }
    if (hit & SVC_BIT_SERVICE) {
        switch (g_svcCursor) {
        case 0: g_game.svcCoin1 = (g_game.svcCoin1 + 1) % 11; break;
        case 1: g_game.svcCoin2 = (g_game.svcCoin2 + 1) % 11; break;
        case 2: Eeprom_Set8(0xED5, (uint8_t)((lim + 1) % 11)); break;
        case 4:
            g_game.svcCoin1 = 5;
            g_game.svcCoin2 = 1;
            Eeprom_Set8(0xED5, 0);
            break;
        case 5:
            GameOption_Save();
            g_svcPage = 0;
            break;
        default:
            break;
        }
    }
}

/* ----------------------------------- ServiceMenu_RenderSoundTest 0x00406030
 * Item 0 toca AUDIO/%03d.AUD, item 1 percorre 35 efeitos (0..0x22), item 2 sai.
 * O original compara g_dwInputBits por igualdade exata, não por máscara.
 */
#if 0
static const char* SVC_SOUND_ITEMS[3] = { "AUDIO", "EFFECT SOUND", "EXIT" };

static void svcRenderSoundTest(void)
{
    char buf[64];
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "SOUND TEST");

    for (i = 0; i < 3; i++) {
        svcColorFor(i, g_svcCursor);
        svcText(180.0f, (float)(352 - i * 20), SVC_SOUND_ITEMS[i]);
    }

    svcColorFor(0, g_svcCursor);
    if (g_svcAudioIdx < 0) {
        svcText(372.0f, 352.0f, "#--");
    } else {
        sprintf(buf, "#%02d", g_svcAudioIdx);
        svcText(372.0f, 352.0f, buf);
    }

    svcColorFor(1, g_svcCursor);
    if (g_svcSeIdx < 0) {
        svcText(372.0f, 332.0f, "#--");
    } else {
        sprintf(buf, "#%02d", g_svcSeIdx);
        svcText(372.0f, 332.0f, buf);
    }

    if (hit == SVC_BIT_TEST) {
        BGM_Stop();
        g_svcCursor++;
        if (g_svcCursor > 2) g_svcCursor = 0;
    } else if (hit == SVC_BIT_SERVICE) {
        if (g_svcCursor == 0) {
            int total = g_game.songDB.songCount;
            g_svcAudioIdx++;
            if (total > 0 && g_svcAudioIdx >= total) g_svcAudioIdx = 0;
            BGM_Stop();
            sprintf(buf, "AUDIO/%03d.AUD", g_svcAudioIdx);
            if (BGM_LoadAUDDirect(buf)) BGM_Play(false);
        } else if (g_svcCursor == 1) {
            g_svcSeIdx++;
            if (g_svcSeIdx > 0x22) g_svcSeIdx = 0;
            Audio_Play(g_svcSeIdx, false);
        } else if (g_svcCursor == 2) {
            BGM_Stop();
            g_svcPage = 0;
        }
    }
}

#endif /* era (Prex3): SOUND TEST com AUDIO/%03d.AUD e EFFECT SOUND */

/* NX SOUND TEST (0x8087bc0): itens AUDIO / EXIT em x = 180 (tabela 0x8144e54),
 * número "#%02d" (ou "#--") em x = 372; tocando: "Now Playing..." (180, 232),
 * "Title : " (180, 212) e "Artist: " (180, 192). Arquivo AUDIO/%X.AUD pelo id. */
static const char* SVC_SOUND_NX[2] = { "AUDIO", "EXIT" };

static void svcRenderSoundTest(void)
{
    char buf[160];
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "SOUND TEST");

    for (i = 0; i < 2; i++) {
        svcColorFor(i, g_svcCursor);
        svcText(180.0f, (float)(352 - i * 20), SVC_SOUND_NX[i]);
    }
    svcColorFor(0, g_svcCursor);
    if (g_svcAudioIdx < 0) svcText(372.0f, 352.0f, "#--");
    else { snprintf(buf, sizeof(buf), "#%02d", g_svcAudioIdx); svcText(372.0f, 352.0f, buf); }

    if (g_svcAudioIdx >= 0 && g_svcAudioIdx < EX_SONG_COUNT && BGM_IsPlaying()) {
        const ExceedSong* e = &g_exSongs[g_svcAudioIdx];
        svcColor(SVC_PALETTE[2]);
        svcText(180.0f, 232.0f, "Now Playing...");
        svcColor(SVC_PALETTE[3]);
        snprintf(buf, sizeof(buf), "Title : %s", e->titleEn ? e->titleEn : "");
        svcText(180.0f, 212.0f, buf);
        snprintf(buf, sizeof(buf), "Artist: %s", e->artistEn ? e->artistEn : "");
        svcText(180.0f, 192.0f, buf);
    }

    if (hit == SVC_BIT_TEST) {
        g_svcCursor = !g_svcCursor;
    } else if (hit == SVC_BIT_SERVICE) {
        if (g_svcCursor == 0) {
            g_svcAudioIdx++;
            if (g_svcAudioIdx >= EX_SONG_COUNT) g_svcAudioIdx = 0;
            BGM_Stop();
            snprintf(buf, sizeof(buf), "%s/AUDIO/%X.AUD", g_game.currentDirectory, (unsigned)g_exSongs[g_svcAudioIdx].id);
            if (BGM_LoadAUDDirect(buf)) BGM_Play(false);
        } else {
            BGM_Stop();
            g_svcPage = 0;
        }
    }
}


/* --------------------------------- ServiceMenu_RenderBookkeeping 0x00406360 */
#if 0
static const char* SVC_BOOK_ITEMS[2] = { "RESET", "EXIT" };

static void svcRenderBookkeeping(void)
{
    char buf[64];
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "BOOKKEEPING MENU");

    sprintf(buf, "SERVICE : %d", g_game.svcServiceTotal);
    svcText(180.0f, 352.0f, buf);
    sprintf(buf, "COIN 1  : %d", g_game.svcCoin1Total);
    svcText(180.0f, 332.0f, buf);
    sprintf(buf, "COIN 2  : %d", g_game.svcCoin2Total);
    svcText(180.0f, 312.0f, buf);

    /* 2 itens a partir de Y=288 descendo 20 */
    for (i = 0; i < 2; i++) {
        svcColorFor(i, g_svcCursor);
        svcText(180.0f, (float)(288 - i * 20), SVC_BOOK_ITEMS[i]);
    }

    if (hit == SVC_BIT_TEST) {
        g_svcCursor++;
        if (g_svcCursor > 1) g_svcCursor = 0;
    } else if (hit == SVC_BIT_SERVICE) {
        if (g_svcCursor == 0) {
            g_svcPage      = 10;
            g_svcSubCursor = 1;   /* começa em NO, como no original */
        } else {
            g_svcPage = 0;
        }
    }
}

/* ---------------------------- ServiceMenu_RenderClearBookkeeping 0x00406210 */
static const char* SVC_YESNO[2] = { "YES", "NO" };

static void svcRenderClearBookkeeping(void)
{
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "BOOKKEEPING MENU");
    svcText(196.0f, 352.0f, "CLEAR BOOKKEEPING DATA ?");

    /* YES/NO na horizontal: x = 228 e 324, ambos em Y=312 */
    for (i = 0; i < 2; i++) {
        svcColorFor(i, g_svcSubCursor);
        svcText((float)(228 + i * 96), 312.0f, SVC_YESNO[i]);
    }

    if (hit == SVC_BIT_TEST) {
        g_svcSubCursor++;
        if (g_svcSubCursor > 1) g_svcSubCursor = 0;
    } else if (hit == SVC_BIT_SERVICE) {
        if (g_svcSubCursor == 0) {
            g_game.svcServiceTotal = 0;
            g_game.svcCoin1Total   = 0;
            g_game.svcCoin2Total   = 0;
            Log_Print("ServiceMenu: bookkeeping zerado\n");
        }
        g_svcPage = 7;
    }
}

/* ----------------------------------- ServiceMenu_RenderStatistics 0x004064e0
 * Duas colunas de 15 entradas (30 por página). As 10 primeiras entradas da
 * coluna esquerda recebem um degradê de vermelho para branco — no original
 * glColor3f(1, i*0.1, i*0.1) para i em 0..9, e branco daí em diante.
 */
static void svcRenderStatistics(void)
{
    char buf[96];
    int i, y, total;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "STATISTICS MENU");

    total = g_game.songDB.songCount;

    /* coluna esquerda — entradas [base, base+15) */
    y = 352;
    for (i = g_svcCursor * 30; i < g_svcCursor * 30 + 15; i++) {
        float f = (i >= 0 && i <= 9) ? (float)i * 0.1f : 1.0f;
        if (i >= total) break;
        glColor3f(1.0f, f, f);
        sprintf(buf, "%02d. %-24s [%04d]", i, g_game.songDB.songs[i].title, 0);
        svcText(16.0f, (float)y, buf);
        y -= 20;
    }

    /* coluna direita — entradas [base+15, base+30), sempre em branco */
    y = 352;
    for (i = g_svcCursor * 30 + 15; i < g_svcCursor * 30 + 30; i++) {
        if (i >= total) break;
        sprintf(buf, "%02d. %-24s [%04d]", i, g_game.songDB.songs[i].title, 0);
        svcText(336.0f, (float)y, buf);
        y -= 20;
    }

    if (hit == SVC_BIT_TEST) {
        int pages = total / 30;
        if (total % 30 != 0) pages++;
        if (pages < 1) pages = 1;
        g_svcCursor++;
        if (g_svcCursor >= pages) g_svcCursor = 0;
    } else if (hit == SVC_BIT_SERVICE) {
        g_svcPage = 0;
    }
}

#endif /* era (Prex3): BOOKKEEPING com RESET/EXIT, YES/NO na horizontal, STATISTICS "%02d. %-24s" */

/* NX BOOKKEEPING (0x8088500): SERVICE/COIN 1/COIN 2 em x = 180, y = 352/332/312;
 * TICKET 1/2 em y = 272/252; opções RESET BOOKKEEPING / RESET RANKING / EXIT em
 * x = 180, y = 224, 204, 184 (tabela 0x8144e84, KR/EN). Contadores da EEPROM:
 * +0xF23 SERVICE, +0xF1B COIN 1, +0xF1F COIN 2, +0xF2F TICKET 1, +0xF33 TICKET 2. */
static const char* SVC_BOOK_ITEMS[3] = { "RESET BOOKKEEPING", "RESET RANKING", "EXIT" };

static void svcRenderBookkeeping(void)
{
    char buf[64];
    int i;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "BOOKKEEPING MENU");

    sprintf(buf, "SERVICE : %d", g_game.svcServiceTotal);
    svcText(180.0f, 352.0f, buf);
    sprintf(buf, "COIN 1  : %d", g_game.svcCoin1Total);
    svcText(180.0f, 332.0f, buf);
    sprintf(buf, "COIN 2  : %d", g_game.svcCoin2Total);
    svcText(180.0f, 312.0f, buf);
    sprintf(buf, "TICKET 1 : %u", (unsigned)Eeprom_Get32(0xF2F));
    svcText(180.0f, 272.0f, buf);
    sprintf(buf, "TICKET 2 : %u", (unsigned)Eeprom_Get32(0xF33));
    svcText(180.0f, 252.0f, buf);

    for (i = 0; i < 3; i++) {
        svcColorFor(i, g_svcCursor);
        svcText(180.0f, (float)(224 - i * 20), SVC_BOOK_ITEMS[i]);
    }

    if (hit == SVC_BIT_TEST) {
        g_svcCursor++;
        if (g_svcCursor > 2) g_svcCursor = 0;
    } else if (hit == SVC_BIT_SERVICE) {
        if (g_svcCursor == 0)      { g_svcPage = 10; g_svcSubCursor = 1; }   /* começa em NO */
        else if (g_svcCursor == 1) { g_svcPage = 13; g_svcSubCursor = 1; }
        else g_svcPage = 0;
    }
}

/* NX 0x8088370 / 0x8088200: pergunta em (196, 352), YES/NO em x = 312 (tabelas
 * 0x8144e74 / 0x8144e64). O y do YES/NO não foi lido: HIPÓTESE 312 e 292. */
static const char* SVC_YESNO[2] = { "YES", "NO" };

static int svcYesNo(const char* question)
{
    int i;
    uint32_t hit = svcBitsHit();
    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "BOOKKEEPING MENU");
    svcText(196.0f, 352.0f, question);
    for (i = 0; i < 2; i++) {
        svcColorFor(i, g_svcSubCursor);
        svcText(312.0f, (float)(312 - i * 20), SVC_YESNO[i]);
    }
    if (hit == SVC_BIT_TEST) {
        g_svcSubCursor++;
        if (g_svcSubCursor > 1) g_svcSubCursor = 0;
        return -1;
    }
    if (hit == SVC_BIT_SERVICE) return g_svcSubCursor == 0 ? 1 : 0;
    return -1;
}

static void svcRenderClearBookkeeping(void)
{
    int r = svcYesNo("CLEAR BOOKKEEPING DATA ?");
    if (r < 0) return;
    if (r == 1) {
        g_game.svcServiceTotal = 0;
        g_game.svcCoin1Total   = 0;
        g_game.svcCoin2Total   = 0;
        Eeprom_Set32(0xF2F, 0);
        Eeprom_Set32(0xF33, 0);
        GameOption_Save();
        Log_Print("ServiceMenu: bookkeeping zerado\n");
    }
    g_svcPage = 7;
}

static void svcRenderClearRanking(void)
{
    int r = svcYesNo("CLEAR HIGHSCORE RANKING DATA ?");
    if (r < 0) return;
    if (r == 1) {
        Rank_Reset();
        Log_Print("ServiceMenu: ranking zerado\n");
    }
    g_svcPage = 7;
}

/* NX STATISTICS (0x8088810): duas colunas de 15, "%03d. %s" e a contagem
 * "[%04d]" em x = 264 (esquerda) e 584 (direita); contagem por música em
 * EEPROM +0xBAA (200 u32). Mesmo degradê do original nas 10 primeiras. */
static void svcRenderStatistics(void)
{
    char buf[96];
    int i, y, total;
    uint32_t hit = svcBitsHit();

    svcColor(SVC_NORMAL);
    svcText(276.0f, 428.0f, "STATISTICS");

    total = g_game.songDB.songCount;
    if (total > 200) total = 200;

    for (int col = 0; col < 2; col++) {
        y = 352;
        for (i = g_svcCursor * 30 + col * 15; i < g_svcCursor * 30 + col * 15 + 15; i++) {
            if (i >= total) break;
            float f = (col == 0 && i <= 9) ? (float)i * 0.1f : 1.0f;
            glColor3f(1.0f, f, f);
            snprintf(buf, sizeof(buf), "%03d. %.22s", i, g_game.songDB.songs[i].title);
            svcText(col ? 336.0f : 16.0f, (float)y, buf);
            snprintf(buf, sizeof(buf), "[%04u]", (unsigned)Eeprom_Get32(0xBAA + 4 * i));
            svcText(col ? 584.0f : 264.0f, (float)y, buf);
            y -= 20;
        }
    }

    if (hit == SVC_BIT_TEST) {
        int pages = total / 30;
        if (total % 30 != 0) pages++;
        if (pages < 1) pages = 1;
        g_svcCursor++;
        if (g_svcCursor >= pages) g_svcCursor = 0;
    } else if (hit == SVC_BIT_SERVICE) {
        g_svcPage = 0;
    }
}

/* ============================================================== interface ==*/

/* ------------------------------------------ ServiceMenu_Enter 0x00404ee0 */
void ServiceMenu_Enter(void)
{
    BGM_Stop();
    /* o setup entra em silêncio: música da Title (EFF_TITLE), efeitos e os
     * vídeos com áudio (prévia da Select na 2ª instância) */
    Title_StopMusic();
    Audio_StopAll();
    Movie_Select(1); Movie_Close(); Movie_Select(0);
    Movie_Close();

    g_svcPage       = 0;
    g_svcOption     = 0;
    g_svcCursor     = 0;
    g_svcSubCursor  = 0;
    g_svcEepromDone = 0;
    g_svcSkipInput  = 1;    /* engole o F2 que abriu o menu */

    /* A ordem aqui importa. Game_ChangeState chama LoadBGAForState, que para
     * STATE_SERVICE_MENU cai em Resource_ClearBGA() — e esse chama
     * Texture_Shutdown() + Font_Shutdown(), zerando g_fontTexId e os display
     * lists GDI. Carregar a fonte antes desta linha seria desfeito na hora,
     * e como a página principal do menu é só texto, o resultado é tela preta. */
    Game_ChangeState(STATE_SERVICE_MENU);

    /* Só agora recarrega a fonte que o clear acabou de derrubar. */
    g_svcFontTex = -2;   /* o SCOREFONT.TGA também caiu no clear: recarrega no 1º svcText */
    Font_Init();   /* display lists GDI (Font_DrawString / overlay de debug) */
    {
        int fid = Font_LoadTexture();   /* textura usada por Font_DrawText */
        Log_Print("ServiceMenu: entrou (fontTexId=%d)%s\n", fid,
                  fid < 0 ? "  <<< AVISO: sem fonte, o menu fica invisivel" : "");
    }
}

/* ------------------------------------------- ServiceMenu_Exit 0x004066d0
 * O original volta para o estado 4, que aqui é STATE_LOGO_ENTER.
 */
void ServiceMenu_Exit(void)
{
    Log_Print("ServiceMenu: saiu\n");
    Game_ChangeState(STATE_LOGO_ENTER);
}

/* --------------------------------------------------- ServiceMenu_Update ---
 * Captura o input do frame. Precisa rodar na fase de update, não no render.
 *
 * Motivo: Game_Update termina com memcpy(prevKeys, keys) (main.c). Como
 * Input_IsKeyHit() é "keys[k] && !prevKeys[k]", depois desse memcpy nenhuma
 * borda é mais detectável — qualquer IsKeyHit() chamado durante Game_Render
 * retorna false. O original lê o input dentro da própria função de render, mas
 * aqui isso deixaria o menu completamente inerte.
 */
void ServiceMenu_Update(void)
{
    if (Input_IsListening()) {
        g_svcHitBits   = 0;
        g_svcHeldBits  = 0;
        g_svcSkipInput = 0;
        return;
    }

    uint32_t hit = 0, held = 0;

    if (Input_IsKeyHit(VK_F1) || Input_IsKeyHit(VK_DOWN))   hit |= SVC_BIT_TEST;     /* TEST    = MOVE   */
    if (Input_IsKeyHit(VK_F2) || Input_IsKeyHit(VK_RETURN)) hit |= SVC_BIT_SERVICE;  /* SERVICE = SELECT */
    if (Input_IsKeyHit(VK_F3)) hit |= SVC_BIT_CLEAR;
    if (Input_IsKeyHit(VK_F4)) hit |= SVC_BIT_COIN1;
    if (Input_IsKeyHit(VK_F5)) hit |= SVC_BIT_COIN2;

    if (Input_IsKeyDown(VK_F1) || Input_IsKeyDown(VK_DOWN))   held |= SVC_BIT_TEST;
    if (Input_IsKeyDown(VK_F2) || Input_IsKeyDown(VK_RETURN)) held |= SVC_BIT_SERVICE;
    if (Input_IsKeyDown(VK_F3)) held |= SVC_BIT_CLEAR;
    if (Input_IsKeyDown(VK_F4)) held |= SVC_BIT_COIN1;
    if (Input_IsKeyDown(VK_F5)) held |= SVC_BIT_COIN2;

    g_svcHitBits   = g_svcSkipInput ? 0u : hit;
    g_svcHeldBits  = held;
    g_svcSkipInput = 0;
}

/* ---------------------------------- ServiceMenu_UpdateRender 0x004066e0
 * Desenha a página atual e aplica o input já capturado por ServiceMenu_Update.
 */
void ServiceMenu_UpdateRender(void)
{
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    svcColor(SVC_NORMAL);

    switch (g_svcPage) {
    case 0:  svcRenderMain();             svcRenderFooter(); break;
    case 1:  svcRenderIOTest();           svcRenderFooter(); break;
    case 2:  svcRenderEEPROMTest();       svcRenderFooter(); break;
    case 3:  svcRenderScreenTest();       svcRenderFooter(); break;
    case 4:  svcRenderGameOption();       svcRenderFooter(); break;
    case 5:  svcRenderCoinOption();       svcRenderFooter(); break;
    case 6:  svcRenderSoundTest();        svcRenderFooter(); break;
    case 7:  svcRenderBookkeeping();      svcRenderFooter(); break;
    case 8:  svcRenderStatistics();       svcRenderFooter(); break;
    case 9:  ServiceMenu_Exit();          svcRenderFooter(); break;
    case 10: svcRenderClearBookkeeping(); svcRenderFooter(); break;
    case 13: svcRenderClearRanking();     svcRenderFooter(); break;   /* NX: CLEAR HIGHSCORE RANKING */
    case SVC_PAGE_GRAPHICS:      svcRenderGraphics();      svcRenderFooter(); break;
    case SVC_PAGE_BUTTON_CONFIG: svcRenderButtonConfig();  svcRenderFooter(); break;
    default: svcRenderFooter(); break;
    }
}
