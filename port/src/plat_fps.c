/*
 * The PC port's frame-rate choice: how many pictures a second are shown, and whether the game itself is meant to
 * run at that rate. Settings live with the other port settings ("fps_mode", "fps_limit", "menu_fps_limit");
 * gs_draw.c asks for the target every tick and turns it into pictures.
 *
 * 30 is the safe, validated fight rate; menus have a setting of their own and start at the console's 60. A fight is
 * still computed 30 times a second in the hybrid mode: only the renderer makes the pictures in between. Online play
 * and replays always use the console's rates, whatever is chosen. The full-rate mode is a saved choice for now, not
 * an active one: see FPS_MODE_FULL below.
 */
#include <stdint.h>
#include <stdlib.h>

extern int Port_Setting(const char *name, int def);
extern void Port_SettingSave(const char *name, int value);
extern int Port_NetSession(void);
extern int Port_NetActive(void);
extern int BattleReplay_IsActive(void);
extern void GsGpu_FpsReset(void);

enum {
    FPS_MODE_HYBRID = 0,
    /* Saved for the next step, but not active: the game's own 30 Hz action frames and timers still have to be
       converted. Port_FpsTarget does not look at the mode, so both choices use the hybrid path for now. */
    FPS_MODE_FULL = 1,
};

static int sMode = -1;
static int sLimit = -1;
static int sMenuLimit = -1;
static int sLastLimit = 60; /* F9: what to go back to */

/* The menu's list. 0 is "Unlocked": no upper limit of its own; the renderer shows as many pictures as the machine
   can make without taking time from the game's own 30/60 Hz ticks. */
static const int kLimits[] = { 30, 60, 90, 120, 144, 165, 180, 200, 240, 0 };

static int valid_limit(int fps) {
    int i;

    for (i = 0; i < (int)(sizeof(kLimits) / sizeof(kLimits[0])); i++) {
        if (kLimits[i] == fps) {
            return 1;
        }
    }
    return 0;
}

/* The old "Smooth motion" setting was 0 off, 1 = 60, 2 = 120, 3 = 240. A saved choice carries over the first time
   this version runs; after that "fps_limit" is the one setting. */
static int old_limit(int interp) {
    return interp <= 0 ? 30 : interp == 1 ? 60 : interp == 2 ? 120 : 240;
}

/* Test switches: BT3_FPS_LIMIT is the new one, BT3_INTERP the old one (kept for the recorded tests). -1: neither. */
static int limit_from_env(void) {
    const char *env = getenv("BT3_FPS_LIMIT");

    if (env != NULL && valid_limit(atoi(env))) {
        return atoi(env);
    }
    env = getenv("BT3_INTERP");
    if (env != NULL) {
        return old_limit(atoi(env));
    }
    return -1;
}

int Port_FpsMode(void) {
    if (sMode < 0) {
        const char *env = getenv("BT3_FPS_MODE");
        int mode = env != NULL ? atoi(env) : Port_Setting("fps_mode", FPS_MODE_HYBRID);
        sMode = mode == FPS_MODE_FULL ? FPS_MODE_FULL : FPS_MODE_HYBRID;
    }
    return sMode;
}

void Port_FpsModeSet(int mode) {
    mode = mode == FPS_MODE_FULL ? FPS_MODE_FULL : FPS_MODE_HYBRID;
    if (mode != Port_FpsMode()) {
        sMode = mode;
        Port_SettingSave("fps_mode", mode);
        GsGpu_FpsReset();
    }
}

int Port_FpsLimit(void) {
    if (sLimit < 0) {
        int limit = limit_from_env();
        if (limit < 0) {
            limit = Port_Setting("fps_limit", -1);
        }
        if (limit < 0) {
            limit = old_limit(Port_Setting("interp", 0));
        }
        sLimit = valid_limit(limit) ? limit : 30;
        if (sLimit != 30) {
            sLastLimit = sLimit;
        }
    }
    return sLimit;
}

void Port_FpsLimitSet(int fps) {
    int was;

    if (!valid_limit(fps)) {
        fps = 30;
    }
    was = Port_FpsLimit();
    if (fps != was) {
        if (was != 30) {
            sLastLimit = was; /* (0, "Unlocked", is a choice to come back to as much as 144 is) */
        }
        sLimit = fps;
        Port_SettingSave("fps_limit", fps);
        GsGpu_FpsReset();
    }
}

/* Menus have their own limit. A 30 Hz fight and a 60 Hz menu are the console's own rates, so a saved choice of 30
   for fights (or the old "Smooth motion" off) starts menus at 60. Any other saved choice carries over to menus the
   first time this version runs, as the old setting applied to both. */
static int menu_limit_default(void) {
    int fight = Port_FpsLimit();

    return fight == 30 ? 60 : fight;
}

int Port_MenuFpsLimit(void) {
    if (sMenuLimit < 0) {
        const char *env = getenv("BT3_MENU_FPS_LIMIT");
        int limit = -1;

        if (env != NULL && valid_limit(atoi(env))) {
            limit = atoi(env);
        }
        if (limit < 0 && limit_from_env() >= 0) {
            /* a test run with BT3_FPS_LIMIT / BT3_INTERP: the saved menu limit must not override it */
            limit = menu_limit_default();
        }
        if (limit < 0) {
            limit = Port_Setting("menu_fps_limit", -1);
            if (!valid_limit(limit)) {
                limit = menu_limit_default();
                Port_SettingSave("menu_fps_limit", limit); /* (make the split explicit from now on) */
            }
        }
        sMenuLimit = limit;
    }
    return sMenuLimit;
}

void Port_MenuFpsLimitSet(int fps) {
    if (!valid_limit(fps)) {
        fps = 60;
    }
    if (fps != Port_MenuFpsLimit()) {
        sMenuLimit = fps;
        Port_SettingSave("menu_fps_limit", fps);
        GsGpu_FpsReset();
    }
}

/* F9: the same quick switch the old "Smooth motion" key had. 30 (the console's own rate) or back to what it was. */
void Port_FpsToggle(void) {
    int limit = Port_FpsLimit();

    if (limit != 30) {
        sLastLimit = limit;
        Port_FpsLimitSet(30);
    } else {
        Port_FpsLimitSet(sLastLimit != 30 ? sLastLimit : 60);
    }
}

/* 1 unless online play or a replay is running: those stay at the rate their input and replay blocks were made for. */
int Port_FpsAllowed(void) {
    if (Port_NetSession() || Port_NetActive() || BattleReplay_IsActive() != 0) {
        return 0;
    }
    return getenv("BT3_REPLAY") == NULL;
}

/* Pictures a second to aim for. 0 (Unlocked) is no upper limit: gs_draw.c then asks for as many pictures as the
   machine can show without delaying the next tick. (Not the display's refresh: a limit that follows it makes the
   same choice show a different rate on another monitor, and a 60 Hz display would hide the machine's real speed.)
   `menu` chooses the menu limit for a 60 Hz scene (one blank per tick) and the fight limit otherwise. Online play
   and replays keep the console's own rates: 30 in a fight, 60 in a menu. */
int Port_FpsTargetFor(int menu) {
    int limit;

    if (!Port_FpsAllowed()) {
        return menu ? 60 : 30;
    }
    limit = menu ? Port_MenuFpsLimit() : Port_FpsLimit();
    return limit > 0 ? limit : 0;
}

int Port_FpsTarget(void) {
    return Port_FpsTargetFor(0);
}

