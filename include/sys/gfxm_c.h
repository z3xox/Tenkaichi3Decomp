#ifndef SYS_GFXM_C_H
#define SYS_GFXM_C_H

#include "types.h"
#include "sys/flash_types.h"
#include "sys/gfxm_b_c.h"
#include "sys/gfxm_b_b.h"

/*
 * The movie player (0x10AD58..0x10EC18, src/sys/gfxm_c.c). Movie files are converted SWF files.
 *
 * File: 'F' 'O' 'D' 0x11 "LIT\0", then tags: u8 code, u8, u16 record count, u32 size, data; code 0 ends.
 * Records inside a tag have the same 8-byte header (their own size at +4). Tags the player reads:
 *   2  sprite list: record i = header + u16 id (the record of tag 6 that holds sprite i's frames)
 *   3  images: header + u16 width + u16 height; image i draws with Flash.tex[i]
 *   4  shapes: header + u16 quad count + FlashShapeQuad[count]
 *   6  sprite frames: one record per sprite; the header's count field is the sprite's number of depths
 *   7  root frames: one record, same layout
 * A frame record is a list of named blocks: the frame's label (NUL-terminated, usually empty), u16 size, then the
 * frame's tags: u16 SWF tag code + data, ended by code 1 (ShowFrame). A block of size 0 ends the timeline.
 *   4  PlaceObject:   u16 depth, u8 kind (4 shape, 6 sprite), u16 id, matrix, (sprite only) colour transform
 *   5  RemoveObject:  u16 depth, u8 kind, u16 id
 *   12 DoAction:      SWF actions until 0: 0x81 GotoFrame u16, 4 NextFrame, 5 PrevFrame, 6 Play, 7 Stop,
 *                     0x83 GetURL url target (two strings), 0x8A WaitForFrame (3 bytes, ignored),
 *                     0x8B SetTarget name, 0x8C GotoLabel label
 *   26 PlaceObject2:  u8 flags (2 new, 4 matrix, 8 colour transform, 0x20 name), u16 depth,
 *                     [u8 kind, u16 id], [matrix], [colour transform], [name]
 *   28 RemoveObject2: u16 depth
 * Multi-byte fields are unaligned. Matrix: see Flash_ReadMtx. Colour transform: see Flash_ReadCxform.
 * GetURL is the movie's channel to the game: "pad" "true" / "false" (Flash.flags bit 2), "trig" "n" and "se" "n"
 * (bit n of Flash.trig / Flash.se for one tick), "trigger" "end" (Flash.flags bit 8 for one tick).
 */

void Flash_CxformIdentity(FlashCxform *cx);
void Flash_ReadCxform(u8 *data, FlashCxform *cx, s32 *ofs);
void Flash_Create(Flash *flash, u8 *file, FlashTex **tex);
void Flash_Destroy(Flash *flash);
void Flash_Advance(Flash *flash);
void Flash_Draw(Flash *flash);
void Flash_Reset(Flash *flash, s32 keepClips);
void Flash_Play(Flash *flash, s32 speed);
void Flash_Stop(Flash *flash);
void Flash_SetOffset(Flash *flash, s32 x, s32 y);
void Flash_SetFlag(Flash *flash, u32 mask, u8 on);
void Flash_GotoLabel(Flash *flash, char *label, u8 fromStart);
void Flash_StepFrames(Flash *flash, s32 step);
void Flash_FindLabel(Flash *flash, char *parent, char *name, FlashRef *out);
void Flash_ClipGotoLabel(Flash *flash, FlashRef *ref, char *label);
void Flash_ClipSetCallbackA(Flash *flash, FlashRef *ref, void *fn, void *arg);
void Flash_ClipSetCallbackB(Flash *flash, FlashRef *ref, void *fn, void *arg);
void Flash_ClipSetCallbackC(Flash *flash, FlashRef *ref, void *fn, void *arg);
s32 Flash_ClipSetFlags(Flash *flash, FlashRef *ref, s32 props, u8 on); /* returns nothing meaningful */
void Flash_ClipSetOffset(Flash *flash, FlashRef *ref, s32 x, s32 y);
void Flash_ClipSetScale(Flash *flash, FlashRef *ref, f32 x, f32 y);
void Flash_ClipSetAlpha(Flash *flash, FlashRef *ref, f32 alpha);
void Flash_ClipSetColor(Flash *flash, FlashRef *ref, f32 color);
void Flash_ClipSetTex(Flash *flash, FlashRef *ref, s32 tex);
void Flash_ClipSetUv(Flash *flash, FlashRef *ref, void *rect);
void Flash_ClipGetPos(Flash *flash, FlashRef *ref, s32 *x, s32 *y);
f32 Flash_ClipGetAlpha(Flash *flash, FlashRef *ref);

/* Flash_ClipSetFlags `props` bits. */
#define FLASH_PROP_PLAY 0x001
#define FLASH_PROP_VISIBLE 0x002
#define FLASH_PROP_FLIPX 0x004
#define FLASH_PROP_FLIPY 0x008
#define FLASH_PROP_NO_OFS 0x010
#define FLASH_PROP_ADD 0x020
#define FLASH_PROP_SUB 0x040
#define FLASH_PROP_MASK 0x080
#define FLASH_PROP_MASK_SET 0x100
#define FLASH_PROP_REPEAT 0x200
#define FLASH_PROP_FREE_RUN 0x400

#endif
