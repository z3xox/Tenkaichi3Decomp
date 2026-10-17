/* F1 settings strings: English default, Italian when game text language is Italian.
   Voices stay En/Ja — there is no Italian dub for this game. */
#ifndef PORT_UI_I18N_H
#define PORT_UI_I18N_H

#include "../../include/plat_lang.h"

static inline int Ui_IsItalian(void) {
    return PortLang_Get() == PORT_LANG_IT;
}

/* Pick Italian copy when Menu & dialogue text is Italian. */
static inline const char *Tr(const char *en, const char *it) {
    return Ui_IsItalian() ? it : en;
}

#endif
