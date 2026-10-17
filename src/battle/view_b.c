#include "common.h"
#include "battle/view_a.h"
#include "battle/view_b.h"

/*
 * TextBox, second half: 0x2600B0..0x260D20. The module starts in view_a_e.c (TextBox_Init at 0x25FE00) and this
 * is the same object; see battle/view_b.h for the complete layout of a text box.
 *
 * A text box is a text file plus the style its lines are drawn in. TextBox_AttachLine hangs one line on a movie
 * clip: it fills box->draw and registers TextBox_DrawClip as the clip's "draw over" callback, so the movie
 * player draws the text at the clip's position, tinted with the clip's colour transform, each time it draws
 * the clip.
 */

extern void Flash_ClipSetCallbackC(Flash *flash, FlashRef *ref, void *fn, void *arg);
extern f32 Flash_ClipGetAlpha(Flash *flash, FlashRef *ref);
extern void Gfx_AddDefaultEnv(void);

extern void Font_FlushAll(void);
extern s32 Font_GetWidth(u16 *str);
extern s32 Font_GetHeight(u16 *str);
extern s32 Font_CountLines(u16 *str);
extern void Font_PrintAt(s32 x, s32 y, u16 *str);
extern void Font_PushStyle(void);
extern void Font_PopStyle(void);
extern void Font_SetScaleXY(f32 sx, f32 sy);
extern void Font_SetAlign(s32 align);
extern void Font_SetShadowMode(s32 mode);
extern void Font_SetShadowColor(u32 color);
extern void Font_SetShadowOffset(s32 x, s32 y);
extern void Font_SetColorRGBA(s32 r, s32 g, s32 b, s32 a);
extern void Font_SetClip(s32 x0, s32 y0, s32 x1, s32 y1);
extern void Font_SetSpacing(s32 x, s32 y);
#ifdef PORT
extern const u16 *UiText_TranslateLine(const u32 *table, int line, const u16 *original);
#endif

#define TB(box) ((TextBoxFull *)(box))

/* A line wider than `w` is shrunk horizontally to fit. */
void TextBox_SetMaxWidth(TextBox *box, s32 w) {
    TB(box)->maxW = w;
    TB(box)->flags |= TEXTBOX_FLAG_MAX_W;
}

/* A text taller than `h` is shrunk vertically to fit. */
void TextBox_SetMaxHeight(TextBox *box, s32 h) {
    TB(box)->maxH = h;
    TB(box)->flags |= TEXTBOX_FLAG_MAX_H;
}

/* Both limits. `unused` is not looked at. */
void TextBox_SetMaxSize(TextBox *box, s32 w, s32 h) {
    TextBox_SetMaxWidth(box, w);
    TextBox_SetMaxHeight(box, h);
}

/* Extra y offset for a text of one, two, three, four and five rows (to centre it in its window). */
void TextBox_SetLineOffsets(TextBox *box, s32 y1, s32 y2, s32 y3, s32 y4, s32 y5) {
    TB(box)->flags |= TEXTBOX_FLAG_LINE_Y;
    TB(box)->lineY[0] = y1;
    TB(box)->lineY[1] = y2;
    TB(box)->lineY[2] = y3;
    TB(box)->lineY[3] = y4;
    TB(box)->lineY[4] = y5;
}

/* Character and line spacing. */
void TextBox_SetSpacing(TextBox *box, s32 x, s32 y) {
    TB(box)->flags |= TEXTBOX_FLAG_SPACING;
    TB(box)->spacingX = x;
    TB(box)->spacingY = y;
}

/* The clip callback: moves the text by the clip's position, tints text and shadow with the clip's colour
   transform, and prints the string unless it has become fully transparent.

   NOT MATCHING: kept as INCLUDE_ASM. The attempt below is behaviourally exact. It differs in how the clamped
   component reaches its store: the original copies it with `move v1,v0` (seven times; the eighth needs no
   copy), this C narrows it with `andi v1,v0,0xff` (eight times, so the function is one instruction longer).
   An `s32` helper gives the plain copy but then the compiler turns the outer test into a branch around, or
   stores in both arms; some 40 shapes of the clamp were tried. Everything else is identical. */
/* Second cleanup pass: on this compiler `move` is also what an int-to-long sign extension compiles to
   (`(s64)u0 << 4` in BtlText_PutSprite), and a helper returning s64 / long does give a move in the arm
   (`move v0,v0`), but the narrowing then moves to the join (`andi v0,v0,0xff` in front of the `sb`): 48
   differences. A `u8` result local in a macro is promoted to a full register and has its zero hoisted in front
   of the branch (204 instructions). So the original copy is most likely a plain copy between two int
   registers that the allocator could not merge (the next statement's `lbu` already sits in v0 at the join),
   in a shape where the compiler does not hoist the `= 0` arm; not found (15 more shapes tried here).
   Cleanup 4: twelve more shapes (nested ternaries on an `s32` temporary, `r = 0xFF; if (t < 0x100) r = t;` in
   a block or in an `s32` inline helper, min / max macros, a helper taking `u8 *`): 62 to 130 differences, all
   because the `< 0` test then becomes straight-line code or the zero is hoisted. The `u8` helper below stays
   the closest form. */
#if 0
/* Clamps a colour component to a byte. */
static inline u8 TextBox_ClampByte(s32 c) {
    return c < 0 ? 0 : (c > 0xFF ? 0xFF : c);
}

/* A colour component after a clip's colour transform. */
#define TB_TINT(c, mul, add) (c) = TextBox_ClampByte((s32)((f32)(c) * (mul)) + (add))

s32 TextBox_DrawClip(TextBoxDraw *draw, TextBoxClipProp *prop) {
    u32 shadow;

    if (prop != NULL) {
        draw->x = (f32)draw->x + prop->x;
        draw->y = (f32)draw->y + prop->y;
        TB_TINT(draw->color[0], prop->mul[0], prop->add[0]);
        TB_TINT(draw->color[1], prop->mul[1], prop->add[1]);
        TB_TINT(draw->color[2], prop->mul[2], prop->add[2]);
        TB_TINT(draw->color[3], prop->mul[3], prop->add[3]);
        TB_TINT(draw->shadow[0], prop->mul[0], prop->add[0]);
        TB_TINT(draw->shadow[1], prop->mul[1], prop->add[1]);
        TB_TINT(draw->shadow[2], prop->mul[2], prop->add[2]);
        TB_TINT(draw->shadow[3], prop->mul[3], prop->add[3]);
    }
    if (draw->color[3] != 0) {
        shadow = (draw->shadow[0] | (draw->shadow[2] << 16)) | ((draw->shadow[3] << 24) | (draw->shadow[1] << 8));
        Font_PushStyle();
        Font_SetAlign(draw->align);
        Font_SetScaleXY(draw->scaleX, draw->scaleY);
        Font_SetColorRGBA(draw->color[0], draw->color[1], draw->color[2], draw->color[3]);
        Font_SetShadowMode(2);
        Font_SetShadowOffset(1, 2);
        Font_SetShadowColor(shadow);
        Font_SetClip(draw->clip[0], draw->clip[1], draw->clip[2], draw->clip[3]);
        Font_SetSpacing(draw->spacingX, draw->spacingY);
        Font_PrintAt(draw->x, draw->y, draw->str);
        Font_PopStyle();
        if (draw->noFlush != 1) {
            Font_FlushAll();
            Gfx_AddDefaultEnv();
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/battle/view_b", TextBox_DrawClip);
#endif

/* Fills box->draw for one string and hangs it on a clip. */
#define TB_ATTACH(flash, ref, x, y, str, box) \
    ofs[1] = 0; \
    ofs[0] = 0; \
    alpha = Flash_ClipGetAlpha(flash, ref); \
    TB_ATTACH_STR(str, box) \
    if (TB(box)->flags & TEXTBOX_FLAG_MAX_W) { \
        size = Font_GetWidth(str); \
        if (TB(box)->maxW < size) { \
            scale[0] = (f32)TB(box)->maxW / (f32)size; \
        } else { \
            scale[0] = 1.0f; \
        } \
    } else { \
        scale[0] = 1.0f; \
    } \
    if (TB(box)->flags & TEXTBOX_FLAG_MAX_H) { \
        size = Font_GetHeight(str); \
        if (TB(box)->maxH < size) { \
            scale[1] = (f32)TB(box)->maxH / (f32)size; \
        } else { \
            scale[1] = 1.0f; \
        } \
    } else { \
        scale[1] = 1.0f; \
    } \
    if (TB(box)->flags & TEXTBOX_FLAG_LINE_Y) { \
        switch (Font_CountLines(str)) { \
        case 1: \
            ofs[1] += TB(box)->lineY[0]; \
            break; \
        case 2: \
            ofs[1] += TB(box)->lineY[1]; \
            break; \
        case 3: \
            ofs[1] += TB(box)->lineY[2]; \
            break; \
        case 4: \
            ofs[1] += TB(box)->lineY[3]; \
            break; \
        case 5: \
            ofs[1] += TB(box)->lineY[4]; \
            break; \
        } \
    } \
    TB(box)->draw.x = ofs[0] + TB(box)->x + x; \
    TB(box)->draw.y = ofs[1] + TB(box)->y + y; \
    TB(box)->draw.scaleX = scale[0]; \
    TB(box)->draw.scaleY = scale[1]; \
    if (TB(box)->flags & TEXTBOX_FLAG_COLOR) { \
        TB(box)->draw.color[0] = TB(box)->color[0]; \
        TB(box)->draw.color[1] = TB(box)->color[1]; \
        TB(box)->draw.color[2] = TB(box)->color[2]; \
        TB(box)->draw.color[3] = (u32)(TB(box)->color[3] * alpha); \
    } else { \
        TB(box)->draw.color[0] = 0xFF; \
        TB(box)->draw.color[1] = 0xFF; \
        TB(box)->draw.color[2] = 0xFF; \
        TB(box)->draw.color[3] = (u32)(alpha * 128.0f); \
    } \
    if (TB(box)->flags & TEXTBOX_FLAG_COLOR2) { \
        TB(box)->draw.shadow[0] = TB(box)->shadow[0]; \
        TB(box)->draw.shadow[1] = TB(box)->shadow[1]; \
        TB(box)->draw.shadow[2] = TB(box)->shadow[2]; \
        TB(box)->draw.shadow[3] = (u32)(TB(box)->shadow[3] * alpha); \
    } else { \
        TB(box)->draw.shadow[0] = 0x20; \
        TB(box)->draw.shadow[1] = 0x20; \
        TB(box)->draw.shadow[2] = 0x20; \
        TB(box)->draw.shadow[3] = (u32)(alpha * 64.0f); \
    } \
    if (TB(box)->flags & TEXTBOX_FLAG_RECT) { \
        TB(box)->draw.clip[0] = TB(box)->clip[0]; \
        TB(box)->draw.clip[1] = TB(box)->clip[1]; \
        TB(box)->draw.clip[2] = TB(box)->clip[2]; \
        TB(box)->draw.clip[3] = TB(box)->clip[3]; \
    } else { \
        TB(box)->draw.clip[2] = 0x1FF; \
        TB(box)->draw.clip[3] = 0x1BF; \
        TB(box)->draw.clip[0] = 0; \
        TB(box)->draw.clip[1] = 0; \
    } \
    if (TB(box)->flags & TEXTBOX_FLAG_SPACING) { \
        TB(box)->draw.spacingX = TB(box)->spacingX; \
        TB(box)->draw.spacingY = TB(box)->spacingY; \
    } else { \
        TB(box)->draw.spacingX = 0; \
        TB(box)->draw.spacingY = 0; \
    } \
    TB(box)->draw.str = str; \
    Flash_ClipSetCallbackC(flash, ref, TextBox_DrawClip, &TB(box)->draw)

/* Shows line `line` of the box's text file on a clip, at (x, y) from the clip's position. A negative line or
   a missing clip takes the text off the clip. */
void TextBox_AttachLine(Flash *flash, FlashRef *ref, s32 x, s32 y, s32 line, TextBox *box) {
    s32 size;
    s32 ofs[2];
    f32 scale[2];
    f32 alpha;
    u16 *str;

    if (line < 0 || ref->id < 0) {
        Flash_ClipSetCallbackC(flash, ref, NULL, NULL);
        return;
    }
#ifdef PORT
#define TB_ATTACH_STR(str, box) str = (u16 *)((u8 *)TB(box)->text + ((((u32 *)TB(box)->text)[line + 1] >> 2) << 2)); \
    str = (u16 *)UiText_TranslateLine((const u32 *)TB(box)->text, line, str);
#else
#define TB_ATTACH_STR(str, box) str = (u16 *)((u8 *)TB(box)->text + ((((u32 *)TB(box)->text)[line + 1] >> 2) << 2));
#endif
    TB_ATTACH(flash, ref, x, y, str, box);
#undef TB_ATTACH_STR
}

/* The same for a string that is not in the box's text file. */
void TextBox_AttachString(Flash *flash, FlashRef *ref, s32 x, s32 y, u16 *str, TextBox *box) {
    s32 size;
    s32 ofs[2];
    f32 scale[2];
    f32 alpha;

    if (ref->id < 0) {
        Flash_ClipSetCallbackC(flash, ref, NULL, NULL);
        return;
    }
#define TB_ATTACH_STR(str, box)
    TB_ATTACH(flash, ref, x, y, str, box);
#undef TB_ATTACH_STR
}
