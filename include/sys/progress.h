#ifndef SYS_PROGRESS_H
#define SYS_PROGRESS_H
#include "types.h"

struct LoadSprite;

#define PROGRESS_FLAG_40 0x40
#define PROGRESS_FLAG_FREEZE 0x100
#define PROGRESS_TEAM_COUNT 7

typedef struct ProgressEntry {
    /* 0x00 */ s32 unk0;
    /* 0x04 */ s32 unk4[11];
} ProgressEntry;

typedef struct ProgressTeam {
    /* 0x00 */ s32 flags;
    /* 0x04 */ s32 chara[2][5];
} ProgressTeam;

typedef struct ProgressReward {
    /* 0x00 */ s32 points[3];   /* by level */
    /* 0x0C */ s32 item[3];     /* item ids, -1 = none */
    /* 0x18 */ s32 chara[3];    /* character ids, -1 = none */
    /* 0x24 */ s32 stage[3];    /* stage ids, -1 = none */
    /* 0x30 */ s32 episode[3];  /* episode numbers counted through all sub menus, -1 = none */
} ProgressReward;

/* The 0x4C-byte session range zeroed by Progress_ClearSession. */
typedef struct ProgressMenuSession {
    /* 0x00 */ s32 unk0;
    /* 0x04 */ s32 subMenu;
    /* 0x08 */ s32 subMenuItem;
    /* 0x0C */ s32 difficulty;
    /* 0x10 */ ProgressReward reward;
} ProgressMenuSession;

/* Seven replay/team records plus the flow's control words form the 0x144-byte
 * domain reset by Progress_ClearSession. The former eight-entry view was wrong. */
typedef struct ProgressReplay {
    /* 0x00 */ s32 flags;
    /* 0x04 */ s32 unk4[3];
    /* 0x10 */ ProgressTeam team[PROGRESS_TEAM_COUNT];
} ProgressReplay;

typedef struct ProgressTraining {
    /* 0x00 */ u8 unk0[0x20];
    /* 0x20 */ s32 tutorial;
    /* 0x24 */ s32 unk24;
} ProgressTraining;

/* The full 0x7FC-byte block allocated by Progress_Init. */
typedef struct Progress {
    /* 0x000 */ s32 unk0;
    /* 0x004 */ s32 baseFile;
    /* 0x008 */ void *loadPack;
    /* 0x00C */ void *loadRes;
    /* 0x010 */ struct LoadSprite *loadSprites;
    /* 0x014 */ s32 flags;
    /* 0x018 */ s32 mode;
    /* 0x01C */ s32 prevMode;
    /* 0x020 */ s32 lastLoadType;
    /* 0x024 */ s32 demoPick;
    /* 0x028 */ s32 demoCount;
    /* 0x02C */ s32 mainMenuItem;
    /* 0x030 */ ProgressMenuSession menu;
    /* 0x07C */ u8 unk7C[0x3C4];
    /* 0x440 */ ProgressEntry unk440[5];
    /* 0x530 */ ProgressEntry unk530[5];
    /* 0x620 */ s32 players;
    /* 0x624 */ s32 unk624;
    /* 0x628 */ s32 unk628[3];
    /* 0x634 */ u8 unk634[0x58];
    /* 0x68C */ ProgressReplay replay;
    /* 0x7D0 */ s32 unk7D0;
    /* 0x7D4 */ ProgressTraining training;
} Progress;

extern Progress *gProgress;
#endif
