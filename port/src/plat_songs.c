/* The manifest of songs added to the music select (see plat_songs.h). Runs at start-up: it gives the game the
   added tracks' list offsets and names, and the file layer where their files live. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "plat_songs.h"

char gPortSongNames[PORT_SONG_MAX][64];
int gPortSongCount;
int gPortSongOffsets[PORT_SONG_MAX];

typedef struct SongAlias {
    char rel[48];     /* the path the game asks for, under the data root */
    char target[192]; /* where it is served from */
} SongAlias;

static SongAlias sAliases[PORT_SONG_MAX];
static int sAliasCount;
static int sDone;

static const char *data_root(void) {
    const char *r = getenv("BT3_DATA");
    return r != NULL ? r : "gamedata";
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

static char sDir[256]; /* the folder the songs are in (plat_extras.c) */
static char sFiles[PORT_SONG_MAX][128];

extern int PortExtras_Dir(const char *kind, const char *env, char *out, unsigned size);
extern int PortExtras_Scan(const char *dir, const char *ext, char names[][128], int max);
extern void PortExtras_NameFromFile(const char *file, char *name, unsigned size);

void PortSongs_Init(void);

/* The order the files it does not mention follow: by name without regard to case, as PortExtras_Scan sorts them. */
static int song_cmp(const void *a, const void *b) {
    const unsigned char *x = a, *y = b;
    for (; *x != '\0' && tolower(*x) == tolower(*y); x++, y++) {
    }
    return tolower(*x) != tolower(*y) ? tolower(*x) - tolower(*y) : strcmp((const char *)a, (const char *)b);
}

const char *PortSongs_Dir(void) {
    PortSongs_Init(); /* (the overlay may ask before the game has started) */
    return sDir;
}

/* Adds one song: its place in the music list, its name, and the file the game's id for it stands for. */
static void add_song(const char *file, const char *name) {
    int i, offset, index;

    for (i = 0; i < gPortSongCount; i++) {
        if (strcmp(sFiles[i], file) == 0) {
            return;
        }
    }
    if (gPortSongCount >= PORT_SONG_MAX) {
        fprintf(stderr, "bt3: songs: more than %d added, %s is left out\n", PORT_SONG_MAX, file);
        return;
    }
    if (strlen(file) >= sizeof(sFiles[0]) || strlen(sDir) + 1 + strlen(file) >= sizeof(sAliases[0].target)) {
        /* (cut short, the name was that of no file: the song was in the list and silent) */
        fprintf(stderr, "bt3: songs: %s is left out: the folder and the name together are too long\n", file);
        return;
    }
    i = gPortSongCount;
    snprintf(sFiles[i], sizeof(sFiles[i]), "%s", file);
    offset = PORT_SONG_FIRST_OFFSET + i;
    index = PORT_SONG_FILE_INDEX + i; /* PORT_BGM_FILE(offset), as an index of the second archive */
    gPortSongOffsets[i] = offset;
    snprintf(gPortSongNames[i], 64, "%s", name);
    snprintf(sAliases[sAliasCount].rel, sizeof(sAliases[sAliasCount].rel), "pzs3us2/%05d.bin", index);
    snprintf(sAliases[sAliasCount].target, sizeof(sAliases[sAliasCount].target), "%s/%s", sDir, file);
    sAliasCount++;
    gPortSongCount++;
}

/* The ADX that earlier releases wrote next to an MP3 they converted (the mark "BT3:MP3" 1 at 0x30 of its header):
   the MP3 beside it is now played as it is, and the copy would be the same song in the list a second time. 1 when
   `file` is such a copy and its MP3 is still there (`mp3` gets the MP3's name, any case of ".mp3", as the converter
   took it); a copy without its MP3 is a song like any other ADX, and an ADX made by hand or by ffmpeg has no mark.
   The file is left where it is. */
static int converted_copy(const char *file, char *mp3, unsigned size) {
    static const char kMark[8] = {'B', 'T', '3', ':', 'M', 'P', '3', 1};
    char path[512];
    unsigned char h[0x38];
    size_t l = strlen(file);
    FILE *fp;
    int c, got;

    if (l <= 4 || l + 1 > size || file[l - 4] != '.' || tolower((unsigned char)file[l - 3]) != 'a' || tolower((unsigned char)file[l - 2]) != 'd' ||
        tolower((unsigned char)file[l - 1]) != 'x') {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s", sDir, file);
    if ((fp = fopen(path, "rb")) == NULL) {
        return 0;
    }
    got = fread(h, 1, sizeof(h), fp) == sizeof(h);
    fclose(fp);
    if (!got || h[0] != 0x80 || h[1] != 0 || memcmp(h + 0x30, kMark, sizeof(kMark)) != 0) {
        return 0;
    }
    for (c = 0; c < 4; c++) { /* (.mp3 .mP3 .Mp3 .MP3: on Linux the disk tells them apart) */
        snprintf(mp3, size, "%.*s.%c%c3", (int)(l - 4), file, c & 2 ? 'M' : 'm', c & 1 ? 'P' : 'p');
        snprintf(path, sizeof(path), "%s/%s", sDir, mp3);
        if ((fp = fopen(path, "rb")) != NULL) {
            fclose(fp);
            return 1;
        }
    }
    return 0;
}

/* Reads the folder and adds the songs that are not in the list yet, at its end: a song keeps its place (the game
   holds the place's number while a fight with that music is set up and runs). */
static void collect(void) {
    char path[512];
    char line[256];
    FILE *fp;

    /* the list first (its names and its order), then whatever else is in the folder, by name */
    snprintf(path, sizeof(path), "%s/songs.txt", sDir);
    fp = fopen(path, "rb");
    while (fp != NULL && fgets(line, sizeof(line), fp) != NULL) {
        char *bar;
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        bar = strchr(line, '|');
        if (bar == NULL) {
            continue;
        }
        *bar = '\0';
        /* The line may name an ADX, an mp3, a wav or a flac: the file it names is the one played (gs/snd_ma.c for
           the last three), the MP3 for a converted copy of one (a list written for the converter's ADX). */
        {
            char *file = trim(line), mp3[128];
            add_song(converted_copy(file, mp3, sizeof(mp3)) ? mp3 : file, trim(bar + 1));
        }
    }
    if (fp != NULL) {
        fclose(fp);
    }
    {
        /* Everything else in the folder, of any of the formats the port plays, by name. The four scans' names are
           put together and sorted once, so the order does not depend on the extension. */
        static const char *exts[] = { ".adx", ".mp3", ".wav", ".flac" };
        static char found[PORT_SONG_MAX * 4][128];
        static char one[64][128];
        int n = 0, e, k;

        for (e = 0; e < (int)(sizeof(exts) / sizeof(exts[0])); e++) {
            int m = PortExtras_Scan(sDir, exts[e], one, 64), j;
            for (j = 0; j < m && n < (int)(sizeof(found) / sizeof(found[0])); j++) {
                snprintf(found[n++], 128, "%s", one[j]);
            }
        }
        qsort(found, (size_t)n, 128, song_cmp);
        for (k = 0; k < n; k++) {
            char name[64], mp3[128];
            if (converted_copy(found[k], mp3, sizeof(mp3))) {
                continue; /* (its MP3 is in the list instead) */
            }
            PortExtras_NameFromFile(found[k], name, sizeof(name));
            add_song(found[k], name);
        }
    }
}

void PortSongs_Init(void) {
    if (sDone) {
        return;
    }
    sDone = 1;
    if (!PortExtras_Dir("songs", "BT3_SONGS", sDir, sizeof(sDir))) {
        return;
    }
    collect();
    if (gPortSongCount > 0) {
        fprintf(stderr, "bt3: songs: %d added from %s\n", gPortSongCount, sDir);
    }
}

/* Kept for the game's select screens (src/menu/menu_c_e.c, menu_e_b.c), which took in the songs a background
   conversion had finished. Nothing is converted any more: the folder is read once, at start, and a song in it is
   played as it is (gs/snd_ma.c), so the list does not change while the program runs. */
void PortSongs_Refresh(void) {
}

const char *PortSongs_Name(int index) {
    return index >= 0 && index < gPortSongCount ? gPortSongNames[index] : "";
}

int PortSongs_Count(void) {
    return gPortSongCount;
}

int PortSongs_Offset(int index) {
    if (index < 0 || index >= gPortSongCount) {
        return 0;
    }
    return gPortSongOffsets[index];
}

/* Whether `path` is the file of a song added from outside the disc (for the sound code: such a file is played
   round and round like the disc's music, though it has no loop points of its own). */
int PortSongs_IsFile(const char *path) {
    size_t pl = strlen(path), tl;
    int i;
    for (i = 0; i < sAliasCount; i++) {
        tl = strlen(sAliases[i].target);
        if (tl != 0 && tl <= pl && strcmp(path + pl - tl, sAliases[i].target) == 0) {
            return 1;
        }
    }
    return 0;
}

int PortSongs_Alias(const char *rel, char *out, unsigned n) {
    int i;
    for (i = 0; i < sAliasCount; i++) {
        if (strcmp(sAliases[i].rel, rel) == 0) {
            snprintf(out, n, "%s", sAliases[i].target);
            return 1;
        }
    }
    return 0;
}

/*
 * Online fights with this player's own music. A match computes with the music its menus chose (the host's or the
 * one who joins, whoever picked it), and that choice stays what both copies compute with: the stream's length the
 * game is told is the original disc's (snd_adx.c, view_start). What this copy HEARS during the fight can be a song
 * of this folder instead, as the other voice set is heard in its place (snd_adx.c, voice_own_path). Nothing of it
 * goes over the network and the other player hears their own choice.
 *
 *   net_music (bt3_settings.txt): 0 the match's music, as before (the default); 1 one of the songs checked here at
 *                                 random for each fight; 2 the checked songs one after the other.
 *   online.txt (this folder):     the checked songs, one file name per line, in the order of the list.
 *
 * The Dragon Net Battle window (gs/ui.cpp) and Tenkaichi Budokai Online's Profile (src/menu/port_tbo.c) show the
 * list and write both.
 */
static unsigned char sNetChosen[PORT_SONG_MAX];
static int sNetRead, sNetLast = -1, sBattleBgm;
static unsigned sNetSeed;

static void net_read(void) {
    char path[512], line[256];
    FILE *fp;
    int i;

    if (sNetRead) {
        return;
    }
    sNetRead = 1;
    PortSongs_Init();
    if (sDir[0] == '\0') {
        return;
    }
    snprintf(path, sizeof(path), "%s/online.txt", sDir);
    fp = fopen(path, "rb");
    while (fp != NULL && fgets(line, sizeof(line), fp) != NULL) {
        char *file = trim(line);
        for (i = 0; i < gPortSongCount && file[0] != '\0' && file[0] != '#'; i++) {
            if (strcmp(sFiles[i], file) == 0) {
                sNetChosen[i] = 1;
            }
        }
    }
    if (fp != NULL) {
        fclose(fp);
    }
}

int PortSongs_NetChosen(int index) {
    net_read();
    return index >= 0 && index < gPortSongCount && sNetChosen[index];
}

void PortSongs_NetChoose(int index, int on) {
    net_read();
    if (index >= 0 && index < gPortSongCount) {
        sNetChosen[index] = on != 0;
    }
}

/* Writes online.txt (the checked songs). A song that is not in the folder any more is left out of it. */
void PortSongs_NetSave(void) {
    char path[512];
    FILE *fp;
    int i;

    net_read();
    if (sDir[0] == '\0') {
        return;
    }
    snprintf(path, sizeof(path), "%s/online.txt", sDir);
    if ((fp = fopen(path, "wb")) == NULL) {
        fprintf(stderr, "bt3: songs: cannot write %s\n", path);
        return;
    }
    fprintf(fp, "# The songs played in your online fights (Dragon Net Battle > Your music in online fights).\n");
    for (i = 0; i < gPortSongCount; i++) {
        if (sNetChosen[i]) {
            fprintf(fp, "%s\n", sFiles[i]);
        }
    }
    fclose(fp);
}

int PortSongs_NetMode(void) {
    extern int Port_Setting(const char *name, int def); /* plat_settings.c */
    int m = Port_Setting("net_music", 0);
    return m >= 0 && m <= 2 ? m : 0;
}

/* The game's battle code (src/battle/battle_work.c) says when the fight's music is started: the one stream this
   replaces. The menus' music, the stage select's preview and every other stream are left alone. */
void Port_BattleBgm(int on) {
    sBattleBgm = on != 0;
}

int PortSongs_BattleBgm(void) {
    return sBattleBgm;
}

/* The file to play as this fight's music in an online match, or NULL for the match's own (net_music off, no song
   checked). `again`: the blank is only being run again (rollback), so the song picked the first time is kept and
   the stream plays on. A song of its own is picked with a generator of its own: the game's random numbers are part
   of what the two copies compute and are not touched. */
/* Whether song i's file can be opened (on Windows a name with characters outside the system's code page is read
   from the folder with '?' in their place, and no such file opens: that song is left out rather than leaving the
   fight without music). */
static int net_playable(int i) {
    FILE *fp = fopen(sAliases[i].target, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

const char *PortSongs_NetPath(int again) {
    int mode = PortSongs_NetMode(), list[PORT_SONG_MAX], n = 0, i, pick;

    if (mode == 0) {
        return NULL;
    }
    net_read();
    if (again && sNetLast >= 0 && sNetLast < gPortSongCount && sNetChosen[sNetLast]) {
        return sAliases[sNetLast].target;
    }
    for (i = 0; i < gPortSongCount; i++) {
        if (sNetChosen[i] && net_playable(i)) {
            list[n++] = i;
        }
    }
    if (n == 0) {
        return NULL;
    }
    if (mode == 2) {
        for (pick = 0; pick < n && list[pick] <= sNetLast; pick++) {
        }
        pick = pick < n ? list[pick] : list[0];
    } else {
        if (sNetSeed == 0) {
            sNetSeed = (unsigned)time(NULL) ^ (unsigned)clock() ^ 0x9E3779B9u;
        }
        do {
            sNetSeed ^= sNetSeed << 13;
            sNetSeed ^= sNetSeed >> 17;
            sNetSeed ^= sNetSeed << 5;
            pick = list[sNetSeed % (unsigned)n];
        } while (n > 1 && pick == sNetLast); /* (not the same song twice running) */
    }
    sNetLast = pick;
    fprintf(stderr, "bt3: songs: this online fight plays %s for this player\n", sFiles[pick]);
    return sAliases[pick].target;
}

/* BT3_TEST_BGM=<entry>: every battle is set up with that entry of the music list, whatever the menus chose
   (testing: 0x1A is the first added song). */
int Port_TestBgm(void) {
    const char *e = getenv("BT3_TEST_BGM");
    return e != NULL && e[0] != '\0' ? (int)strtol(e, NULL, 0) : -1;
}
