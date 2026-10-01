#include "pumpy.h"
#include "movie.h"

#define LOADING_DURATION_MS 3550
#define LOADING_FADE_MS 48

static int g_pnzTexId = -1;
static int g_loadingSongId = -1;
static int g_loadingTimer = 0;
/* PUMPY.EXE: a confirmação executa o comando de console "run" -> 0x410cf0,
 * que carrega tudo e espera [0xd35eb4] >= 0x3C0 (0x4116b5) antes da música.
 * ATENÇÃO: esse contador conta desde o início do TIME do Song Select
 * (0x4096bd/0x40aadc), então no original normalmente NÃO há espera — o título
 * fica congelado só durante a carga (lenta no original: DirectMusic + .DAT).
 * O piso de 4 s abaixo aproxima esse tempo de carga percebido (validado em jogo);
 * não é uma regra literal do original. */
#define LOADING_MIN_TO_MUSIC_MS 4000
static uint32_t g_loadingStartMs = 0;

void Loading_Enter(int songId) {
    g_loadingSongId = songId;
    g_loadingTimer = LOADING_DURATION_MS;
    g_loadingStartMs = timeGetTime();
    g_pnzTexId = -1;

    // Decrementa stage count (exceto no bonus que nao altera)
    if (g_game.stageCount > 0)
        g_game.stageCount--;
    else
        g_game.isBonusSong = true;

    // Destroi BGA do Song Select (099.DAT) antes de entrar no Title
    Resource_ClearBGA();

    char path[MAX_PATH];
    /* Zero (piu 0x8081594 / 0x80825ad): TITLE/T%sH.PNZ (coreano) ou T%sE.PNZ,
     * sempre com o id da própria música. Idioma ainda fixo em inglês (E). */
    /* era (Zero): "%s/TITLE/T%sE.PNZ". NX: "TITLE/%03X.PNZ" (string do piu);
     * sem o arquivo, o recurso de 0x8061d10 (Another -> base). */
    snprintf(path, sizeof(path), "%s/TITLE/%s.PNZ", g_game.currentDirectory, Song_IdStr(songId));
    {
        FILE* tf = fopen(path, "rb");
        if (tf) fclose(tf);
        else if (NX_ResId(songId) >= 0)
            snprintf(path, sizeof(path), "%s/TITLE/%s.PNZ", g_game.currentDirectory, Song_IdStr(NX_ResId(songId)));
    }
    /* era (Exceed2): "%s/TITLE/T%s.pnz" com Song_DataIdStr(songId) */
    Log_Print("Loading: loading PNZ '%s'\n", path);

    g_pnzTexId = Resource_LoadPNZ(path);
    if (g_pnzTexId < 0) {
        Log_Print("Loading: PNZ not found for song %d\n", songId);
    }

    g_game.state = STATE_SONG_TITLE;
    g_game.stateFrame = 0;
    Render_SetGlobalColor(0, 0, 0, 0);
}

void Loading_Update(float dt) {
    if (g_game.state == STATE_SONG_TITLE) {
        int ms = (int)(dt * 1000.0f);
        if (ms < 1) ms = 1;
        g_loadingTimer -= ms;

        if (g_loadingTimer <= 0) {
            g_loadingTimer = LOADING_FADE_MS;
            g_game.state = STATE_SONG_TITLE_OUT;
            g_game.stateFrame = 0;
        }
        return;
    }

    if (g_game.state == STATE_SONG_TITLE_OUT) {
        int ms = (int)(dt * 1000.0f);
        if (ms < 1) ms = 1;
        g_loadingTimer -= ms;

        if (g_loadingTimer <= 0) {
            /* Resource_ClearBGA chama Texture_Shutdown, que destrói TODAS as texturas
               incluindo o pnz. Resetar g_pnzTexId antes para evitar Texture_Unload
               posterior no slot que já foi reutilizado pelo BGA (bug: destruía HALL.PNG). */
            g_pnzTexId = -1;

            Resource_ClearBGA();

            /* exceed.exe 0x402243: testa BGA\%s.MOV (0x4254E4 = fopen "rb");
             * se existe, 0x4229C8(path, 0) sem loop e [+0x17C2C] = 2 (0x402688);
             * senão BGA\%s.DAT. O vídeo é aberto junto com a música, abaixo. */
            /* Zero, CPlayEngine::Run: BGA/%X.DAT (id, depois base: 0x80809a6..0x8080a2a)
             * tem prioridade; sem ele, BGA/%X.MOV (id, depois base: 0x8081c63..)
             * e, por fim, BGA/000.MOV em loop (0x8081d11). */
            char movPath[MAX_PATH];
            char bgaPath[MAX_PATH];
            Movie_Close();
            bool useDat = Song_FindFile(g_loadingSongId, "%s/BGA/%s.DAT", false, bgaPath, sizeof(bgaPath));
            bool useMov = false, movLoop = false;
            if (useDat) {
                Log_Print("Loading: loading BGA '%s'\n", bgaPath);
                Resource_LoadBGADirect(bgaPath);
            } else {
                useMov = Song_FindFile(g_loadingSongId, "%s/BGA/%s.MOV", false, movPath, sizeof(movPath));
                if (!useMov) {
                    snprintf(movPath, sizeof(movPath), "%s/BGA/000.MOV", g_game.currentDirectory);
                    FILE* mf = fopen(movPath, "rb");
                    useMov = movLoop = (mf != NULL);
                    if (mf) fclose(mf);
                }
            }
            g_game.bgaLoop = false;
            BGA_Reset();

            char audioPath[MAX_PATH];
            /* Zero 0x8081d74: AUDIO/%03X.AUD, id e depois base (Another usa a da base) */
            Song_FindFile(g_loadingSongId, "%s/AUDIO/%s.AUD", true, audioPath, sizeof(audioPath));
            Log_Print("Loading: loading AUD '%s'\n", audioPath);
            bool audOk = BGM_LoadAUDDirect(audioPath);

            g_game.songSelectHighlighted = g_game.selectedSongIndex;

            /* Steps carregados antes da espera, como na init do original
             * (0x410cf0 carrega o step antes do loop de 0x4116b5). */
            g_game.state = STATE_GAMEPLAY;
            Gameplay_Start(g_loadingSongId);

            /* Espera ativa como no original (0x4116b5): nada é desenhado. */
            while (timeGetTime() - g_loadingStartMs < LOADING_MIN_TO_MUSIC_MS)
                Sleep(1);

            if (useMov) {
                Log_Print("Loading: loading MOV '%s'\n", movPath);
                Movie_Open(movPath, movLoop);
            }
            if (audOk && !(g_exDemo && !Demo_SoundOn()))   /* 0x40236A */
                BGM_Play(false);
            g_game.stateFrame = 0;
            g_game.bgaFrame = 0;
            Render_SetGlobalColor(0, 0, 0, 0);
        }
        return;
    }
}

void Loading_Render(void) {
    if (g_game.state != STATE_SONG_TITLE && g_game.state != STATE_SONG_TITLE_OUT)
        return;

    if (g_pnzTexId >= 0) {
        int tw = Texture_GetWidth(g_pnzTexId);
        int th = Texture_GetHeight(g_pnzTexId);

        float scaleX = 640.0f / (float)tw;
        float scaleY = 480.0f / (float)th;
        float scale = scaleX < scaleY ? scaleX : scaleY;
        float dw = tw * scale;
        float dh = th * scale;
        float dx = (640.0f - dw) / 2.0f;
        float dy = (480.0f - dh) / 2.0f;

        Texture_Draw(g_pnzTexId, dx, dy, scale, scale, 1.0f);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Loading %d...", g_loadingSongId);
        Font_DrawStringCentered(320, 240, buf, 0.5f, 0.5f, 0.5f, 1.0f);
    }
}

bool Loading_IsActive(void) {
    return g_game.state == STATE_SONG_TITLE || g_game.state == STATE_SONG_TITLE_OUT;
}
