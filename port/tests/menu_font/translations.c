/* Exercise the real catalog loader without a disc, renderer, or running game.
 * Build and run through port/tools/check_translations.py --runtime <this executable>.
 * Only disc extraction and font selection are omitted; parsing, discovery, copying,
 * UTF-8 decoding, English fallback, and online format validation are production code. */
#include "menu_translation.h"
#include "ui_text_extract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void UiText_ExtractOnStartup(const char *dir) { (void)dir; }
int MenuText_FirstFontId(int kind) { (void)kind; return 1; }
int Port_NetSession(void) { return 0; }

#define CHECK(test, message) do { if (!(test)) { \
    fprintf(stderr, "%s: %s\n", argv[2], message); return 1; \
} } while (0)

int main(int argc, char **argv) {
    static const char *const locales[] = {"en", "es", "fr", "de", "it", "ca"};
    static const char *const names[] = {"English", "Español", "Français", "Deutsch", "Italiano", "Català"};
    MenuTranslationOption options[16];
    const char *line;
    char why[256];
    int i, count;
    if (argc != 7) return 2;
    setenv("BT3_SETTINGS", "", 1);
    setenv("BT3_MENU_LANG", argv[2], 1);
    count = MenuTranslation_List(argv[1], options, 16);
    CHECK(count == 6, "expected English and five bundled languages");
    for (i = 0; i < count; i++) {
        CHECK(strcmp(options[i].locale, locales[i]) == 0, "language missing, duplicated, or out of order");
        CHECK(strcmp(options[i].name, names[i]) == 0, "UTF-8 language name differs");
    }
    CHECK(MenuTranslation_Load(argv[1], why, sizeof(why)), why);
    CHECK(strcmp(MenuTranslation_Locale(), argv[2]) == 0, "selected locale fell back to English");
    CHECK(strcmp(MenuTranslation_Text("main-6/0/0"), argv[3]) == 0, "main menu translation differs");
    CHECK(strcmp(MenuTranslation_Original("main-6/0/0"), "Dragon History") == 0, "original label changed");
    CHECK(strcmp(MenuTranslation_PortText(PORT_TEXT_QUIT_CONFIRM), argv[4]) == 0, "quit dialog/newline differs");
    CHECK(strcmp(MenuTranslation_Line("Quick Match"), argv[5]) == 0, "online menu translation differs");
    line = MenuTranslation_Line("Match %d: %s vs %s");
    CHECK(argv[6][0] ? line && strcmp(line, argv[6]) == 0 : line == NULL,
          "online format was rejected or an invalid translation was accepted");
    CHECK(MenuTranslation_Line("a line with no translation") == NULL, "missing text did not use English fallback");
    puts("catalog discovery, load, UTF-8, online text, and fallback: OK");
    return 0;
}
