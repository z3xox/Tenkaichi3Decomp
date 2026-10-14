/*
 * Browser build: the settings are the page's (port/web/settings.js), not a window drawn over the game. This file is
 * the engine's side of ui.h for that build, in place of ui.cpp (Dear ImGui, which the browser build does not
 * compile): the canvas shows the game alone.
 *
 * The page reaches the engine through the exported Bt3Ui_* functions below. Reading is immediate. A setting or an
 * action is queued and applied by the game's own thread between two frames: the queue is announced with an SDL user
 * event, which GsDraw_PollEvents hands to Ui_Event like any other event. So a change never lands in the middle of a
 * frame, and an action that waits (reading a picture back from WebGPU) runs where the game may wait. Bindings and a
 * player's controller are plain values gs_input.c reads when it polls: they are written at once, as ui.cpp does.
 *
 * What each setting is, and where it is kept and saved, stays where the native build has it: PortVideo
 * (GsGpu_GetSettings / GsGpu_SetSettings), gs_input.c's bindings, gsInterp, the clash option, the unlock request.
 */
#include <SDL3/SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs/ui.h"
#include "sys/pad.h" /* the game's pad bits (PAD_CROSS, ...): what the game receives, by the game's own names */

extern int gsInterp;                     /* gs_draw.c: smooth motion 0..3; -1 before the first frame */
extern unsigned gGsFrame;                /* gs_core.c */
extern unsigned gPortLiveBlanks;         /* plat_stub.c */
extern unsigned gInterpShown;            /* gs_draw.c: in-between pictures shown (smooth motion) since the start */
extern int gGsMainFbp;                   /* gs_sony.c: the shown frame buffer, -1 before the first swap */
extern volatile int gPortUnlockAll, gPortUnlockDone; /* headless.c: the game's own "unlock everything" */
extern void *gSaveData; /* the game's save in memory, NULL until one is loaded (a game pointer: 32 bits, as wasm's) */
extern unsigned GsWgpu_DeviceErrors(void); /* gs_wgpu.c */
extern const char *GsWgpu_Error(unsigned i);
extern unsigned GsWgpu_GpuKBytes(void);
extern int GsWgpu_GpuTiming(void), GsGl_GpuTiming(void); /* gs_wgpu.c, gs_gl.c: 1 = frames are timed on the GPU */
extern unsigned GsWgpu_GpuTimingSkipped(void), GsGl_GpuTimingSkipped(void);
int GsGpu_ReadFrame(uint32_t fbp, uint8_t *rgba);
int Port_ClashSquare(void);
void Port_ClashSquareSet(int on);
int Port_ClashSquareX(void);
void Port_ClashSquareXSet(int quarters);
void Port_SettingSave(const char *name, int value);
void Port_SettingsWrite(void);
int Port_Setting(const char *name, int def);
int Port_GameSpeed(void);               /* plat_stub.c: the player's own game speed, percent (never used online) */
void Port_GameSpeedSet(int percent);
void Port_AudioRefresh(void);           /* snd_adx.c: playing streams follow the volumes and the speed at once */
extern int gPortMusicFollows;           /* snd_adx.c: 1 = looping music follows the game speed; -1 before it is read */

/* The picture GsGpu_ReadFrame gives: the GS's own resolution. */
#define FRAME_W 512
#define FRAME_H 448
/* A setting value the page cannot have asked for: the queue's entries for actions carry their argument instead. */
#define UI_QUEUE_SIZE 64

/* ---- the page's side (Module.bt3Ui, given to createBt3 by port/web/index.html) ------------------------------- */

/* A fault in the page's panel must not end the game: it is reported (the log, and a notice on the page) and the
   engine goes on. */
EM_JS(void, page_call, (const char *what, int arg), {
    const name = UTF8ToString(what);
    if (!Module.bt3Ui || typeof Module.bt3Ui[name] !== 'function') {
        err(`bt3: the page has no settings handler "${name}"`);
        return;
    }
    try {
        Module.bt3Ui[name](arg);
    } catch (e) {
        err(`bt3: settings handler "${name}" of the page failed: ${e && e.stack || e}`);
        try { Module.bt3Ui.notice(`The settings panel failed (${name}): see the log.`); } catch (e2) { /* (reported above) */ }
    }
});

EM_JS(void, page_notice, (const char *text), {
    if (!Module.bt3Ui) { err('bt3: notice (no page handler): ' + UTF8ToString(text)); return; }
    Module.bt3Ui.notice(UTF8ToString(text));
});

EM_JS(void, page_screenshot, (const uint8_t *rgba, int w, int h), {
    if (!Module.bt3Ui) { err('bt3: screenshot taken, but the page has no handler'); return; }
    Module.bt3Ui.screenshot(HEAPU8.slice(rgba, rgba + w * h * 4), w, h);
});

/* ---- settings ------------------------------------------------------------------------------------------------- */

/* The PortVideo fields, each read and written through the native build's own pair (which also saves it). */
#define VIDEO_FIELD(field)                                                                                          \
    static int get_##field(void) { PortVideo v; GsGpu_GetSettings(&v); return v.field; }                          \
    static void set_##field(int x) { PortVideo v; GsGpu_GetSettings(&v); v.field = x; GsGpu_SetSettings(&v); }
VIDEO_FIELD(scale)
VIDEO_FIELD(aspectMilli)
VIDEO_FIELD(smooth2d)
VIDEO_FIELD(fxOff)
VIDEO_FIELD(glow)
VIDEO_FIELD(music)
VIDEO_FIELD(effects)
VIDEO_FIELD(voices)
VIDEO_FIELD(stage)
VIDEO_FIELD(hudLayout)
VIDEO_FIELD(frameSkip)
VIDEO_FIELD(texPack)
static int get_texPackCount(void) { PortVideo v; GsGpu_GetSettings(&v); return v.texPackCount; }
/* Content added from outside the disc: what was found, and whether it has been looked for yet (web_extras.c) */
extern int PortStages_Count(void), PortSongs_Count(void), WebExtras_Checked(void);
/* Whether the data server lists its folders; -1 while not asked yet (it is asked by the game's first look, at start:
   asking from here, outside the game's own run, could not wait for the answer) */
static int sListable = -1;
static int get_data_listable(void) { return sListable; }
void WebUi_NoteListable(int can) { sListable = can; }
/* Whether the game data was checked against the original disc's (web_verify.c) */
extern int WebVerify_State(void);

/* Smooth motion (in-between pictures, gs_draw.c) in the browser, measured with port/tests/bench/present_rate.js on the
   demo battle (2026-10-12, RTX 5070 Ti, 200 Hz display; BROWSER_PORT_STATUS.md has the tables). Each in-between picture
   replays the whole frame. WebGPU at "60" (one per tick, ~5 ms each): 59 pictures a second reach the screen, evenly
   (longest gap 34 ms, as without), game speed 100 %. Every other case shows more pictures only by leaving real ticks
   out to stay in time (frame skipping), which makes longer gaps than without (47 to 53 ms against 36): WebGPU at 120 /
   240, and WebGL 2 at any setting (~11 ms per in-between picture; without frame skipping the game slows to 90 %).
   So: WebGPU up to "60", WebGL 2 none. */
int Web_InterpLimit(const char *renderer) {
    return strcmp(renderer, "webgpu") == 0 ? 1 : 0;
}

static int get_interp(void) { return gsInterp < 0 ? 0 : gsInterp; }
static int get_interp_limit(void) { return GsDraw_InterpLimit(); }
static void set_interp(int x) {
    if (gsInterp < 0) { /* (its buffers are made with the first frame: gs_draw.c) */
        fprintf(stderr, "bt3: smooth motion can be set once the first frame is drawn\n");
        return;
    }
    if (x > GsDraw_InterpLimit()) {
        fprintf(stderr, "bt3: smooth motion %d is above what this renderer shows here (%d)\n", x, GsDraw_InterpLimit());
        return;
    }
    gsInterp = x < 0 ? 0 : x;
    Port_SettingSave("interp", gsInterp);
    Port_SettingsWrite();
}
/* (as ui.cpp's Audio tab and Cheats tab change them) */
static int get_music_follows(void) { return gPortMusicFollows < 0 ? Port_Setting("music_follows_speed", 0) != 0 : gPortMusicFollows; }
static void set_music_follows(int x) {
    gPortMusicFollows = x != 0;
    Port_SettingSave("music_follows_speed", gPortMusicFollows);
    Port_SettingsWrite();
}
/* The start of the game and the menus' shape (upstream 0.1.32; ui.cpp's Misc and UI tabs set them the same way).
   skip_logos / skip_intro are read once at the start (FirstRun_Main, src/menu/menu_i_c.c): from the next start, which
   in the browser is the next load of the page (the settings file is kept by the page). skip_copyright is read by
   the title screen each time it runs (Title_Run, src/menu/menu_a_c.c). */
extern int Port_Menus43(void);
extern void Port_Menus43Set(int on);
static int get_skip_logos(void) { return Port_Setting("skip_logos", 0) != 0; }
static void set_skip_logos(int x) { Port_SettingSave("skip_logos", x != 0); Port_SettingsWrite(); }
static int get_skip_intro(void) { return Port_Setting("skip_intro", 0) != 0; }
static void set_skip_intro(int x) { Port_SettingSave("skip_intro", x != 0); Port_SettingsWrite(); }
static int get_skip_copyright(void) { return Port_Setting("skip_copyright", 0) != 0; }
static void set_skip_copyright(int x) { Port_SettingSave("skip_copyright", x != 0); Port_SettingsWrite(); }
static int get_menus43(void) { return Port_Menus43(); }
static void set_menus43(int x) { Port_Menus43Set(x != 0); Port_SettingSave("menus_43", x != 0); Port_SettingsWrite(); }
static void set_game_speed(int x) {
    Port_GameSpeedSet(x);
    Port_AudioRefresh();
    Port_SettingsWrite();
}
static void set_clash_square(int x) { Port_ClashSquareSet(x); }
static void set_clash_square_x(int x) { Port_ClashSquareXSet(x); Port_SettingsWrite(); }
static int get_unlock_done(void) { return gPortUnlockDone; }
/* (the unlock is carried out only with a save loaded: headless.c drops the request otherwise) */
static int get_save_loaded(void) { return gSaveData != NULL; }
static int get_frame(void) { return (int)gGsFrame; }
static int get_vblanks(void) { return (int)gPortLiveBlanks; }
static int get_interp_shown(void) { return (int)gInterpShown; }
static int get_webgpu_errors(void) { return (int)GsWgpu_DeviceErrors(); }
/* (only the renderer that runs ever turns its timing on) */
static int get_gpu_timing(void) { return GsWgpu_GpuTiming() || GsGl_GpuTiming(); }
static int get_gpu_timing_skipped(void) { return (int)(GsWgpu_GpuTimingSkipped() + GsGl_GpuTimingSkipped()); }
static int get_webgpu_kbytes(void) { return (int)GsWgpu_GpuKBytes(); }
static int get_actions(void) { return PORT_ACTIONS; }
static int get_pad_buttons(void) { return PORT_PAD_BUTTONS; }

/* Names as the settings file has them (plat_settings.c) where there is one. */
static const struct {
    const char *name;
    int (*get)(void);
    void (*set)(int); /* NULL: read only */
} kSettings[] = {
    {"scale", get_scale, set_scale},
    {"aspect_milli", get_aspectMilli, set_aspectMilli},
    {"smooth_2d", get_smooth2d, set_smooth2d},
    {"fx_off", get_fxOff, set_fxOff},
    {"glow", get_glow, set_glow},
    {"music", get_music, set_music},
    {"effects", get_effects, set_effects},
    {"voices", get_voices, set_voices},
    {"stage_sounds", get_stage, set_stage},
    {"music_follows_speed", get_music_follows, set_music_follows},
    {"hud_layout", get_hudLayout, set_hudLayout},
    {"frame_skip", get_frameSkip, set_frameSkip},
    {"menus_43", get_menus43, set_menus43},
    {"skip_logos", get_skip_logos, set_skip_logos},
    {"skip_intro", get_skip_intro, set_skip_intro},
    {"skip_copyright", get_skip_copyright, set_skip_copyright},
    {"game_speed", Port_GameSpeed, set_game_speed},
    {"texture_pack", get_texPack, set_texPack},
    {"texture_pack_count", get_texPackCount, NULL},
    {"added_checked", WebExtras_Checked, NULL},
    {"added_stage_count", PortStages_Count, NULL},
    {"added_song_count", PortSongs_Count, NULL},
    {"data_listable", get_data_listable, NULL},
    {"data_check", WebVerify_State, NULL},
    {"interp", get_interp, set_interp},
    {"interp_limit", get_interp_limit, NULL},
    {"clash_square", Port_ClashSquare, set_clash_square},
    {"clash_square_x", Port_ClashSquareX, set_clash_square_x},
    {"unlock_done", get_unlock_done, NULL},
    {"save_loaded", get_save_loaded, NULL},
    {"frame", get_frame, NULL},
    {"vblanks", get_vblanks, NULL},
    {"interp_shown", get_interp_shown, NULL},
    {"webgpu_errors", get_webgpu_errors, NULL},
    {"gpu_timing", get_gpu_timing, NULL},
    {"gpu_timing_skipped", get_gpu_timing_skipped, NULL},
    {"webgpu_kbytes", get_webgpu_kbytes, NULL},
    {"actions", get_actions, NULL},
    {"pad_buttons", get_pad_buttons, NULL},
};
#define SETTING_COUNT ((int)(sizeof(kSettings) / sizeof(kSettings[0])))

static int setting_index(const char *name) {
    int i;
    for (i = 0; i < SETTING_COUNT; i++) {
        if (strcmp(kSettings[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ---- the queue of changes ------------------------------------------------------------------------------------- */

typedef enum { REQ_SETTING, REQ_UNLOCK_ALL, REQ_RESET_CONTROLS, REQ_RESET_KEYS, REQ_RESET_PADS, REQ_SCREENSHOT } RequestKind;
static struct {
    RequestKind kind;
    int index, value;
} sQueue[UI_QUEUE_SIZE];
static int sQueued;
static Uint32 sEventType; /* the SDL user event that says the queue has something */

static int queue_push(RequestKind kind, int index, int value) {
    SDL_Event ev;
    if (sEventType == 0 || sQueued == UI_QUEUE_SIZE) {
        fprintf(stderr, "bt3: settings change refused (%s)\n", sEventType == 0 ? "the game has not started" : "too many at once");
        return 0;
    }
    sQueue[sQueued].kind = kind;
    sQueue[sQueued].index = index;
    sQueue[sQueued].value = value;
    if (sQueued++ == 0) {
        SDL_zero(ev);
        ev.type = sEventType;
        SDL_PushEvent(&ev);
    }
    return 1;
}

static void screenshot(void) {
    uint8_t *rgba = malloc(FRAME_W * FRAME_H * 4);
    if (gGsMainFbp >= 0 && GsGpu_ReadFrame((uint32_t)gGsMainFbp, rgba)) {
        page_screenshot(rgba, FRAME_W, FRAME_H);
    } else {
        page_notice("No picture to take yet.");
    }
    free(rgba);
}

/* One player's keys only, or controller buttons only, back to the engine's defaults (gs_input.c): the defaults are
   taken whole and the other half put back. */
static void reset_half(int keys, int player) {
    int keep[PORT_ACTIONS], k, slot = *PortInput_PadSlot(player);
    int *other = keys ? PortInput_PadButtons(player) : PortInput_Keys(player), n = keys ? PORT_PAD_BUTTONS : PORT_ACTIONS;
    memcpy(keep, other, sizeof(int) * (size_t)n);
    PortInput_ResetDefaults(player);
    other = keys ? PortInput_PadButtons(player) : PortInput_Keys(player);
    for (k = 0; k < n; k++) {
        other[k] = keep[k];
    }
    *PortInput_PadSlot(player) = slot; /* (the controller is a choice of its own, not a binding) */
    PortInput_Save();
}

static void queue_apply(void) {
    int i;
    for (i = 0; i < sQueued; i++) {
        switch (sQueue[i].kind) {
        case REQ_SETTING: kSettings[sQueue[i].index].set(sQueue[i].value); break;
        case REQ_UNLOCK_ALL: gPortUnlockAll = 1; break; /* (carried out by the game at its next vertical blank) */
        case REQ_RESET_CONTROLS: PortInput_ResetDefaults(sQueue[i].value); PortInput_Save(); break;
        case REQ_RESET_KEYS:
        case REQ_RESET_PADS: reset_half(sQueue[i].kind == REQ_RESET_KEYS, sQueue[i].value); break;
        case REQ_SCREENSHOT: screenshot(); break;
        }
    }
    sQueued = 0;
    page_call("changed", 0);
}

/* ---- bindings: the next key or controller button is taken by the engine itself (SDL's names and codes) -------- */

enum { CAPTURE_NONE, CAPTURE_KEY, CAPTURE_PAD };
static int sCapKind, sCapPlayer, sCapAction;

/* the row PORT_ACTIONS is the key / button that opens the settings (one for the program, not a player's) */
static int *binding(int kind, int player, int action) {
    if (action == PORT_ACTIONS) {
        return kind == CAPTURE_KEY ? PortInput_OverlayKey() : PortInput_OverlayPad();
    }
    return kind == CAPTURE_KEY ? &PortInput_Keys(player)[action] : &PortInput_PadButtons(player)[action];
}

static void capture_end(void) {
    sCapKind = CAPTURE_NONE;
    gPortInputCapture = 0;
    page_call("captureDone", 0);
}

static void bind(int value) {
    if (sCapAction == PORT_ACTIONS && sCapKind == CAPTURE_KEY && value <= 0) {
        value = SDL_SCANCODE_F1; /* (cleared: F1 again, so that a keyboard always has a way in) */
    }
    if (sCapAction == PORT_ACTIONS && sCapKind == CAPTURE_PAD && value >= PORT_PAD_LT) {
        return; /* (a trigger cannot open the settings: a button only) */
    }
    *binding(sCapKind, sCapPlayer, sCapAction) = value;
    PortInput_Save();
}

/* Esc keeps the binding, Delete or Backspace clears it. */
static int capture_event(const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        SDL_Scancode sc = ev->key.scancode;
        if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
            bind(sCapKind == CAPTURE_KEY ? 0 : -1);
            capture_end();
        } else if (sc == SDL_SCANCODE_ESCAPE) {
            capture_end();
        } else if (sCapKind == CAPTURE_KEY) {
            bind((int)sc);
            capture_end();
        }
        return 1;
    }
    if (ev->type == SDL_EVENT_KEY_UP) {
        return 1;
    }
    if (sCapKind == CAPTURE_PAD && ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        bind((int)ev->gbutton.button);
        capture_end();
        return 1;
    }
    if (sCapKind == CAPTURE_PAD && ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && ev->gaxis.value > SDL_JOYSTICK_AXIS_MAX / 2 &&
        (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        bind(ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PORT_PAD_LT : PORT_PAD_RT);
        capture_end();
        return 1;
    }
    return 0;
}

/* ---- ui.h --------------------------------------------------------------------------------------------------- */

int Ui_Init(SDL_Window *window, SDL_GPUDevice *device, void *gl_context) {
    (void)window; (void)device; (void)gl_context;
    sEventType = SDL_RegisterEvents(1);
    if (sEventType == 0) {
        fprintf(stderr, "bt3: no SDL event for the page's settings: %s\n", SDL_GetError());
        return 0;
    }
    page_call("ready", 0);
    return 1;
}

int Ui_Event(const SDL_Event *ev) {
    if (sEventType != 0 && ev->type == sEventType) {
        queue_apply();
        return 1;
    }
    return sCapKind != CAPTURE_NONE && capture_event(ev);
}

/* The engine's settings key (F1, START + SELECT, or the bound one): the page opens or closes its panel. */
void Ui_Toggle(void) {
    page_call("toggle", 0);
}

/* The OpenGL back end calls this after its present: the browser build draws nothing over the game. */
void Ui_DrawGL(void) {
}

/* Escape outside the settings (gs_draw.c asks before the program closes): a browser tab is closed by the browser, not
   by the game. The page keeps Escape from the game (settings.js); should one arrive, it says so instead of ending the
   page's game. 1 = answered here (nothing closes). */
int Ui_QuitAsk(void) {
    page_notice("The game cannot be quit from inside a browser: close the tab to leave it.");
    return 1;
}

/* gs_draw.c applies a change of V-sync to both desktop renderers (GsGl_Vsync, and this, the Vulkan one's). The browser
   build has no Vulkan renderer and the page offers no V-sync: a browser shows a page's pictures at the display's
   refresh whatever the setting. */
void GsGpu_Vsync(int on) {
    fprintf(stderr, "bt3: V-sync %d not applied: a browser shows pictures at the display's refresh\n", on);
}

void Port_UiNotice(const char *text) {
    page_notice(text != NULL ? text : "");
}

/* ---- performance samples (the page's Developer section) ---------------------------------------------------------
   One sample per game tick, as gs_core.c measures it (the same numbers BT3_FRAME_LOG prints), and one per GPU time a
   renderer could measure (it arrives frames later, so it has its own ring). Fixed size: the page reads what is new
   straight from memory (Bt3Ui_PerfRing; the layout below is the page's contract, settings.js PerfReader). */
#define PERF_RING 1024 /* samples: about 34 s of ticks at 30 a second */
typedef struct PerfSample {
    uint32_t frame, vblanks;
    float timeMs;   /* when the tick ended (the page's performance.now() clock) */
    float workMs;   /* the tick's CPU time, the wait for the vertical blank left out */
    float gameMs;   /* of that: the game's own code (the simulation) */
    float listMs;   /* of that: drawing (the display list walked, the renderer's frame) */
    float submitMs; /* of the list: the renderer's replay and submission (GsGpu_FrameEnd) */
} PerfSample;
typedef struct GpuSample {
    uint32_t frame;
    float gpuMs;    /* the GPU's own time for the frame (timer / timestamp queries) */
} GpuSample;
static struct {
    uint32_t capacity, written, gpuWritten, sampleBytes; /* (written counts up for ever: index = written % capacity) */
    PerfSample sample[PERF_RING];
    GpuSample gpu[PERF_RING];
} sPerf = {PERF_RING, 0, 0, sizeof(PerfSample)};

void Web_FrameSample(unsigned frame, unsigned vblanks, uint64_t workNs, uint64_t gameNs, uint64_t listNs, uint64_t submitNs) {
    PerfSample *s = &sPerf.sample[sPerf.written % PERF_RING];
    s->frame = frame;
    s->vblanks = vblanks;
    s->timeMs = (float)emscripten_get_now();
    s->workMs = (float)workNs / 1e6f;
    s->gameMs = (float)gameNs / 1e6f;
    s->listMs = (float)listNs / 1e6f;
    s->submitMs = (float)submitNs / 1e6f;
    sPerf.written++;
}

void Web_GpuSample(unsigned frame, double ms) {
    GpuSample *g = &sPerf.gpu[sPerf.gpuWritten % PERF_RING];
    g->frame = frame;
    g->gpuMs = (float)ms;
    sPerf.gpuWritten++;
}

EMSCRIPTEN_KEEPALIVE const void *Bt3Ui_PerfRing(void) {
    return &sPerf;
}

/* WebGPU device error i, 0 the latest ("" past what is kept). */
EMSCRIPTEN_KEEPALIVE const char *Bt3Ui_WebGpuError(int i) {
    return GsWgpu_Error(i < 0 ? 0u : (unsigned)i);
}

/* ---- what the game receives from the pads ---------------------------------------------------------------------
   The browser build links gs_input.c's Port_PadRead wrapped (wasm_build.py: --wrap=Port_PadRead): every read is
   recorded as the game got it (the Controllers tab and the page's tests show it). In the Controllers tab's test, with
   the panel open, the engine's own read runs as if the panel were closed, so the bindings are tried exactly as the
   game would see them, and the game itself is handed a released pad (nothing the player tries reaches it). */
#define PAD_REPLY_BYTES 18     /* the libpad reply Port_PadRead fills: two active-low button bytes, then rx ry lx ly */
#define PAD_AXIS_CENTRE 0x80
#define PAD_BUTTON_BYTES 0xFFu /* (active low: all released) */
typedef struct PadSeen {
    uint32_t buttons;            /* PAD_* bits held */
    uint8_t rx, ry, lx, ly;
} PadSeen;
static PadSeen sGameSaw[PAD_COUNT], sTried[PAD_COUNT];
static int sInputTest;

int __real_Port_PadRead(int socket, unsigned char *data);

static PadSeen pad_seen(const unsigned char *data) {
    PadSeen s;
    s.buttons = (uint32_t)(~data[0] & 0xFFu) | (uint32_t)(~data[1] & 0xFFu) << 8;
    s.rx = data[2];
    s.ry = data[3];
    s.lx = data[4];
    s.ly = data[5];
    return s;
}

/* ---- the page's touch controls (port/web/touch.js) --------------------------------------------------------------
   They hold engine actions as held keys do: gs_input.c's Port_PadRead asks PortInput_VirtualActions where it reads the
   keyboard (and not while the settings window is open), and turns them into the game's pad bits through the same
   action table, before "Square counts as a direction". The page sets them on every change (Bt3Ui_TouchSet); they play
   as player 1. */
#define TOUCH_SOCKET 0           /* the player the touch controls play as (player 1) */
#define TOUCH_STICK_NONE (-1)    /* no on-screen stick held */
#define TOUCH_STICK_MAX 0xFF     /* a stick axis: 0 (up / left) .. 0x80 (centre) .. 0xFF (down / right) */
static uint32_t sTouchActions;
static int sTouchLx = TOUCH_STICK_NONE, sTouchLy = TOUCH_STICK_NONE;

uint32_t PortInput_VirtualActions(int socket, int *lx, int *ly) {
    if (socket != TOUCH_SOCKET) {
        return 0;
    }
    *lx = sTouchLx;
    *ly = sTouchLy;
    return sTouchActions;
}

/* actions: bits in the engine's action order (Bt3Ui_ActionLabel); lx, ly: the on-screen stick (0..255), or -1 */
EMSCRIPTEN_KEEPALIVE void Bt3Ui_TouchSet(uint32_t actions, int lx, int ly) {
    sTouchActions = actions & ((1u << PORT_ACTIONS) - 1u);
    sTouchLx = lx < 0 ? TOUCH_STICK_NONE : lx > TOUCH_STICK_MAX ? TOUCH_STICK_MAX : lx;
    sTouchLy = ly < 0 ? TOUCH_STICK_NONE : ly > TOUCH_STICK_MAX ? TOUCH_STICK_MAX : ly;
}

int __wrap_Port_PadRead(int socket, unsigned char *data) {
    int n;
    if (sInputTest && gPortOverlayOpen && socket >= 0 && socket < PAD_COUNT) {
        gPortOverlayOpen = 0;
        n = __real_Port_PadRead(socket, data);
        gPortOverlayOpen = 1;
        if (n) {
            sTried[socket] = pad_seen(data);
            memset(data, 0, PAD_REPLY_BYTES);
            data[0] = data[1] = PAD_BUTTON_BYTES;
            data[2] = data[3] = data[4] = data[5] = PAD_AXIS_CENTRE;
        }
    } else {
        n = __real_Port_PadRead(socket, data);
    }
    if (n && socket >= 0 && socket < PAD_COUNT) {
        sGameSaw[socket] = pad_seen(data);
    }
    return n;
}

/* ---- the memory card's files reach the page ------------------------------------------------------------------
   Game saves are files on an emulated memory card (plat_mc.c: saves/card1/<save folder>/...), which in the browser lie
   in the engine's in-memory file system. The browser build links the card calls wrapped (wasm_build.py --wrap) to see
   when they are complete: a file is open from sceMcSync's "open" result until its close; when a file that was written
   to is closed and no card file is open any more, every file on the card is whole, and the page is told (cardWritten):
   it copies the card at once and keeps it (port/web/saves.js). plat_mc.c itself is unchanged. */
#define MC_CMD_OPEN 2      /* libmc request numbers (plat_mc.c) */
#define MC_FILES_SEEN 32   /* (plat_mc.c has 8 file handles; a bit each) */
int __real_func_002A1FB8(int fd);                               /* sceMcClose */
int __real_func_002A2320(int fd, void *buf, int size);          /* sceMcWrite */
int __real_func_002A2498(int mode, int *cmd, int *result);      /* sceMcSync */
static uint32_t sCardOpen;  /* card files open (bit = handle) */
static int sCardWritten;    /* a file was written since the page was last told */

int __wrap_func_002A2498(int mode, int *cmd, int *result) {
    int c = 0, r = 0, done = __real_func_002A2498(mode, &c, &r);
    if (done == 1 && c == MC_CMD_OPEN && r >= 0 && r < MC_FILES_SEEN) {
        sCardOpen |= 1u << r;
    }
    if (cmd != NULL) { *cmd = c; }
    if (result != NULL) { *result = r; }
    return done;
}

int __wrap_func_002A2320(int fd, void *buf, int size) {
    sCardWritten = 1;
    return __real_func_002A2320(fd, buf, size);
}

int __wrap_func_002A1FB8(int fd) {
    int r = __real_func_002A1FB8(fd);
    if (fd >= 0 && fd < MC_FILES_SEEN) {
        sCardOpen &= ~(1u << fd);
    }
    if (sCardWritten && sCardOpen == 0) {
        sCardWritten = 0;
        page_call("cardWritten", 0);
    }
    return r;
}

/* The game's own check of a save block (McCard_Checksum, src/battle/stgm_a_b.c: the sum in the block's first two
   words): 1 = the `size` bytes at `buf` pass it. The page checks saves with it before keeping or loading them. */
extern int McCard_Checksum(uint32_t *buf, int size, int write);
EMSCRIPTEN_KEEPALIVE int Bt3Ui_SaveChecksumOk(void *buf, int size) {
    return McCard_Checksum((uint32_t *)buf, size, 0) == 0;
}

/* ---- the page's interface ------------------------------------------------------------------------------------ */

/* A setting's value; -1 and a message for an unknown name. */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_Get(const char *name) {
    int i = setting_index(name);
    if (i < 0) {
        fprintf(stderr, "bt3: unknown setting %s\n", name);
        return -1;
    }
    return kSettings[i].get();
}

/* 1 = queued (applied between the next two frames); 0 = unknown, read only, or refused (with a message). */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_Set(const char *name, int value) {
    int i = setting_index(name);
    if (i < 0 || kSettings[i].set == NULL) {
        fprintf(stderr, "bt3: setting %s %s\n", name, i < 0 ? "is unknown" : "cannot be changed");
        return 0;
    }
    return queue_push(REQ_SETTING, i, value);
}

/* "unlock_all", "reset_controls" / "reset_keys" / "reset_pads" (arg: player), "screenshot". */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_Action(const char *name, int arg) {
    if (strcmp(name, "unlock_all") == 0) { return queue_push(REQ_UNLOCK_ALL, 0, 0); }
    if (strcmp(name, "reset_controls") == 0) { return queue_push(REQ_RESET_CONTROLS, 0, arg & 1); }
    if (strcmp(name, "reset_keys") == 0) { return queue_push(REQ_RESET_KEYS, 0, arg & 1); }
    if (strcmp(name, "reset_pads") == 0) { return queue_push(REQ_RESET_PADS, 0, arg & 1); }
    if (strcmp(name, "screenshot") == 0) { return queue_push(REQ_SCREENSHOT, 0, 0); }
    fprintf(stderr, "bt3: unknown settings action %s\n", name);
    return 0;
}

/* The panel is open: neither the keyboard nor a controller reaches the game (gs_input.c), as with the native window. */
EMSCRIPTEN_KEEPALIVE void Bt3Ui_SetOpen(int open) {
    if (!open && sCapKind != CAPTURE_NONE) {
        capture_end();
    }
    if (!open && gPortOverlayOpen) {
        sInputTest = 0;
        PortInput_Swallow(); /* (controller buttons still held from the panel are not the game's until let go) */
    }
    gPortOverlayOpen = open != 0;
}

/* The Controllers tab's test: 1 = the bindings are tried while the panel is open (the game gets a released pad). */
EMSCRIPTEN_KEEPALIVE void Bt3Ui_InputTest(int on) {
    sInputTest = on != 0;
    if (!on) {
        memset(sTried, 0, sizeof(sTried));
    }
}

/* PAD_* bits held: which 0 = what the game last received, 1 = what the test last tried. */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_PadButtons(int which, int player) {
    return (int)(which ? sTried : sGameSaw)[player & 1].buttons;
}

/* The sticks as the game gets them: lx << 24 | ly << 16 | rx << 8 | ry (0x80 = centre). */
EMSCRIPTEN_KEEPALIVE uint32_t Bt3Ui_PadSticks(int which, int player) {
    const PadSeen *s = &(which ? sTried : sGameSaw)[player & 1];
    return (uint32_t)s->lx << 24 | (uint32_t)s->ly << 16 | (uint32_t)s->rx << 8 | s->ry;
}

/* The name of the game's pad bit i (0..15), as include/sys/pad.h has them. */
EMSCRIPTEN_KEEPALIVE const char *Bt3Ui_PadBitName(int i) {
    static const struct { uint32_t bit; const char *name; } kBits[] = {
        {PAD_SELECT, "SELECT"}, {PAD_L3, "L3"}, {PAD_R3, "R3"}, {PAD_START, "START"}, {PAD_UP, "Up"}, {PAD_RIGHT, "Right"},
        {PAD_DOWN, "Down"}, {PAD_LEFT, "Left"}, {PAD_L2, "L2"}, {PAD_R2, "R2"}, {PAD_L1, "L1"}, {PAD_R1, "R1"},
        {PAD_TRIANGLE, "Triangle"}, {PAD_CIRCLE, "Circle"}, {PAD_CROSS, "Cross"}, {PAD_SQUARE, "Square"},
    };
    int k;
    for (k = 0; k < (int)(sizeof(kBits) / sizeof(kBits[0])); k++) {
        if (kBits[k].bit == 1u << i) {
            return kBits[k].name;
        }
    }
    return "";
}

/* Sets a binding directly (the page's presets, and swapping or clearing a duplicate): the same values capture gives. */
EMSCRIPTEN_KEEPALIVE void Bt3Ui_BindingSet(int kind, int player, int action, int value) {
    if (action < 0 || action > PORT_ACTIONS || (kind == CAPTURE_PAD && action >= PORT_PAD_BUTTONS && action != PORT_ACTIONS)) {
        fprintf(stderr, "bt3: no %s binding for action %d\n", kind == CAPTURE_PAD ? "controller" : "key", action);
        return;
    }
    *binding(kind, player & 1, action) = value;
    PortInput_Save();
}

/* SDL's scancode for a key name ("W", "Return", "Left Shift"; 0 = no such key): the presets name their keys. */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_ScancodeFromName(const char *name) {
    return (int)SDL_GetScancodeFromName(name);
}

/* USB vendor << 16 | product of connected controller i as SDL knows it (0 = not known), to match the browser's list. */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_GamepadVendorProduct(int i) {
    int count = 0, vp = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids != NULL && i >= 0 && i < count) {
        vp = (int)((uint32_t)SDL_GetGamepadVendorForID(ids[i]) << 16 | SDL_GetGamepadProductForID(ids[i]));
    }
    SDL_free(ids);
    return vp;
}

EMSCRIPTEN_KEEPALIVE const char *Bt3Ui_ActionLabel(int action) {
    return action == PORT_ACTIONS ? "Open the settings" : PortInput_ActionLabel(action);
}

/* kind 1: the key (SDL scancode, 0 none); kind 2: the controller source (SDL_GamepadButton, PORT_PAD_LT / _RT, -1 none). */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_Binding(int kind, int player, int action) {
    return *binding(kind, player & 1, action);
}

EMSCRIPTEN_KEEPALIVE const char *Bt3Ui_KeyName(int scancode) {
    return scancode > 0 ? SDL_GetScancodeName((SDL_Scancode)scancode) : "";
}

/* Takes the next key (kind 1) or controller button (kind 2) as the binding; the page puts the focus on the canvas,
   where SDL listens. Ui_Event takes it and the page hears captureDone. */
EMSCRIPTEN_KEEPALIVE void Bt3Ui_CaptureBegin(int kind, int player, int action) {
    sCapKind = kind == CAPTURE_PAD ? CAPTURE_PAD : CAPTURE_KEY;
    sCapPlayer = player & 1;
    sCapAction = action < 0 ? 0 : action > PORT_ACTIONS ? PORT_ACTIONS : action;
    gPortInputCapture = 1;
}

EMSCRIPTEN_KEEPALIVE void Bt3Ui_CaptureCancel(void) {
    if (sCapKind != CAPTURE_NONE) {
        capture_end();
    }
}

/* Which connected controller a player uses: -1 none, 0 = the first, ... */
EMSCRIPTEN_KEEPALIVE int Bt3Ui_PadSlot(int player) {
    return *PortInput_PadSlot(player & 1);
}

EMSCRIPTEN_KEEPALIVE void Bt3Ui_PadSlotSet(int player, int slot) {
    *PortInput_PadSlot(player & 1) = slot < -1 ? -1 : slot;
    PortInput_Save();
}

EMSCRIPTEN_KEEPALIVE int Bt3Ui_GamepadCount(void) {
    int count = 0;
    SDL_free(SDL_GetGamepads(&count));
    return count;
}

/* The name of connected controller i (a static buffer, valid until the next call). */
EMSCRIPTEN_KEEPALIVE const char *Bt3Ui_GamepadName(int i) {
    static char name[128];
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    const char *n = ids != NULL && i >= 0 && i < count ? SDL_GetGamepadNameForID(ids[i]) : NULL;
    snprintf(name, sizeof(name), "%s", n != NULL ? n : "");
    SDL_free(ids);
    return name;
}
