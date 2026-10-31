#ifndef BATTLE_EFT_I_H
#define BATTLE_EFT_I_H

#include "types.h"
#include "battle/eft_h.h"
#include "battle/btl_task.h"
#include "sys/math3d.h"

/*
 * Technique effects, third part: src/battle/eft_i.c = 0x14F230..0x1532A0 (43 functions).
 * It continues src/battle/eft_h.c (layer 1 of the effect scene, the technique effect manager) and holds
 *
 * 1. The rest of the effect pack library (EftEmit_*): the last four per-kind part drivers
 *    (EftEmit_SpawnType9 / 10 / 15 / 12), the dispatcher every technique effect type calls for each part of
 *    its pack (EftEmit_Spawn, EftEmit_SpawnOwn), the passes over all parts (EftEmit_KillAll,
 *    EftEmit_UpdateAlive, EftEmit_MarkCond, EftEmit_MarkKind6), the node slots a pack's parts are attached to
 *    (EftEmit_UpdateNodes...), and the two key-framed values of the pack header.
 *
 * 2. Two of the technique effect types of gEftShotClass (0x2C3700, row = EftShotParam.type + 1):
 *      type 3 (row 4, classes 0x2C3808 group / 0x2C3820 instance)  EftSweep*: a beam from a fighter node whose
 *                 far end sweeps a circle through the opponent's position. It carries a hit record, scorches
 *                 the stage where the beam touches it and destroys the stage object it touches.
 *      type 7 (row 8, classes 0x2C3838 group / 0x2C3850 instance)  EftFollow*: an effect that stays on the
 *                 fighter (node 3) and carries a hit record there; optionally drives the stage blur light.
 *                 Its group class and its last two instance callbacks (reset 0x1532A0, draw 0x1532E8) are in
 *                 the next file.
 *
 * Shared set and technique records are imported from eft_h.h; node records belong to this range.
 * Legacy hit-record views below remain pending.
 */

#define EFT_EMIT_TYPES 19    /* groups in a set; also the bits of *EftSet.mask */
#define EFT_EMIT_MAX 40      /* emitters over all groups (handle slots per half) */
#define EFT_EMIT_NODES 6

/* Flags argument of EftEmit_Spawn and of the eleven EftEmit_SpawnXxx. */
#define EFT_SPAWN_START 0x001  /* create the particle object if the slot is empty */
#define EFT_SPAWN_STOP 0x002   /* let it finish (module "stop" call) */
#define EFT_SPAWN_FADE 0x004   /* with STOP: clear its second parameter first */
#define EFT_SPAWN_KILL 0x008   /* destroy now and clear the slot */
#define EFT_SPAWN_MOVE 0x010   /* write the position */
#define EFT_SPAWN_WARP 0x020   /* with MOVE: the module's second "set position" entry */
#define EFT_SPAWN_SCALE 0x040  /* write the size; also advances the emitter's scale animation */
#define EFT_SPAWN_DIR 0x080    /* write the direction */
#define EFT_SPAWN_DIR2 0x200   /* module 15 only: write the second point without MOVE */

/* EftSetState.flags[n] */
#define EFT_EMIT_ONESHOT 0x01  /* a kind 2 / 6 emitter has been fired */
#define EFT_EMIT_STARTED 0x02
#define EFT_EMIT_STOPPED 0x04
#define EFT_EMIT_ALTNODE 0x20  /* use EftSetDef.altNode (set by EftEmit_MarkCond) */
#define EFT_EMIT_KIND6 0x40    /* set by EftEmit_MarkKind6 */

#define EFT_EMIT_RES(set, i) ((set)->pairAt[(i) + EFT_SET_PAIR])

typedef struct EftEmitNode {
    /* 0x00 */ s32 id;
    /* 0x04 */ s32 unk4[3];
    /* 0x10 */ Vec4 pos;
} EftEmitNode; /* size 0x20 */

/* Node positions sampled once per frame for a set (EftEmit_UpdateNodes). */
typedef struct EftEmitNodes {
    /* 0x000 */ u32 flags;  /* bit 2i slot i resolved, bit 2i+1 slot i is a pair; 0x1000 frozen */
    /* 0x004 */ u32 flagsC; /* bit i: c[i] resolved */
    /* 0x008 */ u32 fixed;  /* bit 2i: slot i was given a node by EftEmit_SetNode */
    /* 0x00C */ s32 unkC;
    /* 0x010 */ EftEmitNode n[EFT_EMIT_NODES][2];
    /* 0x190 */ EftEmitNode c[EFT_EMIT_NODES];
} EftEmitNodes; /* size 0x250 */

/* The four vectors a hit record carries (record +0x10); copied whole, with 64-bit moves. */
typedef struct EftHitPose {
    /* 0x00 */ Vec4 start; /* where the effect was started */
    /* 0x10 */ Vec4 cur;   /* position this frame (record +0x20) */
    /* 0x20 */ Vec4 prev;  /* position last frame (record +0x30) */
    /* 0x30 */ Vec4 vel;
} __attribute__((aligned(8))) EftHitPose; /* size 0x40 */

/* A hit record of the effect scene (0x190 bytes, EftHit_GetNew); only what this file writes. */
typedef struct EftIHitRec {
    /* 0x00 */ u8 unk0[0x10];
    /* 0x10 */ EftHitPose pose;
    /* 0x50 */ s32 flags;      /* 0x40 set here */
    /* 0x54 */ u8 unk54[0xC];
    /* 0x60 */ BtlTask *task;
    /* 0x64 */ EftHSlot *owner;
} EftIHitRec;

/* Work of the parent task of both classes: one emitter set shared by its children. */
typedef struct EftSetWork {
    /* 0x000 */ s32 unk0;
    /* 0x004 */ EftSet set;
} EftSetWork; /* size 0x324 */

/* Four floats, 8-byte aligned: copied with 64-bit moves. */
typedef struct EftIVec4A {
    f32 x, y, z, w;
} __attribute__((aligned(8))) EftIVec4A;

typedef struct EftSweepMark {
    /* 0x00 */ EftIVec4A pos;
    /* 0x10 */ s32 active;
    /* 0x14 */ f32 timer; /* frames */
    /* 0x18 */ s32 pad[2];
} EftSweepMark; /* size 0x20 */

/* EftSweepWork.flags */
#define EFT_SWEEP_ENDING 0x001   /* counting time up to life */
#define EFT_SWEEP_DEAD 0x002
#define EFT_SWEEP_NOW 0x004      /* end without waiting for life */
#define EFT_SWEEP_STOP_SEEN 0x008
#define EFT_SWEEP_TRAIL 0x010    /* fired: add the hit record every frame */
#define EFT_SWEEP_STARTED 0x020
#define EFT_SWEEP_PREV_VALID 0x040
#define EFT_SWEEP_KILLED 0x080
#define EFT_SWEEP_OWN_WIDTH 0x100

/* Work of the instance class 0x2C3820 (technique effect type 3). */
typedef struct EftSweepWork {
    /* 0x000 */ Vec4 pos;      /* beam origin and centre of the sweep: node slot 1 */
    /* 0x010 */ Vec4 dir;
    /* 0x020 */ EftHitPose pose; /* start = node slot 1 at the start, cur = end point of the beam (0x30),
                                    prev = its last position (0x40), vel = dir * speed (0x50) */
    /* 0x060 */ EftHSlot *owner;
    /* 0x064 */ EftSetState state;
    /* 0x330 */ EftEmitNodes nodes;
    /* 0x580 */ EftSet *set;
    /* 0x584 */ s32 unk584[3];
    /* 0x590 */ EftSweepMark mark[15];
    /* 0x770 */ f32 radius;    /* distance between the fighters' node 3, plus 50, at most 800 */
    /* 0x774 */ f32 angle;
    /* 0x778 */ f32 angleStep;
    /* 0x77C */ s32 flags;     /* EFT_SWEEP_* */
    /* 0x780 */ f32 speed;
    /* 0x784 */ f32 unk784;
    /* 0x788 */ f32 scale;
    /* 0x78C */ f32 width;     /* hit radius before the pack's width animation */
    /* 0x790 */ f32 unk790;
    /* 0x794 */ f32 time;
    /* 0x798 */ f32 life;
    /* 0x79C */ s32 unk79C;
} EftSweepWork; /* size 0x7A0 */

/* EftFollowWork.flags: the EFT_SWEEP_* values shifted where they differ. */
#define EFT_FOLLOW_ENDING 0x001
#define EFT_FOLLOW_DEAD 0x002
#define EFT_FOLLOW_NOW 0x004
#define EFT_FOLLOW_STOP_SEEN 0x008
#define EFT_FOLLOW_TRAIL 0x010
#define EFT_FOLLOW_STARTED 0x040
#define EFT_FOLLOW_KILLED 0x080
#define EFT_FOLLOW_OWN_WIDTH 0x200
#define EFT_FOLLOW_LIGHT 0x400

/* Work of the instance class 0x2C3850 (technique effect type 7). */
typedef struct EftFollowWork {
    /* 0x000 */ s32 flags;
    /* 0x004 */ f32 unk4;
    /* 0x008 */ f32 unk8;
    /* 0x00C */ f32 scale;
    /* 0x010 */ f32 width;
    /* 0x014 */ f32 unk14;
    /* 0x018 */ f32 time;
    /* 0x01C */ f32 life;
    /* 0x020 */ f32 light;   /* 0..1, stage light strength */
    /* 0x024 */ s32 unk24[3];
    /* 0x030 */ Vec4 dir;    /* unit vector from node 3 to node 0x2E; also the stage light direction */
    /* 0x040 */ EftHitPose pose; /* start = node 3 at the start, cur = node 3 now (0x50), prev (0x60), vel unused */
    /* 0x080 */ EftHSlot *owner;
    /* 0x084 */ EftSetState state;
    /* 0x350 */ EftEmitNodes nodes;
    /* 0x5A0 */ EftSet *set;
    /* 0x5A4 */ s32 unk5A4[3];
} EftFollowWork; /* size 0x5B0 */

void EftEmit_SpawnType9(EftSet *set, EftSetHandles *handles, s32 flags, s32 arg3, s32 objId, s32 idx, f32 size,
                        f32 scale, f32 rate, Vec4 *pos, Vec4 *dir);
void EftEmit_SpawnType10(EftSet *set, EftSetHandles *handles, s32 flags, s32 arg3, s32 objId, s32 idx, f32 size,
                         f32 scale, f32 rate, Vec4 *pos, Vec4 *unused, Vec4 *dir);
void EftEmit_SpawnType15(EftSet *set, EftSetHandles *handles, s32 flags, s32 arg3, s32 objId, s32 idx, f32 size,
                         f32 scale, f32 rate, Vec4 *pos, Vec4 *pos2, Vec4 *dir);
void EftEmit_SpawnType12(EftSet *set, EftSetHandles *handles, s32 flags, s32 arg3, s32 objId, s32 idx, f32 size,
                         f32 scale, f32 rate, Vec4 *pos, Vec4 *dir);
void EftEmit_SpawnOwn(EftSet *set, EftSetState *state, EftEmitNodes *nodes, Vec4 *pos, Vec4 *dir, s32 type,
                      s32 idx, s32 flags, f32 scale);
void EftEmit_Spawn(EftSet *set, EftSetState *state, EftEmitNodes *nodes, Vec4 *pos, Vec4 *dir, s32 objId,
                   s32 node, s32 arg7, s32 type, s32 idx, s32 flags, f32 scale);
void EftEmit_MarkCond(s32 objId, s32 extra, EftSet *set, EftSetState *state);
void EftEmit_MarkKind6(EftSet *set, EftSetState *state);
void EftEmit_KillAll(EftSet *set, EftSetState *state);
s32 EftEmit_UpdateAlive(EftSet *set, EftSetState *state);
void EftEmit_UpdateNodesReq(EftHSlot *owner, EftEmitNodes *nodes);
void EftEmit_UpdateNodes(EftHSlot *owner, EftEmitNodes *nodes, s32 req);
void EftEmit_SetNode(EftEmitNodes *nodes, s32 slot, s32 node, Vec4 *pos);
void EftEmit_SetNodePos(EftEmitNodes *nodes, s32 slot, Vec4 *pos);
void EftEmit_RefreshFixedNodes(s32 objId, EftEmitNodes *nodes);
void EftEmit_UpdateTrailWidth(EftSet *set, EftSetState *state);
f32 EftEmit_GetTrailWidth(EftSetState *state);
s32 EftEmit_HasWidth2(EftSet *set);
void EftEmit_UpdateWidth2(EftSet *set, EftSetState *state);
f32 EftEmit_GetWidth2(EftSetState *state);

void EftSweep_AddMark(BtlTask *task);
s32 EftSweep_UpdateMarks(BtlTask *task);
void EftSweep_InitPath(BtlTask *task);
void EftSweep_Move(BtlTask *task);
void EftSweep_AddHit(BtlTask *task);
void EftSweep_Emit(s32 objId, BtlTask *task, EftSet *set, s32 reqOnly);
void EftSweep_Init(BtlTask *task, EftHSlot *owner);
void EftSweep_Term(BtlTask *task);
void EftSweep_Update(BtlTask *task);
void EftSweep_PostUpdate(BtlTask *task);
void EftSweep_Reset(BtlTask *task);
void EftSweep_Draw(BtlTask *task);
void EftSweepGroup_Init(BtlTask *task, EftHSlot *owner);
void EftSweepGroup_Term(BtlTask *task);
void EftSweepGroup_Update(BtlTask *task);
void EftSweepGroup_Reset(BtlTask *task);

void EftFollow_AddHit(BtlTask *task);
void EftFollow_UpdateLight(BtlTask *task);
void EftFollow_Emit(s32 objId, BtlTask *task, EftSet *set, s32 reqOnly);
void EftFollow_Init(BtlTask *task, EftHSlot *owner);
void EftFollow_Term(BtlTask *task);
void EftFollow_Update(BtlTask *task);
void EftFollow_PostUpdate(BtlTask *task);

extern s32 gEftEmitNodeSlot[8];

#endif
