#ifndef MOVIE_H
#define MOVIE_H

#include <stdbool.h>

/* Player dos .MOV (MOV2) do Exceed — ver src/movie.c */
bool Movie_Open(const char* path, bool loop);
void Movie_Close(void);
bool Movie_IsOpen(void);
bool Movie_HasEnded(void);
int  Movie_GetDecoded(void);   /* [+0x30]: quadros decodificados */
void Movie_Update(float dt);
void Movie_Render(void);
void Movie_RenderRect(float x0, float y0, float x1, float y1, float c, float alpha); /* Zero: prévia da Select */

void Movie_Select(int slot);    /* NX: 0 = padrão, 1 = segunda instância (prévia da Select) */
unsigned Movie_GLTexture(void); /* textura GL do quadro atual (0 sem quadro) */
bool Movie_Preload(void);       /* extra do port: vídeo inteiro em memória */
void Movie_Prime(void);         /* extra do port: 1º quadro pronto antes do gameplay */
#endif
