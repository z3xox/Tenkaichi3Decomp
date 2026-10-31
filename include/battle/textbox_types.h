#ifndef BATTLE_TEXTBOX_TYPES_H
#define BATTLE_TEXTBOX_TYPES_H
#include "types.h"

#define TEXTBOX_FLAG_MAX_W 8      /* shrink the line horizontally to maxW */
#define TEXTBOX_FLAG_MAX_H 0x10   /* shrink the line vertically to maxH */
#define TEXTBOX_FLAG_LINE_Y 0x20  /* add lineY[rows - 1] to y */
#define TEXTBOX_FLAG_SPACING 0x40 /* use spacingX / spacingY */
#define TEXTBOX_LINE_Y_COUNT 5

typedef struct FlashTexRes {
    /* 0x00 */ s32 unk0[4];
    /* 0x10 */ u8 *tex;
} FlashTexRes;

typedef struct TextBoxDraw {
    /* 0x00 */ s32 align;      /* Font_SetAlign */
    /* 0x04 */ u16 *str;
    /* 0x08 */ s32 x;          /* the clip's position is added by the callback, every time it runs */
    /* 0x0C */ s32 y;
    /* 0x10 */ f32 scaleX;
    /* 0x14 */ f32 scaleY;
    /* 0x18 */ u8 color[4];    /* r, g, b, a (0x80 = opaque) */
    /* 0x1C */ u8 shadow[4];
    /* 0x20 */ s32 clip[4];    /* Font_SetClip */
    /* 0x30 */ s32 noFlush;    /* 1: the text is left in the font queue instead of being drawn at once */
    /* 0x34 */ s32 spacingX;   /* Font_SetSpacing */
    /* 0x38 */ s32 spacingY;
} TextBoxDraw;

typedef struct TextBox {
    /* 0x00 */ s32 flags;      /* TEXTBOX_FLAG_* */
    /* 0x04 */ void *text;     /* text file: line n is at text + (((u32 *)text)[n + 1] & ~3) */
    /* 0x08 */ s32 unk8;
    /* 0x0C */ s32 x;          /* offset added to the position of every line (TextBox_SetUnkC) */
    /* 0x10 */ s32 y;
    /* 0x14 */ s32 lineY[TEXTBOX_LINE_Y_COUNT]; /* extra y for a text of 1..5 rows */
    /* 0x28 */ s32 maxW;
    /* 0x2C */ s32 maxH;
    /* 0x30 */ u8 color[4];
    /* 0x34 */ u8 shadow[4];   /* "color2" in view_a.h */
    /* 0x38 */ s32 clip[4];    /* "rect" in view_a.h */
    /* 0x48 */ s32 spacingX;
    /* 0x4C */ s32 spacingY;
    /* 0x50 */ TextBoxDraw draw; /* +0x50 align (TextBox_SetUnk50), +0x80 noFlush (TextBox_SetUnk80) */
} TextBox;

#endif
