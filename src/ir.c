/* Exceed: CInternetRanking (ctor 0x413678, proc "IR").
 * Mostra a senha do Internet Ranking depois do estágio extra.
 *   Begin  0x41371B: BGA\IR.DAT + AUDIO\IR.AUD; ClearForNewStage soma o
 *                    score do último estágio ao total; IR_Encode(nome,
 *                    TOTAL_SCORE [player+4], cursos [0x568FFC..0x569008]).
 *                    1P tem prioridade (0x413817), igual ao src.
 *   Start  0x413961: frames 0..59 do BGA, texto cheio; 60 quadros (0x413A1B).
 *   Run    0x413A49: frame 60 + t % 60; fade nos últimos 30 quadros
 *                    (0x6EA = 1770), sai para GAMEOVER em 0x708 = 1800.
 *                    Centro do jogador ativo pula para 1770.
 *   Texto  0x41E9E0(x, y, 58, 64, passo 52): "XXXX-XXXX" em (85,125) e
 *          "-XXXX-XXXX" em (33,61) — y na base, espaço Y-UP como o highscore. */
#include "pumpy.h"
#include "bga.h"
#include "ir_password.h"

#define IR_BGA        0
#define IR_START_LEN  0x3C
#define IR_TIME_LIMIT 0x708

uint32_t g_exIrTotal[2];
uint8_t  g_exIrCourse[4];

static int  g_bfontTex = -1;
static int  g_t;
static int  g_phase;              /* 0 = Start, 1 = Run */
static int  g_player;             /* jogador cuja senha é mostrada */
static char g_code[IR_CODE_LEN + 1];

/* Chamado no fechamento de cada estágio (result.c). O exe soma em
 * ClearForNewStage; aqui a soma é feita no resultado — mesmo total. */
void IR_RecordStage(int stage, int songIndex, const uint32_t score[2])
{
    if (stage == 0) {
        memset(g_exIrTotal, 0, sizeof(g_exIrTotal));
        memset(g_exIrCourse, 0, sizeof(g_exIrCourse));
    }
    if (stage < 0 || stage > 3) return;
    g_exIrCourse[stage] = (uint8_t)songIndex;   /* (unsigned char)m_PlayOrder[i] */
    g_exIrTotal[0] += score[0];
    g_exIrTotal[1] += score[1];
}

/* 0x41E63C / 0x41E6EC: mesma fonte do highscore (bfont.tga, grade 8x8) */
static int glyphIndex(int ch)
{
    static const char k_set[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789~!@#$%^&*()_-+=\\:;/? ";
    for (int i = 0; k_set[i]; i++)
        if ((unsigned char)k_set[i] == (unsigned)ch) return i;
    return 0x3B;
}

static void drawText(float x, float y, float w, float h, float step, const char* s)
{
    if (g_bfontTex < 0 || !g_game.textures[g_bfontTex].inUse) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_game.textures[g_bfontTex].id);
    for (; *s; s++, x += step) {
        int idx = glyphIndex((unsigned char)*s);
        if (idx == 0x3B) continue;
        float u0 = (float)(idx % 8) * 0.125f, u1 = u0 + 0.125f;
        float v0 = (float)(idx / 8) * 0.125f, v1 = v0 + 0.125f;
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(x,     y + h);
        glTexCoord2f(u0, v1); glVertex2f(x,     y);
        glTexCoord2f(u1, v1); glVertex2f(x + w, y);
        glTexCoord2f(u1, v0); glVertex2f(x + w, y + h);
        glEnd();
    }
}

static void IR_Enter(void)
{
    char path[MAX_PATH];

    snprintf(path, sizeof(path), "%s/BGA/BFONT.DAT", g_game.currentDirectory);
    g_bfontTex = -1;
    if (RES_Open(path)) {
        g_bfontTex = loadTextureFromRES("bfont.tga");
        RES_Close();
    }
    if (g_bfontTex < 0) Log_Print("IR: bfont.tga do BFONT.DAT não carregou\n");

    BGM_Stop();
    snprintf(path, sizeof(path), "%s/AUDIO/IR.AUD", g_game.currentDirectory);
    if (BGM_LoadAUDDirect(path)) BGM_Play(true);

    g_player = (g_game.activePlayerMask & 1) ? 0 : 1;
    IR_Encode(g_code, g_exIrTotal[g_player], g_exIrCourse);
    Log_Print("IR: %dP senha %s (total %u, cursos %d %d %d %d)\n", g_player + 1, g_code,
              (unsigned)g_exIrTotal[g_player], g_exIrCourse[0], g_exIrCourse[1],
              g_exIrCourse[2], g_exIrCourse[3]);

    g_t = 0;
    g_phase = 0;
    BGA_SetColor(IR_BGA, 1.0f, 1.0f);
}

void IR_Update(float dt)
{
    (void)dt;
    if (g_game.stateFrame == 1) IR_Enter();

    if (g_phase == 0) {
        if (++g_t >= IR_START_LEN) { g_phase = 1; g_t = 0; }
        return;
    }
    if (Input_IsPadHit(g_player, PAD_C) && g_t < IR_TIME_LIMIT - 30)
        g_t = IR_TIME_LIMIT - 30;
    if (g_t > IR_TIME_LIMIT) {
        BGM_Stop();
        BGA_SetColor(IR_BGA, 1.0f, 1.0f);
        Resource_ClearBGA();
        Game_ChangeState(STATE_GAMEOVER_ENTER);
        return;
    }
    g_t++;
}

void IR_Render(void)
{
    char part1[10], part2[11];
    float fade = 1.0f;

    memcpy(part1, g_code, 9);      part1[9] = '\0';   /* strncpy(m_IR_Part1, pw, 9) */
    memcpy(part2, g_code + 9, 10); part2[10] = '\0';  /* strcpy(m_IR_Part2, &pw[9]) */

    if (g_phase == 0) {
        if (g_game.bgaPicCount > 0) BGA_Render(IR_BGA, g_t);
    } else {
        if (g_t > IR_TIME_LIMIT - 30) fade = (float)(IR_TIME_LIMIT - g_t) / 30.0f;
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;
        BGA_SetColor(IR_BGA, fade, 1.0f);
        if (g_game.bgaPicCount > 0) BGA_Render(IR_BGA, 60 + g_t % 60);
    }

    glColor4f(fade, fade, fade, 1.0f);
    drawText(85.0f, 125.0f, 58.0f, 64.0f, 52.0f, part1);
    drawText(85.0f - 58.0f + 6.0f, 125.0f - 64.0f, 58.0f, 64.0f, 52.0f, part2);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
