/*
 * nx_mission.c — tela do objetivo da missão do WORLD TOUR (piu NX).
 *
 * Original: fase de abertura da CPlay em modo missão (0x806bee0, [0xa7f4a64] != 0),
 * depois do título e antes da música. BGA/MICF.DAT (0x806ac28, objeto 0x9e0d800):
 *   "mission"      todo quadro; no stage n > 0 o slot 0xe recebe o slot 0x28 + n
 *   texto          objetivo por idioma [0x9e3d8a9] (EEPROM +0xBA9): 0 0x8142340,
 *                  1 0x8142640, 2 0x8142040, 3 0x8142c40; índice local * 3 + stage;
 *                  0x8090c40(x 0x55, y 0xc2), '@' quebra a linha
 *   "command"      só com itens na música (+0x103b4..+0x103c0 -> slots 0x19..0x1c)
 *   "time"         contador: 0x5014 ms; dígitos no slot 0x12 (unidade) e 0x11 (dezena)
 *                  com o slot 0x27 + dígito
 *   "center step"  botão amarelo, depois de 0x7d0 ms; a partir daí CENTER encerra
 *   EFF_MISSION_READY (V_READY.WAV) quando o contador chega a 5 (0x806c669);
 *   EFF_MISSION_START (START.WAV) ao sair (0x806c250, com [+0x1047d]).
 * Ao sair segue para o início da música (vtable +0x10).
 *
 * Ainda não feito: itens (fase 2) — sem eles a cena "command" não aparece.
 * O fundo é preto (o original não desenha o BGA da música nesta fase).
 */
#include "pumpy.h"
#include "bga.h"

void NxText_SetBlock(const char* str);
void NxText_DrawBlock(int x, int y);
void Loading_BeginGameplay(void);

static uint32_t s_t0;
static int  s_secs, s_prevSecs;
static int  s_stage;
static bool s_ok;
static int  s_sndReady = -1, s_sndStart = -1;
static BGALayerSrc s_dig[10];
static bool s_cmd;   /* cena "command" (há ícones) */
static bool s_lesson;   /* TRAINING: cena "lesson" (0x806c470) */
extern const char* const g_nxTrainIntro[4][21];

/* 0x806bf85: local a partir do id da missão (0xAAnnk): n = 10*dezena + unidade */
static int missionLoc(int id) {
    int n = ((id >> 8) & 0xf) * 10 + ((id >> 4) & 0xf);
    if (n < 1) n = 1;
    return ((n - 1) % 4) * 16 + (n - 1) / 4;
}

void NxMission_Enter(int id) {
    g_game.state = STATE_NX_MISSION;
    g_game.stateFrame = 0;
    Resource_ClearBGA();
    s_ok = Resource_LoadBGAByName("MICF");
    if (!s_ok) Log_Print("MISSION: falha ao carregar BGA\\MICF.DAT\n");
    BGA_Reset();
    s_lesson = g_game.nxGameMode == 3;
    s_stage = 2 - g_game.stageCount;   /* [0x81f8910] (Loading_Enter já decrementou) */
    if (s_stage < 0) s_stage = 0;
    if (s_stage > 2) s_stage = 2;
    if (s_ok) {
        for (int i = 0; i < 10; i++) s_ok &= BGA_GetLayerSrc(0, 0x27 + i, &s_dig[i]);
        BGALayerSrc st;
        if (s_lesson) {   /* 0x806c48e..0x806c57d: lição (dezena 0xd, unidade 0xe) e parte (0x10) */
            int n = g_nxTrainN;
            BGA_SetLayerSrc(0, 0xd, &s_dig[(n / 100) % 10]);
            BGA_SetLayerSrc(0, 0xe, &s_dig[(n / 10) % 10]);
            BGA_SetLayerSrc(0, 0x10, &s_dig[n % 10]);
        } else if (s_stage > 0 && BGA_GetLayerSrc(0, 0x28 + s_stage, &st)) BGA_SetLayerSrc(0, 0xe, &st);
    }
    /* 0x806c00f..0x806c056: ícones dos modificadores (COMMAND.DAT, 0x806ac0d) nos slots 0x19..0x1c */
    s_cmd = false;
    if (s_ok && g_nxMods.nIcons > 0 && Resource_LoadBGAByName("COMMAND")) {
        int cb = g_game.bgaPicCount - 1;
        for (int i = 0; i < g_nxMods.nIcons; i++) {
            BGALayerSrc ic;
            if (BGA_GetLayerSrc(cb, g_nxMods.icons[i], &ic)) { BGA_SetLayerSrc(0, 0x19 + i, &ic); s_cmd = true; }
        }
        BGA_SceneReset(0, "command");
    }
    int lang = g_game.svcLangOption;
    if (lang < 0 || lang > 3) lang = 1;
    int loc = missionLoc(id);
    if (s_lesson) {   /* 0x806c582: texto pela lição (KR 0x8141ebc, EN 0x8141f1c, 0x8141e5c, 0x8141f7c) */
        int L = g_nxTrainN / 10;
        NxText_SetBlock(g_nxTrainIntro[lang][L >= 1 && L <= 20 ? L : 0]);
    } else
    NxText_SetBlock(g_nxWorldDesc[lang][loc * 3 + s_stage]);
    if (s_sndReady < 0) s_sndReady = Audio_LoadWaveFile("V_READY.WAV");
    if (s_sndStart < 0) s_sndStart = Audio_LoadWaveFile("START.WAV");
    BGA_SceneReset(0, "mission");
    BGA_SceneReset(0, "lesson");
    BGA_SceneReset(0, "time");
    BGA_SceneReset(0, "center step");
    s_t0 = timeGetTime();
    s_secs = s_prevSecs = 0x5014 / 1000;
    Log_Print("MISSION: %X local %d stage %d: \"%s\"\n", (unsigned)id, loc, s_stage + 1, g_nxWorldDesc[1][loc * 3 + s_stage]);
}

static void leave(void) {
    if (s_sndStart >= 0) Audio_Play(s_sndStart, false);   /* EFF_MISSION_START */
    Loading_BeginGameplay();
}

void NxMission_Update(float dt) {
    (void)dt;
    uint32_t el = timeGetTime() - s_t0;
    s_prevSecs = s_secs;
    s_secs = el >= 0x5014 ? 0 : (int)((0x5014 - el) / 1000);
    if (s_secs <= 5 && s_prevSecs > 5 && s_sndReady >= 0) Audio_Play(s_sndReady, false);   /* 0x806bf3e */
    if (el > 0x5014) { leave(); return; }
    if (el > 0x7d0)   /* 0x806c117: CENTER só depois de 2 s */
        for (int p = 0; p < 2; p++)
            if ((g_game.activePlayerMask & (1 << p)) && Input_IsPadHit(p, PAD_C)) { leave(); return; }
}

void NxMission_Render(void) {
    if (!s_ok) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
    BGA_ScenePlay(0, s_lesson ? "lesson" : "mission", true);
    NxText_DrawBlock(0x55, 0xc2);
    if (s_cmd) BGA_ScenePlay(0, "command", true);
    BGA_SetLayerSrc(0, 0x12, &s_dig[s_secs % 10]);
    BGA_SetLayerSrc(0, 0x11, &s_dig[(s_secs / 10) % 10]);
    BGA_ScenePlay(0, "time", true);
    if (timeGetTime() - s_t0 > 0x7d0) BGA_ScenePlay(0, "center step", true);
    glColor4f(1, 1, 1, 1);
}

/* ===========================================================================
 * CContinue (vtable 0x810fb28): Stage Break do WORLD TOUR.
 *   Begin 0x8072940: BGA/CONTINUE.DAT, BGA/STAGEBREAK.MOV (sem loop),
 *                    AUDIO/STAGEBREAK.AUD
 *   0x8072a80: espera o vídeo acabar -> EFF_CONTINUE (T2_10.WAV), zera o cronômetro
 *   0x8072af0: quadros = ms / 1000 * 60; cena "continue"; cena "COUNT" no quadro
 *              decorrido (0x8058640); contador = 11 - (s - 1/15); quando muda e
 *              <= 9, EFF_CONTINUE_TIME_TICK. Com crédito, CENTER -> EFF_JOIN,
 *              consome o crédito e repete a missão; setas (UL UR DL DR) -> tick
 *              e +60 quadros. Quadros > 0x298 -> fim (game over).
 * Não feito: crédito inserido durante a contagem reinicia o cronômetro (0x8072b7b).
 * ========================================================================= */
#include "movie.h"
void NxWorld_Retry(void);

static uint32_t s_cT0;
static int  s_cPhase;        /* 0 vídeo, 1 contagem, 2 fim */
static int  s_cCount;        /* +0x10 */
static int  s_cExtra;        /* quadros adiantados pelas setas */
static bool s_cOk;
static int  s_sndCont = -1, s_sndTick = -1, s_sndJoin = -1;

void NxContinue_Enter(void) {
    BGM_Stop();
    Movie_Close();
    Resource_ClearBGA();
    s_cOk = Resource_LoadBGAByName("CONTINUE");
    if (!s_cOk) Log_Print("CONTINUE: falha ao carregar BGA\CONTINUE.DAT\n");
    BGA_Reset();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/BGA/STAGEBREAK.MOV", g_game.currentDirectory);
    if (!Movie_Open(path, false)) Log_Print("CONTINUE: '%s' não abriu\n", path);
    snprintf(path, sizeof(path), "%s/AUDIO/STAGEBREAK.AUD", g_game.currentDirectory);
    if (BGM_LoadAUDDirect(path)) BGM_Play(false);
    if (s_sndCont < 0) s_sndCont = Audio_LoadWaveFile("T2_10.WAV");
    if (s_sndTick < 0) s_sndTick = Audio_LoadWaveFile("TIME_LIMIT.WAV");
    if (s_sndJoin < 0) s_sndJoin = Audio_LoadWaveFile("PUSHPANEL.WAV");
    s_cPhase = 0;
    s_cCount = -1;
    s_cExtra = 0;
}

static int contFrames(void) {
    return (int)((timeGetTime() - s_cT0) / 1000.0 * 60.0) + s_cExtra;
}

void NxContinue_Update(float dt) {
    Movie_Update(dt);
    if (s_cPhase == 0) {   /* 0x8072a80 */
        if (Movie_IsOpen() && !Movie_HasEnded()) return;
        if (s_sndCont >= 0) Audio_Play(s_sndCont, false);   /* EFF_CONTINUE */
        s_cT0 = timeGetTime();
        BGA_SceneReset(0, "continue");
        BGA_SceneReset(0, "COUNT");
        s_cPhase = 1;
        return;
    }
    if (s_cPhase != 1) return;
    int fr = contFrames();
    if (fr > 0x298) {   /* 0x8072b69 */
        s_cPhase = 2;
        BGM_Stop();
        Movie_Close();
        Game_ChangeState(STATE_GAMEOVER_ENTER);
        return;
    }
    double s = fr / 60.0;
    int c = (int)(11.0 - (s - 1.0 / 15.0));   /* 0x8072bae */
    if (c != s_cCount) {
        s_cCount = c;
        if (c <= 9 && s_sndTick >= 0) Audio_Play(s_sndTick, false);
    }
    for (int p = 0; p < 2; p++) {
        if (Coin_HasCredit() && Input_IsPadHit(p, PAD_C)) {   /* 0x8072d1a */
            if (s_sndJoin >= 0) Audio_Play(s_sndJoin, false);   /* EFF_JOIN */
            Coin_ConsumeCredit();
            s_cPhase = 2;
            BGM_Stop();
            Movie_Close();
            NxWorld_Retry();
            return;
        }
        if (Input_IsPadHit(p, PAD_UL) || Input_IsPadHit(p, PAD_UR) ||
            Input_IsPadHit(p, PAD_DL) || Input_IsPadHit(p, PAD_DR)) {   /* 0x8072ce7 */
            if (s_sndTick >= 0) Audio_Play(s_sndTick, false);
            s_cExtra += 0x3c;
        }
    }
}

void NxContinue_Render(void) {
    if (Movie_IsOpen()) Movie_Render();
    if (s_cPhase != 1 || !s_cOk) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
    BGA_ScenePlay(0, "continue", true);
    BGA_ScenePlayAt(0, "COUNT", contFrames());   /* 0x8058640("COUNT", quadros) */
    glColor4f(1, 1, 1, 1);
}

/* ===========================================================================
 * Condição da missão (0x8055a80): expressão infixa com inteiros, avaliada por
 * pilhas (shunting-yard). Operadores 0x80faa20 {texto, precedência}, menor = mais forte:
 *   * / % (0)  + - (1)  < > <= >= (2)  == != (3)  && (4)  || (5); "(" e ")".
 * Identificadores (0x80fab40, comparados sem caixa): rank S A B C D F perfect great
 * good bad miss totalstep hidden allhidden maxcombo misscombo score item allitem
 * heart allheart mine allmine potion allpotion velocity allvelocity.
 * rank: S=5 .. F=0. Sem condição (string vazia) = sucesso.
 * ========================================================================= */
static const struct { const char* op; int prec; } k_ops[13] = {
    { "*", 0 }, { "/", 0 }, { "%", 0 }, { "+", 1 }, { "-", 1 }, { "<", 2 }, { ">", 2 },
    { "<=", 2 }, { ">=", 2 }, { "==", 3 }, { "!=", 3 }, { "&&", 4 }, { "||", 5 },
};

static int condVar(const char* n, int p, int rank, bool* ok) {
    const NxMisStats* m = &g_nxMis[p];
    const GameplayStats* s = &g_game.stats;
    *ok = true;
    if (!_stricmp(n, "rank")) return rank;
    if (!_stricmp(n, "S")) return 5;
    if (!_stricmp(n, "A")) return 4;
    if (!_stricmp(n, "B")) return 3;
    if (!_stricmp(n, "C")) return 2;
    if (!_stricmp(n, "D")) return 1;
    if (!_stricmp(n, "F")) return 0;
    if (!_stricmp(n, "perfect")) return (int)s->perfectCount[p];
    if (!_stricmp(n, "great")) return (int)s->greatCount[p];
    if (!_stricmp(n, "good")) return (int)s->goodCount[p];
    if (!_stricmp(n, "bad")) return (int)s->badCount[p];
    if (!_stricmp(n, "miss")) return (int)s->missCount[p];
    if (!_stricmp(n, "totalstep"))   /* [+0x10]: linhas julgadas (aprox.: soma dos julgamentos) */
        return (int)(s->perfectCount[p] + s->greatCount[p] + s->goodCount[p] + s->badCount[p] + s->missCount[p]);
    if (!_stricmp(n, "hidden")) return m->hidden;
    if (!_stricmp(n, "allhidden")) return m->allHidden;
    if (!_stricmp(n, "maxcombo")) return (int)s->maxCombo[p];
    if (!_stricmp(n, "misscombo")) return (int)s->missCombo[p];
    if (!_stricmp(n, "score")) return (int)s->score[p];
    if (!_stricmp(n, "item")) return m->item;
    if (!_stricmp(n, "allitem")) return m->allItem;
    if (!_stricmp(n, "heart")) return m->heart;
    if (!_stricmp(n, "allheart")) return m->allHeart;
    if (!_stricmp(n, "mine")) return m->mine;
    if (!_stricmp(n, "allmine")) return m->allMine;
    if (!_stricmp(n, "potion")) return m->potion;
    if (!_stricmp(n, "allpotion")) return m->allPotion;
    if (!_stricmp(n, "velocity")) return m->velocity;
    if (!_stricmp(n, "allvelocity")) return m->allVelocity;
    *ok = false;
    Log_Print("unknown: %s\n", n);   /* 0x8055d75 */
    return 0;
}

static int s_cv[32], s_cvn, s_co[32], s_con;   /* valores / operadores (-1 = parêntese) */

static void condApply(void) {   /* 0x8055ff0 */
    if (s_cvn < 2 || s_con < 1) { if (s_con > 0) s_con--; return; }
    int r = s_cv[--s_cvn], l = s_cv[--s_cvn], o = s_co[--s_con], v = 0;
    switch (o) {
    case 0: v = l * r; break;
    case 1: v = r ? l / r : 0; break;
    case 2: v = r ? l % r : 0; break;
    case 3: v = l + r; break;
    case 4: v = l - r; break;
    case 5: v = l < r; break;
    case 6: v = l > r; break;
    case 7: v = l <= r; break;
    case 8: v = l >= r; break;
    case 9: v = l == r; break;
    case 10: v = l != r; break;
    case 11: v = l && r; break;
    case 12: v = l || r; break;
    }
    s_cv[s_cvn++] = v;
}

bool NxCond_Eval(const char* c, int p, int rank) {
    if (!c || !*c) return true;   /* 0x8055a8d */
    s_cvn = s_con = 0;
    while (*c) {
        if (*c == ' ' || *c == '\t') { c++; continue; }
        if (*c >= '0' && *c <= '9') {   /* número (0x804c2ec, base 10) */
            int v = 0;
            while (*c >= '0' && *c <= '9') v = v * 10 + (*c++ - '0');
            if (s_cvn < 32) s_cv[s_cvn++] = v;
            continue;
        }
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) {   /* identificador */
            char n[32]; int k = 0;
            while (((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) && k < 31) n[k++] = *c++;
            n[k] = 0;
            bool ok;
            int v = condVar(n, p, rank, &ok);
            if (s_cvn < 32) s_cv[s_cvn++] = v;
            continue;
        }
        if (*c == '(') { if (s_con < 32) s_co[s_con++] = -1; c++; continue; }
        if (*c == ')') {
            while (s_con > 0 && s_co[s_con - 1] != -1) condApply();
            if (s_con > 0) s_con--;
            c++;
            continue;
        }
        int o = -1;
        for (int k = 12; k >= 0; k--) {   /* operadores de 2 caracteres primeiro */
            size_t L = strlen(k_ops[k].op);
            if (L == 2 && strncmp(c, k_ops[k].op, 2) == 0) { o = k; break; }
        }
        if (o < 0) for (int k = 0; k < 13; k++) if (strlen(k_ops[k].op) == 1 && *c == k_ops[k].op[0]) { o = k; break; }
        if (o < 0) { c++; continue; }
        while (s_con > 0 && s_co[s_con - 1] != -1 && k_ops[s_co[s_con - 1]].prec <= k_ops[o].prec) condApply();
        if (s_con < 32) s_co[s_con++] = o;
        c += strlen(k_ops[o].op);
    }
    while (s_con > 0) condApply();
    return s_cvn > 0 ? s_cv[0] != 0 : true;
}

/* ===========================================================================
 * MISSIONCLEAR (CMissionClear, vtable 0x810fee8): BGA/CONQUEST.MOV sem loop e
 * EFF_CONQUEST (WAVE/CONQUEST.WAV, SFX_NEXTSTAGE.LUA). Depois o original mostra o
 * UNLOCK.DAT (cena "unlock") com a música liberada e acende as lâmpadas — não feito.
 * ========================================================================= */
static uint32_t s_clT0;
static int s_sndConq = -1;
static int s_clSong = -1;    /* música liberada (índice em g_exSongs) */
static int s_clPhase;        /* 0 vídeo, 1 "unlock" */
void NxText_DrawCentered(const char* str, int x, int y);

void NxClear_Enter(void) {
    BGM_Stop();
    Movie_Close();
    Resource_ClearBGA();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/BGA/CONQUEST.MOV", g_game.currentDirectory);
    if (!Movie_Open(path, false)) Log_Print("CLEAR: '%s' não abriu\n", path);
    if (s_sndConq < 0) s_sndConq = Audio_LoadWaveFile("CONQUEST.WAV");
    if (s_sndConq >= 0) Audio_Play(s_sndConq, false);
    s_clT0 = timeGetTime();
    s_clPhase = 0;
    s_clSong = NxClear_UnlockSong();
    if (s_clSong >= 0) Log_Print("CLEAR: libera %X %s\n", (unsigned)g_exSongs[s_clSong].id, g_exSongs[s_clSong].titleEn);
}

void NxClear_Update(float dt) {
    Movie_Update(dt);
    bool skip = false;
    for (int p = 0; p < 2; p++)
        if ((g_game.activePlayerMask & (1 << p)) && Input_IsPadHit(p, PAD_C) && timeGetTime() - s_clT0 > 1000) skip = true;
    if (s_clPhase == 0 && (!Movie_IsOpen() || Movie_HasEnded() || skip)) {
        Movie_Close();
        if (s_clSong >= 0 && Resource_LoadBGAByName("UNLOCK")) {   /* 0x807580a / 0x8075faa */
            BGA_Reset();
            BGA_SceneReset(0, "unlock");
            s_clPhase = 1;
            s_clT0 = timeGetTime();
            return;
        }
        Game_ChangeState(STATE_NAMEINPUT);
        return;
    }
    if (s_clPhase == 1 && ((BGA_SceneDone(0, "unlock") && timeGetTime() - s_clT0 > 3000) ||
                           (skip && timeGetTime() - s_clT0 > 1000))) {
        Resource_ClearBGA();
        Game_ChangeState(STATE_NAMEINPUT);
    }
}

void NxClear_Render(void) {
    if (Movie_IsOpen()) Movie_Render();
    if (s_clPhase == 1 && g_game.bgaPicCount > 0) {   /* "unlock" + título em (320, 185) (0x8075fc8) */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, 1);
        BGA_ScenePlay(0, "unlock", true);
        const ExceedSong* e = &g_exSongs[s_clSong];
        NxText_DrawCentered(g_game.svcLangOption == 0 ? e->titleKr : e->titleEn, 0x140, 0xb9);
    }
}
