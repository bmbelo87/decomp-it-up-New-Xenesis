/*
 * intro.c — telas de vídeo do Exceed.
 *   STATE_INTRO  (CIntro::Begin): BGA\INTRO.MOV + AUDIO\001.AUD, depois do logo 81.
 *   STATE_CREDIT (CTitle::Begin): CREDIT.MOV (raiz do jogo), em loop.
 *
 * Ordem das telas informada pelo usuário: R_WARN_A -> 81 -> INTRO -> CREDIT.
 * Hipótese ainda não conferida no exceed.exe: CENTER pula o INTRO.
 */
#include "pumpy.h"
#include "movie.h"
#include "bga.h"

static unsigned g_titleJoined;   /* [0x568FF4]: bit0 = P1 entrou, bit1 = P2 */
static int      g_titleFade;     /* [CTitle+0x1C] */

/* exceed.exe CIdle (vtable 0x1282BC8, "Idle 루프입니다. 크레딧이 없는 상태."):
 * Begin 0x4130F4 para todos os WAVE; 0x41311C escolhe a próxima tela pelo
 * contador [CIdle+4] (começa em 0, +1 a cada entrada):
 *   % 4 == 0 -> LOGO, 1 -> INTRO, 2 -> HIGHSCORE, 3 -> demo
 *   ("RUN A%02d -h -demo", 0x41926C).
 * Toda tela da atração vai para IDLE ao terminar (0x413394) e para TITLE
 * quando há crédito (0x41FE94). */
static int g_idleCount;         /* [CIdle+4] */
static int g_demoCount = -1;    /* [0x455100] */
static bool g_demoSound = true; /* EEPROM +0x7F0 (0x40265B) */
bool g_demoPlay;

bool Demo_SoundOn(void) { return g_demoSound; }

/* 0x41315A + 0x41926C: n = contador % 25 + 1, pulando a 3 (A03 é oculta);
 * "RUN A%02d -h -demo". O -demo (0x401CE0) entra com os dois jogadores em
 * autoplay e [0x568FF4] = 0x100533. */
/* NX DemoPlayRunNext (piu 0x8061c00 = NX2 songmanager.cpp:24): percorre a tabela de
 * músicas a partir de um contador que persiste ([0x9e3dd0c]) e roda a próxima com
 * a flag de demo (+0x41): "RUN %03X -n -demo". Dá a volta na tabela.
 * O teste de censura (+0x34 == 0) não está mapeado em g_songTable e ficou de fora.
 * Sem NORMAL na música, usa a primeira dificuldade de um painel que existir. */
static int s_demoNext;          /* [0x9e3dd0c] */
static bool Demo_StartNX(void) {
    for (int n = 0; n < SONG_TABLE_COUNT; n++) {
        int i = (s_demoNext + n) % SONG_TABLE_COUNT;
        const TrackEntry* e = &g_songTable[i];
        if (!e->demo || (e->channel & 4)) continue;
        int m = -1;
        for (int k = 0; k < 3; k++) if (e->level[k] >= 0) { m = k; break; }
        if (m < 0) continue;
        for (int p = 0; p < 2; p++) {   /* a demo não herda os comandos do último crédito */
            g_game.cmdVanish[p] = g_game.cmdNonStep[p] = g_game.cmdMirror[p] = g_game.cmdRandomStep[p] = false;
            g_game.cmdEarthworm[p] = g_game.cmdFlash[p] = g_game.cmdFreedom[p] = g_game.cmdGradeRev[p] = false;
            g_game.cmdDecel[p] = g_game.cmdAccel[p] = g_game.cmdUnderAttack[p] = false;
            g_game.cmdXMode[p] = g_game.cmdNXMode[p] = g_game.cmdTestBGA[p] = false;
        }
        g_demoSound = (g_game.svcDemoSound == 0);
        if (!Select_Run((int)e->id, m, 1)) continue;
        s_demoNext = i + 1;
        Log_Print("DEMO: RUN %03X -%c -demo (som %d)\n", (unsigned)e->id, "nhc"[m], g_demoSound);
        return true;
    }
    Log_Print("DEMO: nenhuma música com a flag de demo\n");
    return false;
}

/* era (Exceed, "RUN A%02d -h -demo"): mantida, não é mais chamada na NX */
static bool Demo_Start(void) {
    g_demoCount++;
    int n = g_demoCount % 25 + 1;
    if (n == 3) {
        g_demoCount++;
        n = g_demoCount % 25 + 1;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "A%02d", n);
    int id = (int)strtol(buf, NULL, 16);

    Menu_ResetState();
    g_game.selectedSongIndex = Song_FindByID(&g_game.songDB, id);
    g_game.selectedModeIndex = Song_FindMode(&g_game.songDB, "HARD");
    if (g_game.selectedSongIndex < 0 || g_game.selectedModeIndex < 0) {
        Log_Print("DEMO: música %s/HARD não encontrada\n", buf);
        return false;
    }
    g_game.activePlayerMask = 3;
    g_game.isBattleMode = false;
    for (int p = 0; p < 2; p++) {
        g_game.cmdSpeedMult[p] = 1;
        g_game.cmdRandomVelocity[p] = false;
    }
    /* Opção da EEPROM: som na demo */
    /* era (Exceed2): g_demoSound = (OldSave_Data()[0x7F0] != 0);   EEPROM +0x7F0 (PIUEXCEED2.INI)
     * NX: DEMO SOUND do Service Menu (EEPROM +0xED1, g_game.svcDemoSound, 0 = ligado) */
    g_demoSound = (g_game.svcDemoSound == 0);
    Log_Print("DEMO: RUN %s -h -demo (som %d)\n", buf, g_demoSound);
    g_demoPlay = true;
    Loading_Enter(id);
    return true;
}

/* 0x402B82: com crédito ou 35 s de música ([+0x17C38] >= 35000) -> IDLE */
void Demo_End(void) {
    g_demoPlay = false;
    Gameplay_Exit();
    Movie_Close();
    Resource_ClearBGA();
    Attract_Idle();
}

/* Extra do port (GAME STYLE = MODDED): atração em FREE PLAY / EVENT.
 * No original as telas da atração (LOGO, INTRO, HIGHSCORE, demo) pulam para o TITLE
 * sempre que há crédito, e em FREE PLAY / EVENT "há crédito" o tempo todo: o jogo
 * fica preso no "CENTER STEP" e a atração nunca roda. No MODDED, com FREE PLAY, a
 * atração roda normalmente e só vai para o TITLE quando alguém pisa no CENTER; o
 * TITLE volta para a atração depois de ATTRACT_TITLE_FRAMES sem ninguém entrar.
 * Fora disso (VANILLA, ou máquina com moeda) vale o teste de crédito do original. */
/* era: #define ATTRACT_TITLE_FRAMES 1200 (20 s, valor meu). Agora o TITLE usa o tempo
 * do original: 3150 quadros sem moeda e 60 de fade (NX2 title.cpp Run/End). */
#define TITLE_IDLE_FRAMES 3150
#define TITLE_FADE_FRAMES 60
static bool s_attractWantTitle;
static int  s_attractStep;      /* 0 LOGO 81, 1 NEXCADE, 2 OP, 3 TITLE, 4 demo, 5 HIGHSCORE, 6 WPF2007 */
static const char* s_movieName = "OP";   /* PLAYMOVIE <nome>: BGA/<nome>.MOV + AUDIO/<nome>.AUD */
static int  s_titleFadeOut;     /* CTitle::End: 0..60 */
static bool attractFree(void) { return g_game.gameStyleModded && Coin_IsFreePlay(); }

bool Attract_HasCredit(void) {
    if (!attractFree()) return Coin_HasCredit();
    if (Input_IsPadHit(0, PAD_C) || Input_IsPadHit(1, PAD_C)) s_attractWantTitle = true;
    return s_attractWantTitle;
}

void Attract_Idle(void) {
    Audio_StopAll();            /* 0x4130F9..0x41310F */
    if (s_attractWantTitle) {   /* CENTER durante a demo: Demo_End cai aqui */
        s_attractStep = 3;
        Game_ChangeState(STATE_CREDIT);
        return;
    }
    /* era (Exceed CIdle 0x41311C): contador % 4 -> LOGO, INTRO, HIGHSCORE, demo:
     *     switch (g_idleCount++ & 3) { case 0: LOGO_ENTER; case 1: INTRO; case 2: HIGHSCORE; case 3: Demo_Start(); }
     * NX CFlowControl::Next (piu 0x805fbc0 = NX2 flowcontrol.cpp): laço da atração
     *   PublisherLogo "PLAYMOVIE 81" -> DeveloperLogo "PLAYMOVIE NEXCADE" ->
     *   Opening "PLAYMOVIE OP" -> Title -> Demo -> HighScore -> volta ao começo.
     * No piu do NX a tabela de saltos (0x80fcf70) segue com "PLAYMOVIE WPF2007" depois do
     * HIGHSCORE: o vídeo WPF2007 fecha o laço antes de voltar ao logo (confirmado pelo
     * usuário no arcade). No NX2 ele virou propaganda chamada por botão.
     * era: laço de 6 passos, sem o WPF2007. */
    (void)g_idleCount; (void)Demo_Start;
    /* O passo atual vem da tela em que se está, quando ela é do laço: quem entra no
     * ranking ou no logo por outro caminho (gancho de teste, console) saía do passo
     * errado — do ranking ia direto para o NEXCADE, pulando o WPF2007 e o logo. */
    if (g_game.state == STATE_HIGHSCORE) s_attractStep = 5;
    else if (g_game.state == STATE_LOGO_ENTER) s_attractStep = 0;
    for (int guard = 0; guard < 7; guard++) {
        s_attractStep = (s_attractStep + 1) % 7;
        switch (s_attractStep) {
        case 0: Game_ChangeState(STATE_LOGO_ENTER); return;
        case 1: s_movieName = "NEXCADE"; Game_ChangeState(STATE_INTRO); return;
        case 2: s_movieName = "OP";      Game_ChangeState(STATE_INTRO); return;
        case 3: Game_ChangeState(STATE_CREDIT);     return;
        case 4: if (Demo_StartNX()) return; break;
        case 5: Game_ChangeState(STATE_HIGHSCORE);  return;
        case 6: s_movieName = "WPF2007"; Game_ChangeState(STATE_INTRO); return;
        }
    }
}

/* Fim de um crédito (NX2 flowcontrol.cpp, Process::GameOver): com crédito vai para
 * o TITLE, senão recomeça o laço pelo logo. O logo já pula para o TITLE quando há
 * crédito (Attract_HasCredit), então basta recomeçar o laço. */
void Attract_Restart(void) {
    s_attractStep = -1;
    Attract_Idle();
}

/* [0x568FF4] bits 0/1: quem entrou no CREDIT (lido pela Select) */
unsigned Title_GetJoinedMask(void) { return g_titleJoined; }
void Title_SetJoinedMask(unsigned m) { g_titleJoined = m; }   /* CStation: entrada tardia */

static int g_titleMusic = -1;   /* EFF_TITLE */
void Title_StopMusic(void) {
    if (g_titleMusic >= 0) Audio_Stop(g_titleMusic);
}

static void intro_open(const char* rel, bool loop) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", g_game.currentDirectory, rel);
    Movie_Open(path, loop);
}

void Gamestate_UpdateIntro(float dt) {
    switch (g_game.state) {
    case STATE_INTRO:
        if (g_game.stateFrame == 1) {
            /* era (Exceed): BGA/INTRO.MOV + AUDIO/001.AUD (não existem na NX).
             * NX CPlayMovie::Begin (NX2 playmovie.cpp): BGA/<nome>.MOV e, com DEMO
             * SOUND ligado, AUDIO/<nome>.AUD se existir. NEXCADE não tem áudio. */
            char rel[64], aud[MAX_PATH];
            snprintf(rel, sizeof(rel), "BGA/%s.MOV", s_movieName);
            snprintf(aud, sizeof(aud), "%s/AUDIO/%s.AUD", g_game.currentDirectory, s_movieName);
            intro_open(rel, false);
            BGM_Stop();
            if (g_game.svcDemoSound == 0) {
                FILE* af = fopen(aud, "rb");
                if (af) { fclose(af); if (BGM_LoadAUDDirect(aud)) BGM_Play(false); }
            }
        }
        Movie_Update(dt);
        /* 0x4132F0: com crédito vai para TITLE (era: Coin_HasCredit()) */
        if (Attract_HasCredit()) {
            Movie_Close();
            Game_ChangeState(STATE_CREDIT);
            break;
        }
        /* Hipótese antiga (CENTER pulava o INTRO), não existe no original:
        if (g_game.stateFrame > 1 &&
            (Movie_HasEnded() || !Movie_IsOpen() ||
             Input_IsPadHit(0, PAD_C) || Input_IsPadHit(1, PAD_C))) { */
        if (g_game.stateFrame > 1 && (Movie_HasEnded() || !Movie_IsOpen())) {
            Log_Print("INTRO: fim (frame %d, ended=%d open=%d)\n", g_game.stateFrame,
                      Movie_HasEnded(), Movie_IsOpen());
            Movie_Close();
            /* Game_ChangeState(STATE_CREDIT); */
            Attract_Idle();     /* fim do vídeo -> IDLE (0x413394) */
        }
        break;
    case STATE_CREDIT:
        if (g_game.stateFrame == 1) {
            /* CTitle::Begin (0x41C05A): 0x426570("AUDIO\TITLE.AUD") e depois
             * 0x4229C8("CREDIT.MOV", 1) — vídeo em loop. */
            /* Exceed2 (desativado): AUDIO\TITLE.AUD como BGM
            char aud[MAX_PATH];
            snprintf(aud, sizeof(aud), "%s/AUDIO/TITLE.AUD", g_game.currentDirectory);
            BGM_Stop();
            if (BGM_LoadAUDDirect(aud)) BGM_Play(true);
            */
            /* Zero CTitle (piu 0x8067xxx): 0x804f220("EFF_TITLE", 1) ->
             * WAVE/TITLE.WAV (SFX_TITLE.LUA) em loop; segue tocando na Station */
            BGM_Stop();
            if (g_titleMusic < 0) g_titleMusic = Audio_LoadWaveFile("TITLE.WAV");
            /* era: tocava sempre. NX2 CTitle::Begin: só com DEMO SOUND ligado ou com moeda */
            if (g_titleMusic >= 0) {
                Audio_Stop(g_titleMusic);
                if (g_game.svcDemoSound == 0 || Coin_HasCredit()) Audio_Play(g_titleMusic, true);
            }
            s_titleFadeOut = 0;
            s_attractStep = 3;
            /* Zero CTitle::Begin (piu 0x80670fc): BGA/CREDIT.MOV em loop */
            intro_open("BGA/CREDIT.MOV", true);
            /* era (Exceed2): intro_open("CREDIT.MOV", true); */
            g_titleJoined = 0;
            g_titleFade = 0;
            s_attractWantTitle = false;   /* chegou ao TITLE */
        }
        /* NX2 CTitle::Run/End: sem moeda, depois de 3150 quadros a tela escurece em 60
         * e a atração segue (demo). Com moeda o TITLE fica; em FREE PLAY / EVENT o
         * original considera "com moeda" (fica para sempre) — no MODDED também sai. */
        {
            bool semMoeda = attractFree() || (!Coin_IsFreePlay() && g_game.svcCoinTotal == 0);
            if (!(g_titleJoined & 3) && semMoeda && g_game.stateFrame >= TITLE_IDLE_FRAMES) {
                Render_SetGlobalColor(0.0f, 0.0f, 0.0f, (float)s_titleFadeOut / (float)TITLE_FADE_FRAMES);
                if (s_titleFadeOut >= TITLE_FADE_FRAMES) {
                    Movie_Close();
                    Attract_Idle();   /* Game_ChangeState zera o escurecimento */
                    break;
                }
                s_titleFadeOut++;
            } else if (s_titleFadeOut) {   /* entrou moeda durante o fade: volta */
                s_titleFadeOut = 0;
                Render_SetGlobalColor(0.0f, 0.0f, 0.0f, 0.0f);
            }
        }
        /* NX CTitle::Begin (0x808c661 / 0x808c67c): já carrega BGA/COMMON.DAT e
         * BGA/ARRO.DAT, que a CStation usa sem recarregar. Extra deste projeto:
         * feito no 2º quadro (o 1º do CREDIT.MOV já está na tela) e o relógio é
         * zerado depois, senão a tela fica preta durante a carga e o vídeo pula. */
        if (g_game.stateFrame == 2) {
            Resource_LoadBGAByName("COMMON");
            Resource_LoadBGAByName("ARRO");
            g_game.lastTime = timeGetTime();
            dt = 0.0f;
        }
        Movie_Update(dt);

        /* 0x41C192..0x41C1C8 / 0x41C594 / 0x41C5EC: com crédito, CENTER do
         * P1 (bit 0x4 de [0x568FE8]) ou do P2 (bit 0x400) entra o jogador:
         * consome crédito (0x41FEE0), liga o bit em [0x568FF4] e toca
         * WAVE/2-1.wav ([0xA6D304], carregado em 0x4258A6). */
        if (Coin_HasCredit()) {
            if (!(g_titleJoined & 1) && Input_IsPadHit(0, PAD_C)) {
                Coin_ConsumeCredit();
                g_titleJoined |= 1;
                Audio_Play(g_waveSoundIds[SND_2_1], false);
            } else if (!(g_titleJoined & 2) && Input_IsPadHit(1, PAD_C)) {
                Coin_ConsumeCredit();
                g_titleJoined |= 2;
                Audio_Play(g_waveSoundIds[SND_2_1], false);
            }
        }

        /* 0x41C4F3 / 0x41C56B: com alguém dentro, [this+0x1C] conta +1 por
         * frame (fade preto = contador/60) e passando de 60 vai para "SELECT"
         * (0x4102D4). */
        /* Exceed2 (PIU32.EXE 0x41acfe / 0x41aef7): com alguém dentro a fase 3
         * vai direto para "STATION" (0x41af1f), sem fade; o CREDIT.MOV e o
         * TITLE.AUD continuam (o CStation desenha o vídeo, 0x419d1f). */
        if (g_titleJoined & 3) {
            Game_ChangeState(STATE_STATION);
            return;
        }
#if 0   /* Exceed (exceed.exe): fade de 60 quadros e SELECT — DESATIVADO */
        if (g_titleJoined & 3) {
            if (g_titleFade > 60) {
                Movie_Close();
                /* Game_ChangeState(STATE_SONG_SELECT); */  /* select do Prex3 (099.DAT) */
                /* Contagem de estágios do projeto (3 + bônus, regra do Prex3).
                 * HIPÓTESE para o Exceed: ainda não conferido no exceed.exe. */
                Menu_ResetState();
                Game_ChangeState(STATE_SELECT);
                return;
            }
            g_titleFade++;
        }
#endif
        break;
    default:
        break;
    }
}

/* CTitle: desenho do BGA\82.DAT sobre o vídeo (0x41C1CE..0x41C483).
 *   com crédito (0x41FEB8 != 0): slots 16,17,20 no quadro 244 + c%30 (P1)
 *                                 e 365 + c%30 (P2)  — bb, ba, pcs
 *   sem crédito:                  slots 6,7 no quadro c%47 (P1)
 *                                 e 147 + c%47 (P2)   — pic
 *   P1/P2 pulados pelos bits 0/1 de [0x568FF4]; sempre slot 3 (ltd) no quadro 0.
 * c = [CTitle+0x14], +1 por frame (0x41C54E) = stateFrame. */
void Gamestate_RenderIntro(void) {
    Movie_Render();
    if (g_game.state != STATE_CREDIT || g_game.bgaPicCount <= 0) return;

    int c = g_game.stateFrame;
    unsigned joined = g_titleJoined;           /* [0x568FF4] */
    /* NX CTitle (vtable 0x81149a8, quadro 0x808c7b0): cenas do COMMON.DAT
     * ([0x9e0d118]) — com crédito "1p/2p center step", sem crédito "1p/2p insert
     * coin", só do lado que ainda não entrou ([0x81f8904] bits 0/1). */
    {
        int cm = -1;
        for (int i = 0; i < g_game.bgaPicCount; i++)
            if (_stricmp(g_game.bgaPics[i].name, "COMMON") == 0) { cm = i; break; }
        if (cm >= 0) {
            bool cr = Coin_HasCredit();                       /* 0x804e5c0 */
            if (!(joined & 1)) BGA_ScenePlay(cm, cr ? "1p center step" : "1p insert coin", true);
            if (!(joined & 2)) BGA_ScenePlay(cm, cr ? "2p center step" : "2p insert coin", true);
        }
    }
    (void)c;
#if 0   /* era (Exceed): slots do BGA\82.DAT (0x41C1CE..0x41C483) */
    if (Coin_HasCredit()) {
        if (!(joined & 1)) {
            BGA_DrawSlot(0, 244 + c % 30, 16);
            BGA_DrawSlot(0, 244 + c % 30, 17);
            BGA_DrawSlot(0, 244 + c % 30, 20);
        }
        if (!(joined & 2)) {
            BGA_DrawSlot(0, 365 + c % 30, 16);
            BGA_DrawSlot(0, 365 + c % 30, 17);
            BGA_DrawSlot(0, 365 + c % 30, 20);
        }
    } else {
        if (!(joined & 1)) {
            BGA_DrawSlot(0, c % 47, 6);
            BGA_DrawSlot(0, c % 47, 7);
        }
        if (!(joined & 2)) {
            BGA_DrawSlot(0, 147 + c % 47, 6);
            BGA_DrawSlot(0, 147 + c % 47, 7);
        }
    }
    BGA_DrawSlot(0, 0, 3);
#endif

    /* 0x41C488: fade preto por cima, alpha = [CTitle+0x1C] / 60.0 */
    if (g_titleFade > 0) {
        float a = (float)g_titleFade / 60.0f;
        if (a > 1.0f) a = 1.0f;
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.0f, 0.0f, 0.0f, a);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(640, 0); glVertex2f(640, 480); glVertex2f(0, 480);
        glEnd();
        glColor4f(1, 1, 1, 1);
    }
}
