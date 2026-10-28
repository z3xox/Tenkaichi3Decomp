// Shared by the Play button and its process-launch regression test.
#ifndef BT3_SETUP_GAME_LAUNCH_H
#define BT3_SETUP_GAME_LAUNCH_H
#include <SDL3/SDL.h>
#include <string>

namespace Bt3Setup {
inline SDL_Process *LaunchGame(const std::string &root, const std::string &launcher) {
    const char *args[] = {launcher.c_str(), nullptr};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Environment *env = SDL_CreateEnvironment(true);
    SDL_SetEnvironmentVariable(env, "BT3_GS", "gpu", true);
    SDL_UnsetEnvironmentVariable(env, "BT3_DEMO");
    SDL_UnsetEnvironmentVariable(env, "BT3_REPLAY");
    // Keep the game's saved language/first-run selector. BT3_MENU_LANG is only
    // inherited when the caller explicitly supplied that developer override.
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, root.c_str());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process *game = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    return game;
}
}
#endif
