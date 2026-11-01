// The settings window (ui.cpp) for a screen reader user: what the D-pad lands on is said through PortA11y_Say
// (gs/a11y.c), and the right stick moves the slider that has the focus without activating it first. Plus the
// Accessibility tab (A11yUi_Tab), with the volumes of the accessibility's own sounds and of the game's voices under it.
//
// How the window is read: between NewFrame and EndFrame, Dear ImGui's own text log (LogToBuffer, the one behind "Log to
// clipboard") takes every piece of text as it is drawn, and the item hooks meant for the Dear ImGui Test Engine
// (IMGUI_ENABLE_TEST_ENGINE, set by port/tools/undefined.py) say where each item starts and ends in it, with its id,
// its window, its kind and its state. So the item under the navigation cursor (g.NavId) has its label and its value,
// and the lines drawn before it are its description. Logging changes nothing on the screen: it only adds to a buffer.
//
// The right stick, left or right, steps the focused slider down or up, and repeats while held. It does it the way the
// D-pad does on an active slider: for that one item, the slider sees itself activated by the controller with D-pad left
// or right just pressed, and all of that is put back as it was right after (push_slider / pop_slider).
//
// With the accessibility off (PortA11y_On() == 0) none of this runs: no log, no hook, no tab.
//
// Tests without a controller or a screen reader ($BT3_A11Y_LOG has what is said):
//   $BT3_A11Y_UI_KEYS="<frame>:<key>[:<frames>],..."  presses a key from that frame on, for one frame or <frames>:
//                                                     up down left right a b l1 r1, rs- rs+ (the right stick), and
//                                                     toggle (closes or opens the window, as F1 does)
//   $BT3_A11Y_UI_DEBUG=1                              the items and lines of each frame that says something, to
//                                                     stderr (2: of every frame)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "net_chat.h" // PortChat_Read: the other player's chat lines said
#include "ui.h"       // UiA11y_*: upstream's reader interface, answered at the end

extern "C" {
int PortA11y_On(void); // a11y.c
const char *PortA11y_TrSense(const char *sense, const char *label);
void PortA11y_Say(const char *text, int flags);
int PortA11y_CueVolume(void);
void PortA11y_SetCueVolume(int percent);
int PortA11y_Voices(int fight); // a11y_game.c
void PortA11y_SetVoices(int fight, int percent);
int PortA11y_FightMusic(void);
void PortA11y_SetFightMusic(int percent);
void PortA11y_ApplyProfile(void); // a11y_fight.c: the blind player profile
void PortA11y_AllByItself(void);  // ... and everything told by itself, nothing that needs Select
void Port_SettingsWrite(void); // plat_settings.c
void Ui_Toggle(void); // ui.cpp: opens or closes the window (F1)
const char *PortA11y_PadSourceName(int src); // a11y_pad.c: a controller button by the name of the one in use
int PortA11y_AlertMode(int alert); // a11y_fight.c: how a fight alert is told, 0 off, 1 sound, 2 voice, 3 both
void PortA11y_SetAlertMode(int alert, int mode);
}

#define A11Y_INTERRUPT 1 // (the same in a11y.c)

namespace {

enum Kind { K_NONE, K_BUTTON, K_CHECK, K_RADIO, K_SLIDER, K_EDIT, K_COMBO, K_OPTION, K_TAB, K_HEADER, K_TABLE, K_GROUP, K_OTHER };
enum State { S_NONE, S_CHECKED, S_UNCHECKED, S_PARTIAL, S_SELECTED, S_COLLAPSED, S_EXPANDED };

// One item of the frame, in the order they were submitted.
struct Item {
    ImGuiID id;
    ImGuiWindow *window, *root;
    int from, to; // its text in the log: from ItemAdd to ItemInfo (to: -1 without an ItemInfo after the ItemAdd)
    ImGuiItemFlags flags;
    ImGuiItemStatusFlags status;
    bool data, info, isWindow, menu, header, slider;
    ImGuiTabBar *tabBar; // a tab of this bar
    ImGuiID table;       // in a cell of this table
    int row, column;
    int vtx;             // the window's vertex count at ItemAdd
    ImRect nav;          // its navigation rectangle (absolute)
    ImGuiID scope;       // its focus scope
    float fontSize;
    char label[96];
    char columnName[48];
};

// A line of text of a window, or an item, in the order they were drawn.
struct Piece {
    ImGuiWindow *root;
    int item;  // the item, or -1 for a line of text
    int text;  // the line, in sText
    int label; // an item: the piece of the line of text it is on the same line as (its label), or -1
    bool nl;
};

// What is said about an item.
struct Desc {
    Kind kind;
    State state;
    char label[160], value[160];
};

struct Speech {
    char s[4096];
    int n;
};

const int kItems = 768, kPieces = 2048;

Item sItem[kItems];
int sCount;
bool sReading;
ImGuiID sPendId; // an ItemInfo before its ItemAdd (a tab)
ImGuiItemStatusFlags sPendStatus;
char sPendLabel[96];
bool sTipOpen; // a tooltip's window began, and nothing after it yet: g.TempBuffer may hold its text
char sTipTemp[2048];

char sLog[1 << 16];
int sLogLen;
int sOwnFrom[kItems], sOwnTo[kItems];
char sText[1 << 16];
int sTextLen;
Piece sPiece[kPieces];
int sPieces;

int sDebug;
Uint64 sLastFrameMs;

// The right stick
int sStickDir, sStep;
double sStickT0, sStickPrevT;
ImGuiID sSliderId, sPushed;
struct {
    ImGuiID activateId;
    ImGuiActivateFlags activateFlags;
    ImGuiInputSource source;
    ImGuiKeyData left, right;
} sBak;

// What was said
ImGuiID sFocusId, sFocusTable;
int sFocusRow, sFocusColumn;
Desc sFocus;
ImGuiID sShown[16]; // roots whose title and first lines were said, while they stay on the screen
int sShownCount;
ImGuiID sIntroOnly; // the root whose first lines were said with nothing under the cursor (not again with its first item)
ImGuiID sTrailing[16]; // roots whose last lines were said (once a run)
int sTrailingCount;
bool sSliderHint;
ImGuiID sTipId; // the item whose tooltip was said
// The whole tooltip of the focused item, when the code that draws it gives it (the Accessibility tab: tab_tip): said in
// place of the lines read from the tooltip's window, which can be fewer than it has.
ImGuiID sFullTipId;
char sFullTip[1024];
bool sComboWait; // back on a combo box from its list: a frame waited for it to show the choice
char sNotice[512];
struct Bar {
    ImGuiID id, selected;
} sBar[4];
Uint64 sTabSaidMs;

// Test keys
struct TestKey {
    int frame, frames;
    ImGuiKey key;
};
TestKey sKey[64];
int sKeys, sFrame;

// ---- Text

// The window's texts with a number or a name in them, or a "%" (as "%%"), as the window draws them: the language's
// line for one of them takes what the conversions matched, in the same order (port/lang/a11y_<code>.txt). The English
// is as it is said: one space between words, and the lines of a tooltip joined by "\n".
const char *const kPatterns[] = {
    "When the game runs at another speed than 100%% (an online match, the Cheats tab):\noff, the music plays as it is; "
    "on, it is sped up or slowed down with the game,\nat its own pitch, as an emulator does. Spoken lines always follow "
    "the game.",
    "100%% is what the PlayStation 2 draws; the default here is 60%%.",
    "How fast the game runs outside online play (the sound keeps its pitch). 100%% is the game's own;\n114%% is what "
    "competitive play uses. An online match never uses this value:\nits speed is the host's or the queue's.",
    "How far the stick has to be moved before the game is told anything (the shaded middle).\nOriginal: the game's own, "
    "39%% for left / right and for up / down separately, a square.\n0%% to 90%%: a round one of that size instead; from "
    "its edge the value grows evenly\nto full at full tilt, and the stick counts as a direction half way between the "
    "two.\nThe white dot is the stick, the ring what the game is given; after the stick's name,\nthe direction the game "
    "takes it for. Choose a size that covers where the dot rests\nwhen the stick is let go.",
    "A trigger counts as pressed from %d%% on. The game is told pressed or not, nothing between.",
    "%d%% pressed",
    "stick %s %s",
    "game %s %s",
    "Update available: 0.%d.%d. F1 for the download page.",
    "%dx (%d x %d)",
    "Running now: %s. %s from the next start.",
    "%d pictures a second",
    "Texture pack (%d textures)",
    "Controller %d (not connected)",
    "Keypad %s",
    "Button %d",
    "Update available: 0.%d.%d (this is 0.%d.%d).",
    "You have 0.%d.%d; 0.%d.%d or newer is needed. github.com/z3xox/Tenkaichi3Decomp/releases",
    "Update available: release 0.%d.%d.",
    "Be matched with whoever is searching the same queue. All quick matches have no time limit and run at %d%% speed, "
    "between games with the original disc's data.",
    "%s (%d searching)",
    "Searching for %d:%02d more. Leave the window open.",
    "Up to %d frames",
    "%d frames",
    "DP battle (%d DP)",
    "%d seconds",
    "How fast the match runs, for both players; the sound keeps its pitch.\n100%% is the game's own speed. %d%% is the "
    "competitive standard, and what\nquick matches run at.\nWith a player on 0.1.29 only 100%% and 120%% are possible, with "
    "an older\nrelease only 100%%.",
    "Looking for %s...",
    "Waiting for the other player on port %s...",
    "Active GPU: %s",
    "Updated to %s.",
    "Update %s available (this is %s).",
    "Downloading %s: %d%% (%.1f of %.1f MB). You can go on playing.",
    "Checking the download of %s (its signature)...",
    "Update %s is downloaded and verified.",
    "Installing %s...",
    "The update to %s was not installed: %s",
    "(installed: %s)",
    "Lobby%s%s",
    "Spectating %s vs %s",
    "View: %s",
};
// Texts one of them would take too, that have a line of their own
const char *const kNotPatterns[] = {"Keypad Enter", "Looking for the room..."};

// The language's line for an English text of the window, or that text: first the one of the key "text|settings", for a
// text the game's own screens say otherwise ("Start": a choice there, a button here). (Only with the speaker on, which
// is when the files have been read: frame_end.)
const char *lang_line(const char *en) {
    return PortA11y_TrSense("settings", en);
}

bool letters(const char *s) {
    for (; *s; s++) {
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) {
            return true;
        }
    }
    return false;
}

const char *conversion_end(const char *p) { // p at the '%'
    p += 1 + strspn(p + 1, "-+ #0123456789.hlz");
    return *p ? p + 1 : p;
}

// Text against an English line that may hold printf conversions; cap[] gets what each one matched.
bool match(const char *p, const char *t, const char **cap, int *capLen, int *n) {
    while (*p) {
        if (p[0] == '%' && p[1] == '%') {
            if (*t != '%') {
                return false;
            }
            p += 2;
            t++;
            continue;
        }
        if (p[0] == '%') {
            char conv = conversion_end(p)[-1];
            int saved = *n;
            if (*n >= 8 || conv == '%') {
                return false;
            }
            p = conversion_end(p);
            if (conv == 's') { // as little as lets the rest match
                for (const char *e = t;; e++) {
                    *n = saved + 1;
                    cap[saved] = t;
                    capLen[saved] = (int)(e - t);
                    if (match(p, e, cap, capLen, n)) {
                        return true;
                    }
                    if (*e == '\0') {
                        *n = saved;
                        return false;
                    }
                }
            }
            const char *e = t;
            if (conv == 'c') {
                if (*e == '\0') {
                    return false;
                }
                e++;
            } else {
                const char *digits = strchr("xX", conv) ? "0123456789abcdefABCDEF" : strchr("feEgG", conv) ? "0123456789." : "0123456789";
                size_t k;
                if (*e == '-' || *e == '+') {
                    e++;
                }
                if ((k = strspn(e, digits)) == 0) {
                    return false;
                }
                e += k;
            }
            cap[saved] = t;
            capLen[saved] = (int)(e - t);
            *n = saved + 1;
            t = e;
            continue;
        }
        if (*p != *t) {
            return false;
        }
        p++;
        t++;
    }
    return *t == '\0';
}

void fill(const char *p, const char **cap, const int *capLen, int n, char *out, int size) {
    int o = 0, k = 0;
    while (*p && o < size - 1) {
        if (p[0] == '%' && p[1] == '%') {
            out[o++] = '%';
            p += 2;
        } else if (p[0] == '%') {
            p = conversion_end(p);
            for (int i = 0; k < n && i < capLen[k] && o < size - 1; i++) {
                out[o++] = cap[k][i];
            }
            k++;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
}

// The controller's buttons as the window names them for any controller ("A / Cross": ui.cpp, pad_source_name, in
// the order of SDL_GamepadButton), and its lines that name them: with the names of the controller in use instead
// (a11y_pad.c), Cross on a PlayStation one, A on an Xbox one. 1: it was one of them and a controller is there.
bool pad_words(const char *en, char *out, int size) {
    static const char *const kSources[] = {"A / Cross", "B / Circle", "X / Square", "Y / Triangle", "Back / Select",
                                           "Guide", "Start", "Left stick click", "Right stick click", "Left shoulder",
                                           "Right shoulder", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
                                           "Misc 1", "Right paddle 1", "Left paddle 1", "Right paddle 2", "Left paddle 2",
                                           "Touchpad"};
    const char *a, *b, *c, *d;

    for (int i = 0; i < (int)(sizeof(kSources) / sizeof(kSources[0])); i++) {
        if (strcmp(en, kSources[i]) == 0 && (a = PortA11y_PadSourceName(i)) != NULL) {
            snprintf(out, size, "%s", a);
            return true;
        }
    }
    if ((strcmp(en, "Left trigger") == 0 || strcmp(en, "Right trigger") == 0) &&
        (a = PortA11y_PadSourceName(en[0] == 'L' ? 100 : 101)) != NULL) { // (ui.h: PORT_PAD_LT, PORT_PAD_RT)
        snprintf(out, size, "%s", a);
        return true;
    }
    if (strcmp(en, "D-pad: move Cross / A: choose Circle / B: back L1 / R1: tab") == 0 &&
        (a = PortA11y_PadSourceName(0)) != NULL && (b = PortA11y_PadSourceName(1)) != NULL &&
        (c = PortA11y_PadSourceName(9)) != NULL && (d = PortA11y_PadSourceName(10)) != NULL) {
        snprintf(out, size, lang_line("D-pad: move. %s: choose. %s: back. %s and %s: tab."), a, b, c, d);
        return true;
    }
    return false;
}

// A text the window draws, in the language spoken. 1: it was found.
bool tr(const char *en, char *out, int size) {
    const char *cap[8], *t;
    int capLen[8], n;
    bool own = false;

    if (strcmp(en, "-") == 0) { // the window's mark for a binding with nothing on it, which a screen reader may skip
        en = "Unbound";
    }
    if (pad_words(en, out, size)) {
        return true;
    }
    if (!letters(en)) {
        snprintf(out, size, "%s", en);
        return false;
    }
    for (const char *p : kNotPatterns) {
        own = own || strcmp(p, en) == 0;
    }
    for (int i = 0; !own && i < (int)(sizeof(kPatterns) / sizeof(kPatterns[0])); i++) {
        if ((n = 0, match(kPatterns[i], en, cap, capLen, &n))) {
            t = lang_line(kPatterns[i]);
            fill(t, cap, capLen, n, out, size);
            return t != kPatterns[i];
        }
    }
    t = lang_line(en);
    snprintf(out, size, "%s", t);
    return t != en;
}

// Adds a part: sep (", " within an item, ". " between sentences), or a space after a mark or before a line that
// carries on the one before (a lower case letter).
void add(Speech &sp, const char *text, const char *sep) {
    int len = (int)strlen(text);
    if (len == 0 || sp.n + len + 3 >= (int)sizeof(sp.s)) {
        return;
    }
    if (sp.n > 0) {
        char last = sp.s[sp.n - 1];
        const char *s = strchr(".,;:!?", last) ? " " : (sep[0] == '.' && text[0] >= 'a' && text[0] <= 'z') ? " " : sep;
        sp.n += snprintf(sp.s + sp.n, sizeof(sp.s) - sp.n, "%s", s);
    }
    sp.n += snprintf(sp.s + sp.n, sizeof(sp.s) - sp.n, "%s", text);
}

void add_tr(Speech &sp, const char *en, const char *sep) {
    char t[1024];
    tr(en, t, sizeof(t));
    add(sp, t, sep);
}

// s[0, n) as one line: without the tables' "|", the separators' dashes and the newlines; spaces trimmed.
int clean(const char *s, int n, char *out, int size) {
    int o = 0;
    bool space = false;
    for (int i = 0; i < n && o < size - 1; i++) {
        char c = s[i];
        if (c == '-' && i + 3 < n && s[i + 1] == '-' && s[i + 2] == '-' && s[i + 3] == '-') {
            while (i + 1 < n && s[i + 1] == '-') {
                i++;
            }
            space = o > 0;
            continue;
        }
        if (c == '|' || c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            space = o > 0;
            continue;
        }
        if (space && o < size - 2) {
            out[o++] = ' ';
        }
        space = false;
        out[o++] = c;
    }
    out[o] = '\0';
    return o;
}

// A window's name as drawn (up to "##").
void visible_name(const ImGuiWindow *w, char *out, int size) {
    const char *end = ImGui::FindRenderedTextEnd(w->Name);
    snprintf(out, size, "%.*s", (int)(end - w->Name), w->Name);
}

// ---- The hooks

bool navigable(int k) {
    const Item &it = sItem[k];
    return it.data && !(it.flags & (ImGuiItemFlags_NoNav | ImGuiItemFlags_Disabled)) && !it.menu && !it.isWindow;
}

bool grabbed(const Item &it) { // a slider draws its grab between its ItemAdd and its ItemInfo; a text field does not
    ImDrawList *dl = it.window->DrawList;
    ImU32 a = ImGui::GetColorU32(ImGuiCol_SliderGrab), b = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
    for (int v = it.vtx; v < dl->VtxBuffer.Size; v++) {
        if (dl->VtxBuffer[v].col == a || dl->VtxBuffer[v].col == b) {
            return true;
        }
    }
    return false;
}

void push_slider(ImGuiContext &g, ImGuiID id) {
    if (sPushed != 0 || g.ActiveId != 0 || g.NavActivateId != 0) {
        return;
    }
    ImGuiKeyData *l = ImGui::GetKeyData(&g, ImGuiKey_GamepadDpadLeft), *r = ImGui::GetKeyData(&g, ImGuiKey_GamepadDpadRight);
    ImGuiKeyData *on = sStep < 0 ? l : r, *off = sStep < 0 ? r : l;
    sBak.activateId = g.NavActivateId;
    sBak.activateFlags = g.NavActivateFlags;
    sBak.source = g.NavInputSource;
    sBak.left = *l;
    sBak.right = *r;
    g.NavActivateId = id;
    g.NavActivateFlags = ImGuiActivateFlags_None;
    g.NavInputSource = ImGuiInputSource_Gamepad;
    on->Down = true;
    on->DownDuration = 0.0f;
    on->DownDurationPrev = -1.0f;
    on->AnalogValue = 1.0f;
    off->Down = false;
    off->DownDuration = off->DownDurationPrev = -1.0f;
    off->AnalogValue = 0.0f;
    sPushed = id;
}

void pop_slider(ImGuiContext &g) {
    g.NavActivateId = sBak.activateId;
    g.NavActivateFlags = sBak.activateFlags;
    g.NavInputSource = sBak.source;
    *ImGui::GetKeyData(&g, ImGuiKey_GamepadDpadLeft) = sBak.left;
    *ImGui::GetKeyData(&g, ImGuiKey_GamepadDpadRight) = sBak.right;
    if (g.ActiveId == sPushed) {
        ImGui::ClearActiveID();
    }
    sPushed = 0;
}

void tip_text(ImGuiContext &g) { // the next thing after a tooltip's window: g.TempBuffer has the tooltip's text, or a later one
    sTipOpen = false;
    sTipTemp[0] = '\0';
    if (g.TempBuffer.Size > 0) {
        snprintf(sTipTemp, sizeof(sTipTemp), "%.*s", g.TempBuffer.Size - 1, g.TempBuffer.Data);
    }
}

void copy_label(char *out, int size, const char *label) {
    if (label == NULL) {
        out[0] = '\0';
        return;
    }
    const char *end = ImGui::FindRenderedTextEnd(label);
    snprintf(out, size, "%.*s", (int)(end - label), label);
}

} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &bb, const ImGuiLastItemData *data) {
    ImGuiContext &g = *ctx;
    if (!sReading || id == 0 || sCount == kItems) {
        return;
    }
    if (sTipOpen) {
        tip_text(g);
    }
    ImGuiWindow *w = g.CurrentWindow;
    Item &it = sItem[sCount++];
    memset(&it, 0, sizeof(it));
    it.id = id;
    it.nav = bb;
    it.scope = g.CurrentFocusScopeId;
    it.window = w;
    it.root = w->RootWindow;
    it.from = g.LogBuffer.size();
    it.to = -1;
    it.data = data != NULL;
    it.flags = data ? data->ItemFlags : 0;
    it.isWindow = data == NULL && id == w->ID;
    it.menu = w->DC.NavLayerCurrent == ImGuiNavLayer_Menu;
    it.vtx = w->DrawList->VtxBuffer.Size;
    it.fontSize = g.FontSize;
    if (it.isWindow && (w->Flags & ImGuiWindowFlags_Tooltip)) {
        g.LogLinePosY = -FLT_MAX; // (its text starts a line of its own in the log)
    }
    ImGuiTable *t = g.CurrentTable;
    if (t != NULL && t->InnerWindow == w && t->CurrentColumn >= 0) {
        const char *name = ImGui::TableGetColumnName(t, t->CurrentColumn);
        it.table = t->ID;
        it.row = t->CurrentRow;
        it.column = t->CurrentColumn;
        it.header = (t->RowFlags & ImGuiTableRowFlags_Headers) != 0;
        snprintf(it.columnName, sizeof(it.columnName), "%s", name ? name : "");
    }
    if (g.CurrentTabBar != NULL && ImGui::TabBarFindTabByID(g.CurrentTabBar, id) != NULL) {
        it.tabBar = g.CurrentTabBar;
    }
    if (sPendId == id) {
        it.info = true;
        it.status = sPendStatus;
        memcpy(it.label, sPendLabel, sizeof(it.label));
        sPendId = 0;
    }
    if (id == sSliderId && sStep != 0) {
        push_slider(g, id);
    }
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext *ctx, ImGuiID id, const char *label, ImGuiItemStatusFlags flags) {
    ImGuiContext &g = *ctx;
    if (!sReading || id == 0) {
        return;
    }
    for (int k = sCount - 1; k >= 0 && k >= sCount - 16; k--) {
        Item &it = sItem[k];
        if (it.id != id) {
            continue;
        }
        it.info = true;
        it.status = flags;
        it.to = g.LogBuffer.size();
        copy_label(it.label, sizeof(it.label), label);
        if (flags & ImGuiItemStatusFlags_Inputable) {
            it.slider = grabbed(it);
        }
        if (it.isWindow && (it.window->Flags & ImGuiWindowFlags_Tooltip)) {
            sTipOpen = true;
        }
        if (sPushed == id) {
            pop_slider(g);
        }
        return;
    }
    sPendId = id;
    sPendStatus = flags;
    copy_label(sPendLabel, sizeof(sPendLabel), label);
}

void ImGuiTestEngineHook_Log(ImGuiContext *ctx, const char *fmt, ...) {
}

const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *ctx, ImGuiID id) {
    return NULL;
}

namespace {

// ---- Reading the frame

// The text an item draws itself: from its ItemAdd to its ItemInfo; a tab's or a combo box's (no ItemInfo after), the
// first line after its ItemAdd.
void own_ranges(void) {
    for (int k = 0; k < sCount; k++) {
        const Item &it = sItem[k];
        int next = k + 1 < sCount ? sItem[k + 1].from : sLogLen, a = it.from < sLogLen ? it.from : sLogLen;
        next = next < a ? a : next > sLogLen ? sLogLen : next;
        sOwnFrom[k] = sOwnTo[k] = a;
        if (it.info && it.to >= 0 && !it.tabBar) {
            sOwnTo[k] = it.to < next ? it.to : next;
            continue;
        }
        int s = a;
        while (s < next && (sLog[s] == '\n' || sLog[s] == ' ')) {
            s++;
        }
        if (!it.info && !it.tabBar && s + 2 < next && sLog[s] == '/' && sLog[s + 1] == ' ' && sLog[s + 2] == '{') {
            s += 2; // a tab's "/" decoration left over in front of a combo box's "{": the combo still starts at its brace
        }
        if (it.tabBar || (!it.info && s < next && sLog[s] == '{')) {
            int e = s;
            while (e < next && sLog[e] != '\n') {
                e++;
            }
            sOwnFrom[k] = s;
            sOwnTo[k] = e;
        }
    }
}

void own_text(int k, char *out, int size) {
    clean(sLog + sOwnFrom[k], sOwnTo[k] - sOwnFrom[k], out, size);
}

int add_text(const char *s, int n) {
    if (sTextLen + n + 2 > (int)sizeof(sText)) {
        return -1;
    }
    int len = clean(s, n, sText + sTextLen, (int)sizeof(sText) - sTextLen);
    if (len == 0) {
        return -1;
    }
    int at = sTextLen;
    sTextLen += len + 1;
    return at;
}

void add_piece(ImGuiWindow *root, int item, int text, int label, bool nl) {
    if (sPieces < kPieces) {
        Piece &p = sPiece[sPieces++];
        p.root = root;
        p.item = item;
        p.text = text;
        p.label = label;
        p.nl = nl;
    }
}

struct Seg {
    int a, b;
    bool nl, open; // open: the line goes on after the gap (the next item is on it)
};

int segments(int a, int b, Seg *seg, int max) {
    int n = 0, s = a;
    bool nl = a > 0 && sLog[a - 1] == '\n';
    for (int i = a; i <= b; i++) {
        if (i == b || sLog[i] == '\n') {
            if (n < max) {
                seg[n].a = s;
                seg[n].b = i;
                seg[n].nl = nl;
                seg[n].open = i == b;
                n++;
            }
            nl = true;
            s = i + 1;
        }
    }
    return n;
}

int sOpenText; // the last line added goes on after it: its piece, or -1

void add_lines(ImGuiWindow *root, const Seg *seg, int n) {
    for (int i = 0; i < n; i++) {
        int t = add_text(sLog + seg[i].a, seg[i].b - seg[i].a);
        if (t >= 0) {
            add_piece(root, -1, t, -1, seg[i].nl);
            sOpenText = seg[i].open ? sPieces - 1 : -1;
        } else if (!seg[i].open || seg[i].b > seg[i].a) {
            sOpenText = seg[i].open ? sOpenText : -1;
        }
    }
}

// The pieces of the frame, each with the root window it belongs to. The text between two items belongs to the
// window of the next one, except at a window's start (its own) and before a window starts (the one before's). A
// window's title is not taken from there (it is the gap up to its title bar's items, in the menu layer).
void read_pieces(void) {
    static Seg seg[256];

    sPieces = 0;
    sTextLen = 0;
    sOpenText = -1;
    own_ranges();
    for (int k = 0; k < sCount; k++) {
        const Item &it = sItem[k];
        const Item *next = k + 1 < sCount ? &sItem[k + 1] : NULL;
        if (it.data && !it.menu && !it.isWindow) {
            bool same = sOpenText >= 0 && sPiece[sOpenText].root == it.root && sOwnTo[k] > sOwnFrom[k] &&
                        sLog[sOwnFrom[k]] != '\n';
            add_piece(it.root, k, -1, same ? sOpenText : -1, false);
        }
        sOpenText = -1;
        int a = sOwnTo[k], b = next ? (next->from < sLogLen ? next->from : sLogLen) : sLogLen;
        if (b <= a || (next && next->menu && next->window == it.window && (it.isWindow || it.menu))) {
            continue;
        }
        int n = segments(a, b, seg, 256);
        ImGuiWindow *owner = (it.isWindow || (next && next->isWindow) || !next) ? it.root : next->root;
        if (it.isWindow && (it.window->Flags & ImGuiWindowFlags_Tooltip)) {
            // Its lines are as many as its text's height holds; what comes after belongs to the window it is over.
            ImGuiWindow *w = it.window;
            int lines = (int)((w->DC.CursorMaxPos.y - w->DC.CursorStartPos.y) / (it.fontSize > 0.0f ? it.fontSize : 13.0f) + 0.5f);
            int first = 0, m = 0;
            while (first < n && seg[first].b == seg[first].a) {
                first++;
            }
            for (m = first; m < n && m - first < (lines > 0 ? lines : 1); m++) {
            }
            ImGuiWindow *rest = w->ParentWindowInBeginStack ? w->ParentWindowInBeginStack->RootWindow : next ? next->root : it.root;
            if (m > first && m <= n && sTipTemp[0] != '\0') {
                // A line drawn under the tooltip right after it may have joined its last line: cut it there.
                char last[512], tail[256];
                int len, tl;
                clean(sLog + seg[m - 1].a, seg[m - 1].b - seg[m - 1].a, last, sizeof(last));
                tl = clean(sTipTemp, (int)(strchr(sTipTemp, '\n') ? strchr(sTipTemp, '\n') - sTipTemp : strlen(sTipTemp)), tail, sizeof(tail));
                len = (int)strlen(last);
                if (tl > 0 && len > tl + 1 && strcmp(last + len - tl, tail) == 0 && last[len - tl - 1] == ' ') {
                    Seg cut = seg[m - 1];
                    add_lines(it.root, seg, m - 1);
                    int t = add_text(last, len - tl - 1);
                    if (t >= 0) {
                        add_piece(it.root, -1, t, -1, cut.nl);
                    }
                    t = add_text(tail, tl);
                    if (t >= 0) {
                        add_piece(rest, -1, t, -1, false);
                    }
                    add_lines(rest, seg + m, n - m);
                    continue;
                }
            }
            add_lines(it.root, seg, m);
            add_lines(rest, seg + m, n - m);
            continue;
        }
        add_lines(owner, seg, n);
    }
}

int piece_of(int k) {
    for (int i = 0; i < sPieces; i++) {
        if (sPiece[i].item == k) {
            return i;
        }
    }
    return -1;
}

void braces(const char *own, char *out, int size) {
    const char *a = strchr(own, '{'), *b = strrchr(own, '}');
    out[0] = '\0';
    if (a && b && b > a) {
        clean(a + 1, (int)(b - a - 1), out, size);
    }
}

bool radio(int k) {
    char own[64];
    own_text(k, own, sizeof(own));
    return strncmp(own, "(x)", 3) == 0 || strncmp(own, "( )", 3) == 0;
}

bool brace_item(int k) {
    return !sItem[k].info && sOwnTo[k] > sOwnFrom[k] && sLog[sOwnFrom[k]] == '{';
}

// The combo box a combo's list belongs to, or -1.
int combo_of(ImGuiContext &g, ImGuiWindow *popup) {
    for (ImGuiPopupData &p : g.OpenPopupStack) {
        if (p.Window != popup) {
            continue;
        }
        for (int k = 0; k < sCount; k++) {
            if (brace_item(k) && ImHashStr("##ComboPopup", 0, sItem[k].id) == p.PopupId) {
                return k;
            }
        }
    }
    return -1;
}

bool inside(const ImGuiWindow *w, const ImGuiWindow *c) {
    for (; w != NULL; w = w->ParentWindow) {
        if (w == c) {
            return true;
        }
    }
    return false;
}

// The child window an item is the frame of (a region of BeginChild, entered with Cross), or NULL.
ImGuiWindow *child_of(int k) {
    for (int j = 0; j < sCount; j++) {
        if (sItem[j].isWindow && sItem[j].window != sItem[k].window && sItem[j].window->ChildId == sItem[k].id) {
            return sItem[j].window;
        }
    }
    return NULL;
}

void describe(ImGuiContext &g, int k, Desc &d) {
    const Item &it = sItem[k];
    char own[256];
    own_text(k, own, sizeof(own));
    memset(&d, 0, sizeof(d));
    snprintf(d.label, sizeof(d.label), "%s", it.info ? it.label : "");
    if (it.tabBar) {
        d.kind = K_TAB;
        d.state = it.tabBar->SelectedTabId == it.id ? S_SELECTED : S_NONE;
    } else if (it.header) {
        d.kind = K_HEADER;
    } else if (it.status & ImGuiItemStatusFlags_Checkable) {
        d.kind = K_CHECK;
        d.state = strncmp(own, "[~]", 3) == 0 ? S_PARTIAL : (it.status & ImGuiItemStatusFlags_Checked) ? S_CHECKED : S_UNCHECKED;
    } else if (strncmp(own, "(x)", 3) == 0 || strncmp(own, "( )", 3) == 0) {
        d.kind = K_RADIO;
        d.state = own[1] == 'x' ? S_CHECKED : S_UNCHECKED;
    } else if (it.info && (it.status & ImGuiItemStatusFlags_Inputable)) {
        d.kind = it.slider ? K_SLIDER : K_EDIT;
        braces(own, d.value, sizeof(d.value));
    } else if (brace_item(k)) {
        const char *b = strrchr(own, '}');
        d.kind = K_COMBO;
        braces(own, d.value, sizeof(d.value));
        clean(b + 1, (int)strlen(b + 1), d.label, sizeof(d.label));
    } else if (strncmp(it.root->Name, "##Combo_", 8) == 0) { // (BeginComboPopup's window; ##ComboPopup is its id's seed)
        int c = combo_of(g, it.root);
        d.kind = K_OPTION;
        if (c >= 0) {
            char cown[256], value[160];
            own_text(c, cown, sizeof(cown));
            braces(cown, value, sizeof(value));
            d.state = strcmp(value, d.label) == 0 ? S_SELECTED : S_NONE;
        }
    } else if (ImGuiWindow *c = child_of(k)) {
        d.kind = K_GROUP;
        for (int j = 0; j < sCount; j++) {
            d.kind = sItem[j].table && inside(sItem[j].window, c) ? K_TABLE : d.kind;
        }
    } else if (own[0] == '[') {
        d.kind = K_BUTTON;
    } else {
        d.kind = K_OTHER;
        if (it.status & ImGuiItemStatusFlags_Openable) {
            d.state = (it.status & ImGuiItemStatusFlags_Opened) ? S_EXPANDED : S_COLLAPSED;
        }
    }
    if (d.label[0] == '\0') { // no label of its own: what it draws, without the marks
        const char *s = own;
        if (d.kind == K_BUTTON || d.kind == K_CHECK || d.kind == K_RADIO) {
            s += strcspn(s, "])") + (s[strcspn(s, "])")] ? 1 : 0);
        }
        clean(s, (int)strlen(s), d.label, sizeof(d.label));
        if (d.kind == K_BUTTON && d.label[0] && d.label[strlen(d.label) - 1] == ']') {
            d.label[strlen(d.label) - 1] = '\0';
        }
    }
}

// n of m: among the tabs of its bar, the options of its list, or the radio buttons next to it.
void position(ImGuiContext &g, int k, Kind kind, int &n, int &m) {
    const Item &it = sItem[k];
    n = m = 0;
    if (kind == K_TAB) {
        for (int j = 0; j < sCount; j++) {
            if (sItem[j].tabBar == it.tabBar) {
                m++;
                n = j == k ? m : n;
            }
        }
    } else if (kind == K_OPTION) {
        for (int j = 0; j < sCount; j++) {
            if (sItem[j].root == it.root && navigable(j)) {
                m++;
                n = j == k ? m : n;
            }
        }
    } else if (kind == K_RADIO) {
        int a = k, b = k;
        for (int j = k - 1; j >= 0; j--) {
            if (sItem[j].root == it.root && navigable(j)) {
                if (!radio(j)) {
                    break;
                }
                a = j;
            }
        }
        for (int j = k + 1; j < sCount; j++) {
            if (sItem[j].root == it.root && navigable(j)) {
                if (!radio(j)) {
                    break;
                }
                b = j;
            }
        }
        for (int j = a; j <= b; j++) {
            if (sItem[j].root == it.root && navigable(j)) {
                m++;
                n = j == k ? m : n;
            }
        }
        if (m < 2) {
            n = m = 0;
        }
    }
}

const char *kind_word(Kind kind) {
    switch (kind) {
    case K_BUTTON: return lang_line("button");
    case K_CHECK: return lang_line("check box");
    case K_RADIO: return lang_line("radio button");
    case K_SLIDER: return lang_line("slider");
    case K_EDIT: return lang_line("edit");
    case K_COMBO: return lang_line("combo box");
    case K_TAB: return lang_line("tab");
    case K_HEADER: return lang_line("column header");
    case K_TABLE: return lang_line("table");
    case K_GROUP: return lang_line("group");
    default: return "";
    }
}

const char *state_word(State state) {
    switch (state) {
    case S_CHECKED: return lang_line("checked");
    case S_UNCHECKED: return lang_line("not checked");
    case S_PARTIAL: return lang_line("half checked");
    case S_SELECTED: return lang_line("selected");
    case S_COLLAPSED: return lang_line("collapsed");
    case S_EXPANDED: return lang_line("expanded");
    default: return "";
    }
}

void add_value(Speech &sp, const Desc &d) {
    if (d.kind == K_SLIDER || d.kind == K_EDIT || d.kind == K_COMBO) {
        if (d.value[0] == '\0') {
            add(sp, lang_line("blank"), ", ");
        } else {
            add_tr(sp, d.value, ", ");
        }
    }
}

void add_position(ImGuiContext &g, Speech &sp, int k, Kind kind) {
    int n, m;
    char t[64];
    position(g, k, kind, n, m);
    if (m > 0) {
        snprintf(t, sizeof(t), lang_line("%d of %d"), n, m);
        add(sp, t, ", ");
    }
}

// What is said about an item: its label, its kind, its value or state, where it is among its own.
void add_item(ImGuiContext &g, Speech &sp, int k, const Desc &d, bool label) {
    if (label) {
        add_tr(sp, d.label, ". ");
    }
    add(sp, kind_word(d.kind), ", ");
    add_value(sp, d);
    add(sp, state_word(d.state), ", ");
    add_position(g, sp, k, d.kind);
}

// A line of text, or an item that is there but cannot be reached (disabled).
void add_piece_speech(ImGuiContext &g, Speech &sp, int i) {
    const Piece &p = sPiece[i];
    if (p.item < 0) {
        add_tr(sp, sText + p.text, ". ");
    } else if (sItem[p.item].flags & ImGuiItemFlags_Disabled) {
        Desc d;
        describe(g, p.item, d);
        add_item(g, sp, p.item, d, true);
        add(sp, lang_line("unavailable"), ", ");
    }
}

// A line's text after an item on it ("(F11)" after a check box): that item's, not the next one's.
bool trails(int i) {
    if (sPiece[i].item >= 0 || sPiece[i].nl) {
        return false;
    }
    for (int j = 0; j < sPieces; j++) {
        if (sPiece[j].label == i) {
            return false;
        }
    }
    for (int j = i - 1; j >= 0; j--) {
        if (sPiece[j].root == sPiece[i].root) {
            return sPiece[j].item >= 0 ? navigable(sPiece[j].item) : trails(j);
        }
    }
    return false;
}

bool in_list(const ImGuiID *list, int count, ImGuiID id) {
    for (int i = 0; i < count; i++) {
        if (list[i] == id) {
            return true;
        }
    }
    return false;
}

void reset(void) {
    sFocusId = sFocusTable = 0;
    sFocusRow = sFocusColumn = -1;
    memset(&sFocus, 0, sizeof(sFocus));
    sShownCount = 0;
    sIntroOnly = 0;
    sTipId = 0;
    sComboWait = false;
    sNotice[0] = '\0';
    memset(sBar, 0, sizeof(sBar));
    sStickDir = 0;
}

void debug_dump(ImGuiContext &g) {
    fprintf(stderr, "a11y_ui frame %d nav %08x log %d\n", ImGui::GetFrameCount(), g.NavId, sLogLen);
    for (int k = 0; k < sCount; k++) {
        const Item &it = sItem[k];
        char own[96];
        own_text(k, own, sizeof(own));
        fprintf(stderr, "  #%d %08x %s%s%s%s%s%s%s %-12.12s %d-%d \"%s\" own \"%s\"\n", k, it.id, it.isWindow ? "W" : "-",
                it.menu ? "M" : "-", it.info ? "i" : "-", navigable(k) ? "n" : "-", it.slider ? "s" : "-",
                it.tabBar ? "t" : "-", it.table ? "c" : "-", it.root->Name, it.from, it.to, it.label, own);
    }
    for (int i = 0; i < sPieces; i++) {
        const Piece &p = sPiece[i];
        fprintf(stderr, "  [%d] %-12.12s %s%s\n", i, p.root->Name, p.nl ? "/ " : "", p.item >= 0 ? sItem[p.item].label : sText + p.text);
    }
}

// ---- Speaking

// The first and last pieces of a window that can be reached (-1, -1 with none).
void reach(ImGuiWindow *R, int &first, int &last) {
    first = last = -1;
    for (int i = 0; i < sPieces; i++) {
        if (sPiece[i].root == R && sPiece[i].item >= 0 && navigable(sPiece[i].item)) {
            first = first < 0 ? i : first;
            last = i;
        }
    }
}

// A window as it appears: its title, its open tab (tab: the focus is not on a tab) and the lines before the first
// item that can be reached.
void add_intro(ImGuiContext &g, Speech &sp, ImGuiWindow *R, bool tab, int first) {
    char title[128];
    if (sShownCount < 16) {
        sShown[sShownCount++] = R->ID;
    }
    visible_name(R, title, sizeof(title));
    add_tr(sp, title, ". ");
    for (int j = 0; j < sCount && tab; j++) {
        if (sItem[j].root == R && sItem[j].tabBar && sItem[j].tabBar->SelectedTabId == sItem[j].id) {
            add_tr(sp, sItem[j].label, ". ");
            add(sp, kind_word(K_TAB), ", ");
            break;
        }
    }
    for (int i = 0; i < first; i++) {
        if (sPiece[i].root == R) {
            add_piece_speech(g, sp, i);
        }
    }
}

// The lines after the last item that can be reached (the window's own help), once a run.
void add_trailing(ImGuiContext &g, Speech &sp, ImGuiWindow *R, int last) {
    if (in_list(sTrailing, sTrailingCount, R->ID)) {
        return;
    }
    if (sTrailingCount < 16) {
        sTrailing[sTrailingCount++] = R->ID;
    }
    for (int i = last + 1; i < sPieces && last >= 0; i++) {
        if (sPiece[i].root == R) {
            add_piece_speech(g, sp, i);
        }
    }
}

// Puts the navigation cursor on item k, as a press of the D-pad that lands there would (in the same root window, and
// at the frame's end: the next frame's moves go from it).
bool focus_item(ImGuiContext &g, int k) {
    ImGuiWindow *w = sItem[k].window;
    if (g.NavWindow == NULL || g.NavWindow->RootWindow != w->RootWindow || w->Hidden) {
        return false;
    }
    if (g.NavWindow != w) {
        ImGui::SetNavWindow(w);
    }
    ImGui::SetNavID(sItem[k].id, ImGuiNavLayer_Main, sItem[k].scope, ImGui::WindowRectAbsToRel(w, sItem[k].nav));
    return true;
}

// The chosen tab of a bar, or -1.
int chosen_tab(const ImGuiTabBar *bar) {
    for (int j = 0; j < sCount; j++) {
        if (sItem[j].tabBar == bar && sItem[j].id == bar->SelectedTabId) {
            return j;
        }
    }
    return -1;
}

void speak(ImGuiContext &g) {
    Uint64 now = SDL_GetTicks();
    bool said = false;
    read_pieces();

    // The roots on the screen: a title and first lines are said again only after their window was gone.
    {
        ImGuiID present[16];
        int count = 0, keep = 0;
        for (int k = 0; k < sCount && count < 16; k++) {
            if (sItem[k].isWindow && sItem[k].window == sItem[k].root) {
                present[count++] = sItem[k].id;
            }
        }
        for (int i = 0; i < sShownCount; i++) {
            if (in_list(present, count, sShown[i])) {
                sShown[keep++] = sShown[i];
            }
        }
        sShownCount = keep;
    }

    int f = -1;
    if (g.NavId != 0 && g.NavWindow != NULL) {
        for (int k = 0; k < sCount && f < 0; k++) {
            if (sItem[k].id == g.NavId && sItem[k].root == g.NavWindow->RootWindow && navigable(k)) {
                f = k;
            }
        }
    }

    // A tab chosen while the focus is elsewhere (L1 / R1)
    for (int k = 0; k < sCount; k++) {
        ImGuiTabBar *bar = sItem[k].tabBar;
        if (bar == NULL || (k > 0 && sItem[k - 1].tabBar == bar)) {
            continue;
        }
        Bar *slot = NULL;
        for (Bar &b : sBar) {
            slot = b.id == bar->ID ? &b : slot;
        }
        if (slot == NULL) {
            for (Bar &b : sBar) {
                slot = slot == NULL && b.id == 0 ? &b : slot;
            }
            slot = slot ? slot : &sBar[0];
            slot->id = bar->ID;
            slot->selected = bar->SelectedTabId;
            continue;
        }
        if (slot->selected != bar->SelectedTabId) {
            slot->selected = bar->SelectedTabId;
            int j = chosen_tab(bar);
            if (j >= 0 && !(f >= 0 && sItem[f].id == bar->SelectedTabId)) {
                Speech sp = {};
                Desc d;
                describe(g, j, d);
                add_item(g, sp, j, d, true);
                PortA11y_Say(sp.s, A11Y_INTERRUPT);
                sTabSaidMs = now;
                said = true;
                // and the focus goes to it: left on an item of the tab before (gone now), the next press of the D-pad
                // would go from where that item was, past the first ones of this tab
                if (focus_item(g, j)) {
                    sFocusId = sItem[j].id;
                    sFocusTable = 0;
                    sFocus = d;
                    f = -1; // (said: the old tab, if it had the focus, is not said below)
                }
            }
        }
    }

    // The focus that comes to a bar of tabs from elsewhere (the window opens, or up from the first item of a tab) goes
    // to the chosen tab, the one whose items are shown, not to the nearest one; left and right along the bar stay as
    // they are
    if (f >= 0 && sItem[f].tabBar != NULL && sItem[f].id != sItem[f].tabBar->SelectedTabId) {
        ImGuiWindow *R = sItem[f].root;
        bool along = sIntroOnly != R->ID && in_list(sShown, sShownCount, R->ID);
        bool fromBar = false;
        for (int k = 0; k < sCount && along; k++) {
            fromBar = fromBar || (sItem[k].id == sFocusId && sItem[k].tabBar == sItem[f].tabBar);
        }
        int j = chosen_tab(sItem[f].tabBar);
        if (!(along && fromBar) && j >= 0 && focus_item(g, j)) {
            f = j;
        }
    }

    // Back on a combo box from its list: in this frame it still shows the value from before the choice
    {
        Desc w;
        if (f >= 0) {
            describe(g, f, w);
        }
        sComboWait = f >= 0 && w.kind == K_COMBO && sFocus.kind == K_OPTION && !sComboWait;
    }

    if (f >= 0 && !sComboWait) {
        const Item &it = sItem[f];
        ImGuiWindow *R = it.root;
        Desc d;
        int fp = piece_of(f);
        bool intro = !in_list(sShown, sShownCount, R->ID), introduced = intro || sIntroOnly == R->ID;
        describe(g, f, d);
        int label = fp >= 0 ? sPiece[fp].label : -1;
        if (d.label[0] == '\0' && label >= 0) {
            snprintf(d.label, sizeof(d.label), "%s", sText + sPiece[label].text);
        } else {
            label = -1;
        }
        Speech sp = {};
        int flags = now - sTabSaidMs < 600 ? 0 : A11Y_INTERRUPT;
        if (intro || it.id != sFocusId) {
            int first, last;
            reach(R, first, last);
            if (intro) {
                add_intro(g, sp, R, d.kind != K_TAB, first);
            }
            // A table: the row's own lines when the row changes, the column's name when the column does
            if (it.table && !it.header) {
                bool rowChanged = it.table != sFocusTable || it.row != sFocusRow;
                if (rowChanged) {
                    int from = fp;
                    for (int i = fp - 1; i >= 0; i--) {
                        const Piece &p = sPiece[i];
                        if (p.root != R) {
                            continue;
                        }
                        if (p.item >= 0 && (sItem[p.item].table != it.table || sItem[p.item].row != it.row)) {
                            break;
                        }
                        from = i;
                        if (p.nl) {
                            break; // (the row starts this line: what is before it is the row above's)
                        }
                    }
                    for (int i = from; i < fp; i++) {
                        if (sPiece[i].root == R && sPiece[i].item < 0) {
                            add_piece_speech(g, sp, i);
                        }
                    }
                }
                if (rowChanged || it.column != sFocusColumn) {
                    add_tr(sp, it.columnName, ". ");
                }
            }
            add_item(g, sp, f, d, true);
            // A table: the text after it in its row, up to the next item, in the column after its own (the
            // controller column of a stick's row, that has no button)
            if (it.table && !it.header) {
                ImGuiTable *t = ImGui::TableFindByID(it.table);
                bool named = false;
                for (int i = fp + 1; i < sPieces; i++) {
                    const Piece &p = sPiece[i];
                    if (p.root != R) {
                        continue;
                    }
                    if (p.item >= 0 || p.nl) {
                        break;
                    }
                    if (!named && t != NULL && it.column + 1 < t->ColumnsCount) {
                        add_tr(sp, ImGui::TableGetColumnName(t, it.column + 1), ". ");
                    }
                    named = true;
                    add_piece_speech(g, sp, i);
                }
            }
            if (d.kind == K_SLIDER && !sSliderHint) {
                sSliderHint = true;
                add(sp, lang_line("With the right stick: left lowers, right raises"), ". ");
            }
            ImGuiWindow *region = d.kind == K_TABLE || d.kind == K_GROUP ? child_of(f) : NULL;
            if (region) {
                const char *enter = PortA11y_PadSourceName(0), *leave = PortA11y_PadSourceName(1);
                char line[160];
                if (enter != NULL && leave != NULL) {
                    snprintf(line, sizeof(line), lang_line("%s to enter, %s to leave"), enter, leave);
                    add(sp, line, ". ");
                } else {
                    add(sp, lang_line("Cross or A to enter, Circle or B to leave"), ". ");
                }
            }
            // Its description: the lines since the item before it that can be reached (a region: before what is in it)
            if (!it.table && !(introduced && fp == first)) {
                int end = fp;
                for (int i = 0; i < fp && region && end == fp; i++) {
                    end = sPiece[i].item >= 0 && inside(sItem[sPiece[i].item].window, region) ? i : end;
                }
                int from = end;
                for (int i = end - 1; i >= 0; i--) {
                    const Piece &p = sPiece[i];
                    if (p.root != R) {
                        continue;
                    }
                    if (p.item >= 0 && navigable(p.item)) {
                        break;
                    }
                    from = i;
                }
                for (int i = from; i < end; i++) {
                    if (sPiece[i].root == R && i != label && (!introduced || i >= first) && !trails(i)) {
                        add_piece_speech(g, sp, i);
                    }
                }
            }
            // and the text after it on its line (after the last item, the window's own help: said once, below)
            for (int i = fp + 1; i <= last && fp >= 0 && !it.table; i++) {
                if (sPiece[i].root == R) {
                    if (!trails(i)) {
                        break;
                    }
                    add_piece_speech(g, sp, i);
                }
            }
            if (intro) {
                add_trailing(g, sp, R, last);
            }
        } else {
            // The same item: what changed in it
            if (strcmp(d.label, sFocus.label) != 0) {
                add_tr(sp, d.label, ". ");
            }
            if (strcmp(d.value, sFocus.value) != 0) {
                size_t lo = strlen(sFocus.value), ln = strlen(d.value);
                if (d.kind == K_EDIT && ln > lo && strncmp(d.value, sFocus.value, lo) == 0) {
                    if (ln - lo > 1) { // (one letter typed: the screen reader says it as it is typed)
                        add(sp, d.value + lo, ", ");
                    }
                } else if (d.kind == K_EDIT && ln < lo && ln > 0 && strncmp(d.value, sFocus.value, ln) == 0) {
                    add(sp, sFocus.value + ln, ", ");
                } else {
                    add_value(sp, d);
                }
            }
            if (d.state != sFocus.state) {
                add(sp, d.state == S_NONE && sFocus.state == S_SELECTED ? lang_line("not selected") : state_word(d.state), ", ");
            }
            flags = A11Y_INTERRUPT;
        }
        if (sp.n > 0) {
            PortA11y_Say(sp.s, flags);
            said = true;
        }
        sIntroOnly = 0;
        sFocusId = it.id;
        sFocusTable = it.table;
        sFocusRow = it.row;
        sFocusColumn = it.column;
        sFocus = d;
        if (g.NavId != sTipId) {
            sTipId = 0;
        }
    } else if (f < 0 && g.NavWindow != NULL && !in_list(sShown, sShownCount, g.NavWindow->RootWindow->ID)) {
        // A window that opens with nothing under the cursor yet (or with an item of a tab that is gone): what it
        // shows, so the first press of the D-pad does not come first
        ImGuiWindow *R = g.NavWindow->RootWindow;
        int first, last;
        Speech sp = {};
        reach(R, first, last);
        if (first >= 0) {
            add_intro(g, sp, R, true, first);
            add_trailing(g, sp, R, last);
            sIntroOnly = R->ID;
            if (sp.n > 0) {
                PortA11y_Say(sp.s, A11Y_INTERRUPT);
                said = true;
            }
        }
    }

    // A tooltip over the item that has the focus, once; the notice at the bottom when it changes. The level meters
    // (##meter) say nothing.
    Speech tip = {}, notice = {};
    for (int k = 0; k < sCount; k++) {
        const Item &it = sItem[k];
        if (!it.isWindow || it.window != it.root) {
            continue;
        }
        Speech *sp = (it.window->Flags & ImGuiWindowFlags_Tooltip) ? &tip : strcmp(it.window->Name, "##notice") == 0 ? &notice : NULL;
        if (sp == NULL) {
            continue;
        }
        // (a tooltip in the frames it appears in is still being measured: what is drawn of it is clipped to the size
        // it had, and the lines after the first ones were missing; it is read once it is whole)
        if (sp == &tip && (it.window->Appearing || it.window->HiddenFramesCannotSkipItems > 0 || it.window->HiddenFramesCanSkipItems > 0)) {
            continue;
        }
        char all[2048];
        int n = 0;
        all[0] = '\0';
        for (int i = 0; i < sPieces; i++) {
            if (sPiece[i].root == it.window && sPiece[i].item < 0) {
                n += snprintf(all + n, sizeof(all) - n > 0 ? sizeof(all) - n : 0, "%s%s", n ? "\n" : "", sText + sPiece[i].text);
                n = n < (int)sizeof(all) ? n : (int)sizeof(all) - 1;
            }
        }
        // A tooltip's lines that were not drawn (clipped by the window's edge or the screen's) are not in the log, and
        // text drawn right under it can be taken for more of it: its whole text, when the window formatted it
        // (g.TempBuffer: SetItemTooltip with a format), is the one that starts with the same line.
        size_t first = strcspn(sTipTemp, "\n");
        if (sp == &tip && first > 0 && strncmp(all, sTipTemp, first) == 0 && (all[first] == '\0' || all[first] == '\n')) {
            snprintf(all, sizeof(all), "%s", sTipTemp);
        }
        char t[2048];
        if (tr(all, t, sizeof(t)) || strchr(all, '\n') == NULL) {
            add(*sp, t, ". ");
        } else {
            for (int i = 0; i < sPieces; i++) {
                if (sPiece[i].root == it.window && sPiece[i].item < 0) {
                    add_tr(*sp, sText + sPiece[i].text, ". ");
                }
            }
        }
    }
    if (tip.n > 0 && sFullTipId == g.NavId && sFullTip[0] != '\0') {
        tip = {};
        add(tip, sFullTip, ". ");
    }
    if (tip.n > 0 && f >= 0 && !sComboWait && g.NavCursorVisible && sTipId != g.NavId) {
        sTipId = g.NavId;
        PortA11y_Say(tip.s, 0);
        said = true;
    }
    if (strcmp(notice.s, sNotice) != 0) {
        snprintf(sNotice, sizeof(sNotice), "%s", notice.s);
        if (notice.n > 0) {
            PortA11y_Say(notice.s, 0);
            said = true;
        }
    }
    if (sDebug > 1 || (sDebug && said)) {
        debug_dump(g);
    }
}

// The right stick, read at the frame's start: a step now and then while it is held to a side.
void stick(ImGuiContext &g) {
    float l = ImGui::GetKeyData(&g, ImGuiKey_GamepadRStickLeft)->AnalogValue;
    float r = ImGui::GetKeyData(&g, ImGuiKey_GamepadRStickRight)->AnalogValue;
    double t = ImGui::GetTime();
    int dir = sStickDir;
    if (dir == 0 || (dir < 0 ? l : r) < 0.3f) {
        dir = r >= 0.5f ? 1 : l >= 0.5f ? -1 : 0;
    }
    sStep = 0;
    if (dir != sStickDir) {
        sStickDir = dir;
        sStickT0 = t;
        sStep = dir;
    } else if (dir != 0 && ImGui::CalcTypematicRepeatAmount((float)(sStickPrevT - sStickT0), (float)(t - sStickT0), 0.40f, 0.08f) > 0) {
        sStep = dir;
    }
    sStickPrevT = t;
    sSliderId = sStep != 0 && sFocus.kind == K_SLIDER ? g.NavId : 0;
}

void frame_keys(ImGuiContext *ctx, ImGuiContextHook *) {
    if (sKeys == 0) {
        return;
    }
    ImGuiIO &io = ctx->IO;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    for (int i = 0; i < sKeys; i++) {
        if (sKey[i].key == ImGuiKey_None) {
            if (sFrame == sKey[i].frame) {
                Ui_Toggle();
            }
        } else if (sFrame == sKey[i].frame) {
            io.AddKeyAnalogEvent(sKey[i].key, true, 1.0f);
        } else if (sFrame == sKey[i].frame + sKey[i].frames) {
            io.AddKeyAnalogEvent(sKey[i].key, false, 0.0f);
        }
    }
    sFrame++;
}

// Before the frame (NewFramePre): whether the controller's focus is inside a region (the bindings table), in which
// Circle / B leaves the region (ImGui's NavUpdateCancelRequest, the same test) and must not also close the window,
// which ui.cpp does with that button in the same frame (A11yUi_InRegion). Only with the speaker on.
bool sInRegion;

void frame_region(ImGuiContext *ctx, ImGuiContextHook *) {
    ImGuiContext &g = *ctx;
    sInRegion = PortA11y_On() && g.NavWindow != NULL && g.NavWindow != g.NavWindow->RootWindow &&
                !(g.NavWindow->RootWindowForNav->Flags & ImGuiWindowFlags_Popup) && g.NavWindow->RootWindowForNav->ParentWindow != NULL;
}

void frame_begin(ImGuiContext *ctx, ImGuiContextHook *) {
    ImGuiContext &g = *ctx;
    if (!PortA11y_On() || g.LogEnabled || g.CurrentWindow == NULL) {
        return;
    }
    Uint64 now = SDL_GetTicks();
    if (now - sLastFrameMs > 250) {
        reset();
    }
    sLastFrameMs = now;
    sCount = 0;
    sPendId = 0;
    sPushed = 0;
    sTipOpen = false;
    sTipTemp[0] = '\0';
    sFullTipId = 0;
    stick(g);
    ImGui::LogToBuffer(0);
    g.LogWindow = NULL; // (End() of the window that began the log would end it)
    g.TestEngineHookItems = true;
    sReading = true;
}

void frame_end(ImGuiContext *ctx, ImGuiContextHook *) {
    ImGuiContext &g = *ctx;
    if (!sReading) {
        return;
    }
    if (sTipOpen) {
        tip_text(g);
    }
    sReading = false;
    g.TestEngineHookItems = false;
    if (sPushed != 0) {
        pop_slider(g);
    }
    sLogLen = g.LogBuffer.size() < (int)sizeof(sLog) - 1 ? g.LogBuffer.size() : (int)sizeof(sLog) - 1;
    memcpy(sLog, g.LogBuffer.c_str(), sLogLen);
    sLog[sLogLen] = '\0';
    ImGui::LogFinish();
    // (the question before the game closes, when the game's own screen for it fails, says itself: ui.cpp, build_quit)
    if (g.NavWindow != NULL && strcmp(g.NavWindow->RootWindow->Name, "Exit game") == 0) {
        return;
    }
    speak(g);
}

} // namespace

void A11yUi_Init(void) {
    static const struct {
        const char *name;
        ImGuiKey key;
    } kNames[] = {{"up", ImGuiKey_GamepadDpadUp},     {"down", ImGuiKey_GamepadDpadDown}, {"left", ImGuiKey_GamepadDpadLeft},
                  {"right", ImGuiKey_GamepadDpadRight}, {"a", ImGuiKey_GamepadFaceDown},   {"b", ImGuiKey_GamepadFaceRight},
                  {"l1", ImGuiKey_GamepadL1},           {"r1", ImGuiKey_GamepadR1},         {"rs-", ImGuiKey_GamepadRStickLeft},
                  {"rs+", ImGuiKey_GamepadRStickRight}, {"toggle", ImGuiKey_None}};
    ImGuiContextHook hook;
    const char *keys = getenv("BT3_A11Y_UI_KEYS");

    sDebug = getenv("BT3_A11Y_UI_DEBUG") ? atoi(getenv("BT3_A11Y_UI_DEBUG")) : 0;
    while (keys != NULL && *keys && sKeys < 64) {
        char name[8] = "";
        int frame = 0, frames = 1, used = 0;
        if (sscanf(keys, "%d:%7[a-z0-9+-]%n", &frame, name, &used) >= 2) {
            keys += used;
            if (*keys == ':') {
                frames = (int)strtol(keys + 1, (char **)&keys, 10);
            }
            for (const auto &k : kNames) {
                if (strcmp(k.name, name) == 0) {
                    sKey[sKeys].frame = frame;
                    sKey[sKeys].frames = frames > 0 ? frames : 1;
                    sKey[sKeys].key = k.key;
                    sKeys++;
                }
            }
        }
        keys = strchr(keys, ',');
        keys = keys ? keys + 1 : NULL;
    }
    hook = ImGuiContextHook();
    hook.Type = ImGuiContextHookType_NewFramePre;
    hook.Callback = frame_keys;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    hook = ImGuiContextHook();
    hook.Type = ImGuiContextHookType_NewFramePre;
    hook.Callback = frame_region;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    hook = ImGuiContextHook();
    hook.Type = ImGuiContextHookType_NewFramePost;
    hook.Callback = frame_begin;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    hook = ImGuiContextHook();
    hook.Type = ImGuiContextHookType_EndFramePre;
    hook.Callback = frame_end;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
}

// 1 while the controller's focus is inside a region (frame_region): Circle / B leaves it, and only that.
int A11yUi_InRegion(void) {
    return sInRegion ? 1 : 0;
}

// The Accessibility tab is there only with the accessibility on.
int A11yUi_Tabs(void) {
    return PortA11y_On() ? 1 : 0;
}

// The tab's own texts are drawn in the language the screen reader speaks (port/lang/a11y_<code>.txt), so that what is on
// screen and what is said agree; the reader then says them as they are. "<line>##<English>" keeps each control's id.
namespace {
const char *shown(const char *en) {
    return lang_line(en);
}

const char *label_id(const char *en) {
    static char buf[8][512];
    static int k;
    k = (k + 1) % 8;
    snprintf(buf[k], sizeof(buf[k]), "%s##%s", lang_line(en), en);
    return buf[k];
}

void tab_tip(const char *en) {
    if (ImGui::IsItemFocused()) { // (the whole text, for the reader: speak)
        sFullTipId = ImGui::GetItemID();
        snprintf(sFullTip, sizeof(sFullTip), "%s", lang_line(en));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(lang_line(en));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool tab_slider(const char *en, int *v, int lo, int hi, const char *format) {
    bool changed = ImGui::SliderInt(label_id(en), v, lo, hi, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Port_SettingsWrite();
    }
    return changed;
}
} // namespace

extern "C" int PortA11y_GlossaryCount(void);           // a11y_glossary.c
extern "C" const char *PortA11y_GlossaryName(int i);
extern "C" void PortA11y_GlossaryPlay(int i, int numbered);

void A11yUi_Tab(void) {
    static bool glossary;
    int v;

    ImGui::TextWrapped("%s", shown("Sounds and voices of the accessibility. The screen reader keeps its own volume: see the last button."));
    ImGui::Spacing();
    // The blind player profile first (a11y_fight.c): what is recommended for playing without seeing, at once
    if (ImGui::Button(label_id("Blind player profile: turn on what is recommended"))) {
        PortA11y_ApplyProfile();
        PortA11y_Say(shown("Blind player profile on."), A11Y_INTERRUPT);
    }
    tab_tip("Distance, edge and health by a sound; the important moments by the screen reader; where the rival is, a\n"
            "transformation, the Ultimate Blast and the gauges on request by both; the cues at 85 and the music in\n"
            "fights at 70. Each one can be changed below afterwards. With NVDA or JAWS this is done by itself the first time.");
    if (ImGui::Button(label_id("Everything by itself: every alert by sound and screen reader"))) {
        PortA11y_AllByItself();
        PortA11y_Say(shown("Everything by itself on: nothing needs Select."), A11Y_INTERRUPT);
    }
    tab_tip("Every alert below told by a sound and by the screen reader, and the gauges and where the rival is told by\n"
            "themselves as they change: nothing needs Select, and the fight is full of sounds and words. Each one can be\n"
            "changed below afterwards.");
    ImGui::Spacing();
    v = PortA11y_CueVolume();
    if (tab_slider("Cue sounds", &v, 0, 100, "%d%%")) {
        PortA11y_SetCueVolume(v);
    }
    tab_tip("The accessibility's sounds in a fight: the alerts below that are told by a sound. A loudness scale: each\n"
            "step of 10 is 3 dB, so 60 sounds half as loud as 100. 0 turns them off.");
    v = PortA11y_Voices(0);
    if (tab_slider("Game voices in menus", &v, 0, 100, "%d%%")) {
        PortA11y_SetVoices(0, v);
    }
    tab_tip("The game's spoken lines outside a fight, under the screen reader.");
    v = PortA11y_Voices(1);
    if (tab_slider("Game voices in fights", &v, 0, 100, "%d%%")) {
        PortA11y_SetVoices(1, v);
    }
    tab_tip("The fighters' voices during a fight, under the screen reader.");
    v = PortA11y_FightMusic();
    if (tab_slider("Music in fights", &v, 0, 100, "%d%%")) {
        PortA11y_SetFightMusic(v);
    }
    tab_tip("The music and the stage's background sound during a battle, so that the accessibility's sounds are heard\n"
            "over them; 70 is about 3 dB under. The game's own music volume (Audio tab) stays as it is everywhere else.");
    ImGui::Spacing();
    // Where the rival is (a11y_fight.c ALERT_WHERE): told by itself in a fight, or only when asked with Select. First of
    // the alerts: it is what a player who cannot press Select in the middle of a fight needs most.
    static const char *const kWhereWays[] = {"Only with Select", "By itself: sound", "By itself: screen reader",
                                             "By itself: sound and screen reader"};
    v = PortA11y_AlertMode(8);
    if (tab_slider("Where the rival is", &v, 0, 3, shown(kWhereWays[v < 0 ? 0 : v > 3 ? 3 : v]))) {
        PortA11y_SetAlertMode(8, v);
    }
    tab_tip("In a fight, with nothing to press: a ping from where the rival is, every second or two (sooner when\n"
            "nearer), and the screen reader says where whenever that changes: ahead or behind, to the left or the right,\n"
            "above or below, and how far. Nothing while the rival is out of sight. Select once in a fight (F5 on the\n"
            "keyboard) asks for it at any time, whatever is chosen here.");
    // The fight's alerts (a11y_fight.c), each a slider over its ways of telling, so that the right stick changes it
    // as it does the volumes. They are told by themselves; the gauges are told when asked for, in one of three ways
    // (no "Off"), and with the last three also by themselves as they change.
    static const char *const kWays[] = {"Off", "Sound", "Screen reader", "Sound and screen reader"};
    static const char *const kGaugeWays[] = {"", "Sound, with Select", "Screen reader, with Select",
                                             "Sound and screen reader, with Select", "Sound, also by itself",
                                             "Screen reader, also by itself", "Sound and screen reader, also by itself"};
    static const struct { const char *label, *tip; } kAlerts[] = {
        {"Distance to the rival", "A tick that repeats faster as the rival comes nearer, higher when the rival is above\n"
                                  "you and lower when below; two ticks within close range. By the screen reader: near,\n"
                                  "middle distance or far, and above or below, when it changes."},
        {"Edge of the stage", "A gust of wind while you are near the stage's wall (where the game shows it), faster the\n"
                              "nearer; a high hiss under the stage's ceiling, and two knocks out of the World Tournament ring."},
        {"Health", "Two beeps when a player's health falls under three quarters, a half and a quarter, louder the\n"
                   "less is left; under a quarter, yours again every three seconds. In the middle: yours clean, the\n"
                   "rival's with a buzz."},
        {"Transformation and Ultimate Blast", "When one more transformation or the Ultimate Blast comes within reach."},
        {"Gauges: health, ki and blast", "Select in a fight (Create on a DualSense, View on an Xbox controller): once, where the rival is, a\n"
                              "ping from there and a few words; twice quickly, your gauges; three times, the rival's. F5 on the keyboard:\n"
                              "where the rival is; F3 and F4: player 1's and player 2's gauges. The gauges as two beeps (yours clean, the\n"
                              "rival's with a buzz), or health, ki and blast said by the screen reader, or both. \"Also by itself\": told\n"
                              "too each time a whole bar of health or ki, or a blast stock, comes or goes (the rival's: health and blast),\n"
                              "without pressing anything. Outside a fight, Select says the last message again."},
        {"Important moments", "Told by themselves, for both players: health down to two bars and then to one, the ki\n"
                              "full, max power. By the screen reader, by a sound (two beeps for health, three notes\n"
                              "going up for the ki, higher for max power), or both."},
    };
    for (int i = 0; i < (int)(sizeof(kAlerts) / sizeof(kAlerts[0])); i++) {
        int lo = i == 4 ? 1 : 0, hi = i == 4 ? 6 : 3;
        v = PortA11y_AlertMode(i);
        v = v < lo ? lo : v > hi ? hi : v;
        if (tab_slider(kAlerts[i].label, &v, lo, hi, shown(i == 4 ? kGaugeWays[v] : kWays[v]))) {
            PortA11y_SetAlertMode(i, v);
        }
        tab_tip(kAlerts[i].tip);
    }
    // (a11y_fight.c ALERT_NAMES and ALERT_INTRO: off, or by the screen reader)
    static const struct { int alert; const char *label, *tip; } kSaid[] = {
        {6, "Technique names", "In a fight, the screen reader says the text the game shows: the names of the techniques."},
        {7, "Describe the fight", "While a fight's opening plays, the screen reader says who fights whom, and where."},
    };
    for (const auto &c : kSaid) {
        bool on = PortA11y_AlertMode(c.alert) != 0;
        if (ImGui::Checkbox(label_id(c.label), &on)) {
            PortA11y_SetAlertMode(c.alert, on ? 2 : 0);
            Port_SettingsWrite();
        }
        tab_tip(c.tip);
    }
    {
        bool on = PortChat_Read() != 0;
        if (ImGui::Checkbox(label_id("Read the online chat"), &on)) {
            PortChat_ReadSet(on);
            Port_SettingsWrite();
        }
        tab_tip("In an online match, the screen reader says each line the other player writes in the chat, with their name.");
    }
    ImGui::Spacing();
    // The sounds and what they mean: a button opens the list, a button for each plays it and says its meaning.
    char open[256];
    snprintf(open, sizeof(open), "%s##glossary", shown(glossary ? "Close the sound glossary" : "Sound glossary"));
    if (ImGui::Button(open)) { // (one id for both of its names: the focus stays on it)
        glossary = !glossary;
    }
    tab_tip("Each of the accessibility's sounds, played, with what it means.");
    if (glossary) {
        ImGui::Indent();
        for (int i = 0; i < PortA11y_GlossaryCount(); i++) {
            if (ImGui::Button(label_id(PortA11y_GlossaryName(i)))) {
                PortA11y_GlossaryPlay(i, 0);
            }
        }
        ImGui::Unindent();
    }
    ImGui::Spacing();
    // (A button, not a line of text: a line after the last item would be said with the next one, Exit game)
    static const char kReader[] = "The screen reader's volume is set in the screen reader itself. With NVDA: NVDA+Control+left "
                                  "or right arrow until Volume, then NVDA+Control+up or down arrow.";
    if (ImGui::Button(label_id("Screen reader volume"))) {
        PortA11y_Say(shown(kReader), A11Y_INTERRUPT);
    }
    tab_tip(kReader);
}

// ---- Upstream's reader interface (ui.h) ----
//
// Upstream has a reader of its own for these windows (port/src/gs/ui_a11y.cpp, from 0.1.39.3), driven by these calls
// in ui.cpp and by a line in ImDrawList::AddText (imgui_draw.cpp). This branch reads the windows here instead (with the
// translations, the tooltips, the chat and the Accessibility tab), so that file is left out and its calls do nothing:
// both readers at once would define the same Dear ImGui hooks and say every item twice.
void UiA11y_Frame(int windowOpen, int sayClosed) { (void)windowOpen, (void)sayClosed; }
void UiA11y_End(void) {}
void UiA11y_Radio(int selected) { (void)selected; } // (a radio button's state comes from its ItemInfo here)
void UiA11y_Field(const char *name, const char *text) { (void)name, (void)text; } // (the setup's only)
void UiA11y_QueueNext(void) {}
void ImGuiA11yHook_Text(const char *text_begin, const char *text_end) { (void)text_begin, (void)text_end; } // (the log has the texts)
