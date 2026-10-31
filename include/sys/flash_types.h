#ifndef SYS_FLASH_TYPES_H
#define SYS_FLASH_TYPES_H
#include "types.h"
#include "sys/flash_core.h"
#include "sys/texture_types.h"

struct Flash;
struct FlashClip;

/* The textures of one image: the entry table of a texture file (see TexEntry). Entry n carries palette n; when
   it has no pixels of its own the pixels are entry 0's. */
typedef struct FlashTex {
    TexEntry ent[1];
} FlashTex;

/* Colour transform: channel = channel * mul + add, clamped to 0..255. Order r, g, b, a. */
typedef struct FlashCxform {
    /* 0x00 */ f32 mul[4];
    /* 0x10 */ s16 add[4];
} FlashCxform; /* 0x18 */

/* A placed shape (one slot of the shape pool). */
typedef struct FlashShape {
    /* 0x00 */ u16 used;
    /* 0x02 */ u16 id;      /* record of tag 4 */
    /* 0x04 */ FlashMtx mtx;
    /* 0x28 */ FlashCxform cx;
} FlashShape; /* 0x40 */

/* The shape pool of a movie. */
typedef struct FlashShapePool {
    /* 0x00 */ FlashShape *items;
    /* 0x04 */ u32 count;
} FlashShapePool; /* 8 */

/* One depth of a timeline: what is placed there. */
typedef struct FlashSlot {
    /* 0x00 */ FlashShape *shape;
    /* 0x04 */ struct FlashClip *clip;
} FlashSlot; /* 8 */

/* FlashTl.state */
#define FLASH_TL_PLAY 0x01    /* advance one frame per tick */
#define FLASH_TL_HOLD 0x02    /* a NextFrame / PrevFrame action moved the play head: it no longer steps by itself */
#define FLASH_TL_GOTO 0x04    /* `frame` was set by a jump: rebuild the display list up to it */
#define FLASH_TL_SEEK 0x08    /* replaying earlier frames: actions are skipped */
#define FLASH_TL_KEEP 0x10    /* the jump continues from `prev` instead of from frame 0 */

/* A timeline: the root movie or one sprite instance. */
typedef struct FlashTl {
    /* 0x00 */ struct Flash *owner;
    /* 0x04 */ FlashSlot *slots;  /* `depths` entries; depth n is slots[n - 1] */
    /* 0x08 */ u8 *frames;        /* the named frame blocks, after the 8-byte record header */
    /* 0x0C */ u32 frame;         /* frame to run next */
    /* 0x10 */ u32 prev;          /* frame the last label jump left */
    /* 0x14 */ u8 unk14;
    /* 0x15 */ u8 state;          /* FLASH_TL_ */
    /* 0x16 */ u16 depths;
} FlashTl; /* 0x18 */

/* FlashProp.flags: which overrides the game has set on a clip. */
#define FLASH_OV_TEX 0x0001      /* texture index */
#define FLASH_OV_POS 0x0002      /* add pos */
#define FLASH_OV_UV 0x0004       /* replace the texture rectangle (and index 0) */
#define FLASH_OV_ALPHA 0x0008    /* multiply alpha */
#define FLASH_OV_SCALE 0x0010    /* scale */
#define FLASH_OV_COLOR 0x0020    /* multiply r, g, b */
#define FLASH_OV_FLIPX 0x0040    /* quad flag 0x01 */
#define FLASH_OV_FLIPY 0x0080    /* quad flag 0x02 */
#define FLASH_OV_REPEAT 0x0100   /* quad flag 0x04 */
#define FLASH_OV_NO_OFS 0x0200   /* do not add the movie's screen offset */
#define FLASH_OV_ADD 0x0400      /* quad flag 0x08 */
#define FLASH_OV_SUB 0x0800      /* quad flag 0x10 */
#define FLASH_OV_MASK 0x1000     /* quad flag 0x20; inherited by children */
#define FLASH_OV_MASK_SET 0x2000 /* quad flag 0x40; inherited by children */

/* Texture rectangle of a quad, in texels, and which texture of the image's file. */

/* Drawing state of a clip: the game's overrides, then matrix and colour transform. Children get the product. */
typedef struct FlashProp {
    /* 0x00 */ u32 flags;      /* FLASH_OV_ */
    /* 0x04 */ f32 x;
    /* 0x08 */ f32 y;
    /* 0x0C */ f32 alpha;      /* 1 by default */
    /* 0x10 */ f32 scaleX;
    /* 0x14 */ f32 scaleY;
    /* 0x18 */ f32 color;
    /* 0x1C */ FlashUv uv;
    /* 0x30 */ FlashMtx mtx;
    /* 0x54 */ FlashCxform cx;
} FlashProp; /* 0x6C */

/* FlashClip.flags */
#define FLASH_CLIP_USED 1
#define FLASH_CLIP_VISIBLE 2
#define FLASH_CLIP_FREE_RUN 8 /* keeps advancing while its parent is stopped */

/* A sprite instance. One exists for every sprite definition of the file (record of tag 2). */
typedef struct FlashClip {
    /* 0x00 */ char name[0x40];  /* instance name from the place tag */
    /* 0x40 */ u16 id;           /* record of tag 6 (the sprite's frames) */
    /* 0x42 */ u16 index;        /* own index in the list */
    /* 0x44 */ u32 flags;        /* FLASH_CLIP_ */
    /* 0x48 */ FlashProp prop;
    /* 0xB4 */ void (*preDraw)(void *arg);
    /* 0xB8 */ void (*postDraw)(void *arg);
    /* 0xBC */ void *preArg;
    /* 0xC0 */ void *postArg;
    /* 0xC4 */ void (*drawOver)(void *arg, FlashProp *prop); /* called after the children are drawn */
    /* 0xC8 */ void *overArg;
    /* 0xCC */ FlashTl tl;
} FlashClip; /* 0xE4 */

/* The sprite instances of a movie. */
typedef struct FlashClipList {
    /* 0x00 */ FlashClip *items;
    /* 0x04 */ u32 count;
    /* 0x08 */ u32 depths;       /* sum of the sprites' depth counts */
} FlashClipList; /* 0xC */

/* Flash.flags */
#define FLASH_PLAY 1        /* Flash_Advance runs the timelines */
#define FLASH_PAD 2         /* set by the action url "pad" "true", cleared by "pad" "false" */
#define FLASH_HIDE 4        /* Flash_Draw draws nothing */
#define FLASH_END 8         /* set by the action url "trigger" "end"; cleared by every Flash_Advance */

/* A movie instance (the animation object the menus and the HUD embed). */

/* What Flash_FindLabel returns: the first clip instance with a name and how many more share it. */

/* FlashQuad.flags */
#define FLASH_QUAD_FLIPX 0x01
#define FLASH_QUAD_FLIPY 0x02
#define FLASH_QUAD_REPEAT 0x04   /* texture wraps instead of clamping */
#define FLASH_QUAD_ADD 0x08
#define FLASH_QUAD_SUB 0x10
#define FLASH_QUAD_MASK 0x20
#define FLASH_QUAD_MASK_SET 0x40

/* One quad on its way to the GS. */
typedef struct FlashQuad {
    /* 0x00 */ u32 flags;        /* FLASH_QUAD_ */
    /* 0x04 */ f32 pt[4][2];     /* top left, top right, bottom left, bottom right */
    /* 0x24 */ FlashUv uv;
    /* 0x38 */ s32 rgba[4];
} FlashQuad; /* 0x48 */

/* One quad of a shape record (tag 4): after the 8-byte record header a u16 count, then these. */
typedef struct FlashShapeQuad {
    /* 0x00 */ s16 image;        /* record of tag 3, or negative for a flat quad */
    /* 0x02 */ u16 unk02;
    /* 0x04 */ u32 rgba;         /* r in the top byte */
    /* 0x08 */ s32 x0;
    /* 0x0C */ s32 x1;
    /* 0x10 */ s32 y0;
    /* 0x14 */ s32 y1;
} FlashShapeQuad; /* 0x18 */

#endif
