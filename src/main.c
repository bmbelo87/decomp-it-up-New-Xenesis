#include "pumpy.h"
#include "recomp_version.h"   /* RECOMP_VERSION (gerado) */
#include "testbga.h"
#include "movie.h"
#include "vsl.h"
#include <SDL.h>

extern bool Window_Create(HINSTANCE hInstance, int width, int height, bool fullscreen);
extern void Window_Destroy(void);
extern void Window_SwapBuffers(void);
extern bool Window_ProcessMessages(void);

static void InitSystems(void) {
    Log_Print("=== PUMP IT UP v%s (%s %s) ===\n",
              GAME_VERSION, GAME_BUILD_DATE, GAME_BUILD_TIME);
    Log_Print("Build: RECOMP-%s\n", RECOMP_VERSION);   /* versão do projeto, para conferir em logs de usuário */

    timeBeginPeriod(1);

    /* Tamanho da JANELA. O backbuffer lógico continua 640x480 (é a resolução
     * em que toda a arte foi feita e a base de todas as coordenadas do jogo);
     * o Window_UpdateViewport estica o viewport e mantém o glOrtho em 640x480,
     * então subir aqui só amplia a imagem, sem mexer em nenhuma coordenada.
     * 1024x768 é 4:3, o mesmo aspecto, então não entra tarja preta. */
    /* if (!Window_Create(NULL, WINDOW_DEFAULT_W, WINDOW_DEFAULT_H, false)) { */
    int winW, winH;
    Window_GetResolution(g_game.gfxResIdx, &winW, &winH);   /* GRAPHICS SETTINGS */
    if (!Window_Create(NULL, winW, winH, g_game.isFullscreen)) {
        Log_Print("FATAL: could not create SDL2/OpenGL window: %s\n", SDL_GetError());
        exit(1);
    }

    Render_SetOrtho(LOGICAL_SCREEN_W, LOGICAL_SCREEN_H);

    Log_Print("Systems initialized\n");
}

static void ShutdownSystems(void) {
    Log_Print("Shutting down...\n");
    Window_Destroy();
    timeEndPeriod(1);
    Log_Flush();
}

#include "bga.h"

static void LoadBGAForState(GameState state) {
    const char* bgaName = NULL;
    switch (state) {
    case STATE_WARNING_INIT:
    case STATE_WARNING_ANIM: bgaName = "R_WARN"; break;   /* Exceed2: BGA\R_WARN.DAT (PIU32.EXE); Exceed usava R_WARN_A */
    case STATE_INTRO:        bgaName = ""; break;       /* vídeo, sem BGA */
    case STATE_CREDIT:       bgaName = "82"; break;
    case STATE_NAMEINPUT:    bgaName = "085"; break;    /* CNameInput 0x414012: BGA\085.DAT */
    case STATE_IR:           bgaName = "IR"; break;     /* CInternetRanking 0x41371B: BGA\IR.DAT */
    case STATE_STATION:      bgaName = NULL; break;     /* NX: usa o COMMON/ARRO que a CTitle deixou (era: "" + STATION.DAT) */
    case STATE_HIGHSCORE:    bgaName = "HIGHSCORE"; break;   /* era "HS" (Exceed); NX: BGA/HIGHSCORE.DAT */     /* CHighscore 0x4126E7: BGA\HS.DAT */     /* CTitle::Begin 0x41C04A: BGA\82.DAT sobre o CREDIT.MOV */
    case STATE_LOGO_ENTER:   bgaName = "81"; break;
    case STATE_MENU_ENTER:
    case STATE_MENU_INPUT:
    case STATE_LOGO_SKIP:    bgaName = "82w"; break;
    case STATE_GAME_INIT:
    case STATE_GAMEPLAY:
        if (g_game.selectedSongIndex >= 0 && g_game.selectedSongIndex < g_game.songDB.songCount) {
            static char songBGAName[16];
            snprintf(songBGAName, sizeof(songBGAName), "%s", Song_DataIdStr(g_game.songDB.songs[g_game.selectedSongIndex].id));
            bgaName = songBGAName;
        } else {
            bgaName = "00";
        }
        break;
    case STATE_GAMEOVER:         bgaName = "84"; break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B: bgaName = "099"; break;
    case STATE_SONG_TITLE:
    case STATE_SONG_TITLE_OUT: bgaName = ""; break;
    case STATE_STAFF_ENTER:
    case STATE_STAFF:
    case STATE_STAFF_END:        bgaName = ""; break;
    case STATE_STAGE_TRANSITION:
        if (g_hexSongIds)            /* Exceed: BGA\NST%d.MOV (sem .DAT) */
            bgaName = "";
        else if (g_game.isBonusSong)
            bgaName = "lt03";
        else
            bgaName = "lt01";
        break;
    case STATE_GAMEOVER_ENTER:   bgaName = g_hexSongIds ? "" : "84"; break; /* Exceed: BGA\GAMEOVER.MOV */
    case STATE_STAGE_BREAK:           /* Prex3: 083.DAT + 7-1.WAV; Exceed: BGA\SB.MOV (sem .DAT) */
        bgaName = g_hexSongIds ? "" : "083"; break;
    case STATE_DANCE_GRADE_ENTER:
    case STATE_DANCE_GRADE_DISPLAY: bgaName = g_hexSongIds ? (g_game.nxGameMode == 2 ? "WORLDGRADE" : "GRADE") : "83"; break; /* Exceed 0x40CD2A: BGA\GRADE.DAT; NX WORLD TOUR 0x808e6c6: BGA/WORLDGRADE.DAT */
    case STATE_HOWTOPLAY: bgaName = ""; break; /* limpa BGA do menu imediatamente; 03.DAT carrega no stateFrame==1 */
    case STATE_SERVICE_MENU: bgaName = ""; break; /* SETUP MENU desenha sobre fundo preto */
    default: break;
    }

    if (bgaName) {
        if (bgaName[0] == '\0') {
            Resource_ClearBGA();
        } else if (g_game.bgaPicCount == 0 || _stricmp(g_game.bgaPics[0].name, bgaName) != 0) {
            int idx = Resource_SwitchBGA(bgaName);
            if (idx < 0 && (state == STATE_SONG_SELECT || state == STATE_SONG_SELECT_B ||
                            state == STATE_STAFF_ENTER)) {
                char directPath[MAX_PATH];
                snprintf(directPath, sizeof(directPath), "%s/BGA/%s.DAT",
                         g_game.currentDirectory, bgaName);
                Log_Print("BGA: fallback loading '%s'\n", directPath);
                Resource_LoadBGADirect(directPath);
            }
        }
    }

    /* Exceed CSelect: SELECT.DAT + SELECT2.DAT juntos + banners do 90.DAT */
    if (state == STATE_SELECT) {
        BGM_Stop();
        /* ClassicSelect_Enter(); */    /* Zero: CSelect (select_classic.c) */
        NxSelect_Enter();            /* NX: CSelect (nx_select.c) */
        /* Select_Enter(); */      /* Exceed2 */
    }

    /* NX CStation: Begin no próprio quadro da troca (sem quadro preto entre Title e Station) */
    if (state == STATE_STATION) NxStation_Enter();

    if (state == STATE_MENU_ENTER) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/AUDIO/082.AUD", g_game.currentDirectory);
        BGM_Stop();
        if (BGM_LoadAUDDirect(path)) BGM_Play(true);
    } else if (state == STATE_SONG_SELECT || state == STATE_SONG_SELECT_B) {
        BGM_Stop();
        /* DESATIVADO (Select do Prex3):
        if (g_game.stageCount == 3)
            SongSelect_Reset();
        else
            SongSelect_ResetIntro(); */
    }
    
    g_game.bgaLoop = (state == STATE_MENU_ENTER || state == STATE_MENU_INPUT ||
                      state == STATE_EXIT);
    if (g_game.bgaLoop) {
        g_game.bgaLoopStart = findBGALoopStart();
        g_game.bgaLoopEnd = findBGALoopEnd();
        g_game.bgaFrame = g_game.bgaLoopStart;
        Log_Print("BGA: loop range %d -> %d (maxFrame=%d)\n",
                  g_game.bgaLoopStart, g_game.bgaLoopEnd,
                  g_game.bgaMaxFrame);
    } else {
        g_game.bgaLoop = false;
        if (state == STATE_SONG_SELECT || state == STATE_SONG_SELECT_B) {
            g_game.bgaFrame = 0;
            if (g_game.bgaMaxFrame > 0) {
                g_game.bgaLoopStart = 0;
                g_game.bgaLoopEnd = 600;
                g_game.bgaLoop = true;
            }
        }
    }
}

/* Reseta todos os cheats de todos os players (ESC e Game Over). */
void Game_ResetAllCheats(void) {
    for (int _p = 0; _p < 2; _p++) {
        g_game.cmdSpeedMult[_p]      = 1;
        g_game.cmdMirror[_p]         = false;
        g_game.cmdRandomStep[_p]     = false;
        g_game.cmdRandomVelocity[_p] = false;
        g_game.cmdEarthworm[_p]      = false;
        g_game.cmdFreedom[_p]        = false;
        g_game.cmdVanish[_p]         = false;
        g_game.cmdNonStep[_p]        = false;
        g_game.cmdTestBGA[_p]        = false;
        g_game.cmdXMode[_p] = g_game.cmdAccel[_p] = g_game.cmdDecel[_p] = false;
        g_game.cmdFlash[_p] = g_game.cmdGradeRev[_p] = false;
        g_game.cmdNXMode[_p] = g_game.cmdUnderAttack[_p] = false;
    }
    Log_Print("CHEATS: reset global (ESC/GameOver)\n");
}

void Game_ChangeState(GameState newState) {
    Log_Print("State: %s -> %s\n",
              State_ToString(g_game.state), State_ToString(newState));
    g_game.state = newState;
    g_game.nextState = newState;
    g_game.stateFrame = 0;
    g_game.confirmActive = false;
    g_game.confirmTimer = 0;
    Render_SetGlobalColor(0, 0, 0, 0);
    LoadBGAForState(newState);
}

/* Extra do port (ferramenta, tools/import_old_data.py): PUMPY_BGACAP=<arquivo de tarefas>.
 * Cada linha é "caminho do .DAT;início em s;duração em s;arquivo de saída". Para cada uma,
 * carrega o BGA como o gameplay (Resource_LoadBGADirect), desenha 30 quadros por segundo
 * do trecho pedido — o quadro do BGA é tempo * 60, como em Gameplay_Update — e grava os
 * pixels do backbuffer: int32 largura, int32 altura, depois os quadros em RGB de baixo
 * para cima. Não abre tela nem toca nada; o jogo fecha ao terminar. */
static void bgaCaptureRun(const char* listPath) {
    FILE* lf = fopen(listPath, "r");
    if (!lf) { Log_Print("BGACAP: não abriu '%s'\n", listPath); return; }
    char line[1024];
    while (fgets(line, sizeof(line), lf)) {
        char* f1 = strchr(line, ';');
        char* f2 = f1 ? strchr(f1 + 1, ';') : NULL;
        char* f3 = f2 ? strchr(f2 + 1, ';') : NULL;
        if (!f3) continue;
        *f1 = *f2 = *f3 = 0;
        char* outPath = f3 + 1;
        outPath[strcspn(outPath, "\r\n")] = 0;
        double start = atof(f1 + 1), dur = atof(f2 + 1);

        Resource_ClearBGA();
        Resource_LoadBGADirect(line);
        g_game.bgaLoop = false;
        BGA_Reset();
        int maxFrame = g_game.bgaMaxFrame;
        if (g_game.isVSL && g_vsl.active && g_vsl.frameCount > 0) maxFrame = g_vsl.frameCount - 1;
        if (g_game.bgaPicCount <= 0 && !(g_game.isVSL && g_vsl.active)) {
            Log_Print("BGACAP: '%s' não carregou\n", line);
            continue;
        }
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        int w = vp[2], h = vp[3];
        unsigned char* px = (unsigned char*)malloc((size_t)w * h * 3);
        FILE* of = fopen(outPath, "wb");
        if (!px || !of) { Log_Print("BGACAP: não criou '%s'\n", outPath); free(px); if (of) fclose(of); continue; }
        fwrite(&w, 4, 1, of);
        fwrite(&h, 4, 1, of);
        int n = (int)(dur * 30.0 + 0.5);
        for (int k = 0; k < n; k++) {
            int frame = (int)((start + k / 30.0) * 60.0);
            if (maxFrame > 0 && frame > maxFrame) frame = maxFrame;
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, 640, 0, 480, -1.0, 1.0);
            glMatrixMode(GL_MODELVIEW);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glLoadIdentity();
            glColor4f(1, 1, 1, 1);
            g_game.bgaFrame = frame;
            if (g_game.isVSL && g_vsl.active) VSL_Render(frame);
            else BGA_Render(0, frame);
            glFinish();
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(vp[0], vp[1], w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
            fwrite(px, 1, (size_t)w * h * 3, of);
        }
        fclose(of);
        free(px);
        Log_Print("BGACAP: '%s' %.2f s + %.2f s -> %d quadros %dx%d (maxFrame %d)\n", line, start, dur, n, w, h, maxFrame);
    }
    fclose(lf);
}

void Game_Init(HINSTANCE hInstance) {
    memset(&g_game, 0, sizeof(g_game));
    g_game.hInstance = hInstance;
    /* screenWidth/Height são as dimensões LÓGICAS, usadas como coordenada de
     * desenho em vários lugares (Font_DrawStringCentered(screenWidth/2, ...)).
     * Não são o tamanho da janela — esse fica em Window_GetSize(). */
    g_game.screenWidth = LOGICAL_SCREEN_W;
    g_game.screenHeight = LOGICAL_SCREEN_H;
    /* Vsync ligado: o SDL_GL_SwapWindow passa a ser quem dá o ritmo do loop.
     * Precisa estar definido ANTES do InitSystems, que é quem cria a janela e
     * lê este campo no SDL_GL_SetSwapInterval. */
    g_game.vsync = true;
    g_game.confirmActive = false;
    g_game.confirmTimer = 0;
    g_game.globalScaleX = 1.0f;
    g_game.globalScaleY = 1.0f;
    g_game.globalAlpha = 1.0f;
    /* g_game.showDebug = true; */
    g_game.showDebug = false;  /* debug oculto ao abrir; F11 alterna (Game_Update) */
    /* g_game.stageBreak = 1; */ /* DISABLED: controlado por optionToggle1 no GameOption */
    g_game.showHelp = 0;
    g_game.cmdSpeedMult[0] = 1;    /* Command P1: velocidade padrão x1 */
    g_game.cmdSpeedMult[1] = 1;    /* Command P2: velocidade padrão x1 */
    g_game.activePlayerMask = 0x1; /* P1 ativo por padrão */
    g_game.isBattleMode = false;   /* BATTLE só ativo quando selecionado no song_select */
    Render_SetGlobalColor(0, 0, 0, 0);
    GetCurrentDirectoryA(MAX_PATH, g_game.currentDirectory);
    GameOption_Load(); /* lê PUMPY.INI — antes de qualquer sistema, para que o restante já veja os valores corretos */
    if (getenv("PUMPY_BGACAP")) g_game.isFullscreen = false;   /* captura de BGA (bgaCaptureRun): sempre em janela */
    Skin_SetIndex(Skin_Default());   /* skin padrão conforme o GAME STYLE lido do PUMPY.INI */
    InitSystems();
    Audio_Init();
    Font_Init();
    Texture_Init();
    Audio_LoadAllWaves();
    Ranking_RegisterDefaults();  /* Game_InitState faz isso em 0x0040517c */

    Log_Print("Loading song database...\n");
    char cfgPath[MAX_PATH];
    char stepDir[MAX_PATH];
    snprintf(cfgPath, sizeof(cfgPath), "%s/Stage.cfg", g_game.currentDirectory);
    snprintf(stepDir, sizeof(stepDir), "%s/STEP", g_game.currentDirectory);
    bool dbOk = Song_LoadDatabase(cfgPath, &g_game.songDB);
    if (!dbOk) {
        /* Exceed: sem Stage.cfg, as músicas vêm das tabelas do exceed.exe */
        OldData_ApplyModes();   /* extra do port: modos de .STX antigo, antes de montar o songDB */
        Select_BuildSongDB(&g_game.songDB);
        dbOk = g_game.songDB.songCount > 0;
    }
    if (dbOk)
    {
        Log_Print("Song database: %d songs, %d modes\n",
            g_game.songDB.songCount, g_game.songDB.modeCount);
        for (int i = 0; i < g_game.songDB.songCount; i++)
        {
            SongEntry* e = &g_game.songDB.songs[i];
            char stxPath[MAX_PATH];
            /* era (Zero): "%s/%s.STX" */
            snprintf(stxPath, sizeof(stxPath), "%s/%s.SEE", stepDir, Song_DataIdStr(e->id));
            FILE* test = fopen(stxPath, "rb");
            if (test) { fclose(test); e->hasChart = true; }
            else {
                /* Exceed2: .STX dentro de STEP.DAT (PIU32.EXE 0x40d0d7) */
                char datPath[MAX_PATH], stxName[64];
                uint32_t sz = 0;
                snprintf(datPath, sizeof(datPath), "%s/STEP.DAT", g_game.currentDirectory);
                snprintf(stxName, sizeof(stxName), "%s.SEE", Song_DataIdStr(e->id));
                uint8_t* stx = Resource_ExtractFromPack(datPath, stxName, &sz);
                if (!stx && Song_BaseId(e->id) >= 0) {  /* NX 0x8072380: recurso da base */
                    snprintf(stxName, sizeof(stxName), "%s.SEE", Song_IdStr(Song_BaseId(e->id)));
                    stx = Resource_ExtractFromPack(datPath, stxName, &sz);
                }
                if (stx) { free(stx); e->hasChart = true; }
            }
        }
        g_game.selectedSongIndex = 0;
        g_game.selectedModeIndex = Song_FindMode(&g_game.songDB, "EASY");
        if (g_game.selectedModeIndex < 0 && g_game.songDB.modeCount > 0)
            g_game.selectedModeIndex = 0;
        g_game.songSelectScroll = 0;
        g_game.songSelectHighlighted = 0;
        g_game.selectedDifficulty = 0;
        g_game.previewSongId = -1;
    }
    else
    {
        Log_Print("WARNING: Failed to load song database from '%s'\n", cfgPath);
    }

    /* Zero: sem R_WARN, abre direto o LOGO (BGA\81.DAT) */
    Game_ChangeState(STATE_LOGO_ENTER);
    /* era (Exceed2): Game_ChangeState(STATE_WARNING_INIT); */
    {   /* extra deste projeto (teste): PUMPY_AUTOSTATE=SELECT abre direto a Select */
        const char* as = getenv("PUMPY_AUTOSTATE");
        if (as && _stricmp(as, "SELECT") == 0) { Menu_ResetState(); Game_ChangeState(STATE_SELECT); }
        if (as && _stricmp(as, "STATION") == 0) Game_ChangeState(STATE_STATION);
        if (as && _stricmp(as, "HIGHSCORE") == 0) Game_ChangeState(STATE_HIGHSCORE);   /* teste da tela de ranking */
        /* PUMPY_AUTOSTATE=NXTEST: D02 NORMAL em autoplay (demo) com NX Mode, para testes */
        /* PUMPY_AUTOSTATE=RUN + PUMPY_TEST_ID=<hex> [+ PUMPY_TEST_MODE=0..4]: abre direto
         * uma música/dificuldade (teste; combine com PUMPY_AUTOPLAY) */
        if (as && _stricmp(as, "RUN") == 0 && getenv("PUMPY_TEST_ID")) {
            const char* tm = getenv("PUMPY_TEST_MODE");
            Menu_ResetState();
            Select_Run((int)strtol(getenv("PUMPY_TEST_ID"), NULL, 16), tm ? atoi(tm) : 0,
                         getenv("PUMPY_TEST_DEMO") ? 1 : 0);   /* PUMPY_TEST_DEMO: como DEMO PLAY */
        }
        if (as && _stricmp(as, "NXTEST") == 0) {
            Menu_ResetState();
            if (Select_Run(0xD02, 3, 0)) { g_game.cmdNXMode[0] = g_game.cmdNXMode[1] = true; }
        }
        /* PUMPY_AUTOSTATE=NXTEST2: 2 jogadores, P1 NORMAL e P2 HARD (teste do chart por jogador) */
        if (as && _stricmp(as, "NXTEST2") == 0) {
            static const int sp[2] = { 1, 1 };
            static const bool rv[2] = { false, false };
            Menu_ResetState();
            for (int i = 0; i < SONG_TABLE_COUNT; i++)
                if (g_songTable[i].id && g_songTable[i].level[0] >= 0 && g_songTable[i].level[1] >= 0 && !(g_songTable[i].channel & 4)) {
                    g_nxDiffIdx[0] = 0; g_nxDiffIdx[1] = 1;
                    if (Select_StartSong((int)g_songTable[i].id, 0, 3, sp, rv)) break;
                }
        }
    }
    if (getenv("PUMPY_BGACAP")) {   /* ferramenta: captura e fecha, sem entrar no laço do jogo */
        bgaCaptureRun(getenv("PUMPY_BGACAP"));
        Game_Shutdown();
        exit(0);
    }
    g_game.lastTime = timeGetTime();
}

void Game_Shutdown(void) {
    Audio_Shutdown();
    BGM_Shutdown();
    Font_Shutdown();
    ShutdownSystems();
}



void Game_Update(float dt) {
    Input_Update();
    BGM_Update();   /* reinicia a faixa quando ela está em loop (ver audio.c) */
    if (Input_IsKeyHit(VK_F11)) g_game.showDebug = !g_game.showDebug;

    /* Crase abre/fecha o console de debug (0x29 no original) */
    if (Input_IsKeyHit(VK_OEM_3)) Debug_ConsoleToggle();

    /* Com o console aberto o jogo não enxerga teclado nem pad — as teclas são
     * entregues ao console pelo WndProc. O resto do update segue rodando para
     * as animações não congelarem. */
    if (Debug_ConsoleIsActive()) {
        memset(g_game.input.keys, 0, sizeof(g_game.input.keys));
        memset(g_game.input.padState, 0, sizeof(g_game.input.padState));
    }

    /* Alt+F4: encerra o jogo imediatamente de qualquer tela */
    if (Input_IsKeyHit(VK_F4) && Input_IsKeyDown(VK_MENU))
        Window_RequestQuit();

    /* Alt+Enter: alterna janela / tela cheia exclusiva de qualquer tela. A função
     * Window_ToggleFullscreen existia, mas nada a chamava: o atalho não fazia nada. */
    /* era: if (Input_IsKeyHit(VK_RETURN) && Input_IsKeyDown(VK_MENU)) — o Enter também
     * chegava à tela atual. Agora o input.c retém o Enter enquanto o Alt está segurado. */
    if (Input_AltEnterHit())
        Window_ToggleFullscreen();

    /* F1 abre o SETUP MENU de qualquer tela. Dentro do menu o F1 passa a ser o
     * TEST BUTTON (percorre a lista) e o F2 o SERVICE BUTTON (confirma) —
     * tratados em service_menu.c. */
    /* Botoeira do gabinete, na ordem que o I/O TEST do original lista:
     *   F1 TEST | F2 SERVICE | F3 CLEAR | F4 COIN1 | F5 COIN2
     * Fora do SETUP MENU o SERVICE dá crédito de cortesia (type 3, o mesmo que
     * incrementa g_nServiceTotal no original); dentro dele vira SELECT e o
     * I/O TEST lê as teclas por conta própria. */
    if (g_game.state != STATE_SERVICE_MENU) {
        if (Input_IsKeyHit(VK_F4)) Arcade_ProcessCoin(1);  /* COIN1   */
        if (Input_IsKeyHit(VK_F5)) Arcade_ProcessCoin(2);  /* COIN2   */
        if (Input_IsKeyHit(VK_F2)) Arcade_ProcessCoin(3);  /* SERVICE */
        if (Input_IsKeyHit(VK_F3)) Coin_Clear();           /* COIN CLEAR (pedido do usuário) */
    }

    if (Input_IsKeyHit(VK_F1) && g_game.state != STATE_SERVICE_MENU) {
        ServiceMenu_Enter();
        /* Consome a borda do F1 antes de sair: este return pula o
         * memcpy(prevKeys, keys) do fim de Game_Update, e sem isso o mesmo
         * press seria lido de novo no frame seguinte, já como MOVE. */
        memcpy(g_game.input.prevKeys, g_game.input.keys, sizeof(g_game.input.keys));
        return;
    }

    if (Input_IsKeyHit(VK_ESCAPE)) {
        GameState s = g_game.state;
        /* Consome a borda do ESC (mesmo motivo do F1 acima): os returns abaixo
         * pulam o memcpy do fim de Game_Update e o ESC seria lido de novo no
         * frame seguinte, pulando duas cenas (WARN -> LOGO -> MENU). */
        memcpy(g_game.input.prevKeys, g_game.input.keys, sizeof(g_game.input.keys));

        /* Pedido do usuário (como no original): fora de um crédito, o ESC passa para a
         * próxima tela da atração (Logo -> NEXCADE -> abertura -> título -> demo ->
         * ranking -> WPF2007 -> Logo); dentro de um crédito (station, seleção, missão,
         * training, gameplay, grade, próxima fase...), volta para o Logo zerando o
         * crédito, como no fim de jogo.
         * era: ESC no CREDIT não fazia nada e qualquer outra tela ia para o CREDIT
         * (bloco abaixo, mantido para as telas que não são do NX). */
        if (s == STATE_SERVICE_MENU) {   /* ESC no SETUP MENU sai como a opção EXIT */
            ServiceMenu_Exit();
            return;
        }
        {
            bool attract = g_demoPlay ||
                           s == STATE_LOGO_ENTER || s == STATE_LOGO_UPDATE || s == STATE_INTRO ||
                           s == STATE_CREDIT || s == STATE_HIGHSCORE ||
                           s == STATE_WARNING_INIT || s == STATE_WARNING_ANIM || s == STATE_WARNING_END;
            BGM_Stop();
            Movie_Select(1); Movie_Close(); Movie_Select(0); Movie_Close();
            memset(g_game.input.padPrevState, 0, sizeof(g_game.input.padPrevState));
            if (attract) {
                if (g_demoPlay) Demo_End();   /* fecha a demo e segue o laço */
                else Attract_Idle();
                return;
            }
            if (s == STATE_GAMEPLAY) Gameplay_Exit();
            Game_ResetAllCheats();            /* comandos */
            Menu_ResetState();                /* estágio, corações, skin, modo */
            Resource_ClearBGA();
            Attract_Restart();                /* Logo */
            return;
        }

        /* Exceed: ESC no CREDIT não faz nada (já é o destino do ESC) */
        if (s == STATE_CREDIT) return;

        BGM_Stop();
        Menu_ResetState();
        memset(g_game.input.padPrevState, 0, sizeof(g_game.input.padPrevState));

        /* Exceed: WARN -> LOGO -> CREDIT; o resto vai para o CREDIT */
        if (s == STATE_WARNING_INIT || s == STATE_WARNING_ANIM || s == STATE_WARNING_END) {
            Game_ChangeState(STATE_LOGO_ENTER);
            return;
        }

        if (s == STATE_STAFF || s == STATE_STAFF_ENTER) {
            Game_ChangeState(STATE_CREDIT);
            return;
        }

        /* ESC no SETUP MENU sai como a opção EXIT (página 9 → estado 4) */
        if (s == STATE_SERVICE_MENU) {
            ServiceMenu_Exit();
            return;
        }

        /* ESC de qualquer tela de jogo reseta os ponteiros de música por modo */
        /* SongSelect_ResetCreditIndices(); */   /* DESATIVADO (Select do Prex3) */

        if (s == STATE_GAMEPLAY || s == STATE_GAME_INIT) {
            Game_ResetAllCheats(); /* ESC durante gameplay: zera todos os cheats */
            Resource_ClearBGA();
            Game_ChangeState(STATE_CREDIT);
            return;
        }

        Resource_ClearBGA();
        Game_ChangeState(STATE_CREDIT);
    }

    g_game.stateFrame++;


    if (g_game.bgaPicCount > 0 && g_game.state != STATE_WARNING_END) {
        bool manualBGA = (g_game.state == STATE_GAMEPLAY ||
                          g_game.state == STATE_DANCE_GRADE_DISPLAY ||
                          g_game.state == STATE_GAMEOPTION_ENTER ||
                          g_game.state == STATE_GAMEOPTION_ANIM ||
                          g_game.state == STATE_GAMEOPTION ||
                          g_game.state == STATE_GAMEOPTION_EXIT ||
                          g_game.state == STATE_STAFF);
        if (!manualBGA) {
            g_game.bgaTimer += dt;
            if (g_game.bgaTimer >= 1.0f / 60.0f) {
                g_game.bgaTimer -= 1.0f / 60.0f;
                if (g_game.bgaLoop && g_game.bgaLoopEnd > g_game.bgaLoopStart) {
                    g_game.bgaFrame++;
                    if (g_game.bgaFrame > g_game.bgaLoopEnd) {
                        Log_Print("BGA: wrap %d -> %d\n", g_game.bgaFrame, g_game.bgaLoopStart);
                        g_game.bgaFrame = g_game.bgaLoopStart;
                    }
                } else {
                    if (g_game.bgaFrame < g_game.bgaMaxFrame) {
                        g_game.bgaFrame++;
                    }
                }
            }
        }
    } else if (g_game.isVSL && g_vsl.active && g_game.bgaFrame < g_vsl.frameCount - 1) {
        g_game.bgaTimer += dt;
        if (g_game.bgaTimer >= 1.0f / 60.0f) {
            g_game.bgaTimer -= 1.0f / 60.0f;
            g_game.bgaFrame++;
            static int lastFrameAdvLog = -1;
            if (abs(g_game.bgaFrame - lastFrameAdvLog) >= 30) {
                Log_Print("VSL: bgaFrame advanced to %d (timer=%.4f, dt=%.4f)\n", g_game.bgaFrame, g_game.bgaTimer, dt);
                lastFrameAdvLog = g_game.bgaFrame;
            }
        }
    }

    switch (g_game.state) {
    case STATE_WARNING_INIT:
    case STATE_WARNING_ANIM:
    case STATE_WARNING_END:
        Gamestate_UpdateWarning(dt);
        break;
    case STATE_LOGO_ENTER:
    case STATE_LOGO_UPDATE:
    case STATE_LOGO_SKIP:
        Gamestate_UpdateLogo(dt);
        break;
    case STATE_INTRO:
    case STATE_CREDIT:
        Gamestate_UpdateIntro(dt);
        break;
    case STATE_HIGHSCORE:
        Highscore_Update(dt);
        break;
    case STATE_IR:
        IR_Update(dt);
        break;
    case STATE_NAMEINPUT:
        NameInput_Update(dt);
        break;
    case STATE_SELECT:
        /* ClassicSelect_Update(dt); */
        NxSelect_Update(dt);
        /* Select_Update(dt); */
        break;
    case STATE_STATION:
        Station_Update(dt);
        break;
    case STATE_MENU_ENTER:
    case STATE_MENU_INPUT:
        /* Gamestate_UpdateMenu(dt); */   /* DESATIVADO (menu do Prex3) */
        break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B:
        /* Gamestate_UpdateSongSelect(dt); */   /* DESATIVADO (Select do Prex3) */
        break;
    case STATE_SONG_TITLE:
    case STATE_SONG_TITLE_OUT:
        Loading_Update(dt);
        break;
    case STATE_NX_MISSION:
        NxMission_Update(dt);
        break;
    case STATE_NX_SPORTS:
        NxSports_Update(dt);
        break;
    case STATE_NX_CLEAR:
        if (g_game.stateFrame == 1) NxClear_Enter();
        NxClear_Update(dt);
        break;
    case STATE_GAMEPLAY:
        /* era: if (Movie_IsOpen()) Movie_Update(dt); */
        if (Movie_IsOpen() && !Gameplay_IsFrozen()) Movie_Update(dt);   /* BGA\%s.MOV da música; parado no stage break */
        Gameplay_Update(dt);
        break;
    case STATE_DANCE_GRADE_ENTER:
        if (g_demoPlay) { Demo_End(); break; }    /* a demo não tem Grade */
        if (g_game.stateFrame == 1) {
            Movie_Close();
            Result_Enter();
        }
        Result_Update(dt);
        break;
    case STATE_DANCE_GRADE_DISPLAY:
        Result_Update(dt);
        break;
    case STATE_STAGE_TRANSITION:
        if (g_game.stateFrame == 1) {
            Log_Print("STAGE: transition count=%d bonus=%d\n", g_game.stageCount, g_game.bonusStage);
        }
        {
            /* Exceed "NEXTSTAGE n" (0x415078): n = [0x568FF8]+1 (0x40D131) —
             * 1 depois do stage 1, 2 depois do stage 2, 3 para o extra.
             * Abre BGA\NST<n>.MOV sem loop e toca NEXTSTAGE.WAV do início.
             * 0x4151F4: com decoded >= 57 (0x39) vai para SELECT. */
            bool stageDone = false;
            if (g_hexSongIds) {
                if (g_game.stateFrame == 1) {
                    int n = (g_game.stageCount > 0) ? 3 - g_game.stageCount : 3;
                    if (n < 1) n = 1;
                    char path[MAX_PATH];
                    snprintf(path, sizeof(path), "%s/BGA/NST%d.MOV", g_game.currentDirectory, n);
                    Movie_Close();
                    if (!Movie_Open(path, false))
                        Log_Print("NST: '%s' não abriu\n", path);
                    /* era (Exceed): NEXTSTAGE.WAV
                    if (g_waveSoundIds[SND_NEXTSTAGE] >= 0) {
                        Audio_Stop(g_waveSoundIds[SND_NEXTSTAGE]);
                        Audio_Play(g_waveSoundIds[SND_NEXTSTAGE], false);
                    }
                    */
                    /* NX: AUDIO/NST<n>.AUD, mesmo n do NST<n>.MOV */
                    snprintf(path, sizeof(path), "%s/AUDIO/NST%d.AUD", g_game.currentDirectory, n);
                    BGM_Stop();
                    if (BGM_LoadAUDDirect(path)) BGM_Play(false);
                    else Log_Print("NST: '%s' não abriu\n", path);
                }
                Movie_Update(dt);
                /* NX PLAYMOVIE (0x80784d0): sai quando o vídeo E o áudio terminam
                 * ([+0xa] / [+9]). Era (Exceed): decoded >= 57. */
                stageDone = (!Movie_IsOpen() || Movie_HasEnded()) && !BGM_IsPlaying();
                if (stageDone) Movie_Close();
            }
        if (g_hexSongIds ? stageDone : (g_game.stateFrame >= 60)) {
            if (g_hexSongIds && g_game.nxGameMode >= 1 && g_game.nxGameMode <= 3) {
                /* NX SPECIAL ZONE: os corações já decidiram em Result_GetNextState */
                Game_ChangeState(STATE_SELECT);
            } else if (g_game.bonusStage && g_game.stageCount == 0 && !g_game.isBonusSong) {
                // Vai pro bonus stage
                /* Exceed: volta para a CSelect */
                Game_ChangeState(g_hexSongIds ? STATE_SELECT : STATE_SONG_SELECT);
            } else if (g_game.isBonusSong) {
                // Bonus terminou, game over
                Game_ChangeState(STATE_GAMEOVER_ENTER);
            } else if (g_game.stageCount == 0 && !g_game.bonusStage) {
                // Sem bonus, game over
                Game_ChangeState(STATE_GAMEOVER_ENTER);
            } else {
                // Proximo stage
                Game_ChangeState(g_hexSongIds ? STATE_SELECT : STATE_SONG_SELECT);
            }
        }
        }
        break;
    case STATE_STAGE_BREAK:
        if (g_demoPlay) { Demo_End(); break; }
        if (g_hexSongIds && g_game.nxGameMode == 2) {   /* NX WORLD TOUR: CContinue (nx_mission.c) */
            if (g_game.stateFrame == 1) NxContinue_Enter();
            NxContinue_Update(dt);
            break;
        }
        if (g_hexSongIds) {
            /* exceed.exe 0x4152B0 (Begin): 0x4229C8("BGA\SB.MOV", 0) sem loop;
             * se falhar vai para IDLE (aqui: GameOver). Toca GAMESTOP.WAV
             * do início ([0xA6D2F0]: Stop, SetCurrentPosition(0), Play).
             * 0x41532C (Update): avança o vídeo e, com o cronômetro
             * >= 4500 ms (0x1194), vai para GAMEOVER. */
            static uint32_t sbStart;
            if (g_game.stateFrame == 1) {
                Game_ResetAllCheats();
                Movie_Close();
                char path[MAX_PATH];
                /* Zero CStageBreak (piu 0x8077757): BGA/STAGEBREAK.MOV */
                snprintf(path, sizeof(path), "%s/BGA/STAGEBREAK.MOV", g_game.currentDirectory);
                /* era (Exceed): "%s/BGA/SB.MOV" */
                if (!Movie_Open(path, false)) {
                    Log_Print("SB: '%s' não abriu\n", path);
                    /* Game_ChangeState(STATE_GAMEOVER_ENTER); */
                    Attract_Restart();  /* 0x4152E0: "IDLE" (era: Attract_Idle; fim de crédito recomeça o laço) */
                    break;
                }
                /* NX "PLAYMOVIE STAGEBREAK" (0x80fceea): o PLAYMOVIE toca AUDIO/%s.AUD
                 * (0x811017a) — o WAVE/STAGEBREAK.WAV do Zero não existe no NX */
                {
                    char ap[MAX_PATH];
                    snprintf(ap, sizeof(ap), "%s/AUDIO/STAGEBREAK.AUD", g_game.currentDirectory);
                    BGM_Stop();
                    if (BGM_LoadAUDDirect(ap)) BGM_Play(false);
                    else Log_Print("SB: '%s' não abriu\n", ap);
                }
                /* era (Zero 0x80776b0): WAVE/STAGEBREAK.WAV (o GAMESTOP.WAV do Exceed não existe)
                {
                    static int s_sndSB = -1;
                    if (s_sndSB < 0) s_sndSB = Audio_LoadWaveFile("STAGEBREAK.WAV");
                    if (s_sndSB >= 0) { Audio_Stop(s_sndSB); Audio_Play(s_sndSB, false); }
                }
                */
                /* era (Exceed): g_waveSoundIds[SND_GAMESTOP] */
                sbStart = timeGetTime();
            }
            Movie_Update(dt);
            /* era: if (timeGetTime() - sbStart >= 4500) {
             * NX PLAYMOVIE (0x80784d0): sai com o vídeo E o áudio no fim */
            if ((!Movie_IsOpen() || Movie_HasEnded()) && !BGM_IsPlaying()) {
                (void)sbStart;
                Movie_Close();
                /* era: Game_ChangeState(STATE_GAMEOVER_ENTER); */
                /* NX passo 19 (0x806005a): SPORTS se [0x81f8910] > 0 */
                int sbStage = g_game.isBonusSong ? 3 : 2 - g_game.stageCount;
                Game_ChangeState(NxSports_Route(STATE_GAMEOVER_ENTER, true, sbStage));
            }
            break;
        }
        /* 083.DAT aparece enquanto 7-1.WAV toca; quando termina → GameOver */
        if (g_game.stateFrame == 1) {
            Game_ResetAllCheats();
            Audio_Play(g_waveSoundIds[SND_7_1], false);
        }
        if (g_game.stateFrame > 2 && !Audio_IsPlaying(g_waveSoundIds[SND_7_1])) {
            Game_ChangeState(STATE_GAMEOVER_ENTER);
        }
        break;
    case STATE_GAMEOVER_ENTER:
        if (g_hexSongIds) {
            /* exceed.exe 0x415224: BGA\GAMEOVER.MOV sem loop (falha -> IDLE);
             * 0x41527C: cronômetro >= 4000 ms (0xFA0) -> IDLE. Sem som. */
            static uint32_t goStart;
            if (g_game.stateFrame == 1) {
                BGM_Stop();
                Game_ResetAllCheats();
                Render_SetGlobalColor(0, 0, 0, 0);
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s/BGA/GAMEOVER.MOV", g_game.currentDirectory);
                Movie_Close();
                if (!Movie_Open(path, false))
                    Log_Print("GAMEOVER: '%s' não abriu\n", path);  /* 0x41525D: -> IDLE (abaixo) */
                /* NX "PLAYMOVIE GAMEOVER 4000" (0x80fceff): o PLAYMOVIE também toca
                 * AUDIO/%s.AUD (0x811017a). Era: sem som (Exceed). */
                snprintf(path, sizeof(path), "%s/AUDIO/GAMEOVER.AUD", g_game.currentDirectory);
                if (BGM_LoadAUDDirect(path)) BGM_Play(false);
                else Log_Print("GAMEOVER: '%s' não abriu\n", path);
                goStart = timeGetTime();
            }
            Movie_Update(dt);
            if (!Movie_IsOpen() || timeGetTime() - goStart >= 4000) {
                Movie_Close();
                BGM_Stop();
                Resource_ClearBGA();
                Menu_ResetState();
                /* Game_ChangeState(STATE_MENU_ENTER); */
                Attract_Restart();  /* 0x41527C: "IDLE" (era: Attract_Idle; fim de crédito recomeça o laço) */
            }
            break;
        }
        if (g_game.stateFrame == 1) {
            BGM_Stop();
            Game_ResetAllCheats(); /* Game Over: zera todos os cheats (centralizado) */
            Render_SetGlobalColor(0, 0, 0, 0);
        }
        if (g_game.stateFrame >= 120) {
            Render_SetGlobalColor(0, 0, 0, 1);
        }
        if (g_game.stateFrame >= 150) {
            Resource_ClearBGA();
            Menu_ResetState();
            Game_ChangeState(STATE_MENU_ENTER);
        }
        break;
    case STATE_STAFF_ENTER:
        /* Staff_Enter(); */   /* DESATIVADO (Staff do Prex3) */
        break;
    case STATE_STAFF:
        /* Staff_Update(dt); */   /* DESATIVADO (Staff do Prex3) */
        break;
    case STATE_STAFF_END:
        BGM_Stop();
        Menu_ResetState();
        Game_ChangeState(STATE_MENU_ENTER);
        break;
    case STATE_GAMEOPTION_ENTER:
    case STATE_GAMEOPTION_ANIM:
    case STATE_GAMEOPTION:
    case STATE_GAMEOPTION_EXIT:
        /* Gamestate_UpdateGameOption(dt); */   /* DESATIVADO (Game Option do Prex3) */
        break;
    case STATE_SERVICE_MENU:
        /* Só captura o input aqui; o desenho e a aplicação ficam em
         * ServiceMenu_UpdateRender, no Game_Render. A captura precisa ser
         * nesta fase porque Game_Update termina zerando as bordas do teclado
         * com memcpy(prevKeys, keys). */
        ServiceMenu_Update();
        break;
    case STATE_HOWTOPLAY:
        /* Frame 1: carrega 03.DAT e inicia 003.AUD (igual ao padrão GAMEOPTION_ENTER) */
        if (g_game.stateFrame == 1) {
            Resource_SwitchBGA("03");
            g_game.bgaFrame = 0;
            g_game.bgaLoop  = false;
            {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s/AUDIO/003.AUD", g_game.currentDirectory);
                BGM_Stop();
                if (BGM_LoadAUDDirect(path)) BGM_Play(true); /* loop=true: igual demais AUDs do jogo */
            }
        }
        /* Sai para SongSelect: CN do player ativo OU BGA terminou */
        if (g_game.stateFrame > 30) {
            bool skipCN = false;
            if (g_game.activePlayerMask & 0x1) skipCN |= Input_IsPadHit(0, PAD_C);
            if (g_game.activePlayerMask & 0x2) skipCN |= Input_IsPadHit(1, PAD_C);
            bool bgaEnded = (g_game.bgaMaxFrame > 0 && g_game.bgaFrame >= g_game.bgaMaxFrame);
            if (skipCN || bgaEnded) {
                BGM_Stop();
                Game_ChangeState(STATE_SONG_SELECT);
            }
        }
        break;
    case STATE_RESET_WARNING:
    Game_ChangeState(STATE_LOGO_ENTER);
        break;
    case STATE_EXIT:
        if (g_game.stateFrame < 30) {
            float a = g_game.globalColorA + dt * 2.0f;
            if (a > 1.0f) a = 1.0f;
            Render_SetGlobalColor(0, 0, 0, a);
        }
        if (g_game.stateFrame >= 30)
            Window_RequestQuit();
        break;
    default:
        break;
    }
    memcpy(g_game.input.prevKeys, g_game.input.keys, sizeof(g_game.input.keys));
}

static void Render_StateInfo(void) {
    static uint32_t lastFpsTime = 0;
    static int fpsCount = 0;
    static float currentFps = 0;
    char buf[256];
    int y = -4;

    fpsCount++;
    uint32_t now = timeGetTime();
    if (now - lastFpsTime >= 1000) {
        currentFps = fpsCount / ((now - lastFpsTime) / 1000.0f);
        fpsCount = 0;
        lastFpsTime = now;
    }

    // FPS no canto inferior direito
    snprintf(buf, sizeof(buf), "FPS: %.1f", currentFps);
    int fw = (int)strlen(buf) * 8;
    Font_DrawString(g_game.screenWidth - fw - 8, 20,
                    buf, 1.0f, 1.0f, 0.0f, 1.0f);

    snprintf(buf, sizeof(buf), "State: %s  Frame: %u/%u",
             State_ToString(g_game.state), g_game.stateFrame, g_game.frameCounter);
    Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
    y += 16;

    if (g_game.isVSL && g_vsl.active) {
        int songId = 0;
        if (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
            g_game.selectedSongIndex >= 0) {
            SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
            if (g_game.selectedSongIndex < mode->songCount)
                songId = mode->songIds[g_game.selectedSongIndex];
        }
        snprintf(buf, sizeof(buf), "BGA: %d.DAT - VSL | frame=%d/%d | meshs=%d",
                 songId, g_game.bgaFrame, g_vsl.frameCount, g_vsl.meshTableCount);
        Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
        y += 16;
    } else if (g_game.bgaPicCount > 0) {
        BGAPicture* pic = &g_game.bgaPics[0];
        snprintf(buf, sizeof(buf), "BGA: %s | layers=%d | frame=%d/%d | tiles=%d",
                 pic->name, pic->layerCount, g_game.bgaFrame, g_game.bgaMaxFrame, g_game.sprTileCount);
        Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
        y += 16;
    } else {
        snprintf(buf, sizeof(buf), "BGA: (none loaded)");
        Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
        y += 16;
    }
    
    if (g_game.bgm.buffer || g_game.bgm.useMCI) {
        snprintf(buf, sizeof(buf), "BGM: %s | mode=%s | playing=%d",
                 g_game.bgm.name[0] ? g_game.bgm.name : "(unnamed)",
                 g_game.bgm.useMCI ? "MCI" : "DSound",
                 g_game.bgm.playing);
        Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
        y += 16;
    }
    
    if (g_game.state == STATE_SONG_SELECT || g_game.state == STATE_SONG_SELECT_B) {
        if (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount) {
            SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
            snprintf(buf, sizeof(buf), "Mode: %s | Song %d/%d | Preview ID=%d",
                     mode->name, g_game.songSelectHighlighted + 1, mode->songCount,
                     g_game.previewSongId);
            Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
            y += 16;
        }
    }
}

void Game_Render(void) {
    Gameplay_RefreshClock();
    /* Rendering subsystems such as VSL may leave either matrix selected.
     * Re-establish the fixed 640x480 2D transform for every frame. */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 640, 0, 480, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    /* Keep a missing-assets run visibly diagnosable instead of presenting a
     * featureless black window. Normal game rendering is unchanged once the
     * BGA/song assets have loaded. */
    bool missingAssets = (g_game.songDB.songCount == 0);
    if (missingAssets) {
        glDisable(GL_TEXTURE_2D);
        glColor4f(0.035f, 0.055f, 0.09f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0);
        glVertex2f(640, 0);
        glVertex2f(640, 480);
        glVertex2f(0, 480);
        glEnd();
        glColor4f(1, 1, 1, 1);
        Font_DrawStringCentered(320, 190, "PUMP IT UP", 1.0f, 0.85f, 0.1f, 1.0f);
        Font_DrawStringCentered(320, 225, "ASSETS NOT FOUND", 1.0f, 1.0f, 1.0f, 1.0f);
        Font_DrawStringCentered(320, 250, "Stage.cfg / BGA / AUDIO / WAVE", 0.7f, 0.8f, 0.9f, 1.0f);
        Font_DrawStringCentered(320, 285, g_game.currentDirectory, 0.6f, 0.7f, 0.8f, 1.0f);
    }

    // No background fill for states that have full-screen BGA
    if (!missingAssets && g_game.state != STATE_WARNING_INIT && g_game.state != STATE_WARNING_ANIM && 
        g_game.state != STATE_WARNING_END &&
        g_game.state != STATE_LOGO_ENTER && g_game.state != STATE_LOGO_UPDATE &&
        g_game.state != STATE_LOGO_SKIP) {
        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0);
        glVertex2f(640, 0);
        glVertex2f(640, 480);
        glVertex2f(0, 480);
        glEnd();
        glColor4f(1, 1, 1, 1);
    }

    if ((g_game.cmdTestBGA[0] || g_game.cmdTestBGA[1]) &&
               (g_game.state == STATE_GAMEPLAY || g_game.state == STATE_GAMEPLAY_BEGIN)) {
        DrawStar(); /* Extra do port: BGA Off (TestBGA) substitui o BGA/VSL da música */
    } else if (g_game.isVSL && g_vsl.active) {
        VSL_Render(g_game.bgaFrame);
    } else if (g_game.bgaPicCount > 0 &&
        g_game.state != STATE_LOGO_SKIP &&
        g_game.state != STATE_DANCE_GRADE_DISPLAY &&
        g_game.state != STATE_GAMEOPTION_ENTER &&
        g_game.state != STATE_GAMEOPTION_ANIM &&
        g_game.state != STATE_GAMEOPTION &&
        g_game.state != STATE_GAMEOPTION_EXIT &&
        g_game.state != STATE_SONG_SELECT &&
        g_game.state != STATE_SONG_SELECT_B &&
        g_game.state != STATE_CREDIT &&         /* CREDIT desenha por slot (intro.c) */
        g_game.state != STATE_SELECT &&
        g_game.state != STATE_STATION &&        /* STATION desenha por cena (station.c) */
        g_game.state != STATE_NX_MISSION &&     /* objetivo da missão desenha por cena (nx_mission.c) */
        g_game.state != STATE_NX_CLEAR &&
        g_game.state != STATE_NX_SPORTS &&       /* SPORTS desenha por cena (nx_sports.c) */
        g_game.state != STATE_HIGHSCORE &&
        g_game.state != STATE_IR &&
        g_game.state != STATE_NAMEINPUT) {      /* NAMEINPUT desenha por slot (nameinput.c) */             /* IR desenha o próprio BGA (ir.c) */      /* HIGHSCORE desenha por slot (highscore.c) */       /* EXSELECT desenha por slot (select_core.c) */
        BGA_Render(0, g_game.bgaFrame);
    }

    switch (g_game.state) {
        case STATE_INTRO:
        case STATE_CREDIT:
            Gamestate_RenderIntro();
            break;
        case STATE_HIGHSCORE:
            Highscore_Render();
            break;
        case STATE_IR:
            IR_Render();
            break;
        case STATE_NAMEINPUT:
            NameInput_Render();
            break;
        case STATE_SELECT:
            /* ClassicSelect_Render(); */
            NxSelect_Render();
            /* Select_Render(); */
            break;
        case STATE_STATION:
            Station_Render();
            break;
        case STATE_MENU_ENTER:
        case STATE_MENU_INPUT:
        case STATE_EXIT:
            /* Gamestate_RenderMenu(0, g_game.bgaFrame); */   /* DESATIVADO (menu do Prex3) */
            break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B:
        /* Gamestate_RenderSongSelect(); */   /* DESATIVADO (Select do Prex3) */
        break;
    case STATE_SONG_TITLE:
    case STATE_SONG_TITLE_OUT:
        Loading_Render();
        break;
    case STATE_NX_MISSION:
        NxMission_Render();
        break;
    case STATE_NX_CLEAR:
        NxClear_Render();
        break;
    case STATE_NX_SPORTS:
        NxSports_Render();
        break;
    case STATE_GAMEPLAY:
        if (Movie_IsOpen() && !g_game.cmdTestBGA[0] && !g_game.cmdTestBGA[1])
            Movie_Render();     /* fundo no lugar do .DAT; TestBGA (extra do port) substitui */
        Gameplay_Render();
        break;
    case STATE_STAGE_BREAK:
        if (g_hexSongIds && g_game.nxGameMode == 2) { NxContinue_Render(); break; }
        if (g_hexSongIds && Movie_IsOpen()) Movie_Render();   /* SB.MOV */
        break;
    case STATE_DANCE_GRADE_DISPLAY:
        Result_Render();
        break;
    case STATE_STAGE_TRANSITION:
        if (g_hexSongIds) {
            if (Movie_IsOpen()) Movie_Render();   /* NST<n>.MOV */
        } else if (g_game.isBonusSong) {
            Font_DrawStringCentered(320, 240, "BONUS STAGE", 1, 1, 0, 1);
        } else if (g_game.stageCount > 0) {
            Font_DrawStringCentered(320, 240, "NEXT STAGE", 1, 1, 1, 1);
        } else {
            Font_DrawStringCentered(320, 240, "FINAL STAGE", 1, 0, 0, 1);
        }
        break;
    case STATE_GAMEOVER_ENTER:
        if (g_hexSongIds) {
            if (Movie_IsOpen()) Movie_Render();   /* GAMEOVER.MOV */
        } else
        Font_DrawStringCentered(320, 240, "GAME OVER", 1, 0, 0, 1);
        break;
    case STATE_GAMEOPTION_ENTER:
    case STATE_GAMEOPTION_ANIM:
    case STATE_GAMEOPTION:
    case STATE_GAMEOPTION_EXIT:
        /* Gamestate_RenderGameOption(); */   /* DESATIVADO (Game Option do Prex3) */
        break;
    case STATE_SERVICE_MENU:
        ServiceMenu_UpdateRender();
        break;
    default:
        break;
    }

    /* Console por último: é overlay, desenha sobre tudo */
    Debug_ConsoleRender();

    if (g_game.showDebug) Render_StateInfo();
    else if (g_game.gfxShowFps) {
        /* SHOW FPS (GRAPHICS SETTINGS): só o contador, no canto inferior direito. */
        static uint32_t t0 = 0; static int n = 0; static float fps = 0;
        char fb[32];
        n++;
        uint32_t now = timeGetTime();
        if (now - t0 >= 1000) { fps = n / ((now - t0) / 1000.0f); n = 0; t0 = now; }
        snprintf(fb, sizeof(fb), "FPS: %.1f", fps);
        Font_Init();   /* Resource_ClearBGA -> Font_Shutdown desliga a fonte */
        Font_DrawString(g_game.screenWidth - (int)strlen(fb) * 8 - 8, 20, fb, 1.0f, 1.0f, 0.0f, 1.0f);
    }
    static int renderLogCount = 0;
    if (g_game.isVSL && renderLogCount < 30) {
        Log_Print("RENDER: state=%d isVSL=%d active=%d globalA=%.2f frame=%d fc=%d\n", g_game.state, g_game.isVSL, g_vsl.active, g_game.globalColorA, g_game.bgaFrame, g_vsl.frameCount);
        renderLogCount++;
    }
    if (renderLogCount < 5) {
        Log_Print("RENDER: state=%d isVSL=%d globalA=%.2f frame=%d\n", g_game.state, g_game.isVSL, g_game.globalColorA, g_game.bgaFrame);
        renderLogCount++;
    }
    NxCredit_Draw();   /* NX2 main.cpp:604: g_CreditManager.Draw() por cima de toda tela, menos o SETUP */
    Render_EndScene();
}

/* Loop de passo fixo a 60 Hz com acumulador, renderizando com vsync.
 *
 * O loop antigo comparava ticks de 240 Hz e fazia `lastTick = tick` a cada
 * frame, jogando fora o resto acumulado. Como a condição era >= 4 ticks
 * (16,67 ms) mas o relógio só tem resolução de 1 ms, o intervalo real entre
 * frames alternava entre ~16,7 e ~20,8 ms: a rolagem das setas ficava com um
 * tremor periódico mesmo com o jogo "a 60 fps". Além disso o loop girava em
 * busy-wait, sem dormir, o que esquenta um núcleo e piora o jitter do
 * escalonador.
 *
 * Agora o resto fica no acumulador (nada é descartado), o passo de lógica é
 * sempre exatamente 1/60 e o SwapBuffers com vsync é quem alinha o ritmo ao
 * refresh do monitor. Sem vsync disponível, o Sleep(1) segura o loop em ~60 Hz
 * sem queimar CPU. */
void Game_MainLoop(void) {
    bool running = true;
    const double STEP_MS = 1000.0 / 60.0;
    const int    MAX_CATCHUP = 5;   /* teto de passos por iteração */

    double accumulator = 0.0;
    /* era: uint32_t prevTime = timeGetTime();  (1 ms de resolução: o número de
     * passos por refresh oscilava 0/1/2 e as setas tremiam, com ou sem vsync) */
    double qpcHz = (double)SDL_GetPerformanceFrequency();
    uint64_t prevQpc = SDL_GetPerformanceCounter();
    uint64_t lastRenderQpc = prevQpc;
    uint64_t startQpc = prevQpc;      /* origem dos tempos em ms da agenda de desenho */
    uint64_t lastSwapQpc = prevQpc;
    double nextRenderMs = 0.0;        /* próximo desenho do gameplay sem vsync */
    bool   pendingTick = false;       /* houve passo de lógica desde o último desenho */
    int    fastFrames = 0;            /* quadros seguidos rápidos demais com "vsync" */
    (void)lastRenderQpc;
    double presentSec = 0.0;      /* instante (s) do último swap, filtrado */
    bool   presentValid = false;

    while (running) {
        if (!Window_ProcessMessages()) {
            running = false;
            break;
        }

        /* era: uint32_t now = timeGetTime(); double elapsed = (double)(now - prevTime); */
        uint64_t nowQpc = SDL_GetPerformanceCounter();
        double elapsed = (double)(nowQpc - prevQpc) * 1000.0 / qpcHz;
        prevQpc = nowQpc;

        /* Uma pausa longa (janela minimizada, breakpoint, troca de faixa que
         * travou) não pode virar uma avalanche de catch-up. */
        if (elapsed > 250.0) elapsed = 250.0;
        accumulator += elapsed;

        /* Extra do port: detector de travadas. Mede update/render deste giro e,
         * se passar de 50 ms, grava no log onde o tempo foi gasto. */
        extern double g_movieMs, g_logMs;
        double hzF = (double)SDL_GetPerformanceFrequency();
        uint64_t hz0 = SDL_GetPerformanceCounter();
        g_movieMs = 0.0; g_logMs = 0.0;

        int steps = 0;
        while (accumulator >= STEP_MS && steps < MAX_CATCHUP) {
            accumulator -= STEP_MS;
            g_game.frameCounter++;
            Game_Update(1.0f / 60.0f);
            steps++;
        }
        if (steps == MAX_CATCHUP)
            accumulator = 0.0;   /* desistiu de alcançar: não acumula dívida */

        /* era: só desenhava com steps > 0. O timeGetTime (1 ms) e o refresh do
         * monitor não andam juntos, então às vezes um refresh ficava sem quadro
         * novo e a imagem repetia (engasgo das setas). Com vsync agora desenha
         * em TODO refresh; o swap bloqueia e dá o ritmo. As setas pegam o
         * relógio da música no momento do desenho (Gameplay_RefreshClock). */
        /* Desenho extra entre passos (refresh > 60 Hz) só no gameplay, para as
         * setas; nesses desenhos g_renderTick = false e as cenas do BGA não
         * avançam, então animações, judge e spark ficam em 60 Hz. Fora do
         * gameplay volta ao ritmo de 60 Hz (várias telas avançam no desenho). */
        /* era: bool extra = g_game.vsync && g_game.state == STATE_GAMEPLAY;
         * Sem vsync o gameplay só desenhava a 60 Hz fora de fase com o monitor
         * (tremor em 75/120/144 Hz). Agora desenha também sem vsync, limitado à
         * taxa do monitor; com vsync o próprio swap limita. */
        /* era:
         *     bool extra = false;
         *     if (g_game.state == STATE_GAMEPLAY) {
         *         if (g_game.vsync) extra = true;
         *         else extra = (nowQpc - lastRenderQpc) em ms >= período do monitor;
         *     }
         *     if (steps > 0 || extra) { lastRenderQpc = nowQpc; g_renderTick = (steps > 0); ...
         * Dois defeitos sem vsync no gameplay:
         *  1. o teste era "já passou um período desde o ÚLTIMO desenho" com Sleep(1)
         *     entre os giros: o intervalo saía sempre um pouco maior que o período
         *     (~17 ms em 60 Hz) e o erro não era compensado -> o jogo ficava abaixo
         *     da taxa do monitor e repetia um quadro de tempos em tempos;
         *  2. além desses desenhos, todo passo de lógica também desenhava -> dois
         *     desenhos por período, fora de fase.
         * Agora o gameplay sem vsync desenha numa agenda fixa (próximo += período,
         * sem acumular atraso) e só nela; e "vsync" vale o que foi aplicado de fato
         * (Window_VsyncActive), não o que o INI pede. */
        bool vsOn = Window_VsyncActive();
        bool inGp = (g_game.state == STATE_GAMEPLAY);
        double monHz = (double)Window_GetRefreshRate();
        double periodMs = 1000.0 / (monHz > 0.0 ? monHz : 60.0);
        double nowMs = (double)(nowQpc - startQpc) * 1000.0 / qpcHz;
        if (steps > 0) pendingTick = true;
        bool doRender;
        if (!inGp) {
            doRender = (steps > 0);
            nextRenderMs = nowMs;            /* a agenda recomeça ao entrar no gameplay */
        } else if (vsOn) {
            doRender = true;                 /* o swap bloqueia e dá o ritmo */
        } else {
            doRender = (nowMs >= nextRenderMs);
            if (doRender) {
                nextRenderMs += periodMs;
                if (nextRenderMs < nowMs) nextRenderMs = nowMs + periodMs;   /* atrasou mais de um período */
            }
        }
        if (doRender) {
            lastRenderQpc = nowQpc;
            g_renderTick = pendingTick;      /* era: (steps > 0) */
            pendingTick = false;
            uint64_t hz1 = SDL_GetPerformanceCounter();
            double movUpd = g_movieMs, logUpd = g_logMs;
            /* era: Game_Render() direto; o relógio das setas era lido no começo
             * do desenho, depois do Game_Update, e o custo do update virava
             * tremor. Com vsync usa o instante do último swap (presentSec). */
            bool rc = false;
            if (vsOn && presentValid && inGp)   /* era: g_game.vsync && ... */
                rc = Gameplay_BeginRenderClock(presentSec);
            Game_Render();       /* o swap com vsync bloqueia até o refresh */
            if (rc) Gameplay_EndRenderClock();
            g_renderTick = true;
            uint64_t hz2 = SDL_GetPerformanceCounter();
            /* vsync "ligado" mas o swap não bloqueia (driver forçando desligado): os
             * quadros saem muito mais rápido que o monitor. 120 seguidos abaixo de
             * 35% do período -> passa para o limitador. */
            if (vsOn && inGp) {
                double dMs = (double)(hz2 - lastSwapQpc) * 1000.0 / qpcHz;
                fastFrames = (dMs < periodMs * 0.35) ? fastFrames + 1 : 0;
                if (fastFrames >= 120) { Window_VsyncFailed(); fastFrames = 0; nextRenderMs = nowMs; }
            } else {
                fastFrames = 0;
            }
            lastSwapQpc = hz2;
            /* Instante do swap, filtrado: perto do previsto (anterior + período)
             * só corrige 10% do erro, para o intervalo ficar regular mesmo se o
             * retorno do swap oscilar; longe (quadro perdido, vsync que não
             * pegou) assume o valor medido. */
            {
                double tSwap = (double)hz2 / hzF;
                double rr = (double)Window_GetRefreshRate();
                double period = 1.0 / (rr > 0.0 ? rr : 60.0);
                double pred = presentSec + period;
                double err = tSwap - pred;
                if (!presentValid || err > period * 0.5 || err < -period * 0.5)
                    presentSec = tSwap;
                else
                    presentSec = pred + err * 0.1;
                presentValid = true;
            }
            double updMs = (double)(hz1 - hz0) * 1000.0 / hzF;
            double rndMs = (double)(hz2 - hz1) * 1000.0 / hzF;
            if (updMs + rndMs > 50.0 || elapsed > 50.0) {
                double logTot = g_logMs;
                Log_Print("HITCH: gap=%.0f ms update=%.1f ms (%d passos, video=%.1f, log=%.1f) render=%.1f ms (log=%.1f) estado=%d\n",
                          elapsed, updMs, steps, movUpd, logUpd, rndMs, logTot - logUpd, (int)g_game.state);
            }
        } else {
            /* era: Sleep(1) sempre. Perto do próximo passo ou do próximo desenho da
             * agenda, só cede a vez (Sleep(0)) para não passar do ponto em 1 ms. */
            double waitMs = STEP_MS - accumulator;
            if (inGp && !vsOn && nextRenderMs - nowMs < waitMs) waitMs = nextRenderMs - nowMs;
            Sleep(waitMs > 2.0 ? 1 : 0);
        }
    }
}

/* On Linux this is a plain main(); the original WinMain/SEH path only exists
 * on Windows. */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    Game_Init(NULL);
    Game_MainLoop();
    Game_Shutdown();
    return 0;
}
