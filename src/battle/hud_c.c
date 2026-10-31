#include "common.h"
#include "battle/hud_types.h"
#include "battle/hud_c.h"
#include "sys/heap.h"

/*
 * Battle HUD: the end of the gauge part, 0x222400-0x222730. These two functions belong to the module of
 * src/battle/hud_b.c (work gHudGauge, built by HudGauge_Init at 0x21FC10): Hud_Term calls HudGauge_Term and
 * Hud_Reset calls HudGauge_Reset. The struct is this file's view of the gauge work; names follow HudGauge of hud_b.c.
 */

extern void *memset(void *dst, s32 c, u32 n);

#define HUDC_GAUGE_NODE_COUNT 12

/* Shake of one node (HudShake of battle/hud_b.h). */

/* Gauge work (0x288 bytes). */

/* Battle end: frees the sprites, every node's sprite and child lists, the nodes and the work. */
void HudGauge_Term(void) {
    s32 i;

    if (gHudGauge->sprites != NULL) {
        Heap_Free(gHudGauge->sprites);
    }
    for (i = 0; i < HUDC_GAUGE_NODE_COUNT; i++) {
        if (gHudGauge->nodes[i].sprites != NULL) {
            Heap_Free(gHudGauge->nodes[i].sprites);
        }
        if (gHudGauge->nodes[i].children != NULL) {
            Heap_Free(gHudGauge->nodes[i].children);
        }
    }
    if (gHudGauge->nodes != NULL) {
        Heap_Free(gHudGauge->nodes);
    }
    if (gHudGauge != NULL) {
        Heap_Free(gHudGauge);
    }
}

/* Round reset: zeroes the values, ramps and shakes of both sides and the whole-part slide. The status icon
   animation is put at rest (state -1). hpDrain, kiDrain, statPos and kiReservePulse are left as they are. */
void HudGauge_Reset(void) {
    s32 i;
    s32 j;

    for (i = 0; i < 2; i++) {
        gHudGauge->hpShown[i] = 0;
        gHudGauge->cur.hp[i] = 0;
        gHudGauge->auraAlpha[i] = 0;
        gHudGauge->blast[i] = 0;
        gHudGauge->ki[i] = 0;
        gHudGauge->kiShown[i] = 0;
        gHudGauge->maxPower[i] = 0;
        gHudGauge->statMask[i] = 0;
        gHudGauge->statPrev[i] = 0;
        gHudGauge->statState[i] = -1;
        memset(gHudGauge->statTo[i], 0, sizeof(gHudGauge->statTo[i]));
        memset(gHudGauge->statFrom[i], 0, sizeof(gHudGauge->statFrom[i]));
        memset(&gHudGauge->statRamp[i], 0, sizeof(Ramp));
        gHudGauge->kiReserve[i] = 0;
        memset(&gHudGauge->kiBarFlash[i], 0, sizeof(Ramp));
        memset(&gHudGauge->kiFullPulse[i], 0, sizeof(Ramp));
        memset(&gHudGauge->blastFlash[i], 0, sizeof(Ramp));
        memset(&gHudGauge->lowHpPulse[i], 0, sizeof(Ramp));
        memset(&gHudGauge->slide[i], 0, sizeof(Ramp));
        memset(&gHudGauge->hpBarFade[i], 0, sizeof(Ramp));
        gHudGauge->faceRes[i] = NULL;
        gHudGauge->obj[i] = NULL;
        gHudGauge->switching[i] = 0;
        memset(&gHudGauge->slide[i], 0, sizeof(Ramp));
        for (j = 0; j < 3; j++) {
            memset(&gHudGauge->shake[i][j], 0, sizeof(HudShake));
        }
    }
    memset(&gHudGauge->slideAll, 0, sizeof(Ramp));
}
