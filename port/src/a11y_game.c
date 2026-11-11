/*
 * Accessibility: what the screen reader says about the game's screens. Called once per blank from Port_VBlank
 * (plat_stub.c) on the game's thread; it reads the game's state and passes what changed to the speaker
 * (gs/a11y.c), which is off unless its library is there.
 *
 * It only reads: nothing here writes the game's memory or draws a random number, so the game runs the same with it
 * on or off and an online session stays in step. Its own variables are not game state (port/tools/make_state.py
 * lists the port files that are). Blanks being run again after a rollback say nothing.
 *
 * The game's structures are declared here only as far as they are read, at their offsets (the whole ones are in
 * include/; the port's files do not include the game's headers).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "plat_songs.h"
#include "plat_stages.h"

#if (defined(__x86_64__) || defined(__aarch64__)) && defined(__clang__)
#undef __ptr32 /* the Windows headers of mingw define these two away */
#undef __uptr
#define GAME_PTR *__ptr32 __uptr
#else
#define GAME_PTR *
#endif
/* The game's layouts. On Apple arm64 only where its pointers are 4 bytes (not in ptr32.py's libclang 18 parse there,
   which ignores __ptr32; the compile after it checks them); everywhere else always, so that a build whose game
   pointers are not 4 bytes stops here instead of reading the game's structures wrongly */
#if defined(__APPLE__) && defined(__aarch64__)
#define LAYOUT_ASSERT(c, m) _Static_assert(sizeof(void GAME_PTR) != 4 || (c), m)
#else
#define LAYOUT_ASSERT(c, m) _Static_assert(c, m)
#endif

#define A11Y_INTERRUPT 1 /* gs/a11y.c */
#define A11Y_TEXT 1024
extern int PortA11y_Begin(void);
extern const char *PortA11y_Tr(const char *label) __attribute__((format_arg(1))); /* checked like its label */
#define tr(label) PortA11y_Tr(label) /* gs/a11y.c: a label in the language of a11y_<code>.txt, when it has one */
extern const char *PortA11y_TrSense(const char *sense, const char *label) __attribute__((format_arg(2)));
#define trc(sense, label) PortA11y_TrSense(sense, label) /* a label some language says another way in this sense
                                                            (key "label|sense" of a11y_<code>.txt) */
extern void PortA11y_Say(const char *text, int flags);
extern void PortA11y_Note(const char *text);
extern int PortA11y_Utf8(char *out, int cap, const uint16_t *str);
extern int gPortResim; /* gs/state.c: blanks being run again */
extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_AudioRefresh(void);                /* gs/snd_adx.c: the streams take their volume again */

/* include/menu/menu_a.h: MenuProgress; the modes are in docs/systems/menu_overlay.md */
extern struct { int unk0[2]; int loadPack, loadRes, loadSprites; int flags; int mode; } GAME_PTR gProgress;
#define MODE_TITLE 1
#define MODE_MAIN_MENU 4
#define MODE_HIST_SEL 6 /* the story mode's saga select */
#define MODE_EPISODES 7 /* the saga's episode list */

/* The game's progress mode, for the port's own lines over the picture (gs/ui.cpp: the update's line shows over the
   main menu, 4, and the port's screens, 80, Tenkaichi Budokai Online); -1 before the menus exist. */
int Port_GameMode(void) {
    return gProgress != NULL ? gProgress->mode : -1;
}

/* include/menu/menu_a.h: Title (src/menu/menu_a_c.c) */
typedef struct {
    char unk0[0xA8];
    int flags;     /* 0xA8: TITLE_ */
    int cursor;    /* 0xAC: 0 "New Game", 1 "Continue" */
    int timer;     /* 0xB0 */
    int itemCount; /* 0xB4: 2 with a saved game */
    int state;     /* 0xB8: 0 intro, 1 "press start", 2 the two choices */
    int mcState;   /* 0xBC: not 0 while a memory card flow runs (its dialog speaks) */
} A11yTitle;
LAYOUT_ASSERT(offsetof(A11yTitle, mcState) == 0xBC, "Title");
extern A11yTitle GAME_PTR gTitle;
#define TITLE_CHOSEN 1
#define TITLE_LEAVING 2

/* include/sys/dialog.h: Dialog, the memory card window (src/sys/dialog.c) */
typedef struct {
    uint32_t GAME_PTR titleTbl; /* 0x00: the window's own text file (layout 1: the title and the body) */
    uint32_t GAME_PTR msgTbl;   /* 0x04: the caller's text file (layout 0) */
    char unk08[8];
    int windowFlags;            /* 0x10: flash[0].flags, bit 1: fully open, the choices can be used */
    char unk14[0x64 - 0x14];
    int flags;                  /* 0x64: DIALOG_FLAG_ */
    int cursor;                 /* 0x68: 0 "Yes", 1 "No" */
    int msgIdx;                 /* 0x6C */
    int defCursor;              /* 0x70 */
    int size;                   /* 0x74 */
    int layout;                 /* 0x78 */
    int titleIdx;               /* 0x7C */
    int unk80;
    int choices;                /* 0x84: not 0: "Yes" and "No" are shown */
} A11yDialog;
LAYOUT_ASSERT(offsetof(A11yDialog, choices) == 0x84, "Dialog");
extern A11yDialog GAME_PTR gDialog;
#define DIALOG_FLAG_OPEN 4

/* include/menu/menu_a.h: MainMenu (src/menu/menu_a.c). Its "subtitles" (0x0C) are the guides' mouth data, not
   text: the line about the item is in the message window (msg_window). */
typedef struct {
    char unk0[0x108];
    int flags;                   /* 0x108: MAINMENU_ */
    int cursor;                  /* 0x10C: row 0..3 */
    int timer;                   /* 0x110 */
    int unk114;
    int items[12];               /* 0x118: item ids in list order (MAINMENU_ITEM_MAX: Tenkaichi Budokai Online too) */
    int itemCount;               /* 0x148 */
    int top;                     /* 0x14C: list index of the plate above the first row (a ring) */
} A11yMainMenu;
LAYOUT_ASSERT(offsetof(A11yMainMenu, top) == 0x14C, "MainMenu");
extern A11yMainMenu GAME_PTR gMainMenu;
#define MAINMENU_CHOSEN 1
#define MAINMENU_LEAVING 2

/* include/battle/view_a.h: MsgWin (src/battle/view_a_b.c), the window that shows one line of text: the main
   menu's line about the item, the duel settings' help. */
typedef struct {
    void GAME_PTR text;      /* 0x00: NULL: drawn without text */
    char unk04[0x98 - 0x04];
    uint16_t GAME_PTR str;   /* 0x98: box (0x44) .draw.str (+0x54), the line shown (TextBox_AttachLine) */
} A11yMsgWin;
LAYOUT_ASSERT(offsetof(A11yMsgWin, str) == 0x98, "MsgWin");
extern A11yMsgWin GAME_PTR gMsgWin;

/* include/menu/menu_d.h: CharSel (src/menu/menu_c_e.c), the character, stage and music select. */
typedef struct {
    int col;          /* 0x00: cursor in the character grid (7 columns) */
    int row;          /* 0x04 */
    int form;         /* 0x08: chip of the form reel */
    int customCol;    /* 0x0C: cursor in the saved custom characters (7 x 2) */
    int customRow;    /* 0x10 */
    int customPlate;  /* 0x14: item set, 0 none */
    int colorPlate;   /* 0x18: costume */
    char unk1C[0x68 - 0x1C];
    int flags;        /* 0x68: CHARSEL_SIDE_ */
    int chara;        /* 0x6C: character under the cursor, line of the name files */
    int state;        /* 0x70: CHARSEL_ST_ */
    char unk74[0xF0 - 0x74];
} A11yCharSide;
LAYOUT_ASSERT(offsetof(A11yCharSide, chara) == 0x6C && offsetof(A11yCharSide, state) == 0x70, "CharSelSide");
LAYOUT_ASSERT(sizeof(A11yCharSide) == 0xF0, "CharSelSide");
#define CHARSEL_SIDE_CUSTOM 0x80
#define CHARSEL_ST_GRID 0
#define CHARSEL_ST_FORM 1
#define CHARSEL_ST_ITEMSET 2
#define CHARSEL_ST_ITEMS 3
#define CHARSEL_ST_ITEMINFO 4
#define CHARSEL_ST_COLOR 5
#define CHARSEL_ST_CUSTOM 6
#define CHARSEL_ST_DONE 7
#define CHARSEL_ST_STAGE 8
#define CHARSEL_ST_BGM 9

typedef struct {
    int col;          /* 0x00: cursor in the stage grid; CHARSEL_STAGE_BGM_COL is the music entry */
    int row;          /* 0x04 */
    char unk08[0x3C - 0x08];
    int stage;        /* 0x3C: stage id under the cursor (kept on the music entry) */
    int state;        /* 0x40: CHARSEL_ST_STAGE or CHARSEL_ST_BGM */
    char unk44[0x4C - 0x44];
    int bgm;          /* 0x4C: index in the music list */
    int bgmPlaying;   /* 0x50: index of the entry that plays */
} A11yCharStage;
LAYOUT_ASSERT(offsetof(A11yCharStage, state) == 0x40 && offsetof(A11yCharStage, bgmPlaying) == 0x50, "CharSelStage");
#define CHARSEL_STAGE_BGM_COL 6

typedef struct {
    int id;
    int formCount;    /* 0x04: forms on the reel, 0 = the cell is the one character */
    int form[7];
} A11yChrCell;
LAYOUT_ASSERT(sizeof(A11yChrCell) == 0x24, "MChrCell");
#define MCHR_COLS 7
#define MCHR_RANDOM 0xA1 /* to MCHR_EMPTY 0xA4: random, locked, custom characters, empty */

typedef struct {
    char unk0[0x28];
    uint32_t GAME_PTR nameText;     /* 0x28: section 43, line `chara`: the name */
    uint32_t GAME_PTR formText;     /* 0x2C: section 44, line `chara`: the form */
    char unk30[0x2FC - 0x30];
    int flags;                      /* 0x2FC: CHARSEL_ */
    char unk300[0x4F8 - 0x300];
    A11yCharSide GAME_PTR side[2];  /* 0x4F8 */
    char unk500[0x554 - 0x500];
    A11yCharStage GAME_PTR stage;   /* 0x554 */
    char unk558[0x560 - 0x558];
    A11yChrCell GAME_PTR cells[2];  /* 0x560: each side's grid */
    int cellCount[2];               /* 0x568 */
    char unk570[0x33D8 - 0x570];
    int customCount[2];             /* 0x33D8: saved custom characters */
    char unk33E0[0x37D0 - 0x33E0];
    int rows[2];                    /* 0x37D0: rows of each side's grid */
    int customRows[2];              /* 0x37D8: item sets offered */
    int colorCount[2];              /* 0x37E0: costumes */
    int stageRows;                  /* 0x37E8 */
    int players;                    /* 0x37EC: 1 = a pad a side; else pad 0 chooses side `turn` */
    char unk37F0[0x3800 - 0x37F0];
    int turn;                       /* 0x3800 */
    int unk3804;
    int GAME_PTR bgmIds;            /* 0x3808: the music list (values: kSongNames, then the markers below) */
    int stageCount;                 /* 0x380C */
    int bgmCount;                   /* 0x3810 */
} A11yCharSel;
LAYOUT_ASSERT(offsetof(A11yCharSel, flags) == 0x2FC && offsetof(A11yCharSel, side) == 0x4F8, "CharSel");
LAYOUT_ASSERT(offsetof(A11yCharSel, stage) == 0x554 && offsetof(A11yCharSel, cells) == 0x560, "CharSel");
LAYOUT_ASSERT(offsetof(A11yCharSel, customCount) == 0x33D8 && offsetof(A11yCharSel, rows) == 0x37D0, "CharSel");
LAYOUT_ASSERT(offsetof(A11yCharSel, players) == 0x37EC && offsetof(A11yCharSel, turn) == 0x3800, "CharSel");
LAYOUT_ASSERT(offsetof(A11yCharSel, bgmIds) == 0x3808 && offsetof(A11yCharSel, bgmCount) == 0x3810, "CharSel");
extern A11yCharSel GAME_PTR gCharSel;
#define CHARSEL_STARTED 2
#define CHARSEL_STAGE_PHASE 4
#define CHARSEL_LEAVING 8
#define CHARSEL_BGM_RANDOM 0x18
#define CHARSEL_STAGE_RANDOM 0x23

/* include/menu/menu_l.h LSelEntry = menu_j.h ESelEntry: the fighter being chosen in the one-fighter selects. */
typedef struct {
    int col;          /* 0x00: grid column */
    int row;          /* 0x04 */
    int form;         /* 0x08: index in the cell's forms */
    int recCol, recRow;
    int custom;       /* 0x14: item set, 0 none */
    int costume;      /* 0x18 */
    char unk1C[0x30 - 0x1C];
} A11ySelEntry;
LAYOUT_ASSERT(offsetof(A11ySelEntry, custom) == 0x14 && sizeof(A11ySelEntry) == 0x30, "LSelEntry");
/* SOLOSEL_STEP_ = ESEL_STEP_ */
#define PICK_STEP_CHARA 0
#define PICK_STEP_FORM 1
#define PICK_STEP_CUSTOM 2
#define PICK_STEP_PANEL 3
#define PICK_STEP_HELP 4
#define PICK_STEP_COLOR 5

/* include/menu/menu_l.h: SoloSel (src/menu/menu_l_b.c), one fighter for modes 15, 18, 21, 25 and 29. */
typedef struct {
    char unk0[0x14];
    uint32_t GAME_PTR nameText;    /* 0x14: section 28 */
    uint32_t GAME_PTR formText;    /* 0x18: section 29 */
    char unk1C[0x18C - 0x1C];
    int flags;                     /* 0x18C: SOLOSEL_ */
    char unk190[0x1A4 - 0x190];
    A11ySelEntry entry;            /* 0x1A4: state.entry */
    char unk1D4[0x210 - 0x1D4];
    int image;                     /* 0x210: state.image, the character whose name is shown */
    int step;                      /* 0x214: state.step */
    char unk218[0x220 - 0x218];
    int gridCount;                 /* 0x220 */
    A11yChrCell GAME_PTR grid;     /* 0x224 */
    char unk228[0x1960 - 0x228];
    int rows;                      /* 0x1960 */
    int customCount;               /* 0x1964 */
    int colorCount;                /* 0x1968 */
} A11ySoloSel;
LAYOUT_ASSERT(offsetof(A11ySoloSel, flags) == 0x18C && offsetof(A11ySoloSel, entry) == 0x1A4, "SoloSel");
LAYOUT_ASSERT(offsetof(A11ySoloSel, step) == 0x214 && offsetof(A11ySoloSel, grid) == 0x224, "SoloSel");
LAYOUT_ASSERT(offsetof(A11ySoloSel, colorCount) == 0x1968, "SoloSel");
extern A11ySoloSel GAME_PTR gSoloSel; /* NULL outside the screen */
#define SOLOSEL_STARTED 2
#define SOLOSEL_LEAVING 0x10

/* include/menu/menu_j.h: ESel (src/menu/menu_i_d.c), the tournament's entrants (mode 34). */
typedef struct {
    char unk0[0x14];
    uint32_t GAME_PTR nameText;    /* 0x14: section 30 */
    uint32_t GAME_PTR formText;    /* 0x18: section 31 */
    char unk1C[0x1D8 - 0x1C];
    int flags;                     /* 0x1D8: ESEL_ */
    char unk1DC[0x1F8 - 0x1DC];
    A11ySelEntry entry[8];         /* 0x1F8: state.entry, one per entrant */
    char unk378[0x3B4 - 0x378];
    int image;                     /* 0x3B4: state.image */
    int cur;                       /* 0x3B8: state.cur, the entrant being chosen */
    int step;                      /* 0x3BC: state.step */
    char unk3C0[0x3C8 - 0x3C0];
    int gridCount;                 /* 0x3C8 */
    A11yChrCell GAME_PTR grid;     /* 0x3CC */
    char unk3D0[0x1B08 - 0x3D0];
    int rows;                      /* 0x1B08 */
    int customCount;               /* 0x1B0C */
    int colorCount;                /* 0x1B10 */
} A11yEntrySel;
LAYOUT_ASSERT(offsetof(A11yEntrySel, flags) == 0x1D8 && offsetof(A11yEntrySel, entry) == 0x1F8, "ESel");
LAYOUT_ASSERT(offsetof(A11yEntrySel, image) == 0x3B4 && offsetof(A11yEntrySel, step) == 0x3BC, "ESel");
LAYOUT_ASSERT(offsetof(A11yEntrySel, grid) == 0x3CC && offsetof(A11yEntrySel, colorCount) == 0x1B10, "ESel");
extern A11yEntrySel GAME_PTR gEntrySel; /* NULL outside the screen */
#define ESEL_LEAVING 2
#define ESEL_STARTED 4
#define ESEL_ENTRIES 8
#define TOUR_ENTRY_NUM (0x8C / 4) /* gProgress, in ints: entrants the player chooses (menu_j.h TourSession) */

/* include/menu/menu_g.h: DuelMenu (src/menu/menu_g_c.c). The line about the item is in the message window. */
typedef struct {
    char unk0[0x10C];
    int flags;      /* 0x10C: DUELMENU_ */
    int sel[5];     /* 0x110: the cursor of each level */
    int timer;      /* 0x124 */
    int level;      /* 0x128: DUEL_LEVEL_ */
    char unk12C[0x138 - 0x12C];
    int column;     /* 0x138: which of the two values of setting 3 the cursor is on */
    int value[2];   /* 0x13C */
    int valueCount; /* 0x144 */
    int startState; /* 0x148: not 0 once a battle type is chosen */
    char unk14C[0x184 - 0x14C];
    int resetState; /* 0x184: not 0 while the "restore the default settings?" question runs (a Dialog) */
} A11yDuelMenu;
LAYOUT_ASSERT(offsetof(A11yDuelMenu, level) == 0x128 && offsetof(A11yDuelMenu, valueCount) == 0x144, "DuelMenu");
LAYOUT_ASSERT(offsetof(A11yDuelMenu, resetState) == 0x184, "DuelMenu");
extern A11yDuelMenu GAME_PTR gDuelMenu;
#define DUELMENU_CHOSEN 1
#define DUELMENU_LEAVING 2
#define DUELMENU_STARTED 4
#define DUEL_LEVEL_TOP 0
#define DUEL_LEVEL_TYPE 1
#define DUEL_LEVEL_DP 2
#define DUEL_LEVEL_SETTING 3
#define DUEL_LEVEL_VALUE 4
#define DUEL_SETTINGS 6 /* five settings and "restore the defaults" */

/* include/sys/save.h: SaveData. rule[] holds the duel settings: time, CPU level, referee, CPU transformation for
   each side (two words) and stage destruction; each is the row of its value list. */
typedef struct {
    unsigned char cleared;        /* 0x0 */
    unsigned char rank;           /* 0x1: the best rank, a column of the rank strip (kRankLetter) */
    unsigned char hours, minutes, seconds; /* 0x2: the best time (src/menu/menu_o_d.c MisSel_Draw) */
    char unk5[3];
    int score;                    /* 0x8: shown * 100 */
} A11yMissionRecord;               /* include/menu/menu_p.h: PSaveMission; include/menu/menu_o.h: UoMissionRec */
/* A rank's letter as the strips draw it (menu_p_b.c; menu_o_d.c MisSel_SetRank; column 0 is a dash). */
static const char kRankLetter[5] = {'-', 'C', 'B', 'A', 'Z'};

typedef struct {
    int defeated;                    /* opponents beaten */
    int score;                       /* shown * 100 */
    unsigned char rank, hours, minutes, seconds;
} A11ySurvRecord;                    /* include/menu/menu_s.h: SSaveSurv */

typedef struct {
    int flags;  /* 0x0: MSLOT_ (include/menu/menu_c.h) */
    int val[3]; /* 0x4: bit n is the sub menu's item n: [0] listed, [1] cleared, [2] new */
} A11ySaveSlot;                    /* include/sys/save.h: SaveSlot */

typedef struct {
    int chara;
    int score;                       /* shown * 100 */
    unsigned char cleared;           /* the ladder was finished */
    unsigned char unk9[3];
} A11ySimRank;                       /* include/menu/menu_r.h: SimRankRec */

typedef struct {
    char unk0[0xC];
    int histLevel;                /* 0x00C: the level chosen in the history menu, 0..2 */
    A11ySaveSlot slot[9];         /* 0x010: one per history sub menu */
    char unkA0[0x210 - 0xA0];
    A11ySimRank simRank[10];      /* 0x210: the Sim Dragon ranking */
    char unk288[0x28C - 0x288];
    A11yMissionRecord mission[100]; /* 0x28C: Mission 100 */
    A11ySurvRecord surv[5];       /* 0x73C: the best result of each survival course */
    char unk778[0xA08 - 0x778];
    int tourOpen;                /* 0xA08: bit n: tournament n is the open one (TOUR_SAVE_INVITE_MASK, menu_j.h) */
    int tourHour;                 /* 0xA0C: the World Tour's clock, 0..23 */
    int tourLevel;                /* 0xA10: the open tournament's level, 0..2 */
    char unkA14[0xC34 - 0xA14];
    int rule[6];                  /* 0xC34 */
    char unkC4C[0xE0C - 0xC4C];
    int trainClear[3];            /* 0xE0C: bit n: lesson n of the training class cleared (menu_i.h MSaveTrain) */
    char unkE18[0x16A8 - 0xE18];
    int bgmVolume;                /* 0x16A8: 0..9 */
    int seVolume;                 /* 0x16AC: 0..9; the effects and the voices (sys/adx.c channels 2-5) */
    char unk16B0[0x2EC8 - 0x16B0];
    unsigned char item[350];      /* 0x2EC8: SAVE_ITEM_ bits per item, 0-based */
    char unk3026[0x3028 - 0x3026];
    int money;                    /* 0x3028 */
} A11ySave;
LAYOUT_ASSERT(sizeof(A11yMissionRecord) == 0xC && sizeof(A11ySaveSlot) == 0x10 && offsetof(A11ySave, slot) == 0x10 &&
                   offsetof(A11ySave, mission) == 0x28C && sizeof(A11ySurvRecord) == 0xC &&
                   offsetof(A11ySave, surv) == 0x73C && sizeof(A11ySimRank) == 0xC && offsetof(A11ySave, simRank) == 0x210 &&
                   offsetof(A11ySave, tourLevel) == 0xA10 && offsetof(A11ySave, rule) == 0xC34 &&
                   offsetof(A11ySave, trainClear) == 0xE0C &&
                   offsetof(A11ySave, seVolume) == 0x16AC && offsetof(A11ySave, item) == 0x2EC8 &&
                   offsetof(A11ySave, money) == 0x3028, "SaveData");
#define SAVE_ITEM_OWNED 1
#define SAVE_ITEMS 350
extern A11ySave GAME_PTR gSaveData;

/* Labels the game draws as pictures (the plates of pzs3us1/00449.bin and the title's). All the labels of this file
   are in English and said through tr(): the other languages are in a11y_<code>.txt (gs/a11y.c; Spanish in
   port/lang/a11y_es.txt, which has every label here and must get the new ones). */
static const char *const kMainMenuItems[11] = { /* by item id: src/menu/menu_a.c, MainMenu_Init */
    "Dragon History", "Ultimate Battle", "Dragon World Tour", "Duel", "Dragon Net Battle", "Evolution Z",
    "Ultimate Training", "Data Center", "Character Reference", "Options", "Shenron",
};
static const char *const kTitleItems[2] = {"New Game", "Continue"};
static const char *const kYesNo[2] = {"Yes", "No"};
/* By stage id; the English as in gs/namefont.c (kStageNames, what the name pictures say). */
static const char *const kStageNames[CHARSEL_STAGE_RANDOM + 1] = {
    "Wasteland - Noon", "Rocky Area - Noon", "Planet Namek", "Dying Namek", "World Tournament Stage - Noon",
    "Kami's Lookout", "Cell Games Arena - Evening", "Supreme Kai's World", "Hyperbolic Time Chamber",
    "City Ruins - Noon", "Mountain Road - Noon", "Islands", "Kame House", "Planet - Night", "Glacier",
    "Ruined Earth", "Outer Space", "Penguin Village", "Hell", "Desert - Noon", "King's Castle", "Muscle Tower",
    "Mount Paozu", "Wasteland - Evening", "Wasteland - Night", "Rocky Area - Evening", "Rocky Area - Night",
    "World Tournament Stage - Evening", "Cell Game - Noon", "City Ruins - Evening", "City Ruins - Night",
    "Desert - Evening", "Desert - Night", "Mountain Road - Evening", "Planet - Evening", "Random",
};
/* By music-list value 0x00..0x13 (gs/namefont.c, kSongNames); titles, the same in both languages. */
static const char *const kSongNames[20] = {
    "The Meteor", "Vital Burner", "Innocent World", "After The Fire", "Sweet Vibration", "Survive", "Heat Capacity",
    "Overture", "Shine", "Power Scale", "Edge of Spirit", "Caution !", "Menace", "Hot Soul", "High and Scream",
    "Shootout In Meteor", "Dynamite Battle", "Burnin' Up", "Wild Rush", "Evolution",
};
/* The grid's marker cells, when the name file has no line for them. */
static const char *const kCellMarks[4] = {
    "Random", "Locked", "Custom characters", "Empty",
};
/* The duel menu (src/menu/menu_g_c.c, DuelMenu_ClipGoto): its plates by level and row, then the settings' values
   by row of their lists (DuelMenu_SetupValue). */
static const char *const kDuelTop[4] = {
    "1P vs CPU", "1P vs 2P", "CPU vs CPU", "Battle Settings",
};
static const char *const kDuelType[3] = {
    "Single Battle", "Team Battle", "DP Battle",
};
static const char *const kDuelDp[3] = {"10 DP", "15 DP", "20 DP"};
static const char *const kDuelSettings[DUEL_SETTINGS] = {
    "Duel Time", "CPU Level", "Referee", "Transformation", "Stage Destruction", "Restore Defaults",
};
static const char *const kDuelTime[5] = {
    "60", "90", "180", "240", "Infinite",
};
static const char *const kDuelCpu[5] = {
    "Very Weak", "Weak", "Normal", "Strong", "Very Strong",
};
static const char *const kDuelReferee[7] = {
    "Ox-King", "Videl", "Supreme Kai", "Shenron", "Announcer 1", "Announcer 2", "Announcer 3",
};

static int sAfterHeading; /* a screen's name or a window's text was just said */

/* A screen's name or a window's text: it cuts what was being said. */
static void heading(const char *text) {
    PortA11y_Say(text, A11Y_INTERRUPT);
    sAfterHeading = 1;
}

/* The flags for an item: the first one after a heading is queued behind it, the next ones cut the last item. */
static int follow(void) {
    int flags = sAfterHeading ? 0 : A11Y_INTERRUPT;

    sAfterHeading = 0;
    return flags;
}

/* Appends ", 2 of 5" when there is more than one. */
static void add_position(char *text, int cap, int index, int count) {
    size_t len = strlen(text);

    if (count > 1 && index >= 0 && index < count) {
        snprintf(text + len, cap - len, tr(", %d of %d"), index + 1, count);
    }
}

/* "label, 2 of 5". */
static void say_item(const char *label, int index, int count, int flags) {
    char text[256];

    snprintf(text, sizeof(text), "%s", label);
    add_position(text, sizeof(text), index, count);
    PortA11y_Say(text, flags);
}

/* Line `n` of one of the game's text files, as UTF-8 into out: the file is a table of byte offsets (entry n + 1;
   entry 0 is the number of lines) to UTF-16 strings that begin with U+FEFF (src/sys/dialog.c DIALOG_TEXT,
   src/battle/view_b.c TextBox_AttachLine). The strings come after the table, so the table ends where the first
   one starts: an entry from there on is not read. Empty when it does not look like such a line. Returns the
   length (text_addr: the string in the game's memory, NULL then). */
static uint16_t GAME_PTR text_addr(uint32_t GAME_PTR file, int n) {
    uint32_t ofs;
    uint16_t GAME_PTR str;

    if (file == NULL || n < 0 || n >= 0x1000 || (uint32_t)n + 1 >= file[1] >> 2) {
        return NULL;
    }
    ofs = file[n + 1] >> 2 << 2;
    if (ofs == 0 || ofs >= 0x200000) {
        return NULL;
    }
    str = (uint16_t GAME_PTR)((uint8_t GAME_PTR)file + ofs);
    return str[0] == 0xFEFF ? str : NULL;
}

static int text_line(char *out, int cap, uint32_t GAME_PTR file, int n) {
    uint16_t GAME_PTR str = text_addr(file, n);

    out[0] = '\0';
    return str != NULL ? PortA11y_Utf8(out, cap, (const uint16_t *)str) : 0;
}

/* The memory card window: its text when it opens or changes, then the choice under the cursor. */
static void dialog(void) {
    static char said[A11Y_TEXT], seen[A11Y_TEXT];
    static int cursor = -1;
    char text[A11Y_TEXT], body[A11Y_TEXT];
    A11yDialog GAME_PTR d = gDialog;

    if (d == NULL || !(d->flags & DIALOG_FLAG_OPEN)) {
        said[0] = seen[0] = '\0';
        cursor = -1;
        return;
    }
    if (d->layout == 0) {
        text_line(text, sizeof(text), d->msgTbl, d->msgIdx);
    } else {
        text_line(text, sizeof(text), d->titleTbl, d->titleIdx);
        if (text_line(body, sizeof(body), d->titleTbl, d->msgIdx) > 0 && strcmp(body, text) != 0) {
            snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s", text[0] != '\0' ? ". " : "", body);
        }
    }
    if (strcmp(text, seen) != 0) { /* a window being set up can show a line for one blank: wait a blank */
        memcpy(seen, text, sizeof(seen));
        return;
    }
    if (text[0] != '\0' && strcmp(text, said) != 0) {
        memcpy(said, text, sizeof(said));
        heading(text);
        cursor = -1;
    }
    if (d->choices != 0 && (d->windowFlags & 2) && d->cursor != cursor && (d->cursor & ~1) == 0) {
        say_item(tr(kYesNo[d->cursor]), d->cursor, 2, follow());
        cursor = d->cursor;
    }
}

/* The title: "press start", then "New Game" / "Continue". */
static void title(void) {
    static int state = -1, cursor = -1;
    A11yTitle GAME_PTR t = gTitle;

    if (gProgress->mode != MODE_TITLE || t == NULL) {
        state = cursor = -1;
        return;
    }
    if (t->mcState != 0 || (t->flags & (TITLE_CHOSEN | TITLE_LEAVING))) {
        cursor = -1; /* (said again when the card's window closes without leaving) */
        return;
    }
    if (t->state == 1 && state != 1) {
        heading(tr("Press START button"));
    }
    if (t->state == 2 && t->cursor != cursor && t->cursor >= 0 && t->cursor < 2 && t->itemCount >= 1) {
        say_item(tr(kTitleItems[t->cursor]), t->cursor, t->itemCount, follow());
        cursor = t->cursor;
    }
    state = t->state;
}

/* The main menu: its name when it opens, then the item under the cursor (the guide's line about it is in the
   message window). */
static void main_menu(void) {
    static int open, item = -1;
    A11yMainMenu GAME_PTR m = gMainMenu;
    int index, cur;

    if (gProgress->mode != MODE_MAIN_MENU || m == NULL) {
        open = 0;
        item = -1;
        return;
    }
    if (m->itemCount < 1 || m->itemCount > 12 || m->top < 0 || m->cursor < 0 ||
        (m->flags & (MAINMENU_CHOSEN | MAINMENU_LEAVING))) {
        return;
    }
    index = (m->top + m->cursor + 1) % m->itemCount;
    cur = m->items[index];
    if (cur < 0 || cur > 11) {
        return;
    }
    if (!open) {
        heading(tr("Main Menu"));
        open = 1;
    }
    if (cur != item) {
        say_item(cur == 11 ? "Tenkaichi Budokai Online" : tr(kMainMenuItems[cur]), index, m->itemCount, follow());
        item = cur;
    }
}

/* include/menu/menu_p.h: UbMenu (src/menu/menu_p_c.c), the menu of mode 13. Its four plates lead to modes 20, 14,
   17 and 24 (Ub_Main, src/menu/menu_o_c.c); the third stays shut, and the cursor skips it, until 30 missions are
   cleared (gUbMenuPlate3Open, UbMenu_Input). The guides say each plate's name; the plates are pictures. */
typedef struct {
    char unk0[0x84];
    int flags; /* 0x84: UBMENU_ */
    int cur;   /* 0x88: plate, 0..3 */
} A11yUbMenu;
LAYOUT_ASSERT(offsetof(A11yUbMenu, cur) == 0x88, "UbMenu");
extern A11yUbMenu GAME_PTR gUbMenu;
extern int gUbMenuPlate3Open; /* src/menu/menu_p_c.c: the third plate is open */
#define UBMENU_CHOSEN 1
#define UBMENU_LEAVING 2
#define UBMENU_PLATES 4
#define UBMENU_SHUT_PLATE 2

/* As the mod's plates say them; its shut third plate shows "???", and its open name is not seen yet. */
static const char *const kUbMenuItems[UBMENU_PLATES] = {
    "Sim Dragon", "Mission 100", "Survival", "Disc Fusion",
};

static void ub_menu(void) {
    static int item = -1;
    A11yUbMenu GAME_PTR m = gUbMenu;
    int count = UBMENU_PLATES, index;

    if (m == NULL) {
        item = -1;
        return;
    }
    if (m->cur < 0 || m->cur >= UBMENU_PLATES || (m->flags & (UBMENU_CHOSEN | UBMENU_LEAVING))) {
        return;
    }
    index = m->cur;
    if (gUbMenuPlate3Open == 0) {
        count--;
        index -= index > UBMENU_SHUT_PLATE;
    }
    if (m->cur != item) {
        say_item(tr(kUbMenuItems[m->cur]), index, count, follow());
        item = m->cur;
    }
}

/* The top menus of Evolution Z (mode 48, EvoTop: src/menu/menu_w_c.c) and of the Data Center (mode 53, DcMenu:
   src/menu/menu_z_c.c): three plates each, drawn as pictures, with a guide who speaks. Their work structures are
   locals of their Run functions, so nothing points at them: each one's Draw gives its cursor here every frame it
   draws (PortA11y_Plate), and plates() says the plate when it changes. The names are descriptions of where each
   plate leads (EvoTop_Input: the customising screen, the shop, the guide's explanation; include/menu/menu_z.h:
   password, character list, replay), not yet checked against the plates' pictures. */
#define PLATE_SCREENS 2
#define PLATE_COUNT 3
static const char *const kPlateNames[PLATE_SCREENS][PLATE_COUNT] = {
    {"Customize characters", "Item shop", "About Evolution Z"},
    {"Passwords", "Saved characters", "Replays"},
};
static struct {
    int cursor, busy, blank; /* as last given, and the blank it was given on (0: never) */
} sPlate[PLATE_SCREENS];

static int sOn;    /* (defined with the tick, below) */
static int sBlank;

void PortA11y_Plate(int screen, int cursor, int busy) {
    if (!sOn || gPortResim || screen < 0 || screen >= PLATE_SCREENS) {
        return;
    }
    sPlate[screen].cursor = cursor;
    sPlate[screen].busy = busy != 0;
    sPlate[screen].blank = sBlank;
}

static void plates(void) {
    static int item[PLATE_SCREENS] = {-1, -1};
    int s, cur;

    for (s = 0; s < PLATE_SCREENS; s++) {
        if (sPlate[s].blank == 0 || sBlank - sPlate[s].blank > 4) { /* (not drawn lately: the screen is gone) */
            item[s] = -1;
            continue;
        }
        cur = sPlate[s].cursor;
        if (sPlate[s].busy || cur < 0 || cur >= PLATE_COUNT || cur == item[s]) {
            continue;
        }
        say_item(tr(kPlateNames[s][cur]), cur, PLATE_COUNT, follow());
        item[s] = cur;
    }
}

/* include/menu/menu_o.h: DiscFusion (src/menu/menu_o_b.c), the Disc Fusion screen of mode 24: two plates, one per
   earlier game's disc, with text line 0x15A + disc (0x15C + disc once that disc was read: DiscFusion_Init) of the
   screen's own text. Its disc swap asks through the Dialog, read by dialog(). */
typedef struct {
    char unk0[0x10];
    uint32_t GAME_PTR text; /* 0x010: section 12 */
    char unk14[0x1E0 - 0x14];
    int flags;              /* 0x1E0: UBRES_ */
    int cursor;             /* 0x1E4: cursor[0], the plate */
    char unk1E8[0x204 - 0x1E8];
    int dlgStep;            /* 0x204: 0 no disc swap running */
    char unk208[0x25C - 0x208];
    int have[2];            /* 0x25C: that disc was read */
} A11yDiscFusion;
LAYOUT_ASSERT(offsetof(A11yDiscFusion, cursor) == 0x1E4 && offsetof(A11yDiscFusion, dlgStep) == 0x204 &&
                   offsetof(A11yDiscFusion, have) == 0x25C, "DiscFusion");
extern A11yDiscFusion GAME_PTR gDiscFusion;
#define DISCFUSION_DONE 1    /* UBRES_DONE */
#define DISCFUSION_LEAVING 2 /* UBRES_LEAVING */
#define DISCFUSION_STARTED 4 /* UBRES_STARTED */
#define DISCFUSION_DISCS 2
#define DISCFUSION_LINE 0x15A
#define DISCFUSION_READ_LINE 0x15C

static void disc_fusion(void) {
    static int item = -1;
    A11yDiscFusion GAME_PTR d = gDiscFusion;
    char line[256];
    int cur;

    if (d == NULL) {
        item = -1;
        return;
    }
    cur = d->cursor;
    if (!(d->flags & DISCFUSION_STARTED) || (d->flags & (DISCFUSION_DONE | DISCFUSION_LEAVING)) || d->dlgStep != 0 ||
        cur < 0 || cur >= DISCFUSION_DISCS || cur == item) {
        return;
    }
    if (text_line(line, sizeof(line), d->text, (d->have[cur] ? DISCFUSION_READ_LINE : DISCFUSION_LINE) + cur) > 0) {
        say_item(line, cur, DISCFUSION_DISCS, follow());
    }
    item = cur;
}

/* include/menu/menu_o.h: MisSel (src/menu/menu_o_d.c), the Mission 100 select of mode 14: pages of five missions
   (left and right turn the page). Confirm opens the mission's window, which shows its team kind, its rule and the
   opponents' faces; confirm again starts it. The names, the kinds and the rules are text lines 0xD8 + mission,
   0x148 + kind and 0x156 + rule of the screen's own text (MisSel_Draw). */
typedef struct {
    char unk0[0x18];
    int kind;   /* 0x18 */
    int rule;   /* 0x1C: 0 none */
    int opp[5]; /* 0x20: indices into the opponents */
} A11yMisDef;
LAYOUT_ASSERT(sizeof(A11yMisDef) == 0x34, "MisSelDef");

typedef struct {
    int chara; /* 0x00: MISSEL_NONE ends the team */
    char unk4[0x2C - 4];
} A11yMisOpp;
LAYOUT_ASSERT(sizeof(A11yMisOpp) == 0x2C, "MisSelOpp");

typedef struct {
    char unk0[0xC];
    uint32_t GAME_PTR text;       /* 0x0C: section 18 */
    char unk10[0x3E8 - 0x10];
    A11yMisDef GAME_PTR missions; /* 0x3E8: section 10 */
    A11yMisOpp GAME_PTR opps;     /* 0x3EC: section 11 */
    char unk3F0[0x4BC - 0x3F0];
    int flags;                    /* 0x4BC: MISSEL_ */
    int row;                      /* 0x4C0: cursor[0], the plate on the page */
    char unk4C4[0x4DC - 0x4C4];
    int level;                    /* 0x4DC: 0 choosing, 1 the mission's window is open */
    char unk4E0[0x4FC - 0x4E0];
    int pageCount;                /* 0x4FC */
    int page;                     /* 0x500 */
    int mission;                  /* 0x504: page * 5 + row, set when the window opens */
} A11yMisSel;
LAYOUT_ASSERT(offsetof(A11yMisSel, flags) == 0x4BC && offsetof(A11yMisSel, level) == 0x4DC &&
                   offsetof(A11yMisSel, mission) == 0x504, "MisSel");
extern A11yMisSel GAME_PTR gMisSel;
#define MISSEL_CHOSEN 1
#define MISSEL_LEAVING 2
#define MISSEL_STARTED 4
#define MISSEL_ROWS 5
#define MISSEL_NONE 0x3E7
#define MISSEL_NAME_LINE 0xD8
#define MISSEL_KIND_LINE 0x148
#define MISSEL_RULE_LINE 0x156
#define MISSION_COUNT 100
#define MISSEL_OPPS_MAX 1024 /* the game does not check a mission's opponent indices and the section's size is not
                                kept; one beyond this (the disc has 5 a mission at most) is not followed */

static void mission_select(void) {
    static int level = -1, mission = -1, page = -1;
    A11yMisSel GAME_PTR m = gMisSel;
    A11yMisDef GAME_PTR def;
    char text[512], line[256];
    int cur, n, i, opps = 0;

    if (m == NULL) {
        level = mission = page = -1;
        return;
    }
    if (!(m->flags & MISSEL_STARTED) || (m->flags & (MISSEL_CHOSEN | MISSEL_LEAVING)) || m->row < 0 ||
        m->row >= MISSEL_ROWS || m->page < 0 || m->page >= m->pageCount) {
        return;
    }
    cur = m->level == 0 ? m->page * MISSEL_ROWS + m->row : m->mission;
    if ((m->level != 0 && m->level != 1) || cur < 0 || cur >= MISSION_COUNT || (m->level == level && cur == mission)) {
        return;
    }
    n = 0;
    if (m->level == 0) {
        if (m->page != page && m->pageCount > 1) {
            n = snprintf(text, sizeof(text), tr("Page %d of %d: "), m->page + 1, m->pageCount);
        }
        n += snprintf(text + n, sizeof(text) - n, tr("Mission %d"), cur + 1);
        if (text_line(line, sizeof(line), m->text, MISSEL_NAME_LINE + cur) > 0) {
            n += snprintf(text + n, sizeof(text) - n, ", %s", line);
        }
        if (gSaveData != NULL && gSaveData->mission[cur].cleared) {
            n += snprintf(text + n, sizeof(text) - n, "%s", tr(", cleared"));
        }
        /* (the row's rank letter is a picture: MisSel_SetRank) */
        if (gSaveData != NULL && gSaveData->mission[cur].rank >= 1 && gSaveData->mission[cur].rank <= 4) {
            snprintf(text + n, sizeof(text) - n, tr(", rank %c"), kRankLetter[gSaveData->mission[cur].rank]);
        }
    } else {
        if (m->missions == NULL || m->opps == NULL) {
            return;
        }
        def = &m->missions[cur];
        if (text_line(line, sizeof(line), m->text, MISSEL_KIND_LINE + def->kind) > 0) {
            n += snprintf(text + n, sizeof(text) - n, "%s. ", line);
        }
        if (def->rule != 0 && text_line(line, sizeof(line), m->text, MISSEL_RULE_LINE + def->rule) > 0) {
            n += snprintf(text + n, sizeof(text) - n, "%s. ", line);
        }
        for (i = 0; i < 5 && (unsigned)def->opp[i] < MISSEL_OPPS_MAX && m->opps[def->opp[i]].chara != MISSEL_NONE; i++) {
            opps++;
        }
        n += snprintf(text + n, sizeof(text) - n, "%d %s", opps,
                      tr(opps == 1 ? "opponent" : "opponents"));
        /* (the best time and score are numbers drawn as pictures: MisSel_Draw) */
        if (gSaveData != NULL && gSaveData->mission[cur].score > 0) {
            snprintf(text + n, sizeof(text) - n, tr(". Best: time %d:%02d:%02d, score %d"),
                     gSaveData->mission[cur].hours, gSaveData->mission[cur].minutes,
                     gSaveData->mission[cur].seconds, gSaveData->mission[cur].score * 100);
        }
    }
    PortA11y_Say(text, follow());
    level = m->level;
    mission = cur;
    page = m->page;
}

/* include/menu/menu_j.h: TourMenu (src/menu/menu_j_b.c), the World Tour's menu of mode 33. One cursor per level
   (TOURMENU_LV_): the two top plates (0 the open tournament, for prizes, at the saved level and alone; 1 free play),
   the five tournaments, the tournament's explanation (the guide speaks it), the level and the number of entrants.
   TourMenu_Draw shows the two prizes of the tournament under the cursor and dims the shut ones in the top plate 0.
   The plates and the levels are pictures. */
typedef struct {
    char unk0[0x10];
    int prize[2][3]; /* 0x10: the two prize sums for each level */
    char unk28[0x64 - 0x28];
} A11yTourInfo;
LAYOUT_ASSERT(sizeof(A11yTourInfo) == 0x64, "TourInfo");

typedef struct {
    char unk0[0x108];
    int flags;                   /* 0x108: TOURMENU_ */
    int cursor[5];               /* 0x10C: one per level */
    char unk120[0x12C - 0x120];
    int level;                   /* 0x12C: TOURMENU_LV_ */
    char unk130[0x144 - 0x130];
    A11yTourInfo GAME_PTR info;  /* 0x144: one per tournament */
} A11yTourMenu;
LAYOUT_ASSERT(offsetof(A11yTourMenu, level) == 0x12C && offsetof(A11yTourMenu, info) == 0x144, "TourMenu");
extern A11yTourMenu GAME_PTR gTourMenu;
#define TOURMENU_DONE 1
#define TOURMENU_LEAVING 2
#define TOURMENU_STARTED 4
#define TOURMENU_LV_TOP 0
#define TOURMENU_LV_TOUR 1
#define TOURMENU_LV_INFO 2
#define TOURMENU_LV_LEVEL 3
#define TOURMENU_LV_NUM 4
#define TOUR_COUNT 5
#define TOUR_LEVELS 3
#define TOUR_ENTRY_MAX 8

static const char *const kTourTop[2] = {
    "Open tournament", "Free play",
};
static const char *const kTourNames[TOUR_COUNT] = {
    "World Tournament", "Cell Games", "Other World Tournament", "Yamcha Game", "Tenkaichi Budokai",
};
static const char *const kTourLevels[TOUR_LEVELS] = {
    "Novice", "Advanced", "Expert",
};

static void tour_menu(void) {
    static int level = -1, item = -1;
    A11yTourMenu GAME_PTR m = gTourMenu;
    char text[256];
    int cur, open, n;

    if (m == NULL) {
        level = item = -1;
        return;
    }
    if (!(m->flags & TOURMENU_STARTED) || (m->flags & (TOURMENU_DONE | TOURMENU_LEAVING)) ||
        m->level < TOURMENU_LV_TOP || m->level > TOURMENU_LV_NUM) {
        return;
    }
    cur = m->cursor[m->level];
    if (m->level == level && cur == item) {
        return;
    }
    level = m->level;
    item = cur;
    open = m->cursor[TOURMENU_LV_TOP] == 0; /* the open tournament: the level and the entrants are fixed */
    switch (m->level) {
    case TOURMENU_LV_TOP:
        if (cur >= 0 && cur < 2) {
            say_item(tr(kTourTop[cur]), cur, 2, follow());
        }
        break;
    case TOURMENU_LV_TOUR:
        if (cur < 0 || cur >= TOUR_COUNT) {
            break;
        }
        n = snprintf(text, sizeof(text), "%s", tr(kTourNames[cur]));
        if (open && gSaveData != NULL && !(gSaveData->tourOpen & (1 << cur))) {
            snprintf(text + n, sizeof(text) - n, "%s", tr(", closed"));
        } else if (open && gSaveData != NULL && m->info != NULL && gSaveData->tourLevel >= 0 &&
                   gSaveData->tourLevel < TOUR_LEVELS) {
            snprintf(text + n, sizeof(text) - n, tr(", prizes %d and %d"),
                     m->info[cur].prize[0][gSaveData->tourLevel], m->info[cur].prize[1][gSaveData->tourLevel]);
        }
        say_item(text, cur, TOUR_COUNT, follow());
        break;
    case TOURMENU_LV_LEVEL:
        if (cur >= 0 && cur < TOUR_LEVELS) {
            snprintf(text, sizeof(text), tr("Level: %s"), tr(kTourLevels[cur]));
            say_item(text, open ? -1 : cur, TOUR_LEVELS, follow());
        }
        break;
    case TOURMENU_LV_NUM:
        if (cur >= 0 && cur < TOUR_ENTRY_MAX) {
            snprintf(text, sizeof(text), tr("Entrants: %d"), cur + 1);
            say_item(text, -1, 1, follow());
        }
        break;
    default: /* TOURMENU_LV_INFO: the guide explains the tournament */
        break;
    }
}

/* include/menu/menu_c.h: HistSel (src/menu/menu_b_d.c), the story mode's saga select of mode 6. Focus 0 is the menu
   (0 the sagas, 1 the level), focus 1 the ring of sagas and focus 2 the level; HistSel_Input reads the pad only while
   no guide script runs. The plates and the saga names are pictures; Goku's voice speaks each move. */
typedef struct {
    char unk0[0x1CC];
    int flags;           /* 0x1CC: HISTSEL_ */
    int cursor[3];       /* 0x1D0: by focus */
    char unk1DC[0x1EC - 0x1DC];
    int focus;           /* 0x1EC */
    int script;          /* 0x1F0: guide script step, 0 = none */
    int items[9];        /* 0x1F4: listed sagas (8 the ending movie), padded with HISTSEL_ITEM_NONE */
    int itemCount;       /* 0x218 */
    char unk21C[0x228 - 0x21C];
    int percent;         /* 0x228: cleared episodes, in percent */
} A11yHistSel;
LAYOUT_ASSERT(offsetof(A11yHistSel, focus) == 0x1EC && offsetof(A11yHistSel, percent) == 0x228, "HistSel");
extern A11yHistSel GAME_PTR gHistSel;
#define HISTSEL_CHOSEN 1
#define HISTSEL_LEAVING 2
#define HISTSEL_STARTED 4
#define HISTSEL_ITEM_ENDING 8
#define HISTSEL_ITEM_NONE 9
#define HIST_SAGAS 8
#define HIST_LEVELS 3

/* Episodes of each saga: ModeMenu's itemMax (MD_SETUP in ModeMenu_Init). */
static const int kHistEpisodes[HIST_SAGAS] = {3, 4, 7, 5, 16, 5, 4, 4};
/* Provisional: the plates are pictures, not yet read from the disc. */
static const char *const kHistMenu[2] = {
    "Choose a saga", "Level",
};
static const char *const kHistLevels[HIST_LEVELS] = {
    "Easy", "Normal", "Hard",
};

static int bit_count(unsigned v) {
    int n = 0;

    for (; v != 0; v &= v - 1) {
        n++;
    }
    return n;
}

static const char *hist_level(int level) {
    return level >= 0 && level < HIST_LEVELS ? tr(kHistLevels[level]) : "";
}

static void hist_select(void) {
    static int focus = -1, item = -1, told = 0;
    A11yHistSel GAME_PTR m = gHistSel;
    char text[256];
    int cur, id, real, i, n;

    /* the object is freed, not cleared, when the screen leaves: trust it only in its own mode */
    if (m == NULL || gProgress == NULL || gProgress->mode != MODE_HIST_SEL) {
        focus = item = -1;
        told = 0;
        return;
    }
    if (!(m->flags & HISTSEL_STARTED) || (m->flags & (HISTSEL_CHOSEN | HISTSEL_LEAVING)) || m->script != 0 ||
        m->focus < 0 || m->focus > 2) {
        return;
    }
    cur = m->cursor[m->focus];
    if (m->focus == focus && cur == item) {
        return;
    }
    focus = m->focus;
    item = cur;
    switch (m->focus) {
    case 0:
        if (cur < 0 || cur > 1) {
            break;
        }
        n = snprintf(text, sizeof(text), "%s", tr(kHistMenu[cur]));
        if (cur == 1 && gSaveData != NULL) {
            snprintf(text + n, sizeof(text) - n, ": %s", hist_level(gSaveData->histLevel));
        }
        say_item(text, cur, 2, follow());
        if (!told) {
            snprintf(text, sizeof(text), tr("Completed: %d percent"), m->percent);
            PortA11y_Say(text, 0);
            told = 1;
        }
        break;
    case 1:
        if (cur < 0 || cur >= m->itemCount || cur >= 9) {
            break;
        }
        id = m->items[cur];
        for (real = 0, i = 0; i < m->itemCount && i < 9; i++) {
            real += m->items[i] != HISTSEL_ITEM_NONE;
        }
        if (id == HISTSEL_ITEM_NONE) {
            say_item(tr("Empty"), -1, 1, follow());
            break;
        }
        if (id == HISTSEL_ITEM_ENDING) {
            say_item(tr("Watch the ending"), cur, real, follow());
            break;
        }
        n = snprintf(text, sizeof(text), "Saga %d", id + 1);
        if (gSaveData != NULL && id >= 0 && id < HIST_SAGAS) {
            if (gSaveData->slot[id].flags & 2) { /* MSLOT_NEW */
                n += snprintf(text + n, sizeof(text) - n, tr(", new"));
            }
            snprintf(text + n, sizeof(text) - n, tr(", %d of %d episodes cleared"),
                     bit_count((unsigned)gSaveData->slot[id].val[1]), kHistEpisodes[id]);
        }
        say_item(text, cur, real, follow());
        break;
    default:
        if (cur >= 0 && cur < HIST_LEVELS) {
            snprintf(text, sizeof(text), tr("Level: %s"), hist_level(cur));
            say_item(text, cur, HIST_LEVELS, follow());
        }
        break;
    }
}

/* include/menu/menu_b.h: ModeMenu (src/menu/menu_a_d.c), the episode list of mode 7 for the saga gProgress->subMenu.
   Left and right move without wrapping, X opens the episode's description, which the narrator reads aloud (its
   lines are real text and already reach the speech through the text boxes). The episode titles are pictures. */
typedef struct {
    char unk0[0xF4];
    int flags;           /* 0xF4: MODEMENU_ */
    int cursor[2];       /* 0xF8: [0] the index into items */
    char unk100[0x104 - 0x100];
    int focus;           /* 0x104: 0 the list, 1 the description */
    char unk108[0x114 - 0x108];
    int step;            /* 0x114: MODEMENU_STEP_, 0 = idle */
    char unk118[0x128 - 0x118];
    int items[16];       /* 0x128: episode ids, padded with itemMax */
    int itemCount;       /* 0x168 */
    char unk16C[0x178 - 0x16C];
    int itemMax;         /* 0x178 */
} A11yModeMenu;
LAYOUT_ASSERT(offsetof(A11yModeMenu, step) == 0x114 && offsetof(A11yModeMenu, itemMax) == 0x178, "ModeMenu");
extern A11yModeMenu GAME_PTR gModeMenu;
#define MODEMENU_CHOSEN 1
#define MODEMENU_LEAVING 2
#define MODEMENU_STARTED 4
#define PROGRESS_SUB_MENU (0x34 / 4) /* gProgress, in ints: the saga of the story mode */

static void episode_list(void) {
    static int item = -1;
    A11yModeMenu GAME_PTR m = gModeMenu;
    char text[256];
    int cur, id, saga, real, i, n;

    if (m == NULL || gProgress == NULL || gProgress->mode != MODE_EPISODES) {
        item = -1;
        return;
    }
    if (!(m->flags & MODEMENU_STARTED) || (m->flags & (MODEMENU_CHOSEN | MODEMENU_LEAVING)) || m->focus != 0 ||
        m->step != 0) {
        item = -1; /* back from the description: say the episode again */
        return;
    }
    cur = m->cursor[0];
    if (cur == item || cur < 0 || cur >= m->itemCount || cur >= 16) {
        return;
    }
    item = cur;
    id = m->items[cur];
    if (id == m->itemMax) {
        return;
    }
    for (real = 0, i = 0; i < m->itemCount && i < 16; i++) {
        real += m->items[i] != m->itemMax;
    }
    n = snprintf(text, sizeof(text), tr("Episode %d"), id + 1);
    saga = gProgress != NULL ? ((int GAME_PTR)gProgress)[PROGRESS_SUB_MENU] : -1;
    if (gSaveData != NULL && saga >= 0 && saga < 9 && id >= 0 && id < 32) {
        if (gSaveData->slot[saga].val[2] & (1 << id)) {
            n += snprintf(text + n, sizeof(text) - n, trc("episode", ", new"));
        }
        if (gSaveData->slot[saga].val[1] & (1 << id)) {
            snprintf(text + n, sizeof(text) - n, trc("episode", ", cleared"));
        }
    }
    say_item(text, cur, real, follow());
}

/* include/menu/menu_i.h: Train (src/menu/menu_h_d.c), the training menu of mode 44. Level 0 has two plates (0 the
   character select for free practice, 1 the lessons), level 1 the three classes and level 2 the class's lessons,
   three rows of a scrolling list whose names are real text. The explanation pages that follow are the message
   window's text (msg_window). The plates and the class names are pictures. */
typedef struct {
    char unk0[0x420];
    uint32_t GAME_PTR text;  /* 0x420: the lesson names */
    char unk424[0x444 - 0x424];
    int sel[2];              /* 0x444: the cursor of levels 0 and 1 */
    char unk44C[0x458 - 0x44C];
    int level;               /* 0x458: TRAIN_LV_ */
    int top;                 /* 0x45C: first visible lesson */
    int row;                 /* 0x460: row of the cursor, 0..2 */
    char unk464[0x478 - 0x464];
    int flags;               /* 0x478: TRAIN_ */
    char unk47C[0x1994 - 0x47C];
    int nameLine[3][15];     /* 0x1994: text line of each listed lesson's name */
    char unk1A48[0x1BB0 - 0x1A48];
    int count[3];            /* 0x1BB0: lessons listed per class */
} A11yTrain;
LAYOUT_ASSERT(offsetof(A11yTrain, level) == 0x458 && offsetof(A11yTrain, nameLine) == 0x1994 &&
                   offsetof(A11yTrain, count) == 0x1BB0, "Train");
extern A11yTrain GAME_PTR gTrain;
#define TRAIN_LEAVING 8
#define TRAIN_LV_TOP 0
#define TRAIN_LV_CLASS 1
#define TRAIN_LV_LESSON 2
#define TRAIN_CLASSES 3

/* Provisional: they say what the plates lead to; the pictures are not yet read from the disc. */
static const char *const kTrainTop[2] = {
    "Free practice, choose a character", "Lessons",
};

static void train_menu(void) {
    static int level = -1, item = -1;
    A11yTrain GAME_PTR m = gTrain;
    char text[256], name[200];
    int cur = -1, class, done, n;

    if (m == NULL) {
        level = item = -1;
        return;
    }
    if ((m->flags & TRAIN_LEAVING) || m->level < TRAIN_LV_TOP || m->level > TRAIN_LV_LESSON) {
        level = -1; /* the lesson's introduction and pages: say the lesson again when back */
        return;
    }
    class = m->sel[1];
    cur = m->level == TRAIN_LV_LESSON ? m->top + m->row : m->sel[m->level];
    if (m->level == level && cur == item) {
        return;
    }
    level = m->level;
    item = cur;
    switch (m->level) {
    case TRAIN_LV_TOP:
        if (cur >= 0 && cur < 2) {
            say_item(tr(kTrainTop[cur]), cur, 2, follow());
        }
        break;
    case TRAIN_LV_CLASS:
        if (cur < 0 || cur >= TRAIN_CLASSES) {
            break;
        }
        n = snprintf(text, sizeof(text), tr("Class %d"), cur + 1);
        if (gSaveData != NULL && m->count[cur] > 0 && m->count[cur] <= 15) {
            snprintf(text + n, sizeof(text) - n, tr(", %d of %d lessons cleared"),
                     bit_count((unsigned)gSaveData->trainClear[cur] & ((1U << m->count[cur]) - 1)), m->count[cur]);
        }
        say_item(text, cur, TRAIN_CLASSES, follow());
        break;
    default:
        if (class < 0 || class >= TRAIN_CLASSES || cur < 0 || cur >= m->count[class] || cur >= 15) {
            break;
        }
        if (!text_line(name, sizeof(name), m->text, m->nameLine[class][cur])) {
            snprintf(name, sizeof(name), tr("Lesson %d"), cur + 1);
        }
        done = gSaveData != NULL && (gSaveData->trainClear[class] & (1 << cur));
        snprintf(text, sizeof(text), "%s%s", name, done ? tr(", cleared") : "");
        say_item(text, cur, m->count[class], follow());
        break;
    }
}

/* include/menu/menu_s.h: SurvSel (src/menu/menu_s_b.c), the survival course select of mode 17. Up and down move
   through the three courses (wrapping), X opens the course's window (level 1) and X there starts it. The course
   names are real text (lines 0x13C.. of the screen's text); the best result is drawn with digit pictures. */
typedef struct {
    char unk0[0x8];
    uint32_t GAME_PTR text;  /* 0x008 */
    char unkC[0x2F0 - 0xC];
    int flags;               /* 0x2F0: SURVSEL_ */
    int cur[2];              /* 0x2F4: only cur[0], the course, is used */
    char unk2FC[0x308 - 0x2FC];
    int level;               /* 0x308: 0 choosing, 1 the course's window is open */
} A11ySurvSel;
LAYOUT_ASSERT(offsetof(A11ySurvSel, flags) == 0x2F0 && offsetof(A11ySurvSel, level) == 0x308, "SurvSel");
extern A11ySurvSel GAME_PTR gSurvSel;
#define SURVSEL_CHOSEN 1
#define SURVSEL_LEAVING 2
#define SURVSEL_STARTED 4
#define SURV_COURSES 3
#define SURV_NAME_LINE 0x13C

static void survival_select(void) {
    static int level = -1, item = -1;
    A11ySurvSel GAME_PTR m = gSurvSel;
    A11ySurvRecord GAME_PTR r;
    char text[256], name[200];
    int cur, n;

    if (m == NULL) {
        level = item = -1;
        return;
    }
    cur = m->cur[0];
    if (!(m->flags & SURVSEL_STARTED) || (m->flags & (SURVSEL_CHOSEN | SURVSEL_LEAVING)) || cur < 0 ||
        cur >= SURV_COURSES || (m->level == level && cur == item)) {
        return;
    }
    level = m->level;
    item = cur;
    if (!text_line(name, sizeof(name), m->text, SURV_NAME_LINE + cur)) {
        snprintf(name, sizeof(name), tr("Course %d"), cur + 1);
    }
    if (m->level != 0) {
        snprintf(text, sizeof(text),
                 tr("%s: X to start, triangle to go back"), name);
        PortA11y_Say(text, follow());
        return;
    }
    snprintf(text, sizeof(text), "%s", name);
    add_position(text, sizeof(text), cur, SURV_COURSES);
    n = (int)strlen(text);
    r = gSaveData != NULL ? &gSaveData->surv[cur] : NULL;
    if (r != NULL && r->defeated > 0) {
        snprintf(text + n, sizeof(text) - n,
                 tr(". Best: %d opponents beaten, time %d:%02d:%02d, score %d"),
                 r->defeated, r->hours, r->minutes, r->seconds, r->score * 100);
    }
    PortA11y_Say(text, follow());
}

/* include/menu/menu_r.h: SimTop (src/menu/menu_r_d.c), the Sim Dragon entry screen of mode 20. Up and down move
   through three plates drawn as pictures (start, ranking, how to play; wrapping), X opens one and triangle goes back.
   The ranking shows five of its ten rows and scrolls one row at a time; the help is one text line per page. */
typedef struct {
    char unk0[0xC];
    uint32_t GAME_PTR text;  /* 0x00C: section 8; line n is help page n */
    char unk10[0x170 - 0x10];
    int flags;               /* 0x170: SIMTOP_ */
    int cur[6];              /* 0x174: only cur[0], the plate, is used */
    char unk18C[0x190 - 0x18C];
    int level;               /* 0x190: SIMTOP_LV_ */
    char unk194[0x1B8 - 0x194];
    int top;                 /* 0x1B8: first ranking row shown, 0..5 */
    int cursor;              /* 0x1BC */
    int page;                /* 0x1C0: help page, 0..10 */
} A11ySimTop;
LAYOUT_ASSERT(offsetof(A11ySimTop, flags) == 0x170 && offsetof(A11ySimTop, level) == 0x190 &&
               offsetof(A11ySimTop, top) == 0x1B8 && offsetof(A11ySimTop, page) == 0x1C0, "SimTop");
extern A11ySimTop GAME_PTR gSimTop;
#define SIMTOP_CHOSEN 1
#define SIMTOP_LEAVING 2
#define SIMTOP_STARTED 4
#define SIMTOP_LV_MENU 0
#define SIMTOP_LV_RANKING 1
#define SIMTOP_LV_HELP 2
#define SIMTOP_PLATES 3
#define SIMTOP_SHOWN 5
#define SIMTOP_RANKS 10
#define SIMTOP_PAGES 11

/* Descriptive: the plates are pictures. */
static const char *const kSimTopPlates[SIMTOP_PLATES] = {
    "Start", "Ranking", "How to play",
};

/* Row `row` of the ranking. Its portrait is the only thing that says who it was, so the character is left out. */
static void sim_rank_row(char *out, int cap, int row) {
    A11ySimRank GAME_PTR r = gSaveData != NULL ? &gSaveData->simRank[row] : NULL;

    if (r == NULL || r->score <= 0) {
        snprintf(out, cap, tr("Rank %d: no record"), row + 1);
        return;
    }
    snprintf(out, cap, tr("Rank %d: %d points%s"), row + 1, r->score * 100,
             r->cleared ? trc("ranking", ", cleared") : "");
}

static void sim_top(void) {
    static int level = -1, item = -1, top = -1, page = -1;
    A11ySimTop GAME_PTR m = gSimTop;
    char text[1024];
    int i, n;

    if (m == NULL) {
        level = item = top = page = -1;
        return;
    }
    if (!(m->flags & SIMTOP_STARTED) || (m->flags & (SIMTOP_CHOSEN | SIMTOP_LEAVING))) {
        return;
    }
    switch (m->level) {
    case SIMTOP_LV_MENU:
        if (m->cur[0] < 0 || m->cur[0] >= SIMTOP_PLATES || (level == SIMTOP_LV_MENU && m->cur[0] == item)) {
            return;
        }
        level = m->level;
        item = m->cur[0];
        say_item(tr(kSimTopPlates[item]), item, SIMTOP_PLATES, follow());
        break;
    case SIMTOP_LV_RANKING:
        if (m->top < 0 || m->top > SIMTOP_RANKS - SIMTOP_SHOWN || (level == SIMTOP_LV_RANKING && m->top == top)) {
            return;
        }
        if (level != SIMTOP_LV_RANKING) {
            /* Just opened: the five rows on screen. */
            n = 0;
            for (i = 0; i < SIMTOP_SHOWN && n < (int)sizeof(text) - 2; i++) {
                sim_rank_row(text + n, (int)sizeof(text) - n - 2, m->top + i);
                n = (int)strlen(text);
                text[n++] = '.';
                text[n++] = ' ';
                text[n] = '\0';
            }
            PortA11y_Say(text, follow());
        } else {
            /* Scrolled: only the row that came in. */
            sim_rank_row(text, sizeof(text), m->top > top ? m->top + SIMTOP_SHOWN - 1 : m->top);
            PortA11y_Say(text, A11Y_INTERRUPT);
        }
        level = m->level;
        top = m->top;
        break;
    case SIMTOP_LV_HELP:
        if (m->page < 0 || m->page >= SIMTOP_PAGES || (level == SIMTOP_LV_HELP && m->page == page)) {
            return;
        }
        level = m->level;
        page = m->page;
        n = text_line(text, sizeof(text) - 40, m->text, page);
        snprintf(text + n, sizeof(text) - n, tr("%sPage %d of %d"), n > 0 ? ". " : "",
                 page + 1, SIMTOP_PAGES);
        PortA11y_Say(text, follow());
        break;
    }
}

/* Every name a select's text gave, kept for the screens that show the characters only as pictures (the replay
   list). Only in memory: it comes from the disc each run. */
static char sNameCache[MCHR_RANDOM][64];

/* The name of character `chara` from a select's own text (line chara of its name and form files: sections 43 and 44
   of the versus select, as CharSel_Draw shows them), and its form after it when that says something else. */
static void chara_name(char *out, int cap, uint32_t GAME_PTR nameText, uint32_t GAME_PTR formText, int chara) {
    char form[256];
    size_t len;

    if (text_line(out, cap, nameText, chara) == 0) {
        if (chara >= MCHR_RANDOM && chara < MCHR_RANDOM + 4) {
            snprintf(out, cap, "%s", tr(kCellMarks[chara - MCHR_RANDOM]));
        } else {
            snprintf(out, cap, tr("Character %d"), chara);
        }
        return;
    }
    if (text_line(form, sizeof(form), formText, chara) > 0 && strcmp(form, out) != 0) {
        len = strlen(out);
        snprintf(out + len, cap - len, ", %s", form);
    }
    if (chara >= 0 && chara < MCHR_RANDOM) {
        snprintf(sNameCache[chara], sizeof(sNameCache[chara]), "%s", out);
    }
}

/* Reads every character's name once from a select's text into sNameCache. */
static void name_cache_fill(uint32_t GAME_PTR nameText, uint32_t GAME_PTR formText) {
    char name[A11Y_TEXT];
    int chara, lines = MCHR_RANDOM;

    if (nameText == NULL) {
        return;
    }
    /* the offset table ends where the first line starts: no line past it */
    if (nameText[1] >> 2 != 0 && (int)(nameText[1] >> 2) - 1 < lines) {
        lines = (int)(nameText[1] >> 2) - 1;
    }
    for (chara = 0; chara < lines; chara++) {
        chara_name(name, sizeof(name), nameText, formText, chara);
    }
}

/* A name from sNameCache, or the character's number when no select has given it yet. */
static void cached_name(char *out, int cap, int chara) {
    if (chara >= 0 && chara < MCHR_RANDOM && sNameCache[chara][0] != '\0') {
        snprintf(out, cap, "%s", sNameCache[chara]);
    } else {
        snprintf(out, cap, tr("Character %d"), chara);
    }
}

static void char_name(char *out, int cap, A11yCharSel GAME_PTR cs, int chara) {
    chara_name(out, cap, cs->nameText, cs->formText, chara);
}

/* include/menu/menu_k.h: Bracket (src/menu/menu_k_b.c), the tournament tree of mode 35. While BRK_INPUT is set the
   tree can be scrolled and X starts the next match; the winner's chip moving up (BRK_MOVE_B) shows who won. The
   names come from the screen's own name and form text, as its versus panel shows them. */
typedef struct {
    unsigned short flags;    /* 0x00: TOUR_ENT_ */
    unsigned short unk2;
    int chara;               /* 0x04 */
    int costume;             /* 0x08 */
    int player;              /* 0x0C: which of the player's entrants, the "1P".."8P" plate */
    char unk10[0x28 - 0x10];
} A11yBrkEntrant;            /* include/menu/menu_k.h: BrkEntrant */

typedef struct {
    unsigned short flags;    /* 0x00: TOUR_MATCH_ */
    unsigned short ent[2];   /* 0x02: entrant indices, left and right */
    char unk6[0x10 - 0x6];
} A11yTourMatch;             /* include/menu/menu_k.h: TourMatch */

typedef struct {
    char unk0[0x24];
    uint32_t GAME_PTR nameText;  /* 0x024: section 21 */
    uint32_t GAME_PTR formText;  /* 0x028: section 22 */
    char unk2C[0x2DC - 0x2C];
    int flags;                   /* 0x2DC: BRK_ */
    int voiceLine;               /* 0x2E0 */
    int winSide;                 /* 0x2E4: side of the current match that won, 0 left, 1 right */
    char unk2E8[0x1A44 - 0x2E8];
    int round;                   /* 0x1A44 */
    int match;                   /* 0x1A48: index into m[] */
    char unk1A4C[0x1A68 - 0x1A4C];
    A11yBrkEntrant ent[17];      /* 0x1A68 */
    A11yTourMatch m[16];         /* 0x1D10 */
} A11yBracket;
LAYOUT_ASSERT(sizeof(A11yBrkEntrant) == 0x28 && sizeof(A11yTourMatch) == 0x10, "Bracket");
LAYOUT_ASSERT(offsetof(A11yBracket, flags) == 0x2DC && offsetof(A11yBracket, round) == 0x1A44 &&
               offsetof(A11yBracket, ent) == 0x1A68 && offsetof(A11yBracket, m) == 0x1D10, "Bracket");
extern A11yBracket GAME_PTR gBracket;
#define BRK_INPUT 4
#define BRK_MOVE_B 0x20
#define BRK_ENTRANTS 17
#define BRK_MATCHES 16
#define BRK_REC_NAME_LINE 0xA3   /* the versus panel's name line of a saved custom character */
#define TOUR_ENT_PLAYER 1
#define TOUR_ENT_REC 0x10
#define TOUR_MATCH_FINAL 0x10

static void brk_entrant(char *out, int cap, A11yBracket GAME_PTR b, int e) {
    A11yBrkEntrant GAME_PTR p;
    size_t len;

    if (e < 0 || e >= BRK_ENTRANTS) {
        snprintf(out, cap, "%s", tr("nobody"));
        return;
    }
    p = &b->ent[e];
    chara_name(out, cap, b->nameText, b->formText, (p->flags & TOUR_ENT_REC) ? BRK_REC_NAME_LINE : p->chara);
    if (p->flags & TOUR_ENT_PLAYER) {
        len = strlen(out);
        snprintf(out + len, cap - len, tr(" (player %d)"), p->player + 1);
    }
}

static void bracket(void) {
    static int input = 0, moving = 0, match = -1;
    A11yBracket GAME_PTR b = gBracket;
    A11yTourMatch GAME_PTR mt;
    char text[640], left[256], right[256];
    int n;

    if (b == NULL) {
        input = moving = 0;
        match = -1;
        return;
    }
    if (b->match < 0 || b->match >= BRK_MATCHES) {
        return;
    }
    mt = &b->m[b->match];
    if ((b->flags & BRK_MOVE_B) && !moving && (b->winSide == 0 || b->winSide == 1)) {
        brk_entrant(left, sizeof(left), b, mt->ent[b->winSide]);
        snprintf(text, sizeof(text), tr("%s wins"), left);
        PortA11y_Say(text, A11Y_INTERRUPT);
    }
    moving = (b->flags & BRK_MOVE_B) != 0;
    if (!(b->flags & BRK_INPUT)) {
        input = 0;
        return;
    }
    if (input && b->match == match) {
        return;
    }
    input = 1;
    match = b->match;
    brk_entrant(left, sizeof(left), b, mt->ent[0]);
    brk_entrant(right, sizeof(right), b, mt->ent[1]);
    if (mt->flags & TOUR_MATCH_FINAL) {
        n = snprintf(text, sizeof(text), "%s", "Final: ");
    } else {
        n = snprintf(text, sizeof(text), tr("Round %d: "), b->round + 1);
    }
    snprintf(text + n, sizeof(text) - n, tr("%s versus %s. X to go on"), left, right);
    PortA11y_Say(text, 0);
}

/* What each side of the character select showed when it was last told. */
static struct { int state, pos, row, chara; } sSide[2];
static int sSideSaid; /* the side last spoken for, -1 none */

/* One side of the character select: the character under the cursor, and each step after it (form, item set,
   costume, done) with its place in the list. */
static void char_side(A11yCharSel GAME_PTR cs, int i) {
    A11yCharSide GAME_PTR s = cs->side[i];
    A11yChrCell GAME_PTR cell;
    char text[A11Y_TEXT], what[A11Y_TEXT];
    int pos = 0, count = 0, cellIdx, newRow, flags;

    if (s == NULL || s->state < CHARSEL_ST_GRID || s->state > CHARSEL_ST_DONE) {
        return;
    }
    newRow = s->state != sSide[i].state || s->row != sSide[i].row;
    what[0] = '\0';
    switch (s->state) {
    case CHARSEL_ST_GRID:
        pos = s->col;
        char_name(what, sizeof(what), cs, s->chara);
        if (newRow && cs->rows[i] > 1) {
            snprintf(what + strlen(what), sizeof(what) - strlen(what), tr(", row %d of %d"),
                     s->row + 1, cs->rows[i]);
        }
        break;
    case CHARSEL_ST_FORM:
        pos = s->form;
        char_name(what, sizeof(what), cs, s->chara);
        cellIdx = s->col + s->row * MCHR_COLS;
        if (!(s->flags & CHARSEL_SIDE_CUSTOM) && cs->cells[i] != NULL && cellIdx >= 0 && cellIdx < cs->cellCount[i]) {
            cell = &cs->cells[i][cellIdx];
            count = cell->formCount >= 1 && cell->formCount <= 7 ? cell->formCount : 0;
        }
        break;
    case CHARSEL_ST_ITEMSET:
        pos = s->customPlate;
        count = cs->customRows[i];
        if (pos == 0) {
            snprintf(what, sizeof(what), "%s", tr("No items"));
        } else {
            snprintf(what, sizeof(what), tr("Item set %d"), pos);
        }
        break;
    case CHARSEL_ST_ITEMS:
        snprintf(what, sizeof(what), "%s", tr("Item panel"));
        break;
    case CHARSEL_ST_ITEMINFO: /* the item's description window over the panel */
        sSide[i].state = s->state;
        return;
    case CHARSEL_ST_COLOR:
        pos = s->colorPlate;
        count = cs->colorCount[i];
        snprintf(what, sizeof(what), tr("Costume %d"), pos + 1);
        break;
    case CHARSEL_ST_CUSTOM:
        pos = s->customCol + s->customRow * MCHR_COLS;
        count = cs->customCount[i];
        char_name(what, sizeof(what), cs, s->chara);
        break;
    case CHARSEL_ST_DONE:
        snprintf(what, sizeof(what), "%s", tr("Ready"));
        break;
    }
    if (s->state == sSide[i].state && pos == sSide[i].pos && s->row == sSide[i].row && s->chara == sSide[i].chara) {
        return;
    }
    sSide[i].state = s->state;
    sSide[i].pos = pos;
    sSide[i].row = s->row;
    sSide[i].chara = s->chara;
    add_position(what, sizeof(what), pos, count);
    flags = follow();
    if (i != sSideSaid) { /* the other side spoke last: say whose this is, after it */
        snprintf(text, sizeof(text), tr("Player %d: %s"), i + 1, what);
        if (sSideSaid != -1) {
            flags = 0;
        }
    } else {
        snprintf(text, sizeof(text), "%s", what);
    }
    sSideSaid = i;
    PortA11y_Say(text, flags);
}

/* A stage's name by id: the disc's, then the ones the port adds (plat_stages.h), then the grid's markers. */
static const char *stage_name(int id) {
    if (id >= 0 && id <= CHARSEL_STAGE_RANDOM) {
        return tr(kStageNames[id]);
    }
    if (id >= PORT_STAGE_FIRST && id - PORT_STAGE_FIRST < gPortExtraStageCount && id - PORT_STAGE_FIRST < PORT_STAGE_MAX) {
        return gPortStageNames[id - PORT_STAGE_FIRST];
    }
    return tr("Locked"); /* STGGRID_ID_LOCKED; the empty cells are not reached */
}

/* A song's name by music-list value: the disc's, "random", the ones the port adds (plat_songs.h), else locked. */
static const char *song_name(int v) {
    if (v >= 0 && v < 20) {
        return kSongNames[v];
    }
    if (v == CHARSEL_BGM_RANDOM) {
        return trc("song", "Random");
    }
    if (v >= PORT_SONG_FIRST_OFFSET && v - PORT_SONG_FIRST_OFFSET < gPortSongCount && v - PORT_SONG_FIRST_OFFSET < PORT_SONG_MAX) {
        return gPortSongNames[v - PORT_SONG_FIRST_OFFSET];
    }
    return trc("song", "Locked"); /* 0x19, and the disc's four "********" */
}

/* The stage and music choice: the stage under the cursor, the music entry with the song that plays, and in the
   music list the song under the cursor. `stStage` and `stBgm` are the select's own state numbers for its stage grid
   and its music list (the team select's stage work has the same layout under other numbers). NULL: it starts
   (again). */
static void stage_pick(A11yCharStage GAME_PTR st, int GAME_PTR bgmIds, int bgmCount, int rows, int stStage,
                       int stBgm) {
    static int open, state, col, row, value;
    char text[A11Y_TEXT];
    int v;

    if (st == NULL) {
        open = 0;
        state = -1;
        return;
    }
    if (bgmIds == NULL || bgmCount < 1 || bgmCount > 64) {
        return;
    }
    if (!open) {
        heading(tr("Stage and music"));
        open = 1;
    }
    if (st->state == stStage && st->col == CHARSEL_STAGE_BGM_COL) {
        v = st->bgmPlaying >= 0 && st->bgmPlaying < bgmCount ? bgmIds[st->bgmPlaying] : -1;
        snprintf(text, sizeof(text), tr("Music: %s"), song_name(v));
    } else if (st->state == stStage) {
        v = st->stage;
        snprintf(text, sizeof(text), "%s", stage_name(v));
        if ((st->row != row || state != stStage) && rows > 1) {
            snprintf(text + strlen(text), sizeof(text) - strlen(text), tr(", row %d of %d"),
                     st->row + 1, rows);
        }
    } else if (st->state == stBgm && st->bgm >= 0 && st->bgm < bgmCount) {
        v = bgmIds[st->bgm];
        snprintf(text, sizeof(text), "%s", song_name(v));
        add_position(text, sizeof(text), st->bgm, bgmCount);
    } else {
        return;
    }
    if (st->state == state && st->col == col && st->row == row && v == value) {
        return;
    }
    state = st->state;
    col = st->col;
    row = st->row;
    value = v;
    PortA11y_Say(text, follow());
}

/* The character select: its name when the cursors light, each side's choice, then the stage and music. Quiet once
   the stage is confirmed. */
static void char_select(void) {
    static int open, stage;
    A11yCharSel GAME_PTR cs = gCharSel;
    int i;

    if (cs == NULL || !(cs->flags & CHARSEL_STARTED)) {
        open = 0;
        return;
    }
    if (cs->flags & CHARSEL_LEAVING) {
        return;
    }
    if (!open) {
        heading(tr("Character select"));
        name_cache_fill(cs->nameText, cs->formText);
        open = 1;
        stage = 0;
        sSideSaid = -1;
        for (i = 0; i < 2; i++) {
            sSide[i].state = sSide[i].pos = sSide[i].row = sSide[i].chara = -1;
        }
    }
    if (cs->flags & CHARSEL_STAGE_PHASE) {
        if (!stage) {
            stage = 1;
            stage_pick(NULL, NULL, 0, 0, 0, 0);
        }
        if (cs->stage != NULL) {
            stage_pick(cs->stage, cs->bgmIds, cs->bgmCount, cs->stageRows, CHARSEL_ST_STAGE, CHARSEL_ST_BGM);
        }
        return;
    }
    if (stage) { /* back from the stage choice to the sides */
        stage = 0;
        for (i = 0; i < 2; i++) {
            sSide[i].state = -1;
        }
    }
    for (i = 0; i < 2; i++) { /* with one pad, the sides it has reached (`turn`) */
        if (cs->players == 1 || i <= cs->turn) {
            char_side(cs, i);
        }
    }
}

/* What a one-fighter select shows, from either layout. */
typedef struct {
    uint32_t GAME_PTR nameText;
    uint32_t GAME_PTR formText;
    A11ySelEntry GAME_PTR entry;
    A11yChrCell GAME_PTR grid;
    int gridCount, image, step, rows, customCount, colorCount;
} A11yPick;

/* What the one-fighter select showed when it was last told (all -1 when it opens). */
static struct { int step, pos, row, image, entrant; } sPick;

/* One step of a one-fighter select: the character under the cursor, then its form, item set and costume, with the
   place in the list; in the tournament, before it, the entrant being chosen when that changes. */
static void fighter_pick(const A11yPick *p, int entrant, int entrants) {
    A11ySelEntry GAME_PTR e = p->entry;
    A11yChrCell GAME_PTR cell;
    char text[A11Y_TEXT], what[A11Y_TEXT];
    int pos = 0, count = 0, cellIdx;
    size_t len;

    switch (p->step) {
    case PICK_STEP_CHARA:
        pos = e->col;
        chara_name(what, sizeof(what), p->nameText, p->formText, p->image);
        if ((p->step != sPick.step || e->row != sPick.row) && p->rows > 1) {
            len = strlen(what);
            snprintf(what + len, sizeof(what) - len, tr(", row %d of %d"), e->row + 1, p->rows);
        }
        break;
    case PICK_STEP_FORM:
        pos = e->form;
        chara_name(what, sizeof(what), p->nameText, p->formText, p->image);
        cellIdx = e->col + e->row * MCHR_COLS;
        if (p->grid != NULL && cellIdx >= 0 && cellIdx < p->gridCount) {
            cell = &p->grid[cellIdx];
            count = cell->formCount >= 1 && cell->formCount <= 7 ? cell->formCount : 0;
        }
        break;
    case PICK_STEP_CUSTOM:
        pos = e->custom;
        count = p->customCount;
        if (pos == 0) {
            snprintf(what, sizeof(what), "%s", tr("No items"));
        } else {
            snprintf(what, sizeof(what), tr("Item set %d"), pos);
        }
        break;
    case PICK_STEP_PANEL:
        snprintf(what, sizeof(what), "%s", tr("Item panel"));
        break;
    case PICK_STEP_COLOR:
        pos = e->costume;
        count = p->colorCount;
        snprintf(what, sizeof(what), tr("Costume %d"), pos + 1);
        break;
    default: /* the item's help window over the panel (its text is drawn: shown_text) */
        sPick.step = p->step;
        return;
    }
    if (p->step == sPick.step && pos == sPick.pos && e->row == sPick.row && p->image == sPick.image &&
        entrant == sPick.entrant) {
        return;
    }
    add_position(what, sizeof(what), pos, count);
    if (entrant != sPick.entrant && entrants > 1) {
        snprintf(text, sizeof(text), tr("Entrant %d of %d: %s"), entrant + 1, entrants,
                 what);
    } else {
        snprintf(text, sizeof(text), "%s", what);
    }
    sPick.step = p->step;
    sPick.pos = pos;
    sPick.row = e->row;
    sPick.image = p->image;
    sPick.entrant = entrant;
    PortA11y_Say(text, follow());
}

/* A one-fighter select opens: its name, and nothing told yet. */
static void pick_open(const char *name) {
    heading(tr(name));
    sPick.step = sPick.pos = sPick.row = sPick.image = sPick.entrant = -1;
}

/* The one-fighter select of the main menu's second item (modes 15, 18, 21, 25 and 29). Quiet once it leaves. */
static void solo_select(void) {
    static int open;
    A11ySoloSel GAME_PTR s = gSoloSel;
    A11yPick p;

    if (s == NULL || !(s->flags & SOLOSEL_STARTED)) {
        open = 0;
        return;
    }
    if (s->flags & SOLOSEL_LEAVING) {
        return;
    }
    if (!open) {
        pick_open("Choose your fighter");
        open = 1;
    }
    p.nameText = s->nameText;
    p.formText = s->formText;
    p.entry = &s->entry;
    p.grid = s->grid;
    p.gridCount = s->gridCount;
    p.image = s->image;
    p.step = s->step;
    p.rows = s->rows;
    p.customCount = s->customCount;
    p.colorCount = s->colorCount;
    fighter_pick(&p, 0, 1);
}

/* The tournament's entrants (mode 34), one after the other. Quiet once it leaves. */
static void entry_select(void) {
    static int open;
    A11yEntrySel GAME_PTR s = gEntrySel;
    A11yPick p;
    int cur = s != NULL ? s->cur : -1;
    int entrants = gProgress != NULL ? ((int GAME_PTR)gProgress)[TOUR_ENTRY_NUM] : ESEL_ENTRIES;

    if (s == NULL || !(s->flags & ESEL_STARTED)) {
        open = 0;
        return;
    }
    if ((s->flags & ESEL_LEAVING) || cur < 0 || cur >= ESEL_ENTRIES || cur >= entrants) {
        /* past the last entrant the cursor rests on an empty entry until the screen leaves */
        return;
    }
    if (!open) {
        pick_open("Tournament entry");
        open = 1;
    }
    p.nameText = s->nameText;
    p.formText = s->formText;
    p.entry = &s->entry[cur];
    p.grid = s->grid;
    p.gridCount = s->gridCount;
    p.image = s->image;
    p.step = s->step;
    p.rows = s->rows;
    p.customCount = s->customCount;
    p.colorCount = s->colorCount;
    fighter_pick(&p, cur, entrants);
}

/* include/menu/menu_e.h and menu_f.h: TeamSel (src/menu/menu_e_b.c), the team select of the versus modes. Each side
   fills a list of up to five members, each chosen like in the versus select (grid, form, item set, costume), and
   confirms the list on its last plate; then pad 0 chooses the stage and the music. */
typedef struct {
    int col;          /* 0x00: cursor in the character grid */
    int row;          /* 0x04 */
    int form;         /* 0x08: chip of the form reel */
    int customCol;    /* 0x0C: cursor in the saved custom characters */
    int customRow;    /* 0x10 */
    int plate;        /* 0x14: item set, 0 none */
    int color;        /* 0x18: costume */
    int chara;        /* 0x1C: the chosen character, -1 none yet */
    char unk20[0x30 - 0x20];
} A11yTsMember;       /* include/menu/menu_e.h: TsMember */

typedef struct {
    A11yTsMember member[5]; /* 0x000 */
    char unkF0[0x128 - 0xF0];
    int flags;        /* 0x128: TEAMSEL_SIDE_ */
    int cur;          /* 0x12C: member being chosen; TS_CUR_MENU = on the plate that confirms the team */
    int cost;         /* 0x130: DP total of the team */
    int memberCount;  /* 0x134 */
    int chara;        /* 0x138: character under the cursor, line of the name files */
    int state;        /* 0x13C: TEAMSEL_ST_ */
} A11yTeamSide;       /* include/menu/menu_e.h: TeamSelSide */
LAYOUT_ASSERT(sizeof(A11yTsMember) == 0x30 && offsetof(A11yTeamSide, state) == 0x13C, "TeamSelSide");

typedef struct {
    char unk0[0x28];
    uint32_t GAME_PTR nameText;     /* 0x028: section 43 */
    uint32_t GAME_PTR formText;     /* 0x02C: section 44 */
    char unk30[0x3BC - 0x30];
    int flags;                      /* 0x3BC: TEAMSEL_ */
    char unk3C0[0x8E8 - 0x3C0];
    A11yTeamSide GAME_PTR side[2];  /* 0x8E8 */
    char unk8F0[0x9A4 - 0x8F0];
    A11yCharStage GAME_PTR stage;   /* 0x9A4: TeamSelStage, the versus select's layout; states 9 and 10 */
    char unk9A8[0x9B0 - 0x9A8];
    A11yChrCell GAME_PTR cells[2];  /* 0x9B0: each side's grid */
    int cellCount[2];               /* 0x9B8 */
    char unk9C0[0x3828 - 0x9C0];
    int customCount[2];             /* 0x3828 */
    char unk3830[0x3C20 - 0x3830];
    int rows[2];                    /* 0x3C20: rows of each side's grid */
    int plateCount[2];              /* 0x3C28: item sets offered */
    int colorCount[2];              /* 0x3C30 */
    int players;                    /* 0x3C38: TEAMSEL_PLAYERS_ */
    int battleType;                 /* 0x3C3C: 2 = with a DP limit */
    int dpLevel;                    /* 0x3C40 */
    int dpMax;                      /* 0x3C44: 10, 15 or 20 */
    char unk3C48[0x3C58 - 0x3C48];
    int base;                       /* 0x3C58: the side pad 0 chooses for */
    int GAME_PTR stageIds;          /* 0x3C5C */
    int GAME_PTR bgmIds;            /* 0x3C60 */
    int stageCount;                 /* 0x3C64 */
    int bgmCount;                   /* 0x3C68 */
} A11yTeamSel;
LAYOUT_ASSERT(offsetof(A11yTeamSel, flags) == 0x3BC && offsetof(A11yTeamSel, side) == 0x8E8 &&
               offsetof(A11yTeamSel, stage) == 0x9A4 && offsetof(A11yTeamSel, cells) == 0x9B0, "TeamSel");
LAYOUT_ASSERT(offsetof(A11yTeamSel, customCount) == 0x3828 && offsetof(A11yTeamSel, rows) == 0x3C20 &&
               offsetof(A11yTeamSel, base) == 0x3C58 && offsetof(A11yTeamSel, bgmCount) == 0x3C68, "TeamSel");
extern A11yTeamSel GAME_PTR gTeamSel;
#define TEAMSEL_STARTED 2
#define TEAMSEL_STAGE_PHASE 4
#define TEAMSEL_LEAVING 0x10
#define TEAMSEL_PLAYERS_TWO 1
#define TEAMSEL_DP 2
#define TEAMSEL_SIDE_CUSTOM 0x80
#define TEAMSEL_ST_GRID 0
#define TEAMSEL_ST_FORM 1
#define TEAMSEL_ST_PLATE 2
#define TEAMSEL_ST_PANEL 3
#define TEAMSEL_ST_PANEL_HELP 4
#define TEAMSEL_ST_COLOR 5
#define TEAMSEL_ST_TEAM 6
#define TEAMSEL_ST_CUSTOM 7
#define TEAMSEL_ST_DONE 8
#define TEAMSEL_STAGE_GRID 9
#define TEAMSEL_STAGE_BGM 10
#define TS_MEMBERS 5
#define TS_CUR_MENU 5

/* One side of the team select, as char_side does it for the versus select; in the member list, the member under the
   cursor (or the plate that confirms the team) and, with a DP limit, the team's total. */
static void team_side(A11yTeamSel GAME_PTR ts, int i) {
    static int cur[2] = {-1, -1};
    A11yTeamSide GAME_PTR s = ts->side[i];
    A11yTsMember GAME_PTR mb;
    A11yChrCell GAME_PTR cell;
    char text[A11Y_TEXT], what[A11Y_TEXT];
    int pos = 0, count = 0, cellIdx, newRow, flags, chara;
    size_t len;

    if (ts == NULL) { /* the choice starts (again) */
        cur[0] = cur[1] = -1;
        return;
    }
    if (s == NULL || s->state < TEAMSEL_ST_GRID || s->state > TEAMSEL_ST_DONE || s->cur < 0 || s->cur > TS_CUR_MENU) {
        return;
    }
    mb = &s->member[s->cur < TS_MEMBERS ? s->cur : 0];
    newRow = s->state != sSide[i].state || mb->row != sSide[i].row;
    chara = s->chara;
    what[0] = '\0';
    switch (s->state) {
    case TEAMSEL_ST_GRID:
        pos = mb->col;
        chara_name(what, sizeof(what), ts->nameText, ts->formText, chara);
        if (newRow && ts->rows[i] > 1) {
            len = strlen(what);
            snprintf(what + len, sizeof(what) - len, tr(", row %d of %d"), mb->row + 1,
                     ts->rows[i]);
        }
        break;
    case TEAMSEL_ST_FORM:
        pos = mb->form;
        chara_name(what, sizeof(what), ts->nameText, ts->formText, chara);
        cellIdx = mb->col + mb->row * MCHR_COLS;
        if (!(s->flags & TEAMSEL_SIDE_CUSTOM) && ts->cells[i] != NULL && cellIdx >= 0 && cellIdx < ts->cellCount[i]) {
            cell = &ts->cells[i][cellIdx];
            count = cell->formCount >= 1 && cell->formCount <= 7 ? cell->formCount : 0;
        }
        break;
    case TEAMSEL_ST_PLATE:
        pos = mb->plate;
        count = ts->plateCount[i];
        if (pos == 0) {
            snprintf(what, sizeof(what), "%s", tr("No items"));
        } else {
            snprintf(what, sizeof(what), tr("Item set %d"), pos);
        }
        break;
    case TEAMSEL_ST_PANEL:
        snprintf(what, sizeof(what), "%s", tr("Item panel"));
        break;
    case TEAMSEL_ST_PANEL_HELP: /* the item's description window over the panel */
        sSide[i].state = s->state;
        return;
    case TEAMSEL_ST_COLOR:
        pos = mb->color;
        count = ts->colorCount[i];
        snprintf(what, sizeof(what), tr("Costume %d"), pos + 1);
        break;
    case TEAMSEL_ST_TEAM:
        chara = -1;
        if (s->cur == TS_CUR_MENU) {
            snprintf(what, sizeof(what), "%s", tr("Confirm the team"));
            pos = -1;
        } else {
            pos = s->cur;
            count = s->memberCount < TS_MEMBERS ? s->memberCount + 1 : TS_MEMBERS;
            if (s->cur < s->memberCount && mb->chara >= 0) {
                chara = mb->chara;
                len = (size_t)snprintf(what, sizeof(what), tr("Member %d: "), s->cur + 1);
                chara_name(what + len, sizeof(what) - len, ts->nameText, ts->formText, chara);
            } else {
                snprintf(what, sizeof(what), tr("Member %d: empty"), s->cur + 1);
            }
        }
        break;
    case TEAMSEL_ST_CUSTOM:
        pos = mb->customCol + mb->customRow * MCHR_COLS;
        count = ts->customCount[i];
        chara_name(what, sizeof(what), ts->nameText, ts->formText, chara);
        break;
    case TEAMSEL_ST_DONE:
        snprintf(what, sizeof(what), "%s", tr("Team ready"));
        break;
    }
    if (s->state == sSide[i].state && pos == sSide[i].pos && mb->row == sSide[i].row && chara == sSide[i].chara &&
        s->cur == cur[i]) {
        return;
    }
    add_position(what, sizeof(what), pos, count);
    if (s->state == TEAMSEL_ST_TEAM && sSide[i].state != TEAMSEL_ST_TEAM && ts->battleType == TEAMSEL_DP) {
        len = strlen(what);
        snprintf(what + len, sizeof(what) - len, tr(". Team DP: %d of %d"), s->cost,
                 ts->dpMax);
    }
    if (s->state != TEAMSEL_ST_TEAM && s->cur != cur[i] && s->cur < TS_MEMBERS) {
        /* choosing another member: say which */
        snprintf(text, sizeof(text), tr("Member %d: %s"), s->cur + 1, what);
        snprintf(what, sizeof(what), "%s", text);
    }
    sSide[i].state = s->state;
    sSide[i].pos = pos;
    sSide[i].row = mb->row;
    sSide[i].chara = chara;
    cur[i] = s->cur;
    flags = follow();
    if (i != sSideSaid) { /* the other side spoke last: say whose this is, after it */
        snprintf(text, sizeof(text), tr("Team %d: %s"), i + 1, what);
        if (sSideSaid != -1) {
            flags = 0;
        }
    } else {
        snprintf(text, sizeof(text), "%s", what);
    }
    sSideSaid = i;
    PortA11y_Say(text, flags);
}

/* The team select (mode 40 and the others that use it): its name when the cursors light, each side's choice, then
   the stage and music. Quiet once it leaves. */
static void team_select(void) {
    static int open, stage;
    A11yTeamSel GAME_PTR ts = gTeamSel;
    int i, rows;

    if (ts == NULL || !(ts->flags & TEAMSEL_STARTED)) {
        open = 0;
        return;
    }
    if (ts->flags & TEAMSEL_LEAVING) {
        return;
    }
    if (!open) {
        heading(tr("Team select"));
        name_cache_fill(ts->nameText, ts->formText);
        open = 1;
        stage = 0;
        sSideSaid = -1;
        team_side(NULL, 0);
        for (i = 0; i < 2; i++) {
            sSide[i].state = sSide[i].pos = sSide[i].row = sSide[i].chara = -1;
        }
    }
    if (ts->flags & TEAMSEL_STAGE_PHASE) {
        if (!stage) {
            stage = 1;
            stage_pick(NULL, NULL, 0, 0, 0, 0);
        }
        /* rows of six, at least six (menu_e_b.c, the stages the port adds) */
        rows = (ts->stageCount + 5) / 6;
        if (ts->stage != NULL) {
            stage_pick(ts->stage, ts->bgmIds, ts->bgmCount, rows < 6 ? 6 : rows, TEAMSEL_STAGE_GRID,
                       TEAMSEL_STAGE_BGM);
        }
        return;
    }
    if (stage) { /* back from the stage choice to the teams */
        stage = 0;
        for (i = 0; i < 2; i++) {
            sSide[i].state = -1;
        }
    }
    if (ts->players == TEAMSEL_PLAYERS_TWO) {
        team_side(ts, 0);
        team_side(ts, 1);
    } else if (ts->base == 0 || ts->base == 1) { /* one pad chooses both teams, one after the other */
        team_side(ts, ts->base);
    }
}

/* include/menu/menu_za.h: ReplayMenu (src/menu/menu_za_d.c), the replay list of mode 56. It shows one of the seven
   replay files at a time, its number and the two teams as chips; confirm loads it (from the Data Center) or saves
   the last fight into it (after a fight). The card's questions and messages are a Dialog, read by dialog(). */
typedef struct {
    char unk0[0xE8];
    int busy;       /* 0xE8: REPLAY_BUSY_, a card flow running */
    int unkEC;
    int cursor;     /* 0xF0: slot shown, 0..6 */
    int timer;      /* 0xF4 */
    int flags;      /* 0xF8: REPLAY_ */
    int unkFC;
    int mcState;    /* 0x100: not 0 while a card flow runs */
} A11yReplayMenu;
LAYOUT_ASSERT(offsetof(A11yReplayMenu, busy) == 0xE8 && offsetof(A11yReplayMenu, mcState) == 0x100, "ReplayMenu");
extern A11yReplayMenu GAME_PTR gReplayMenu; /* NULL outside the screen */

typedef struct {
    int flags;      /* bit 0: the slot holds a replay */
    int chara[10];  /* two teams of five, REPLAY_NO_CHARA empty */
} A11yReplaySlot;
typedef struct {
    char unk0[0x68C];
    int replayFlags;              /* 0x68C: ZAPROG_REPLAY_SAVE */
    char unk690[0x69C - 0x690];
    A11yReplaySlot replay[7];     /* 0x69C */
} A11yZaProgress;                 /* include/menu/menu_za.h: ZaProgress, the view of gProgress */
LAYOUT_ASSERT(sizeof(A11yReplaySlot) == 0x2C && offsetof(A11yZaProgress, replay) == 0x69C, "ZaProgress");
#define REPLAY_LEAVE 1
#define REPLAY_STARTED 4
#define REPLAY_BUSY_NONE 0
#define REPLAY_SLOTS 7
#define REPLAY_MEMBERS 5
#define REPLAY_NO_CHARA 0xA4
#define ZAPROG_REPLAY_SAVE 1

/* The replay list: whether it loads or saves, then the slot shown and who fought in it, once the card is read and
   again after each card question. */
static void replay_menu(void) {
    static int open, cursor;
    A11yReplayMenu GAME_PTR r = gReplayMenu;
    A11yZaProgress GAME_PTR prog = (A11yZaProgress GAME_PTR)gProgress;
    A11yReplaySlot GAME_PTR slot;
    char text[A11Y_TEXT], name[96];
    int team, i, n, len, chara;

    if (r == NULL || prog == NULL || !(r->flags & REPLAY_STARTED)) {
        open = 0;
        return;
    }
    if (r->flags & REPLAY_LEAVE) {
        return;
    }
    if (!open) {
        heading(prog->replayFlags & ZAPROG_REPLAY_SAVE
                    ? tr("Save replay data")
                    : tr("View replay data"));
        open = 1;
        cursor = -1;
    }
    if (r->busy != REPLAY_BUSY_NONE || r->mcState != 0) {
        cursor = -1; /* the slot is said again once the card is done */
        return;
    }
    if (r->cursor == cursor || r->cursor < 0 || r->cursor >= REPLAY_SLOTS) {
        return;
    }
    cursor = r->cursor;
    slot = &prog->replay[cursor];
    len = snprintf(text, sizeof(text), tr("Replay %d of %d: "), cursor + 1,
                   REPLAY_SLOTS);
    if (!(slot->flags & 1)) {
        snprintf(text + len, sizeof(text) - len, "%s", tr("empty"));
    } else {
        for (team = 0; team < 2; team++) {
            n = 0;
            for (i = 0; i < REPLAY_MEMBERS; i++) {
                chara = slot->chara[team * REPLAY_MEMBERS + i];
                if (chara < 0 || chara >= MCHR_RANDOM) {
                    continue;
                }
                cached_name(name, sizeof(name), chara);
                len += snprintf(text + len, sizeof(text) - len, "%s%s",
                                n > 0 ? ", " : team > 0 ? tr(" versus ") : "", name);
                n++;
                if (len >= (int)sizeof(text)) {
                    len = (int)sizeof(text) - 1;
                }
            }
        }
    }
    PortA11y_Say(text, follow());
}

/* include/menu/menu_h.h: CharRef (src/menu/menu_h_b.c), the character reference of mode 60. A list of the unlocked
   characters; X opens one's page, whose menu has the profile (real text), a voice sample, the guide's comment
   (voiced, with subtitles) and the model viewer. Left and right page through the characters there too. */
typedef struct {
    char unk0[8];
    int chara;        /* 0x08: line of the name, form and profile files */
    int commentLine;
    int commentNum;
} A11yCharRefEntry;   /* 0x14 */
typedef struct {
    char unk0[0x18];
    uint32_t GAME_PTR text[4];          /* 0x18: names, forms, voice labels, profiles */
    char unk28[0x190 - 0x28];
    A11yCharRefEntry entry[161];        /* 0x190: in list order */
    char unkE24[0x15DC - 0xE24];
    int cursor;                         /* 0x15DC: list index of the current character */
    int count;                          /* 0x15E0: listed characters */
    int total;                          /* 0x15E4 */
    int costume;                        /* 0x15E8: for the model viewer */
    char unk15EC[0x15FC - 0x15EC];
    int menuCursor;                     /* 0x15FC: item of the page's menu, 5 while the model is shown */
    char unk1600[0x1628 - 0x1600];
    int state;                          /* 0x1628: CHARREF_ST_ */
} A11yCharRef;
LAYOUT_ASSERT(sizeof(A11yCharRefEntry) == 0x14 && offsetof(A11yCharRef, cursor) == 0x15DC, "CharRef");
LAYOUT_ASSERT(offsetof(A11yCharRef, menuCursor) == 0x15FC && offsetof(A11yCharRef, state) == 0x1628, "CharRef");
extern A11yCharRef GAME_PTR gCharRef; /* NULL outside the screen */
#define CHARREF_CHARA_MAX 161
#define CHARREF_ITEMS 4
#define CHARREF_ST_INTRO 0
#define CHARREF_ST_LIST 1
#define CHARREF_ST_DETAILS 2
#define CHARREF_ST_PROFILE 3
#define CHARREF_ST_VOICE 4
#define CHARREF_ST_COMMENT 5
#define CHARREF_ST_COSTUME 6
#define CHARREF_ST_VIEW 7
#define CHARREF_ST_VIEW_WAIT 8

/* The items of a character's page: pictures, named here (menu_h_b.c, CharRef_StateDetails). */
static const char *const kCharRefItems[CHARREF_ITEMS] = {
    "Profile", "Voice", "Chi-Chi's comment", "Model",
};

/* Says a long text in two pieces if it does not fit one message, cut at a sentence or a word. */
static void say_long(const char *text, int flags) {
    char part[A11Y_TEXT];
    size_t len = strlen(text), cut;

    if (len < sizeof(part)) {
        PortA11y_Say(text, flags);
        return;
    }
    cut = sizeof(part) - 1;
    while (cut > sizeof(part) / 2 && text[cut] != '.' && text[cut] != '\n') {
        cut--;
    }
    if (cut == sizeof(part) / 2) {
        for (cut = sizeof(part) - 1; cut > 0 && text[cut] != ' '; cut--) {
        }
    }
    if (cut == 0) {
        cut = sizeof(part) - 2;
    }
    snprintf(part, sizeof(part), "%.*s", (int)cut + 1, text);
    PortA11y_Say(part, flags);
    PortA11y_Say(text + cut + 1, 0);
}

/* The character reference: the character under the cursor in the list, the item of its page, the profile when it
   opens, the voice's label, the costume of the model viewer. The guide's comment is voiced: not read. */
extern const char *PortA11y_ActionName(int player, int action); /* gs/a11y_pad.c: a game button by the controller */
#define ACTION_CROSS 4    /* gs_input.c, kActions */
#define ACTION_TRIANGLE 7

static void char_ref(void) {
    static int open, state, cursor, item, costume;
    A11yCharRef GAME_PTR c = gCharRef;
    char text[2 * A11Y_TEXT], name[A11Y_TEXT];
    const char *button;
    int chara, len;

    if (c == NULL) {
        open = 0;
        return;
    }
    if (c->state < CHARREF_ST_LIST || c->state > CHARREF_ST_VIEW_WAIT || c->count <= 0 ||
        c->count > CHARREF_CHARA_MAX || c->cursor < 0 || c->cursor >= c->count) {
        return; /* the intro, or leaving */
    }
    if (!open) {
        heading(tr("Character reference"));
        open = 1;
        state = cursor = item = costume = -1;
    }
    chara = c->entry[c->cursor].chara;
    switch (c->state) {
    case CHARREF_ST_LIST:
        if (state == CHARREF_ST_LIST && cursor == c->cursor) {
            break;
        }
        chara_name(name, sizeof(name), c->text[0], c->text[1], chara);
        add_position(name, sizeof(name), c->cursor, c->count);
        PortA11y_Say(name, follow());
        break;
    case CHARREF_ST_DETAILS:
        if (c->menuCursor < 0 || c->menuCursor >= CHARREF_ITEMS ||
            (state == CHARREF_ST_DETAILS && cursor == c->cursor && item == c->menuCursor)) {
            break;
        }
        text[0] = '\0';
        if (cursor != c->cursor) { /* opened, or paged to another character */
            chara_name(name, sizeof(name), c->text[0], c->text[1], chara);
            add_position(name, sizeof(name), c->cursor, c->count);
            snprintf(text, sizeof(text), "%s. ", name);
        }
        len = (int)strlen(text);
        snprintf(text + len, sizeof(text) - len, "%s", tr(kCharRefItems[c->menuCursor]));
        add_position(text, sizeof(text), c->menuCursor, CHARREF_ITEMS);
        PortA11y_Say(text, follow());
        break;
    case CHARREF_ST_PROFILE:
        if (state != CHARREF_ST_PROFILE && text_line(text, sizeof(text), c->text[3], chara * 2 + 1) > 0) {
            say_long(text, follow());
        }
        break;
    case CHARREF_ST_VOICE:
        if (state != CHARREF_ST_VOICE) {
            len = text_line(text, sizeof(text) - 80, c->text[2], chara * 2);
            snprintf(text + len, sizeof(text) - len, tr("%sX plays a line, triangle goes back"),
                     len > 0 ? ". " : "");
            PortA11y_Say(text, follow());
        }
        break;
    case CHARREF_ST_COSTUME:
        if (state != CHARREF_ST_COSTUME || costume != c->costume) {
            if ((button = PortA11y_ActionName(-1, ACTION_CROSS)) != NULL) { /* (the controller in use's name) */
                snprintf(text, sizeof(text), tr("Costume %d. %s shows the model"), c->costume + 1, button);
            } else {
                snprintf(text, sizeof(text), tr("Costume %d. X shows the model"), c->costume + 1);
            }
            PortA11y_Say(text, follow());
        }
        break;
    case CHARREF_ST_VIEW:
        if (state != CHARREF_ST_VIEW) {
            if ((button = PortA11y_ActionName(-1, ACTION_TRIANGLE)) != NULL) {
                snprintf(text, sizeof(text), tr("Model shown. %s goes back"), button);
                PortA11y_Say(text, follow());
            } else {
                PortA11y_Say(tr("Model shown. Triangle goes back"), follow());
            }
        }
        break;
    }
    state = c->state;
    cursor = c->cursor;
    item = c->menuCursor;
    costume = c->costume;
}

/* include/menu/menu_v.h: ItemHelp (src/menu/menu_v_c.c), the item details page: the item's name and description
   from its own text. Its owner (the shop) says when it is open and which item it shows. */
typedef struct {
    uint32_t GAME_PTR nameText;   /* 0x00: item names */
    uint32_t GAME_PTR descText;   /* 0x04: item descriptions */
} A11yItemHelp;
extern A11yItemHelp GAME_PTR gItemHelp; /* NULL while no screen has it loaded */

/* Says the details page of item `item` (0-based). */
static void item_help(int item) {
    char text[2 * A11Y_TEXT];
    int len;

    if (gItemHelp == NULL || item < 0 || item >= SAVE_ITEMS) {
        return;
    }
    len = text_line(text, sizeof(text) / 4, gItemHelp->nameText, item);
    if (len > 0) {
        text[len++] = '.';
        text[len++] = ' ';
        text[len] = '\0';
    }
    if (text_line(text + len, sizeof(text) - len, gItemHelp->descText, item) == 0 && len == 0) {
        return;
    }
    say_long(text, follow());
}

/* include/menu/menu_w.h: Shop (src/menu/menu_v_d.c), the item shop of Evolution Z (mode 50). Two plates (buy, the
   collection); each opens a list with four tabs (L1 and R1), the buy list asks for confirmation (buy, cancel,
   details) and shows what was bought. The guides' lines are voiced, with subtitles: not read. */
typedef struct {
    unsigned char type;   /* 0x00: 0..2, the list tab is type + 1 */
    unsigned char unk1;
    unsigned char picture;
    unsigned char slots;
    int price;            /* 0x04 */
    char unk8[0x14 - 8];
    int flags;            /* 0x14: WITEM_ */
    char unk18[0x28 - 0x18];
} A11yShopItem;           /* include/menu/menu_w.h: WItemEntry */
typedef struct {
    int cur[4];           /* 0x0000: cursor of each tab */
    int tab;              /* 0x0010 */
    char unk14[0x20 - 0x14];
    int count[4];         /* 0x0020 */
    int ids[4][350];      /* 0x0030: 0-based items */
    char unk1610[0x1630 - 0x1610];
} A11yShopList;           /* include/menu/menu_w.h: ShopList */
typedef struct {
    char unk0[0x10];
    uint32_t GAME_PTR itemText;   /* 0x0010: item names; line 350 is the name of an item not owned */
    char unk14[0x230 - 0x14];
    int flags;                    /* 0x0230: SHOP_ */
    int cursor[6];                /* 0x0234: by level; [0] the plate, [4] the confirmation row */
    int timer;
    int level;                    /* 0x0250: SHOP_LV_ */
    int seq;                      /* 0x0254: a scripted step, 0 = the pad is read */
    char unk258[0x26C - 0x258];
    int helpItem;                 /* 0x026C: item of the details page */
    int item;                     /* 0x0270: item just bought */
    int total;                    /* 0x0274: collectable items */
    int owned;                    /* 0x0278 */
    int percent;                  /* 0x027C */
    char unk280[4];
    A11yShopList list[2];         /* 0x0284: [0] on sale, [1] every item */
    A11yShopItem GAME_PTR items;  /* 0x2EE4 */
} A11yShop;
LAYOUT_ASSERT(sizeof(A11yShopItem) == 0x28 && offsetof(A11yShopItem, flags) == 0x14 && sizeof(A11yShopList) == 0x1630, "ShopList");
LAYOUT_ASSERT(offsetof(A11yShop, flags) == 0x230 && offsetof(A11yShop, level) == 0x250 &&
               offsetof(A11yShop, list) == 0x284 && offsetof(A11yShop, items) == 0x2EE4, "Shop");
extern A11yShop GAME_PTR gShop; /* NULL outside the screen */
#define SHOP_STARTED 4
#define SHOP_TABS 4
#define SHOP_LV_MENU 0
#define SHOP_LV_BUY 1
#define SHOP_LV_ALL 2
#define SHOP_LV_HELP 3
#define SHOP_LV_CONFIRM 4
#define SHOP_LV_BOUGHT 5
#define WITEM_HIDDEN 5
#define WITEM_SOLD 0x20

static const char *const kShopPlates[2] = {"Buy", "Collection"};
static const char *const kShopConfirm[3] = {"Buy", "Cancel", "Details"};

/* The shop: the plate, then in a list the tab, each item with its price or whether it is owned, the confirmation
   rows, what was bought and the details page. The money and the collection when the menu level comes back. */
static void shop(void) {
    static int open, level, plate, tab, cur, row;
    A11yShop GAME_PTR s = gShop;
    A11yShopList GAME_PTR l;
    A11yShopItem GAME_PTR it;
    char text[A11Y_TEXT], name[256];
    int k, item, owned, len, idx;

    if (s == NULL) {
        open = 0;
        return;
    }
    if (!(s->flags & SHOP_STARTED) || s->seq != 0 || gSaveData == NULL || s->items == NULL) {
        return; /* a scripted step: the guide is talking */
    }
    if (!open) {
        heading(tr("Item shop"));
        open = 1;
        level = plate = tab = cur = row = -1;
    }
    switch (s->level) {
    case SHOP_LV_MENU:
        if (s->cursor[0] < 0 || s->cursor[0] > 1 || (level == SHOP_LV_MENU && plate == s->cursor[0])) {
            break;
        }
        snprintf(text, sizeof(text), "%s", tr(kShopPlates[s->cursor[0]]));
        add_position(text, sizeof(text), s->cursor[0], 2);
        if (level != SHOP_LV_MENU) {
            len = (int)strlen(text);
            snprintf(text + len, sizeof(text) - len,
                     tr(". Z Points: %d. Collection: %d of %d, %d percent"),
                     gSaveData->money, s->owned, s->total, s->percent);
        }
        PortA11y_Say(text, follow());
        break;
    case SHOP_LV_BUY:
    case SHOP_LV_ALL:
        k = s->level == SHOP_LV_BUY ? 0 : 1;
        l = &s->list[k];
        if (l->tab < 0 || l->tab >= SHOP_TABS || l->count[l->tab] <= 0 || l->count[l->tab] > SAVE_ITEMS) {
            break;
        }
        idx = l->cur[l->tab];
        if (idx < 0 || idx >= l->count[l->tab] || (level == s->level && tab == l->tab && cur == idx)) {
            break;
        }
        item = l->ids[l->tab][idx];
        if (item < 0 || item >= SAVE_ITEMS) {
            break;
        }
        it = &s->items[item];
        owned = gSaveData->item[item] & SAVE_ITEM_OWNED;
        text[0] = '\0';
        if (level != s->level || tab != l->tab) {
            snprintf(text, sizeof(text), tr("Tab %d of %d%s. "), l->tab + 1, SHOP_TABS,
                     l->tab == 0 ? tr(", all") : "");
        }
        len = (int)strlen(text);
        if (k == 1 && !owned) {
            snprintf(text + len, sizeof(text) - len, "%s", tr("Not owned"));
        } else {
            if (text_line(name, sizeof(name), s->itemText, item) == 0) {
                snprintf(name, sizeof(name), tr("Item %d"), item + 1);
            }
            if (k == 1) {
                snprintf(text + len, sizeof(text) - len, "%s", name);
            } else if (owned) {
                snprintf(text + len, sizeof(text) - len, tr("%s, owned"), name);
            } else if (!(it->flags & WITEM_HIDDEN) && (it->flags & WITEM_SOLD)) {
                snprintf(text + len, sizeof(text) - len, tr("%s, %d Z Points"), name, it->price);
            } else {
                snprintf(text + len, sizeof(text) - len, "%s", name);
            }
        }
        add_position(text, sizeof(text), idx, l->count[l->tab]);
        PortA11y_Say(text, follow());
        tab = l->tab;
        cur = idx;
        break;
    case SHOP_LV_CONFIRM:
        if (s->cursor[4] < 0 || s->cursor[4] > 2 || (level == SHOP_LV_CONFIRM && row == s->cursor[4])) {
            break;
        }
        text[0] = '\0';
        if (level != SHOP_LV_CONFIRM) {
            l = &s->list[0];
            item = l->tab >= 0 && l->tab < SHOP_TABS && l->cur[l->tab] >= 0 && l->cur[l->tab] < SAVE_ITEMS
                       ? l->ids[l->tab][l->cur[l->tab]]
                       : -1;
            if (item >= 0 && item < SAVE_ITEMS && text_line(name, sizeof(name), s->itemText, item) > 0) {
                snprintf(text, sizeof(text), tr("Buy %s for %d Z Points? You have %d. "),
                         name, s->items[item].price, gSaveData->money);
            }
        }
        len = (int)strlen(text);
        snprintf(text + len, sizeof(text) - len, "%s", tr(kShopConfirm[s->cursor[4]]));
        add_position(text, sizeof(text), s->cursor[4], 3);
        PortA11y_Say(text, follow());
        row = s->cursor[4];
        break;
    case SHOP_LV_BOUGHT:
        if (level != SHOP_LV_BOUGHT && s->item >= 0 && s->item < SAVE_ITEMS) {
            if (text_line(name, sizeof(name), s->itemText, s->item) == 0) {
                snprintf(name, sizeof(name), tr("Item %d"), s->item + 1);
            }
            snprintf(text, sizeof(text), tr("Bought %s. %d Z Points left. X to go on"),
                     name, gSaveData->money);
            PortA11y_Say(text, follow());
        }
        break;
    case SHOP_LV_HELP:
        if (level != SHOP_LV_HELP) {
            item_help(s->helpItem);
        }
        break;
    }
    if (s->level != SHOP_LV_CONFIRM) {
        row = -1;
    }
    plate = s->cursor[0];
    level = s->level;
}

/* The label of value `v` of the duel setting in row `row`, NULL when out of range. */
static const char *duel_value(int row, int v) {
    switch (row) {
    case 0:
        return v >= 0 && v < 5 ? tr(kDuelTime[v]) : NULL;
    case 1:
        return v >= 0 && v < 5 ? tr(kDuelCpu[v]) : NULL;
    case 2:
        return v >= 0 && v < 7 ? tr(kDuelReferee[v]) : NULL;
    case 3: /* one value per side */
    case 4:
        return (v & ~1) == 0 ? tr(kYesNo[v]) : NULL;
    }
    return NULL;
}

/* A row of the duel settings as the list shows it: the name and the saved value (one per side in row 3). */
static void duel_setting(char *out, int cap, int row) {
    const char *a, *b;
    size_t len;

    snprintf(out, cap, "%s", tr(kDuelSettings[row]));
    len = strlen(out);
    if (gSaveData == NULL || row >= 5) {
        return;
    }
    if (row == 3) {
        a = duel_value(3, gSaveData->rule[3]);
        b = duel_value(3, gSaveData->rule[4]);
        if (a != NULL && b != NULL) {
            snprintf(out + len, cap - len, tr(": 1P %s, 2P %s"), a, b);
        }
    } else if ((a = duel_value(row, gSaveData->rule[row == 4 ? 5 : row])) != NULL) {
        snprintf(out + len, cap - len, ": %s", a);
    }
}

/* The duel menu: its name when the cursor lights, then the plate under the cursor of the level in use; in the
   settings each row with its saved value, then the value list. Quiet once a battle is chosen, and while the
   "restore the defaults?" question runs (dialog() reads it). */
static void duel_menu(void) {
    static int open, level;
    static char said[256];
    A11yDuelMenu GAME_PTR m = gDuelMenu;
    char text[256];
    const char *label = NULL;
    int lv, cur, count = 0;

    if (m == NULL || !(m->flags & DUELMENU_STARTED)) {
        open = 0;
        return;
    }
    if ((m->flags & (DUELMENU_CHOSEN | DUELMENU_LEAVING)) || m->startState != 0 || m->resetState != 0) {
        said[0] = '\0'; /* the row is said again when the question closes */
        return;
    }
    lv = m->level;
    if (lv < DUEL_LEVEL_TOP || lv > DUEL_LEVEL_VALUE) {
        return;
    }
    if (!open) {
        heading(trc("screen", "Duel"));
        open = 1;
        level = lv;
        said[0] = '\0';
    }
    if (lv == DUEL_LEVEL_SETTING && level == DUEL_LEVEL_TOP) {
        heading(tr(kDuelTop[3]));
    }
    level = lv;
    text[0] = '\0';
    cur = m->sel[lv];
    switch (lv) {
    case DUEL_LEVEL_TOP:
        count = 4;
        label = cur >= 0 && cur < count ? tr(kDuelTop[cur]) : NULL;
        break;
    case DUEL_LEVEL_TYPE:
        count = 3;
        label = cur >= 0 && cur < count ? tr(kDuelType[cur]) : NULL;
        break;
    case DUEL_LEVEL_DP:
        count = 3;
        label = cur >= 0 && cur < count ? tr(kDuelDp[cur]) : NULL;
        break;
    case DUEL_LEVEL_SETTING:
        count = DUEL_SETTINGS;
        if (cur >= 0 && cur < count) {
            duel_setting(text, sizeof(text), cur);
        }
        break;
    case DUEL_LEVEL_VALUE:
        if ((m->column & ~1) != 0 || m->valueCount < 1 || m->valueCount > 7) {
            return;
        }
        cur = m->value[m->column];
        count = m->valueCount;
        label = duel_value(m->sel[DUEL_LEVEL_SETTING], cur);
        if (label != NULL && m->sel[DUEL_LEVEL_SETTING] == 3) {
            snprintf(text, sizeof(text), tr("%dP: %s"), m->column + 1, label);
        }
        break;
    }
    if (label != NULL && text[0] == '\0') {
        snprintf(text, sizeof(text), "%s", label);
    }
    if (text[0] == '\0') {
        return;
    }
    add_position(text, sizeof(text), cur, count);
    if (strcmp(text, said) != 0) {
        snprintf(said, sizeof(said), "%s", text);
        PortA11y_Say(text, follow());
    }
}

/* ---- the options screen ---- */

/* include/menu/menu_x.h: Option (src/menu/menu_x_c.c, Option_Input), mode 62. The rows, the choices and the
   volumes are pictures; the guide's line under each row says what it does, not its name or value. */
typedef struct {
    char unk0[0x10];
    uint32_t flags;          /* 0x010: flash[0].flags, OPTION_PAD: the screen takes input */
    char unk14[0xF0];
    int cursor;              /* 0x104: OPT_ the row under the cursor */
    int state;               /* 0x108: OPT_; equal to a row number, that row is open */
    char unk10C[4];
    int bgmVolume;           /* 0x110: the volumes when their window opened or X was last pressed in it */
    int seVolume;            /* 0x114 */
    int bgmCursor;           /* 0x118: the sound test's row */
    char unk11C[0xC];
    int value;               /* 0x128: a two-way choice's column, the volume row (0 music, 1 effects), the player */
    char unk12C[0xC];
    int pad;                 /* 0x138: the controller page's player */
    char unk13C[0x10];
    int type;                /* 0x14C: the screen type shown, 0..2 */
    char unk150[0x38];
    int mcBusy;              /* 0x188: a memory card flow runs; dialog() reads it */
    char unk18C[0x10];
    int GAME_PTR bgmIds;     /* 0x19C: the sound test's list (values: kSongNames) */
    int bgmCount;            /* 0x1A0 */
} A11yOption;
LAYOUT_ASSERT(offsetof(A11yOption, cursor) == 0x104 && offsetof(A11yOption, value) == 0x128 &&
                   offsetof(A11yOption, type) == 0x14C && offsetof(A11yOption, bgmCount) == 0x1A0,
               "Option");
extern A11yOption GAME_PTR gOption; /* NULL outside the screen */
#define OPTION_PAD 2 /* MFLASH_PAD */
#define OPT_ST_TOP 0
#define OPT_ITEM_SAVE 1
#define OPT_ITEM_SCREEN 2
#define OPT_ITEM_SOUND 3
#define OPT_ITEM_CTRL 4
#define OPT_ITEM_TYPE 7
#define OPT_ITEM_SCR2 8
#define OPT_ITEM_STEREO 12
#define OPT_ITEM_VOLUME 13
#define OPT_ITEM_BGM 14
#define OPT_ITEM_VOICE 15
#define OPT_ITEM_KEYS 17
#define OPT_ITEM_VIB 18
#define OPT_ST_VIB_EDIT 20
#define OPT_ST_KEYS_PAD 21
#define OPT_ITEMS 20

/* By OPT_ number; NULL: not a row. The three pages start at 7, 12 and 17. */
static const char *const kOptionRows[OPT_ITEMS] = {
    NULL, "Save / Load", "Screen", "Sound", "Controller", "Exit", NULL, "Screen type", "Screen setting 2",
    "Screen position", "Default", NULL, "Stereo / Mono", "Volume", "Sound test", "Voices", "Default",
    "Button setup", "Vibration", "Default",
};
static const char *const kOptionSave[2] = {"Save", "Load"};
static const char *const kOptionScreenType[3] = {"Type 1", "Type 2", "Type 3"};
static const char *const kOptionScreen2[2] = {"Choice 1", "Choice 2"};
static const char *const kOptionStereo[2] = {"Stereo", "Mono"};
static const char *const kOptionVoice[2] = {"English", "Japanese"};
static const char *const kOptionOnOff[2] = {"Off", "On"};
static const char *const kOptionKeys[2] = {"default buttons", "own buttons, X edits them"};

/* The first row of a row's page and how many rows it has. */
static int option_page(int row, int *count) {
    if (row >= 1 && row <= 5) {
        *count = 5;
        return 1;
    }
    if (row >= 7 && row <= 10) {
        *count = 4;
        return 7;
    }
    if (row >= 12 && row <= 16) {
        *count = 5;
        return 12;
    }
    if (row >= 17 && row <= 19) {
        *count = 3;
        return 17;
    }
    return -1;
}

/* What the open row shows: the choice under the cursor, the volume being moved, the song under the cursor.
   Empty for what other readers say (the reset questions, the memory card) and for the screen position. */
static void option_open(char *text, int cap, A11yOption GAME_PTR o) {
    int v = o->value, two = (v & ~1) == 0;

    text[0] = '\0';
    switch (o->state) {
    case OPT_ITEM_SAVE:
        if (two) {
            snprintf(text, cap, "%s", tr(kOptionSave[v]));
            add_position(text, cap, v, 2);
        }
        break;
    case OPT_ITEM_TYPE:
        if (o->type >= 0 && o->type < 3) {
            snprintf(text, cap, "%s", tr(kOptionScreenType[o->type]));
            add_position(text, cap, o->type, 3);
        }
        break;
    case OPT_ITEM_SCR2:
        if (two) {
            snprintf(text, cap, "%s", tr(kOptionScreen2[v]));
            add_position(text, cap, v, 2);
        }
        break;
    case OPT_ITEM_STEREO:
        if (two) {
            snprintf(text, cap, "%s", tr(kOptionStereo[v]));
            add_position(text, cap, v, 2);
        }
        break;
    case OPT_ITEM_VOLUME:
        if (two && gSaveData != NULL) {
            /* 0..9, and the game wraps from 9 to 0 (Option_Input, src/menu/menu_x_c.c:646): the top is said */
            snprintf(text, cap, tr("%s, %d of 9"),
                     v ? tr("Effects") : tr("Music"),
                     v ? gSaveData->seVolume : gSaveData->bgmVolume);
        }
        break;
    case OPT_ITEM_BGM:
        if (o->bgmIds != NULL && o->bgmCount > 0 && o->bgmCount <= 64 && o->bgmCursor >= 0 && o->bgmCursor < o->bgmCount) {
            snprintf(text, cap, "%s", song_name(o->bgmIds[o->bgmCursor]));
            add_position(text, cap, o->bgmCursor, o->bgmCount);
        }
        break;
    case OPT_ITEM_VOICE:
        if (two) {
            snprintf(text, cap, "%s", tr(kOptionVoice[v]));
            add_position(text, cap, v, 2);
        }
        break;
    case OPT_ITEM_KEYS:
    case OPT_ITEM_VIB:
        if (two) {
            snprintf(text, cap, tr("Player %d"), v + 1);
            add_position(text, cap, v, 2);
        }
        break;
    case OPT_ST_KEYS_PAD: /* 0 the default buttons, 1 the player's own, which X opens to edit (Option_Input) */
        if (two && (o->pad & ~1) == 0) {
            snprintf(text, cap, tr("Player %d: %s"), o->pad + 1, tr(kOptionKeys[v]));
        }
        break;
    case OPT_ST_VIB_EDIT:
        if (two && (o->pad & ~1) == 0) {
            snprintf(text, cap, tr("Player %d, vibration: %s"), o->pad + 1,
                     tr(kOptionOnOff[v]));
        }
        break;
    }
}

/* The options screen: its name, then on each page the row under the cursor; an open row says its choice, the
   volume window its number. X keeps a volume and leaves the window open; triangle leaves and puts back the last
   kept volumes (Option_Input, case OPT_ITEM_VOLUME), so that is said: once the first time the window opens, and
   when leaving drops a change. */
static void options(void) {
    static int open, page, volume, hinted, kept[2], shown[2];
    static char said[256];
    A11yOption GAME_PTR o = gOption;
    char text[256];
    int first, count, st;

    if (o == NULL || !(o->flags & OPTION_PAD)) {
        open = 0;
        return;
    }
    if (!open) {
        heading(tr("Options"));
        open = 1;
        page = 1;
        volume = 0;
        said[0] = '\0';
    }
    if (o->mcBusy) {
        said[0] = '\0'; /* the row is said again when the memory card closes */
        return;
    }
    st = o->state;
    if (volume && st != OPT_ITEM_VOLUME) {
        volume = 0;
        if (shown[0] != kept[0] || shown[1] != kept[1]) {
            heading(tr("Volume change dropped, it was not kept with X"));
        }
    }
    text[0] = '\0';
    if (st == OPT_ST_TOP || st == OPT_ITEM_SCREEN || st == OPT_ITEM_SOUND || st == OPT_ITEM_CTRL) {
        first = option_page(o->cursor, &count);
        if (first < 0 || o->cursor >= OPT_ITEMS || kOptionRows[o->cursor] == NULL) {
            return;
        }
        if (first != page) {
            heading(first == 1 ? tr("Options") : tr(kOptionRows[st]));
            page = first;
        }
        snprintf(text, sizeof(text), "%s", tr(kOptionRows[o->cursor]));
        add_position(text, sizeof(text), o->cursor - first, count);
    } else {
        if (st == OPT_ITEM_VOLUME) {
            if (!volume) {
                volume = 1;
                kept[0] = o->bgmVolume;
                kept[1] = o->seVolume;
                if (!hinted) {
                    hinted = 1;
                    heading(tr("Volume. Left and right change it, X keeps it, triangle leaves and undoes what was not kept"));
                }
            } else if (o->bgmVolume != kept[0] || o->seVolume != kept[1]) {
                kept[0] = o->bgmVolume;
                kept[1] = o->seVolume;
                PortA11y_Say(tr("Kept"), A11Y_INTERRUPT);
            }
            if (gSaveData != NULL) {
                shown[0] = gSaveData->bgmVolume;
                shown[1] = gSaveData->seVolume;
            }
        }
        option_open(text, sizeof(text), o);
        if (text[0] == '\0') {
            said[0] = '\0'; /* the row is said again when the question or the screen position closes */
        }
    }
    if (text[0] != '\0' && strcmp(text, said) != 0) {
        snprintf(said, sizeof(said), "%s", text);
        PortA11y_Say(text, follow());
    }
}

/* ---- the battle's pause and result menus ---- */

/* include/battle/hud_0.h: BtlMenuItem, BtlMenu and BtlMenuWork (src/battle/hud_0_b.c, the menu engine), only what
   is read. */
typedef struct {
    int16_t text[2];      /* 0x00: text number per controller port (entry n + 8 of the battle's text table); negative:
                             a picture */
    char unk4[4];
    void GAME_PTR sub;    /* 0x08: menu opened by confirm */
    char unkC[8];
    int type;             /* 0x14 */
    char unk18[4];
    int disabled;         /* 0x1C: the cursor steps over it (the menus' first item, their title) */
    char unk20[0x10];
} A11yBtlMenuItem;
LAYOUT_ASSERT(sizeof(A11yBtlMenuItem) == 0x30, "BtlMenuItem");
typedef struct {
    A11yBtlMenuItem GAME_PTR items; /* 0x00 */
    int count;                      /* 0x04 */
    char unk8[0xC];
    int open;                       /* 0x14: index of the item whose sub menu is open, -1 none */
    int cursor;                     /* 0x18 */
    int state;                      /* 0x1C */
} A11yBtlMenu;
LAYOUT_ASSERT(sizeof(A11yBtlMenu) == 0x20, "BtlMenu");
typedef struct {
    char unk0[8];
    int active;              /* 0x08: the menu is shown */
    int pad;                 /* 0x0C: controller port that owns it (-1: nobody) */
    char unk10[4];
    A11yBtlMenu GAME_PTR top; /* 0x14: the tree being shown */
} A11yBtlMenuWork;
LAYOUT_ASSERT(sizeof(A11yBtlMenuWork) == 0x18, "BtlMenuWork");
extern A11yBtlMenuWork GAME_PTR gBtlMenu; /* NULL outside a battle */
extern int gPauseMenuWhich;              /* src/battle/hud_0.c: 0 the pause menu, 1 the result menu */
#define BTL_MENU_ITEM_SKILLS 3   /* as the first item: the page is the skill list */
#define BTL_MENU_ITEM_CONTROLS 4 /* as the first item: the page is the controls */
#define BTL_MENU_STATE_ACTIVE 4
extern void PortA11y_SkillList(int shown); /* a11y_skills.c: the skill list, one move at a time */
extern int PortA11y_SkillListShown(void);

/* include/sys/common.h: CommonRes; in a battle, 0x28 is its common file, whose offset table holds the menus' text
   (src/battle/hud_0_b.c, BtlMenu_GetText). */
extern struct { char unk0[0x28]; uint32_t GAME_PTR battle; } GAME_PTR gCommonRes;

/* include/battle/battle.h: BattleWork, its result block (+0x1938, Battle_GetResult: who won and why, filled by the
   fight's sequence when it ends, src/battle/btl_seq.c), its flags and `running` (see battle_watch below). */
extern struct {
    char unk0[0x1938];
    int winner; /* 0x1938: BTL_RESULT_ bits: 1 side 0 won, 2 side 1, 4 a draw, 8 left from the pause, 0x10 a replay's */
    int reason; /* 0x193C: BTL_REASON_ bits: 1 K.O., 2 time up, 4 out of the ring (fighter flag 7) */
    char unk1940[0x19F0 - 0x1940];
    uint64_t flags;
    int running;
} gBattleWork;
_Static_assert(offsetof(__typeof__(gBattleWork), reason) == 0x193C && offsetof(__typeof__(gBattleWork), flags) == 0x19F0, "BattleWork");
#define BATTLE_FLAG_PAUSE_MENU 0x4000 /* the pause menu is open */
#define BATTLE_FLAG_PAUSE 0x100 /* the fight is frozen (offline, set with the pause menu) */

/* gs/net.c: 1 in an online match, and which player this copy plays there. src/battle/hud_e_d.c: the port whose
   player opened the pause menu (it only reads). */
extern int Port_NetActive(void), Port_NetMe(void);
extern int BtlPause_GetMenuPad(void);

/* The menu of the battle's tree that takes input (the deepest one open), or NULL when none is shown. Online the
   pause menu belongs to the player who opened it: the fight goes on under it and it is not drawn on the other
   screen (src/battle/hud_0.c, PauseMenu_Draw), so there it is not a menu for this player either: it is not read
   and the gauges and the cues go on. The menu after the fight is both players'. */
static A11yBtlMenu GAME_PTR btl_menu_shown(void) {
    A11yBtlMenuWork GAME_PTR w = gBtlMenu;
    A11yBtlMenu GAME_PTR m;
    int depth;

    if (w == NULL || !w->active || w->top == NULL) {
        return NULL;
    }
    if (Port_NetActive() && gPauseMenuWhich == 0 && (gBattleWork.flags & BATTLE_FLAG_PAUSE_MENU) &&
        BtlPause_GetMenuPad() != Port_NetMe()) {
        return NULL;
    }
    m = w->top;
    for (depth = 0; depth < 8 && m->open >= 0 && m->open < m->count && m->items[m->open].sub != NULL; depth++) {
        m = (A11yBtlMenu GAME_PTR)m->items[m->open].sub;
    }
    return m->count > 0 && m->count < 64 ? m : NULL;
}

/* 1: the menu is the skill list or the controls page, which are not a list of items (left to shown_text). */
static int btl_menu_page(A11yBtlMenu GAME_PTR m) {
    return m->items[0].type == BTL_MENU_ITEM_SKILLS || m->items[0].type == BTL_MENU_ITEM_CONTROLS;
}

/* Item i's text, as the engine draws it for the menu's owner. */
static void btl_menu_item(char *out, int cap, A11yBtlMenu GAME_PTR m, int i) {
    uint32_t GAME_PTR file = gCommonRes != NULL ? gCommonRes->battle : NULL;
    uint32_t ofs;
    int pad = gBtlMenu->pad < 0 || gBtlMenu->pad > 1 ? 0 : gBtlMenu->pad, n = m->items[i].text[pad];

    out[0] = '\0';
    if (file != NULL && n >= 0 && n < 0x1000) {
        ofs = file[n + 8] >> 2 << 2;
        if (ofs != 0 && ofs < 0x200000) {
            PortA11y_Utf8(out, cap, (const uint16_t *)(uint16_t GAME_PTR)((uint8_t GAME_PTR)file + ofs));
        }
    }
    if (out[0] == '\0') {
        snprintf(out, cap, trc("menu", "Item %d"), i + 1);
    }
}

/* The pause menu and the menu after a fight: its name when it opens, then the item under the cursor, one at a time
   (the engine draws the whole list at once, which the text reader would say as one message). */
static void btl_menu(void) {
    static A11yBtlMenu GAME_PTR said; /* the menu last spoken for */
    static int saidCursor, named;
    A11yBtlMenu GAME_PTR m = btl_menu_shown();
    char text[256];
    int i, pos = 0, count = 0;

    PortA11y_SkillList(m != NULL && m->items[0].type == BTL_MENU_ITEM_SKILLS && m->state == BTL_MENU_STATE_ACTIVE);
    if (m == NULL) {
        said = NULL;
        named = 0;
        return;
    }
    if (btl_menu_page(m)) {
        said = m; /* (back from it, the item is said again) */
        return;
    }
    if (m->state != BTL_MENU_STATE_ACTIVE || m->cursor < 0 || m->cursor >= m->count) {
        return;
    }
    if (!named) {
        heading(gPauseMenuWhich ? tr("Fight over") : tr("Pause"));
        named = 1;
    }
    if (m != said || m->cursor != saidCursor) {
        for (i = 0; i < m->count; i++) { /* (counted among the items the cursor stops on) */
            if (!m->items[i].disabled) {
                pos += i < m->cursor;
                count++;
            }
        }
        btl_menu_item(text, sizeof(text), m, m->cursor);
        say_item(text, pos, count, follow());
        said = m;
        saidCursor = m->cursor;
    }
}

/* ---- the voices, and the text on screens without a reader of their own ---- */

/* include/battle/battle.h: BattleWork; `running` is set to 1 at Battle_Main's entry and to 0 at its exit
   (src/battle/battle.c), but it does not stay 1 in between in every mode: a battle that a script sets up (the
   story's) has the whole work zeroed a few blanks in, while the battle still loads (BtlScriptCmd_SetRule ->
   Battle_ClearWork, src/battle/btl_script_cmd.c). The fighters' roster and the battle's sequence are there from
   Battle_Init, after the loading, to Battle_Term in every mode, and NULL outside (src/battle/btl_char_mgr.c:
   BtlChar_AllocAll / BtlChar_FreeAll; src/battle/btl_seq.c: BtlSeq_Init / BtlSeq_Term). Only compared with NULL
   here. */
extern void GAME_PTR gBtlChars;
extern void GAME_PTR gBtlSeq;

static int sInBattle;   /* the game is in Battle_Main, as far as it can be told (battle_watch) */
static int sRosterSeen; /* this battle has had its roster */

/* Once per blank: whether the game is in a battle, from Battle_Main's entry (the loading before the fight too) to
   its end. It starts with `running` (or the roster); from there `running` is not asked again until the roster has
   come, and it is over when the roster is gone and `running` is 0, which is Battle_Main's exit, or the freeing of
   the fighters at the start of Battle_Term where a script had zeroed `running`. */
static void battle_watch(void) {
    int running = gBattleWork.running != 0, roster = gBtlChars != NULL;

    if (!sInBattle) {
        sInBattle = running || roster;
        sRosterSeen = roster;
    } else if (roster) {
        sRosterSeen = 1;
    } else if (sRosterSeen && !running) {
        sInBattle = 0;
    }
}

static int in_battle(void) {
    return sInBattle;
}

/* include/battle/hud_a.h: Hud, only its visibility bits. The fight shows the gauges from its "ready" state to the
   finish (src/battle/btl_seq.c, BtlSeqReady_Enter and the finish's Hud_ShowGauges(0)). */
typedef struct {
    char unk0[0x30];
    uint32_t flags; /* 0x30: HUD_SHOW_* */
} A11yHud;
extern A11yHud GAME_PTR gHud; /* allocated by each battle and never set back to NULL: see fighting() */
#define HUD_SHOW_GAUGES 0x01

/* 1: a fight is on, with its gauges on screen. gHud keeps the address of the last battle's HUD after it is freed,
   so it is read only while this battle's is there: it is made before the sequence (BtlGame_Init: Hud_Init, then
   BtlSeq_Init) and freed after the roster (Battle_Term: BtlChar_FreeAll, then BtlGame_Term), so with both of those
   not NULL it is this battle's. */
static int fighting(void) {
    A11yHud GAME_PTR h = gBtlChars != NULL && gBtlSeq != NULL ? gHud : NULL;

    return h != NULL && (h->flags & HUD_SHOW_GAUGES);
}

/* include/sys/file.h: AdxPlayer, one per channel (include/sys/adx.h); `adxt` is the port's player
   (gs/snd_adx.c, ADXT_Create). */
typedef struct {
    char unk0[0x10];
    void GAME_PTR adxt; /* 0x10 */
} A11yAdxPlayer;
LAYOUT_ASSERT(sizeof(A11yAdxPlayer) == 0x14, "AdxPlayer");
extern A11yAdxPlayer gAdxPlayerTbl[6];
#define ADX_CH_MUSIC 0 /* "BGM": the music */
#define ADX_CH_STAGE 1 /* "MAP": a stage's background sound */
#define ADX_CH_VOICE0 4
#define ADX_CH_VOICE1 5

/* Voices by id of partition 2 (file number + 0xD48, gs/snd_adx.c): the story's lines and the menu guides' (both
   voice sets) are the ones the game also shows as text; the fighters' and the announcer's come after them. */
#define VOICE_SUBTITLED_FIRST 0xD48
#define VOICE_SUBTITLED_END 0x8D4E

#define SUBTITLE_BEFORE 10 /* blanks: a line shown this soon after such a voice started is what it says */
#define TEXT_STABLE 20     /* blanks a line is shown before it is said (its voice may start late; a line typed in
                              letter by letter is whole by then) */
#define TEXT_FORGET 30     /* blanks a line must be gone to be new when it shows again */
#define TEXT_SLOTS 64

static int sOn;                  /* set by the first blank with the speaker on */
static int sBlank;               /* blanks counted since then */
static int sVoiceBlank = -1000;  /* the blank the last voice with a shown line started in */
static int sMenuVoices = 100;    /* bt3_settings.txt a11y_menu_voices, percent */
static int sFightVoices = 100;   /* bt3_settings.txt a11y_fight_voices, percent (default: a11y_menu_voices) */
static int sFightMusic = 100;    /* bt3_settings.txt a11y_fight_music, percent */

/* gs/snd_adx.c, ADXT_StartAfs: the game started a stream (path: its file). */
void PortA11y_StreamStart(const char *path) {
    const char *dir;
    char note[64];
    int id;

    if (!sOn || gPortResim || path == NULL || (dir = strstr(path, "pzs3us2/")) == NULL ||
        sscanf(dir + 8, "%d.bin", &id) != 1) {
        return;
    }
    id += 0xD48;
    if (id >= VOICE_SUBTITLED_FIRST && id < VOICE_SUBTITLED_END) {
        sVoiceBlank = sBlank;
        snprintf(note, sizeof(note), "voice %#x", id);
        PortA11y_Note(note);
    }
}

/* gs/a11y.c, PortA11y_StreamGain (for gs/snd_adx.c, apply_volume): the percent of the game's own volume one of its
   players gets, or -1 to leave it alone. The two voice channels play at a11y_menu_voices percent, so the screen
   reader can be heard over the guides, and in a battle at a11y_fight_voices (the same unless set: the fighters'
   voices do not jump up as the battle starts). In a battle the music and the stage's background (the first two
   players) play at a11y_fight_music percent, so that the accessibility's sounds are heard over them; it is the same
   from the battle's loading to its result menu, so it does not jump while the fight is on. An int, not a gain: this
   file is built soft-float and gs/ is not, so a float returned from here would not reach the caller (it would read
   another register). */
int PortA11y_VoicePercent(const void *player) {
    if (!sOn || player == NULL) {
        return -1;
    }
    if (player == (const void *)gAdxPlayerTbl[ADX_CH_VOICE0].adxt ||
        player == (const void *)gAdxPlayerTbl[ADX_CH_VOICE1].adxt) {
        return in_battle() ? sFightVoices : sMenuVoices;
    }
    if (in_battle() && sFightMusic != 100 &&
        (player == (const void *)gAdxPlayerTbl[ADX_CH_MUSIC].adxt || player == (const void *)gAdxPlayerTbl[ADX_CH_STAGE].adxt)) {
        return sFightMusic;
    }
    return -1;
}

/* For the settings window: the music's percent in a battle, and setting it (stored, saved by Port_SettingsWrite). */
int PortA11y_FightMusic(void) {
    int v;

    if (sOn) {
        return sFightMusic;
    }
    v = Port_Setting("a11y_fight_music", 100);
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

void PortA11y_SetFightMusic(int percent) {
    sFightMusic = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    Port_SettingSave("a11y_fight_music", sFightMusic);
    Port_AudioRefresh();
}

/* For the settings window (gs/a11y_ui.cpp): the voices' percent in menus (fight 0) or in a battle (fight 1), and
   setting it, which stores it (Port_SettingsWrite saves it) and gives the playing streams their volume again. */
int PortA11y_Voices(int fight) {
    int menu, v;

    if (sOn) {
        return fight ? sFightVoices : sMenuVoices;
    }
    v = Port_Setting("a11y_menu_voices", 50);
    menu = v < 0 ? 0 : v > 100 ? 100 : v;
    if (!fight) {
        return menu;
    }
    v = Port_Setting("a11y_fight_voices", menu);
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

void PortA11y_SetVoices(int fight, int percent) {
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    if (fight) {
        sFightVoices = percent;
    } else {
        sMenuVoices = percent;
    }
    Port_SettingSave(fight ? "a11y_fight_voices" : "a11y_menu_voices", percent);
    Port_AudioRefresh();
}

/* 1: a line first shown at blank `first` is what a voice says (it started a little before or since). */
static int subtitle(int first) {
    return sVoiceBlank >= first - SUBTITLE_BEFORE;
}

/* The message window's line, whenever it shows another one: once it has stayed TEXT_STABLE blanks, and not when a
   voice says it (the main menu's guides); queued behind the item it is about. */
static void msg_window(void) {
    extern int PortTbo_BoxQuiet(void); /* src/menu/port_tbo.c: the online pages' line said once elsewhere */
    static uint16_t GAME_PTR shown;
    static int first, done;
    A11yMsgWin GAME_PTR w = gMsgWin;
    uint16_t GAME_PTR str = w != NULL && w->text != NULL ? w->str : NULL;
    char text[A11Y_TEXT];

    if (str != shown) {
        shown = str;
        first = sBlank;
        done = 0;
        return;
    }
    if (done || shown == NULL || sBlank - first < TEXT_STABLE) {
        return;
    }
    done = 1;
    if (shown[0] != 0xFEFF) {
        return;
    }
    if (subtitle(first)) {
        PortA11y_Note("message window: a voice says it");
    } else if (PortTbo_BoxQuiet()) {
        PortA11y_Note("message window: said elsewhere (online: the rival found, the match's banner)");
    } else if (PortA11y_Utf8(text, sizeof(text), (const uint16_t *)shown) > 0) {
        PortA11y_Say(text, 0);
    }
}

/* Screens with no reader of their own, by mode (docs/systems/menu_overlay.md): the main menu's name for the first
   screen of a mode (item), or a short description. "Not read yet" where a choice is made that is not read. */
#define MODE_TEAM_PICK 15 /* a team (UbTeamSel, not read yet) or one fighter (SoloSel, solo_select) */
#define TEAM_SIZE (0x644 / 4) /* gProgress, in ints: fighters to choose there (menu_o.h) */
static const struct {
    int mode, item;
    const char *text;
} kModeNames[] = {
    {6, 0, "choose a saga"},
    {7, -1, "Episode list"},
    {8, -1, "Episode result"},
    {9, -1, "Saga ending"},
    {10, -1, "Save"},
    {13, 1, NULL},
    {14, -1, "Choose a mission"},
    {MODE_TEAM_PICK, -1, "Choose your fighters, not read yet"},
    {16, -1, "Mission result"},
    {17, -1, "Survival"},
    {19, -1, "Survival result"},
    {20, -1, "Sim Dragon"},
    {22, -1, "Sim Dragon, the day"},
    {23, -1, "Day result"},
    {24, -1, "Disc Fusion"},
    {26, -1, "Ranking"},
    {27, -1, "Result"},
    {28, -1, "Choose a course"},
    {30, -1, "Result"},
    {33, 2, NULL},
    {35, -1, "Tournament bracket"},
    {40, -1, "Team select"},
    {44, 6, NULL},
    {48, 5, NULL},
    {53, 7, NULL},
    {60, 8, NULL},
    {62, 9, NULL},
    {70, 10, NULL},
};
#define MODE_DUEL_MENU 38
#define MODE_DUEL_CHARSEL 39
#define MODE_PORT_SCREEN 80 /* the port's own screens (include/menu/port_screen.h) */
#define MODE_TRAIN_CHARSEL 45
#define MODE_OPTION 62

/* 1: the game set this mode as it left for a fight, whose result it shows after (the episode's, the mission's...):
   said when the fight is over, not before. */
static int after_fight(int mode) {
    return mode == 8 || mode == 16 || mode == 19 || mode == 23 || mode == 27 || mode == 30;
}

/* The description of a mode into out; 0 when it has none. */
static int mode_text(char *out, int cap, int mode) {
    size_t i;
    int n;

    if (mode == MODE_TEAM_PICK && gProgress != NULL && ((int GAME_PTR)gProgress)[TEAM_SIZE] < 2) {
        return 0;
    }
    for (i = 0; i < sizeof(kModeNames) / sizeof(kModeNames[0]); i++) {
        if (kModeNames[i].mode != mode) {
            continue;
        }
        n = snprintf(out, cap, "%s", kModeNames[i].item >= 0 ? tr(kMainMenuItems[kModeNames[i].item]) : "");
        if (kModeNames[i].text != NULL) {
            snprintf(out + n, cap - n, "%s%s", n > 0 ? ", " : "", tr(kModeNames[i].text));
        }
        return 1;
    }
    return 0;
}

/* Notes (in the log) where the game goes; the description of a screen with no reader of its own; the voices' volume
   when a fight starts or ends. */
static void screen_change(void) {
    static int mode = -1, fight = -1;
    char text[256];
    int m = gProgress != NULL ? gProgress->mode : -1, f = in_battle();

    if (f != fight) {
        if (fight != -1) {
            PortA11y_Note(f ? "fight" : "fight over");
            Port_AudioRefresh();
            if (!f && mode_text(text, sizeof(text), m)) {
                heading(text);
            }
        }
        fight = f;
    }
    if (m != mode) {
        snprintf(text, sizeof(text), "mode %d", m);
        PortA11y_Note(text);
        mode = m;
        if (!f && !after_fight(m) && mode_text(text, sizeof(text), m)) {
            heading(text);
        }
    }
}

/* The reward window (include/battle/view_a.h, GetWin): a prize per Setup, in the story's result, the tournament's
   prizes and the dragon's wishes. Its title strip is a picture of the kind (src/battle/view_a.c GetWin_Draw); its
   lines are lines of the screen's text files (box1 / box2 name the file of each), but the Z points' amount is a
   number drawn as an image. */
typedef struct {
    uint32_t GAME_PTR text[5];    /* 0x000: the screen pack's sections 8, 7, 9, 11, 11 */
    char unk14[0x334 - 0x14];
    int kind;                     /* 0x334: GETWIN_ */
    int box1, box2;               /* 0x338 */
    int line1, line2;             /* 0x340: -1 = none; GETWIN_MONEY: line2 is the amount */
    int tall;                     /* 0x348 */
    int se;                       /* 0x34C: 0 until the first Setup (Init clears the window) */
} A11yGetWin;
LAYOUT_ASSERT(sizeof(A11yGetWin) == 0x350 && offsetof(A11yGetWin, kind) == 0x334 && offsetof(A11yGetWin, se) == 0x34C,
              "GetWin");
extern A11yGetWin GAME_PTR gGetWin; /* NULL outside those screens */
#define GETWIN_CHARA 0
#define GETWIN_STAGE 1
#define GETWIN_ITEM 3 /* 3..5, by the item's kind */
#define GETWIN_MONEY 6
#define GETWIN_SAGA 7
#define GETWIN_EPISODE 8
#define GETWIN_BALL 9

/* 1: the string at `str` is a line the reward window shows now (get_win says it, not shown_text). */
static int getwin_line(uint32_t str) {
    A11yGetWin GAME_PTR g = gGetWin;
    uint32_t a;
    int k, box;

    if (g == NULL || g->se == 0 || str == 0) {
        return 0;
    }
    for (k = 0; k < 2; k++) {
        box = k ? g->box2 : g->box1;
        if (box < 0 || box >= 5 || (k && g->kind == GETWIN_MONEY)) {
            continue;
        }
        a = (uint32_t)(uintptr_t)text_addr(g->text[box], k ? g->line2 : g->line1);
        if (a != 0 && (str == a || str == a + 2)) { /* (with or without the U+FEFF) */
            return 1;
        }
    }
    return 0;
}

/* Each prize as the window shows it: its kind and its lines. The first one is queued behind what the screen said;
   the next ones, which the player calls with the confirm button, cut the last. */
static void get_win(void) {
    static uint32_t win;
    static int kind, line1, line2, said;
    A11yGetWin GAME_PTR g = gGetWin;
    char text[A11Y_TEXT], first[256], second[256];
    const char *label, *sep;

    if (g == NULL || g->se == 0) {
        win = 0;
        said = 0;
        return;
    }
    if ((uint32_t)(uintptr_t)g == win && g->kind == kind && g->line1 == line1 && g->line2 == line2) {
        return;
    }
    win = (uint32_t)(uintptr_t)g;
    kind = g->kind;
    line1 = g->line1;
    line2 = g->line2;
    /* The title strips' words (the Spanish disc's; the two the strip cuts short, "desbloq." and "desbloque.", are
       written out, and its "Bola Dragón" is said "Esfera del Dragón", the name Latin America knows, by the owner's
       choice). Of the English, only "Battle Unlocked!" (a recording of the game, docs/port/README.md) and the
       term Z Points are the game's; the rest is our wording, as the US disc was not at hand. Z Points is also
       what the save's money is called by the game's own counters (mc_zpoint_suji, mc_num_use_zp) and by the US
       players' guides, the item shop's prices included; the US manual's "Zenie" for the shop is not followed. */
    switch (kind) {
    case GETWIN_CHARA: label = tr("Character unlocked!"); break;
    case GETWIN_STAGE: label = tr("Battle stage unlocked!"); break;
    case GETWIN_ITEM:
    case GETWIN_ITEM + 1:
    case GETWIN_ITEM + 2: label = tr("Item unlocked!"); break;
    case GETWIN_MONEY: label = tr("Z Points obtained!"); break;
    case GETWIN_SAGA: label = tr("Scenario unlocked!"); break;
    case GETWIN_EPISODE: label = tr("Battle Unlocked!"); break;
    case GETWIN_BALL: label = tr("You got a Dragon Ball"); break;
    default: label = NULL; break;
    }
    sep = label != NULL && label[0] != '\0' && label[strlen(label) - 1] == '!' ? " " : ": ";
    if (kind == GETWIN_MONEY) {
        snprintf(text, sizeof(text), "%s %d", label, line2);
    } else if (label == NULL) {
        PortA11y_Note("reward window: a kind this reader does not know");
        return;
    } else {
        text_line(first, sizeof(first), g->box1 >= 0 && g->box1 < 5 ? g->text[g->box1] : NULL, line1);
        text_line(second, sizeof(second), g->box2 >= 0 && g->box2 < 5 ? g->text[g->box2] : NULL, line2);
        if (first[0] != '\0' && second[0] != '\0') {
            snprintf(text, sizeof(text), "%s%s%s, %s", label, sep, first, second);
        } else if (first[0] != '\0' || second[0] != '\0') {
            snprintf(text, sizeof(text), "%s%s%s", label, sep, first[0] != '\0' ? first : second);
        } else {
            snprintf(text, sizeof(text), "%s", label);
        }
    }
    PortA11y_Say(text, said ? A11Y_INTERRUPT : 0);
    said = 1;
}

/* The score sheet of the mission, survival, Sim Dragon and ranking results (include/menu/menu_p.h UbScore; the
   other three have its layout): lines counted into a total, pages of bonus plates, the rank, and the total paid out
   as Z points. Every number on it is an image (Num_Draw); the bonus names are lines of the screen's text. */
typedef struct {
    int value;                    /* what was measured; a bonus: its line of the screen's text - 0x20 */
    int remain;                   /* not counted into the total yet */
    int points;                   /* shown * 100 */
} A11yScoreLine;
typedef struct {
    int total;                    /* 0x000: shown * 100; paid out as that many Z points */
    int saved;                    /* 0x004: the total, copied as the pay-out starts */
    int convert;                  /* 0x008 */
    int unkC;
    A11yScoreLine line[4];        /* 0x010: health left (percent), longest combo, biggest damage, (survival) foes beaten */
    A11yScoreLine bonus[48];      /* 0x040: three to a page */
    int shown[3];                 /* 0x280 */
    int lineCount;                /* 0x28C */
    int bonusCount;               /* 0x290 */
    unsigned ticks;               /* 0x294: the fight's clock */
    short hours, minutes, seconds, ms, timeLeft, unk2A2;
    int rank;                     /* 0x2A4: 0 none, 1 (the lowest) .. 4: a column of the mc_rankmoji_anime strip */
} A11yScore;
LAYOUT_ASSERT(sizeof(A11yScore) == 0x2A8 && offsetof(A11yScore, bonus) == 0x40 && offsetof(A11yScore, lineCount) == 0x28C &&
              offsetof(A11yScore, hours) == 0x298 && offsetof(A11yScore, rank) == 0x2A4, "UbScore");
extern uint8_t GAME_PTR gMisResult;  /* src/menu/menu_p_b.c; each NULL outside its screen */
extern uint8_t GAME_PTR gSurvResult; /* menu_s_c.c */
extern uint8_t GAME_PTR gSimResult;  /* menu_r_c.c */
extern uint8_t GAME_PTR gUbResult;   /* menu_n_d.c */

/* Where each screen keeps its sheet's state (MisResult, SurvResult, SimResult, UbResult): the lines count in state 1,
   the pages turn in state 2, then the pay-out, whose sub step 4 waits with the money paid. */
typedef struct {
    uint32_t state, pages, page, record, score, step; /* offsets in the screen's work; record 0: it has none */
    int money;                    /* the pay-out's state */
} A11yResultAt;
static const A11yResultAt kResultAt[4] = {
    {0x524, 0x544, 0x548, 0x554, 0x56C, 0x818, 3}, /* include/menu/menu_p.h */
    {0x50C, 0x52C, 0x530, 0x538, 0x544, 0x7F0, 3}, /* menu_s.h */
    {0x544, 0x564, 0x568, 0, 0x57C, 0x828, 7},     /* menu_r.h (pages: the last page, as in the others) */
    {0x490, 0x4B0, 0x4B4, 0x4BC, 0x4D0, 0x77C, 3}, /* menu_o.h */
};
#define RESULT_COUNT 1
#define RESULT_PAGES 2
#define RESULT_PAID 4
#define RESULT_TEXT 0x00C /* every one's section 12: the bonus names */
#define RESULT_INT(w, ofs) (*(int GAME_PTR)((w) + (ofs)))

/* The result screen with a score sheet that is up, and which of kResultAt; NULL when none. */
static uint8_t GAME_PTR result_work(int *which) {
    uint8_t GAME_PTR w[4];
    int i;

    w[0] = gMisResult;
    w[1] = gSurvResult;
    w[2] = gSimResult;
    w[3] = gUbResult;
    for (i = 0; i < 4; i++) {
        if (w[i] != NULL) {
            *which = i;
            return w[i];
        }
    }
    return NULL;
}

/* 1: the string at `str` is the name on a bonus plate of the sheet that is up (result_sheet says it, not
   shown_text; the rest of the screen's text, such as the outcome, is left to shown_text). */
static int result_line(uint32_t str) {
    int which, k;
    uint8_t GAME_PTR w = result_work(&which);
    A11yScore GAME_PTR sc;
    uint32_t GAME_PTR text;
    uint32_t a;

    if (w == NULL || str == 0) {
        return 0;
    }
    sc = (A11yScore GAME_PTR)(w + kResultAt[which].score);
    text = *(uint32_t GAME_PTR GAME_PTR)(w + RESULT_TEXT);
    for (k = 0; k < sc->bonusCount && k < 48; k++) {
        a = (uint32_t)(uintptr_t)text_addr(text, sc->bonus[k].value + 0x20);
        if (a != 0 && (str == a || str == a + 2)) {
            return 1;
        }
    }
    return 0;
}

/* "Z points earned: 15" when earned >= 0, then the player's Z points under the screen's own label (have_label, with
   its %d) when it is not NULL. */
static void say_points(int earned, const char *have_label, int have, int flags) {
    char text[256];
    size_t len;

    text[0] = '\0';
    if (earned >= 0) {
        snprintf(text, sizeof(text), tr("Z Points earned: %d"), earned);
    }
    if (have_label != NULL) {
        len = strlen(text);
        snprintf(text + len, sizeof(text) - len, "%s", len > 0 ? ". " : "");
        len = strlen(text);
        snprintf(text + len, sizeof(text) - len, have_label, have);
    }
    PortA11y_Say(text, flags);
}

/* Page `page` of a sheet: the lines with their points and the fight's time, or a page of bonus plates. */
static void say_sheet_page(A11yScore GAME_PTR sc, uint32_t GAME_PTR text, int page, int pages, int flags) {
    char out[A11Y_TEXT], line[256], name[160];
    size_t len;
    int i, k;

    out[0] = '\0';
    if (page == 0) {
        for (i = 0; i < sc->lineCount && i < 4; i++) {
            switch (i) {
            case 0: snprintf(line, sizeof(line), tr("Health: %d percent, %d points"), sc->line[i].value, sc->line[i].points * 100); break;
            case 1: snprintf(line, sizeof(line), tr("Max combo: %d hits, %d points"), sc->line[i].value, sc->line[i].points * 100); break;
            case 2: snprintf(line, sizeof(line), tr("Max damage: %d, %d points"), sc->line[i].value, sc->line[i].points * 100); break;
            default: snprintf(line, sizeof(line), tr("Defeated: %d, %d points"), sc->line[i].value, sc->line[i].points * 100); break;
            }
            len = strlen(out);
            snprintf(out + len, sizeof(out) - len, "%s%s", len > 0 ? ". " : "", line);
        }
        if (sc->hours > 0) {
            snprintf(line, sizeof(line), tr("Time: %d hours, %d minutes, %d seconds"), sc->hours, sc->minutes, sc->seconds);
        } else {
            snprintf(line, sizeof(line), tr("Time: %d minutes, %d seconds"), sc->minutes, sc->seconds);
        }
    } else {
        snprintf(out, sizeof(out), tr("Bonus page %d of %d"), page, pages);
        for (i = 0; i < 3; i++) {
            k = (page - 1) * 3 + i; /* (UbScore_SetPage) */
            if (k >= sc->bonusCount || k >= 48) {
                break;
            }
            if (text_line(name, sizeof(name), text, sc->bonus[k].value + 0x20) == 0) {
                continue;
            }
            snprintf(line, sizeof(line), tr("%s, %d points"), name, sc->bonus[k].points * 100);
            len = strlen(out);
            snprintf(out + len, sizeof(out) - len, ". %s", line);
        }
        line[0] = '\0';
    }
    len = strlen(out);
    snprintf(out + len, sizeof(out) - len, "%s%s", len > 0 && line[0] != '\0' ? ". " : "", line);
    if (out[0] != '\0') {
        PortA11y_Say(out, flags);
    }
}

/* The sheets after a mission, a survival, a Sim Dragon day and a ranking fight: each page as it shows, the score
   and the rank once all is counted, then the Z points the score made. While the game counts, each is queued behind
   the screen's name and the one before; a page the player turns cuts what was said. */
static void result_sheet(void) {
    static uint32_t seen;
    static int state, page, paid;
    int which, s, p, n, rec;
    uint8_t GAME_PTR w = result_work(&which);
    const A11yResultAt *at;
    A11yScore GAME_PTR sc;
    char text[256];
    size_t len;

    if (w == NULL) {
        seen = 0;
        return;
    }
    at = &kResultAt[which];
    if ((uint32_t)(uintptr_t)w != seen) {
        seen = (uint32_t)(uintptr_t)w;
        state = page = -1;
        paid = 0;
    }
    sc = (A11yScore GAME_PTR)(w + at->score);
    s = RESULT_INT(w, at->state);
    p = RESULT_INT(w, at->page);
    n = RESULT_INT(w, at->pages);
    if ((s == RESULT_COUNT || s == RESULT_PAGES) && p != page && p >= 0 && p <= n && n < 17) {
        say_sheet_page(sc, *(uint32_t GAME_PTR GAME_PTR)(w + RESULT_TEXT), p, n,
                       s == RESULT_PAGES && state == RESULT_PAGES ? A11Y_INTERRUPT : 0);
        page = p;
    }
    if (s == RESULT_PAGES && state != RESULT_PAGES) {
        if (sc->rank >= 1 && sc->rank <= 4) {
            snprintf(text, sizeof(text), tr("Score: %d. Rank %c"), sc->total * 100, kRankLetter[sc->rank]);
        } else {
            snprintf(text, sizeof(text), tr("Score: %d"), sc->total * 100);
        }
        rec = at->record != 0 ? RESULT_INT(w, at->record) : 0;
        if (rec) {
            len = strlen(text);
            snprintf(text + len, sizeof(text) - len, "%s", tr(". New record"));
        }
        PortA11y_Say(text, 0);
    }
    if (s == at->money && RESULT_INT(w, at->step) == RESULT_PAID && !paid && gSaveData != NULL) {
        paid = 1;
        say_points(sc->saved, tr("Z Points: %d"), gSaveData->money, 0);
    }
    state = s;
}

/* The story's result (include/menu/menu_c.h, HistResult): after a won fight the points count up (state 2) and wait
   for the confirm button (3), which pays them out (4); a lost fight stays in state 0 with 0 points and the total. The
   caption, the labels and the numbers are images; so is the fight's title, which is not said. */
typedef struct {
    char unk0[0x80];
    int flags;                    /* 0x080: HISTRESULT_ */
    char unk84[0x90 - 0x84];
    int state;                    /* 0x090: 0 before the count and after the pay-out; 51.. the reward windows */
    int points;                   /* 0x094: counted so far, then left to pay */
    int pointsTotal;              /* 0x098: the fight's Z points */
    char unk9C[0x140 - 0x9C];
} A11yHistResult;
LAYOUT_ASSERT(sizeof(A11yHistResult) == 0x140 && offsetof(A11yHistResult, state) == 0x90, "HistResult");
extern A11yHistResult GAME_PTR gHistResult; /* NULL outside the screen */
#define HISTRESULT_FADED_IN 8 /* the screen is up; its jingle plays before it takes the buttons */
#define HISTRESULT_LOST 0x20

/* The story's fight as its result shows it: the caption once the screen is up; after a win the Z points it made when
   the count stops and what the player has once they are paid, after a loss the 0 points and the total at once. */
static void hist_result(void) {
    static uint32_t seen;
    static int said; /* 1: the caption, 2: the points, 3: the total */
    A11yHistResult GAME_PTR h = gHistResult;

    if (h == NULL) {
        seen = 0;
        return;
    }
    if ((uint32_t)(uintptr_t)h != seen) {
        seen = (uint32_t)(uintptr_t)h;
        said = 0;
    }
    if (!(h->flags & HISTRESULT_FADED_IN) || gSaveData == NULL) {
        return;
    }
    if (said == 0 && (h->flags & HISTRESULT_LOST)) {
        PortA11y_Say(tr("Failed..."), 0);
        say_points(0, tr("Total Z Points: %d"), gSaveData->money, 0);
        said = 3;
    } else if (said == 0) {
        PortA11y_Say(tr("Cleared!"), 0);
        said = 1;
    } else if (said == 1 && h->state == 3) {
        say_points(h->pointsTotal, NULL, 0, 0);
        said = 2;
    } else if (said == 2 && (h->state == 5 || h->state == 0 || h->state > 50)) {
        say_points(-1, tr("Total Z Points: %d"), gSaveData->money, A11Y_INTERRUPT);
        said = 3;
    }
}

/* 1: one of the readers above has this screen (what it draws is not read again from the text). */
static int own_screen(void) {
    A11yBtlMenu GAME_PTR btl = btl_menu_shown();
    int m;

    if (gDialog != NULL && (gDialog->flags & DIALOG_FLAG_OPEN)) {
        return 1;
    }
    if (btl != NULL) {
        return !btl_menu_page(btl) || PortA11y_SkillListShown();
    }
    if (in_battle() || gProgress == NULL) {
        return 0;
    }
    if (gSoloSel != NULL || gEntrySel != NULL || gUbMenu != NULL || gMisSel != NULL || gTourMenu != NULL ||
        gDiscFusion != NULL || gTrain != NULL || gSurvSel != NULL || gSimTop != NULL ||
        gBracket != NULL || gTeamSel != NULL || gReplayMenu != NULL ||
        gCharRef != NULL || gShop != NULL) {
        return 1;
    }
    m = gProgress->mode;
    return m == MODE_TITLE || m == MODE_MAIN_MENU || m == MODE_DUEL_MENU || m == MODE_DUEL_CHARSEL ||
           m == MODE_TRAIN_CHARSEL || m == MODE_OPTION || m == MODE_HIST_SEL || m == MODE_EPISODES || m == MODE_PORT_SCREEN;
}

/* The game's strings drawn lately (PortA11y_FontText), by their hash. */
typedef struct {
    uint64_t hash;
    uint32_t str;      /* the game's address */
    int len;           /* UTF-16 units drawn */
    int oneLine;       /* drawn to the first line break */
    int first, last;   /* blanks it was first and last drawn in */
    int order;         /* when it was first drawn, among the others */
    int done;          /* said, or left to another reader */
} A11yShown;
static A11yShown sShown[TEXT_SLOTS];
static int sShownOrder;

/* A letter of some script: a string without one (a counter, a line of dashes) is not said. */
static int is_letter(uint32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c < 0x2000 && c != 0xD7 && c != 0xF7) ||
           (c >= 0x3040 && c < 0xA000) || (c >= 0xAC00 && c < 0xD7A4);
}

/* FNV-1a over the first `len` units of a string; *len is set to how many are drawn (to the terminator, or to the
   first line break when the style draws one line) and *letters to whether there is a letter. */
static uint64_t string_hash(const uint16_t *s, int oneLine, int *len, int *letters) {
    uint64_t h = 0xCBF29CE484222325ULL;
    int n;

    *letters = 0;
    for (n = 0; n < 1024 && s[n] != 0 && !(oneLine && (s[n] == '\n' || s[n] == '\r')); n++) {
        h = (h ^ s[n]) * 0x100000001B3ULL;
        *letters |= is_letter(s[n]);
    }
    *len = n;
    return h;
}

/* src/battle/col_c.c, Font_NewCmd: the game queued a string to draw (kind 0: UTF-16 at `arg`; flags bit 0: one
   line). Only noted here; said at the blank (shown_text). */
void PortA11y_FontText(int kind, int arg, int flags) {
    A11yShown *e, *old = NULL;
    uint64_t h;
    int i, len, letters;

    if (!sOn || gPortResim || kind != 0 || arg == 0) {
        return;
    }
    h = string_hash((const uint16_t *)(uintptr_t)(uint32_t)arg, flags & 1, &len, &letters);
    if (!letters) {
        return;
    }
    for (i = 0; i < TEXT_SLOTS; i++) {
        e = &sShown[i];
        if (e->hash == h && e->last >= sBlank - TEXT_FORGET) {
            e->str = (uint32_t)arg;
            e->last = sBlank;
            return;
        }
        if (old == NULL || e->last < old->last) {
            old = e;
        }
    }
    old->hash = h;
    old->str = (uint32_t)arg;
    old->len = len;
    old->oneLine = flags & 1;
    old->first = old->last = sBlank;
    old->order = sShownOrder++;
    old->done = 0;
}

/* The strings that showed up on a screen no reader has, in the order they were first drawn, once they have
   stayed TEXT_STABLE blanks: together as one message. Not the message window's line (msg_window) nor a line a
   voice says. In a fight with no menu over it (the HUD's technique names), said as in the first version of this
   reader, which the player asked for again, unless the Accessibility tab's "Technique names" is off (then only
   noted in the log). */
extern int PortA11y_TechniqueNames(void); /* a11y_fight.c */
#define NAME_SHORT 48 /* bytes: a line of the fight shorter than this is a name (a technique's), not a sentence */
#define NAME_GAP 90   /* blanks between two names said in a fight: 1.5 s */

static void shown_text(void) {
    A11yMsgWin GAME_PTR w = gMsgWin;
    uint32_t skip = w != NULL ? (uint32_t)(uintptr_t)w->str : 0;
    uint16_t buf[1025];
    char text[A11Y_TEXT], line[A11Y_TEXT], note[A11Y_TEXT + 32];
    int idx[TEXT_SLOTS], n = 0, own = own_screen(), quiet = !own && btl_menu_shown() == NULL && fighting(), i, j, k,
        len, letters;
    A11yShown *e;
    const uint16_t *s;
    size_t used;

    for (i = 0; i < TEXT_SLOTS; i++) {
        e = &sShown[i];
        if (e->hash == 0 || e->done || e->last < sBlank - 2) { /* (a frame can last two blanks) */
            continue;
        }
        if (own || e->str == skip || getwin_line(e->str) || result_line(e->str)) {
            e->done = 1;
            continue;
        }
        if (sBlank - e->first < TEXT_STABLE) {
            continue;
        }
        e->done = 1;
        if (subtitle(e->first)) {
            PortA11y_Note("text: a voice says it");
            continue;
        }
        for (j = n++; j > 0 && sShown[idx[j - 1]].order > e->order; j--) {
            idx[j] = idx[j - 1];
        }
        idx[j] = i;
    }
    text[0] = '\0';
    used = 0;
    for (k = 0; k < n; k++) {
        e = &sShown[idx[k]];
        s = (const uint16_t *)(uintptr_t)e->str;
        if (string_hash(s, e->oneLine, &len, &letters) != e->hash) {
            continue; /* (the string changed since it was drawn) */
        }
        for (i = j = 0; i < e->len && j < 1024; i++) {
            if (s[i] < 0xE000 || s[i] >= 0xF900) { /* (not the private use area: the font's own marks) */
                buf[j++] = s[i];
            }
        }
        buf[j] = 0;
        if (PortA11y_Utf8(line, sizeof(line), buf) > 0 && used + strlen(line) + 3 < sizeof(text)) {
            used += snprintf(text + used, sizeof(text) - used, "%s%s", used > 0 && strchr(".!?:", text[used - 1]) == NULL ? ". " : used > 0 ? " " : "", line);
        }
    }
    if (text[0] != '\0' && quiet && !PortA11y_TechniqueNames()) {
        snprintf(note, sizeof(note), "in a fight, not said: %s", text);
        PortA11y_Note(note);
    } else if (text[0] != '\0' && quiet) {
        /* In a fight: queued, so that it does not cut what is being said nor drop the important moments queued
           (an interrupting line empties the queue: gs/a11y.c); a short one, a technique's name, at most one every
           1.5 s, the others noted only (they were long gone by the time a queue of them was said). */
        static int lastName = -100000;
        if (used < NAME_SHORT && sBlank - lastName < NAME_GAP) {
            snprintf(note, sizeof(note), "technique name: dropped (1.5 s): %s", text);
        } else {
            lastName = used < NAME_SHORT ? sBlank : lastName;
            snprintf(note, sizeof(note), used < NAME_SHORT ? "technique name: said: %s" : "fight text: said: %s", text);
            PortA11y_Say(text, 0);
        }
        PortA11y_Note(note);
    } else if (text[0] != '\0') {
        /* (over a score sheet, the outcome is queued behind its page: result_sheet) */
        PortA11y_Say(text, result_work(&k) != NULL ? 0 : follow());
    }
}

/* ---- the fight's gauges, on request ---- */

/* src/battle/btl_capi_b.c: the active member's gauges by object id, which is the side (docs/systems/fighter.md);
   0 while there is no such fighter (they only read, and check gBtlChars, NULL outside a battle). */
extern int BtlCharApi_GetHp(int objId), BtlCharApi_GetHpMax(int objId), BtlCharApi_GetKi(int objId),
    BtlCharApi_GetBlast(int objId), BtlCharApi_GetBlastMax(int objId);
extern int BtlSide_IsPoweredUp(int side), BtlSide_CountAlive(int side);
extern int PortA11y_GaugesAsked(int fight); /* gs/a11y.c */
extern int PortA11y_GaugesQuery(int side, int byItself); /* a11y_fight.c: the beeps; 2 set when the gauges are said too */
extern int PortA11y_GaugesByItself(void);   /* a11y_fight.c: told as they change too, not only when asked for */
extern void PortA11y_WhereQuery(void);      /* a11y_fight.c: where the rival is, a ping from there and its words */
#define ASK_WHERE 3                         /* PortA11y_GaugesAsked: where the rival is (1, 2: a player's gauges) */
#define ASK_SKILLS 4                        /* a player's techniques and their costs (the side in bit 4) */
extern void PortA11y_SkillsQuery(int side); /* a11y_fight.c */
extern int BtlSeq_GetTimeLeft(void);        /* src/battle/btl_seq.c: seconds left, -1 no limit (only read) */
extern void PortA11y_PowerCue(int side, int kind, int count); /* a11y_fight.c: the cue as a sound, the voice or both */
extern void PortA11y_FightAlerts(int fight); /* a11y_fight.c: distance, the stage's edge, health */
extern void PortA11y_FightMenu(int open);    /* gs/a11y.c: a fight's pause menu is open */
extern int PortA11y_IntroDescription(void);  /* a11y_fight.c: the Accessibility tab's "Describe the fight" */
extern int Battle_GetStage(void);            /* src/battle/battle_load.c (only reads) */

#define GAUGE_HP_BAR 10000  /* docs/systems/hud.md: health per bar */
#define GAUGE_KI_BAR 20000  /* ki per bar, five bars */
#define GAUGE_KI_BARS 5
#define GAUGE_BLAST 100000  /* blast per stock */

/* `value` in units of `per`, with a decimal when it has one ("3,4"); at least 0,1 while above 0. */
static void gauge_units(char *out, int cap, int value, int per) {
    int tenths = value <= 0 ? 0 : (int)((int64_t)value * 10 / per);

    if (value > 0 && tenths == 0) {
        tenths = 1;
    }
    if (tenths % 10 == 0) {
        snprintf(out, cap, "%d", tenths / 10);
    } else {
        snprintf(out, cap, tr("%d.%d"), tenths / 10, tenths % 10); /* (the decimal mark is the language's) */
    }
}

/* A player's gauges as the HUD shows them: health and ki in bars, the blast stock, the powered-up mode and how many
   of a team are still up. */
static void gauges_say(int side, int flags) {
    char hp[16], hpMax[16], ki[16], text[256];
    size_t n;
    int alive;

    gauge_units(hp, sizeof(hp), BtlCharApi_GetHp(side), GAUGE_HP_BAR);
    gauge_units(hpMax, sizeof(hpMax), BtlCharApi_GetHpMax(side), GAUGE_HP_BAR);
    gauge_units(ki, sizeof(ki), BtlCharApi_GetKi(side), GAUGE_KI_BAR);
    n = (size_t)snprintf(text, sizeof(text),
                         tr("Player %d: health %s of %s bars, ki %s of %d, blast %d of %d"),
                         side + 1, hp, hpMax, ki, GAUGE_KI_BARS, BtlCharApi_GetBlast(side) / GAUGE_BLAST,
                         BtlCharApi_GetBlastMax(side) / GAUGE_BLAST);
    if (BtlSide_IsPoweredUp(side) && n < sizeof(text)) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, tr(", max power"));
    }
    if ((alive = BtlSide_CountAlive(side)) > 1 && n < sizeof(text)) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, tr(", %d fighters still up"), alive);
    }
    if (BtlSeq_GetTimeLeft() >= 0 && n < sizeof(text)) { /* (the HUD's clock, when the fight has a time limit) */
        snprintf(text + n, sizeof(text) - n, tr(", %d seconds left"), BtlSeq_GetTimeLeft());
    }
    PortA11y_Say(text, flags);
}

/* The gauges told by themselves (the Accessibility tab's gauges "by itself too"): a player's, each time a whole bar
   of health or of ki, or a blast stock, comes or goes and then holds half a second; at most every two seconds a
   player, queued after what is being said. Yours all three; the rival's health and blast (its ki moves all the
   time). A new fighter (a team's next, a transformation) is told as it comes. */
static void gauges_auto(int fight) {
    static int pend[2][3], said[2][3], held[2], next[2], blank, was;
    int side, cur[3], any = 0, me = Port_NetActive() ? Port_NetMe() & 1 : 0;

    if (!fight || !PortA11y_GaugesByItself()) {
        was = 0;
        return;
    }
    blank++;
    for (side = 0; side < 2; side++) {
        if (BtlCharApi_GetHpMax(side) <= 0) {
            continue;
        }
        any = 1;
        cur[0] = BtlCharApi_GetHp(side) / GAUGE_HP_BAR;
        cur[1] = side == me ? BtlCharApi_GetKi(side) / GAUGE_KI_BAR : 0;
        cur[2] = BtlCharApi_GetBlast(side) / GAUGE_BLAST;
        if (!was) { /* (a fight starting: what the gauges are is not news) */
            memcpy(pend[side], cur, sizeof(cur));
            memcpy(said[side], cur, sizeof(cur));
            held[side] = 0;
            next[side] = blank + 120;
            continue;
        }
        if (memcmp(cur, pend[side], sizeof(cur)) != 0) {
            memcpy(pend[side], cur, sizeof(cur));
            held[side] = 0;
            continue;
        }
        if (++held[side] < 30 || memcmp(pend[side], said[side], sizeof(cur)) == 0 || blank < next[side]) {
            continue;
        }
        memcpy(said[side], pend[side], sizeof(cur));
        next[side] = blank + 120;
        if (PortA11y_GaugesQuery(side, 1) & 2) {
            gauges_say(side, 0);
        }
    }
    was = was || any;
}

/* The gauges of the player asked for (F3 / F4, Select twice or three times, or the touchpad click in a fight:
   gs/a11y.c), or by themselves as they change. */
static void fight_gauges(void) {
    static int shown;
    int fight = fighting(), ask = PortA11y_GaugesAsked(fight && btl_menu_shown() == NULL), side = ask - 1;

    if (fight != shown) {
        shown = fight;
        PortA11y_Note(fight ? "gauges on screen" : "gauges gone");
    }
    gauges_auto(fight && btl_menu_shown() == NULL);
    if (ask == 0) {
        return;
    }
    if (!fight) {
        PortA11y_Note("gauges asked for outside a fight");
        return;
    }
    if (ask == ASK_WHERE) {
        PortA11y_WhereQuery();
        return;
    }
    if ((ask & 15) == ASK_SKILLS) {
        PortA11y_SkillsQuery(Port_NetActive() ? Port_NetMe() & 1 : ask >> 4);
        return;
    }
    if (!(PortA11y_GaugesQuery(side, 0) & 2)) {
        return; /* (told by the beeps alone: a11y_fight.c) */
    }
    gauges_say(side, A11Y_INTERRUPT);
}

/* ---- the fight's cues: a transformation or the Ultimate Blast within reach ---- */

/* src/battle: the checks the game makes when the buttons are pressed (btl_act_j.c: BtlAct_CheckTransformInput and
   BtlAct_CanUseTechnique), and what they need. All of them only read; the fighter goes through here by address. */
extern void GAME_PTR BtlChar_FindBySide(int side);
extern int BtlChar_GetFrame(void), BtlCharApi_GetChara(int objId), BtlCharApi_GetActiveMember(int objId);
extern int BtlAct_CanTransform(void GAME_PTR chr, unsigned index, int needBlast, int needAllowed);
extern int BtlChar_TestFlag(void GAME_PTR chr, unsigned n), BtlChar_TestMemberUnk70(void GAME_PTR chr);
extern int BtlMember_HasKi(void GAME_PTR chr, int amount), BtlSuper_GetKiCost(void GAME_PTR chr, int slot);
extern void PortA11y_Cue(int side, int kind); /* gs/a11y.c */

#define CUE_TRANSFORM 0
#define CUE_ULTIMATE 1
#define CUE_STEADY 15          /* blanks a change must hold before it counts: a quarter of a second */
#define CHR_POWERED_UP 6       /* fighter flag: the powered-up mode, which the Ultimate Blast needs */
#define CHR_ANY_FORM 0x10F     /* fighter flag: transforms even where the side's rule refuses it */
#define CHR_FOE_TECHNIQUE 0xAF /* fighter flag: the rival is firing a technique (btl_char_flag_opp.c), no transforming */
#define TECH_ULTIMATE 4        /* include/battle/btl_tech_b.h: BTL_SLOT_ULTIMATE */
#define TRANSFORMS 4

/* How many of its transformations side's fighter could take, or whether it could fire its Ultimate Blast (1 or 0),
   by the game's own checks; left out is what comes and goes from one moment to the next: whether the fighter is
   free to act, the lock-on the Ultimate Blast also needs, and (in fight_cues) the rival's technique under way. */
static int cue_ready(void GAME_PTR chr, int kind) {
    unsigned i;
    int n = 0;

    if (kind == CUE_ULTIMATE) {
        return BtlChar_TestFlag(chr, CHR_POWERED_UP) && !BtlChar_TestMemberUnk70(chr) &&
               BtlMember_HasKi(chr, BtlSuper_GetKiCost(chr, TECH_ULTIMATE));
    }
    for (i = 0; i < TRANSFORMS; i++) {
        n += BtlAct_CanTransform(chr, i, 1, !BtlChar_TestFlag(chr, CHR_ANY_FORM)) != 0;
    }
    return n;
}

/* The cues, a sound on the player's side (gs/a11y.c): one sounds when what it tells has grown (one transformation
   more within reach, the Ultimate Blast within reach) and held a quarter of a second; a change back must hold as
   long too. It all starts again when the fighter changes (another member, a transformation, the next fight).
   Nothing while the gauges are hidden or a menu is over the fight. */
static void fight_cues(void) {
    static int said[2][2], run[2][2], who[2], frame;
    void GAME_PTR chr;
    char note[80];
    int side, kind, now, was;

    if (!fighting() || btl_menu_shown() != NULL || BtlChar_FindBySide(0) == NULL) {
        return;
    }
    if ((now = BtlChar_GetFrame()) < frame) {
        memset(who, 0, sizeof(who));
    }
    frame = now;
    for (side = 0; side < 2; side++) {
        if ((chr = BtlChar_FindBySide(side)) == NULL) {
            continue;
        }
        now = (BtlCharApi_GetActiveMember(side) << 16) | (BtlCharApi_GetChara(side) + 1);
        if (now != who[side]) {
            who[side] = now;
            memset(said[side], 0, sizeof(said[side]));
            memset(run[side], 0, sizeof(run[side]));
        }
        for (kind = CUE_TRANSFORM; kind <= CUE_ULTIMATE; kind++) {
            if (kind == CUE_TRANSFORM && BtlChar_TestFlag(chr, CHR_FOE_TECHNIQUE)) {
                run[side][kind] = 0; /* the count holds until the rival's technique is over */
                continue;
            }
            if ((now = cue_ready(chr, kind)) == (was = said[side][kind])) {
                run[side][kind] = 0;
            } else if (++run[side][kind] >= CUE_STEADY) {
                said[side][kind] = now;
                run[side][kind] = 0;
                if (now > was) {
                    snprintf(note, sizeof(note), kind == CUE_TRANSFORM ? "cue: player %d can take %d transformations"
                                                                       : "cue: player %d can fire the Ultimate Blast",
                             side + 1, now);
                    PortA11y_Note(note);
                    PortA11y_PowerCue(side, kind, now);
                }
            }
        }
    }
}

/* A short description of the fight while its opening plays, before the fighting starts: who against whom (the
   names the selects gave: sNameCache; a team's size when it has more than one) and where. Once per battle, queued
   after what is being said. The player asked for it: the opening has no words of its own for who is on screen. */
static void fight_intro(void) {
    static int said;
    char text[A11Y_TEXT], who[2][160];
    int side, team;

    if (!in_battle() || gBtlChars == NULL) {
        said = 0;
        return;
    }
    if (said || fighting() || BtlChar_FindBySide(0) == NULL || BtlChar_FindBySide(1) == NULL || !PortA11y_IntroDescription()) {
        return;
    }
    said = 1;
    for (side = 0; side < 2; side++) {
        int chara = BtlCharApi_GetChara(side);
        if (chara < 0 || chara >= MCHR_RANDOM || sNameCache[chara][0] == '\0') { /* (no select has named it: only where) */
            snprintf(text, sizeof(text), tr("Stage: %s."), stage_name(Battle_GetStage()));
            PortA11y_Say(text, 0);
            return;
        }
        cached_name(who[side], sizeof(who[side]), chara);
        if ((team = BtlSide_CountAlive(side)) > 1) {
            size_t len = strlen(who[side]);
            snprintf(who[side] + len, sizeof(who[side]) - len, tr(" and %d more"), team - 1);
        }
    }
    snprintf(text, sizeof(text), tr("%s against %s. Stage: %s."), who[0], who[1], stage_name(Battle_GetStage()));
    PortA11y_Say(text, 0);
    PortA11y_Note("fight: the opening described");
}

/* Who won, said as the fight ends (when the game shows its K.O. or its time up, before the win talk and the menu
   after the fight, which said only "Fight over"): "K.O.: you win, with Goku", "Time up: the rival wins, Vegeta",
   "Time up: a draw". You are the side whose alerts these are (online the side this copy plays, else player 1); the
   fighter's name when a select named it (sNameCache). The result must hold 10 blanks (online a guessed finish can
   be taken back), and a fight left from the pause or a replay's end says nothing. */
#define WIN_HOLD 10
static void fight_result(void) {
    static int armed, said, held, last;
    int w = gBattleWork.winner, why = gBattleWork.reason, me, side, chara;
    char text[256], body[192], name[160], note[128];
    const char *how;

    if (!in_battle()) {
        armed = said = held = last = 0;
        return;
    }
    if (fighting() && w == 0) {
        armed = 1; /* (this fight's result: the block may still hold the last one's until the game clears it) */
    }
    if (!armed || said || w == 0 || (w & 0x18) || !(w & 7)) {
        held = 0;
        last = w;
        return;
    }
    held = w == last ? held + 1 : 0;
    last = w;
    if (held < WIN_HOLD) {
        return;
    }
    said = 1;
    me = Port_NetActive() ? Port_NetMe() & 1 : 0;
    how = why & 1 ? tr("K.O.") : why & 2 ? tr("Time up") : why & 4 ? tr("Out of the ring") : NULL;
    side = w & 4 ? -1 : w & 1 ? 0 : 1;
    if (side < 0) {
        snprintf(body, sizeof(body), "%s", tr("a draw"));
    } else {
        chara = BtlCharApi_GetChara(side);
        name[0] = '\0';
        if (chara >= 0 && chara < MCHR_RANDOM && sNameCache[chara][0] != '\0') {
            cached_name(name, sizeof(name), chara);
        }
        if (name[0] == '\0') {
            snprintf(body, sizeof(body), "%s", side == me ? tr("you win") : tr("the rival wins"));
        } else if (side == me) {
            snprintf(body, sizeof(body), tr("you win, with %s"), name);
        } else {
            snprintf(body, sizeof(body), tr("the rival wins, %s"), name);
        }
    }
    if (how != NULL) {
        snprintf(text, sizeof(text), "%s: %s", how, body);
    } else { /* (no reason the reader knows: the words alone) */
        snprintf(text, sizeof(text), tr("Result: %s"), body);
    }
    snprintf(note, sizeof(note), "winner: %s (side %d), result %#x, reason %#x", side < 0 ? "draw" : side == me ? "you" : "the rival",
             side, w, why);
    PortA11y_Note(note);
    heading(text);
}

/* The first time (setting a11y_tour, bit 0): a welcome, in the quiet before the title (after the memory card's
   messages, or with "Press START" at the latest): what Select does, with the names of the controller in the hands
   (or the keys), how the settings open, and that the blind player profile is on when it was applied now
   (a11y_fight.c). A profile applied in a later run is said by itself. */
extern int PortA11y_ProfileTick(void);   /* a11y_fight.c: -1 not decided yet, 1 applied now */
extern int PortA11y_PadFamily(int player); /* gs/a11y_pad.c: 0 the keyboard */
extern void Port_SettingsWrite(void);
#define ACTION_START 14 /* gs_input.c, kActions */
#define ACTION_SELECT 15
#define TOUR_WELCOME 1  /* a11y_tour: the welcome was said */
static void welcome(void) {
    static int done, seen, quiet;
    const char *sel, *start;
    char text[A11Y_TEXT];
    int profile, tour;

    if (done || (profile = PortA11y_ProfileTick()) < 0) {
        return;
    }
    tour = Port_Setting("a11y_tour", 0);
    if (tour & TOUR_WELCOME) {
        done = 1;
        if (profile == 1) {
            PortA11y_Say(tr("Blind player profile on. It is changed in F1, Accessibility."), 0);
        }
        return;
    }
    if (gDialog != NULL && (gDialog->flags & DIALOG_FLAG_OPEN)) {
        seen = 1; /* (the memory card's messages first: a heading of theirs would cut the welcome) */
        quiet = 0;
        return;
    }
    quiet++;
    if (!(seen && quiet >= 120) && !(gProgress != NULL && gProgress->mode == MODE_TITLE && gTitle != NULL && gTitle->state >= 1) &&
        sBlank < 1800) {
        return;
    }
    done = 1;
    sel = PortA11y_ActionName(-1, ACTION_SELECT);
    start = PortA11y_ActionName(-1, ACTION_START);
    if (PortA11y_PadFamily(-1) != 0 && sel != NULL && start != NULL) {
        snprintf(text, sizeof(text), tr("Welcome. This game speaks through your screen reader. %s says the last message again; in a "
                                        "fight, once says where the rival is, twice your gauges, three times the rival's. F1, or %s "
                                        "and %s together, opens the settings: their Accessibility tab has the fight's sounds and "
                                        "their glossary."), sel, sel, start);
    } else {
        snprintf(text, sizeof(text), "%s", tr("Welcome. This game speaks through your screen reader. F2 says the last message again; "
                                              "in a fight, F5 says where the rival is, F3 and F4 the gauges. F1 opens the settings: "
                                              "their Accessibility tab has the fight's sounds and their glossary."));
    }
    PortA11y_Say(text, 0);
    if (profile == 1) {
        PortA11y_Say(tr("Blind player profile on. It is changed in F1, Accessibility."), 0);
    }
    Port_SettingSave("a11y_tour", tour | TOUR_WELCOME);
    Port_SettingsWrite();
    PortA11y_Note(profile == 1 ? "welcome: said, with the profile" : "welcome: said");
}

/* Once per blank (plat_stub.c: Port_VBlank). */
/* The day of Dragon Sim (mode 22, SimDay: src/menu/menu_q_b.c, include/menu/menu_q.h). The command under the cursor
   and the results are said through the message window; the board's numbers are pictures. They are said when the
   board comes back with a number changed, after it has stopped rolling. */
typedef struct {
    char unk0[0x590];
    int state;        /* 0x590: SIMDAY_ST_ */
} A11ySimDay;
LAYOUT_ASSERT(offsetof(A11ySimDay, state) == 0x590, "SimDay");
extern A11ySimDay GAME_PTR gSimDay;
#define SIMDAY_ST_BOARD 0
#define SIM_STATE_WORD (0x64C / 4) /* SimState in gProgress: level, stat[4] (attack, defense, health, points) ... */
#define SIM_TURN_WORD 10           /* SimState.turn: / 10 the round, % 10 the day */
#define SIM_TURNS 10
#define SIM_VALUES 6
#define SIM_SETTLE 20

static void sim_day(void) {
    static int said[SIM_VALUES], prev[SIM_VALUES], settle, first = 1;
    static const char *const kSimValue[SIM_VALUES] = {
        NULL, "Health %d percent", "Attack %d", "Defense %d", "%d points", "Level %d",
    };
    A11ySimDay GAME_PTR d = gSimDay;
    int GAME_PTR s;
    char text[A11Y_TEXT];
    int now[SIM_VALUES], i, changed = 0, moved = 0;
    size_t len = 0;

    if (d == NULL || gProgress == NULL) {
        first = 1;
        settle = 0;
        return;
    }
    if (d->state != SIMDAY_ST_BOARD) {
        settle = 0;
        return;
    }
    s = (int GAME_PTR)gProgress + SIM_STATE_WORD;
    now[0] = s[SIM_TURN_WORD];
    now[1] = s[3];
    now[2] = s[1];
    now[3] = s[2];
    now[4] = s[4] * 100;
    now[5] = s[0] + 1;
    for (i = 0; i < SIM_VALUES; i++) {
        if (now[i] != prev[i]) {
            moved = 1;
        }
        if (first || now[i] != said[i]) {
            changed |= 1 << i;
        }
        prev[i] = now[i];
    }
    if (moved) {
        settle = 0;
    }
    if (changed == 0 || now[0] < 0 || now[0] > 9999 || ++settle < SIM_SETTLE) {
        return;
    }
    text[0] = '\0';
    if (changed & 1) {
        len = (size_t)snprintf(text, sizeof(text), tr("Day %d of %d"), now[0] % SIM_TURNS + 1, SIM_TURNS);
        if (now[0] >= SIM_TURNS && len < sizeof(text)) {
            len += (size_t)snprintf(text + len, sizeof(text) - len, tr(", round %d"), now[0] / SIM_TURNS + 1);
        }
    }
    for (i = 1; i < SIM_VALUES; i++) {
        if ((changed & (1 << i)) && len < sizeof(text) - 4) {
            if (len > 0) {
                len += (size_t)snprintf(text + len, sizeof(text) - len, ". ");
            }
            len += (size_t)snprintf(text + len, sizeof(text) - len, tr(kSimValue[i]), now[i]);
        }
    }
    for (i = 0; i < SIM_VALUES; i++) {
        said[i] = now[i];
    }
    first = 0;
    PortA11y_Say(text, 0); /* behind the command's line */
}

/* The team of Ultimate Battle (UbTeamSel: src/menu/menu_m_b.c, include/menu/menu_m.h), shown instead of SoloSel when
   the course asks for two fighters or more. Its state is the team select's side record. */
typedef struct {
    char unk0[0x14];
    uint32_t GAME_PTR nameText;   /* 0x0014 */
    uint32_t GAME_PTR formText;   /* 0x0018 */
    char unk1C[0x1EC - 0x1C];
    int flags;                    /* 0x01EC: UBSEL_ */
    char unk1F0[0x200 - 0x1F0];
    A11yTeamSide state;           /* 0x0200: UbTeamState */
    char unk340[0x1AB8 - 0x340];
    int rows;                     /* 0x1AB8 */
    int plateCount;               /* 0x1ABC */
    int colorCount;               /* 0x1AC0 */
    int teamMax;                  /* 0x1AC4: fighters the team may have */
    int dpLevel;                  /* 0x1AC8 */
    int dpMax;                    /* 0x1ACC: 10, 15 or 20 */
} A11yUbTeamSel;
LAYOUT_ASSERT(offsetof(A11yUbTeamSel, flags) == 0x1EC && offsetof(A11yUbTeamSel, state) == 0x200 &&
               offsetof(A11yUbTeamSel, rows) == 0x1AB8 && offsetof(A11yUbTeamSel, dpMax) == 0x1ACC, "UbTeamSel");
extern A11yUbTeamSel GAME_PTR gUbTeamSel;
extern int ChrTbl_GetCost(int chara); /* src/battle/view_b_b.c: reads the character table */
#define UBSEL_STARTED 2
#define UBSEL_DONE 8
#define UBSEL_LEAVING 0x10
#define UBSEL_DP 0x20

/* Appends the character's cost when the team has a DP limit. */
static void ub_add_cost(char *text, int cap, A11yUbTeamSel GAME_PTR t, int chara) {
    size_t len = strlen(text);

    if ((t->flags & UBSEL_DP) && chara >= 0 && chara < MCHR_RANDOM && (int)len < cap) {
        snprintf(text + len, cap - len, tr(", %d DP"), ChrTbl_GetCost(chara));
    }
}

static void ub_team_select(void) {
    static int open, step, pos, row, chara, cur;
    A11yUbTeamSel GAME_PTR t = gUbTeamSel;
    A11yTeamSide GAME_PTR s;
    A11yTsMember GAME_PTR mb;
    char text[A11Y_TEXT], what[A11Y_TEXT];
    int p = 0, count = 0, c;
    size_t len;

    if (t == NULL) {
        open = 0;
        return;
    }
    if (!(t->flags & UBSEL_STARTED) || (t->flags & (UBSEL_DONE | UBSEL_LEAVING))) {
        return;
    }
    s = &t->state;
    if (s->state < TEAMSEL_ST_GRID || s->state > TEAMSEL_ST_TEAM || s->cur < 0 || s->cur > TS_CUR_MENU) {
        return;
    }
    if (!open) {
        open = 1;
        step = pos = row = chara = cur = -1;
        len = (size_t)snprintf(text, sizeof(text), "%s", tr("Team select"));
        if (t->teamMax >= 2 && t->teamMax <= TS_MEMBERS && len < sizeof(text)) {
            len += (size_t)snprintf(text + len, sizeof(text) - len, tr(". Fighters: %d"), t->teamMax);
        }
        if ((t->flags & UBSEL_DP) && len < sizeof(text)) {
            snprintf(text + len, sizeof(text) - len, tr(". DP limit: %d"), t->dpMax);
        }
        heading(text);
    }
    mb = &s->member[s->cur < TS_MEMBERS ? s->cur : 0];
    c = s->chara;
    what[0] = '\0';
    switch (s->state) {
    case TEAMSEL_ST_GRID:
        p = mb->col;
        chara_name(what, sizeof(what), t->nameText, t->formText, c);
        ub_add_cost(what, sizeof(what), t, c);
        if ((step != s->state || mb->row != row) && t->rows > 1) {
            len = strlen(what);
            snprintf(what + len, sizeof(what) - len, tr(", row %d of %d"), mb->row + 1, t->rows);
        }
        break;
    case TEAMSEL_ST_FORM:
        p = mb->form;
        chara_name(what, sizeof(what), t->nameText, t->formText, c);
        ub_add_cost(what, sizeof(what), t, c);
        break;
    case TEAMSEL_ST_PLATE:
        p = mb->plate;
        count = t->plateCount;
        if (p == 0) {
            snprintf(what, sizeof(what), "%s", tr("No items"));
        } else {
            snprintf(what, sizeof(what), tr("Item set %d"), p);
        }
        break;
    case TEAMSEL_ST_PANEL:
        snprintf(what, sizeof(what), "%s", tr("Item panel"));
        break;
    case TEAMSEL_ST_PANEL_HELP: /* the item's description window */
        step = s->state;
        return;
    case TEAMSEL_ST_COLOR:
        p = mb->color;
        count = t->colorCount;
        snprintf(what, sizeof(what), tr("Costume %d"), p + 1);
        break;
    case TEAMSEL_ST_TEAM:
        c = -1;
        if (s->cur == TS_CUR_MENU) {
            snprintf(what, sizeof(what), "%s", tr("Confirm the team"));
            p = -1;
        } else {
            p = s->cur;
            count = t->teamMax >= 1 && t->teamMax <= TS_MEMBERS ? t->teamMax : TS_MEMBERS;
            if (s->cur < s->memberCount && mb->chara >= 0) {
                c = mb->chara;
                len = (size_t)snprintf(what, sizeof(what), tr("Member %d: "), s->cur + 1);
                chara_name(what + len, sizeof(what) - len, t->nameText, t->formText, c);
                ub_add_cost(what, sizeof(what), t, c);
            } else {
                snprintf(what, sizeof(what), tr("Member %d: empty"), s->cur + 1);
            }
        }
        break;
    }
    if (s->state == step && p == pos && mb->row == row && c == chara && s->cur == cur) {
        return;
    }
    add_position(what, sizeof(what), p, count);
    if (s->state == TEAMSEL_ST_TEAM && step != TEAMSEL_ST_TEAM && (t->flags & UBSEL_DP)) {
        len = strlen(what);
        snprintf(what + len, sizeof(what) - len, tr(". Team DP: %d of %d"), s->cost, t->dpMax);
    }
    if (s->state != TEAMSEL_ST_TEAM && s->cur != cur && s->cur < TS_MEMBERS) {
        snprintf(text, sizeof(text), tr("Member %d: %s"), s->cur + 1, what);
        snprintf(what, sizeof(what), "%s", text);
    }
    step = s->state;
    pos = p;
    row = mb->row;
    chara = c;
    cur = s->cur;
    PortA11y_Say(what, follow());
}

/* The password entry of the Data Center (mode 54, DcPass: src/menu/menu_z_d.c, include/menu/menu_z.h). Its work
   structure is a local of DcPass_Run, so its loop gives a copy here every frame (PortA11y_DcPass), as the plates
   above do. The guide's lines are said through the message window. */
typedef struct {
    char unk0[0x85C];
    uint32_t GAME_PTR nameText;   /* 0x85C: view.nameText */
    uint32_t GAME_PTR formText;   /* 0x860 */
    char unk864[4];
    char text[35];                /* 0x868: 34 cells, ' ' = empty */
    char pass[0x25];              /* 0x88B */
    int cursor;                   /* 0x8B0: cell the next character goes to */
    int len;                      /* 0x8B4 */
    char unk8B8[0x8D0 - 0x8B8];
    unsigned int state;           /* 0x8D0: DCPASS_ST_ */
    int page, row, col;           /* 0x8D4: the keyboard's cursor */
    int top, listCursor, extra;   /* 0x8E0: the slot list */
    int chara[14];                /* 0x8EC: -1 = free */
    char unk924[0x964 - 0x924];
    int flags;                    /* 0x964: DCPASS_ */
    char unk968[0x97C - 0x968];
} A11yDcPass;
LAYOUT_ASSERT(offsetof(A11yDcPass, text) == 0x868 && offsetof(A11yDcPass, cursor) == 0x8B0 &&
               offsetof(A11yDcPass, state) == 0x8D0 && offsetof(A11yDcPass, chara) == 0x8EC &&
               offsetof(A11yDcPass, flags) == 0x964 && sizeof(A11yDcPass) == 0x97C, "DcPass");
extern char gDcPassKeys[2][5][14]; /* src/menu/menu_z_d.c: characters, or the codes below */
#define DCPASS_CELLS 34
#define DCPASS_SLOTS 14
#define DCPASS_ST_TYPE 0
#define DCPASS_ST_LIST 3
#define DCPASS_LEAVE 1
#define DCPASS_LEAVING 2
#define DCPASS_STARTED 4
#define DCKEY_CODES 7
#define DCKEY_ZERO 4

static A11yDcPass sDcPass;
static int sDcPassBlank; /* the blank the copy was taken on (0: never) */

void PortA11y_DcPass(unsigned int pass) {
    if (!sOn || gPortResim || pass == 0) {
        return;
    }
    memcpy(&sDcPass, (const void *)(uintptr_t)pass, sizeof(sDcPass));
    sDcPassBlank = sBlank;
}

/* Descriptive: the three buttons and the wide keys are pictures. */
static const char *const kDcKeys[DCKEY_CODES] = {
    "Quit", "Capitals or small letters", "Confirm password", "Delete character", NULL, "Cursor left", "Cursor right",
};
static const char kDcSymbols[] = "#$%&@!?-+*()";
static const char *const kDcSymbolNames[12] = {
    "Hash", "Dollar", "Percent", "Ampersand", "At", "Exclamation mark",
    "Question mark", "Hyphen", "Plus", "Asterisk", "Left parenthesis", "Right parenthesis",
};

/* How a password character (or a key code) is said: a screen reader may skip a bare symbol and does not tell a
   capital from a small letter. */
static void pass_char_name(char *out, int cap, int c) {
    const char *sym;

    if (c == DCKEY_ZERO) {
        c = '0';
    }
    if (c >= 0 && c < DCKEY_CODES) {
        snprintf(out, cap, "%s", tr(kDcKeys[c]));
    } else if (c >= 'A' && c <= 'Z') {
        snprintf(out, cap, tr("Capital %c"), c);
    } else if (c == ' ') {
        snprintf(out, cap, "%s", tr("empty"));
    } else if (c != '\0' && (sym = strchr(kDcSymbols, c)) != NULL) {
        snprintf(out, cap, "%s", tr(kDcSymbolNames[sym - kDcSymbols]));
    } else {
        snprintf(out, cap, "%c", c);
    }
}

static void dc_pass(void) {
    static int open, state, key, cells, cursor, slot;
    static char shown[DCPASS_CELLS];
    A11yDcPass *p = &sDcPass;
    char text[A11Y_TEXT], name[256];
    int k, n, i, idx;
    size_t len;

    if (sDcPassBlank == 0 || sBlank - sDcPassBlank > 2) { /* (not run lately: the screen is gone) */
        open = 0;
        return;
    }
    if (!(p->flags & DCPASS_STARTED) || (p->flags & (DCPASS_LEAVE | DCPASS_LEAVING))) {
        return;
    }
    if (!open) {
        open = 1;
        state = key = cells = cursor = slot = -1;
        memset(shown, ' ', sizeof(shown));
        heading(tr("Password entry"));
    }
    switch (p->state) {
    case DCPASS_ST_TYPE:
        if (p->page < 0 || p->page > 1 || p->row < 0 || p->row > 4 || p->col < 0 || p->col > 13) {
            break;
        }
        k = (unsigned char)gDcPassKeys[p->page][p->row][p->col];
        for (n = 0, i = 0; i < DCPASS_CELLS; i++) {
            n += p->text[i] != ' ' && p->text[i] != '\0';
        }
        if (cells >= 0 && memcmp(shown, p->text, sizeof(shown)) != 0) {
            snprintf(text, sizeof(text), n < cells ? tr("Deleted. Characters: %d") : tr("Characters: %d"), n);
            PortA11y_Say(text, A11Y_INTERRUPT);
        } else if (cursor >= 0 && p->cursor != cursor && p->cursor >= 0 && p->cursor < DCPASS_CELLS) {
            pass_char_name(name, sizeof(name), p->text[p->cursor]);
            snprintf(text, sizeof(text), tr("Position %d: %s"), p->cursor + 1, name);
            PortA11y_Say(text, A11Y_INTERRUPT);
        } else if (state != DCPASS_ST_TYPE || k != key) {
            pass_char_name(name, sizeof(name), k);
            PortA11y_Say(name, follow());
        }
        key = k;
        cells = n;
        cursor = p->cursor;
        memcpy(shown, p->text, sizeof(shown));
        break;
    case DCPASS_ST_LIST:
        idx = p->top + p->listCursor;
        if (idx < 0 || idx >= DCPASS_SLOTS || (state == DCPASS_ST_LIST && idx == slot)) {
            break;
        }
        slot = idx;
        if (p->chara[idx] < 0) {
            snprintf(text, sizeof(text), tr("Slot %d: free"), idx + 1);
        } else {
            len = (size_t)snprintf(text, sizeof(text), tr("Slot %d: "), idx + 1);
            chara_name(text + len, (int)(sizeof(text) - len), p->nameText, p->formText, p->chara[idx]);
        }
        add_position(text, sizeof(text), idx, DCPASS_SLOTS);
        PortA11y_Say(text, follow());
        break;
    default:
        break;
    }
    state = (int)p->state;
}

/* The customising screen of Evolution Z (mode 49, EvoZ: src/menu/menu_v_b.c, include/menu/menu_v.h) after a
   character is chosen: the menu, the three item sets, the eight slots with their four actions, the list of owned
   items, the password window and the page that turns Z Points into experience. The grid before it is char_ref's.
   The menu rows and the actions are pictures: their names here are descriptions of what EvoZ_Input does. */
typedef struct {
    unsigned short id[3][8];      /* 0x00: the three item sets, 1-based items, 0 = empty */
    int exp;                      /* 0x30 */
    unsigned short level, unk36;  /* 0x34 */
} A11yEvoCustom;                  /* include/menu/menu_v.h: VSaveCustom */
typedef struct {
    char unk0[0x20];
    uint32_t GAME_PTR text[3];    /* 0x0020: [2] the item names, by 0-based item */
    char unk2C[0x22C - 0x2C];
    int flags;                    /* 0x022C: EVOZ_ */
    int cur[11];                  /* 0x0230: [1] menu row, [2] item set, [3] slot row, [5] digit, [8] slot action */
    int leaveTimer;
    int state;                    /* 0x0260: EVOZ_ST_ */
    int faceState;
    int listSide;
    int helpItem;                 /* 0x026C: 0-based item of the details page */
    int zp;                       /* 0x0270: Z Points chosen on the points page */
    int set;                      /* 0x0274: item set being edited */
    int lastTab;
    int curItem;
    int rowsUsed;                 /* 0x0280 */
    int dialog;                   /* 0x0284: 0 = closed */
    char unk288[0x2A0 - 0x288];
    int col, row;                 /* 0x02A0: the grid cursor: the save's cell is col + row * 7 */
    char unk2A8[0x30C - 0x2A8];
    int chara;                    /* 0x030C */
    char unk310[0x1A60 - 0x310];
    int capacity;                 /* 0x1A60: item slots of the character */
    int fits;                     /* 0x1A64: item slots the set takes */
    int kind;
    int exp;
    int nextExp;                  /* 0x1A70: 0 at the last level */
    int expLeft;                  /* 0x1A74 */
    char unk1A78[0x1A98 - 0x1A78];
    A11yShopList list;            /* 0x1A98: the owned items that can be equipped */
    A11yShopItem GAME_PTR items;  /* 0x30C8 */
} A11yEvoZ;
LAYOUT_ASSERT(sizeof(A11yEvoCustom) == 0x38 && offsetof(A11yEvoZ, flags) == 0x22C &&
               offsetof(A11yEvoZ, state) == 0x260 && offsetof(A11yEvoZ, rowsUsed) == 0x280 &&
               offsetof(A11yEvoZ, chara) == 0x30C && offsetof(A11yEvoZ, capacity) == 0x1A60 &&
               offsetof(A11yEvoZ, list) == 0x1A98 && offsetof(A11yEvoZ, items) == 0x30C8, "EvoZ");
extern A11yEvoZ GAME_PTR gEvoZ;
extern char GAME_PTR ChrPass_GetText(void); /* src/sys/misc_a_b.c: the password the window shows */
#define EVOZ_STARTED 4
#define EVOZ_ST_GRID 0
#define EVOZ_ST_MENU 1
#define EVOZ_ST_SLOTS 3
#define EVOZ_ST_PASSWORD 4
#define EVOZ_ST_ZP 5
#define EVOZ_ST_LEVEL_UP 7
#define EVOZ_ST_HELP 9
#define EVOZ_ST_LIST 10
#define EVOZ_SLOTS 8
#define EVOZ_CELLS 97
#define SAVE_CUSTOM_OFFSET 0x1808

static const char *const kEvoMenu[3] = {"Equip Z items", "Show password", "Use Z Points"};
static const char *const kEvoActions[4] = {"Change", "Details", "Remove", "Remove all"};

/* The name of 1-based item `id`, "Empty" for 0. */
static void evo_item_name(char *out, int cap, A11yEvoZ GAME_PTR ez, int id) {
    if (id <= 0) {
        snprintf(out, cap, "%s", tr("Empty"));
    } else if (id > SAVE_ITEMS || text_line(out, cap, ez->text[2], id - 1) == 0) {
        snprintf(out, cap, tr("Item %d"), id);
    }
}

static void evo_custom(void) {
    static int state = -1, row, set, act, tab, idx, zp, digit, item;
    A11yEvoZ GAME_PTR ez = gEvoZ;
    A11yEvoCustom GAME_PTR cu;
    A11yShopList GAME_PTR l;
    char GAME_PTR pass;
    char text[A11Y_TEXT], name[256];
    int cell, id, i, n, step, entering;
    size_t len;

    if (ez == NULL || gSaveData == NULL) {
        state = -1;
        return;
    }
    cell = ez->col + ez->row * MCHR_COLS;
    if (!(ez->flags & EVOZ_STARTED) || ez->dialog != 0 || cell < 0 || cell >= EVOZ_CELLS || ez->set < 0 || ez->set > 2) {
        return;
    }
    cu = (A11yEvoCustom GAME_PTR)((char GAME_PTR)gSaveData + SAVE_CUSTOM_OFFSET) + cell;
    entering = state != ez->state;
    text[0] = '\0';
    switch (ez->state) {
    case EVOZ_ST_MENU:
        if (ez->cur[1] < 0 || ez->cur[1] > 2 || ez->cur[2] < 0 || ez->cur[2] > 2) {
            return;
        }
        if (!entering && row == ez->cur[1] && set == ez->cur[2]) {
            break;
        }
        if (!entering && set != ez->cur[2]) { /* left and right change the item set */
            snprintf(text, sizeof(text), tr("Item set %d"), ez->cur[2] + 1);
            add_position(text, sizeof(text), ez->cur[2], 3);
        } else {
            snprintf(text, sizeof(text), "%s", tr(kEvoMenu[ez->cur[1]]));
            if (ez->cur[1] == 2 && ez->nextExp == 0) {
                len = strlen(text);
                snprintf(text + len, sizeof(text) - len, "%s", tr(", not available"));
            }
            add_position(text, sizeof(text), ez->cur[1], 3);
            if (state == EVOZ_ST_GRID || state == -1) { /* a character was just chosen */
                len = strlen(text);
                snprintf(text + len, sizeof(text) - len, tr(". Item set %d of 3. Item slots: %d"), ez->cur[2] + 1,
                         ez->capacity);
            }
        }
        row = ez->cur[1];
        set = ez->cur[2];
        PortA11y_Say(text, A11Y_INTERRUPT);
        break;
    case EVOZ_ST_SLOTS:
        if (ez->cur[3] < 0 || ez->cur[3] >= EVOZ_SLOTS || ez->cur[8] < 0 || ez->cur[8] > 3) {
            return;
        }
        id = cu->id[ez->set][ez->cur[3]];
        if (entering || row != ez->cur[3] || item != id) {
            evo_item_name(name, sizeof(name), ez, id);
            len = 0;
            if (entering && state != EVOZ_ST_HELP && state != EVOZ_ST_LIST) {
                len = (size_t)snprintf(text, sizeof(text), tr("Item set %d. Slots used: %d of %d. "), ez->set + 1,
                                       ez->fits, ez->capacity);
            }
            snprintf(text + len, sizeof(text) - len, tr("Slot %d: %s"), ez->cur[3] + 1, name);
            if (entering) {
                len = strlen(text);
                snprintf(text + len, sizeof(text) - len, ". %s", tr(kEvoActions[ez->cur[8]]));
            }
            PortA11y_Say(text, A11Y_INTERRUPT);
        } else if (act != ez->cur[8]) {
            say_item(tr(kEvoActions[ez->cur[8]]), ez->cur[8], 4, A11Y_INTERRUPT);
        }
        row = ez->cur[3];
        item = id;
        act = ez->cur[8];
        break;
    case EVOZ_ST_LIST:
        l = &ez->list;
        if (l->tab < 0 || l->tab >= SHOP_TABS) {
            return;
        }
        n = l->count[l->tab];
        i = l->cur[l->tab];
        if (!entering && tab == l->tab && idx == i) {
            break;
        }
        len = 0;
        if (entering || tab != l->tab) {
            len = (size_t)snprintf(text, sizeof(text), tr("Tab %d of %d%s. "), l->tab + 1, SHOP_TABS, "");
        }
        if (n <= 0 || n > SAVE_ITEMS || i < 0 || i >= n) {
            snprintf(text + len, sizeof(text) - len, "%s", tr("No items"));
        } else {
            id = l->ids[l->tab][i];
            evo_item_name(text + len, (int)(sizeof(text) - len), ez, id + 1);
            if (ez->items != NULL && id >= 0 && id < SAVE_ITEMS && ez->items[id].slots > 0) {
                len = strlen(text);
                snprintf(text + len, sizeof(text) - len, tr(", slots: %d"), ez->items[id].slots);
            }
            add_position(text, sizeof(text), i, n);
        }
        tab = l->tab;
        idx = i;
        PortA11y_Say(text, A11Y_INTERRUPT);
        break;
    case EVOZ_ST_HELP:
        if (entering) {
            item_help(ez->helpItem);
        }
        break;
    case EVOZ_ST_PASSWORD:
        if (entering) {
            /* Character by character: it has to be copied exactly. */
            pass = ChrPass_GetText();
            len = (size_t)snprintf(text, sizeof(text), "%s", tr("Password: "));
            for (i = 0; pass != NULL && i < DCPASS_CELLS && pass[i] != '\0' && len < sizeof(text) - 40; i++) {
                pass_char_name(name, sizeof(name), (unsigned char)pass[i]);
                len += (size_t)snprintf(text + len, sizeof(text) - len, "%s%s", i > 0 ? ", " : "", name);
            }
            say_long(text, A11Y_INTERRUPT);
        }
        break;
    case EVOZ_ST_ZP:
        if (ez->cur[5] < 0 || ez->cur[5] > 4) {
            return;
        }
        if (entering) {
            snprintf(text, sizeof(text), tr("Z Points to use: %d. You have %d. To the next level: %d"), ez->zp,
                     gSaveData->money, ez->expLeft);
        } else if (zp != ez->zp) {
            snprintf(text, sizeof(text), "%d", ez->zp);
        } else if (digit != ez->cur[5]) {
            for (step = 1, i = 0; i < ez->cur[5]; i++) {
                step *= 10;
            }
            snprintf(text, sizeof(text), tr("Step: %d"), step);
        }
        zp = ez->zp;
        digit = ez->cur[5];
        if (text[0] != '\0') {
            PortA11y_Say(text, A11Y_INTERRUPT);
        }
        break;
    case EVOZ_ST_LEVEL_UP:
        if (entering) {
            PortA11y_Say(tr("Level up!"), A11Y_INTERRUPT);
        }
        break;
    default:
        break;
    }
    state = ez->state;
}

void PortA11y_GameTick(void) {
    int v;

    if (!PortA11y_Begin() || gPortResim) {
        return;
    }
    if (!sOn) {
        sOn = 1;
        v = Port_Setting("a11y_menu_voices", 50);
        sMenuVoices = v < 0 ? 0 : v > 100 ? 100 : v;
        v = Port_Setting("a11y_fight_voices", sMenuVoices);
        sFightVoices = v < 0 ? 0 : v > 100 ? 100 : v;
        v = Port_Setting("a11y_fight_music", 100);
        sFightMusic = v < 0 ? 0 : v > 100 ? 100 : v;
    }
    sBlank++;
    battle_watch();
    screen_change();
    dialog();
    if (gProgress != NULL) {
        title();
        main_menu();
    }
    welcome();
    duel_menu();
    options();
    char_select();
    solo_select();
    ub_team_select();
    entry_select();
    ub_menu();
    plates();
    mission_select();
    tour_menu();
    hist_select();
    episode_list();
    train_menu();
    survival_select();
    sim_top();
    sim_day();
    bracket();
    team_select();
    replay_menu();
    char_ref();
    shop();
    evo_custom();
    dc_pass();
    disc_fusion();
    btl_menu();
    msg_window();
    result_sheet();
    hist_result();
    get_win();
    shown_text();
    fight_gauges();
    fight_cues();
    fight_result();
    /* (BATTLE_FLAG_PAUSE: the fight frozen; online a pause menu does not freeze it, and the rival's is not this player's) */
    PortA11y_FightAlerts(fighting() && btl_menu_shown() == NULL && !(gBattleWork.flags & BATTLE_FLAG_PAUSE));
    PortA11y_FightMenu(fighting() && btl_menu_shown() != NULL); /* (a11y.c: Select goes through the sound glossary) */
    fight_intro();
}
