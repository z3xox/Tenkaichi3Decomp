/*
 * Controller input for the windowed PC build: fills the game's pad buffer (the libpad2 layout the game reads in
 * src/sys/pad.c) from the keyboard and from SDL gamepads, through bindings the settings window (ui.cpp) edits and
 * the settings file keeps.
 *
 * Each player has: one key per action (the 16 buttons and the 8 stick directions), one controller button or
 * trigger per pad button, and a controller slot (the n-th connected controller, or none). Defaults:
 *   player 1   first controller; arrows = d-pad, W A S D = left stick, Enter = START, Right Shift = SELECT,
 *              K = cross, L = circle, J = square, I = triangle, U = L1, O = R1, 7 = L2, 9 = R2
 *   player 2   second controller, no keys
 *   controller the usual layout (south = cross, east = circle, west = square, north = triangle, triggers = L2 / R2)
 * The sticks of a controller are passed through, or given a round deadzone first (stick_bytes, below). The pressure
 * bytes of the buffer are left at zero. The game's motor commands go to the player's controller (Port_PadVibrate).
 */
#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_internal.h"
#include "ui.h"

int gPortOverlayOpen, gPortInputCapture, gPortChatTyping;
extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value), Port_SettingsWrite(void);
extern const char *PortA11y_Tr(const char *label) __attribute__((format_arg(1))); /* a11y.c: the reader's line in its language */

/* bits of the two button bytes together (byte 0 low), as include/sys/pad.h has them; 1 = pressed here */
enum { B_SELECT = 0x0001, B_L3 = 0x0002, B_R3 = 0x0004, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040,
       B_LEFT = 0x0080, B_L2 = 0x0100, B_R2 = 0x0200, B_L1 = 0x0400, B_R1 = 0x0800, B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000,
       B_CROSS = 0x4000, B_SQUARE = 0x8000 };

/* The actions in the order the settings window lists them. 16..23: left stick up, down, left, right, then right. */
static const struct { const char *name, *label; unsigned bit; int key; int pad; } kActions[PORT_ACTIONS] = {
    {"up", "D-pad up", B_UP, SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"down", "D-pad down", B_DOWN, SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"left", "D-pad left", B_LEFT, SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"right", "D-pad right", B_RIGHT, SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"cross", "Cross", B_CROSS, SDL_SCANCODE_K, SDL_GAMEPAD_BUTTON_SOUTH},
    {"circle", "Circle", B_CIRCLE, SDL_SCANCODE_L, SDL_GAMEPAD_BUTTON_EAST},
    {"square", "Square", B_SQUARE, SDL_SCANCODE_J, SDL_GAMEPAD_BUTTON_WEST},
    {"triangle", "Triangle", B_TRIANGLE, SDL_SCANCODE_I, SDL_GAMEPAD_BUTTON_NORTH},
    {"l1", "L1", B_L1, SDL_SCANCODE_U, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"r1", "R1", B_R1, SDL_SCANCODE_O, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"l2", "L2", B_L2, SDL_SCANCODE_7, PORT_PAD_LT},
    {"r2", "R2", B_R2, SDL_SCANCODE_9, PORT_PAD_RT},
    {"l3", "L3", B_L3, 0, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"r3", "R3", B_R3, 0, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"start", "START", B_START, SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START},
    {"select", "SELECT", B_SELECT, SDL_SCANCODE_RSHIFT, SDL_GAMEPAD_BUTTON_BACK},
    {"ls_up", "Left stick up", 0, SDL_SCANCODE_W, -1}, {"ls_down", "Left stick down", 0, SDL_SCANCODE_S, -1},
    {"ls_left", "Left stick left", 0, SDL_SCANCODE_A, -1}, {"ls_right", "Left stick right", 0, SDL_SCANCODE_D, -1},
    {"rs_up", "Right stick up", 0, 0, -1}, {"rs_down", "Right stick down", 0, 0, -1},
    {"rs_left", "Right stick left", 0, 0, -1}, {"rs_right", "Right stick right", 0, 0, -1},
};

static int sKeys[2][PORT_ACTIONS], sPadBtn[2][PORT_PAD_BUTTONS], sPadSlot[2];
/* Per player: the deadzone of the left and of the right stick (-1 = the game's own, as the pad's bytes always were
   passed; 0..PORT_DEADZONE_MAX = a round one of that many percent, stick_bytes below), and vibration (on, strength
   in percent). */
static int sDeadzone[2][2], sVib[2][2];
/* What opens and closes the settings window: a key (F1) and a controller button (none), both of the settings
   window's list; and START + SELECT of any controller together, always. */
static int sOverlayKey = SDL_SCANCODE_F1, sOverlayPad = -1;
static int sChatKey = SDL_SCANCODE_TAB; /* opens the online chat's line (setting key_chat; Tab by default) */
static int sComboLatch; /* START + SELECT has opened or closed the window and is still held */
static int sSwallow; /* a controller opened or closed the window: nothing of the controllers reaches the game until those buttons are let go */
static volatile int sKeySwallow; /* the chat's line was sent or put away: nothing of the keyboard reaches the game until no bound key is held
                                    (Enter, which sends it, is START) */
static pthread_once_t sLoadOnce = PTHREAD_ONCE_INIT;

static void defaults(int player) {
    int a;
    for (a = 0; a < PORT_ACTIONS; a++) {
        sKeys[player][a] = player == 0 ? kActions[a].key : 0;
        if (a < PORT_PAD_BUTTONS) {
            sPadBtn[player][a] = kActions[a].pad;
        }
    }
    sPadSlot[player] = player;
    if (player == 1 && getenv("BT3_PAD_SHARED") != NULL) {
        sPadSlot[player] = 0; /* BT3_PAD_SHARED: both players on controller 1 (testing 1P vs 2P with one pad) */
    }
    sDeadzone[player][0] = sDeadzone[player][1] = -1;
    sVib[player][0] = 1;
    sVib[player][1] = 100;
}

static int range(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void load(void) {
    char name[32];
    int p, a;
    for (p = 0; p < 2; p++) {
        defaults(p);
        for (a = 0; a < PORT_ACTIONS; a++) {
            snprintf(name, sizeof(name), "key_p%d_%s", p + 1, kActions[a].name);
            sKeys[p][a] = Port_Setting(name, sKeys[p][a]);
            if (a < PORT_PAD_BUTTONS) {
                snprintf(name, sizeof(name), "pad_p%d_%s", p + 1, kActions[a].name);
                sPadBtn[p][a] = Port_Setting(name, sPadBtn[p][a]);
            }
        }
        snprintf(name, sizeof(name), "padslot_p%d", p + 1);
        sPadSlot[p] = Port_Setting(name, sPadSlot[p]);
        if (p == 1 && getenv("BT3_PAD_SHARED") != NULL) {
            sPadSlot[p] = 0; /* overrides the saved setting too */
        }
        for (a = 0; a < 2; a++) {
            snprintf(name, sizeof(name), "deadzone_p%d_%s", p + 1, a == 0 ? "ls" : "rs");
            sDeadzone[p][a] = range(Port_Setting(name, -1), -1, PORT_DEADZONE_MAX);
        }
        snprintf(name, sizeof(name), "vibration_p%d", p + 1);
        sVib[p][0] = Port_Setting(name, 1) != 0;
        snprintf(name, sizeof(name), "vibration_p%d_strength", p + 1);
        sVib[p][1] = range(Port_Setting(name, 100), 0, 100);
    }
    sOverlayKey = Port_Setting("key_overlay", SDL_SCANCODE_F1);
    sOverlayPad = Port_Setting("pad_overlay", -1);
    sChatKey = Port_Setting("key_chat", SDL_SCANCODE_TAB);
}

const char *PortInput_ActionLabel(int action) { return kActions[action].label; }
int *PortInput_Keys(int player) { pthread_once(&sLoadOnce, load); return sKeys[player]; }
int *PortInput_PadButtons(int player) { pthread_once(&sLoadOnce, load); return sPadBtn[player]; }
int *PortInput_PadSlot(int player) { pthread_once(&sLoadOnce, load); return &sPadSlot[player]; }
int *PortInput_Deadzone(int player) { pthread_once(&sLoadOnce, load); return sDeadzone[player]; }
int *PortInput_Vibration(int player) { pthread_once(&sLoadOnce, load); return sVib[player]; }
int *PortInput_OverlayKey(void) { pthread_once(&sLoadOnce, load); return &sOverlayKey; }
int *PortInput_OverlayPad(void) { pthread_once(&sLoadOnce, load); return &sOverlayPad; }

/* 1 = this event opens or closes the settings window. */
int PortInput_Hotkey(const SDL_Event *ev) {
    pthread_once(&sLoadOnce, load);
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_UP &&
        (ev->gbutton.button == SDL_GAMEPAD_BUTTON_START || ev->gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
        sComboLatch = 0;
        return 0;
    }
    if (gPortInputCapture || (PortInput_Learning() && ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)) {
        return 0; /* (a binding is being taken, or the online screen is up; a controller's presses are a setup's) */
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
        return sOverlayKey > 0 && (int)ev->key.scancode == sOverlayKey;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        SDL_Gamepad *g = SDL_GetGamepadFromID(ev->gbutton.which);
        int b = (int)ev->gbutton.button;
        if (sOverlayPad >= 0 && b == sOverlayPad) {
            sSwallow = 1;
            return 1;
        }
        /* START + SELECT counts once per press of the two (sComboLatch, cleared when either is let go). Pressed
           in the same moment, both "down" events come in one round and each already finds the other held:
           without the latch the window opened and closed again at once, and only "hold one, press the other"
           worked. */
        if (g != NULL && (b == SDL_GAMEPAD_BUTTON_START || b == SDL_GAMEPAD_BUTTON_BACK) &&
            SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START) && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) {
            sSwallow = 1;
            if (sComboLatch) {
                return 0;
            }
            sComboLatch = 1;
            return 1;
        }
    }
    return 0;
}
/* The settings window closed, or its exit question was answered, with a controller button: that press is not the game's. */
void PortInput_Swallow(void) { sSwallow = 2; }
void PortInput_SwallowKeys(void) { sKeySwallow = 1; }

/* Tenkaichi Budokai Online's keypads (src/menu/port_tbo.c): while one takes text (gPortTboTyping), what is typed
   goes to it, a character at a time (PortTbo_TextRead: the character, 8 backspace, 13 enter, 27 escape, 0 none),
   and the keys that type (letters, digits, space, punctuation, enter, backspace, escape) are not the pad's: the
   arrow keys and the controller still move on the keypad. The window's events come in on the renderer's thread
   (PortInput_TboEvent, from Ui_Event), the game reads them on its own. */
int gPortTboTyping;
static char sTboKeys[64];
static unsigned sTboHead, sTboTail;
static SDL_SpinLock sTboLock;

static int typing_key(int sc) {
    return (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_SLASH && sc != SDL_SCANCODE_TAB) ||
           (sc >= SDL_SCANCODE_KP_DIVIDE && sc <= SDL_SCANCODE_KP_PERIOD);
}

static void tbo_push(int c) {
    SDL_LockSpinlock(&sTboLock);
    if (sTboHead - sTboTail < sizeof(sTboKeys)) {
        sTboKeys[sTboHead++ % sizeof(sTboKeys)] = (char)c;
    }
    SDL_UnlockSpinlock(&sTboLock);
}

void PortTbo_TextMode(int on) {
    SDL_LockSpinlock(&sTboLock);
    sTboHead = sTboTail = 0;
    SDL_UnlockSpinlock(&sTboLock);
    if (gPortTboTyping && !on) {
        sKeySwallow = 1; /* (Enter, which ended it, is START: not the game's until it is let go) */
    }
    gPortTboTyping = on != 0;
}

int PortTbo_TextRead(void) {
    int c = 0;
    SDL_LockSpinlock(&sTboLock);
    if (sTboTail != sTboHead) {
        c = (unsigned char)sTboKeys[sTboTail++ % sizeof(sTboKeys)];
    }
    SDL_UnlockSpinlock(&sTboLock);
    return c;
}

/* "Copy Code" (the room's code, to give the rival): the game's thread hands the text over, the window's thread
   puts it on the clipboard (PortInput_TboClipboard, from the overlay's frame: SDL wants the clipboard there). */
static char sTboCopy[64];
static int sTboCopyDue;
static int sTboPasteDue; /* the keypad's PASTE: 1 the clipboard's code (Join), 2 its plain characters (Join
                            by Address), typed on the window's thread */

void PortTbo_Copy(const char *text) {
    SDL_LockSpinlock(&sTboLock);
    SDL_strlcpy(sTboCopy, text != NULL ? text : "", sizeof(sTboCopy));
    sTboCopyDue = sTboCopy[0] != '\0';
    SDL_UnlockSpinlock(&sTboLock);
}

void PortTbo_Paste(void) {
    SDL_LockSpinlock(&sTboLock);
    sTboPasteDue = 1;
    SDL_UnlockSpinlock(&sTboLock);
}

void PortTbo_PasteText(void) {
    SDL_LockSpinlock(&sTboLock);
    sTboPasteDue = 2;
    SDL_UnlockSpinlock(&sTboLock);
}

/* An address in a copied text: its last word ("192.168.1.20:7000" of "My address: 192.168.1.20:7000"), typed as if
   by hand; the page keeps the characters an address can have. */
static void tbo_paste_text(const char *clip) {
    const unsigned char *p = (const unsigned char *)clip, *from = p;
    int n = 0;

    for (; p != NULL && *p; p++) {
        if (*p == ' ' && p[1] != '\0' && p[1] != ' ') {
            from = p + 1; /* (the last word is the address) */
        }
    }
    for (p = from; p != NULL && *p && *p != ' ' && *p != '\n' && *p != '\r' && n < 48; p++) {
        if (*p > 32 && *p < 127) {
            tbo_push(*p);
            n++;
        }
    }
}

/* The room code in a copied text: the last run of exactly six letters and digits ("Room code: MFT8FD"); without
   one, every letter and digit of it. Typed as if by hand. */
static void tbo_paste_code(const char *clip) {
    const unsigned char *p = (const unsigned char *)clip, *code = NULL;
    int run = 0, n = 0;

    for (; p != NULL && *p; p++) {
        int alnum = (*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z');
        run = alnum ? run + 1 : 0;
        if (run == 6 && !((p[1] >= '0' && p[1] <= '9') || (p[1] >= 'A' && p[1] <= 'Z') || (p[1] >= 'a' && p[1] <= 'z'))) {
            code = p - 5;
        }
    }
    if (code != NULL) {
        for (n = 0; n < 6; n++) {
            tbo_push(code[n]);
        }
        return;
    }
    for (p = (const unsigned char *)clip; p != NULL && *p && n < 48; p++) {
        if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            tbo_push(*p);
            n++;
        }
    }
}

void PortInput_TboClipboard(void) {
    char text[sizeof(sTboCopy)];
    int due, paste;
    SDL_LockSpinlock(&sTboLock);
    due = sTboCopyDue;
    sTboCopyDue = 0;
    paste = sTboPasteDue;
    sTboPasteDue = 0;
    memcpy(text, sTboCopy, sizeof(text));
    SDL_UnlockSpinlock(&sTboLock);
    if (due) {
        SDL_SetClipboardText(text);
    }
    if (paste) {
        char *clip = SDL_GetClipboardText();
        if (paste == 2) {
            tbo_paste_text(clip);
        } else {
            tbo_paste_code(clip);
        }
        SDL_free(clip);
    }
}

/* 1: the event was the keypad's. Only plain characters (32..126) are typed: a name or a room code. */
int PortInput_TboEvent(const SDL_Event *ev) {
    if (!gPortTboTyping) {
        return 0;
    }
    if (ev->type == SDL_EVENT_TEXT_INPUT) {
        const unsigned char *p = (const unsigned char *)ev->text.text;
        for (; p != NULL && *p; p++) {
            if (*p >= 32 && *p < 127) {
                tbo_push(*p);
            }
        }
        return 1;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        SDL_Scancode sc = ev->key.scancode;
        if (sc == SDL_SCANCODE_V && (ev->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && !ev->key.repeat) {
            /* Ctrl+V (Cmd+V): the clipboard's text, as if typed (a room code copied from a message); only its
               plain characters, and no more than the keypad can hold */
            char *clip = SDL_GetClipboardText();
            const unsigned char *p = (const unsigned char *)clip;
            int n = 0;
            for (; p != NULL && *p && n < 48; p++) {
                if (*p >= 32 && *p < 127) {
                    tbo_push(*p);
                    n++;
                }
            }
            SDL_free(clip);
            return 1;
        }
        if (sc == SDL_SCANCODE_BACKSPACE) {
            tbo_push(8);
        } else if ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) && !ev->key.repeat) {
            tbo_push(13);
        } else if (sc == SDL_SCANCODE_ESCAPE && !ev->key.repeat) {
            tbo_push(27);
        }
        return typing_key(sc);
    }
    return ev->type == SDL_EVENT_KEY_UP && typing_key(ev->key.scancode);
}
int *PortInput_ChatKey(void) { pthread_once(&sLoadOnce, load); return &sChatKey; }
void PortInput_ResetDefaults(int player) { pthread_once(&sLoadOnce, load); defaults(player); }

void PortInput_Save(void) {
    char name[32];
    int p, a;
    for (p = 0; p < 2; p++) {
        for (a = 0; a < PORT_ACTIONS; a++) {
            snprintf(name, sizeof(name), "key_p%d_%s", p + 1, kActions[a].name);
            Port_SettingSave(name, sKeys[p][a]);
            if (a < PORT_PAD_BUTTONS) {
                snprintf(name, sizeof(name), "pad_p%d_%s", p + 1, kActions[a].name);
                Port_SettingSave(name, sPadBtn[p][a]);
            }
        }
        snprintf(name, sizeof(name), "padslot_p%d", p + 1);
        Port_SettingSave(name, sPadSlot[p]);
        for (a = 0; a < 2; a++) {
            snprintf(name, sizeof(name), "deadzone_p%d_%s", p + 1, a == 0 ? "ls" : "rs");
            Port_SettingSave(name, sDeadzone[p][a]);
        }
        snprintf(name, sizeof(name), "vibration_p%d", p + 1);
        Port_SettingSave(name, sVib[p][0]);
        snprintf(name, sizeof(name), "vibration_p%d_strength", p + 1);
        Port_SettingSave(name, sVib[p][1]);
    }
    Port_SettingSave("key_overlay", sOverlayKey);
    Port_SettingSave("pad_overlay", sOverlayPad);
    Port_SettingSave("key_chat", sChatKey);
    Port_SettingsWrite();
}

/* Testing without a controller or a person. BT3_STICK_TEST="lx,ly,rx,ry[,lt,rt]" (the sticks -32768..32767, + =
   right / down; the triggers 0..32767) and BT3_RUMBLE_LOG=1 each attach a controller made by SDL itself ("virtual"),
   which becomes controller 1 and the only one (the real ones are left out): its sticks are held at those values,
   and what SDL hands its motors is printed (BT3_RUMBLE_SLOW=ms: it keeps the caller waiting that long). The rest of
   the program does not know it from a real one. */
static bool SDLCALL test_pad_rumble(void *userdata, Uint16 low, Uint16 high) {
    const char *slow = getenv("BT3_RUMBLE_SLOW"); /* ms the controller keeps the caller waiting (a slow one) */
    (void)userdata;
    fprintf(stderr, "rumble: the test controller's motors: low %u, high %u\n", low, high);
    if (slow != NULL) {
        SDL_Delay((Uint32)atoi(slow));
    }
    return true;
}

static SDL_Joystick *sTestPad;

/* (Said again after the program has opened the controller: an axis SDL has not seen move reads as resting.) */
static void test_pad_axes(void) {
    const char *t = getenv("BT3_STICK_TEST");
    int v[6] = {0, 0, 0, 0, 0, 0}, i, pass;

    if (sTestPad == NULL) {
        return;
    }
    if (t != NULL) {
        sscanf(t, "%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    }
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < 6; i++) { /* (a trigger is an axis whose rest is its lowest value) */
            int x = range(i < 4 ? v[i] : v[i] * 2 - 32768, -32768, 32767);
            SDL_SetJoystickVirtualAxis(sTestPad, i, (Sint16)(pass == 0 ? x ^ 1 : x));
        }
        SDL_UpdateJoysticks();
    }
}

static void test_pad(void) {
    SDL_VirtualJoystickDesc desc;
    SDL_JoystickID id;

    if (getenv("BT3_STICK_TEST") == NULL && getenv("BT3_RUMBLE_LOG") == NULL) {
        return;
    }
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Test controller";
    desc.Rumble = test_pad_rumble;
    id = SDL_AttachVirtualJoystick(&desc);
    sTestPad = id != 0 ? SDL_OpenJoystick(id) : NULL;
    if (sTestPad == NULL) {
        fprintf(stderr, "bt3: no test controller: %s\n", SDL_GetError());
    }
}

/* The connected controllers, in SDL's order (the order the settings window lists them in), kept open. */
#define MAX_PADS 8
static struct { SDL_JoystickID id; SDL_Gamepad *pad; char name[64]; } sOpen[MAX_PADS];
static SDL_JoystickID sIds[MAX_PADS];
static int sIdCount, sInit;
static Uint64 sInitAt; /* when the controllers started (SDL_GetTicks) */

/* A line for the log (stderr, and the reader's, a11y_log=1). The controllers start with the game's first pad read,
   the reader with its first blank: what comes before that is kept for the reader's log until it is on. */
static char sPending[16][400];
static int sPendingCount;

static void pad_log(const char *line) {
    extern void PortA11y_Note(const char *text); /* a11y.c */
    extern int PortA11y_On(void);

    fprintf(stderr, "bt3: %s\n", line);
    if (PortA11y_On()) {
        PortA11y_Note(line);
    } else if (sPendingCount < (int)(sizeof(sPending) / sizeof(sPending[0]))) {
        snprintf(sPending[sPendingCount++], sizeof(sPending[0]), "%s", line);
    }
}

/* A controller opened: what SDL knows of it, in the log, for a report of buttons that do not do what they say: its
   name, the type its buttons are named by, maker and model, and the mapping SDL reads its buttons through (a
   controller of an unusual make can be mapped wrongly, every button then another one). */
static void pad_note(SDL_Gamepad *g) {
    char line[1024];
    char *map = SDL_GetGamepadMapping(g);
    const char *name = SDL_GetGamepadName(g), *type = SDL_GetGamepadStringForType(SDL_GetGamepadType(g));

    snprintf(line, sizeof(line), "controller: \"%s\", type %s, %04x:%04x, mapping %s", name != NULL ? name : "?",
             type != NULL ? type : "unknown", SDL_GetGamepadVendor(g), SDL_GetGamepadProduct(g), map != NULL ? map : "none");
    SDL_free(map);
    pad_log(line);
}

/* ---- Controllers SDL does not know, and saying what is plugged in (accessibility) ----
 *
 * SDL makes a gamepad of a joystick only through a mapping, and a USB controller that talks DirectInput (most cheap
 * ones, and many switchable ones in their D mode) has none unless SDL carries one for its maker and model: such a
 * controller was not opened, not said and not even in the log, and a player who cannot see could not tell it from no
 * controller at all. So:
 *   - The community's mappings (gamecontrollerdb.txt, github.com/mdqinc/SDL_GameControllerDB, zlib licence; the release
 *     carries a fixed version of it: port/release/Dockerfile, port/tools/package.py) and the player's own
 *     (gamecontrollerdb_own.txt, which no release replaces; its lines win) are read from the program's folder as the
 *     controllers start; then SDL_GAMECONTROLLERCONFIG_FILE again when it is set, so that it still wins. SDL makes a
 *     gamepad of a controller that is already there as soon as it has a mapping.
 *   - A joystick still without one goes to the log with its name, maker and model (VID:PID), GUID, type and what it
 *     has. When it has the shape of a controller (type unknown or gamepad, 10 buttons or more, 2 axes or more) it is
 *     given the layout most of those have, the PlayStation one as the community's mappings have it for 46 of their 92
 *     Windows controllers with the face buttons in that order (Triangle 0, Circle 1, Cross 2, Square 3; L1 4, R1 5,
 *     L2 6, R2 7, Select 8, Start 9, L3 10, R3 11; the d-pad on the hat, or on the first two axes without one; the
 *     right stick on axes 2 and 3, 2 and 4 on DragonRise's own, maker 0079, with 5 axes): it becomes a gamepad, and
 *     that is said, for it is a guess. Where the guess is wrong, the player sets the controller up a button at a time,
 *     by voice (learn_*, below: F1, Controls), and that is kept.
 *   - What is plugged in is said: once, a few seconds after the start (each controller and its player, or that there
 *     is none and the keyboard plays); after that every controller that comes or goes, and every player whose
 *     controller changed. The controller a player gets vibrates once (player 2's twice), unless that player has
 *     vibration off, and its light, if it has one, shows the player.
 * The game sees none of this: only which controller is read can change, as plugging one in always did.
 *
 * Testing without a controller: BT3_PAD_UNKNOWN="<vid>:<pid>[@<seconds>]" attaches a joystick made by SDL itself, with
 * no mapping (12 buttons, a hat, 4 axes; 5 for maker 0079), named "USB Joystick" or $BT3_PAD_UNKNOWN_NAME, at the
 * start or that many seconds after it; BT3_PAD_UNKNOWN_OFF=<seconds> takes it away again then. BT3_PAD_UNKNOWN_SHAPE=
 * "<buttons>,<axes>,<hats>" gives it other parts. What it is told to vibrate is printed, as with BT3_RUMBLE_LOG.
 */
#define PAD_SAY_AFTER 3000 /* ms after the controllers start: the first round of what is plugged in (some are found late) */
#define MAX_JOYS 16

static SDL_JoystickID sJoySeen[MAX_JOYS]; /* the joysticks without a mapping already looked at (ids are not reused) */
static unsigned sJoySeenCount;
static SDL_JoystickID sGuessed[MAX_JOYS]; /* the ones given the common layout here */
static unsigned sGuessedCount;
static char sUnusable[4][64]; /* joysticks found without a mapping and not of a controller's shape, not said yet */
static int sUnusableCount;
static int sSaid; /* the first round has been said */
static SDL_JoystickID sSaidPad[2]; /* each player's controller as last said (0: none) */
static struct { SDL_JoystickID id; int left; Uint64 next; } sPulse[2]; /* identification pulses still to come */

static void mappings_load(void) {
    static const char *const kFiles[2] = {"gamecontrollerdb.txt", "gamecontrollerdb_own.txt"};
    const char *base = SDL_GetBasePath(), *env = SDL_GetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE), *what;
    char path[1024], line[1200];
    int i, n;

    for (i = 0; i < 3; i++) {
        if (i < 2 && base != NULL) {
            snprintf(path, sizeof(path), "%s%s", base, kFiles[i]);
            what = kFiles[i];
        } else if (i == 2 && env != NULL && env[0] != '\0') {
            snprintf(path, sizeof(path), "%s", env);
            what = "SDL_GAMECONTROLLERCONFIG_FILE";
        } else {
            continue;
        }
        if (!SDL_GetPathInfo(path, NULL)) {
            snprintf(line, sizeof(line), "controller mappings: %s: not there (%s)", what, path);
        } else if ((n = SDL_AddGamepadMappingsFromFile(path)) < 0) {
            snprintf(line, sizeof(line), "controller mappings: %s: not read: %s", what, SDL_GetError());
        } else {
            snprintf(line, sizeof(line), "controller mappings: %s: %d new for this system (%s)", what, n, path);
        }
        pad_log(line);
    }
}

static int guessed(SDL_JoystickID id) {
    unsigned i;
    for (i = 0; i < sGuessedCount && i < MAX_JOYS; i++) {
        if (sGuessed[i] == id) {
            return 1;
        }
    }
    return 0;
}

/* A joystick SDL has no mapping for: into the log, and the common layout when it has a controller's shape. */
static void joystick_unknown(SDL_JoystickID id) {
    static const char *const kTypes[] = {"unknown", "gamepad", "wheel", "arcade stick", "flight stick", "dance pad",
                                         "guitar", "drum kit", "arcade pad", "throttle"};
    SDL_Joystick *j = SDL_OpenJoystick(id);
    SDL_JoystickType type = SDL_GetJoystickTypeForID(id);
    const char *name = SDL_GetJoystickNameForID(id);
    Uint16 vid = SDL_GetJoystickVendorForID(id), pid = SDL_GetJoystickProductForID(id);
    int buttons = j != NULL ? SDL_GetNumJoystickButtons(j) : 0, axes = j != NULL ? SDL_GetNumJoystickAxes(j) : 0;
    int hats = j != NULL ? SDL_GetNumJoystickHats(j) : 0, n, i;
    char guid[33], plain[64], map[512], line[1200];

    if (j != NULL) {
        SDL_CloseJoystick(j);
    }
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
    for (i = n = 0; name != NULL && name[i] != '\0' && n < (int)sizeof(plain) - 1; i++) {
        if (name[i] != ',') { /* (a comma ends the name in a mapping) */
            plain[n++] = name[i];
        }
    }
    plain[n] = '\0';
    if (n == 0) {
        snprintf(plain, sizeof(plain), "Controller %04x:%04x", vid, pid);
    }
    n = snprintf(line, sizeof(line), "controller not recognized: \"%s\", %04x:%04x, GUID %s, type %s, %d buttons, %d axes, %d hats",
                 plain, vid, pid, guid, (unsigned)type < sizeof(kTypes) / sizeof(kTypes[0]) ? kTypes[type] : "other", buttons, axes, hats);
    if ((type == SDL_JOYSTICK_TYPE_UNKNOWN || type == SDL_JOYSTICK_TYPE_GAMEPAD) && buttons >= 10 && axes >= 2) {
        /* The d-pad on the hat; without one (a PlayStation-style pad in its digital mode, the ANALOG light off) the
           d-pad is what moves the first two axes, so they are both the d-pad and the left stick. The right stick only
           where there are axes for it. */
        char right[48] = "";
        if (axes >= 4) {
            snprintf(right, sizeof(right), "rightx:a2,righty:a%d,", vid == 0x0079 && axes >= 5 ? 4 : 3);
        }
        snprintf(map, sizeof(map), "%s,%s,a:b2,b:b1,x:b3,y:b0,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,"
                 "back:b8,start:b9,%s%sleftx:a0,lefty:a1,%s",
                 guid, plain, buttons >= 12 ? "leftstick:b10,rightstick:b11," : "",
                 hats >= 1 ? "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8," : "dpup:-a1,dpdown:+a1,dpleft:-a0,dpright:+a0,", right);
        if (SDL_AddGamepadMapping(map) >= 0) {
            sGuessed[sGuessedCount++ % MAX_JOYS] = id;
            snprintf(line + n, sizeof(line) - n, "; given the common layout: %s", map);
        } else {
            snprintf(line + n, sizeof(line) - n, "; the common layout was refused: %s", SDL_GetError());
        }
    } else {
        snprintf(line + n, sizeof(line) - n, "; not used (not the shape of a controller)");
        if (sUnusableCount < 4) {
            snprintf(sUnusable[sUnusableCount++], sizeof(sUnusable[0]), "%s", plain);
        }
    }
    pad_log(line);
}

/* The joysticks SDL has no mapping for that were not looked at yet. */
static void joysticks_unknown(void) {
    SDL_JoystickID *ids;
    int n = 0, i;
    unsigned k;

    ids = SDL_GetJoysticks(&n);
    for (i = 0; ids != NULL && i < n; i++) {
        for (k = 0; k < sJoySeenCount && k < MAX_JOYS && sJoySeen[k] != ids[i]; k++) {
        }
        if ((k < sJoySeenCount && k < MAX_JOYS) || SDL_IsGamepad(ids[i])) {
            continue;
        }
        sJoySeen[sJoySeenCount++ % MAX_JOYS] = ids[i];
        joystick_unknown(ids[i]);
    }
    SDL_free(ids);
}

/* BT3_PAD_UNKNOWN (see above): attaches or takes away the test joystick at its time. */
static void test_unknown_pad(void) {
    static SDL_JoystickID id;
    static int done;
    const char *t = getenv("BT3_PAD_UNKNOWN"), *off = getenv("BT3_PAD_UNKNOWN_OFF"), *at;
    Uint64 now = SDL_GetTicks() - sInitAt;

    if (t == NULL) {
        return;
    }
    if (id == 0 && !done && now >= (Uint64)((at = strchr(t, '@')) != NULL ? atof(at + 1) * 1000.0 : 0.0)) {
        SDL_VirtualJoystickDesc desc;
        unsigned vid = 0, pid = 0;
        int shape[3] = {12, 0, 1};

        done = 1;
        sscanf(t, "%x:%x", &vid, &pid);
        shape[1] = vid == 0x0079 ? 5 : 4;
        if (getenv("BT3_PAD_UNKNOWN_SHAPE") != NULL) {
            sscanf(getenv("BT3_PAD_UNKNOWN_SHAPE"), "%d,%d,%d", &shape[0], &shape[1], &shape[2]);
        }
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_UNKNOWN; /* (the virtual driver maps only its "gamepad" kind by itself) */
        desc.vendor_id = (Uint16)vid;
        desc.product_id = (Uint16)pid;
        desc.nbuttons = (Uint16)shape[0];
        desc.naxes = (Uint16)shape[1];
        desc.nhats = (Uint16)shape[2];
        desc.name = getenv("BT3_PAD_UNKNOWN_NAME") != NULL ? getenv("BT3_PAD_UNKNOWN_NAME") : "USB Joystick";
        desc.Rumble = test_pad_rumble;
        id = SDL_AttachVirtualJoystick(&desc);
        if (id == 0) {
            fprintf(stderr, "bt3: no test joystick: %s\n", SDL_GetError());
        }
    } else if (id != 0 && off != NULL && now >= (Uint64)(atof(off) * 1000.0)) {
        SDL_DetachVirtualJoystick(id);
        id = 0;
    }
}

/* The SDL id of a player's controller (0: none). */
static SDL_JoystickID player_pad(int player) {
    int slot = sPadSlot[player];
    return slot >= 0 && slot < sIdCount ? sIds[slot] : 0;
}

static const char *open_name(SDL_JoystickID id) {
    int k;
    for (k = 0; k < MAX_PADS; k++) {
        if (sOpen[k].pad != NULL && sOpen[k].id == id) {
            return sOpen[k].name;
        }
    }
    return "?";
}

/* Adds a sentence (a format of the reader's, translated) to text. */
static void say_add(char *text, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
static void say_add(char *text, size_t size, const char *format, ...) {
    size_t len = strlen(text);
    va_list ap;

    if (len > 0 && len + 1 < size) {
        text[len++] = ' ';
        text[len] = '\0';
    }
    va_start(ap, format);
    vsnprintf(text + len, size - len, PortA11y_Tr(format), ap);
    va_end(ap);
}

/* Once per look at the controllers: what came and went (`added`, the ids opened now; `gone`, the names of those
   closed), said; the first time, a few seconds in, the whole of what is there. */
static void pads_say(const SDL_JoystickID *added, int nAdded, char (*gone)[64], int nGone) {
    extern int PortA11y_On(void);
    extern void PortA11y_Say(const char *text, int flags);
    SDL_JoystickID now[2];
    char text[1536], list[768];
    int i, p;

    if (!PortA11y_On() || (!sSaid && SDL_GetTicks() - sInitAt < PAD_SAY_AFTER)) {
        return; /* (what came before the first round is in it) */
    }
    now[0] = player_pad(0);
    now[1] = player_pad(1);
    text[0] = '\0';
    if (!sSaid) {
        list[0] = '\0';
        for (i = 0; i < sIdCount; i++) {
            size_t len = strlen(list);
            snprintf(list + len, sizeof(list) - len, "%s", len > 0 ? "; " : "");
            len = strlen(list);
            if (now[0] == sIds[i] || now[1] == sIds[i]) {
                snprintf(list + len, sizeof(list) - len, PortA11y_Tr("%s, player %d"), open_name(sIds[i]), now[0] == sIds[i] ? 1 : 2);
            } else {
                snprintf(list + len, sizeof(list) - len, PortA11y_Tr("%s, no player"), open_name(sIds[i]));
            }
        }
        if (sIdCount > 0) {
            say_add(text, sizeof(text), "Controllers: %s.", list);
        } else {
            say_add(text, sizeof(text), "No controller: the keyboard plays.");
        }
        for (i = 0; i < sIdCount; i++) {
            if (guessed(sIds[i])) {
                say_add(text, sizeof(text), "%s is not recognized: it was given the most common layout. If its buttons do not match, F1, Controls, \"Set up a controller\" asks for them one by one.",
                        open_name(sIds[i]));
            }
        }
    } else {
        for (i = 0; i < nGone; i++) {
            say_add(text, sizeof(text), "Controller disconnected: %s.", gone[i]);
        }
        for (i = 0; i < nAdded; i++) {
            if (now[0] == added[i] || now[1] == added[i]) {
                say_add(text, sizeof(text), "Controller connected: %s, player %d.", open_name(added[i]), now[0] == added[i] ? 1 : 2);
            } else {
                say_add(text, sizeof(text), "Controller connected: %s, no player (chosen in F1, Controls).", open_name(added[i]));
            }
            if (guessed(added[i])) {
                say_add(text, sizeof(text), "%s is not recognized: it was given the most common layout. If its buttons do not match, F1, Controls, \"Set up a controller\" asks for them one by one.",
                        open_name(added[i]));
            }
        }
        for (p = 0; p < 2; p++) {
            for (i = 0; i < nAdded && added[i] != now[p]; i++) {
            }
            if (now[p] == sSaidPad[p] || i < nAdded) {
                continue; /* (unchanged, or said with "connected") */
            }
            if (now[p] == 0) {
                say_add(text, sizeof(text), "Player %d has no controller.", p + 1);
                if (p == 0) {
                    say_add(text, sizeof(text), "The keyboard still plays.");
                }
            } else {
                say_add(text, sizeof(text), "Player %d: %s.", p + 1, open_name(now[p]));
            }
        }
    }
    for (i = 0; i < sUnusableCount; i++) {
        say_add(text, sizeof(text), "Controller not recognized: %s. F1, Controls, \"Set up a controller\" asks for its buttons one by one.", sUnusable[i]);
    }
    sUnusableCount = 0;
    if (text[0] != '\0') {
        PortA11y_Say(text, sSaid); /* (the first round waits its turn: the game's start is being read) */
    }
    for (p = 0; p < 2; p++) {
        if (now[p] != 0 && now[p] != sSaidPad[p] && !(p == 1 && now[1] == now[0])) {
            SDL_Gamepad *g = SDL_GetGamepadFromID(now[p]);
            if (g != NULL) {
                SDL_SetGamepadPlayerIndex(g, p);
            }
            if (sVib[p][0]) {
                sPulse[p].id = now[p];
                sPulse[p].left = p + 1;
                sPulse[p].next = SDL_GetTicks();
            }
        }
        sSaidPad[p] = now[p];
    }
    sSaid = 1;
}

/* ---- Setting up a controller a button at a time, by voice (F1, Controls, "Set up a controller") ----
 *
 * For a controller whose buttons the game does not know (no mapping, or the common layout guessed wrong) or knows
 * wrongly: the reader asks for each of the PlayStation controller's parts in turn ("Press Cross"), and the press is
 * taken from the controller as it is, a button, a direction of its hat or an axis moved, so that any controller can
 * be set up without seeing it. First "press any button on the controller to set up", which chooses it among those
 * plugged in. A part the controller does not have is skipped by waiting (LEARN_SKIP); Escape stops it all (ui.cpp).
 * At the end the mapping is SDL's at once (the controller becomes a gamepad, or takes the new buttons) and is kept in
 * gamecontrollerdb_own.txt beside the program, read at every start after the community's (mappings_load): the
 * controller is known from then on. Nothing of any controller reaches the game meanwhile (Port_PadRead), nor the
 * settings window (ui.cpp). The settings window's thread asks for it to start or stop (sLearnAsk); all the rest runs
 * on the game's, with the pad reads (learn_tick).
 */
#define LEARN_SKIP 10000   /* ms without a press: the part is skipped */
#define LEARN_CHOOSE 20000 /* ms to press a button on the controller to set up; else it stops */
#define LEARN_LET_GO 3000  /* ms at most waiting for everything to be let go after a press */
#define LEARN_MOVED 20000  /* of 32767: an axis this far from where it rested counts as moved */
#define LEARN_REST 8000    /* ... and back within this as let go */
#define LEARN_JOYS 8
#define LEARN_IN 64        /* a controller's buttons and axes looked at (more are left out) */

enum { L_BUTTON, L_TRIGGER, L_AXIS };
static const struct { const char *out, *name, *ask; int kind; } kLearn[] = {
    {"a", "Cross", "Press Cross: of the four buttons on the right, the one nearest to you.", L_BUTTON},
    {"b", "Circle", "Press Circle: of the four, the one on the right.", L_BUTTON},
    {"x", "Square", "Press Square: of the four, the one on the left.", L_BUTTON},
    {"y", "Triangle", "Press Triangle: of the four, the one farthest from you.", L_BUTTON},
    {"dpup", "D-pad up", "Press up on the d-pad, the cross on the left.", L_BUTTON},
    {"dpdown", "D-pad down", "Press down on the d-pad.", L_BUTTON},
    {"dpleft", "D-pad left", "Press left on the d-pad.", L_BUTTON},
    {"dpright", "D-pad right", "Press right on the d-pad.", L_BUTTON},
    {"leftshoulder", "L1", "Press L1: the upper button on the left edge, under your index finger.", L_BUTTON},
    {"rightshoulder", "R1", "Press R1: the upper button on the right edge.", L_BUTTON},
    {"lefttrigger", "L2", "Press L2: the lower button on the left edge.", L_TRIGGER},
    {"righttrigger", "R2", "Press R2: the lower button on the right edge.", L_TRIGGER},
    {"back", "SELECT", "Press Select: of the small buttons in the middle, the one on the left.", L_BUTTON},
    {"start", "START", "Press Start: of the small buttons in the middle, the one on the right.", L_BUTTON},
    {"leftstick", "L3", "Press the left stick down into the controller, until it clicks.", L_BUTTON},
    {"rightstick", "R3", "Press the right stick down into the controller, until it clicks.", L_BUTTON},
    {"leftx", "Left stick, left and right", "Push the left stick all the way to the right, and let it go.", L_AXIS},
    {"lefty", "Left stick, up and down", "Push the left stick all the way down, towards you, and let it go.", L_AXIS},
    {"rightx", "Right stick, left and right", "Push the right stick all the way to the right, and let it go.", L_AXIS},
    {"righty", "Right stick, up and down", "Push the right stick all the way down, towards you, and let it go.", L_AXIS},
};
#define LEARN_STEPS ((int)(sizeof(kLearn) / sizeof(kLearn[0])))

static volatile int sLearnAsk; /* the settings window's thread: 1 start, 2 stop (taken by learn_tick) */
static struct {
    int stage;     /* 0 not running, 1 choosing the controller, 2 asking for its parts */
    int step;      /* the part asked for (stage 2); LEARN_STEPS: all asked */
    int letGo;     /* a press was taken: the next question once everything is let go */
    int again;     /* the press was refused: the same question again, after the reason, without cutting it */
    Uint64 since;  /* when the question was asked, or the press taken (SDL_GetTicks) */
    SDL_Joystick *joy[LEARN_JOYS]; /* stage 1: every joystick, opened here; stage 2: [0], the one chosen */
    int joys;
    Sint16 rest[LEARN_JOYS][LEARN_IN]; /* each axis where it rested when the question was asked */
    Uint8 held[LEARN_JOYS][LEARN_IN];  /* each button held then (not a press until it is let go) */
    char bind[LEARN_STEPS][16];        /* what each part became ("b2", "h0.1", "+a3", "a2", "a1~"); "" skipped */
    char status[256];                  /* what is asked now, in English (the settings window shows it) */
} sLearn;

/* Says a line of the reader's (a format, translated) and keeps it in English for the settings window. */
static void learn_say(int flags, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void learn_say(int flags, const char *format, ...) {
    extern void PortA11y_Say(const char *text, int flags);
    char text[512];
    va_list ap, ap2;

    va_start(ap, format);
    va_copy(ap2, ap);
    vsnprintf(sLearn.status, sizeof(sLearn.status) - 1, format, ap);
    vsnprintf(text, sizeof(text), PortA11y_Tr(format), ap2);
    va_end(ap2);
    va_end(ap);
    PortA11y_Say(text, flags);
}

static void learn_close(void) {
    int i;

    for (i = 0; i < sLearn.joys; i++) {
        if (sLearn.joy[i] != NULL) {
            SDL_CloseJoystick(sLearn.joy[i]);
        }
        sLearn.joy[i] = NULL;
    }
    sLearn.joys = 0;
    sLearn.stage = 0;
}

static void learn_stop(const char *why) {
    char line[320];

    learn_close();
    learn_say(1, "%s", PortA11y_Tr(why));
    snprintf(sLearn.status, sizeof(sLearn.status) - 1, "%s", why);
    snprintf(line, sizeof(line), "controller setup: %s", why);
    pad_log(line);
}

/* Where joystick i's axes rest and which of its buttons are held, now. */
static void learn_rest(int i) {
    SDL_Joystick *j = sLearn.joy[i];
    int k, n;

    n = SDL_GetNumJoystickAxes(j);
    for (k = 0; k < n && k < LEARN_IN; k++) {
        sLearn.rest[i][k] = SDL_GetJoystickAxis(j, k);
    }
    n = SDL_GetNumJoystickButtons(j);
    for (k = 0; k < n && k < LEARN_IN; k++) {
        sLearn.held[i][k] = SDL_GetJoystickButton(j, k) ? 1 : 0;
    }
}

/* A press on joystick i for a part of this kind, as a mapping's input: a new button, a hat's direction, or the axis
   that moved most. 0: none. Asked to the right and down, a stick's axis that goes the other way is turned ("~"); a
   trigger alone on its axis rests at one end of it and takes all of it; a part on half an axis (a d-pad given as an
   axis, two triggers on one) takes that half. */
static int learn_press(int i, int kind, char *bind, int size) {
    SDL_Joystick *j = sLearn.joy[i];
    int k, n, best = -1, most = 0, dev, rest;

    n = SDL_GetNumJoystickButtons(j);
    for (k = 0; k < n && k < LEARN_IN; k++) {
        if (!SDL_GetJoystickButton(j, k)) {
            sLearn.held[i][k] = 0; /* (held when asked, let go since: its next press counts) */
        } else if (!sLearn.held[i][k] && kind != L_AXIS) {
            snprintf(bind, size, "b%d", k);
            return 1;
        }
    }
    n = SDL_GetNumJoystickHats(j);
    for (k = 0; k < n && kind != L_AXIS; k++) {
        Uint8 h = SDL_GetJoystickHat(j, k);
        if (h == SDL_HAT_UP || h == SDL_HAT_RIGHT || h == SDL_HAT_DOWN || h == SDL_HAT_LEFT) {
            snprintf(bind, size, "h%d.%d", k, (int)h);
            return 1;
        }
    }
    n = SDL_GetNumJoystickAxes(j);
    for (k = 0; k < n && k < LEARN_IN; k++) {
        dev = (int)SDL_GetJoystickAxis(j, k) - sLearn.rest[i][k];
        if ((dev < 0 ? -dev : dev) > (most < 0 ? -most : most)) {
            best = k;
            most = dev;
        }
    }
    if (best < 0 || (most < 0 ? -most : most) < LEARN_MOVED) {
        return 0;
    }
    rest = sLearn.rest[i][best];
    if (kind == L_AXIS) {
        snprintf(bind, size, "a%d%s", best, most > 0 ? "" : "~");
    } else if (kind == L_TRIGGER && (rest < -24000 || rest > 24000)) {
        snprintf(bind, size, "a%d%s", best, rest > 0 ? "~" : "");
    } else {
        snprintf(bind, size, "%sa%d", most > 0 ? "+" : "-", best);
    }
    return 1;
}

/* 1 when nothing of joystick i is pressed or moved any more. */
static int learn_let_go(int i) {
    SDL_Joystick *j = sLearn.joy[i];
    int k, n, d;

    n = SDL_GetNumJoystickButtons(j);
    for (k = 0; k < n && k < LEARN_IN; k++) {
        if (SDL_GetJoystickButton(j, k)) {
            return 0;
        }
    }
    n = SDL_GetNumJoystickHats(j);
    for (k = 0; k < n; k++) {
        if (SDL_GetJoystickHat(j, k) != SDL_HAT_CENTERED) {
            return 0;
        }
    }
    n = SDL_GetNumJoystickAxes(j);
    for (k = 0; k < n && k < LEARN_IN; k++) {
        d = (int)SDL_GetJoystickAxis(j, k) - sLearn.rest[i][k];
        if (d > LEARN_REST || d < -LEARN_REST) {
            return 0;
        }
    }
    return 1;
}

/* The part a press was already taken for (its index + 1), or 0: the same input, or a stick's axis already a stick's. */
static int learn_used(int step, const char *bind) {
    int s;

    for (s = 0; s < step; s++) {
        const char *b = sLearn.bind[s];
        if (b[0] == '\0') {
            continue;
        }
        if (strcmp(b, bind) == 0 ||
            (kLearn[step].kind == L_AXIS && kLearn[s].kind == L_AXIS && atoi(b + 1) == atoi(bind + 1))) {
            return s + 1;
        }
    }
    return 0;
}

/* The game's thread: the request of the settings window's, to start. Every joystick plugged in is opened, to see
   which one is pressed. */
static void learn_begin(void) {
    SDL_JoystickID *ids;
    int n = 0, i;

    learn_close();
    memset(sLearn.bind, 0, sizeof(sLearn.bind));
    ids = SDL_GetJoysticks(&n);
    for (i = 0; ids != NULL && i < n && sLearn.joys < LEARN_JOYS; i++) {
        SDL_Joystick *j = SDL_OpenJoystick(ids[i]);
        if (j != NULL) {
            sLearn.joy[sLearn.joys] = j;
            learn_rest(sLearn.joys);
            sLearn.joys++;
        }
    }
    SDL_free(ids);
    if (sLearn.joys == 0) {
        learn_stop("No controller is plugged in: plug one in and try again.");
        return;
    }
    sLearn.stage = 1;
    sLearn.since = SDL_GetTicks();
    learn_say(1, "Setting up a controller. Press any button on the controller to set up. Escape stops.");
    pad_log("controller setup: started");
}

static void learn_ask(void) {
    if (sLearn.step < LEARN_STEPS) {
        learn_say(sLearn.again ? 0 : 1, "%s", PortA11y_Tr(kLearn[sLearn.step].ask));
        snprintf(sLearn.status, sizeof(sLearn.status) - 1, "%s", kLearn[sLearn.step].ask);
    }
    sLearn.again = 0;
    sLearn.since = SDL_GetTicks();
}

/* The parts asked for, as SDL's mapping: made SDL's at once and kept for the next starts. */
static void learn_finish(void) {
    SDL_Joystick *j = sLearn.joy[0];
    const char *name = SDL_GetJoystickName(j), *base = SDL_GetBasePath();
    char guid[33], plain[64], map[1200], path[1024], line[1400];
    int s, n, got = 0, kept = 0;
    SDL_IOStream *io;

    SDL_GUIDToString(SDL_GetJoystickGUID(j), guid, sizeof(guid));
    for (s = n = 0; name != NULL && name[s] != '\0' && n < (int)sizeof(plain) - 1; s++) {
        if (name[s] != ',') {
            plain[n++] = name[s];
        }
    }
    plain[n] = '\0';
    if (n == 0) {
        snprintf(plain, sizeof(plain), "Controller %04x:%04x", SDL_GetJoystickVendor(j), SDL_GetJoystickProduct(j));
    }
    n = snprintf(map, sizeof(map), "%s,%s,", guid, plain);
    for (s = 0; s < LEARN_STEPS; s++) {
        if (sLearn.bind[s][0] != '\0' && n < (int)sizeof(map)) {
            n += snprintf(map + n, sizeof(map) - n, "%s:%s,", kLearn[s].out, sLearn.bind[s]);
            got++;
        }
    }
    if (n < (int)sizeof(map)) {
        snprintf(map + n, sizeof(map) - n, "platform:%s,", SDL_GetPlatform());
    }
    if (got == 0) {
        learn_stop("Nothing was pressed: the setup stopped, and nothing changed.");
        return;
    }
    if (SDL_AddGamepadMapping(map) < 0) {
        snprintf(line, sizeof(line), "controller setup: SDL refused the mapping (%s): %s", SDL_GetError(), map);
        pad_log(line);
        learn_close();
        learn_say(1, "The setup could not be used. The game's log has the reason.");
        return;
    }
    if (base != NULL) {
        snprintf(path, sizeof(path), "%sgamecontrollerdb_own.txt", base);
        if ((io = SDL_IOFromFile(path, "ab")) != NULL) {
            kept = SDL_WriteIO(io, map, strlen(map)) == strlen(map) && SDL_WriteIO(io, "\n", 1) == 1;
            SDL_CloseIO(io);
        }
    }
    snprintf(line, sizeof(line), "controller setup: %d of %d parts, %s: %s", got, LEARN_STEPS, kept ? "kept in gamecontrollerdb_own.txt" : "NOT kept (the file could not be written)", map);
    pad_log(line);
    learn_close();
    if (kept) {
        learn_say(1, "Done: %s is set up, %d parts of %d, and it is kept for the next times.", plain, got, LEARN_STEPS);
    } else {
        learn_say(1, "Done: %s works now, %d parts of %d, but it could not be kept for the next times: the game's folder cannot be written.",
                  plain, got, LEARN_STEPS);
    }
}

/* The game's thread, every pad read. */
static void learn_tick(void) {
    Uint64 now = SDL_GetTicks();
    char bind[16], line[160];
    int i, used, ask = sLearnAsk;

    if (ask != 0) {
        sLearnAsk = 0;
        if (ask == 1) {
            learn_begin();
        } else if (sLearn.stage != 0) {
            learn_stop("The setup stopped. The controller is as it was.");
        }
        return;
    }
    if (sLearn.stage == 1) {
        if (now - sLearn.since < 400) { /* (a joystick just opened reads its axes as resting until they first report:
                                           where they really rest is taken once they have) */
            for (i = 0; i < sLearn.joys; i++) {
                if (sLearn.joy[i] != NULL) {
                    learn_rest(i);
                }
            }
            return;
        }
        for (i = 0; i < sLearn.joys; i++) {
            if (sLearn.joy[i] != NULL && learn_press(i, L_BUTTON, bind, sizeof(bind))) {
                SDL_Joystick *chosen = sLearn.joy[i];
                int k;
                for (k = 0; k < sLearn.joys; k++) { /* (only the chosen one stays open) */
                    if (k != i && sLearn.joy[k] != NULL) {
                        SDL_CloseJoystick(sLearn.joy[k]);
                    }
                    sLearn.joy[k] = NULL;
                }
                sLearn.joy[0] = chosen;
                sLearn.joys = 1;
                learn_rest(0);
                sLearn.stage = 2;
                sLearn.step = 0;
                sLearn.letGo = 1;
                sLearn.again = 1; /* (the first question waits for this line) */
                sLearn.since = now;
                learn_say(1, "%s. Now each part in turn. A part it does not have: wait ten seconds, and it is skipped.",
                          SDL_GetJoystickName(chosen) != NULL ? SDL_GetJoystickName(chosen) : "?");
                return;
            }
        }
        if (now - sLearn.since > LEARN_CHOOSE) {
            learn_stop("No button was pressed: the setup stopped.");
        }
        return;
    }
    if (sLearn.stage != 2) {
        return;
    }
    if (!SDL_JoystickConnected(sLearn.joy[0])) {
        learn_stop("The controller was unplugged: the setup stopped.");
        return;
    }
    if (sLearn.letGo) {
        if (!learn_let_go(0) && now - sLearn.since < LEARN_LET_GO) {
            return;
        }
        sLearn.letGo = 0;
        learn_rest(0); /* (what is still held now is not a press: let go, it can be one again) */
        if (sLearn.step >= LEARN_STEPS) {
            learn_finish();
        } else {
            learn_ask();
        }
        return;
    }
    if (learn_press(0, kLearn[sLearn.step].kind, bind, sizeof(bind))) {
        if ((used = learn_used(sLearn.step, bind)) > 0) {
            learn_say(1, "That one is already %s. Press another, or wait to skip.", PortA11y_Tr(kLearn[used - 1].name));
            sLearn.again = 1;
        } else {
            snprintf(sLearn.bind[sLearn.step], sizeof(sLearn.bind[0]), "%s", bind);
            snprintf(line, sizeof(line), "controller setup: %s is %s", kLearn[sLearn.step].out, bind);
            pad_log(line);
            sLearn.step++;
        }
        sLearn.letGo = 1;
        sLearn.since = now;
        return;
    }
    if (now - sLearn.since > LEARN_SKIP) {
        learn_say(1, "Skipped.");
        sLearn.again = 1;
        sLearn.step++;
        if (sLearn.step >= LEARN_STEPS) {
            learn_finish();
        } else {
            learn_rest(0);
            learn_ask();
        }
    }
}

/* The settings window (ui.cpp): start, stop, and whether it runs; what it asks now. */
void PortInput_LearnStart(void) { sLearnAsk = 1; }
void PortInput_LearnStop(void) { sLearnAsk = 2; }
int PortInput_Learning(void) { return sLearn.stage != 0 || sLearnAsk == 1; }
const char *PortInput_LearnStatus(void) { return sLearn.status; }

/* ---- Player 1's controller: the one that is pressed ----
 *
 * Windows can list first a controller nobody holds (a virtual one made by DS4Windows, HidHide, ViGEm, vJoy, Parsec or
 * a handheld's own software; the second half of an adapter for two controllers), and player 1 then had that one: the
 * real controller seemed not to work at all (a tester's, with Steam closed). So, while player 1's controller has not
 * been pressed in this run and the settings window is closed, the first press of another controller makes it player
 * 1's; when it was player 2's, the two change places. Once player 1's controller has been pressed it stays. Said, as
 * any change of a player's controller (pads_say), and not saved: the settings keep what F1 chose. pad_follow=0 in the
 * settings file turns it off.
 */
static SDL_JoystickID sPressed[MAX_PADS]; /* the controllers pressed in this run */
static int sPressedCount;

static int was_pressed(SDL_JoystickID id) {
    int i;

    for (i = 0; i < sPressedCount; i++) {
        if (sPressed[i] == id) {
            return 1;
        }
    }
    return 0;
}

static int pad_pressed_now(SDL_Gamepad *g) {
    int b;

    for (b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        if (SDL_GetGamepadButton(g, (SDL_GamepadButton)b)) {
            return 1;
        }
    }
    return SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > PORT_TRIGGER_AT ||
           SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > PORT_TRIGGER_AT; /* (not the sticks: a cheap one drifts) */
}

static void pads_follow(void) {
    static int on = -1;
    SDL_JoystickID mine, theirs;
    char line[256];
    int k, slot;

    if (on < 0) {
        on = Port_Setting("pad_follow", 1) != 0;
    }
    for (k = 0; k < MAX_PADS; k++) {
        if (sOpen[k].pad == NULL || was_pressed(sOpen[k].id) || !pad_pressed_now(sOpen[k].pad)) {
            continue;
        }
        if (gPortOverlayOpen || gPortInputCapture || sLearn.stage != 0) {
            continue; /* (a press in the settings window, or a setup's, is not the game's: not counted) */
        }
        if (sPressedCount < MAX_PADS) {
            sPressed[sPressedCount++] = sOpen[k].id;
        }
        mine = player_pad(0);
        theirs = player_pad(1);
        if (!on || sPadSlot[0] < 0 || sOpen[k].id == mine || (mine != 0 && was_pressed(mine))) {
            continue; /* (player 1 on "None" in F1 plays with the keyboard on purpose: nothing is taken from player 2) */
        }
        for (slot = 0; slot < sIdCount && sIds[slot] != sOpen[k].id; slot++) {
        }
        if (slot == sIdCount) {
            continue;
        }
        if (sOpen[k].id == theirs) {
            sPadSlot[1] = sPadSlot[0];
        }
        sPadSlot[0] = slot;
        snprintf(line, sizeof(line), "controllers: player 1 takes \"%s\", the first one pressed (player 1's had not been)%s", sOpen[k].name,
                 sOpen[k].id == theirs ? "; player 2 takes player 1's" : "");
        pad_log(line);
    }
}

/* ---- A controller's presses that also come as keys ----
 *
 * Steam's desktop layout, DS4Windows, JoyToKey, reWASD, a handheld's own software (Armoury Crate, Legion Space) and
 * others turn a controller's buttons into keys too, and the game got both: Circle was also Escape and asked to close
 * the game, Cross was also Enter, START. A key pressed within ECHO_NS after a controller's button is taken for one of
 * those, and the window's events leave it out (PortInput_Echo, from gs_gpu.c and gs_gl.c). Once three have come so,
 * that is said and logged, and the keyboard is not read for the game while a controller was used within the last
 * second and a half (Port_PadRead): the keys held by that program are not the game's either.
 */
#define ECHO_NS 50000000ull       /* 50 ms */
#define ECHO_HOLD_NS 1500000000ull
static volatile Uint64 sPadPressNs, sPadUseNs; /* (the event watch's: the thread that takes the window's events) */
static volatile int sEchoes;

static bool SDLCALL echo_watch(void *unused, SDL_Event *ev) {
    (void)unused;
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || ev->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ||
        (ev->type == SDL_EVENT_JOYSTICK_HAT_MOTION && ev->jhat.value != SDL_HAT_CENTERED)) {
        sPadPressNs = sPadUseNs = ev->common.timestamp;
    } else if (ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (ev->gaxis.value > 16000 || ev->gaxis.value < -16000)) {
        sPadUseNs = ev->common.timestamp;
    }
    return true;
}

int PortInput_Echo(const SDL_Event *ev) {
    extern void PortA11y_Say(const char *text, int flags);
    Uint64 at = sPadPressNs;

    if (ev->type != SDL_EVENT_KEY_DOWN || ev->key.repeat || at == 0 || ev->common.timestamp < at || ev->common.timestamp - at > ECHO_NS) {
        return 0;
    }
    if (++sEchoes == 3) {
        char line[160];
        snprintf(line, sizeof(line), "controllers: keys come with the controller's presses (the third: %s, %u ms after): the keyboard is left out "
                 "while a controller is used", SDL_GetScancodeName(ev->key.scancode), (unsigned)((ev->common.timestamp - at) / 1000000ull));
        pad_log(line);
        PortA11y_Say(PortA11y_Tr("The controller's buttons also arrive as keyboard keys: a program turns them into keys (Steam, "
                                 "DS4Windows, JoyToKey, or the console's own software). The game now leaves the keyboard out while "
                                 "the controller is used."), 0);
    }
    return 1;
}

/* Every pad read: the identification pulses that are due, the log's lines kept from before the reader started, and
   the test joystick. */
static void pads_tick(void) {
    extern void PortA11y_Note(const char *text);
    extern int PortA11y_On(void);
    Uint64 now = SDL_GetTicks();
    int p, i;

    for (p = 0; p < 2; p++) {
        SDL_Gamepad *g;
        if (sPulse[p].left <= 0 || now < sPulse[p].next) {
            continue;
        }
        if ((g = SDL_GetGamepadFromID(sPulse[p].id)) == NULL) {
            sPulse[p].left = 0;
            continue;
        }
        if (getenv("BT3_RUMBLE_LOG") != NULL) {
            fprintf(stderr, "rumble: identification, player %d -> SDL_RumbleGamepad(low 40000, high 40000, 200 ms)\n", p + 1);
        }
        SDL_RumbleGamepad(g, 40000, 40000, 200);
        sPulse[p].left--;
        sPulse[p].next = now + 450;
    }
    learn_tick();
    pads_follow();
    if (sPendingCount > 0 && PortA11y_On()) {
        for (i = 0; i < sPendingCount; i++) {
            PortA11y_Note(sPending[i]);
        }
        sPendingCount = 0;
    }
    test_unknown_pad();
}

static void pads_refresh(void) {
    static unsigned calls;
    SDL_JoystickID *ids, added[MAX_PADS];
    char gone[MAX_PADS][64];
    int n = 0, i, k, nAdded = 0, nGone = 0;

    if (!sInit) {
        SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        sInit = 1;
        sInitAt = SDL_GetTicks();
        SDL_AddEventWatch(echo_watch, NULL);
        mappings_load();
        test_pad();
        test_unknown_pad();
    } else {
        pads_tick();
        if (calls++ % 120 != 0) { /* look for plugged and unplugged controllers about once a second */
            return;
        }
    }
    joysticks_unknown();
    ids = SDL_GetGamepads(&n);
    sIdCount = 0;
    for (i = 0; ids != NULL && i < n && sIdCount < MAX_PADS; i++) {
        if (sTestPad != NULL && !SDL_IsJoystickVirtual(ids[i])) {
            continue; /* (a test is never felt on a real controller) */
        }
        sIds[sIdCount++] = ids[i];
    }
    SDL_free(ids);
    for (k = 0; k < MAX_PADS; k++) { /* close the ones that are gone */
        for (i = 0; i < sIdCount && sIds[i] != sOpen[k].id; i++) {
        }
        if (sOpen[k].pad != NULL && i == sIdCount) {
            snprintf(gone[nGone++], sizeof(gone[0]), "%s", sOpen[k].name);
            SDL_CloseGamepad(sOpen[k].pad);
            sOpen[k].pad = NULL;
        }
    }
    for (i = 0; i < sIdCount; i++) { /* open the new ones */
        for (k = 0; k < MAX_PADS && !(sOpen[k].pad != NULL && sOpen[k].id == sIds[i]); k++) {
        }
        if (k == MAX_PADS) {
            for (k = 0; k < MAX_PADS && sOpen[k].pad != NULL; k++) {
            }
            if (k < MAX_PADS) {
                sOpen[k].id = sIds[i];
                sOpen[k].pad = SDL_OpenGamepad(sIds[i]);
                if (sOpen[k].pad != NULL) {
                    const char *name = SDL_GetGamepadName(sOpen[k].pad);
                    snprintf(sOpen[k].name, sizeof(sOpen[k].name), "%s", name != NULL && name[0] != '\0' ? name : "?");
                    pad_note(sOpen[k].pad);
                    added[nAdded++] = sIds[i];
                }
            }
        }
    }
    pads_say(added, nAdded, gone, nGone);
    if (calls < 400) {
        test_pad_axes();
    }
}

static SDL_Gamepad *pad_of(int player) {
    int slot = sPadSlot[player], k;
    if (slot < 0 || slot >= sIdCount) {
        return NULL;
    }
    for (k = 0; k < MAX_PADS; k++) {
        if (sOpen[k].pad != NULL && sOpen[k].id == sIds[slot]) {
            return sOpen[k].pad;
        }
    }
    return NULL;
}

/* BT3_PAD="frame:button,frame:button,...": scripted presses for player 1, each held for 4 frames from that frame
   (frame = the renderer's frame counter). Buttons: start select up down left right cross circle square triangle
   l1 r1 l2 r2. For tests without a person at the keyboard. */
static unsigned script_buttons(void) {
    static const struct { const char *name; unsigned bit; } names[] = {
        {"start", B_START}, {"select", B_SELECT}, {"up", B_UP}, {"down", B_DOWN}, {"left", B_LEFT}, {"right", B_RIGHT},
        {"cross", B_CROSS}, {"circle", B_CIRCLE}, {"square", B_SQUARE}, {"triangle", B_TRIANGLE}, {"l1", B_L1}, {"r1", B_R1},
        {"l2", B_L2}, {"r2", B_R2},
    };
    static struct { unsigned frame, bit; } ev[256];
    static int count = -1;
    unsigned btn = 0;
    int i;

    if (count < 0) {
        const char *p = getenv("BT3_PAD");
        count = 0;
        while (p != NULL && *p != 0 && count < 256) {
            char name[16];
            unsigned frame;
            int used = 0;
            size_t k;
            if (sscanf(p, "%u:%15[a-z0-9]%n", &frame, name, &used) != 2) {
                break;
            }
            for (k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
                if (strcmp(name, names[k].name) == 0) {
                    ev[count].frame = frame;
                    ev[count++].bit = names[k].bit;
                }
            }
            p += used;
            if (*p == ',') { p++; }
        }
    }
    for (i = 0; i < count; i++) {
        if (gGsFrame >= ev[i].frame && gGsFrame < ev[i].frame + 4) {
            btn |= ev[i].bit;
        }
    }
    return btn;
}

/* Fills `data` (18 bytes) for controller port `socket`. Returns 0 when there is no window (the caller keeps the
   idle pad). */
/* "Clashes: Square counts as a direction" (setting clash_square, off by default; BT3_CLASH_SQUARE=1).
 *
 * In a clash (two beams or two rushes meeting) the game counts one thing: the frames in which a direction is newly
 * pressed (input 51 = BTLC_DIR_P; BtlAct_ClashStruggleHandler and BtlAct_ClashBlowsHandler). Turning the stick is
 * the usual way to make them, four a turn; tapping directions makes them as well. With the setting on, while this
 * player's fighter is in a clash action, Square is taken off the pad and each press of it holds the stick in the
 * next of up, right, down, left for as long as it is held: one press, one newly pressed direction, as a tap of
 * the d-pad gives. The stick and the d-pad go on working beside it.
 *
 * A press can be made to count for more than one (setting clash_square_x, in quarters: 4, 6, 7, 8 or 9 for 1, 1.5,
 * 1.75, 2 or 2.25; 2.25 unless set; BT3_CLASH_SQUARE_X): the d-pad has four buttons to tap in turn and the stick four
 * directions a turn, Square is one button. What a press is worth beyond one is kept as credit, and whenever a
 * whole point of it is owed the stick is put in the next direction for one frame without a press: four presses at
 * 1.75 give 2, 2, 2, 1. Still at most one newly pressed direction a frame, the game's own limit, and credit does
 * not pile up beyond two points or outlast the clash.
 *
 * This is done to the pad's own bytes, before they reach the game or the other player of an online match: the
 * game sees an ordinary pad, so the two copies of an online match compute the same fight whoever has it on. */
static int sClashSquare = -1;

int Port_ClashSquare(void) {
    if (sClashSquare < 0) {
        sClashSquare = getenv("BT3_CLASH_SQUARE") != NULL ? atoi(getenv("BT3_CLASH_SQUARE")) != 0 : Port_Setting("clash_square", 0) != 0;
    }
    return sClashSquare;
}

/* What a press of Square is worth in a clash, in quarters of a point (4, 6, 7, 8 or 9). */
static int sClashSquareX = -1;

int Port_ClashSquareX(void) {
    if (sClashSquareX < 0) {
        int x = getenv("BT3_CLASH_SQUARE_X") != NULL ? atoi(getenv("BT3_CLASH_SQUARE_X")) : Port_Setting("clash_square_x", 9);
        sClashSquareX = x == 4 || x == 6 || x == 7 || x == 8 || x == 9 ? x : 9;
    }
    return sClashSquareX;
}

void Port_ClashSquareXSet(int quarters) {
    sClashSquareX = quarters == 4 || quarters == 6 || quarters == 7 || quarters == 8 || quarters == 9 ? quarters : 9;
    Port_SettingSave("clash_square_x", sClashSquareX);
    Port_SettingsWrite(); /* (in the file at once: the value was lost at the end unless another setting was written after it) */
}

void Port_ClashSquareSet(int on) {
    sClashSquare = on != 0;
    Port_SettingSave("clash_square", sClashSquare);
    Port_SettingsWrite();
}

static void clash_square(int socket, unsigned *btn, unsigned char *lx, unsigned char *ly) {
    extern void *BtlChar_FindByObjId(int objId); /* the game: the fighter of a side, NULL outside a fight */
    extern int BtlAct_GetCurrent(void *chr);     /* its action */
    extern int Port_NetActive(void), Port_NetMe(void);
    static int dir[2], was[2], credit[2]; /* (credit: quarters of a point owed beyond the press's own) */
    int side = Port_NetActive() ? Port_NetMe() : socket, in = 0, down = (*btn & B_SQUARE) != 0, extra = 0;
    void *chr;

    if (!Port_ClashSquare()) {
        return;
    }
    if (getenv("BT3_CLASH_TEST") != NULL) {
        in = 1; /* testing: as if in a clash all the time */
    } else if ((chr = BtlChar_FindByObjId(side)) != NULL) {
        int action = BtlAct_GetCurrent(chr);
        in = (action >= 0x130 && action <= 0x132) || action == 0xFA || action == 0xFB || action == 0xFC;
    }
    if (!in) {
        was[socket] = down; /* (a Square held into the clash is not a press in it) */
        credit[socket] = 0;
        return;
    }
    if (down && !was[socket]) {
        dir[socket] = (dir[socket] + 1) & 3;
        credit[socket] += Port_ClashSquareX() - 4;
        if (credit[socket] > 8) {
            credit[socket] = 8;
        }
    } else if (credit[socket] >= 4) { /* a whole point is owed: the next direction for this frame, without a press */
        dir[socket] = (dir[socket] + 1) & 3;
        credit[socket] -= 4;
        extra = 1;
    }
    was[socket] = down;
    *btn &= ~(unsigned)B_SQUARE;
    if (down || extra) {
        switch (dir[socket]) {
        case 0: *ly = 0x00; break; /* up */
        case 1: *lx = 0xFF; break; /* right */
        case 2: *ly = 0xFF; break; /* down */
        default: *lx = 0x00; break; /* left */
        }
    }
}

/* BT3_CLASH_LOG=1: one line for every point a clash counts from a newly pressed direction (the stick, the d-pad, or
   Square with the option above), and the total as a beam clash ends. Called from the game's clash handlers. */
void Port_ClashLog(int player, const char *what, int fromInput, int total, int frame) {
    static int on = -1;

    if (on < 0) {
        on = getenv("BT3_CLASH_LOG") != NULL;
    }
    if (!on) {
        return;
    }
    if (fromInput) {
        fprintf(stderr, "clash: player %d, %s, frame %d: +1 from input, total %d%s\n", player + 1, what, frame, total,
                Port_ClashSquare() ? " (Square option on)" : "");
    } else {
        fprintf(stderr, "clash: player %d, %s ended after %d frames with %d points\n", player + 1, what, frame, total);
    }
}

/* The sticks.
 *
 * The game (src/sys/pad.c, Pad_Update) takes each axis as a byte, 0..255 with the rest between 127 and 128, and
 * gives every axis by itself a deadzone of PAD_STICK_DEADZONE = 50 of the 127.5 (39%): value = (|byte - 127.5| -
 * 50) / 77.5, zero below. The pair is then brought inside the unit circle, and the stick counts as a direction
 * (the menus, dashes, the clash counter) when the larger of the two values is beyond 0.5: 69.6% of the way on an
 * axis. Its zone of rest is therefore a square, and a diagonal starts late.
 *
 * Deadzone -1 ("Original") passes the controller's bytes as they are: that. A deadzone d of 0..PORT_DEADZONE_MAX
 * percent is a round one instead: with m the stick's distance from the centre (0..1, the corners of a square gate
 * cut to 1), nothing below d, and from there
 *     out = (m - d) / (1 - d)        in the stick's own direction
 * which is zero at the edge of the zone and 1 at full tilt. The bytes are then the ones the game's own formula
 * turns back into `out` (0x80 for zero, else 127.5 +- (50 + |out| * 77.5), to the nearest of the 78 steps a side),
 * so the game is left as it is and its direction test, 0.5 of `out`, falls at d + (1 - d) / 2 of the way: half way
 * between the zone's edge and full tilt, in every direction. It is done to the pad's bytes, before they reach the
 * game or the other player of an online match (as the Square option above).
 */
static unsigned char game_byte(double v) {
    double a = fabs(v) * 77.5;
    int k;

    if (a < 0.25) { /* (nearer to nothing than to the first step, 0.5) */
        return 0x80;
    }
    k = range((int)a, 0, 77); /* the steps are at k + 0.5 */
    return (unsigned char)(v > 0.0 ? 178 + k : 77 - k);
}

static void stick_bytes(int deadzone, int x, int y, unsigned char *bx, unsigned char *by) {
    double fx = x < -32767 ? -1.0 : x / 32767.0, fy = y < -32767 ? -1.0 : y / 32767.0;
    double m = sqrt(fx * fx + fy * fy), d = deadzone / 100.0, out;

    if (deadzone < 0) {
        *bx = (unsigned char)((x + 32768) >> 8);
        *by = (unsigned char)((y + 32768) >> 8);
        return;
    }
    if (m <= d || m <= 0.0) {
        *bx = *by = 0x80;
        return;
    }
    out = ((m > 1.0 ? 1.0 : m) - d) / (1.0 - d);
    *bx = game_byte(fx / m * out);
    *by = game_byte(fy / m * out);
}

/* What the game makes of a stick's two bytes: Pad_StickToFloat and Pad_Update's steps (src/sys/pad.c), done again
   here in the same single-precision arithmetic. (The game's own function cannot be called from this side: its
   floats are not passed as the host's are. BT3_STICK_TEST prints this beside what the game itself then has.) */
static float game_axis(unsigned char raw) {
    float value = (float)raw - 127.5f;

    if (0.0f < value) {
        value -= 50.0f;
        if (value < 0.0f) {
            value = 0.0f;
        }
    } else {
        value += 50.0f;
        if (0.0f < value) {
            value = 0.0f;
        }
    }
    return value / (127.5f - 50.0f);
}

static void game_stick(const unsigned char b[2], float out[2], int *dir) {
    float len;

    out[0] = game_axis(b[0]);
    out[1] = game_axis(b[1]);
    len = out[0] * out[0] + out[1] * out[1];
    if (len > 1.0f) {
        len = 1.0f / sqrtf(len);
        out[0] *= len;
        out[1] *= len;
    }
    if (fabsf(out[0]) > fabsf(out[1])) {
        *dir = out[0] > 0.5f ? 4 : out[0] < -0.5f ? 3 : 0;
    } else {
        *dir = out[1] > 0.5f ? 2 : out[1] < -0.5f ? 1 : 0;
    }
}

/* For the settings window: the sticks and triggers of a player's controller now, and what the game would get. */
void PortInput_StickView(int player, PortStickView *out) {
    static const SDL_GamepadAxis axes[2][2] = {{SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY}, {SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY}};
    SDL_Gamepad *g;
    int s, i;

    pthread_once(&sLoadOnce, load);
    memset(out, 0, sizeof(*out));
    g = pad_of(player);
    out->connected = g != NULL;
    out->canVibrate = g != NULL && SDL_GetBooleanProperty(SDL_GetGamepadProperties(g), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    for (s = 0; s < 2; s++) {
        int v[2] = {0, 0};
        for (i = 0; i < 2 && g != NULL; i++) {
            v[i] = SDL_GetGamepadAxis(g, axes[s][i]);
            out->raw[s][i] = v[i] < -32767 ? -1.0f : (float)v[i] / 32767.0f;
        }
        stick_bytes(sDeadzone[player][s], v[0], v[1], &out->bytes[s][0], &out->bytes[s][1]);
        game_stick(out->bytes[s], out->game[s], &out->dir[s]);
        if (g != NULL) {
            int t = SDL_GetGamepadAxis(g, s == 0 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            out->trigger[s] = (float)t / 32767.0f;
            out->triggerHeld[s] = t > PORT_TRIGGER_AT;
        }
    }
}

/* Vibration.
 *
 * The game drives the DualShock 2's two motors itself: every frame Pad_Update (src/sys/pad.c) ends with
 * Pad_SendActuator for each port, which calls sceVibSetActParam with two bytes: bit 0 = the small motor (on / off),
 * bits 1..7 = the large motor's power (0..127). What it sends is what the fight asked for in that frame
 * (BtlChar_UpdateVibration -> Pad_AddVibration: hits taken and given, clashes, charging; nothing in a replay, for a
 * CPU side, or with the game's own Options > Controller > Vibration off for that player), and zero otherwise:
 * a motor stops by the game sending zero. That call comes here (plat_stub.c).
 *
 * The large motor is SDL's low-frequency one (its power, of 127, becomes SDL's, of 65535), the small motor the
 * high-frequency one (all or nothing), each times the player's strength. The game runs 30 steps a second and
 * sends a new command with every one for as long as an effect lasts: the large motor's power swings between 50%
 * and 100% of what was asked, six times a second, and the small motor is on in every other step. That is passed on
 * as it comes; a command that only repeats the last one is not sent again until half its time is over.
 * Nothing is sent while the settings window is open or the game's window is not the one being used, with the
 * player's setting off, or in a frame that is only being re-run (online play's rollback). Every command is given
 * RUMBLE_MS to live, so a controller is never left humming by a game that stopped coming here; and the
 * controllers are stopped as the program ends.
 *
 * A command is sent on the game's frame, and some controllers keep the caller waiting for it (generic pads through
 * DirectInput, some Bluetooth links): 30 of them a second of 50 ms each and the game stands still. Each call is
 * timed: a controller that takes RUMBLE_SLOW_NS or more is sent at most one command per ten times its longest call
 * (each living long enough to reach the next), and one that takes RUMBLE_OFF_NS or more twice (once could be the
 * system's own pause) is not sent any more until another controller is this player's, with a notice and a line in
 * the log.
 *
 * Nothing here is seen by the game: two bytes are read, nothing is written and the result is the constant it
 * always was. A fight, a recording of one and both sides of an online match are the same with it on or off. Online,
 * both ports' commands are computed on both sides: only this side's (Port_NetMe) are felt, on player 1's
 * controller, which is the one that plays there.
 */
#define RUMBLE_MS 250
#define RUMBLE_SLOW_NS 4000000ull /* 4 ms */
#define RUMBLE_OFF_NS 50000000ull /* 50 ms */
static struct {
    SDL_Gamepad *pad;
    Uint16 low, high;
    Uint64 at;
    int lastLarge, lastSmall;
    Uint64 cost; /* the controller's longest call, ns (0: never slow) */
    int stalls;  /* its calls of RUMBLE_OFF_NS or more */
    int off;     /* the controller held the game up: nothing more is sent to it */
} sRumble[2];

static void rumble_send(int player, SDL_Gamepad *g, Uint16 low, Uint16 high, int why) {
    Uint64 now = SDL_GetTicks(), gap, took;
    int ms = RUMBLE_MS;

    if (sRumble[player].pad != g && sRumble[player].pad != NULL && (sRumble[player].low | sRumble[player].high) != 0) {
        int k; /* another controller became this player's: the old one, if it is still there, is stopped */
        for (k = 0; k < MAX_PADS; k++) {
            if (sOpen[k].pad == sRumble[player].pad) {
                SDL_RumbleGamepad(sOpen[k].pad, 0, 0, 0);
            }
        }
        sRumble[player].low = sRumble[player].high = 0;
    }
    if (sRumble[player].pad != g) {
        sRumble[player].cost = 0;
        sRumble[player].stalls = 0;
        sRumble[player].off = 0;
    }
    sRumble[player].pad = g;
    if (sRumble[player].off) {
        return;
    }
    if (sRumble[player].cost >= RUMBLE_SLOW_NS) { /* a slow controller: one command per ten times its longest call */
        gap = sRumble[player].cost * 10 / 1000000;
        if (gap < RUMBLE_MS / 2) {
            gap = RUMBLE_MS / 2;
        }
        if (now - sRumble[player].at < gap) {
            return;
        }
        ms = (int)gap + RUMBLE_MS;
    }
    if (low == sRumble[player].low && high == sRumble[player].high && ((low | high) == 0 || now - sRumble[player].at < (Uint64)ms / 2)) {
        return; /* (the same again: only said again before it would run out) */
    }
    if (getenv("BT3_RUMBLE_LOG") != NULL) {
        extern unsigned gPortVBlanks;
        static const char *const whys[] = {"", " (the settings window is open)", " (the window is not in use)", " (vibration is off)"};
        fprintf(stderr, "rumble: vblank %u, player %d: small %d, large %d/127 -> %s(low %u, high %u, %d ms)%s\n", gPortVBlanks, player + 1,
                sRumble[player].lastSmall, sRumble[player].lastLarge, g != NULL ? "SDL_RumbleGamepad" : "no controller", low, high,
                (low | high) != 0 ? ms : 0, whys[why]);
    }
    if (g != NULL) {
        took = SDL_GetTicksNS();
        SDL_RumbleGamepad(g, low, high, (low | high) != 0 ? ms : 0);
        took = SDL_GetTicksNS() - took;
        if (took >= RUMBLE_OFF_NS && ++sRumble[player].stalls >= 2) {
            extern void Port_UiNotice(const char *text);
            sRumble[player].off = 1;
            fprintf(stderr, "rumble: player %d's controller (%s) took %llu ms for a vibration command: vibration is off for it\n",
                    player + 1, SDL_GetGamepadName(g) != NULL ? SDL_GetGamepadName(g) : "?", (unsigned long long)(took / 1000000));
            Port_UiNotice("Vibration turned off: the controller held the game up");
        } else if (took >= RUMBLE_SLOW_NS && took > sRumble[player].cost) {
            if (sRumble[player].cost == 0) {
                fprintf(stderr, "rumble: player %d's controller (%s) took %.1f ms for a vibration command: fewer are sent\n", player + 1,
                        SDL_GetGamepadName(g) != NULL ? SDL_GetGamepadName(g) : "?", (double)took / 1e6);
            }
            sRumble[player].cost = took;
        }
    }
    sRumble[player].low = low;
    sRumble[player].high = high;
    sRumble[player].at = now;
}

static void rumble_stop(void) {
    int k;
    for (k = 0; k < MAX_PADS; k++) {
        if (sOpen[k].pad != NULL) {
            SDL_RumbleGamepad(sOpen[k].pad, 0, 0, 0);
        }
    }
}

/* The game's motor command for a port: `small` 0 / 1, `large` 0..127. Once per port and frame. */
void Port_PadVibrate(int socket, int small, int large) {
    extern int Port_NetActive(void), Port_NetMe(void);
    extern int gPortResim;
    static int atExit;
    SDL_Window *w;
    int player = socket, why = 0;
    unsigned low, high;

    if (!sInit || gPortResim || socket < 0 || socket > 1) {
        return;
    }
    if (Port_NetActive()) {
        extern int Port_NetSpectating(void);
        if (socket != Port_NetMe() || Port_NetSpectating()) { /* (a spectator's controller plays nobody) */
            return;
        }
        player = 0;
    }
    if (!atExit) {
        atExit = 1;
        atexit(rumble_stop);
    }
    sRumble[player].lastLarge = large;
    sRumble[player].lastSmall = small;
    low = (unsigned)large * 65535u / 127u * (unsigned)sVib[player][1] / 100u;
    high = small ? 65535u * (unsigned)sVib[player][1] / 100u : 0;
    w = GsDraw_Window();
    if (gPortOverlayOpen || gPortInputCapture) {
        why = 1;
    } else if (w != NULL && !(SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) && getenv("BT3_RUMBLE_LOG") == NULL) {
        why = 2; /* (the test's window, which nobody sees, never is) */
    } else if (!sVib[player][0]) {
        why = 3;
    }
    if (why != 0) {
        low = high = 0;
    }
    rumble_send(player, pad_of(player), (Uint16)low, (Uint16)high, why);
}

/* 1 with BT3_RUMBLE_TEST set: the game's rule "no vibration in a replay" is lifted (src/battle/btl_char_get.c). */
int Port_RumbleTest(void) {
    static int on = -1;
    if (on < 0) {
        on = getenv("BT3_RUMBLE_TEST") != NULL;
    }
    return on;
}

/* The settings window's "Test": half a second of both motors at the player's strength (the one thing that is felt
   while that window is open). */
void PortInput_VibrationTest(int player) {
    SDL_Gamepad *g = pad_of(player);
    Uint16 s = (Uint16)(65535u * (unsigned)sVib[player][1] / 100u);

    if (getenv("BT3_RUMBLE_LOG") != NULL) {
        fprintf(stderr, "rumble: test, player %d -> %s(low %u, high %u, 500 ms)\n", player + 1, g != NULL ? "SDL_RumbleGamepad" : "no controller", s, s);
    }
    if (g != NULL) {
        SDL_RumbleGamepad(g, s, s, 500);
    }
}

/* The controllers' and the keyboard's state as it is at this moment (plat_stub.c, "Low input lag": the pads are
   read long after the window's events were last taken). The events themselves stay queued for the window. */
void PortInput_Fresh(void) {
    if (Gs_RenderThreaded() && !Gs_OnRenderThread()) {
        return; /* (the window is the render thread's: it takes the events, every millisecond it waits: PortInput_Pump) */
    }
    SDL_PumpEvents();
}

/* The render thread (BT3_GS_THREAD): the window's events, taken while it waits for work or for a picture's time.
   The controllers' and the keys' state the game's thread reads is then never more than a millisecond old. */
void PortInput_Pump(void) {
    SDL_PumpEvents();
}

/* BT3_LAG_LOG (plat_stub.c, Port_LagMark): how old a press is when the game first reads it. An event watch notes
   when each button or key press arrived (the event's own time: the system's or SDL's reading of the controller);
   the read that first has a new press logs "lag P <time> <frame> <microseconds since the press arrived>". */
static uint64_t sPressNs; /* the latest press's arrival, SDL_GetTicksNS's clock */
static bool SDLCALL press_watch(void *unused, SDL_Event *ev) {
    (void)unused;
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat)) {
        __atomic_store_n(&sPressNs, ev->common.timestamp, __ATOMIC_RELEASE);
    }
    return true;
}
static void press_age(int socket, unsigned btn) {
    static unsigned prev[2];
    static int on = -1;
    if (on < 0) {
        on = getenv("BT3_LAG_LOG") != NULL;
        if (on) {
            SDL_AddEventWatch(press_watch, NULL);
        }
    }
    if (on && (btn & ~prev[socket]) != 0) {
        /* (each press counted once: a direction from a stick is a new bit without a press of its own, and was given
           the age of the last button press, seconds old) */
        static uint64_t used;
        uint64_t at = __atomic_load_n(&sPressNs, __ATOMIC_ACQUIRE), now = SDL_GetTicksNS();
        extern void Port_LagNote(int what, unsigned long long value);
        if (at != 0 && at != used && now >= at && now - at < 1000000000ull) {
            used = at;
            Port_LagNote('P', (unsigned long long)((now - at) / 1000ull));
        }
    }
    prev[socket] = btn;
}

int Port_PadRead(int socket, unsigned char *data) {
    unsigned btn = 0;
    unsigned char rx = 0x80, ry = 0x80, lx = 0x80, ly = 0x80;
    SDL_Gamepad *g;
    int a;

    if (!GsGpu_Enabled() || socket < 0 || socket > 1) {
        return 0;
    }
    pthread_once(&sLoadOnce, load);
    pads_refresh();
    if (sSwallow) { /* the buttons that opened or closed the settings window are not the game's (START would pause) */
        int k, b, held = 0;
        for (k = 0; k < MAX_PADS && sSwallow == 2; k++) { /* (2: whichever button it was) */
            for (b = 0; sOpen[k].pad != NULL && b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
                if (SDL_GetGamepadButton(sOpen[k].pad, (SDL_GamepadButton)b)) {
                    held = 2;
                }
            }
        }
        for (k = 0; k < MAX_PADS; k++) {
            if (sOpen[k].pad != NULL &&
                (SDL_GetGamepadButton(sOpen[k].pad, SDL_GAMEPAD_BUTTON_START) || SDL_GetGamepadButton(sOpen[k].pad, SDL_GAMEPAD_BUTTON_BACK) ||
                 (sOverlayPad >= 0 && sOverlayPad < SDL_GAMEPAD_BUTTON_COUNT && SDL_GetGamepadButton(sOpen[k].pad, (SDL_GamepadButton)sOverlayPad)))) {
                held = held != 0 ? held : 1;
            }
        }
        sSwallow = held;
    }
    /* (the settings window is worked with the controller too while it is open: nothing of it for the game; nor while
       a controller is being set up) */
    g = gPortInputCapture || gPortOverlayOpen || sSwallow || sLearn.stage != 0 ? NULL : pad_of(socket);
    if (g != NULL) {
        for (a = 0; a < PORT_PAD_BUTTONS; a++) {
            int src = sPadBtn[socket][a];
            if (src == PORT_PAD_LT ? SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > PORT_TRIGGER_AT :
                src == PORT_PAD_RT ? SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > PORT_TRIGGER_AT :
                src >= 0 && SDL_GetGamepadButton(g, (SDL_GamepadButton)src)) {
                btn |= kActions[a].bit;
            }
        }
        stick_bytes(sDeadzone[socket][0], SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY), &lx, &ly);
        stick_bytes(sDeadzone[socket][1], SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY), &rx, &ry);
    }
    if (sKeySwallow) {
        const bool *ks = SDL_GetKeyboardState(NULL);
        int p, held = 0;
        for (p = 0; p < 2; p++) {
            for (a = 0; a < PORT_ACTIONS; a++) {
                held = held || (sKeys[p][a] > 0 && sKeys[p][a] < SDL_SCANCODE_COUNT && ks[sKeys[p][a]]);
            }
        }
        sKeySwallow = held;
    }
    /* the settings window takes the keyboard while it is open, and the online chat while a line is typed (the
       controller still plays then); and a program that turns the controller's buttons into keys has it while the
       controller is used (PortInput_Echo) */
    if (!gPortOverlayOpen && !gPortChatTyping && !sKeySwallow &&
        !(sEchoes >= 3 && SDL_GetTicksNS() - sPadUseNs < ECHO_HOLD_NS)) {
        const bool *ks = SDL_GetKeyboardState(NULL);
        const int *k = sKeys[socket];
        for (a = 0; a < PORT_PAD_BUTTONS; a++) {
            if (k[a] > 0 && k[a] < SDL_SCANCODE_COUNT && ks[k[a]] && !(gPortTboTyping && typing_key(k[a]))) {
                btn |= kActions[a].bit;
            }
        }
#define HELD(n) (k[n] > 0 && k[n] < SDL_SCANCODE_COUNT && ks[k[n]] && !(gPortTboTyping && typing_key(k[n])))
        if (HELD(16)) { ly = 0x00; }
        if (HELD(17)) { ly = 0xFF; }
        if (HELD(18)) { lx = 0x00; }
        if (HELD(19)) { lx = 0xFF; }
        if (HELD(20)) { ry = 0x00; }
        if (HELD(21)) { ry = 0xFF; }
        if (HELD(22)) { rx = 0x00; }
        if (HELD(23)) { rx = 0xFF; }
#undef HELD
    }
    if (socket == 0) {
        btn |= script_buttons();
    }
    if (socket == 0 && getenv("BT3_STICK_TEST") != NULL) { /* the numbers, once: what was read, what was passed, what the game has */
        static const char *const dirs[5] = {"-", "up", "down", "left", "right"};
        extern unsigned char gPad[]; /* the game's (include/sys/pad.h): 0x130 left x, y, 0x138 right x, y, 0x148 held */
        static int reads;
        if (++reads == 40 && g != NULL) {
            PortStickView v;
            int s;
            PortInput_StickView(0, &v);
            for (s = 0; s < 2; s++) {
                fprintf(stderr, "sticks: %s: raw %6d %6d (%+.3f %+.3f), deadzone %d -> bytes %3u %3u -> game %+.4f %+.4f, direction %s\n",
                        s == 0 ? "left " : "right", SDL_GetGamepadAxis(g, s == 0 ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX),
                        SDL_GetGamepadAxis(g, s == 0 ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY), v.raw[s][0], v.raw[s][1],
                        sDeadzone[0][s], v.bytes[s][0], v.bytes[s][1], v.game[s][0], v.game[s][1], dirs[v.dir[s]]);
            }
        } else if (reads == 44) {
            float f[4];
            unsigned held;
            memcpy(f, gPad + 0x130, sizeof(f));
            memcpy(&held, gPad + 0x148, sizeof(held));
            fprintf(stderr, "sticks: the game's pad 1 has left %+.4f %+.4f, right %+.4f %+.4f, held %08X\n", f[0], f[1], f[2], f[3], held);
        }
    }
    {
        extern int PortA11y_TakesSelect(void); /* a11y.c: with the screen reader on, Select in a fight is its own */
        if (PortA11y_TakesSelect()) {
            btn &= ~(unsigned)B_SELECT;
        }
    }
    clash_square(socket, &btn, &lx, &ly);
    press_age(socket, btn);
    memset(data, 0, 18);
    data[0] = (unsigned char)(~btn & 0xFF);       /* active low */
    data[1] = (unsigned char)(~(btn >> 8) & 0xFF);
    data[2] = rx; data[3] = ry; data[4] = lx; data[5] = ly;
    return 18;
}
