#ifndef IR_PASSWORD_H
#define IR_PASSWORD_H

#include <stdint.h>

/* Internet Ranking do Exceed (exceed.exe 0x421358..0x422420).
 * Senha "XXXX-XXXX-XXXX-XXXX": 16 caracteres x 5 bits = 80 bits =
 * score(32) + 4 cursos(4x7) + checksum(12) + mixpattern(8). */

#define IR_MIX_ROWS 64
#define IR_MIX_COLS 72
#define IR_CODE_LEN 19   /* sem o '\0' */

extern const unsigned char g_irMixTable[IR_MIX_ROWS][IR_MIX_COLS];

typedef struct {
    uint32_t score;
    uint8_t  course[4];   /* índice em g_exSongs (7 bits) */
    uint16_t checksum;    /* 12 bits */
} IrData;

/* 0x42229C IR_Encode(name, score, c1..c4). O nome é recebido mas NÃO entra no
 * checksum no exe (no src de 2003-12-29 entrava): checksum = (score + c1..c4) & 0xFFF. */
uint16_t IR_Checksum(uint32_t score, const uint8_t course[4]);
void     IR_Encode(char out[IR_CODE_LEN + 1], uint32_t score, const uint8_t course[4]);
/* 0x421C00 Decode: 0 = ok, -1 = mixpattern inválido, -2 = tamanho inválido */
int      IR_Decode(IrData* d, const char* code);

#endif
