/*
 * A release program started without the game's data (a player who opened the game before its setup): the setup
 * next to it is opened in its place, which finds the disc image in the folder, installs and opens the game again
 * (port/setup/setup.cpp). Before this the program only wrote a line to a console nobody sees and closed.
 *
 * Only for a player's start: not when the setup itself started the game (BT3_FROM_SETUP, so that the two cannot open
 * each other in turn), nor for a run without a window or a test (the setup's self-test, a replay, a recorded pad, BT3_GS
 * other than the window's), nor with the data asked for elsewhere (BT3_DATA). Those end as before, with the reason
 * written and exit code 2. A setup already at work (installing) does not start twice: it keeps one copy running.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETUP_NAME "Tenkaichi3Decomp-setup"

/* 1 when the setup was started (plat_mem.c: the program then ends). */
int Port_OpenSetup(void) {
    const char *base = SDL_GetBasePath();
    char path[1024], dir[1024];
    const char *args[2] = {path, NULL};
    SDL_PathInfo info;
    SDL_PropertiesID props;
    SDL_Environment *env;
    SDL_Process *proc;

    static const char *const kNotForPlayers[] = {"BT3_FROM_SETUP", "BT3_DEMO", "BT3_DATA", "BT3_REPLAY", "BT3_PAD_PLAY",
                                                 "BT3_SIGN_TEST", "BT3_SHOT", "BT3_HASH", "BT3_SYNCTEST"};
    const char *gs = getenv("BT3_GS");
    size_t i;

    for (i = 0; i < sizeof(kNotForPlayers) / sizeof(kNotForPlayers[0]); i++) {
        if (getenv(kNotForPlayers[i]) != NULL) {
            return 0;
        }
    }
    if ((gs != NULL && strcmp(gs, "gpu") != 0) || base == NULL) {
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s", base);
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s" SETUP_NAME ".exe", base);
#else
    snprintf(path, sizeof(path), "%s" SETUP_NAME, base);
#endif
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        return 0;
    }
    props = SDL_CreateProperties();
    env = SDL_CreateEnvironment(true);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, dir);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (proc == NULL) {
        fprintf(stderr, "bt3: the setup could not be opened: %s\n", SDL_GetError());
        return 0;
    }
    SDL_DestroyProcess(proc);
    fprintf(stderr, "bt3: the game's data is not installed: the setup was opened\n");
    return 1;
}
