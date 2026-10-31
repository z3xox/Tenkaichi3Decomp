#include "common.h"
#include "battle/hud_types.h"
#include "battle/hud_b.h"
#include "battle/battle.h"
#include "sys/randf.h"
#include "sys/heap.h"

/*
 * Battle HUD: the gauge part (health, ki, blast stock, powered-up timer, status icons, face), 0x21CA60-0x222400.
 * It continues src/battle/hud_a_d.c (the GS mask states and HudGauge_UpdateHp) and is one module with it: the work
 * is gHudGauge, allocated by HudGauge_Init at the end of this file.
 *
 * The part is one tree of 12 nodes over 76 sprites, built once and shown twice a frame: Hud_Draw selects side 0,
 * updates and draws the tree, then does the same for side 1 mirrored (HudGauge_SelectSide). Everything that
 * differs by side is therefore a [2] array indexed by gHudGauge->side.
 * Hud_PreUpdate copies the fighter state into the work once a frame with the setters at the end of the file; the
 * node update callbacks turn it into sprite rectangles and colours, the draw callbacks draw the sprites.
 * Every animation is skipped while the battle is paused (battle flag 0x100).
 *
 * Gauge, sprite and node records are owned by battle/hud_types.h. The fighter view below is legacy debt.
 */

#define HUDG_PAUSED() (Battle_GetWork()->flags & 0x100)

#define HUDG_HP_BAR 10000      /* health per bar */
#define HUDG_KI_BAR 20000      /* ki per bar, five bars */
#define HUDG_POWER_BAR 6000    /* powered-up timer per bar, five bars */
#define HUDG_BLAST_STOCK 100000

/* One texture entry of a sprite sheet (0x40 bytes). */

/* A sprite sheet. */

/* The side's fighter object as BtlCtrl_GetObj returns it: only what the face drawing reads. */
typedef struct HudBObj {
    /* 0x000 */ u8 unk0[0x50];
    /* 0x050 */ HudRes *faceRes; /* sheet of the face picture; NULL: no face drawn */
    /* 0x054 */ u8 unk54[0xA40 - 0x54];
    /* 0xA40 */ u32 unkA40;       /* bit 0x40000: the face is tinted (0x80, 0x50, 0x70) */
} HudBObj;

/* Gauge work (gHudGauge, 0x288 bytes). */

#define HUDB_MAX(a, b) ((a) < (b) ? (b) : (a))

/* Sprite / node library at 0x224B50.. (not decompiled yet). */
extern void HudSprite_Show(HudSprite *spr, s32 show);
extern void HudSprite_SetColor(HudSprite *spr, s32 r, s32 g, s32 b, s32 a);
extern void HudNode_SetPos(HudNode *node, s32 x, s32 y);        /* node position */
extern void HudSprite_SetRect(HudSprite *spr, s32 x0, s32 x1, s32 y0, s32 y1); /* screen rectangle */
extern void HudSprite_SetUv(HudSprite *spr, s32 u0, s32 u1, s32 v0, s32 v1); /* texel rectangle */
extern void HudSprite_InitPlain(HudSprite *spr, s32 r, s32 g, s32 b, s32 a);     /* untextured, coloured */
extern void HudSprite_Move(HudSprite *spr, s32 dx, s32 dy);                 /* moves the rectangle */
extern void HudNode_Show(HudNode *node, s32 show);
extern void *memcpy(void *dst, const void *src, u32 n);
extern void *memset(void *dst, s32 c, u32 n);
extern f32 powf(f32 x, f32 y);
extern void HudGfx_CallBegin(void (*fn)(void)); /* run a GS state function (begin) */
extern void HudGfx_CallEnd(void (*fn)(void)); /* run a GS state function (end) */
#ifdef PORT
/* PC build, online play: the player who joined is side 1 of the game, and would have their own gauges, team, prompts
   and counters on the right. With the swap (gs/net.c) the two sides change places on THIS copy's screen only: a
   side's data is the same, where it is drawn is the other side's place. HUD_SCR(side) is the place of a side. */
extern int Port_HudSwap(void);
#define HUD_SCR(side) ((side) ^ Port_HudSwap())
#else
#define HUD_SCR(side) (side)
#endif
extern void HudSprite_SetMirror(HudSprite *spr, s32 mirror);
extern void HudSprite_InitTex(HudSprite *spr, void *res, s32 tex, s32 sub);    /* sprite of a sheet's texture */
extern void HudSprite_DrawAlphaClear(HudSprite *spr);
extern void HudSprite_DrawAt(HudSprite *spr, void *res, s32 a, s32 b, s32 c);
extern void HudSprite_Draw(HudSprite *spr, void *res, s32 flag);            /* draw */
extern void HudGauge_GsBeginMask(void);
extern void HudGauge_GsEndMask(void);
extern void HudGauge_GsBeginMask2(void);
extern void HudGauge_GsEndMask2(void);
extern void HudGauge_UpdateHp(void);
extern HudBObj *BtlCtrl_GetObj(s32 side);
extern s32 BtlSide_GetBlastMax(s32 side);
extern s32 BtlSide_IsPoweredUp(s32 side);
extern s32 BtlCtrl_IsActiveDead(s32 side);

/* The six markers of the health bars in reserve (sprites 9..14): one lit per full bar above the bar on show; the
 * one just used up fades out with hpBarFade. Called by HudGauge_DrawHpBarCount, i.e. at draw time. */
void HudGauge_UpdateHpBarCount(void) {
    s32 side = gHudGauge->side;
    s32 bars = gHudGauge->hpShown[side] / 10000;
    s32 i;

    if (gHudGauge->hpShown[side] % 10000 == 0 && bars != 0) {
        bars--;
    }
    for (i = 0; i < 6; i++) {
        HudSprite *spr = &gHudGauge->sprites[9 + i];
        HudSprite_SetColor(spr, 0x80, 0x80, 0x80, 0x80);
        if (i < bars) {
            HudSprite_Show(spr, 1);
        } else if (i == bars) {
            if (gHudGauge->hpBarFade[side].value == 0.0f) {
                HudSprite_Show(spr, 0);
            } else {
                HudSprite_Show(spr, 1);
                HudSprite_SetColor(spr, 0x80, 0x80, 0x80, (u8)(gHudGauge->hpBarFade[side].value * 128.0f));
            }
        } else {
            HudSprite_Show(spr, 0);
        }
    }
}

/* Update of node 0 (root): steps the whole-part slide once a frame (on the side 0 pass) and moves the node up to
 * (-256, -224) away. */
void HudGauge_UpdateRoot(HudNode *node) {
    if (!HUDG_PAUSED() && gHudGauge->side == 0) {
        Ramp_Step(&gHudGauge->slideAll);
    }
    HudNode_SetPos(node, gHudGauge->slideAll.value * -256.0f, gHudGauge->slideAll.value * -224.0f);
}

/* Update of node 1 (the side's panel): steps the side's slide and moves the node; at rest it sits at (-2, 8). */
void HudGauge_UpdatePanel(HudNode *node) {
    Ramp *ramp = &gHudGauge->slide[gHudGauge->side];

    if (!HUDG_PAUSED()) {
        Ramp_Step(ramp);
    }
    HudNode_SetPos(node, (s32)(ramp->value * -256.0f) - 2, (s32)(ramp->value * -224.0f) + 8);
}

/* Update of node 3 (health): applies the three shakes (a random offset of +-amp while the count runs) and lets the
 * trailing health fall towards the real one. Speed: 100 a frame, at least 120 / 150 / 200 once the gap exceeds
 * 5000 / 10000 / 20000; the speed only goes back to 100 when the trail has caught up. */
void HudGauge_UpdateHpTrail(void) {
    s32 side = gHudGauge->side;
    s32 i;

    for (i = 0; i < 3; i++) {
        HudShake *sh = &gHudGauge->shake[side][i];
        HudNode *grp;

        switch (i) {
        case 0:
            grp = &gHudGauge->nodes[1];
            break;
        case 1:
            grp = &gHudGauge->nodes[3];
            break;
        case 2:
            grp = &gHudGauge->nodes[4];
            break;
        default:
            continue;
        }
        if (sh->count != 0) {
            if (HUDG_PAUSED()) {
                continue;
            }
            sh->count--;
            grp->ofsX = Rand_IntRange(-sh->amp, sh->amp);
            grp->ofsY = Rand_IntRange(-sh->amp, sh->amp);
        } else {
            grp->ofsX = 0;
            grp->ofsY = 0;
        }
    }
    if (!HUDG_PAUSED()) {
        Ramp_Step(&gHudGauge->hpBarFade[side]);
        if (gHudGauge->cur.hp[side] < gHudGauge->hpShown[side]) {
            if (gHudGauge->cur.hp[side] < gHudGauge->hpShown[side] - 20000) {
                gHudGauge->hpDrain[side] = HUDB_MAX(gHudGauge->hpDrain[side], 200);
            } else if (gHudGauge->cur.hp[side] < gHudGauge->hpShown[side] - 10000) {
                gHudGauge->hpDrain[side] = HUDB_MAX(gHudGauge->hpDrain[side], 150);
            } else if (gHudGauge->cur.hp[side] < gHudGauge->hpShown[side] - 5000) {
                gHudGauge->hpDrain[side] = HUDB_MAX(gHudGauge->hpDrain[side], 120);
            }
            if (gHudGauge->hpShown[side] > 10000 && gHudGauge->hpShown[side] % 10000 > 0 &&
                gHudGauge->hpShown[side] % 10000 <= gHudGauge->hpDrain[side]) {
                Ramp_Start(&gHudGauge->hpBarFade[side], 0.2f, 1.0f, 0.0f);
            }
            gHudGauge->hpShown[side] -= gHudGauge->hpDrain[side];
            if (gHudGauge->hpShown[side] < gHudGauge->cur.hp[side]) {
                gHudGauge->hpShown[side] = gHudGauge->cur.hp[side];
            }
        } else {
            gHudGauge->hpShown[side] = gHudGauge->cur.hp[side];
            gHudGauge->hpDrain[side] = 100;
        }
    }
}


void HudGauge_UpdateAura(void) {
    s32 ofs[2];
    s16 tbl[10][4] = {
        { 0x00, 0x40, 0x00, 0x40 }, { 0x40, 0x80, 0x00, 0x40 }, { 0x80, 0xC0, 0x00, 0x40 }, { 0xC0, 0x100, 0x00, 0x40 },
        { 0x00, 0x40, 0x40, 0x80 }, { 0x40, 0x80, 0x40, 0x80 }, { 0x80, 0xC0, 0x40, 0x80 }, { 0xC0, 0x100, 0x40, 0x80 },
        { 0x00, 0x40, 0x80, 0xC0 }, { 0x40, 0x80, 0x80, 0xC0 },
    };
    s32 n;
    s32 i;
    HudSprite *spr;
    Ramp *ramp;

    if (BtlSide_IsPoweredUp(gHudGauge->side) && !BtlCtrl_IsActiveDead(gHudGauge->side)) {
        gHudGauge->auraAlpha[gHudGauge->side] += 0x10;
        if (gHudGauge->auraAlpha[gHudGauge->side] > 0x80) {
            gHudGauge->auraAlpha[gHudGauge->side] = 0x80;
        }
    } else {
        gHudGauge->auraAlpha[gHudGauge->side] -= 0x10;
        if (gHudGauge->auraAlpha[gHudGauge->side] < 0) {
            gHudGauge->auraAlpha[gHudGauge->side] = 0;
        }
    }
    for (i = 0; i < 10; i++) {
        spr = &gHudGauge->sprites[53 + i];
        if (HUDG_PAUSED()) {
            n = i;
        } else {
            for (;;) {
                n = Rand_IntRange(0, 9);
                if (tbl[n][0] != spr->uv.x0 || tbl[n][1] != spr->uv.x1 || tbl[n][2] != spr->uv.y0 ||
                    tbl[n][3] != spr->uv.y1) {
                    break;
                }
            }
        }
        spr->uv.x0 = tbl[n][0];
        spr->uv.x1 = tbl[n][1];
        spr->uv.y0 = tbl[n][2];
        spr->uv.y1 = tbl[n][3];
        if (HUDG_PAUSED()) {
            ofs[0] = 0;
            ofs[1] = -8;
        } else {
            ofs[0] = Rand_IntRange(-5, 5);
            ofs[1] = Rand_IntRange(-5, 5) - 8;
        }
        HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
        HudSprite_Move(spr, (i % 5) * 46 + ofs[0], (i / 5) * 20 + ofs[1]);
        HudSprite_SetColor(spr, 0x80, 0x80, 0x80, (u8)gHudGauge->auraAlpha[gHudGauge->side]);
    }
    spr = &gHudGauge->sprites[62];
    HudSprite_InitPlain(spr, 0, 0, 0xFF, (u8)((f32)gHudGauge->auraAlpha[gHudGauge->side] * 0.0078125f * 44.0f));
    HudSprite_SetRect(spr, 0, 0x180, 0, 0x6C);
    ramp = &gHudGauge->lowHpPulse[gHudGauge->side];
    if (gHudGauge->cur.hp[gHudGauge->side] <= 10000 && !BtlCtrl_IsActiveDead(gHudGauge->side)) {
        if (!HUDG_PAUSED() && Ramp_Step(ramp)) {
            if (ramp->value == 0.0f) {
                Ramp_Start(ramp, 0.5f, 0.0f, 1.0f);
            } else {
                Ramp_Start(ramp, 0.5f, 1.0f, 0.0f);
            }
        }
    } else {
        if (!HUDG_PAUSED() && Ramp_Step(ramp) && ramp->value == 1.0f) {
            Ramp_Start(ramp, 0.5f, 1.0f, 0.0f);
        }
    }
    spr = &gHudGauge->sprites[63];
    HudSprite_InitPlain(spr, 0xFF, 0, 0, (u8)(ramp->value * 44.0f));
    HudSprite_SetRect(spr, 0, 0x180, 0, 0x6C);
}

/* Update of node 4 (ki): trailing ki (400 a frame, at least 606 / 800 for a gap over 20000 / 40000), then three
 * layers of five bars: the trailing ki (sprites 20..24), the ki (25..29) and the reserve (15..19, pulsing red).
 * A partly filled bar is cut from the top: n = part * 17 / 20000 rows of 17. */
void HudGauge_UpdateKi(void) {
    s32 shown = gHudGauge->kiShown[gHudGauge->side];
    s32 ki = gHudGauge->ki[gHudGauge->side];
    s32 extra = gHudGauge->kiReserve[gHudGauge->side];
    s32 i;
    s32 lo;
    s32 n;
    HudSprite *spr;

    if (ki < shown - 40000) {
        gHudGauge->kiDrain[gHudGauge->side] = HUDB_MAX(gHudGauge->kiDrain[gHudGauge->side], 800);
    } else if (ki < shown - 20000) {
        gHudGauge->kiDrain[gHudGauge->side] = HUDB_MAX(gHudGauge->kiDrain[gHudGauge->side], 606);
    } else {
        gHudGauge->kiDrain[gHudGauge->side] = HUDB_MAX(gHudGauge->kiDrain[gHudGauge->side], 400);
    }
    if (!HUDG_PAUSED() && ki < gHudGauge->kiShown[gHudGauge->side]) {
        gHudGauge->kiShown[gHudGauge->side] -= gHudGauge->kiDrain[gHudGauge->side];
    }
    if (ki >= gHudGauge->kiShown[gHudGauge->side]) {
        gHudGauge->kiShown[gHudGauge->side] = ki;
        gHudGauge->kiDrain[gHudGauge->side] = 400;
    }
    shown = gHudGauge->kiShown[gHudGauge->side];
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->sprites[20 + i];
        if (shown >= i * 20000 + 20000) {
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
        } else if (i * 20000 >= shown) {
            HudSprite_Show(spr, 0);
        } else {
            n = (shown % 20000) * 17 / 20000;
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0x11 - n, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x21 - n, 0x21);
        }
        HudSprite_Move(spr, i * 8, 0);
    }
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->sprites[25 + i];
        if (ki >= i * 20000 + 20000) {
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
        } else if (i * 20000 >= ki) {
            HudSprite_Show(spr, 0);
        } else {
            n = ki - i * 20000;
            n = n * 17 / 20000;
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0x11 - n, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x21 - n, 0x21);
        }
        HudSprite_Move(spr, i * 8, 0);
    }
    if (Ramp_Step(&gHudGauge->kiReservePulse[gHudGauge->side]) && extra != 0) {
        if (gHudGauge->kiReservePulse[gHudGauge->side].value == 0.0f) {
            Ramp_Start(&gHudGauge->kiReservePulse[gHudGauge->side], 0.3f, 0.0f, 1.0f);
        } else {
            Ramp_Start(&gHudGauge->kiReservePulse[gHudGauge->side], 0.3f, 1.0f, 0.0f);
        }
    }
    n = extra / 20000;
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->sprites[15 + i];
        HudSprite_SetColor(spr, (u8)(gHudGauge->kiReservePulse[gHudGauge->side].value * 192.0f), 0, 0, 0x40);
        if (extra >= i * 20000 + 20000) {
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
        } else if (i * 20000 >= extra) {
            HudSprite_Show(spr, 0);
        } else {
            n = extra - i * 20000;
            n = n * 17 / 20000;
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0x11 - n, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x21 - n, 0x21);
        }
        HudSprite_Move(spr, i * 8, 0);
    }
}

/* Update of node 6: the five bars of the powered-up timer (sprites 30..34), 6000 each, drawn like the ki bars. */
void HudGauge_UpdateMaxPower(void) {
    s32 *p = gHudGauge->maxPower;
    s32 v = p[gHudGauge->side];
    s32 i;
    s32 n;
    HudSprite *spr;

    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->sprites[30 + i];
        if (v >= i * 6000 + 6000) {
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
        } else if (i * 6000 >= v) {
            HudSprite_Show(spr, 0);
        } else {
            n = (v - i * 6000) * 17 / 6000;
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0x11 - n, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x21 - n, 0x21);
        }
        HudSprite_Move(spr, i * 8, 0);
    }
}

/* Update of node 5: the lamps over the ki bars (sprites 65..69). The lamp of the bar just filled fades with
 * kiBarFlash; with all five bars full (ki > 99999) and that fade over, all five pulse with kiFullPulse. */
void HudGauge_UpdateKiLamps(void) {
    Ramp *a = &gHudGauge->kiBarFlash[gHudGauge->side];
    Ramp *b = &gHudGauge->kiFullPulse[gHudGauge->side];
    s32 ki = gHudGauge->ki[gHudGauge->side];
    s32 bars = ki / 20000;
    s32 i;
    s32 r;
    u8 alpha;
    HudSprite *spr;

    if (!HUDG_PAUSED()) {
        r = Ramp_Step(a);
    } else {
        r = a->value == a->target;
    }
    if (r) {
        if (!HUDG_PAUSED()) {
            r = Ramp_Step(b);
        } else {
            r = 0;
        }
        if (r) {
            if (b->value == 0.0f) {
                Ramp_Start(b, 1.0f, 0.0f, 1.0f);
            } else {
                Ramp_Start(b, 1.0f, 1.0f, 0.0f);
            }
        }
        if (ki > 99999) {
            alpha = b->value * 128.0f;
            for (i = 0; i < 5; i++) {
                spr = &gHudGauge->sprites[65 + i];
                HudSprite_Show(spr, 1);
                HudSprite_SetColor(spr, 0x80, 0x80, 0x80, alpha);
            }
            return;
        }
    }
    alpha = a->value * 128.0f;
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->sprites[65 + i];
        if (bars == i + 1) {
            HudSprite_Show(spr, 1);
            HudSprite_SetColor(spr, 0x80, 0x80, 0x80, alpha);
        } else {
            HudSprite_Show(spr, 0);
        }
    }
}

/* Update of node 7: one lamp (sprites 70..74) per full bar of the powered-up timer, at 0.8 * kiFullPulse; a lit
 * lamp hides the ki lamp under it. */
void HudGauge_UpdateMaxPowerLamps(HudNode *node) {
    s32 v = gHudGauge->maxPower[gHudGauge->side];
    Ramp *b = &gHudGauge->kiFullPulse[gHudGauge->side];
    s32 bars;
    s32 i;
    u8 alpha;
    HudSprite *spr;

    HudNode_Show(node, 1);
    bars = v / 6000;
    alpha = b->value * 0.8f * 128.0f;
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->sprites[70 + i];
        if (bars >= i + 1) {
            HudSprite_Show(spr, 1);
            HudSprite_SetColor(spr, 0x80, 0x80, 0x80, alpha);
            HudSprite_Show(&gHudGauge->sprites[65 + i], 0);
        } else {
            HudSprite_Show(spr, 0);
        }
    }
}

/* Update of node 8 (blast stock): the digit 0..7 (sprite 35), a copy of it that flashes (75: once when a stock is
 * gained, pulsing while the stock is full), and the fill bar towards the next stock (37, 25 px for 100000),
 * hidden with its frame (36, 38, 48) when the stock is full. */
void HudGauge_UpdateBlast(void) {
    Ramp *ramp = &gHudGauge->blastFlash[gHudGauge->side];
    s32 blast = gHudGauge->blast[gHudGauge->side];
    u16 tbl[8][4] = {
        { 0x00, 0x20, 0x00, 0x20 }, { 0x20, 0x40, 0x00, 0x20 }, { 0x40, 0x60, 0x00, 0x20 }, { 0x60, 0x80, 0x00, 0x20 },
        { 0x00, 0x20, 0x20, 0x40 }, { 0x20, 0x40, 0x20, 0x40 }, { 0x40, 0x60, 0x20, 0x40 }, { 0x60, 0x80, 0x20, 0x40 },
    };
    s32 max = BtlSide_GetBlastMax(gHudGauge->side);
    HudSprite *spr;
    u32 n;
    s32 r;

    n = blast / 100000;
    spr = &gHudGauge->sprites[35];
    if (n >= 8) {
        n = 7;
    }
    HudSprite_SetRect(spr, -0x20, 0, 0, 0x20);
    HudSprite_SetUv(spr, tbl[n][0], tbl[n][1], tbl[n][2], tbl[n][3]);
    HudSprite_Move(spr, 10, -16);
    if (!HUDG_PAUSED()) {
        r = Ramp_Step(ramp);
    } else {
        r = 0;
    }
    if (r) {
        if (blast >= max) {
            if (ramp->value == 0.0f) {
                Ramp_Start(ramp, 1.0f, 0.0f, 1.0f);
            } else {
                Ramp_Start(ramp, 1.0f, 1.0f, 0.0f);
            }
        } else if (ramp->value == 1.0f) {
            Ramp_Start(ramp, 1.0f, 1.0f, 0.0f);
        }
    }
    spr = &gHudGauge->sprites[75];
    HudSprite_SetRect(spr, -0x20, 0, 0, 0x20);
    HudSprite_SetUv(spr, tbl[n][0], tbl[n][1], tbl[n][2], tbl[n][3]);
    HudSprite_Move(spr, 10, -16);
    if (ramp->value > 0.0f) {
        HudSprite_Show(spr, 1);
    } else {
        HudSprite_Show(spr, 0);
    }
    HudSprite_SetColor(spr, 0x80, 0x80, 0x80, (u8)(ramp->value * 128.0f));
    n = blast % 100000;
    n = n * 25 / 100000;
    spr = &gHudGauge->sprites[37];
    HudSprite_SetRect(spr, n, n + 0x1A, 0, 6);
    HudSprite_Move(spr, 5, 3);
    if (blast >= max) {
        HudSprite_Show(&gHudGauge->sprites[36], 0);
        HudSprite_Show(&gHudGauge->sprites[38], 0);
        HudSprite_Show(&gHudGauge->sprites[37], 0);
    } else {
        HudSprite_Show(&gHudGauge->sprites[36], 1);
        HudSprite_Show(&gHudGauge->sprites[38], 1);
        HudSprite_Show(&gHudGauge->sprites[37], 1);
    }
    spr = &gHudGauge->sprites[48];
    if (blast >= max) {
        HudSprite_Show(spr, 0);
    } else {
        HudSprite_Show(spr, 1);
    }
}

/* Update of node 9: the four status icons (sprites 43..46, textures 19..22) with their frames (39..42, 49..52).
 * After HudGauge_SetStatIcons changed the set: 0/1 fade out the icons that went (0.2 s), 2/3 slide the ones that
 * stay to their new places (0.3 s, cubic ease-out), 4/5 fade in the new ones (0.2 s); the state then runs past 5
 * (or is -1) and the icons are drawn at rest. An icon is bright if its bit of statPos is set, dark otherwise. */
void HudGauge_UpdateStatIcons(void) {
    s32 side = gHudGauge->side;
    u32 mask = gHudGauge->statMask[side];
    u32 pos = gHudGauge->statPos[side];
    u32 prev = gHudGauge->statPrev[side];
    s32 *state = &gHudGauge->statState[side];
    s32 *to = gHudGauge->statTo[side];
    Ramp *ramp = &gHudGauge->statRamp[side];
    HudSprite *icon = &gHudGauge->sprites[43];
    HudSprite *frames = &gHudGauge->sprites[39];
    HudSprite *shades = &gHudGauge->sprites[49];
    u8 col[2][4] = { { 0x80, 0x80, 0x80, 0x80 }, { 0x40, 0x40, 0x40, 0x80 } };
    s32 *from = gHudGauge->statFrom[side];
    u32 removed = (mask ^ prev) & prev;
    u32 added = (mask ^ prev) & mask;
    u32 kept = mask & prev;
    s32 r;
    s32 i;
    s32 x;
    u8 *c;
    f32 t;

    if (*state == 0) {
        if (removed != 0) {
            *state = 0;
        } else if (added != 0) {
            *state = 2;
        } else {
            *state = -1;
        }
    }
    switch (*state) {
    case 0:
        Ramp_Start(ramp, 0.2f, 1.0f, 0.0f);
        (*state)++;
    case 1:
        if (!HUDG_PAUSED()) {
            r = Ramp_Step(ramp);
        } else {
            r = 0;
        }
        for (i = 0; i < 4; i++) {
            c = (pos & (1 << i)) ? col[0] : col[1];
            HudSprite_InitTex(icon, gHudGauge->res, i + 0x13, 0);
            if (removed & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_SetColor(icon, c[0], c[1], c[2], (u8)((f32)c[3] * ramp->value));
                HudSprite_Show(&frames[i], 1);
                HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, (u8)(ramp->value * 128.0f));
                HudSprite_Show(&shades[i], 0);
            } else if (prev & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_SetColor(icon, c[0], c[1], c[2], c[3]);
                HudSprite_Show(&frames[i], 1);
                HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, 0x80);
                HudSprite_Show(&shades[i], 1);
            } else {
                HudSprite_Show(icon, 0);
                HudSprite_Show(&frames[i], 0);
                HudSprite_Show(&shades[i], 0);
            }
            HudSprite_Move(icon, 5, -5);
            HudSprite_Move(icon, from[i], 0);
            HudSprite_SetRect(&frames[i], 0, 0x2A, 0, 0x16);
            HudSprite_SetUv(&frames[i], 0, 0x2A, 0x16, 0x2C);
            HudSprite_Move(&frames[i], 0x66, 0x18);
            HudSprite_Move(&frames[i], from[i], 0);
            HudSprite_SetRect(&shades[i], 3, 0x27, 1, 0x15);
            HudSprite_SetUv(&shades[i], 0, 0x24, 0x2C, 0x40);
            HudSprite_Move(&shades[i], 0x66, 0x18);
            HudSprite_Move(&shades[i], from[i], 0);
            icon++;
        }
        if (r) {
            (*state)++;
        }
        break;
    case 2:
        Ramp_Start(ramp, 0.3f, 0.0f, 1.0f);
        (*state)++;
    case 3:
        if (!HUDG_PAUSED()) {
            r = Ramp_Step(ramp);
        } else {
            r = 0;
        }
        for (i = 0; i < 4; i++) {
            c = (pos & (1 << i)) ? col[0] : col[1];
            HudSprite_InitTex(icon, gHudGauge->res, i + 0x13, 0);
            if (kept & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_Show(&frames[i], 1);
                HudSprite_Show(&shades[i], 1);
            } else {
                HudSprite_Show(icon, 0);
                HudSprite_Show(&frames[i], 0);
                HudSprite_Show(&shades[i], 0);
            }
            t = 1.0f - powf(1.0f - ramp->value, 3.0f);
            x = (f32)from[i] + (f32)(to[i] - from[i]) * t;
            HudSprite_SetColor(icon, c[0], c[1], c[2], c[3]);
            HudSprite_Move(icon, 5, -5);
            HudSprite_Move(icon, x, 0);
            HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, 0x80);
            HudSprite_SetRect(&frames[i], 0, 0x2A, 0, 0x16);
            HudSprite_SetUv(&frames[i], 0, 0x2A, 0x16, 0x2C);
            HudSprite_Move(&frames[i], 0x66, 0x18);
            HudSprite_Move(&frames[i], x, 0);
            HudSprite_SetRect(&shades[i], 3, 0x27, 1, 0x15);
            HudSprite_SetUv(&shades[i], 0, 0x24, 0x2C, 0x40);
            HudSprite_Move(&shades[i], 0x66, 0x18);
            HudSprite_Move(&shades[i], x, 0);
            icon++;
        }
        if (r) {
            if (added != 0) {
                (*state)++;
            } else {
                *state = -1;
            }
        }
        break;
    case 4:
        Ramp_Start(ramp, 0.2f, 0.0f, 1.0f);
        (*state)++;
    case 5:
        if (!HUDG_PAUSED()) {
            r = Ramp_Step(ramp);
        } else {
            r = 0;
        }
        for (i = 0; i < 4; i++) {
            c = (pos & (1 << i)) ? col[0] : col[1];
            HudSprite_InitTex(icon, gHudGauge->res, i + 0x13, 0);
            if (added & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_SetColor(icon, c[0], c[1], c[2], (u8)((f32)c[3] * ramp->value));
                HudSprite_Show(&frames[i], 1);
                HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, (u8)(ramp->value * 128.0f));
                HudSprite_Show(&shades[i], 0);
            } else if (mask & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_SetColor(icon, c[0], c[1], c[2], c[3]);
                HudSprite_Show(&frames[i], 1);
                HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, 0x80);
                HudSprite_Show(&shades[i], 1);
            } else {
                HudSprite_Show(icon, 0);
                HudSprite_Show(&frames[i], 0);
                HudSprite_Show(&shades[i], 0);
            }
            HudSprite_Move(icon, 5, -5);
            HudSprite_Move(icon, to[i], 0);
            HudSprite_SetRect(&frames[i], 0, 0x2A, 0, 0x16);
            HudSprite_SetUv(&frames[i], 0, 0x2A, 0x16, 0x2C);
            HudSprite_Move(&frames[i], 0x66, 0x18);
            HudSprite_Move(&frames[i], to[i], 0);
            HudSprite_SetRect(&shades[i], 3, 0x27, 1, 0x15);
            HudSprite_SetUv(&shades[i], 0, 0x24, 0x2C, 0x40);
            HudSprite_Move(&shades[i], 0x66, 0x18);
            HudSprite_Move(&shades[i], to[i], 0);
            icon++;
        }
        if (r) {
            (*state)++;
        }
        break;
    default:
        for (i = 0; i < 4; i++) {
            c = (pos & (1 << i)) ? col[0] : col[1];
            HudSprite_InitTex(icon, gHudGauge->res, i + 0x13, 0);
            if (mask & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_Show(&frames[i], 1);
                HudSprite_Show(&shades[i], 1);
            } else {
                HudSprite_Show(icon, 0);
                HudSprite_Show(&frames[i], 0);
                HudSprite_Show(&shades[i], 0);
            }
            HudSprite_SetColor(icon, c[0], c[1], c[2], c[3]);
            HudSprite_Move(icon, 5, -5);
            HudSprite_Move(icon, to[i], 0);
            HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, 0x80);
            HudSprite_SetRect(&frames[i], 0, 0x2A, 0, 0x16);
            HudSprite_SetUv(&frames[i], 0, 0x2A, 0x16, 0x2C);
            HudSprite_Move(&frames[i], 0x66, 0x18);
            HudSprite_Move(&frames[i], to[i], 0);
            HudSprite_SetRect(&shades[i], 3, 0x27, 1, 0x15);
            HudSprite_SetUv(&shades[i], 0, 0x24, 0x2C, 0x40);
            HudSprite_Move(&shades[i], 0x66, 0x18);
            HudSprite_Move(&shades[i], to[i], 0);
            icon++;
        }
        break;
    }
}

#ifdef PORT
extern int Port_AspectMilli(void);
extern void Port_GsMarker(s32 effect);

/* Marks the side's life bar so the renderer does not take it for a full-screen flash at very wide ratios. */
static void HudGauge_HpAnchor(s32 stretch) {
    Port_GsMarker((stretch ? 0x15 : 0x10) + (HUD_SCR(gHudGauge->side) != 0));
}

/* Adds half the extra screen width before the renderer narrows the panel, bringing the bar to the timer. */
static s32 HudGauge_WideHpExtra(void) {
    extern int Port_HudLayout(void); /* gs_draw.c: 1 = the long bars were asked for in the settings */
    s32 aspect = Port_AspectMilli();
    return Port_HudLayout() == 1 && aspect > 1340 ? (256 * (aspect * 3 - 4000) + 2000) / 4000 : 0;
}

/* Extends the life-bar frame and its flash mask through a plain strip; the portrait and lower gauges stay put. */
static void HudGauge_DrawWideFrame(HudSprite *spr, s32 clear) {
    s32 extra = HudGauge_WideHpExtra();
    HudSprite piece;
    s32 i;

    if (!extra) {
        if (clear) { HudSprite_DrawAlphaClear(spr); }
        else { HudSprite_Draw(spr, gHudGauge->res, 0); }
        return;
    }
    HudGauge_HpAnchor(1);
    for (i = 0; i < 4; i++) {
        piece = *spr;
        if (i == 3) {
            piece.pos.y0 = piece.uv.y0 = 27;
        } else {
            piece.pos.y1 = piece.uv.y1 = 27;
            if (i == 0) {
                piece.pos.x1 = piece.uv.x1 = 210;
            } else if (i == 1) {
                piece.pos.x0 = piece.uv.x0 = 210;
                piece.uv.x1 = 220;
                piece.pos.x1 = 220 + extra;
            } else {
                piece.uv.x0 = 220;
                piece.pos.x0 = 220 + extra;
                piece.pos.x1 += extra;
            }
        }
        if (clear) { HudSprite_DrawAlphaClear(&piece); }
        else { HudSprite_Draw(&piece, gHudGauge->res, 0); }
    }
    HudGauge_HpAnchor(0);
}
#endif

#ifdef PORT
/* Extends the bar's straight middle, keeping the step and angled tip. The moving mask follows the health
 * fraction over the new width. */
static void HudGauge_DrawHpSprite(s32 index, s32 flag, s32 clear) {
    HudSprite *spr = &gHudGauge->sprites[index];
    s32 extra = HudGauge_WideHpExtra();
    s32 cuts[4] = {0, 64, 144, 160};
    s32 i, origin = spr->pos.x0 * (160 + extra) / 160;
    HudSprite piece;

    if (!extra) {
        if (clear) { HudSprite_DrawAlphaClear(spr); }
        else { HudSprite_Draw(spr, gHudGauge->res, flag); }
        return;
    }
    for (i = 0; i < 3; i++) {
        piece = *spr;
        piece.pos.x0 = origin + cuts[i] + (i == 2 ? extra : 0);
        piece.pos.x1 = origin + cuts[i + 1] + (i >= 1 ? extra : 0);
        piece.uv.x0 = cuts[i];
        piece.uv.x1 = cuts[i + 1];
        if (clear) { HudSprite_DrawAlphaClear(&piece); }
        else { HudSprite_Draw(&piece, gHudGauge->res, flag); }
    }
}
#define HUDG_HP_DRAW(i, flag) HudGauge_DrawHpSprite(i, flag, 0)
#define HUDG_HP_CLEAR() HudGauge_DrawHpSprite(8, 0, 1)
#else
#define HUDG_HP_DRAW(i, flag) HudSprite_Draw(&gHudGauge->sprites[i], gHudGauge->res, flag)
#define HUDG_HP_CLEAR() HudSprite_DrawAlphaClear(&gHudGauge->sprites[8])
#endif

/* Draw of node 3: the health bar is drawn three times (sprites 1..3, 4..6, 7..8), each through the alpha mask of
 * sprite 8; HudGauge_UpdateHp (hud_a_d.c) sets the rectangles first. */
void HudGauge_DrawHp(HudNode *node) {
#ifdef PORT
    HudGauge_HpAnchor(1);
#endif
    HUDG_HP_CLEAR();
    HudGauge_UpdateHp();
    HudGfx_CallBegin(HudGauge_GsBeginMask);
    HUDG_HP_DRAW(1, 0);
    HUDG_HP_DRAW(2, 1);
    HUDG_HP_DRAW(3, 0);
    HudGfx_CallEnd(HudGauge_GsEndMask);
    HUDG_HP_CLEAR();
    HudGfx_CallBegin(HudGauge_GsBeginMask);
    HUDG_HP_DRAW(4, 0);
    HUDG_HP_DRAW(5, 1);
    HUDG_HP_DRAW(6, 0);
    HudGfx_CallEnd(HudGauge_GsEndMask);
    HUDG_HP_CLEAR();
    HudGfx_CallBegin(HudGauge_GsBeginMask);
    HUDG_HP_DRAW(7, 1);
    HUDG_HP_DRAW(8, 0);
    HudGfx_CallEnd(HudGauge_GsEndMask);
#ifdef PORT
    HudGauge_HpAnchor(0);
#endif
}
#undef HUDG_HP_DRAW
#undef HUDG_HP_CLEAR

/* Draw of node 10: sprite 47 as mask, the panel pieces, the red flash and the node's ten sparks. */
void HudGauge_DrawAura(HudNode *node) {
    HudSprite *spr = &gHudGauge->sprites[47];
    u32 i;

#ifdef PORT
    HudGauge_DrawWideFrame(spr, 1);
#else
    HudSprite_DrawAlphaClear(spr);
#endif
    HudGfx_CallBegin(HudGauge_GsBeginMask2);
#ifdef PORT
    HudGauge_DrawWideFrame(spr, 0);
#else
    HudSprite_Draw(spr, gHudGauge->res, 0);
#endif
    HudSprite_Draw(&gHudGauge->sprites[48], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->sprites[49], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->sprites[50], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->sprites[51], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->sprites[52], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->sprites[63], gHudGauge->res, 1);
    for (i = 0; i < node->spriteCount; i++) {
        HudSprite_Draw(node->sprites[i], gHudGauge->res, 1);
    }
    HudGfx_CallEnd(HudGauge_GsEndMask2);
#ifdef PORT
    HudGauge_DrawWideFrame(spr, 1);
#else
    HudSprite_DrawAlphaClear(&gHudGauge->sprites[47]);
#endif
}

/* Draw of node 2: the bars-in-reserve markers. */
void HudGauge_DrawHpBarCount(HudNode *node) {
    u32 i;

    HudGauge_UpdateHpBarCount();
    for (i = 0; i < node->spriteCount; i++) {
        HudSprite_Draw(node->sprites[i], gHudGauge->res, 0);
    }
}

/* Draw of node 8: the fill bar through the mask of sprite 38, then the digit and its flash. */
void HudGauge_DrawBlast(HudNode *node) {
    HudSprite_DrawAlphaClear(&gHudGauge->sprites[38]);
    HudGfx_CallBegin(HudGauge_GsBeginMask);
    HudSprite_Draw(&gHudGauge->sprites[37], gHudGauge->res, 1);
    HudSprite_Draw(&gHudGauge->sprites[38], gHudGauge->res, 0);
    HudGfx_CallEnd(HudGauge_GsEndMask);
    HudSprite_Draw(&gHudGauge->sprites[35], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->sprites[75], gHudGauge->res, 0);
}

/* Clears the mark of texture i of side i's face sheet. */
void HudGauge_ClearFaceTexMark(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        if (gHudGauge->faceRes[i] != NULL) {
            HudTex *tex = gHudGauge->faceRes[i]->tex;

            tex += HUD_SCR(i);
            tex->mark = 0;
        }
    }
}

/* Draw of node 11: the fighter's face. The sheet is taken from the fighter object every frame. */
void HudGauge_DrawFace(HudNode *node) {
    u32 i;
    HudSprite *spr;

    gHudGauge->faceRes[gHudGauge->side] = gHudGauge->obj[gHudGauge->side]->faceRes;
    if (gHudGauge->faceRes[gHudGauge->side] != NULL) {
        HudGauge_ClearFaceTexMark();
        for (i = 0; i < node->spriteCount; i++) {
            spr = node->sprites[i];
            /* (PC build: a fighter's face sheet has two pictures, one drawn for the left place and one for the right;
               with the sides swapped the picture is the one of the place, not of the side) */
            HudSprite_InitTex(spr, gHudGauge->faceRes[gHudGauge->side], HUD_SCR(gHudGauge->side), 0);
            if (BtlCtrl_GetObj(gHudGauge->side)->unkA40 & 0x40000) {
                HudSprite_SetColor(spr, 0x80, 0x50, 0x70, 0x80);
            } else {
                HudSprite_SetColor(spr, 0x80, 0x80, 0x80, 0x80);
            }
            HudSprite_SetMirror(spr, HUD_SCR(gHudGauge->side));
            HudSprite_DrawAt(spr, gHudGauge->faceRes[gHudGauge->side], 0, 0x2C00, 0x2CD0);
        }
    }
}

/* Draw callback of the plain nodes: every sprite of the node. */
void HudGauge_DrawSprites(HudNode *node) {
    u32 i;

    for (i = 0; i < node->spriteCount; i++) {
#ifdef PORT
        if (node->sprites[i] == &gHudGauge->sprites[0]) {
            HudGauge_DrawWideFrame(node->sprites[i], 0);
            continue;
        }
#endif
        HudSprite_Draw(node->sprites[i], gHudGauge->res, 0);
    }
}

/* Selects the side shown by the next update / draw of the tree; side 1 is mirrored. */
#ifdef PORT
/* Widescreen on PC: tells the renderer where the 2D pieces that follow belong (port/src/gs_marker.c). */
#define PORT_2D_LEFT 0x10
#define PORT_2D_RIGHT 0x11
#define PORT_2D_CENTER 0x12
#define PORT_2D_END 0x13
extern void Port_GsMarker(s32 effect);
#endif

void HudGauge_SelectSide(s32 side) {
    s32 flip = HUD_SCR(side) != 0;
    s32 i;

    gHudGauge->side = side;
#ifdef PORT
    Port_GsMarker(HUD_SCR(side) ? PORT_2D_RIGHT : PORT_2D_LEFT);
#endif
    gHudGauge->nodes->flags = (gHudGauge->nodes->flags & ~2) | (flip << 1);
    HudSprite_SetMirror(&gHudGauge->sprites[35], flip);
    HudSprite_SetMirror(&gHudGauge->sprites[75], flip);
    for (i = 0; i < 4; i++) {
        HudSprite_SetMirror(&gHudGauge->sprites[43 + i], flip);
    }
}

/* --- Setters fed by Hud_PreUpdate, by side. --- */
void HudGauge_SetHp(s32 side, s32 hp) {
    gHudGauge->cur.hp[side] = hp;
}

/* Gaining a ki bar restarts the bar's lamp; reaching full ki restarts the pulse. */
void HudGauge_SetKi(s32 side, s32 ki) {
    if (gHudGauge->ki[side] / 20000 < ki / 20000) {
        Ramp_Start(&gHudGauge->kiBarFlash[side], 1.0f, 1.0f, 0.0f);
        if (ki > 99999) {
            Ramp_Start(&gHudGauge->kiFullPulse[side], 1.0f, 0.0f, 1.0f);
        }
    }
    gHudGauge->ki[side] = ki;
}

void HudGauge_SetMaxPower(s32 side, s32 value) {
    gHudGauge->maxPower[side] = value;
}

/* Gaining a stock starts the digit's flash. */
void HudGauge_SetBlast(s32 side, s32 blast) {
    if (gHudGauge->blast[side] / 100000 < blast / 100000) {
        Ramp_Start(&gHudGauge->blastFlash[side], 1.0f, 1.0f, 0.0f);
    }
    gHudGauge->blast[side] = blast;
}

/* mask: icons to show, pos: which of them are positive. On a change keeps the old layout in statFrom, lays the
 * shown icons out 24 apart and restarts the animation. An icon keeps its old brightness unless it is in mask. */
void HudGauge_SetStatIcons(s32 side, s32 mask, s32 pos) {
    s32 n = 0;
    s32 i;

    if (mask == gHudGauge->statMask[side] &&
        gHudGauge->statPos[side] ==
            ((mask & pos) | (gHudGauge->statPrev[side] & gHudGauge->statPos[side] & ~mask))) {
        return;
    }
    gHudGauge->statPrev[side] = gHudGauge->statMask[side];
    gHudGauge->statMask[side] = mask;
    gHudGauge->statPos[side] = (mask & pos) | (gHudGauge->statPrev[side] & gHudGauge->statPos[side] & ~mask);
    gHudGauge->statState[side] = 0;
    memcpy(gHudGauge->statFrom[side], gHudGauge->statTo[side], sizeof(gHudGauge->statTo[side]));
    for (i = 0; i < 4; i++) {
        if (gHudGauge->statMask[side] & (1 << i)) {
            gHudGauge->statTo[side][i] = n * 24;
            n++;
        } else {
            gHudGauge->statTo[side][i] = n * 24;
        }
    }
}

void HudGauge_SetKiReserve(s32 side, s32 value) {
    gHudGauge->kiReserve[side] = value;
}

/* Shake the panel and the health node for `count` frames by +-amp (Hud_PreUpdate: the side that takes combo
 * damage, 5 / 10 / 15 frames by the size of the damage). */
void HudGauge_ShakeHp(s32 side, u16 count, u16 amp) {
    gHudGauge->shake[side][0].count = count;
    gHudGauge->shake[side][0].amp = amp;
    gHudGauge->shake[side][1].count = count;
    gHudGauge->shake[side][1].amp = amp;
}

/* Shake the panel and the ki node (Hud_PreUpdate: 8 frames when the fighter powers up). */
void HudGauge_ShakeKi(s32 side, u16 count, u16 amp) {
    gHudGauge->shake[side][0].count = count;
    gHudGauge->shake[side][0].amp = amp;
    gHudGauge->shake[side][2].count = count;
    gHudGauge->shake[side][2].amp = amp;
}

void HudGauge_SetFighter(s32 side, HudBObj *obj) {
    gHudGauge->obj[side] = obj;
}

/* Slide the whole part away / back in `seconds`. */
void HudGauge_SlideOut(f32 seconds) {
    Ramp_Start(&gHudGauge->slideAll, seconds, 0.0f, 1.0f);
}

void HudGauge_SlideIn(f32 seconds) {
    Ramp_Start(&gHudGauge->slideAll, seconds, 1.0f, 0.0f);
}

/* Hud_PreUpdate passes BtlCtrl_IsSwitching(side): the side's panel slides away (0.3 s) while the member switch
 * runs and back after it; on the way back both trailing values restart from 0, so the bars fill up again. */
void HudGauge_SetSwitching(s32 side, s32 switching) {
    if (gHudGauge->switching[side] != switching) {
        if (switching) {
            Ramp_Start(&gHudGauge->slide[side], 0.3f, 0.0f, 1.0f);
        } else {
            Ramp_Start(&gHudGauge->slide[side], 0.3f, 1.0f, 0.0f);
        }
        if (!switching) {
            gHudGauge->hpShown[side] = 0;
            gHudGauge->kiShown[side] = 0;
        }
    }
    gHudGauge->switching[side] = switching;
}

/* Builds the part: the work, 76 sprites, 12 nodes. Tree: 0 root > 1 panel > { 10 aura, 2 bars in reserve,
 * 3 health, 4 ki > 5 ki lamps, 6 powered-up timer > 7 its lamps, 8 blast stock, 9 status icons, 11 face }.
 * `res` is sprite sheet 0 of the HUD file; the root is returned through `out`. */
void HudGauge_Init(HudNode **out, void *res) {
    HudSprite *spr;
    HudNode *grp;
    s32 i;

    gHudGauge = Heap_Alloc(sizeof(HudGauge), 0x20, 0, HEAP_ANY);
    memset(gHudGauge, 0, sizeof(HudGauge));
    gHudGauge->sprites = Heap_Alloc(76 * sizeof(HudSprite), 0x20, 0, HEAP_ANY);
    memset(gHudGauge->sprites, 0, 76 * sizeof(HudSprite));
    gHudGauge->nodes = Heap_Alloc(12 * sizeof(HudNode), 0x20, 0, HEAP_ANY);
    memset(gHudGauge->nodes, 0, 12 * sizeof(HudNode));
    gHudGauge->res = res;
    for (i = 0; i < 2; i++) {
        gHudGauge->statState[i] = -1;
    }
    for (i = 0; i < 2; i++) {
        gHudGauge->faceRes[i] = NULL;
        gHudGauge->obj[i] = NULL;
    }
    spr = &gHudGauge->sprites[0];
    HudSprite_InitTex(spr, res, 6, 0);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[7];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0x30, 0x40);
    HudSprite_Move(spr, 0xA0, 0);
    spr = &gHudGauge->sprites[8];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[5];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0x30, 0x40);
    HudSprite_Move(spr, 0xA0, 0);
    spr = &gHudGauge->sprites[6];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[2];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0x30, 0x40);
    HudSprite_Move(spr, 0xA0, 0);
    spr = &gHudGauge->sprites[3];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[1];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[4];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[9];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[10];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 9, 0);
    spr = &gHudGauge->sprites[11];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0x12, 0);
    spr = &gHudGauge->sprites[12];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0x1B, 0);
    spr = &gHudGauge->sprites[13];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0x24, 0);
    spr = &gHudGauge->sprites[14];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0x2D, 0);
    spr = &gHudGauge->sprites[15];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 0x20, 0);
    spr = &gHudGauge->sprites[16];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 0x18, 0);
    spr = &gHudGauge->sprites[17];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 0x10, 0);
    spr = &gHudGauge->sprites[18];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 8, 0);
    spr = &gHudGauge->sprites[19];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[20];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x1C, 0);
    spr = &gHudGauge->sprites[21];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x15, 0);
    spr = &gHudGauge->sprites[22];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0xE, 0);
    spr = &gHudGauge->sprites[23];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 7, 0);
    spr = &gHudGauge->sprites[24];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[25];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x1C, 0);
    spr = &gHudGauge->sprites[26];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x15, 0);
    spr = &gHudGauge->sprites[27];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0xE, 0);
    spr = &gHudGauge->sprites[28];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 7, 0);
    spr = &gHudGauge->sprites[29];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[30];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x1C, 0);
    spr = &gHudGauge->sprites[31];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x15, 0);
    spr = &gHudGauge->sprites[32];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0xE, 0);
    spr = &gHudGauge->sprites[33];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 7, 0);
    spr = &gHudGauge->sprites[34];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[35];
    HudSprite_InitTex(spr, res, 0xE, 0);
    HudSprite_SetRect(spr, -32, 0, 0, 0x20);
    HudSprite_SetUv(spr, 0, 0x20, 0, 0x20);
    HudSprite_Move(spr, 0xA, -6);
    spr = &gHudGauge->sprites[36];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x30, 0, 0xC);
    HudSprite_SetUv(spr, 0, 0x30, 0, 0xC);
    HudSprite_Move(spr, 0x1B, 0x2E);
    spr = &gHudGauge->sprites[37];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0x1A, 0, 6);
    HudSprite_SetUv(spr, 0xC0, 0xDA, 0, 6);
    HudSprite_Move(spr, 5, 3);
    spr = &gHudGauge->sprites[38];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x1A, 0, 6);
    HudSprite_SetUv(spr, 0xA1, 0xBB, 0, 6);
    HudSprite_Move(spr, 5, 3);
    spr = &gHudGauge->sprites[39];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x2A, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x2A, 0x16, 0x2C);
    HudSprite_Move(spr, 0, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->sprites[40];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x2A, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x2A, 0x16, 0x2C);
    HudSprite_Move(spr, 0x18, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->sprites[41];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x2A, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x2A, 0x16, 0x2C);
    HudSprite_Move(spr, 0x30, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->sprites[42];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x2A, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x2A, 0x16, 0x2C);
    HudSprite_Move(spr, 0x48, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->sprites[43];
    HudSprite_InitTex(spr, res, 0x13, 0);
    HudSprite_Move(spr, 5, -5);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[44];
    HudSprite_InitTex(spr, res, 0x14, 0);
    HudSprite_Move(spr, 5, -5);
    HudSprite_Move(spr, 0x18, 0);
    spr = &gHudGauge->sprites[45];
    HudSprite_InitTex(spr, res, 0x15, 0);
    HudSprite_Move(spr, 5, -5);
    HudSprite_Move(spr, 0x30, 0);
    spr = &gHudGauge->sprites[46];
    HudSprite_InitTex(spr, res, 0x16, 0);
    HudSprite_Move(spr, 5, -5);
    HudSprite_Move(spr, 0x48, 0);
    spr = &gHudGauge->sprites[47];
    HudSprite_InitTex(spr, res, 7, 0);
    spr = &gHudGauge->sprites[48];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x30, 1, 0xB);
    HudSprite_SetUv(spr, 0, 0x30, 0xC, 0x16);
    HudSprite_Move(spr, 0x1B, 0x2E);
    spr = &gHudGauge->sprites[49];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 3, 0x27, 1, 0x15);
    HudSprite_SetUv(spr, 0, 0x24, 0x2C, 0x40);
    HudSprite_Move(spr, 0, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->sprites[50];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 3, 0x27, 1, 0x15);
    HudSprite_SetUv(spr, 0, 0x24, 0x2C, 0x40);
    HudSprite_Move(spr, 0x18, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->sprites[51];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 3, 0x27, 1, 0x15);
    HudSprite_SetUv(spr, 0, 0x24, 0x2C, 0x40);
    HudSprite_Move(spr, 0x30, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->sprites[52];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 3, 0x27, 1, 0x15);
    HudSprite_SetUv(spr, 0, 0x24, 0x2C, 0x40);
    HudSprite_Move(spr, 0x48, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->sprites[53];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0, 0x40, 0, 0x40);
    spr = &gHudGauge->sprites[54];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x40, 0x80, 0, 0x40);
    spr = &gHudGauge->sprites[55];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x80, 0xC0, 0, 0x40);
    spr = &gHudGauge->sprites[56];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0xC0, 0x100, 0, 0x40);
    spr = &gHudGauge->sprites[57];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0, 0x40, 0x40, 0x80);
    spr = &gHudGauge->sprites[58];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x40, 0x80, 0x40, 0x80);
    spr = &gHudGauge->sprites[59];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x80, 0xC0, 0x40, 0x80);
    spr = &gHudGauge->sprites[60];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0xC0, 0x100, 0x40, 0x80);
    spr = &gHudGauge->sprites[61];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x40, 0x80, 0, 0x40);
    spr = &gHudGauge->sprites[62];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x80, 0xC0, 0, 0x40);
    spr = &gHudGauge->sprites[64];
    HudSprite_Show(spr, 1);
    HudSprite_InitPlain(spr, 0x80, 0x80, 0x80, 0x80);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    spr = &gHudGauge->sprites[65];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[66];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 8, 0);
    spr = &gHudGauge->sprites[67];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 0x10, 0);
    spr = &gHudGauge->sprites[68];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 0x18, 0);
    spr = &gHudGauge->sprites[69];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 0x20, 0);
    spr = &gHudGauge->sprites[70];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->sprites[71];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 8, 0);
    spr = &gHudGauge->sprites[72];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 0x10, 0);
    spr = &gHudGauge->sprites[73];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 0x18, 0);
    spr = &gHudGauge->sprites[74];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 0x20, 0);
    spr = &gHudGauge->sprites[75];
    HudSprite_Show(spr, 1);
    HudSprite_InitTex(spr, res, 0x12, 0);
    HudSprite_SetRect(spr, -32, 0, 0, 0x20);
    HudSprite_SetUv(spr, 0, 0x20, 0, 0x20);
    HudSprite_Move(spr, 0xA, -6);
    grp = &gHudGauge->nodes[9];
    grp->x = 102;
    grp->y = 24;
    grp->spriteCount = 4;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[43];
    grp->sprites[1] = &gHudGauge->sprites[44];
    grp->sprites[2] = &gHudGauge->sprites[45];
    grp->sprites[3] = &gHudGauge->sprites[46];
    grp->update = HudGauge_UpdateStatIcons;
    grp->draw = HudGauge_DrawSprites;
    grp = &gHudGauge->nodes[10];
    grp->spriteCount = 10;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[53];
    grp->sprites[1] = &gHudGauge->sprites[54];
    grp->sprites[2] = &gHudGauge->sprites[55];
    grp->sprites[3] = &gHudGauge->sprites[56];
    grp->sprites[4] = &gHudGauge->sprites[57];
    grp->sprites[5] = &gHudGauge->sprites[58];
    grp->sprites[6] = &gHudGauge->sprites[59];
    grp->sprites[7] = &gHudGauge->sprites[60];
    grp->sprites[8] = &gHudGauge->sprites[61];
    grp->sprites[9] = &gHudGauge->sprites[62];
    grp->update = HudGauge_UpdateAura;
    grp->draw = HudGauge_DrawAura;
    grp = &gHudGauge->nodes[8];
    grp->x = 29;
    grp->y = 46;
    grp->spriteCount = 3;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[35];
    grp->sprites[1] = &gHudGauge->sprites[38];
    grp->sprites[2] = &gHudGauge->sprites[75];
    grp->update = HudGauge_UpdateBlast;
    grp->draw = HudGauge_DrawBlast;
    grp = &gHudGauge->nodes[3];
    grp->x = 69;
    grp->y = 9;
    grp->spriteCount = 8;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[1];
    grp->sprites[1] = &gHudGauge->sprites[2];
    grp->sprites[2] = &gHudGauge->sprites[3];
    grp->sprites[3] = &gHudGauge->sprites[4];
    grp->sprites[4] = &gHudGauge->sprites[5];
    grp->sprites[5] = &gHudGauge->sprites[6];
    grp->sprites[6] = &gHudGauge->sprites[7];
    grp->sprites[7] = &gHudGauge->sprites[8];
    grp->update = HudGauge_UpdateHpTrail;
    grp->draw = HudGauge_DrawHp;
    grp = &gHudGauge->nodes[2];
    grp->x = 73;
    grp->y = 2;
    grp->spriteCount = 6;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[9];
    grp->sprites[1] = &gHudGauge->sprites[10];
    grp->sprites[2] = &gHudGauge->sprites[11];
    grp->sprites[3] = &gHudGauge->sprites[12];
    grp->sprites[4] = &gHudGauge->sprites[13];
    grp->sprites[5] = &gHudGauge->sprites[14];
    grp->draw = HudGauge_DrawHpBarCount;
    grp->update = NULL;
    grp = &gHudGauge->nodes[5];
    grp->x = -3;
    grp->y = -3;
    grp->spriteCount = 5;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[65];
    grp->sprites[1] = &gHudGauge->sprites[66];
    grp->sprites[2] = &gHudGauge->sprites[67];
    grp->sprites[3] = &gHudGauge->sprites[68];
    grp->sprites[4] = &gHudGauge->sprites[69];
    grp->update = HudGauge_UpdateKiLamps;
    grp->draw = HudGauge_DrawSprites;
    grp = &gHudGauge->nodes[7];
    grp->x = -3;
    grp->y = -3;
    grp->spriteCount = 5;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[70];
    grp->sprites[1] = &gHudGauge->sprites[71];
    grp->sprites[2] = &gHudGauge->sprites[72];
    grp->sprites[3] = &gHudGauge->sprites[73];
    grp->sprites[4] = &gHudGauge->sprites[74];
    grp->update = HudGauge_UpdateMaxPowerLamps;
    grp->draw = HudGauge_DrawSprites;
    grp = &gHudGauge->nodes[4];
    grp->spriteCount = 15;
    grp->x = 64;
    grp->y = 27;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[15];
    grp->sprites[1] = &gHudGauge->sprites[16];
    grp->sprites[2] = &gHudGauge->sprites[17];
    grp->sprites[3] = &gHudGauge->sprites[18];
    grp->sprites[4] = &gHudGauge->sprites[19];
    grp->sprites[5] = &gHudGauge->sprites[20];
    grp->sprites[6] = &gHudGauge->sprites[21];
    grp->sprites[7] = &gHudGauge->sprites[22];
    grp->sprites[8] = &gHudGauge->sprites[23];
    grp->sprites[9] = &gHudGauge->sprites[24];
    grp->sprites[10] = &gHudGauge->sprites[25];
    grp->sprites[11] = &gHudGauge->sprites[26];
    grp->sprites[12] = &gHudGauge->sprites[27];
    grp->sprites[13] = &gHudGauge->sprites[28];
    grp->sprites[14] = &gHudGauge->sprites[29];
    grp->childCount = 1;
    grp->children = Heap_Alloc(grp->childCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->children, 0, grp->childCount * sizeof(void *));
    grp->children[0] = &gHudGauge->nodes[5];
    grp->draw = HudGauge_DrawSprites;
    grp->update = HudGauge_UpdateKi;
    grp = &gHudGauge->nodes[6];
    grp->x = 64;
    grp->y = 27;
    grp->spriteCount = 5;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[30];
    grp->sprites[1] = &gHudGauge->sprites[31];
    grp->sprites[2] = &gHudGauge->sprites[32];
    grp->sprites[3] = &gHudGauge->sprites[33];
    grp->sprites[4] = &gHudGauge->sprites[34];
    grp->childCount = 1;
    grp->children = Heap_Alloc(grp->childCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->children, 0, grp->childCount * sizeof(void *));
    grp->children[0] = &gHudGauge->nodes[7];
    grp->draw = HudGauge_DrawSprites;
    grp->update = HudGauge_UpdateMaxPower;
    grp = &gHudGauge->nodes[11];
    grp->x = 16;
    grp->y = 3;
    grp->spriteCount = 1;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[64];
    grp->draw = HudGauge_DrawFace;
    grp->update = NULL;
    grp = &gHudGauge->nodes[1];
    grp->x = -2;
    grp->spriteCount = 6;
    grp->y = 8;
    grp->sprites = Heap_Alloc(grp->spriteCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprites, 0, grp->spriteCount * sizeof(void *));
    grp->sprites[0] = &gHudGauge->sprites[0];
    grp->sprites[1] = &gHudGauge->sprites[36];
    grp->sprites[2] = &gHudGauge->sprites[39];
    grp->sprites[3] = &gHudGauge->sprites[40];
    grp->sprites[4] = &gHudGauge->sprites[41];
    grp->sprites[5] = &gHudGauge->sprites[42];
    grp->childCount = 8;
    grp->children = Heap_Alloc(grp->childCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->children, 0, grp->childCount * sizeof(void *));
    grp->children[0] = &gHudGauge->nodes[10];
    grp->children[1] = &gHudGauge->nodes[2];
    grp->children[2] = &gHudGauge->nodes[3];
    grp->children[3] = &gHudGauge->nodes[4];
    grp->children[4] = &gHudGauge->nodes[6];
    grp->children[5] = &gHudGauge->nodes[8];
    grp->children[6] = &gHudGauge->nodes[9];
    grp->children[7] = &gHudGauge->nodes[11];
    grp->update = HudGauge_UpdatePanel;
    grp->draw = HudGauge_DrawSprites;
    grp = &gHudGauge->nodes[0];
    grp->childCount = 1;
    grp->x = 0;
    grp->y = 0;
    grp->children = Heap_Alloc(grp->childCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->children, 0, grp->childCount * sizeof(void *));
    grp->children[0] = &gHudGauge->nodes[1];
    grp->update = HudGauge_UpdateRoot;
    grp->draw = NULL;
    *out = gHudGauge->nodes;
}
