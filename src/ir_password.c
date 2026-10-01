/* Internet Ranking — senha (exceed.exe 0x421358 Encode, 0x421C00 Decode,
 * 0x42229C IR_Encode). Port literal do ir_password.cpp do src do Exceed
 * (snapshot 2003-12-29), conferido contra o binário:
 *   - mixtable 0x45C1A0 idêntica (ver tools/gen_ir_mixtable.py);
 *   - alfabetos 0x449F04 / 0x449EE0 (encode) e 0x449F68 (decode);
 *   - mixpattern = rand() & 0x3F (0x421362..0x421373), máscaras XOR iguais;
 *   - checksum SEM o nome (0x4222C7..0x4222E5), diferente do src. */
#include <stdlib.h>
#include <string.h>
#include "ir_password.h"

/* posições onde os 8 bits do mixpattern são inseridos (MSB primeiro) */
static const int k_mixPos[8] = { 10, 24, 33, 36, 49, 56, 62, 75 };

static unsigned char xorMask(unsigned char mixpattern)
{
    static const unsigned char k_mask[8] = { 0x0F, 0xF0, 0xAA, 0x33, 0xCC, 0x66, 0x99, 0x55 };
    return k_mask[mixpattern % 8];
}

uint16_t IR_Checksum(uint32_t score, const uint8_t course[4])
{
    return (uint16_t)((score + course[0] + course[1] + course[2] + course[3]) & 0xFFF);
}

void IR_Encode(char out[IR_CODE_LEN + 1], uint32_t score, const uint8_t course[4])
{
    static const char k_chars1[] = "0123TMSPBCLRAKDFIHGJ687O54EQV9UN";
    static const char k_chars2[] = "WXYZTMSPBCLRAKDFIHGJ687O54EQV9UN";
    unsigned char code[9], bits[80], bits2[80];
    uint16_t chk = IR_Checksum(score, course);
    unsigned char mixpattern = (unsigned char)(rand() & 0x3F);
    int i;

    memset(bits, 0, sizeof(bits));
    memset(bits2, 0, sizeof(bits2));

    /* empacota: score LE, 4 cursos, bits 8..11 do checksum no MSB dos cursos */
    memcpy(&code[0], &score, 4);
    for (i = 0; i < 4; i++) {
        code[4 + i] = course[i];
        if (chk & (0x100 << i)) code[4 + i] |= 0x80;
    }
    code[8] = (unsigned char)(chk & 0xFF);

    unsigned char mask = xorMask(mixpattern);
    for (i = 0; i < 9; i++) code[i] ^= mask;

    for (i = 0; i < 72; i++)
        bits[i] = (code[i / 8] >> (7 - (i % 8))) & 1;

    for (i = 0; i < 72; i++)
        bits2[i] = bits[g_irMixTable[mixpattern][i]];

    for (int k = 0; k < 8; k++) {
        int p = k_mixPos[k];
        for (i = 78; i >= p; i--) bits2[i + 1] = bits2[i];
        bits2[p] = (mixpattern >> (7 - k)) & 1;
    }

    int c = 0;
    for (i = 0; i < 80; i += 5) {
        unsigned char j = (unsigned char)((bits2[i] << 4) | (bits2[i + 1] << 3) |
                                          (bits2[i + 2] << 2) | (bits2[i + 3] << 1) | bits2[i + 4]);
        out[c++] = (rand() % 2) ? k_chars1[j] : k_chars2[j];
        if ((c % 5) == 4 && c < IR_CODE_LEN) out[c++] = '-';
    }
    out[IR_CODE_LEN] = '\0';
}

int IR_Decode(IrData* d, const char* code)
{
    static const char k_chars[] = "0123TMSPBCLRAKDFIHGJ687O54EQV9UNWXYZ";
    unsigned char codebin[9], bits[80], bits2[80];
    unsigned char mixpattern = 0;
    int i, j = 0;

    memset(codebin, 0, sizeof(codebin));
    memset(bits, 0, sizeof(bits));
    memset(bits2, 0, sizeof(bits2));

    for (i = 0; i < IR_CODE_LEN && code[i]; i++) {
        const char* p = strchr(k_chars, code[i]);
        if (!p || !*p) continue;          /* '-' e caracteres fora do alfabeto */
        int v = (int)(p - k_chars);
        if (v >= 32) v -= 32;
        if (j > 75) return -2;
        for (int b = 4; b >= 0; b--) bits[j++] = (v >> b) & 1;
    }
    if (j != 80) return -2;

    for (int k = 0; k < 8; k++) mixpattern |= (unsigned char)(bits[k_mixPos[k]] << (7 - k));
    for (int k = 0; k < 8; k++)
        for (i = k_mixPos[k] - k; i < 79; i++) bits[i] = bits[i + 1];
    if (mixpattern >= 64) return -1;

    for (i = 0; i < 72; i++) bits2[g_irMixTable[mixpattern][i]] = bits[i];
    for (i = 0; i < 72; i++) codebin[i / 8] |= (unsigned char)(bits2[i] << (7 - (i % 8)));

    unsigned char mask = xorMask(mixpattern);
    for (i = 0; i < 9; i++) codebin[i] ^= mask;

    memcpy(&d->score, &codebin[0], 4);
    d->checksum = codebin[8];
    for (i = 0; i < 4; i++) {
        d->checksum |= (uint16_t)((codebin[4 + i] & 0x80) << (1 + i));
        d->course[i] = codebin[4 + i] & 0x7F;
    }
    return 0;
}
