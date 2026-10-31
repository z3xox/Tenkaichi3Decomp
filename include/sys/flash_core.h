#ifndef SYS_FLASH_CORE_H
#define SYS_FLASH_CORE_H

#include "types.h"

/* Movie instance, label handle, transform and texture rectangle shared by UI consumers. */
typedef struct FlashMtx {
    f32 m[9];
} FlashMtx;

typedef struct FlashUv {
    /* 0x00 */ s32 u0;
    /* 0x04 */ s32 v0;
    /* 0x08 */ s32 u1;
    /* 0x0C */ s32 v1;
    /* 0x10 */ s32 tex;
} FlashUv;

typedef struct FlashRef {
    /* 0x00 */ s32 index;        /* -1 when not found */
    /* 0x04 */ s32 more;
} FlashRef;

typedef struct Flash {
    /* 0x00 */ u8 *data;         /* the tag list (file + 8) */
    /* 0x04 */ struct FlashTex **tex;   /* one per image record (tag 3); NULL: quads using it are not drawn */
    /* 0x08 */ u32 flags;        /* FLASH_ */
    /* 0x0C */ u32 trig;         /* bit n set by the action url "trig" "n" this tick */
    /* 0x10 */ u32 se;           /* bit n set by the action url "se" "n" this tick */
    /* 0x14 */ s32 speed;        /* frames per Flash_Advance */
    /* 0x18 */ s32 ofs[2];       /* screen offset added to every quad */
    /* 0x20 */ struct FlashTl *root;
    /* 0x24 */ struct FlashClipList *clips;
    /* 0x28 */ struct FlashShapePool *shapes;
} Flash;

#endif
