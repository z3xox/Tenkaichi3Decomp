/*
 * plat_discord.cpp — Discord Rich Presence for the Tenkaichi 3 PC port.
 *
 * Runs on the render thread. Called from:
 *   - GsDraw_WindowCreate()  (gs_draw.c)  → PortDiscord_Init()
 *   - Port_VBlank()          (plat_stub.c) → PortDiscord_Tick()
 *   - atexit()                             → PortDiscord_Shutdown()
 *
 * Safe: Reads game memory exclusively via SafePtr(addr, len) on 32-bit game
 * addresses, using explicit byte-offset loads. Completely immune to 64-bit
 * struct padding or pointer-width discrepancies between compilers.
 *
 * How it works: every 5th tick PortDiscord_Tick() asks "in a battle?" (PollInBattle). If so UpdateBattle() builds the
 * presence from gBattleWork, else UpdateMenu() builds it from the progress mode (gProgress + 0x18) and, on the
 * select screens, from the screen's own structs. A presence is only built when something changed, and
 * DispatchDiscordUpdate() holds it back if one went out in the last 10 s (Discord's rate limit).
 *
 * Settings (bt3_settings.txt): discord=0 turns it off. Env: BT3_NO_DISCORD, BT3_DISCORD=0.
 */

#include "plat_discord.h"
#include "discord_ipc.h"
#include "../port_addr.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <chrono>

#if defined(__linux__)
#include <sys/uio.h>
#include <unistd.h>
#endif

/* Rate limiter constants: 10 seconds */
#define DISCORD_RATE_LIMIT_MS 10000ULL

struct DiscordPendingState {
    char state[128];
    char details[128];
    char large_key[32];
    char large_text[128];
    char small_key[32];
    char small_text[128];
    long long start_timestamp;
};

static uint64_t sLastSendMs = 0;
static bool sHasPending = false;
static DiscordPendingState sPendingState;

static uint64_t GetMonotonicMs(void) {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void DispatchDiscordUpdate(const char *state, const char *details,
                                  const char *large_key, const char *large_text,
                                  const char *small_key, const char *small_text,
                                  long long start_timestamp) {
    uint64_t now = GetMonotonicMs();

    if (sLastSendMs == 0 || (now - sLastSendMs) >= DISCORD_RATE_LIMIT_MS) {
        /* Not on cooldown: send immediately with zero artificial lag */
        Discord_UpdateEx2(state, details, large_key, large_text, small_key, small_text, start_timestamp);
        sLastSendMs = now;
        sHasPending = false;
    } else {
        /* Within 10-second cooldown: queue newest state without discarding */
        snprintf(sPendingState.state, sizeof(sPendingState.state), "%s", state ? state : "");
        snprintf(sPendingState.details, sizeof(sPendingState.details), "%s", details ? details : "");
        snprintf(sPendingState.large_key, sizeof(sPendingState.large_key), "%s", large_key ? large_key : "");
        snprintf(sPendingState.large_text, sizeof(sPendingState.large_text), "%s", large_text ? large_text : "");
        snprintf(sPendingState.small_key, sizeof(sPendingState.small_key), "%s", small_key ? small_key : "");
        snprintf(sPendingState.small_text, sizeof(sPendingState.small_text), "%s", small_text ? small_text : "");
        sPendingState.start_timestamp = start_timestamp;
        sHasPending = true;
    }
}

extern "C" {
    /* 32-bit game pointer variables in the game's BSS (each is 4 bytes) */
    extern uint32_t gProgress;
    extern uint32_t gCharSel;
    extern uint32_t gTeamSel;
    extern uint32_t gBtlChars;
    extern uint32_t gBtlSeq;

    /* 0x1A00-byte battle work block */
    extern unsigned char gBattleWork[];

    /* Stages added from outside the disc (port/include/plat_stages.h) */
    extern char gPortStageNames[26][64];
    extern int gPortExtraStageCount;

    /* Title of the port's own screen that runs now ("" if none), set by PortScreen_Main (menu/port_screen.c) */
    extern char gPortScreenTitle[64];
    extern char gPortScreenDetail[64];

    /* UTF-8 decoder from a11y.c */
    int PortA11y_Utf8(char *out, int cap, const uint16_t *str);
}

/* A game address as a pointer, only if the len bytes from it can be read (NULL otherwise). Port_Ptr is a plain
 * cast on Linux and Windows, so a stale or garbage pointer (a screen's struct after the screen is gone) would crash
 * the game from here. On Linux the first and last byte are probed with process_vm_readv, which fails with EFAULT
 * instead of faulting; elsewhere only an obviously invalid address is rejected. */
static const void *SafePtr(uint32_t addr, size_t len) {
    /* (every pointer read from the game goes through here: a screen's struct may be freed between two ticks) */
    if (addr < 0x10000 || len == 0) return nullptr;
    const void *p = Port_Ptr(addr);
#if defined(__linux__)
    const char *ends[2] = { (const char *)p, (const char *)p + (len - 1) };
    for (int i = 0; i < 2; i++) {
        char b;
        struct iovec local = { &b, 1 };
        struct iovec remote = { (void *)ends[i], 1 };
        if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != 1) return nullptr;
    }
#endif
    return p;
}

#define PORT_STAGE_FIRST 0x24
#define PORT_STAGE_MAX   26

/* ============================================================
 * Build constants
 * ============================================================ */
#define DISCORD_APP_ID   "1559682390878585305"
#define DISCORD_LOGO     "main_logo"
#define DISCORD_STAGE_UNLISTED "stage_unlisted" /* the large image of a stage the disc does not have */
#define DISCORD_TOOLTIP  "Dragon Ball Z: Budokai Tenkaichi 3"

#define BTL_MODE_VERSUS   0
#define BTL_MODE_SCRIPT   1   /* Dragon History */
#define BTL_MODE_POOL     3   /* Survival / Mission 100 */
#define BTL_MODE_TRAINING 6
#define BTL_MODE_DEMO     7

#define BTL_CONTROL_CPU   2

/* ============================================================
 * Stage name table (indices 0..35)
 * ============================================================ */
static const char *const kStageNames[36] = {
    "Wasteland - Noon",          /* 0 */
    "Rocky Area - Noon",         /* 1 */
    "Planet Namek",              /* 2 */
    "Dying Namek",               /* 3 */
    "World Tournament Stage",    /* 4 */
    "Kami's Lookout",            /* 5 */
    "Cell Games Arena",          /* 6 */
    "Supreme Kai's World",       /* 7 */
    "Hyperbolic Time Chamber",   /* 8 */
    "City Ruins - Noon",         /* 9 */
    "Mountain Road - Noon",      /* 10 */
    "Islands",                   /* 11 */
    "Kame House",                /* 12 */
    "Planet - Night",            /* 13 */
    "Glacier",                   /* 14 */
    "Ruined Earth",              /* 15 */
    "Outer Space",               /* 16 */
    "Penguin Village",           /* 17 */
    "Hell",                      /* 18 */
    "Desert - Noon",             /* 19 */
    "King's Castle",             /* 20 */
    "Muscle Tower",              /* 21 */
    "Mount Paozu",               /* 22 */
    "Wasteland - Evening",       /* 23 */
    "Wasteland - Night",         /* 24 */
    "Rocky Area - Evening",      /* 25 */
    "Rocky Area - Night",        /* 26 */
    "World Tournament - Evening",/* 27 */
    "Cell Games - Noon",         /* 28 */
    "City Ruins - Evening",      /* 29 */
    "City Ruins - Night",        /* 30 */
    "Desert - Evening",          /* 31 */
    "Desert - Night",            /* 32 */
    "Mountain Road - Evening",   /* 33 */
    "Planet - Evening",          /* 34 */
    "Random Stage",              /* 35 */
};

/* ============================================================
 * Official 161 character & transformation table (IDs 0..160).
 * Extracted directly from SLUS-21678 game data.
 * ============================================================ */
#define CHARA_MAX 161
static const char *const kCharaNames[CHARA_MAX] = {
/*   0 */ "Goku (Early)",
/*   1 */ "Goku (Mid)",
/*   2 */ "Goku (Mid) (Super Saiyan)",
/*   3 */ "Goku (End)",
/*   4 */ "Goku (End) (Super Saiyan)",
/*   5 */ "Goku (End) (Super Saiyan 2)",
/*   6 */ "Goku (End) (Super Saiyan 3)",
/*   7 */ "Goku (GT)",
/*   8 */ "Goku (GT) (Super Saiyan)",
/*   9 */ "Goku (GT) (Super Saiyan 3)",
/*  10 */ "Goku (GT) (Super Saiyan 4)",
/*  11 */ "Kid Goku",
/*  12 */ "Great Ape",
/*  13 */ "Kid Gohan",
/*  14 */ "Teen Gohan",
/*  15 */ "Teen Gohan (Super Saiyan)",
/*  16 */ "Teen Gohan (Super Saiyan 2)",
/*  17 */ "Gohan",
/*  18 */ "Gohan (Super Saiyan)",
/*  19 */ "Gohan (Super Saiyan 2)",
/*  20 */ "Great Saiyaman",
/*  21 */ "Ultimate Gohan",
/*  22 */ "Piccolo (Early)",
/*  23 */ "Piccolo (End)",
/*  24 */ "Nail",
/*  25 */ "Krillin",
/*  26 */ "Yamcha",
/*  27 */ "Tien",
/*  28 */ "Chiaotzu",
/*  29 */ "Vegeta (Scouter)",
/*  30 */ "Great Ape Vegeta",
/*  31 */ "Vegeta",
/*  32 */ "Vegeta (Super Saiyan)",
/*  33 */ "Super Vegeta",
/*  34 */ "Vegeta (second form)",
/*  35 */ "Vegeta (second form) (Super Saiyan)",
/*  36 */ "Vegeta (second form) (Super Saiyan 2)",
/*  37 */ "Vegeta (End) (Majin)",
/*  38 */ "Vegeta (second form) (Super Saiyan 4)",
/*  39 */ "Trunks (Sword)",
/*  40 */ "Trunks (Sword) (Super Saiyan)",
/*  41 */ "Trunks",
/*  42 */ "Trunks (Super Saiyan)",
/*  43 */ "Super Trunks",
/*  44 */ "Kid Trunks",
/*  45 */ "Kid Trunks (Super Saiyan)",
/*  46 */ "Goten",
/*  47 */ "Goten (Super Saiyan)",
/*  48 */ "Gotenks",
/*  49 */ "Gotenks (Super Saiyan)",
/*  50 */ "Gotenks (Super Saiyan 3)",
/*  51 */ "Vegito",
/*  52 */ "Super Vegito",
/*  53 */ "Super Gogeta",
/*  54 */ "Gogeta (Super Saiyan 4)",
/*  55 */ "Hercule",
/*  56 */ "Videl",
/*  57 */ "Great Saiyaman 2",
/*  58 */ "Supreme Kai",
/*  59 */ "Kibitokai",
/*  60 */ "Yajirobe",
/*  61 */ "Pikkon",
/*  62 */ "Tapion",
/*  63 */ "Pan",
/*  64 */ "Uub",
/*  65 */ "Majuub",
/*  66 */ "Master Roshi",
/*  67 */ "Master Roshi (Max Power)",
/*  68 */ "Grandpa Gohan",
/*  69 */ "Nam",
/*  70 */ "Android #8",
/*  71 */ "King Vegeta",
/*  72 */ "Great Ape King Vegeta",
/*  73 */ "Bardock",
/*  74 */ "Great Ape Bardock",
/*  75 */ "Fasha",
/*  76 */ "Great Ape Fasha",
/*  77 */ "Raditz",
/*  78 */ "Great Ape Raditz",
/*  79 */ "Saibamen",
/*  80 */ "Nappa",
/*  81 */ "Great Ape Nappa",
/*  82 */ "Zarbon",
/*  83 */ "Zarbon (Post-Transformation)",
/*  84 */ "Dodoria",
/*  85 */ "Cui",
/*  86 */ "Captain Ginyu",
/*  87 */ "Recoome",
/*  88 */ "Burter",
/*  89 */ "Jeice",
/*  90 */ "Guldo",
/*  91 */ "Frieza (1st Form)",
/*  92 */ "Frieza (2nd Form)",
/*  93 */ "Frieza (3rd Form)",
/*  94 */ "Frieza (Final Form)",
/*  95 */ "Frieza (Full Power)",
/*  96 */ "Mecha Frieza",
/*  97 */ "King Cold",
/*  98 */ "Appule",
/*  99 */ "Frieza Soldier",
/* 100 */ "Android #16",
/* 101 */ "Android #17",
/* 102 */ "Android #18",
/* 103 */ "Android #19",
/* 104 */ "Dr. Gero",
/* 105 */ "Cell (1st Form)",
/* 106 */ "Cell (2nd Form)",
/* 107 */ "Cell (Perfect Form)",
/* 108 */ "Cell (Perfect)",
/* 109 */ "Cell Jr.",
/* 110 */ "Babidi",
/* 111 */ "Demon King Dabura",
/* 112 */ "Majin Buu",
/* 113 */ "Majin Buu (Pure Evil)",
/* 114 */ "Super Buu",
/* 115 */ "Super Buu (Gotenks Absorbed)",
/* 116 */ "Super Buu (Gohan Absorbed)",
/* 117 */ "Kid Buu",
/* 118 */ "Garlic Jr.",
/* 119 */ "Super Garlic Jr.",
/* 120 */ "Dr. Wheelo",
/* 121 */ "Turles",
/* 122 */ "Great Ape Turles",
/* 123 */ "Slug",
/* 124 */ "Slug (Giant)",
/* 125 */ "Salza",
/* 126 */ "Cooler",
/* 127 */ "Cooler (Final Form)",
/* 128 */ "Meta-Cooler",
/* 129 */ "Android 13",
/* 130 */ "Android 13 (Fusion)",
/* 131 */ "Broly",
/* 132 */ "Broly (Super Saiyan)",
/* 133 */ "Broly (Legendary Super Saiyan)",
/* 134 */ "Zangya",
/* 135 */ "Bojack",
/* 136 */ "Bojack (Full Power)",
/* 137 */ "Janemba",
/* 138 */ "Super Janemba",
/* 139 */ "Hirudegarn",
/* 140 */ "Baby Vegeta",
/* 141 */ "Super Baby 1",
/* 142 */ "Super Baby 2",
/* 143 */ "Great Ape Baby",
/* 144 */ "Super 17",
/* 145 */ "Nuova Shenron",
/* 146 */ "Syn Shenron",
/* 147 */ "Omega Shenron",
/* 148 */ "General Tao",
/* 149 */ "Cyborg Tao",
/* 150 */ "General Blue",
/* 151 */ "Devilman",
/* 152 */ "Pilaf Machine",
/* 153 */ "Pilaf Machine (Fusion)",
/* 154 */ "Tambourine",
/* 155 */ "Demon King Piccolo",
/* 156 */ "Arale",
/* 157 */ "Chi-Chi",
/* 158 */ "Spopovich",
/* 159 */ "Future Gohan",
/* 160 */ "Future Gohan (Super Saiyan)",
};

/* ============================================================
 * Menu mode labels
 * ============================================================ */
struct DiscModeEntry { int mode; const char *label; };
static const DiscModeEntry kModeLabels[] = {
    {1,  "Title Screen"},
    {2,  "Opening Movie"},
    {4,  "Main Menu"},
    {6,  "Dragon History – Saga Select"},
    {7,  "Dragon History – Episodes"},
    {8,  "Dragon History – Result"},
    {9,  "Dragon History – Saga Ending"},
    {10, "Dragon History – Save"},
    {13, "Ultimate Battle"},
    {14, "Ultimate Battle – Mission Select"},
    {15, "Ultimate Battle – Team Setup"},
    {16, "Ultimate Battle – Result"},
    {17, "Survival Mode"},
    {19, "Survival – Result"},
    {20, "Sim Dragon"},
    {22, "Sim Dragon – The Day"},
    {23, "Sim Dragon – Day Result"},
    {24, "Disc Fusion"},
    {26, "Ranking"},
    {27, "Result"},
    {28, "Choose a Course"},
    {33, "Dragon World Tour"},
    {34, "World Tour – Match"},
    {35, "Tournament Bracket"},
    {38, "Duel Mode Menu"},
    {39, "Duel – Character Select"},
    {40, "Duel – Team Select"},
    {41, "Duel – Character Select (Team)"},
    {44, "Training Mode"},
    {45, "Training – Character Select"},
    {48, "Evolution Z"},
    {49, "Evolution Z – Customize"},
    {50, "Evolution Z – Result"},
    {53, "Data Center"},
    {55, "Data Center – Rankings"},
    {56, "Data Center – Save"},
    {60, "Character Reference"},
    {62, "Options"},
    {70, "Shenron – Wish Screen"},
    {-1, nullptr},
};

/* ============================================================
 * Dynamic character name cache (supports modded slots)
 * ============================================================ */
#define DYNAMIC_CHARA_MAX 512
static char sDynamicNames[DYNAMIC_CHARA_MAX][64];

static int ReadTextLine(char *out, int cap, uint32_t gameFileAddr, int n) {
    out[0] = '\0';
    if (!gameFileAddr || n < 0 || n >= 0x1000) return 0;
    const uint32_t *file = (const uint32_t *)SafePtr(gameFileAddr, 8);
    if (!file) return 0;

    uint32_t firstOfs = file[1] >> 2;
    if (firstOfs > 0x1000 || (uint32_t)n + 1 >= firstOfs) return 0;
    /* the offset table itself must be readable up to entry n+1 */
    if (!SafePtr(gameFileAddr, ((size_t)n + 2) * 4)) return 0;

    uint32_t ofs = (file[n + 1] >> 2) << 2;
    if (ofs < firstOfs * 4 || ofs >= 0x200000) return 0;

    /* the string starts after the table and must be 2-byte aligned and readable */
    if ((ofs & 1) || !SafePtr(gameFileAddr + ofs, 2)) return 0;
    const uint16_t *str = (const uint16_t *)((const uint8_t *)file + ofs);
    if (str[0] != 0xFEFF) return 0;

    return PortA11y_Utf8(out, cap, str);
}

static void CleanBadges(char *str) {
    char *p = str;
    char *w = str;
    while (*p) {
        if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xAE) {
            p += 2;
        } else if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x8C && (unsigned char)p[2] == 0xA7) {
            p += 3;
        } else {
            *w++ = *p++;
        }
    }
    *w = '\0';
    while (w > str && (*(w - 1) == ' ' || *(w - 1) == '\t')) {
        *(--w) = '\0';
    }
}

/* The select screens keep the fighters' names as game text (UTF-16 tables); they are converted once per table into
 * sDynamicNames, so a character added by a mod has its name too. */
static void CacheSelectNames(uint32_t nameTextAddr, uint32_t formTextAddr) {
    /* The text tables only change when the screen loads other ones: read them again only then */
    static uint32_t sCachedName = 0, sCachedForm = 0;
    if (!nameTextAddr) return;
    if (nameTextAddr == sCachedName && formTextAddr == sCachedForm) return;
    const uint32_t *nameFile = (const uint32_t *)SafePtr(nameTextAddr, 8);
    if (!nameFile) return;
    sCachedName = nameTextAddr;
    sCachedForm = formTextAddr;
    memset(sDynamicNames, 0, sizeof(sDynamicNames)); /* no name of a previous list survives */

    int lines = (int)(nameFile[1] >> 2) - 1;
    if (lines <= 0) lines = CHARA_MAX;
    if (lines > DYNAMIC_CHARA_MAX) lines = DYNAMIC_CHARA_MAX;

    char name[128];
    char form[128];
    for (int i = 0; i < lines; i++) {
        if (ReadTextLine(name, sizeof(name), nameTextAddr, i) > 0) {
            CleanBadges(name);
            if (formTextAddr && ReadTextLine(form, sizeof(form), formTextAddr, i) > 0) {
                CleanBadges(form);
                if (form[0] != '\0' && strcmp(form, name) != 0) {
                    snprintf(sDynamicNames[i], sizeof(sDynamicNames[i]), "%s (%s)", name, form);
                    continue;
                }
            }
            snprintf(sDynamicNames[i], sizeof(sDynamicNames[i]), "%s", name);
        }
    }
}

/* ============================================================
 * State tracking
 * ============================================================ */
static long long sModeSwitchTime   = 0;
static int       sLastMode         = -2;
static int       sLastInBattle     = -1;
static int       sLastBtlMode      = -1;
static int       sLastStage        = -1;
static int       sLastChar0        = -1;
static int       sLastChar1        = -1;
static char      sLastTitle[64]    = "";
static char      sLastDetail[64]   = "";
static int       sTickCounter      = 0;
static int       sBattleInFlight   = 0;
static int       sBattleRosterSeen = 0;

static const char *StageName(int id) {
    if (id >= 0 && id < 36) return kStageNames[id];
    /* Maps added from mods */
    if (id >= PORT_STAGE_FIRST && id - PORT_STAGE_FIRST < gPortExtraStageCount &&
        id - PORT_STAGE_FIRST < PORT_STAGE_MAX) {
        return gPortStageNames[id - PORT_STAGE_FIRST];
    }
    return "Unknown Stage";
}

static const char *CharaName(int id) {
    if (id >= 0 && id < DYNAMIC_CHARA_MAX && sDynamicNames[id][0] != '\0') {
        return sDynamicNames[id];
    }
    if (id >= 0 && id < CHARA_MAX) {
        return kCharaNames[id];
    }
    if (id == 0xA1) return "Random";
    if (id == 0xA2) return "Locked";
    if (id == 0xA3) return "Custom";
    return "Unknown";
}

static const char *ModeName(int mode) {
    for (const DiscModeEntry *e = kModeLabels; e->label; ++e) {
        if (e->mode == mode) return e->label;
    }
    return "In Menus";
}

static const char *BtlModeName(int btlMode, int side1IsCPU) {
    switch (btlMode) {
    case BTL_MODE_VERSUS:   return side1IsCPU ? "Vs COM" : "Vs Player";
    case BTL_MODE_SCRIPT:   return "Dragon History";
    case BTL_MODE_POOL:     return "Survival / Mission 100";
    case BTL_MODE_TRAINING: return "Training Mode";
    case BTL_MODE_DEMO:     return "Watching Demo";
    default:                return "Battle";
    }
}

static int GetProgressMode() {
    uint32_t progAddr = gProgress;
    if (!progAddr) return 1;
    const uint8_t *prog = (const uint8_t *)SafePtr(progAddr, 0x1C);
    if (!prog) return 1;
    /* mode is an int at offset 0x18 */
    int m = 1;
    __builtin_memcpy(&m, prog + 0x18, sizeof(int));
    return m;
}

static int PollInBattle(void) {
    int running = 0;
    int roster  = (gBtlChars != 0);

    /* running is at offset 0x19F8 in gBattleWork */
    __builtin_memcpy(&running, &gBattleWork[0x19F8], sizeof(int));
    running = (running != 0);

    if (!sBattleInFlight) {
        if (running || roster) {
            sBattleInFlight   = 1;
            sBattleRosterSeen = roster ? 1 : 0;
        }
    } else {
        if (roster) {
            sBattleRosterSeen = 1;
        } else if (sBattleRosterSeen && !running) {
            sBattleInFlight   = 0;
            sBattleRosterSeen = 0;
        }
    }
    return sBattleInFlight;
}

static const char *const kSagaNames[8] = {
    "Saiyan Saga",
    "Frieza Saga",
    "Androids Saga",
    "Majin Buu Saga",
    "Special Saga",
    "GT Saga",
    "Dragon Ball Saga",
    "What-If Saga"
};

static const char *const kTourNames[5] = {
    "World Tournament",
    "World Martial Arts Big Tournament",
    "Cell Games",
    "Otherworld Tournament",
    "Yamcha Game"
};

static void UpdateBattle(void) {
    int btlMode = 0, stageId = 0;
    __builtin_memcpy(&btlMode, &gBattleWork[0x008], sizeof(int));
    __builtin_memcpy(&stageId, &gBattleWork[0x028], sizeof(int));

    /* Active fighter on Side 0: setup.sides[0].form.chara = offset 0x31C
     * Active fighter on Side 1: setup.sides[1].form.chara = offset 0x58C
     * Side 1 control: setup.sides[1].control = offset 0x538 */
    int char0 = -1, char1 = -1, side1CPU = 1;
    __builtin_memcpy(&char0, &gBattleWork[0x31C], sizeof(int));
    __builtin_memcpy(&char1, &gBattleWork[0x58C], sizeof(int));

    int ctrl1 = 0;
    __builtin_memcpy(&ctrl1, &gBattleWork[0x538], sizeof(int));
    side1CPU = (ctrl1 == BTL_CONTROL_CPU) ? 1 : 0;

    /* Fallback if form.chara is not yet initialized */
    if (char0 < 0 || char0 > 0xA1) {
        __builtin_memcpy(&char0, &gBattleWork[0x0C4], sizeof(int));
    }
    if (char1 < 0 || char1 > 0xA1) {
        __builtin_memcpy(&char1, &gBattleWork[0x334], sizeof(int));
    }

    int changed = (btlMode  != sLastBtlMode  ||
                   stageId  != sLastStage     ||
                   char0    != sLastChar0     ||
                   char1    != sLastChar1     ||
                   sLastInBattle != 1);

    if (!changed) return;

    if (sLastInBattle != 1) {
        sModeSwitchTime = (long long)time(nullptr);
    }
    sLastBtlMode  = btlMode;
    sLastStage    = stageId;
    sLastChar0    = char0;
    sLastChar1    = char1;
    sLastInBattle = 1;

    char state[128];
    char details[128];
    const char *typeName  = BtlModeName(btlMode, side1CPU);
    const char *stageName = StageName(stageId);

    snprintf(state, sizeof(state), "%s", typeName);

    if (char0 >= 0 && char1 >= 0) {
        snprintf(details, sizeof(details), "%s vs %s", CharaName(char0), CharaName(char1));
    } else if (char0 >= 0) {
        snprintf(details, sizeof(details), "%s", CharaName(char0));
    } else {
        snprintf(details, sizeof(details), "In Battle");
    }

    char stageKey[32];
    if (stageId >= 0 && stageId < 35) {
        snprintf(stageKey, sizeof(stageKey), "stage_%d", stageId);
    } else {
        snprintf(stageKey, sizeof(stageKey), "%s", DISCORD_STAGE_UNLISTED);
    }

    /* Large image: Stage preview thumbnail with stage name tooltip.
     * Small image: Game logo with DBZ BT3 tooltip. */
    DispatchDiscordUpdate(state, details, stageKey, stageName, DISCORD_LOGO, DISCORD_TOOLTIP, sModeSwitchTime);
}

/* Menus: `mode` is gProgress->mode. The select screens (39/45 character, 40/41 team) also give the picked fighters,
 * read from the screen's struct. The presence is rebuilt only when the mode, the fighters, or the title/page of a
 * port screen (gPortScreenTitle / gPortScreenDetail, mode 80) changed. */
static void UpdateMenu(int mode) {
    int char0 = -1, char1 = -1;

    /* Live character selection reading */
    if ((mode == 39 || mode == 45) && gCharSel != 0) {
        const uint8_t *cs = (const uint8_t *)SafePtr(gCharSel, 0x500);
        if (cs) {
            uint32_t nameTextAddr = 0, formTextAddr = 0;
            __builtin_memcpy(&nameTextAddr, cs + 0x28, sizeof(uint32_t));
            __builtin_memcpy(&formTextAddr, cs + 0x2C, sizeof(uint32_t));
            CacheSelectNames(nameTextAddr, formTextAddr);

            uint32_t side0Addr = 0, side1Addr = 0;
            __builtin_memcpy(&side0Addr, cs + 0x4F8, sizeof(uint32_t));
            __builtin_memcpy(&side1Addr, cs + 0x4FC, sizeof(uint32_t));

            if (side0Addr != 0) {
                const uint8_t *s0 = (const uint8_t *)SafePtr(side0Addr, 0x74);
                if (s0) {
                    int state = 0, chara = -1, picked = -1;
                    __builtin_memcpy(&state, s0 + 0x70, sizeof(int));
                    __builtin_memcpy(&chara, s0 + 0x6C, sizeof(int));
                    __builtin_memcpy(&picked, s0 + 0x1C, sizeof(int));
                    char0 = (state == 7 && picked >= 0) ? picked : chara;
                }
            }
            if (side1Addr != 0) {
                const uint8_t *s1 = (const uint8_t *)SafePtr(side1Addr, 0x74);
                if (s1) {
                    int state = 0, chara = -1, picked = -1;
                    __builtin_memcpy(&state, s1 + 0x70, sizeof(int));
                    __builtin_memcpy(&chara, s1 + 0x6C, sizeof(int));
                    __builtin_memcpy(&picked, s1 + 0x1C, sizeof(int));
                    char1 = (state == 7 && picked >= 0) ? picked : chara;
                }
            }
        }
    } else if ((mode == 40 || mode == 41) && gTeamSel != 0) {
        const uint8_t *ts = (const uint8_t *)SafePtr(gTeamSel, 0x8F0);
        if (ts) {
            uint32_t nameTextAddr = 0, formTextAddr = 0;
            __builtin_memcpy(&nameTextAddr, ts + 0x28, sizeof(uint32_t));
            __builtin_memcpy(&formTextAddr, ts + 0x2C, sizeof(uint32_t));
            CacheSelectNames(nameTextAddr, formTextAddr);

            uint32_t side0Addr = 0, side1Addr = 0;
            __builtin_memcpy(&side0Addr, ts + 0x8E8, sizeof(uint32_t));
            __builtin_memcpy(&side1Addr, ts + 0x8EC, sizeof(uint32_t));

            if (side0Addr != 0) {
                const uint8_t *s0 = (const uint8_t *)SafePtr(side0Addr, 0x13C);
                if (s0) __builtin_memcpy(&char0, s0 + 0x138, sizeof(int));
            }
            if (side1Addr != 0) {
                const uint8_t *s1 = (const uint8_t *)SafePtr(side1Addr, 0x13C);
                if (s1) __builtin_memcpy(&char1, s1 + 0x138, sizeof(int));
            }
        }
    }

    /* A screen of the port's own (or a mod's) names itself; a mode nobody named stays "In Menus" */
    char title[64];
    snprintf(title, sizeof(title), "%s", gPortScreenTitle);
    char page[64];
    snprintf(page, sizeof(page), "%s", gPortScreenDetail);

    int changed = (mode != sLastMode || sLastInBattle != 0 ||
                   char0 != sLastChar0 || char1 != sLastChar1 ||
                   strcmp(title, sLastTitle) != 0 || strcmp(page, sLastDetail) != 0);
    if (!changed) return;

    if (mode != sLastMode || sLastInBattle != 0) {
        sModeSwitchTime = (long long)time(nullptr);
    }
    snprintf(sLastTitle, sizeof(sLastTitle), "%s", title);
    snprintf(sLastDetail, sizeof(sLastDetail), "%s", page);
    sLastMode     = mode;
    sLastInBattle = 0;
    sLastBtlMode  = -1;
    sLastStage    = -1;
    sLastChar0    = char0;
    sLastChar1    = char1;

    char state[128];
    char details[128];
    const uint8_t *prog = (gProgress != 0) ? (const uint8_t *)SafePtr(gProgress, 0x88) : nullptr;

    if (title[0] != '\0') {
        snprintf(state, sizeof(state), "%s", title);
        snprintf(details, sizeof(details), "%s", page[0] != '\0' ? page : "In Menus");
    } else if (mode == 1) {
        snprintf(state, sizeof(state), "Title Screen");
        snprintf(details, sizeof(details), "Press Start");
    } else if (mode == 2) {
        snprintf(state, sizeof(state), "Opening Movie");
        snprintf(details, sizeof(details), "Watching Intro");
    } else if (mode == 4) {
        snprintf(state, sizeof(state), "Main Menu");
        snprintf(details, sizeof(details), "Selecting Mode");
    } else if (mode >= 6 && mode <= 10) {
        snprintf(state, sizeof(state), "Dragon History");
        int subMenu = -1, subMenuItem = -1;
        if (prog) {
            __builtin_memcpy(&subMenu, prog + 0x34, sizeof(int));
            __builtin_memcpy(&subMenuItem, prog + 0x38, sizeof(int));
        }
        const char *saga = (subMenu >= 0 && subMenu < 8) ? kSagaNames[subMenu] : "Story";
        if (mode == 6) {
            snprintf(details, sizeof(details), "%s – Saga Select", saga);
        } else if (mode == 7) {
            if (subMenuItem >= 0) {
                snprintf(details, sizeof(details), "%s – Episode %d", saga, subMenuItem + 1);
            } else {
                snprintf(details, sizeof(details), "%s – Episode Select", saga);
            }
        } else if (mode == 8) {
            snprintf(details, sizeof(details), "%s – Battle Result", saga);
        } else if (mode == 9) {
            snprintf(details, sizeof(details), "%s – Saga Cleared!", saga);
        } else {
            snprintf(details, sizeof(details), "Saving Story Progress");
        }
    } else if (mode >= 13 && mode <= 16) {
        snprintf(state, sizeof(state), "Ultimate Battle");
        if (mode == 14) snprintf(details, sizeof(details), "Mission 100 – Selecting Mission");
        else if (mode == 15) snprintf(details, sizeof(details), "Mission 100 – Team Setup");
        else if (mode == 16) snprintf(details, sizeof(details), "Mission 100 – Result");
        else snprintf(details, sizeof(details), "Course Select");
    } else if (mode == 17 || mode == 19) {
        snprintf(state, sizeof(state), "Survival Mode");
        snprintf(details, sizeof(details), (mode == 19) ? "Survival – Result" : "Endless Battle");
    } else if (mode == 20 || mode == 22 || mode == 23) {
        snprintf(state, sizeof(state), "Sim Dragon");
        if (mode == 22) snprintf(details, sizeof(details), "Training & Exploration");
        else if (mode == 23) snprintf(details, sizeof(details), "Day Result");
        else snprintf(details, sizeof(details), "Menu");
    } else if (mode == 24) {
        snprintf(state, sizeof(state), "Disc Fusion");
        snprintf(details, sizeof(details), "System Menu");
    } else if (mode >= 33 && mode <= 35) {
        snprintf(state, sizeof(state), "Dragon World Tour");
        int tour = -1;
        if (prog) {
            __builtin_memcpy(&tour, prog + 0x84, sizeof(int));
        }
        const char *tName = (tour >= 0 && tour < 5) ? kTourNames[tour] : "Tournament";
        if (mode == 33) snprintf(details, sizeof(details), "%s", tName);
        else if (mode == 34) snprintf(details, sizeof(details), "%s – Entrants", tName);
        else snprintf(details, sizeof(details), "%s – Bracket", tName);
    } else if (mode == 38) {
        snprintf(state, sizeof(state), "Duel Mode");
        snprintf(details, sizeof(details), "Match Setup");
    } else if (mode == 39) {
        snprintf(state, sizeof(state), "Duel Mode");
        if (char0 >= 0 && char1 >= 0) {
            snprintf(details, sizeof(details), "%s vs %s", CharaName(char0), CharaName(char1));
        } else if (char0 >= 0) {
            snprintf(details, sizeof(details), "%s picking…", CharaName(char0));
        } else {
            snprintf(details, sizeof(details), "Character Select");
        }
    } else if (mode == 40 || mode == 41) {
        snprintf(state, sizeof(state), "Team Battle");
        if (char0 >= 0 && char1 >= 0) {
            snprintf(details, sizeof(details), "%s vs %s", CharaName(char0), CharaName(char1));
        } else if (char0 >= 0) {
            snprintf(details, sizeof(details), "%s picking…", CharaName(char0));
        } else {
            snprintf(details, sizeof(details), "Team Select");
        }
    } else if (mode == 44) {
        snprintf(state, sizeof(state), "Training Mode");
        snprintf(details, sizeof(details), "Training Menu");
    } else if (mode == 45) {
        snprintf(state, sizeof(state), "Training Mode");
        if (char0 >= 0) {
            snprintf(details, sizeof(details), "Selected: %s", CharaName(char0));
        } else {
            snprintf(details, sizeof(details), "Character Select");
        }
    } else if (mode >= 48 && mode <= 50) {
        snprintf(state, sizeof(state), "Evolution Z");
        if (mode == 48) snprintf(details, sizeof(details), "Item Shop");
        else if (mode == 49) snprintf(details, sizeof(details), "Customizing Z-Items");
        else snprintf(details, sizeof(details), "Z-Item Fusion");
    } else if (mode >= 53 && mode <= 56) {
        snprintf(state, sizeof(state), "Data Center");
        if (mode == 55) snprintf(details, sizeof(details), "Viewing Rankings");
        else snprintf(details, sizeof(details), "Password Management");
    } else if (mode == 60) {
        snprintf(state, sizeof(state), "Character Reference");
        snprintf(details, sizeof(details), "Viewing Character Profiles");
    } else if (mode == 62) {
        snprintf(state, sizeof(state), "Options");
        snprintf(details, sizeof(details), "Game Settings");
    } else if (mode == 70) {
        snprintf(state, sizeof(state), "Shenron Shrine");
        snprintf(details, sizeof(details), "Making a Wish");
    } else {
        snprintf(state, sizeof(state), "In Menus");
        snprintf(details, sizeof(details), "%s", ModeName(mode));
    }

    DispatchDiscordUpdate(state, details, DISCORD_LOGO, DISCORD_TOOLTIP, nullptr, nullptr, sModeSwitchTime);
}

/* ============================================================
 * Public API
 * ============================================================ */
extern "C" {

extern int Port_Setting(const char *name, int def);
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);

void PortDiscord_Init(void) {
    /* The setting is on by default: write it into bt3_settings.txt so it is there to turn off (discord=0) */
    Port_SettingSave("discord", Port_Setting("discord", 1));
    Port_SettingsWrite();
    const char *gs = getenv("BT3_GS");
    if (gs && strcmp(gs, "none") == 0) return;
    if (getenv("BT3_DEMO") != nullptr)  return;
    if (getenv("BT3_NO_DISCORD") != nullptr) return;
    const char *envDisc = getenv("BT3_DISCORD");
    if (envDisc && strcmp(envDisc, "0") == 0) return;
    if (Port_Setting("discord", 1) == 0) return;

    Discord_Init(DISCORD_APP_ID);

    sModeSwitchTime = (long long)time(nullptr);
    sLastSendMs = 0;
    sHasPending = false;
    /* Do not dispatch a dummy update here: the first real frame in UpdateMenu will send immediately */
}

void PortDiscord_Tick(void) {
    if (Port_Setting("discord", 1) == 0) {
        if (Discord_IsConnected()) Discord_Shutdown();
        return;
    }

    uint64_t now = GetMonotonicMs();

    if (!Discord_IsConnected()) {
        static uint64_t sLastReconnectMs = 0;
        if (sLastReconnectMs == 0 || (now - sLastReconnectMs) >= 3000) {
            sLastReconnectMs = now;
            Discord_Init(DISCORD_APP_ID);
            if (Discord_IsConnected()) {
                sLastSendMs = 0;
                sLastMode = -2;
                sLastInBattle = -1;
            }
        }
        return;
    }

    Discord_RunCallbacks();

    /* Automatically dispatch the latest state when the 10s cooldown timer expires */
    if (sHasPending && (now - sLastSendMs) >= DISCORD_RATE_LIMIT_MS) {
        Discord_UpdateEx2(sPendingState.state, sPendingState.details,
                          sPendingState.large_key, sPendingState.large_text,
                          sPendingState.small_key, sPendingState.small_text,
                          sPendingState.start_timestamp);
        sLastSendMs = now;
        sHasPending = false;
    }

    /* Poll every 5 frames (~83 ms at 60 FPS) so changes are detected immediately */
    if (++sTickCounter < 5) return;
    sTickCounter = 0;

    int inBattle = PollInBattle();
    if (inBattle) {
        UpdateBattle();
    } else {
        int mode = GetProgressMode();
        UpdateMenu(mode);
    }
}

void PortDiscord_Shutdown(void) {
    Discord_Shutdown();
}

} /* extern "C" */