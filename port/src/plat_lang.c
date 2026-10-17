/*
 * Language overlay for the USA port: Italian menu/UI archives from a Europe disc.
 *
 * Locations (first READY wins):
 *   1) patcheu/          next to the game (drop-in pack from make_patcheu.py / make_patcheu.bat)
 *   2) gamedata/lang/it/ built by install.py --iso-eu
 *
 * game_lang selects on-screen text (English / Italian). voice_lang selects En/Ja spoken lines.
 * They are independent: Italian text can use Japanese voices.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_lang.h"

extern const char *Port_FileRoot(void); /* plat_file.c */
extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);

#define SAVE_FLAG_VOICE 1 /* include/sys/save.h: set = English-ish alt set 0xCC32, clear = 0x8D4E */

typedef struct AdxSaveView {
    unsigned char unk0[0x1608];
    int flags;
} AdxSaveView;

extern AdxSaveView *gSaveData;

#define MAX_MAP 512 /* menu band + story GSCF scripts + EU movies */
static struct {
    char rel[96];
    char path[512];
} sMap[MAX_MAP];
static int sMapN;
static int sLang = PORT_LANG_EN;
static int sVoice = PORT_VOICE_EN;
static int sHasIt;
static int sInited;
static char sPackRoot[512]; /* folder that holds READY + pzs3us1/… */

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* Parent of "gamedata" when the data root ends with that name; else NULL. */
static void parent_of_data(char *out, size_t n) {
    const char *root = Port_FileRoot();
    size_t len;
    const char *slash;

    snprintf(out, n, "%s", root);
    len = strlen(out);
    while (len > 0 && (out[len - 1] == '/' || out[len - 1] == '\\')) {
        out[--len] = '\0';
    }
    slash = strrchr(out, '/');
#ifdef _WIN32
    {
        const char *b = strrchr(out, '\\');
        if (b > slash) {
            slash = b;
        }
    }
#endif
    if (slash != NULL && (strcmp(slash + 1, "gamedata") == 0 || strcmp(slash + 1, "GAMEDATA") == 0)) {
        out[slash - out] = '\0';
        return;
    }
    out[0] = '\0';
}

static int try_pack_dir(const char *dir) {
    char ready[600];

    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
    snprintf(ready, sizeof(ready), "%s/READY", dir);
    if (!file_exists(ready)) {
        return 0;
    }
    snprintf(sPackRoot, sizeof(sPackRoot), "%s", dir);
    return 1;
}

static void load_manifest(void) {
    char path[600], line[700], rel[96], src[128], parent[512];
    FILE *fp;
    int bytes;

    sMapN = 0;
    sHasIt = 0;
    sPackRoot[0] = '\0';

    /* 1) drop-in pack beside the game; 2) install.py overlay under gamedata */
    parent_of_data(parent, sizeof(parent));
    if (!try_pack_dir("patcheu")) {
        if (parent[0] != '\0') {
            snprintf(path, sizeof(path), "%s/patcheu", parent);
            try_pack_dir(path);
        }
    }
    if (sPackRoot[0] == '\0') {
        snprintf(path, sizeof(path), "%s/lang/it", Port_FileRoot());
        try_pack_dir(path);
    }
    if (sPackRoot[0] == '\0') {
        return;
    }

    sHasIt = 1;
    snprintf(path, sizeof(path), "%s/manifest.txt", sPackRoot);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL && sMapN < MAX_MAP) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        bytes = 0;
        if (sscanf(line, "%95s %127s %d", rel, src, &bytes) < 2) {
            continue;
        }
        snprintf(sMap[sMapN].rel, sizeof(sMap[sMapN].rel), "%s", rel);
        snprintf(sMap[sMapN].path, sizeof(sMap[sMapN].path), "%s/%s", sPackRoot, rel);
        if (file_exists(sMap[sMapN].path)) {
            sMapN++;
        }
    }
    fclose(fp);
}

/* When the Italian overlay turns on/off, drop cached host files and forget menu archive pointers so the
   next screen reloads. Do NOT Heap_Free here: Hist/ModeMenu may still hold pointers into those buffers
   (use-after-free → crash on the next draw, e.g. Dragon History episode list). The orphaned blocks leak
   once per language toggle; Progress_Main frees arcs when leaving story/menus. */
static void flush_menu_arcs_if_overlay_changed(int was_it, int now_it) {
    extern void Port_FileCacheFlush(void); /* plat_fcache.c */
    extern void *gMenuArc0, *gMenuArc1, *gMenuArc2, *gMenuArc3, *gMenuArc4, *gMenuArc5;
    extern void *gMenuArc6, *gMenuArc7, *gMenuArc8, *gMenuArc9, *gMenuArc10, *gMenuArc11, *gMenuArc12;
    void **arcs[] = {
        &gMenuArc0, &gMenuArc1, &gMenuArc2, &gMenuArc3, &gMenuArc4, &gMenuArc5,
        &gMenuArc6, &gMenuArc7, &gMenuArc8, &gMenuArc9, &gMenuArc10, &gMenuArc11, &gMenuArc12,
    };
    int i;

    if (was_it == now_it) {
        return;
    }
    Port_FileCacheFlush();
    for (i = 0; i < (int)(sizeof(arcs) / sizeof(arcs[0])); i++) {
        *arcs[i] = NULL;
    }
}

void PortLang_Init(void) {
    int lang;
    int voice;

    if (sInited) {
        return;
    }
    sInited = 1;
    load_manifest();
    lang = Port_Setting("game_lang", PORT_LANG_EN);
    if (lang == PORT_LANG_IT && !sHasIt) {
        lang = PORT_LANG_EN;
    }
    /* Legacy: game_lang=JA meant English menus + Japanese voices. Keep text as English. */
    if (lang == PORT_LANG_JA) {
        lang = PORT_LANG_EN;
        if (Port_Setting("voice_lang", -1) < 0) {
            Port_SettingSave("voice_lang", PORT_VOICE_JA);
        }
    }
    if (lang != PORT_LANG_EN && lang != PORT_LANG_IT) {
        lang = PORT_LANG_EN;
    }
    sLang = lang;

    voice = Port_Setting("voice_lang", -1);
    if (voice < 0) {
        /* Migrate from old game_lang==JA, else English voices. */
        voice = PORT_VOICE_EN;
    }
    if (voice != PORT_VOICE_EN && voice != PORT_VOICE_JA) {
        voice = PORT_VOICE_EN;
    }
    sVoice = voice;
    PortLang_SyncSaveVoice();
}

int PortLang_Get(void) {
    if (!sInited) {
        PortLang_Init();
    }
    return sLang;
}

int PortLang_GetVoice(void) {
    if (!sInited) {
        PortLang_Init();
    }
    return sVoice;
}

int PortLang_HasItalian(void) {
    if (!sInited) {
        PortLang_Init();
    }
    return sHasIt;
}

void PortLang_Set(int lang) {
    int was_it;

    if (!sInited) {
        PortLang_Init();
    }
    if (lang == PORT_LANG_IT && !sHasIt) {
        lang = PORT_LANG_EN;
    }
    /* Text language is English or Italian only. Japanese is voices (PortLang_SetVoice). */
    if (lang == PORT_LANG_JA) {
        PortLang_SetVoice(PORT_VOICE_JA);
        lang = (sLang == PORT_LANG_IT) ? PORT_LANG_IT : PORT_LANG_EN;
    }
    if (lang != PORT_LANG_EN && lang != PORT_LANG_IT) {
        lang = PORT_LANG_EN;
    }
    was_it = (sLang == PORT_LANG_IT);
    sLang = lang;
    Port_SettingSave("game_lang", sLang);
    Port_SettingsWrite();
    flush_menu_arcs_if_overlay_changed(was_it, sLang == PORT_LANG_IT);
    PortLang_SyncSaveVoice();
}

void PortLang_SetVoice(int voice) {
    if (!sInited) {
        PortLang_Init();
    }
    if (voice != PORT_VOICE_EN && voice != PORT_VOICE_JA) {
        voice = PORT_VOICE_EN;
    }
    sVoice = voice;
    Port_SettingSave("voice_lang", sVoice);
    Port_SettingsWrite();
    PortLang_SyncSaveVoice();
}

const char *PortLang_Alias(const char *rel) {
    int i;

    if (!sInited) {
        PortLang_Init();
    }
    if (sLang != PORT_LANG_IT || rel == NULL) {
        return NULL;
    }
    for (i = 0; i < sMapN; i++) {
        if (strcmp(rel, sMap[i].rel) == 0) {
            return sMap[i].path;
        }
    }
    return NULL;
}

void PortLang_SyncSaveVoice(void) {
    if (gSaveData == NULL) {
        return;
    }
    /* SAVE_FLAG_VOICE set = English voice bank; clear = Japanese. */
    if (sVoice == PORT_VOICE_JA) {
        gSaveData->flags &= ~SAVE_FLAG_VOICE;
    } else {
        gSaveData->flags |= SAVE_FLAG_VOICE;
    }
}
