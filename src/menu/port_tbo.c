#include "common.h"
#include "menu/menu_e.h"
#include "sys/pad.h"
#include "sys/save.h"
#include "sys/dialog.h"

#ifdef PORT
#include <stdlib.h>
extern s32 File_IsPartitionLoaded(s32 pt);
static void Tb_DisconnectDialogTerm(void);
static s32 Tb_DisconnectDialogStartClose(void);
static s32 Tb_DisconnectDialogIsClosed(void);
static s32 Tb_DisconnectDialogIsOpen(void);
/*
 * PC only: "Tenkaichi Budokai Online", the main menu's item 11 (menu_a.c). The port's screen mode (port_screen.c)
 * runs this screen. Its own look for online play, after the mockups of online_design: every picture, piece,
 * letter and button is its own (tbo/ui/*.png, drawn by the renderer at full resolution; see "the pictures" below).
 *
 * The hub lists nine modes. What runs today is what the room-code lobby of port/src/gs/net.c can do (the same as
 * the Dragon Net Battle window, without the window):
 *   Create Room   the host's rules, then a room code to give the other player;
 *   Join Room     a pad keypad (the PC keyboard types too) for the other player's code;
 *   the search    a Dragon Radar while the two games find each other; square: "Train While Waiting" leaves for the
 *                 Ultimate Training menu with the room still open (net.c polls it each blank and starts the match
 *                 from wherever the game is when the other player comes);
 *   Profile       the name the other player sees and the defaults of the connection.
 * Quick Match, Ranked Match, Friends, Tournament, Replays and Leaderboard need the matchmaking service to do more
 * than room codes (a public queue, player ids, ratings): they are listed, marked SOON, and say what they will be.
 * Once both players are there the session is the game's own versus mode (character select, the fight, results).
 */

/* The PC keyboard (port/src/gs/gs_input.c): on a keypad it types (a character at a time: 8 backspace, 13 enter,
   27 escape (back), 0 none) and the arrow keys and the controller move on it. On the other screens it is the pad,
   and Esc asks to close the game, as everywhere. */
extern void PortTbo_TextMode(int on);
extern int PortTbo_TextRead(void);
extern void PortTbo_Copy(const char *text); /* onto the clipboard ("Copy Code") */
extern void PortTbo_Paste(void);            /* the clipboard's room code, typed (Join Room's PASTE) */

/* The game's windows of every menu (battle/view_a_b.c, view_a_c.c), made from the Duel menu's data as
   DuelMenu_Init makes them: the heading's window (IconWin: the cloud swirls in the corners and the line under the
   heading) and the message window (MsgWin: the teal box with the meander corners). */
extern void MsgWin_SetText(void *text); /* (the rest: menu/menu_a.h) */
extern void IconWin_PortHideWords(void);
extern s32 IconWin_PortHeadingMotion(s32 frame, f32 *iconDx, f32 *iconDy, s32 *iconAlpha,
                                     f32 *iconScaleX, f32 *iconScaleY, f32 *titleDx, f32 *titleDy,
                                     s32 *titleAlpha, f32 *titleScaleX, f32 *titleScaleY);
extern void MsgWin_PortDrawString(u16 *str); /* (battle/view_a_b.c) the box with a line of ours, in the game's font */
extern void MsgWin_PortPrepareNativeNames(void);
extern void MsgWin_PortDrawNativeName(const char *name, s32 x0, s32 y0, s32 x1, s32 y1);
extern s32 MsgWin_PortLineRoom(s32 right);
extern void MsgWin_PortSameTop(void);
extern s32 Port_WiiArtAvailable(void);
extern s32 PortTbo_ResultTextCopy(s32 side, char *out, s32 capacity);
extern s32 PortTbo_WiiTextCopy(s32 index, char *out, s32 capacity);
extern s32 PortTbo_DisconnectTextCopy(s32 reason, char *out, s32 capacity);
extern s32 PortTbo_WiiUseFont(void);
extern s32 PortTbo_WiiUseFontForArtwork(s32 textKind);
extern s32 PortTbo_TextId(const char *text, s32 kind);
extern s32 PortTbo_TextInfo(s32 id, s32 what);
extern s32 Port_DcNameOpen(void);
extern void Port_DcNameClose(void);
extern s32 Port_DcNameKey(s32 page, s32 row, s32 col);
extern void Port_DcNamePress(s32 page, s32 row, s32 col);
extern void Port_DcNameDraw(s32 page, s32 row, s32 col, s32 rise);
extern s32 Port_WiiDialogText(s32 line, char *out, s32 capacity);
extern s32 Port_WiiMenuText(s32 line, char *out, s32 capacity);
extern s32 Port_WiiDialogDuration(s32 line, s32 japanese);
extern s32 Port_WiiDialogMouth(s32 line, s32 frame, s32 japanese);
extern s32 Port_WiiVoicePlay(s32 line, s32 japanese);
extern void Voice_Stop(s32 voice);
extern void Adx_SetVolume(s32 ch, s32 vol);
extern void Adx_Resume(s32 ch);
extern s32 Font_GetWidth(u16 *str); /* (battle/col_c.c) a line's width in the game's font */
#define Port_TextMode PortTbo_TextMode
#define Port_TextRead PortTbo_TextRead
static void Port_EscQuit(s32 on) {
    (void)on;
}
static s32 Port_EscTake(void) {
    return 0;
}

/* The fighters' names (Train While Waiting's choice), by character id. */
static const char *const sTbNames[] = {
    "Goku (Early)",
    "Goku (Med)",
    "Goku (Med) Super Saiyan",
    "Goku (End)",
    "Goku (End) Super Saiyan",
    "Goku (End) Super Saiyan 2",
    "Goku (End) Super Saiyan 3",
    "Goku (GT)",
    "Goku (GT) Super Saiyan",
    "Goku (GT) Super Saiyan 3",
    "Goku (GT) Super Saiyan 4",
    "Kid Goku",
    "Great Ape",
    "Kid Gohan",
    "Teen Gohan",
    "Teen Gohan Super Saiyan",
    "Teen Gohan Super Saiyan 2",
    "Gohan",
    "Gohan Super Saiyan",
    "Gohan Super Saiyan 2",
    "Great Saiyaman",
    "Ultimate Gohan",
    "Piccolo (Early)",
    "Piccolo (End)",
    "Nail",
    "Krillin",
    "Yamcha",
    "Tien",
    "Chiaotzu",
    "Vegeta (Scouter)",
    "Vegeta (Scouter) Great Ape",
    "Vegeta",
    "Vegeta Super Saiyan",
    "Super Vegeta",
    "Vegeta (2nd Form)",
    "Vegeta (2nd Form) Super Saiyan",
    "Vegeta (2nd Form) Super Saiyan 2",
    "Majin Vegeta",
    "Vegeta (2nd Form) Super Saiyan 4",
    "Trunks (Sword)",
    "Trunks (Sword) Super Saiyan",
    "Trunks",
    "Trunks Super Saiyan",
    "Super Trunks",
    "Kid Trunks",
    "Kid Trunks Super Saiyan",
    "Goten",
    "Goten Super Saiyan",
    "Gotenks",
    "Gotenks Super Saiyan",
    "Gotenks Super Saiyan 3",
    "Vegito",
    "Super Vegito",
    "Super Gogeta",
    "Gogeta Super Saiyan 4",
    "Mr Satan",
    "Videl",
    "Great Saiyaman 2",
    "Supreme Kai",
    "Kibito Kai",
    "Yajirobe",
    "Paikuhan",
    "Tapion",
    "Pan",
    "Uub",
    "Majuub",
    "Master Roshi",
    "Master Roshi Full Power",
    "Grandpa Gohan",
    "Namu",
    "Hatchan",
    "King Vegeta",
    "King Vegeta Great Ape",
    "Bardock",
    "Bardock Great Ape",
    "Celipa",
    "Celipa Great Ape",
    "Raditz",
    "Raditz Great Ape",
    "Saibamen",
    "Nappa",
    "Nappa Great Ape",
    "Zarbon",
    "Zarbon Transformed",
    "Dodoria",
    "Cui",
    "Ginyu",
    "Recoome",
    "Burter",
    "Jeice",
    "Guldo",
    "Frieza 1st Form",
    "Frieza 2nd Form",
    "Frieza 3rd Form",
    "Frieza Final Form",
    "Frieza 100% Power",
    "Mecha Frieza",
    "King Cold",
    "Appule",
    "Frieza Soldier",
    "Android 16",
    "Android 17",
    "Android 18",
    "Android 19",
    "Android 2",
    "Cell 1st Form",
    "Cell 2nd Form",
    "Perfect Cell",
    "Super Perfect Cell",
    "Cell Jr",
    "Babidi",
    "Dabura",
    "Majin Buu",
    "Evil Buu",
    "Super Buu",
    "Super Buu Gotenks Absorbed",
    "Super Buu Gohan Absorbed",
    "Kid Buu",
    "Garlic Jr",
    "Super Garlic Jr",
    "Wheelo",
    "Turles",
    "Turles Great Ape",
    "Slug",
    "Slug Giant",
    "Sauza",
    "Cooler",
    "Cooler Final Form",
    "Metal Cooler",
    "Android 13",
    "Android 13 Fusion",
    "Broly",
    "Broly Super Saiyan",
    "Broly Legendary Super Saiyan",
    "Zangya",
    "Bojack",
    "Bojack 100% Power",
    "Janemba",
    "Super Janemba",
    "Hildegarn",
    "Baby Vegeta",
    "Super Baby 1",
    "Super Baby 2",
    "Super Baby Great Ape",
    "Super 17",
    "Nuova Shenron",
    "Shin Shenron",
    "Omega Shenron",
    "Taopaipai",
    "Cyborg Tao",
    "General Blue",
    "Akuman",
    "Pilaf Machine",
    "Pilaf Machine Fusion",
    "Tambourine",
    "Demon King Piccolo",
    "Arale",
    "Chichi",
    "Spopovich",
    "Future Gohan",
    "Future Gohan Super Saiyan",
};
#include "menu/tbo_art.h"
#include "menu/tbo_sizes.h"
#include "menu/port_screen.h"

extern void Job_Run(void);
extern void Gfx_BeginFrame(void);
extern void Gfx_EndFrame(s32 arg);
extern void Dma_Flush(void);
extern void Dma_ResetBuffers(void);
extern void File_Stub264D90(void);
extern void ColorFade_StartIn(s32 r, s32 g, s32 b, s32 frames);
extern void ColorFade_StartOut(s32 r, s32 g, s32 b, s32 frames);
extern void ColorFade_Update(void);
extern void ColorFade_Draw(void);
extern s32 ColorFade_IsInDone(void);
extern s32 ColorFade_IsOutDone(void);
extern s32 sceGsSyncPath(s32 mode, u16 timeout);

/* port/src/gs/net.c, state.c, plat_settings.c */
extern void Port_NetOptions(int rollback, int delay);
extern void Port_NetRulesSet(int type, int dp, int time);
extern const char *Port_NetNameGet(void);
extern void Port_NetNameSave(const char *name);
extern int Port_LobbyStartCode(int host, const char *code);
extern int Port_LobbyStartLan(int host, const char *address, int port, int queue);
extern int Port_LanChoice(char *address, int capacity);
extern const char *Port_LobbyPeerName(void);
extern int Port_LobbyPoll(void);
extern void Port_LobbyCancel(void);
extern void Port_LobbyLaunch(void);
extern int Port_NetBattleMenuStyle(int style);
extern int Port_NetSessionsEnded(void); /* gs/net.c */
extern int Port_NetTakeEndReason(void); /* 1: peer left; 2: connection lost */
extern const char *Port_LobbyCode(void);
extern const char *Port_LobbyError(void);
extern int Port_LobbyOther(void);
extern void Port_LobbyBackground(int on);
extern int Port_LobbyInBackground(void);
extern int Port_RollCan(void);
extern int Port_LobbyStartQuick(int queue);   /* S, T, D10, D15, D20 matchmaking queues */
extern void Port_LobbyQueuesAsk(void);        /* asks how many wait in each queue */
extern int Port_LobbyQueueWaiting(int queue); /* ... as last heard, -1 if not known */
extern void Port_NetSpeedSet(int percent);
extern int Port_NetSpeedStandard(void);
extern void Port_NetNameSet(const char *name);
extern void Battle_ClearWork(void);
extern void BattleSetup_SetRule(s32 screenMode, s32 mode, s32 bgm, s32 timeLimit, s32 announcer, s32 stage, s32 unk10);
extern void BattleSetup_SetSide(s32 sideNo, s32 control, s32 pad, s32 memberCount, s32 unk1FC, s32 unk200, s32 lead, void *unk);
extern void BattleSetup_SetMember(s32 sideNo, s32 idx, s32 chara, s32 costume, s32 variant, s32 cpuLevel, f32 health, void *items);
extern void BattleSetup_Finish(void);
extern int Port_DataFingerprint(unsigned long long *out); /* plat_fprint.c: 1 known, 0 being worked out, -1 not done */
extern int Port_DataFingerprintOriginal(void);           /* 1 the data the game computes with is the original disc's */
extern const char *Port_DataFingerprintText(void);
extern int Port_Setting(const char *name, int def);
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingTextSave(const char *name, const char *value);
extern void Port_SettingsWrite(void);

/* the screens */
#define TB_HUB 0
#define TB_CREATE 1
#define TB_JOIN 2
#define TB_WAIT 3
#define TB_PROFILE 4
#define TB_NAME 5
#define TB_QUICK 6
#define TB_SPAR 7 /* Train While Waiting: the fighters of the sparring fight */
#define TB_BLOCK 8 /* "Cannot Go Online": this game's data is not the disc's */
#define TB_WII_HOME 9
#define TB_WII_BATTLE 10
#define TB_WII_FRIENDS 11
#define TB_WII_SETTINGS 12
#define TB_WII_RANKING 13
#define TB_WII_NAME 14
#define TB_WII_CUSTOM 15
#define TB_WII_VS 16
#define TB_WII_FRIEND_MENU 17
#define TB_WII_FRIEND_FORMAT 18
#define TB_WII_FRIEND_CODE 19
#define TB_FIGHTERS 161
#define TB_TOURNAMENT_STAGE 4 /* the World Tournament Stage, at noon */

/* the hub's modes, in the order of the list */
#define HUB_QUICK 0
#define HUB_RANKED 1
#define HUB_CREATE 2
#define HUB_JOIN 3
#define HUB_FRIENDS 4
#define HUB_TOURNAMENT 5
#define HUB_REPLAYS 6
#define HUB_LEADERBOARD 7
#define HUB_PROFILE 8
#define HUB_COUNT 9

/* Create Room's rows */
#define CR_BATTLE 0
#define CR_TIME 1
#define CR_DELAY 2
#define CR_ROLL 3
#define CR_OPEN 4
#define CR_COUNT 5

/* Profile's rows */
/* (the input delay and the rollback are the room's: Create Room sets them; a joiner gets the host's, a quick match
   has its own) */
#define PR_NAME 0
#define PR_COUNT 1

#define KEY_COLS 9
#define KEY_ROWS 4
#define CODE_LEN 6

typedef struct TboScreen {
    s32 frames;
    s32 mode;
    s32 wii;           /* opened from the Wii game's Dragon Net Battle plate */
    s32 wiiCur;
    s32 wiiPrevCur;
    s32 wiiSelectT;
    s32 wiiFocusFrame;
    s32 wiiHomeT;      /* Wii FOD entrance frame; begins after the screen fade */
    s32 wiiDialogLine;
    s32 wiiDialogT;
    s32 wiiDialogStarted;
    s32 wiiDialogTouched;
    s32 wiiDialogJapanese;
    s32 wiiDialogVoiceStarted;
    char wiiDialogText[256];
    s32 wiiBattleCur;
    s32 wiiCustomCur;
    s32 wiiNormalBattle; /* format page opened from Normal Battle, rather than Custom Battle */
    s32 wiiFriend;       /* room-code battle, rather than a public/LAN search */
    s32 wiiBattleT;     /* battle-page entrance, after its fade in */
    s32 wiiNextMode;    /* pending Wii page switch, after fade out */
    s32 resultBattles, resultWins[2];
    s32 wiiSubCur;
    s32 modeT;
    s32 leaving;
    s32 train;          /* it is left for the Ultimate Training, the room still open */
    s32 note;
    char noteLine[96];
    s32 hubCur;
    f32 hubY;           /* where the hub's cursor plate is drawn: it glides */
    /* the host's choices (kept in the settings) */
    s32 battle;         /* 0 one on one, 1 teams of up to five */
    s32 time;           /* 0..4: 60, 90, 180, 240 seconds, no limit */
    s32 delay;          /* -1 automatic, else frames */
    s32 roll;           /* 0 off, else frames (2..8) */
    s32 crCur;
    s32 prCur;
    /* the keypad */
    char code[CODE_LEN + 1];
    s32 codeLen;
    s32 keyCol;
    s32 keyRow;         /* KEY_ROWS: the row of DELETE / JOIN */
    /* the search */
    s32 host;           /* this side opened the room */
    s32 waitFrom;       /* frames when the search began */
    s32 found;          /* the other player is there: the match is starting */
    s32 foundAt;        /* Wii VS: keep both names on screen before launching */
    s32 ended;          /* Port_NetSessionsEnded when it started: a change says the match has been played */
    s32 disconnectReason; /* dismissible message after an interrupted Wii match */
    s32 disconnectClosing;
    s32 disconnectCloseFrames;
    s32 disconnectOpenFrames;
    char disconnectLine[128];
    s32 waitBack;       /* the screen a cancelled search goes back to */
    /* Quick Match */
    s32 queue;          /* 0 one on one, 1 teams, 2..4 teams with 10/15/20 DP */
    s32 qkCur;
    s32 asked;          /* frames when the queues were last asked about */
    /* Train While Waiting */
    s32 spMe;           /* your fighter */
    s32 spFoe;          /* your sparring partner */
    s32 spCur;
    s32 blockBack;      /* the screen "Cannot Go Online" goes back to */
    s32 typing;         /* the PC keyboard types (circle on a keypad); else its keys are the pad's, as everywhere */
    s32 lower;          /* the name pad shows small letters */
    s32 nameCursor;     /* insertion point in the Wii name editor */
    /* typing the name */
    char typed[20];
    s32 typedLen;
} TboScreen;

static TboScreen *sTb;
s32 gPortTboStartWii;
static s32 sTbBgKind;  /* what the search left open for "Train While Waiting" is: 0 joining, 1 hosting, 2 a quick match */
static s32 sTbBgQueue;
static s32 sTbBgFrames; /* how long it had been searching when the sparring fight began; the sign counts on from it */
s32 gPortTboSpar;       /* the battle that runs is Train While Waiting's sparring fight (its sign, its pause menu) */
static char sTbBgCode[CODE_LEN + 1];
static f32 Tb_Beat(s32 period);

static char *sTbHubName[HUB_COUNT] = {"Quick Match", "Ranked Match", "Create Room", "Join Room", "Friends", "Tournament", "Replays", "Leaderboard", "Profile"};
static char *sTbWiiHomeName[3] = {"Nintendo WFC Battle", "Manage Friends", "User Settings"};
static char *sTbWiiBattleName[6] = {"Custom Battle", "Normal Battle", "Friend Battle", "Ranking Battle", "View Ranking", "Battle Record"};
#define TB_WII_CUSTOM_COUNT 5
#define TB_WII_BATTLE_ROW_STEP_X 10.5f
static char *sTbWiiCustomName[TB_WII_CUSTOM_COUNT] = {
    "1 VS 1 Battle", "Team Battle", "10 DP Team Battle", "15 DP Team Battle", "20 DP Team Battle"
};
static char *sTbWiiFriendAction[2] = {"Host Battle", "Join Battle"};
static const char *const sTbWiiBattleHelpDefault[6] = {
    "Search for opponents nationwide and fight in a single battle. (Custom characters OK)",
    "Search for opponents nationwide and fight in a single battle. (Normal characters only)",
    "Search for opponents from your Friend Roster. (Custom characters OK)",
    "A ranking battle; Dragon Net Battle fight. (Custom characters OK)",
    "View Rankings.",
    "View Battle History."
};
static char *sTbWiiBattleHelp[6];
static void Tb_SetWiiTitles(void);
static char *sTbWiiFriendsName[3] = {"Friend List", "Add Friend", "My Friend Code"};
static char *sTbWiiSettingsName[2] = {"Create Player Name", "Initialize Wi-Fi Data"};
static char *sTbWiiRankingName[2] = {"My Area", "Top 10"};

/* Keep the selected catalog's words in game memory for drawing, help text,
   and the screen reader. Art-backed labels use the font only when edited. */
enum { TB_WII_TEXT_COUNT = 37, TB_WII_TEXT_SIZE = 256 };
static char sTbWiiText[TB_WII_TEXT_COUNT][TB_WII_TEXT_SIZE];
static u8 sTbWiiTextEdited[TB_WII_TEXT_COUNT];
#define TB_WII_TX_HOME_TITLE 0
#define TB_WII_TX_HOME_ROW 1
#define TB_WII_TX_BATTLE_TITLE 4
#define TB_WII_TX_BATTLE_ROW 5
#define TB_WII_TX_NORMAL_TITLE 11
#define TB_WII_TX_CUSTOM_ROW 12
#define TB_WII_TX_FRIENDS_TITLE 17
#define TB_WII_TX_FRIENDS_ROW 18
#define TB_WII_TX_SETTINGS_TITLE 21
#define TB_WII_TX_SETTINGS_ROW 22
#define TB_WII_TX_RANKING_TITLE 24
#define TB_WII_TX_RANKING_ROW 25
#define TB_WII_TX_NAME_TITLE 27
#define TB_WII_TX_CUSTOM_TITLE 28
#define TB_WII_TX_FRIEND_HOST 29
#define TB_WII_TX_FRIEND_JOIN 30
#define TB_WII_TX_BATTLE_HELP 31
static void Tb_LoadWiiText(void) {
    s32 i;
    for (i = 0; i < TB_WII_TX_BATTLE_HELP; i++)
        sTbWiiTextEdited[i] = PortTbo_WiiTextCopy(i, sTbWiiText[i], TB_WII_TEXT_SIZE);
    for (i = 0; i < 3; i++) {
        sTbWiiHomeName[i] = sTbWiiText[TB_WII_TX_HOME_ROW + i];
        sTbWiiFriendsName[i] = sTbWiiText[TB_WII_TX_FRIENDS_ROW + i];
    }
    for (i = 0; i < 6; i++) {
        sTbWiiBattleName[i] = sTbWiiText[TB_WII_TX_BATTLE_ROW + i];
        Ps_Copy(sTbWiiText[TB_WII_TX_BATTLE_HELP + i], sTbWiiBattleHelpDefault[i], TB_WII_TEXT_SIZE);
        Port_WiiMenuText(47 + i, sTbWiiText[TB_WII_TX_BATTLE_HELP + i], TB_WII_TEXT_SIZE);
        sTbWiiBattleHelp[i] = sTbWiiText[TB_WII_TX_BATTLE_HELP + i];
    }
    for (i = 0; i < TB_WII_CUSTOM_COUNT; i++) sTbWiiCustomName[i] = sTbWiiText[TB_WII_TX_CUSTOM_ROW + i];
    sTbWiiFriendAction[0] = sTbWiiText[TB_WII_TX_FRIEND_HOST];
    sTbWiiFriendAction[1] = sTbWiiText[TB_WII_TX_FRIEND_JOIN];
    for (i = 0; i < 2; i++) {
        sTbWiiSettingsName[i] = sTbWiiText[TB_WII_TX_SETTINGS_ROW + i];
        sTbWiiRankingName[i] = sTbWiiText[TB_WII_TX_RANKING_ROW + i];
    }
}

/* Apply the same GS tint used by the disabled Wii bar artwork to its text. */
static u32 Tb_WiiTintText(u32 color, u32 tint) {
    return Ps_Rgba(((color & 0xff) * (tint & 0xff)) / 0x80,
                   (((color >> 8) & 0xff) * ((tint >> 8) & 0xff)) / 0x80,
                   (((color >> 16) & 0xff) * ((tint >> 16) & 0xff)) / 0x80,
                   (color >> 24) & 0xff);
}

static s32 Tb_WiiFocusAlpha(s32 row, s32 current) {
    if (row == current) return sTb->wiiSelectT >= 4 ? 0x80 : sTb->wiiSelectT * 0x80 / 4;
    if (row == sTb->wiiPrevCur && sTb->wiiSelectT < 4)
        return (4 - sTb->wiiSelectT) * 0x80 / 4;
    return 0;
}

/* Evolution Z's mc_menu_text_on track in the PS2 movie (00452.bin):
   fl_on_loop has 49 frames, rising by 1/96 to 1.25 then falling by 1/100.
   Only the vertical component is used for the Wii caption. */
static f32 Tb_WiiFocusStretch(void) {
    s32 frame = sTb->wiiFocusFrame % 49;
    if (frame <= 24) return 1.0f + (f32)frame / 96.0f;
    return 1.0f + (f32)(49 - frame) / 100.0f;
}

/* The Wii texture labels switch from their idle sheet to the selected sheet,
   whose vertical scale follows Evolution Z's focused text loop. */
static void Tb_WiiTextureLabel(s32 idle, s32 selected, f32 x, f32 y,
                               f32 v0, f32 v1, s32 focusAlpha, u32 tint, u16 capHeight) {
    /* EvoTop's normal and selected labels have a measured 19-pixel letter fill. */
    const f32 size = 19.0f / (f32)capHeight;
    const f32 middle = y + 16.0f;
    if (focusAlpha < 0x80)
        Ps_SpriteScreen(idle, x, middle - 16.0f * size,
                        x + 512.0f * size, middle + 16.0f * size,
                        0.0f, v0, 1.0f, v1, tint, 0x80 - focusAlpha);
    if (focusAlpha > 0) {
        f32 stretch = size * Tb_WiiFocusStretch();
        f32 top = middle - 16.0f * stretch;
        Ps_SpriteScreen(selected, x, top, x + 512.0f * size, top + 32.0f * stretch,
                        0.0f, v0, 1.0f, v1, tint, focusAlpha);
    }
}

/* Wii coordinates are screen pixels; menu text uses page coordinates. The
   selected layer replaces the idle layer as the Evolution Z clip switches. */
static void Tb_WiiLabelText(char *label, f32 x, f32 y, s32 focusAlpha, u32 tint) {
    s32 layer;
    y -= 3.0f;
    if (PortTbo_WiiUseFont() || sTb->mode == TB_WII_CUSTOM ||
        sTb->mode == TB_WII_FRIEND_MENU || sTb->mode == TB_WII_FRIEND_FORMAT) {
        for (layer = 0; layer < 2; layer++) {
            s32 opacity = layer ? focusAlpha : 0x80 - focusAlpha;
            s32 id;
            if (opacity <= 0) continue;
            id = PortTbo_TextId(label, layer ? 4 : 3);
            if (id >= 0) {
                f32 stretch = layer ? Tb_WiiFocusStretch() : 1.0f;
                f32 top = y + 16.0f - 16.0f * stretch;
                Ps_SpriteScreen(id, x, top, x + 512.0f, top + 32.0f * stretch,
                                0.0f, 0.0f, 1.0f, 1.0f, tint, opacity);
            }
        }
        return;
    }
    {
        f32 sx = 1.0f / (Ps_X(1.0f) - Ps_X(0.0f));
        f32 sy = 1.0f / (Ps_Y(1.0f) - Ps_Y(0.0f));
        f32 scaleX = (19.0f / (PS_CELL_UNITS * PS_CAP_OF_CELL)) * sx;
        f32 scaleY = (19.0f / (PS_CELL_UNITS * PS_CAP_OF_CELL)) * sy;
        f32 tx = (x - Ps_X(0.0f)) * sx;
        f32 textMiddle = (y + 16.0f - Ps_Y(0.0f)) * sy;
        Ps_Align(0);
        for (layer = 0; layer < 2; layer++) {
            s32 opacity = layer ? focusAlpha : 0x80 - focusAlpha;
            f32 stretch = layer ? Tb_WiiFocusStretch() : 1.0f;
            f32 textY = Ps_TextTop(textMiddle, scaleY * stretch);
            if (opacity <= 0) continue;
            Ps_TextExOuterXY(tx, textY, label, scaleX, scaleY * stretch, 230.0f * sx,
                             Tb_WiiTintText(Ps_Rgba(layer ? 0xF4 : 0xCB, layer ? 0xEF : 0xCB,
                                                    layer ? 0x89 : 0xCB, opacity), tint),
                             Tb_WiiTintText(Ps_Rgba(layer ? 0xD2 : 0xCB, layer ? 0x68 : 0xCB,
                                                    layer ? 0x4B : 0xCB, opacity), tint),
                             Tb_WiiTintText(Ps_Rgba(layer ? 0x2C : 0x46, layer ? 0x15 : 0x00,
                                                    layer ? 0x1B : 0x59, opacity), tint),
                             Tb_WiiTintText(Ps_Rgba(layer ? 0x2C : 0x46, layer ? 0x15 : 0x00,
                                                    layer ? 0x1B : 0x59, opacity), tint), sy);
        }
    }
}
static void Tb_WiiTitleText(s32 index, f32 x, f32 y, f32 scaleX, f32 scaleY, s32 alpha) {
    /* Friend Battle has no dedicated Wii title sheet. Match the converted
       Normal/Custom heading even when Menu font is set to Original. */
    if (PortTbo_WiiUseFont() || index == TB_WII_TX_BATTLE_ROW + 2) {
        s32 id = PortTbo_TextId(sTbWiiText[index], 5);
        if (id >= 0)
            Ps_SpriteScreen(id, x, y, x + 512.0f * scaleX, y + 64.0f * scaleY,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, alpha);
        return;
    }
    f32 sx = 1.0f / (Ps_X(1.0f) - Ps_X(0.0f));
    f32 sy = 1.0f / (Ps_Y(1.0f) - Ps_Y(0.0f));
    /* Evolution Z's heading face is 21 screen pixels high. Keep the Wii
       title motion while matching that measured letter height. */
    f32 textScale = (21.0f / (PS_CELL_UNITS * PS_CAP_OF_CELL)) * scaleY * sy;
    Ps_Heading((x - Ps_X(0.0f)) * sx,
               Ps_TextTop((y + 32.0f * scaleY - Ps_Y(0.0f)) * sy, textScale),
               sTbWiiText[index], textScale, 470.0f * scaleX * sx, alpha);
}

static s32 Tb_Home(void) {
    return sTb->wii ? TB_WII_BATTLE : TB_HUB;
}
static const s32 sTbHubReady[HUB_COUNT] = {1, 0, 1, 1, 0, 0, 0, 0, 1};
static char *sTbHubSay[HUB_COUNT] = {
    "Fight whoever is searching right now! No time limit, and you can train while you wait.",
    "Fights that move your rank, season by season. That one is coming soon!",
    "Open a room with your own rules, then give your rival its code. No port forwarding needed!",
    "Got a room code from your rival? Type it in and jump right into their room!",
    "Your rivals, who is online, and invites. That one is coming soon!",
    "The Tenkaichi Budokai itself: brackets of 4 to 16 players. That one is coming soon!",
    "Your last matches, saved to watch again. That one is coming soon!",
    "The strongest in the world, among your friends, and with each fighter. Coming soon!",
    "The name your rivals see.",
};
static char *sTbTime[5] = {"60 SECONDS", "90 SECONDS", "180 SECONDS", "240 SECONDS", "NO LIMIT"};
static char sTbKeys[KEY_ROWS][KEY_COLS + 1] = {"ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ0", "123456789"};
/* Data Center's password keyboard. The last two columns on rows 1/2 move
   the text cursor; the last two on row 3 delete. */
#define WII_KEY_COLS 14
#define WII_KEY_ROWS 4

/* ---------------------------------------------------------------------------------------------- small things */
static f32 Tb_Beat(s32 period) {
    s32 k = sTb->frames % period;

    return (f32)(k * 2 < period ? k * 2 : (period - k) * 2) / (f32)period;
}

static f32 Tb_In(s32 delay) {
    f32 t = (f32)(((sTb->mode == TB_WII_BATTLE || sTb->mode == TB_WII_CUSTOM ||
                    sTb->mode == TB_WII_FRIEND_MENU || sTb->mode == TB_WII_FRIEND_FORMAT) ? sTb->wiiBattleT : sTb->modeT) - delay) / 16.0f;

    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    t = 1.0f - t;
    return 1.0f - t * t * t;
}

static void Tb_Note(char *line) {
    Ps_Copy(sTb->noteLine, line, 96);
    sTb->note = 150;
}

static void Tb_SetMode(s32 mode) {
    s32 previous = sTb->mode;
    if ((sTb->mode == TB_WII_NAME || sTb->mode == TB_WII_FRIEND_CODE) &&
        mode != TB_WII_NAME && mode != TB_WII_FRIEND_CODE) Port_DcNameClose();
    sTb->typing = mode == TB_JOIN || mode == TB_NAME;
    Port_TextMode(sTb->typing);
    if (sTb->mode != mode) { /* (the page that is up again, as when the rival comes on the search: it does not come in anew) */
        sTb->wiiPrevCur = -1;
        sTb->wiiSelectT = mode >= TB_WII_HOME ? 0 : 4;
        sTb->wiiFocusFrame = 0;
        if (sTb->wiiDialogStarted) Voice_Stop(0);
        sTb->wiiDialogStarted = 0;
        sTb->wiiDialogText[0] = 0;
        sTb->modeT = 0;
        if (previous == TB_WII_NAME && mode == TB_WII_SETTINGS)
            sTb->modeT = 24; /* return with the settings page already settled */
        if (mode == TB_WII_HOME) {
            if (previous != TB_WII_SETTINGS && previous != TB_WII_BATTLE) {
                sTb->wiiHomeT = 0;
                sTb->wiiDialogTouched = 0;
            }
            Port_WiiDialogText(0, sTb->wiiDialogText, sizeof(sTb->wiiDialogText));
        }
        if (mode == TB_WII_BATTLE || mode == TB_WII_CUSTOM || mode == TB_WII_VS ||
            mode == TB_WII_FRIEND_MENU || mode == TB_WII_FRIEND_FORMAT || mode == TB_WII_FRIEND_CODE)
            sTb->wiiBattleT = 0;
    }
    sTb->mode = mode;
}

/* Resource 7's PAL text and resource 6's paired Japanese/English lip tracks
   share a line index with both sets of Wii ADX streams. */
static s32 Tb_WiiLine(void) {
    static const s32 battle[6] = {32, 32, 33, 34, 35, 36};
    static const s32 friends[3] = {19, 20, 21};
    static const s32 settings[2] = {30, 31};
    switch (sTb->mode) {
    case TB_WII_HOME: return 6 + sTb->wiiCur;
    case TB_WII_BATTLE: return battle[sTb->wiiBattleCur];
    case TB_WII_FRIENDS: return friends[sTb->wiiSubCur];
    case TB_WII_SETTINGS: return settings[sTb->wiiSubCur];
    case TB_WII_RANKING: return 35;
    }
    return -1;
}

static void Tb_WiiDialogSet(s32 line) {
    s32 played;
    if (sTb->wiiDialogStarted) Voice_Stop(0);
    sTb->wiiDialogLine = line;
    sTb->wiiDialogT = 0;
    sTb->wiiDialogStarted = 1;
    sTb->wiiDialogVoiceStarted = 0;
    /* The game's Sound Options use flag 1 for English, clear for Japanese. */
    sTb->wiiDialogJapanese = gSaveData != NULL && !(gSaveData->flags & 1);
    sTb->wiiDialogText[0] = 0;
    if (!Port_WiiDialogText(line, sTb->wiiDialogText, sizeof(sTb->wiiDialogText))) return;
    Adx_SetVolume(4, 0x80); /* the game's voice 0 ADX channel */
    Adx_Resume(4);
    played = Port_WiiVoicePlay(line, sTb->wiiDialogJapanese);
    sTb->wiiDialogVoiceStarted = played != 0;
    if (played) sTb->wiiDialogJapanese = played == 2;
}

static void Tb_SaveChoices(void) {
    Port_SettingSave("tbo_battle", sTb->battle);
    Port_SettingSave("tbo_time", sTb->time);
    Port_SettingSave("net_delay2", sTb->delay);
    Port_SettingSave("net_rollback2", sTb->roll);
    Port_SettingsWrite();
}

static void Tb_LoadChoices(void) {
    sTb->battle = Port_Setting("tbo_battle", 0);
    sTb->time = Port_Setting("tbo_time", 3);
    sTb->delay = Port_Setting("net_delay2", -1);
    sTb->roll = Port_RollCan() ? Port_Setting("net_rollback2", 8) : 0;
    if (sTb->battle < 0 || sTb->battle > 1) {
        sTb->battle = 0;
    }
    if (sTb->time < 0 || sTb->time > 4) {
        sTb->time = 3;
    }
    if (sTb->delay < -1 || sTb->delay > 6) {
        sTb->delay = -1;
    }
    if (sTb->roll < 0 || sTb->roll > 8) {
        sTb->roll = 8;
    }
    sTb->roll &= ~1;
    sTb->queue = Port_Setting("tbo_queue", 0) == 1;
    sTb->spMe = Port_Setting("tbo_spar_me", 0);
    sTb->spFoe = Port_Setting("tbo_spar_foe", 31);
    if (sTb->spMe < 0 || sTb->spMe >= TB_FIGHTERS) {
        sTb->spMe = 0;
    }
    if (sTb->spFoe < 0 || sTb->spFoe >= TB_FIGHTERS) {
        sTb->spFoe = 31;
    }
}

static void Tb_DelayText(char *out, s32 delay) {
    if (delay < 0) {
        sprintf(out, "AUTOMATIC");
    } else {
        sprintf(out, "%d FRAME", (int)delay);
        if (delay != 1) {
            Ps_Cat(out, "S", 32);
        }
    }
}

static void Tb_RollText(char *out, s32 roll) {
    if (!Port_RollCan()) {
        sprintf(out, "NOT ON THIS PC");
    } else if (roll <= 0) {
        sprintf(out, "OFF");
    } else {
        sprintf(out, "UP TO %d FRAMES", (int)roll);
    }
}

/* ---------------------------------------------------------------------------------------------- the rule */
/* Online matches are only between two unmodded games. Fighter mods are switched off by themselves while a session
   runs (plat_mods.c); what is checked here is the rest of the data the game computes with, against the original
   disc (the gameplay fingerprint, plat_fprint.c: files added under gamedata/mods count). Returns 1 when a search may
   start; else the note or the "Cannot Go Online" window says why. */
static s32 Tb_Vanilla(void) {
    s32 state = Port_DataFingerprint(NULL);

    if (state == 0) {
        Tb_Note("CHECKING YOUR GAME DATA AGAINST THE DISC... TRY AGAIN IN A MOMENT");
        return 0;
    }
    if (state == 1 && Port_DataFingerprintOriginal() == 1) {
        return 1;
    }
    sTb->blockBack = sTb->mode;
    Snd_PlaySe(1, 2);
    Tb_SetMode(TB_BLOCK);
    return 0;
}

static void Tb_BlockUpdate(s32 live) {
    Pad *pad = &gPad[0];

    Ps_Prompt(0, "Back");
    if (live && (pad->gamePressed & (PADG_CROSS | PADG_CIRCLE | PADG_TRIANGLE))) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(sTb->blockBack);
    }
}

/* ---------------------------------------------------------------------------------------------- the search */
static void Tb_BeginSearch(s32 host) {
    s32 state;

    if (!Tb_Vanilla()) {
        return;
    }
    if (host) {
        Port_NetOptions(sTb->roll, sTb->delay < 0 ? -2 : sTb->delay);
        Port_NetRulesSet(sTb->battle, 0, sTb->time);
        Tb_SaveChoices();
    } else {
        Port_NetOptions(-1, -1); /* joining: the host's choices arrive with its answer */
    }
    Port_NetNameGet(); /* (the saved name is the one sent) */
    state = Port_LobbyStartCode(host, host ? "" : sTb->code);
    if (state < 0) {
        Tb_Note("THE NETWORK COULD NOT BE OPENED ON THIS PC");
        return;
    }
    sTb->host = host;
    sTb->waitFrom = sTb->frames;
    sTb->found = 0;
    sTb->waitBack = sTb->mode;
    Tb_SetMode(TB_WAIT);
}

static void Tb_ShowDisconnect(s32 reason) {
    sTb->disconnectReason = reason;
    sTb->disconnectClosing = 0;
    sTb->disconnectCloseFrames = 0;
    sTb->disconnectOpenFrames = 0;
    if (!PortTbo_DisconnectTextCopy(reason, sTb->disconnectLine, sizeof(sTb->disconnectLine)))
        Ps_Copy(sTb->disconnectLine, reason == 1 ? "The other player left the match."
                                                : "Connection lost.", sizeof(sTb->disconnectLine));
}

static void Tb_ShowRoomCodeMessage(void) {
    sTb->disconnectReason = 3; /* validation message, using the same dismissible dialog */
    sTb->disconnectClosing = 0;
    sTb->disconnectCloseFrames = 0;
    sTb->disconnectOpenFrames = 0;
    Ps_Copy(sTb->disconnectLine, "A Room Code has 6 letters and numbers.", sizeof(sTb->disconnectLine));
}

static void Tb_ShowLobbyError(void) {
    const char *line = Port_LobbyError();
    if (strncmp(line, "Room with code", sizeof("Room with code") - 1) == 0) {
        sTb->disconnectReason = 3;
        sTb->disconnectClosing = 0;
        sTb->disconnectCloseFrames = 0;
        sTb->disconnectOpenFrames = 0;
        Ps_Copy(sTb->disconnectLine, line, sizeof(sTb->disconnectLine));
    } else {
        Tb_Note((char *)line);
    }
}

/* What the search says (each frame on the screens that wait): the rival came (the match starts), or it failed.
   Returns 1 when the screen that called has nothing more to do this frame. */
static s32 Tb_SearchNews(void) {
    s32 state;

    /* The session is over: the game is back as it was when it was asked for. */
    if (sTb->found && Port_NetSessionsEnded() != sTb->ended) {
        s32 reason = Port_NetTakeEndReason();
        sTb->found = 0;
        if (sTb->wii && reason) {
            Tb_ShowDisconnect(reason);
            Tb_SetMode(sTb->waitBack);
        } else {
            Tb_SetMode(sTb->wii ? TB_WII_HOME : TB_HUB);
        }
        return 1;
    }
    state = sTb->found ? 2 : Port_LobbyPoll();
    if (state == 2 && !sTb->found) {
        sTb->found = 1;
        sTb->ended = Port_NetSessionsEnded();
        Snd_PlaySe(1, 1);
        Tb_SetMode(TB_WAIT);
        Port_LobbyLaunch(); /* the session begins at the game's next blank, from the game's first moment */
        return 1;
    }
    if (state <= 0 && !sTb->found) {
        if (state == -3) {
            Tb_ShowLobbyError();
        } else if (state == -4) {
            Tb_Note("THE TWO GAMES COULD NOT REACH EACH OTHER, EVEN THROUGH THE RELAY");
        } else if (state < 0) {
            Tb_Note("THE ROOM COULD NOT BE OPENED");
        }
        Port_LobbyCancel();
        Tb_SetMode(sTb->waitBack == TB_WAIT || sTb->waitBack == TB_SPAR ? TB_HUB : sTb->waitBack);
        return 1;
    }
    return sTb->found;
}

static void Tb_WaitUpdate(s32 live) {
    Pad *pad = &gPad[0];

    if (!sTb->found) {
        if (sTb->host == 1 && Port_LobbyCode()[0] != 0) {
            Ps_Prompt(0, "Copy Code");
        }
        Ps_Prompt(1, "Train While Waiting");
        Ps_Prompt(4, "Cancel");
    }
    if (Tb_SearchNews()) {
        return;
    }
    if (!live || sTb->found) {
        return;
    }
    if ((pad->gamePressed & PADG_CROSS) && sTb->host == 1 && Port_LobbyCode()[0] != 0) { /* the code, to paste to the rival */
        PortTbo_Copy(Port_LobbyCode());
        Snd_PlaySe(1, 1);
        Tb_Note("COPIED: PASTE THE CODE TO YOUR RIVAL");
    } else if (pad->gamePressed & PADG_SQUARE) { /* the fighters of the sparring fight first; the search goes on */
        Snd_PlaySe(1, 1);
        sTb->spCur = 2;
        Tb_SetMode(TB_SPAR);
    } else if (pad->gamePressed & (PADG_CIRCLE | PADG_TRIANGLE)) {
        Snd_PlaySe(1, 2);
        Port_LobbyCancel();
        Tb_SetMode(sTb->waitBack == TB_WAIT ? Tb_Home() : sTb->waitBack);
    }
}

/* ---------------------------------------------------------------------------------------------- Train While Waiting */
static char *Tb_Fighter(s32 chara) {
    static char name[TB_FIGHTERS][48];
    static u8 got[TB_FIGHTERS];

    if (chara < 0 || chara >= TB_FIGHTERS) {
        return "-";
    }
    if (!got[chara]) {
        /* (not `?:` between the table's entry and a literal: the 32-bit pointer build read the entry as 8 bytes) */
        if (chara < (s32)(sizeof(sTbNames) / sizeof(sTbNames[0]))) {
            Ps_Copy(name[chara], sTbNames[chara], 48);
        } else {
            Ps_Copy(name[chara], "?", 48);
        }
        got[chara] = 1;
    }
    return name[chara];
}

static void Tb_SparUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 *who;

    Ps_Prompt(3, "Change");
    Ps_Prompt(0, sTb->spCur == 2 ? "Start Training" : "Next");
    Ps_Prompt(2, "Back");
    if (Tb_SearchNews()) {
        return;
    }
    if (!live) {
        return;
    }
    Ps_Move(&sTb->spCur, 3);
    who = sTb->spCur == 0 ? &sTb->spMe : sTb->spCur == 1 ? &sTb->spFoe : NULL;
    if (who != NULL) {
        s32 step = Ps_Side();

        if (pad->gameRepeat & PADG_R1) {
            step = 10;
        } else if (pad->gameRepeat & PADG_L1) {
            step = -10;
        }
        if (step != 0) {
            *who = (*who + step + TB_FIGHTERS * 4) % TB_FIGHTERS;
            Snd_PlaySe(1, 0);
        }
    }
    if (pad->gamePressed & (PADG_CROSS | PADG_START)) {
        if (sTb->spCur == 2 || (pad->gamePressed & PADG_START)) { /* the search stays open: net.c polls it from here on */
            Snd_PlaySe(1, 1);
            Port_SettingSave("tbo_spar_me", sTb->spMe);
            Port_SettingSave("tbo_spar_foe", sTb->spFoe);
            Port_SettingsWrite();
            Port_LobbyBackground(1);
            sTbBgKind = sTb->host;
            sTbBgQueue = sTb->queue;
            sTbBgFrames = sTb->frames - sTb->waitFrom;
            Ps_Copy(sTbBgCode, sTb->code, CODE_LEN + 1);
            sTb->train = 1;
            sTb->leaving = 1;
            ColorFade_StartOut(0, 0, 0, 0x14);
        } else {
            Snd_PlaySe(1, 0);
            sTb->spCur++;
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(TB_WAIT);
    }
}

/* ---------------------------------------------------------------------------------------------- Quick Match */
static void Tb_QuickStart(void) {
    s32 state;

    if (!Tb_Vanilla()) {
        return;
    }
    Port_NetSpeedSet(Port_NetSpeedStandard());
    Port_NetRulesSet(sTb->queue, 0, 4); /* (a queue's rules: its battle type, no time limit, the standard speed) */
    Port_NetOptions(Port_RollCan() ? 8 : 0, -2);
    Port_NetNameSet(Port_NetNameGet());
    Port_SettingSave("tbo_queue", sTb->queue);
    Port_SettingsWrite();
    state = Port_LobbyStartQuick(sTb->queue);
    if (state < 0) {
        Tb_Note("THE NETWORK COULD NOT BE OPENED ON THIS PC");
        return;
    }
    sTb->host = 2;
    sTb->waitFrom = sTb->frames;
    sTb->found = 0;
    sTb->waitBack = TB_QUICK;
    Tb_SetMode(TB_WAIT);
}

static void Tb_QuickUpdate(s32 live) {
    Pad *pad = &gPad[0];

    Ps_Prompt(3, "Change");
    Ps_Prompt(0, sTb->qkCur == 1 ? "Search" : "Next");
    Ps_Prompt(2, "Back");
    if (sTb->frames - sTb->asked > 1800) { /* how many wait in each queue, asked again every half minute */
        sTb->asked = sTb->frames;
        Port_LobbyQueuesAsk();
    }
    if (!live) {
        return;
    }
    Ps_Move(&sTb->qkCur, 2);
    if (sTb->qkCur == 0 && Ps_Side() != 0) {
        sTb->queue = !sTb->queue;
        Snd_PlaySe(1, 0);
    }
    if (pad->gamePressed & (PADG_CROSS | PADG_START)) {
        if (sTb->qkCur == 1 || (pad->gamePressed & PADG_START)) {
            Snd_PlaySe(1, 1);
            Tb_QuickStart();
        } else {
            Snd_PlaySe(1, 0);
            sTb->qkCur = 1;
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(Tb_Home());
    }
}

/* Both Wii format pages share the PC quick-match queues, in their original order. */
static void Tb_WiiCustomSearchStart(void) {
    s32 queue = sTb->wiiFriend ? sTb->wiiCustomCur :
                sTb->wiiCustomCur + (sTb->wiiNormalBattle ? 2 : 0);
    char address[128];
    s32 lan = Port_LanChoice(address, sizeof(address));
    s32 state;

    sTb->queue = queue;
    Port_NetSpeedSet(Port_NetSpeedStandard());
    Port_NetRulesSet(queue == 0 ? 0 : queue == 1 ? 1 : 2,
                     queue >= 2 ? queue - 2 : 0, 4);
    Port_NetOptions(Port_RollCan() ? 8 : 0, -2);
    Port_NetNameSet(Port_NetNameGet());
    if (sTb->wiiFriend) {
        state = Port_LobbyStartCode(1, "");
        sTb->host = 1;
    } else if (lan == 3) {
        state = Port_LobbyStartQuick(queue);
        sTb->host = 2;
    } else {
        /* Direct LAN play is the default for this Wii page. The first local copy hosts; a second copy
           whose bind finds that port occupied joins it over loopback. On separate PCs, give the host IP. */
        if (lan == 0) {
            state = Port_LobbyStartLan(1, "", 25443, queue);
            sTb->host = 1;
            if (state < 0) {
                state = Port_LobbyStartLan(0, "127.0.0.1", 25443, queue);
                sTb->host = 0;
            }
        } else if (lan == 1) {
            state = Port_LobbyStartLan(1, "", 25443, queue);
            sTb->host = 1;
        } else if (lan == 2) {
            state = Port_LobbyStartLan(0, address, 25443, queue);
            sTb->host = 0;
        } else {
            Tb_Note("BT3_LAN MUST BE auto, host, join:<HOST IP>, OR online");
            Tb_SetMode(sTb->wiiFriend ? TB_WII_FRIEND_FORMAT : TB_WII_CUSTOM);
            return;
        }
    }
    if (state < 0) {
        Tb_Note("THE NETWORK COULD NOT BE OPENED ON THIS PC");
        Tb_SetMode(sTb->wiiFriend ? TB_WII_FRIEND_FORMAT : TB_WII_CUSTOM);
        return;
    }
    sTb->waitFrom = sTb->frames;
    sTb->found = 0;
    sTb->waitBack = sTb->wiiFriend ? TB_WII_FRIEND_FORMAT : TB_WII_CUSTOM;
}

static void Tb_WiiFriendJoinStart(void) {
    s32 state;
    if (!Tb_Vanilla()) return;
    Port_NetNameSet(Port_NetNameGet());
    Port_NetOptions(-1, -1); /* the host supplies the battle rules */
    state = Port_LobbyStartCode(0, sTb->code);
    if (state < 0) {
        Tb_Note("THE NETWORK COULD NOT BE OPENED ON THIS PC");
        Tb_SetMode(TB_WII_FRIEND_MENU);
        return;
    }
    sTb->host = 0;
    sTb->found = 0;
    sTb->waitFrom = sTb->frames;
    sTb->waitBack = TB_WII_FRIEND_MENU;
    sTb->wiiNextMode = TB_WII_VS;
    ColorFade_StartOut(0, 0, 0, 0x14);
}

/* ---------------------------------------------------------------------------------------------- the hub */
static void Tb_HubUpdate(s32 live) {
    Pad *pad = &gPad[0];

    Ps_Prompt(0, "Enter");
    Ps_Prompt(2, "Back");
    if (!live) {
        return;
    }
    Ps_Move(&sTb->hubCur, HUB_COUNT);
    if (pad->gamePressed & PADG_CROSS) {
        switch (sTb->hubCur) {
        case HUB_QUICK:
            Snd_PlaySe(1, 1);
            sTb->qkCur = 1;
            sTb->asked = sTb->frames - 100000;
            Tb_SetMode(TB_QUICK);
            break;
        case HUB_CREATE:
            Snd_PlaySe(1, 1);
            sTb->crCur = CR_OPEN;
            Tb_SetMode(TB_CREATE);
            break;
        case HUB_JOIN:
            Snd_PlaySe(1, 1);
            sTb->code[0] = 0;
            sTb->codeLen = 0;
            sTb->keyCol = 0;
            sTb->keyRow = 0;
            Tb_SetMode(TB_JOIN);
            break;
        case HUB_PROFILE:
            Snd_PlaySe(1, 1);
            sTb->prCur = 0;
            Tb_SetMode(TB_PROFILE);
            break;
        default:
            Snd_PlaySe(1, 2);
            Tb_Note("NOT OPEN YET: IT NEEDS MORE FROM THE MATCHMAKING SERVICE THAN ROOM CODES");
            break;
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        sTb->leaving = 1;
        ColorFade_StartOut(0, 0, 0, 0x14);
    }
}

/* The Wii home menu opens its battle or settings page. */
static void Tb_WiiUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 *cur = sTb->mode == TB_WII_HOME ? &sTb->wiiCur
             : sTb->mode == TB_WII_BATTLE ? &sTb->wiiBattleCur
             : (sTb->mode == TB_WII_CUSTOM || sTb->mode == TB_WII_FRIEND_FORMAT) ? &sTb->wiiCustomCur : &sTb->wiiSubCur;
    s32 count = sTb->mode == TB_WII_HOME || sTb->mode == TB_WII_FRIENDS ? 3
              : sTb->mode == TB_WII_BATTLE ? 6
              : sTb->mode == TB_WII_FRIEND_FORMAT ? 5
              : sTb->mode == TB_WII_CUSTOM ? (sTb->wiiNormalBattle ? 3 : 2) : 2;

    if (sTb->disconnectReason) {
        if (sTb->disconnectClosing && Tb_DisconnectDialogIsClosed()) {
            Tb_DisconnectDialogTerm();
            sTb->disconnectReason = 0;
            sTb->disconnectClosing = 0;
        } else if (!sTb->disconnectClosing && live && Tb_DisconnectDialogIsOpen() &&
                   (pad->gamePressed & (PADG_CROSS | PADG_TRIANGLE))) {
            if (Tb_DisconnectDialogStartClose()) {
                sTb->disconnectClosing = 1;
                sTb->disconnectCloseFrames = 0;
            } else {
                Tb_DisconnectDialogTerm();
                sTb->disconnectReason = 0;
            }
        }
        return;
    }

    if (sTb->mode == TB_WII_VS) {
        if (sTb->found && Port_NetSessionsEnded() != sTb->ended) {
            s32 reason = Port_NetTakeEndReason();
            sTb->found = 0;
            if (reason) Tb_ShowDisconnect(reason);
            Tb_SetMode(sTb->waitBack);
            return;
        }
        if (sTb->wiiNextMode) return;
        if (!sTb->found) {
            s32 state = Port_LobbyPoll();
            if (state == 2) {
                sTb->found = 1;
                sTb->foundAt = live ? sTb->frames : -1;
                sTb->ended = Port_NetSessionsEnded();
                Snd_PlaySe(1, 1);
            } else if (state <= 0) {
                if (state == -3) Tb_ShowLobbyError();
                else if (state == -4) Tb_Note("THE TWO GAMES COULD NOT REACH EACH OTHER");
                else if (state < 0) Tb_Note("THE ROOM COULD NOT BE OPENED");
                Port_LobbyCancel();
                Tb_SetMode(sTb->waitBack);
                return;
            }
        } else if (sTb->foundAt < 0 && live) {
            sTb->foundAt = sTb->frames;
        } else if (sTb->foundAt >= 0 && sTb->frames - sTb->foundAt >= 180) {
            Tb_SetMode(TB_WAIT);
            Port_NetBattleMenuStyle(sTb->wiiNormalBattle | (sTb->wiiFriend << 1));
            Port_LobbyLaunch();
            return;
        }
        if (live && !sTb->found && (pad->gamePressed & PADG_TRIANGLE)) {
            Snd_PlaySe(1, 2);
            Port_LobbyCancel();
            sTb->wiiNextMode = sTb->waitBack;
            ColorFade_StartOut(0, 0, 0, 0x14);
        }
        return;
    }

    if (sTb->mode != TB_WII_HOME && sTb->mode != TB_WII_SETTINGS &&
        sTb->mode != TB_WII_BATTLE && sTb->mode != TB_WII_CUSTOM &&
        sTb->mode != TB_WII_FRIEND_MENU && sTb->mode != TB_WII_FRIEND_FORMAT) {
        Ps_Prompt(0, "Enter");
        Ps_Prompt(2, "Back");
    }
    if (!live) {
        return;
    }
    if (sTb->mode == TB_WII_HOME && sTb->wiiHomeT < 23 && Port_WiiArtAvailable()) {
        return; /* the Wii movie enables its buttons after fl_btn_in_1 */
    }
    if ((sTb->mode == TB_WII_BATTLE || sTb->mode == TB_WII_CUSTOM ||
         sTb->mode == TB_WII_FRIEND_MENU || sTb->mode == TB_WII_FRIEND_FORMAT) &&
        sTb->wiiBattleT < 30 && Port_WiiArtAvailable()) {
        return;
    }
    if (sTb->mode != TB_WII_BATTLE && sTb->mode != TB_WII_CUSTOM &&
        sTb->mode != TB_WII_FRIEND_MENU && sTb->mode != TB_WII_FRIEND_FORMAT && !sTb->wiiDialogStarted)
        Tb_WiiDialogSet(sTb->mode == TB_WII_HOME ? 0 : Tb_WiiLine());
    if (sTb->mode == TB_WII_HOME && !sTb->wiiDialogTouched && sTb->wiiDialogLine == 0 &&
        sTb->wiiDialogT > Port_WiiDialogDuration(0, sTb->wiiDialogJapanese) + 12 &&
        Port_WiiDialogDuration(0, sTb->wiiDialogJapanese) > 0)
        Tb_WiiDialogSet(1); /* Giru answers Pan's opening line. */
    {
        s32 previous = *cur;
        Ps_Move(cur, count);
        if (*cur != previous) {
            sTb->wiiPrevCur = previous;
            sTb->wiiSelectT = 0;
            sTb->wiiFocusFrame = 0;
            if (sTb->mode == TB_WII_HOME) sTb->wiiDialogTouched = 1;
        }
        if (*cur != previous && sTb->mode != TB_WII_BATTLE && sTb->mode != TB_WII_CUSTOM &&
            sTb->mode != TB_WII_FRIEND_MENU && sTb->mode != TB_WII_FRIEND_FORMAT)
            Tb_WiiDialogSet(Tb_WiiLine());
    }
    if (pad->gamePressed & PADG_CROSS) {
        if (sTb->mode == TB_WII_HOME) {
            if (*cur == 0) {
                Snd_PlaySe(1, 1);
                sTb->wiiBattleCur = 0;
                sTb->wiiNextMode = TB_WII_BATTLE;
                ColorFade_StartOut(0, 0, 0, 0x14);
            } else if (*cur == 1) {
                /* Manage Friends responds to focus and selection, but has no PC service. */
                Snd_PlaySe(1, 1);
            } else if (*cur == 2) {
                Snd_PlaySe(1, 1);
                sTb->wiiSubCur = 0;
                Tb_SetMode(TB_WII_SETTINGS);
            }
        } else {
            Snd_PlaySe(1, 1);
            if (sTb->mode == TB_WII_BATTLE && *cur == 2) {
                sTb->wiiFriend = 1;
                Tb_SetWiiTitles();
                sTb->wiiSubCur = 0;
                sTb->wiiNextMode = TB_WII_FRIEND_MENU;
                ColorFade_StartOut(0, 0, 0, 0x14);
            } else if (sTb->mode == TB_WII_FRIEND_MENU) {
                if (*cur == 0) {
                    sTb->host = 1;
                    sTb->wiiCustomCur = 0;
                    sTb->wiiNextMode = TB_WII_FRIEND_FORMAT;
                    ColorFade_StartOut(0, 0, 0, 0x14);
                } else {
                    if (!Port_DcNameOpen()) return;
                    sTb->typed[0] = 0;
                    sTb->typedLen = 0;
                    sTb->nameCursor = 0;
                    sTb->lower = 0;
                    sTb->keyCol = sTb->keyRow = 0;
                    sTb->wiiNextMode = TB_WII_FRIEND_CODE;
                    ColorFade_StartOut(0, 0, 0, 0x14);
                }
            } else if (sTb->mode == TB_WII_BATTLE && (*cur == 0 || *cur == 1)) {
                sTb->wiiFriend = 0;
                sTb->wiiNormalBattle = *cur == 1;
                Tb_SetWiiTitles();
                sTb->wiiCustomCur = 0;
                sTb->wiiNextMode = TB_WII_CUSTOM;
                ColorFade_StartOut(0, 0, 0, 0x14);
            } else if (sTb->mode == TB_WII_CUSTOM || sTb->mode == TB_WII_FRIEND_FORMAT) {
                if (Tb_Vanilla()) {
                    sTb->wiiNextMode = TB_WII_VS;
                    ColorFade_StartOut(0, 0, 0, 0x14);
                }
            } else if (sTb->mode == TB_WII_SETTINGS && *cur == 0) {
                if (!Port_DcNameOpen()) {
                    return;
                }
                Ps_Copy(sTb->typed, Port_NetNameGet(), sizeof(sTb->typed));
                sTb->typed[16] = 0; /* matches the online window's 16-character field */
                sTb->typedLen = Ps_Len(sTb->typed);
                sTb->nameCursor = sTb->typedLen;
                sTb->keyRow = 0;
                sTb->keyCol = 0;
                sTb->lower = 0;
                Tb_SetMode(TB_WII_NAME);
            } else {
                /* Leave unsupported Wii options without a port-specific dialog. */
            }
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        if (sTb->mode == TB_WII_FRIEND_FORMAT) {
            sTb->wiiNextMode = TB_WII_FRIEND_MENU;
            ColorFade_StartOut(0, 0, 0, 0x14);
        } else if (sTb->mode == TB_WII_FRIEND_MENU) {
            sTb->wiiNextMode = TB_WII_BATTLE;
            ColorFade_StartOut(0, 0, 0, 0x14);
        } else if (sTb->mode == TB_WII_CUSTOM) {
            sTb->wiiNextMode = TB_WII_BATTLE;
            ColorFade_StartOut(0, 0, 0, 0x14);
        } else if (sTb->mode == TB_WII_BATTLE) {
            sTb->wiiNextMode = TB_WII_HOME;
            ColorFade_StartOut(0, 0, 0, 0x14);
        } else if (sTb->mode == TB_WII_FRIENDS || sTb->mode == TB_WII_SETTINGS) {
            Tb_SetMode(TB_WII_HOME);
        } else if (sTb->mode == TB_WII_RANKING) {
            Tb_SetMode(TB_WII_BATTLE);
        } else {
            sTb->leaving = 1;
            ColorFade_StartOut(0, 0, 0, 0x14);
        }
    }
}

/* ---------------------------------------------------------------------------------------------- Create Room */
static void Tb_CreateUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 side;

    Ps_Prompt(3, "Change");
    Ps_Prompt(0, sTb->crCur == CR_OPEN ? "Open Room" : "Next");
    Ps_Prompt(2, "Back");
    if (!live) {
        return;
    }
    Ps_Move(&sTb->crCur, CR_COUNT);
    side = Ps_Side();
    if (side != 0) {
        switch (sTb->crCur) {
        case CR_BATTLE:
            sTb->battle = !sTb->battle;
            break;
        case CR_TIME:
            sTb->time = Ps_Wrap(sTb->time + side, 0, 4);
            break;
        case CR_DELAY:
            sTb->delay = Ps_Wrap(sTb->delay + side, -1, 6);
            break;
        case CR_ROLL:
            if (Port_RollCan()) {
                sTb->roll = Ps_Wrap(sTb->roll + side * 2, 0, 8);
            }
            break;
        }
        if (sTb->crCur != CR_OPEN) {
            Snd_PlaySe(1, 0);
        }
    }
    if (pad->gamePressed & (PADG_CROSS | PADG_START)) {
        if (sTb->crCur == CR_OPEN || (pad->gamePressed & PADG_START)) {
            Snd_PlaySe(1, 1);
            Tb_BeginSearch(1);
        } else {
            Snd_PlaySe(1, 0);
            sTb->crCur++;
        }
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SaveChoices();
        Tb_SetMode(Tb_Home());
    }
}

/* ---------------------------------------------------------------------------------------------- the keypads */
/* A pad keypad: rows of nine keys and, under them, a row of wide buttons, each over a run of the columns. Up / down
   / left / right move, cross presses; circle switches the PC keyboard to typing and back. */
typedef struct TbKeyBtn {
    char *label;
    s32 c0;
    s32 c1;
    s32 act; /* TB_KEY_ */
} TbKeyBtn;
#define TB_KEY_DELETE 1
#define TB_KEY_OK 2
#define TB_KEY_CASE 3
#define TB_KEY_SPACE 4
#define TB_KEY_PASTE 5

static const TbKeyBtn sTbJoinBtn[3] = {{"DELETE", 0, 2, TB_KEY_DELETE}, {"PASTE", 3, 5, TB_KEY_PASTE}, {"JOIN", 6, 8, TB_KEY_OK}};
static const TbKeyBtn sTbNameBtn[4] = {{"Aa", 0, 1, TB_KEY_CASE}, {"SPACE", 2, 4, TB_KEY_SPACE}, {"DELETE", 5, 6, TB_KEY_DELETE}, {"DONE", 7, 8, TB_KEY_OK}};
#define NAME_ROWS 5
static char sTbNameKeys[2][NAME_ROWS][KEY_COLS + 1] = {{"ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ0", "123456789", "-_.!?#&*+"},
                                                     {"abcdefghi", "jklmnopqr", "stuvwxyz0", "123456789", "-_.!?#&*+"}};

static s32 Tb_BtnAt(const TbKeyBtn *b, s32 nb, s32 col) {
    s32 k;

    for (k = 0; k < nb; k++) {
        if (col >= b[k].c0 && col <= b[k].c1) {
            return k;
        }
    }
    return 0;
}

/* The cursor's moves. Returns 0, a key's letter, or -(TB_KEY_) for a button, when cross is pressed. */
static s32 Tb_PadUpdate(s32 rows, char (*keys)[KEY_COLS + 1], const TbKeyBtn *b, s32 nb) {
    Pad *pad = &gPad[0];

    if (sTb->keyRow == rows) { /* the buttons' row: a step goes to the next button */
        s32 k = Tb_BtnAt(b, nb, sTb->keyCol);

        if (pad->gameRepeat & (PADG_RIGHT | PADG_RIGHT2)) {
            sTb->keyCol = b[(k + 1) % nb].c0;
            Snd_PlaySe(1, 0);
        } else if (pad->gameRepeat & (PADG_LEFT | PADG_LEFT2)) {
            sTb->keyCol = b[(k + nb - 1) % nb].c0;
            Snd_PlaySe(1, 0);
        }
    } else if (pad->gameRepeat & (PADG_RIGHT | PADG_RIGHT2)) {
        sTb->keyCol = (sTb->keyCol + 1) % KEY_COLS;
        Snd_PlaySe(1, 0);
    } else if (pad->gameRepeat & (PADG_LEFT | PADG_LEFT2)) {
        sTb->keyCol = (sTb->keyCol + KEY_COLS - 1) % KEY_COLS;
        Snd_PlaySe(1, 0);
    }
    if (pad->gameRepeat & (PADG_DOWN | PADG_DOWN2)) {
        sTb->keyRow = (sTb->keyRow + 1) % (rows + 1);
        Snd_PlaySe(1, 0);
    } else if (pad->gameRepeat & (PADG_UP | PADG_UP2)) {
        sTb->keyRow = (sTb->keyRow + rows) % (rows + 1);
        Snd_PlaySe(1, 0);
    }
    if (sTb->keyRow == rows) {
        sTb->keyCol = b[Tb_BtnAt(b, nb, sTb->keyCol)].c0;
    }
    if (pad->gamePressed & PADG_CROSS) {
        return sTb->keyRow < rows ? (u8)keys[sTb->keyRow][sTb->keyCol] : -b[Tb_BtnAt(b, nb, sTb->keyCol)].act;
    }
    return 0;
}

/* Draws a keypad at (x, y): its keys, the cursor and the buttons' row. */
static void Tb_Keypad(f32 x, f32 y, s32 rows, char (*keys)[KEY_COLS + 1], const TbKeyBtn *b, s32 nb) {
    char one[2] = {0, 0};
    s32 r;
    s32 c;
    s32 k;
    f32 by = y + 34.0f * (f32)rows + 4.0f;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < KEY_COLS; c++) {
            f32 kx = x + 36.0f * (f32)c;
            f32 ky = y + 34.0f * (f32)r;
            s32 on = r == sTb->keyRow && c == sTb->keyCol;

            if (on) {
                Ps_Pill(TBO_KIT_SEL, kx - 2.0f, ky - 2.0f, kx + 34.0f, ky + 30.0f, 0x80);
            } else {
                Ps_Pill(TBO_KIT_BAR, kx, ky, kx + 32.0f, ky + 28.0f, 0x80);
            }
            one[0] = keys[r][c];
            Ps_Align(1);
            Ps_Text(kx + 16.0f, Ps_TextTop(ky + 14.0f, 0.72f), one, 0.72f, 0.0f, on ? 1 : 0);
            Ps_Align(0);
        }
    }
    for (k = 0; k < nb; k++) {
        f32 x0 = x + 36.0f * (f32)b[k].c0;
        f32 x1 = x + 36.0f * (f32)b[k].c1 + 32.0f;
        s32 on = sTb->keyRow == rows && sTb->keyCol >= b[k].c0 && sTb->keyCol <= b[k].c1;

        /* (JOIN and DONE as the others: gold is the one the cursor is on) */
        Ps_Pill(on ? TBO_KIT_SEL : TBO_KIT_BAR, x0, by, x1, by + 28.0f, 0x80);
        Ps_Align(1);
        Ps_Text((x0 + x1) * 0.5f, Ps_TextTop(by + 14.0f, 0.6f), b[k].label, 0.6f, x1 - x0 - 12.0f, on ? 1 : 0);
        Ps_Align(0);
    }
}

/* ---------------------------------------------------------------------------------------------- Join Room */
static void Tb_CodeAdd(s32 c) {
    if (c >= 'a' && c <= 'z') {
        c -= 'a' - 'A';
    }
    if (((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) && sTb->codeLen < CODE_LEN) {
        sTb->code[sTb->codeLen++] = (char)c;
        sTb->code[sTb->codeLen] = 0;
        Snd_PlaySe(1, 0);
    }
}

static void Tb_CodeBack(void) {
    if (sTb->codeLen > 0) {
        sTb->code[--sTb->codeLen] = 0;
        Snd_PlaySe(1, 2);
    }
}

static void Tb_JoinTry(void) {
    if (sTb->codeLen == CODE_LEN) {
        Snd_PlaySe(1, 1);
        if (sTb->mode == TB_WII_FRIEND_CODE) Tb_WiiFriendJoinStart();
        else Tb_BeginSearch(0);
    } else {
        Snd_PlaySe(1, 2);
        Tb_ShowRoomCodeMessage();
    }
}

static void Tb_JoinUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 c;

    Ps_Prompt(0, "Enter");
    Ps_Prompt(1, "Delete");
    Ps_Prompt(2, "Back");
    if (!live) {
        return;
    }
    while (sTb->typing && (c = Port_TextRead()) != 0) { /* the PC keyboard: letters, backspace, enter, escape */
        if (c == 13) {
            Tb_JoinTry();
            return;
        } else if (c == 27) { /* Esc: back, as triangle */
            Snd_PlaySe(1, 2);
            Tb_SetMode(sTb->mode == TB_WII_FRIEND_CODE ? TB_WII_FRIEND_MENU : Tb_Home());
            return;
        } else if (c == 8) {
            Tb_CodeBack();
        } else {
            Tb_CodeAdd(c);
            if (sTb->codeLen == CODE_LEN) { /* full (typed or pasted): the cursor goes to JOIN */
                sTb->keyRow = KEY_ROWS;
                sTb->keyCol = sTbJoinBtn[2].c0;
            }
        }
    }
    c = Tb_PadUpdate(KEY_ROWS, sTbKeys, sTbJoinBtn, 3);
    if (c > 0) {
        Tb_CodeAdd(c);
        if (sTb->codeLen == CODE_LEN) { /* full: the cursor goes to JOIN */
            sTb->keyRow = KEY_ROWS;
            sTb->keyCol = sTbJoinBtn[2].c0;
        }
    } else if (c == -TB_KEY_PASTE) { /* the copied code in place of what is typed (it comes with the next frames) */
        sTb->codeLen = 0;
        sTb->code[0] = 0;
        Snd_PlaySe(1, 1);
        PortTbo_Paste();
    } else if (c == -TB_KEY_DELETE) {
        Tb_CodeBack();
    } else if (c == -TB_KEY_OK || (pad->gamePressed & PADG_START)) {
        Tb_JoinTry();
    } else if (pad->gamePressed & PADG_SQUARE) {
        Tb_CodeBack();
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(sTb->mode == TB_WII_FRIEND_CODE ? TB_WII_FRIEND_MENU : Tb_Home());
    }
}

/* ---------------------------------------------------------------------------------------------- Profile */
static void Tb_ProfileUpdate(s32 live) {
    Pad *pad = &gPad[0];

    Ps_Prompt(0, "Type Name");
    Ps_Prompt(2, "Back");
    if (!live) {
        return;
    }
    Ps_Move(&sTb->prCur, PR_COUNT);
    if ((pad->gamePressed & PADG_CROSS) && sTb->prCur == PR_NAME) {
        Snd_PlaySe(1, 1);
        Ps_Copy(sTb->typed, Port_NetNameGet(), 20);
        sTb->typedLen = Ps_Len(sTb->typed);
        Tb_SetMode(TB_NAME);
        sTb->keyRow = 0;
        sTb->keyCol = 0;
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        Snd_PlaySe(1, 2);
        Tb_SaveChoices();
        Tb_SetMode(sTb->wii ? TB_WII_HOME : TB_HUB);
    }
}

/* Whether a character is one of the name keypad's (or a space): what the keyboard types into a name too, so a name
   has only letters the game's font has, and the two ways of typing give the same names. */
static s32 Tb_NameKey(s32 c) {
    s32 r;
    s32 k;

    if (c == ' ' && sTb->mode != TB_WII_NAME && sTb->mode != TB_WII_FRIEND_CODE) {
        return 1;
    }
    if (sTb->mode == TB_WII_NAME || sTb->mode == TB_WII_FRIEND_CODE) {
        s32 row, col, page;
        for (page = 0; page < 2; page++)
            for (row = 0; row < 4; row++)
                for (col = 0; col < 14; col++)
                    if (Port_DcNameKey(page, row, col) == c) return 1;
        return 0;
    }
    for (r = 0; r < NAME_ROWS; r++) {
        for (k = 0; k < KEY_COLS; k++) {
            if (sTbNameKeys[0][r][k] == c || sTbNameKeys[1][r][k] == c) {
                return 1;
            }
        }
    }
    return 0;
}

static void Tb_NameAdd(s32 c) {
    if (sTb->mode == TB_WII_FRIEND_CODE) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return;
    }
    if (Tb_NameKey(c) && sTb->typedLen < (sTb->mode == TB_WII_FRIEND_CODE ? CODE_LEN : 16)) {
        if (sTb->mode == TB_WII_NAME || sTb->mode == TB_WII_FRIEND_CODE) {
            s32 i;
            for (i = sTb->typedLen; i >= sTb->nameCursor; i--)
                sTb->typed[i + 1] = sTb->typed[i];
            sTb->typed[sTb->nameCursor++] = (char)c;
            sTb->typedLen++;
        } else {
            sTb->typed[sTb->typedLen++] = (char)c;
            sTb->typed[sTb->typedLen] = 0;
        }
        Snd_PlaySe(1, 0);
    }
}

static void Tb_NameBack(void) {
    if ((sTb->mode == TB_WII_NAME || sTb->mode == TB_WII_FRIEND_CODE) && sTb->typedLen > 0) {
        s32 i;
        if (sTb->nameCursor > 0) sTb->nameCursor--;
        for (i = sTb->nameCursor + 1; i <= sTb->typedLen; i++)
            sTb->typed[i - 1] = sTb->typed[i];
        sTb->typedLen--;
        Snd_PlaySe(1, 2);
    } else if (sTb->mode != TB_WII_NAME && sTb->mode != TB_WII_FRIEND_CODE && sTb->typedLen > 0) {
        sTb->typed[--sTb->typedLen] = 0;
        Snd_PlaySe(1, 2);
    }
}

/* The Data Center password keyboard has 14 columns, four rows, and three wide
   buttons underneath: Quit, Case, Decide. Its side cells move the text cursor. */
static s32 Tb_WiiNameKey(void) {
    Pad *pad = &gPad[0];
    s32 col = sTb->keyCol;
    s32 row = sTb->keyRow;
    s32 next;

    if (pad->gameRepeat & (PADG_RIGHT | PADG_RIGHT2)) {
        if (row == WII_KEY_ROWS) next = col < 4 ? 4 : col < 10 ? 10 : 0;
        else if (row == 3 && col == 0) next = 2;
        else if (row == 3 && col == 12) next = 0;
        else next = (col + 1) % WII_KEY_COLS;
        col = next;
    } else if (pad->gameRepeat & (PADG_LEFT | PADG_LEFT2)) {
        if (row == WII_KEY_ROWS) next = col < 4 ? 10 : col < 10 ? 0 : 4;
        else if (row == 3 && col == 0) next = 12;
        else if (row == 3 && col == 2) next = 0;
        else next = (col + WII_KEY_COLS - 1) % WII_KEY_COLS;
        col = next;
    }
    if (pad->gameRepeat & (PADG_DOWN | PADG_DOWN2)) row = (row + 1) % (WII_KEY_ROWS + 1);
    else if (pad->gameRepeat & (PADG_UP | PADG_UP2)) row = (row + WII_KEY_ROWS) % (WII_KEY_ROWS + 1);
    if (row == WII_KEY_ROWS) col = col < 4 ? 0 : col < 10 ? 4 : 10;
    if (row == 3 && col == 1) col = 0;
    if (row == 3 && col == 13) col = 12;
    if (row != sTb->keyRow || col != sTb->keyCol) {
        sTb->keyRow = row;
        sTb->keyCol = col;
        Snd_PlaySe(1, 0);
        return 0;
    }
    sTb->keyRow = row;
    sTb->keyCol = col;
    if (!(pad->gamePressed & PADG_CROSS)) return 0;
    Port_DcNamePress(sTb->lower, row, col);
    if (row == WII_KEY_ROWS) return col == 0 ? -1 : col == 4 ? -2 : -3;
    if ((row == 1 || row == 2) && col >= 12) return col == 12 ? -4 : -5;
    if (row == 3 && col >= 12) return -6;
    return Port_DcNameKey(sTb->lower, row, col);
}

static void Tb_WiiNameUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 c, done = 0;

    if (!live) return;
    /* Match Data Center: shoulder buttons move the text cursor, Start selects
       Decide, Triangle deletes (or leaves when empty), and Cross uses the
       highlighted on-screen key. Physical typing is disabled for this mode. */
    if (pad->gameRepeat & (PADG_L1 | PADG_L2)) {
        if (sTb->nameCursor > 0) sTb->nameCursor--;
    } else if (pad->gameRepeat & (PADG_R1 | PADG_R2)) {
        if (sTb->nameCursor < sTb->typedLen) sTb->nameCursor++;
    } else if (pad->gamePressed & PADG_CIRCLE) {
        sTb->lower = !sTb->lower;
        Snd_PlaySe(1, 1);
    } else if (pad->gamePressed & PADG_START) {
        sTb->keyRow = WII_KEY_ROWS;
        sTb->keyCol = 10;
        Snd_PlaySe(1, 1);
    } else if (pad->gamePressed & PADG_TRIANGLE) {
        if (sTb->typedLen == 0) done = -1;
        else Tb_NameBack();
    } else {
        c = Tb_WiiNameKey();
        if (c > 0) Tb_NameAdd(c);
        else if (c == -1) done = -1;
        else if (c == -2) {
            sTb->lower = !sTb->lower;
            Snd_PlaySe(1, 1);
        } else if (c == -3) done = 1;
        else if (c == -4 && sTb->nameCursor > 0) sTb->nameCursor--;
        else if (c == -5 && sTb->nameCursor < sTb->typedLen) sTb->nameCursor++;
        else if (c == -6) Tb_NameBack();
    }
    if (done == 1) {
        if (sTb->mode == TB_WII_FRIEND_CODE) {
            if (sTb->typedLen != CODE_LEN) {
                Tb_ShowRoomCodeMessage();
                Snd_PlaySe(1, 2);
                return;
            }
            Ps_Copy(sTb->code, sTb->typed, sizeof(sTb->code));
            sTb->codeLen = CODE_LEN;
            Tb_WiiFriendJoinStart();
            return;
        }
        while (sTb->typedLen > 0 && sTb->typed[sTb->typedLen - 1] == ' ')
            sTb->typed[--sTb->typedLen] = 0;
        if (sTb->typedLen > 0) {
            Port_NetNameSave(sTb->typed);
            Port_SettingTextSave("net_name", sTb->typed);
            Port_SettingsWrite();
            Tb_Note("PLAYER NAME SAVED");
        } else {
            Tb_Note("ENTER A PLAYER NAME");
            return;
        }
        Snd_PlaySe(1, 1);
        Tb_SetMode(TB_WII_SETTINGS);
    } else if (done == -1) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(sTb->mode == TB_WII_FRIEND_CODE ? TB_WII_FRIEND_MENU : TB_WII_SETTINGS);
    }
}

static void Tb_NameUpdate(s32 live) {
    Pad *pad = &gPad[0];
    s32 c;
    s32 done = 0;

    Ps_Prompt(0, "Enter");
    Ps_Prompt(1, "Delete");
    Ps_Prompt(2, "Cancel");
    if (!live) {
        return;
    }
    while (sTb->typing && (c = Port_TextRead()) != 0) {
        if (c == 13) {
            done = 1;
        } else if (c == 27) { /* Esc: cancel, as triangle */
            done = -1;
            break;
        } else if (c == 8) {
            Tb_NameBack();
        } else {
            Tb_NameAdd(c);
        }
    }
    if (!done) {
        c = Tb_PadUpdate(NAME_ROWS, sTbNameKeys[sTb->lower], sTbNameBtn, 4);
        if (c > 0) {
            Tb_NameAdd(c);
        } else if (c == -TB_KEY_SPACE) {
            Tb_NameAdd(' ');
        } else if (c == -TB_KEY_CASE) {
            sTb->lower = !sTb->lower;
            Snd_PlaySe(1, 0);
        } else if (c == -TB_KEY_DELETE || (pad->gamePressed & PADG_SQUARE)) {
            Tb_NameBack();
        } else if (c == -TB_KEY_OK || (pad->gamePressed & PADG_START)) {
            done = 1;
        } else if (pad->gamePressed & PADG_TRIANGLE) {
            done = -1;
        }
    }
    if (done == 1) {
        while (sTb->typedLen > 0 && sTb->typed[sTb->typedLen - 1] == ' ') {
            sTb->typed[--sTb->typedLen] = 0;
        }
        if (sTb->typedLen > 0) {
            Port_NetNameSave(sTb->typed);
            Tb_Note("SAVED: YOUR RIVALS WILL SEE THIS NAME");
        }
        Snd_PlaySe(1, 1);
        Tb_SetMode(TB_PROFILE);
    } else if (done == -1) {
        Snd_PlaySe(1, 2);
        Tb_SetMode(TB_PROFILE);
    }
}

/* ---------------------------------------------------------------------------------------------- what is drawn */
/* ---------------------------------------------------------------------------------------------- the pictures */
/* (how a screen draws: menu/port_screen.c) */
#define TB_BAR_LEFT -30.0f /* the hub's bars start here (off the page: on a 4:3 screen, off its edge) */


/* The left of the crest and the title: in line with the bars, but on the screen. */
static f32 Tb_TitleLeft(void) {
    f32 l = Ps_Left() + 6.0f;

    return l > TB_BAR_LEFT ? l : TB_BAR_LEFT;
}

extern s32 Port_WiiHomeMotionQuarterMilli(s32 track, s32 quarterFrame);

static f32 Tb_WiiMotionQuarter(s32 track, s32 quarterFrame) {
    return (f32)Port_WiiHomeMotionQuarterMilli(track, quarterFrame) / 1000.0f;
}
static u16 *Tb_BoxLine(void);

/* The tournament grounds over the whole window: the middle of the 16:9 picture, as wide as the window is. */
static void Tb_Backdrop(void) {
    f32 aspect;
    f32 pic;
    f32 u;
    f32 v;
    s32 i;

    if (sTb->wii && sTb->mode >= TB_WII_HOME && Port_WiiArtAvailable()) {
        if (sTb->mode == TB_WII_BATTLE || sTb->mode == TB_WII_CUSTOM || sTb->mode == TB_WII_VS ||
            sTb->mode == TB_WII_FRIEND_MENU || sTb->mode == TB_WII_FRIEND_FORMAT ||
            sTb->mode == TB_WII_FRIEND_CODE) {
            f32 nebulaPulse = (1.0f + cosf((f32)sTb->frames * 0.025f)) * 0.5f;
            for (i = 0; i < 8; i++) {
                f32 x = (f32)(i % 4) * 128.0f;
                f32 y = (f32)(i / 4) * 224.0f;
                Ps_SpriteScreen(TBO_WII_BATTLE_BG_0 + i, x, y, x + 128.0f, y + 224.0f,
                                0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
            }
            /* The extracted space layer belongs inside the clock's circular
               window, behind the rows and the VS panels. */
            Ps_SpriteScreen(TBO_WII_BATTLE_NEBULA, 101.0f, 43.0f, 411.0f, 401.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN,
                            0x58 + (s32)(nebulaPulse * 0x28));
            for (i = 0; i < 2; i++) {
                /* The glowing large stars sit around the source sheet's rim.
                   Keep the small central stars at their original screen size. */
                f32 starPulse = (1.0f + cosf((f32)sTb->frames * 0.041f + (f32)i * 1.7f)) * 0.5f;
                f32 srcX0 = i ? 256.0f : 120.0f;
                f32 srcX1 = i ? 390.0f : 256.0f;
                f32 x0 = 101.0f + srcX0 * (310.0f / 512.0f);
                f32 x1 = 101.0f + srcX1 * (310.0f / 512.0f);
                Ps_SpriteScreen(TBO_WII_BATTLE_STARS_0 + i,
                                x0, 43.0f + 110.0f * (358.0f / 512.0f),
                                x1, 43.0f + 390.0f * (358.0f / 512.0f),
                                (srcX0 - (f32)i * 256.0f) / 256.0f, 110.0f / 512.0f,
                                (srcX1 - (f32)i * 256.0f) / 256.0f, 390.0f / 512.0f, PS_PLAIN,
                                0x60 + (s32)(starPulse * 0x20));
            }
            return;
        }
        f32 cloudPhase = (f32)(sTb->frames % 1280) * 0.4f;
        /* Resource 1's eight 128x256 strips form the original 512x512 landscape. */
        for (i = 0; i < 8; i++) {
            f32 x = (f32)(i % 4) * 128.0f;
            f32 y = (f32)(i / 4) * 224.0f;
            Ps_SpriteScreen(TBO_WII_BG_0 + i, x, y, x + 128.0f, y + 224.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
        }
        /* Start at the Wii movie's (0, -10) placement. Adjacent copies make
           the cloud sheet travel left at constant speed and wrap in a loop. */
        for (i = -1; i <= 1; i++) {
            f32 x = (f32)i * 512.0f - cloudPhase;
            Ps_SpriteScreen(TBO_WII_CLOUDS, x, -10.0f, x + 512.0f, 118.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
        }
        for (i = 0; i < 2; i++) {
            f32 glow = Tb_WiiMotionQuarter(TBO_WII_MOTION_SPARKLE_0 + i, sTb->frames * 3);
            s32 alpha = (s32)(glow * 128.0f);
            Ps_SpriteScreen(TBO_WII_SPARKLE_0 + i, 36.0f, 115.0f, 292.0f, 179.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, alpha);
        }
        return;
    }
    if (sTb->wii && sTb->mode >= TB_WII_HOME) {
        Ps_Rect(0.0f, 0.0f, 512.0f, 448.0f, Ps_Rgba(0x20, 0x27, 0x2C, 0x80), Ps_Rgba(0x08, 0x15, 0x1A, 0x80));
        return;
    }
    aspect = (f32)Port_PageAspectMilli() / 1000.0f;
    pic = Ps_PicW(TBO_BG) / Ps_PicH(TBO_BG);
    u = aspect < pic ? aspect / pic : 1.0f;
    v = aspect < pic ? 1.0f : pic / aspect;

    Ps_SpriteScreen(TBO_BG, 0.0f, 0.0f, 512.0f, 448.0f, 0.5f - u * 0.5f, 0.5f - v * 0.5f, 0.5f + u * 0.5f, 0.5f + v * 0.5f, PS_PLAIN, 0x80);
}

/* Home artwork decoded from DragonNe section 0. Each option has its own
   unselected and selected label in resource 5, so moving the cursor uses
   the Wii artwork instead of recoloring a captured frame. */
/* Five complete, smooth up/down cycles over the Wii movie's 500-frame idle
   span. The cosine eases into each turn and joins the next cycle seamlessly. */
static f32 Tb_WiiIdleBob(s32 quarter, f32 height) {
    s32 phase = quarter % 400;
    return -cosf((f32)phase * (6.283185307f / 400.0f)) * height;
}

#define TB_WII_NATIVE_GIRU_W 256.0f
#define TB_WII_NATIVE_GIRU_H 128.0f

static void Tb_DrawWiiHomeArt(void) {
    s32 i;
    s32 idleQuarter = (sTb->frames * 3) % 2000;
    s32 idleFrame = idleQuarter / 4;
    s32 giruIdleQuarter = (idleQuarter + 500) % 2000;
    s32 entryQuarter = sTb->wiiHomeT * 3;
    f32 barX = Tb_WiiMotionQuarter(TBO_WII_MOTION_BARS_X, entryQuarter);
    f32 panX = Tb_WiiMotionQuarter(TBO_WII_MOTION_PAN_ENTRY_X, entryQuarter) + 11.95f;
    f32 panY = Tb_WiiIdleBob(idleQuarter, 6.0f);
    f32 giruX = Tb_WiiMotionQuarter(TBO_WII_MOTION_GIRU_X, entryQuarter) - 184.05f;
    f32 giruY = Tb_WiiMotionQuarter(TBO_WII_MOTION_GIRU_Y, entryQuarter) - 71.85f;
    f32 giruHover = Tb_WiiIdleBob(giruIdleQuarter, 4.5f);
    f32 radarScale = Tb_WiiFocusStretch();
    f32 iconDx = 0.0f, iconDy = 0.0f;
    f32 titleDx = 0.0f, titleDy = 0.0f;
    s32 iconAlpha = 0x80, titleAlpha = 0x80;
    f32 iconScaleX = 1.0f, iconScaleY = 1.0f;
    f32 titleScaleX = 1.0f, titleScaleY = 1.0f;

    IconWin_PortHeadingMotion(sTb->frames, &iconDx, &iconDy, &iconAlpha,
                              &iconScaleX, &iconScaleY, &titleDx, &titleDy,
                              &titleAlpha, &titleScaleX, &titleScaleY);
    /* The reversed 256x128 Wii sprite has 85 pixels of transparent canvas
       before its visible art. Set its resting bounds to x=346..512. */
    giruX -= 117.0f;

    /* The entrance plays at 3/4 speed. The idle bob has smooth turns at the
       same cadence, with independent Pan and Giru phases. */

    {
        s32 pointing = sTb->wiiDialogTouched && sTb->wiiDialogLine != 1;
        s32 lipOpen = sTb->wiiDialogVoiceStarted && sTb->wiiDialogLine != 1 &&
                      sTb->wiiDialogT <= Port_WiiDialogDuration(sTb->wiiDialogLine, sTb->wiiDialogJapanese) &&
                      Port_WiiDialogMouth(sTb->wiiDialogLine, sTb->wiiDialogT, sTb->wiiDialogJapanese);
        /* The two full-body textures contain Pan's widest mouth. The mouth
           overlays are only the half-open and closed shapes. */
        s32 mouthShape = !lipOpen ? 0 : (sTb->wiiDialogT / 10 & 1) ? 1 : 2;
        s32 blink = (idleFrame >= 80 && idleFrame < 88) || (idleFrame >= 200 && idleFrame < 208) ||
                    (idleFrame >= 320 && idleFrame < 328) || (idleFrame >= 440 && idleFrame < 448);
        f32 mouthX = (pointing ? 70.0f : 100.0f) + panX;
        f32 mouthY = (pointing ? 183.0f : 197.0f) + panY;

        Ps_SpriteScreen(pointing ? TBO_WII_PAN_POINT : TBO_WII_PAN,
                        -13.0f + panX, 9.0f + panY, 243.0f + panX, 521.0f + panY,
                        0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
        /* The base art already has open eyes. Only cover them for a blink. */
        if (blink) {
            f32 eyeX = (pointing ? 56.0f : 79.0f) + panX;
            f32 eyeY = (pointing ? 158.0f : 176.0f) + panY;
            Ps_SpriteScreen(pointing ? TBO_WII_PAN_POINT_EYES_CLOSED : TBO_WII_PAN_EYES_CLOSED,
                            eyeX, eyeY, eyeX + 64.0f, eyeY + 32.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
        }
        if (mouthShape != 2)
            Ps_SpriteScreen(pointing ? (mouthShape ? TBO_WII_PAN_POINT_MOUTH_OPEN : TBO_WII_PAN_POINT_MOUTH_CLOSED)
                                     : (mouthShape ? TBO_WII_PAN_MOUTH_OPEN : TBO_WII_PAN_MOUTH_CLOSED),
                            mouthX, mouthY, mouthX + 32.0f, mouthY + 32.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
    }
    Ps_SpriteScreen(TBO_WII_GIRU, 378.0f + giruX, 233.0f + giruY + giruHover,
                    378.0f + giruX + TB_WII_NATIVE_GIRU_W,
                    233.0f + giruY + giruHover + TB_WII_NATIVE_GIRU_H,
                    1.0f, 0.0f, 0.0f, 1.0f, PS_PLAIN, 0x80);
    {
        s32 giruTalking = sTb->wiiDialogVoiceStarted && sTb->wiiDialogLine == 1 &&
                          sTb->wiiDialogT <= Port_WiiDialogDuration(1, sTb->wiiDialogJapanese) &&
                          Port_WiiDialogMouth(1, sTb->wiiDialogT, sTb->wiiDialogJapanese);
        s32 eye = giruTalking ? TBO_WII_GIRU_EYE_BRIGHT : TBO_WII_GIRU_EYE_DIM;
        Ps_SpriteScreen(eye, 378.0f + giruX, 233.0f + giruY + giruHover,
                        378.0f + giruX + TB_WII_NATIVE_GIRU_W,
                        233.0f + giruY + giruHover + TB_WII_NATIVE_GIRU_H,
                        1.0f, 0.0f, 0.0f, 1.0f, PS_PLAIN, 0x80);
    }
    for (i = 0; i < 3; i++) {
        f32 y = 91.0f + (f32)i * 47.0f;
        s32 selectAlpha = Tb_WiiFocusAlpha(i, sTb->wiiCur);
        u32 tint = i == 1 ? Ps_Rgba(0x58, 0x58, 0x58, 0x80) : PS_PLAIN;
        Ps_SpriteScreen(TBO_WII_BAR_BLUE, barX, y, barX + 461.0f, y + 58.0f,
                        0.0f, 0.0f, 1.0f, 1.0f, tint, 0x80);
        if (selectAlpha > 0)
            Ps_SpriteScreen(TBO_WII_BAR_GOLD, barX, y, barX + 461.0f, y + 58.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, tint, selectAlpha);
        if (PortTbo_WiiUseFontForArtwork(sTbWiiTextEdited[TB_WII_TX_HOME_ROW + i])) {
            Tb_WiiLabelText(sTbWiiHomeName[i], barX + 49.0f, y + 2.0f,
                            selectAlpha, tint);
        } else {
            Tb_WiiTextureLabel(TBO_WII_LABEL_IDLE, TBO_WII_LABEL_SELECTED,
                               barX + 49.0f, y + 2.0f,
                               (f32)i * 0.25f, (f32)(i + 1) * 0.25f, selectAlpha, tint,
                               sTboWiiLabelCapHeight[0]);
        }
    }
    /* The rows finish sliding in at source frame 17 (port frame 23).
       Show the radar afterward and put it directly on the selected row. */
    if (sTb->wiiHomeT >= 23) {
        f32 radarY = 108.0f + 47.0f * (f32)sTb->wiiCur;
        s32 radarAlpha = (sTb->wiiHomeT - 22) * 16;
        if (radarAlpha > 0x80) radarAlpha = 0x80;
        Ps_SpriteScreen(TBO_WII_RADAR, 196.0f - 32.0f * radarScale, radarY - 32.0f * radarScale,
                        196.0f + 32.0f * radarScale, radarY + 32.0f * radarScale,
                        0.0f, 0.0f, 1.0f, 1.0f,
                        sTb->wiiCur == 1 ? Ps_Rgba(0x58, 0x58, 0x58, 0x80) : PS_PLAIN,
                        radarAlpha);
    }
    /* The native mc_title grows horizontally from its left edge. */
    if (PortTbo_WiiUseFontForArtwork(sTbWiiTextEdited[TB_WII_TX_HOME_TITLE]))
        Tb_WiiTitleText(TB_WII_TX_HOME_TITLE, 62.0f + titleDx, 17.0f + titleDy,
                        titleScaleX, titleScaleY, titleAlpha);
    else
        Ps_SpriteScreen(TBO_WII_TITLE, 62.0f + titleDx,
                        17.0f + titleDy,
                        62.0f + titleDx + 512.0f * titleScaleX,
                        17.0f + titleDy + 64.0f * titleScaleY,
                        0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, titleAlpha);
    Ps_SpriteScreen(TBO_WII_CREST, 45.0f + iconDx - 32.0f * iconScaleX,
                    48.0f + iconDy - 32.0f * iconScaleY,
                    45.0f + iconDx + 32.0f * iconScaleX,
                    48.0f + iconDy + 32.0f * iconScaleY,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, iconAlpha);
    MsgWin_PortDrawString(Tb_BoxLine());
}

/* The host in a pose (TBO_HOSTS_*), standing on `feet` around `cx`, `height` units tall; they breathe. The ones that
   stand on the message box's line are drawn half behind it (the box is drawn over them, and is see-through): their
   pictures run down to the foot of the screen. */
#define TB_HOSTS_BOX_TOP 326.0f /* the top edge of the message box (the game's, MsgWin) */
#define TB_HOSTS_SCALE 1.2f     /* those half behind the box, larger than the pages ask */
static void Tb_Hosts(s32 pose, f32 cx, f32 feet, f32 height) {
    if (feet >= 300.0f) { /* (on the box's line) half the picture behind the box: its middle on the box's top edge */
        height *= TB_HOSTS_SCALE;
        feet = TB_HOSTS_BOX_TOP + height * 0.5f;
    }
    Ps_Stand(pose, cx, feet + 2.0f * Tb_Beat(90) - 1.0f, height, 0x80);
}

/* The Dragon Radar of the search: its glass, its grid, the sweeping line, a few dragon balls blinking. */
static void Tb_Radar(f32 cx, f32 cy, f32 r) {
    f32 rx = r * 0.857f;
    f32 sweep = (f32)sTb->frames * 0.06f;
    s32 k;

    Ps_Disc(cx, cy, r + 10.0f, Ps_Rgba(0xD8, 0xDC, 0xE4, 0x80), Ps_Rgba(0x70, 0x78, 0x88, 0x80)); /* the casing */
    Ps_Disc(cx, cy, r + 3.0f, Ps_Rgba(0x20, 0x30, 0x28, 0x80), Ps_Rgba(0x10, 0x18, 0x14, 0x80));
    Ps_Disc(cx, cy, r, Ps_Rgba(0x40, 0xC8, 0x60, 0x80), Ps_Rgba(0x10, 0x60, 0x28, 0x80));    /* the glass */
    for (k = -3; k <= 3; k++) {                                                                  /* the grid */
        f32 t = (f32)k / 3.5f;
        f32 half = sqrtf(1.0f - t * t);

        Ps_Rect(cx + t * rx - 0.6f, cy - half * r, cx + t * rx + 0.6f, cy + half * r, Ps_Rgba(0x90, 0xFF, 0xA0, 0x50), Ps_Rgba(0x90, 0xFF, 0xA0, 0x50));
        Ps_Rect(cx - half * rx, cy + t * r - 0.6f, cx + half * rx, cy + t * r + 0.6f, Ps_Rgba(0x90, 0xFF, 0xA0, 0x50), Ps_Rgba(0x90, 0xFF, 0xA0, 0x50));
    }
    for (k = 0; k < 10; k++) { /* the sweep: a wedge that fades behind the line */
        f32 a0 = sweep - (f32)(k + 1) * 0.07f;
        f32 a1 = sweep - (f32)k * 0.07f;
        s32 al = 0x60 - k * 6;

        Ps_Quad(cx, cy, cx, cy, cx + cosf(a0) * rx, cy + sinf(a0) * r, cx + cosf(a1) * rx, cy + sinf(a1) * r, Ps_Rgba(0xD0, 0xFF, 0xC0, al),
                Ps_Rgba(0xD0, 0xFF, 0xC0, al), Ps_Rgba(0xD0, 0xFF, 0xC0, al / 2), Ps_Rgba(0xD0, 0xFF, 0xC0, al / 2));
    }
    {
        static const f32 ball[5][2] = {{0.35f, -0.4f}, {-0.5f, 0.2f}, {0.15f, 0.55f}, {-0.2f, -0.65f}, {0.62f, 0.25f}};

        for (k = 0; k < 5; k++) {
            f32 bx = cx + ball[k][0] * rx;
            f32 by = cy + ball[k][1] * r;
            s32 al = ((sTb->frames + k * 17) / 20) % 3 == 0 ? 0x40 : 0x80;

            Ps_Disc(bx, by, 4.0f, Ps_Rgba(0xFF, 0xF8, 0x80, al), Ps_Rgba(0xFF, 0xA0, 0x20, al));
        }
    }
    Ps_Disc(cx, cy, 3.0f, Ps_Rgba(0xFF, 0x50, 0x40, 0x80), Ps_Rgba(0xA0, 0x10, 0x10, 0x80)); /* you */
}

static f32 Tb_HubRowY(s32 i) {
    return 68.0f + 27.5f * (f32)i; /* the last bar (under the cursor: to +27) ends above the box (y 326) */
}

static f32 Tb_HubRowEnd(s32 i) {
    return 270.0f - 7.0f * (f32)i; /* the bars cascade: each a little shorter than the one above */
}

static void Tb_DrawHub(void) {
    s32 i;
    f32 slide = (1.0f - Tb_In(0)) * -160.0f;
    f32 target = Tb_HubRowY(sTb->hubCur);

    Tb_Hosts(TBO_HOSTS_MIC_WAVE, 396.0f - slide * 0.6f, 350.0f, 262.0f);
    sTb->hubY += (target - sTb->hubY) * 0.35f;
    for (i = 0; i < HUB_COUNT; i++) {
        f32 y = Tb_HubRowY(i);
        f32 x1 = Tb_HubRowEnd(i) + slide;

        if (i != sTb->hubCur) {
            Ps_Pill(TBO_KIT_BAR, TB_BAR_LEFT + slide, y, x1, y + 24.0f, sTbHubReady[i] ? 0x80 : 0x60);
        }
    }
    Ps_Pill(TBO_KIT_SEL, TB_BAR_LEFT + slide, sTb->hubY - 3.0f, Tb_HubRowEnd(sTb->hubCur) + 22.0f + slide, sTb->hubY + 27.0f, 0x80);
    for (i = 0; i < HUB_COUNT; i++) {
        if (!sTbHubReady[i]) {
            Ps_Pill(TBO_KIT_STEEL, Tb_HubRowEnd(i) - 46.0f + slide + (i == sTb->hubCur ? 18.0f : 0.0f), Tb_HubRowY(i) + 5.0f,
                    Tb_HubRowEnd(i) - 6.0f + slide + (i == sTb->hubCur ? 18.0f : 0.0f), Tb_HubRowY(i) + 19.0f, 0x80);
        }
    }
}

static void Tb_DrawHubText(void) {
    s32 i;
    f32 slide = (1.0f - Tb_In(0)) * -160.0f;

    for (i = 0; i < HUB_COUNT; i++) {
        f32 y = Tb_HubRowY(i);

        /* each label's capitals in the middle of its bar (the bar under the cursor: hubY - 3 to hubY + 27) */
        if (i == sTb->hubCur) {
            Ps_Text(36.0f + slide, Ps_TextTop(sTb->hubY + 12.0f, 0.84f), sTbHubName[i], 0.84f, 0.0f, 1);
        } else {
            Ps_Text(36.0f + slide, Ps_TextTop(y + 12.0f, 0.7f), sTbHubName[i], 0.7f, 0.0f, sTbHubReady[i] ? 0 : 4);
        }
        if (!sTbHubReady[i]) {
            Ps_Align(1);
            Ps_Text(Tb_HubRowEnd(i) - 26.0f + slide + (i == sTb->hubCur ? 18.0f : 0.0f), Ps_TextTop(Tb_HubRowY(i) + 12.0f, 0.42f), "SOON", 0.42f,
                    0.0f, 4);
            Ps_Align(0);
        }
    }
}

static void Tb_DrawWiiBattle(void) {
    s32 i;
    s32 friendMenu = sTb->mode == TB_WII_FRIEND_MENU;
    s32 friendFormat = sTb->mode == TB_WII_FRIEND_FORMAT;
    s32 custom = sTb->mode == TB_WII_CUSTOM || friendFormat || friendMenu;
    s32 rows = friendMenu ? 2 : friendFormat ? 5 : custom ? (sTb->wiiNormalBattle ? 3 : 2) : 6;
    f32 giruIn = (f32)sTb->wiiBattleT / 29.0f;
    f32 giruHover = Tb_WiiIdleBob(((sTb->frames * 3) + 500) % 2000, 4.5f);
    f32 giruX, giruY;
    f32 iconDx = 0.0f, iconDy = 0.0f, titleDx = 0.0f, titleDy = 0.0f;
    f32 iconScaleX = 1.0f, iconScaleY = 1.0f, titleScaleX = 1.0f, titleScaleY = 1.0f;
    f32 radarScale = Tb_WiiFocusStretch();
    s32 iconAlpha = 0x80, titleAlpha = 0x80;
    u32 disabled = Ps_Rgba(0x58, 0x58, 0x58, 0x80);

    Ps_SetShift(0.0f);
    /* Giru's section 1 texture is 256x128. Its visible pixels begin five
       pixels into the canvas, so x=0 shows him fully at the left edge. */
    if (giruIn > 1.0f) giruIn = 1.0f;
    giruIn = giruIn * giruIn * (3.0f - 2.0f * giruIn);
    giruX = -TB_WII_NATIVE_GIRU_W + TB_WII_NATIVE_GIRU_W * giruIn;
    giruY = 82.0f + giruHover;
    Ps_SpriteScreen(TBO_WII_BATTLE_GIRU,
                    giruX, giruY, giruX + TB_WII_NATIVE_GIRU_W, giruY + TB_WII_NATIVE_GIRU_H,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
    Ps_SpriteScreen(TBO_WII_BATTLE_GIRU_EYE_DIM,
                    giruX, giruY, giruX + TB_WII_NATIVE_GIRU_W, giruY + TB_WII_NATIVE_GIRU_H,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);

    if (!custom && (sTb->wiiBattleCur == 3 || sTb->wiiBattleCur == 4) && sTb->wiiBattleT >= 30) {
        /* The Wii's ranking-side panel. The displayed points are a four-digit
           placeholder until ranking data exists in the port. */
        Ps_SpriteScreen(TBO_WII_BATTLE_POINT_PANEL, -40.0f, 234.0f, 172.0f, 344.0f,
                        0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
        Ps_SpriteScreen(TBO_WII_BATTLE_POINT_LABEL, 15.0f, 239.0f, 220.0f, 271.0f,
                        0.0f, 0.0f, 1.0f, 0.5f, PS_PLAIN, 0x80);
        for (i = 0; i < 4; i++) {
            f32 x = 54.0f + (f32)i * 16.0f;
            Ps_SpriteScreen(TBO_WII_BATTLE_POINT_DIGITS, x, 270.0f, x + 32.0f, 302.0f,
                            0.0f, 0.0f, 0.25f, 0.25f, PS_PLAIN, 0x80);
        }
    }

    for (i = 0; i < rows; i++) {
        /* In the Wii menu each lower row begins a little farther right. */
        f32 x = 170.0f + (f32)i * TB_WII_BATTLE_ROW_STEP_X
                + (1.0f - Tb_In(i * 2)) * 512.0f;
        f32 y = 67.0f + (f32)i * (custom ? (friendFormat ? 42.0f : 45.0f) : 42.0f);
        s32 focusAlpha = Tb_WiiFocusAlpha(i, friendMenu ? sTb->wiiSubCur : custom ? sTb->wiiCustomCur : sTb->wiiBattleCur);
        f32 v0 = (f32)i * 32.0f / 256.0f;
        f32 v1 = (f32)(i + 1) * 32.0f / 256.0f;
        u32 tint = (custom || i <= 2) ? PS_PLAIN : disabled;
        Ps_SpriteScreen(TBO_WII_BATTLE_BAR_BLUE, x, y, x + 512.0f, y + 64.0f,
                        0.0f, 0.0f, 1.0f, 1.0f, tint, 0x80);
        if (focusAlpha > 0)
            Ps_SpriteScreen(TBO_WII_BATTLE_BAR_GOLD, x, y, x + 512.0f, y + 64.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, tint, focusAlpha);
        if (friendMenu) {
            Tb_WiiLabelText(sTbWiiFriendAction[i], x + 46.0f, y + 4.0f, focusAlpha, tint);
        } else if (custom) {
            /* The format names have no Wii lettering sheet. Use the same
               menu-label rasterizer with the first Menu font on Original. */
            Tb_WiiLabelText(sTbWiiCustomName[i + (friendFormat ? 0 : sTb->wiiNormalBattle ? 2 : 0)],
                            x + 46.0f, y + 4.0f, focusAlpha, tint);
        } else {
            if (PortTbo_WiiUseFontForArtwork(sTbWiiTextEdited[TB_WII_TX_BATTLE_ROW + i]))
                Tb_WiiLabelText(sTbWiiBattleName[i], x + 49.0f, y + 5.0f, focusAlpha, tint);
            else {
                Tb_WiiTextureLabel(TBO_WII_BATTLE_LABEL_IDLE, TBO_WII_BATTLE_LABEL_SELECTED,
                                   x + 49.0f, y + 5.0f, v0, v1, focusAlpha, tint,
                                   sTboWiiLabelCapHeight[1]);
            }
        }
    }
    if (sTb->wiiBattleT >= 20) {
        s32 cur = friendMenu ? sTb->wiiSubCur : custom ? sTb->wiiCustomCur : sTb->wiiBattleCur;
        f32 radarX = 196.0f + (f32)cur * TB_WII_BATTLE_ROW_STEP_X;
        f32 radarY = 84.0f + (f32)cur * (custom ? (friendFormat ? 42.0f : 45.0f) : 42.0f);
        Ps_SpriteScreen(TBO_WII_RADAR, radarX - 32.0f * radarScale,
                        radarY - 32.0f * radarScale,
                        radarX + 32.0f * radarScale, radarY + 32.0f * radarScale,
                        0.0f, 0.0f, 1.0f, 1.0f,
                        custom || sTb->wiiBattleCur <= 1 ? PS_PLAIN : disabled,
                        0x80);
    }
    IconWin_PortHeadingMotion(sTb->wiiBattleT, &iconDx, &iconDy, &iconAlpha,
                              &iconScaleX, &iconScaleY, &titleDx, &titleDy,
                              &titleAlpha, &titleScaleX, &titleScaleY);
    if (friendMenu || friendFormat) {
        Tb_WiiTitleText(TB_WII_TX_BATTLE_ROW + 2, 62.0f + titleDx, 17.0f + titleDy,
                        titleScaleX, titleScaleY, titleAlpha);
    } else if (PortTbo_WiiUseFontForArtwork(sTbWiiTextEdited[custom ? (sTb->wiiNormalBattle ? TB_WII_TX_NORMAL_TITLE
                                                                               : TB_WII_TX_CUSTOM_TITLE)
                                                              : TB_WII_TX_BATTLE_TITLE]))
        Tb_WiiTitleText(custom ? (sTb->wiiNormalBattle ? TB_WII_TX_NORMAL_TITLE : TB_WII_TX_CUSTOM_TITLE)
                               : TB_WII_TX_BATTLE_TITLE,
                        62.0f + titleDx, 17.0f + titleDy, titleScaleX, titleScaleY, titleAlpha);
    else
        Ps_SpriteScreen(custom ? (sTb->wiiNormalBattle ? TBO_WII_NORMAL_TITLE : TBO_WII_CUSTOM_TITLE)
                               : TBO_WII_BATTLE_TITLE,
                        62.0f + titleDx, 17.0f + titleDy,
                        62.0f + titleDx + 512.0f * titleScaleX,
                        17.0f + titleDy + 64.0f * titleScaleY,
                        0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, titleAlpha);
    Ps_SpriteScreen(TBO_WII_CREST, 45.0f + iconDx - 32.0f * iconScaleX,
                    48.0f + iconDy - 32.0f * iconScaleY,
                    45.0f + iconDx + 32.0f * iconScaleX,
                    48.0f + iconDy + 32.0f * iconScaleY,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, iconAlpha);
}

/* The Wii record plate's badges, three labels, and zeroed record values. */
static void Tb_WiiVsRecord(f32 top, s32 second) {
    static const f32 column[3] = {103.0f, 248.0f, 403.0f};
    s32 i;

    Ps_SpriteScreen(TBO_WII_VS_PLAYERS, 18.0f, top + 32.0f, 82.0f, top + 64.0f,
                    0.0f, second ? 0.5f : 0.0f, 1.0f, second ? 1.0f : 0.5f,
                    PS_PLAIN, 0x80);
    for (i = 0; i < 3; i++) {
        s32 wins = i == 1;
        f32 x = column[i];
        s32 value = i == 0 ? sTb->resultBattles : i == 1 ? sTb->resultWins[second]
                                                                 : sTb->resultWins[!second];
        s32 place = value >= 100 ? 100 : value >= 10 ? 10 : 1;
        f32 dx = x - (place == 100 ? 24.0f : place == 10 ? 12.0f : 0.0f);
        for (; place; place /= 10, dx += 12.0f) {
            s32 digit = (value / place) % 10;
            f32 u = (f32)(digit % 4) * 0.25f;
            f32 v = (f32)(digit / 4) * 0.25f;
            Ps_SpriteScreen(wins ? TBO_WII_VS_WINS_DIGITS : TBO_WII_VS_DIGITS,
                            dx, top + 70.0f, dx + 18.0f, top + 96.0f,
                            u, v, u + 26.0f / 128, v + 0.25f,
                            wins ? PS_PLAIN
                                 : i == 0 ? Ps_Rgba(0x50, 0x78, 0x58, 0x80) : PS_PLAIN, 0x80);
        }
        Ps_SpriteScreen(TBO_WII_VS_RECORD_LABELS,
                        x + 18.0f, top + 70.0f, x + 98.0f, top + 102.0f,
                        0.0f, (f32)i * 0.25f, 80.0f / 128.0f, (f32)(i + 1) * 0.25f,
                        PS_PLAIN, 0x80);
    }
}

/* The two halves of each Wii plate are mirrored from resource 3's left cap. */
static void Tb_DrawWiiVs(void) {
    f32 giruIn = (f32)sTb->wiiBattleT / 29.0f;
    f32 giruX, giruY;
    f32 iconDx = 0.0f, iconDy = 0.0f, titleDx = 0.0f, titleDy = 0.0f;
    f32 iconScaleX = 1.0f, iconScaleY = 1.0f, titleScaleX = 1.0f, titleScaleY = 1.0f;
    s32 iconAlpha = 0x80, titleAlpha = 0x80;

    Ps_SetShift(0.0f);
    if (giruIn > 1.0f) giruIn = 1.0f;
    giruIn = giruIn * giruIn * (3.0f - 2.0f * giruIn);
    giruX = -TB_WII_NATIVE_GIRU_W + TB_WII_NATIVE_GIRU_W * giruIn;
    giruY = 275.0f + Tb_WiiIdleBob(((sTb->frames * 3) + 500) % 2000, 4.5f);
    Ps_SpriteScreen(TBO_WII_BATTLE_GIRU, giruX, giruY,
                    giruX + TB_WII_NATIVE_GIRU_W, giruY + TB_WII_NATIVE_GIRU_H,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
    Ps_SpriteScreen(TBO_WII_BATTLE_GIRU_EYE_DIM, giruX, giruY,
                    giruX + TB_WII_NATIVE_GIRU_W, giruY + TB_WII_NATIVE_GIRU_H,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);

    Ps_SpriteScreen(TBO_WII_VS_PANELS, 0.0f, 63.0f, 256.0f, 191.0f,
                    0.0f, 0.0f, 1.0f, 0.5f, PS_PLAIN, 0x80);
    Ps_SpriteScreen(TBO_WII_VS_PANELS, 256.0f, 63.0f, 512.0f, 191.0f,
                    1.0f, 0.0f, 0.0f, 0.5f, PS_PLAIN, 0x80);
    Ps_SpriteScreen(TBO_WII_VS_PANELS, 0.0f, 203.0f, 256.0f, 331.0f,
                    0.0f, 0.5f, 1.0f, 1.0f, PS_PLAIN, 0x80);
    Ps_SpriteScreen(TBO_WII_VS_PANELS, 256.0f, 203.0f, 512.0f, 331.0f,
                    1.0f, 0.5f, 0.0f, 1.0f, PS_PLAIN, 0x80);
    Ps_SpriteScreen(TBO_WII_VS_LINES, 0.0f, 63.0f, 512.0f, 191.0f,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
    Ps_SpriteScreen(TBO_WII_VS_LINES, 0.0f, 203.0f, 512.0f, 331.0f,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
    Tb_WiiVsRecord(63.0f, 0);
    Tb_WiiVsRecord(203.0f, 1);
    Ps_SpriteScreen(TBO_WII_VS_MARK, 186.0f, 165.0f, 314.0f, 229.0f,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);

    IconWin_PortHeadingMotion(sTb->wiiBattleT, &iconDx, &iconDy, &iconAlpha,
                              &iconScaleX, &iconScaleY, &titleDx, &titleDy,
                              &titleAlpha, &titleScaleX, &titleScaleY);
    if (sTb->wiiFriend) {
        Tb_WiiTitleText(TB_WII_TX_BATTLE_ROW + 2, 62.0f + titleDx, 17.0f + titleDy,
                        titleScaleX, titleScaleY, titleAlpha);
    } else if (PortTbo_WiiUseFontForArtwork(sTbWiiTextEdited[sTb->wiiNormalBattle ? TB_WII_TX_NORMAL_TITLE
                                                                          : TB_WII_TX_CUSTOM_TITLE]))
        Tb_WiiTitleText(sTb->wiiNormalBattle ? TB_WII_TX_NORMAL_TITLE : TB_WII_TX_CUSTOM_TITLE,
                        62.0f + titleDx, 17.0f + titleDy, titleScaleX, titleScaleY, titleAlpha);
    else
        Ps_SpriteScreen(sTb->wiiNormalBattle ? TBO_WII_NORMAL_TITLE : TBO_WII_CUSTOM_TITLE,
                        62.0f + titleDx, 17.0f + titleDy,
                        62.0f + titleDx + 512.0f * titleScaleX,
                        17.0f + titleDy + 64.0f * titleScaleY,
                        0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, titleAlpha);
    Ps_SpriteScreen(TBO_WII_CREST, 45.0f + iconDx - 32.0f * iconScaleX,
                    48.0f + iconDy - 32.0f * iconScaleY,
                    45.0f + iconDx + 32.0f * iconScaleX,
                    48.0f + iconDy + 32.0f * iconScaleY,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, iconAlpha);
}

static void Tb_DrawWii(void) {
    s32 i;
    s32 count = sTb->mode == TB_WII_HOME || sTb->mode == TB_WII_FRIENDS ? 3
              : sTb->mode == TB_WII_BATTLE ? 6
              : sTb->mode == TB_WII_FRIEND_FORMAT ? 5
              : sTb->mode == TB_WII_CUSTOM ? (sTb->wiiNormalBattle ? 3 : 2) : 2;
    s32 cur = sTb->mode == TB_WII_HOME ? sTb->wiiCur
            : sTb->mode == TB_WII_BATTLE ? sTb->wiiBattleCur
            : (sTb->mode == TB_WII_CUSTOM || sTb->mode == TB_WII_FRIEND_FORMAT) ? sTb->wiiCustomCur : sTb->wiiSubCur;
    f32 y0 = count == 6 ? 87.0f : 114.0f;

    if ((sTb->mode == TB_WII_BATTLE || sTb->mode == TB_WII_CUSTOM ||
         sTb->mode == TB_WII_FRIEND_MENU || sTb->mode == TB_WII_FRIEND_FORMAT) && Port_WiiArtAvailable()) {
        Tb_DrawWiiBattle();
        return;
    }

    if (sTb->mode == TB_WII_SETTINGS && Port_WiiArtAvailable()) {
        f32 iconDx = 0.0f, iconDy = 0.0f, titleDx = 0.0f, titleDy = 0.0f;
        f32 iconScaleX = 1.0f, iconScaleY = 1.0f, titleScaleX = 1.0f, titleScaleY = 1.0f;
        f32 panStartX, panStartY, giruStartX, giruStartY, giruHover;
        f32 radarScale = Tb_WiiFocusStretch();
        s32 iconAlpha = 0x80, titleAlpha = 0x80;

        panStartX = Tb_WiiMotionQuarter(TBO_WII_MOTION_PAN_ENTRY_X, sTb->wiiHomeT * 3) + 11.95f;
        panStartY = Tb_WiiIdleBob((sTb->frames * 3) % 2000, 6.0f);
        giruStartX = 378.0f + Tb_WiiMotionQuarter(TBO_WII_MOTION_GIRU_X, sTb->wiiHomeT * 3)
                     - 184.05f - 117.0f;
        giruStartY = 233.0f + Tb_WiiMotionQuarter(TBO_WII_MOTION_GIRU_Y, sTb->wiiHomeT * 3)
                     - 71.85f;
        giruHover = Tb_WiiIdleBob(((sTb->frames * 3) + 500) % 2000, 4.5f);
        /* Pan and Giru remain together on User Settings. Giru moves to the
           name field only when the keyboard opens. */
        {
            s32 idleFrame = ((sTb->frames * 3) % 2000) / 4;
            s32 pointing = sTb->wiiDialogTouched && sTb->wiiDialogLine != 1;
            s32 lipOpen = sTb->wiiDialogVoiceStarted && sTb->wiiDialogLine != 1 &&
                          sTb->wiiDialogT <= Port_WiiDialogDuration(sTb->wiiDialogLine, sTb->wiiDialogJapanese) &&
                          Port_WiiDialogMouth(sTb->wiiDialogLine, sTb->wiiDialogT, sTb->wiiDialogJapanese);
            s32 mouthShape = !lipOpen ? 0 : (sTb->wiiDialogT / 10 & 1) ? 1 : 2;
            s32 blink = (idleFrame >= 80 && idleFrame < 88) ||
                        (idleFrame >= 200 && idleFrame < 208) ||
                        (idleFrame >= 320 && idleFrame < 328) ||
                        (idleFrame >= 440 && idleFrame < 448);
            f32 eyeX = (pointing ? 56.0f : 79.0f) + panStartX;
            f32 eyeY = (pointing ? 158.0f : 176.0f) + panStartY;
            f32 mouthX = (pointing ? 70.0f : 100.0f) + panStartX;
            f32 mouthY = (pointing ? 183.0f : 197.0f) + panStartY;

            Ps_SpriteScreen(pointing ? TBO_WII_PAN_POINT : TBO_WII_PAN,
                            -13.0f + panStartX, 9.0f + panStartY,
                            243.0f + panStartX, 521.0f + panStartY,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
            if (blink)
                Ps_SpriteScreen(pointing ? TBO_WII_PAN_POINT_EYES_CLOSED : TBO_WII_PAN_EYES_CLOSED,
                                eyeX, eyeY, eyeX + 64.0f, eyeY + 32.0f,
                                0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
            if (mouthShape != 2)
                Ps_SpriteScreen(pointing ? (mouthShape ? TBO_WII_PAN_POINT_MOUTH_OPEN : TBO_WII_PAN_POINT_MOUTH_CLOSED)
                                         : (mouthShape ? TBO_WII_PAN_MOUTH_OPEN : TBO_WII_PAN_MOUTH_CLOSED),
                                mouthX, mouthY, mouthX + 32.0f, mouthY + 32.0f,
                                0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
        }
        Ps_SpriteScreen(TBO_WII_GIRU, giruStartX, giruStartY + giruHover,
                        giruStartX + TB_WII_NATIVE_GIRU_W,
                        giruStartY + giruHover + TB_WII_NATIVE_GIRU_H,
                        1.0f, 0.0f, 0.0f, 1.0f, PS_PLAIN, 0x80);
        Ps_SpriteScreen(TBO_WII_GIRU_EYE_DIM, giruStartX, giruStartY + giruHover,
                        giruStartX + TB_WII_NATIVE_GIRU_W,
                        giruStartY + giruHover + TB_WII_NATIVE_GIRU_H,
                        1.0f, 0.0f, 0.0f, 1.0f, PS_PLAIN, 0x80);
        IconWin_PortHeadingMotion(sTb->frames, &iconDx, &iconDy, &iconAlpha,
                                  &iconScaleX, &iconScaleY, &titleDx, &titleDy,
                                  &titleAlpha, &titleScaleX, &titleScaleY);
        for (i = 0; i < 2; i++) {
            f32 y = 103.0f + (f32)i * 48.0f;
            s32 disabled = i == 1;
            u32 tint = disabled ? Ps_Rgba(0x58, 0x58, 0x58, 0x80) : PS_PLAIN;
            s32 focusAlpha = Tb_WiiFocusAlpha(i, sTb->wiiSubCur);

            Ps_SpriteScreen(TBO_WII_BAR_BLUE,
                            162.0f, y, 623.0f, y + 58.0f,
                            0.0f, 0.0f, 1.0f, 1.0f, tint, 0x80);
            if (focusAlpha > 0)
                Ps_SpriteScreen(TBO_WII_BAR_GOLD, 162.0f, y, 623.0f, y + 58.0f,
                                0.0f, 0.0f, 1.0f, 1.0f, tint, focusAlpha);
            if (PortTbo_WiiUseFontForArtwork(sTbWiiTextEdited[TB_WII_TX_SETTINGS_ROW + i]))
                Tb_WiiLabelText(sTbWiiSettingsName[i], 212.0f, y + 3.0f, focusAlpha, tint);
            else {
                Tb_WiiTextureLabel(TBO_WII_SETTINGS_LABEL_IDLE, TBO_WII_SETTINGS_LABEL_SELECTED,
                                   212.0f, y + 3.0f,
                                   (f32)i * 0.5f, (f32)(i + 1) * 0.5f, focusAlpha, tint,
                                   sTboWiiLabelCapHeight[2]);
            }
        }
        {
            f32 radarY = 120.0f + (f32)sTb->wiiSubCur * 48.0f;
            Ps_SpriteScreen(TBO_WII_RADAR, 196.0f - 32.0f * radarScale, radarY - 32.0f * radarScale,
                            196.0f + 32.0f * radarScale, radarY + 32.0f * radarScale,
                        0.0f, 0.0f, 1.0f, 1.0f,
                        sTb->wiiSubCur == 1 ? Ps_Rgba(0x58, 0x58, 0x58, 0x80) : PS_PLAIN,
                        0x80);
        }
        if (PortTbo_WiiUseFontForArtwork(sTbWiiTextEdited[TB_WII_TX_SETTINGS_TITLE]))
            Tb_WiiTitleText(TB_WII_TX_SETTINGS_TITLE, 62.0f + titleDx, 17.0f + titleDy,
                            titleScaleX, titleScaleY, titleAlpha);
        else
            Ps_SpriteScreen(TBO_WII_SETTINGS_TITLE, 62.0f + titleDx,
                            17.0f + titleDy,
                            62.0f + titleDx + 512.0f * titleScaleX,
                            17.0f + titleDy + 64.0f * titleScaleY,
                            0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, titleAlpha);
        Ps_SpriteScreen(TBO_WII_CREST, 45.0f + iconDx - 32.0f * iconScaleX,
                        48.0f + iconDy - 32.0f * iconScaleY,
                        45.0f + iconDx + 32.0f * iconScaleX,
                        48.0f + iconDy + 32.0f * iconScaleY,
                        0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, iconAlpha);
        return;
    }

    Ps_Panel(TBO_KIT_WINDOW, 18.0f, y0 - 20.0f, 492.0f, y0 + 36.0f * (f32)count + 14.0f, 0x80);
    for (i = 0; i < count; i++) {
        f32 y = y0 + 36.0f * (f32)i;
        Ps_Pill(i == cur ? TBO_KIT_SEL : TBO_KIT_BAR, 30.0f, y, 480.0f, y + 30.0f, 0x80);
    }
}

static void Tb_DrawWiiText(void) {
    s32 i;
    s32 count = sTb->mode == TB_WII_HOME || sTb->mode == TB_WII_FRIENDS ? 3
              : sTb->mode == TB_WII_BATTLE ? 6
              : sTb->mode == TB_WII_FRIEND_FORMAT ? 5
              : sTb->mode == TB_WII_CUSTOM ? (sTb->wiiNormalBattle ? 3 : 2) : 2;
    s32 cur = sTb->mode == TB_WII_HOME ? sTb->wiiCur
            : sTb->mode == TB_WII_BATTLE ? sTb->wiiBattleCur
            : (sTb->mode == TB_WII_CUSTOM || sTb->mode == TB_WII_FRIEND_FORMAT) ? sTb->wiiCustomCur : sTb->wiiSubCur;
    char **names = sTb->mode == TB_WII_HOME ? sTbWiiHomeName
                  : sTb->mode == TB_WII_BATTLE ? sTbWiiBattleName
                  : sTb->mode == TB_WII_CUSTOM ? sTbWiiCustomName + (sTb->wiiNormalBattle ? 2 : 0)
                  : sTb->mode == TB_WII_FRIEND_FORMAT ? sTbWiiCustomName
                  : sTb->mode == TB_WII_FRIEND_MENU ? sTbWiiFriendAction
                  : sTb->mode == TB_WII_FRIENDS ? sTbWiiFriendsName
                  : sTb->mode == TB_WII_SETTINGS ? sTbWiiSettingsName : sTbWiiRankingName;
    f32 y0 = count == 6 ? 87.0f : 114.0f;

    if ((sTb->mode == TB_WII_SETTINGS || sTb->mode == TB_WII_BATTLE ||
         sTb->mode == TB_WII_CUSTOM || sTb->mode == TB_WII_FRIEND_MENU ||
         sTb->mode == TB_WII_FRIEND_FORMAT) && Port_WiiArtAvailable()) return;

    for (i = 0; i < count; i++) {
        f32 y = y0 + 36.0f * (f32)i;
        f32 scale = 19.0f / ((Ps_Y(1.0f) - Ps_Y(0.0f)) * PS_CELL_UNITS * PS_CAP_OF_CELL);
        Ps_Text(44.0f, Ps_TextTop(y + 15.0f, scale), names[i], scale, 420.0f, i == cur ? 1 : 0);
    }
}

/* The Wii name field is 80 pixels tall inside its border. Draw each 32-pixel
   glyph on its middle line, and place the underline directly below that line.
   The name begins at the Wii field's left inset, like the original editor. */
static void Tb_DrawWiiNameText(f32 rise, s32 alpha) {
    static const char symbols[] = "#$%&@!?-+*()";
    s32 i;

    for (i = 0; i < sTb->typedLen && i < 16; i++) {
        char c = sTb->typed[i];
        s32 page = 0, row, col, sx, sy;
        f32 dx = 32.0f + (f32)i * 25.0f;

        if (c == ' ') continue;
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
            page = 1;
        }
        if (c >= 'A' && c <= 'N') {
            row = 0;
            col = c - 'A';
            sx = 37 + col * 31 + (col >= 7 ? 10 : 0);
        } else if (c >= 'O' && c <= 'Z') {
            row = 1;
            col = c - 'O';
            sx = 32 + col * 31 + (col >= 7 ? 10 : 0);
        } else if (c >= '0' && c <= '9') {
            row = 3;
            col = c - '0';
            sx = 84 + col * 31 + (col >= 5 ? 10 : 0);
        } else {
            for (col = 0; symbols[col] != 0 && symbols[col] != c; col++) {}
            if (symbols[col] == 0) col = 6;
            row = 2;
            sx = 27 + col * 31 + (col >= 7 ? 9 : 0);
        }
        sy = page * 128 + row * 32;
        Ps_SpriteScreen(TBO_WII_NAME_GLYPHS, dx, 111.0f + rise,
                        dx + 31.0f, 143.0f + rise,
                        (f32)sx / 512.0f, (f32)sy / 256.0f,
                        (f32)(sx + 31) / 512.0f, (f32)(sy + 32) / 256.0f,
                        PS_PLAIN, alpha);
    }
    Ps_SpriteScreen(TBO_WII_NAME_CURSOR,
                    32.0f + (f32)sTb->nameCursor * 25.0f, 117.0f + rise,
                    64.0f + (f32)sTb->nameCursor * 25.0f, 149.0f + rise,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, alpha);
}

static void Tb_DrawWiiName(void) {
    f32 in = (f32)sTb->modeT / 20.0f;
    f32 rise, panX, panY, giruX, giruY, giruHover, turn, giruCx, travel, climb;
    s32 giruFrame = sTb->modeT < 29 ? sTb->modeT : 29;
    s32 alpha;

    if (in > 1.0f) in = 1.0f;
    if (in < 0.0f) in = 0.0f;
    in = in * in * (3.0f - 2.0f * in);
    /* The Wii nickname FOD brings every key up by 270 pixels over 16 frames. */
    rise = sTb->modeT < 16 ? (f32)(16 - sTb->modeT) * 18.0f : 0.0f;
    alpha = (s32)(128.0f * in);
    /* Move the keyboard and name field together during the page entrance. */
    Ps_SetShift(0.0f);
    /* Data Center supplies the complete keyboard movie. Wii art supplies
       the name field, title and hosts above it. */
    /* The Wii sheet contains one side of this frame. Its straight edge is
       the centre seam, so mirror it to keep both shaped corners intact. */
    panX = Tb_WiiMotionQuarter(TBO_WII_MOTION_PAN_ENTRY_X, sTb->wiiHomeT * 3) + 11.95f;
    panY = Tb_WiiIdleBob((sTb->frames * 3) % 2000, 6.0f);
    if (sTb->mode == TB_WII_NAME && in < 1.0f)
        Ps_SpriteScreen(sTb->wiiDialogTouched && sTb->wiiDialogLine != 1 ? TBO_WII_PAN_POINT : TBO_WII_PAN,
                        -13.0f + panX - 270.0f * in, 9.0f + panY,
                        243.0f + panX - 270.0f * in, 521.0f + panY,
                        0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
    giruHover = Tb_WiiIdleBob(((sTb->frames * 3) + 500) % 2000, 4.5f);
    /* Match the settings pose at frame zero. Travel left first, then climb
       while Giru turns toward the name field at the upper left. */
    travel = giruFrame < 10 ? (f32)giruFrame / 10.0f : 1.0f;
    climb = giruFrame <= 10 ? 0.0f : (f32)(giruFrame - 10) / 19.0f;
    climb = climb * climb * (3.0f - 2.0f * climb);
    giruX = 261.0f * (1.0f - travel);
    giruY = 233.0f - 195.0f * climb + giruHover;
    turn = giruFrame < 15 ? 1.0f - (f32)giruFrame / 15.0f : (f32)(giruFrame - 15) / 14.0f;
    if (turn < 0.02f) turn = 0.02f;
    giruCx = giruX + TB_WII_NATIVE_GIRU_W * 0.5f;
    Ps_SpriteScreen(TBO_WII_GIRU, giruCx - TB_WII_NATIVE_GIRU_W * turn * 0.5f, giruY,
                    giruCx + TB_WII_NATIVE_GIRU_W * turn * 0.5f, giruY + TB_WII_NATIVE_GIRU_H,
                    giruFrame < 15 ? 1.0f : 0.0f, 0.0f,
                    giruFrame < 15 ? 0.0f : 1.0f, 1.0f, PS_PLAIN, 0x80);
    Ps_SpriteScreen(TBO_WII_GIRU_EYE_DIM, giruCx - TB_WII_NATIVE_GIRU_W * turn * 0.5f, giruY,
                    giruCx + TB_WII_NATIVE_GIRU_W * turn * 0.5f, giruY + TB_WII_NATIVE_GIRU_H,
                    giruFrame < 15 ? 1.0f : 0.0f, 0.0f,
                    giruFrame < 15 ? 0.0f : 1.0f, 1.0f, PS_PLAIN, 0x80);
    /* Draw the frame at the source crop's native height. Its lower contour
       meets the keyboard's upper contour without a doubled border. */
    Port_DcNameDraw(sTb->lower, sTb->keyRow, sTb->keyCol, (s32)rise);
    Ps_SpriteScreen(TBO_WII_NAME_INPUT_FRAME, 0.0f, 42.0f + rise, 256.0f, 186.0f + rise,
                    0.0f, 100.0f / 256.0f, 1.0f, 245.0f / 256.0f, PS_PLAIN, alpha);
    Ps_SpriteScreen(TBO_WII_NAME_INPUT_FRAME, 256.0f, 42.0f + rise, 512.0f, 186.0f + rise,
                    1.0f, 100.0f / 256.0f, 0.0f, 245.0f / 256.0f, PS_PLAIN, alpha);
    Tb_DrawWiiNameText(rise, alpha);
}

/* rows of label / value, as Create Room and Profile have them */
static void Tb_DrawRows(s32 count, s32 cur, f32 y0) {
    s32 i;

    Ps_Panel(TBO_KIT_WINDOW, 14.0f, y0 - 14.0f, 300.0f, y0 + 34.0f * (f32)count + 6.0f, 0x80);
    for (i = 0; i < count; i++) {
        f32 y = y0 + 34.0f * (f32)i;

        if (i == cur) {
            Ps_Pill(TBO_KIT_SEL, 20.0f, y - 2.0f, 294.0f, y + 28.0f, 0x80);
            Ps_Button(3, 278.0f, y + 13.0f, 20.0f);
        } else {
            Ps_Pill(TBO_KIT_BAR, 24.0f, y, 290.0f, y + 26.0f, 0x80);
        }
    }
}

static void Tb_DrawCreate(void) {
    Tb_Hosts(TBO_HOSTS_ARMS_CROSSED, 408.0f, 350.0f, 250.0f);
    Tb_DrawRows(CR_COUNT, sTb->crCur, 100.0f);
}

static void Tb_DrawCreateText(void) {
    static char value[CR_COUNT][32];
    static char *label[CR_COUNT] = {"Battle", "Time Limit", "Input Delay", "Rollback", "Open Room"};
    s32 i;

    sprintf(value[CR_BATTLE], sTb->battle ? "TEAMS (UP TO 5)" : "ONE ON ONE");
    Ps_Copy(value[CR_TIME], sTbTime[sTb->time], 32);
    Tb_DelayText(value[CR_DELAY], sTb->delay);
    Tb_RollText(value[CR_ROLL], sTb->roll);
    value[CR_OPEN][0] = 0;
    Ps_Align(1);
    Ps_Text(157.0f, Ps_TextTop(80.0f, 0.56f), "ROOM SETTINGS", 0.56f, 0.0f, 2);
    Ps_Align(0);
    for (i = 0; i < CR_COUNT; i++) {
        f32 y = 100.0f + 34.0f * (f32)i;

        Ps_Text(36.0f, Ps_TextTop(y + 13.0f, 0.66f), label[i], 0.66f, 120.0f, i == sTb->crCur ? 1 : 0);
        Ps_Align(2);
        Ps_Text(266.0f, Ps_TextTop(y + 13.0f, 0.54f), value[i], 0.54f, 130.0f, 3);
        Ps_Align(0);
    }
}

static void Tb_DrawJoin(void) {
    s32 c;

    Tb_Hosts(TBO_HOSTS_PUZZLED, 432.0f, 350.0f, 200.0f);
    for (c = 0; c < CODE_LEN; c++) { /* the code's six boxes */
        f32 x = 34.0f + 50.0f * (f32)c;

        Ps_Pill(c == sTb->codeLen ? TBO_KIT_SEL : TBO_KIT_BAR, x, 76.0f, x + 44.0f, 116.0f, 0x80);
    }
    Ps_Panel(TBO_KIT_WINDOW, 14.0f, 126.0f, 352.0f, 330.0f, 0x80);
}

static void Tb_DrawJoinText(void) {
    char one[2] = {0, 0};
    s32 c;

    Ps_Align(1);
    for (c = 0; c < CODE_LEN; c++) {
        f32 x = 56.0f + 50.0f * (f32)c;

        one[0] = c < sTb->codeLen ? sTb->code[c] : (c == sTb->codeLen && (sTb->frames / 16) & 1 ? '_' : ' ');
        Ps_Text(x, 81.0f, one, 1.0f, 0.0f, c < sTb->codeLen ? 3 : 0); /* (what is typed: a value) */
    }
    Ps_Align(0);
    Tb_Keypad(24.0f, 138.0f, KEY_ROWS, sTbKeys, sTbJoinBtn, 3);
}

static void Tb_DrawWait(void) {
    if (sTb->found) {
        Tb_Hosts(TBO_HOSTS_CHEER, 426.0f, 350.0f, 180.0f);
    } else {
        Tb_Hosts(TBO_HOSTS_BINOCULARS, 434.0f, 350.0f, 210.0f);
    }
    Tb_Radar(264.0f, 188.0f, 80.0f);
    Ps_Panel(TBO_KIT_WINDOW, 10.0f, 84.0f, 180.0f, 246.0f, 0x80);
    Ps_Panel(TBO_KIT_WINDOW, 176.0f, 292.0f, 348.0f, 328.0f, 0x80); /* the search's clock (a window, not a bar to choose) */
}

static void Tb_DrawWaitText(void) {
    static char line[96];
    s32 secs = (sTb->frames - sTb->waitFrom) / 60;
    const char *code = Port_LobbyCode();

    if (sTb->host == 2) {
        s32 n = Port_LobbyQueueWaiting(sTb->queue);

        Ps_Text(22.0f, 94.0f, "QUICK MATCH", 0.56f, 0.0f, 0);
        Ps_Align(1);
        Ps_Text(95.0f, 124.0f, sTb->queue ? "TEAMS" : "ONE ON ONE", 0.86f, 156.0f, 1);
        Ps_Align(0);
        Ps_Text(22.0f, 172.0f, "THE SERVICE PAIRS YOU", 0.46f, 150.0f, 0);
        Ps_Text(22.0f, 190.0f, "WITH THE NEXT RIVAL", 0.46f, 150.0f, 0);
        if (n > 0) {
            sprintf(line, "%d IN THIS QUEUE", (int)n);
            Ps_Text(22.0f, 214.0f, line, 0.42f, 150.0f, 5);
        }
    } else if (sTb->host) {
        Ps_Text(22.0f, 94.0f, "ROOM CODE", 0.56f, 0.0f, 0);
        Ps_Align(1);
        if (code[0] != 0) {
            Ps_Text(95.0f, 120.0f, (char *)code, 1.2f, 156.0f, 0);
        } else {
            Ps_Text(95.0f, 130.0f, "GETTING A CODE...", 0.5f, 156.0f, 0);
        }
        Ps_Align(0);
        Ps_Text(22.0f, 172.0f, code[0] != 0 ? "GIVE THIS CODE TO" : "ASKING THE", 0.46f, 150.0f, 0);
        Ps_Text(22.0f, 190.0f, code[0] != 0 ? "YOUR RIVAL" : "MATCHMAKING SERVICE", 0.46f, 150.0f, 0);
        if (Port_LobbyOther()) {
            Ps_Text(22.0f, 214.0f, "ANOTHER VERSION TRIED", 0.42f, 150.0f, 3);
        }
    } else {
        Ps_Text(22.0f, 94.0f, "JOINING ROOM", 0.56f, 0.0f, 0);
        Ps_Align(1);
        Ps_Text(95.0f, 120.0f, sTb->code, 1.2f, 156.0f, 0);
        Ps_Align(0);
        Ps_Text(22.0f, 172.0f, "LOOKING FOR", 0.46f, 150.0f, 0);
        Ps_Text(22.0f, 190.0f, "THE HOST...", 0.46f, 150.0f, 0);
    }
    Ps_Align(1);
    if (sTb->found) {
        Ps_Text(262.0f, 302.0f, "YOUR RIVAL IS HERE!", 0.56f, 164.0f, 5);
    } else {
        sprintf(line, "SEARCHING...  %d:%02d", (int)(secs / 60), (int)(secs % 60));
        Ps_Text(262.0f, 302.0f, line, 0.52f, 164.0f, 0);
    }
    Ps_Align(0);
}

static void Tb_DrawBlock(void) {
    Ps_Panel(TBO_KIT_WINDOW, 60.0f, 96.0f, 452.0f, 316.0f, 0x80);
    Tb_Hosts(TBO_HOSTS_PUZZLED, 404.0f, 120.0f, 92.0f);
    Ps_Pill(TBO_KIT_GOLD, 206.0f, 282.0f, 306.0f, 306.0f, 0x80);
}

static void Tb_DrawBlockText(void) {
    static char parts[96];
    const char *t = Port_DataFingerprintText();
    const char *m = t;
    s32 n;

    Ps_Heading(80.0f, 108.0f, "Cannot Go Online", 0.9f, 250.0f, 0x80);
    Ps_Msg(80.0f, 146.0f, "Online matches are only between", 1.0f, 0x80);
    Ps_Msg(80.0f, 166.0f, "two unmodded games.", 1.0f, 0x80);
    while (*m != 0 && strncmp(m, "modified:", 9) != 0) {
        m++;
    }
    if (*m != 0) { /* which parts differ, as the data check says it */
        Ps_Copy(parts, "Changed: ", 96);
        Ps_Cat(parts, m + 10, 9 + 81);
    } else if (Port_DataFingerprint(NULL) == 1) {
        sprintf(parts, "Your game data is not the disc's.");
    } else {
        sprintf(parts, "Your game data could not be checked.");
    }
    for (n = 0; parts[n] != 0; n++) {
        if (parts[n] == ',') {
            parts[n] = ' ';
        }
    }
    Ps_Msg(80.0f, 196.0f, parts, 1.0f, 0x80);
    Ps_Msg(80.0f, 226.0f, "Take out what you added in gamedata/mods.", 1.0f, 0x80);
    Ps_Msg(80.0f, 246.0f, "(Fighter mods switch off by themselves online.)", 1.0f, 0x70);
    Ps_Align(1);
    Ps_Text(256.0f, 285.0f, "BACK", 0.56f, 0.0f, 1);
    Ps_Align(0);
}

/* The name's window: over everything else on the screen (drawn last). */
static void Tb_DrawName(void) {
    static char line[40];

    Ps_Panel(TBO_KIT_WINDOW, 64.0f, 62.0f, 448.0f, 340.0f, 0x80);
    Ps_Text(84.0f, 70.0f, "TYPE YOUR NAME", 0.6f, 0.0f, 2);
    Ps_Pill(TBO_KIT_BAR, 84.0f, 94.0f, 428.0f, 124.0f, 0x80);
    Ps_Copy(line, sTb->typed, 40);
    Ps_Cat(line, (sTb->frames / 16) & 1 ? "_" : " ", 40);
    Ps_Text(100.0f, 97.0f, line, 0.86f, 310.0f, 1);
    Tb_Keypad(94.0f, 134.0f, NAME_ROWS, sTbNameKeys[sTb->lower], sTbNameBtn, 4);
}

static void Tb_DrawSpar(void) {
    Tb_Hosts(TBO_HOSTS_CHEER, 406.0f, 350.0f, 200.0f);
    Tb_DrawRows(3, sTb->spCur, 110.0f);
    Ps_Panel(TBO_KIT_WINDOW, 14.0f, 230.0f, 300.0f, 326.0f, 0x80);
}

static void Tb_DrawSparText(void) {
    static char line[64];
    s32 secs = (sTb->frames - sTb->waitFrom) / 60;

    Ps_Text(26.0f, 76.0f, "SPARRING AT THE TOURNAMENT", 0.6f, 270.0f, 2);
    Ps_Text(36.0f, 115.0f, "You", 0.66f, 70.0f, sTb->spCur == 0 ? 1 : 0);
    Ps_Text(36.0f, 149.0f, "Partner", 0.66f, 80.0f, sTb->spCur == 1 ? 1 : 0);
    Ps_Text(36.0f, 183.0f, "Start Training", 0.66f, 200.0f, sTb->spCur == 2 ? 1 : 0);
    Ps_Align(2);
    Ps_Text(266.0f, 117.0f, Tb_Fighter(sTb->spMe), 0.54f, 170.0f, 3);
    Ps_Text(266.0f, 151.0f, Tb_Fighter(sTb->spFoe), 0.54f, 170.0f, 3);
    Ps_Align(0);
    Ps_Text(28.0f, 240.0f, sTb->host == 2 ? "QUICK MATCH SEARCH" : sTb->host ? "YOUR ROOM IS OPEN" : "JOINING A ROOM", 0.5f, 260.0f, 2);
    sprintf(line, "SEARCHING  %d:%02d", (int)(secs / 60), (int)(secs % 60));
    Ps_Text(28.0f, 264.0f, line, 0.52f, 260.0f, 5);
    Ps_Text(28.0f, 292.0f, "THE MATCH STARTS THE MOMENT", 0.42f, 260.0f, 0);
    Ps_Text(28.0f, 308.0f, "YOUR RIVAL ARRIVES", 0.42f, 260.0f, 0);
}

static void Tb_DrawQuick(void) {
    Tb_Hosts(TBO_HOSTS_MIC_WAVE, 404.0f, 350.0f, 240.0f);
    Tb_DrawRows(2, sTb->qkCur, 110.0f);
    Ps_Panel(TBO_KIT_WINDOW, 14.0f, 196.0f, 300.0f, 330.0f, 0x80);
}

static void Tb_DrawQuickText(void) {
    static char line[64];
    s32 q;

    Ps_Text(26.0f, 76.0f, "PICK YOUR BATTLE", 0.62f, 0.0f, 2);
    Ps_Text(36.0f, 115.0f, "Battle", 0.66f, 110.0f, sTb->qkCur == 0 ? 1 : 0);
    Ps_Align(2);
    Ps_Text(266.0f, 117.0f, sTb->queue ? "TEAMS (UP TO 5)" : "ONE ON ONE", 0.54f, 140.0f, 3);
    Ps_Align(0);
    Ps_Text(36.0f, 149.0f, "Search", 0.66f, 110.0f, sTb->qkCur == 1 ? 1 : 0);
    Ps_Text(28.0f, 206.0f, "WAITING RIGHT NOW", 0.52f, 0.0f, 2);
    for (q = 0; q < 2; q++) {
        s32 n = Port_LobbyQueueWaiting(q);

        Ps_Text(36.0f, 234.0f + 26.0f * (f32)q, q ? "Teams" : "One on One", 0.56f, 120.0f, q == sTb->queue ? 1 : 0);
        if (n < 0) {
            sprintf(line, "...");
        } else {
            sprintf(line, "%d WAITING", (int)n);
        }
        Ps_Align(2);
        Ps_Text(286.0f, 236.0f + 26.0f * (f32)q, line, 0.5f, 120.0f, n > 0 ? 5 : 3);
        Ps_Align(0);
    }
    sprintf(line, "NO TIME LIMIT - %d%% SPEED", (int)Port_NetSpeedStandard());
    Ps_Text(28.0f, 296.0f, line, 0.46f, 260.0f, 3);
}

static void Tb_DrawProfile(void) {
    Tb_Hosts(TBO_HOSTS_THUMBS_UP, 408.0f, 350.0f, 250.0f);
    Tb_DrawRows(PR_COUNT, sTb->prCur, 110.0f);
}

static void Tb_DrawProfileText(void) {
    static char value[PR_COUNT][32];
    static char *label[PR_COUNT] = {"Name"};
    s32 i;

    Ps_Copy(value[PR_NAME], Port_NetNameGet(), 32);
    Ps_Text(26.0f, 76.0f, "PLAYER CARD", 0.62f, 0.0f, 2);
    for (i = 0; i < PR_COUNT; i++) {
        f32 y = 110.0f + 34.0f * (f32)i;

        Ps_Text(36.0f, Ps_TextTop(y + 13.0f, 0.66f), label[i], 0.66f, 110.0f, i == sTb->prCur ? 1 : 0);
        Ps_Align(2);
        Ps_Text(266.0f, Ps_TextTop(y + 13.0f, 0.54f), value[i], 0.54f, 140.0f, 3);
        Ps_Align(0);
    }
}

/* What the hosts say in the box at the foot of the screen (two lines at most). */
static char *Tb_Help(void) {
    switch (sTb->mode) {
    case TB_WII_HOME:
        if (sTb->wiiDialogText[0]) return sTb->wiiDialogText;
        return "Alright Giru! We're gonna go and find fighters we've never seen before!";
    case TB_WII_BATTLE:
        return sTbWiiBattleHelp[sTb->wiiBattleCur];
    case TB_WII_CUSTOM:
        return "Choose a battle format.";
    case TB_WII_FRIEND_MENU:
        return "Host a battle or enter a friend's room code.";
    case TB_WII_FRIEND_FORMAT:
        return "Choose the battle format for your room.";
    case TB_WII_FRIEND_CODE:
        return "Enter the six-character room code.";
    case TB_WII_VS: {
        static char matchHelp[256];
        if (sTb->wiiFriend && !sTb->found) {
            if (sTb->host) {
                const char *code = Port_LobbyCode();
                Ps_Copy(matchHelp, code[0] ? "Room code: " : "Getting a room code...", sizeof(matchHelp));
                if (code[0]) Ps_Cat(matchHelp, code, sizeof(matchHelp));
                return matchHelp;
            }
            return "Connecting to your friend...";
        }
        if (Port_WiiMenuText(sTb->found ? 54 : 53, matchHelp, sizeof(matchHelp))) return matchHelp;
        return sTb->found ? "Opponent found!" : "Searching for opponent...";
    }
    case TB_WII_FRIENDS:
        if (sTb->wiiDialogText[0]) return sTb->wiiDialogText;
        return "";
    case TB_WII_SETTINGS:
        if (sTb->wiiDialogText[0]) return sTb->wiiDialogText;
        return "";
    case TB_WII_NAME:
        return "";
    case TB_WII_RANKING:
        if (sTb->wiiDialogText[0]) return sTb->wiiDialogText;
        return "";
    case TB_HUB:
        return sTbHubSay[sTb->hubCur];
    case TB_CREATE:
        return sTb->crCur == CR_DELAY ? "How late your moves come out. Automatic measures the line when the match starts."
               : sTb->crCur == CR_ROLL ? "Your moves come out at once and the game corrects itself. More frames cope with a slower line."
               : "Set the rules of your tournament! Then your rival joins with the room code.";
    case TB_JOIN:
        return "Enter your rival's room code, or Paste the one they sent you! Then Join.";
    case TB_WAIT:
        return sTb->found ? "Here we go! The match is starting!" : "Looking for a worthy challenger... Warm up in Training meanwhile!";
    case TB_BLOCK:
        return "Sorry! Online fights have to be fair, so only the game straight off the disc can play.";
    case TB_SPAR:
        return "Warm up at the tournament while we search! Pick yourself and a sparring partner.";
    case TB_QUICK:
        return "Pick a battle and we'll find you a rival! No time limit, and the game runs a little faster.";
    case TB_NAME:
        return "Pick your letters, then Done! Square deletes, Start finishes.";
    case TB_PROFILE:
        return "Show everyone who you are! This is the name your rivals see.";
    }
    return "";
}

/* Each page's title (by TB_ mode), as its heading shows it and the screen reader says it. */
static char *sTbTitle[20] = {"Tenkaichi Budokai Online", "Create Room", "Join Room", "Searching", "Profile", "Profile", "Quick Match",
                             "Train While Waiting", "Tenkaichi Budokai Online", "Dragon Net Battle", "Nintendo WFC Battle",
                             "Manage Friends", "User Settings", "View Ranking", "Create Player Name", "Normal Battle", "Normal Battle",
                             "Friend Battle", "Friend Battle", "Friend Battle"};
static void Tb_SetWiiTitles(void) {
    sTbTitle[TB_WII_HOME] = sTbWiiText[TB_WII_TX_HOME_TITLE];
    sTbTitle[TB_WII_BATTLE] = sTbWiiText[TB_WII_TX_BATTLE_TITLE];
    sTbTitle[TB_WII_CUSTOM] = sTbWiiText[sTb->wiiNormalBattle ? TB_WII_TX_NORMAL_TITLE : TB_WII_TX_CUSTOM_TITLE];
    sTbTitle[TB_WII_FRIENDS] = sTbWiiText[TB_WII_TX_FRIENDS_TITLE];
    sTbTitle[TB_WII_SETTINGS] = sTbWiiText[TB_WII_TX_SETTINGS_TITLE];
    sTbTitle[TB_WII_RANKING] = sTbWiiText[TB_WII_TX_RANKING_TITLE];
    sTbTitle[TB_WII_NAME] = sTbWiiText[TB_WII_TX_NAME_TITLE];
    sTbTitle[TB_WII_VS] = sTb->wiiFriend ? sTbWiiText[TB_WII_TX_BATTLE_ROW + 2] : sTbTitle[TB_WII_CUSTOM];
    sTbTitle[TB_WII_FRIEND_MENU] = sTbWiiText[TB_WII_TX_BATTLE_ROW + 2];
    sTbTitle[TB_WII_FRIEND_FORMAT] = sTbTitle[TB_WII_FRIEND_MENU];
    sTbTitle[TB_WII_FRIEND_CODE] = sTbTitle[TB_WII_FRIEND_MENU];
}

/* ---- the screen reader (port/src/gs/a11y.c; silent unless the player has it on). The game's own screens are read
   from their state by port/src/a11y_game.c; these pages say what they show themselves: the page's title as it opens,
   then what the cursor is on (and its value), the code or the name typed so far, and the notes. The hosts' line is
   the box's, which the reader reads as it reads the game's. ---- */
extern void PortA11y_Say(const char *text, int flags);
#define TB_SAY_INTERRUPT 1

/* A room code letter by letter ("J D Y 2 T E"). */
static void Tb_Spell(char *out, const char *code, s32 max) {
    s32 n = 0;
    s32 i;

    for (i = 0; code[i] != 0 && n < max - 3; i++) {
        if (n > 0) {
            out[n++] = ' ';
        }
        out[n++] = code[i];
    }
    out[n] = 0;
}

/* What a keypad's cursor is on: a key's character, or a button's label. */
static void Tb_KeyFocus(char *out, s32 rows, char (*keys)[KEY_COLS + 1], const TbKeyBtn *b, s32 nb) {
    s32 k;

    out[0] = 0;
    if (sTb->keyRow < rows) {
        out[0] = keys[sTb->keyRow][sTb->keyCol];
        out[1] = 0;
        return;
    }
    for (k = 0; k < nb; k++) {
        if (sTb->keyCol >= b[k].c0 && sTb->keyCol <= b[k].c1) {
            Ps_Copy(out, b[k].label, 32);
        }
    }
}

/* Once a frame, after the page's update. */
static void Tb_Speak(void) {
    static s32 mode = -1;
    static s32 disconnectSaid;
    static char said[160];
    static char typed[40];
    static s32 noted;
    char focus[160];
    char part[64];
    char now[40];

    if (sTb->disconnectReason) {
        if (!disconnectSaid) {
            PortA11y_Say(sTb->disconnectLine, TB_SAY_INTERRUPT);
            disconnectSaid = 1;
        }
        return;
    }
    disconnectSaid = 0;

    if (sTb->frames == 0) { /* (the screens opened again) */
        mode = -1;
        typed[0] = 0;
    }
    focus[0] = 0;
    now[0] = 0;
    switch (sTb->mode) {
    case TB_WII_HOME:
        Ps_Copy(focus, sTbWiiHomeName[sTb->wiiCur], 160);
        break;
    case TB_WII_BATTLE:
        Ps_Copy(focus, sTbWiiBattleName[sTb->wiiBattleCur], 160);
        break;
    case TB_WII_CUSTOM:
        Ps_Copy(focus, sTbWiiCustomName[sTb->wiiCustomCur + (sTb->wiiNormalBattle ? 2 : 0)], 160);
        break;
    case TB_WII_FRIEND_MENU:
        Ps_Copy(focus, sTbWiiFriendAction[sTb->wiiSubCur], 160);
        break;
    case TB_WII_FRIEND_FORMAT:
        Ps_Copy(focus, sTbWiiCustomName[sTb->wiiCustomCur], 160);
        break;
    case TB_WII_VS:
        if (sTb->wiiFriend && sTb->host && !sTb->found) Ps_Copy(focus, Port_LobbyCode(), 160);
        break;
    case TB_WII_FRIENDS:
        Ps_Copy(focus, sTbWiiFriendsName[sTb->wiiSubCur], 160);
        break;
    case TB_WII_SETTINGS:
        Ps_Copy(focus, sTbWiiSettingsName[sTb->wiiSubCur], 160);
        break;
    case TB_WII_RANKING:
        Ps_Copy(focus, sTbWiiRankingName[sTb->wiiSubCur], 160);
        break;
    case TB_HUB:
        Ps_Copy(focus, sTbHubName[sTb->hubCur], 160);
        if (!sTbHubReady[sTb->hubCur]) {
            Ps_Cat(focus, ", soon", 160);
        }
        break;
    case TB_CREATE: {
        static char *label[CR_COUNT] = {"Battle", "Time Limit", "Input Delay", "Rollback", "Open Room"};

        Ps_Copy(focus, label[sTb->crCur], 160);
        part[0] = 0;
        if (sTb->crCur == CR_BATTLE) {
            Ps_Copy(part, sTb->battle ? "Teams, up to 5" : "One on one", 64);
        } else if (sTb->crCur == CR_TIME) {
            Ps_Copy(part, sTbTime[sTb->time], 64);
        } else if (sTb->crCur == CR_DELAY) {
            Tb_DelayText(part, sTb->delay);
        } else if (sTb->crCur == CR_ROLL) {
            Tb_RollText(part, sTb->roll);
        }
        if (part[0] != 0) {
            Ps_Cat(focus, ": ", 160);
            Ps_Cat(focus, part, 160);
        }
        break;
    }
    case TB_JOIN:
        Tb_KeyFocus(focus, KEY_ROWS, sTbKeys, sTbJoinBtn, 3);
        Ps_Copy(now, sTb->code, 40);
        break;
    case TB_NAME:
        Tb_KeyFocus(focus, NAME_ROWS, sTbNameKeys[sTb->lower], sTbNameBtn, 4);
        Ps_Copy(now, sTb->typed, 40);
        break;
    case TB_WII_NAME:
    case TB_WII_FRIEND_CODE:
        if (sTb->keyRow == WII_KEY_ROWS)
            Ps_Copy(focus, sTb->keyCol == 0 ? "Quit" : sTb->keyCol == 4 ? "Change case" : "Decide", 160);
        else if ((sTb->keyRow == 1 || sTb->keyRow == 2) && sTb->keyCol >= 12)
            Ps_Copy(focus, sTb->keyCol == 12 ? "Move text cursor left" : "Move text cursor right", 160);
        else if (sTb->keyRow == 3 && sTb->keyCol >= 12)
            Ps_Copy(focus, "Delete", 160);
        else {
            focus[0] = Port_DcNameKey(sTb->lower, sTb->keyRow, sTb->keyCol);
            focus[1] = 0;
        }
        Ps_Copy(now, sTb->typed, 40);
        break;
    case TB_WAIT:
        if (sTb->found) {
            Ps_Copy(focus, "Your rival is here! The match begins.", 160);
        } else if (sTb->host == 2) {
            Ps_Copy(focus, sTb->queue ? "Quick match, teams: searching" : "Quick match, one on one: searching", 160);
        } else if (sTb->host) {
            if (Port_LobbyCode()[0] != 0) {
                Ps_Copy(focus, "Your room code: ", 160);
                Tb_Spell(part, Port_LobbyCode(), 64);
                Ps_Cat(focus, part, 160);
            } else {
                Ps_Copy(focus, "Getting a room code", 160);
            }
        } else {
            Ps_Copy(focus, "Joining room ", 160);
            Tb_Spell(part, sTb->code, 64);
            Ps_Cat(focus, part, 160);
        }
        break;
    case TB_QUICK:
        if (sTb->qkCur == 0) {
            Ps_Copy(focus, sTb->queue ? "Battle: teams" : "Battle: one on one", 160);
        } else {
            Ps_Copy(focus, "Search", 160);
        }
        break;
    case TB_SPAR:
        if (sTb->spCur == 0) {
            Ps_Copy(focus, "You: ", 160);
            Ps_Cat(focus, Tb_Fighter(sTb->spMe), 160);
        } else if (sTb->spCur == 1) {
            Ps_Copy(focus, "Partner: ", 160);
            Ps_Cat(focus, Tb_Fighter(sTb->spFoe), 160);
        } else {
            Ps_Copy(focus, "Start training", 160);
        }
        break;
    case TB_BLOCK:
        Ps_Copy(focus, "Cannot go online. Online matches are only between two unmodded games.", 160);
        break;
    default:
        Ps_Copy(focus, "Name: ", 160);
        Ps_Cat(focus, (char *)Port_NetNameGet(), 160);
        break;
    }
    if (sTb->mode != mode) {
        static char line[224];

        mode = sTb->mode;
        Ps_Copy(line, sTbTitle[mode], 224);
        Ps_Cat(line, ". ", 224);
        Ps_Cat(line, focus, 224);
        PortA11y_Say(line, TB_SAY_INTERRUPT);
        Ps_Copy(said, focus, 160);
        Ps_Copy(typed, now, 40);
    } else { /* what was typed, then where the cursor went (one line: two in one blank, the second cuts the first) */
        static char line[256];
        s32 k;

        line[0] = 0;
        for (k = 0; now[k] == typed[k] && now[k] != 0; k++) {
        }
        if (now[k] != typed[k]) { /* the code or the name typed so far */
            Ps_Copy(typed, now, 40);
            Ps_Copy(line, sTb->mode == TB_JOIN ? "Code: " : "Name: ", 256);
            if (sTb->mode == TB_JOIN) {
                Tb_Spell(part, now, 64);
                Ps_Cat(line, now[0] != 0 ? part : "empty", 256);
            } else {
                Ps_Cat(line, now[0] != 0 ? now : "empty", 256);
            }
        }
        for (k = 0; focus[k] == said[k] && focus[k] != 0; k++) {
        }
        if (focus[k] != said[k]) {
            Ps_Copy(said, focus, 160);
            if (line[0] != 0) {
                Ps_Cat(line, ". ", 256);
            }
            Ps_Cat(line, focus, 256);
        }
        if (line[0] != 0) {
            PortA11y_Say(line, TB_SAY_INTERRUPT);
        }
    }
    if (sTb->note > 0 && !noted) { /* a note (copied, a code that did not work ...) */
        PortA11y_Say(sTb->noteLine, 0);
    }
    noted = sTb->note > 0;
}

/* The hosts' line (Tb_Help) for the game's box, in the game's text: 16-bit, 0xFEFF first, cut at spaces into lines of
   what fits (three at most) up to the step of the box's top right corner (at about 410 of the screen's 512: the box
   is the game's movie, over the whole screen). Another buffer for another line (the screen reader tells them apart by
   that). NULL: no line. */
#define TB_BOX_LINE 224
static u16 *Tb_BoxLine(void) {
    static u16 buf[2][TB_BOX_LINE];
    static char said[TB_BOX_LINE];
    static s32 cur = -1;
    char *say = Tb_Help();
    u16 *out;
    s32 room;
    s32 n;
    s32 start;
    s32 lines;
    s32 at;
    s32 k;

    if (say == NULL || say[0] == 0) {
        return NULL;
    }
    for (k = 0; cur >= 0 && say[k] == said[k] && say[k] != 0; k++) {
    }
    if (cur >= 0 && say[k] == said[k]) {
        return buf[cur];
    }
    Ps_Copy(said, say, TB_BOX_LINE);
    cur = cur == 0 ? 1 : 0;
    out = buf[cur];
    room = MsgWin_PortLineRoom(400);
    out[0] = 0xFEFF;
    n = 1;
    start = 1;
    lines = 1;
    at = 0;
    while (say[at] == ' ') {
        at++;
    }
    while (say[at] != 0 && n < TB_BOX_LINE - 2) {
        s32 end = at;
        s32 mark = n;

        while (say[end] != 0 && say[end] != ' ') {
            end++;
        }
        if (n > start) {
            out[n++] = ' ';
        }
        for (k = at; k < end && n < TB_BOX_LINE - 1;) {
            u8 ch = (u8)say[k++];
            if (ch >= 0xC2 && ch <= 0xDF && k < end && ((u8)say[k] & 0xC0) == 0x80) {
                out[n++] = (u16)(((ch & 0x1F) << 6) | ((u8)say[k++] & 0x3F));
            } else if (ch >= 0xE0 && ch <= 0xEF && k + 1 < end &&
                       ((u8)say[k] & 0xC0) == 0x80 && ((u8)say[k + 1] & 0xC0) == 0x80) {
                out[n++] = (u16)(((ch & 0x0F) << 12) | (((u8)say[k] & 0x3F) << 6) | ((u8)say[k + 1] & 0x3F));
                k += 2;
            } else {
                out[n++] = ch < 0x80 ? ch : '?';
            }
        }
        out[n] = 0;
        if (mark > start && lines < 3 && Font_GetWidth(out + start) > room) { /* the word goes to the next line */
            out[mark] = '\n';
            start = mark + 1;
            lines++;
        }
        at = end;
        while (say[at] == ' ') {
            at++;
        }
    }
    out[n] = 0;
    return out;
}

/* What a page draws comes in from the left as it opens, as the hub's bars do (the hub moves its own: Tb_DrawHub);
   the title, the player card and the box stay where they are. The name's keypad comes in over the profile, which
   stays (on: 2 for the keypad). */
static void Tb_PageIn(s32 on) {
    s32 page = on == 2 || (on == 1 && sTb->mode != TB_HUB && sTb->mode != TB_BLOCK && sTb->mode != TB_NAME);

    Ps_SetShift(page ? (1.0f - Tb_In(0)) * -160.0f : 0.0f);
}

static void Tb_Draw(void) {
    Tb_PageIn(1);
    switch (sTb->mode) {
    case TB_WII_HOME:
    case TB_WII_BATTLE:
    case TB_WII_CUSTOM:
    case TB_WII_FRIEND_MENU:
    case TB_WII_FRIEND_FORMAT:
    case TB_WII_FRIENDS:
    case TB_WII_SETTINGS:
    case TB_WII_RANKING:
        Tb_DrawWii();
        break;
    case TB_WII_VS:
        if (Port_WiiArtAvailable()) Tb_DrawWiiVs();
        break;
    case TB_WII_NAME:
    case TB_WII_FRIEND_CODE:
        Tb_DrawWiiName();
        break;
    case TB_HUB:
        Tb_DrawHub();
        break;
    case TB_CREATE:
        Tb_DrawCreate();
        break;
    case TB_JOIN:
        Tb_DrawJoin();
        break;
    case TB_WAIT:
        Tb_DrawWait();
        break;
    case TB_QUICK:
        Tb_DrawQuick();
        break;
    case TB_SPAR:
        Tb_DrawSpar();
        break;
    case TB_BLOCK:
        break; /* (drawn last, over the rest) */
    default:
        Tb_DrawProfile();
        break;
    }
    Tb_PageIn(0);
    if (sTb->mode < TB_WII_HOME) /* the PC online pages' player card */
        Ps_Panel(TBO_KIT_WINDOW, 300.0f, 10.0f, 504.0f, 44.0f, 0x80);
    if (sTb->mode != TB_WII_NAME && sTb->mode != TB_WII_FRIEND_CODE)
        MsgWin_PortDrawString(Tb_BoxLine());
    if (sTb->mode == TB_BLOCK) {
        Tb_DrawBlock();
    }
}

static void Tb_DrawText(void) {
    char **title = sTbTitle;

    /* its capitals centred on the crest beside it (y 32); up to the player card */
    if (!(sTb->mode == TB_WII_NAME || sTb->mode == TB_WII_FRIEND_CODE ||
          ((sTb->mode == TB_WII_SETTINGS || sTb->mode == TB_WII_BATTLE ||
            sTb->mode == TB_WII_CUSTOM || sTb->mode == TB_WII_FRIEND_MENU ||
            sTb->mode == TB_WII_FRIEND_FORMAT || sTb->mode == TB_WII_FRIEND_CODE ||
            sTb->mode == TB_WII_VS) && Port_WiiArtAvailable())))
        Ps_Heading(Tb_TitleLeft() + 44.0f, Ps_TextTop(32.0f, 1.3f), title[sTb->mode], 1.3f, 248.0f, 0x80);
    Tb_PageIn(1);
    switch (sTb->mode) {
    case TB_WII_HOME:
    case TB_WII_BATTLE:
    case TB_WII_CUSTOM:
    case TB_WII_FRIEND_MENU:
    case TB_WII_FRIEND_FORMAT:
    case TB_WII_FRIENDS:
    case TB_WII_SETTINGS:
    case TB_WII_RANKING:
        Tb_DrawWiiText();
        break;
    case TB_WII_NAME:
    case TB_WII_FRIEND_CODE:
        {
            f32 rise = sTb->modeT < 16 ? (f32)(16 - sTb->modeT) * 18.0f : 0.0f;
            if (sTb->mode == TB_WII_NAME)
                Ps_SpriteScreen(TBO_WII_NAME_TITLE, 0.0f, 46.0f + rise, 512.0f, 110.0f + rise,
                                0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
            else {
                s32 id = PortTbo_TextId(sTbWiiText[TB_WII_TX_BATTLE_ROW + 2], 5);
                f32 width = id >= 0 ? 21.0f * PortTbo_TextInfo(id, 0) / 1000.0f : 0.0f;
                /* The heading glyphs begin nine pixels into its 512-pixel sheet. */
                Tb_WiiTitleText(TB_WII_TX_BATTLE_ROW + 2, 256.0f - width * 0.5f - 9.0f,
                                52.0f + rise, 1.0f, 1.0f, 0x80);
            }
        }
        break;
    case TB_WII_VS:
        if (Port_WiiArtAvailable()) {
            MsgWin_PortDrawNativeName(Port_NetNameGet(), 42, 70, 470, 111);
            if (sTb->found && Port_LobbyPeerName()[0] != 0)
                MsgWin_PortDrawNativeName(Port_LobbyPeerName(), 42, 210, 470, 251);
        }
        break;
    case TB_HUB:
        Tb_DrawHubText();
        break;
    case TB_CREATE:
        Tb_DrawCreateText();
        break;
    case TB_JOIN:
        Tb_DrawJoinText();
        break;
    case TB_WAIT:
        Tb_DrawWaitText();
        break;
    case TB_QUICK:
        Tb_DrawQuickText();
        break;
    case TB_SPAR:
        Tb_DrawSparText();
        break;
    case TB_BLOCK:
        break;
    default:
        Tb_DrawProfileText();
        break;
    }
    Tb_PageIn(0);
    /* (no status and no title under it: "ONLINE" was not whether the game is connected, and the title is the
       screen's heading) */
    if (sTb->mode < TB_WII_HOME)
        Ps_Text(322.0f, Ps_TextTop(27.0f, 0.6f), (char *)Port_NetNameGet(), 0.6f, 166.0f, 1);
    if (sTb->mode == TB_BLOCK) {
        Tb_DrawBlockText();
    }
    if (sTb->mode == TB_NAME) {
        Tb_PageIn(2);
        Tb_DrawName();
        Tb_PageIn(0);
    }
    if (sTb->note > 0 && sTb->mode != TB_WII_NAME && sTb->mode != TB_WII_SETTINGS) {
        s32 a = sTb->note > 16 ? 0x80 : sTb->note * 8;

        Ps_Align(1);
        Ps_TextEx(256.0f, 330.0f, sTb->noteLine, 0.52f, 480.0f, Ps_Rgba(0xFF, 0xF8, 0xB0, a), Ps_Rgba(0xFF, 0xC8, 0x30, a), Ps_Rgba(0x5C, 0x24, 0x08, a));
        Ps_Align(0);
    }
}

/* The memory-card window is also used for the disconnect message. Keep its Dialog separate from the game's
   global instance, as the PC quit prompt does. Its text table must live in game-addressable memory. */
static Dialog *sTbDisconnectDialog;
static void *sTbDisconnectArchive;
static void *sTbDisconnectResource;
static struct {
    u32 offsets[2];
    u16 text[128];
} sTbDisconnectMessage;

static void Tb_DisconnectText(void) {
    const u8 *src = (const u8 *)sTb->disconnectLine;
    s32 used = 0;

    sTbDisconnectMessage.offsets[0] = 0;
    sTbDisconnectMessage.offsets[1] = sizeof(sTbDisconnectMessage.offsets);
    while (*src && used < 126) {
        u32 c = *src++;
        if (c >= 0xC2 && c <= 0xDF && (src[0] & 0xC0) == 0x80) {
            c = ((c & 0x1F) << 6) | (*src++ & 0x3F);
        } else if (c >= 0xE0 && c <= 0xEF && src[0] && src[1] &&
                   (src[0] & 0xC0) == 0x80 && (src[1] & 0xC0) == 0x80) {
            c = ((c & 0x0F) << 12) | ((src[0] & 0x3F) << 6) | (src[1] & 0x3F);
            src += 2;
        } else if (c >= 0x80) {
            c = '?';
        }
        if (c >= 0xD800 && c <= 0xDFFF) c = '?';
        sTbDisconnectMessage.text[used++] = (u16)c;
    }
    sTbDisconnectMessage.text[used] = 0;
}

static void Tb_DisconnectDialogInit(void) {
    Dialog *previous;

    if (sTbDisconnectDialog || sTbDisconnectArchive || !gProgress || !File_IsPartitionLoaded(1)) return;
    sTbDisconnectArchive = File_LoadSync(gProgress->baseFile + 0x19, NULL, 0);
    if (!sTbDisconnectArchive) return;
    sTbDisconnectResource = Sprite_Unpack(MPACK_AT(sTbDisconnectArchive, 2), NULL, NULL);
    if (!sTbDisconnectResource) {
        Heap_Free(sTbDisconnectArchive);
        sTbDisconnectArchive = NULL;
        return;
    }
    Tb_DisconnectText();
    previous = gDialog;
    Dialog_Init(MPACK_AT(sTbDisconnectResource, 1), sTbDisconnectMessage.offsets, 0);
    sTbDisconnectDialog = gDialog;
    if (sTbDisconnectDialog) {
        Dialog_SetLayout(0);
        Dialog_SetMsg(0);
        Dialog_SetChoices(0);
        Dialog_Start(DIALOG_CMD_OPEN);
    }
    gDialog = previous;
}

static s32 Tb_DisconnectDialogStartClose(void) {
    Dialog *previous;

    if (!sTbDisconnectDialog) return 0;
    previous = gDialog;
    gDialog = sTbDisconnectDialog;
    Dialog_Start(DIALOG_CMD_CLOSE);
    gDialog = previous;
    return 1;
}

static s32 Tb_DisconnectDialogIsOpen(void) {
    return sTbDisconnectDialog && (sTbDisconnectDialog->flash[0].flags & 2);
}

static s32 Tb_DisconnectDialogIsClosed(void) {
    return sTbDisconnectDialog && (sTbDisconnectDialog->flags & DIALOG_FLAG_CLOSED);
}

static void Tb_DisconnectDialogTerm(void) {
    Dialog *previous = gDialog;
    if (sTbDisconnectDialog) {
        gDialog = sTbDisconnectDialog;
        Dialog_Term();
        gDialog = previous;
        sTbDisconnectDialog = NULL;
    }
    if (sTbDisconnectResource) {
        Heap_Free(sTbDisconnectResource);
        sTbDisconnectResource = NULL;
    }
    if (sTbDisconnectArchive) {
        Heap_Free(sTbDisconnectArchive);
        sTbDisconnectArchive = NULL;
    }
}

/* Shown after the saved menu state is restored from an interrupted match. */
static void Tb_DrawDisconnect(void) {
    Dialog *previous;
    f32 bob;
    f32 drop;
    s32 eyeAlpha;
    Tb_DisconnectDialogInit();
    if (!sTbDisconnectDialog) return;
    Ps_Rect(0.0f, 0.0f, 512.0f, 448.0f, Ps_Rgba(0, 0, 0, 0x50), Ps_Rgba(0, 0, 0, 0x50));
    previous = gDialog;
    gDialog = sTbDisconnectDialog;
    Dialog_Draw(1);
    sTbDisconnectDialog = gDialog;
    gDialog = previous;
    bob = Tb_WiiIdleBob((sTb->frames * 3) % 2000, 4.0f);
    if (sTb->disconnectClosing) {
        drop = (f32)(sTb->disconnectCloseFrames++ * 6);
    } else {
        f32 enter = (f32)sTb->disconnectOpenFrames / 24.0f;
        if (enter > 1.0f) enter = 1.0f;
        /* Ease up from below the viewport; the eye shares the body's offset. */
        drop = 180.0f * (1.0f - enter) * (1.0f - enter) * (1.0f - enter);
        if (sTb->disconnectOpenFrames < 24) sTb->disconnectOpenFrames++;
    }
    Ps_SpriteScreen(TBO_WII_DISCONNECT_GIRU, 192.0f, 274.0f + bob + drop, 320.0f, 424.0f + bob + drop,
                    0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
    eyeAlpha = (s32)(56.0f * (1.0f - cosf((f32)(sTb->frames % 120) * (6.283185307f / 120.0f))));
    /* The glow occupies texels (1,1)..(20,21) in its 32x32 sheet; keep its round alpha edge. */
    Ps_SpriteScreen(TBO_WII_DISCONNECT_GIRU_EYE, 249.0f, 328.0f + bob + drop, 268.0f, 348.0f + bob + drop,
                    1.0f / 32.0f, 1.0f / 32.0f, 20.0f / 32.0f, 21.0f / 32.0f, PS_PLAIN, eyeAlpha);
}

/* ----------------------------------------------------------------------------------------------- the frame loop */
/* From the main menu's item until triangle on the hub. Returns 1 when it was left for "Train While Waiting" (the
   room is still open; Duel_Main goes on to the Ultimate Training menu), 0 back to the main menu. */
/* The Duel menu's screen unpacked (the windows' data), while the screens run. */
static void *sTbWinRes;

static void Tb_WinInit(void) {
    void *icons;

    sTbWinRes = Sprite_Unpack(MPACK_AT(gMenuArc5, 1), NULL, NULL); /* (Duel_Main: DuelMenu_Run(1)) */
    icons = MPACK_AT(sTbWinRes, 24);
    Res_RelocateOffsets(&icons, icons, icons);
    IconWin_Init(MPACK_AT(sTbWinRes, 23), icons);
    IconWin_PortHideWords(); /* "Versus Menu" and its emblem: the screens' own title is written there */
    IconWin_Open();
    MsgWin_Init(MPACK_AT(sTbWinRes, 22), MPACK_AT(sTbWinRes, 21), sTb->wii ? 0 : 1, 0);
    MsgWin_SetText(NULL); /* the box only: the hosts' lines are the screens' own (Tb_BoxLine) */
    MsgWin_PortSameTop();
    MsgWin_Open();
}

static void Tb_WinTerm(void) {
    if (sTbWinRes == NULL) {
        return;
    }
    MsgWin_Term();
    IconWin_Term();
    Heap_Free(sTbWinRes);
    sTbWinRes = NULL;
}

s32 PortTbo_Run(void) {
    s32 train;

    sTb = Heap_Alloc(sizeof(TboScreen), 0x20, 0, 2);
    memset(sTb, 0, sizeof(TboScreen));
    sTb->wii = gPortTboStartWii;
    Tb_LoadWiiText();
    Tb_SetWiiTitles();
    sTb->wiiPrevCur = -1;
    sTb->wiiSelectT = sTb->wii ? 0 : 4;
    gPortTboStartWii = 0;
    if (sTb->wii) {
        sTb->mode = TB_WII_HOME;
        Port_WiiDialogText(0, sTb->wiiDialogText, sizeof(sTb->wiiDialogText));
    }
    sTb->hubCur = HUB_CREATE;
    sTb->hubY = Tb_HubRowY(HUB_CREATE);
    Tb_LoadChoices();
    Port_NetNameGet();
    gPortTboSpar = 0;
    if (Port_LobbyInBackground()) { /* back with a room open (from the main menu after training): its search again */
        Port_LobbyBackground(0);
        sTb->host = sTbBgKind;
        sTb->queue = sTbBgQueue;
        Ps_Copy(sTb->code, sTbBgCode, CODE_LEN + 1);
        sTb->waitBack = Tb_Home();
        Tb_SetMode(TB_WAIT);
    }
    Port_EscQuit(0);
    Tb_WinInit();
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        s32 live;

        Job_Run();
        Gfx_BeginFrame();
        Pad_Update();
        if (Port_EscTake() && !sTb->typing) {
            gPad[0].gamePressed |= PADG_TRIANGLE;
        }
        Snd_Update();
        ColorFade_Update();
        if (sTb->wiiNextMode && ColorFade_IsOutDone()) {
            s32 next = sTb->wiiNextMode;
            sTb->wiiNextMode = 0;
            Tb_SetMode(next);
            if (next == TB_WII_VS && (!sTb->wiiFriend || sTb->host != 0)) Tb_WiiCustomSearchStart();
            ColorFade_StartIn(0, 0, 0, 0x14);
        }
        live = !sTb->leaving && ColorFade_IsInDone();
        Ps_PromptsClear();
        if (sTb->disconnectReason) Tb_WiiUpdate(live);
        else switch (sTb->mode) {
        case TB_WII_HOME:
        case TB_WII_BATTLE:
        case TB_WII_CUSTOM:
        case TB_WII_FRIEND_MENU:
        case TB_WII_FRIEND_FORMAT:
        case TB_WII_FRIENDS:
        case TB_WII_SETTINGS:
        case TB_WII_RANKING:
        case TB_WII_VS:
            Tb_WiiUpdate(live);
            break;
        case TB_WII_NAME:
            Tb_WiiNameUpdate(live);
            break;
        case TB_WII_FRIEND_CODE:
            Tb_WiiNameUpdate(live);
            break;
        case TB_HUB:
            Tb_HubUpdate(live);
            break;
        case TB_CREATE:
            Tb_CreateUpdate(live);
            break;
        case TB_JOIN:
            Tb_JoinUpdate(live);
            break;
        case TB_WAIT:
            Tb_WaitUpdate(live);
            break;
        case TB_PROFILE:
            Tb_ProfileUpdate(live);
            break;
        case TB_NAME:
            Tb_NameUpdate(live);
            break;
        case TB_QUICK:
            Tb_QuickUpdate(live);
            break;
        case TB_SPAR:
            Tb_SparUpdate(live);
            break;
        case TB_BLOCK:
            Tb_BlockUpdate(live);
            break;
        }
        Tb_Speak();
        Ps_PageSet();
        Tb_Backdrop();
        if (sTb->mode == TB_WII_HOME && Port_WiiArtAvailable()) {
            IconWin_Draw(); /* the original corner scrollwork and title underline */
            Tb_DrawWiiHomeArt();
        } else {
            if (sTb->mode != TB_WII_NAME && sTb->mode != TB_WII_FRIEND_CODE) IconWin_Draw();
            Tb_Draw();
            Tb_DrawText();
            if (sTb->disconnectReason) Tb_DrawDisconnect();
            if (!sTb->disconnectReason && sTb->mode != TB_WII_NAME &&
                sTb->mode != TB_WII_FRIEND_CODE && sTb->mode != TB_WII_SETTINGS &&
                sTb->mode != TB_WII_CUSTOM && sTb->mode != TB_WII_FRIEND_MENU &&
                sTb->mode != TB_WII_FRIEND_FORMAT)
                Ps_PromptsDraw();
        }
        ColorFade_Draw();
        File_Stub264D90();
        Gfx_EndFrame(1);
        Dma_Flush();
        sTb->frames++;
        sTb->modeT++;
        if (sTb->mode == TB_WII_HOME && live && sTb->wiiHomeT < 40) sTb->wiiHomeT++;
        if ((sTb->mode == TB_WII_BATTLE || sTb->mode == TB_WII_CUSTOM ||
             sTb->mode == TB_WII_FRIEND_MENU || sTb->mode == TB_WII_FRIEND_FORMAT ||
             sTb->mode == TB_WII_FRIEND_CODE || sTb->mode == TB_WII_VS) && live) sTb->wiiBattleT++;
        if (sTb->wiiDialogStarted && live) sTb->wiiDialogT++;
        if (sTb->wiiSelectT < 4 &&
            (!Port_WiiArtAvailable() ||
             (sTb->mode != TB_WII_HOME || sTb->wiiHomeT >= 23) &&
             ((sTb->mode != TB_WII_BATTLE && sTb->mode != TB_WII_CUSTOM &&
               sTb->mode != TB_WII_FRIEND_MENU && sTb->mode != TB_WII_FRIEND_FORMAT) || sTb->wiiBattleT >= 30)))
            sTb->wiiSelectT++;
        if (sTb->wiiSelectT >= 4 && live && sTb->mode >= TB_WII_HOME)
            sTb->wiiFocusFrame++;
        if (sTb->note > 0) {
            sTb->note--;
        }
        if (sTb->leaving && ColorFade_IsOutDone()) {
            break;
        }
    }
    sceGsSyncPath(0, 0);
    Tb_DisconnectDialogTerm();
    if (sTb->wiiDialogStarted) Voice_Stop(0);
    Port_DcNameClose();
    Port_EscQuit(1);
    Port_TextMode(0);
    Tb_WinTerm();
    if (!sTb->train && (sTb->mode == TB_WAIT || sTb->mode == TB_SPAR)) {
        Port_LobbyCancel();
    }
    train = sTb->train;
    if (train) {
        static u8 noItems[0x10]; /* an empty set of Z items */
        s32 me = sTb->spMe;
        s32 foe = sTb->spFoe;

        Heap_Free(sTb);
        sTb = NULL;
        Dma_ResetBuffers();
        Battle_ClearWork();
        BattleSetup_SetRule(0, 6, 8, 0, 1, TB_TOURNAMENT_STAGE, 0); /* training: no time limit, a partner who stands */
        BattleSetup_SetSide(0, 0, 0, 1, 1, 1, 0, NULL);
        BattleSetup_SetSide(1, 2, 1, 1, 1, 1, 0, NULL);
        BattleSetup_SetMember(0, 0, me, 0, 0, -1, 100.0f, noItems);
        BattleSetup_SetMember(1, 0, foe, 0, 0, -1, 100.0f, noItems);
        BattleSetup_Finish();
        gPortTboSpar = 1;
        return 1;
    }
    Heap_Free(sTb);
    sTb = NULL;
    Dma_ResetBuffers();
    return train;
}
/* The sign over the sparring fight (battle/hud_a.c, Hud_Draw, every frame): how long it has searched, what for, and
   how to go back. Nothing when the fight is not Train While Waiting's or the search is over. */
static s32 sTbSignFrames;

void PortTbo_SparSign(void) {
    static char line[64];
    s32 secs;

    if (!gPortTboSpar || !Port_LobbyInBackground()) {
        return;
    }
    sTbSignFrames++;
    secs = (sTbBgFrames + sTbSignFrames) / 60;
    Ps_PageSet();
    Ps_Panel(TBO_KIT_DIALOG, 128.0f, 66.0f, 384.0f, 124.0f, 0x70); /* (as much room under the lines as over them) */
    sprintf(line, "Searching for a rival...  %d:%02d", (int)(secs / 60), (int)(secs % 60));
    Ps_Msg(146.0f, 72.0f, line, 1.0f, 0x80);
    if (sTbBgKind == 2) {
        Ps_Copy(line, "Quick Match: ", 64);
        Ps_Cat(line, sTbBgQueue ? "Teams" : "One on One", 64);
    } else if (sTbBgKind == 1) {
        Ps_Copy(line, "Your room: ", 64);
        Ps_Cat(line, Port_LobbyCode(), 64);
    } else {
        Ps_Copy(line, "Joining room ", 64);
        Ps_Cat(line, sTbBgCode, 64);
    }
    Ps_Msg(146.0f, 93.0f, line, 1.0f, 0x80);
}
#endif

#include "battle/battle.h"
#include "sys/ramp.h"
extern int Port_NetMe(void);
extern int Port_NetBotSide(void);
extern const char *Port_NetName(int side);
extern s32 BattleResult_HasWinner(void);
extern s32 BattleResult_GetWinnerSide(void);
extern void BattleResult_Set(s32 flags, s32 reason);
typedef struct OnlineResult {
    s32 active, frames, winner, botDecided, rematchWait;
    s32 cursor[2], requested[2], ended[2];
} OnlineResult;
static OnlineResult *sOnlineResult;

static void OnlineResult_Update(void) {
    s32 side;
    if (!sOnlineResult->active) {
        sOnlineResult->active = 1;
        sOnlineResult->winner = BattleResult_HasWinner() ? BattleResult_GetWinnerSide() : -1;
    }
    if (sOnlineResult->rematchWait > 0) {
        if (--sOnlineResult->rematchWait == 0)
            BattleResult_Set(BTL_RESULT_ABORT, 0x10); /* stock return to character/team selection */
        return;
    }
    if (++sOnlineResult->frames < 20) return; /* ignore buttons carried from the victory sequence */
    for (side = 0; side < 2; side++) {
        u32 keys = gPad[side].pressed;
        s32 previousCursor = sOnlineResult->cursor[side];
        if (sOnlineResult->ended[side]) continue;
        if (side == Port_NetBotSide()) {
            keys = 0;
            /* This menu runs at 60 Hz (Gfx_EndFrame(1)), including during rollback. */
            if (sOnlineResult->frames >= 5 * 60) {
                if (sOnlineResult->ended[!side]) {
                    keys = PAD_RIGHT | PAD_CROSS;
                } else if (!sOnlineResult->botDecided) {
                    sOnlineResult->botDecided = 1;
                    keys = ((sOnlineResult->winner >= 0 && sOnlineResult->winner != side) ||
                            Rand_Range(10) < 8 ? PAD_LEFT : PAD_RIGHT) | PAD_CROSS;
                }
            }
        }
        if (!(sOnlineResult->ended[0] || sOnlineResult->ended[1]) &&
            (keys & (PAD_LEFT | PAD_LSTICK_LEFT))) sOnlineResult->cursor[side] = 0;
        if (keys & (PAD_RIGHT | PAD_LSTICK_RIGHT)) sOnlineResult->cursor[side] = 1;
        if (side == Port_NetMe() && previousCursor != sOnlineResult->cursor[side])
            Snd_PlaySe(1, 0);
        if (keys & PAD_CROSS) {
            if (sOnlineResult->cursor[side]) {
                sOnlineResult->ended[side] = 1;
                if (side == Port_NetMe()) Snd_PlaySe(1, 1);
            } else if (!(sOnlineResult->ended[0] || sOnlineResult->ended[1]) &&
                       !sOnlineResult->requested[side]) {
                sOnlineResult->requested[side] = 1;
                if (side == Port_NetMe()) Snd_PlaySe(1, 1);
            }
        }
    }
    /* A refusal cancels every rematch request and leaves each player time to acknowledge it. */
    if (sOnlineResult->ended[0] || sOnlineResult->ended[1]) {
        sOnlineResult->requested[0] = sOnlineResult->requested[1] = 0;
        sOnlineResult->cursor[0] = sOnlineResult->cursor[1] = 1;
        if (sOnlineResult->ended[0] && sOnlineResult->ended[1])
            BattleResult_Set(BTL_RESULT_ABORT, 0x20);
    } else if (sOnlineResult->requested[0] && sOnlineResult->requested[1])
        sOnlineResult->rematchWait = 3 * 60;
}


/* Scale each badge around its own center so the lettering and backing move together. */
static void Tb_ResultPulseSprite(s32 id, f32 x0, f32 y0, f32 x1, f32 y1,
                                 f32 u0, f32 v0, f32 u1, f32 v1, f32 scale) {
    f32 cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    f32 hw = (x1 - x0) * 0.5f * scale, hh = (y1 - y0) * 0.5f * scale;
    Ps_SpriteScreen(id, cx - hw, cy - hh, cx + hw, cy + hh,
                    u0, v0, u1, v1, PS_PLAIN, 128);
}

/* This is a menu phase: no battle objects, HUD, scene updates, or battle drawing remain alive. */
s32 PortTbo_ResultRun(s32 battles, s32 wins0, s32 wins1) {
    s32 ownedArc = gMenuArc5 == NULL;
    s32 decision = 0, closing = 0;
    char labels[2][128];
    s32 edited[2];
    s32 style = Port_NetBattleMenuStyle(-1);
    extern s32 gPortMenuMode;
    gPortMenuMode = 1;
    if (ownedArc) gMenuArc5 = File_LoadSync(gProgress->baseFile, NULL, 0);
    sTb = Heap_Alloc(sizeof(*sTb), 0x20, 0, 2);
    memset(sTb, 0, sizeof(*sTb));
    sTb->wii = 1;
    sTb->mode = TB_WII_VS;
    sTb->wiiNormalBattle = style & 1;
    sTb->wiiFriend = (style >> 1) & 1;
    sTb->resultBattles = battles;
    sTb->resultWins[0] = wins0;
    sTb->resultWins[1] = wins1;
    Tb_LoadWiiText();
    Tb_SetWiiTitles();
    /* Online boot bypasses the main menu, so its heading samples may never have
       been captured. Sample the original main-menu movie before creating our window. */
    {
        void *arc = File_LoadSync(gProgress->baseFile + 1, NULL, 0);
        void *res = Sprite_Unpack(MPACK_AT(arc, 2), NULL, NULL);
        void *icons = MPACK_AT(res, 23);
        extern void IconWin_PortCaptureMainHeading(void);
        Res_RelocateOffsets(&icons, icons, icons);
        IconWin_Init(MPACK_AT(res, 22), icons);
        IconWin_PortCaptureMainHeading();
        IconWin_Term();
        Heap_Free(res);
        Heap_Free(arc);
    }
    Tb_WinInit();
    edited[0] = PortTbo_ResultTextCopy(0, labels[0], sizeof(labels[0]));
    edited[1] = PortTbo_ResultTextCopy(1, labels[1], sizeof(labels[1]));
    sOnlineResult = Heap_Alloc(sizeof(*sOnlineResult), 0x20, 0, 2);
    memset(sOnlineResult, 0, sizeof(*sOnlineResult));
    sOnlineResult->winner = BattleResult_HasWinner() ? BattleResult_GetWinnerSide() : -1;
    sOnlineResult->active = 1;
    Bgm_Play(0x10B1B);
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        s32 side, me, disabled;
        f32 pulse = 1.0f + 0.06f * sinf((f32)(sTb->frames % 90) * (6.283185307f / 90.0f));
        Job_Run();
        Gfx_BeginFrame();
        Pad_Update();
        Snd_Update();
        ColorFade_Update();
        if (!closing && ColorFade_IsInDone()) {
            OnlineResult_Update();
            if (Battle_GetResult()->winner & BTL_RESULT_ABORT) {
                decision = (Battle_GetResult()->reason & 0x10) != 0;
                closing = 1;
                ColorFade_StartOut(0, 0, 0, 0x14);
            }
        }
        Ps_PageSet();
        Ps_SetShift(0);
        Tb_Backdrop();
        IconWin_Draw();
        Tb_DrawWiiVs();
        MsgWin_PortPrepareNativeNames();
        MsgWin_PortDrawNativeName(Port_NetName(0), 42, 70, 470, 111);
        MsgWin_PortDrawNativeName(Port_NetName(1), 42, 210, 470, 251);
        if (sOnlineResult->winner >= 0) {
            f32 top = sOnlineResult->winner ? 203.0f : 63.0f;
            Tb_ResultPulseSprite(TBO_WII_RESULT_BADGE, 12, top - 22, 110, top + 70,
                                 0, 0, 1, 1, pulse);
            Tb_ResultPulseSprite(TBO_WII_RESULT_WIN, 8, top - 22, 118, top + 69,
                                 18.0f / 128, 18.0f / 128, 117.0f / 128, 109.0f / 128, pulse);
        }
        me = Port_NetMe();
        if (me < 0 || me > 1) me = 0;
        disabled = sOnlineResult->ended[0] || sOnlineResult->ended[1];
        for (side = 0; side < 2; side++) {
            f32 x = side ? 270.0f : 10.0f;
            s32 selected = sOnlineResult->cursor[me] == side;
            u32 tint = side == 0 && disabled ? Ps_Rgba(0x58, 0x58, 0x58, 0x80) : PS_PLAIN;
            Ps_SpriteScreen(selected ? TBO_WII_RESULT_BAR_GOLD : TBO_WII_RESULT_BAR_BLUE,
                            x, 382, x + 232, 414, 0, 0, 225.0f / 256, 37.0f / 64, tint, 0x80);
            if (PortTbo_WiiUseFont() || edited[side]) {
                s32 id = PortTbo_TextId(labels[side], selected ? 4 : 3);
                if (id >= 0) {
                    f32 width = 19.0f * (f32)PortTbo_TextInfo(id, 0) / 1000.0f;
                    f32 fit = width > 208.0f ? 208.0f / width : 1.0f;
                    f32 left = x + (232.0f - width * fit) * 0.5f;
                    Ps_SpriteScreen(id, left, 398.0f - 16.0f * fit,
                                    left + 512.0f * fit, 398.0f + 16.0f * fit,
                                    0, 0, 1, 1, tint, 0x80);
                }
            } else {
                Ps_SpriteScreen(selected ? TBO_WII_RESULT_LABEL_SELECTED : TBO_WII_RESULT_LABEL_IDLE,
                                x + 12, 381, x + 220, 413,
                                0, side * 0.5f, 1, (side + 1) * 0.5f, tint, 0x80);
            }
            if (sOnlineResult->requested[side] || sOnlineResult->ended[side]) {
                f32 bx = (side ? 170.0f : 26.0f) + (sOnlineResult->ended[side] ? 260.0f : 0.0f);
                /* Share a center for the badge and player lettering. */
                f32 cx = bx + 32, cy = 364;
                Ps_SpriteScreen(TBO_WII_RESULT_BADGE,
                                cx - 36 * pulse, cy - 32 * pulse, cx + 32 * pulse, cy + 24 * pulse,
                                0, 0, 1, 1, PS_PLAIN, 128);
                Tb_ResultPulseSprite(TBO_WII_VS_PLAYERS, bx, 348, bx + 64, 380,
                                     0, side * 0.5f, 1, (side + 1) * 0.5f, pulse);
            }
        }
        ColorFade_Draw();
        File_Stub264D90();
        Gfx_EndFrame(1);
        Dma_Flush();
        sTb->frames++;
        /* Match the other Wii pages: play the heading entrance after fade-in. */
        if (!closing && ColorFade_IsInDone()) sTb->wiiBattleT++;
        if (closing && ColorFade_IsOutDone()) break;
    }
    sceGsSyncPath(0, 0);
    Tb_WinTerm();
    Heap_Free(sOnlineResult);
    sOnlineResult = NULL;
    Heap_Free(sTb);
    sTb = NULL;
    if (ownedArc) { Heap_Free(gMenuArc5); gMenuArc5 = NULL; }
    Adx_StopAll();
    gPortMenuMode = 0;
    return decision;
}
