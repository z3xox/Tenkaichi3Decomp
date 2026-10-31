#ifndef BATTLE_EFT_RAY_TYPES_H
#define BATTLE_EFT_RAY_TYPES_H

#include "sys/vector_types.h"
#include "sys/color.h"

/* Shared creation record of the ray-burst owner and its callers. */
typedef struct EftRayArg {
    /* 0x00 */ Vec4Aligned pos;
    /* 0x10 */ Color4i color;      /* r, g, b, alpha; each ray's alpha is alpha - rand() % 16 */
    /* 0x20 */ f32 life;          /* seconds before the fade starts; <= 0: no timed fade */
    /* 0x24 */ f32 length;        /* ray length; + up to 20 at random */
    /* 0x28 */ f32 width;         /* ray width factor (times up to 5 at random, at least 0.4) */
    /* 0x2C */ f32 inner;         /* distance of the rays' near end from the centre */
    /* 0x30 */ f32 jitter;        /* random extra distance per ray */
    /* 0x34 */ s32 mode;          /* EFT_RAY_MODE_* */
    /* 0x38 */ u32 count;         /* rays, at most 48 */
    /* 0x3C */ s32 objId;         /* owner; negative = none */
    /* 0x40 */ s32 blend;         /* passed to the quad queue */
    /* 0x44 */ s32 space;         /* 0: in the world, facing the camera; 1: on the screen */
    /* 0x48 */ s32 delay;         /* frames before anything happens */
    /* 0x4C */ s32 fadeFrames;    /* frames the final fade takes */
    /* 0x50 */ s32 autoKill;      /* non-zero: the task ends itself when the fade is over */
} EftRayArg; /* size 0x60, including natural trailing alignment */

#endif
