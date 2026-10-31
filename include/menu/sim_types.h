#ifndef MENU_SIM_TYPES_H
#define MENU_SIM_TYPES_H
#include "types.h"
#include "sys/flash_core.h"
#include "battle/textbox_types.h"

#define SIMTRAIN_OUTCOMES 6
#define SIMDAY_TRAIN_RANKS 5
#define SIMDAY_TRAIN_KINDS 3
#define SIMDAY_FLASH_NUM 7
#define Q_ITEM_NUM 0x15E
#define SIM_STAT_NUM 4

typedef struct SimTrainTbl {
    /* 0x00 */ s32 weight[3][SIMTRAIN_OUTCOMES]; /* by times the training was repeated: chance of each outcome, of 100 */
    /* 0x48 */ s32 hp[SIMTRAIN_OUTCOMES];        /* change of stat 2 */
    /* 0x60 */ s32 atkMin[SIMTRAIN_OUTCOMES];    /* change of stat 0: drawn from min..max */
    /* 0x78 */ s32 atkMax[SIMTRAIN_OUTCOMES];
    /* 0x90 */ s32 defMin[SIMTRAIN_OUTCOMES];    /* change of stat 1 */
    /* 0xA8 */ s32 defMax[SIMTRAIN_OUTCOMES];
} SimTrainTbl;

typedef struct SimTrain {
    /* 0x00 */ s32 param;       /* kind 0: cards on the table */
    /* 0x04 */ s32 unk4;
    /* 0x08 */ s32 points;
    /* 0x0C */ s32 gain;
    /* 0x10 */ s32 loss;
    /* 0x14 */ s32 turn;        /* first turn (1-based) of this rank */
} SimTrain;

typedef struct SimRound {
    /* 0x00 */ s32 announcer;
    /* 0x04 */ s32 unk4;        /* BattleSetup_SetRule's last argument (non-zero = 1) */
    /* 0x08 */ s32 timeLimit;
    /* 0x0C */ s32 stage;
    /* 0x10 */ s32 bgm;
    /* 0x14 */ s32 unk14;       /* BattleSetup_SetSide's unk1FC of the opponent's side (non-zero = 1) */
    /* 0x18 */ s32 enemy;       /* index into the enemy table */
} SimRound;

typedef struct SimEnemy {
    /* 0x00 */ s32 chara;       /* 998 = drawn from the pool of the round */
    /* 0x04 */ s32 color;
    /* 0x08 */ s32 cpuLevel;
    /* 0x0C */ s32 lastItem;    /* item id for slot 7, 999 = none */
    /* 0x10 */ s32 item[7];     /* item ids, 999 = none (the list is packed into slots 0..) */
} SimEnemy;

typedef struct SimPool {
    s32 chara[16];
} SimPool;

typedef struct SimLevel {
    /* 0x00 */ s32 max;         /* highest attack / defence at this level */
    /* 0x04 */ s32 need;        /* points / 100 above which this level is reached */
} SimLevel;

typedef struct SimEventRow {
    s32 weight[5];
} SimEventRow;

typedef struct SimChange {
    /* 0x00 (0xB90) */ s32 mask;        /* bit n: stat n has a change pending; 0x10 = level-up shown */
    /* 0x04 (0xB94) */ s32 applying;    /* the change shown is being counted into the stat */
    /* 0x08 (0xB98) */ s32 delta[SIM_STAT_NUM]; /* pending change of each stat */
    /* 0x18 (0xBA8) */ s32 stat;        /* stat whose change is shown */
    /* 0x1C (0xBAC) */ s32 shown;       /* attack / defence: picture of the step (2 + step); health / points: the amount */
    /* 0x20 (0xBB0) */ s32 mark;        /* which of three marks shows the step */
} SimChange;

typedef struct SimDay {
    /* 0x000 */ void *pack;         /* file baseFile + 0x1A (compressed) */
    /* 0x004 */ u32 *res;           /* the same unpacked: a pack of 34 sections */
    /* 0x008 */ void *faceFile[2];  /* 0x16800 bytes each: compressed portraits (the player's character, the round's opponent) */
    /* 0x010 */ void *faceRes[2];   /* 0x20800 bytes each: the same unpacked */
    /* 0x018 */ void *msgText;      /* section 24 */
    /* 0x01C */ void *text;         /* section 32 */
    /* 0x020 */ Flash flash[SIMDAY_FLASH_NUM];
    /* 0x154 */ void *bg;           /* section 4: background picture */
    /* 0x158 */ u8 *tex0[63];       /* board */
    /* 0x254 */ u8 *tex5[6];        /* window */
    /* 0x26C */ u8 *tex1[11];       /* event */
    /* 0x298 */ u8 *tex2[16];       /* cards */
    /* 0x2D8 */ u8 *tex3[15];
    /* 0x314 */ u8 *tex4[18];
    /* 0x35C */ u8 *tex6[16];       /* round; [13] and [14] are the two portraits: the opponent's, the player's (SimDay_UpdateFaceLoad) */
    /* 0x39C */ FlashTexRes *numRes;    /* section 22 */
    /* 0x3A0 */ FlashTexRes *faceResA;  /* section 6: pictures for tex0[32] */
    /* 0x3A4 */ FlashTexRes *faceResB;  /* section 7: pictures for tex0[33] */
    /* 0x3A8 */ u8 *faceDefault;    /* shown for picture 12 */
    /* 0x3AC */ TextBox box[3];    /* item names */
    /* 0x550 */ s32 flags;          /* SIMDAY_ */
    /* 0x554 */ s32 loadState;      /* portrait loader: SIMDAY_LOAD_ */
    /* 0x558 */ s32 cur[13];        /* cursor of each state */
    /* 0x58C */ s32 timer;
    /* 0x590 */ s32 state;          /* SIMDAY_ST_ */
    /* 0x594 */ s32 prevState;      /* state the window was opened from */
    /* 0x598 */ s32 talk[2];        /* [0] the guide's closing line to say (1..5), 0 = none; [1] the last one said */
    /* 0x5A0 */ s32 msgLine;        /* line of the message window, -1 = none */
    /* 0x5A4 */ s32 menuText;       /* 1 = the window shows the item names, 0 = its plates */
    /* 0x5A8 */ s32 bgm;            /* + 0x10B16 = music of the round */
    /* 0x5AC */ s32 wait;           /* frames the round picture stays (300) */
    /* 0x5B0 */ s32 day;            /* turn of the round, 0..9 */
    /* 0x5B4 */ s32 preview[5];     /* what a script wants shown for each stat */
    /* 0x5C8 */ s32 own[Q_ITEM_NUM]; /* owned items an event can hand out */
    /* 0xB40 */ s32 ownCount;
    /* 0xB44 */ s32 eventSet;       /* column of the event table: 0..3 by what the save has unlocked */
    /* 0xB48 */ SimEventRow *eventTbl; /* section 28: the 32 random events */
    /* 0xB4C */ SimRound *round;  /* section 25 */
    /* 0xB50 */ SimPool *pool;    /* section 27 */
    /* 0xB54 */ SimEnemy *enemy;  /* section 26 */
    /* 0xB58 */ s32 enemyChara;     /* the opponent of the fight set up */
    /* 0xB5C */ SimTrainTbl *train;       /* section 29 */
    /* 0xB60 */ SimLevel *level;  /* section 30 */
    /* 0xB64 */ s32 event;          /* what the turn turned into (script number) */
    /* 0xB68 */ s32 seq;
    /* 0xB6C */ s32 seqTimer;
    /* 0xB70 */ s32 lastEvent;
    /* 0xB74 */ s32 repeat;         /* times in a row the same event came up, at most 2 */
    /* 0xB78 */ s32 faceA;          /* pictures the monitor goes back to */
    /* 0xB7C */ s32 faceB;
    /* 0xB80 */ s32 rank[SIMDAY_TRAIN_KINDS]; /* rank of each training at the current turn */
    /* 0xB8C */ s32 unkB8C;         /* answer of the two-row menu: 1 = first row */
    /* 0xB90 */ SimChange chg;
    /* 0xBB4 */ s32 potara;         /* bit n: item plate n is lit */
    /* 0xBB8 */ s32 levelUp;        /* a level-up is due when the board comes back */
} SimDay;

extern SimDay *gSimDay;
extern const SimTrain gSimTrain0[SIMDAY_TRAIN_RANKS];
extern const SimTrain gSimTrain1[SIMDAY_TRAIN_RANKS];
extern const SimTrain gSimTrain2[SIMDAY_TRAIN_RANKS];
#endif
