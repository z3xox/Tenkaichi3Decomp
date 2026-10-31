#ifndef BATTLE_BTL_TASK_H
#define BATTLE_BTL_TASK_H

#include "types.h"
#include "sys/math3d.h"

struct BtlTask;
struct BtlTaskList;

/* The six callbacks shared by all tasks in the effect scene. */
typedef struct BtlTaskCls {
    /* 0x00 */ void (*update)(struct BtlTask *task);            /* BtlTaskList_Update */
    /* 0x04 */ void (*init)(struct BtlTask *task, void *arg);   /* when the task is added */
    /* 0x08 */ void (*term)(struct BtlTask *task);              /* BtlTask_Kill */
    /* 0x0C */ void (*postUpdate)(struct BtlTask *task);        /* BtlTaskList_PostUpdate */
    /* 0x10 */ void (*reset)(struct BtlTask *task);             /* BtlTaskList_Reset */
    /* 0x14 */ void (*draw)(struct BtlTask *task);              /* BtlTaskList_Draw */
} BtlTaskCls; /* size 0x18 */

#define BTL_TASK_TAG_CHAR0 0x800  /* belongs to object id 0: BtlTaskList_Reset(root, 0x800) resets it */
#define BTL_TASK_TAG_CHAR1 0x1000 /* belongs to any other object id */
#define BTL_TASK_TAG_2000 0x2000  /* third reset group */

/* The first byte of a task. The original reads it as bit fields of a byte (lbu / andi / andi 0xFF); a view
   with byte alignment reproduces that. */
typedef struct BtlTaskBits {
    u8 dead : 1;    /* removed by the next list pass (update or reset) that reaches the task */
    u8 updated : 1; /* set after an update of a task that has a draw callback; the draw pass needs it */
    s8 rest : 6;    /* all ones from BtlTask_Init, never read */
} BtlTaskBits;

#define BTL_TASK_BITS(task) ((BtlTaskBits *)(task))

typedef struct BtlTask {
    /* 0x00 */ u8 bits;         /* BtlTaskBits */
    /* 0x01 */ u8 chr;          /* free for the class (the shot layer stores the character index) */
    /* 0x02 */ u16 index;       /* index in the list's task array */
    /* 0x04 */ u32 tag;         /* BTL_TASK_TAG_* and the hit feedback bits of battle/eft_a.h */
    /* 0x08 */ u16 hit;          /* hit feedback: moved / start end timer */
    /* 0x0A */ u8 unkA;
    /* 0x0B */ u8 unkB;
    /* 0x0C */ s32 unkC;
    /* 0x10 */ Vec4 pos;        /* (0, 0, 0, 1) at creation; written by BtlTask_SetPos (hit feedback position) */
    /* 0x20 */ struct BtlTaskList *list;
    /* 0x24 */ struct BtlTaskList *child;
    /* 0x28 */ BtlTaskCls *cls;
    /* 0x2C */ struct BtlTask *prev;
    /* 0x30 */ struct BtlTask *next;
    /* 0x34 */ struct BtlTask *nextFree;
    /* 0x38 */ void *work;      /* this task's work block (the size given when the list was created) */
    /* 0x3C */ s32 unk3C;
} BtlTask; /* size 0x40 */

typedef struct BtlTaskList {
    /* 0x00 */ BtlTask *parent; /* the task that owns this list; NULL for the root */
    /* 0x04 */ BtlTask *head;
    /* 0x08 */ BtlTask *tail;
    /* 0x0C */ BtlTask *free;   /* unused tasks, linked through nextFree */
    /* 0x10 */ BtlTask *tasks;  /* the array */
    /* 0x14 */ void *work;      /* count * work size bytes, or NULL */
} BtlTaskList; /* size 0x18 */

#endif
