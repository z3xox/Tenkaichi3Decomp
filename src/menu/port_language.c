#include "common.h"
#include "sys/pad.h"
#include "menu/port_screen.h"
#include "menu/tbo_art.h"

#ifdef PORT
#include "../../port/src/gs/menu_translation.h"
#include <string.h>

extern int gPortOverlayOpen;
extern void PortA11y_Say(const char *text, int flags);
extern void MenuText_BootLanguageChosen(void);
extern void Ui_LanguageChosen(const char *locale);
extern void Port_SettingText(const char *name, const char *def, char *out, int size);

#define LANGUAGE_MAX 256
static MenuTranslationOption sLanguage[LANGUAGE_MAX];
static char sLanguageDisplay[LANGUAGE_MAX][128];
static s32 sLanguageArt[3];

typedef struct PortLanguageStyle {
    u8 top[3], bottom[3], outline[3], outer[3];
    f32 outerWidth;
} PortLanguageStyle;

/* The reference's pale text and blue edge; the chosen name turns yellow with a thin white rim. */
static const PortLanguageStyle kLanguageStyle[2] = {
    {{255, 255, 255}, {222, 220, 255}, {20, 16, 105}, {0, 0, 0}, 0.0f},
    {{255, 251, 25}, {255, 218, 8}, {12, 16, 87}, {255, 255, 255}, 1.5f}
};

static u32 PortLanguage_Color(const u8 rgb[3]) {
    return Ps_Rgba(rgb[0], rgb[1], rgb[2], 0x80);
}

/* Uppercase the visible Latin language names without changing their metadata or spoken names. */
static void PortLanguage_UppercaseName(char *out, s32 capacity, const char *name) {
    s32 i = 0, used = 0;
    while (name[i] != 0 && used + 3 < capacity) {
        u8 a = (u8)name[i], b = (u8)name[i + 1];
        if (a >= 'a' && a <= 'z') {
            out[used++] = (char)(a - 'a' + 'A');
            i++;
        } else if (a == 0xC3 && b >= 0xA0 && b <= 0xBE && b != 0xB7) {
            out[used++] = (char)0xC3;
            out[used++] = (char)(b - 0x20);
            i += 2;
        } else if (a == 0xC3 && b == 0x9F) { /* ß -> SS */
            out[used++] = 'S'; out[used++] = 'S'; i += 2;
        } else if (a == 0xC3 && b == 0xBF) { /* ÿ -> Ÿ */
            out[used++] = (char)0xC5; out[used++] = (char)0xB8; i += 2;
        } else {
            out[used++] = name[i++];
        }
    }
    out[used] = 0;
}

static void PortLanguage_Text(f32 y, char *name, s32 selected, f32 heightScale) {
    const PortLanguageStyle *style = &kLanguageStyle[selected != 0];
    f32 widthScale = selected ? 2.00f : 1.75f;
    f32 top = Ps_TextTop(y, heightScale);
    Ps_TextExOuterXY(256.0f, top, name, widthScale, heightScale, 252.0f,
                   PortLanguage_Color(style->top), PortLanguage_Color(style->bottom),
                   PortLanguage_Color(style->outline), PortLanguage_Color(style->outer),
                   style->outerWidth);
    if (selected) {
        /* Expand the blue edge over the inner half of the white rim, leaving white outermost. */
        Ps_TextExOuterXY(256.0f, top, name, widthScale, heightScale, 252.0f,
                       PortLanguage_Color(style->top), PortLanguage_Color(style->bottom),
                       PortLanguage_Color(style->outline), PortLanguage_Color(style->outline), 0.75f);
    }
}

static f32 PortLanguage_Width(char *name, f32 scale) {
    f32 width = Ps_TextWidth(name, scale);
    /* Match Ps_TextExOuter's width limit and minimum fit for the selected name. */
    if (width > 252.0f) {
        width = width * 0.6f > 252.0f ? width * 0.6f : 252.0f;
    }
    return width;
}

/* Smooth back-and-forth motion without pulling the host math header into game code. */
static f32 PortLanguage_Wave(s32 frame, s32 period) {
    f32 phase = (f32)(frame % period) / (f32)period;
    f32 rise = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    return 2.0f * rise * rise * (3.0f - 2.0f * rise) - 1.0f;
}

static void PortLanguage_Draw(s32 choice, s32 count, s32 selectionFrames) {
    s32 visible = count < 7 ? count : 7;
    s32 first = choice - visible / 2;
    s32 slot, index;
    f32 row = 50.5f, top;
    f32 pulse = PortLanguage_Wave(selectionFrames, 52);
    f32 selectedHeightScale = 2.00f + 0.12f * pulse;
    f32 arrowShift = 4.0f * PortLanguage_Wave(selectionFrames, 80);

    if (first < 0) first = 0;
    if (first > count - visible) first = count - visible;
    top = (448.0f - row * (f32)visible) * 0.5f;
    Ps_PageSet();
    Ps_SetShift(0.0f);
    Ps_Rect(Ps_Left(), 0.0f, 512.0f - Ps_Left(), 448.0f,
            Ps_Rgba(0, 0, 0, 0x80), Ps_Rgba(0, 0, 0, 0x80));
    if (sLanguageArt[0]) {
        const f32 topV = 0.17f, bottomV = 0.86f;
        /* The PNG starts with a solid dark band. Crop it from both halves, then stretch
           the remaining picture to the full screen height. */
        Ps_SpriteScreen(TBO_LANG_BG, 0.0f, 0.0f, 256.0f, 448.0f,
                        1.0f, bottomV, 0.0f, topV, PS_PLAIN, 0x80);
        Ps_SpriteScreen(TBO_LANG_BG, 256.0f, 0.0f, 512.0f, 448.0f,
                        0.0f, topV, 1.0f, bottomV, PS_PLAIN, 0x80);
    }
    Ps_Align(1);
    for (slot = 0; slot < visible; slot++) {
        f32 y = top + (f32)slot * row;
        index = first + slot;
        if (index == choice) {
            u32 center = Ps_Rgba(30, 144, 132, 0x49 + (s32)((pulse + 1.0f) * 6.0f));
            u32 edge = Ps_Rgba(9, 55, 56, 0);
            /* About the selected text's cap height (27 units), plus room for its outline. */
            f32 effectTop = y + 8.0f, effectBottom = y + 40.0f;
            Ps_Quad(72.0f, effectTop, 170.0f, effectTop, 72.0f, effectBottom, 170.0f, effectBottom,
                    edge, center, edge, center);
            Ps_Rect(170.0f, effectTop, 342.0f, effectBottom, center, center);
            Ps_Quad(342.0f, effectTop, 440.0f, effectTop, 342.0f, effectBottom, 440.0f, effectBottom,
                    center, edge, center, edge);
        }
        PortLanguage_Text(y + 24.0f, sLanguageDisplay[index], index == choice,
                          index == choice ? selectedHeightScale : 1.75f);
        if (index == choice) {
            f32 halfText = PortLanguage_Width(sLanguageDisplay[index], 2.00f) * 0.5f;
            f32 leftArrow = 256.0f - halfText - 55.0f + arrowShift;
            f32 rightArrow = 256.0f + halfText + 22.0f - arrowShift;
            /* Draw the chevrons last, above the highlight and its lettering. */
            if (sLanguageArt[2])
                Ps_Sprite(TBO_LANG_ARROW_RIGHT, leftArrow, y + 7.0f, leftArrow + 33.0f, y + 40.0f,
                          0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
            if (sLanguageArt[1])
                Ps_Sprite(TBO_LANG_ARROW_LEFT, rightArrow, y + 7.0f, rightArrow + 33.0f, y + 40.0f,
                          0.0f, 0.0f, 1.0f, 1.0f, PS_PLAIN, 0x80);
        }
    }
}

/* Runs before BootCard_Run: the language is saved before the memory card is read. */
void PortLanguage_Run(void) {
    s32 count = MenuTranslation_Available(sLanguage, LANGUAGE_MAX);
    s32 choice = 0, selected = 0, selectionFrames = 0;
    s32 i;
    char saved[16];

    if (!MenuTranslation_ShouldPrompt(sLanguage, count)) return;
    Port_SettingText("menu_lang", "", saved, sizeof(saved));
    for (i = 0; i < count; i++) {
        if (strcmp(sLanguage[i].locale, saved) == 0) { choice = i; break; }
    }
    {
        char path[1200];
        sLanguageArt[0] = MenuTranslation_ArtPath(TBO_LANG_BG, path, sizeof(path));
        sLanguageArt[1] = MenuTranslation_ArtPath(TBO_LANG_ARROW_LEFT, path, sizeof(path));
        sLanguageArt[2] = MenuTranslation_ArtPath(TBO_LANG_ARROW_RIGHT, path, sizeof(path));
    }
    for (i = 0; i < count; i++) {
        PortLanguage_UppercaseName(sLanguageDisplay[i], sizeof(sLanguageDisplay[i]), sLanguage[i].name);
    }
    ColorFade_StartIn(0, 0, 0, 15);
    PortA11y_Say("Select language", 1);
    PortA11y_Say(sLanguage[choice].name, 0);
    while (1) {
        Gfx_BeginFrame();
        Pad_Update();
        Snd_Update();
        ColorFade_Update();
        if (!selected && !gPortOverlayOpen) {
            if (Ps_Move(&choice, count)) {
                selectionFrames = 0;
                PortA11y_Say(sLanguage[choice].name, 1); /* native move sound */
            }
            if (gPad[0].gamePressed & (PADG_CROSS | PADG_START)) {
                Snd_PlaySe(1, 1); /* native confirm sound */
                selected = 1;
                ColorFade_StartOut(0, 0, 0, 15);
            }
        }
        PortLanguage_Draw(choice, count, selectionFrames);
        ColorFade_Draw();
        Gfx_EndFrame(1);
        selectionFrames++;
        Dma_Flush();
        File_Stub264D90();
        if (selected && ColorFade_IsOutDone()) break;
    }
    Dma_ResetBuffers();
    MenuTranslation_SaveChoice(sLanguage[choice].locale);
    MenuText_BootLanguageChosen();
    Ui_LanguageChosen(sLanguage[choice].locale);
}
#endif
