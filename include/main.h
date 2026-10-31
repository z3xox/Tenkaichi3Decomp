#ifndef MAIN_H
#define MAIN_H

#include "types.h"
#include "battle/btl_cam.h"

/* One loadable code overlay (gOverlayTbl; only entry 0 exists). */
typedef struct OverlayEntry {
    /* 0x00 */ char *label; /* "PROGRESS\n" */
    /* 0x04 */ char *path;  /* "cdrom0:\BIN\DBZP.BIN;1" */
} OverlayEntry;

/* Part of the view/camera object that View_ApplyScissor reads (the rest is not known here). */

s32 main(void);
s32 Overlay_Load(s32 idx);
void Sys_RebootIop(void);
void Sys_LoadIopModules(void);
void Game_Main(void);
void View_ApplyScissor(View *view);

#endif
