#ifndef BATTLE_HUD_B_H
#define BATTLE_HUD_B_H

#include "types.h"
#include "battle/hud_types.h"
#include "sys/ramp.h"

/*
 * Battle HUD, gauge part: 0x21CA60-0x222400 (src/battle/hud_b.c). See the top of that file.
 * Gauge, sprite and node layouts are shared through battle/hud_types.h.
 */

/* A HUD sprite (0x1C bytes); written through the setters at 0x224B90..0x224CA0. */

/* Shake of one HUD node: `count` frames left, a random offset of +-amp each frame. */

/* A HUD node (0x38 bytes): it owns a list of sprites and a list of child nodes. HudNode_Update calls `update`
 * (with the node still in $a0) and then the children; HudNode_Draw calls `draw(node)`, which draws the sprites. */

struct HudBObj;

/* Interface used by the HUD manager (src/battle/hud_a.c). */
void HudGauge_SelectSide(s32 side);
void HudGauge_SetHp(s32 side, s32 hp);
void HudGauge_SetKi(s32 side, s32 ki);
void HudGauge_SetMaxPower(s32 side, s32 value);
void HudGauge_SetBlast(s32 side, s32 blast);
void HudGauge_SetStatIcons(s32 side, s32 mask, s32 pos);
void HudGauge_SetKiReserve(s32 side, s32 value);
void HudGauge_ShakeHp(s32 side, u16 count, u16 amp);
void HudGauge_ShakeKi(s32 side, u16 count, u16 amp);
void HudGauge_SetFighter(s32 side, struct HudBObj *obj);
void HudGauge_SlideOut(f32 seconds);
void HudGauge_SlideIn(f32 seconds);
void HudGauge_SetSwitching(s32 side, s32 switching);
void HudGauge_Init(HudNode **out, void *res);

#endif
