/* dog_serve.c — extra do port: "Pumpy.exe --dog-serve", usado pelo OldDataWizard (tools/old_data_wizard.py).
 *
 * Modo SEM janela, sem log e sem abrir nenhum arquivo do jogo: o executável responde, por stdin/stdout,
 * consultas da tabela do MicroDog que ele já traz compilada (dog_table.c). Assim o assistente não precisa
 * de nenhum arquivo de chave: a decifragem dos .AUD/.PNZ do NX e do Zero e a gravação do MOV3 usam as
 * respostas que estão dentro do Pumpy.exe.
 *
 * Protocolo (texto, uma linha por comando, resposta numa linha; tudo em hexadecimal maiúsculo):
 *   HELLO         ->  PUMPY-DOG 1 <quantidade de consultas, decimal>
 *   Q <32 hex>    ->  <8 hex da resposta>   ou   -  (consulta fora da tabela)
 *   N <i>         ->  <32 hex da i-ésima consulta>   ou   -  (i fora da faixa)
 *   QUIT          ->  (encerra)
 * Fim do stdin também encerra. Comando desconhecido responde "?".
 *
 * Windows: ReadFile/WriteFile direto nos handles padrão (o .exe é SUBSYSTEM:WINDOWS, sem console).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int Dog_Convert16(const uint8_t* q, uint32_t* resp);   /* dog_table.c */
int Dog_Count(void);
int Dog_QueryAt(int i, uint8_t* q);

/* lê uma linha (sem o fim de linha) em buf; devolve o tamanho, ou -1 no fim da entrada */
static int readLine(char* buf, int cap) {
    int n = 0;
    for (;;) {
        char c;
#ifdef _WIN32
        DWORD got = 0;
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        if (!ReadFile(h, &c, 1, &got, NULL) || got == 0) return n ? n : -1;
#else
        if (read(0, &c, 1) != 1) return n ? n : -1;
#endif
        if (c == '\n') break;
        if (c == '\r') continue;
        if (n < cap - 1) buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

static void writeLine(const char* s) {
    char out[96];
    int n = snprintf(out, sizeof(out), "%s\n", s);
#ifdef _WIN32
    DWORD put = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out, (DWORD)n, &put, NULL);
#else
    if (write(1, out, (size_t)n) < 0) { /* sem para onde responder */ }
#endif
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int DogServe_Run(void) {
    char line[128];
    for (;;) {
        int n = readLine(line, (int)sizeof(line));
        if (n < 0 || strcmp(line, "QUIT") == 0) return 0;
        char out[96];
        if (strcmp(line, "HELLO") == 0) {
            snprintf(out, sizeof(out), "PUMPY-DOG 1 %d", Dog_Count());
            writeLine(out);
        } else if (line[0] == 'Q' && line[1] == ' ' && strlen(line + 2) == 32) {
            uint8_t q[16];
            int ok = 1;
            for (int i = 0; i < 16 && ok; i++) {
                int a = hexVal(line[2 + i * 2]), b = hexVal(line[3 + i * 2]);
                if (a < 0 || b < 0) ok = 0; else q[i] = (uint8_t)(a * 16 + b);
            }
            uint32_t r = 0;
            if (!ok) writeLine("?");
            else if (Dog_Convert16(q, &r)) { snprintf(out, sizeof(out), "%08X", (unsigned)r); writeLine(out); }
            else writeLine("-");
        } else if (line[0] == 'N' && line[1] == ' ') {
            uint8_t q[16];
            if (Dog_QueryAt(atoi(line + 2), q)) {
                for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02X", q[i]);
                writeLine(out);
            } else {
                writeLine("-");
            }
        } else {
            writeLine("?");
        }
    }
}
