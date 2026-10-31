#ifndef BATTLE_EFT_STAGE_EMIT_H
#define BATTLE_EFT_STAGE_EMIT_H

#include "sys/vector_types.h"

/* Shared creation records of the stage smoke and steam emitters. */
typedef struct EftSmokeArg {
    /* 0x00 */ Vec4Packed pos;
    /* 0x10 */ Vec4Packed ambient;
    /* 0x20 */ Vec4Packed diffuse;
    /* 0x30 */ f32 size;
    /* 0x34 */ f32 alpha;
    /* 0x38 */ f32 speed;
    /* 0x3C */ f32 damp;
    /* 0x40 */ s32 lifeBase;
    /* 0x44 */ s32 lifeRange;
    /* 0x48 */ s32 rate;
} EftSmokeArg; /* size0x50, alignment16 */

typedef struct EftSteamArg {
    /* 0x00 */ Vec4Packed pos;
    /* 0x10 */ Vec4Packed dir;
    /* 0x20 */ Vec4Packed color;
    /* 0x30 */ f32 speed;
    /* 0x34 */ f32 accel;
    /* 0x38 */ f32 size;
    /* 0x3C */ s32 life;
    /* 0x40 */ f32 posRange;
    /* 0x44 */ f32 dirRange;
    /* 0x48 */ f32 speedRange;
    /* 0x4C */ f32 sizeRange;
    /* 0x50 */ s32 lifeRange;
} EftSteamArg; /* size0x60, alignment16 */

typedef struct EftSmokeSrc {
    /* 0x00 */ f32 pos[3];
    /* 0x0C */ f32 damp;
    /* 0x10 */ f32 size;
    /* 0x14 */ f32 speed;
    /* 0x18 */ u16 lifeBase;
    /* 0x1A */ u16 rate;
    /* 0x1C */ u8 ambient[4];
    /* 0x20 */ u8 diffuse[4];
    /* 0x24 */ u8 unk24[0xC];
} EftSmokeSrc; /* 0x30 */

typedef struct EftStageMarker {
    /* 0x00 */ f32 pos[3];
    /* 0x0C */ f32 speed;
    /* 0x10 */ f32 accel;
    /* 0x14 */ f32 size;
    /* 0x18 */ u16 life;
    /* 0x1A */ u8 color[4];
    /* 0x1E */ u8 unk1E[0x12];
} EftStageMarker; /* size 0x30 */

struct EftSteamTask;
void EftSteam_SetPaused(struct EftSteamTask *task, s32 paused);

#endif
