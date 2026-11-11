/*
 * Accessibility: the game's icon tags in its text, said as words.
 *
 * A 16-bit string of the game can carry tags that its printer draws as pictures (src/battle/col_c_b.c,
 * FontTag_Handle): "<PAD=names>" one or more controller buttons side by side, "<PADS=a,b,c,d>" one such list per
 * controller type, "<COL=RRGGBBAA>" / "<COL=DEF>" a colour, "<UB0>" a mark of the font. Read as they are, the
 * reader spelled them out. Here a button becomes the name of the button the player presses for it, as their own
 * controller calls it (gs/a11y_pad.c): Cross on a DualSense, A on an Xbox controller, the bound key without one; a
 * colour or a mark becomes nothing.
 *
 * An icon's name is the game's (its table gFontIconTables, of the disc's program; at most four characters, several
 * of them run together in one tag, the longest first, as FontIcon_Find takes them). Which button a name stands for
 * is not in the table, which only points at a picture: kIcons below holds names that say it themselves ("L1", a
 * circle sign, the romanised Japanese), which is a guess until the names of the disc have been seen. A name not
 * there is said as it is and written once to the reader's log (a11y_log=1) with all the table's names, to fill
 * kIcons from.
 *
 * It only reads the table; nothing of the game is written.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if (defined(__x86_64__) || defined(__aarch64__)) && defined(__clang__)
#undef __ptr32 /* the Windows headers of mingw define these two away */
#undef __uptr
#define GAME_PTR *__ptr32 __uptr
#else
#define GAME_PTR *
#endif
/* The game's layouts. On Apple arm64 only where its pointers are 4 bytes, as in a11y_game.c: ptr32.py's libclang 18
   parse there ignores __ptr32, and the compile after it checks them */
#if defined(__APPLE__) && defined(__aarch64__)
#define LAYOUT_ASSERT(c, m) _Static_assert(sizeof(void GAME_PTR) != 4 || (c), m)
#else
#define LAYOUT_ASSERT(c, m) _Static_assert(c, m)
#endif

extern const char *PortA11y_ActionName(int player, int action); /* gs/a11y_pad.c */
extern const char *PortA11y_TrSense(const char *sense, const char *label);
extern void PortA11y_Note(const char *text);

/* include/battle/col_c.h: FontIconName and FontIconEntry (a table ends with a NULL name). */
typedef struct {
    uint16_t chars[4];
    int len;
} A11yIconName;
typedef struct {
    A11yIconName GAME_PTR name;
    void GAME_PTR icon;
    int scale; /* (a float; not read) */
} A11yIconEntry;
LAYOUT_ASSERT(sizeof(A11yIconName) == 0xC && sizeof(A11yIconEntry) == 0xC, "FontIcon");
extern A11yIconEntry GAME_PTR gFontIconTables[4];

/* The actions of gs_input.c, and three marks that are not one button. */
enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_CROSS, ACT_CIRCLE, ACT_SQUARE, ACT_TRIANGLE, ACT_L1, ACT_R1, ACT_L2,
       ACT_R2, ACT_L3, ACT_R3, ACT_START, ACT_SELECT, ACT_LS_UP, ACT_LS_DOWN, ACT_LS_LEFT, ACT_LS_RIGHT,
       ICON_LSTICK = 100, ICON_RSTICK, ICON_DPAD };

/* Icon names that say which button they are (see the top: inferred, not seen on the disc yet). */
static const struct { uint16_t name[5]; int what; } kIcons[] = {
    {{0x25CB}, ACT_CIRCLE}, {{0x25EF}, ACT_CIRCLE}, {{'O'}, ACT_CIRCLE}, {{'M', 'A', 'R', 'U'}, ACT_CIRCLE},
    {{'C', 'I', 'R', 'C'}, ACT_CIRCLE}, {{'C', 'I', 'R'}, ACT_CIRCLE},
    {{0x00D7}, ACT_CROSS}, {{0x2715}, ACT_CROSS}, {{0x2716}, ACT_CROSS}, {{'X'}, ACT_CROSS},
    {{'B', 'A', 'T', 'U'}, ACT_CROSS}, {{'B', 'A', 'T', 'S'}, ACT_CROSS}, {{'C', 'R', 'O', 'S'}, ACT_CROSS},
    {{0x25B3}, ACT_TRIANGLE}, {{0x25B2}, ACT_TRIANGLE}, {{'S', 'A', 'N', 'K'}, ACT_TRIANGLE},
    {{'T', 'R', 'I'}, ACT_TRIANGLE}, {{'T', 'R', 'I', 'A'}, ACT_TRIANGLE},
    {{0x25A1}, ACT_SQUARE}, {{0x25A0}, ACT_SQUARE}, {{'S', 'I', 'K', 'A'}, ACT_SQUARE}, {{'S', 'H', 'I', 'K'}, ACT_SQUARE},
    {{'S', 'Q', 'U', 'A'}, ACT_SQUARE}, {{'S', 'Q'}, ACT_SQUARE},
    {{'L', '1'}, ACT_L1}, {{'R', '1'}, ACT_R1}, {{'L', '2'}, ACT_L2}, {{'R', '2'}, ACT_R2}, {{'L', '3'}, ACT_L3},
    {{'R', '3'}, ACT_R3},
    {{'S', 'T', 'A', 'R'}, ACT_START}, {{'S', 'T', 'R', 'T'}, ACT_START}, {{'S', 'T'}, ACT_START},
    {{'S', 'E', 'L', 'E'}, ACT_SELECT}, {{'S', 'E', 'L'}, ACT_SELECT}, {{'S', 'E'}, ACT_SELECT},
    {{0x2191}, ACT_UP}, {{0x2193}, ACT_DOWN}, {{0x2190}, ACT_LEFT}, {{0x2192}, ACT_RIGHT},
    {{'U', 'P'}, ACT_UP}, {{'D', 'O', 'W', 'N'}, ACT_DOWN}, {{'L', 'E', 'F', 'T'}, ACT_LEFT}, {{'R', 'I', 'G', 'H'}, ACT_RIGHT},
    {{'L', 'S'}, ICON_LSTICK}, {{'R', 'S'}, ICON_RSTICK},
};

static int sPlayer = -1; /* whose controller names the buttons: 0 or 1, -1 whoever holds one (a11y_skills.c sets it) */
static char sNoted[16][16]; /* the unknown icon names written to the log already */
static int sNotedCount;

void PortA11y_TextPlayer(int player) {
    sPlayer = player;
}

static int same(const uint16_t *a, const uint16_t *b, int len) {
    int i;
    for (i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* The game's table, once into the log: what kIcons has to know. */
static void log_table(void) {
    static int done;
    A11yIconEntry GAME_PTR e = gFontIconTables[0];
    char line[512];
    int n = 0, i, k;

    if (done || e == NULL) {
        return;
    }
    done = 1;
    n = snprintf(line, sizeof(line), "icon names of the game:");
    for (i = 0; i < 64 && e[i].name != NULL && n < (int)sizeof(line) - 32; i++) {
        n += snprintf(line + n, sizeof(line) - n, " %d=", i);
        for (k = 0; k < e[i].name->len && k < 4; k++) {
            uint16_t c = e[i].name->chars[k];
            n += snprintf(line + n, sizeof(line) - n, c >= 0x21 && c < 0x7F ? "%c" : "U+%04X", c);
        }
    }
    PortA11y_Note(line);
}

/* The word for one icon of the table: its button's name, or for a name kIcons does not know, the name itself. */
static void icon_word(const A11yIconName GAME_PTR name, char *out, int cap) {
    static const char *const kMarks[] = {"Left stick", "Right stick", "D-pad"};
    const char *word = NULL;
    uint16_t chars[4];
    int len = name->len < 4 ? name->len : 4, i, n = 0, known;
    char note[96];

    for (i = 0; i < len; i++) {
        chars[i] = name->chars[i];
    }
    for (i = 0; i < (int)(sizeof(kIcons) / sizeof(kIcons[0])); i++) {
        int k = 0;
        while (k < 4 && kIcons[i].name[k] != 0) {
            k++;
        }
        if (k == len && same(kIcons[i].name, chars, len)) {
            word = kIcons[i].what >= ICON_LSTICK ? PortA11y_TrSense("button", kMarks[kIcons[i].what - ICON_LSTICK])
                                                 : PortA11y_ActionName(sPlayer, kIcons[i].what);
            break;
        }
    }
    if (word != NULL) {
        snprintf(out, cap, "%s", word);
        return;
    }
    known = i < (int)(sizeof(kIcons) / sizeof(kIcons[0])); /* (a known button with nothing bound to it: said by its name) */
    for (i = 0; i < len && n < cap - 4; i++) { /* (UTF-8 of the name) */
        uint16_t c = chars[i];
        if (c < 0x80) {
            out[n++] = (char)c;
        } else if (c < 0x800) {
            out[n++] = (char)(0xC0 | c >> 6);
            out[n++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[n++] = (char)(0xE0 | c >> 12);
            out[n++] = (char)(0x80 | (c >> 6 & 0x3F));
            out[n++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[n] = '\0';
    for (i = 0; !known && i < sNotedCount && strcmp(sNoted[i], out) != 0; i++) {
    }
    if (!known && i == sNotedCount && sNotedCount < 16) { /* (once per name: a string can be read every blank) */
        snprintf(sNoted[sNotedCount++], sizeof(sNoted[0]), "%s", out);
        snprintf(note, sizeof(note), "icon name not known: %s", out);
        PortA11y_Note(note);
    }
}

/* The icons named in one list of a tag (s..end: names run together), as words separated by spaces. */
static void icon_list(const uint16_t *s, const uint16_t *end, char *out, int cap) {
    A11yIconEntry GAME_PTR table = gFontIconTables[0];
    int n = 0, i, best, bestLen;
    char word[64];

    out[0] = '\0';
    log_table();
    while (s < end && table != NULL) {
        best = -1;
        bestLen = 0;
        for (i = 0; i < 64 && table[i].name != NULL; i++) { /* the longest name that starts here (FontIcon_Find) */
            int len = table[i].name->len;
            uint16_t chars[4];
            int k;
            if (len < 1 || len > 4 || len <= bestLen || s + len > end) {
                continue;
            }
            for (k = 0; k < len; k++) {
                chars[k] = table[i].name->chars[k];
            }
            if (same(s, chars, len)) {
                best = i;
                bestLen = len;
            }
        }
        if (best < 0) {
            break; /* (the printer stops there too) */
        }
        icon_word(table[best].name, word, sizeof(word));
        if (word[0] != '\0' && n + (int)strlen(word) + 2 < cap) {
            n += snprintf(out + n, cap - n, "%s%s", n > 0 ? " " : "", word);
        }
        s += bestLen;
    }
}

static int starts(const uint16_t *s, const char *ascii) {
    while (*ascii != '\0') {
        if (*s++ != (uint16_t)*ascii++) {
            return 0;
        }
    }
    return 1;
}

/* A tag at `at` (a '<'): its words into out (empty for a colour or a mark), and how many characters it takes; 0 when
   it is not one of the printer's tags (then the '<' is text). For gs/a11y.c, PortA11y_Utf8. */
int PortA11y_GameTag(const uint16_t *at, char *out, int cap) {
    const uint16_t *arg, *end, *comma;
    int i;

    out[0] = '\0';
    if (at[0] != '<') {
        return 0;
    }
    if (starts(at + 1, "PADS=") || starts(at + 1, "PAD=")) {
        arg = at + (at[4] == 'S' ? 6 : 5);
        for (end = arg; *end != 0 && *end != '>' && end - arg < 128; end++) {
        }
        if (*end != '>') {
            return 0;
        }
        if (at[4] == 'S') { /* one list per controller type: the first is the PS2's own (the others are empty there) */
            for (comma = arg; comma < end && *comma != ','; comma++) {
            }
            icon_list(arg, comma, out, cap);
        } else {
            icon_list(arg, end, out, cap);
        }
        return (int)(end - at) + 1;
    }
    if (starts(at + 1, "COL=DEF>")) { /* (FontTag_Color: "DEF", or eight capital hex digits; else it is text) */
        return 9;
    }
    if (starts(at + 1, "COL=")) {
        for (i = 5; i < 13 && ((at[i] >= '0' && at[i] <= '9') || (at[i] >= 'A' && at[i] <= 'F')); i++) {
        }
        return i == 13 && at[13] == '>' ? 14 : 0;
    }
    if (starts(at + 1, "UB0>")) {
        return 5;
    }
    return 0;
}
