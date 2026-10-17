/* Italian (and later Multi5) language overlay for the USA port. Built from a Europe disc by
   port/tools/map_eu_lang.py into gamedata/lang/it/. */
#ifndef PORT_PLAT_LANG_H
#define PORT_PLAT_LANG_H

#ifdef __cplusplus
extern "C" {
#endif

/* game_lang (on-screen text): 0 English, 1 Japanese menus (unused with IT pack), 2 Italian. */
enum { PORT_LANG_EN = 0, PORT_LANG_JA = 1, PORT_LANG_IT = 2 };

/* voice_lang (spoken lines only): 0 English, 1 Japanese. Independent of menu/dialogue text. */
enum { PORT_VOICE_EN = 0, PORT_VOICE_JA = 1 };

void PortLang_Init(void);                 /* load overlay + setting (call once at start) */
int PortLang_Get(void);                   /* current on-screen text language */
void PortLang_Set(int lang);              /* save + apply text language; clamps to available packs */
int PortLang_GetVoice(void);              /* 0 English, 1 Japanese battle/story voices */
void PortLang_SetVoice(int voice);        /* En/Ja voices only; does not touch menu archives */
int PortLang_HasItalian(void);            /* 1 if gamedata/lang/it/READY is present */
const char *PortLang_Alias(const char *rel); /* host path when an overlay serves `rel`, else NULL */
void PortLang_SyncSaveVoice(void);        /* push En/Ja into gSaveData->flags from voice_lang */

#ifdef __cplusplus
}
#endif

#endif
