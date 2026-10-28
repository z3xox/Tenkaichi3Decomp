// Build with port/setup/native.cpp and -lSDL3 -lpthread, using the generated interface header.
// This checks locale sharing across translation units and the real installer's worker thread.
#include "ui_localization.h"
#include "../../setup/native.h"
#include "../../setup/game_launch.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #test); std::exit(1); } } while (0)

int main(int argc, char **argv) {
    if (const char *report = std::getenv("BT3_SETUP_LAUNCH_REPORT")) {
        std::string temp = std::string(report) + ".new";
        std::ofstream out(temp);
        out << (std::getenv("BT3_MENU_LANG") ? std::getenv("BT3_MENU_LANG") : "not-forced") << '\n';
        out << (std::getenv("BT3_GS") ? std::getenv("BT3_GS") : "missing") << '\n';
        out << (std::getenv("BT3_DEMO") || std::getenv("BT3_REPLAY") ? "test-mode" : "normal-play") << '\n';
        out.close();
        std::filesystem::rename(temp, report);
        return 0;
    }
    (void)argc;
    char directory[] = "/tmp/bt3-setup-locale-XXXXXX";
    CHECK(mkdtemp(directory));
    std::string root = std::string(directory) + "/";
    std::string missing = root + "Play-à.iso", invalid = root + "invalid.iso";
    std::ofstream(invalid) << "This is a test file, not a disc image.";
    CHECK(SDL_Init(0));
    for (int i = 0; i < Bt3Ui::LanguageCount(); i++) {
        CHECK(Bt3Ui::SetLocale(kBt3UiLanguages[i].locale));
        for (bool exists : {false, true}) {
            Native_Start(root, exists ? invalid : missing, "");
            Uint64 start = SDL_GetTicks();
            while (Native_Running() && SDL_GetTicks() - start < 5000) SDL_Delay(1);
            CHECK(!Native_Running());
            Native_Join();
            std::string event, stopped;
            bool step = false, failed = false, done = false;
            while (Native_Poll(event)) {
                if (event.rfind("@step 1 3 Disc image", 0) == 0) step = true;
                if (event == "@fail") failed = true;
                if (event.rfind("@done", 0) == 0) done = true;
                if (event.rfind("@stopped ", 0) == 0) stopped = event.substr(9);
            }
            CHECK(step && failed && !done);
            if (exists) {
                CHECK(stopped == Bt3Ui::T("This file is not a disc image (.iso). Compressed images (.chd, .cso, .gz) have to be converted to .iso first."));
            } else {
                CHECK(stopped == std::string(Bt3Ui::T("The disc image could not be opened: ")) + missing);
            }
        }
    }
    std::string executable = std::filesystem::absolute(argv[0]).string();
    std::string report = root + "launch-result.txt";
    CHECK(setenv("BT3_SETUP_LAUNCH_REPORT", report.c_str(), 1) == 0);
    for (bool forced : {false, true}) {
        if (forced) CHECK(setenv("BT3_MENU_LANG", "fr", 1) == 0);
        else CHECK(unsetenv("BT3_MENU_LANG") == 0);
        CHECK(setenv("BT3_DEMO", "1", 1) == 0);
        CHECK(setenv("BT3_REPLAY", "test-recording", 1) == 0);
        std::filesystem::remove(report);
        SDL_Process *child = Bt3Setup::LaunchGame(root, executable);
        CHECK(child);
        SDL_DestroyProcess(child);
        // Play deliberately detaches the game. Wait for the child's atomic report, not its exit status.
        Uint64 start = SDL_GetTicks();
        while (!std::filesystem::exists(report) && SDL_GetTicks() - start < 5000) SDL_Delay(1);
        std::ifstream in(report);
        std::string locale, renderer, mode;
        CHECK(std::getline(in, locale) && std::getline(in, renderer) && std::getline(in, mode));
        CHECK(locale == (forced ? "fr" : "not-forced"));
        CHECK(renderer == "gpu" && mode == "normal-play");
    }
    CHECK(unsetenv("BT3_SETUP_LAUNCH_REPORT") == 0);
    SDL_Quit();
    std::filesystem::remove_all(root);
    std::puts("native setup: six locales, invalid images rejected, protocol/UTF-8 paths preserved; Play preserves normal language selection and explicit overrides");
}
