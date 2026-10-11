#ifndef SONG_H
#define SONG_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if !defined(_WIN32)
#include <strings.h>
#ifndef _stricmp
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#endif
#endif

/* era: 256. A tabela foi a 250 com o USE OLD DATA SONGS e as missões do WORLD TOUR / TRAINING
 * entram no songDB em tempo de jogo (Select_StartMission): sobravam só 6 vagas. */
#define MAX_SONGS 512          /* NX: 193 músicas + missões do WORLD TOUR (era 160; truncava a lista da NX) */
#define MAX_MODES 16
#define MAX_SONGS_PER_MODE 512 /* era 160 */

typedef struct {
    int id;
    float bpm;
    char title[64];
    bool hasChart;
    int stxChartCount;
} SongEntry;

typedef struct {
    char name[32];
    int songCount;
    int songIds[MAX_SONGS_PER_MODE];
    int difficulties[MAX_SONGS_PER_MODE];
} SongMode;

typedef struct {
    int songCount;
    SongEntry songs[MAX_SONGS];
    int modeCount;
    SongMode modes[MAX_MODES];
} SongDB;

bool Song_LoadDatabase(const char* path, SongDB* db);
int Song_FindByID(const SongDB* db, int id);
int Song_FindMode(const SongDB* db, const char* name);
void Song_PrintDB(const SongDB* db);

#endif
