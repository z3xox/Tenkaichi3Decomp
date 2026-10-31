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

/* A line wider than `w` is shrunk horizontally to fit. */
void TextBox_SetMaxWidth(TextBox *box, s32 w) {
    box->maxW = w;
    box->flags |= TEXTBOX_FLAG_MAX_W;
}

/* A text taller than `h` is shrunk vertically to fit. */
void TextBox_SetMaxHeight(TextBox *box, s32 h) {
    box->maxH = h;
    box->flags |= TEXTBOX_FLAG_MAX_H;
}

/* Both limits. `unused` is not looked at. */
void TextBox_SetMaxSize(TextBox *box, s32 w, s32 h) {
    TextBox_SetMaxWidth(box, w);
    TextBox_SetMaxHeight(box, h);
}

/* Extra y offset for a text of one, two, three, four and five rows (to centre it in its window). */
void TextBox_SetLineOffsets(TextBox *box, s32 y1, s32 y2, s32 y3, s32 y4, s32 y5) {
    box->flags |= TEXTBOX_FLAG_LINE_Y;
    box->lineY[0] = y1;
    box->lineY[1] = y2;
    box->lineY[2] = y3;
    box->lineY[3] = y4;
    box->lineY[4] = y5;
}

/* Character and line spacing. */
void TextBox_SetSpacing(TextBox *box, s32 x, s32 y) {
    box->flags |= TEXTBOX_FLAG_SPACING;
    box->spacingX = x;
    box->spacingY = y;
}



/* Clamps a colour component to a byte. */
#define TB_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* A colour component after a clip's colour transform. */
#define TB_TINT(c, mul, add) (c) = TB_CLAMP((s32)((f32)(c) * (mul)) + (add), 0, 0xFF)

s32 TextBox_DrawClip(TextBoxDraw *draw, FlashProp *prop) {
    u32 shadow;

    if (prop != NULL) {
        draw->x = (f32)draw->x + prop->mtx.m[2];
        draw->y = (f32)draw->y + prop->mtx.m[5];
        TB_TINT(draw->color[0], prop->cx.mul[0], prop->cx.add[0]);
        TB_TINT(draw->color[1], prop->cx.mul[1], prop->cx.add[1]);
        TB_TINT(draw->color[2], prop->cx.mul[2], prop->cx.add[2]);
        TB_TINT(draw->color[3], prop->cx.mul[3], prop->cx.add[3]);
        TB_TINT(draw->shadow[0], prop->cx.mul[0], prop->cx.add[0]);
        TB_TINT(draw->shadow[1], prop->cx.mul[1], prop->cx.add[1]);
        TB_TINT(draw->shadow[2], prop->cx.mul[2], prop->cx.add[2]);
        TB_TINT(draw->shadow[3], prop->cx.mul[3], prop->cx.add[3]);
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

/* Fills box->draw for one string and hangs it on a clip. */
#define TB_ATTACH(flash, ref, x, y, str, box) \
    ofs[1] = 0; \
    ofs[0] = 0; \
    alpha = Flash_ClipGetAlpha(flash, ref); \
    TB_ATTACH_STR(str, box) \
    if (box->flags & TEXTBOX_FLAG_MAX_W) { \
        size = Font_GetWidth(str); \
        if (box->maxW < size) { \
            scale[0] = (f32)box->maxW / (f32)size; \
        } else { \
            scale[0] = 1.0f; \
        } \
    } else { \
        scale[0] = 1.0f; \
    } \
    if (box->flags & TEXTBOX_FLAG_MAX_H) { \
        size = Font_GetHeight(str); \
        if (box->maxH < size) { \
            scale[1] = (f32)box->maxH / (f32)size; \
        } else { \
            scale[1] = 1.0f; \
        } \
    } else { \
        scale[1] = 1.0f; \
    } \
    if (box->flags & TEXTBOX_FLAG_LINE_Y) { \
        switch (Font_CountLines(str)) { \
        case 1: \
            ofs[1] += box->lineY[0]; \
            break; \
        case 2: \
            ofs[1] += box->lineY[1]; \
            break; \
        case 3: \
            ofs[1] += box->lineY[2]; \
            break; \
        case 4: \
            ofs[1] += box->lineY[3]; \
            break; \
        case 5: \
            ofs[1] += box->lineY[4]; \
            break; \
        } \
    } \
    box->draw.x = ofs[0] + box->x + x; \
    box->draw.y = ofs[1] + box->y + y; \
    box->draw.scaleX = scale[0]; \
    box->draw.scaleY = scale[1]; \
    if (box->flags & TEXTBOX_FLAG_COLOR) { \
        box->draw.color[0] = box->color[0]; \
        box->draw.color[1] = box->color[1]; \
        box->draw.color[2] = box->color[2]; \
        box->draw.color[3] = (u32)(box->color[3] * alpha); \
    } else { \
        box->draw.color[0] = 0xFF; \
        box->draw.color[1] = 0xFF; \
        box->draw.color[2] = 0xFF; \
        box->draw.color[3] = (u32)(alpha * 128.0f); \
    } \
    if (box->flags & TEXTBOX_FLAG_COLOR2) { \
        box->draw.shadow[0] = box->shadow[0]; \
        box->draw.shadow[1] = box->shadow[1]; \
        box->draw.shadow[2] = box->shadow[2]; \
        box->draw.shadow[3] = (u32)(box->shadow[3] * alpha); \
    } else { \
        box->draw.shadow[0] = 0x20; \
        box->draw.shadow[1] = 0x20; \
        box->draw.shadow[2] = 0x20; \
        box->draw.shadow[3] = (u32)(alpha * 64.0f); \
    } \
    if (box->flags & TEXTBOX_FLAG_RECT) { \
        box->draw.clip[0] = box->clip[0]; \
        box->draw.clip[1] = box->clip[1]; \
        box->draw.clip[2] = box->clip[2]; \
        box->draw.clip[3] = box->clip[3]; \
    } else { \
        box->draw.clip[2] = 0x1FF; \
        box->draw.clip[3] = 0x1BF; \
        box->draw.clip[0] = 0; \
        box->draw.clip[1] = 0; \
    } \
    if (box->flags & TEXTBOX_FLAG_SPACING) { \
        box->draw.spacingX = box->spacingX; \
        box->draw.spacingY = box->spacingY; \
    } else { \
        box->draw.spacingX = 0; \
        box->draw.spacingY = 0; \
    } \
    box->draw.str = str; \
    Flash_ClipSetCallbackC(flash, ref, TextBox_DrawClip, &box->draw)

/* Shows line `line` of the box's text file on a clip, at (x, y) from the clip's position. A negative line or
   a missing clip takes the text off the clip. */
void TextBox_AttachLine(Flash *flash, FlashRef *ref, s32 x, s32 y, s32 line, TextBox *box) {
    s32 size;
    s32 ofs[2];
    f32 scale[2];
    f32 alpha;
    u16 *str;

    if (line < 0 || ref->index < 0) {
        Flash_ClipSetCallbackC(flash, ref, NULL, NULL);
        return;
    }
#ifdef PORT
#define TB_ATTACH_STR(str, box) str = (u16 *)((u8 *)box->text + ((((u32 *)box->text)[line + 1] >> 2) << 2)); \
    str = (u16 *)UiText_TranslateLine((const u32 *)box->text, line, str);
#else
#define TB_ATTACH_STR(str, box) str = (u16 *)((u8 *)box->text + ((((u32 *)box->text)[line + 1] >> 2) << 2));
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

    if (ref->index < 0) {
        Flash_ClipSetCallbackC(flash, ref, NULL, NULL);
        return;
    }
#define TB_ATTACH_STR(str, box)
    TB_ATTACH(flash, ref, x, y, str, box);
#undef TB_ATTACH_STR
}
