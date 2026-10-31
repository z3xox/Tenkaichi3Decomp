#ifndef SYS_VECTOR_TYPES_H
#define SYS_VECTOR_TYPES_H

#include "sys/math3d.h"
#include "sys/int128.h"

/* Array and aligned record values retain distinct original argument/initializer representations. */
typedef f32 FVec4[4] __attribute__((aligned(16)));
typedef s32 IVec4Array[4] __attribute__((aligned(16)));
typedef Vec4 Vec4Aligned __attribute__((aligned(16)));

/* Floating components and the EE quadword representation used by stage emitter records. */
typedef union Vec4Packed {
    struct {
        f32 x, y, z, w;
    };
    u128 q;
} __attribute__((aligned(16))) Vec4Packed;

#endif
