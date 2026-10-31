#ifndef BATTLE_HUD_C_H
#define BATTLE_HUD_C_H

#include "types.h"
#include "battle/hud_types.h"
#include "sys/ramp.h"

/*
 * Battle HUD, 0x222400-0x226488: the tail of the gauge part (src/battle/hud_c.c), the combo part
 * (src/battle/hud_c_b.c) and the sprite library every HUD part draws with (src/battle/hud_c_c.c).
 *
 * The sprite, texture, sheet and node layouts below are local views; they agree with HudSprite / HudTex / HudRes /
 * HudNode of include/battle/hud_a.h and add the fields that only the sprite library reads.
 */

/* A HUD sprite (0x1C bytes). */

#define HUDC_SPR_HIDDEN 1
#define HUDC_SPR_MIRROR 2

/* One texture entry of a HUD sprite sheet (0x40 bytes). */

/* A HUD sprite sheet after Res_RelocateOffsets. */

/* A HUD node (0x38 bytes). */

/* VRAM blocks HudSprite_DrawGradient uploads to. */
#define HUDC_GRADIENT_TBP 0x2A00
#define HUDC_GRADIENT_CBP 0x2C80

/* hud_c.c: gauge part */
void HudGauge_Term(void);
void HudGauge_Reset(void);

/* hud_c_b.c: combo part */
void HudCombo_SelectSide(s32 side);
void HudCombo_SetMessage(s32 side, s32 message);
void HudCombo_SetText(s32 side, s32 text);
void HudCombo_SetDamage(s32 side, s32 damage, s32 isNew);
void HudCombo_SetHits(s32 side, u32 hits);
void HudCombo_SlideOut(f32 seconds);
void HudCombo_SlideIn(f32 seconds);
void HudCombo_DrawText(void);
void HudCombo_Init(HudNode **out, HudRes *res);
void HudCombo_Term(void);
void HudCombo_Reset(void);

/* hud_c_c.c: sprite library */
void HudGfx_CallBegin(void (*fn)(s32));
void HudGfx_CallEnd(void (*fn)(s32));
void HudSprite_Show(HudSprite *spr, s32 show);
void HudSprite_SetMirror(HudSprite *spr, s32 mirror);
void HudSprite_SetRect(HudSprite *spr, s32 x0, s32 x1, s32 y0, s32 y1);
void HudSprite_SetUv(HudSprite *spr, s32 u0, s32 u1, s32 v0, s32 v1);
void HudSprite_SetTex(HudSprite *spr, s32 tex, s32 sub);
void HudSprite_SetColor(HudSprite *spr, s32 r, s32 g, s32 b, s32 a);
void HudSprite_InitPlain(HudSprite *spr, u8 r, u8 g, u8 b, u8 a);
void HudSprite_Move(HudSprite *spr, s32 dx, s32 dy);
void HudSprite_MoveUv(HudSprite *spr, s32 du, s32 dv);
void HudSprite_FlipU(HudSprite *spr);
void HudSprite_FlipV(HudSprite *spr);
void HudSprite_Scale(HudSprite *spr, f32 sx, f32 sy);
void HudSprite_Center(HudSprite *spr);
void HudSprite_InitTex(HudSprite *spr, HudRes *res, s32 tex, s32 sub);
void HudRes_UploadTex(HudRes *res, s32 tex, s32 sub, s32 tbp, s32 cbp);
void HudSprite_DrawAlphaClear(HudSprite *spr);
void HudSprite_DrawAlphaSet(HudSprite *spr);
void HudGfx_ClearAlpha(void);
void HudGfx_SetEnv(void);
void HudSprite_DrawAt(HudSprite *spr, HudRes *res, u8 ctx2, s32 tbp, s32 cbp);
void HudSprite_DrawGradient(HudSprite *spr, HudRes *res, u8 ctx2, s32 colorL, s32 colorR);

#endif
