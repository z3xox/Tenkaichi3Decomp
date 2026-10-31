#ifndef SYS_TEXTURE_TYPES_H
#define SYS_TEXTURE_TYPES_H
#include "types.h"

typedef struct TexEntry {
    /* 0x00 */ u32 pixOfs;   /* pixel packet, in words from the file's base; 0 = none */
    /* 0x04 */ u32 clutOfs;  /* CLUT packet, same */
    /* 0x08 */ s32 pixSize;  /* bytes */
    /* 0x0C */ s32 clutSize;
    /* 0x10 */ s32 tbpStep;  /* GS blocks the pixels take */
    /* 0x14 */ s32 cbpStep;  /* GS blocks the CLUT takes */
    /* 0x18 */ u32 pixBlt;   /* bits 48..63 of BITBLTBUF for the pixels (DBW, DPSM) */
    /* 0x1C */ u32 clutBlt;  /* the same for the CLUT */
    /* 0x20 */ s32 tbpOfs;   /* block offset of the pixels from the file's first texture */
    /* 0x24 */ s32 cbpOfs;
    /* 0x28 */ u8 unk28[8];
    /* 0x30 */ u64 tex0;     /* TEX0 without the block pointers */
    /* 0x38 */ void *pix;    /* set by Res_RelocateOffsets */
    /* 0x3C */ void *clut;
} TexEntry;

#endif
