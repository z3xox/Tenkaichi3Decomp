/*
 * Browser build: what becomes of the content added from outside the disc (stages, songs, texture packs).
 *
 * The native build finds it by looking through folders: plat_extras.c (stages/, songs/: stat and opendir) and
 * gs/gs_texpack.c (textures/: SDL_GetPathInfo, SDL_EnumerateDirectory). In the browser the data folder is on the
 * HTTP server; web_fs.c answers those calls under the data root from the server's folder listings, when the server
 * gives them (port/web/devserver.py does: <folder>/?list). Natively "no folder" means "nothing added", and nothing
 * is said; that holds here too when the server lists its folders. When it does not, "nothing found" would hide that
 * the feature cannot work, so it is said. The three start-up calls are linked wrapped (wasm_build.py:
 * --wrap=PortStages_Init,PortSongs_Init,TexPack_Init): each runs as always, and then the reason, if any, is said
 * once. Stages named through BT3_EXTRA_STAGES (plat_stages.c, no folder needed) count as found. The page reads the
 * same state (web_ui.c: added_checked, added_stage_count, added_song_count, texture_pack_count, data_listable).
 */
#include <stdio.h>

void __real_PortStages_Init(void);
void __real_PortSongs_Init(void);
void __real_TexPack_Init(void);
int PortStages_Count(void);
int PortSongs_Count(void);
int TexPack_Count(void);
int WebFs_CanList(void); /* web_fs.c */

enum { CHECKED_STAGES = 1, CHECKED_SONGS = 2, CHECKED_TEXTURES = 4 };
static int sChecked;

static const char kNoListing[] = "not available: the data server does not list its folders, so files added to the "
                                 "data folder cannot be found (port/web/devserver.py lists them)";

/* After the call for `bit`: when nothing was found and the reason is not "none added", says it once. */
static void checked(int bit, int count, const char *what, const char *why) {
    if (sChecked & bit) {
        return;
    }
    sChecked |= bit;
    if (count == 0 && why != NULL) {
        fprintf(stderr, "bt3: %s: %s\n", what, why);
    }
}

void __wrap_PortStages_Init(void) {
    __real_PortStages_Init();
    checked(CHECKED_STAGES, PortStages_Count(), "added stages", WebFs_CanList() ? NULL : kNoListing);
}

void __wrap_PortSongs_Init(void) {
    __real_PortSongs_Init();
    checked(CHECKED_SONGS, PortSongs_Count(), "added songs", WebFs_CanList() ? NULL : kNoListing);
}

void __wrap_TexPack_Init(void) {
    __real_TexPack_Init();
    /* (their folder: the data folder's "textures", which the page names: index.html, BT3_TEXTURES) */
    checked(CHECKED_TEXTURES, TexPack_Count(), "texture packs", WebFs_CanList() ? NULL : kNoListing);
}

/* Which of the three have been looked for (CHECKED_*): the page says "not looked for yet" before. */
int WebExtras_Checked(void) {
    return sChecked;
}
