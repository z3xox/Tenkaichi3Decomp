#include "common.h"
#include "sys/math3d.h"
#include "battle/eft_i.h"

/*
 * Technique effects, third part: 0x14F230..0x1532A0. See include/battle/eft_i.h.
 *
 * Callees are declared here with this file's own view types. The ones without a name yet are called by
 * address; what each does is in the comment at its declaration (from how this file uses it, unless stated).
 *
 * Emitted data: .rodata 0x2EC990..0x2ECAD0 (the two jump tables of EftEmit_Spawn, those of EftEmit_KillAll and
 * EftEmit_UpdateAlive, the event bit tables of EftEmit_UpdateNodes and EftFollow_UpdateLight) and
 * .lit4 0x2FC744..0x2FC764 (three constants of EftEmit_Spawn, then EftSweep_InitPath, EftSweep_Move,
 * EftFollow_UpdateLight).
 */

extern void *memset(void *dst, s32 c, u32 n);
extern s32 rand(void);
extern f32 asinf(f32 x);
extern f32 atan2f(f32 y, f32 x);
extern f32 sqrtf(f32 x);

extern void Vec4_Set(Vec4 *dst, f32 x, f32 y, f32 z, f32 w);
extern void Vec4_Copy(Vec4 *dst, Vec4 *src);
extern void Vec4_Sub(Vec4 *dst, Vec4 *a, Vec4 *b);
extern void Vec3_Add(Vec4 *dst, Vec4 *a, Vec4 *b);
extern void Vec3_Sub(Vec4 *dst, Vec4 *a, Vec4 *b);
extern void Vec3_Scale(Vec4 *dst, Vec4 *src, f32 s);
extern void Vec3_Cross(Vec4 *dst, Vec4 *a, Vec4 *b);
extern void Vec3_Normalize(Vec4 *dst, Vec4 *src);
extern f32 Vec3_Dot(Vec4 *a, Vec4 *b);
extern void Mtx_StoreIdentity(Mtx44 *m);
extern void Mtx_MulVec4(Vec4 *dst, Mtx44 *m, Vec4 *src);
extern void Vec3_ScaleAdd(Vec4 *dst, Vec4 *dir, Vec4 *base, f32 s); /* dst = base + dir * s */
extern void Vec3_Set(Vec4 *dst, f32 x, f32 y, f32 z);          /* set x, y, z */
extern void Mtx_RotateZ(Mtx44 *dst, Mtx44 *src, f32 angle);       /* rotate about Z */
extern void Mtx_RotateX(Mtx44 *dst, Mtx44 *src, f32 angle);       /* rotate about X */
extern void Mtx_RotateY(Mtx44 *dst, Mtx44 *src, f32 angle);       /* rotate about Y */

extern s32 BtlCharApi_GetOpponentObjId(s32 objId);
extern s32 BtlCharApi_ObjGetParamFlags0(s32 objId);
extern void BtlCharApi_GetNodePos(s32 objId, s32 node, Vec4 *out);
extern f32 BtlScene_GetCharScale(s32 objId);
extern s32 BtlScene_IsCharStopped(s32 objId);
extern void BtlTask_CreateChildList(BtlTask *task, s32 count, s32 workSize);

/* Layer 1 services (0x14A8C0..0x14DBB8, the file before this one). */
extern s32 EftShot_TestBits(s32 objId, s32 bit);                 /* is this effect event of the fighter raised */
extern s32 EftShot_GetAttrKind(s32 objId, u64 bit);                 /* model node of the event */
#ifdef PORT /* PC build: the definition's types (the PS2 passes both in one 64-bit register) */
extern s32 EftShot_HasTwoAttrs(s32 objId, u64 bit);
#else
extern s32 EftShot_HasTwoAttrs(s32 objId, s32 bit);                 /* does the event have two nodes */
#endif
extern void EftShot_GetAttrPair(s32 objId, u64 bit, s32 *a, s32 *b); /* both nodes of the event */
extern void EftShot_SetHeldFlagA8(s32 objId);                         /* sets the fighter's held flag 0xA8 */
extern void EftShot_Nop(s32 size);                          /* empty */
extern void EftEmit_LoadSet(EftHSlot *owner, EftSet *set, s32 a2, s32 *pack, s32 a4, s32 a5);
extern void EftEmit_FreeSet(EftSet *set);
extern void EftEmit_BeginFrame(EftSet *set);
extern s32 EftEmit_GetEndFrames(EftSet *set);                    /* frames the set keeps running after its end */
extern void EftEmit_InitState(EftSet *set, EftSetState *state);
extern void EftEmit_TermState(EftSet *set, EftSetState *state);
extern s32 EftEmit_GetFlagsFromReq(EftSet *set, EftSetState *state, s32 objId, s32 type, s32 idx, s32 ending, s32 now);
extern s32 EftEmit_GetResetFlags(EftSet *set, EftSetState *state, s32 type, s32 idx);
extern void EftEmit_TagTask(void *handle, s32 objId, s32 arg);

/* Effect scene services. */
extern EftIHitRec *EftHit_GetNew(void);                        /* new hit record */
extern void EftHit_Add(EftIHitRec *rec);                    /* adds it to this frame's list */
extern void *EftHitArena_AllocSphere(void);                              /* sphere from the hit arena */
extern void *EftHitArena_AllocBox(void);                              /* box from the hit arena */
extern void EftHit_SetShapeSpheres(EftIHitRec *rec, void *a, void *b);  /* record shape: two spheres */
extern void EftHit_SetShapeBoxes(EftIHitRec *rec, void *a, void *b);  /* record shape: two boxes */
extern void ColSphere_Set(void *sphere, Vec4 *center, f32 radius);
extern void ColCapsule_Set(void *box, Vec4 *from, Vec4 *to, f32 radius);
extern void EftAim_GetDir(Vec4 *dir, Vec4 *from, s32 objId);   /* aim direction of the fighter from a point */
extern void EftAim_GetDirKeep(EftHSlot *owner, Vec4 *dir, Vec4 *from, s32 objId);
extern BtlTask *BtlTask_GetParent(BtlTask *task);                  /* task that owns the list the task is in */
extern void BtlTask_SetOwnerTag(BtlTask *task, s32 flags);           /* ors into the task flags */
extern void BtlTask_SetDead(BtlTask *task);                      /* kills the task */

/* Stage. */
typedef struct EftISegment {
    Vec4 a;
    Vec4 b;
} EftISegment;
typedef struct EftIStageHit {
    /* 0x00 */ u8 unk0[0x10];
    /* 0x10 */ Vec4 pos;
    /* 0x20 */ u8 unk20[0x20];
    /* 0x40 */ s32 obj;  /* stage object that was hit, < 0 none */
} EftIStageHit;
extern void ColSeg_Set(EftISegment *seg, Vec4 *a, Vec4 *b); /* builds a segment */
extern s32 StgCol_TraceSegment(EftISegment *seg);                    /* segment against the stage */
extern EftIStageHit *StgCol_GetHit(void);                      /* result of the last stage line test */
extern void BtlStage_DestroyObj(s32 objId, s32 obj, Vec4 *dir); /* stage (stg_a): destroys a stage object */
extern void StgBlur_SetCenter(s32 idx, Vec4 *dir, s32 arg);    /* stage blur (stg_c) */
extern void StgBlur_SetColor0Rgba(s32 idx, s32 r, s32 g, s32 b, s32 a);
extern void StgBlur_SetColor1Rgba(s32 idx, s32 r, s32 g, s32 b, s32 a);
extern void StgBlur_SetColor2Rgba(s32 idx, s32 r, s32 g, s32 b, s32 a);
extern void StgBlur_SetColor3Rgba(s32 idx, s32 r, s32 g, s32 b, s32 a);

/* Vector as the impact argument holds it: a 16-byte aligned union with an array view (the same shape as Vec4Aligned in
   eft_a.h; only this shape makes the compound literal below copy it with two doubleword moves). */
typedef union EftIVecU {
    struct {
        f32 x, y, z, w;
    };
    f32 v[4];
} __attribute__((aligned(16))) EftIVecU;

/* Argument of the impact effect 0x187BE0 (EftImpactArg in eft_a.c), passed by value. */
typedef struct EftIMarkArg {
    /* 0x00 */ EftIVecU pos;
    /* 0x10 */ EftIVecU dir;
    /* 0x20 */ s32 effect;
    /* 0x24 */ s32 objId;
    /* 0x28 */ s32 unk28;
} EftIMarkArg; /* size 0x30 */
#ifdef PORT /* PC build: by address, as the definition takes it (it only reads the struct; the PS2 passes a by-value struct of this size by address) */
extern s32 EftImpact_SpawnBlast(EftIMarkArg *arg, f32 size, f32 unk);
#define EftImpact_SpawnBlast(arg, size, unk) EftImpact_SpawnBlast(&(arg), size, unk)
#else
extern s32 EftImpact_SpawnBlast(EftIMarkArg arg, f32 size, f32 unk);
#endif

/* Spawners of the other particle modules (previous file), same shape as the four in this file. */
extern void EftEmit_SpawnType0(EftSet *, EftSetHandles *, s32, s32, s32, s32, f32, f32, f32, Vec4 *, Vec4 *);
extern void EftEmit_SpawnType2(EftSet *, EftSetHandles *, s32, s32, s32, s32, f32, f32, f32, Vec4 *, Vec4 *);
extern void EftEmit_SpawnType16(EftSet *, EftSetHandles *, s32, s32, s32, s32, f32, f32, f32, Vec4 *, Vec4 *);
extern void EftEmit_SpawnType17(EftSet *, EftSetHandles *, s32, s32, s32, s32, s32, f32, f32, f32, Vec4 *, Vec4 *, Vec4 *);
extern void EftEmit_SpawnType18(EftSet *, EftSetHandles *, s32, s32, s32, s32, s32, f32, f32, f32, Vec4 *, Vec4 *);
extern void EftEmit_SpawnType14(EftSet *, EftSetHandles *, s32, s32, s32, s32, f32, f32, f32, Vec4 *, Vec4 *);
extern void EftEmit_SpawnType5(EftSet *, EftSetHandles *, s32, s32, s32, s32, f32, f32, f32, Vec4 *, Vec4 *);

/* Particle modules. Per module: create(arg block), parameter setters, set position (two variants), set size,
   set direction, kill, stop, "is alive". */
typedef struct EftArg9 {
    /* 0x00 */ EftSetPair res;
    /* 0x08 */ void *tex;
    /* 0x10 */ Vec4 pos __attribute__((aligned(16)));
    /* 0x20 */ Vec4 dir __attribute__((aligned(16)));
    /* 0x30 */ f32 size;
    /* 0x34 */ f32 rate;
    /* 0x38 */ s32 objId;
} EftArg9; /* size 0x40 */
extern void *EftQuad_Create(EftArg9 *arg);
extern void EftQuad_Stop(void *h);
extern void EftQuad_SetFade(void *h, s32 v);
extern void EftQuad_Kill(void *h);
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftQuad_SetPos(void *h, Vec4 pos);
#define EftQuad_SetPos(h, pos) EftQuad_SetPos(h, *(Vec4 *)(pos))
#else
extern void EftQuad_SetPos(void *h, Vec4 *pos);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftQuad_Warp(void *h, Vec4 pos);
#define EftQuad_Warp(h, pos) EftQuad_Warp(h, *(Vec4 *)(pos))
#else
extern void EftQuad_Warp(void *h, Vec4 *pos);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftQuad_SetDir(void *h, Vec4 dir);
#define EftQuad_SetDir(h, dir) EftQuad_SetDir(h, *(Vec4 *)(dir))
#else
extern void EftQuad_SetDir(void *h, Vec4 *dir);
#endif
extern void EftQuad_SetSize(void *h, f32 size);
extern void EftQuad_SetTexPair(void *h, void *tex, s32 a, s32 b);
extern void EftQuad_SetDelay(void *h, s32 v);
extern void EftQuad_SetStopDelay(void *h, s32 v);
extern void EftQuad_SetViewOnly(void *h);
extern s32 EftQuad_IsAlive(void *h);
extern void EftQuad_SetOwnOrigin(void *h, s32 v);
extern void EftQuad_SetCut(void *h, s32 v);

typedef struct EftArg10 {
    /* 0x00 */ EftSetPair res;
    /* 0x08 */ void *tex;
    /* 0x10 */ Vec4 pos __attribute__((aligned(16)));
    /* 0x20 */ Vec4 dir __attribute__((aligned(16)));
    /* 0x30 */ f32 size;
    /* 0x34 */ f32 rate;
    /* 0x38 */ s32 unk38;
    /* 0x3C */ s32 unk3C;
    /* 0x40 */ s32 objId;
} EftArg10; /* size 0x50 */
extern void *EftPart10_Create(EftArg10 *arg);
extern void EftPart10_Stop(void *h);
extern void EftPart10_SetFade(void *h, s32 v);
extern void EftPart10_Kill(void *h);
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftPart10_SetPos(void *h, Vec4 pos);
#define EftPart10_SetPos(h, pos) EftPart10_SetPos(h, *(Vec4 *)(pos))
#else
extern void EftPart10_SetPos(void *h, Vec4 *pos);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftPart10_Warp(void *h, Vec4 pos);
#define EftPart10_Warp(h, pos) EftPart10_Warp(h, *(Vec4 *)(pos))
#else
extern void EftPart10_Warp(void *h, Vec4 *pos);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftPart10_SetDir(void *h, Vec4 dir);
#define EftPart10_SetDir(h, dir) EftPart10_SetDir(h, *(Vec4 *)(dir))
#else
extern void EftPart10_SetDir(void *h, Vec4 *dir);
#endif
extern void EftPart10_SetSize(void *h, f32 size);
extern void EftPart10_SetDelay(void *h, s32 v);
extern void EftPart10_SetHold(void *h, s32 v);
extern void EftPart10_SetNoDepth(void *h);
extern void EftPart10_SetKind(void *h, s32 v);
extern s32 EftPart10_IsAlive(void *h);

typedef struct EftArg15 {
    /* 0x00 */ EftSetPair res;
    /* 0x08 */ void *tex;
    /* 0x10 */ Vec4 pos __attribute__((aligned(16)));
    /* 0x20 */ Vec4 pos2 __attribute__((aligned(16)));
    /* 0x30 */ f32 size;
    /* 0x34 */ f32 rate;
    /* 0x38 */ s32 unk38;
    /* 0x3C */ s32 unk3C;
    /* 0x40 */ s32 objId;
} EftArg15; /* size 0x50 */
extern void *EftLink_Create(EftArg15 *arg);
extern void EftLink_Stop(void *h);
extern void EftLink_Kill(void *h);
extern void EftLink_SetFade(void *h, s32 v);
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftLink_SetPos(void *h, Vec4 pos);
#define EftLink_SetPos(h, pos) EftLink_SetPos(h, *(Vec4 *)(pos))
#else
extern void EftLink_SetPos(void *h, Vec4 *pos);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftLink_SetPos2(void *h, Vec4 pos2);
#define EftLink_SetPos2(h, pos2) EftLink_SetPos2(h, *(Vec4 *)(pos2))
#else
extern void EftLink_SetPos2(void *h, Vec4 *pos2);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftLink_Warp(void *h, Vec4 pos);
#define EftLink_Warp(h, pos) EftLink_Warp(h, *(Vec4 *)(pos))
#else
extern void EftLink_Warp(void *h, Vec4 *pos);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftLink_SetDir(void *h, Vec4 dir);
#define EftLink_SetDir(h, dir) EftLink_SetDir(h, *(Vec4 *)(dir))
#else
extern void EftLink_SetDir(void *h, Vec4 *dir);
#endif
extern void EftLink_SetSize(void *h, f32 size);
extern void EftLink_SetDelay(void *h, s32 v);
extern void EftLink_SetStopDelay(void *h, s32 v);
extern void EftLink_SetType(void *h, s32 v);
extern s32 EftLink_IsAlive(void *h);

typedef struct EftArg12 {
    /* 0x00 */ EftSetPair res;
    /* 0x08 */ void *tex;
    /* 0x10 */ Vec4 dir __attribute__((aligned(16)));
    /* 0x20 */ Vec4 pos __attribute__((aligned(16)));
    /* 0x30 */ f32 size;
    /* 0x34 */ f32 rate;
    /* 0x38 */ s32 unk38;
    /* 0x3C */ s32 objId;
    /* 0x40 */ u8 unk40[1];
} EftArg12; /* size 0x50 */
extern void *EftZap_Create(EftArg12 *arg);
extern void EftZap_Kill(void *h);
extern void EftZap_Stop(void *h);
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftZap_SetPos(void *h, Vec4 pos);
#define EftZap_SetPos(h, pos) EftZap_SetPos(h, *(Vec4 *)(pos))
#else
extern void EftZap_SetPos(void *h, Vec4 *pos);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftZap_WarpPos(void *h, Vec4 pos);
#define EftZap_WarpPos(h, pos) EftZap_WarpPos(h, *(Vec4 *)(pos))
#else
extern void EftZap_WarpPos(void *h, Vec4 *pos);
#endif
#ifdef PORT /* PC build: by value, as the definition takes it (the PS2 passes a struct of this size by address) */
extern void EftZap_SetDir(void *h, Vec4 dir);
#define EftZap_SetDir(h, dir) EftZap_SetDir(h, *(Vec4 *)(dir))
#else
extern void EftZap_SetDir(void *h, Vec4 *dir);
#endif
extern void EftZap_SetSize(void *h, f32 size);
extern void EftZap_SetDelay(void *h, s32 v);
extern void EftZap_SetFadeDelay(void *h, s32 v);
extern void EftZap_SetFadeTime(void *h, s32 v);
extern void EftZap_SetFlag20000(void *h);
extern s32 EftZap_IsAlive(void *h);

/* Kill / "is alive" entries of the other seven modules (types 0..8). */
extern void EftRay_Kill(void *h);
extern void EftRays_Kill(void *h);
extern void EftBill_Kill(void *h);
extern void EftRibbon_Kill(void *h);
extern void EftChain_Kill(void *h);
extern void EftAnimPart_Kill(void *h);
extern void EftPtcl_Kill(void *h);
extern s32 EftRay_IsAlive(void *h);
extern s32 EftRays_IsAlive(void *h);
extern s32 EftBill_IsAlive(void *h);
extern s32 EftRibbon_IsAlive(void *h);
extern s32 EftChain_IsAlive(void *h);
extern s32 EftAnimPart_IsAlive(void *h);
extern s32 EftPtcl_IsAlive(void *h);

/* Sweep: 0.003 of a turn a frame (1.08 degrees), starting 11 steps before the opponent. */
#define EFT_SWEEP_STEP (6.2831853f * 0.003f)
#define EFT_SWEEP_START (6.2831853f * 0.003f * -11.0f)

#define H(n) (handles->h[n])
#define EFT_EMIT_SCALE(state, n) ((state)->unk168[n])
#define EFT_EMIT_TIME(state, n) ((state)->time[n])

/* Starts / moves / stops the particle object of emitter idx of group 9 (module 0x194970). */
void EftEmit_SpawnType9(EftSet *set, EftSetHandles *handles, s32 flags, s32 arg3, s32 objId, s32 idx, f32 size,
                        f32 scale, f32 rate, Vec4 *pos, Vec4 *dir) {
    Vec4 p;
    EftSetGroup *grp = &set->group[9];
    s32 n = grp->firstPart + idx;
    EftSetDef *def = &set->parts[n];

    Vec3_ScaleAdd(&p, dir, pos, def->unk10 * scale);
    p.w = 1.0f;
    if (flags & EFT_SPAWN_START) {
        if (H(n) == NULL) {
            void *tex = set->array[2] + (grp->catFirst + def->res) * 0x88;
            s32 *res0 = EFT_EMIT_RES(set, grp->firstPair + idx).a;
            s32 *res1 = EFT_EMIT_RES(set, grp->firstPair + idx).b;
            EftArg9 arg = { { res0, res1 }, tex, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, size, rate, objId };

            Vec4_Copy(&arg.dir, dir);
            if (def->flags & 0x40) {
                Vec4_Copy(&arg.pos, pos);
            } else {
                Vec4_Copy(&arg.pos, &p);
            }
            H(n) = EftQuad_Create(&arg);
            EftQuad_SetDelay(H(n), def->unk5);
            EftQuad_SetStopDelay(H(n), def->unk6);
            EftQuad_SetFade(H(n), def->unk7);
            EftQuad_SetTexPair(H(n), tex, def->unk2, def->unk2);
            if (def->flags & 0x20) {
                EftQuad_SetViewOnly(H(n));
            }
            if (def->unk34 & 8) {
                EftQuad_SetOwnOrigin(H(n), 1);
            }
            EftQuad_SetCut(H(n), arg3);
            EftEmit_TagTask(H(n), objId, arg3);
        }
    }
    if (H(n) != NULL) {
        if (flags & EFT_SPAWN_MOVE) {
            if (flags & EFT_SPAWN_WARP) {
                EftQuad_Warp(H(n), &p);
            } else {
                EftQuad_SetPos(H(n), &p);
            }
        }
        if (flags & EFT_SPAWN_SCALE) {
            EftQuad_SetSize(H(n), size);
        }
        if (flags & EFT_SPAWN_DIR) {
            EftQuad_SetDir(H(n), dir);
        }
        if (flags & EFT_SPAWN_KILL) {
            EftQuad_Kill(H(n));
            H(n) = NULL;
        } else if (flags & EFT_SPAWN_STOP) {
            if (flags & EFT_SPAWN_FADE) {
                EftQuad_SetStopDelay(H(n), 0);
            }
            EftQuad_Stop(H(n));
        }
    }
}

/* Starts / moves / stops the particle object of emitter idx of group 10 (module 0x190610). */
void EftEmit_SpawnType10(EftSet *set, EftSetHandles *handles, s32 flags, s32 arg3, s32 objId, s32 idx, f32 size,
                         f32 scale, f32 rate, Vec4 *pos, Vec4 *unused, Vec4 *dir) {
    Vec4 p;
    EftSetGroup *grp = &set->group[10];
    s32 n = grp->firstPart + idx;
    EftSetDef *def = &set->parts[n];

    Vec3_ScaleAdd(&p, dir, pos, def->unk10 * scale);
    p.w = 1.0f;
    if (flags & EFT_SPAWN_START) {
        if (H(n) == NULL) {
            void *tex = set->array[1] + (grp->catFirst + def->res) * 0x108;
            s32 *res0 = EFT_EMIT_RES(set, grp->firstPair + idx).a;
            s32 *res1 = EFT_EMIT_RES(set, grp->firstPair + idx).b;
            EftArg10 arg = { { res0, res1 }, tex, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, size, rate, def->unk2, def->unk2, objId };

            Vec4_Copy(&arg.dir, dir);
            if (def->flags & 0x40) {
                Vec4_Copy(&arg.pos, pos);
            } else {
                Vec4_Copy(&arg.pos, &p);
            }
            H(n) = EftPart10_Create(&arg);
            EftPart10_SetDelay(H(n), def->unk5);
            EftPart10_SetHold(H(n), def->unk6);
            EftPart10_SetFade(H(n), def->unk7);
            if (def->flags & 0x20) {
                EftPart10_SetNoDepth(H(n));
            }
            EftPart10_SetKind(H(n), arg3);
            EftEmit_TagTask(H(n), objId, arg3);
        }
    }
    if (H(n) != NULL) {
        if (flags & EFT_SPAWN_MOVE) {
            if (flags & EFT_SPAWN_WARP) {
                EftPart10_Warp(H(n), &p);
            } else {
                EftPart10_SetPos(H(n), &p);
            }
        }
        if (flags & EFT_SPAWN_SCALE) {
            EftPart10_SetSize(H(n), size);
        }
        if (flags & EFT_SPAWN_DIR) {
            EftPart10_SetDir(H(n), dir);
        }
        if (flags & EFT_SPAWN_KILL) {
            EftPart10_Kill(H(n));
            H(n) = NULL;
        } else if (flags & EFT_SPAWN_STOP) {
            if (flags & EFT_SPAWN_FADE) {
                EftPart10_SetHold(H(n), 0);
            }
            EftPart10_Stop(H(n));
        }
    }
}

/* Starts / moves / stops the particle object of emitter idx of group 15 (module 0x18BB08), which has two
   end points: pos + dir * offset and pos2 + dir * offset2. */
void EftEmit_SpawnType15(EftSet *set, EftSetHandles *handles, s32 flags, s32 arg3, s32 objId, s32 idx, f32 size,
                         f32 scale, f32 rate, Vec4 *pos, Vec4 *pos2, Vec4 *dir) {
    Vec4 p;
    Vec4 p2;
    EftSetGroup *grp = &set->group[15];
    s32 n = grp->firstPart + idx;
    EftSetDef *def = &set->parts[n];

    Vec3_ScaleAdd(&p, dir, pos, def->unk10 * scale);
    p.w = 1.0f;
    Vec3_ScaleAdd(&p2, dir, pos2, def->unk14 * scale);
    p2.w = 1.0f;
    if (flags & EFT_SPAWN_START) {
        if (H(n) == NULL) {
            void *tex = set->array[1] + (grp->catFirst + def->res) * 0x108;
            s32 *res0 = EFT_EMIT_RES(set, grp->firstPair + idx).a;
            s32 *res1 = EFT_EMIT_RES(set, grp->firstPair + idx).b;
            EftArg15 arg = { { res0, res1 }, tex, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, size, rate, def->unk2, 0, objId };

            if (def->flags & 0x40) {
                Vec4_Copy(&arg.pos, pos);
                Vec4_Copy(&arg.pos2, pos2);
            } else {
                Vec4_Copy(&arg.pos, &p);
                Vec4_Copy(&arg.pos2, &p2);
            }
            H(n) = EftLink_Create(&arg);
            EftLink_SetDelay(H(n), def->unk5);
            EftLink_SetStopDelay(H(n), def->unk6);
            EftLink_SetFade(H(n), def->unk7);
            EftLink_SetType(H(n), arg3);
            EftEmit_TagTask(H(n), objId, arg3);
        }
    }
    if (H(n) != NULL) {
        if (flags & EFT_SPAWN_MOVE) {
            if (flags & EFT_SPAWN_WARP) {
                EftLink_Warp(H(n), &p);
            } else {
                EftLink_SetPos(H(n), &p);
                EftLink_SetPos2(H(n), &p2);
            }
        } else if (flags & EFT_SPAWN_DIR2) {
            EftLink_SetPos2(H(n), &p2);
        }
        if (flags & EFT_SPAWN_SCALE) {
            EftLink_SetSize(H(n), size);
        }
        if (flags & EFT_SPAWN_DIR) {
            EftLink_SetDir(H(n), dir);
        }
        if (flags & EFT_SPAWN_KILL) {
            EftLink_Kill(H(n));
            H(n) = NULL;
        } else if (flags & EFT_SPAWN_STOP) {
            if (flags & EFT_SPAWN_FADE) {
                EftLink_SetStopDelay(H(n), 0);
            }
            EftLink_Stop(H(n));
        }
    }
}

/* Starts / moves / stops the particle object of emitter idx of group 12 (module 0x1A6598). */
void EftEmit_SpawnType12(EftSet *set, EftSetHandles *handles, s32 flags, s32 arg3, s32 objId, s32 idx, f32 size,
                         f32 scale, f32 rate, Vec4 *pos, Vec4 *dir) {
    Vec4 p;
    EftSetGroup *grp = &set->group[12];
    s32 n = grp->firstPart + idx;
    EftSetDef *def = &set->parts[n];

    Vec3_ScaleAdd(&p, dir, pos, def->unk10 * scale);
    p.w = 1.0f;
    if (flags & EFT_SPAWN_START) {
        if (H(n) == NULL) {
            void *tex = set->array[1] + (grp->catFirst + def->res) * 0x108;
            s32 *res0 = EFT_EMIT_RES(set, grp->firstPair + idx).a;
            s32 *res1 = EFT_EMIT_RES(set, grp->firstPair + idx).b;
            EftArg12 arg = { { res0, res1 }, tex, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, size, rate, def->unk2, objId, { arg3 } };

            if (def->flags & 0x40) {
                Vec4_Copy(&arg.pos, pos);
            } else {
                Vec4_Copy(&arg.pos, &p);
            }
            Vec4_Copy(&arg.dir, dir);
            H(n) = EftZap_Create(&arg);
            EftZap_SetDelay(H(n), def->unk5);
            EftZap_SetFadeDelay(H(n), def->unk6);
            EftZap_SetFadeTime(H(n), def->unk7);
            if (def->flags & 0x20) {
                EftZap_SetFlag20000(H(n));
            }
            EftEmit_TagTask(H(n), objId, arg3);
        }
    }
    if (H(n) != NULL) {
        if (flags & EFT_SPAWN_MOVE) {
            if (flags & EFT_SPAWN_WARP) {
                EftZap_WarpPos(H(n), &p);
            } else {
                EftZap_SetPos(H(n), &p);
            }
        }
        if (flags & EFT_SPAWN_SCALE) {
            EftZap_SetSize(H(n), size);
        }
        if (flags & EFT_SPAWN_DIR) {
            EftZap_SetDir(H(n), dir);
        }
        if (flags & EFT_SPAWN_KILL) {
            EftZap_Kill(H(n));
            H(n) = NULL;
        } else if (flags & EFT_SPAWN_STOP) {
            if (flags & EFT_SPAWN_FADE) {
                EftZap_SetFadeDelay(H(n), 0);
            }
            EftZap_Stop(H(n));
        }
    }
}

/* EftEmit_Spawn for the set's own fighter: object id and node come from the set's owner, arg7 is 1. */
void EftEmit_SpawnOwn(EftSet *set, EftSetState *state, EftEmitNodes *nodes, Vec4 *pos, Vec4 *dir, s32 type,
                      s32 idx, s32 flags, f32 scale) {
    EftEmit_Spawn(set, state, nodes, pos, dir, set->owner->arg.chr, set->owner->param->unk6, 1, type, idx, flags,
                  scale);
}

/* Gives one part of an effect pack its command: resolves where it is (node slot, opponent, or the caller's
   position, plus a random offset inside the part's spread when it starts), which way it points and how big it
   is, hands that to the part's module, then records started / stopped and advances the part's scale animation. */

void EftEmit_Spawn(EftSet *set, EftSetState *state, EftEmitNodes *nodes, Vec4 *pos, Vec4 *dir, s32 objId,
                   s32 node, s32 arg7, s32 type, s32 idx, s32 flags, f32 scale) {
    Vec4 p;
    Vec4 off;
    Vec4 d;
    Vec4 up;
    Mtx44 m;
    s32 half;
    s32 count = 1;
    s32 slot;
    s32 sel;
    s32 n = set->group[type].firstPart + idx;
    EftSetDef *def = &set->parts[n];
    f32 rate;
    f32 size;

    if (def->unk34 & 1) {
        if (def->unk34 & 0x10) {
            if (!(BtlCharApi_ObjGetParamFlags0(BtlCharApi_GetOpponentObjId(objId)) & 0x80)) {
                return;
            }
        } else {
            if (BtlCharApi_ObjGetParamFlags0(BtlCharApi_GetOpponentObjId(objId)) & 0x80) {
                return;
            }
        }
    }
    sel = def->node;
    if (state->flag[n] & EFT_EMIT_ALTNODE) {
        sel = def->unk36;
    }
    slot = gEftEmitNodeSlot[sel];
    if (flags & EFT_SPAWN_START) {
        if (!(state->flag[n] & EFT_EMIT_STARTED)) {
            if (def->phase == 2) {
                if (def->endPhase != 0) {
                    return;
                }
                if (def->unk18 <= 0.0f) {
                    return;
                }
                state->flag[n] |= EFT_EMIT_ONESHOT;
            } else if (def->phase == 6) {
                if (def->endPhase != 0) {
                    return;
                }
                if (def->unk18 <= 0.0f) {
                    return;
                }
                state->flag[n] |= EFT_EMIT_ONESHOT;
            }
            if (def->endPhase == 0 && def->unk18 <= 0.0f) {
                return;
            }
        }
    }
    rate = def->unk18;
    if (flags & EFT_SPAWN_STOP) {
        if (def->phase == 2) {
            flags &= ~EFT_SPAWN_STOP;
        }
    }
    switch (def->scaleMode) {
    case 0:
        size = EFT_EMIT_SCALE(state, n) * scale;
        break;
    case 1:
        size = EFT_EMIT_SCALE(state, n) * BtlScene_GetCharScale(objId);
        break;
    case 2:
        size = EFT_EMIT_SCALE(state, n) * BtlScene_GetCharScale(BtlCharApi_GetOpponentObjId(objId));
        break;
    default:
        size = EFT_EMIT_SCALE(state, n) * scale;
        break;
    }
    switch (def->dirMode) {
    case 0:
        Vec4_Copy(&d, dir);
        break;
    case 1:
        Vec4_Copy(&d, dir);
        Vec3_Scale(&d, &d, -1.0f);
        break;
    case 2:
        Vec4_Set(&up, 0.0f, 1.0f, 0.0f, 1.0f);
        if (dir->y == -1.0f) {
            Vec4_Set(&up, 0.0f, 0.0f, 1.0f, 1.0f);
        }
        Vec3_Cross(&d, dir, &up);
        Vec3_Cross(&d, dir, &d);
        break;
    case 3:
        Vec4_Set(&up, 0.0f, 1.0f, 0.0f, 1.0f);
        if (dir->y == -1.0f) {
            Vec4_Set(&up, 0.0f, 0.0f, 1.0f, 1.0f);
        }
        Vec3_Cross(&d, dir, &up);
        Vec3_Cross(&d, dir, &d);
        Vec3_Scale(&d, &d, -1.0f);
        break;
    case 4:
        Vec4_Set(&d, 0.0f, -1.0f, 0.0f, 1.0f);
        break;
    case 5:
        Vec4_Set(&d, 0.0f, 1.0f, 0.0f, 1.0f);
        break;
    }
    if (nodes->flags & (1 << (slot * 2 + 1))) {
        count = 2;
    }
    for (half = 0; half < count; half++) {
        EftSetHandles *handles = &state->handle[half];

        if (slot >= 0) {
            if (def->flags & 8) {
                Vec4_Copy(&p, &nodes->c[slot].pos);
            } else {
                Vec4_Copy(&p, &nodes->n[slot][half].pos);
            }
        } else if (sel == 6) {
            BtlCharApi_GetNodePos(BtlCharApi_GetOpponentObjId(objId), 3, &p);
        } else {
            Vec4_Copy(&p, pos);
        }
        if (flags & EFT_SPAWN_START) {
            f32 spread;

            Vec4_Set(&off, 0.0f, 0.0f, 0.0f, 1.0f);
            spread = def->spread;
            if (0.0f < spread) {
                f32 angle = rand() / 2147483647.0f * 6.2831853f;
                f32 pitch;
                f32 yaw;

                if (3.14159265f <= angle) {
                    angle -= 6.2831853f;
                }
                Vec3_Set(&off, 0.0f, spread * (rand() / 2147483647.0f), 0.0f);
                Mtx_StoreIdentity(&m);
                Mtx_RotateZ(&m, &m, angle);
                Mtx_MulVec4(&off, &m, &off);
                pitch = asinf(-dir->y);
                yaw = atan2f(dir->x, dir->z);
                Mtx_StoreIdentity(&m);
                Mtx_RotateX(&m, &m, pitch);
                Mtx_RotateY(&m, &m, yaw);
                Mtx_MulVec4(&off, &m, &off);
                Vec3_Add(&p, &p, &off);
            }
        }
        switch (type) {
        case 0:
            EftEmit_SpawnType0(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, &d);
            break;
        case 2:
            EftEmit_SpawnType2(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, &d);
            break;
        case 16:
            EftEmit_SpawnType16(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, &d);
            break;
        case 17:
            EftEmit_SpawnType17(set, handles, flags, arg7, objId, node, idx, size, scale, rate, &p, pos, &d);
            break;
        case 18:
            EftEmit_SpawnType18(set, handles, flags, arg7, objId, node, idx, size, scale, rate, &p, &d);
            break;
        case 14:
            EftEmit_SpawnType14(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, &d);
            break;
        case 5:
            EftEmit_SpawnType5(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, &d);
            break;
        case 9:
            EftEmit_SpawnType9(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, &d);
            break;
        case 10:
            EftEmit_SpawnType10(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, pos, &d);
            break;
        case 15:
            EftEmit_SpawnType15(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, pos, &d);
            break;
        case 12:
            EftEmit_SpawnType12(set, handles, flags, arg7, objId, idx, size, scale, rate, &p, &d);
            break;
        }
    }
    if (flags & EFT_SPAWN_START) {
        state->flag[n] = (state->flag[n] | EFT_EMIT_STARTED) & ~EFT_EMIT_STOPPED;
    }
    if (flags & EFT_SPAWN_STOP) {
        state->flag[n] |= EFT_EMIT_STOPPED;
    }
    if (state->flag[n] & EFT_EMIT_STARTED) {
        def = &set->parts[n];
        if (flags & EFT_SPAWN_SCALE) {
            if (0.0f < def->scaleTime) {
                f32 total = def->scaleTime * 30.0f;
                f32 first;
                f32 t;
                f32 dd;
                f32 r;

                first = total * def->scaleSplit;
                t = state->time[n] + 1.0f;
                state->time[n] = t;
                if (t < first) {
                    r = t / first;
                    dd = def->scale1 - def->unk1C;
                    state->unk168[n] = def->unk1C + dd * r;
                    if (state->unk168[n] < 0.0f) {
                        state->unk168[n] = 0.0f;
                    }
                } else if (t < total) {
                    r = (t - first) / (total - first);
                    dd = def->scale2 - def->scale1;
                    state->unk168[n] = def->scale1 + dd * r;
                    if (state->unk168[n] < 0.0f) {
                        state->unk168[n] = 0.0f;
                    }
                } else {
                    state->unk168[n] = def->scale2;
                }
            } else {
                state->unk168[n] += def->scaleStep;
                if (def->scale2 != 0.0f) {
                    if (0.0f < def->scaleStep) {
                        if (def->scale2 < state->unk168[n]) {
                            state->unk168[n] = def->scale2;
                        }
                    } else if (def->scaleStep < 0.0f) {
                        if (state->unk168[n] < def->scale2) {
                            state->unk168[n] = def->scale2;
                        }
                        if (state->unk168[n] < 0.0f) {
                            state->unk168[n] = 0.0f;
                        }
                    }
                }
            }
        }
    }
}

/* Flags (0x20, "use the alternative node") the emitters whose condition bit is among the fighter's requests. */
void EftEmit_MarkCond(s32 objId, s32 extra, EftSet *set, EftSetState *state) {
    u32 mask;
    s32 half;
    s32 type;
    s32 i;
    EftSetHead *hdr;

    mask = EftShot_TestBits(objId, 2) != 0;
    if (EftShot_TestBits(objId, 4)) {
        mask |= 2;
    }
    if (EftShot_TestBits(objId, 0x10)) {
        mask |= 4;
    }
    if (EftShot_TestBits(objId, 0x20)) {
        mask |= 8;
    }
    if (EftShot_TestBits(objId, 0x40)) {
        mask |= 0x10;
    }
    if (extra) {
        mask |= 0x20;
    }
    hdr = set->head;
    for (half = 0; half < 2; half++) {
        for (type = 0; type < EFT_EMIT_TYPES; type++) {
            if (hdr->mask & (1 << type)) {
                EftSetGroup *grp = &set->group[type];
                EftSetGroupDef *gd = grp->entry;

                for (i = 0; i < gd->nParts; i++) {
                    s32 n = grp->firstPart + i;
                    EftSetDef *def = &set->parts[n];

                    if ((def->unk34 & 4) && (mask & (1 << def->unk35))) {
                        state->flag[n] |= EFT_EMIT_ALTNODE;
                    }
                }
            }
        }
    }
}

/* Flags (0x40) the kind 6 emitters that have neither been started nor stopped. */
void EftEmit_MarkKind6(EftSet *set, EftSetState *state) {
    s32 type;
    s32 i;
    EftSetHead *hdr = set->head;

    for (type = 0; type < EFT_EMIT_TYPES; type++) {
        if (hdr->mask & (1 << type)) {
            EftSetGroup *grp = &set->group[type];
            EftSetGroupDef *gd = grp->entry;

            for (i = 0; i < gd->nParts; i++) {
                s32 n = grp->firstPart + i;
                EftSetDef *def = &set->parts[n];

                if (!(state->flag[n] & EFT_EMIT_STARTED)) {
                    s32 stopped = state->flag[n] & EFT_EMIT_STOPPED;

                    if (!stopped && def->phase == 6) {
                        state->flag[n] |= EFT_EMIT_KIND6;
                    }
                }
            }
        }
    }
}

/* Destroys every particle object the set has started (both halves) and clears the slots. */
void EftEmit_KillAll(EftSet *set, EftSetState *state) {
    s32 half;
    s32 type;
    s32 i;
    EftSetHead *hdr = set->head;

    for (half = 0; half < 2; half++) {
        EftSetHandles *handles = &state->handle[half];

        for (type = 0; type < EFT_EMIT_TYPES; type++) {
            if (hdr->mask & (1 << type)) {
                EftSetGroup *grp = &set->group[type];
                EftSetGroupDef *gd = grp->entry;

                for (i = 0; i < gd->nParts; i++) {
                    s32 n = grp->firstPart + i;

                    switch (type) {
                    case 0:
                        if (handles->h[n] != NULL) {
                            EftRay_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 2:
                        if (handles->h[n] != NULL) {
                            EftRays_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 16:
                        if (handles->h[n] != NULL) {
                            EftBill_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 17:
                        if (handles->h[n] != NULL) {
                            EftRibbon_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 18:
                        if (handles->h[n] != NULL) {
                            EftChain_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 14:
                        if (handles->h[n] != NULL) {
                            EftAnimPart_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 5:
                        if (handles->h[n] != NULL) {
                            EftPtcl_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 9:
                        if (handles->h[n] != NULL) {
                            EftQuad_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 10:
                        if (handles->h[n] != NULL) {
                            EftPart10_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 15:
                        if (handles->h[n] != NULL) {
                            EftLink_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    case 12:
                        if (handles->h[n] != NULL) {
                            EftZap_Kill(handles->h[n]);
                            handles->h[n] = NULL;
                        }
                        break;
                    }
                }
            }
        }
    }
}

/* Clears the slots whose particle object has ended; returns the groups that still have a live object of an
   emitter that is not a one-shot (kind 2). */
s32 EftEmit_UpdateAlive(EftSet *set, EftSetState *state) {
    s32 result = 0;
    EftSetHead *hdr = set->head;
    s32 half;
    s32 type;
    s32 i;

    for (half = 0; half < 2; half++) {
        EftSetHandles *handles = &state->handle[half];

        for (type = 0; type < EFT_EMIT_TYPES; type++) {
            if (hdr->mask & (1 << type)) {
                EftSetGroup *grp = &set->group[type];
                EftSetGroupDef *gd = grp->entry;

                for (i = 0; i < gd->nParts; i++) {
                    s32 n = grp->firstPart + i;
                    EftSetDef *def = &set->parts[n];

                    switch (type) {
                    case 0:
                        if (EftRay_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 2:
                        if (EftRays_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 16:
                        if (EftBill_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 17:
                        if (EftRibbon_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 18:
                        if (EftChain_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 14:
                        if (EftAnimPart_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 5:
                        if (EftPtcl_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 9:
                        if (EftQuad_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 10:
                        if (EftPart10_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 15:
                        if (EftLink_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    case 12:
                        if (EftZap_IsAlive(handles->h[n])) {
                            if (def->phase != 2) {
                                result |= 1 << type;
                            }
                        } else {
                            handles->h[n] = NULL;
                        }
                        break;
                    }
                }
            }
        }
    }
    return result;
}

/* Builds the mask of the fighter's active effect requests and updates the node slots with it. */
void EftEmit_UpdateNodesReq(EftHSlot *owner, EftEmitNodes *nodes) {
    s32 objId = owner->arg.chr;
    s32 req;

    req = EftShot_TestBits(objId, 2) ? 2 : 0;
    if (EftShot_TestBits(objId, 4)) {
        req |= 4;
    }
    if (EftShot_TestBits(objId, 8)) {
        req |= 8;
    }
    if (EftShot_TestBits(objId, 0x10)) {
        req |= 0x10;
    }
    if (EftShot_TestBits(objId, 0x20)) {
        req |= 0x20;
    }
    if (EftShot_TestBits(objId, 0x40)) {
        req |= 0x40;
    }
    if (EftShot_TestBits(objId, 0x400)) {
        req |= 0x400;
    }
    EftEmit_UpdateNodes(owner, nodes, req);
}

/* Resolves each node slot the first time its request bit is seen, then samples the node positions.
   Request 0x400 freezes the slots (flag 0x1000) for good. */
void EftEmit_UpdateNodes(EftHSlot *owner, EftEmitNodes *nodes, s32 req) {
    s32 bits[EFT_EMIT_NODES] = { 2, 4, 8, 0x10, 0x20, 0x40 };
    s32 objId = owner->arg.chr;
    s32 i;

    if (req & 0x400) {
        nodes->flags |= 0x1000;
    }
    if (nodes->flags & 0x1000) {
        return;
    }
    if (owner->param->flags & 4) {
        for (i = 0; i < EFT_EMIT_NODES; i++) {
            if (!(nodes->flags & (1 << (i * 2)))) {
                if (req & bits[i]) {
                    nodes->flags |= 1 << (i * 2);
                    nodes->n[i][0].id = owner->param->unkE[i];
                }
            }
            if (nodes->flags & (1 << (i * 2))) {
                BtlCharApi_GetNodePos(objId, nodes->n[i][0].id, &nodes->n[i][0].pos);
            } else if (nodes->flags & 1) {
                BtlCharApi_GetNodePos(objId, nodes->n[0][0].id, &nodes->n[i][0].pos);
            }
            if (nodes->flags & (1 << (i * 2 + 1))) {
                BtlCharApi_GetNodePos(objId, nodes->n[i][1].id, &nodes->n[i][1].pos);
            } else if (nodes->flags & 1) {
                BtlCharApi_GetNodePos(objId, nodes->n[0][0].id, &nodes->n[i][1].pos);
            }
        }
    } else {
        for (i = 0; i < EFT_EMIT_NODES; i++) {
            if (!(nodes->flags & (1 << (i * 2)))) {
                if (req & bits[i]) {
                    nodes->flags |= 1 << (i * 2);
                    if (EftShot_HasTwoAttrs(objId, bits[i])) {
                        nodes->flags |= 1 << (i * 2 + 1);
                        EftShot_GetAttrPair(objId, bits[i], &nodes->n[i][0].id, &nodes->n[i][1].id);
                    } else {
                        nodes->n[i][0].id = EftShot_GetAttrKind(objId, bits[i]);
                    }
                }
            }
            if (nodes->flags & (1 << (i * 2))) {
                BtlCharApi_GetNodePos(objId, nodes->n[i][0].id, &nodes->n[i][0].pos);
            } else if (nodes->flags & 1) {
                BtlCharApi_GetNodePos(objId, nodes->n[0][0].id, &nodes->n[i][0].pos);
            }
            if (nodes->flags & (1 << (i * 2 + 1))) {
                BtlCharApi_GetNodePos(objId, nodes->n[i][1].id, &nodes->n[i][1].pos);
            } else if (nodes->flags & 1) {
                BtlCharApi_GetNodePos(objId, nodes->n[0][0].id, &nodes->n[i][1].pos);
            }
            if (owner->param->flags & 8) {
                if (!(nodes->flagsC & (1 << i))) {
                    if (req & bits[i]) {
                        nodes->flagsC |= 1 << i;
                        nodes->flags |= 1 << (i * 2);
                        nodes->c[i].id = owner->param->unkE[i];
                    }
                }
                if (nodes->flagsC & (1 << i)) {
                    BtlCharApi_GetNodePos(objId, nodes->c[i].id, &nodes->c[i].pos);
                } else if (nodes->flagsC & 1) {
                    BtlCharApi_GetNodePos(objId, nodes->c[0].id, &nodes->c[i].pos);
                }
            }
        }
    }
}

/* Marks a slot resolved: fixed to a fighter node, or (node < 0) to a position. */
void EftEmit_SetNode(EftEmitNodes *nodes, s32 slot, s32 node, Vec4 *pos) {
    nodes->flags |= 1 << (slot * 2);
    if (node >= 0) {
        nodes->fixed |= 1 << (slot * 2);
        nodes->n[slot][0].id = node;
    } else {
        Vec4_Copy(&nodes->n[slot][0].pos, pos);
    }
}

/* Overwrites the position of a slot. */
void EftEmit_SetNodePos(EftEmitNodes *nodes, s32 slot, Vec4 *pos) {
    Vec4_Copy(&nodes->n[slot][0].pos, pos);
}

/* Samples again the slots that EftEmit_SetNode fixed to a fighter node. */
void EftEmit_RefreshFixedNodes(s32 objId, EftEmitNodes *nodes) {
    s32 i;

    for (i = 0; i < EFT_EMIT_NODES; i++) {
        s32 bit = 1 << (i * 2);

        if ((nodes->flags & bit) && (nodes->fixed & bit)) {
            BtlCharApi_GetNodePos(objId, nodes->n[i][0].id, &nodes->n[i][0].pos);
        }
    }
}

/* Advances the trail width animation of the set header by one frame. */
void EftEmit_UpdateTrailWidth(EftSet *set, EftSetState *state) {
    EftSetHead *hdr = set->head;

    if (0.0f < hdr->unk14) {
        f32 total = hdr->unk14 * 30.0f;
        f32 first;
        f32 t;
        f32 d;
        f32 r;

        first = total * hdr->unk18;
        t = state->unk2B4 + 1.0f;
        state->unk2B4 = t;
        if (t < first) {
            r = t / first;
            d = hdr->unkC[0] - hdr->unk8;
            state->unk2A8 = hdr->unk8 + d * r;
            if (state->unk2A8 < 0.0f) {
                state->unk2A8 = 0.0f;
            }
        } else if (t < total) {
            r = (t - first) / (total - first);
            d = hdr->unkC[1] - hdr->unkC[0];
            state->unk2A8 = hdr->unkC[0] + d * r;
            if (state->unk2A8 < 0.0f) {
                state->unk2A8 = 0.0f;
            }
        } else {
            state->unk2A8 = hdr->unkC[1];
        }
    } else {
        state->unk2A8 += hdr->unk1C;
        if (0.0f < hdr->unkC[1] && hdr->unkC[1] < state->unk2A8) {
            state->unk2A8 = hdr->unkC[1];
        }
    }
}

/* Current trail width factor. */
f32 EftEmit_GetTrailWidth(EftSetState *state) {
    return state->unk2A8;
}

/* Does the set header use the second animation. */
s32 EftEmit_HasWidth2(EftSet *set) {
    if (set->head->unk21 & 4) {
        return 1;
    }
    return 0;
}

/* Advances the second animation of the set header by one frame. */
void EftEmit_UpdateWidth2(EftSet *set, EftSetState *state) {
    EftSetHead *hdr = set->head;

    if (0.0f < hdr->unk38) {
        f32 total = hdr->unk38 * 30.0f;
        f32 first;
        f32 t;
        f32 d;
        f32 r;

        t = state->unk2C4 + 1.0f;
        first = total * hdr->unk3C;
        state->unk2C4 = t;
        if (t < first) {
            r = t / first;
            d = hdr->unk30[0] - hdr->unk2C;
            state->unk2B8 = hdr->unk2C + d * r;
            if (state->unk2B8 < 0.0f) {
                state->unk2B8 = 0.0f;
            }
        } else if (t < total) {
            r = (t - first) / (total - first);
            d = hdr->unk30[1] - hdr->unk30[0];
            state->unk2B8 = hdr->unk30[0] + d * r;
            if (state->unk2B8 < 0.0f) {
                state->unk2B8 = 0.0f;
            }
        } else {
            state->unk2B8 = hdr->unk30[1];
        }
    }
}

/* Current value of the second animation. */
f32 EftEmit_GetWidth2(EftSetState *state) {
    return state->unk2B8;
}

/* Drops a mark where the last stage line test hit: 10 frames later the mark effect starts there. The beam end
   is pulled back onto the surface and the stage object that was hit, if any, is destroyed. */
/* (The flag is read into a local that is then set to 1 and stored: a variable set twice is not hoisted out of the
   loop, a plain `active = 1` is.) */
void EftSweep_AddMark(BtlTask *task) {
    Vec4 dir;
    Vec4 v;
    EftSweepWork *w = task->work;
    EftIStageHit *hit = StgCol_GetHit();
    s32 i;

    for (i = 0; i < 15; i++) {
        s32 active = w->mark[i].active;

        if (active == 0) {
            f32 width;

            w->mark[i].timer = 10.0f;
            active = 1;
            w->mark[i].active = active;
            Vec4_Copy((Vec4 *)&w->mark[i].pos, &hit->pos);
            width = w->width * EftEmit_GetTrailWidth(&w->state) * 0.5f;
            Vec3_Sub(&v, &w->pos, (Vec4 *)&w->mark[i].pos);
            Vec3_Normalize(&v, &v);
            Vec3_Scale(&v, &v, width);
            Vec3_Add((Vec4 *)&w->mark[i].pos, (Vec4 *)&w->mark[i].pos, &v);
            w->mark[i].pos.w = 1.0f;
            Vec4_Copy(&w->pose.cur, (Vec4 *)&w->mark[i].pos);
            if (hit->obj >= 0) {
                Vec3_Sub(&dir, &w->pose.cur, &w->pos);
                Vec3_Normalize(&dir, &dir);
                dir.w = 1.0f;
                BtlStage_DestroyObj(-1, hit->obj, &dir);
            }
            return;
        }
    }
}

/* Counts the marks down and starts the mark effect of the ones that reach zero; returns whether any is left. */
/* (The argument is a compound literal, as in EftHit_SpawnBlastImpact, and the address of the mark's position is
   taken before the `active` test: that decides which address the loop's walking pointer holds, +0x10 here.) */
s32 EftSweep_UpdateMarks(BtlTask *task) {
    EftIMarkArg arg;
    s32 any = 0;
    EftSweepWork *w = task->work;
    EftHSlot *owner = w->owner;
    s32 i;

    for (i = 0; i < 15; i++) {
        EftIVecU *pos = (EftIVecU *)&w->mark[i].pos;

        if (w->mark[i].active) {
            if (w->mark[i].timer <= 0.0f) {
                s32 effect = w->owner->param->unk42;

                if (effect >= 0) {
                    f32 size;

                    arg = (EftIMarkArg){ *pos, { 0.0f, 0.0f, 0.0f, 0.0f }, effect, owner->arg.chr, 0 };
                    size = owner->param->unk54;
                    Vec4_Sub((Vec4 *)&arg.dir, &w->pose.cur, &w->pos);
                    Vec3_Normalize((Vec4 *)&arg.dir, (Vec4 *)&arg.dir);
                    EftImpact_SpawnBlast(arg, size, 1.0f);
                }
                w->mark[i].active = 0;
            } else {
                any = 1;
                w->mark[i].timer -= 1.0f;
            }
        }
    }
    return any;
}

/* Radius of the sweep (distance between the fighters' node 3, plus 50, at most 800) and its start angle. */
void EftSweep_InitPath(BtlTask *task) {
    Vec4 b;
    Vec4 a;
    EftSweepWork *w = task->work;
    EftHSlot *owner = w->owner;
    s32 opp = BtlCharApi_GetOpponentObjId(owner->arg.chr);

    BtlCharApi_GetNodePos(owner->arg.chr, 3, &a);
    BtlCharApi_GetNodePos(opp, 3, &b);
    Vec4_Sub(&a, &b, &a);
    w->radius = sqrtf(Vec3_Dot(&a, &a)) + 50.0f;
    if (800.0f < w->radius) {
        w->radius = 800.0f;
    }
    w->angleStep = EFT_SWEEP_STEP;
    w->angle = EFT_SWEEP_START;
}

/* Moves the beam end one step along its circle around pos, in the plane given by dir. */
void EftSweep_Move(BtlTask *task) {
    Mtx44 m;
    Mtx44 m2;
    Vec4 a;
    Vec4 b;
    EftSweepWork *w = task->work;
    EftHSlot *owner = w->owner;
    Vec4 *cur = &w->pose.cur;
    s32 opp = BtlCharApi_GetOpponentObjId(owner->arg.chr);
    f32 pitch;
    f32 yaw;

    BtlCharApi_GetNodePos(owner->arg.chr, 3, &a);
    BtlCharApi_GetNodePos(opp, 3, &b);
    Vec4_Sub(&a, &b, &a);
    w->radius = sqrtf(Vec3_Dot(&a, &a)) + 50.0f;
    if (800.0f < w->radius) {
        w->radius = 800.0f;
    }
    Vec4_Set(cur, 0.0f, 0.0f, w->radius, 1.0f);
    Mtx_StoreIdentity(&m);
    Mtx_RotateY(&m, &m, w->angle);
    Mtx_MulVec4(cur, &m, cur);
    pitch = asinf(-w->dir.y);
    yaw = atan2f(w->dir.x, w->dir.z);
    Mtx_RotateX(&m2, &m, pitch);
    Mtx_RotateY(&m2, &m2, yaw);
    Mtx_MulVec4(cur, &m2, cur);
    if (!(w->flags & EFT_SWEEP_PREV_VALID)) {
        Vec3_Add(&w->pose.prev, cur, &w->pos);
        w->flags |= EFT_SWEEP_PREV_VALID;
    }
    Vec3_Add(cur, cur, &w->pos);
    w->angle += w->angleStep;
    if (3.14159265f < w->angle) {
        w->angle -= 6.2831853f;
    }
}

/* Adds this frame's hit record: two spheres at the beam end now and last frame, or two boxes from the beam
   origin (node slot 1) to those points, by the technique's hit shape. */
void EftSweep_AddHit(BtlTask *task) {
    EftSweepWork *w = task->work;
    EftIHitRec *rec = EftHit_GetNew();
    f32 width = w->width * EftEmit_GetTrailWidth(&w->state);

    rec->pose = w->pose;
    rec->task = task;
    rec->owner = w->owner;
    rec->flags |= 0x40;
    switch (w->owner->param->unk8) {
    case 1: {
        void *a = EftHitArena_AllocBox();
        void *b = EftHitArena_AllocBox();

        ColCapsule_Set(a, &w->nodes.n[1][0].pos, &w->pose.cur, width);
        ColCapsule_Set(b, &w->nodes.n[1][0].pos, &w->pose.prev, width);
        EftHit_SetShapeBoxes(rec, a, b);
        break;
    }
    case 0: {
        void *a = EftHitArena_AllocSphere();
        void *b = EftHitArena_AllocSphere();

        ColSphere_Set(a, &w->pose.cur, width);
        ColSphere_Set(b, &w->pose.prev, width);
        EftHit_SetShapeSpheres(rec, a, b);
        break;
    }
    default:
        return;
    }
    EftHit_Add(rec);
}

/* Gives every part of the effect pack its command for this frame. */
void EftSweep_Emit(s32 objId, BtlTask *task, EftSet *set, s32 reset) {
    EftSweepWork *w = task->work;
    s32 type;
    s32 i;

    for (type = 0; type < EFT_EMIT_TYPES; type++) {
        if (set->head->mask & (1 << type)) {
            EftSetGroupDef *gd = set->group[type].entry;

            for (i = 0; i < gd->nParts; i++) {
                s32 flags;

                if (reset == 0) {
                    flags = EftEmit_GetFlagsFromReq(set, &w->state, objId, type, i, w->flags & EFT_SWEEP_ENDING,
                                          w->flags & EFT_SWEEP_NOW);
                } else {
                    flags = EftEmit_GetResetFlags(set, &w->state, type, i);
                }
                if (w->flags & EFT_SWEEP_KILLED) {
                    flags = EFT_SPAWN_STOP;
                }
                if (flags) {
                    EftEmit_SpawnOwn(set, &w->state, &w->nodes, &w->pose.cur, &w->dir, type, i, flags, w->scale);
                }
            }
        }
    }
}

/* Instance init: clears the work, takes the technique's scale and speed, attaches the group's effect pack. */
void EftSweep_Init(BtlTask *task, EftHSlot *owner) {
    BtlTask *group = BtlTask_GetParent(task);
    EftSweepWork *w = task->work;
    EftSet *set = &((EftSetWork *)group->work)->set;
    EftShotParam *p;

    memset(w, 0, sizeof(EftSweepWork));
    w->owner = owner;
    p = owner->param;
    w->speed = p->unk34;
    w->unk784 = p->unk30;
    w->scale = p->unk30;
    w->width = w->unk784;
    w->set = set;
    EftEmit_InitState(set, &w->state);
    if (EftEmit_HasWidth2(w->set)) {
        w->speed = EftEmit_GetWidth2(&w->state);
        w->flags |= EFT_SWEEP_OWN_WIDTH;
    }
    w->life = EftEmit_GetEndFrames(w->set);
    EftSweep_InitPath(task);
    BtlTask_SetOwnerTag(task, owner->arg.chr == 0 ? 0x800 : 0x1000);
}

/* Instance term. */
void EftSweep_Term(BtlTask *task) {
    EftSweepWork *w = task->work;
    EftHSlot *owner = w->owner;

    EftEmit_TermState(w->set, &w->state);
    if (owner->param->kind != 0) {
        EftShot_SetHeldFlagA8(owner->arg.chr);
    }
}

/* Instance update: follows the fighter's effect events (2 aim, 4 fire, 8 stop, 0x400 end), sweeps the beam,
   marks the stage, drives the effect pack and adds the hit record. */
void EftSweep_Update(BtlTask *task) {
    EftISegment seg;
    EftSweepWork *w = task->work;
    EftHSlot *owner = w->owner;
    s32 next = 0;
    s32 any;

    if (BtlScene_IsCharStopped(owner->arg.chr)) {
        return;
    }
    EftEmit_UpdateNodesReq(owner, &w->nodes);
    if (!(w->flags & EFT_SWEEP_STARTED)) {
        if (EftShot_TestBits(owner->arg.chr, 2)) {
            EftAim_GetDir(&w->dir, &w->nodes.n[0][0].pos, owner->arg.chr);
            task->chr = 0;
            w->flags |= EFT_SWEEP_STARTED;
        } else if (EftShot_TestBits(owner->arg.chr, 4)) {
            task->chr = 0;
            w->flags |= EFT_SWEEP_STARTED;
        }
    }
    if (w->flags & EFT_SWEEP_STARTED) {
        switch (task->chr) {
        case 0:
            if (EftShot_TestBits(owner->arg.chr, 4)) {
                w->flags |= EFT_SWEEP_TRAIL;
                Vec4_Copy(&w->pos, &w->nodes.n[1][0].pos);
                Vec4_Copy(&w->pose.start, &w->nodes.n[1][0].pos);
                Vec4_Copy(&w->pose.cur, &w->pose.start);
                BtlCharApi_GetNodePos(owner->arg.chr, 0x11, &w->pose.prev);
                EftAim_GetDirKeep(owner, &w->dir, &w->pose.cur, owner->arg.chr);
                Vec3_Scale(&w->pose.vel, &w->dir, w->speed);
                task->chr = 1;
            }
            break;
        case 2:
            Vec4_Copy(&w->pose.prev, &w->pose.cur);
            Vec4_Copy(&w->pos, &w->nodes.n[1][0].pos);
            EftSweep_Move(task);
            ColSeg_Set(&seg, &w->pos, &w->pose.cur);
            if (StgCol_TraceSegment(&seg)) {
                EftSweep_AddMark(task);
            }
            if (EftShot_TestBits(owner->arg.chr, 8)) {
                w->flags |= EFT_SWEEP_STOP_SEEN | EFT_SWEEP_ENDING;
                task->chr = 3;
            }
            break;
        case 1:
        case 3:
            next = 1;
            break;
        }
    }
    if (EftShot_TestBits(owner->arg.chr, 0x400)) {
        if (!(w->flags & EFT_SWEEP_STOP_SEEN)) {
            w->flags |= EFT_SWEEP_NOW;
        }
        w->flags |= EFT_SWEEP_ENDING;
    }
    EftSweep_Emit(owner->arg.chr, task, w->set, 0);
    if (next) {
        task->chr++;
    }
    any = EftSweep_UpdateMarks(task);
    if (w->flags & EFT_SWEEP_ENDING) {
        w->time += 1.0f;
    }
    if (!any && (w->flags & EFT_SWEEP_DEAD)) {
        BtlTask_SetDead(task);
        return;
    }
    if (w->flags & EFT_SWEEP_ENDING) {
        if (!(w->flags & EFT_SWEEP_NOW)) {
            if (!(w->time >= w->life)) {
                return;
            }
        }
        w->flags |= EFT_SWEEP_DEAD;
    } else if (owner->param->flags & 1) {
        if (w->flags & EFT_SWEEP_TRAIL) {
            EftEmit_UpdateTrailWidth(w->set, &w->state);
            EftSweep_AddHit(task);
        }
    }
}

/* Instance post-update: drops the parts that ended; a hit result (task flags 1 / 2) stops the hit record. */
void EftSweep_PostUpdate(BtlTask *task) {
    EftSweepWork *w = task->work;

    if (!BtlScene_IsCharStopped(w->owner->arg.chr)) {
        EftEmit_UpdateAlive(w->set, &w->state);
        if (task->tag & 1) {
            w->flags &= ~EFT_SWEEP_TRAIL;
        } else if (task->tag & 2) {
            w->flags &= ~EFT_SWEEP_TRAIL;
        }
    }
}

/* Instance reset: kills the parts and the task. */
void EftSweep_Reset(BtlTask *task) {
    EftSweepWork *w = task->work;

    if (!(w->flags & EFT_SWEEP_KILLED)) {
        w->flags |= EFT_SWEEP_KILLED;
        EftEmit_KillAll(w->set, &w->state);
    }
    BtlTask_SetDead(task);
}

/* Instance draw: nothing. */
void EftSweep_Draw(BtlTask *task) {
}

/* Group init: loads the technique's effect pack and makes room for two instances. */
void EftSweepGroup_Init(BtlTask *task, EftHSlot *owner) {
    EftSetWork *gw = task->work;

    EftShot_Nop(sizeof(EftSetWork));
    memset(gw, 0, sizeof(EftSetWork));
    EftEmit_LoadSet(owner, &gw->set, 0, owner->pack, 0, 4);
    BtlTask_CreateChildList(task, 2, sizeof(EftSweepWork));
}

/* Group term: frees the pack. */
void EftSweepGroup_Term(BtlTask *task) {
    EftEmit_FreeSet(&((EftSetWork *)task->work)->set);
}

/* Group update. */
void EftSweepGroup_Update(BtlTask *task) {
    EftEmit_BeginFrame(&((EftSetWork *)task->work)->set);
}

/* Group reset: nothing. */
void EftSweepGroup_Reset(BtlTask *task) {
}

/* Adds this frame's hit record: two spheres, at node 3 now and last frame. */
void EftFollow_AddHit(BtlTask *task) {
    EftFollowWork *w = task->work;
    EftIHitRec *rec = EftHit_GetNew();
    f32 width = w->width * EftEmit_GetTrailWidth(&w->state);
    void *a;
    void *b;

    rec->pose = w->pose;
    rec->task = task;
    rec->owner = w->owner;
    rec->flags |= 0x40;
    a = EftHitArena_AllocSphere();
    b = EftHitArena_AllocSphere();
    ColSphere_Set(a, &w->pose.cur, width);
    ColSphere_Set(b, &w->pose.prev, width);
    EftHit_SetShapeSpheres(rec, a, b);
    EftHit_Add(rec);
}

/* Stage blur light of the technique: fades in from the "on" event, cut by the "off" event. */
void EftFollow_UpdateLight(BtlTask *task) {
    Vec4 pos;
    EftFollowWork *w = task->work;
    EftHSlot *owner = w->owner;

    if (owner->param->unk48 != 2) {
        s32 bits[EFT_EMIT_NODES] = { 2, 4, 8, 0x10, 0x20, 0x40 };
        s32 off = bits[owner->param->unk4A];
        s32 a0;
        s32 a1;
        s32 a2;

        if (EftShot_TestBits(owner->arg.chr, bits[owner->param->unk48])) {
            w->flags |= EFT_FOLLOW_LIGHT;
            BtlCharApi_GetNodePos(owner->arg.chr, 0x11, &pos);
            EftAim_GetDir(&w->dir, &pos, owner->arg.chr);
        }
        if (EftShot_TestBits(owner->arg.chr, off)) {
            w->light = 0.0f;
            w->flags &= ~EFT_FOLLOW_LIGHT;
        }
        if (w->flags & EFT_FOLLOW_LIGHT) {
            w->light += 0.1f;
            if (1.0f < w->light) {
                w->light = 1.0f;
            }
        }
        a0 = w->light * 32.0f;
        a1 = w->light * 64.0f;
        a2 = w->light * 128.0f;
        StgBlur_SetColor0Rgba(0, 0x80, 0x80, 0x80, a0 & 0xFF);
        StgBlur_SetColor1Rgba(0, 0x80, 0x80, 0x80, a1 & 0xFF);
        StgBlur_SetColor2Rgba(0, 0x80, 0x80, 0x80, a2 & 0xFF);
        StgBlur_SetColor3Rgba(0, 0x80, 0x80, 0x80, 0x40);
        StgBlur_SetCenter(0, &w->dir, 0);
    }
}

/* Gives every part of the effect pack its command for this frame. */
void EftFollow_Emit(s32 objId, BtlTask *task, EftSet *set, s32 reset) {
    EftFollowWork *w = task->work;
    s32 type;
    s32 i;

    for (type = 0; type < EFT_EMIT_TYPES; type++) {
        if (set->head->mask & (1 << type)) {
            EftSetGroupDef *gd = set->group[type].entry;

            for (i = 0; i < gd->nParts; i++) {
                s32 flags;

                if (reset == 0) {
                    flags = EftEmit_GetFlagsFromReq(set, &w->state, objId, type, i, w->flags & EFT_FOLLOW_ENDING,
                                          w->flags & EFT_FOLLOW_NOW);
                } else {
                    flags = EftEmit_GetResetFlags(set, &w->state, type, i);
                }
                if (w->flags & EFT_FOLLOW_KILLED) {
                    flags = EFT_SPAWN_STOP;
                }
                if (flags) {
                    EftEmit_SpawnOwn(set, &w->state, &w->nodes, &w->pose.cur, &w->dir, type, i, flags, w->scale);
                }
            }
        }
    }
}

/* Instance init. */
void EftFollow_Init(BtlTask *task, EftHSlot *owner) {
    BtlTask *group = BtlTask_GetParent(task);
    EftFollowWork *w = task->work;
    EftSet *set = &((EftSetWork *)group->work)->set;
    EftShotParam *p;

    memset(w, 0, sizeof(EftFollowWork));
    w->owner = owner;
    p = owner->param;
    w->unk4 = 0.0f;
    w->unk8 = p->unk30;
    w->scale = p->unk30;
    w->width = w->unk8;
    w->set = set;
    EftEmit_InitState(set, &w->state);
    if (EftEmit_HasWidth2(w->set)) {
        w->unk4 = EftEmit_GetWidth2(&w->state);
        w->flags |= EFT_FOLLOW_OWN_WIDTH;
    }
    w->life = EftEmit_GetEndFrames(w->set);
    BtlTask_SetOwnerTag(task, owner->arg.chr == 0 ? 0x800 : 0x1000);
}

/* Instance term. */
void EftFollow_Term(BtlTask *task) {
    EftFollowWork *w = task->work;
    EftHSlot *owner = w->owner;

    EftEmit_TermState(w->set, &w->state);
    EftShot_SetHeldFlagA8(owner->arg.chr);
}

/* Instance update: follows the fighter's effect events (2 / 4 start, 4 fire, 8 stop, 0x400 end), tracks node 3,
   drives the effect pack and the stage light, adds the hit record. */
void EftFollow_Update(BtlTask *task) {
    Vec4 pos;
    EftFollowWork *w = task->work;
    EftHSlot *owner = w->owner;
    s32 next = 0;
    s32 flags;

    if (BtlScene_IsCharStopped(owner->arg.chr)) {
        return;
    }
    EftEmit_UpdateNodesReq(owner, &w->nodes);
    if (!(w->flags & EFT_FOLLOW_STARTED)) {
        if (EftShot_TestBits(owner->arg.chr, 2) || EftShot_TestBits(owner->arg.chr, 4)) {
            task->chr = 0;
            w->flags |= EFT_FOLLOW_STARTED;
        }
    }
    if (w->flags & EFT_FOLLOW_STARTED) {
        switch (task->chr) {
        case 0:
            if (EftShot_TestBits(owner->arg.chr, 4)) {
                w->flags |= EFT_FOLLOW_TRAIL;
                BtlCharApi_GetNodePos(owner->arg.chr, 3, &w->pose.start);
                Vec4_Copy(&w->pose.cur, &w->pose.start);
                BtlCharApi_GetNodePos(owner->arg.chr, 0x11, &w->pose.prev);
                task->chr = 1;
            }
            break;
        case 1:
        case 3:
            next = 1;
            break;
        case 2:
            if (EftShot_TestBits(owner->arg.chr, 8)) {
                w->flags |= EFT_FOLLOW_STOP_SEEN | EFT_FOLLOW_ENDING;
                task->chr = 3;
            }
            break;
        }
    }
    if (EftShot_TestBits(owner->arg.chr, 0x400)) {
        if (!(w->flags & EFT_FOLLOW_STOP_SEEN)) {
            w->flags |= EFT_FOLLOW_NOW;
        }
        w->flags |= EFT_FOLLOW_ENDING;
    }
    BtlCharApi_GetNodePos(owner->arg.chr, 0x2E, &w->dir);
    BtlCharApi_GetNodePos(owner->arg.chr, 3, &pos);
    Vec3_Sub(&w->dir, &pos, &w->dir);
    w->dir.w = 1.0f;
    Vec3_Normalize(&w->dir, &w->dir);
    Vec4_Copy(&w->pose.prev, &w->pose.cur);
    BtlCharApi_GetNodePos(owner->arg.chr, 3, &w->pose.cur);
    EftFollow_Emit(owner->arg.chr, task, w->set, 0);
    if (owner->param->flags & 0x200) {
        EftFollow_UpdateLight(task);
    }
    if (next) {
        task->chr++;
    }
    flags = w->flags;
    if (flags & EFT_FOLLOW_ENDING) {
        w->time += 1.0f;
    }
    if (flags & EFT_FOLLOW_DEAD) {
        BtlTask_SetDead(task);
        return;
    }
    if (flags & EFT_FOLLOW_ENDING) {
        if (!(flags & EFT_FOLLOW_NOW)) {
            if (!(w->time >= w->life)) {
                return;
            }
        }
        w->flags = flags | EFT_FOLLOW_DEAD;
    } else if (flags & EFT_FOLLOW_TRAIL) {
        EftEmit_UpdateTrailWidth(w->set, &w->state);
        EftFollow_AddHit(task);
    }
}

/* Instance post-update: drops the parts that ended; a hit result (task flags 1 / 2) stops the hit record, and
   on result 1 the kind 6 parts are flagged. */
void EftFollow_PostUpdate(BtlTask *task) {
    EftFollowWork *w = task->work;
    EftSetState *state = &w->state;

    if (!BtlScene_IsCharStopped(w->owner->arg.chr)) {
        EftEmit_UpdateAlive(w->set, state);
        if (task->tag & 1) {
            EftEmit_MarkKind6(w->set, state);
            w->flags &= ~EFT_FOLLOW_TRAIL;
        } else if (task->tag & 2) {
            w->flags &= ~EFT_FOLLOW_TRAIL;
        }
    }
}
