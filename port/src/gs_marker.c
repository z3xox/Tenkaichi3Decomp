/*
 * Markers for the PC renderer inside the game's display list. The game builds each frame's list first and the
 * renderer executes it afterwards, so "a native effect goes HERE" has to travel in the list: a one-register GIF
 * packet that writes the effect number to GS register 0x7F, which the real GS does not have. The front end
 * (port/src/gs/gs_core.c) hands it to the GPU back end; the software reference ignores it and keeps drawing the
 * PS2 passes. Called from the game's effect functions under `#ifdef PORT`.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Dma_AddData(void *src, int size);

void Port_GsMarker(int effect) {
    uint32_t pkt[12] = {
        0x10000000u | 2, 0, 0x10000000u, 0x50000000u | 2, /* DMA CNT, 2 quadwords; VIF FLUSHE, DIRECT 2 */
        0x8000u | 1, 0x10000000u, 0xE, 0,                  /* GIF tag: 1 loop, EOP, one register: A+D */
        (uint32_t)effect, 0, 0x7F, 0,                      /* value, register number */
    };

    Dma_AddData(pkt, sizeof(pkt));
}

/* Widescreen menus (the menu layout "Widescreen", port/src/gs/gs_draw.c). A screen converted for it marks its draw,
   and each 2D piece of it is kept at its true shape, anchored where the screen's design has it:
     PORT_MENU_CENTER  about the middle of the picture (the default of a page)
     PORT_MENU_LEFT    against the left edge (a menu's column)
     PORT_MENU_RIGHT   against the right edge (the characters beside it)
     PORT_MENU_FILL    across the whole width, stretched (backdrops, scrolling scenery)
     PORT_MENU_CENTER_ALL, _LEFT_ALL, _RIGHT_ALL  as CENTER, LEFT, RIGHT, and pieces as wide as the 4:3 screen too
                       (in the others they fill the width): a bar whose cells hold other pieces
     PORT_MENU_PAGE    as CENTER, and a piece from the top edge against a 4:3 edge, in that half, against that edge
                       (the gold clouds in the corners of the screens from 10 on)
     PORT_MENU_PAGE_TOP  as PAGE, and any piece in the top 120 lines starting in the right half against the right
                       edge (the item shop's counters, their icons and frames)
     PORT_MENU_PAGE_EDGE  as PAGE, and a piece cut off at the 4:3 screen's right edge (starting right of 200) against
                       the right edge, at its left edge (ending left of 312) against the left: frames whose art is
                       cut there (World Tour's timer, Character Reference's list)
     PORT_MENU_SPLIT   each piece against the edge of the half it starts in, at its true shape (wide ones too): a screen
                       whose left half is one thing and its right half another, frames and text alike (marker 0x2F)
     PORT_MENU_CENTER_FREE  as CENTER, not cut at the 4:3 screen's edges: a picture that flies across the whole of
                       the wide picture (World Tour's announcer poster)
   Which clip of a screen's movie goes where is the table in Port_MenuClipPart, by the clips' names. A screen that
   marks nothing is stretched as before. The renderer acts on the markers only with that layout chosen. */
enum { PORT_MENU_NONE = -1, PORT_MENU_CENTER, PORT_MENU_FILL, PORT_MENU_LEFT, PORT_MENU_RIGHT, PORT_MENU_CENTER_ALL, PORT_MENU_LEFT_ALL,
       PORT_MENU_RIGHT_ALL, PORT_MENU_PAGE, PORT_MENU_PAGE_TOP, PORT_MENU_PAGE_EDGE, PORT_MENU_CENTER_FREE, PORT_MENU_SPLIT };
static int sMenuPart = PORT_MENU_NONE;
static int sMenuMovie;               /* which of the screen's movies is being drawn (Port_MenuMovie) */
static int sMenuScreen;              /* PORT_MENU_SCREEN_* of the page being drawn, 0: none */

int Port_MenuPart(int part);

/* The screen's movie `index` is drawn next. Character select: movie 1 is the first player's roster and 3 that
   player's window of customisations, 2 and 4 the other player's: each against that player's edge. */
void Port_MenuMovie(int index) {
    sMenuMovie = index;
    if (sMenuScreen == 3) {
        Port_MenuPart(index == 0 ? PORT_MENU_CENTER_ALL : index & 1 ? PORT_MENU_LEFT_ALL : PORT_MENU_RIGHT_ALL);
    }
}

/* The part what follows belongs to; returns the one before (to be given back). */
int Port_MenuPart(int part) {
    int prev = sMenuPart;
    if (part != sMenuPart) {
        sMenuPart = part;
        Port_GsMarker(part == PORT_MENU_NONE ? 0x13 : part == PORT_MENU_SPLIT ? 0x2F : 0x22 + part); /* (0x13: the HUD's "end") */
    }
    return prev;
}

/* The part being drawn now (PORT_MENU_NONE outside a converted screen) */
int Port_MenuPartNow(void) {
    return sMenuPart;
}

/* A converted screen's draw begins (its number) / ends (0). A screen's draw can call another's (a backdrop drawn by
   a function of its own): the end of the inner one goes back to the outer one. */
static int sPageStack[8], sPageDepth;
static int sMoveDraw; /* converted screens' draws so far (Port_MenuPage): Port_MenuClipMoving's clock */
void Port_MenuPage(int screen) {
    if (screen != 0 && sPageDepth == 0) {
        sMoveDraw++; /* (a draw of a converted screen: Port_MenuClipMoving's clock) */
    }
    if (screen != 0) {
        if (screen == sMenuScreen) {
            return; /* (marked again in the same draw, as character select does per stage picture: one page) */
        }
        if (sPageDepth < 8) {
            sPageStack[sPageDepth++] = sMenuScreen;
        }
    } else {
        screen = sPageDepth > 0 ? sPageStack[--sPageDepth] : 0;
    }
    sMenuScreen = screen;
    /* (the main menu: what is not named in Port_MenuClipPart belongs to its column, on the left) */
    /* (character select: all of it at its true shape, its bars too, which are pieces as wide as the 4:3 screen, so
       that the bars' cells and the characters in them stay together; only the stage pictures fill the width) */
    /* (what a page draws itself, not in a clip, stays in the middle: on most screens its windows and plates. On
       Dragon History, 13, it is the scenery, which fills the width. Character Reference's shelves are such pieces
       too, but so is its window of rows: in the middle, the shelves meet the backdrop at the 4:3 page's edges.) */
    /* (the title, 1: its logo is a picture as wide as the 4:3 screen, kept at its true shape; its sky and clouds fill
       by their own markers)
       (Evolution Z's character customising, 37, is laid out as character select: its bars at their true shape too;
       the Data Center's password, 44: its keyboard is one picture as wide as the 4:3 screen, its letters on it) */
    /* (the character selects of Ultimate Battle and the World Tournament, 15, 19, 23, 24, 25, as character select) */
    Port_MenuPart(screen == 0 ? PORT_MENU_NONE : screen == 2 ? PORT_MENU_LEFT
                  : screen == 51 ? PORT_MENU_CENTER_FREE /* (the logo page before the title: a picture of the whole 4:3 screen) */
                  : screen == 37 ? PORT_MENU_SPLIT /* (Evolution Z's customising: the player's side left, the points right) */
                  : screen == 1 || screen == 3 || screen == 44 || screen == 50 ? PORT_MENU_CENTER_ALL
                                                : screen == 14 ? PORT_MENU_LEFT_ALL
                                                : screen == 13 || screen == 42 || screen == 18 ? PORT_MENU_FILL : screen == 38 ? PORT_MENU_PAGE_TOP
                                                : screen == 17 || screen == 20 ? PORT_MENU_PAGE_EDGE
                                                : screen >= 10 ? PORT_MENU_PAGE : PORT_MENU_CENTER);
}

/* Backdrops: on, and back to the page's part */
static int sFillPrev;
void Port_MenuFill(int on) {
    if (on) {
        sFillPrev = Port_MenuPart(PORT_MENU_FILL);
    } else {
        Port_MenuPart(sFillPrev);
    }
}

static int starts(const char *name, const char *prefix) {
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

/* The side of a measured clip that lies from x0 to x1 on the 4:3 screen: one touching its left edge (a guide on the
   left, a column of plates out of it) against the left edge, its right edge the right edge, otherwise by the third
   its middle is in (the rows of a window in the middle stay with it). One as wide as the screen, or with nothing to draw, is left to its pieces. */
static int clip_place(int x0, int x1);
int Port_MenuClipPlace(int x0, int x1) {
    int part = clip_place(x0, x1);
    return part == PORT_MENU_LEFT ? PORT_MENU_LEFT_ALL : part == PORT_MENU_RIGHT ? PORT_MENU_RIGHT_ALL : part;
}
static int clip_place(int x0, int x1) {
    int c = (x0 + x1) / 2;
    if (getenv("BT3_CLIP_LOG") != NULL) {
        fprintf(stderr, "bt3: menu clip measured %d..%d\n", x0, x1);
    }
    if (x1 < x0 || (x0 <= 1 && x1 >= 511)) {
        return PORT_MENU_NONE;
    }
    if (x0 < 16 && x1 < 496) {
        return PORT_MENU_LEFT;
    }
    if (x1 > 496 && x0 > 16) {
        return PORT_MENU_RIGHT;
    }
    return c < 171 ? PORT_MENU_LEFT : c > 341 ? PORT_MENU_RIGHT : PORT_MENU_CENTER;
}

static int clip_part(const char *name, int x, int top);

/* The part of a clip of the page being drawn (FlashSlot_Draw), PORT_MENU_NONE: as its parent's. `x`: where the clip's
   origin is on the 4:3 screen (0..512); `top`: a clip of the movie itself, not inside another. A clip set against
   an edge keeps all of its pieces at their true shape, one as wide as the 4:3 screen too (the *_ALL parts): a
   plate's sprite sliding in is that wide for a frame, and would be taken for scenery and stretched. */
int Port_MenuClipPart(const char *name, int x, int top) {
    int part = clip_part(name, x, top);
    if (sMenuScreen < 2) {
        return part;
    }
    /* (the main menu too: its selected plate's streak is a piece as wide as the 4:3 screen at some places of the
       column, and was then stretched across the screen, scrolling up) */
    return part == PORT_MENU_LEFT ? PORT_MENU_LEFT_ALL : part == PORT_MENU_RIGHT ? PORT_MENU_RIGHT_ALL : part;
}

int Port_MenuClipPlace(int x0, int x1);

static int clip_part(const char *name, int x, int top) {
    static int log = -1;
    if (log < 0) {
        log = getenv("BT3_CLIP_LOG") != NULL;
    }
    if (sMenuScreen == 0) {
        return PORT_MENU_NONE;
    }
    if (log) { /* (testing: the clips a converted screen draws, each name once) */
        static char seen[512][64];
        static int n;
        int i;
        char key[64];
        snprintf(key, sizeof(key), "%d %d %s", sMenuScreen, sMenuMovie, name);
        for (i = 0; i < n && strcmp(seen[i], key) != 0; i++) {
        }
        if (i == n && n < 512) {
            snprintf(seen[n++], 64, "%s", key);
            fprintf(stderr, "bt3: menu clip %s%s x %d\n", key, top ? " (top)" : "", x);
        }
    }
    if (starts(name, "mc_bg_") || starts(name, "mc_kumo_")) { /* (not "mc_bgm_now": the music's plate) */
        return PORT_MENU_FILL;
    }
    if (sMenuScreen == 1 && starts(name, "mc_menu_plate_")) {
        return PORT_MENU_CENTER_ALL; /* (the title's New Game / Continue: sprites as wide as the 4:3 screen, kept at their
                                        true shape with their shadows, not stretched as scenery) */
    }
    if (sMenuScreen == 2) { /* the main menu: the column of plates on the left, the guides on the right */
        if (starts(name, "mc_menu_plate_") || starts(name, "mc_icon") || starts(name, "mc_title") || starts(name, "mc_ch_a_db_") ||
            starts(name, "mc_dragonball") || starts(name, "mc_menu_yajirusi") || starts(name, "mc_chara_db")) {
            return PORT_MENU_LEFT;
        }
        if (starts(name, "mc_guide_") || starts(name, "mc_uide_")) { /* ("mc_uide_ghn_bdl_hair": so named in the movie) */
            return PORT_MENU_RIGHT;
        }
    }
    if (sMenuScreen == 3) {
        /* character select: each player's name plates, windows and their text against that player's edge (the
           text is queued and drawn after the screen, Font_Flush, in the part it was queued in). The big pictures
           (mc_single_chara_*, their masks) are as wide as half of the 4:3 screen and keep to the line between the
           two players, as there (the user's choice). */
        size_t n = strlen(name);
        if (n > 2 && name[n - 2] == '_' && (name[n - 1] == 'l' || name[n - 1] == 'r') && !starts(name, "mc_single_chara_") &&
            !starts(name, "mc_face_mask_")) {
            return name[n - 1] == 'l' ? PORT_MENU_LEFT : PORT_MENU_RIGHT;
        }
    }
    if (sMenuScreen == 38 && top && (starts(name, "mc_pay_num_") || starts(name, "mc_shop_num_") || starts(name, "mc_item_num_"))) {
        return PORT_MENU_RIGHT; /* (the item shop: the digits of its counters of zenni and of the share collected; their
                                   frames are the page's own pieces, PORT_MENU_PAGE_TOP) */
    }
    if (sMenuScreen == 20 && (strcmp(name, "mc_yajirusi_l") == 0 || strcmp(name, "mc_yajirusi_r") == 0)) {
        return PORT_MENU_CENTER; /* (World Tour: the window's left and right arrows, inside the list's arrow clip
                                    mc_yajirusi_u_l, which is on the right: with the window, in the middle) */
    }
    if (sMenuScreen == 20 && top && (starts(name, "mc_menu_text_") || strcmp(name, "mc_suji_01") == 0 || starts(name, "mc_dragonball_off_") ||
                                     starts(name, "mc_plate_text_"))) {
        return PORT_MENU_CENTER; /* (World Tour: the window of a tournament's prizes, difficulty and number of characters
                                    in the middle with its frame, which is the page's own piece) */
    }
    if (sMenuScreen == 20 && top && (strcmp(name, "mc_timer") == 0 || starts(name, "mc_taikai") || starts(name, "mc_menu_plate_") ||
                                     starts(name, "mc_yajirusi_"))) {
        return PORT_MENU_RIGHT; /* (World Tour: the timer and the list of tournaments, their art cut at the right edge) */
    }
    if (sMenuScreen == 17 && top && (starts(name, "mc_menu_plate") || strcmp(name, "mc_scroll_bar_point") == 0 || starts(name, "mc_yajirusi_") ||
                                     starts(name, "mc_name_text_") || starts(name, "mc_form_text_") || starts(name, "mc_voice_text_") ||
                                     strcmp(name, "mc_dammy_text") == 0 || strcmp(name, "mc_icon_voice_play") == 0)) {
        return PORT_MENU_RIGHT; /* (Character Reference: its list and a character's page, cut at the right edge) */
    }
    if (sMenuScreen == 17 && top && strcmp(name, "mc_single_chara_l") == 0) {
        return PORT_MENU_LEFT; /* (and the character's picture there, on the left) */
    }
    if (sMenuScreen >= 10 && top && strcmp(name, "mc_message_text") == 0) {
        return PORT_MENU_LEFT; /* (the message window's text from its left, as on the main menu: its window fills) */
    }
    if (sMenuScreen == 16 && top && starts(name, "mc_mode_")) {
        return PORT_MENU_LEFT; /* (and the window of its modes, Single, Team, DP, under the chosen plate) */
    }
    if (sMenuScreen == 16 && top && starts(name, "mc_menu_plate_")) {
        return PORT_MENU_LEFT; /* (the versus menu: its plates run off the left edge, the chosen one with them) */
    }
    if (sMenuScreen == 14 && top) { /* Dragon History's result: its panels and their text and digits slide in from the
                                        left (as wide as the 4:3 screen at rest, narrower while sliding: kept at their
                                        true shape throughout, or they changed size every frame); its lines fill */
        if (starts(name, "mc_line_") || strcmp(name, "mc_noise") == 0) {
            return PORT_MENU_FILL;
        }
        if (starts(name, "mc_chara_")) {
            return PORT_MENU_RIGHT; /* (the guide, Mr. Satan: he comes to rest against the right edge) */
        }
        if (starts(name, "mc_text_base_") || starts(name, "mc_point_text_") || starts(name, "mc_get_point_") ||
            starts(name, "mc_total_point_") || strcmp(name, "mc_battle_text") == 0 || starts(name, "mc_result_text_") ||
            starts(name, "mc_title_get") || starts(name, "mc_icon_get") || starts(name, "mc_dammy_text_")) {
            return PORT_MENU_LEFT;
        }
    }
    if (sMenuScreen == 18 && top && starts(name, "mc_menu_plate_")) {
        return PORT_MENU_RIGHT; /* (Training: its plates run off the right edge) */
    }
    if (sMenuScreen == 20 && top && strcmp(name, "mc_world_chirashi") == 0) {
        return PORT_MENU_CENTER_FREE; /* (World Tour: the announcer's poster, one picture flying across the screen) */
    }
    if (sMenuScreen == 13 && top && (starts(name, "mc_menu_plate_") || starts(name, "mc_level_plate_") || starts(name, "mc_scenario"))) {
        return PORT_MENU_CENTER; /* (Dragon History: its plates at their true shape, over the scenery that fills) */
    }
    if ((sMenuScreen == 15 || sMenuScreen == 19 || (sMenuScreen >= 23 && sMenuScreen <= 25)) && top) {
        /* (the character selects of Ultimate Battle and the World Tournament: the player's name plate, picture side
           and their text against the left edge as in the versus one; the roster with its cells in the middle) */
        size_t n = strlen(name);
        if (n > 2 && name[n - 2] == '_' && name[n - 1] == 'l' && !starts(name, "mc_single_chara_")) {
            return PORT_MENU_LEFT;
        }
        if (starts(name, "mc_chara_chip_") || strcmp(name, "mc_plate") == 0 || starts(name, "mc_yajirusi_") || strcmp(name, "mc_chara_mask") == 0) {
            /* (with its mask: the cells outside the bar are hidden by it, so it has to be where they are) */
            return PORT_MENU_LEFT_ALL; /* (the player's roster against the left edge, as the versus one's: the user's choice) */
        }
        if (starts(name, "mc_custom_") || starts(name, "mc_item_") || starts(name, "mc_icon_item_")) {
            return PORT_MENU_CENTER_ALL;
        }
    }
    if (sMenuScreen == 37 && top) { /* Evolution Z's customising (the user's choice): the player's side on the left, the
                                        points on the right; the page's own pieces by the half they start in (SPLIT) */
        if (starts(name, "mc_chara_chip_") || strcmp(name, "mc_plate") == 0 || starts(name, "mc_yajirusi_") ||
            starts(name, "mc_name_") || starts(name, "mc_form_") || strcmp(name, "mc_single_chara_l") == 0 ||
            starts(name, "mc_status_ability_") || strcmp(name, "mc_status_attribute") == 0) {
            return PORT_MENU_LEFT_ALL;
        }
        if (strcmp(name, "mc_status_power_base") == 0 || strcmp(name, "mc_status_base_1") == 0 || starts(name, "mc_text_power_") ||
            strcmp(name, "mc_exp_bar_point") == 0 || starts(name, "mc_ability_limit_") || starts(name, "mc_num_zp_")) {
            return PORT_MENU_RIGHT_ALL;
        }
    }
    if (sMenuScreen == 38 && top && starts(name, "menu_plate_")) {
        return PORT_MENU_LEFT; /* (the item shop: its two plates slide in from the left, as Evolution Z's) */
    }
    if (sMenuScreen == 42 && top && starts(name, "mc_menu_plate_")) {
        return PORT_MENU_LEFT; /* (Evolution Z: its column slides in from the left: against the left edge, so it comes in
                                  from the picture's edge, as the main menu's) */
    }
    if ((sMenuScreen == 43 || sMenuScreen == 39) && top && starts(name, "mc_menu_plate_")) {
        return PORT_MENU_RIGHT; /* (the Data Center, Options: their plates reach the right edge, as their art shows) */
    }
    if (sMenuScreen == 39 && top && starts(name, "mc_bottom_plate_")) {
        return PORT_MENU_LEFT; /* (Options: "EXIT" below them, from the left) */
    }
    if ((sMenuScreen >= 10 || sMenuScreen == 3) && top && (starts(name, "mc_title") || strcmp(name, "mc_icon") == 0)) {
        return PORT_MENU_LEFT; /* (the title row against the left edge, as on the main menu) */
    }
    if (sMenuScreen == 13 && (starts(name, "mc_yuragi_") || starts(name, "mc_taki") || starts(name, "mc_sakana"))) {
        return PORT_MENU_FILL; /* (Dragon History: its scenery, the waterfall, its shimmer, the fish, with the backdrop) */
    }
    if ((sMenuScreen == 38 || sMenuScreen == 18 || sMenuScreen == 42) && top && starts(name, "mc_ch_")) {
        return PORT_MENU_FILL; /* (the item shop's, Training's and Evolution Z's scene characters (Oolong at the window,
                                  Goten, Videl) are drawn with pieces of their scenery as wide as the screen: with the
                                  scenery, which fills the width there) */
    }
    if (sMenuScreen >= 10 && top &&
        (starts(name, "mc_guide_") || strcmp(name, "mc_pikkoro") == 0 || strcmp(name, "mc_ch_gohan") == 0 || strcmp(name, "mc_ch_bidel") == 0 ||
         strcmp(name, "mc_ch_goten") == 0 || strcmp(name, "mc_ch_uron") == 0)) {
        /* the other screens, all of one kind (a title, plates, a guide or two, a message across the bottom): the
           guides, the characters beside the menu, against the side of the 4:3 screen they are on. Their place is
           measured first (-2: FlashSlot_Draw, then Port_MenuClipPlace). The rest stays one page in the middle:
           its parts are often clips of their own that belong together (a frame and the digits in it, a keyboard
           and its letters, a monitor and the rows on it) and have to keep their places to one another. */
        return -2;
    }
    return PORT_MENU_NONE;
}

/* Whether a clip of a converted screen's movie is moving (sliding in or out): its position changed within the last
   10 draws of its screen. Then the clip is drawn whole, not cut at the 4:3 screen's edges (marker 0x2D, and 0x2E
   after it): the renderer's own test of a piece's motion (menu_piece_moving) cannot follow a plate drawn as several
   alike pieces, whose text then moved ahead of it uncut. Clips of the movie itself only; what they hold goes with
   them. */
#define MOVE_CLIPS 128
static struct { char name[48]; int screen, x, lastDraw, moved; } sMoveClip[MOVE_CLIPS];
int Port_MenuClipMoving(const char *name, int x, int top) {
    int i, freeSlot = -1, oldest = 0;
    if (sMenuScreen == 0 || !top) {
        return 0;
    }
    for (i = 0; i < MOVE_CLIPS; i++) {
        if (sMoveClip[i].screen == sMenuScreen && strcmp(sMoveClip[i].name, name) == 0) {
            break;
        }
        if (sMoveClip[i].screen == 0 && freeSlot < 0) {
            freeSlot = i;
        }
        if (sMoveClip[i].lastDraw < sMoveClip[oldest].lastDraw) {
            oldest = i;
        }
    }
    if (i == MOVE_CLIPS) {
        i = freeSlot >= 0 ? freeSlot : oldest;
        snprintf(sMoveClip[i].name, sizeof(sMoveClip[i].name), "%s", name);
        sMoveClip[i].screen = sMenuScreen;
        sMoveClip[i].x = x;
        sMoveClip[i].moved = -1000;
    } else if (sMoveClip[i].x != x) {
        sMoveClip[i].x = x;
        sMoveClip[i].moved = sMoveDraw;
    }
    sMoveClip[i].lastDraw = sMoveDraw;
    return sMoveDraw - sMoveClip[i].moved <= 1; /* (cut as soon as it stops: a plate parked off the screen goes) */
}

/* The clip being drawn moves (1) or not (0); returns what was before. */
static int sMenuFreeNow;
int Port_MenuFree(int on) {
    int prev = sMenuFreeNow;
    if (on != sMenuFreeNow) {
        sMenuFreeNow = on;
        Port_GsMarker(on ? 0x2D : 0x2E);
    }
    return prev;
}
