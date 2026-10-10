#include "pumpy.h"
#include "vsl.h"
#include "bga.h"

#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0  /* dump de debug; alguns gl.h (Linux) so definem GL_BGR */
#endif

/* Declarado em main.c — reseta todos os cheats (ESC / Game Over) */
void Game_ResetAllCheats(void);

#define MAX_PANELS 10
#define PANEL_SIZE 30
/* Zero (piu 0x8083070 / 0x807eb20): setas 64x64, colunas de 49 px a partir de
 * x = 28 + 2 (P1), 348 + 2 (P2), 65 + 2 / 312 + 2 (Double), topo da seta em
 * y = 33 (378 + 5 no sistema Y para cima) -> centro 65. Receptor (01/02.spr)
 * deslocado (32, 42) da posição do .spr (0x807e820); W01/W02 em (70, 42). */
#define ZERO_RECEPTOR_Y 37   /* + rh2/2 (28) = 65, centro da seta no receptor (era 65: julgava 28 px abaixo) */
#define ZERO_ARROW_W    64
#define ZERO_COL_STEP   50.0f   /* 0x810098c (DAT_0811cea0 = coluna do painel) */
#define ZERO_REC_DY     42.0f
#define P1_CENTER_X 160
#define P2_CENTER_X 480

#define MISS_WINDOW 0.25f

/* Mantidos apenas para código de auto-play e range-checks de holds */
#define JUDGE_PERFECT  0.055f
#define JUDGE_GREAT    0.110f
#define JUDGE_GOOD     0.165f
#define JUDGE_BAD      0.220f

/* ── Janelas de timing (PUMPY.EXE: GameInit 0x00410cf0, recalculadas em GameUpdate 0x004127a0) ──
 * O original guarda cada janela em UNIDADES (60 por batida) como INTEIRO:
 *     win = (int)( k * (bpm * 0.00833333f) )
 * A constante é float32 (um pouco MENOR que 1/120) e o _ftol trunca, então em
 * BPM 120 exato 12 vira 11 unidades. As janelas são recalculadas a cada mudança
 * de BPM. Em segundos: win / bpm  (≈ k/120 s = 8,333 ms por unidade).
 *
 * SENTIDO DO DELTA (conferido): o original mede delta = posLinha - posAtual, ou
 * seja POSITIVO = nota ainda no futuro = aperto CEDO. Ele testa
 *     Perfect: -12b < delta < +7b   (Easy)
 * portanto a tolerância é MAIOR para TARDE (12) do que para CEDO (7). Provas: a
 * fórmula em 0x412944-0x41294e (rowPos + off - pos) e a passada de Miss em
 * 0x40eaac, que só olha as 10 linhas mais antigas e perde a linha com
 * delta < -Bad. (Antes este arquivo tinha early/late invertidos.)
 *   janela 0=Perfect, 1=Great, 2=Good, 3=Bad
 *   Easy  (0): cedo 7,12,17,22   tarde 12,17,22,27
 *   Normal(1): cedo 5,10,15,20   tarde 10,15,20,25
 *   Hard  (2): cedo 3, 8,13,18   tarde  8,13,18,23
 * No código: diff = songTime - rowTime (negativo = cedo). */
static const int k_unitEarly[3][4] = { { 7,12,17,22}, { 5,10,15,20}, { 3, 8,13,18} };
static const int k_unitLate [3][4] = { {12,17,22,27}, {10,15,20,25}, { 8,13,18,23} };

/* ── Janelas do Exceed (exceed.exe CPlayEngine::SetJudgeZone 0x404138) ──
 * Constantes float em 0x4492A0..0x4492E8, mesmas do src do Exceed
 * (playengine.cpp SetJudgeZone, snapshot 2003-12-29). Única chamada em
 * 0x4022EF: SetJudgeZone(1, BPM do 1º bloco) -> SEMPRE a tabela NORMAL,
 * independente da dificuldade, calculada UMA vez no início da música.
 * Zona em unidades de Y (60 por batida, PUMP_ARROW_Y):
 *     zona = k * (BPM0 / 120.0f)   (float, sem _ftol)
 * Teste estrito U < Y < D (Y > 0 = nota no futuro = cedo), então
 *     tarde = |U| = 7,12,17,22   cedo = D = 8,13,18,23
 * Y avança a BPM_atual*60/60 unidades por segundo -> segundos = zona / BPM_atual. */
static const int k_exUnitEarly[4] = { 8, 13, 18, 23 };
static const int k_exUnitLate [4] = { 7, 12, 17, 22 };
static double exJudgeBpm0(void); /* BPM do 1º bloco do chart (definido após g_chart) */

static void judgeWindows(int lvl, double bpm, double early[4], double late[4])
{
    const double f = (double)0.00833333f; /* mesma constante float32 do original */
    if (g_exceedSongIds) {
        /* era (Exceed): zonas em unidades de Y escaladas pelo BPM (k_exUnitEarly/Late *
         * BPM0/120 / BPM): no NX a janela crescia nos blocos lentos.
         * const float scale = (float)exJudgeBpm0() / 120.0f;
         * for (int j = 0; j < 4; j++) {
         *     early[j] = (double)((float)k_exUnitEarly[j] * scale) / bpm;
         *     late[j]  = (double)((float)k_exUnitLate [j] * scale) / bpm;
         * } */
        /* NX GetJudgeZone (piu 0x8070fd0 = NX2 player.cpp:194), em ms, fixo:
         *   perfect = 7 - nível*2 px (EASY 7, NORMAL 5, HARD 3), px -> ms = p/120*1000;
         *   Interval = Delay = 5 px = 41.667 ms (0x4226aaab), 4 regiões.
         * Julgamento (NX2 step.cpp:965): cedo usa |dt|, tarde usa dt - Delay;
         * região = (v - Perfect)/Interval + 1 -> PERFECT < P, GREAT < P+I, GOOD < P+2I,
         * BAD até MaxJudge = P+3I; o MISS vem depois de Delay + MaxJudge. */
        (void)bpm;
        const float P = (float)(7 - lvl * 2) / 120.0f * 1000.0f;
        const float I = 41.6666679f, D = 41.6666679f;
        for (int j = 0; j < 4; j++) {
            early[j] = (double)(P + I * (float)j) / 1000.0;
            late[j]  = (double)(P + I * (float)j + D) / 1000.0;
        }
        return;
    }
    for (int j = 0; j < 4; j++) {
        early[j] = (double)(int)((double)k_unitEarly[lvl][j] * (bpm * f)) / bpm;
        late[j]  = (double)(int)((double)k_unitLate [lvl][j] * (bpm * f)) / bpm;
    }
}

/* ── Lifebar — valores exatos do GameInit (Ghidra) ───────────────────────
 * DAT_00da2324 = 500    (vida inicial P1)
 * _DAT_00da2328 = 500   (speed inicial, modo normal)
 * DAT_00da2260 = 200    (speed mínimo)
 * DAT_00da22c0 = 1000   (speed máximo)
 * DAT_00da225c = -700   (penalidade de speed em MISS; /2 em BAD)
 * Danger threshold: vida < 180 → barra vermelha
 * Stage break: missCombo > 50 → game over imediato */
#define LIFE_INITIAL        500
static int s_nxGauge = -1;
static int s_nxGaugeBlink[2];  /* [0xa7f4a54 + p*4]: alterna a cada quadro */
/* Barra de life (PUMPY.EXE 0x411c6b-0x411c99): displayF = life * 0.001 - (1 - pulso) * k, limitado a [0,1],
 * com k = 0.1 nos modos simples e 0.05 em HalfDouble/Double/Nightmare. A barra cheia é life = 1000:
 * o life inicial (500) aparece como MEIA barra. Antes o port usava life/500 (barra cheia no início). */
#define LIFE_BAR_SCALE      0.001f
/* PUMPY.EXE compara life < 0xB4 (180) nos 4 pontos de desenho da barra (0x411ed2, 0x412102, 0x41223c, 0x41239f) */
#define LIFE_DANGER         334   /* source oficial DrawGauge: (int)(life/1000*33) <= 10 -> life <= 333 (era 180) */
/* Substituídos pelas tabelas k_lifeSpeedInit/Min/Max (ver applyLife): no
 * original estes três valores variam por nível de dificuldade, e fixá-los aqui
 * deixava NORMAL e HARD com a curva do EASY.
 * #define LIFE_SPEED_INIT     500
 * #define LIFE_SPEED_MIN      200
 * #define LIFE_SPEED_MAX      1000
 */
#define LIFE_SPEED_PENALTY  (-700)
#define STAGE_BREAK_MISSES  50
/* Escala visual: barra cheia = 252 pixels (original), mapeada como 252.0f */
#define LIFE_FULL_DISPLAY   252.0f

/* BASE_ROW_SPACING: no original, o espaçamento por row = 60.0 / beatSplit.
 * beatSplit=2 → 30 px/row; beatSplit=4 → 15 px/row.
 * Confirmado via Ghidra: fórmula original y = 376 - scrollSpeed*beatPos*(1/1000),
 * onde beatPos += 60.0/beatSplit por row, e scrollSpeed=1000 no x1.
 * Calculado dinamicamente como g_baseRowSpacing = 60.0f / g_baseBeatSplit. */
/* #define BASE_ROW_SPACING 18.0f */
#define MAX_VISIBLE_ROWS 512

typedef enum {
    JT_NONE = 0,
    JT_PERFECT,
    JT_GREAT,
    JT_GOOD,
    JT_BAD,
    JT_MISS
} JudgeType;

static const char* g_judgeNames[] = { "", "Perfect!", "Great", "Good", "Bad", "Miss" };
static const int g_judgeSpriteIndices[] = { -1, 7, 8, 9, 10, 11 }; // n1, n2, n3, n4, perfec, great_
static const float g_judgeColors[6][3] = {
    {0,0,0}, {0.6f,1.0f,1.0f}, {0.6f,1.0f,0.6f}, {1.0f,1.0f,0.2f}, {1.0f,0.4f,0.4f}, {1.0f,0.5f,1.0f}
};

typedef struct {
    int rowIndex;
    bool judged;
    JudgeType judgment;
    double hitTime;
} NoteHit;

static StepSong g_playSong;
static int g_chartIdx;
StepChart* g_chart;
static bool g_songLoaded;

static double g_songTime;
double Gameplay_SongTimeSec(void) { return g_songTime; }   /* NX kcal: [play+0xd2c8] em ms */
static unsigned s_exPrev[2][3];   /* contadores GOOD/BAD/MISS já pontuados (Gameplay_ExScoreSync) */
static double g_secondsPerRow;
static double g_totalSongSeconds;
static double g_chartDelay;
static int g_baseBeatSplit;
static double g_baseBpm;
static double* g_visualRow = NULL;
static int g_visualRowCount = 0;
static double g_maxSongTime;
static int g_stagnantFrames;
static uint32_t g_lastPosMs;
static int g_lastNoteRow;
static bool g_hasAudio;
static bool g_autoplay;
static bool g_autoPanel[10]; // per-panel autoplay: 0-4 P1, 5-9 P2
static float g_scrollSpeedX[2];      // velocidade atual por player
static float g_scrollSpeedTarget[2]; // target por player
static int   g_rvLastMeasure[2];     // última medida onde RV disparou, por player
static int   g_ewLastRow[2];         // última row vista pelo Earthworm (DAT_00da24bc)
static float g_stageBreakFreezeTimer = -1.0f; // >0: travado antes de ir p/ STATE_STAGE_BREAK
bool Gameplay_IsFrozen(void) { return g_stageBreakFreezeTimer >= 0.0f; }   /* tela parada (vídeo, BGA, setas) */

/* Aplica variação de vida para o julgamento dado (fórmulas exatas do Ghidra). */
/* Curva de lifeSpeed por nível de dificuldade — GameInit 0x00411381.
 *
 * O original escolhe o perfil por [0x00d39041] (o nível) e grava:
 *   inicial -> [0x00da2328] (P1) e [0x00da23c0] (P2)  = piVar16[0x18]
 *   mínimo  -> [0x00da2260]
 *   máximo  -> [0x00da22c0]
 *
 * Atenção ao que NÃO varia: a barra de vida em si (piVar16[0x17], gravada em
 * [0x00da2324]/[0x00da23bc]) começa sempre em 500, seja qual for o nível.
 * Só o lifeSpeed muda — e ele é o multiplicador de ganho, então um lifeSpeed
 * inicial menor faz a vida subir bem mais devagar no HARD.
 */
static const int k_lifeSpeedInit[3] = {  500, 300, 100 };  /* easy, normal, hard */
static const int k_lifeSpeedMin [3] = {  200, 100,   0 };
static const int k_lifeSpeedMax [3] = { 1000, 900, 800 };

static int lifeLevel(void)
{
    int lvl = g_game.optionDifficulty;
    if (lvl < 0) lvl = 0;
    if (lvl > 2) lvl = 2;
    return lvl;
}

static void applyLife(int player, JudgeType jt)
{
    int  lvl   = lifeLevel();
    int* life  = &g_game.stats.life[player];
    int* speed = &g_game.stats.lifeSpeed[player];
    switch (jt) {
        case JT_PERFECT:
            *life  += (*speed * 12) / 1000;
            *speed += 20;
            break;
        case JT_GREAT:
            *life  += (*speed * 10) / 1000;
            *speed += 16;
            break;
        case JT_GOOD:
            /* Sem mudança na vida — apenas quebra o missCombo */
            break;
        case JT_BAD:
            *life  -= 50;
            *speed += LIFE_SPEED_PENALTY / 2;   /* -= 350 */
            break;
        case JT_MISS:
            /* O original faz "life - (life*500)/2000 - 20" (0x0041042c região
             * do case 5), que em inteiro é "life - life/4 - 20". Escrito assim
             * de propósito: (life*3)/4 arredonda 1 unidade para baixo quando
             * life não é múltiplo de 4. */
            *life   = *life - (*life / 4) - 20;
            *speed += LIFE_SPEED_PENALTY;        /* -= 700 */
            break;
        default:
            break;
    }
    /* Exceed (0x409BC2..0x40A0CD): a vida em [+0x168] nunca é limitada —
     * pode ficar negativa e passar de 1000; só o desenho (0x40B084) faz clamp.
     * Os únicos outros acessos são os testes "< 1" do stage break (0x402C3D..). */
    if (!g_exceedSongIds && *life < 0) *life = 0;
    /* Clamp único no fim, como o original faz em Gameplay_ProcessJudgment:
     * testa os dois limites de uma vez depois do switch, não dentro de cada caso. */
    if (*speed < k_lifeSpeedMin[lvl]) *speed = k_lifeSpeedMin[lvl];
    if (*speed > k_lifeSpeedMax[lvl]) *speed = k_lifeSpeedMax[lvl];
}

/* ─── Diagnóstico temporário: holds fantasmas ───────────────────────────────
 * Registra toda abertura de hold com o caminho de código que a causou e a
 * distância temporal entre a row capturada e o tempo atual da música. Uma
 * abertura legítima tem |dt| dentro da janela de julgamento (~0.22s); qualquer
 * coisa muito fora disso é o hold que aparece do nada.
 * Remover quando o bug estiver fechado. */
static double getRowTime(int ri);
static void holdOpenDbg(const char* tag, int p, int panel, int ri)
{
    double rt = (g_chart && ri >= 0 && ri < (int)g_chart->rowCount) ? getRowTime(ri) : -999.0;
    double dt = g_songTime - rt;
    Log_Print("HOLD[%s] p=%d pan=%d row=%d dt=%+.3f t=%.2f%s\n",
              tag, p, panel, ri, dt, g_songTime,
              (rt < -900.0) ? "  <<< ROW FORA DO CHART"
                            : ((dt < -0.25 || dt > 0.25) ? "  <<< FORA DA JANELA" : ""));
}

static double getSegmentSpr(int seg)
{
    return 60.0 / ((double)g_chart->segments[seg].bpm * (double)g_chart->segments[seg].beatSplit);
}

static double getSegmentDelay(int seg)
{
    return g_chart->segments[seg].delay / (double)g_chart->delayDiv;
}

static double getRowTime(int ri)
{
    if (!g_chart) return ri * g_secondsPerRow + g_chartDelay;
    // Find which segment this row belongs to
    for (int s = g_chart->segmentCount - 1; s >= 0; s--)
    {
        if (ri >= (int)g_chart->segments[s].rowStart)
        {
            double accum = 0;
            for (int ps = 0; ps < s; ps++)
                accum += g_chart->segments[ps].rowCount * getSegmentSpr(ps) + getSegmentDelay(ps);
            accum += getSegmentDelay(s);
            return accum + (ri - g_chart->segments[s].rowStart) * getSegmentSpr(s);
        }
    }
    return ri * g_secondsPerRow + g_chartDelay;
}

// Compute block number and line-within-block for a given row
static int getBlockInfo(int ri, int* outLine) {
    if (!g_chart || ri < 0) { if (outLine) *outLine = 1; return 1; }
    // Walk through rows counting block boundaries
    // A block boundary occurs at: every rowsPerBlock rows, OR at each split (segment start mid-block)
    int block = 1;
    int lastSplitRow = 0; // row where current block started
    int row = 0;
    int segIdx = 0;
    
    // Collect all block boundary rows
    #define MAX_BOUNDARIES 2000
    static int boundaries[MAX_BOUNDARIES];
    int bc = 0;
    
    for (int s = 0; s < g_chart->segmentCount && bc < MAX_BOUNDARIES; s++)
    {
        int rpBlock = g_chart->segments[s].beatPerMeasure * g_chart->segments[s].beatSplit;
        int segStart = g_chart->segments[s].rowStart;
        int segEnd = segStart + g_chart->segments[s].rowCount;
        
        // If this segment starts mid-block (after a split), that's a block boundary
        if (s > 0 && bc < MAX_BOUNDARIES)
            boundaries[bc++] = segStart; // split creates new block
        
        // Normal block boundaries within this segment
        int firstBlockRow = segStart;
        if (s > 0) {
            int prevRPB = g_chart->segments[s-1].beatPerMeasure * g_chart->segments[s-1].beatSplit;
            firstBlockRow = ((segStart / prevRPB) + 1) * prevRPB;
            if (firstBlockRow < segStart) firstBlockRow = segStart;
        }
        for (int br = firstBlockRow + rpBlock; br < segEnd && bc < MAX_BOUNDARIES; br += rpBlock)
            boundaries[bc++] = br;
    }
    
    // Find which boundary segment our row is in
    int blockStart = 0;
    for (int b = 0; b < bc; b++) {
        if (ri < boundaries[b]) break;
        blockStart = boundaries[b];
        block++;
    }
    if (outLine) *outLine = ri - blockStart + 1;
    return block;
}

/* Division: notas especiais tiradas do chart na carga (W=2, G=3, A=4), por
 * jogador, em [linha*5 + coluna]. O julgamento normal nunca as vê: no original
 * elas não dão MISS nem judge (0x40f3a7..0x40f446), só explodem se pisadas. */
static uint8_t* g_divSpec[2];
static int g_divW[2], g_divG[2];   /* contadores: G = tipo 2 ([jog+0x2C]), W = tipo 3 ([jog+0x28]) */
/* Os contadores valem POR PÁGINA: nos 9 charts, o máximo de W pedido pelos ramos
 * de uma página é igual ao número de W da página anterior (ex.: 712 p1.2 tem 2 W
 * e p2.3 pede W 2-2; 736 p3.4 tem 4 W e p4.5 pede W 4-4). Acumulando, esses ramos
 * seriam inalcançáveis. O ponto do original que zera não foi localizado. */
static int g_divLastPage[2];

/* ---------------------------------------------------------------------------
 * NX WORLD TOUR: notas especiais da missão (tabela de atributos 0x8142fa0, por nota & 0x7f):
 *   0x0d  0x08 desenhada, não julgada (seta falsa)
 *   0x0e  0x21 julgada, invisível (hidden)
 *   0x0f  0x48 desenhada, não julgada (falsa)
 *   0x10  0x69 julgada e desenhada (hidden)
 *   0x14..0x28  0x0a itens: sprite item_<nome>.spr do BGA/MICON.DAT (0x8142f40,
 *               [+0xd300 + (nota-0x14)*4]); coletado ao pisar na janela (0x8070569 ->
 *               0x8071050: item++, score += (5 - julgamento) * 100 e a categoria de
 *               0x810f880: 0x1d heart, 0x19 mine, 0x24 potion, 0x1e/0x20..0x23 velocity)
 * As falsas e os itens saem da grade (o julgamento nunca as vê); as hidden ficam
 * como nota comum (1) e só deixam de ser desenhadas. Tabela [linha*10 + coluna]
 * (colunas 5..9 = metade 2). Efeitos dos itens (velocidade, dreno etc., 0x8071190)
 * ainda não feitos.
 * ------------------------------------------------------------------------- */
NxMisStats g_nxMis[2];
static uint8_t* g_misSpec;
static int g_misTrack;          /* [0xa7f4a60]: 0x11 ud (normal), 0x12 rd, 0x13 dd, 0x14 ld */
static int g_misFlash[2];       /* +0x4f0 do item Flash (wea) */
static int g_misWhite[2];       /* +0x4ec: tela branca da mina e das setas de pista, 50 quadros */
static int g_misSndMine = -1;   /* EFF_ITEM_MINE = WAVE/B09.WAV (SFX_GLOBAL.LUA) */
static int g_misItemSpr[21];   /* índice do 1º tile de item_<nome>.spr, -1 sem */
static bool g_misHiddenRow(int ri) {
    if (!g_misSpec) return false;
    for (int i = 0; i < 10; i++) { uint8_t s = g_misSpec[ri * 10 + i]; if (s == 0x0e || s == 0x10) return true; }
    return false;
}

static void misExtract(void)
{
    free(g_misSpec); g_misSpec = NULL;
    memset(g_nxMis, 0, sizeof(g_nxMis));
    /* era: g_misTrack = 0;
     * piu 0x806ad97 + 0x806ae14..0x806ae40: começa normal; quem tem UA (0x80) põe 0x13,
     * o mesmo valor do item dd (o item ud volta para 0x11 e desfaz o UA) */
    g_misTrack = (g_game.cmdUnderAttack[0] || g_game.cmdUnderAttack[1]) ? 0x13 : 0x11;
    g_misFlash[0] = g_misFlash[1] = 0;
    g_misWhite[0] = g_misWhite[1] = 0;
    if (g_misSndMine < 0) g_misSndMine = Audio_LoadWaveFile("B09.WAV");
    if (!g_chart || g_game.nxGameMode != 2) return;
    g_misSpec = (uint8_t*)calloc((size_t)g_chart->rowCount * 10, 1);
    if (!g_misSpec) return;
    int hiddenRows = 0, n[0x30] = { 0 };
    for (uint32_t r = 0; r < g_chart->rowCount; r++) {
        uint8_t* h[2] = { (uint8_t*)&g_chart->rows[r].half1, (uint8_t*)&g_chart->rows[r].half2 };
        bool hid = false;
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < 5; i++) {
                uint8_t v = (uint8_t)(h[k][i] & 0x7f);
                if (v < 0x0d || v > 0x28 || (v > 0x10 && v < 0x14)) continue;
                if (v == 0x1f) {   /* ran: item aleatório na carga (0x807273a, rand() % 10, tabela 0x810fa80) */
                    static const uint8_t k_ran[10] = { 0x18, 0x19, 0x24, 0x23, 0x1e, 0x20, 0x21, 0x22, 0x25, 0x27 };
                    v = k_ran[rand() % 10];   /* wea min pot 1x vel 3x 4x 8x ud dd */
                }
                g_misSpec[r * 10 + k * 5 + i] = v;
                n[v]++;
                if (v == 0x0e || v == 0x10) { h[k][i] = 1; hid = true; }
                else h[k][i] = 0;
            }
        if (hid) hiddenRows++;
    }
    /* totais (all*): o original soma ao coletar; aqui é o total do chart */
    for (int p = 0; p < 2; p++) {
        g_nxMis[p].allHidden = hiddenRows;
        for (int v = 0x14; v <= 0x28; v++) g_nxMis[p].allItem += n[v];
        g_nxMis[p].allHeart = n[0x1d];
        g_nxMis[p].allMine = n[0x19];
        g_nxMis[p].allPotion = n[0x24];
        g_nxMis[p].allVelocity = n[0x1e] + n[0x20] + n[0x21] + n[0x22] + n[0x23];
    }
    /* sprites dos itens (0x806a536) */
    static const char* const k_item[21] = { "act", "shi", "cha", "acc", "wea", "min", "min", "sma", "dra", "bon",
                                            "vel", "ran", "3x", "4x", "8x", "1x", "pot", "ud", "rd", "dd", "ld" };
    char path[MAX_PATH];
    for (int i = 0; i < 21; i++) g_misItemSpr[i] = -1;
    snprintf(path, sizeof(path), "%s/BGA/MICON.DAT", g_game.currentDirectory);
    if (RES_Open(path)) {
        for (int i = 0; i < 21; i++) {
            char nm[32];
            snprintf(nm, sizeof(nm), "item_%s.spr", k_item[i]);
            int start = g_game.sprTileCount;
            SPR_LoadSPR(nm, NULL, NULL, NULL);
            if (g_game.sprTileCount > start) g_misItemSpr[i] = start;
        }
        RES_Close();
    }
    Log_Print("MISSION: hidden %d linhas, itens %d (heart %d mine %d potion %d vel %d)\n", hiddenRows,
              g_nxMis[0].allItem, n[0x1d], n[0x19], n[0x24], g_nxMis[0].allVelocity);
}

/* 0x8071050: item pisado */
static void misCollect(int p, uint8_t v, JudgeType jt)
{
    NxMisStats* m = &g_nxMis[p];
    m->item++;
    int j = (jt == JT_PERFECT) ? 1 : (jt == JT_GREAT) ? 2 : (jt == JT_GOOD) ? 3 : 4;
    g_game.stats.score[p] += (5 - j) * 100;
    if (v == 0x1d) m->heart++;
    else if (v == 0x19) m->mine++;
    else if (v == 0x24) m->potion++;
    else if (v == 0x1e || (v >= 0x20 && v <= 0x23)) m->velocity++;

    /* 0x8071190: efeito pelo item, k = julgamento - 1 (PERFECT 0 .. BAD 3) */
    static const int k_mine[4] = { 490, 450, 400, 340 };   /* 0x810f908 */
    static const int k_pot[4]  = { 260, 240, 210, 170 };   /* 0x810f918 */
    int k = j - 1;
    int* life = &g_game.stats.life[p];
    switch (v) {
    case 0x18:   /* wea: Flash, +0x4f0 = (4 - k) * 120 (0x80711b3) */
        g_misFlash[p] = (4 - k) * 120;
        break;
    case 0x19:   /* min: bomba, vida -= tabela (0x80711ec) */
        *life -= k_mine[k];
        if (*life < 0) *life = 0;
        if (g_misSndMine >= 0) Audio_Play(g_misSndMine, false);
        g_misWhite[p] = 50;   /* 0x807121f: +0x4ec = 0x32 */
        break;
    case 0x1e: g_scrollSpeedTarget[p] = 2.0f; break;   /* vel: 2000 (0x807122c) */
    case 0x20: g_scrollSpeedTarget[p] = 3.0f; break;   /* 3x: 3000 */
    case 0x21: g_scrollSpeedTarget[p] = 4.0f; break;   /* 4x: 4000 */
    case 0x22: g_scrollSpeedTarget[p] = 8.0f; break;   /* 8x: 8000 */
    case 0x23: g_scrollSpeedTarget[p] = 1.0f; break;   /* 1x: 1000 */
    case 0x24:   /* pot: vida += tabela, até o máximo (0x80712d5) */
        *life += k_pot[k];
        if (*life > 1000) *life = 1000;
        break;
    case 0x25: case 0x26: case 0x27: case 0x28:   /* setas: direção da pista (0x8071303) */
        g_misTrack = v - 0x14;
        g_misWhite[p] = 50;   /* 0x8071308 -> 0x807121f: mesma tela branca, sem som */
        break;
    default: break;   /* heart e os outros: só contam */
    }
}

/* Tira W/G/A das linhas [r0, r1) do chart e guarda em g_divSpec. */
static void divExtractSpecials(uint32_t r0, uint32_t r1)
{
    if (!g_chart) return;
    for (int p = 0; p < 2; p++) {
        if (!g_divSpec[p]) continue;
        for (uint32_t r = r0; r < r1 && r < g_chart->rowCount; r++) {
            uint8_t* h1 = (uint8_t*)&g_chart->rows[r].half1;
            uint8_t* h2 = (uint8_t*)&g_chart->rows[r].half2;
            for (int i = 0; i < 5; i++) {
                uint8_t* src = (p == 0) ? h1 : h2;
                uint8_t v = src[i];
                g_divSpec[p][r * 5 + i] = (v == NT_DIV_W || v == NT_DIV_G || v == NT_DIV_A) ? v : 0;
            }
        }
    }
    for (uint32_t r = r0; r < r1 && r < g_chart->rowCount; r++) {
        uint8_t* h1 = (uint8_t*)&g_chart->rows[r].half1;
        uint8_t* h2 = (uint8_t*)&g_chart->rows[r].half2;
        for (int i = 0; i < 5; i++) {
            if (h1[i] == NT_DIV_W || h1[i] == NT_DIV_G || h1[i] == NT_DIV_A) h1[i] = 0;
            if (h2[i] == NT_DIV_W || h2[i] == NT_DIV_G || h2[i] == NT_DIV_A) h2[i] = 0;
        }
    }
}

/* Escolha do ramo da página seguinte — PUMPY.EXE 0x40f19a..0x40f39d: para cada
 * ramo existente, 10 faixas [mín,máx] (0 e 0 = ignorada) contra PERFECT, GREAT,
 * GOOD, BAD, MISS, W, G e mais 3 contadores; vence o ÚLTIMO ramo válido. Nos 9
 * charts de Division só W e G são usados. As linhas do ramo substituem a página
 * no chart tocável (todos os ramos de uma página têm o mesmo tamanho). */
static void divApplyBranch(int p, int hitRow)
{
    if (!g_chart || g_chart->divPageCount <= 0) return;
    int pg = -1;
    for (int i = 0; i < g_chart->divPageCount; i++) {
        uint32_t s = g_chart->divPages[i].rowStart, n = g_chart->divPages[i].rowCount;
        if ((uint32_t)hitRow >= s && (uint32_t)hitRow < s + n) { pg = i; break; }
    }
    int np = pg + 1;
    if (pg < 0 || np >= g_chart->divPageCount) return;
    int stat[10] = {
        (int)g_game.stats.perfectCount[p], (int)g_game.stats.greatCount[p],
        (int)g_game.stats.goodCount[p], (int)g_game.stats.badCount[p],
        (int)g_game.stats.missCount[p], g_divG[p], g_divW[p], 0, 0, 0 };  /* faixa 5 = tipo 2 (G), faixa 6 = tipo 3 (W) */
    int chosen = 0;
    for (int br = 0; br < g_chart->divPages[np].branchCount && br < 10; br++) {
        if (!g_chart->divPages[np].branchRows[br]) continue;
        const int32_t* c = g_chart->divPages[np].cond[br];
        bool ok = true;
        for (int q = 0; q < 10; q++) {
            int mn = c[2 * q], mx = c[2 * q + 1];
            if (mn == 0 && mx == 0) continue;
            if (q >= 7) { Log_Print("DIV: condicao %d desconhecida ignorada\n", q); continue; }
            if (stat[q] < mn || stat[q] > mx) { ok = false; break; }
        }
        if (ok) chosen = br;
    }
    uint32_t s = g_chart->divPages[np].rowStart, n = g_chart->divPages[np].rowCount;
    if (s + n <= g_chart->rowCount)
        memcpy(&g_chart->rows[s], g_chart->divPages[np].branchRows[chosen], n * sizeof(StepRow));
    divExtractSpecials(s, s + n);
    /* No Division as páginas são os segmentos, na mesma ordem (ramo 0 de cada
     * página foi emendado como segmento); a página leva a velocidade do ramo. */
    if ((uint32_t)np < g_chart->segmentCount)
        g_chart->segments[np].speed = g_chart->divPages[np].speed[chosen];
    Log_Print("DIV: P%d W=%d G=%d -> pagina %d ramo %d\n", p + 1, g_divW[p], g_divG[p], np, chosen);
}

static int getRowAtTime(double t)
{
    if (!g_chart) return 0;
    double accum = 0;
    for (int s = 0; s < g_chart->segmentCount; s++)
    {
        double segSpr = getSegmentSpr(s);
        double segDelay = getSegmentDelay(s);
        double segDur = g_chart->segments[s].rowCount * segSpr + segDelay;
        if (t < accum + segDur || s == g_chart->segmentCount - 1)
        {
            double tInSeg = t - accum - segDelay;
            if (tInSeg < 0) tInSeg = 0;
            int ri = g_chart->segments[s].rowStart + (int)(tInSeg / segSpr);
            if (ri < 0) ri = 0;
            if (ri >= (int)g_chart->rowCount) ri = g_chart->rowCount - 1;
            return ri;
        }
        accum += segDur;
    }
    return (int)g_chart->rowCount - 1;
}

static double getRowAtTimeFloat(double t);
/* Quadro da animacao da seta (0..5). PUMPY.EXE 0x412905..0x412930:
 * [0xda24c4] = (fase % 60) / 10, onde a fase e a posicao em 1/60 de batida
 * (acumulador de 60/beatSplit por linha em 0x4127a0) -> 6 quadros por batida,
 * acompanhando o BPM. Antes o port usava (frameCounter / 3) % 6 (20 fps fixos). */
static int arrowAnimFrame(void)
{
    if (!g_chart || g_chart->segmentCount == 0) return 0;
    double row = getRowAtTimeFloat(g_songTime);
    double beats = 0.0;
    for (uint32_t s = 0; s < g_chart->segmentCount; s++) {
        double split = (double)(g_chart->segments[s].beatSplit ? g_chart->segments[s].beatSplit : 1);
        double start = (double)g_chart->segments[s].rowStart;
        double cnt   = (double)g_chart->segments[s].rowCount;
        if (row < start + cnt || s == g_chart->segmentCount - 1) {
            double r = row - start; if (r < 0.0) r = 0.0;
            beats += r / split;
            break;
        }
        beats += cnt / split;
    }
    int phase = (int)(beats * 60.0) % 60;
    if (phase < 0) phase += 60;
    return phase / 10;
}

/* Delay de bloco (Stop n' Go / freeze; Zero piu 0x8086170 / 0x80863d0 / carga 0x80935xx):
 *   flag (+100) == 1 e delay > 0 -> STOP: a posição não avança por delay x 10 ms
 *                                   (aqui: getRowAtTimeFloat fica na 1ª linha do bloco);
 *   flag == 0 (ou delay < 0)     -> o delay vira distância no scroll (delay*10 * BPM/1000
 *                                   batidas): as setas seguem andando e abre um vão.
 * Devolve o vão em linhas visuais (unidade de g_visualRow = divisão do bloco 0). */
static double g_clkAnchor;          /* relógio do gameplay: âncora congelada */
static bool   g_clkHave, g_clkLocked;

static double zeroDelayGapRows(int s)
{
    if (!g_chart || s < 0 || s >= (int)g_chart->segmentCount) return 0.0;
    int32_t d = g_chart->segments[s].delay;
    if (d == 0) return 0.0;
    if (g_chart->segments[s].stopFlag != 0 && d > 0) return 0.0;   /* Stop (ou lixo: sem vão) */
    /* NX (.SEE, delay em ms): delay positivo = Stop mesmo com a flag 0.
     * Evidência (dados, sem assembly): D08 Free! seção 3 bloco 1 dl=859 flag=1 e
     * seção 4 (CRAZY) bloco 1 dl=860 flag=0 — o mesmo Stop nas duas. */
    /* Exceção: o delay do PRIMEIRO bloco sem a flag de Stop é só o offset/lag
     * do início do step (ex.: 312 Don't Bother Me HARD, dl=2230, 1ª nota na
     * linha 0) -> vira linhas em branco e as setas entram deslizando.
     * era: if (g_chart->delayDiv == 1000 && d > 0) return 0.0; */
    if (g_chart->delayDiv == 1000 && d > 0 && s > 0) return 0.0;
    double beats = (d / (double)(g_chart->delayDiv > 0 ? g_chart->delayDiv : 100)) * (double)g_chart->segments[s].bpm / 60.0;
    /* era: (d / 100.0) — unidade da ZERO; no .SEE dava 10x o vão */
    return beats * (double)(g_baseBeatSplit > 0 ? g_baseBeatSplit : 4);
}

/* Posição visual durante o delay de um bloco com vão: anda de (início - vão) até o
 * início do bloco. Fora disso devolve 'fallback' (interpolação normal). */
static double zeroVisualScrollInDelay(double t, double fallback)
{
    if (!g_chart || !g_visualRow) return fallback;
    double accum = 0;
    for (int s = 0; s < (int)g_chart->segmentCount; s++) {
        double segDelay = getSegmentDelay(s);
        if (segDelay > 0 && t >= accum && t < accum + segDelay) {
            double gap = zeroDelayGapRows(s);
            int rs = (int)g_chart->segments[s].rowStart;
            if (gap <= 0 || rs >= g_visualRowCount) return fallback;
            return g_visualRow[rs] - gap + gap * ((t - accum) / segDelay);
        }
        accum += g_chart->segments[s].rowCount * getSegmentSpr(s) + segDelay;
    }
    return fallback;
}

static double getRowAtTimeFloat(double t)
{
    if (!g_chart) return t / g_secondsPerRow;
    double accum = 0;
    for (int s = 0; s < g_chart->segmentCount; s++)
    {
        double segSpr = getSegmentSpr(s);
        double segDelay = getSegmentDelay(s);
        double segDur = g_chart->segments[s].rowCount * segSpr + segDelay;
        if (t < accum + segDur || s == g_chart->segmentCount - 1)
        {
            double tInSeg = t - accum - segDelay;
            if (tInSeg < 0) tInSeg = 0;
            double ri = (double)g_chart->segments[s].rowStart + tInSeg / segSpr;
            if (ri < 0) ri = 0;
            if (ri >= (double)g_chart->rowCount) ri = (double)g_chart->rowCount - 1;
            return ri;
        }
        accum += segDur;
    }
    return (double)g_chart->rowCount - 1;
}

/* ---------------------------------------------------------------------------
 * Modificadores da NX na pista (piu 0x806ecf0 / 0x806cae8)
 * ------------------------------------------------------------------------- */
/* AC/DC (0x806f079..0x806f4a3): d = distância em px até o receptor (positiva
 * abaixo dele). DC: d^3/1600. AC: d >= -83 -> 120000*(0.005 - 1/(2.4d + 200)). */
static float nxAccelDist(int p, float d)
{
    if (g_game.cmdDecel[p]) return d * d * d / 1600.0f;
    if (g_game.cmdAccel[p] && d >= -83.0f) return 120000.0f * (0.005f - 1.0f / (d * 2.4f + 200.0f));
    return d;
}
/* Inversa de nxAccelDist: distância crua a partir da já curvada. O X-MODE do piu
 * desloca pela distância ANTES da curva (0x806e9d0: play+0x103cc, gravado em
 * 0x806f071; a curvada vai para +0x103c8 em 0x806f0ae) — igual ao NX2 (x = y,
 * scr_y = CalcScreenY(y)). */
static float nxAccelInv(int p, float s)
{
    if (g_game.cmdDecel[p]) return cbrtf(s * 1600.0f);
    if (g_game.cmdAccel[p]) {
        float lim = 120000.0f * (0.005f - 1.0f / (-83.0f * 2.4f + 200.0f));
        if (s >= lim) return (1.0f / (0.005f - s / 120000.0f) - 200.0f) / 2.4f;
    }
    return s;
}

/* FL (0x806cf97 + 0x806f4d5): contador +0x4f0 cai 1 por quadro;
 * alpha das setas = 0.5 + 0.5*sin(c*0.25) */
static int g_flashCnt[2];

/* NX / UA (0x806cae8..0x806d6ab): câmera em volta de receptores e setas.
 * 0x808fc30(75): gluPerspective(75, 640/480, 0.1, 5000) + Translate(-320,-240,0)
 * na projeção e LookAt(0,0,d) com d = 240*cot(37.5); depois Rotate(-60,1,0,0) e
 * escala 1.5 em torno de (320,240) (Y -1.5 junto com UA). UA: Translate(640,480)
 * + Rotate(180,0,0,1) = giro de 180 graus no centro da tela. */
static int g_nxField;   /* 0 nada, 1 só modelview, 2 projeção + modelview */
static void nxFieldBegin(void)
{
    bool nx = g_game.cmdNXMode[0] || g_game.cmdNXMode[1];
    /* era: bool ua = cmdUnderAttack[0] || [1]; if (g_misTrack == 0x13) ua = !ua; */
    /* [0xa7f4a60]: 0x13 = 180 graus (UA ou item dd), rd = Translate(160,560) +
     * Rotate(-90), ld = Translate(480,-80) + Rotate(90) */
    bool ua = (g_misTrack == 0x13);
    bool side = (g_misTrack == 0x12 || g_misTrack == 0x14);
    g_nxField = 0;
    if (!nx && !ua && !side) return;
    if (nx) {
        const double fov = 75.0, n = 0.1, f = 5000.0;
        double t = n * tan(fov * 0.5 * 3.14159265358979 / 180.0);
        double d = 240.0 / tan(fov * 0.5 * 3.14159265358979 / 180.0);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glFrustum(-t * 640.0 / 480.0, t * 640.0 / 480.0, -t, t, n, f);
        glTranslatef(-320.0f, -240.0f, 0.0f);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, (float)-d);   /* = gluLookAt(0,0,d, 0,0,0, 0,1,0) */
        glRotatef(-60.0f, 1.0f, 0.0f, 0.0f);
        glTranslatef(320.0f, 240.0f, 0.0f);
        glScalef(1.5f, ua ? -1.5f : 1.5f, 1.5f);
        glTranslatef(-320.0f, -240.0f, 0.0f);
        g_nxField = 2;
    } else {
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        g_nxField = 1;
    }
    if (ua) {
        glTranslatef(640.0f, 480.0f, 0.0f);
        glRotatef(180.0f, 0.0f, 0.0f, 1.0f);
    }
    if (g_misTrack == 0x12) { glTranslatef(160.0f, 560.0f, 0.0f); glRotatef(-90.0f, 0.0f, 0.0f, 1.0f); }   /* 0x806d50a */
    if (g_misTrack == 0x14) { glTranslatef(480.0f, -80.0f, 0.0f); glRotatef(90.0f, 0.0f, 0.0f, 1.0f); }    /* 0x806d566 */
}
static void nxFieldEnd(void)
{
    if (g_nxField == 0) return;
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    if (g_nxField == 2) {
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
    g_nxField = 0;
}

static NoteHit g_noteHits[2][MAX_PANELS][2048];
static int g_noteHitCount[2][MAX_PANELS];
static int g_nextNoteRow[2][MAX_PANELS];

// Hold tracking: which rows have active hold heads per player/panel
static int g_holdRows[2][MAX_PANELS]; // row index of active hold (-1 = none)

static float g_judgeDisplayTimer[2];
static JudgeType g_judgeDisplayType[2];
static int g_judgeDisplayCombo[2];
static int g_judgeFrame[2]; // frame counter 25->0 for judge animation
/* Exceed2 (PIU32.EXE 0x407400): contador do julgamento por jogador ([+0x1D4]),
 * zerado a cada julgamento novo e +1 por quadro; cena desenhada enquanto < 50.
 * O julgamento e o combo são cenas do BGA/00.DAT (BGA3, [0x484FD8]). */
static int g_exJudgeCnt[2];
static int g_exJudgeBga = -1;   /* índice em g_game.bgaPics do 00.BGA, -1 = sem */

/* Exceed2 (PIU32.EXE 0x4043A1..0x404655): setas das notas vêm do BGA\SKIN0X.DAT.
 * [0x484F7C] & 0x10000 -> SKIN02, & 0x20000 -> SKIN01, senão SKIN00.
 * Por painel (DL UL C UR DR): skinN.spr = nota (6 quadros), skinN_l1 = cabeça do
 * hold, skinN_l2 = corpo, skinN_l3 = ponta. Ajuste em X por skin e painel em
 * [0x46AFF0..0x46B000], aplicado em 0x406AEC (índice = painel). -1 = sem skin:
 * volta para o ARROW54X / ARROWETC do 00.DAT. */
static bool  g_zeroSkinArrows;   /* ARROW54x apontando para a skin (Zero) */
static int   g_skinTap[5] = { -1, -1, -1, -1, -1 };
static int   g_skinL1[5]  = { -1, -1, -1, -1, -1 };
static int   g_skinL2[5]  = { -1, -1, -1, -1, -1 };
static int   g_skinL3[5]  = { -1, -1, -1, -1, -1 };
static float g_skinOffX[5];
static float g_skinOffY;
static int   g_skinArrowP = -1;   /* Zero [+0xb904]: arrowp.spr da skin */
static int   g_skinSpark[5] = { -1, -1, -1, -1, -1 };   /* Zero [+0xf44..]: spark1..5.spr */          /* Zero [0x0862825c] (Y para cima) */
static float g_skinFieldX[2];     /* Zero [0x08628260] campo P1/single, [0x08628264] campo P2 */

static void exLoadSkin(int sk)   /* era: (void), sk = Zero_SkinIndex() */
{
    static const float k_off[3][5] = {
        /* Zero 0x8080a40..: [0x08628248..58] = 2, 0, -2, -1, -3 (SKIN00);
         * a ordem dos painéis desses cinco ainda não foi confirmada. */
        { 2.0f, 1.0f, 0.0f, 0.0f, 0.0f },    /* SKIN00: 0x4044D4 (2, 1, 0, 0, 0) */
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },    /* SKIN01: 0x4044AF */
        { 3.0f, 3.0f, 1.0f, -3.0f, -3.0f },  /* SKIN02: 0x4043C0 */
    };
    /* Zero 0x80806f0: deslocamento de cada coluna (DL UL C UR DR = [0x08628248..58]),
     * Y [0x0862825c] e ajuste do campo P1 [0x08628260] / P2 [0x08628264] por skin. */
    static const struct { float x[5], y, f1, f2; } k_zero[8] = {
        { {  2,  0, -2, -1, -3 },  0, 0, 0.5f },   /* SKIN00 */
        { {  1,  0,  0,  2,  0 },  0, 0, 0    },   /* SKIN01 */
        { {  3,  2,  0, -2, -3 },  0, 0, 0    },   /* SKIN02 */
        { {  1, -1, -2, -4, -6 }, -3, 3, 8    },   /* SKIN03 */
        { {  6,  2,  1,  0, -2 },  0, 0, 4    },   /* SKIN04 */
        { {  2,  1,  0,  0,  0 }, -1, 0, 4    },   /* SKIN05 */
        { { -2, -4, -5, -5, -6 }, -4, 0, 9    },   /* SKIN06 */
        { {  2,  1,  0,  0,  0 },  0, -1, 0   },   /* SKIN07 */
    };
    /* NX (piu 0xc7140, {char nome[16]; float x[5], y, campoP1, campoP2}); SKIN09..12
     * da tabela não existem na NX (só na NX2) e ficaram de fora.
     * SKIN01..07 = os mesmos valores do Zero; SKIN00 e SKIN08 diferem.
     * Os campos P1/P2 batem com o x_start_interpolate da NX2 (SKIN00 = {0, 1}).
     * Antes: k_zero[sk & 7] — o SKIN08 (skin padrão da NX) usava o SKIN00 do Zero. */
    static const struct { float x[5], y, f1, f2; } k_nx[9] = {
        { {  2,  0, -1,  1, -1 }, -1,  0, 1 },   /* SKIN00 */
        { {  1,  0,  0,  2,  0 },  0,  0, 0 },   /* SKIN01 */
        { {  3,  2,  0, -2, -3 },  0,  0, 0 },   /* SKIN02 */
        { {  1, -1, -2, -4, -6 }, -3,  3, 8 },   /* SKIN03 */
        { {  6,  2,  1,  0, -2 },  0,  0, 4 },   /* SKIN04 */
        { {  2,  1,  0,  0,  0 }, -1,  0, 4 },   /* SKIN05 */
        { { -2, -4, -5, -5, -6 }, -4,  0, 9 },   /* SKIN06 */
        { {  2,  1,  0,  0,  0 },  0, -1, 0 },   /* SKIN07 */
        { {  2,  0, -1,  1, -1 }, -1,  0, 1 },   /* SKIN08 (NX) */
    };
    (void)k_zero;   /* tabela do Zero mantida como referência */
    for (int k = 0; k < 5; k++) g_skinTap[k] = g_skinL1[k] = g_skinL2[k] = g_skinL3[k] = -1;
    g_skinArrowP = -1;
    /* Exceed2: unsigned fl = ExSelect_GetFlags(); sk = 0x10000 ? 2 : 0x20000 ? 1 : 0 */
    /* int sk = Zero_SkinIndex();  (agora vem do jogador: NX2 m_CurrentSkin) */
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/BGA/SKIN%02d.DAT", g_game.currentDirectory, sk);
    if (!RES_Open(path)) { Log_Print("GP: skin '%s' não abriu\n", path); return; }
    for (int k = 0; k < 5; k++) {
        char nm[32];
        int* dst[4] = { &g_skinTap[k], &g_skinL1[k], &g_skinL2[k], &g_skinL3[k] };
        static const char* suf[4] = { "", "_l1", "_l2", "_l3" };
        for (int j = 0; j < 4; j++) {
            snprintf(nm, sizeof(nm), "skin%d%s.spr", k + 1, suf[j]);
            int start = g_game.sprTileCount;
            SPR_LoadSPR(nm, NULL, NULL, NULL);
            *dst[j] = (g_game.sprTileCount > start) ? start : -1;
        }
        /* Exceed2: g_skinOffX[k] = k_off[sk][k]; */
        g_skinOffX[k] = k_nx[(sk >= 0 && sk < 9) ? sk : 8].x[k];
    }
    /* 0x80806f0: arrowp.spr (efeito de pisar, 5 tiles: DL UL C UR DR) */
    {
        int start = g_game.sprTileCount;
        SPR_LoadSPR("arrowp.spr", NULL, NULL, NULL);
        g_skinArrowP = (g_game.sprTileCount >= start + 5) ? start : -1;
    }
    /* 0x80806f0: spark1..5.spr (5 quadros de 256x256) */
    for (int k = 0; k < 5; k++) {
        char nm[16];
        snprintf(nm, sizeof(nm), "spark%d.spr", k + 1);
        int start = g_game.sprTileCount;
        SPR_LoadSPR(nm, NULL, NULL, NULL);
        g_skinSpark[k] = (g_game.sprTileCount > start) ? start : -1;
    }
    g_skinOffY = k_nx[(sk >= 0 && sk < 9) ? sk : 8].y;
    g_skinFieldX[0] = k_nx[(sk >= 0 && sk < 9) ? sk : 8].f1;
    g_skinFieldX[1] = k_nx[(sk >= 0 && sk < 9) ? sk : 8].f2;
    RES_Close();
    Log_Print("GP: skin SKIN%02d carregada (nota DL=%d)\n", sk, g_skinTap[0]);
}
/* DESATIVADO: versão anterior (Zero 0x8087520), trocada pelo DrawLongNote da NX2.
/* Zero 0x8087520: corpo skinN_l2 esticado (UV inteiro) da base da cabeça ao topo
 * da ponta; ponta skinN_l3 inteira; distância < 64 -> só a parte de baixo da
 * ponta (0x809e0d0). yh/yt = centros das setas (Y para baixo). * /
static void zeroHoldDrawOld(int panel, int col, float left, float yh, float yt, bool held)
{
    (void)panel;
    if (g_skinL2[col] < 0 || g_skinL3[col] < 0) return;
    SPRTileDef* bt = &g_game.sprTiles[g_skinL2[col]];
    SPRTileDef* tt = &g_game.sprTiles[g_skinL3[col]];
    int bw = Texture_GetWidth(bt->texId); if (bw <= 0) bw = 256;
    int bh = Texture_GetHeight(bt->texId); if (bh <= 0) bh = 256;
    int tw = Texture_GetWidth(tt->texId); if (tw <= 0) tw = 256;
    int th = Texture_GetHeight(tt->texId); if (th <= 0) th = 256;
    /* segurado (cabeça já passou, 0xC sozinho em 0x8087520): o corpo sai do MEIO
     * da step zone (y = 32 da seta do receptor), não da base da cabeça * /
    /* era: headBase = held ? yh : yh + 32. A cabeça (skinN_l1) tem 62-63 px a
     * partir de y = 1 na célula de 64, então o corpo começando na base exata
     * deixava 1 px de fresta (a "linha preta" sob a cabeça). Começa 2 px antes,
     * por baixo da cabeça, que é desenhada depois. * /
    float headBase = held ? yh : yh + 30.0f, tailTop = yt - 32.0f, tailBase = yt + 32.0f;
    /* Deslocamento X do próprio tile no .spr ("T tex x y w h ..."): skin4_l2/skin5_l2
     * têm x = 1 e a ponta x = 0; sem ele corpo e ponta ficavam 1 px desalinhados.
     * era: corpo e ponta em 'left' sem o deslocamento. * /
    float bodyL = left + (float)bt->srcX, tailL = left + (float)tt->srcX;
    if (tailTop > headBase) {
        Texture_DrawUV(bt->texId, bodyL, headBase, (float)bt->srcW, tailTop - headBase,
                       bt->u1 * bw, bt->v1 * bh, bt->u2 * bw, bt->v2 * bh, 1, 1, 1, 1);
        Texture_DrawUV(tt->texId, tailL, tailTop, (float)tt->srcW, (float)tt->srcH,
                       tt->u1 * tw, tt->v1 * th, tt->u2 * tw, tt->v2 * th, 1, 1, 1, 1);
    } else if (tailBase > headBase) {
        float frac = (tailBase - headBase) / 64.0f;
        float vTop = tt->v2 - frac * (tt->v2 - tt->v1);
        Texture_DrawUV(tt->texId, tailL, headBase, (float)tt->srcW, tailBase - headBase,
                       tt->u1 * tw, vTop * th, tt->u2 * tw, tt->v2 * th, 1, 1, 1, 1);
    }
}
*/
/* NX2 DrawLongNote (playengine.cpp:2603), linha por linha. O original desenha em
 * Y para cima com a origem na LINHA da ponta (base da célula de 64 da seta):
 *   Translate(x + 65*seta, ScrY); height = y(cabeça) - ScrY;
 *   height > 62:
 *     LongMiddle.DrawPicYY(estado, 63 + vtxY(l2), height + 1 + vtxY(l2))
 *       -> corpo com o UV inteiro do tile, do topo da ponta até 1 px dentro
 *          da cabeça; v1 do tile no lado da cabeça (DrawPicYY: texY1 em y2);
 *     LongEnd.DrawPic(estado)                      -> ponta no próprio retângulo;
 *   senão:
 *     LongEnd.DrawPicYY_UV(estado, 0 + vtxY(l3), height + 1 + vtxY(l3),
 *                          (height + 1) / 63, 0)
 *       -> só a parte de baixo da ponta: v de v2 - len*(height+1)/63 até v2;
 *   Translate(0, height); LongStart.DrawPic(estado) -> cabeça por cima.
 * Segurado (bPress e a linha já passou): y = STEP_Y (cabeça no receptor).
 * Aqui (Y para baixo): yh/yt = centros das células da cabeça e da ponta; a
 * linha (origem) de cada uma fica em centro + 32. Y para cima local v vira
 * tela = linha - v. X: 'left' = coluna + ajuste da skin; cada tile soma o
 * próprio x do .spr (rcVtx[X1]). */
static void zeroHoldDraw(int panel, int col, float left, float yh, float yt, bool held)
{
    (void)panel; (void)held;   /* segurado: o chamador já passa yh = receptor */
    if (g_skinL1[col] < 0 || g_skinL2[col] < 0 || g_skinL3[col] < 0) return;
    int st = arrowAnimFrame();   /* m_ArrowState: o mesmo quadro nas três peças */
    int iL1 = g_skinL1[col] + st, iL2 = g_skinL2[col] + st, iL3 = g_skinL3[col] + st;
    if (iL1 >= g_game.sprTileCount) iL1 = g_skinL1[col];
    if (iL2 >= g_game.sprTileCount) iL2 = g_skinL2[col];
    if (iL3 >= g_game.sprTileCount) iL3 = g_skinL3[col];
    SPRTileDef* hs = &g_game.sprTiles[iL1];   /* LongStart  (skinN_l1) */
    SPRTileDef* bt = &g_game.sprTiles[iL2];   /* LongMiddle (skinN_l2) */
    SPRTileDef* tt = &g_game.sprTiles[iL3];   /* LongEnd    (skinN_l3) */
    int hw = Texture_GetWidth(hs->texId); if (hw <= 0) hw = 256;
    int hh = Texture_GetHeight(hs->texId); if (hh <= 0) hh = 256;
    int bw = Texture_GetWidth(bt->texId); if (bw <= 0) bw = 256;
    int bh = Texture_GetHeight(bt->texId); if (bh <= 0) bh = 256;
    int tw = Texture_GetWidth(tt->texId); if (tw <= 0) tw = 256;
    int th = Texture_GetHeight(tt->texId); if (th <= 0) th = 256;

    float lineT = yt + 32.0f;            /* ScrY: linha da ponta */
    float height = (yt - yh);            /* y - ScrY (linha da cabeça - linha da ponta) */
    if (height < 0.0f) height = 0.0f;

    if (height > 62.0f) {
        /* LongMiddle.DrawPicYY(st, 63 + vtxY, height + 1 + vtxY) */
        float yLo = 63.0f + (float)bt->srcY, yHi = height + 1.0f + (float)bt->srcY;
        Texture_DrawUV(bt->texId, left + (float)bt->srcX, lineT - yHi, (float)bt->srcW, yHi - yLo,
                       bt->u1 * bw, bt->v1 * bh, bt->u2 * bw, bt->v2 * bh, 1, 1, 1, 1);
        /* LongEnd.DrawPic(st) */
        Texture_DrawUV(tt->texId, left + (float)tt->srcX, lineT - (float)(tt->srcY + tt->srcH),
                       (float)tt->srcW, (float)tt->srcH,
                       tt->u1 * tw, tt->v1 * th, tt->u2 * tw, tt->v2 * th, 1, 1, 1, 1);
    } else {
        /* LongEnd.DrawPicYY_UV(st, 0 + vtxY, height + 1 + vtxY, (height + 1) / 63, 0) */
        float yLo = (float)tt->srcY, yHi = height + 1.0f + (float)tt->srcY;
        float len = tt->v2 - tt->v1;
        float vTop = tt->v2 - len * ((height + 1.0f) / 63.0f);
        Texture_DrawUV(tt->texId, left + (float)tt->srcX, lineT - yHi, (float)tt->srcW, yHi - yLo,
                       tt->u1 * tw, vTop * th, tt->u2 * tw, tt->v2 * th, 1, 1, 1, 1);
    }
    /* Translate(0, height); LongStart.DrawPic(st) */
    float lineH = lineT - height;
    Texture_DrawUV(hs->texId, left + (float)hs->srcX, lineH - (float)(hs->srcY + hs->srcH),
                   (float)hs->srcW, (float)hs->srcH,
                   hs->u1 * hw, hs->v1 * hh, hs->u2 * hw, hs->v2 * hh, 1, 1, 1, 1);
}

static int g_hitTimer[2][MAX_PANELS]; // hit flash animation timer (p1)
static int g_glowTimer[2][MAX_PANELS];    // glow aditivo: apenas PERFECT/GREAT
static int g_p1FlashTimer[2][MAX_PANELS]; // tile p1: zoom+fade ao pressionar

// Maquina de estados da nota
static int g_noteState[2][MAX_PANELS]; // 0=normal, 1=exploding, 2=dead
static int g_noteExplodeRow[2][MAX_PANELS]; // row index for clearing when dead
static int g_noteExplodeFrame[2][MAX_PANELS]; // explosion frame counter 0..15
static void holdHitFx(int player, int pan, int row);
static bool rowOnlyLong(int player, int row);
static void nxLoadSkins(void);
static int g_blindTimer[2];
static int g_prevBlindRow;
static int g_lastPerfectRow[2][MAX_PANELS];

// Pop-up de score (catch effect)
#define MAX_POPUPS 32
static struct {
    int score;       // valor do score (+1000, +500)
    int combo;       // combo atual
    float y;         // posicao Y atual (sobe)
    float alpha;     // fade out
    bool active;
    int player;
} g_popups[MAX_POPUPS];

// Linhas com multiplas setas aguardando julgamento (ate BAD window expirar)
#define MAX_PENDING 32
static struct {
    int row;
    double deadline;
    int totalMask;    // bits 0-4: setas que EXISTEM na linha
    int hitMask;      // bits 0-4: setas que ja foram pressionadas
    float worstDiff;
    bool active;
} g_pending[MAX_PENDING];
static int g_pendingCount;

// Conta tiles consecutivos de um SPR pelo nome (ex: 01.spr_0, 01.spr_1 = 2)
static int sprTileCount(int startIdx) {
    if (startIdx < 0 || startIdx >= g_game.sprTileCount) return 0;
    const char* name = g_game.sprTiles[startIdx].name;
    const char* us = strrchr(name, '_');
    if (!us) return 1;
    char prefix[64];
    int plen = (int)(us - name);
    if (plen > 63) plen = 63;
    memcpy(prefix, name, plen);
    prefix[plen] = '\0';
    int c = 0;
    while (startIdx + c < g_game.sprTileCount) {
        char exp[64];
        snprintf(exp, sizeof(exp), "%s_%d", prefix, c);
        if (_stricmp(g_game.sprTiles[startIdx + c].name, exp) != 0) break;
        c++;
    }
    return c;
}

/* NX2 (nx2src playengine.cpp:1330): cada CPlayer é um CStep e carrega o próprio
 * STEP/<id>/<modo>.NX (m_StepMode[a]); só o relógio da música é comum. Aqui o
 * chart e o que deriva dele ficam num contexto por jogador, trocado (ctxUse)
 * antes de julgar/desenhar cada um. Com um chart só (g_ctxN == 1) nada muda. */
typedef struct {
    StepChart* chart;
    int        chartIdx;
    double     secondsPerRow, totalSongSeconds, chartDelay, baseBpm;
    int        baseBeatSplit;
    double*    visualRow;
    int        visualRowCount;
    int        lastNoteRow;
    int        zRows;
} GpChartCtx;
static GpChartCtx g_ctx[2];
static int g_ctxN = 1, g_ctxCur = 0;
static int g_zRows;

static void ctxSave(int i)
{
    g_ctx[i].chart = g_chart;               g_ctx[i].chartIdx = g_chartIdx;
    g_ctx[i].secondsPerRow = g_secondsPerRow; g_ctx[i].totalSongSeconds = g_totalSongSeconds;
    g_ctx[i].chartDelay = g_chartDelay;     g_ctx[i].baseBpm = g_baseBpm;
    g_ctx[i].baseBeatSplit = g_baseBeatSplit;
    g_ctx[i].visualRow = g_visualRow;       g_ctx[i].visualRowCount = g_visualRowCount;
    g_ctx[i].lastNoteRow = g_lastNoteRow;   g_ctx[i].zRows = g_zRows;
}

static void ctxLoad(int i)
{
    g_chart = g_ctx[i].chart;               g_chartIdx = g_ctx[i].chartIdx;
    g_secondsPerRow = g_ctx[i].secondsPerRow; g_totalSongSeconds = g_ctx[i].totalSongSeconds;
    g_chartDelay = g_ctx[i].chartDelay;     g_baseBpm = g_ctx[i].baseBpm;
    g_baseBeatSplit = g_ctx[i].baseBeatSplit;
    g_visualRow = g_ctx[i].visualRow;       g_visualRowCount = g_ctx[i].visualRowCount;
    g_lastNoteRow = g_ctx[i].lastNoteRow;   g_zRows = g_ctx[i].zRows;
}

static void ctxUse(int p)
{
    int i = (g_ctxN > 1 && p == 1) ? 1 : 0;
    if (g_ctxN < 2 || i == g_ctxCur) return;
    ctxSave(g_ctxCur);
    ctxLoad(i);
    g_ctxCur = i;
}

static double zeroDelayGapRows(int s);

/* Mesmas contas do loadChartForSong para o chart corrente (g_chart). */
static void ctxDerive(void)
{
    g_lastNoteRow = -1;
    for (int ri = (int)g_chart->rowCount - 1; ri >= 0; ri--) {
        StepRow* r = &g_chart->rows[ri];
        if (r->half1.dl || r->half1.ul || r->half1.cn || r->half1.ur || r->half1.dr ||
            r->half2.dl || r->half2.ul || r->half2.cn || r->half2.ur || r->half2.dr) { g_lastNoteRow = ri; break; }
    }
    g_chartDelay = g_chart->delay / (double)g_chart->delayDiv;
    float bpm = g_chart->bpm;
    if (bpm <= 0) bpm = 120.0f;
    uint32_t subdiv = g_chart->beatSplit;
    if (subdiv == 0) subdiv = 4;
    g_secondsPerRow = 60.0 / ((double)bpm * (double)subdiv);
    {
        double total = 0;
        for (int s = 0; s < (int)g_chart->segmentCount; s++) {
            double segSpr = 60.0 / ((double)g_chart->segments[s].bpm * (double)g_chart->segments[s].beatSplit);
            total += g_chart->segments[s].rowCount * segSpr + (g_chart->segments[s].delay / (double)g_chart->delayDiv);
        }
        g_totalSongSeconds = total;
    }
    g_baseBeatSplit = g_chart->segments[0].beatSplit;
    g_baseBpm = g_chart->segments[0].bpm;
    g_visualRowCount = (int)g_chart->rowCount;
    g_visualRow = (double*)malloc((size_t)(g_visualRowCount > 0 ? g_visualRowCount : 1) * sizeof(double));
    if (g_visualRow) {
        double vRow = 0;
        for (int s = 0; s < (int)g_chart->segmentCount; s++) {
            double beatRatio = (double)g_baseBeatSplit / (double)g_chart->segments[s].beatSplit;
            vRow += zeroDelayGapRows(s);
            for (uint32_t r = g_chart->segments[s].rowStart; r < g_chart->segments[s].rowStart + g_chart->segments[s].rowCount; r++) {
                if ((int)r < g_visualRowCount) g_visualRow[r] = vRow;
                vRow += beatRatio;
            }
        }
    }
    g_zRows = (int)g_chart->rowCount;
}

static void ctxFree(void)
{
    if (g_ctxN > 1) {
        ctxUse(0);
        free(g_ctx[1].visualRow);
        g_ctx[1].visualRow = NULL;
    }
    g_ctxN = 1;
    g_ctxCur = 0;
}

/* 2 jogadores em single com dificuldades diferentes: o P2 recebe o próprio chart
 * do mesmo .SEE (half1 dele copiado para o half2, lido pelo P2). */
static void nxBuildP2Ctx(void)
{
    static const char* const k_name[5] = { "NORMAL", "HARD", "CRAZY", "DOUBLE", "NIGHTMARE" };
    int d0 = g_nxDiffIdx[0], d1 = g_nxDiffIdx[1];
    ctxFree();
    if (!g_exceedSongIds || d0 == d1 || d1 < 0 || d1 > 2) return;
    int ci = Step_SelectChart(k_name[d1], -1);
    if (ci < 0 || ci >= g_playSong.chartCount || ci == g_chartIdx) return;
    StepChart* c2 = &g_playSong.charts[ci];
    if (!c2->rows || c2->rowCount == 0 || c2->segmentCount == 0) {
        Log_Print("GP: chart %s do P2 vazio — P2 usa o chart do P1\n", k_name[d1]);
        return;
    }
    for (uint32_t ri = 0; ri < c2->rowCount; ri++) c2->rows[ri].half2 = c2->rows[ri].half1;
    g_zRows = (int)g_chart->rowCount;
    ctxSave(0);
    g_chart = c2;
    g_chartIdx = ci;
    ctxDerive();
    ctxSave(1);
    /* o fim do chart espera o mais longo dos dois */
    {
        double t = g_ctx[0].totalSongSeconds > g_ctx[1].totalSongSeconds ? g_ctx[0].totalSongSeconds : g_ctx[1].totalSongSeconds;
        g_ctx[0].totalSongSeconds = g_ctx[1].totalSongSeconds = t;
    }
    ctxLoad(0);
    g_ctxN = 2;
    g_ctxCur = 0;
    Log_Print("GP: P1 chart %d, P2 chart %d (%s, %u linhas)\n", g_ctx[0].chartIdx, ci, k_name[d1], c2->rowCount);
}

static bool loadChartForSong(int songId, int diffTier, const char* modeName)
{
    ctxFree();
    g_songLoaded = false;
    g_chart = NULL;

    char stxPath[MAX_PATH];
    /* era (Zero): "%s/STEP/%s.STX" */
    snprintf(stxPath, sizeof(stxPath), "%s/STEP/%s.SEE",
             g_game.currentDirectory, Song_DataIdStr(songId));

    Log_Print("GP: loading '%s' (song %d, mode=%s)\n", stxPath, songId, modeName ? modeName : "?");

    bool loaded = Step_LoadSong(stxPath, &g_playSong);
    if (!loaded && Song_BaseId(songId) >= 0) {
        /* NX 0x8072380: sem o .SEE do chart, 0x8061d10(id) e tenta o id do recurso */
        snprintf(stxPath, sizeof(stxPath), "%s/STEP/%s.SEE",
                 g_game.currentDirectory, Song_IdStr(Song_BaseId(songId)));
        Log_Print("GP: tentando o recurso '%s'\n", stxPath);
        loaded = Step_LoadSong(stxPath, &g_playSong);
    }
    if (!loaded)
    {
        Log_Print("GP: FAILED to load STX\n");
        return false;
    }

    g_chartIdx = Step_SelectChart(modeName, 1);
    if (g_chartIdx < 0 || g_chartIdx >= g_playSong.chartCount)
    {
        Log_Print("GP: chart index %d out of range, using 0\n", g_chartIdx);
        g_chartIdx = 0;
    }

    g_chart = &g_playSong.charts[g_chartIdx];
    g_songTime = 0.0;
    g_clkHave = g_clkLocked = false;
    g_maxSongTime = 0.0;
    g_stagnantFrames = 0;
    g_lastPosMs = 0;
    // Encontrar último row com nota
    g_lastNoteRow = -1;
    if (g_chart) {
        for (int ri = (int)g_chart->rowCount - 1; ri >= 0; ri--) {
            StepRow* r = &g_chart->rows[ri];
            if (r->half1.dl || r->half1.ul || r->half1.cn || r->half1.ur || r->half1.dr ||
                r->half2.dl || r->half2.ul || r->half2.cn || r->half2.ur || r->half2.dr) {
                g_lastNoteRow = ri;
                break;
            }
        }
    }
    Log_Print("GP: last note row = %d / %u\n", g_lastNoteRow, g_chart ? g_chart->rowCount : 0);
    g_chartDelay = g_chart->delay / (double)g_chart->delayDiv;
    float bpm = g_chart->bpm;
    if (bpm <= 0) bpm = 120.0f;
    uint32_t subdiv = g_chart->beatSplit;
    if (subdiv == 0) subdiv = 4;

    g_secondsPerRow = 60.0 / ((double)bpm * (double)subdiv);
    // Total time using segments
    {
        double total = 0;
        for (int s = 0; s < g_chart->segmentCount; s++)
        {
            double segSpr = 60.0 / ((double)g_chart->segments[s].bpm * (double)g_chart->segments[s].beatSplit);
            total += g_chart->segments[s].rowCount * segSpr + (g_chart->segments[s].delay / (double)g_chart->delayDiv);
        }
        g_totalSongSeconds = total;
    }
    g_autoplay = g_game.input.autoplay;
    if (g_exDemo) {                 /* 0x402A67: 0x40B9EC(0) e (5) — autoplay nas duas metades */
        g_autoplay = true;
        for (int a = 0; a < 10; a++) g_autoPanel[a] = true;
    }

    /* Aplica multiplicador de velocidade do Command — por player. */
    for (int _ip = 0; _ip < 2; _ip++) {
        float spd = (g_game.cmdSpeedMult[_ip] >= 1) ? (float)g_game.cmdSpeedMult[_ip] : 1.0f;
        if (g_game.cmdSpeedNx[_ip] > 0) spd = (float)g_game.cmdSpeedNx[_ip] / 4.0f;   /* NX missão (+0x498, 4 = x1) */
        g_scrollSpeedX[_ip]      = spd;
        g_scrollSpeedTarget[_ip] = spd;
        g_rvLastMeasure[_ip]     = 0; /* igual ao DAT_00da24bc original: inicia em 0 → pula row 0 */
        g_ewLastRow[_ip]         = 0;
    }

    memset(g_noteHits, 0, sizeof(g_noteHits));
    memset(g_noteHitCount, 0, sizeof(g_noteHitCount));
    memset(g_nextNoteRow, 0, sizeof(g_nextNoteRow));
    for (int p = 0; p < 2; p++)
        for (int pan = 0; pan < MAX_PANELS; pan++)
            g_holdRows[p][pan] = -1;

    g_baseBeatSplit = g_chart->segments[0].beatSplit;
    g_baseBpm = g_chart->segments[0].bpm;

    // Pre-compute normalized visual rows (BPM-based, ignoring beatSplit)
    g_visualRowCount = (int)g_chart->rowCount;
    g_visualRow = (double*)realloc(g_visualRow, g_visualRowCount * sizeof(double));
    if (g_visualRow) {
        double vRow = 0;
        for (int s = 0; s < g_chart->segmentCount; s++) {
            double beatRatio = (double)g_baseBeatSplit / (double)g_chart->segments[s].beatSplit;
            vRow += zeroDelayGapRows(s);   /* delay sem Stop = vão no scroll */
            for (uint32_t r = g_chart->segments[s].rowStart; r < g_chart->segments[s].rowStart + g_chart->segments[s].rowCount; r++) {
                if ((int)r < g_visualRowCount) g_visualRow[r] = vRow;
                vRow += beatRatio;
            }
        }
    }

    Log_Print("GP: chart %d: BPM=%.1f subdiv=%d rows=%d panels=%d time=%.1fs spR=%.4f segments=%d baseSpr=%.4f\n",
        g_chartIdx, bpm, subdiv, g_chart->rowCount, g_chart->panelCount, g_totalSongSeconds, g_secondsPerRow, g_chart->segmentCount, 60.0/(g_baseBpm*(double)g_baseBeatSplit));
    g_songLoaded = true;

    for (int p = 0; p < 2; p++) {
        free(g_divSpec[p]); g_divSpec[p] = NULL;
        g_divW[p] = g_divG[p] = 0;
        g_divLastPage[p] = -1;
    }
    if (g_chart->divPageCount > 0) {
        for (int p = 0; p < 2; p++)
            g_divSpec[p] = (uint8_t*)calloc((size_t)g_chart->rowCount * 5, 1);
        divExtractSpecials(0, g_chart->rowCount);
        Log_Print("DIV: %d paginas\n", g_chart->divPageCount);
    }
    misExtract();   /* NX WORLD TOUR */

    g_chart->totalNotes = 0;
    for (uint32_t r = 0; r < g_chart->rowCount; r++)
    {
        StepRow* row = &g_chart->rows[r];
        uint8_t* p1 = (uint8_t*)&row->half1;
        uint8_t* p2 = (uint8_t*)&row->half2;
        for (int i = 0; i < 5; i++)
        {
            if (p1[i] != 0) g_chart->totalNotes++;
            if (p2[i] != 0) g_chart->totalNotes++;
        }
    }
    return true;
}

static void loadChart(void)
{
    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int songId = mode->songIds[g_game.songSelectHighlighted];
    int diffTier = mode->difficulties[g_game.songSelectHighlighted];
    loadChartForSong(songId, diffTier, mode->name);
}

/* BPM do segmento em que estamos (o original recalcula as janelas ao trocar de BPM). */
static double currentJudgeBpm(void)
{
    double bpm = 120.0;
    if (g_chart && g_chart->segmentCount > 0) {
        bpm = g_chart->segments[0].bpm;
        for (int s = (int)g_chart->segmentCount - 1; s >= 0; s--) {
            if (g_songTime >= getRowTime((int)g_chart->segments[s].rowStart)) {
                bpm = g_chart->segments[s].bpm;
                break;
            }
        }
    }
    return bpm > 0.0 ? bpm : 120.0;
}

/* Exceed: 0x4022E5 passa o BPM do primeiro bloco; o recálculo por troca
 * de BPM está comentado no original (playengine.cpp:2386). */
static double exJudgeBpm0(void)
{
    double bpm = (g_chart && g_chart->segmentCount > 0) ? g_chart->segments[0].bpm : 120.0;
    return bpm > 0.0 ? bpm : 120.0;
}

/* Limites do Bad (varredura de notas e prazo do Miss), por nível e BPM atual.
 * Cache por frame: g_songTime não muda dentro do mesmo frame. */
static void badLimits(double* early, double* late)
{
    static double cacheTime = -1e30, cacheE, cacheL;
    static int cacheLvl = -1;
    int lvl = g_game.optionDifficulty;
    if (lvl < 0) lvl = 0;
    if (lvl > 2) lvl = 2;
    if (g_songTime != cacheTime || lvl != cacheLvl) {
        double e[4], l[4];
        judgeWindows(lvl, currentJudgeBpm(), e, l);
        cacheE = e[3]; cacheL = l[3]; cacheTime = g_songTime; cacheLvl = lvl;
    }
    *early = cacheE;
    *late  = cacheL;
}
static double judgeBadEarly(void) { double e, l; badLimits(&e, &l); return e; }
static double judgeBadLate(void)  { double e, l; badLimits(&e, &l); return l; }

static JudgeType evaluateTiming(double diff)
{
    /* diff < 0: pressionou cedo; diff > 0: pressionou tarde */
    int lvl = g_game.optionDifficulty;
    if (lvl < 0) lvl = 0;
    if (lvl > 2) lvl = 2;
    double e[4], l[4];
    judgeWindows(lvl, currentJudgeBpm(), e, l);
    if (diff > -e[0] && diff < l[0]) return JT_PERFECT;
    if (diff > -e[1] && diff < l[1]) return JT_GREAT;
    if (diff > -e[2] && diff < l[2]) return JT_GOOD;
    if (diff > -e[3] && diff < l[3]) return JT_BAD;
    return JT_MISS;
}

static void popupCreate(int player, int score, int combo, float y)
{
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_popups[i].active) {
            g_popups[i].player = player;
            g_popups[i].score = score;
            g_popups[i].combo = combo;
            g_popups[i].y = y;
            g_popups[i].alpha = 1.0f;
            g_popups[i].active = true;
            break;
        }
    }
}

static int getPanelForButton(PadButton btn)
{
    switch (btn)
    {
        case PAD_DL: return 0;
        case PAD_UL: return 1;
        case PAD_C:  return 2;
        case PAD_UR: return 3;
        case PAD_DR: return 4;
        default: return -1;
    }
}

static uint8_t getPanelValue(StepRow* row, int panel, int player)
{
    StepHalf* h = (player == 0) ? &row->half1 : &row->half2;
    switch (panel) {
        case 0: return h->dl;
        case 1: return h->ul;
        case 2: return h->cn;
        case 3: return h->ur;
        case 4: return h->dr;
    }
    return 0;
}

// HalfDouble note reading: 6 posicoes mapeadas para colunas 2-7 do STX (10-col)
// pos 0..2 = half1 cols 2..4 (CN, UR, DR)
// pos 3..5 = half2 cols 0..2 (DL, UL, CN)
static uint8_t getNoteHD(StepRow* row, int pos)
{
    StepHalf* h = (pos < 3) ? &row->half1 : &row->half2;
    int pd = (pos < 3) ? (pos + 2) : (pos - 3);
    switch (pd) {
        case 0: return h->dl;
        case 1: return h->ul;
        case 2: return h->cn;
        case 3: return h->ur;
        case 4: return h->dr;
        default: return 0;
    }
}

static void clearHDPanel(StepRow* row, int pan)
{
    switch (pan) { case 0: row->half1.cn = 0; break; case 1: row->half1.ur = 0; break; case 2: row->half1.dr = 0; break; case 3: row->half2.dl = 0; break; case 4: row->half2.ul = 0; break; case 5: row->half2.cn = 0; break; }
}

static void clearPanel(StepRow* row, int pan, int player)
{
    StepHalf* hh = (player == 0) ? &row->half1 : &row->half2;
    switch (pan) { case 0: hh->dl = 0; break; case 1: hh->ul = 0; break; case 2: hh->cn = 0; break; case 3: hh->ur = 0; break; case 4: hh->dr = 0; break; }
}

// HD: panels 0=P1_CN(left/center), 1=P1_UR(up), 2=P1_DR(down)
// panels 3=P2_DL(keypad1), 4=P2_UL(keypad7), 5=P2_CN(keypad5)
static PadButton hdPanelBtn(int pan)
{
    static const PadButton hdBtn[6] = { PAD_C, PAD_UR, PAD_DR, PAD_DL, PAD_UL, PAD_C };
    return (pan >= 0 && pan < 6) ? hdBtn[pan] : PAD_C;
}

static int hdPanelPlayer(int pan)
{
    return (pan < 3) ? 0 : 1;
}

static bool isHDMode(void)
{
    return (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
            strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "HALFDOUBLE") == 0);
}

static bool isDNMode(void)
{
    return (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
           (strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "DOUBLE") == 0 ||
            strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "NIGHTMARE") == 0));
}

static int dnPanelBtn(int pan)
{
    static const PadButton dnBtn[10] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR, PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
    return (pan >= 0 && pan < 10) ? dnBtn[pan] : PAD_C;
}

static int dnPanelPlayer(int pan)
{
    return (pan < 5) ? 0 : 1;
}

static uint8_t getDNPanelValue(StepRow* row, int pan)
{
    return (pan < 5) ? getPanelValue(row, pan, 0) : getPanelValue(row, pan - 5, 1);
}

static void clearDNPanel(StepRow* row, int pan)
{
    if (pan < 5) clearPanel(row, pan, 0);
    else clearPanel(row, pan - 5, 1);
}

// Processa o julgamento final de uma linha
static void processRowJudgment(int player, int row, JudgeType jt) {
    int receptorY = ZERO_RECEPTOR_Y; /* era 38 (Exceed) */
    bool hdCheck = isHDMode();
    bool dnJdg = isDNMode();
    int jdPanels = hdCheck ? 6 : (dnJdg ? 10 : 5);
    // So Perfect/Great consomem a nota (ela some). Good/Bad/Miss passam reto.
    if (jt == JT_PERFECT || jt == JT_GREAT) {
        StepRow* r = &g_chart->rows[row];
        for (int pan = 0; pan < jdPanels; pan++) {
            uint8_t pv = hdCheck ? getNoteHD(r, pan) : (dnJdg ? getDNPanelValue(r, pan) : getPanelValue(r, pan, player));
            if (pv) {
                g_noteState[player][pan] = 1;
                g_noteExplodeRow[player][pan] = row;
                g_noteExplodeFrame[player][pan] = 0;
                if (hdCheck) clearHDPanel(r, pan);
                else if (dnJdg) clearDNPanel(r, pan);
                else clearPanel(r, pan, player);
            }
            g_lastPerfectRow[player][pan] = row;
        }
        /* clearPanel já zerou os painéis do player atual.
         * memset destruiria half do outro player em modo 2P. */
        /* if (!hdCheck && !dnJdg)
            memset(r, 0, sizeof(StepRow)); */
    }
    g_judgeDisplayType[player] = jt;
    g_judgeDisplayTimer[player] = 0.6f;
    g_judgeFrame[player] = 25; g_exJudgeCnt[player] = 0; /* PUMPY.EXE 0x40dd9a: 25 p/ todos (40 so nos tipos 6/7). Era: (jt == JT_GREAT || jt == JT_PERFECT) ? 40 : 25 */
    int sc = 0, cb = g_game.stats.combo[player];
    switch (jt) {
        case JT_PERFECT: sc = 1000; if (cb > 3) sc += 1000; cb++; break;
        case JT_GREAT:   sc = 500;  if (cb > 3) sc += 1000; cb++; break;
        case JT_GOOD:    sc = 0;    break;
        case JT_BAD:     sc = 0;    cb = 0; break;
        default: break;
    }
if (sc > 0) popupCreate(player, sc, cb, 178.0f); // Y=80 (Ghidra) + 98 offset = Y=178 (game reality)
    switch (jt) {
        case JT_PERFECT:
        case JT_GREAT:
            g_game.stats.combo[player]++;
            g_judgeDisplayCombo[player] = g_game.stats.combo[player];
            g_game.stats.missCombo[player] = 0;
            break;
        case JT_GOOD:
            g_judgeDisplayCombo[player] = g_game.stats.combo[player];
            g_game.stats.missCombo[player] = 0;
            break;
        case JT_BAD:
            g_game.stats.combo[player] = 0;
            g_judgeDisplayCombo[player] = 0;
            g_game.stats.missCombo[player] = 0;
            break;
        default: break;
    }
    switch (jt) {
        case JT_PERFECT:
            g_game.stats.perfectCount[player]++;
            g_game.stats.score[player] += 1000;
            if (g_game.stats.combo[player] > 3)
                g_game.stats.score[player] += 1000;
            break;
        case JT_GREAT:
            g_game.stats.greatCount[player]++;
            g_game.stats.score[player] += 500;
            if (g_game.stats.combo[player] > 3)
                g_game.stats.score[player] += 1000;
            break;
        case JT_GOOD:
            g_game.stats.goodCount[player]++;
            break;
        case JT_BAD:
            g_game.stats.badCount[player]++;
            break;
        default: break;
    }
    applyLife(player, jt);
    if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
        g_game.stats.maxCombo[player] = g_game.stats.combo[player];
}

// Processa linhas multi-seta pendentes
static void processPendingRows(int player) {
    double now = g_songTime;
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!g_pending[i].active) continue;
        if (g_pending[i].deadline > now && g_pending[i].hitMask != g_pending[i].totalMask) continue;
        g_pending[i].active = false;
        g_pendingCount--;
        JudgeType jt;
        bool hdCheck = isHDMode();
        bool dnPr = isDNMode();
        int jdPanels = hdCheck ? 6 : (dnPr ? 10 : 5);
        if (g_pending[i].hitMask == g_pending[i].totalMask)
            jt = evaluateTiming(g_pending[i].worstDiff);
        else
            jt = JT_MISS;
        /* long na linha: qualquer zona = PERFECT (ver rowOnlyLong) */
        if (jt != JT_MISS && rowOnlyLong(player, g_pending[i].row)) jt = JT_PERFECT;
        for (int pan = 0; pan < jdPanels; pan++) {
            uint8_t pv = hdCheck ? getNoteHD(&g_chart->rows[g_pending[i].row], pan) : (dnPr ? getDNPanelValue(&g_chart->rows[g_pending[i].row], pan) : getPanelValue(&g_chart->rows[g_pending[i].row], pan, player));
            if (!pv) continue;
            int hdPly = hdCheck ? hdPanelPlayer(pan) : (dnPr ? dnPanelPlayer(pan) : player);
            PadButton holdBtn = hdCheck ? hdPanelBtn(pan) : (dnPr ? dnPanelBtn(pan) : 0);
            if ((pv == NT_HOLD_H || pv == NT_HOLD_B || pv == NT_HOLD_T) && g_holdRows[hdPly][pan] < 0) {
                if (hdCheck || dnPr) {
                    if (!Input_IsPadDown(hdPly, holdBtn))
                        jt = JT_MISS;
                    else {
                        g_holdRows[hdPly][pan] = g_pending[i].row;
                        holdOpenDbg("pend-HD", hdPly, pan, g_pending[i].row);
                        holdHitFx(hdPly, pan, g_pending[i].row);
                        if (hdCheck) clearHDPanel(&g_chart->rows[g_pending[i].row], pan);
                        else clearDNPanel(&g_chart->rows[g_pending[i].row], pan);
                    }
                } else {
                    static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                    if (!Input_IsPadDown(player, btnMap[pan]))
                        jt = JT_MISS;
                    else {
                        g_holdRows[player][pan] = g_pending[i].row;
                holdOpenDbg("pend", player, pan, g_pending[i].row);
                        holdOpenDbg("pend-DN", player, pan, g_pending[i].row);
                        holdHitFx(player, pan, g_pending[i].row);
                        clearPanel(&g_chart->rows[g_pending[i].row], pan, player);
                    }
                }
            }
        }
        for (int pan = 0; pan < jdPanels; pan++) {
            uint8_t pv = hdCheck ? getNoteHD(&g_chart->rows[g_pending[i].row], pan) : (dnPr ? getDNPanelValue(&g_chart->rows[g_pending[i].row], pan) : getPanelValue(&g_chart->rows[g_pending[i].row], pan, player));
            if (pv == NT_HOLD_H)
                g_holdRows[player][pan] = g_pending[i].row;
                holdOpenDbg("pend", player, pan, g_pending[i].row);
        }
        for (int pan = 0; pan < jdPanels; pan++) {
            uint8_t pv = hdCheck ? getNoteHD(&g_chart->rows[g_pending[i].row], pan) : (dnPr ? getDNPanelValue(&g_chart->rows[g_pending[i].row], pan) : getPanelValue(&g_chart->rows[g_pending[i].row], pan, player));
            if (pv && pv != NT_HOLD_B && pv != NT_HOLD_T)
                g_nextNoteRow[player][pan] = g_pending[i].row + 1;
        }
        processRowJudgment(player, g_pending[i].row, jt);
    }
}

static int rowAnyNote(StepRow* row, int player)
{
    StepHalf* h = (player == 0) ? &row->half1 : &row->half2;
    return h->dl || h->ul || h->cn || h->ur || h->dr;
}

static int rowIsHold(int player, int panel, StepRow* row)
{
    uint8_t v = getPanelValue(row, panel, player);
    return (v == NT_HOLD_H || v == NT_HOLD_B || v == NT_HOLD_T);
}

static int rowAnyNoteHD(StepRow* row)
{
    return row->half1.cn || row->half1.ur || row->half1.dr ||
           row->half2.dl || row->half2.ul || row->half2.cn;
}

/* Ainda há corpo/cauda de hold à frente da posição atual do hold neste botão?
 * Se não houver, o hold acabou: sem isto, uma cauda consumida por outro caminho
 * (auto-capture, pending) deixava g_holdRows >= 0 para sempre e a animação de
 * explosão reiniciava sozinha, sem nenhuma seta chegando. */
static bool holdHasRowsAhead(int p, int panel, bool isHD, bool isDN)
{
    for (int ri = g_holdRows[p][panel] + 1; ri < (int)g_chart->rowCount; ri++)
    {
        uint8_t v = isHD ? getNoteHD(&g_chart->rows[ri], panel)
                  : (isDN ? getDNPanelValue(&g_chart->rows[ri], panel)
                          : getPanelValue(&g_chart->rows[ri], panel, p));
        if (!v) continue;
        return v == NT_HOLD_B || v == NT_HOLD_T;
    }
    return false;
}

/* Antecedência com que o botão SEGURADO consome uma linha de hold.
 * PUMPY.EXE (verificado emulando 0x40e330): nos modos Normal/Hard/Crazy/Division a linha só é
 * consumida quando delta <= 0 (0x40ed8a, segunda metade); em HalfDouble/Double/Nightmare
 * (flags 0x80/0x200/0x800) já entra na janela CEDO do Perfect (0x40e3e6, primeira metade),
 * então soltar poucos ms antes da cauda ainda vale.
 * exceed.exe NÃO tem essa antecipação: nos dois caminhos (single 0x4086a8, double 0x408a79)
 * o botão segurado ([0x568FF0]) só gera o aperto quando Y <= 0.0 (double em 0x449450). */
static double holdLeadSec(bool earlyFamily)
{
    if (g_exceedSongIds) return 0.0;
    if (!earlyFamily) return 0.0;
    int lvl = g_game.optionDifficulty;
    if (lvl < 0) lvl = 0;
    if (lvl > 2) lvl = 2;
    double e[4], l[4];
    judgeWindows(lvl, currentJudgeBpm(), e, l);
    return e[0];
}

/* Aplica o julgamento de UMA linha de hold (cabeça/corpo/cauda). No original cada linha é um
 * julgamento completo: score, combo, contadores e life/speed (0x41021a..0x410326). O código de hold
 * do port só mexia em combo/score e nunca no life. */
static void applyRowJudgment(int p, JudgeType jt)
{
    switch (jt) {
    case JT_PERFECT:
        g_game.stats.combo[p]++;
        g_game.stats.missCombo[p] = 0;
        g_game.stats.perfectCount[p]++;
        g_game.stats.score[p] += 1000;
        if (g_game.stats.combo[p] > 3) g_game.stats.score[p] += 1000;
        break;
    case JT_GREAT:
        g_game.stats.combo[p]++;
        g_game.stats.missCombo[p] = 0;
        g_game.stats.greatCount[p]++;
        g_game.stats.score[p] += 500;
        if (g_game.stats.combo[p] > 3) g_game.stats.score[p] += 1000;
        break;
    case JT_GOOD:
        g_game.stats.missCombo[p] = 0;
        g_game.stats.goodCount[p]++;
        break;
    case JT_BAD:
        g_game.stats.combo[p] = 0;
        g_game.stats.missCombo[p] = 0;
        g_game.stats.badCount[p]++;
        break;
    case JT_MISS:
        g_game.stats.combo[p] = 0;
        g_game.stats.missCount[p]++;
        g_game.stats.missCombo[p]++;
        break;
    default: return;
    }
    g_judgeDisplayType[p] = jt;
    g_judgeDisplayTimer[p] = 0.6f;
    g_judgeFrame[p] = 25; g_exJudgeCnt[p] = 0; /* PUMPY.EXE 0x40dd9a: 25 p/ todos (40 so nos tipos 6/7). Era: (jt == JT_GREAT || jt == JT_PERFECT) ? 40 : 25 */
    g_judgeDisplayCombo[p] = (jt == JT_MISS) ? (int)g_game.stats.missCombo[p] : (int)g_game.stats.combo[p];
    applyLife(p, jt);
    if (g_game.stats.combo[p] > g_game.stats.maxCombo[p])
        g_game.stats.maxCombo[p] = g_game.stats.combo[p];
}

/* ---------------------------------------------------------------------------
 * Zero (piu 0x808a760): julgamento POR LINHA.
 *   - segurar o botão conta como pisar nas partes do long (corpo 0xB, fim 0xC;
 *     começo 0xA só depois do tempo dele) enquanto a linha está na janela de
 *     PERFECT ([+0x240..+0x244]);
 *   - pisar julga pela janela e guarda o PIOR resultado da linha; parte de long
 *     pisada é consumida, explode e deixa a linha em PERFECT ([+0x1a] = 1);
 *     seta comum fica marcada (-0x80);
 *   - um aperto resolve só a primeira linha com nota (bVar26);
 *   - a linha é julgada quando não sobra nota sem pisar; PERFECT/GREAT explodem
 *     as setas, GOOD/BAD deixam elas subindo;
 *   - passou da janela de BAD com nota sem pisar: todas marcadas e UM MISS.
 * ------------------------------------------------------------------------- */
static uint16_t* g_zHit[2];     /* painéis pisados (marcados) por linha */
static uint8_t*  g_zDone[2];    /* linha resolvida */
static int8_t*   g_zRowJ[2];    /* pior julgamento da linha até agora */
/* NX (piu 0x806fb9d / NX2 step.cpp:1052): a linha é julgada pela MÉDIA das distâncias
 * dos taps pisados (cedo |dt|, tarde dt - Delay, mínimo 0); partes de long não somam */
static float*    g_zSum[2];     /* soma das distâncias (s) */
static uint8_t*  g_zCnt[2];     /* taps somados */
static int       g_zFirst[2];
/* g_zRows: declarado com o contexto de chart (por jogador) */

static void zeroJudgeReset(void)
{
    for (int p = 0; p < 2; p++) {
        free(g_zHit[p]); free(g_zDone[p]); free(g_zRowJ[p]);
        g_zHit[p] = NULL; g_zDone[p] = NULL; g_zRowJ[p] = NULL;
        free(g_zSum[p]); free(g_zCnt[p]); g_zSum[p] = NULL; g_zCnt[p] = NULL;
        g_zFirst[p] = 0;
    }
    g_zRows = (g_songLoaded && g_chart) ? (int)g_chart->rowCount : 0;
    if (g_zRows <= 0) return;
    int zAlloc = g_zRows;
    if (g_ctxN > 1 && g_ctx[1].zRows > zAlloc) zAlloc = g_ctx[1].zRows;   /* chart do P2 */
    for (int p = 0; p < 2; p++) {
        g_zHit[p]  = (uint16_t*)calloc((size_t)zAlloc, sizeof(uint16_t));
        g_zDone[p] = (uint8_t*)calloc((size_t)zAlloc, 1);
        g_zRowJ[p] = (int8_t*)calloc((size_t)zAlloc, 1);
        g_zSum[p]  = (float*)calloc((size_t)zAlloc, sizeof(float));
        g_zCnt[p]  = (uint8_t*)calloc((size_t)zAlloc, 1);
    }
}

static void zeroExplode(int p, int pan, int ri)
{
    g_noteState[p][pan] = 1;
    g_noteExplodeRow[p][pan] = ri;
    g_noteExplodeFrame[p][pan] = 0;
    g_glowTimer[p][pan] = 24;
}

/* resultado da linha: média dos taps (NX); sem tap (só long) fica o PERFECT do long */
static JudgeType zeroRowResult(int p, int ri)
{
    if (!g_zCnt[p] || !g_zCnt[p][ri]) return (JudgeType)g_zRowJ[p][ri];
    if (!g_exceedSongIds) return (JudgeType)g_zRowJ[p][ri];   /* outras versões: pior resultado */
    int lvl = g_game.optionDifficulty < 0 ? 0 : (g_game.optionDifficulty > 2 ? 2 : g_game.optionDifficulty);
    float P = (float)(7 - lvl * 2) / 120.0f * 1000.0f, I = 41.6666679f;   /* 0x8070fd0 */
    float v = g_zSum[p][ri] / (float)g_zCnt[p][ri] * 1000.0f;
    int r = (int)((v - P) / I + 1.0f);                                    /* 0x806fbbb */
    if (r < 0) r = 0;
    if (r > 3) r = 3;
    return (JudgeType)(JT_PERFECT + r);
}

static void zeroJudge(int p)
{
    if (!g_songLoaded || !g_chart || !g_zHit[p]) return;
    bool dn = isDNMode();
    int panCount = dn ? 10 : 5;
    #define ZJ_V(r, pn) (dn ? getDNPanelValue(&g_chart->rows[r], pn) : getPanelValue(&g_chart->rows[r], pn, p))
    #define ZJ_CLR(r, pn) do { if (dn) clearDNPanel(&g_chart->rows[r], pn); else clearPanel(&g_chart->rows[r], pn, p); } while (0)
    #define ZJ_ISHOLD(v) ((v) == NT_HOLD_H || (v) == NT_HOLD_B || (v) == NT_HOLD_T)
    static const PadButton k_btn[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };

    /* botões: flash do receptor e efeito de pisar (0x807ff20), com ou sem nota */
    bool hitB[10], downB[10];
    for (int pan = 0; pan < panCount; pan++) {
        int pl = dn ? dnPanelPlayer(pan) : p;
        PadButton b = dn ? (PadButton)dnPanelBtn(pan) : k_btn[pan];
        hitB[pan] = Input_IsPadHit(pl, b);
        downB[pan] = Input_IsPadDown(pl, b) || g_autoPanel[pan];
        if (hitB[pan]) { g_hitTimer[p][pan] = 17; g_p1FlashTimer[p][pan] = 16; }
    }

    double badE = judgeBadEarly(), badL = judgeBadLate();
    while (g_zFirst[p] < g_zRows && g_zDone[p][g_zFirst[p]]) g_zFirst[p]++;

    /* 1) acertos: a primeira linha com nota pisada encerra a busca do quadro */
    for (int ri = g_zFirst[p]; ri < g_zRows; ri++) {
        double diff = g_songTime - getRowTime(ri);
        if (diff < -badE) break;
        if (g_zDone[p][ri]) continue;
        bool any = false, hitNow = false;
        /* era: qualquer acerto (inclusive o long segurado) encerrava a busca; com um
         * long sendo segurado, o aperto de outra seta no mesmo quadro nunca chegava à
         * linha dele, virava MISS e o MISS soltava o long (que sumia). Só um aperto
         * de verdade (botão apertado neste quadro) conta para o "um aperto, uma linha". */
        for (int pan = 0; pan < panCount; pan++) {
            uint8_t v = ZJ_V(ri, pan);
            if (!v || (g_zHit[p][ri] & (1u << pan))) continue;
            any = true;
            bool autoHit = g_autoPanel[pan] && diff >= 0.0;
            /* era: corpo/cauda contavam em toda a janela de PERFECT, inclusive antes do
             * receptor. A janela do NX é em batidas: em bloco de BPM baixo (D02: 13.59 /
             * 4.53) ela alcança a cauda ainda no alto da tela, o hold terminava e o long
             * sumia. Como no NX2 (DrawLongNote: CurY > 0 && y >= STEP_Y), cada parte
             * segurada só conta quando a linha chega ao receptor. */
            /* era: bool holdHit = downB[pan] && evaluateTiming(diff) == JT_PERFECT && diff >= 0.0 && ...
             * NX2 step.cpp:997..1010 (long, Attr & 4): conta com -(Delay+Interval+Perfect)
             * < (linha - agora) < Perfect, ou seja diff em (-P, D+I+P); a cabeça (4) só
             * com a linha já passada. A janela é em ms (judgeWindows), fixa em qualquer BPM. */
            double hwE[4], hwL[4];
            judgeWindows(g_game.optionDifficulty < 0 ? 0 : (g_game.optionDifficulty > 2 ? 2 : g_game.optionDifficulty),
                         currentJudgeBpm(), hwE, hwL);
            bool holdHit = downB[pan] && diff > -hwE[0] && diff < hwL[1] &&
                           (v == NT_HOLD_B || v == NT_HOLD_T || (v == NT_HOLD_H && diff >= 0.0));
            /* O long é consumido em ordem: corpo/cauda só depois da parte anterior do
             * mesmo painel (pega = apagada, ou perdida = marcada). Sem isso, com longs
             * curtos o corpo e a cauda entravam na janela de PERFECT antes da cabeça
             * (que só vale com diff >= 0); a cabeça vinha depois e g_holdRows apontava
             * para um long já sem cauda, que sumia (D02 CRAZY, linhas 567..570). */
            if ((v == NT_HOLD_B || v == NT_HOLD_T) && ri > 0) {
                uint8_t pv = ZJ_V(ri - 1, pan);
                if ((pv == NT_HOLD_H || pv == NT_HOLD_B) && !(g_zHit[p][ri - 1] & (1u << pan)))
                    continue;   /* parte anterior ainda pendente: nem segurar nem apertar consome esta */
            }
            if (!hitB[pan] && !autoHit && !holdHit) continue;
            JudgeType jt = (autoHit || holdHit) ? JT_PERFECT : evaluateTiming(diff);
            if (jt == JT_MISS || jt == JT_NONE) continue;      /* fora da janela */
            /* era: hitNow = true; */
            if (hitB[pan] && !holdHit && !autoHit) hitNow = true;
            if (ZJ_ISHOLD(v)) {
                zeroExplode(p, pan, ri);
                ZJ_CLR(ri, pan);
                g_zRowJ[p][ri] = (int8_t)JT_PERFECT;           /* [+0x1a] = 1 */
                g_holdRows[p][pan] = (v == NT_HOLD_T) ? -1 : ri;
                g_lastPerfectRow[p][pan] = ri;
            } else {
                g_zHit[p][ri] |= (uint16_t)(1u << pan);
                if (g_zRowJ[p][ri] < (int8_t)jt) g_zRowJ[p][ri] = (int8_t)jt;
                if (!autoHit) {   /* distância NX: cedo |dt|, tarde dt - Delay (0x80701a1) */
                    double dv = diff <= 0.0 ? -diff : diff - 0.0416666679;
                    if (dv < 0.0) dv = 0.0;
                    g_zSum[p][ri] += (float)dv;
                    if (g_zCnt[p][ri] < 255) g_zCnt[p][ri]++;
                }
            }
        }
        /* sobrou nota sem pisar? */
        bool left = false;
        for (int pan = 0; pan < panCount; pan++) {
            uint8_t v = ZJ_V(ri, pan);
            if (v && !(g_zHit[p][ri] & (1u << pan))) { left = true; break; }
        }
        if (!left) {
            /* era: JudgeType jt = (JudgeType)g_zRowJ[p][ri];  (pior resultado) */
            JudgeType jt = g_zRowJ[p][ri] ? zeroRowResult(p, ri) : JT_NONE;
            if (jt != JT_NONE) {
                if (jt == JT_PERFECT || jt == JT_GREAT) {
                    for (int pan = 0; pan < panCount; pan++)
                        if (g_zHit[p][ri] & (1u << pan)) {
                            zeroExplode(p, pan, ri);
                            ZJ_CLR(ri, pan);
                            g_lastPerfectRow[p][pan] = ri;
                        }
                }
                applyRowJudgment(p, jt);
                if (g_misHiddenRow(ri) && jt != JT_MISS) g_nxMis[p].hidden++;   /* 0x806fb8e */
            }
            g_zDone[p][ri] = 1;                                  /* também linha vazia */
        }
        (void)any;
        if (hitNow) break;
    }

    /* 2) MISS: passou da janela de BAD com nota sem pisar -> um MISS na linha */
    for (int ri = g_zFirst[p]; ri < g_zRows; ri++) {
        double diff = g_songTime - getRowTime(ri);
        if (diff <= badL) break;
        if (g_zDone[p][ri]) continue;
        bool missed = false;
        for (int pan = 0; pan < panCount; pan++) {
            uint8_t v = ZJ_V(ri, pan);
            if (!v || (g_zHit[p][ri] & (1u << pan))) continue;
            missed = true;
            g_zHit[p][ri] |= (uint16_t)(1u << pan);
            /* MISS em parte de long: só solta o hold (igual ao Zero). Apagar a nota
             * (ZJ_CLR) tirava a cabeça/corpo da linha e o long inteiro sumia; marcado
             * em g_zHit ele segue subindo como a seta comum perdida.
             * if (ZJ_ISHOLD(v)) { g_holdRows[p][pan] = -1; ZJ_CLR(ri, pan); } */
            if (ZJ_ISHOLD(v)) g_holdRows[p][pan] = -1;
        }
        if (missed) applyRowJudgment(p, JT_MISS);
        else if (g_zRowJ[p][ri]) {
            applyRowJudgment(p, zeroRowResult(p, ri));   /* era: (JudgeType)g_zRowJ[p][ri] */
            if (g_misHiddenRow(ri)) g_nxMis[p].hidden++;
        }
        g_zDone[p][ri] = 1;
    }
    /* NX WORLD TOUR: itens pisados dentro da janela (0x8070569) */
    if (g_misSpec) {
        for (int ri = g_zFirst[p] > 8 ? g_zFirst[p] - 8 : 0; ri < g_zRows; ri++) {
            double diff = g_songTime - getRowTime(ri);
            if (diff < -badE) break;
            if (diff > badL) continue;
            for (int pan = 0; pan < panCount; pan++) {
                int col = dn ? pan : pan + (p ? 5 : 0);
                uint8_t s = g_misSpec[ri * 10 + col];
                if (s < 0x14 || s > 0x28 || !hitB[pan]) continue;
                JudgeType jt = evaluateTiming(diff);
                if (jt == JT_MISS || jt == JT_NONE) jt = JT_BAD;
                misCollect(p, s, jt);
                g_misSpec[ri * 10 + col] = 0;
                zeroExplode(p, pan, ri);   /* spark no painel, como numa seta (observado no original pelo usuário) */
            }
        }
    }
    #undef ZJ_V
    #undef ZJ_CLR
    #undef ZJ_ISHOLD
}

static void processInput(int player)
{
    if (!g_songLoaded) return;

    bool isHD = isHDMode();
    bool isDN = isDNMode();
    int panCount = isHD ? 6 : (isDN ? 10 : 5);
    int numBtns = isHD ? 6 : (isDN ? 10 : PAD_BUTTONS_PER_PLAYER);

    for (int b = 0; b < numBtns; b++)
    {
        int panel;
        PadButton btn;
        int usePlayer;
        if (isHD) {
            panel = b;
            btn = hdPanelBtn(panel);
            usePlayer = hdPanelPlayer(panel);
        } else if (isDN) {
            panel = b;
            btn = dnPanelBtn(panel);
            usePlayer = dnPanelPlayer(panel);
        } else {
            panel = getPanelForButton((PadButton)b);
            btn = (PadButton)b;
            usePlayer = player;
        }
        if (panel < 0) continue;
        if (!Input_IsPadHit(usePlayer, btn)) continue;
        g_hitTimer[player][panel] = 17;
        g_p1FlashTimer[player][panel] = 16; // Zero 0x807ff20: [+0xdd2c..] = 0, 16 quadros (era 15)

        /* Division: W/G pisadas na janela (PERFECT..BAD) explodem, somam no
         * contador e escolhem o ramo — sem judge, combo ou MISS (0x40f16a). */
        if (g_divSpec[player] && !isHD && !isDN && panel < 5) {
            int hitSpec = -1;
            for (int ri = 0; ri < (int)g_chart->rowCount; ri++) {
                uint8_t sv = g_divSpec[player][ri * 5 + panel];
                if (sv != NT_DIV_W && sv != NT_DIV_G) continue;
                double sd = g_songTime - getRowTime(ri);
                if (sd < -judgeBadEarly()) break;
                if (sd > judgeBadLate()) continue;
                hitSpec = ri; break;
            }
            if (hitSpec >= 0) {
                uint8_t sv = g_divSpec[player][hitSpec * 5 + panel];
                g_divSpec[player][hitSpec * 5 + panel] = 0;
                {
                    int hp = -1;
                    for (int i = 0; i < g_chart->divPageCount; i++) {
                        uint32_t s0 = g_chart->divPages[i].rowStart, n0 = g_chart->divPages[i].rowCount;
                        if ((uint32_t)hitSpec >= s0 && (uint32_t)hitSpec < s0 + n0) { hp = i; break; }
                    }
                    if (hp != g_divLastPage[player]) {   /* página nova: contadores zerados */
                        g_divW[player] = g_divG[player] = 0;
                        g_divLastPage[player] = hp;
                    }
                }
                if (sv == NT_DIV_W) g_divW[player]++; else g_divG[player]++;
                g_noteState[player][panel] = 1;
                g_noteExplodeRow[player][panel] = hitSpec;
                g_noteExplodeFrame[player][panel] = 0;
                divApplyBranch(player, hitSpec);
                continue;
            }
        }

        double bestDiff    = 999;  /* diff com sinal: negativo=early, positivo=late */
        int bestRow = -1;

        for (int ri = g_nextNoteRow[player][panel]; ri < (int)g_chart->rowCount; ri++)
        {
            uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (isDN ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, player));
            if (!val || val == NT_HOLD_B || val == NT_HOLD_T) continue;

            double rowTime = getRowTime(ri);
            double diff = g_songTime - rowTime;
            if (diff < -judgeBadEarly()) break;
            if (diff > judgeBadLate()) { g_nextNoteRow[player][panel] = ri + 1; continue; }

            /* PUMPY.EXE 0x40e480-0x40ea34: as 20 linhas candidatas são varridas em ordem
             * CRESCENTE e o aperto consome a PRIMEIRA cuja nota deste botão ainda não foi
             * consumida e cujo delta cai em alguma janela — não a de menor |diff|. Numa
             * sequência rápida em que duas linhas cabem na janela, a mais antiga leva o
             * aperto (a mais nova fica para o próximo). */
            bestDiff = diff;
            bestRow = ri;
            break;
        }

        if (bestRow < 0) continue;

        Log_Print("TAPHIT: p=%d pan=%d row=%d diff=%.3f\n", player, panel, bestRow, bestDiff);

        int arrowsInRow = 0;
        for (int pan = 0; pan < panCount; pan++)
            if (isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player))) arrowsInRow++;

        if (arrowsInRow > 1) {
            g_nextNoteRow[player][panel] = bestRow + 1;
            int slot = -1;
            for (int i = 0; i < MAX_PENDING; i++)
                if (g_pending[i].active && g_pending[i].row == bestRow) { slot = i; break; }
            if (slot < 0)
                for (int i = 0; i < MAX_PENDING; i++)
                    if (!g_pending[i].active) { slot = i; break; }
            if (slot >= 0) {
                if (!g_pending[slot].active) {
                    g_pending[slot].active = true;
                    g_pending[slot].row = bestRow;
                    g_pending[slot].deadline = g_songTime + judgeBadLate();
                    g_pending[slot].totalMask = 0;
                    g_pending[slot].hitMask = 0;
                    g_pending[slot].worstDiff = 0.0;  /* sinal preservado; inicia em 0 */
                    for (int pan = 0; pan < panCount; pan++) {
                        uint8_t pv = isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player));
                        if (pv && pv < NT_HOLD_H)
                            g_pending[slot].totalMask |= (1 << pan);
                    }
                    g_pendingCount++;
                }
                g_pending[slot].hitMask |= (1 << panel);
                /* PUMPY.EXE 0x40e516 (ROW_CHECK): o julgamento da linha só sai quando a
                 * ÚLTIMA nota é consumida, e a janela usada é a do delta DESSA batida
                 * (não a pior das batidas). Antes guardávamos o maior |diff|. */
                g_pending[slot].worstDiff = bestDiff;

                if ((g_pending[slot].hitMask & g_pending[slot].totalMask) == g_pending[slot].totalMask) {
                    g_pending[slot].active = false;
                    g_pendingCount--;
                    JudgeType pjt = evaluateTiming(g_pending[slot].worstDiff);
                    /* long na linha: qualquer zona = PERFECT (ver rowOnlyLong) */
                    if (pjt != JT_MISS && rowOnlyLong(player, bestRow)) pjt = JT_PERFECT;
                    for (int pan = 0; pan < panCount; pan++) {
                        uint8_t pv = isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player));
                        if (!pv) continue;
                        if ((pv == NT_HOLD_H || pv == NT_HOLD_B || pv == NT_HOLD_T) && g_holdRows[player][pan] < 0) {
                            if (isHD) {
                                if (!Input_IsPadDown(hdPanelPlayer(pan), hdPanelBtn(pan)))
                                    pjt = JT_MISS;
                                else {
                                    g_holdRows[player][pan] = bestRow;
                                    holdOpenDbg("input", player, pan, bestRow);
                                    holdHitFx(player, pan, bestRow);
                                    clearHDPanel(&g_chart->rows[bestRow], pan);
                                }
                            } else if (isDN) {
                                if (!Input_IsPadDown(dnPanelPlayer(pan), dnPanelBtn(pan)))
                                    pjt = JT_MISS;
                                else {
                                    g_holdRows[player][pan] = bestRow;
                                    holdOpenDbg("input", player, pan, bestRow);
                                    holdHitFx(player, pan, bestRow);
                                    clearDNPanel(&g_chart->rows[bestRow], pan);
                                }
                            } else {
                                static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                                if (!Input_IsPadDown(player, btnMap[pan]))
                                    pjt = JT_MISS;
                                else {
                                    g_holdRows[player][pan] = bestRow;
                                    holdOpenDbg("input", player, pan, bestRow);
                                    holdHitFx(player, pan, bestRow);
                                    clearPanel(&g_chart->rows[bestRow], pan, player);
                                }
                            }
                        }
                    }
                    if (pjt == JT_PERFECT || pjt == JT_GREAT) {
                        for (int pan = 0; pan < panCount; pan++)
                            if (isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player))) {
                                g_noteState[player][pan] = 1;
                                g_noteExplodeRow[player][pan] = bestRow;
                                g_noteExplodeFrame[player][pan] = 0;
                                g_glowTimer[player][pan] = 24; /* glow aditivo P/G — 24 frames (0x18), igual original */
                                if (isHD) clearHDPanel(&g_chart->rows[bestRow], pan);
                                else if (isDN) clearDNPanel(&g_chart->rows[bestRow], pan);
                                else clearPanel(&g_chart->rows[bestRow], pan, player);
                            }
                        /* clearPanel já zerou cada painel do player atual (half1 ou half2).
                         * memset destruiria dados do outro player em modo 2P. */
                        /* if (!isHD && !isDN)
                        memset(&g_chart->rows[bestRow], 0, sizeof(StepRow)); */
                    }
                    g_judgeDisplayType[player] = pjt;
                    g_judgeDisplayTimer[player] = 0.6f;
                    g_judgeFrame[player] = 25; g_exJudgeCnt[player] = 0; /* PUMPY.EXE 0x40dd9a: 25 p/ todos (40 so nos tipos 6/7). Era: (pjt == JT_GREAT || pjt == JT_PERFECT) ? 40 : 25 */
                    { int sc = 0, cb = g_game.stats.combo[player];
                      int receptorY = ZERO_RECEPTOR_Y; /* era 38 (Exceed) */
                      switch (pjt) {
                        case JT_PERFECT: sc = 1000; if (cb > 3) sc += 1000; cb++; break;
                        case JT_GREAT:   sc = 500;  if (cb > 3) sc += 1000; cb++; break;
                        default: break;
} if (sc > 0) popupCreate(player, sc, cb, 178.0f); }
                    switch (pjt) {
                        case JT_PERFECT: case JT_GREAT:
                            g_game.stats.combo[player]++;
                            g_judgeDisplayCombo[player] = g_game.stats.combo[player];
                            g_game.stats.missCombo[player] = 0;
                            if (pjt == JT_PERFECT) {
                                g_game.stats.perfectCount[player]++;
                                g_game.stats.score[player] += 1000;
                                if (g_game.stats.combo[player] > 3) g_game.stats.score[player] += 1000;
                            } else {
                                g_game.stats.greatCount[player]++;
                                g_game.stats.score[player] += 500;
                                if (g_game.stats.combo[player] > 3) g_game.stats.score[player] += 1000;
                            }
                            break;
                        case JT_GOOD:
                            g_judgeDisplayCombo[player] = g_game.stats.combo[player];
                            g_game.stats.missCombo[player] = 0;
                            g_game.stats.goodCount[player]++;
                            break;
                        case JT_BAD:
                            g_game.stats.combo[player] = 0;
                            g_judgeDisplayCombo[player] = 0;
                            g_game.stats.missCombo[player] = 0;
                            g_game.stats.badCount[player]++;
                            break;
                        default: break;
                    }
                    applyLife(player, pjt);
                    if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
                        g_game.stats.maxCombo[player] = g_game.stats.combo[player];
                }
            }
            continue;
        }

        g_nextNoteRow[player][panel] = bestRow + 1;
        JudgeType jt = evaluateTiming(bestDiff);
        /* long na linha: qualquer zona = PERFECT (ver rowOnlyLong) */
        if (jt != JT_MISS && rowOnlyLong(player, bestRow)) jt = JT_PERFECT;
        if (jt == JT_PERFECT || jt == JT_GREAT) {
            for (int pan = 0; pan < panCount; pan++)
                if (isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player))) {
                    g_noteState[player][pan] = 1;
                    g_noteExplodeRow[player][pan] = bestRow;
                    g_noteExplodeFrame[player][pan] = 0;
                    g_glowTimer[player][pan] = 24; /* glow aditivo P/G — 24 frames (0x18), igual original */
                    if (isHD) clearHDPanel(&g_chart->rows[bestRow], pan);
                    else if (isDN) clearDNPanel(&g_chart->rows[bestRow], pan);
                    else clearPanel(&g_chart->rows[bestRow], pan, player);
                }
            /* clearPanel já zerou cada painel do player atual (half1 ou half2).
             * memset destruiria dados do outro player em modo 2P. */
            /* if (!isHD && !isDN)
                memset(&g_chart->rows[bestRow], 0, sizeof(StepRow)); */
        }
        g_judgeDisplayType[player] = jt;
        g_judgeDisplayTimer[player] = 0.6f;
        g_judgeFrame[player] = 25; g_exJudgeCnt[player] = 0; /* PUMPY.EXE 0x40dd9a: 25 p/ todos (40 so nos tipos 6/7). Era: (jt == JT_GREAT || jt == JT_PERFECT) ? 40 : 25 */
        { int sc = 0, cb = g_game.stats.combo[player];
          int receptorY = ZERO_RECEPTOR_Y; /* era 38 (Exceed) */
          switch (jt) {
            case JT_PERFECT: sc = 1000; if (cb > 3) sc += 1000; cb++; break;
            case JT_GREAT:   sc = 500;  if (cb > 3) sc += 1000; cb++; break;
            default: break;
} if (sc > 0) popupCreate(player, sc, cb, 178.0f); }
        switch (jt) {
            case JT_PERFECT: case JT_GREAT:
                g_game.stats.combo[player]++;
                g_judgeDisplayCombo[player] = g_game.stats.combo[player];
                g_game.stats.missCombo[player] = 0;
                if (jt == JT_PERFECT) {
                    g_game.stats.perfectCount[player]++;
                    g_game.stats.score[player] += 1000;
                    if (g_game.stats.combo[player] > 3) g_game.stats.score[player] += 1000;
                } else {
                    g_game.stats.greatCount[player]++;
                    g_game.stats.score[player] += 500;
                    if (g_game.stats.combo[player] > 3) g_game.stats.score[player] += 1000;
                }
                break;
            case JT_GOOD:
                g_judgeDisplayCombo[player] = g_game.stats.combo[player];
                g_game.stats.missCombo[player] = 0;
                g_game.stats.goodCount[player]++;
                break;
            case JT_BAD:
                g_game.stats.combo[player] = 0;
                g_judgeDisplayCombo[player] = 0;
                g_game.stats.missCombo[player] = 0;
                g_game.stats.badCount[player]++;
                break;
            default: break;
        }
        applyLife(player, jt);
        if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
            g_game.stats.maxCombo[player] = g_game.stats.combo[player];
    }
}

static bool anyAutoPanel(void)
{
    bool isHD = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                 strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "HALFDOUBLE") == 0);
    bool dnAP = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                (strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "DOUBLE") == 0 ||
                 strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "NIGHTMARE") == 0));
    int pc = dnAP ? 10 : (isHD ? 6 : 5);
    for (int a = 0; a < pc; a++)
        if (g_autoPanel[a]) return true;
    return false;
}

static void processAutoplay(void)
{
    if (!g_songLoaded || !anyAutoPanel()) return;

    bool isHD = isHDMode();
    bool dnAP = isDNMode();
    int panCount = isHD ? 6 : (dnAP ? 10 : 5);

    int _ap0 = (isHD || dnAP) ? 0 : ((g_game.activePlayerMask == 0x2) ? 1 : 0);
    int _ap1 = (isHD || dnAP) ? 1 : ((g_game.activePlayerMask == 0x3) ? 2 : _ap0 + 1);
    for (int p = _ap0; p < _ap1; p++)
    {
        ctxUse(p);   /* chart do jogador */
        int hitRows[10], hitCount = 0;
        for (int panel = 0; panel < panCount; panel++)
        {
            if (!g_autoPanel[panel]) continue;

            for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (!val) continue;
                if (val == NT_HOLD_B || val == NT_HOLD_T) continue;

                double rowTime = getRowTime(ri);
                double diff = g_songTime - rowTime;
                if (diff < -JUDGE_PERFECT) break;
                if (diff > JUDGE_PERFECT) { g_nextNoteRow[p][panel] = ri + 1; continue; }

                g_nextNoteRow[p][panel] = ri + 1;
                g_hitTimer[p][panel] = 17;
                hitRows[hitCount++] = ri;
                break;
            }
        }

        int dedupRows[10], dedupCount = 0;
        for (int i = 0; i < hitCount; i++)
        {
            int dup = 0;
            for (int d = 0; d < dedupCount; d++)
                if (dedupRows[d] == hitRows[i]) { dup = 1; break; }
            if (!dup) dedupRows[dedupCount++] = hitRows[i];
        }

        for (int i = 0; i < dedupCount; i++)
        {
            g_judgeDisplayType[p] = JT_PERFECT;
            g_judgeDisplayTimer[p] = 0.6f;
            g_judgeFrame[p] = 25; g_exJudgeCnt[p] = 0; /* 0x40dd9a (era 40) */
            g_judgeDisplayCombo[p] = ++g_game.stats.combo[p];
            g_game.stats.missCombo[p] = 0;
            g_game.stats.score[p] += 1000;
            if (g_game.stats.combo[p] > 3)
                g_game.stats.score[p] += 1000;
            if (g_game.stats.combo[p] > g_game.stats.maxCombo[p])
                g_game.stats.maxCombo[p] = g_game.stats.combo[p];

            for (int panel = 0; panel < panCount; panel++)
            {
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[hitRows[i]], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[hitRows[i]], panel) : getPanelValue(&g_chart->rows[hitRows[i]], panel, p));
                if (val == NT_HOLD_H)
                    g_holdRows[p][panel] = hitRows[i];
                    holdOpenDbg("auto", p, panel, hitRows[i]);
                if (val)
                    g_lastPerfectRow[p][panel] = hitRows[i];
            }

            for (int panel = 0; panel < panCount; panel++) {
                if (!g_autoPanel[panel]) continue;
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[hitRows[i]], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[hitRows[i]], panel) : getPanelValue(&g_chart->rows[hitRows[i]], panel, p));
                if (!val) continue;
                /* explosão como num acerto manual (processRowJudgment): antes o
                 * autoplay limpava a nota sem ligar o efeito */
                g_noteState[p][panel] = 1;
                g_noteExplodeRow[p][panel] = hitRows[i];
                g_noteExplodeFrame[p][panel] = 0;
                g_glowTimer[p][panel] = 24;
                if (isHD) clearHDPanel(&g_chart->rows[hitRows[i]], panel);
                else if (dnAP) clearDNPanel(&g_chart->rows[hitRows[i]], panel);
                else clearPanel(&g_chart->rows[hitRows[i]], panel, p);
            }
        }
    }
}
                
/* Explosão/glow da seta ao pegar a cabeça do long. X1Rus playengine.cpp
 * (~2773): nota longa julgada em QUALQUER zona faz m_LongFade = 1 e
 * m_AniFade = 0. Antes o port só disparava o efeito via processRowJudgment,
 * que pula GOOD/BAD. Mesmos campos usados em processHolds. */
static void holdHitFx(int player, int pan, int row)
{
    g_noteState[player][pan] = 1;
    g_noteExplodeRow[player][pan] = row;
    g_noteExplodeFrame[player][pan] = 0;
    g_glowTimer[player][pan] = 24;
}

/* Linha SÓ com nota longa (H/B/T), sem tap, para o jogador. X1Rus playengine.cpp
 * (~2773): long julgado em qualquer zona = JUDGE_PERFECT; fora da zona (ou sem
 * botão) é MISS. Não existe GREAT/GOOD/BAD de long.
 * Linha com long + tap: vale o julgamento do tap pelo tempo (conta uma vez no
 * combo; long solto nessa linha = MISS, tratado em processPendingRows). */
static bool rowOnlyLong(int player, int row)
{
    bool isHD = isHDMode(), isDN = isDNMode();
    int panCount = isHD ? 6 : (isDN ? 10 : 5);
    bool hasLong = false;
    for (int pan = 0; pan < panCount; pan++) {
        uint8_t v = isHD ? getNoteHD(&g_chart->rows[row], pan)
                  : (isDN ? getDNPanelValue(&g_chart->rows[row], pan)
                  : getPanelValue(&g_chart->rows[row], pan, player));
        if (v == NT_HOLD_H || v == NT_HOLD_B || v == NT_HOLD_T) hasLong = true;
        else if (v) return false;   /* tem tap: julgamento do tap */
    }
    return hasLong;
}

/* Botão do painel apertado AGORA (long_stat do X1Rus DrawStepLine), sem
 * depender do estado de captura do hold. Mesmo mapeamento de processHolds. */
static bool holdPanelDown(int p, int panel)
{
    if (g_autoPanel[panel]) return true;
    if (isHDMode()) return Input_IsPadDown(hdPanelPlayer(panel), hdPanelBtn(panel));
    if (isDNMode()) return Input_IsPadDown(dnPanelPlayer(panel), dnPanelBtn(panel));
    static const PadButton panelToBtn[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
    return Input_IsPadDown(p, panelToBtn[panel]);
}

static void processHolds(void)
{
    if (!g_songLoaded) return;
    bool isHD = isHDMode();
    bool dnAP = isDNMode();
    int panCount = isHD ? 6 : (dnAP ? 10 : 5);
    /* Itera players ativos: P1 (0) e/ou P2 (1). HD/DN usam sempre p=0. */
    int _hp0 = (isHD || dnAP) ? 0 : ((g_game.activePlayerMask == 0x2) ? 1 : 0);
    int _hp1 = (isHD || dnAP) ? 1 : ((g_game.activePlayerMask == 0x3) ? 2 : _hp0 + 1);
    for (int p = _hp0; p < _hp1; p++)
    {
        ctxUse(p);   /* chart do jogador */
        for (int panel = 0; panel < panCount; panel++)
        {
            int holdPly;
            PadButton holdBtn;
            if (isHD) {
                holdPly = hdPanelPlayer(panel);
                holdBtn = hdPanelBtn(panel);
            } else if (dnAP) {
                holdPly = dnPanelPlayer(panel);
                holdBtn = dnPanelBtn(panel);
            } else {
                static const PadButton panelToBtn[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                holdPly = p;  /* era 0 (hardcoded P1) — corrigido para p (player atual) */
                holdBtn = panelToBtn[panel];
            }
            bool held = g_autoPanel[panel] ? true : Input_IsPadDown(holdPly, holdBtn);

            // Auto-capture: botao segurado e HH ou HB/HT nao capturado (re-press)
            if (g_holdRows[p][panel] < 0 && held)
            {
                const double lead = holdLeadSec(isHD || dnAP);
                for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
                {
                    uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                    if (!val) continue;
                    double rt = getRowTime(ri);
                    double diff = g_songTime - rt;
                    if (diff < -lead) break;   /* ainda no futuro: o botão segurado só consome a partir do 'lead' */
                    /* Faltava o limite do PASSADO: sem ele, segurar o botão
                     * capturava rows de hold já vencidas há muito tempo, cada
                     * uma disparando explosion e holdbody do nada. Mesmo padrão
                     * das linhas 817-818, inclusive avançando o cursor para não
                     * revisitar a row todo frame. */
                    if (diff > judgeBadLate()) { g_nextNoteRow[p][panel] = ri + 1; continue; }
                    if (val == NT_HOLD_H || val == NT_HOLD_B || val == NT_HOLD_T) {
                        g_holdRows[p][panel] = ri;
                        holdOpenDbg("capture", p, panel, ri);
                        g_nextNoteRow[p][panel] = ri + 1;
                        if (isHD) clearHDPanel(&g_chart->rows[ri], panel);
                        else if (dnAP) clearDNPanel(&g_chart->rows[ri], panel);
                        else clearPanel(&g_chart->rows[ri], panel, p);
                        g_noteState[p][panel] = 1;
                        g_noteExplodeRow[p][panel] = ri;
                        g_noteExplodeFrame[p][panel] = 0;
                        g_glowTimer[p][panel] = 24;
                        int hasTap = 0;
                        for (int pan = 0; pan < panCount; pan++)
                            if (pan != panel && (isHD ? getNoteHD(&g_chart->rows[ri], pan) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], pan) : getPanelValue(&g_chart->rows[ri], pan, p)))) { hasTap = 1; break; }
                        if (!hasTap) {
                            /* grade pelo tempo real (re-aperto tardio dá Great/Good/Bad) e com life */
                            /* era: evaluateTiming(diff), MISS -> BAD. Long no original
                             * é sempre PERFECT dentro da zona (ver rowOnlyLong). */
                            JudgeType hjt = JT_PERFECT;
                            (void)diff;
                            applyRowJudgment(p, hjt);
                        }
                        /* Captura é uma só. Sem este break o laço seguia varrendo
                         * e re-disparava o explosion (g_noteState/g_glowTimer)
                         * para cada row de hold adiante — os "holds fantasmas"
                         * que apareciam sem pontuar. */
                        break;
                    }
                }
            }

            if (g_holdRows[p][panel] < 0) continue;

            if (!holdHasRowsAhead(p, panel, isHD, dnAP)) {
                Log_Print("HOLD: p=%d pan=%d preso na row=%d sem corpo/cauda a frente, encerrando\n",
                          p, panel, g_holdRows[p][panel]);
                g_holdRows[p][panel] = -1;
                continue;
            }

            for (int ri = g_holdRows[p][panel] + 1; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (val != 0 && val != NT_HOLD_B && val != NT_HOLD_T) break;
                if (val == 0) continue;

                double rowTime = getRowTime(ri);
                if (g_songTime < rowTime - holdLeadSec(isHD || dnAP)) break;

                bool alreadyJudged = false;
                for (int h = 0; h < g_noteHitCount[p][panel]; h++)
                    if (g_noteHits[p][panel][h].rowIndex == ri) { alreadyJudged = true; break; }
                if (alreadyJudged) continue;

                if (!held)
                {
                    /* Carência (PUMPY.EXE, verificado): sem botão a linha só é perdida DEPOIS do
                     * limite tardio do Bad; até lá dá para reapertar e ainda acertá-la. */
                    if (g_songTime <= rowTime + judgeBadLate()) break;
                    applyRowJudgment(p, JT_MISS);
                }
                else
                {
                    /* O original julga por LINHA, não por painel (JudgeStep: cada nota
                     * tratada ganha +128 e a linha só vira JUDGE_END — um julgamento,
                     * +1 combo — quando todas foram tratadas). Antes só um tap em outro
                     * painel segurava o julgamento; corpos B/T de outros holds na mesma
                     * linha não, e 2-3 holds juntos davam +2/+3 por linha — com
                     * BeatSplit alto o combo disparava. Agora o último painel da linha
                     * é quem julga; se sobrar hold solto, processMisses dá o MISS. */
                    int hasUnjudgedTap = false;
                    for (int op = 0; op < panCount; op++) {
                        if (op == panel) continue;
                        uint8_t ov = isHD ? getNoteHD(&g_chart->rows[ri], op) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], op) : getPanelValue(&g_chart->rows[ri], op, p));
                        /* if (ov && ov != NT_HOLD_B && ov != NT_HOLD_T) { hasUnjudgedTap = true; break; } */
                        if (ov) { hasUnjudgedTap = true; break; }
                    }
                    if (isHD) clearHDPanel(&g_chart->rows[ri], panel);
                    else if (dnAP) clearDNPanel(&g_chart->rows[ri], panel);
                    else clearPanel(&g_chart->rows[ri], panel, p);
                    g_noteState[p][panel] = 1;
                    g_noteExplodeRow[p][panel] = ri;
                    g_noteExplodeFrame[p][panel] = 0;
                    g_glowTimer[p][panel] = 24;
                    if (!hasUnjudgedTap) {
                        /* era: evaluateTiming(g_songTime - rowTime), MISS -> BAD.
                         * Long no original é sempre PERFECT (ver rowOnlyLong). */
                        JudgeType hjt = JT_PERFECT;
                        applyRowJudgment(p, hjt);
                    }
                }

                if (val == NT_HOLD_T || !held)
                {
                    g_holdRows[p][panel] = -1;
                    g_nextNoteRow[p][panel] = ri + 1;
                    /* Acabou o hold: parar aqui. Sem o break o laço seguia
                     * varrendo a lane com g_holdRows já em -1 e podia consumir
                     * rows de um hold posterior cuja head não foi julgada,
                     * disparando explosion sem dono. */
                    break;
                }
            }
        }
    }
}

static void processMisses(void)
{
    if (!g_songLoaded) return;

    bool isHD = isHDMode();
    bool dnAP = isDNMode();
    int panCount = isHD ? 6 : (dnAP ? 10 : 5);

    double missThreshold = g_songTime - judgeBadLate();
    /* Itera players ativos. HD/DN usam p=0; single usa p=0, p=1, ou ambos. */
    int _mp0 = (isHD || dnAP) ? 0 : ((g_game.activePlayerMask == 0x2) ? 1 : 0);
    int _mp1 = (isHD || dnAP) ? 1 : ((g_game.activePlayerMask == 0x3) ? 2 : _mp0 + 1);
    for (int p = _mp0; p < _mp1; p++)
    {
        ctxUse(p);   /* chart do jogador */
        int missedRows[256], missCount = 0;
        for (int panel = 0; panel < panCount; panel++)
        {
            int missPly;
            PadButton missBtn;
            if (isHD) {
                missPly = hdPanelPlayer(panel);
                missBtn = hdPanelBtn(panel);
            } else if (dnAP) {
                missPly = dnPanelPlayer(panel);
                missBtn = dnPanelBtn(panel);
            } else {
                static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                missPly = p;  /* era 0 (hardcoded P1) — corrigido para p (player atual) */
                missBtn = btnMap[panel];
            }
            for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (!val) continue;
                if (val == NT_HOLD_H || val == NT_HOLD_B || val == NT_HOLD_T)
                {
                    if (g_holdRows[p][panel] >= 0) continue;
                    if (Input_IsPadDown(missPly, missBtn)) continue;
                }

                double rowTime = getRowTime(ri);
                if (missThreshold <= rowTime) break;

                g_nextNoteRow[p][panel] = ri + 1;
                int dup = 0;
                for (int m = 0; m < missCount; m++)
                    if (missedRows[m] == ri) { dup = 1; break; }
                if (!dup) missedRows[missCount++] = ri;
            }
        }

        if (missCount > 0)
        {
            g_game.stats.combo[p] = 0;
            g_game.stats.missCount[p] += missCount;
            g_game.stats.missCombo[p] += missCount;
            g_judgeDisplayType[p] = JT_MISS;
            g_judgeDisplayTimer[p] = 0.6f;
            g_judgeFrame[p] = 25; g_exJudgeCnt[p] = 0;
            g_judgeDisplayCombo[p] = g_game.stats.missCombo[p];
            for (int m = 0; m < missCount; m++)
                applyLife(p, JT_MISS); /* penalidade por linha perdida */
            for (int m = 0; m < missCount; m++)
            {
                for (int pan = 0; pan < panCount; pan++)
                {
                    uint8_t val = isHD ? getNoteHD(&g_chart->rows[missedRows[m]], pan) : (dnAP ? getDNPanelValue(&g_chart->rows[missedRows[m]], pan) : getPanelValue(&g_chart->rows[missedRows[m]], pan, p));
                    if (val)
                    {
                        NoteHit* nh = &g_noteHits[p][pan][g_noteHitCount[p][pan]++];
                        nh->rowIndex = missedRows[m];
                        nh->judged = true;
                        nh->judgment = JT_MISS;
                        nh->hitTime = g_songTime;
                    }
                }
            }
        }
    }
}

void Gameplay_Start(int songId)
{
    g_stageBreakFreezeTimer = -1.0f;
    if (getenv("PUMPY_AUTOPLAY")) for (int a_ = 0; a_ < MAX_PANELS; a_++) g_autoPanel[a_] = true;   /* teste */
    memset(&g_game.stats, 0, sizeof(g_game.stats));
    memset(s_exPrev, 0, sizeof(s_exPrev));
    g_game.stats.life[0]      = LIFE_INITIAL; /* source oficial: m_Gauge = 500 (era 224, ajuste visual) */
    g_game.stats.life[1]      = LIFE_INITIAL;
    if (g_exceedSongIds) {
        /* exceed.exe 0x4026D6 / 0x4026EE: [player+0x168] = 500 (0x1F4) para
         * cada jogador ativo — o mesmo m_Gauge = 500 do playengine.cpp. */
        g_game.stats.life[0] = LIFE_INITIAL;
        g_game.stats.life[1] = LIFE_INITIAL;
    }
    /* lifeSpeed inicial varia por nível (GameInit 0x00411381):
     * easy=500, normal=300, hard=100. Antes era fixo em 500, o perfil do easy. */
    g_game.stats.lifeSpeed[0] = k_lifeSpeedInit[lifeLevel()];
    g_game.stats.lifeSpeed[1] = k_lifeSpeedInit[lifeLevel()];
    memset(g_judgeDisplayTimer, 0, sizeof(g_judgeDisplayTimer));
    memset(g_hitTimer, 0, sizeof(g_hitTimer));
    memset(g_glowTimer, 0, sizeof(g_glowTimer));
    memset(g_p1FlashTimer, 0, sizeof(g_p1FlashTimer));
    memset(g_noteState, 0, sizeof(g_noteState));
    memset(g_noteExplodeFrame, 0, sizeof(g_noteExplodeFrame));
    for (int p = 0; p < 2; p++)
        for (int pan = 0; pan < MAX_PANELS; pan++)
            g_holdRows[p][pan] = -1;

    g_game.bgaFrame = 0;
    g_blindTimer[0] = 0;
    g_blindTimer[1] = 0;
    g_prevBlindRow = -1;
    memset(g_lastPerfectRow, -1, sizeof(g_lastPerfectRow));
    g_pendingCount = 0;
    memset(g_pending, 0, sizeof(g_pending));
    // Limpa estados de input (nada de input preso do menu)
    memset(g_game.input.padState, 0, sizeof(g_game.input.padState));
    memset(g_game.input.padPrevState, 0, sizeof(g_game.input.padPrevState));
    Log_Print("GP: initialized\n");

    // Igual Font_LoadFontAndArrows no Ghidra — carrega font.tga, dec00.tga e todos os SPRs da 00.DAT
    /* Zero (piu 0x80806f0): receptores (01/02, w01/w02, hd01/hd02), arrowf/arrowp,
     * faíscas e a lifebar (gg_s/gg_d + GG.png) vêm do BGA/SKINxx.DAT da skin do
     * jogador; o 00.DAT do Zero só tem m01..m05. */
    {
        char datPath[MAX_PATH];
        snprintf(datPath, sizeof(datPath), "%s/BGA/SKIN%02d.DAT", g_game.currentDirectory, Zero_SkinIndexP(0));   /* era: Zero_SkinIndex() */
        Resource_LoadFontAndArrows(datPath);
        /* era (Prex3/Exceed): "%s/BGA/00.DAT" */
        /* 0x80860d1: m01..m04 (indicador de estágio) do BGA/00.DAT */
        snprintf(datPath, sizeof(datPath), "%s/BGA/00.DAT", g_game.currentDirectory);
        if (RES_Open(datPath)) {
            /* era (Zero): m01..m05 em M01..M05.
             * NX 0x806e440: "m0%d.spr", i = 0..3 (m00 1st, m01 2nd, m02 Final, m03 BONUS);
             * 0x8069b00 escolhe [0x81f8910] (stage) ou 2 + extra. Aqui vão para
             * M01..M04, que o desenho abaixo já usa como 1st/2nd/final/extra. */
            g_fontSprM05 = -1;
            {   /* NX 0x806e40c: nxgauge.spr (lifebar) antes dos m0%d */
                int start = g_game.sprTileCount;
                SPR_LoadSPR("nxgauge.spr", NULL, NULL, NULL);
                s_nxGauge = (g_game.sprTileCount >= start + 9) ? start : -1;
            }
            int* const mv[4] = { &g_fontSprM01, &g_fontSprM02, &g_fontSprM03, &g_fontSprM04 };
            for (int i = 0; i < 4; i++) {
                char nm[16];
                snprintf(nm, sizeof(nm), "m0%d.spr", i);
                int start = g_game.sprTileCount;
                SPR_LoadSPR(nm, NULL, NULL, NULL);
                *mv[i] = (g_game.sprTileCount > start) ? start : -1;
            }
            RES_Close();
        }
    }
    /* Exceed2 0x405960: BGA/00.DAT também é carregado como BGA ([0x484FD8]);
     * julgamento/combo são cenas dele. Fica como mais um BGA depois do da música. */
    g_exJudgeBga = -1;
    memset(g_exJudgeCnt, 0, sizeof(g_exJudgeCnt));
    /* Sem isto o tipo do último julgamento da música anterior ficava, e com o
     * contador em 0 o PERFECT/MISS dele tocava no início da música seguinte. */
    g_judgeDisplayType[0] = g_judgeDisplayType[1] = JT_NONE;
    /* era: if (g_exceedSongIds) exLoadSkin(); + mapeamento das setas (uma skin só).
     * Zero: a nota é o próprio skinN.spr do SKINxx.DAT (não há ARROW54x). */
    if (g_exceedSongIds) nxLoadSkins();   /* NX2 CPlayer::LoadSkin por jogador */
    /* Zero 0x8080953: julgamento/combo em BGA/COMBO.DAT (mesmas cenas e slots
     * do 00.BGA do Exceed2: PERFECT.., PER-2P.., PER-D.., dígitos 10..13 <- 14..23) */
    if (g_exceedSongIds && Resource_LoadBGAByName("COMBO")) {
        int bi = g_game.bgaPicCount - 1;
        if (g_game.bgaPics[bi].version == 3 && g_game.bgaPics[bi].sceneCount > 0) g_exJudgeBga = bi;
        Log_Print("GP: 00.BGA como BGA %d (v%d, %d cenas)\n", bi, g_game.bgaPics[bi].version, g_game.bgaPics[bi].sceneCount);
    }

    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int diffTier = g_game.selectedDifficulty;
    loadChartForSong(songId, diffTier, mode->name);

    /* 2P Single: duplicar half1 → half2 para que P2 veja os mesmos padrões de P1.
     * NUNCA fazer em DN/HD: esses modos já têm ambos os halves populados pelo chart
     * original — sobrescrever half2 destruiria os dados do pad direito. */
    if (g_game.activePlayerMask == 0x3 && g_songLoaded && g_chart
        && !isDNMode() && !isHDMode()) {
        for (int ri = 0; ri < (int)g_chart->rowCount; ri++)
            g_chart->rows[ri].half2 = g_chart->rows[ri].half1;
        Log_Print("GP: 2P single — duplicated half1 -> half2 (%d rows)\n", g_chart->rowCount);
    }

    if (g_game.activePlayerMask == 0x3 && g_songLoaded && g_chart && !isDNMode() && !isHDMode())
        nxBuildP2Ctx();   /* NX: chart do P2 com outra dificuldade (CPlayer : CStep) */

    /* Modificadores de chart aplicados no load time (apos duplicacao 2P).
     * Ordem: Mirror primeiro, depois Random Step (RS sobre mirror se ambos ativos). */
    if (g_songLoaded && g_chart) {
        int chartMode = isHDMode() ? 2 : (isDNMode() ? 1 : 0);

        /* Mirror: permutacao fixa por modo (Z<->E etc) */
        bool mP1 = (g_game.activePlayerMask & 0x1) && g_game.cmdMirror[0];
        bool mP2 = (g_game.activePlayerMask & 0x2) && g_game.cmdMirror[1];
        if (g_ctxN > 1) {
            if (mP1) Step_ApplyMirror(g_ctx[0].chart, chartMode, true, false);
            if (mP2) Step_ApplyMirror(g_ctx[1].chart, chartMode, false, true);
        } else if (mP1 || mP2)
            Step_ApplyMirror(g_chart, chartMode, mP1, mP2);

        /* Random Step: permutacao aleatoria por row */
        bool rsP1 = (g_game.activePlayerMask & 0x1) && g_game.cmdRandomStep[0];
        bool rsP2 = (g_game.activePlayerMask & 0x2) && g_game.cmdRandomStep[1];
        if (g_ctxN > 1) {
            if (rsP1) Step_ApplyRandomShuffle(g_ctx[0].chart, chartMode, true, false);
            if (rsP2) Step_ApplyRandomShuffle(g_ctx[1].chart, chartMode, false, true);
        } else if (rsP1 || rsP2)
            Step_ApplyRandomShuffle(g_chart, chartMode, rsP1, rsP2);
    }

    /* g_hasAudio nunca era atribuida (sempre false): o fim por "musica acabou"
     * nao rodava e o jogo esperava o chart inteiro (ex.: 815 = 148 s de chart,
     * 95 s de musica). A BGM ja foi carregada pelo Loading antes desta chamada. */
    zeroJudgeReset();
    g_hasAudio = (BGM_GetDurationMs() > 0);
    Log_Print("Gameplay: started song %d (audio=%d, %u ms)\n", songId, (int)g_hasAudio, BGM_GetDurationMs());
}

/* Exceed (exceed.exe 0x409BC2..0x40A0CD): PERFECT e GREAT já batem com o
 * código herdado (+1000/+500, +1000 com combo >= 4). O Exceed ainda soma
 * GOOD +100 (0x409DAB), BAD -700 (0x409C1A) e MISS -1000 (0x409CAE), e o
 * score nunca fica negativo (0x409DB6). Em vez de mexer em cada ponto de
 * julgamento, aplica a diferença dos contadores desde a última chamada
 * (a ordem dentro de um mesmo quadro pode diferir do original no clamp). */
void Gameplay_ExScoreSync(void)
{
    if (!g_exceedSongIds) return;
    for (int p = 0; p < 2; p++) {
        unsigned cur[3] = { g_game.stats.goodCount[p], g_game.stats.badCount[p],
                            g_game.stats.missCount[p] };
        long sc = (long)g_game.stats.score[p];
        sc += 100L * (long)(cur[0] - s_exPrev[p][0]);
        for (unsigned k = s_exPrev[p][1]; k < cur[1]; k++) { sc -= 700;  if (sc < 0) sc = 0; }
        for (unsigned k = s_exPrev[p][2]; k < cur[2]; k++) { sc -= 1000; if (sc < 0) sc = 0; }
        if (sc < 0) sc = 0;
        g_game.stats.score[p] = (unsigned)sc;
        for (int i = 0; i < 3; i++) s_exPrev[p][i] = cur[i];
    }
}

void Gameplay_Exit(void)
{
    BGM_Stop();

    if (g_songLoaded)
    {
        Step_FreeSong(&g_playSong);
        g_songLoaded = false;
    }
    ctxFree();
    free(g_visualRow);
    g_visualRow = NULL;
    g_visualRowCount = 0;
    Log_Print("Gameplay: exit\n");
}

/* Chamado antes de cada desenho: com a âncora já congelada, põe g_songTime no
 * instante atual (contador de alta resolução), para a rolagem ficar lisa em
 * qualquer refresh. Só avança (nunca volta) e não mexe na âncora. */
void Gameplay_RefreshClock(void)
{
    if (g_game.state != STATE_GAMEPLAY || !g_songLoaded || !g_clkLocked) return;
    if (!BGM_IsDSActive() || g_stageBreakFreezeTimer >= 0.0f) return;
    double now;
    if (BGM_ClockAnchorSec(&now) < 0.0) return;
    double t = (now - g_clkAnchor) - (g_game.audioOffsetMs / 1000.0);
    if (t > g_songTime) g_songTime = t;
}

void Gameplay_Update(float dt)
{
    if (g_game.state != STATE_GAMEPLAY) return;
    if (!g_songLoaded) return;
    if (dt > 0.05f) dt = 0.05f;

    Gameplay_ExScoreSync();

    /* Demo: 0x402B82 — crédito ou 35 s -> IDLE */
    if (g_exDemo && (Coin_HasCredit() || g_songTime >= 35.0)) {
        Demo_End();
        return;
    }

    /* Stage Break: freeze de 0.5s depois do trigger, antes de mostrar 083.DAT */
    if (g_stageBreakFreezeTimer >= 0.0f) {
        g_stageBreakFreezeTimer -= dt;
        if (g_stageBreakFreezeTimer < 0.0f) {
            g_stageBreakFreezeTimer = -1.0f;
            Game_ChangeState(STATE_STAGE_BREAK);
        }
        return;
    }

    {
        bool hdAP = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                     strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "HALFDOUBLE") == 0);
        bool dnAP = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                    (strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "DOUBLE") == 0 ||
                     strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "NIGHTMARE") == 0));
        int apCount = dnAP ? 10 : (hdAP ? 6 : 5);

        /* Autoplay por painel individual (F1..F10) — desativado.
         * O F1 passou a abrir o menu de serviço e as demais F* colidiam com
         * outros atalhos. Agora o F8 liga/desliga o autoplay de todas as setas
         * de uma vez (bloco abaixo).
         *
        int apKeys[10] = { VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10 };
        for (int a = 0; a < apCount; a++)
        {
            if (Input_IsKeyHit(apKeys[a]))
            {
                g_autoPanel[a] = !g_autoPanel[a];
                Log_Print("GP: autoplay %d: %s\n", a, g_autoPanel[a] ? "ON" : "OFF");
            }
        }
        */

        /* F8: alterna o autoplay de todos os painéis do modo atual */
        if (Input_IsKeyHit(VK_F8))
        {
            bool allOn = true;
            for (int a = 0; a < apCount; a++)
                if (!g_autoPanel[a]) { allOn = false; break; }

            for (int a = 0; a < MAX_PANELS; a++)
                g_autoPanel[a] = !allOn;

            Log_Print("GP: autoplay (todas as setas): %s\n", allOn ? "OFF" : "ON");
        }

        g_autoplay = true;
        for (int a = 0; a < apCount; a++)
            if (!g_autoPanel[a]) { g_autoplay = false; break; }
    }

    // Scroll speed adjustment (number keys 1-8, smooth animation) — debug only
    for (int k = '1'; k <= '8'; k++)
    {
        if (Input_IsKeyHit(k))
        {
            float spd = (float)(k - '0');
            for (int _dp = 0; _dp < 2; _dp++) {
                if (!(g_game.activePlayerMask & (1 << _dp))) continue;
                g_scrollSpeedTarget[_dp]    = spd;
                g_game.cmdEarthworm[_dp]    = false; /* accel remove Earthworm */
            }
            Log_Print("GP: speed target %.0fX\n", spd);
        }
    }

    // ── Velocidade por player: RV, Earthworm, interpolação ──────────────
    {
        int currentRow = (int)getRowAtTimeFloat(g_songTime);

        for (int _p = 0; _p < 2; _p++)
        {
            if (!(g_game.activePlayerMask & (1 << _p))) continue;

            /* RV e Earthworm só trocam a velocidade na virada de compasso (48 rows).
             * Original (PUMPY.EXE 0x4141cc): DAT_00da24b4 % 0x30 == 0 && != DAT_00da24bc.
             * g_rvLastMeasure[p] inicia em 0 (pula row 0). */
            if (g_game.cmdRandomVelocity[_p]
                && currentRow > 0 && currentRow % 48 == 0 && currentRow != g_rvLastMeasure[_p])
            {
                g_rvLastMeasure[_p] = currentRow;

                /* RV (0x41421a): (rand() % 4 + 1) * 1000 → x1..x4, pode repetir */
                if (g_game.cmdRandomVelocity[_p])
                    g_scrollSpeedTarget[_p] = (float)(rand() % 4 + 1);

            }

            /* Earthworm (0x4142a1): NÃO depende da virada de compasso — o
             * "jne 0x41428e" em 0x4141e7 pula o % 0x30 e só exige row != última
             * row (DAT_00da24bc, gravado a cada frame em 0x412998). Ou seja,
             * sorteia novo alvo a cada row; a rampa gera a ondulação.
             * Alterna conforme o tick DAT_00d35eac (via 0x4024f0). NÃO é ms:
             * o callback de timeSetEvent(1ms) em 0x41a1c0 só incrementa quando
             * (n*240)/1000 muda → tick de 240 Hz. BPM <= 180: x3 se
             * tick%120 <= 60, senão x2 (onda de 0,5 s); BPM > 180: x2 se
             * tick%90 <= 45, senão x1 (onda de 0,375 s). */
            if (g_game.cmdEarthworm[_p] && currentRow != g_ewLastRow[_p]) {
                uint32_t tick = (uint32_t)((uint64_t)timeGetTime() * 240u / 1000u);
                if (g_baseBpm <= 180.0)
                    g_scrollSpeedTarget[_p] = (tick % 120 <= 60) ? 3.0f : 2.0f;
                else
                    g_scrollSpeedTarget[_p] = (tick % 90 <= 45) ? 2.0f : 1.0f;
            }
            g_ewLastRow[_p] = currentRow;

            /* Rampa linear até o alvo: ±50/1000 por frame no original (0x414888),
             * ou seja ~3x por segundo a 60 fps. */
            /* Velocidade do bloco atual — PUMPY.EXE 0x4118d0: alvo = velocidade do
             * jogador, ou velBloco*0,001*velJogador se velBloco != 0. A rampa
             * abaixo (±0,05x/frame, 0x414888) é a mesma do 1/2/3.
            float speedDiff = g_scrollSpeedTarget[_p] - g_scrollSpeedX[_p]; */
            float blockMul = 1.0f;
            if (g_chart && currentRow >= 0) {
                for (uint32_t s = 0; s < g_chart->segmentCount; s++) {
                    uint32_t s0 = g_chart->segments[s].rowStart;
                    if ((uint32_t)currentRow >= s0 && (uint32_t)currentRow < s0 + g_chart->segments[s].rowCount) {
                        if (g_chart->segments[s].speed > 0) blockMul = (float)g_chart->segments[s].speed * 0.001f;
                        break;
                    }
                }
            }
            float speedDiff = g_scrollSpeedTarget[_p] * blockMul - g_scrollSpeedX[_p];
            float speedStep = 3.0f * dt;
            if (fabsf(speedDiff) > speedStep)
                g_scrollSpeedX[_p] += (speedDiff > 0.0f) ? speedStep : -speedStep;
            else
                g_scrollSpeedX[_p] = g_scrollSpeedTarget[_p] * blockMul; /* era: g_scrollSpeedTarget[_p] */
        }
    }

    /* era:
     *     if (BGM_IsDSActive()) {
     *         /* era: g_songTime = posMs/1000 - offset todo frame (ms inteiros +
     *          * clamps do callback = delta irregular por frame -> setas trepidando).
     *          * Agora avança por dt e só puxa suavemente pro relógio do áudio
     *          * (mesma correção da Prex3). * /
     *         double posMs = BGM_GetPositionMsF();
     *         g_songTime += dt;
     *         if (posMs > 100.0) { // ignore first 100ms (startup)
     *             double audioT = posMs / 1000.0 - (g_game.audioOffsetMs / 1000.0); /* offset configuravel em PUMPY.INI (AudioOffset=X ms) * /
     *             double err = audioT - g_songTime;
     *             if (err > 0.05 || err < -0.05)
     *                 g_songTime = audioT;          /* desvio grande: ressincroniza * /
     *             else
     *                 g_songTime += err * 0.1;      /* desvio pequeno: corrige suave * /
     *         }
     *     } else {
     *         g_songTime += dt;
     *     }
     */
    if (BGM_IsDSActive()) {
        /* Relógio fixo (sem ajuste de ms durante a música):
         *   âncora = instante em que a amostra 0 saiu, medida a cada callback.
         *   Callbacks atrasados dão âncora maior, então no 1º segundo fica a
         *   MENOR; depois ela congela e g_songTime = agora - âncora, avançando
         *   pelo contador de alta resolução, sem tremer nem ser corrigido.
         *   Só reancora num desvio real (> 100 ms: travada do áudio/loop). */
        double now, anc = BGM_ClockAnchorSec(&now);
        if (anc >= 0.0) {
            if (!g_clkLocked) {
                if (!g_clkHave || anc < g_clkAnchor) g_clkAnchor = anc;
                g_clkHave = true;
                if (now - g_clkAnchor >= 1.0) g_clkLocked = true;
            } else {
                double d = anc - g_clkAnchor;
                if (d > 0.1 || d < -0.1) {
                    Log_Print("GP: relogio reancorado (desvio %.1f ms)\n", d * 1000.0);
                    g_clkAnchor = anc;
                }
            }
            g_songTime = (now - g_clkAnchor) - (g_game.audioOffsetMs / 1000.0); /* offset configuravel em PUMPY.INI (AudioOffset=X ms) */
        } else {
            g_songTime += dt;
        }
    } else {
        g_songTime += dt;
    }

    {
        int maxFrame = g_game.bgaMaxFrame;
        if (g_game.isVSL && g_vsl.active && g_vsl.frameCount > 0)
            maxFrame = g_vsl.frameCount - 1;
        if (maxFrame > 0) {
            float bgaTime = (float)g_songTime;
            if (bgaTime < 0.0f) bgaTime = 0.0f;
            int newFrame = (int)(bgaTime * 60.0f);
            if (newFrame > maxFrame) newFrame = maxFrame;
            g_game.bgaFrame = newFrame;
        }
    }

    if (g_zeroSkinArrows && !isHDMode()) {
        /* Zero: julgamento por linha (0x808a760) no lugar de input/holds/misses/autoplay */
        if (isDNMode()) zeroJudge(0);
        else {
            if (g_game.activePlayerMask & 0x1) zeroJudge(0);
            if (g_game.activePlayerMask & 0x2) { ctxUse(1); zeroJudge(1); ctxUse(0); }
        }
    } else {
    processInput(0);
    if (g_game.activePlayerMask & 0x2) { ctxUse(1); processInput(1); ctxUse(0); }
    processPendingRows(0);
    if (g_game.activePlayerMask & 0x2) { ctxUse(1); processPendingRows(1); ctxUse(0); }
    processAutoplay();
    processHolds();
    processMisses();
    ctxUse(0);
    }

    /* Stage Break: 51 miss consecutivos OU lifebar == 0 (se opção ativa) */
    {
        bool sbTrigger = false;
        bool twoP = (g_game.activePlayerMask == 0x3);

        /* Fim de jogo — PUMPY.EXE 0x4149ee-0x414a77.
         * STAGE BREAK é um número (0=OFF, 1..4), não um liga/desliga. A checagem de life só vale se
         *   opção != 0  E  (opção - 1) <= estágio atual (0-based)  E  não é o 1º estágio
         * (o 1º estágio nunca falha por life). Com 2 jogadores só termina quando AMBOS estão com life < 1.
         * Já o missCombo > 50 (0x32) vale SEMPRE, com ou sem stage break. */
        int opt = g_game.optionToggle1;
        /* era (Prex3): stageIdx = 3 - stageCount (no NX a Loading já decrementou: a 1ª
         * música dava 1) e lifeFailActive = opt && (opt-1) <= stageIdx && stageIdx != 0
        int stageIdx = 3 - g_game.stageCount;
        if (stageIdx < 0) stageIdx = 0;
        bool lifeFailActive = (opt != 0) && ((opt - 1) <= stageIdx) && (stageIdx != 0);
        */
        /* NX 0x806d243..0x806d2df: estágio = [0x81f8910] (0-based); vida só derruba com
         * opção != 0 e estágio + 1 >= opção (WORLD: sempre); miss > 50 fora do WORLD;
         * TRAINING e EVENT ([0x9e3dbce] == 1) nunca caem */
        int stageIdx = g_game.isBonusSong ? 3 : 2 - g_game.stageCount;
        if (stageIdx < 0) stageIdx = 0;
        bool nxWorld = g_game.nxGameMode == 2;
        bool lifeFailActive = nxWorld || ((opt != 0) && (stageIdx + 1 >= opt));
        bool missFailActive = !nxWorld;
        if (g_game.nxGameMode == 3 || g_game.svcGameMode == 1) lifeFailActive = missFailActive = false;

        if (lifeFailActive) {
            if (twoP) {
                if (g_game.stats.life[0] < 1 && g_game.stats.life[1] < 1) {
                    Log_Print("GP: stage break 2P ambas vidas<1 (opt=%d stage=%d)\n", opt, stageIdx);
                    sbTrigger = true;
                }
            } else {
                for (int _p = 0; _p < 2; _p++) {
                    if (!(g_game.activePlayerMask & (1 << _p))) continue;
                    if (g_game.stats.life[_p] < 1) {
                        Log_Print("GP: stage break P%d life<1 (opt=%d stage=%d)\n", _p+1, opt, stageIdx);
                        sbTrigger = true;
                    }
                }
            }
        }

        if (!sbTrigger && missFailActive) {
            if (twoP) {
                if (g_game.stats.missCombo[0] > STAGE_BREAK_MISSES && g_game.stats.missCombo[1] > STAGE_BREAK_MISSES) {
                    Log_Print("GP: stage break 2P missCombo>50 em ambos\n");
                    sbTrigger = true;
                }
            } else {
                for (int _p = 0; _p < 2; _p++) {
                    if (!(g_game.activePlayerMask & (1 << _p))) continue;
                    if (g_game.stats.missCombo[_p] > STAGE_BREAK_MISSES) {
                        Log_Print("GP: stage break P%d missCombo=%d\n", _p+1, g_game.stats.missCombo[_p]);
                        sbTrigger = true;
                    }
                }
            }
        }

        if (sbTrigger) {
            BGM_Stop();
            g_stageBreakFreezeTimer = 0.5f; /* trava 0.5s antes de mostrar 083.DAT */
            return;
        }
    }

    for (int p = 0; p < 2; p++)
    {
        if (g_judgeDisplayTimer[p] > 0)
            g_judgeDisplayTimer[p] -= dt;
        if (g_judgeFrame[p] > 0)
            g_judgeFrame[p]--;
        if (g_exJudgeCnt[p] < 1000) g_exJudgeCnt[p]++;   /* 0x407439 */
        if (g_game.cmdFlash[p] || g_misFlash[p] > 0) g_flashCnt[p]--; else g_flashCnt[p] = 0;   /* 0x806cf97 */
        if (g_misFlash[p] > 0) g_misFlash[p]--;   /* item Flash: +0x4f0 cai 1 por quadro */
        for (int pan = 0; pan < MAX_PANELS; pan++) {
            if (g_hitTimer[p][pan] > 0)
                g_hitTimer[p][pan]--;
            if (g_glowTimer[p][pan] > 0)
                g_glowTimer[p][pan]--;
            if (g_p1FlashTimer[p][pan] > 0)
                g_p1FlashTimer[p][pan]--;
            if (g_noteState[p][pan] == 1) { // EXPLODING
                g_noteExplodeFrame[p][pan]++;
                if (g_noteExplodeFrame[p][pan] >= 25) {
                    if (g_holdRows[p][pan] >= 0) {
                        /* Hold ainda ativo: reinicia animação sem esperar nova row */
                        g_noteExplodeFrame[p][pan] = 0;
                        g_glowTimer[p][pan] = 24;
                    } else {
                        g_noteState[p][pan] = 0;
                    }
                }
            }
        }
    }
    // Popup update (sobe e fade out)
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_popups[i].active) continue;
        g_popups[i].y += 60.0f * dt;  // sobe
        g_popups[i].alpha -= 1.2f * dt;  // fade
        if (g_popups[i].alpha <= 0) g_popups[i].active = false;
    }

    // Blind por beat (ativa 02.SPR em cada batida)
    if (g_chart && g_chart->beatSplit > 0 && g_songTime > 0) {
        int curBeatRow = (int)(getRowAtTimeFloat(g_songTime) / (double)g_chart->beatSplit);
        if (curBeatRow != g_prevBlindRow) {
            g_prevBlindRow = curBeatRow;
            g_blindTimer[0] = 10;
            g_blindTimer[1] = 10;
        }
    }
    for (int p = 0; p < 2; p++) {
        if (g_blindTimer[p] > 0) g_blindTimer[p]--;
    }

    // ===== Detecção de fim de música =====
    // Rastreia o maior g_songTime visto e conta frames estagnados
    if (g_songTime > g_maxSongTime) {
        g_maxSongTime = g_songTime;
        g_stagnantFrames = 0;
    } else {
        g_stagnantFrames++;
    }

    // ORIGINAL: g_songTime parou por 2s (120 frames) → música acabou
    if (g_stagnantFrames >= 60 && g_songTime > 5.0) {
        Log_Print("GP: song ended (stagnant %.1fs for %d frames)\n", g_songTime, g_stagnantFrames);
        BGM_Stop();
        Game_ChangeState(STATE_DANCE_GRADE_ENTER);
        return;
    }

    /* PUMPY.EXE 0x414902..0x414968: o gameplay termina (estado 0x18, Dance Grade)
     * no que acontecer primeiro:
     *   - o chart do jogador ativo acabou: linha atual [0xda24b4] >= total de
     *     linhas [chart+0xd39130]  (aqui: g_songTime >= duracao do chart);
     *   - a musica terminou: 0x4192a0() == 1 (BGM nao esta mais tocando). */
    if (g_chart && g_totalSongSeconds > 0 && g_songTime >= g_totalSongSeconds) {
        Log_Print("GP: chart ended (%.2f >= %.2f)\n", g_songTime, g_totalSongSeconds);
        BGM_Stop();
        Game_ChangeState(STATE_DANCE_GRADE_ENTER);
        return;
    }
    if (g_hasAudio && g_songTime > 1.0 && !BGM_IsPlaying()) {
        Log_Print("GP: music ended (%.2f)\n", g_songTime);
        BGM_Stop();
        Game_ChangeState(STATE_DANCE_GRADE_ENTER);
        return;
    }

    // MCI: g_songTime continua avançando (+= dt), verificar por timeout
    if (!BGM_IsDSActive() && g_hasAudio) {
        // Tempo estimado da música
        double expectedEnd = 0;
        if (g_totalSongSeconds > 0)
            expectedEnd = g_totalSongSeconds;
        else if (g_game.bgm.useMCI && g_game.bgm.durationMs > 0)
            expectedEnd = g_game.bgm.durationMs / 1000.0;
        if (expectedEnd > 0 && g_songTime >= expectedEnd + 5.0) {
            Log_Print("GP: audio ended via MCI/DirectSound timeout (%.1f >= %.1f)\n", g_songTime, expectedEnd);
            BGM_Stop();
            Game_ChangeState(STATE_DANCE_GRADE_ENTER);
            return;
        }
    }

    // Sem audio: esperar o STX terminar
    if (!g_hasAudio) {
        bool canTransition = false;
        if (g_chart && g_chart->rowCount > 0) {
            float scrollRow = (float)getRowAtTimeFloat(g_songTime);
            canTransition = (scrollRow >= (float)(g_chart->rowCount - 1));
        } else {
            canTransition = (g_totalSongSeconds > 0 && g_songTime >= g_totalSongSeconds);
        }
        if (canTransition) {
            Game_ChangeState(STATE_DANCE_GRADE_ENTER);
            return;
        }
    }
}

/* 0x4240A4: tile na posição natural do .spr, cor corrente */
static void exLifeTile(int idx, float r, float g, float b, float a)
{
    if (idx < 0 || idx >= g_game.sprTileCount) return;
    SPRTileDef* t = &g_game.sprTiles[idx];
    if (t->texId < 0) return;
    int tw = Texture_GetWidth(t->texId);  if (tw <= 0) tw = 256;
    int th = Texture_GetHeight(t->texId); if (th <= 0) th = 256;
    Texture_DrawUV(t->texId, (float)t->srcX, (float)t->srcY,
                   (float)t->srcW, (float)t->srcH,
                   t->u1 * (float)tw, t->v1 * (float)th,
                   t->u2 * (float)tw, t->v2 * (float)th, r, g, b, a);
}

/* exceed.exe 0x401000 (max_combo_num) / 0x4010FC (max_combo_mark): um quadro de
 * 40x48 da textura do BT_MC (256x256), a partir de V = 163/256. Dígito n na
 * grade de 6 colunas; marca 0 '<', 1 '=', 2 '>' (a 2 é a 1 espelhada). */
static void exBattleGlyph(int texId, float u1, float v1, float u2, float v2)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_game.textures[texId].id);
    glBegin(GL_QUADS);
    glTexCoord2f(u1, v1); glVertex2f(0.0f, 48.0f);
    glTexCoord2f(u1, v2); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(u2, v2); glVertex2f(40.0f, 0.0f);
    glTexCoord2f(u2, v1); glVertex2f(40.0f, 48.0f);
    glEnd();
}

/* exceed.exe 0x401240 (max_combo_draw): 1000/2000/3000 = marca < = >, senão 3 dígitos
 * da direita para a esquerda, 34 px entre eles. */
static void exBattleNumber(int texId, int combo)
{
    const float tw = 40.0f / 256.0f, th = 48.0f / 256.0f, y0 = 163.0f / 256.0f;
    if (combo == 1000 || combo == 2000 || combo == 3000) {
        int n = combo / 1000 - 1;
        float u1 = (n == 0) ? 4 * tw : 5 * tw;
        float u2 = (n == 2) ? u1 - tw : u1 + tw;
        glTranslatef(-34.0f, 0.0f, 0.0f);
        exBattleGlyph(texId, u1, th + y0, u2, th + y0 + th);
        return;
    }
    for (int i = 0; i < 3; i++) {
        int d = combo % 10;
        float u1 = (float)(d % 6) * tw, v1 = (float)(d / 6) * th + y0;
        exBattleGlyph(texId, u1, v1, u1 + tw, v1 + th);
        combo /= 10;
        glTranslatef(-34.0f, 0.0f, 0.0f);
    }
}

/* exceed.exe 0x40329D..0x403437 (playengine.cpp 1151): no BATTLE, BT_MC01.SPR por
 * cima e o max combo de cada jogador no centro, com a marca de quem vence no meio.
 * blink = pulso da batida (o mesmo das setas-base); Y = 70 ± 6·blink. */
static void exBattleDraw(float blink)
{
    if (g_fontSprBT01 < 0) return;
    int bt1 = (g_fontSprBT02 > g_fontSprBT01) ? g_fontSprBT02 : g_game.sprTileCount;
    for (int i = g_fontSprBT01; i < bt1; i++)
        exLifeTile(i, 1, 1, 1, 1);

    int texId = g_game.sprTiles[g_fontSprBT01].texId;
    if (texId < 0 || texId >= MAX_TEXTURES || !g_game.textures[texId].inUse) return;
    unsigned c0 = g_game.stats.maxCombo[0], c1 = g_game.stats.maxCombo[1];

    glColor4f(1, 1, 1, 1);
    glPushMatrix();
    glTranslatef(256.0f, 70.0f + blink * 6.0f, 0.0f);   /* 320 - (42+22) */
    exBattleNumber(texId, (int)c0);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(337.0f, 70.0f - blink * 6.0f, 0.0f);   /* 320 + 17 */
    exBattleNumber(texId, c0 < c1 ? 1000 : c0 == c1 ? 2000 : 3000);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(408.0f, 70.0f + blink * 6.0f, 0.0f);   /* 320 + (66+22) */
    exBattleNumber(texId, (int)c1);
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);
}

/* exceed.exe 0x40B084: lifebar do Exceed (gg_s.spr / gg_d.spr + GG.png).
 * beat = 1.0 no início da batida caindo a 0.0 (índice (1-beat)*59 na
 * tabela 0x4541A0, a mesma do Prex3). Coordenadas do original em Y-UP,
 * que é o espaço do GL do projeto: entram direto. */
static void exLifebarDraw(int p, float beat, bool dbl)
{
    static const float k_tbl[60] = {                     /* 0x4541A0 */
        1,1,1,1,1,1,1,1,1,1, .6f,.6f,.6f,.6f,.6f,.6f,.6f,.6f,.6f,.6f,
        .3f,.3f,.3f,.3f,.3f,.3f,.3f,.3f,.3f,.3f, .1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,
        0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0 };
    int spr = dbl ? g_fontSprGGD : g_fontSprGGS;
    if (spr < 0) return;
    int life = g_game.stats.life[p];

    int ti = (int)((1.0f - beat) * 59.0f);
    if (ti < 0) ti = 0;
    if (ti > 59) ti = 59;
    float v = (float)life / 1000.0f + (1.0f - k_tbl[ti]) / -10.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    int n7 = (int)(v * 33.0f) * 7;            /* edi */

    glPushMatrix();
    if (!dbl && p != 0)
        glTranslatef(320.0f, 0.0f, 0.0f);     /* 0x40B183 */

    exLifeTile(spr + 4, 1, 1, 1, 1);          /* fundo (ST01) */
    exLifeTile(spr + 1, 1, 1, 1, 1);          /* moldura */

    /* Preenchimento: quad + ponta triangular, textura do tile 0 (GG) */
    int texId = g_game.sprTiles[spr].texId;
    if (texId >= 0 && texId < MAX_TEXTURES && g_game.textures[texId].inUse) {
        float n14 = (float)(n7 * 2);
        float f7  = (float)n7;
        float uL = 45.0f / 512.0f;
        float uR = (45.0f + n14) / 512.0f;
        float uT = ((7.05f + f7) * 2.0f + 45.0f) / 512.0f;
        float x0, xR, xT;
        if (dbl) {                            /* 0x40B60A: x = 85 + 2.05*7n */
            x0 = 85.0f; xR = 2.05f * f7 + 85.0f; xT = (7.05f + f7) * 2.05f + 85.0f;
        } else {                              /* 0x40B202: x = 47 + 7n */
            x0 = 47.0f; xR = 47.0f + f7; xT = 54.05f + f7;
        }
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_game.textures[texId].id);
        glColor4f(1, 1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(uL, 0.40625f); glVertex2f(x0, 471.0f);
        glTexCoord2f(uR, 0.40625f); glVertex2f(xR, 471.0f);
        glTexCoord2f(uR, 0.65625f); glVertex2f(xR, 446.0f);
        glTexCoord2f(uL, 0.65625f); glVertex2f(x0, 446.0f);
        glEnd();
        glBegin(GL_TRIANGLES);
        glTexCoord2f(uR, 0.40625f); glVertex2f(xR, 471.0f);
        glTexCoord2f(uT, 0.40625f); glVertex2f(xT, 471.0f);
        glTexCoord2f(uR, 0.65625f); glVertex2f(xR, 446.0f);
        glEnd();
    }

    /* 0x40B3AC: vida real (sem o pulso) em 0..31 */
    float L = (float)life / 1000.0f;
    if (L < 0.0f) L = 0.0f;
    if (L > 1.0f) L = 1.0f;
    int m = (int)(L * 33.0f);
    if (m < 0) m = 0;
    if (m >= 32) m = 31;
    float cx = (float)m * (dbl ? 14.12f : 6.95f);

    /* Brilho (tile 2): vermelho com m <= 10, pisca com o contador
     * [0x4635E4+p*4] & 1 (HIPÓTESE: contador de quadros) */
    float gR = 1.0f, gG = (m <= 10) ? 0.0f : 1.0f, gB = gG;
    bool blink = (g_game.frameCounter & 1) != 0;
    if ((L >= 1.0f || m <= 10) && blink)
        exLifeTile(spr + 2, gR, gG, gB, 1.0f);

    /* Cursor (tile 3) e o mesmo cursor ampliado 1+beat/4, alpha = beat */
    float pivX = dbl ? 101.0f : 52.0f;
    glPushMatrix();
    glTranslatef(cx, 0.0f, 0.0f);
    exLifeTile(spr + 3, 1, 1, 1, 1);
    glTranslatef(pivX, 457.0f, 0.0f);
    float s = beat / 4.0f + 1.0f;
    glScalef(s, s, 1.0f);
    glTranslatef(-pivX, -457.0f, 0.0f);
    exLifeTile(spr + 3, 1, 1, 1, beat);
    glPopMatrix();

    glPopMatrix();
    glColor4f(1, 1, 1, 1);
}

/* NX 0x80681c0(gauge, jogador, t): lifebar do BGA/00.DAT (nxgauge.spr, 9 tiles).
 * t = fração da batida (0 no tempo, 0x806ca5c). Tiles: 0 moldura, 1 ponta direita,
 * 2 barra (arco-íris), 3 brilho cheio, 4 vermelho, 5 ponta amarela, 6 moldura
 * externa, 7 ponta externa, 8 reflexo. Espelhada no P2 (translate 640 + rotate 180 Y).
 * otherActive: (0x81f8904 & (2 >> p)) — o outro jogador está jogando (sem pontas). */

static void nxLifebarDraw(int p, float t, bool otherActive)
{
    static const float k_tab[6] = { 1.0f, 0.6f, 0.3f, 0.1f, 0.0f, 0.0f };   /* 0x8141e48 */
    int g = s_nxGauge;
    if (g < 0 || g + 8 >= g_game.sprTileCount) return;
    int life = g_game.stats.life[p];

    glPushMatrix();
    if (p != 0) {
        glTranslatef(640.0f, 0.0f, 0.0f);
        glRotatef(180.0f, 0.0f, 1.0f, 0.0f);
    }
    exLifeTile(g + 0, 1, 1, 1, 1);
    if (!otherActive) exLifeTile(g + 1, 1, 1, 1, 1);

    /* largura da barra */
    float v = (float)life / 1000.0f;
    if (life <= 999) {
        int k = (int)((1.0f - t) * 59.0f) / 10;
        if (k < 0) k = 0;
        if (k > 5) k = 5;
        v -= (1.0f - k_tab[k]) / 10.0f;
    }
    int n = (int)(v * 61.0f);
    if (n < 0) n = 0;
    if (n >= 0x3e) n = 0x3d;
    int len = n * 4;
    if (life != 0) len++;
    if (life >= 1000) len++;

    SPRTileDef* bar = &g_game.sprTiles[g + 2];
    if (bar->texId >= 0 && bar->texId < MAX_TEXTURES && g_game.textures[bar->texId].inUse) {
        int tw = Texture_GetWidth(bar->texId); if (tw <= 0) tw = 512;
        float x0 = (float)bar->srcX, x1 = x0 + (float)len;
        float yT = 480.0f - (float)bar->srcY, yB = 480.0f - (float)(bar->srcY + bar->srcH);
        float u0 = bar->u1, u1 = bar->u1 + (float)len / (float)tw;
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_game.textures[bar->texId].id);
        glColor4f(1.0f, 1.0f, 1.0f, 0.75f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, bar->v1); glVertex2f(x0, yT);
        glTexCoord2f(u0, bar->v2); glVertex2f(x0, yB);
        glTexCoord2f(u1, bar->v2); glVertex2f(x1, yB);
        glTexCoord2f(u1, bar->v1); glVertex2f(x1, yT);
        glEnd();
    }

    if (life > 999) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        exLifeTile(g + 3, 1, 1, 1, s_nxGaugeBlink[p] ? 0.5f : 0.0f);
    } else if (life <= 0x14d) {
        exLifeTile(g + 4, 1, 1, 1, s_nxGaugeBlink[p] ? 0.75f : 0.25f);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int n2 = life * 61 / 1000;
    if (n2 < 0) n2 = 0;
    if (n2 >= 0x3e) n2 = 0x3d;
    if (s_nxGaugeBlink[p]) {
        glTranslatef((float)(n2 * 4), 0.0f, 0.0f);
        exLifeTile(g + 5, 1, 1, 1, 1);
        glTranslatef((float)(-n2 * 4), 0.0f, 0.0f);
    }
    exLifeTile(g + 6, 1, 1, 1, 1);
    if (!otherActive) exLifeTile(g + 7, 1, 1, 1, 1);
    exLifeTile(g + 8, 1, 1, 1, 1);
    glPopMatrix();
    glColor4f(1, 1, 1, 1);
    s_nxGaugeBlink[p] = 1 - s_nxGaugeBlink[p];
}

/* ------------------------------------------------------------------------
 * Skin por jogador (NX2 CPlayer::m_CurrentSkin / m_Skin, player.cpp:537).
 * Cada jogador guarda o conjunto de sprites do próprio BGA/SKINxx.DAT; o
 * desenho troca para o conjunto do jogador (nxSkinUse) antes de desenhar o
 * lado dele. Receptores (01/02, w, hd), arrowf, spark e as setas/long vêm da
 * skin; o resto (fonte, m0x, lifebar) é comum e não troca.
 * ---------------------------------------------------------------------- */
typedef struct {
    int   idx;                         /* SKINxx carregado (-1 = vazio) */
    bool  zeroArrows;
    int   tap[5], l1[5], l2[5], l3[5], spark[5], arrowP;
    float offX[5], offY, fieldX[2];
    int   a541, a542, a543, a544, a545, arrowF, sparkF;
    int   s01, s02, w01, w02, hd01, hd02;
} NxSkinSet;
static NxSkinSet s_skinSet[2] = { { -1 }, { -1 } };

static void nxSkinSave(NxSkinSet* k, int idx)
{
    k->idx = idx; k->zeroArrows = g_zeroSkinArrows;
    for (int i = 0; i < 5; i++) {
        k->tap[i] = g_skinTap[i]; k->l1[i] = g_skinL1[i]; k->l2[i] = g_skinL2[i];
        k->l3[i] = g_skinL3[i]; k->spark[i] = g_skinSpark[i]; k->offX[i] = g_skinOffX[i];
    }
    k->arrowP = g_skinArrowP; k->offY = g_skinOffY;
    k->fieldX[0] = g_skinFieldX[0]; k->fieldX[1] = g_skinFieldX[1];
    k->a541 = g_fontArrow541; k->a542 = g_fontArrow542; k->a543 = g_fontArrow543;
    k->a544 = g_fontArrow544; k->a545 = g_fontArrow545;
    k->arrowF = g_fontArrowF; k->sparkF = g_fontSpark;
    k->s01 = g_fontSpr01; k->s02 = g_fontSpr02; k->w01 = g_fontSprW01; k->w02 = g_fontSprW02;
    k->hd01 = g_fontSprHD01; k->hd02 = g_fontSprHD02;
}

static void nxSkinUse(int p)
{
    /* Double/HalfDouble: um campo só, com a skin do jogador que está jogando */
    if (isDNMode() || isHDMode()) p = (g_game.activePlayerMask == 0x2) ? 1 : 0;
    const NxSkinSet* k = &s_skinSet[(p == 1) ? 1 : 0];
    if (k->idx < 0) k = &s_skinSet[0];
    if (k->idx < 0) return;
    g_zeroSkinArrows = k->zeroArrows;
    for (int i = 0; i < 5; i++) {
        g_skinTap[i] = k->tap[i]; g_skinL1[i] = k->l1[i]; g_skinL2[i] = k->l2[i];
        g_skinL3[i] = k->l3[i]; g_skinSpark[i] = k->spark[i]; g_skinOffX[i] = k->offX[i];
    }
    g_skinArrowP = k->arrowP; g_skinOffY = k->offY;
    g_skinFieldX[0] = k->fieldX[0]; g_skinFieldX[1] = k->fieldX[1];
    g_fontArrow541 = k->a541; g_fontArrow542 = k->a542; g_fontArrow543 = k->a543;
    g_fontArrow544 = k->a544; g_fontArrow545 = k->a545;
    g_fontArrowF = k->arrowF; g_fontSpark = k->sparkF;
    g_fontSpr01 = k->s01; g_fontSpr02 = k->s02; g_fontSprW01 = k->w01; g_fontSprW02 = k->w02;
    g_fontSprHD01 = k->hd01; g_fontSprHD02 = k->hd02;
}

/* Setas da skin no lugar dos ARROW54x (grupo 0..4 = 542, 541, 545, 543, 544) */
static void nxSkinMapArrows(void)
{
    g_zeroSkinArrows = false;
    if (g_skinTap[0] >= 0) {
        g_fontArrow542 = g_skinTap[0];
        g_fontArrow541 = g_skinTap[1];
        g_fontArrow545 = g_skinTap[2];
        g_fontArrow543 = g_skinTap[3];
        g_fontArrow544 = g_skinTap[4];
        g_zeroSkinArrows = true;
    }
}

/* Carrega a skin dos dois jogadores. A do P1 já entrou por
 * Resource_LoadFontAndArrows(SKIN do P1) na inicialização; se o P2 escolheu
 * outra, o SKINxx.DAT dele é carregado também, preservando o que é comum. */
static void nxLoadSkins(void)
{
    int sk0 = Zero_SkinIndexP(0), sk1 = Zero_SkinIndexP(1);
    exLoadSkin(sk0);
    nxSkinMapArrows();
    nxSkinSave(&s_skinSet[0], sk0);
    if (sk1 == sk0) {
        s_skinSet[1] = s_skinSet[0];
    } else {
        /* comuns que Resource_LoadFontAndArrows também escreve */
        int tex = g_fontTexId, dec = g_fontDec00Id, etc = g_fontArrowETC;
        int m1 = g_fontSprM01, m2 = g_fontSprM02, m3 = g_fontSprM03, m4 = g_fontSprM04, m5 = g_fontSprM05;
        int s3 = g_fontSpr03, s4 = g_fontSpr04, s5 = g_fontSpr05;
        int w3 = g_fontSprW03, w4 = g_fontSprW04, w5 = g_fontSprW05;
        int h3 = g_fontSprHD03, h5 = g_fontSprHD05, b1 = g_fontSprBT01, b2 = g_fontSprBT02;
        int gs = g_fontSprGGS, gd = g_fontSprGGD;
        char datPath[MAX_PATH];
        snprintf(datPath, sizeof(datPath), "%s/BGA/SKIN%02d.DAT", g_game.currentDirectory, sk1);
        Resource_LoadFontAndArrows(datPath);
        exLoadSkin(sk1);
        nxSkinMapArrows();
        nxSkinSave(&s_skinSet[1], sk1);
        g_fontTexId = tex; g_fontDec00Id = dec; g_fontArrowETC = etc;
        g_fontSprM01 = m1; g_fontSprM02 = m2; g_fontSprM03 = m3; g_fontSprM04 = m4; g_fontSprM05 = m5;
        g_fontSpr03 = s3; g_fontSpr04 = s4; g_fontSpr05 = s5;
        g_fontSprW03 = w3; g_fontSprW04 = w4; g_fontSprW05 = w5;
        g_fontSprHD03 = h3; g_fontSprHD05 = h5; g_fontSprBT01 = b1; g_fontSprBT02 = b2;
        g_fontSprGGS = gs; g_fontSprGGD = gd;
    }
    nxSkinUse(0);
    Log_Print("GP: skins P1=SKIN%02d P2=SKIN%02d\n", sk0, sk1);
}

/* Zero 0x807ff20 (arrowp.spr ao pisar). Separado do bloco do receptor para ser
 * desenhado depois das notas (NX2: DrawStep -> DrawPushArrow -> DrawFadeArrow). */
static void nxPushArrowDraw(int p, int panelCount, bool isDoubleOrNightmare)
{
    static const float k_alpha[9] = { 0.0f, 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.4f, 0.2f, 0.0f };
    for (int pan = 0; pan < panelCount; pan++) {
        int ft = g_p1FlashTimer[p][pan];
        if (ft <= 0) continue;
        int col = pan % 5;
        int t = 17 - ft;                       /* 1..16 */
        float field;
        if (isDoubleOrNightmare) field = (pan < 5) ? 65.0f + g_skinFieldX[0] : 312.0f + g_skinFieldX[1];
        else                     field = (p == 1 ? 348.0f : 28.0f) + g_skinFieldX[0];
        float sc = (float)t * 0.01875f + 0.8f;
        int idx = g_skinArrowP + col;
        float w = (float)g_game.sprTiles[idx].srcW, h = (float)g_game.sprTiles[idx].srcH;
        float cx = field + col * 49.0f + g_skinOffX[col] + w * 0.5f;
        float cy = 480.0f - 378.0f - (g_skinOffY + h * 0.5f);
        Sprite_DrawTileUV(idx, cx, cy, w * sc, h * sc, k_alpha[t / 2]);
    }
}

/* Bloco de rolagem do Gameplay_Render (sem mudança nas contas), separado para
 * poder ser refeito com o chart de cada jogador (ctxUse). */
typedef struct {
    int currentSeg;
    double currentSpr, actualScrollRow, visualScrollRow;
    float baseRowSpacing, pixelsPerRow, currentPixelsPerSec, jZoneHalf[4];
    int startRow, endRow;
} GpScroll;

static void gpScroll(GpScroll* S, int receptorY, int scrollBottom)
{
    // Current segment and actual scrollRow for timing-dependent calculations
    int currentSeg = 0;
    double currentSpr = g_secondsPerRow;
    for (int s = g_chart->segmentCount - 1; s >= 0; s--)
    {
        if (g_songTime >= getRowTime(g_chart->segments[s].rowStart))
        {
            currentSpr = getSegmentSpr(s);
            currentSeg = s;
            break;
        }
    }
    double actualScrollRow = getRowAtTimeFloat(g_songTime);

    // Visual scroll row: BPM-based, beatSplit-normalized for constant visual speed
    // Espaçamento original: 60.0 / beatSplit px/row × speedMult (confirmado Ghidra).
    float g_baseRowSpacing = (g_baseBeatSplit > 0) ? (60.0f / (float)g_baseBeatSplit) : 15.0f;
    /* Para startRow/endRow usa a velocidade mínima (mais rows visíveis = conservador) */
    float _spMin = g_scrollSpeedX[0];
    if (g_scrollSpeedX[1] < _spMin) _spMin = g_scrollSpeedX[1];
    float pixelsPerRow = g_baseRowSpacing * _spMin;
    double visualScrollRow = 0;
    if (g_visualRow && g_visualRowCount > 0) {
        int vr = (int)actualScrollRow;
        if (vr < 0) vr = 0;
        if (vr >= g_visualRowCount) vr = g_visualRowCount - 1;
        double frac = actualScrollRow - floor(actualScrollRow);
        visualScrollRow = g_visualRow[vr];
        if (vr + 1 < g_visualRowCount)
            visualScrollRow += (g_visualRow[vr + 1] - g_visualRow[vr]) * frac;
        visualScrollRow = zeroVisualScrollInDelay(g_songTime, visualScrollRow);
    }
    float currentPixelsPerSec = (float)(pixelsPerRow / currentSpr);

    // Determine visible actual row range (uses actualScrollRow, which is in actual-row space)
    int startRow = (int)actualScrollRow - (int)(480 / pixelsPerRow) - 2;
    if (startRow < 0) startRow = 0;

    /* endRow: percorre g_visualRow para achar o ultimo actual row visivel na tela.
     * Necessario porque quando beatSplit aumenta (ex: 4->8), cada actual row ocupa
     * menos espaco visual — o calculo simples (480/pixelsPerRow) fica curto e as
     * setas aparecem do nada em vez de surgir organicamente pelo fundo da tela. */
    int endRow;
    if (g_visualRow && g_visualRowCount > 0) {
        float targetVisualEnd = (float)visualScrollRow
            + (float)(scrollBottom + PANEL_SIZE - receptorY) / pixelsPerRow;
        endRow = (int)actualScrollRow;
        for (int _ri = (int)actualScrollRow; _ri < g_visualRowCount; _ri++) {
            if ((float)g_visualRow[_ri] >= targetVisualEnd) {
                endRow = _ri + 2;
                break;
            }
            endRow = _ri;
        }
    } else {
        endRow = (int)actualScrollRow + (int)((scrollBottom - receptorY) / pixelsPerRow) + 4;
    }
    if (endRow >= (int)g_chart->rowCount) endRow = g_chart->rowCount - 1;

    // Compute judgment zone half-heights using current BPM-based scroll speed
    // Usa média (early+late)/2 das janelas do nível atual para visualização centrada no receptor
    float jZoneHalf[4];
    {
        int _lvl = g_game.optionDifficulty;
        if (_lvl < 0) _lvl = 0; if (_lvl > 2) _lvl = 2;
        double _e[4], _l[4];
        judgeWindows(_lvl, currentJudgeBpm(), _e, _l);
        float jWindows[4] = {
            (float)(_e[3] + _l[3]) * 0.5f,  /* Bad  */
            (float)(_e[2] + _l[2]) * 0.5f,  /* Good */
            (float)(_e[1] + _l[1]) * 0.5f,  /* Great*/
            (float)(_e[0] + _l[0]) * 0.5f,  /* Perf */
        };
        for (int j = 0; j < 4; j++)
            jZoneHalf[j] = jWindows[j] * currentPixelsPerSec;
    }

    S->currentSeg = currentSeg; S->currentSpr = currentSpr;
    S->actualScrollRow = actualScrollRow; S->visualScrollRow = visualScrollRow;
    S->baseRowSpacing = g_baseRowSpacing; S->pixelsPerRow = pixelsPerRow;
    S->currentPixelsPerSec = currentPixelsPerSec;
    memcpy(S->jZoneHalf, jZoneHalf, sizeof(S->jZoneHalf));
    S->startRow = startRow; S->endRow = endRow;
}

void Gameplay_Render(void)
{
    if (g_game.state != STATE_GAMEPLAY) return;

    if (!g_songLoaded)
    {
        Font_DrawStringCentered(g_game.screenWidth/2, g_game.screenHeight/2,
            "Loading...", 1,1,1,1);
        return;
    }

    int receptorY = ZERO_RECEPTOR_Y; /* era 38 (Exceed) */
    bool isHalfDouble = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                         strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "HALFDOUBLE") == 0);
    bool isDoubleOrNightmare = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                               (strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "DOUBLE") == 0 ||
                                strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "NIGHTMARE") == 0));
    int sprReceptor = isHalfDouble ? g_fontSprHD01 : (isDoubleOrNightmare ? g_fontSprW01 : g_fontSpr01);
    int sprBrilho   = isHalfDouble ? g_fontSprHD02 : (isDoubleOrNightmare ? g_fontSprW02 : g_fontSpr02); /* 02.SPR — receptor com brilho (BPM) */
    int sprLifeBord = isHalfDouble ? g_fontSprHD03 : (isDoubleOrNightmare ? g_fontSprW03 : g_fontSpr03);
    int sprLifeGlow = isHalfDouble ? g_fontSprHD05 : (isDoubleOrNightmare ? g_fontSprW05 : g_fontSpr05);
    int scrollBottom = 480;

    /* rolagem/linhas visíveis do chart corrente (gpScroll, recalculado por jogador) */
    GpScroll _sc;
    gpScroll(&_sc, receptorY, scrollBottom);
    int currentSeg = _sc.currentSeg;
    double currentSpr = _sc.currentSpr;
    double actualScrollRow = _sc.actualScrollRow;
    float g_baseRowSpacing = _sc.baseRowSpacing;
    float pixelsPerRow = _sc.pixelsPerRow;
    double visualScrollRow = _sc.visualScrollRow;
    float currentPixelsPerSec = _sc.currentPixelsPerSec;
    int startRow = _sc.startRow;
    int endRow = _sc.endRow;
    float jZoneHalf[4];
    memcpy(jZoneHalf, _sc.jZoneHalf, sizeof(jZoneHalf));
    (void)currentSeg; (void)currentPixelsPerSec;

    /* Para HD/DN: sempre p=0, layout especial.
     * Para modos single (Normal/Hard/Crazy/Battle com 2P): loop pelos players ativos.
     *   P1 sozinho  (0x1): p=0
     *   P2 sozinho  (0x2): p=1
     *   P1+P2       (0x3): p=0 e p=1
     * Posições: P1 solo → centro; P1 com P2 → esquerda; P2 → direita. */
    bool twoPlayers = (g_game.activePlayerMask == 0x3) && !isHalfDouble && !isDoubleOrNightmare;
    int pRend0 = (isHalfDouble || isDoubleOrNightmare) ? 0 :
                 ((g_game.activePlayerMask == 0x2) ? 1 : 0);
    int pRend1 = (isHalfDouble || isDoubleOrNightmare) ? 1 :
                 (twoPlayers ? 2 : pRend0 + 1);

    for (int p = pRend0; p < pRend1; p++)
    {
        nxSkinUse(p);   /* NX2: m_Skin do jogador */
        if (g_ctxN > 1) {   /* NX2: chart do jogador (CPlayer : CStep) */
            ctxUse(p);
            gpScroll(&_sc, receptorY, scrollBottom);
            currentSeg = _sc.currentSeg; currentSpr = _sc.currentSpr;
            actualScrollRow = _sc.actualScrollRow; g_baseRowSpacing = _sc.baseRowSpacing;
            pixelsPerRow = _sc.pixelsPerRow; visualScrollRow = _sc.visualScrollRow;
            currentPixelsPerSec = _sc.currentPixelsPerSec;
            startRow = _sc.startRow; endRow = _sc.endRow;
            memcpy(jZoneHalf, _sc.jZoneHalf, sizeof(jZoneHalf));
        }
        /* Velocidade de scroll deste player (para posição Y das notas) */
        float pPixelsPerRow = g_baseRowSpacing * g_scrollSpeedX[p];

        /* Posições e contagem de painéis para este player/iteração */
        float posX[10];
        int pW[10];
        int panelCount;
        int centerX;

        if (isHalfDouble) {
            panelCount = 6;
            for (int i = 0; i < 6; i++) { posX[i] = 171.0f + i * 48.0f + (i >= 3 ? 7.0f : 0.0f); pW[i] = 54; }
            centerX = 320;
        } else if (isDoubleOrNightmare) {
            panelCount = 10;
            /* Exceed: pW 54, posX 74 + 48i / 323 + 48(i-5) */
            for (int i = 0; i < 10; i++) pW[i] = ZERO_ARROW_W;
            /* Zero 0x8083070: campo em 65 + [0x08628260] e 312 + [0x08628264] */
            for (int i = 0; i < 5; i++) posX[i] = 65.0f + g_skinFieldX[0] + i * ZERO_COL_STEP;
            for (int i = 5; i < 10; i++) posX[i] = 312.0f + g_skinFieldX[1] + (i-5) * ZERO_COL_STEP;
            centerX = 320;
        } else {
            panelCount = 5;
            for (int i = 0; i < 5; i++) pW[i] = ZERO_ARROW_W;   /* Exceed: 54 */
            if (p == 1) {
                /* P2 (sozinho ou com P1): lado direito, espelhado de P1.
                 * P1 centro=161, P2 centro=479 (simetrico em 640px).
                 * P2[0]=356, ..., P2[4]=548. Gap entre P1(284) e P2(356) = 72px. */
                /* Exceed: 358 + 48i */
                /* Zero: campo em 348 + [0x08628260] (x = coluna * 50) */
                for (int i = 0; i < 5; i++) posX[i] = 348.0f + g_skinFieldX[0] + i * ZERO_COL_STEP;
                centerX = P2_CENTER_X;
            } else {
                /* P1 (sozinho ou com P2): posição padrão esquerda (mesma do solo) */
                /* Exceed: 38 + 48i */
                /* Zero: campo em 28 + [0x08628260] (x = coluna * 50) */
                for (int i = 0; i < 5; i++) posX[i] = 28.0f + g_skinFieldX[0] + i * ZERO_COL_STEP;
                centerX = P1_CENTER_X;
            }
        }

        /* // Zonas de acerto (julgamento) - desativadas, 01.SPR substitui
        {
            float jColors[4][4] = {
                {1, 0.3f, 0.3f, 0.50f},
                {1, 1, 0, 0.55f},
                {0.5f, 1, 0.5f, 0.55f},
                {0.5f, 0.8f, 1, 0.60f},
            };
            for (int j = 0; j < 4; j++)
            {
                int halfH = (int)(jZoneHalf[j] + 0.5f);
                for (int panel = 0; panel < 5; panel++)
                    Render_Rect(posX[panel] + 1, (float)(receptorY - halfH + 2), (float)(pW[panel] - 2), (float)(halfH * 2 - 4),
                        (uint8_t)(jColors[j][0] * 255), (uint8_t)(jColors[j][1] * 255),
                        (uint8_t)(jColors[j][2] * 255), (uint8_t)(jColors[j][3] * 255));
                }
            }
        }
        */

        /* Grid de fundo (compasso/batida/sub-batida) - desativado
        if (g_chart->beatPerMeasure > 0 && g_chart->beatSplit > 0)
        {
            int gx0 = baseX - 22;
            int gx1 = baseX + 162;
            for (int ri = startRow; ri <= endRow; ri++)
            {
                float gy = (float)(receptorY + (ri - scrollRow) * pixelsPerRow);
                if (gy < receptorY - PANEL_SIZE || gy > scrollBottom + PANEL_SIZE) continue;

                int blockLine;
                int blockNum = getBlockInfo(ri, &blockLine);
                if (blockLine == 1)
                {
                    Render_Rect((float)gx0, (float)gy, (float)(gx1 - gx0), 3, 255, 255, 255, 255);
                    char num[8];
                    snprintf(num, sizeof(num), "%d", blockNum);
                    int numY = g_game.screenHeight - (int)gy - 6;
                    Font_DrawStringScaled(gx0 - 30, numY, num, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f);
                }
                else if ((blockLine - 1) % g_chart->beatSplit == 0)
                {
                    for (int x = gx0; x < gx1; x += 16)
                        Render_Rect((float)x, (float)gy, 8, 2, 255, 255, 255, 200);
                }
                else
                {
                    for (int x = gx0; x < gx1; x += 24)
                        Render_Rect((float)x, (float)gy, 4, 1, 200, 200, 200, 150);
                }
            }
        }
        */

        /* // Receptor no topo - desativado, 01.SPR substitui
        int rh = 57;
        for (int panel = 0; panel < 5; panel++)
        {
            float px = posX[panel];
            int rw = pW[panel];
            uint8_t rr, rg, rb;
            if (panel == 0 || panel == 4)      { rr = 52; rg = 120; rb = 200; }
            else if (panel == 1 || panel == 3) { rr = 200; rg = 60; rb = 60; }
            else                                { rr = 220; rg = 200; rb = 40; }
            Render_Rect(px, receptorY, (float)rw, (float)rh, rr, rg, rb, 180);
            Render_Rect(px + 2, receptorY + 2, (float)(rw - 4), (float)(rh - 4), 0, 0, 0, 120);
        }
        */

        /* 0x806cf89..0x806d228: +0x4ec > 0 -> quadro branco 640x480 com alfa n/50,
         * antes do campo do jogador; cai 1 por quadro */
        if (g_misWhite[p] > 0) {
            Render_Rect(0.0f, 0.0f, 640.0f, 480.0f, 255, 255, 255, (uint8_t)(g_misWhite[p] * 255 / 50));
            g_misWhite[p]--;
            glColor4f(1, 1, 1, 1);
        }
        nxFieldBegin();   /* NX/UA: só receptores e setas (0x806a150 / 0x8069d20) */
        // 01.SPR receptor (g_fontSpr01) — renderiza ANTES das notas (abaixo delas)
        // Freedom: oculta o receptor completamente (sprites não são desenhados)
        // Para single (não HD/DN): srcX baked para P1-solo (base=38). Offset por player.
        {
            /* Exceed: recOffX = (HD/DN) ? 0 : posX[0] - 38, sem deslocamento em Y */
            /* 0x807e820: 01/02 em (32, -42) no P1 e (352, -42) no P2; W01/W02 em (70, -42) */
            float recOffX = isHalfDouble ? 0.0f : isDoubleOrNightmare ? 70.0f : (p == 1 ? 352.0f : 32.0f);
            float recOffY = isHalfDouble ? 0.0f : ZERO_REC_DY;
            if (sprReceptor >= 0 && !g_game.cmdFreedom[p]) {
                int cnt = sprTileCount(sprReceptor);
                for (int t = cnt - 1; t >= 0; t--) {
                    int idx = sprReceptor + t;
                    float sx = (float)g_game.sprTiles[idx].srcX + recOffX;
                    float sy = (float)g_game.sprTiles[idx].srcY + recOffY;
                    float sw = (float)g_game.sprTiles[idx].srcW;
                    float sh = (float)g_game.sprTiles[idx].srcH;
                    Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, 1.0f);
                }
            }
            // 02.SPR — Receptor com brilho (pisca com BPM)
            // Freedom: oculta junto com 01.SPR
            if (sprBrilho >= 0 && g_blindTimer[p] > 0 && !g_game.cmdFreedom[p]) {
                float blindA = (float)g_blindTimer[p] / 10.0f;
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                int cnt = sprTileCount(sprBrilho);
                for (int t = cnt - 1; t >= 0; t--) {
                    int idx = sprBrilho + t;
                    float sx = (float)g_game.sprTiles[idx].srcX + recOffX;
                    float sy = (float)g_game.sprTiles[idx].srcY + recOffY;
                    float sw = (float)g_game.sprTiles[idx].srcW;
                    float sh = (float)g_game.sprTiles[idx].srcH;
                    Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, blindA);
                }
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
        }

        /* Zero 0x807ff20: ao pisar, arrowp.spr da skin na coluna (tiles DL UL C UR DR;
         * a diagonal é a mesma imagem do ARROW02 girada pelo UV, o 2 é o Center).
         * Origem (campo, 378) no sistema Y para cima; coluna * 49 (98/49 dos
         * glTranslatef) + deslocamento da skin; tile 70x70 escalado em torno de
         * (35, 35) por t * 0.01875 + 0.8, alfa = [0x0811ce40][t / 2], t = 1..16. */
        if (g_skinArrowP >= 0 && !isHalfDouble) {
            /* Desenhado depois das notas (nxPushArrowDraw), como no original:
             * NX2 CPlayEngine::Run chama DrawStep e só depois DrawPushArrow /
             * DrawFadeArrow. Aqui antes a ponta do long chegando cobria o efeito. */
        } else
        /* Tile "p1" do ARROW54X.SP2 — borda branca/cinza da seta.
         * Original (Ghidra): ao pressionar o botão (borda de subida), aparece com
         * zoom (1.3x→1.0x) e some. IsPadHit dispara g_p1FlashTimer. */
        {
            for (int pan = 0; pan < panelCount; pan++) {
                int ft = g_p1FlashTimer[p][pan];
                if (ft <= 0) continue;

                int arrowType;
                if (isHalfDouble) {
                    arrowType = (pan == 0 || pan == 5) ? 2 : (pan == 1) ? 3 : (pan == 2) ? 4 : (pan == 3) ? 0 : 1;
                } else if (isDoubleOrNightmare) {
                    arrowType = pan % 5;
                } else {
                    arrowType = pan;
                }
                int baseIdx = (arrowType == 0) ? g_fontArrow542 :
                              (arrowType == 1) ? g_fontArrow541 :
                              (arrowType == 2) ? g_fontArrow545 :
                              (arrowType == 3) ? g_fontArrow543 :
                              (arrowType == 4) ? g_fontArrow544 : -1;
                if (baseIdx < 0) continue;
                int p1Idx = baseIdx + 6;  /* tile "p1" = índice 6 no SP2 */
                if (p1Idx >= g_game.sprTileCount) continue;
                float sw = (float)g_game.sprTiles[p1Idx].srcW;
                float sh = (float)g_game.sprTiles[p1Idx].srcH;
                int p1Pan = isDoubleOrNightmare ? (pan % 5) : (isHalfDouble ? arrowType : pan);
                static const float p1OffXReg[5] = {-7.0f, -6.0f, -5.0f, -6.0f, -7.0f};
                static const float p1OffXHD[5]  = {-5.0f, -6.0f, -7.0f, -6.0f, -5.0f};
                const float* p1OffX = (isHalfDouble || isDoubleOrNightmare) ? p1OffXHD : p1OffXReg;

                /* t: 1.0 no início → 0.0 no fim da animação */
                float t = (float)ft / 15.0f;
                float scale = 1.1f - 0.3f * t;  /* 0.8x → 1.1x (zoom out) */
                float alpha = t;                  /* fade out */
                float cx = posX[pan] + p1OffX[p1Pan] + sw / 2.0f;
                float cy = (float)(receptorY + 28);
                Sprite_DrawTileUV(p1Idx, cx, cy, sw * scale, sh * scale, alpha);
            }
        }

        int rh = 57;
        // Notas (scroll do fundo para o topo)
        int rh2 = 57;
        static const int kPanelOrder[5] = {1, 3, 0, 2, 4};
        static const int kBodyTile[5] = {12, 16, 20, 18, 14};
        static const int kTailTile[5] = {13, 17, 21, 19, 15};
        static const float kBodyOffX[5] = {-4.0f, -3.0f, 0.0f, 3.0f, 4.0f};
        // HD mappings: pos 0=CN, 1=UR, 2=DR, 3=DL, 4=UL, 5=CN
        static const int kHDBodyTile[6] = {20, 18, 14, 12, 16, 20};
        static const int kHDTailTile[6] = {21, 19, 15, 13, 17, 21};
        static const float kHDBodyOffX[6] = {0.0f, 3.0f, 4.0f, -4.0f, -3.0f, 0.0f};

        /* X-MODE (exceed.exe 0x40648E / 0x40658B): com [0x568FF4] & 0x8000 a
         * nota ganha Translate(d*speed/±1000) em X, onde d*speed/1000 é a
         * mesma distância que a afasta do receptor (y = 378 - d*speed/1000).
         * +1000 para o jogador 0, -1000 para o outro ([ebx+8] = 1º argumento
         * de 0x406190). Resultado: a nota chega na diagonal de 45°. */
        /* Double: metade esquerda (colunas 0..4) vem da direita e a direita
         * (5..9) vem da esquerda, formando um X (observado pelo usuário no
         * original; o código passa o sinal pelo 1º argumento de 0x406190). */
        float xmS = 0.0f;
        if (g_exceedSongIds && ExSelect_IsXMode()) xmS = (p == 0) ? 1.0f : -1.0f;
        if (g_game.cmdXMode[p]) xmS = (p == 0) ? 1.0f : -1.0f;   /* NX: 0x1000 */
        /* FL: alpha das setas (0x806f4d5) */
        g_drawAlphaMul = (g_game.cmdFlash[p] || g_misFlash[p] > 0) ? 0.5f + 0.5f * sinf((float)g_flashCnt[p] * 0.25f) : 1.0f;
        float xmY0 = (float)(receptorY + rh2 / 2);
        #define XM_S(pn) ((isDoubleOrNightmare && (pn) >= 5) ? -xmS : xmS)
        /* era: #define XM_DX(yy) (xmS * ((yy) - xmY0))
         *      #define XM_DXP(pn, yy) (XM_S(pn) * ((yy) - xmY0))  (distância já curvada pelo AC/DC) */
        #define XM_DX(yy) (xmS * nxAccelInv(p, (yy) - xmY0))
        #define XM_DXP(pn, yy) (XM_S(pn) * nxAccelInv(p, (yy) - xmY0))

        /* Zero (piu 0x8087520): long note pela skin em uso.
         *   linha da nota = base da seta (a seta ocupa 64 px acima dela);
         *   corpo skinN_l2: UM quad com o UV inteiro do tile (0x809e040), da base
         *     da cabeça até o topo da ponta;
         *   ponta skinN_l3 inteira na linha final (0x809e2a0); se a distância for
         *     < 64 px, sem corpo e a ponta só com a parte de baixo da textura
         *     (0x809e0d0: fração = distância / 64);
         *   cabeça skinN_l1 por cima (Pass 2 / exHeldHead). Segurado: a cabeça
         *     fica no receptor e o corpo sai dele. */
        if (g_zeroSkinArrows && !isHalfDouble && !g_game.cmdNonStep[p]) {
            #define Z_PV(r, pn) (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[r], pn)                                                     : getPanelValue(&g_chart->rows[r], pn, p))
            #define Z_ROWY(r) ((float)(receptorY + rh2 / 2) + nxAccelDist(p,                 (((r) < g_visualRowCount && g_visualRow) ? (float)g_visualRow[r] : (float)(r)) * pPixelsPerRow                 - (float)(visualScrollRow * pPixelsPerRow)))
            for (int panel = 0; panel < panelCount; panel++) {
                int col = panel % 5;
                if (g_skinL2[col] < 0 || g_skinL3[col] < 0) continue;
                int rows = (int)g_chart->rowCount;
                /* ponta de um hold a partir da cabeça h: atravessa corpo e linhas
                 * vazias (apagadas ao passar), para em outra nota */
                #define Z_TAIL(h, out) do { out = -1;                     for (int k_ = (h) + 1; k_ < rows; k_++) { uint8_t v_ = Z_PV(k_, panel);                         if (v_ == NT_HOLD_T) { out = k_; break; }                         if (v_ != NT_HOLD_B && v_ != 0) break; } } while (0)
                #define Z_DRAW(h, t, isHeld) do {                     float yh = ((isHeld) ? (float)(receptorY + rh2 / 2) : Z_ROWY(h)) - g_skinOffY;                     float yt = Z_ROWY(t) - g_skinOffY;                     if ((isHeld) && yt < yh) yt = yh;                     zeroHoldDraw(panel, col, posX[panel] + g_skinOffX[col] + XM_DXP(panel, yh), yh, yt, (isHeld)); } while (0)
                int lastTail = -1;
                /* 1) hold segurado: cabeça no receptor (a linha dela já foi apagada) */
                int held = g_holdRows[p][panel];
                if (held >= 0) {
                    int t; Z_TAIL(held, t);
                    if (t >= 0) { Z_DRAW(held, t, true); lastTail = t; }
                }
                /* 2) cabeça acima da tela (não segurada): só se a linha inicial for
                 * corpo/ponta de um hold que ainda tem a cabeça no gráfico */
                /* era: só procurava depois da cauda do long segurado (lastTail); um long
                 * do mesmo painel perdido ANTES dele (MISS, segue subindo) não era
                 * desenhado e sumia quando o próximo long já estava seguro (D02 CRAZY,
                 * longs curtos colados). Agora a busca cobre a tela toda e só pula o
                 * trecho do long segurado (held+1 .. lastTail). */
                int from = startRow;
                {
                    for (int k = startRow; k >= 0; k--) {
                        uint8_t v = Z_PV(k, panel);
                        if (v == NT_HOLD_H) { from = k; break; }
                        /* long solto no meio: a cabeça e o trecho segurado foram
                         * apagados; o resto começa no corpo depois da linha vazia */
                        if (v == 0 && k < startRow && Z_PV(k + 1, panel) == NT_HOLD_B) { from = k + 1; break; }
                        if (v != NT_HOLD_B && !(k == startRow && v == NT_HOLD_T)) break;
                    }
                }
                /* 3) cabeças visíveis
                 * era: for (int h = (from > lastTail ? from : lastTail + 1); ...) */
                for (int h = from; h <= endRow && h < rows; h++) {
                    if (held >= 0 && h > held && h <= lastTail) { h = lastTail; continue; }   /* long segurado */
                    uint8_t hv = Z_PV(h, panel);
                    /* era: só HOLD_H. Também o corpo órfão (linha anterior apagada):
                     * é o que sobra de um long solto no meio, que segue subindo. */
                    if (hv != NT_HOLD_H &&
                        !(hv == NT_HOLD_B && h > 0 && Z_PV(h - 1, panel) == 0)) continue;
                    int t; Z_TAIL(h, t);
                    if (t < 0) continue;
                    Z_DRAW(h, t, false);
                    h = t;
                }
                #undef Z_TAIL
                #undef Z_DRAW
            }
            #undef Z_PV
            #undef Z_ROWY
        }
        bool exHeldHead[MAX_PANELS] = { false };
        float exHeadY[MAX_PANELS] = { 0 };   /* Y da cabeça do hold redesenhada (Exceed) */
        // Pass 0: Hold bodies (esticados entre runs de NT_HOLD_B)
        if (g_fontArrowETC >= 0 && !g_zeroSkinArrows) {
            for (int panel = 0; panel < panelCount; panel++)
            {
                int arrowIdx = isDoubleOrNightmare ? (panel % 5) : panel;
                for (int ri = startRow; ri <= endRow; ri++)
                {
                    uint8_t val = isHalfDouble ? getNoteHD(&g_chart->rows[ri], panel) : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                    if (val != NT_HOLD_B) continue;
                    if (ri == g_lastPerfectRow[p][panel]) continue;

                    int endRi = ri + 1;
                    while (endRi < (int)g_chart->rowCount) {
                        uint8_t nv = isHalfDouble ? getNoteHD(&g_chart->rows[endRi], panel) : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[endRi], panel) : getPanelValue(&g_chart->rows[endRi], panel, p));
                        if (nv != NT_HOLD_B) break;
                        endRi++;
                    }

                    float vri = (ri < g_visualRowCount && g_visualRow) ? (float)g_visualRow[ri] : (float)ri;
                    float vendRi = (endRi < g_visualRowCount && g_visualRow) ? (float)g_visualRow[endRi] : (float)endRi;
                    float y1 = (float)(receptorY + rh2 / 2 + nxAccelDist(p, (float)((vri - visualScrollRow) * pPixelsPerRow)));
                    float y2 = (float)(receptorY + rh2 / 2 + nxAccelDist(p, (float)((vendRi - visualScrollRow) * pPixelsPerRow)));
                    if (y2 < y1) { float t = y1; y1 = y2; y2 = t; }

                    /* O clamp abaixo só vale para o body do hold que está sendo
                     * segurado AGORA. Antes bastava g_holdRows >= 0, e isso
                     * pegava qualquer run de HOLD_B do mesmo painel visível na
                     * tela — inclusive o do PRÓXIMO hold, lá em cima. Aquele run
                     * era esticado do receptor até o topo e virava a "sujeira"
                     * que cobria a lane inteira (os taps continuavam aparecendo
                     * por cima porque são desenhados em outro pass).
                     * Critério: um HOLD_H entre o hold ativo e esta row significa
                     * que este body pertence a outro hold. */
                    int activeHold = g_holdRows[p][panel];
                    bool ownedByActive = (activeHold >= 0 && activeHold < ri);
                    if (ownedByActive) {
                        for (int hr = activeHold + 1; hr <= ri; hr++) {
                            uint8_t sv = isHalfDouble ? getNoteHD(&g_chart->rows[hr], panel)
                                       : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[hr], panel)
                                       : getPanelValue(&g_chart->rows[hr], panel, p));
                            if (sv == NT_HOLD_H) { ownedByActive = false; break; }
                        }
                    }

                    if (ownedByActive) {
                        /* HOLD SEGURADO: body começa no receptor (X1Rus playengine.cpp
                         * DrawStepLine, ramo "m_CurY > 0.0f && long_stat": m_CurY > 0 = linha
                         * AINDA NÃO chegou, já que m_Y diminui com o tempo; corpo e cabeça
                         * são transladados de volta ao receptor). */
                        y1 = (float)(receptorY + rh2 / 2);
                        /* era (leitura invertida do m_CurY, cabeça "caminhava" até o receptor):
                        float vh = (activeHold < g_visualRowCount && g_visualRow) ? (float)g_visualRow[activeHold] : (float)activeHold;
                        float yh = (float)(receptorY + rh2 / 2 + (vh - visualScrollRow) * pPixelsPerRow);
                        y1 = (yh > (float)(receptorY + rh2 / 2)) ? yh : (float)(receptorY + rh2 / 2);
                        */
                    } else if (g_exceedSongIds && ri > 0 &&
                               (isHalfDouble ? getNoteHD(&g_chart->rows[ri - 1], panel)
                               : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[ri - 1], panel)
                               : getPanelValue(&g_chart->rows[ri - 1], panel, p))) == 0) {
                        /* Corpo sem cabeça (linha anterior já consumida) e sem hold
                         * capturado. X1Rus DrawStepLine, MIDDLE com NOTE(-1) == 0:
                         *   botão apertado e linha ainda não chegou (m_CurY > 0)
                         *     -> corpo e seta presos no receptor;
                         *   senão -> seta desenhada na própria linha, corpo dali.
                         * Antes o port desenhava só o corpo, cortado reto e sem seta. */
                        float yr = (float)(receptorY + rh2 / 2);
                        if (holdPanelDown(p, panel) && y1 >= yr) y1 = yr;
                        if (!exHeldHead[panel] && !g_game.cmdNonStep[p]) {   /* só o 1º run (o de cima) */
                            exHeldHead[panel] = true;
                            exHeadY[panel] = y1;
                        }
                    } else {
                        /* MISS / não segurado: ancora topo do body no centro do HoldHead (ri-1)
                         * para eliminar o buraco visual entre head e body. */
                        int headRi = ri - 1;
                        if (headRi >= 0) {
                            uint8_t hv = isHalfDouble ? getNoteHD(&g_chart->rows[headRi], panel)
                                       : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[headRi], panel)
                                       : getPanelValue(&g_chart->rows[headRi], panel, p));
                            if (hv == NT_HOLD_H) {
                                float vhRi = (headRi < g_visualRowCount && g_visualRow) ? (float)g_visualRow[headRi] : (float)headRi;
                                float headY = (float)(receptorY + rh2 / 2 + nxAccelDist(p, (float)((vhRi - visualScrollRow) * pPixelsPerRow)));
                                if (headY < y1) y1 = headY;
                                else if (headY > y2) y2 = headY;
                            }
                        }
                    }

                    float totalH = y2 - y1;
                    if (totalH <= 0) { ri = endRi - 1; continue; }

                    if (!g_game.cmdNonStep[p]) {
                        int idx = g_fontArrowETC + (isHalfDouble ? kHDBodyTile[panel] : kBodyTile[arrowIdx]);
                        float sw   = (float)g_game.sprTiles[idx].srcW;
                        float sprH = (float)g_game.sprTiles[idx].srcH;
                        float offX = isHalfDouble ? kHDBodyOffX[panel] : kBodyOffX[arrowIdx];
                        /* Exceed2: corpo pela skin (skinN_l2), no centro da nota */
                        if (!isHalfDouble && arrowIdx < 5 && g_skinL2[arrowIdx] >= 0) {
                            float aw = (g_fontArrow542 >= 0) ? (float)g_game.sprTiles[g_fontArrow542].srcW : 54.0f;
                            idx  = g_skinL2[arrowIdx];
                            sw   = (float)g_game.sprTiles[idx].srcW;
                            sprH = (float)g_game.sprTiles[idx].srcH;
                            offX = (aw - sw) / 2.0f + g_skinOffX[arrowIdx];
                        }
                        (void)sprH;
                        float bodyAlpha = 1.0f;
                        if (g_game.cmdVanish[p]) {
                            /* Thresholds derivados do PUMPY.EXE (Ghidra):
                             * _DAT_004349ac=196, _DAT_00434934=256, ref=316
                             * → fade zone 62%-81% da área de play a partir do receptor */
                            float fade = ((y1 + totalH / 2.0f) - 122.0f) / 84.0f;
                            bodyAlpha = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade);
                        }
                        float cx = posX[panel] + sw / 2.0f + offX;
                        /* Estica apenas a faixa CENTRAL do UV do sprite (25%-75% vertical).
                         * Evita o gradiente alpha das bordas do sprite e elimina linhas de corte. */
                        {
                            SPRTileDef* bt = &g_game.sprTiles[idx];
                            int btW = Texture_GetWidth(bt->texId); if (btW <= 0) btW = 256;
                            int btH = Texture_GetHeight(bt->texId); if (btH <= 0) btH = 256;
                            float u1px = bt->u1 * (float)btW;
                            float u2px = bt->u2 * (float)btW;
                            float vC1 = (bt->v1 + (bt->v2 - bt->v1) * 0.25f) * (float)btH;
                            float vC2 = (bt->v1 + (bt->v2 - bt->v1) * 0.75f) * (float)btH;
                            /* X-MODE: o original (0x404DFF..0x405165) desenha corpo e
                             * ponta de uma vez, dentro da translação da linha da
                             * CABEÇA — peça vertical que segue o X da cabeça (y1). */
                            Texture_DrawUV(bt->texId, cx - sw / 2.0f + XM_DXP(panel, y1), y1, sw, totalH,
                                           u1px, vC1, u2px, vC2, 1.0f, 1.0f, 1.0f, bodyAlpha);
                        }
                    }
                    ri = endRi - 1;
                }
            }
        }

        /* Exceed — hold segurado, olhando a próxima linha não vazia à frente:
         *   HOLD_B: o corpo já sai do receptor no Pass 0; falta a cabeça presa no
         *           receptor (0x4053D5..0x405419: Translate(0, CurY*speed/1000) e
         *           DrawPic da seta), desenhada depois do Pass 2.
         *   HOLD_T: só sobrou a ponta (linha END com a anterior vazia,
         *           0x404CAB..0x404D81). O original liga o receptor à ponta com o
         *           corpo (y_interp + 30 + CurY*speed/1000 .. 55); aqui os corpos
         *           consumidos já foram apagados por clearPanel, então sem este
         *           bloco sobrava um buraco entre o receptor e a ponta. */
        if (g_exceedSongIds && (g_fontArrowETC >= 0 || g_zeroSkinArrows) && !g_game.cmdNonStep[p]) {
            #define EX_PV(r, pn) (isHalfDouble ? getNoteHD(&g_chart->rows[r], pn) \
                               : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[r], pn) \
                               : getPanelValue(&g_chart->rows[r], pn, p)))
            for (int panel = 0; panel < panelCount; panel++) {
                int hr = g_holdRows[p][panel];
                if (hr < 0) continue;
                int tailRi = -1;
                for (int ri = hr + 1; ri < (int)g_chart->rowCount; ri++) {
                    uint8_t v = EX_PV(ri, panel);
                    if (!v) continue;
                    if (v == NT_HOLD_B) {
                        /* DrawStepLine: segurado, a seta fica no receptor */
                        exHeadY[panel] = (float)(receptorY + rh2 / 2);
                        exHeldHead[panel] = true;
                    }
                    else if (v == NT_HOLD_T) tailRi = ri;
                    break;
                }
                if (tailRi < 0 || g_zeroSkinArrows) continue;   /* Zero: corpo já desenhado acima */
                float vt = (tailRi < g_visualRowCount && g_visualRow) ? (float)g_visualRow[tailRi] : (float)tailRi;
                float y1 = (float)(receptorY + rh2 / 2);
                float y2 = (float)(receptorY + rh2 / 2 + nxAccelDist(p, (float)((vt - visualScrollRow) * pPixelsPerRow)));
                if (y2 <= y1) continue;
                int arrowIdx = isDoubleOrNightmare ? (panel % 5) : panel;
                int idx = g_fontArrowETC + (isHalfDouble ? kHDBodyTile[panel] : kBodyTile[arrowIdx]);
                float offXs = 0.0f;
                bool useSkin = (!isHalfDouble && arrowIdx < 5 && g_skinL2[arrowIdx] >= 0);
                if (useSkin) idx = g_skinL2[arrowIdx];   /* Exceed2: corpo pela skin */
                SPRTileDef* bt = &g_game.sprTiles[idx];
                float sw = (float)bt->srcW;
                if (useSkin) {
                    float aw = (g_fontArrow542 >= 0) ? (float)g_game.sprTiles[g_fontArrow542].srcW : 54.0f;
                    offXs = (aw - sw) / 2.0f + g_skinOffX[arrowIdx];
                }
                float offX = useSkin ? offXs : (isHalfDouble ? kHDBodyOffX[panel] : kBodyOffX[arrowIdx]);
                int btW = Texture_GetWidth(bt->texId); if (btW <= 0) btW = 256;
                int btH = Texture_GetHeight(bt->texId); if (btH <= 0) btH = 256;
                /* mesma faixa de UV do Pass 0, para o corpo não mudar de cara no fim */
                float vC1 = (bt->v1 + (bt->v2 - bt->v1) * 0.25f) * (float)btH;
                float vC2 = (bt->v1 + (bt->v2 - bt->v1) * 0.75f) * (float)btH;
                Texture_DrawUV(bt->texId, posX[panel] + offX + XM_DXP(panel, y1), y1, sw, y2 - y1,
                               bt->u1 * (float)btW, vC1, bt->u2 * (float)btW, vC2, 1.0f, 1.0f, 1.0f, 1.0f);
            }
            #undef EX_PV
        }

        // Pass 1: Hold tails
        for (int ri = startRow; ri <= endRow; ri++)
        {
            if (g_fontArrowETC < 0 || g_zeroSkinArrows) break;   /* Zero: ponta no bloco acima */
                float vri = (ri < g_visualRowCount && g_visualRow) ? (float)g_visualRow[ri] : (float)ri;
            float y = (float)(receptorY + rh2 / 2 + nxAccelDist(p, (float)((vri - visualScrollRow) * pPixelsPerRow)));
            if (y < receptorY - rh2 / 2 - 50 || y > scrollBottom + PANEL_SIZE) continue;
            for (int rio = 0; rio < panelCount; rio++)
            {
                int panel, arrowIdx;
                if (isDoubleOrNightmare) {
                    int halfBase = (rio < 5) ? 0 : 5;
                    int localIdx = kPanelOrder[rio % 5];
                    panel = halfBase + localIdx;
                    arrowIdx = panel % 5;
                } else if (isHalfDouble) {
                    panel = rio;
                    arrowIdx = panel;
                } else {
                    panel = kPanelOrder[rio];
                    arrowIdx = panel;
                }
                uint8_t val = isHalfDouble ? getNoteHD(&g_chart->rows[ri], panel) : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (val != NT_HOLD_T) continue;
                if (g_game.cmdNonStep[p]) continue;
                int idx = g_fontArrowETC + (isHalfDouble ? kHDTailTile[panel] : kTailTile[arrowIdx]);
                float sw = (float)g_game.sprTiles[idx].srcW;
                float sh = (float)g_game.sprTiles[idx].srcH;
                /* Exceed2: ponta pela skin (skinN_l3), no centro da nota.
                 * posX + sw/2 abaixo fica = posX + aw/2 + ajuste da skin. */
                float tailSkinDX = 0.0f;
                if (!isHalfDouble && arrowIdx < 5 && g_skinL3[arrowIdx] >= 0) {
                    float aw = (g_fontArrow542 >= 0) ? (float)g_game.sprTiles[g_fontArrow542].srcW : 54.0f;
                    idx = g_skinL3[arrowIdx];
                    sw = (float)g_game.sprTiles[idx].srcW;
                    sh = (float)g_game.sprTiles[idx].srcH;
                    tailSkinDX = (aw - sw) / 2.0f + g_skinOffX[arrowIdx];
                }
                float tailAlpha = 1.0f;
                if (g_game.cmdVanish[p]) {
                    float fade = (y - 122.0f) / 84.0f;
                    tailAlpha = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade);
                }
                /* X-MODE: a ponta sai na mesma chamada do corpo, com o X da
                 * cabeça do hold (volta até o NT_HOLD_H do mesmo painel). */
                float tailDX = 0.0f;
                if (xmS != 0.0f) {
                    float headY = y;
                    for (int hr = ri - 1; hr >= 0; hr--) {
                        uint8_t hv = isHalfDouble ? getNoteHD(&g_chart->rows[hr], panel)
                                   : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[hr], panel)
                                   : getPanelValue(&g_chart->rows[hr], panel, p));
                        if (hv == NT_HOLD_H) {
                            float vh = (hr < g_visualRowCount && g_visualRow) ? (float)g_visualRow[hr] : (float)hr;
                            headY = (float)(receptorY + rh2 / 2 + nxAccelDist(p, (float)((vh - visualScrollRow) * pPixelsPerRow)));
                            break;
                        }
                        if (hv != NT_HOLD_B) break;
                    }
                    if (g_holdRows[p][panel] >= 0 && headY < xmY0) headY = xmY0;  /* segurado */
                    tailDX = XM_DXP(panel, headY);
                }
                Sprite_DrawTileUV(idx, posX[panel] + sw / 2.0f + tailDX + tailSkinDX, y, sw, sh, tailAlpha);
            }
        }
        // Pass 2: Taps/HoldHeads
        for (int ri = startRow; ri <= endRow; ri++)
        {
            float vri = (ri < g_visualRowCount && g_visualRow) ? (float)g_visualRow[ri] : (float)ri;
            float y = (float)(receptorY + rh2 / 2 + nxAccelDist(p, (float)((vri - visualScrollRow) * pPixelsPerRow)));
            if (y < receptorY - rh2 / 2 - 50 || y > scrollBottom + PANEL_SIZE) continue;
            for (int rio = 0; rio < panelCount; rio++)
            {
                int panel, arrowIdx;
                if (isDoubleOrNightmare) {
                    int halfBase = (rio < 5) ? 0 : 5;
                    int localIdx = kPanelOrder[rio % 5];
                    panel = halfBase + localIdx;
                    arrowIdx = panel % 5;
                } else if (isHalfDouble) {
                    panel = rio;
                    arrowIdx = panel;
                } else {
                    panel = kPanelOrder[rio];
                    arrowIdx = panel;
                }
                uint8_t val = isHalfDouble ? getNoteHD(&g_chart->rows[ri], panel) : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (!val && g_divSpec[p] && !isHalfDouble && !isDoubleOrNightmare && panel < 5) {
                    uint8_t sv = g_divSpec[p][ri * 5 + panel];
                    if (sv == NT_DIV_W || sv == NT_DIV_G) val = sv;   /* A (4) nao desenha */
                }
                if (g_misSpec && !isHalfDouble) {   /* NX WORLD TOUR (0x806ea70) */
                    int col = isDoubleOrNightmare ? panel : panel + (p ? 5 : 0);
                    uint8_t ms = g_misSpec[ri * 10 + col];
                    if (ms == 0x0e) continue;                         /* hidden: julgada, não desenhada */
                    if (!val && (ms == 0x0d || ms == 0x0f)) val = 1;  /* falsa: desenhada */
                    if (!val && ms >= 0x14 && ms <= 0x28) {           /* item */
                        int it = g_misItemSpr[ms - 0x14];
                        if (it >= 0) {
                            /* 0x806ec9e: tile = [play+0x103f0], o mesmo quadro (0..5) das setas */
                            int icnt = sprTileCount(it);
                            if (icnt > 1) it += arrowAnimFrame() % icnt;
                            float isw = (float)g_game.sprTiles[it].srcW, ish = (float)g_game.sprTiles[it].srcH;
                            int ab = (arrowIdx >= 0 && arrowIdx < 5) ? arrowIdx : 0;
                            int as = (ab == 0) ? g_fontArrow542 : (ab == 1) ? g_fontArrow541 : (ab == 2) ? g_fontArrow545 :
                                     (ab == 3) ? g_fontArrow543 : g_fontArrow544;
                            float aw = as >= 0 ? (float)g_game.sprTiles[as].srcW : (float)PANEL_SIZE;
                            Sprite_DrawTileUV(it, posX[panel] + aw / 2.0f + XM_DXP(panel, y), y, isw, ish, 1.0f);
                        }
                        continue;
                    }
                }
                if (!val || val == NT_HOLD_B || val == NT_HOLD_T) continue;

                // HD: pos 0=CN(545), 1=UR(543), 2=DR(544), 3=DL(542), 4=UL(541), 5=CN(545)
                int arrowGroup;
                if (isHalfDouble) {
                    arrowGroup = (panel == 0 || panel == 5) ? 2 : (panel == 1) ? 3 : (panel == 2) ? 4 : (panel == 3) ? 0 : 1;
                } else {
                    arrowGroup = arrowIdx;
                }
                int arrowSpr = (arrowGroup == 0) ? g_fontArrow542 :
                               (arrowGroup == 1) ? g_fontArrow541 :
                               (arrowGroup == 2) ? g_fontArrow545 :
                               (arrowGroup == 3) ? g_fontArrow543 :
                               (arrowGroup == 4) ? g_fontArrow544 : -1;
                /* Notas especiais do Division — PUMPY.EXE 0x412b91..0x412c3f:
                 *   2 -> G: tile 0x8bb194 = ARROWETC.SP2 +6
                 *   3 -> W: tile 0x8bb02c = ARROWETC.SP2 +0
                 *   4 -> nao desenha (so liga uma flag e e apagada) */
                uint8_t nv = (uint8_t)(val & 0x7F);
                if (nv == 4) continue;
                if ((nv == 2 || nv == 3) && g_fontArrowETC >= 0)
                    arrowSpr = g_fontArrowETC + ((nv == 2) ? 6 : 0);  /* confirmado em jogo pelo usuario */
                if (arrowSpr >= 0 && !g_game.cmdNonStep[p]) {
                    int af = arrowAnimFrame(); /* era: (g_game.frameCounter / 3) % 6 */
                    int aidx = arrowSpr + af;
                    float sw = (float)g_game.sprTiles[aidx].srcW;
                    float sh = (float)g_game.sprTiles[aidx].srcH;
                    float noteAlpha = 1.0f;
                    if (g_game.cmdVanish[p]) {
                        float fade = (y - 122.0f) / 84.0f;
                        noteAlpha = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade);
                    }
                    /* Exceed2: nota/cabeça do hold pela skin (skinN / skinN_l1),
                     * centrada onde ficava a seta do ARROW54X + ajuste da skin */
                    int skinBase = -1;
                    if (nv != 2 && nv != 3 && arrowGroup >= 0 && arrowGroup < 5)
                        skinBase = (val == NT_HOLD_H) ? g_skinL1[arrowGroup] : g_skinTap[arrowGroup];
                    /* NX2: a cabeça do long (LongStart) só sai no DrawLongNote (zeroHoldDraw) */
                    if (val == NT_HOLD_H && g_zeroSkinArrows && !isHalfDouble && g_skinL2[arrowGroup] >= 0
                        && g_skinL3[arrowGroup] >= 0) continue;
                    if (skinBase >= 0) {
                        int sIdx = skinBase + af;
                        if (sIdx >= g_game.sprTileCount) sIdx = skinBase;
                        float ssw = (float)g_game.sprTiles[sIdx].srcW;
                        float ssh = (float)g_game.sprTiles[sIdx].srcH;
                        Sprite_DrawTileUV(sIdx, posX[panel] + sw / 2.0f + g_skinOffX[arrowGroup] + XM_DXP(panel, y),
                                          y - g_skinOffY, ssw, ssh, noteAlpha);   /* Zero [0x0862825c] */
                    } else
                    Sprite_DrawTileUV(aidx, posX[panel] + sw / 2.0f + XM_DXP(panel, y), y, sw, sh, noteAlpha);
                }
            }
        }
        /* NX2 DrawPushArrow: depois das notas, antes da seta do long segurado */
        if (g_skinArrowP >= 0 && !isHalfDouble) nxPushArrowDraw(p, panelCount, isDoubleOrNightmare);
        /* Exceed 0x4053D5..0x405419: cabeça do hold segurado redesenhada no
         * receptor, por cima do corpo (a linha da cabeça já foi apagada). */
        for (int panel = 0; panel < panelCount; panel++) {
            if (!exHeldHead[panel]) continue;
            /* NX2: segurado, a cabeça (LongStart em STEP_Y) sai no zeroHoldDraw */
            if (g_zeroSkinArrows && !isHalfDouble && g_skinL2[panel % 5] >= 0 && g_skinL3[panel % 5] >= 0) continue;
            int arrowIdx = isDoubleOrNightmare ? (panel % 5) : panel;
            int arrowGroup = isHalfDouble
                ? ((panel == 0 || panel == 5) ? 2 : (panel == 1) ? 3 : (panel == 2) ? 4 : (panel == 3) ? 0 : 1)
                : arrowIdx;
            int arrowSpr = (arrowGroup == 0) ? g_fontArrow542 :
                           (arrowGroup == 1) ? g_fontArrow541 :
                           (arrowGroup == 2) ? g_fontArrow545 :
                           (arrowGroup == 3) ? g_fontArrow543 :
                           (arrowGroup == 4) ? g_fontArrow544 : -1;
            if (arrowSpr < 0) continue;
            int aidx = arrowSpr + arrowAnimFrame();
            float sw = (float)g_game.sprTiles[aidx].srcW;
            float sh = (float)g_game.sprTiles[aidx].srcH;
            float y = exHeadY[panel];   /* era: (float)(receptorY + rh2 / 2) */
            /* Exceed2: cabeça presa no receptor pela skin (skinN_l1) */
            if (arrowGroup >= 0 && arrowGroup < 5 && g_skinL1[arrowGroup] >= 0) {
                int sIdx = g_skinL1[arrowGroup] + arrowAnimFrame();
                if (sIdx >= g_game.sprTileCount) sIdx = g_skinL1[arrowGroup];
                Sprite_DrawTileUV(sIdx, posX[panel] + sw / 2.0f + g_skinOffX[arrowGroup] + XM_DXP(panel, y), y - g_skinOffY,
                                  (float)g_game.sprTiles[sIdx].srcW, (float)g_game.sprTiles[sIdx].srcH, 1.0f);
            } else
            Sprite_DrawTileUV(aidx, posX[panel] + sw / 2.0f + XM_DXP(panel, y), y, sw, sh, 1.0f);
        }
        #undef XM_DX
        #undef XM_DXP
        #undef XM_S
        g_drawAlphaMul = 1.0f;
        nxFieldEnd();

        int centerY = g_game.screenHeight / 2;
    int receptorY = ZERO_RECEPTOR_Y; /* era 38 (Exceed) */ // Same as in rendering loop


        /* Mode/Modifier sprites do ARROW541.SP2 — idêntico ao song_select.
         * P1 → lado esquerdo, P2 → lado direito (usa `p` da iteração atual). */
        if (g_fontArrow541 >= 0 && !g_zeroSkinArrows) {   /* ícones do ARROW541 (Prex3); Zero: MICON.DAT */
            bool hudRight = (p == 1); /* P2 sempre vai pra direita */
            const char* modeName = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount) ? g_game.songDB.modes[g_game.selectedModeIndex].name : "EASY";
            int modeOff = 31; /* modeez (default) */
            if (strcmp(modeName, "HARD") == 0) modeOff = 32;
            else if (strcmp(modeName, "CRAZY") == 0) modeOff = 33;
            int modeIdx = g_fontArrow541 + modeOff;
            if (modeIdx < g_game.sprTileCount) {
                float sw = (float)g_game.sprTiles[modeIdx].srcW;
                float sh = (float)g_game.sprTiles[modeIdx].srcH;
                float hx = hudRight ? (640.0f - 18.0f - sw/2.0f) : (18.0f + sw/2.0f);
                Sprite_DrawTileUV(modeIdx, hx, 152.0f + sh/2.0f, sw, sh, 1.0f);
            }

            /* Velocidade: raccel(12) quando RV ativo, senão accel1/2/3/4 */
            int speedOff;
            if (g_game.cmdRandomVelocity[p]) {
                speedOff = 12; /* raccel */
            } else {
                speedOff = 36; /* accel1 */
                if      (g_game.cmdSpeedMult[p] >= 4) speedOff = 9;
                else if (g_game.cmdSpeedMult[p] >= 3) speedOff = 8;
                else if (g_game.cmdSpeedMult[p] >= 2) speedOff = 7;
            }
            int speedIdx = g_fontArrow541 + speedOff;
            if (speedIdx < g_game.sprTileCount) {
                float sw = (float)g_game.sprTiles[speedIdx].srcW;
                float sh = (float)g_game.sprTiles[speedIdx].srcH;
                float hx = hudRight ? (640.0f - 18.0f - sw/2.0f) : (18.0f + sw/2.0f);
                Sprite_DrawTileUV(speedIdx, hx, 184.0f + sh/2.0f, sw, sh, 1.0f);
            }

            /* Modificadores: RandomStep(R), Mirror(M), Vanish(V), NonStep(NS)
             * OFF: _randm=34, _mirrr=35, _vanis=37, _nnstp=38
             * ON:   random=10,  mirror=11,  vanish=13,  nonstp=14 */
            static const int   kModOff_OFF[4] = { 34, 35, 37, 38 };
            static const int   kModOff_ON[4]  = { 10, 11, 13, 14 };
            static const float kModY[4]       = { 216.0f, 248.0f, 280.0f, 312.0f };
            bool modActive[4] = {
                g_game.cmdRandomStep[p],
                g_game.cmdMirror[p],
                g_game.cmdVanish[p],
                g_game.cmdNonStep[p]
            };
            for (int di = 0; di < 4; di++) {
                int tileOff = modActive[di] ? kModOff_ON[di] : kModOff_OFF[di];
                int didx = g_fontArrow541 + tileOff;
                if (didx < g_game.sprTileCount) {
                    float sw = (float)g_game.sprTiles[didx].srcW;
                    float sh = (float)g_game.sprTiles[didx].srcH;
                    float hx = hudRight ? (640.0f - 18.0f - sw/2.0f) : (18.0f + sw/2.0f);
                    Sprite_DrawTileUV(didx, hx, kModY[di] + sh/2.0f, sw, sh, 1.0f);
                }
            }
        }

        // Stage indicator sprite (M01-M05)
        // loading.c decrementa stageCount ANTES do gameplay:
        // Stage 1: stageCount=2, Stage 2: stageCount=1, Final: stageCount=0, Bonus: isBonusSong
        int stageSpr = -1;
        if (g_game.isBonusSong) stageSpr = g_fontSprM05;
        else if (g_game.stageCount == 2) stageSpr = g_fontSprM01;
        else if (g_game.stageCount == 1) stageSpr = g_fontSprM02;
        else if (g_game.stageCount == 0) stageSpr = g_fontSprM04;
        if (g_exceedSongIds) {
            /* exceed.exe 0x40C9D7: contador [0x568FF8] 0 -> m01, 1 -> m02,
             * 2 -> m03, 3+ -> m04 (extra). Aqui: stage = 2 - stageCount;
             * extra = isBonusSong. O caso [+0x7EC] == 1 (m04 no stage 0)
             * não foi portado: flag não identificada. */
            if (g_game.isBonusSong)          stageSpr = g_fontSprM04;
            else if (g_game.stageCount == 2) stageSpr = g_fontSprM01;
            else if (g_game.stageCount == 1) stageSpr = g_fontSprM02;
            else                             stageSpr = g_fontSprM03;
        }
        if (stageSpr >= 0 && g_game.sprTileCount > stageSpr && s_nxGauge < 0) {   /* NX: depois da lifebar */
            float sx = (float)g_game.sprTiles[stageSpr].srcX;
            float sy = (float)g_game.sprTiles[stageSpr].srcY;
            float sw = (float)g_game.sprTiles[stageSpr].srcW;
            float sh = (float)g_game.sprTiles[stageSpr].srcH;
            if (g_exceedSongIds) {
                /* exceed.exe 0x40C9B9: com [0x568FF4] & 0xA80 (double) faz
                 * Translate(-283, 0, 0) antes do mXX.spr (x=291 -> 8). */
                if (isDoubleOrNightmare) sx -= 283.0f;
            } else if (isHalfDouble && sprLifeBord >= 0) {
                sx = (float)g_game.sprTiles[sprLifeBord].srcX - sw - 10.0f;
            } else if (isDoubleOrNightmare && g_fontSprW04 >= 0) {
                sx = (float)g_game.sprTiles[g_fontSprW04].srcX - sw - 10.0f;
            }
            Sprite_DrawTileUV(stageSpr, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, 1.0f);
        }

        // Judge + combo display (animacao 3 fases — original FUN_0040dd70)
        /* Exceed2 (PIU32.EXE 0x407400): julgamento e combo como cenas do 00.BGA.
         * Nome da cena pelo tipo: PERFECT..MISS (1P), PER-2P..MIS-2P (2P) ou
         * PER-D..MIS-D (double, [0x484F7C] & 0xA80), no quadro início + contador
         * enquanto o contador < 50. Combo (+0x160) >= 4 em branco; combo de MISS
         * (+0x168) >= 4 em (1, 0.3, 0.3) e tem prioridade. Os dígitos trocam a
         * textura dos slots 13 (unidade), 12, 11 e 10 pelas dos slots 14..23
         * (0.SPR..9.SPR); depois COMBO + 4DIGIT (>= 1000) ou 3DIGIT. */
        if (g_exJudgeBga >= 0 && g_judgeDisplayType[p] != JT_NONE) {
            static const char* k_j1P[6] = { NULL, "PERFECT", "GREAT", "GOOD", "BAD", "MISS" };
            static const char* k_j2P[6] = { NULL, "PER-2P", "GRE-2P", "GOO-2P", "BAD-2P", "MIS-2P" };
            static const char* k_jD[6]  = { NULL, "PER-D", "GRE-D", "GOO-D", "BAD-D", "MIS-D" };
            int cnt = g_exJudgeCnt[p];
            JudgeType jt = g_judgeDisplayType[p];
            if (cnt < 50 && jt > JT_NONE && jt <= JT_MISS) {
                /* NX MODE ([0xa7f4a6d], 0x806f800..0x806f840 / 0x806fad0): judge e combo
                 * em y = 20 + 110 (Y para cima); com a pista em 0x13 (UA/dd), 20 - 110 */
                bool jNx = g_game.cmdNXMode[0] || g_game.cmdNXMode[1];
                glPushMatrix();
                if (jNx) glTranslatef(0.0f, 20.0f + (g_misTrack == 0x13 ? -110.0f : 110.0f), 0.0f);
                bool rg = g_game.cmdGradeRev[p];   /* 0x806f856: sprite 5 - t (só visual) */
                if (rg) jt = (JudgeType)(JT_MISS + JT_PERFECT - jt);
                const char* jn = isDoubleOrNightmare ? k_jD[jt] : (p == 0 ? k_j1P[jt] : k_j2P[jt]);
                BGA_ScenePlayAt(g_exJudgeBga, jn, cnt);                         /* 0x4074FE */

                int combo = (int)g_game.stats.combo[p];
                int miss  = (int)g_game.stats.missCombo[p];
                if (combo >= 4 || miss >= 4) {
                    int val = 0;
                    /* RG (0x806f91e): combo em vermelho e sequência de MISS em branco */
                    if (combo >= 4) { if (rg) BGA_SetColor4(g_exJudgeBga, 1.0f, 0.3f, 0.3f, 1.0f); else BGA_SetColor4(g_exJudgeBga, 1.0f, 1.0f, 1.0f, 1.0f); val = combo; }
                    if (miss >= 4)  { if (rg) BGA_SetColor4(g_exJudgeBga, 1.0f, 1.0f, 1.0f, 1.0f); else BGA_SetColor4(g_exJudgeBga, 1.0f, 0.3f, 0.3f, 1.0f); val = miss; }
                    BGALayerSrc dig[10];
                    for (int d = 0; d < 10; d++) BGA_GetLayerSrc(g_exJudgeBga, 14 + d, &dig[d]);
                    int v = val;
                    for (int slot = 13; slot >= 10; slot--) {                    /* 0x40761F */
                        BGA_SetLayerSrc(g_exJudgeBga, slot, &dig[v % 10]);
                        v /= 10;
                    }
                    const char* cn; const char* dn;
                    if (isDoubleOrNightmare) { cn = "COM-D"; dn = (val >= 1000) ? "D-4DIGIT" : "D-3DIGIT"; }
                    else if (p == 0)         { cn = "COMBO"; dn = (val >= 1000) ? "1P-4DIGIT" : "1P-3DIGIT"; }
                    else                     { cn = "COM-2P"; dn = (val >= 1000) ? "2P-4DIGIT" : "2P-3DIGIT"; }
                    BGA_ScenePlayAt(g_exJudgeBga, cn, cnt);
                    BGA_ScenePlayAt(g_exJudgeBga, dn, cnt);
                    BGA_SetColor4(g_exJudgeBga, 1.0f, 1.0f, 1.0f, 1.0f);       /* 0x407724 */
                }
                glPopMatrix();
            }
        } else
        if (g_judgeDisplayTimer[p] > 0)
        {
            JudgeType jt = g_judgeDisplayType[p];
            int decTimer = g_judgeFrame[p]; // decremented timer (0..24 normal, 0..39 P/G)
            if (decTimer > 39) decTimer = 39;
            /* int isPG = (jt == JT_GREAT || jt == JT_PERFECT); -- o ramo 'P/G' era a logica dos
             * tipos 6/7 do original (0x40de19..0x40de81), que nao desenham judge. */
            int isPG = 0;

            // Tabelas do original (Ghidra DAT_004428d4 / 00442850 / 00442910)
            // normalScaleTable[decTimer] para decTimer 11..24 (pop-in uniform)
            static float normalScale[25] = {
                0.0f,0.0f,0.0f,0.0f,0.0f, 0.0f,0.0f,0.0f,0.0f,0.0f,
                0.0f, // [10]=1.0 (<=10)
                0.99f, 0.98f, 0.97f, 0.98f,  // [11..14]
                0.99f, 1.01f, 1.03f, 1.06f,  // [15..18]
                1.10f, 1.15f, 1.21f, 1.28f,  // [19..22]
                1.35f, 1.43f                   // [23..24]
            };
            // squeezeXTable[decTimer] para decTimer 0..8 (esmagamento X)
            static float squeezeXTable[9] = {
                1.35f, 1.30f, 1.25f, 1.20f, 1.15f,
                1.10f, 1.05f, 1.00f, 1.52f
            };

            float uniformScale;
            float squeezeX = 1.0f;
            float spriteAlpha;

            if (decTimer <= 10) {
                // FASE 2 (timer 0..10): scale uniforme = 1.0
                uniformScale = 1.0f;
            } else if (isPG) {
                if (decTimer > 25) {
                    // P/G EXTENDED (timer 26..39): tabela DAT_00442910
                    // Mapeia 26→11, 39→24 (mesmos valores da normalScale)
                    int idx = decTimer - 15;
                    if (idx < 11) idx = 11;
                    if (idx > 24) idx = 24;
                    uniformScale = normalScale[idx];
                } else {
                    // P/G timer 11..25: constante 0.99 (DAT_004428a8)
                    uniformScale = 0.99f;
                }
            } else {
                // FASE 1 (timer 11..24): tabela normal DAT_004428d4
                int idx = decTimer;
                if (idx > 24) idx = 24;
                if (idx < 11) idx = 11;
                uniformScale = normalScale[idx];
            }
            spriteAlpha = 1.0f;

            if (decTimer < 9) {
                // FASE 3 - Esmagamento (timer 0..8): X squeeze + alpha fade
                squeezeX = squeezeXTable[decTimer];
                spriteAlpha = (float)decTimer * 0.125f;
            }

            // Scale implicito de 0.8x (do glPushMatrix/glScalef interno do original)
            float finalScaleX = uniformScale * squeezeX * 0.8f;
            float finalScaleY = uniformScale * 1.0f * 0.8f;

            // Desenha o sprite do julgamento
            int judgeSpriteIdx = g_fontArrow542 + g_judgeSpriteIndices[jt];
            if (g_fontArrow542 >= 0 && judgeSpriteIdx >= 0 && judgeSpriteIdx < g_game.sprTileCount) {
                float ow = (float)g_game.sprTiles[judgeSpriteIdx].srcW;
                float oh = (float)g_game.sprTiles[judgeSpriteIdx].srcH;

                if (decTimer < 9) {
                    // Additive blend no esmagamento
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                }
                Sprite_DrawTileUV(judgeSpriteIdx, centerX, centerY, ow * finalScaleX, oh * finalScaleY, spriteAlpha);
                if (decTimer < 9) {
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
            }

            // Combo digits usando DEC00 (com a mesma animacao do julgamento)
            int comboVal = 0;
            bool showCombo = false;
            if (jt == JT_MISS) {
                comboVal = g_game.stats.missCombo[p];
                showCombo = comboVal > 3;  /* Ghidra: "if (3 < count)" — exibe a partir de 4 */
            } else {
                comboVal = g_game.stats.combo[p];
                showCombo = comboVal > 3;  /* Ghidra: "if (3 < count)" — exibe a partir de 4 */
            }

            if (showCombo)
            {
                /* Ghidra Judge_RenderAnim (0x40dd70) — layout fiel ao original:
                 *
                 * Dígitos do combo:
                 *   glTranslatef(0, -70, 0) no espaço anim-scaled, depois Combo_RenderValue:
                 *     glTranslatef(24, 0, 0) + glScalef(1.1) + glifos em (0,0)-(44,45)
                 *   → dígito BOTTOM world Y = 240-70 = 170 → screen Y = centerY+70
                 *   → dígito TOP world Y = 219.5 → screen Y = centerY+20.5
                 *   → tens LEFT em local = X_player-20; spacing = 44px (= T(-40) dentro de S(1.1))
                 *
                 * Sprite COMBO_ (label "COMBO" em DEC00.TGA):
                 *   Inner block: glPushMatrix + glScalef(0.8) + RenderSPRData(0x44606c)
                 *   combo_ em ARROW542.SP2: srcX=-67 srcY=-105 srcW=126 srcH=42
                 *   centro local Y-UP = (-4, -84); após 0.8x → screen (centerX-3, centerY+67)
                 */
                float comboScaleX = uniformScale * squeezeX;
                float comboScaleY = uniformScale;

                /* Dígitos — Y: bottom = centerY+70*scale, top = centerY+20.5*scale */
                float dy = (float)centerY + 70.0f * uniformScale - 49.5f * comboScaleY;
                /* X: T(24)*S(1.1)*T(-40 per digit) → tens LEFT = X-20, spacing = 44px */
                float spacing = 44.0f * comboScaleX;
                float tensLeft = (float)centerX - 20.0f * comboScaleX;
                float ux = tensLeft + spacing;   /* units LEFT = X+24 */
                float hx = tensLeft - spacing;   /* hundreds LEFT = X-64 */

                int d3 = comboVal % 10;
                int d2 = (comboVal / 10) % 10;
                int d1 = comboVal / 100;
                float cr = 1.0f;
                float cg = (jt == JT_MISS) ? 0.3f : 1.0f;
                float cb = (jt == JT_MISS) ? 0.3f : 1.0f;
                Font_DrawDecDigit(g_fontDec00Id, ux,       dy, d3, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);
                Font_DrawDecDigit(g_fontDec00Id, tensLeft, dy, d2, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);
                Font_DrawDecDigit(g_fontDec00Id, hx,       dy, d1, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);

                /* Sprite COMBO_ (label): inner 0.8x scale, srcX/srcY em local Y-UP
                 * Centro: (srcX+srcW/2, srcY+srcH/2) = (-4, -84) → screen (centerX-3, centerY+67) */
                if (g_fontArrow542 >= 0) {
                    int comboIdx = g_fontArrow542 + g_judgeSpriteIndices[5] + 1;
                    if (comboIdx < g_game.sprTileCount) {
                        SPRTileDef* ct = &g_game.sprTiles[comboIdx];
                        float sw = (float)ct->srcW * comboScaleX * 0.8f;
                        float sh = (float)ct->srcH * comboScaleY * 0.8f;
                        /* Posição calculada a partir do srcX/srcY do tile (local Y-UP, inner 0.8x): */
                        float ctCX = (float)(ct->srcX + ct->srcW / 2);   /* centro local X ≈ -4 */
                        float ctCY = (float)(ct->srcY + ct->srcH / 2);   /* centro local Y ≈ -84 (Y-UP) */
                        float comboTextX = (float)centerX + ctCX * squeezeX * 0.8f * uniformScale;
                        float comboTextY = (float)centerY - ctCY * 0.8f * uniformScale; /* Y-UP → screen Y-DOWN */
                        Sprite_DrawTileUV(comboIdx, comboTextX, comboTextY, sw, sh, spriteAlpha);
                    }
                }
            }
        }  // end judge/combo

        // Pop-up de score (catch effect)
        for (int i = 0; i < MAX_POPUPS; i++) {
            if (!g_popups[i].active || g_popups[i].player != p) continue;
            char buf[32];
            int popCenterX = centerX;
            snprintf(buf, sizeof(buf), "+%d", g_popups[i].score);
            Font_DrawStringCenteredScaled(popCenterX, (int)g_popups[i].y, buf, 1,1,0, g_popups[i].alpha, 1.2f);
            if (g_popups[i].combo > 1) {
                snprintf(buf, sizeof(buf), "%d", g_popups[i].combo);
                Font_DrawStringCenteredScaled(popCenterX, (int)g_popups[i].y - 16, buf, 1,1,1, g_popups[i].alpha * 0.7f, 0.8f);
            }
        }
    }  // end for p

    // Life bars (03/04/05 ou W03/W04/W05) — renderizadas DEPOIS de todos os players
    if (s_nxGauge >= 0) {
        /* NX: 0x806ca5c passa a fração da batida; o "beat" daqui é 1 - fração */
        float beat = 1.0f;
        if (g_chart && g_songLoaded && (float)g_songTime > 0.1f) {
            float curBpm = (float)g_chart->segments[0].bpm;
            double acc = g_chartDelay;
            for (int s = 0; s < g_chart->segmentCount; s++) {
                double segDur = g_chart->segments[s].rowCount * getSegmentSpr(s) + getSegmentDelay(s);
                if (g_songTime < acc + segDur || s == g_chart->segmentCount - 1) {
                    curBpm = (float)g_chart->segments[s].bpm;
                    break;
                }
                acc += segDur;
            }
            if (curBpm > 0.0f) {
                float period = 60.0f / curBpm;
                beat = 1.0f - fmodf((float)g_songTime, period) / period;
            }
        }
        bool both = (pRend1 - pRend0) >= 2;
        ctxUse(0);
        for (int p = pRend0; p < pRend1; p++)
            nxLifebarDraw(p, 1.0f - beat, both);
        /* NX 0x8069b00 (depois da lifebar, 0x806ce65): m00 1st, m01 2nd, m02 Final,
         * m03 BONUS — o m0%d.spr inteiro (texto + moldura M04) */
        int stg = g_game.isBonusSong ? g_fontSprM04 : g_game.stageCount == 2 ? g_fontSprM01
                : g_game.stageCount == 1 ? g_fontSprM02 : g_fontSprM03;
        if (stg >= 0) {
            int n = sprTileCount(stg);
            for (int t = n - 1; t >= 0; t--) exLifeTile(stg + t, 1, 1, 1, 1);
        }
    } else if (g_exceedSongIds && g_fontSprGGS >= 0) {
        /* Exceed: 0x40B084, chamada com (p*5, beat). Double (flags 0xA80 em
         * [0x568FF4], aqui aproximado pelo modo do projeto) usa gg_d. */
        float beat = 1.0f;
        if (g_chart && g_songLoaded && (float)g_songTime > 0.1f) {
            float curBpm = (float)g_chart->segments[0].bpm;
            double acc = g_chartDelay;
            for (int s = 0; s < g_chart->segmentCount; s++) {
                double segDur = g_chart->segments[s].rowCount * getSegmentSpr(s) + getSegmentDelay(s);
                if (g_songTime < acc + segDur || s == g_chart->segmentCount - 1) {
                    curBpm = (float)g_chart->segments[s].bpm;
                    break;
                }
                acc += segDur;
            }
            if (curBpm > 0.0f) {
                float period = 60.0f / curBpm;
                beat = 1.0f - fmodf((float)g_songTime, period) / period;
            }
        }
        if (isDoubleOrNightmare && g_fontSprGGD >= 0) {
            exLifebarDraw(0, beat, true);
        } else {
            for (int p = pRend0; p < pRend1; p++)
                exLifebarDraw(p, beat, false);
        }
        if (g_game.isBattleMode && !isDoubleOrNightmare)
            exBattleDraw(beat);
    } else if (isDoubleOrNightmare) {
        /* DN lifebar: mesma lógica de pulse BPM + glow que single/halfdouble.
         * W04 = única sprite de fill que cresce da ESQUERDA proporcional à vida.
         * Ordem: fill → glow → border (igual single mode). */
        int lifeValDN = g_game.stats.life[0]; /* DN cooperativo: usa vida de P1 */

        /* BPM pulse — fórmula subtrativa original Ghidra (array 0x442758).
         * displayF = clamp(life*0.001 - (1-bpmTiming)*k, 0, 1)   k=0.1 simples, 0.05 HD/DN
         * bpmTiming cai de 1.0 (inicio do beat) a 0.0 (fim do beat).
         * Com vida baixa o clamp em 0 evita que o pulse apareca — comportamento original. */
        static const float bpmTimingArrDN[60] = {
            1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f, /* 0-9  */
            0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f, /* 10-19 */
            0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f, /* 20-29 */
            0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f, /* 30-39 */
            0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f, /* 40-49 */
            0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f  /* 50-59 */
        };
        float bpmTimingDN = 1.0f;
        if (g_chart && g_songLoaded && (float)g_songTime > 0.1f) {
            float curBpm = (float)g_chart->segments[0].bpm;
            double acc = g_chartDelay;
            for (int s = 0; s < g_chart->segmentCount; s++) {
                double segDur = g_chart->segments[s].rowCount * getSegmentSpr(s) + getSegmentDelay(s);
                if (g_songTime < acc + segDur || s == g_chart->segmentCount - 1) {
                    curBpm = (float)g_chart->segments[s].bpm; break;
                } acc += segDur;
            }
            if (curBpm > 0.0f) {
                float beatPeriodSec    = 60.0f / curBpm;
                float beatPeriodFrames = beatPeriodSec * 60.0f;
                float frameInBeat      = fmodf((float)g_songTime * 60.0f, beatPeriodFrames);
                int   beatIdx          = (int)(frameInBeat / beatPeriodFrames * 60.0f);
                if (beatIdx < 0)  beatIdx = 0;
                if (beatIdx > 59) beatIdx = 59;
                bpmTimingDN = bpmTimingArrDN[beatIdx];
            }
        }
        float displayFDN = (float)lifeValDN * LIFE_BAR_SCALE - (1.0f - bpmTimingDN) * 0.05f;
        if (displayFDN < 0.0f) displayFDN = 0.0f;
        if (displayFDN > 1.0f) displayFDN = 1.0f;

        /* iVar5 para lifeIsFull — fórmula original (igual single/halfdouble) */
        float scaledDN   = displayFDN * (-256.0f);
        int roundedDN    = (int)scaledDN;   /* _ftol trunca (0x412141); antes arredondava */
        int roundAbsDN   = (roundedDN >= 0) ? roundedDN : -roundedDN;
        int quotientDN   = roundAbsDN / 6;
        int iVar5DN      = 253 - quotientDN * 6;
        if (iVar5DN < 1) iVar5DN = 1;
        bool lifeIsFullDN = (iVar5DN < 2);

        /* W04: barra contínua formada por N tiles sequenciais.
         * Tile 0 preenche primeiro; tile 1 só começa após tile 0 estar cheio (~50%).
         * fillW = totalW * displayFDN pixels a preencher no total. */
        if (g_fontSprW04 >= 0) {
            int tileCnt = sprTileCount(g_fontSprW04);
            /* Soma largura total dos tiles */
            float totalW = 0.0f;
            for (int t = 0; t < tileCnt; t++)
                totalW += (float)g_game.sprTiles[g_fontSprW04 + t].srcW;
            float fillW    = totalW * displayFDN; /* pixels totais a preencher */
            float consumed = 0.0f;
            for (int t = 0; t < tileCnt; t++) {
                int idx = g_fontSprW04 + t;
                SPRTileDef* tile = &g_game.sprTiles[idx];
                float tileW = (float)tile->srcW;
                float remaining = fillW - consumed;
                if (remaining <= 0.0f) break;       /* tiles seguintes ficam ocultos */
                if (tile->texId < 0) { consumed += tileW; continue; }
                int tw = Texture_GetWidth(tile->texId);  if (tw <= 0) tw = 256;
                int th = Texture_GetHeight(tile->texId); if (th <= 0) th = 256;
                float u1 = tile->u1 * (float)tw;
                float v1 = tile->v1 * (float)th;
                float u2 = tile->u2 * (float)tw;
                float v2 = tile->v2 * (float)th;
                float w_draw = (remaining < tileW) ? remaining : tileW;
                float frac   = w_draw / tileW;          /* 0-1 dentro deste tile */
                float uRight = u1 + (u2 - u1) * frac;   /* UV proporcional */
                Texture_DrawUV(tile->texId, (float)tile->srcX, (float)tile->srcY,
                               w_draw, (float)tile->srcH,
                               u1, v1, uRight, v2, 1.0f, 1.0f, 1.0f, 1.0f);
                consumed += tileW;
            }
        }

        /* W05 glow — mesma lógica que single/halfdouble */
        if (sprLifeGlow >= 0) {
            #define DRAW_GLOW_DN(R, G, B, A) do { \
                int _cnt = sprTileCount(sprLifeGlow); \
                for (int _t = 0; _t < _cnt; _t++) { \
                    int _idx = sprLifeGlow + _t; \
                    SPRTileDef* _gt = &g_game.sprTiles[_idx]; \
                    if (_gt->texId < 0) continue; \
                    int _tw = Texture_GetWidth(_gt->texId);  if (_tw <= 0) _tw = 256; \
                    int _th = Texture_GetHeight(_gt->texId); if (_th <= 0) _th = 256; \
                    Texture_DrawUV(_gt->texId, (float)_gt->srcX, (float)_gt->srcY, \
                                   (float)_gt->srcW, (float)_gt->srcH, \
                                   _gt->u1*(float)_tw, _gt->v1*(float)_th, \
                                   _gt->u2*(float)_tw, _gt->v2*(float)_th, (R),(G),(B),(A)); \
                } \
            } while(0)
            /* Branco: barra cheia → pisca a cada 3 frames */
            if (lifeIsFullDN && (g_game.frameCounter % 3 == 0)) {
                DRAW_GLOW_DN(1.0f, 1.0f, 1.0f, 0.9f);
            }
            /* Vermelho: vida em perigo → pisca a cada 2 frames */
            if (lifeValDN < LIFE_DANGER && (g_game.frameCounter & 1) == 0) {
                DRAW_GLOW_DN(1.0f, 0.0f, 0.0f, 0.8f);
            }
            #undef DRAW_GLOW_DN
        }

        /* W03 border — por último, sobrepõe fill e glow (igual single mode) */
        if (sprLifeBord >= 0) {
            int cnt = sprTileCount(sprLifeBord);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = sprLifeBord + t;
                SPRTileDef* bt = &g_game.sprTiles[idx];
                float bsx = (float)bt->srcX;
                float bsy = (float)bt->srcY;
                float bsw = (float)bt->srcW;
                float bsh = (float)bt->srcH;
                Sprite_DrawTileUV(idx, bsx + bsw / 2.0f, bsy + bsh / 2.0f, bsw, bsh, 1.0f);
            }
        }
    } else {
    for (int p = pRend0; p < pRend1; p++) {
        float px = (p == 0) ? 0.0f : 320.0f;

        int lifeValP = g_game.stats.life[p];

        /* Fórmula subtrativa original Ghidra (array 0x442758):
         * displayF = clamp(life*0.001 - (1-bpmTiming)*k, 0, 1)   k=0.1 simples, 0.05 HD/DN
         * bpmTiming cai de 1.0 (inicio do beat) a 0.0 (fim do beat).
         * Com vida baixa o clamp em 0 impede que o pulse apareca — comportamento original. */
        static const float bpmTimingArr[60] = {
            1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f, /* 0-9  */
            0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f, /* 10-19 */
            0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f, /* 20-29 */
            0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f, /* 30-39 */
            0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f, /* 40-49 */
            0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f  /* 50-59 */
        };
        float bpmTimingP = 1.0f;
        if (g_chart && g_songLoaded && (float)g_songTime > 0.1f) {
            float curBpm = (float)g_chart->segments[0].bpm;
            double acc = g_chartDelay;
            for (int s = 0; s < g_chart->segmentCount; s++) {
                double segDur = g_chart->segments[s].rowCount * getSegmentSpr(s) + getSegmentDelay(s);
                if (g_songTime < acc + segDur || s == g_chart->segmentCount - 1) {
                    curBpm = (float)g_chart->segments[s].bpm;
                    break;
                }
                acc += segDur;
            }
            if (curBpm > 0.0f) {
                float beatPeriodSec    = 60.0f / curBpm;
                float beatPeriodFrames = beatPeriodSec * 60.0f;
                float frameInBeat      = fmodf((float)g_songTime * 60.0f, beatPeriodFrames);
                int   beatIdx          = (int)(frameInBeat / beatPeriodFrames * 60.0f);
                if (beatIdx < 0)  beatIdx = 0;
                if (beatIdx > 59) beatIdx = 59;
                bpmTimingP = bpmTimingArr[beatIdx];
            }
        }
        float displayF = (float)lifeValP * LIFE_BAR_SCALE - (1.0f - bpmTimingP) * (isHalfDouble ? 0.05f : 0.1f);
        if (displayF < 0.0f) displayF = 0.0f;
        if (displayF > 1.0f) displayF = 1.0f;

        float scaled = displayF * (-256.0f);
        int rounded = (int)scaled;   /* _ftol trunca (0x412141); antes arredondava */
        int roundedAbs = (rounded >= 0) ? rounded : -rounded;
        int quotient = roundedAbs / 6;
        int iVar5 = 253 - quotient * 6;
        if (iVar5 < 1) iVar5 = 1;

        bool lifeIsFull = (iVar5 < 2);
        float lifePct = (256.0f - (float)iVar5) / 255.0f;

        /* Half-double: fill via ST02.png — HD04.SPR hipotetico:
         * seg1: srcX=172 srcY=9 srcW=154 srcH=14  tex(0,144,154,158)
         * seg2: srcX=325 srcY=9 srcW=145 srcH=14  tex(0,160,145,174)
         * displayW proporcional a vida (0-299). Texture_DrawUV espera pixels. */
        if (isHalfDouble && g_fontSpr04 >= 0 && g_fontSpr03 >= 0 && sprLifeBord >= 0) {
            int texHd = g_game.sprTiles[sprLifeBord].texId;
            if (texHd >= 0) {
                int th = Texture_GetHeight(texHd); if (th <= 0) th = 256;
                float fillOffX = (float)(g_game.sprTiles[sprLifeBord].srcX - g_game.sprTiles[g_fontSpr03].srcX);
                float fillX = px + fillOffX + (float)g_game.sprTiles[g_fontSpr04].srcX - 2.0f;
                float fillY = (float)g_game.sprTiles[g_fontSpr04].srcY;
                int displayW = (int)(displayF * (154.0f + 145.0f) + 0.5f);
                int seg1w = (displayW > 154) ? 154 : displayW;
                if (seg1w > 0) {
                    /* V=0 é o topo do ST02.PNG (decoder PNG não inverte linhas):
                     * linhas 144..158 entram direto. O (th - 1 - v) era da
                     * convenção TGA e lia as linhas 97..111 (outro sprite).
                    Texture_DrawUV(texHd, fillX, fillY, (float)seg1w, 14.0f,
                        0, (float)(th - 1 - 158), (float)seg1w,
                        (float)(th - 1 - 144), 1,1,1,1); */
                    Texture_DrawUV(texHd, fillX, fillY, (float)seg1w, 14.0f,
                        0, 144.0f, (float)seg1w, 158.0f, 1,1,1,1);
                }
                if (displayW > 154) {
                    int seg2w = displayW - 154;
                    /* Idem: linhas 160..174 direto.
                    Texture_DrawUV(texHd, fillX + 153.0f, fillY, (float)seg2w, 14.0f,
                        0, (float)(th - 1 - 174), (float)seg2w,
                        (float)(th - 1 - 160), 1,1,1,1); */
                    Texture_DrawUV(texHd, fillX + 153.0f, fillY, (float)seg2w, 14.0f,
                        0, 160.0f, (float)seg2w, 174.0f, 1,1,1,1);
                }
            }
        } else if (g_fontSpr04 >= 0) {  /* 04.SPR fill — non-coop */
            int cnt = sprTileCount(g_fontSpr04);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = g_fontSpr04 + t;
                SPRTileDef* tile = &g_game.sprTiles[idx];
                float sx = px + (float)tile->srcX - (p == 0 ? 2.0f : 0.0f);
                float sy = (float)tile->srcY;
                float sw = (float)tile->srcW;
                float sh = (float)tile->srcH;
                if (tile->texId < 0) continue;
                int tw = Texture_GetWidth(tile->texId);
                int th = Texture_GetHeight(tile->texId);
                if (tw <= 0) tw = 256;
                if (th <= 0) th = 256;
                float u1 = (float)tile->u1 * (float)tw;
                float v1 = (float)tile->v1 * (float)th;
                float u2 = (float)tile->u2 * (float)tw;
                float v2 = (float)tile->v2 * (float)th;
                /* P1: ancora u1 (valor alto = u=1.0 do atlas) na DIREITA — ponta vermelha fica à direita.
                 * Ghidra P1: right x=256 u=1.0 FIXO; left x=iVar5 u=iVar5/256 (crescente L→R).
                 * Atlas armazena o sprite espelhado (u1>u2): u1=atlas-right=borda vermelha, u2=atlas-left.
                 * Para display correto: uLeft de u2 até u1, crescente da esquerda para a direita. */
                float x_draw = sx + sw * (1.0f - lifePct);
                float w_draw = sw * lifePct;
                float uLeft  = u2 + (u1 - u2) * (1.0f - lifePct);
                if (w_draw <= 0.0f) continue;
                Texture_DrawUV(tile->texId, x_draw, sy, w_draw, sh,
                              uLeft, v1, u1, v2, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }
        /* 05.SPR glow — Ghidra: MESMO sprite (0x9e0250) para BRANCO e VERMELHO.
         * g_nP1Connected = frame counter simples (++no fim de GameplayUpdate).
         * BRANCO: iVar5<2 (barra cheia) && g_nP1Connected%3==0 → a cada 3 frames, cor branca.
         * VERMELHO: HP<0xb4=180 && (g_nP1Connected&1)==0 → a cada 2 frames, cor vermelha. */
        if (sprLifeGlow >= 0) {
            /* Renderizar helper inline para não duplicar código */
            #define DRAW_GLOW(R, G, B, A) do { \
                int _cnt = sprTileCount(sprLifeGlow); \
                for (int _t = _cnt - 1; _t >= 0; _t--) { \
                    int _idx = sprLifeGlow + _t; \
                    SPRTileDef* _gt = &g_game.sprTiles[_idx]; \
                    if (_gt->texId < 0) continue; \
                    int _tw = Texture_GetWidth(_gt->texId); if (_tw <= 0) _tw = 256; \
                    int _th = Texture_GetHeight(_gt->texId); if (_th <= 0) _th = 256; \
                    Texture_DrawUV(_gt->texId, px+(float)_gt->srcX, (float)_gt->srcY, \
                                   (float)_gt->srcW, (float)_gt->srcH, \
                                   _gt->u1*(float)_tw, _gt->v1*(float)_th, \
                                   _gt->u2*(float)_tw, _gt->v2*(float)_th, (R),(G),(B),(A)); \
                } \
            } while(0)

            /* Branco: vida cheia (lifePct≈1.0) → flash a cada 3 frames */
            if (lifeIsFull && (g_game.frameCounter % 3 == 0)) {
                DRAW_GLOW(1.0f, 1.0f, 1.0f, 0.9f);
            }
            /* Vermelho: vida em perigo (HP < LIFE_DANGER=180) → flash a cada 2 frames */
            if (g_game.stats.life[p] < LIFE_DANGER && (g_game.frameCounter & 1) == 0) {
                DRAW_GLOW(1.0f, 0.0f, 0.0f, 0.8f);
            }
            #undef DRAW_GLOW
        }
        /* 03.SPR border — renderizado por último, sobrepõe fill e glow */
        if (sprLifeBord >= 0) {
            int cnt = sprTileCount(sprLifeBord);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = sprLifeBord + t;
                SPRTileDef* bt = &g_game.sprTiles[idx];
                float bsx = px + (float)bt->srcX;
                float bsy = (float)bt->srcY;
                float bsw = (float)bt->srcW;
                float bsh = (float)bt->srcH;
                Sprite_DrawTileUV(idx, bsx + bsw / 2.0f, bsy + bsh / 2.0f, bsw, bsh, 1.0f);
            }
        }
        }  // end for p (life bars)
    }

    // Explosao: seta congelada + ARROWF (depois de tudo, sobrepoe tudo)
    for (int pe = pRend0; pe < pRend1; pe++) {
        nxSkinUse(pe);   /* NX2: m_Skin do jogador */
        ctxUse(pe);
        /* NX/UA: explosão e spark saem da mesma função dos receptores (piu 0x8069d20,
         * spark em play+0x1134, 0x806a0cc), dentro da câmera da pista */
        nxFieldBegin();
        float expPosX[10];
        int expPanels;
        if (isHalfDouble) {
            expPanels = 6;
            for (int i = 0; i < 6; i++) expPosX[i] = 171.0f + i * 48.0f + (i >= 3 ? 7.0f : 0.0f);
        } else if (isDoubleOrNightmare) {
            expPanels = 10;
            for (int i = 0; i < 5; i++) expPosX[i] = 74.0f + i * 48.0f;
            for (int i = 5; i < 10; i++) expPosX[i] = 323.0f + (i-5) * 48.0f;
        } else {
            expPanels = 5;
            if (pe == 1) {
                for (int i = 0; i < 5; i++) expPosX[i] = 358.0f + i * 48.0f;
            } else {
                for (int i = 0; i < 5; i++) expPosX[i] = 38.0f + i * 48.0f;
            }
        }
        float erY = 38.0f + 28.0f + 1.0f; /* +1px ajuste fino de posição */
        static const float expOffXReg[5] = {-7.0f, -6.0f, -5.0f, -6.0f, -7.0f};
        static const float expOffXHD[6]  = {-5.0f, -6.0f, -7.0f, -7.0f, -6.0f, -5.0f};
        /* Zero 0x807eb20 (com 0x8083070): origem (campo + 2, 378 + 5) no sistema Y
         * para cima, coluna * 49. Em cada quadro t < 24:
         *   arrowf.spr[coluna] aditivo, alfa 1 - t/24, escala 1 + t/100 em (32,32);
         *   skinN.spr aditivo, mesma cor e escala em (27,27) + desvio da coluna
         *   e o deslocamento da skin;
         *   t < 15: sparkN.spr quadro t/3 em origem - (90, 95), aditivo. */
        if (g_zeroSkinArrows && !isHalfDouble) {
            static const float k_dx[5] = { -1, -2, -2, 0, -2 }, k_dy[5] = { -1, -3, -3, -4, -2 };
            static const int k_spark[5] = { 0, 2, 4, 1, 3 };   /* DL s1, UL s3, C s5, UR s2, DR s4 */
            #define Y_UP(yy) (480.0f - (yy))
            for (int pan = 0; pan < expPanels; pan++) {
                if (g_noteState[pe][pan] != 1) continue;
                int col = pan % 5;
                float t = (float)g_noteExplodeFrame[pe][pan];
                if (t >= 24.0f) continue;
                float field;
                if (isDoubleOrNightmare) field = (pan < 5) ? 65.0f + g_skinFieldX[0] : 312.0f + g_skinFieldX[1];
                else                     field = (pe == 1 ? 348.0f : 28.0f) + g_skinFieldX[0];
                float ox = field + 2.0f + col * 49.0f, oy = 383.0f;
                float a = 1.0f - t / 24.0f, sc = 1.0f + t / 100.0f;
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                if (g_fontArrowF >= 0 && g_fontArrowF + col < g_game.sprTileCount) {
                    int fi = g_fontArrowF + col;
                    float w = (float)g_game.sprTiles[fi].srcW, h = (float)g_game.sprTiles[fi].srcH;
                    float cx = 32.0f + (w * 0.5f - 32.0f) * sc, cy = 32.0f + (h * 0.5f - 32.0f) * sc;
                    Sprite_DrawTileUV(fi, ox + cx, Y_UP(oy + cy), w * sc, h * sc, a);
                }
                if (g_skinTap[col] >= 0) {
                    int si = g_skinTap[col] + arrowAnimFrame();
                    if (si >= g_game.sprTileCount) si = g_skinTap[col];
                    float w = (float)g_game.sprTiles[si].srcW, h = (float)g_game.sprTiles[si].srcH;
                    float bx = k_dx[col] + 27.0f + (g_skinOffX[col] + w * 0.5f - 27.0f) * sc;
                    float by = k_dy[col] + 27.0f + (g_skinOffY + h * 0.5f - 27.0f) * sc;
                    Sprite_DrawTileUV(si, ox + bx, Y_UP(oy + by), w * sc, h * sc, a);
                }
                int sp = g_skinSpark[k_spark[col]];
                if (t < 15.0f && sp >= 0) {
                    int si = sp + (int)t / 3;
                    if (si < g_game.sprTileCount) {
                        float w = (float)g_game.sprTiles[si].srcW, h = (float)g_game.sprTiles[si].srcH;
                        Sprite_DrawTileUV(si, ox - 90.0f + w * 0.5f, Y_UP(oy - 95.0f + h * 0.5f), w, h, 1.0f);
                    }
                }
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            #undef Y_UP
            nxFieldEnd();   /* este continue pulava o nxFieldEnd do fim do laço: a pilha de
                             * matrizes enchia a cada quadro e o combo do NX Mode perdia o deslocamento */
            continue;
        }
        for (int pan = 0; pan < expPanels; pan++) {
            if (g_noteState[pe][pan] != 1) continue;
            int base;
            if (isHalfDouble) {
                int arrow = (pan == 0 || pan == 5) ? 2 : (pan == 1) ? 3 : (pan == 2) ? 4 : (pan == 3) ? 0 : 1;
                base = (arrow == 0) ? g_fontArrow542 :
                       (arrow == 1) ? g_fontArrow541 :
                       (arrow == 2) ? g_fontArrow545 :
                       (arrow == 3) ? g_fontArrow543 :
                       (arrow == 4) ? g_fontArrow544 : -1;
            } else {
                int arrowIdx = isDoubleOrNightmare ? (pan % 5) : pan;
                base = (arrowIdx == 0) ? g_fontArrow542 :
                       (arrowIdx == 1) ? g_fontArrow541 :
                       (arrowIdx == 2) ? g_fontArrow545 :
                       (arrowIdx == 3) ? g_fontArrow543 :
                                         g_fontArrow544;
            }
            if (base < 0) continue;
            float ef = (float)g_noteExplodeFrame[pe][pan];
            float eAlpha = 1.0f - ef / 24.0f; /* fade suave do frame 0 ao 24 */
            int af = arrowAnimFrame(); /* era: (g_game.frameCounter / 3) % 6 */
            int aSpr = base + af;
            float sw = (float)g_game.sprTiles[aSpr].srcW;
            float sh = (float)g_game.sprTiles[aSpr].srcH;

            /* Camada 1: seta colorida */
            Sprite_DrawTileUV(aSpr, expPosX[pan] + sw / 2.0f, erY, sw, sh, eAlpha);

            /* Camada 2 + 3: glow em PERFECT/GREAT */
            {
                int ht = g_glowTimer[pe][pan];
                if (ht > 0) {
                    /* exceed.exe 0x406D2D..0x406E66: cor (1,1,1, 1 - t/24) e escala 1 + t/100
                     * (1.0 -> 1.24) no arrowf e na seta aditiva, uma vez cada. */
                    /* float ga = (float)ht / 24.0f; */
                    float ga  = eAlpha;
                    float gsc = 1.0f + ef / 100.0f;
                    (void)ht;

                    /* Camada 2: mesma seta aditiva, uma vez, crescendo */
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                    Sprite_DrawTileUV(aSpr, expPosX[pan] + sw / 2.0f, erY, sw * gsc, sh * gsc, ga);
                    /* Antes: 3x sem escala (acumulava branco)
                    Sprite_DrawTileUV(aSpr, expPosX[pan] + sw / 2.0f, erY, sw, sh, ga);
                    Sprite_DrawTileUV(aSpr, expPosX[pan] + sw / 2.0f, erY, sw, sh, ga);
                    Sprite_DrawTileUV(aSpr, expPosX[pan] + sw / 2.0f, erY, sw, sh, ga);
                    */

                    /* Camada 3: arrowf.spr aditivo — mesma proporção inicial do HitKey (0.8x), sem crescer.
                     * Posição usa o mesmo offset p1OffX do HitKey (centralizado no receptor). */
                    if (g_fontArrowF >= 0) {
                        int fCnt = sprTileCount(g_fontArrowF);
                        int arrowType;
                        if (isHalfDouble) {
                            arrowType = (pan == 0 || pan == 5) ? 2 : (pan == 1) ? 3 : (pan == 2) ? 4 : (pan == 3) ? 0 : 1;
                        } else if (isDoubleOrNightmare) {
                            arrowType = pan % 5;
                        } else {
                            arrowType = pan;
                        }
                        int fIdx = (fCnt > 0 && arrowType < fCnt) ? arrowType : 0;
                        int fSpr = g_fontArrowF + fIdx;
                        if (fSpr < g_game.sprTileCount) {
                            float fw = (float)g_game.sprTiles[fSpr].srcW;
                            float fh = (float)g_game.sprTiles[fSpr].srcH;
                            /* Mesmo offset de posição do HitKey (p1OffX) */
                            static const float afOffXReg[5] = {-7.0f, -6.0f, -5.0f, -6.0f, -7.0f};
                            static const float afOffXHD[5]  = {-5.0f, -6.0f, -7.0f, -6.0f, -5.0f};
                            const float* afOffX = (isHalfDouble || isDoubleOrNightmare) ? afOffXHD : afOffXReg;
                            int afPan = isDoubleOrNightmare ? (pan % 5) : (isHalfDouble ? arrowType : pan);
                            float cx = expPosX[pan] + afOffX[afPan] + fw / 2.0f;
                            /* Escala 1 + t/100 (gsc acima), centrada — 0x406D6D..0x406DD9 */
                            Sprite_DrawTileUV(fSpr, cx, erY, fw * gsc, fh * gsc, ga);
                        }
                    }

                    /* Exceed2 0x403681..0x403847: spark.spr aditivo no painel,
                     * quadro = t/3 enquanto t < 15 (5 quadros de 256x256), com o
                     * mesmo temporizador da explosão. Centro no painel: o original
                     * posiciona por Translate (-90 + 49*k, -95) na matriz do
                     * receptor — aproximado aqui pelo centro da seta. */
                    if (g_fontSpark >= 0 && ef < 15.0f) {
                        int sf = (int)ef / 3;
                        int sCnt = sprTileCount(g_fontSpark);
                        if (sCnt > 0 && sf < sCnt && g_fontSpark + sf < g_game.sprTileCount) {
                            int sIdx = g_fontSpark + sf;
                            float spw = (float)g_game.sprTiles[sIdx].srcW;
                            float sph = (float)g_game.sprTiles[sIdx].srcH;
                            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                            Sprite_DrawTileUV(sIdx, expPosX[pan] + sw / 2.0f, erY, spw, sph, 1.0f);
                        }
                    }

                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
            }
        }
        nxFieldEnd();
    }
    ctxUse(0);

    /* Timer regressivo - desativado
    {
        double totalSec = g_totalSongSeconds;
        if (totalSec <= 0) totalSec = g_songTime + 30.0;
        double secLeft = totalSec - g_songTime;
        if (secLeft < 0) secLeft = 0;
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%.0f:%02.0f", secLeft/60, fmod(secLeft, 60));
        Font_DrawStringCentered(g_game.screenWidth/2, g_game.screenHeight/2 - 40, timeBuf, 1, 1, 1, 0.7f);
        Font_DrawStringCentered(g_game.screenWidth/2, 10, timeBuf, 0.7f, 0.7f, 0.7f, 1.0f);
    }
    */

    if (anyAutoPanel()) {
        char buf[64] = {0}; int pos = 0;
        int apAn = isDoubleOrNightmare ? 10 : (isHalfDouble ? 6 : 5);
        for (int a = 0; a < apAn; a++)
            if (g_autoPanel[a]) {
                int l = snprintf(buf+pos, sizeof(buf)-pos, "%d ", a);
                if (l > 0) pos += l;
            }
        Font_DrawStringCentered(g_game.screenWidth/2, 28, buf, 0, 1, 0, 0.7f);
    }
    /* Debug (F11): cronometro da musica e frame do VSL no centro da tela.
     * g_songTime = posicao da BGM - AudioOffset (mesmo relogio das setas).
     * PUMPY_DUMP_DIR + PUMPY_DUMP_AT="s1,s2,..." salvam o framebuffer em BMP. */
    if (g_game.showDebug) {
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0, 640, 0, 480, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDisable(GL_ALPHA_TEST);
        glDisable(GL_LIGHTING); glDisable(GL_STENCIL_TEST); glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0, 0, 0, 0.6f);
        glBegin(GL_QUADS); glVertex2f(200, 190); glVertex2f(440, 190); glVertex2f(440, 272); glVertex2f(200, 272); glEnd();
        Font_Init(); /* Resource_ClearBGA -> Font_Shutdown zera a fonte de debug */
        double t = g_songTime; if (t < 0) t = 0;
        { static int once = 0; if (once++ < 3) Log_Print("DIAG TIMER: bloco executado t=%.2f\n", t); }
        int mm = (int)(t / 60.0), ss = (int)t % 60, cc = (int)((t - (int)t) * 100.0);
        char tb[48];
        snprintf(tb, sizeof(tb), "%02d:%02d.%02d", mm, ss, cc);
        Font_DrawStringCenteredScaled(322, 218, tb, 0, 0, 0, 1, 3.0f);
        Font_DrawStringCenteredScaled(320, 216, tb, 1, 1, 0, 1, 3.0f);
        if (g_game.isVSL && g_vsl.active) {
            snprintf(tb, sizeof(tb), "VSL frame %d", g_game.bgaFrame);
            Font_DrawStringCenteredScaled(321, 267, tb, 0, 0, 0, 1, 1.5f);
            Font_DrawStringCenteredScaled(320, 266, tb, 1, 1, 1, 1, 1.5f);
        }
        { /* DIAG: PUMPY_DUMP_DIR + PUMPY_DUMP_AT (s) -> salva o framebuffer em BMP */
            static int dumped = 0; const char* dd = getenv("PUMPY_DUMP_DIR"); const char* dl = getenv("PUMPY_DUMP_AT");
            /* lista separada por virgulas; dumped = quantos ja foram salvos */
            char da[32] = {0};
            if (dl) { const char* q = dl; for (int k = 0; k < dumped && q; k++) { q = strchr(q, ','); if (q) q++; }
                      if (q) { size_t n = strcspn(q, ","); if (n > 31) n = 31; memcpy(da, q, n); } }
            if (dd && da[0] && t >= atof(da)) {
                GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp); int W = vp[2], H = vp[3], rs = (W * 3 + 3) & ~3;
                unsigned char* px = (unsigned char*)malloc((size_t)rs * H);
                if (px) { glPixelStorei(GL_PACK_ALIGNMENT, 4); glReadPixels(vp[0], vp[1], W, H, GL_BGR_EXT, GL_UNSIGNED_BYTE, px);
                    char fp[512]; snprintf(fp, sizeof(fp), "%s/dump_%s.bmp", dd, da); FILE* bf = fopen(fp, "wb");
                    if (bf) { unsigned int fs = 54 + rs * H; unsigned char hd[54] = {'B','M'}; memcpy(hd+2,&fs,4); hd[10]=54; hd[14]=40;
                        memcpy(hd+18,&W,4); memcpy(hd+22,&H,4); hd[26]=1; hd[28]=24; fwrite(hd,1,54,bf); fwrite(px,1,(size_t)rs*H,bf); fclose(bf); }
                    free(px); Log_Print("DIAG: dump %s\n", fp); }
                dumped++;
            }
        }
        glPopAttrib();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    /* Demo: 0x40CA41 — Color3f(1, 1, 0.2), tile 0x15 do arrow542.sp2 em
     * Translate(120, 20) e +400. Coordenadas do SP2 em Y-UP local. */
    if (g_exDemo && g_fontArrow542 >= 0 && g_fontArrow542 + 0x15 < g_game.sprTileCount) {
        SPRTileDef* dt2 = &g_game.sprTiles[g_fontArrow542 + 0x15];
        if (dt2->texId >= 0) {
            int tw = Texture_GetWidth(dt2->texId);  if (tw <= 0) tw = 256;
            int th = Texture_GetHeight(dt2->texId); if (th <= 0) th = 256;
            for (int k = 0; k < 2; k++) {
                float xUp = 120.0f + 400.0f * (float)k + (float)dt2->srcX;
                float yTopDown = 480.0f - (20.0f + (float)dt2->srcY + (float)dt2->srcH);
                Texture_DrawUV(dt2->texId, xUp, yTopDown, (float)dt2->srcW, (float)dt2->srcH,
                               dt2->u1 * tw, dt2->v1 * th, dt2->u2 * tw, dt2->v2 * th,
                               1.0f, 1.0f, 0.2f, 1.0f);
            }
        }
    }
}


// Original combo rendering functions from PUMPY.EXE

// Combo_DrawMain — 0x00411b40 no PUMPY.EXE. Desenho principal do combo,
// com os casos especiais de 1000/2000/3000 (comparação entre players).
void Combo_DrawMain(int comboValue)
{
    // Bind the font texture (original uses DAT_0079e70c)
    Texture_Bind(g_fontTexId);
    
    // Special cases for combo comparison sprites (1000, 2000, 3000)
    if (comboValue == 1000) {
        // Player 1 has higher combo - show "COMBO" sprite
        glTranslatef(0x43800000, 0, 0);  // X position from original
        Combo_DrawSprite(0);  // Special sprite type 0
        return;
    }
    
    if (comboValue == 2000) {
        // Both players have same combo - show "MAX COMBO" sprite  
        glTranslatef(0x43800000, 0, 0);  // X position from original
        Combo_DrawSprite(1);  // Special sprite type 1
        return;
    }
    
    if (comboValue == 3000) {
        // Player 2 has higher combo - show "COMBO" sprite
        glTranslatef(0x43800000, 0, 0);  // X position from original
        Combo_DrawSprite(2);  // Special sprite type 2
        return;
    }
    
    // Regular combo numbers - break down into digits
    int digitPos = 3;  // Start with 3 digits (hundreds place)
    do {
        int digit = comboValue % 10;  // Get the rightmost digit
        Combo_DrawDigit(digit);         // Render the digit
        glTranslatef(0xc2080000, 0, 0);  // Move left for next digit (from original)
        digitPos--;
        comboValue = comboValue / 10;  // Remove the rightmost digit
    } while (digitPos > 0);
}

// Combo_DrawSprite — 0x00411a90 no PUMPY.EXE. Sprites "COMBO" / "MAX COMBO".
void Combo_DrawSprite(int spriteType)
{
    float u1, u2;
    
    if (spriteType == 0) {
        // COMBO sprite (Player 1 higher)
        u1 = 0x3f200000;  // 0.125f
        u2 = 0x3f480000;  // 0.28125f
    } 
    else if (spriteType == 1) {
        // MAX COMBO sprite (Both players equal)
        u1 = 0x3f480000;  // 0.28125f
        u2 = 0x3f700000;  // 0.4375f
    } 
    else if (spriteType == 2) {
        // COMBO sprite (Player 2 higher)
        u1 = 0x3f480000;  // 0.28125f
        u2 = 0x3f480000;  // 0.28125f + dynamic width
        u2 = 0.78125f - g_game.sprTiles[g_fontArrow542 + 12].srcW / 256.0f;
    }
    else {
        u2 = g_game.sprTiles[g_fontArrow542 + 12].srcW / 256.0f;
        if (spriteType == 2) {
            u1 = 0x3f480000;  // 0.28125f
            u2 = 0.78125f - g_game.sprTiles[g_fontArrow542 + 12].srcW / 256.0f;
        }
    }
    
    glBegin(GL_QUADS);
    glTexCoord2f(u1, 0x3f530000);  // V = 0.328125f (top)
    glVertex2i(0, 0x30);           // Y = 48 (bottom)
    glTexCoord2f(u1, 0x3f818000);  // V = 0.5078125f (bottom)
    glVertex2i(0, 0);              // Y = 0 (top)
    glTexCoord2f(u2, 0x3f818000);  // V = 0.5078125f (bottom)
    glVertex2i(0x28, 0);          // Y = 0 (top)
    glTexCoord2f(u2, 0x3f530000);  // V = 0.328125f (top)
    glVertex2i(0x28, 0x30);       // Y = 48 (bottom)
    glEnd();
}

// Combo_DrawDigit — 0x004119d0 no PUMPY.EXE. Um dígito, via grid 6x4 da font.
void Combo_DrawDigit(int digit)
{
    // Bind the dec00 texture (same as original)
    Texture_Bind(g_fontDec00Id);
    
    // Original texture coordinates (5 colunas)
    float tileWidth = 0.1875f;    // 48/256
    float tileHeight = 0.203125f; // 52/256  
    float vOffset = 0.15625f;     // 40/256
    
    float u1 = (float)(digit % 5) * tileWidth;
    float v1 = (float)(digit / 5) * tileHeight + vOffset;
    float u2 = u1 + tileWidth;
    float v2 = v1 + tileHeight;
    
    glBegin(GL_QUADS);
    glTexCoord2f(u1, v1);
    glVertex2i(0, 0x2d);           // Y = 45
    glTexCoord2f(u1, v2);
    glVertex2i(0, 0);              // Y = 0
    glTexCoord2f(u2, v2);
    glVertex2i(0x2c, 0);          // Y = 0
    glTexCoord2f(u2, v1);
    glVertex2i(0x2c, 0x2d);       // Y = 45
    glEnd();
}

//force
