#include "common.h"
#include "battle/view_a.h"
#include "sys/heap.h"

/*
 * IconWin, 0x25D290..0x25D468: a small movie window ("fl_in" / "fl_out") whose fifth texture is an icon picked
 * from a second sprite resource. Used all over the menu overlay. See battle/view_a.h.
 */

extern void *memset(void *dst, s32 c, u32 n);

extern void Res_RelocateOffsets(void *out, void *base, void *hdr);
extern void Flash_Create(Flash *flash, void *data, void *tex);
extern void Flash_Destroy(Flash *flash);
extern void Flash_Advance(Flash *flash);
extern void Flash_Draw(Flash *flash);
extern void Flash_Play(Flash *flash, s32 arg);
extern void Flash_GotoLabel(Flash *flash, char *label, s32 arg);
#ifdef PORT
extern void Flash_FindLabel(Flash *flash, char *parent, char *name, FlashRef *out);
extern f32 Flash_ClipGetAlpha(Flash *flash, FlashRef *ref);
extern void Flash_ClipGetTransform(Flash *flash, FlashRef *ref, f32 *x, f32 *y, f32 *sx, f32 *sy);
extern s32 Flash_ClipGetShapeTransform(Flash *flash, FlashRef *ref, s32 depth,
                                       f32 *x, f32 *y, f32 *sx, f32 *sy);
#endif

/* Defined here: this object's .sdata (0x2FF0F0). */
IconWin *gIconWin = NULL;
#ifdef PORT
#define PORT_HEADING_OPEN_FRAMES 59 /* main-menu fl_in is frames 1..59; fl_out starts at 60 */
#define PORT_HEADING_ICON_FRAMES 60
#define PORT_HEADING_TITLE_FRAMES 410
typedef struct PortHeadingPart {
    f32 x, y, scaleX, scaleY;
    s32 alpha;
    s32 found;
} PortHeadingPart;
typedef struct PortHeadingFrame {
    PortHeadingPart icon, title;
} PortHeadingFrame;
static PortHeadingFrame sMainHeadingOpen[PORT_HEADING_OPEN_FRAMES];
static PortHeadingPart sMainHeadingIconIdle[PORT_HEADING_ICON_FRAMES];
static PortHeadingPart sMainHeadingTitleIdle[PORT_HEADING_TITLE_FRAMES];
static s32 sMainHeadingReady;
static void *sCurrentHeadingMovie;

static void IconWin_PortSamplePart(Flash *flash, char *name, s32 icon, PortHeadingPart *part) {
    FlashRef ref;
    FlashRef child;
    f32 x, y, sx, sy;
    f32 alpha;

    Flash_FindLabel(flash, NULL, name, &ref);
    part->found = ref.id >= 0;
    part->x = part->y = 0;
    part->alpha = 0;
    part->scaleX = part->scaleY = 1.0f;
    if (!part->found) return;
    Flash_ClipGetTransform(flash, &ref, &part->x, &part->y, &part->scaleX, &part->scaleY);
    alpha = Flash_ClipGetAlpha(flash, &ref);
    if (icon) { /* mc_icon: the emblem's 60-frame vertical loop */
        Flash_FindLabel(flash, "mc_icon", "mc_title_icon", &child);
        if (child.id >= 0) {
            Flash_ClipGetTransform(flash, &child, &x, &y, &sx, &sy);
            part->x += x * part->scaleX;
            part->y += y * part->scaleY;
            part->scaleX *= sx;
            part->scaleY *= sy;
            alpha *= Flash_ClipGetAlpha(flash, &child);
        }
    } else { /* mc_title: shape at depth 2 is the text; depth 1 is the underline */
        if (Flash_ClipGetShapeTransform(flash, &ref, 2, &x, &y, &sx, &sy)) {
            part->x += x * part->scaleX;
            part->y += y * part->scaleY;
            part->scaleX *= sx;
            part->scaleY *= sy;
        }
    }
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    part->alpha = (s32)(alpha * 128.0f);
}
#endif

/* Allocates the window: movie and three textures from the pack, two more from the icon resource. */
void IconWin_Init(u32 *pack, FlashTexRes *icons) {
    FlashTexRes *res = NULL;

    gIconWin = Heap_Alloc(sizeof(IconWin), 0x20, 0, HEAP_ANY);
    memset(gIconWin, 0, sizeof(IconWin));

    res = PACK_AT(pack, 1);
    Res_RelocateOffsets(&res, res, res);
    gIconWin->tex[1] = res->tex;
    gIconWin->tex[0] = res->tex + FLASH_TEX_SIZE;
    gIconWin->tex[3] = res->tex + FLASH_TEX_SIZE * 2;
    gIconWin->tex[2] = icons->tex;
    gIconWin->tex[4] = icons->tex + FLASH_TEX_SIZE;
    gIconWin->icons = icons;

    Flash_Create(&gIconWin->flash, PACK_AT(pack, 2), gIconWin->tex);
    Flash_Play(&gIconWin->flash, 1);
#ifdef PORT
    sCurrentHeadingMovie = PACK_AT(pack, 2);
#endif
}

/* Destroys the movie and frees the window. */
void IconWin_Term(void) {
    Flash_Destroy(&gIconWin->flash);
    if (gIconWin != NULL) {
        Heap_Free(gIconWin);
        gIconWin = NULL;
    }
}

/* Steps and draws the window. */
void IconWin_Draw(void) {
    s32 i;

    Flash_Advance(&gIconWin->flash);
    /* a loop over one movie, as in the other windows: the call is not a tail call */
    for (i = 0; i < 1; i++) {
        Flash_Draw(&gIconWin->flash);
    }
}

/* Starts the opening animation. */
void IconWin_Open(void) {
    Flash_GotoLabel(&gIconWin->flash, "fl_in", 1);
}

/* Starts the closing animation. */
void IconWin_Close(void) {
    Flash_GotoLabel(&gIconWin->flash, "fl_out", 1);
}

/* Shows icon n: texture n + 1 of the icon resource. */
#ifdef PORT
/* PC build, for screens that write their own heading (menu/port_tbo.c): the window without its words (texture
   4) and without its emblem (texture 2), only its clouds and its line; a shape whose texture is missing is not
   drawn. IconWin_SetIcon brings the words back. */
void IconWin_PortHideWords(void) {
    gIconWin->tex[4] = NULL;
    gIconWin->tex[2] = NULL;
}

/* The main menu and Duel menu use different IconWin movies. Keep the main
   menu's opening and child idle timelines before its resource is freed. */
void IconWin_PortCaptureMainHeading(void) {
    Flash preview;
    s32 frame;

    sMainHeadingReady = 0;
    if (gIconWin == NULL || sCurrentHeadingMovie == NULL) return;
    memset(&preview, 0, sizeof(preview));
    Flash_Create(&preview, sCurrentHeadingMovie, gIconWin->tex);
    Flash_Play(&preview, 1);
    Flash_GotoLabel(&preview, "fl_in", 1);
    for (frame = 0; frame < PORT_HEADING_OPEN_FRAMES; frame++) {
        Flash_Advance(&preview);
        IconWin_PortSamplePart(&preview, "mc_icon", 1, &sMainHeadingOpen[frame].icon);
        IconWin_PortSamplePart(&preview, "mc_title", 0, &sMainHeadingOpen[frame].title);
    }
    sMainHeadingReady = sMainHeadingOpen[PORT_HEADING_OPEN_FRAMES - 1].icon.found &&
                        sMainHeadingOpen[PORT_HEADING_OPEN_FRAMES - 1].title.found;
    if (sMainHeadingReady) {
        /* Frame 59 stops the root timeline. Its placed children continue their
           native 60- and 410-frame loops without entering root fl_out. */
        for (frame = 0; frame < PORT_HEADING_TITLE_FRAMES; frame++) {
            Flash_Advance(&preview);
            if (frame < PORT_HEADING_ICON_FRAMES)
                IconWin_PortSamplePart(&preview, "mc_icon", 1, &sMainHeadingIconIdle[frame]);
            IconWin_PortSamplePart(&preview, "mc_title", 0, &sMainHeadingTitleIdle[frame]);
        }
    }
    Flash_Destroy(&preview);
}

/* The Wii art replaces the icon and words, following their own opening and
   continuing child timelines from the main menu movie. */
s32 IconWin_PortHeadingMotion(s32 frame, f32 *iconDx, f32 *iconDy, s32 *iconAlpha,
                              f32 *iconScaleX, f32 *iconScaleY, f32 *titleDx, f32 *titleDy,
                              s32 *titleAlpha, f32 *titleScaleX, f32 *titleScaleY) {
    PortHeadingPart *icon, *title;
    PortHeadingFrame *base;

    if (!sMainHeadingReady) return 0;
    if (frame < 0) frame = 0;
    if (frame < PORT_HEADING_OPEN_FRAMES) {
        icon = &sMainHeadingOpen[frame].icon;
        title = &sMainHeadingOpen[frame].title;
    } else {
        frame -= PORT_HEADING_OPEN_FRAMES;
        icon = &sMainHeadingIconIdle[frame % PORT_HEADING_ICON_FRAMES];
        title = &sMainHeadingTitleIdle[frame % PORT_HEADING_TITLE_FRAMES];
    }
    base = &sMainHeadingOpen[PORT_HEADING_OPEN_FRAMES - 1];
    *iconDx = icon->found ? icon->x - base->icon.x : 0.0f;
    *iconDy = icon->found ? icon->y - base->icon.y : 0.0f;
    *titleDx = title->found ? title->x - base->title.x : 0.0f;
    *titleDy = title->found ? title->y - base->title.y : 0.0f;
    *iconAlpha = icon->alpha;
    *titleAlpha = title->alpha;
    *iconScaleX = icon->found && base->icon.scaleX != 0.0f ? icon->scaleX / base->icon.scaleX : 1.0f;
    *iconScaleY = icon->found && base->icon.scaleY != 0.0f ? icon->scaleY / base->icon.scaleY : 1.0f;
    *titleScaleX = title->found && base->title.scaleX != 0.0f ? title->scaleX / base->title.scaleX : 1.0f;
    *titleScaleY = title->found && base->title.scaleY != 0.0f ? title->scaleY / base->title.scaleY : 1.0f;
    return 1;
}
#endif

void IconWin_SetIcon(s32 icon) {
    if (gIconWin->icons != NULL) {
        gIconWin->tex[4] = gIconWin->icons->tex + icon * FLASH_TEX_SIZE + FLASH_TEX_SIZE;
    }
}
