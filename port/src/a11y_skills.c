/*
 * Accessibility: the pause menu's skill list, one move at a time, with its buttons named as the player's controller
 * names them.
 *
 * The page (src/battle/btl_seq.c, BtlText_DrawList) is drawn from a script of the owner's fighter: a page title,
 * then each move with the lines under it, and only the selected move's lines are shown. The text reader would say
 * whatever was drawn at once, so the page is read here instead: the title when the page changes (left and right),
 * the move under the cursor (up and down) with its place in the page, then its lines, in which the button icons
 * become the names of the buttons (a11y_text.c, gs/a11y_pad.c) of the owner's controller.
 *
 * The script, as the game reads it (BtlText_CheckLineMask, BtlText_CheckUnlock, BtlText_NextLine): after the U+FEFF,
 * lines ended by a CR or an LF; each starts with two characters, the second a hex digit, a mask of the controller
 * types it is for (bit work->maskBit); then "&ddd" when it is for an unlocked character only (drawn at half
 * strength while locked); then the tag: '$' page title (page digit, then the text), '*' a move (three digits of the
 * small pictures at its right, not read, then the text), '%0' / '%1' a line of the move, '#' a line of buttons, '@'
 * the end. The same walk as the game's, without calling it: those functions step a pointer that would have to be in
 * the game's memory.
 *
 * It only reads; nothing of the game is written.
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

#define A11Y_INTERRUPT 1 /* gs/a11y.c */
extern const char *PortA11y_Tr(const char *label) __attribute__((format_arg(1)));
#define tr(label) PortA11y_Tr(label)
extern void PortA11y_Say(const char *text, int flags);
extern void PortA11y_Note(const char *text);
extern int PortA11y_Utf8(char *out, int cap, const uint16_t *str);
extern void PortA11y_TextPlayer(int player); /* a11y_text.c */
extern int Port_NetActive(void);             /* gs/net.c */

/* include/battle/hud_0.h: BtlMenuList and BtlMenuWork (the same block as btl_seq.h's BtlTextWork), what is read. */
typedef struct {
    int pages;      /* 0x00: '$' lines */
    int count[10];  /* 0x04: moves per page */
    int page;       /* 0x2C */
    int scroll[10]; /* 0x30 */
    int cursor[10]; /* 0x58: the selected move per page */
} A11ySkillList;
typedef struct {
    char unk0[8];
    int active;              /* 0x08: the menu is shown */
    int pad;                 /* 0x0C: the owner's controller port, whose list is shown */
    int maskBit;             /* 0x10: the bit of a line's mask digit that shows the line */
    char unk14[4];
    uint16_t GAME_PTR text;  /* 0x18: the owner's fighter's script */
    char unk1C[4];
    A11ySkillList list[2];   /* 0x20 */
} A11ySkillWork;
LAYOUT_ASSERT(offsetof(A11ySkillWork, list) == 0x20 && sizeof(A11ySkillList) == 0x80, "BtlMenuWork");
extern A11ySkillWork GAME_PTR gBtlMenu;
extern uint64_t GAME_PTR gSaveData;        /* (as btl_seq.c sees it: only the characters' unlock bits, +0xC10) */
extern int BtlCtrl_TestMemberUnk70(int side); /* src/battle/btl_tech_a.c (only reads) */

#define LINE_MAX 512
#define SCRIPT_MAX 0x40000 /* characters: a script that does not end before this is not read */

static int sShown;

/* 1 while the skill list is read here (a11y_game.c: the text reader leaves what it draws alone). */
int PortA11y_SkillListShown(void) {
    return sShown;
}

static int newline(uint16_t c) {
    return c == 10 || c == 13;
}

/* The text from s to the end of its line, as the reader says it, into out. */
static void line_text(uint16_t GAME_PTR s, char *out, int cap) {
    uint16_t buf[LINE_MAX + 1];
    int n = 0;

    while (n < LINE_MAX && s[n] != 0 && !newline(s[n])) {
        buf[n] = s[n];
        n++;
    }
    buf[n] = 0;
    out[0] = '\0';
    PortA11y_Utf8(out, cap, buf);
}

static void add(char *text, int cap, const char *part, const char *sep) {
    size_t len = strlen(text);

    if (part[0] != '\0' && len + strlen(sep) + strlen(part) + 1 < (size_t)cap) {
        snprintf(text + len, cap - len, "%s%s", len > 0 ? sep : "", part);
    }
}

/* What is on the page now: its title (when `title`), the move under the cursor with its place, and its lines. */
static void page_text(A11ySkillWork GAME_PTR w, int side, int title, char *text, int cap) {
    A11ySkillList GAME_PTR list = &w->list[side];
    uint16_t GAME_PTR p = w->text;
    int page = -1, entry = -1, want = list->page, cursor = list->cursor[want], mask, shown, locked, id, k;
    char line[LINE_MAX * 3], head[64];

    text[0] = '\0';
    p += 1; /* (U+FEFF) */
    for (k = 0; k < SCRIPT_MAX && *p != 0; k++) {
        switch (p[1]) {
        case 'F': mask = 0xF; break;
        case '1': mask = 1; break;
        case '2': mask = 2; break;
        case '4': mask = 4; break;
        case '8': mask = 8; break;
        case 'E': mask = 0xE; break;
        case 'D': mask = 0xD; break;
        case 'B': mask = 0xB; break;
        case '7': mask = 7; break;
        default: mask = 0; break;
        }
        shown = (mask >> w->maskBit) & 1;
        p += 2;
        locked = 0;
        if (shown && p[0] == '&') {
            id = (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
            p += 4;
            if (id == 0x56) {
                locked = BtlCtrl_TestMemberUnk70(side) != 0;
            } else if (gSaveData != NULL && id >= 0 && id < 1000) {
                locked = !((gSaveData[0xC10 / 8 + id / 64] >> (id % 64)) & 1);
            }
        }
        if (shown) {
            if (p[0] == '@') {
                break;
            }
            if (p[0] == '$') {
                page = p[1] - '0';
                entry = -1;
                if (page == want && title) {
                    line_text(p + 2, line, sizeof(line));
                    snprintf(head, sizeof(head), tr("page %d of %d"), want + 1, list->pages);
                    add(text, cap, line, ". ");
                    add(text, cap, head, ", ");
                }
            } else if (p[0] == '*') {
                entry++;
                if (page == want && entry == cursor) {
                    line_text(p + 4, line, sizeof(line));
                    add(text, cap, line, ". ");
                    if (list->count[want] > 1) {
                        snprintf(head, sizeof(head), tr(", %d of %d"), entry + 1, list->count[want]);
                        add(text, cap, head, "");
                    }
                    if (locked) {
                        add(text, cap, tr("locked"), ", ");
                    }
                }
            } else if (page == want && entry == cursor && ((p[0] == '%' && (p[1] == '0' || p[1] == '1')) || p[0] == '#')) {
                line_text(p + (p[0] == '%' ? 2 : 1), line, sizeof(line));
                add(text, cap, line, ". ");
            }
        }
        while (*p != 0 && !newline(*p)) { /* BtlText_NextLine: past one CR or LF */
            p++;
        }
        if (*p != 0) {
            p++;
        }
    }
}

/* Once per blank from a11y_game.c (btl_menu): `shown` while the skill list is the active page of the pause menu. */
void PortA11y_SkillList(int shown) {
    static int saidPage = -1, saidCursor = -1, saidSide = -1;
    A11ySkillWork GAME_PTR w = gBtlMenu;
    A11ySkillList GAME_PTR list;
    char text[2048];
    int side, page, cursor;

    if (!shown || w == NULL || w->text == NULL || w->text[0] != 0xFEFF) {
        if (sShown) {
            sShown = 0;
            PortA11y_TextPlayer(-1);
        }
        saidPage = saidCursor = saidSide = -1;
        return;
    }
    side = w->pad == 1 ? 1 : 0;
    list = &w->list[side];
    page = list->page;
    if (page < 0 || page > 9 || list->pages < 1) {
        return;
    }
    cursor = list->cursor[page];
    sShown = 1;
    if (page == saidPage && cursor == saidCursor && side == saidSide) {
        return;
    }
    PortA11y_TextPlayer(Port_NetActive() ? 0 : side); /* (online the local player plays with player 1's controls: net.c) */
    page_text(w, side, page != saidPage || side != saidSide, text, sizeof(text));
    saidPage = page;
    saidCursor = cursor;
    saidSide = side;
    if (text[0] != '\0') {
        PortA11y_Say(text, A11Y_INTERRUPT);
    } else {
        PortA11y_Note("skill list: nothing to say on this page");
    }
}
