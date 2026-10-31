#ifndef BATTLE_HUD_TYPES_H
#define BATTLE_HUD_TYPES_H
#include "types.h"
#include "sys/ramp.h"
/* Shared HUD scene-graph and gauge records. Layouts are owned here once. */
struct HudBObj;
typedef struct HudRect {
    /* 0x00 */ s16 x0;
    /* 0x02 */ s16 x1;
    /* 0x04 */ s16 y0;
    /* 0x06 */ s16 y1;
} HudRect;

typedef struct HudSprite {
    /* 0x00 */ union {
        u32 flags;
        struct { u32 hidden : 1; u32 mirror : 1; };
    };      /* bit 0: hidden (0x224B90(sprite, show)); bit 1: mirrored (0x224BB0) */
    /* 0x04 */ s16 tex;        /* texture entry of the sheet, -1: untextured (0x224C00(sprite, tex, sub)) */
    /* 0x06 */ u16 texSub;     /* added to tex for a second entry (0x225A50) */
    /* 0x08 */ HudRect pos;    /* screen rectangle relative to the node: 0x224BD0(sprite, x0, x1, y0, y1);
                                  0x224CA0(sprite, dx, dy) moves it */
    /* 0x10 */ HudRect uv;     /* texel rectangle: 0x224BE8(sprite, u0, u1, v0, v1) */
    /* 0x18 */ u8 r, g, b, a;  /* 0x224C10(sprite, r, g, b, a); 0x80 is neutral */
} HudSprite;

typedef struct HudTex {
    /* 0x00 */ u8 unk0[8];
    /* 0x08 */ s32 imageSize;  /* bytes of the image transfer packet (0: none) */
    /* 0x0C */ s32 clutSize;   /* bytes of the palette transfer packet (0: none) */
    /* 0x10 */ u8 unk10[8];
    /* 0x18 */ s32 imageWidth; /* BITBLTBUF destination width (units of 64 pixels) of the image */
    /* 0x1C */ s32 clutWidth;  /* the same for the palette */
    /* 0x20 */ s32 imageBlock; /* VRAM block of the image, relative to the block base given when drawing */
    /* 0x24 */ s32 clutBlock;  /* VRAM block of the palette, relative to the palette base */
    /* 0x28 */ s32 mark;       /* uploaded this frame; cleared by Hud_ClearTexMarks at the end of Hud_Draw */
    /* 0x2C */ s32 unk2C;
    /* 0x30 */ u64 tex0;       /* GS TEX0 without TBP0, TCC and CBP */
    /* 0x38 */ void *image;    /* image transfer packet (referenced by the display list) */
    /* 0x3C */ void *clut;     /* palette transfer packet */
} HudTex;

typedef struct HudRes {
    /* 0x00 */ s32 texCount;
    /* 0x04 */ s32 unk4[3];
    /* 0x10 */ HudTex *tex;
} HudRes;

typedef struct HudNode HudNode;
struct HudNode {
    /* 0x00 */ u32 flags;      /* HUD_NODE_HIDDEN, HUD_NODE_MIRROR */
    /* 0x04 */ f32 rot;        /* rotation about Z, radians */
    /* 0x08 */ s32 unk8[2];
    /* 0x10 */ s32 x;          /* set with 0x2264C8(node, x, y) */
    /* 0x14 */ s32 y;
    /* 0x18 */ s32 ofsX;       /* added to x / y */
    /* 0x1C */ s32 ofsY;
    /* 0x20 */ u32 spriteCount;
    /* 0x24 */ HudSprite **sprites;
    /* 0x28 */ u32 childCount;
    /* 0x2C */ HudNode **children;
    /* 0x30 */ void (*update)(); /* called without arguments; a callee that wants the node reads $a0, which
                                    still holds it (HudTeam_UpdateRoot) */
    /* 0x34 */ void (*draw)(HudNode *node);
};

typedef struct HudShake {
    /* 0x00 */ u16 count;
    /* 0x02 */ u16 amp;
} HudShake;

typedef struct HudGauge {
    /* 0x000 */ HudRes *res;             /* sprite sheet 0 of the HUD file */
    /* 0x004 */ HudSprite *sprites;       /* 76 */
    /* 0x008 */ HudNode *nodes;        /* 12 nodes, [0] is the root */
    /* 0x00C */ HudShake shake[2][3]; /* by side: the panel, the health node, the ki node */
    /* 0x024 */ s32 side;              /* side being updated / drawn */
    /* 0x028 */ s32 hpShown[2];        /* trailing (damage) health */
    /* 0x030 */ struct { s32 hp[2]; } cur;             /* health now (HudGauge_SetHp) */
    /* 0x038 */ s32 auraAlpha[2];      /* 0..0x80, +-0x10 a frame: the powered-up sparks and the blue flash */
    /* 0x040 */ Ramp lowHpPulse[2];    /* 0 <-> 1 every 0.5 s while health <= one bar: the red flash */
    /* 0x070 */ s32 blast[2];
    /* 0x078 */ s32 ki[2];
    /* 0x080 */ s32 kiShown[2];        /* trailing ki */
    /* 0x088 */ Ramp kiBarFlash[2];    /* 1 -> 0 in 1 s when a ki bar is gained */
    /* 0x0B8 */ Ramp kiFullPulse[2];   /* 0 <-> 1 every 1 s; shown at full ki and by the powered-up lamps */
    /* 0x0E8 */ s32 maxPower[2];
    /* 0x0F0 */ Ramp blastFlash[2];
    /* 0x120 */ u32 statMask[2];       /* status icons shown, bits 0..3 */
    /* 0x128 */ u32 statPos[2];        /* icons drawn bright (positive modifier); the others are dark */
    /* 0x130 */ s32 kiReserve[2];
    /* 0x138 */ u32 statPrev[2];       /* statMask before the last change */
    /* 0x140 */ s32 statTo[2][4];      /* x of each icon: 24 per shown icon before it */
    /* 0x160 */ s32 statFrom[2][4];    /* x before the last change */
    /* 0x180 */ Ramp statRamp[2];
    /* 0x1B0 */ s32 statState[2];      /* 0..5 the change animation, anything else: at rest */
    /* 0x1B8 */ Ramp hpBarFade[2];     /* 1 -> 0 in 0.2 s when the trailing health uses up a bar */
    /* 0x1E8 */ HudRes *faceRes[2];
    /* 0x1F0 */ struct HudBObj *obj[2];
    /* 0x1F8 */ s32 switching[2];
    /* 0x200 */ Ramp slide[2];         /* 0 on screen .. 1 away, one side's panel */
    /* 0x230 */ Ramp slideAll;         /* the same for the whole part */
    /* 0x248 */ s32 hpDrain[2];        /* speed of the trailing health, per frame */
    /* 0x250 */ s32 kiDrain[2];
    /* 0x258 */ Ramp kiReservePulse[2];
} HudGauge;

extern HudGauge *gHudGauge;
#endif
