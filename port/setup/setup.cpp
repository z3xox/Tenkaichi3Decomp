// The setup window of the PC port: pick your disc image, watch the steps, press Play.
//
// A small native program (SDL3 + Dear ImGui, the same library and look as the game's F1 settings window). It does
// none of the work itself: it runs `python3 install.py --machine --iso <file>` next to it and shows the events
// that script prints (one line each: @step, @note, @ok, @skip, @fail, @missing, @stopped, @done; see install.py).
//
//   build:  port/setup/build.sh   ->  ./Tenkaichi3Decomp-setup
//   test:   BT3_SETUP_ISO=<file> starts the installation at once; BT3_SETUP_SHOT=<prefix> writes the window's
//           picture to <prefix>_<n>.ppm whenever the page or the step changes (no person needed to check it)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#include "imgui.h"
#include "imgui_internal.h" // ImGui::NavInitWindow
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h" // glViewport, glClear for the OpenGL window (set up by ImGui_ImplOpenGL3_Init)
#include "native.h"
#include "ui.h" // UiA11y_*: the focused control said to the screen reader (port/src/gs/ui_a11y.cpp)
#include "setup_voice.h"

#define GAME "Tenkaichi3Decomp" /* the program this sets up: GAME, GAME.dat; this program is GAME-setup */

// The screen reader, as in the game: through Prism when prism.dll is beside this program (port/src/gs/a11y.c, with
// its settings in bt3_settings.txt when there is one: a11y=0 turns it off). ui_a11y.cpp says the focused control;
// said here are each page as it comes up (its heading and text), a disc image chosen, a problem under a field, and
// the installation's steps as they start and end.
extern "C" {
int PortA11y_Begin(void);
void PortA11y_Say(const char *text, int flags);
void PortA11y_Event(const SDL_Event *ev);
// a11y.c's links to the game's sound, which the setup never uses (it plays no cue and no voice)
SDL_AudioDeviceID Port_AudioDevice(void) { return 0; }
int PortA11y_VoicePercent(const void *player) { (void)player; return -1; }
}
#define A11Y_INTERRUPT 1 // (gs/a11y.c)

enum Page { PAGE_PICK, PAGE_RUN, PAGE_DONE, PAGE_FAILED };
enum State { PENDING, RUNNING, OK, SKIPPED, FAILED };
struct StepRow { std::string title, detail; State state = PENDING; Uint64 start = 0, end = 0; };
struct Missing { std::string name, why, pkg; };

// The steps of the two ways to install: from a source checkout (install.py does the work and needs build tools) and
// in a release folder (Tenkaichi3Decomp and Tenkaichi3Decomp.dat next to this program: native.cpp unpacks the disc itself).
static const char *kSourceSteps[] = {"Requirements", "Disc image", "Game data", "Executable data", "Build", "Self-test"};
static const char *kReleaseSteps[] = {"Disc image", "Game data", "Self-test"};
static const struct { const char *title, *help; } kHelp[] = {
    {"European languages", "Spanish, German, French and Italian, plus the opening soundtrack"},
    {"Requirements", "Compilers and tools on this computer"}, {"Disc image", "Your disc image is read and checked"},
    {"Game data", "Models, sounds and text are unpacked"}, {"Executable data", "The data tables of the game's programs"},
    {"Build", "The port is compiled (a few minutes)"}, {"Self-test", "The game's demo fight, without a window"}};
static bool sRelease;

static const char *step_help(const std::string &title) {
    for (auto &h : kHelp) {
        if (title == h.title) {
            return h.help;
        }
    }
    return "";
}

static SDL_Window *sWindow;
static SDL_GPUDevice *sDevice;
// The window is drawn through SDL's GPU layer (Vulkan on Linux, Direct3D 12 on Windows) or, where that cannot
// start (no Vulkan driver, a graphics chip too old for it: reported for Intel Ivy Bridge), through OpenGL.
// BT3_GPU_API=gl asks for OpenGL at once, as it does for the game.
static SDL_GLContext sGl;
static bool sUseGl;
static Page sPage = PAGE_PICK;
static char sIso[1024];
// Optional: a disc image of the European release (SLES-54945), for Spanish, German, French and Italian. The game is
// always set up from the USA one; from this one only the languages' archive entries are taken (native.cpp step_eu,
// install.py --eu-iso).
static char sIsoEu[1024];
static bool sEuOnly;    // only the languages are added (the game was set up before: the "Ready to play" page)
static int sDialogFor;  // which field the file dialog fills: 0 the USA image, 1 the European one
static std::string sRoot, sError, sErrorDetail, sLauncher, sLine; // sErrorDetail: a file or a line, shown, not said
static std::vector<StepRow> sRows;
static std::vector<Missing> sMissing;
static SDL_Process *sProc;
static bool sNative; // the native installer is at work (release folder)
static std::mutex sDialogLock;
static std::string sDialogResult;
static bool sDialogReady, sQuit;
static Uint64 sRunStart, sRunEnd;
static std::vector<std::string> sCandidates; // disc images of the folder still to try (setup_voice.cpp)
static Uint64 sAutoPlayAt;                    // when the game opens by itself, 0 not
static Page sShownPage;                       // the page whose change was last told (page_changed)
static bool sChosen;                          // sIso was chosen by the player (argument, Browse, a drop, typed), not found
static bool sStopAsked;                       // Cancel was pressed in this run: whatever ends it, the game does not open
// The most telling failure of the folder's images in this round (look_for_disc starts one), said at its end in place of
// the last image's, which can be any other file of the folder; and the images that failed the disc step already.
static struct Best { int rank = -1; std::string error, detail, iso; } sBest;
struct Stamp { std::string path; Sint64 size; SDL_Time time; };
static std::vector<Stamp> sBad;   // images that failed the disc step, as they were then
static std::vector<Stamp> sKnown; // images the setup has looked at or tried: the folder's watch takes only others
static bool sDialogOpen;          // the Browse dialog is open (sDialogLock)

// A file's path, size and modify time now (size -1: not there): an image that changed is another image.
static Stamp stamp(const std::string &path) {
    SDL_PathInfo info;
    bool ok = SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
    return {path, ok ? (Sint64)info.size : -1, ok ? info.modify_time : 0};
}

static bool listed(const std::vector<Stamp> &list, const std::string &path) {
    Stamp now = stamp(path);
    for (auto &b : list) {
        if (b.path == path && b.size == now.size && b.time == now.time) {
            return true;
        }
    }
    return false;
}

static void remember(std::vector<Stamp> &list, const std::string &path) {
    if (!listed(list, path)) {
        list.push_back(stamp(path));
    }
}

static const ImVec4 kAccent(0.96f, 0.55f, 0.13f, 1.0f), kGreen(0.36f, 0.80f, 0.45f, 1.0f), kRed(0.93f, 0.36f, 0.33f, 1.0f),
    kDim(0.60f, 0.63f, 0.70f, 1.0f);
// For the screen reader (see the top): the page's words as this frame drew them, the problem shown under a field, and
// what happened since the last frame (steps, a file chosen), said after the page so that the page does not cut it.
static std::string sPageWords, sProblem, sSaidProblem, sNews;
static int sSaidPage = -1, sNotePercent;
static bool sNoDisc; // BT3 a11y: the folder was looked at and has no disc image (look_for_disc)

static void page_words(const std::string &text) {
    if (!text.empty()) {
        sPageWords += (sPageWords.empty() ? "" : " ") + Voice_Words(text); // BT3 a11y: Voice_Words
    }
}

static void news(const std::string &text) {
    if (!text.empty()) {
        sNews += (sNews.empty() ? "" : ". ") + Voice_Words(text); // BT3 a11y: Voice_Words
    }
}

// A file name without its folder (what is said of a disc image chosen).
static std::string file_name(const char *path) {
    const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
    const char *p = slash > back ? slash : back;
    return p != NULL ? p + 1 : path;
}

static bool file_exists(const char *path) {
    SDL_PathInfo info;
    return path[0] != '\0' && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

// The folder to install into: a release folder (this program's own), or the source checkout that holds install.py.
static std::string find_root() {
    std::string dir = SDL_GetBasePath() ? SDL_GetBasePath() : "./";
    if ((file_exists((dir + GAME).c_str()) || file_exists((dir + GAME ".exe").c_str())) && file_exists((dir + GAME ".dat").c_str())) {
        sRelease = true; // a release folder: the finished program is here, only the game data is missing
        return dir;
    }
    for (int up = 0; up < 4; up++) {
        if (file_exists((dir + "install.py").c_str())) {
            return dir;
        }
        dir += "../";
    }
    return "";
}

static void style() {
    ImGuiStyle &st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 0.0f;
    st.FrameRounding = 6.0f;
    st.GrabRounding = 6.0f;
    st.WindowPadding = ImVec2(28.0f, 22.0f);
    st.FramePadding = ImVec2(12.0f, 8.0f);
    st.ItemSpacing = ImVec2(10.0f, 10.0f);
    st.WindowBorderSize = 0.0f;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.11f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.23f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.24f, 0.32f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.38f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.17f, 0.20f, 0.27f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.96f, 0.55f, 0.13f, 0.55f);
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_PlotHistogram] = kAccent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.96f, 0.55f, 0.13f, 0.35f);
}

// ---------------------------------------------------------------------------------------------- the engine script

// The rows as the run numbers its steps: the European languages, when asked for, before the self-test (native.cpp
// work, install.py main); only them when they are added to a game set up before.
static void reset_rows() {
    std::vector<std::string> titles;
    if (sRelease) {
        titles.assign(kReleaseSteps, kReleaseSteps + sizeof(kReleaseSteps) / sizeof(kReleaseSteps[0]));
    } else {
        titles.assign(kSourceSteps, kSourceSteps + sizeof(kSourceSteps) / sizeof(kSourceSteps[0]));
    }
    if (sEuOnly) {
        titles.assign(1, "European languages");
    } else if (sIsoEu[0] != '\0') {
        titles.insert(titles.end() - 1, "European languages");
    }
    sRows.assign(titles.size(), StepRow());
    for (size_t i = 0; i < titles.size(); i++) {
        sRows[i].title = titles[i];
    }
}

static void start_install() {
    static std::string iso, euIso;
    sShownPage = PAGE_RUN; // (a run that fails before a frame has shown it still changes the page: told)
    sStopAsked = false;
    sErrorDetail.clear();
    if (sChosen) {
        sCandidates.clear(); // (the player's own choice: no image of the folder is tried after it)
        sBest = Best();
    }
    if (sRelease) {
        remember(sKnown, sIso); // (the watch does not take it again by itself)
    }
    const char *args[10];
    SDL_PropertiesID props = SDL_CreateProperties();
    std::string script = sRoot + "install.py";
    int n = 0;

    if (sRelease) {
        SDL_DestroyProperties(props);
        reset_rows();
        sMissing.clear();
        sError.clear();
        sLauncher.clear();
        sRunStart = SDL_GetTicks();
        sNative = true;
        Native_Start(sRoot, sEuOnly ? "" : sIso, sIsoEu);
        sPage = PAGE_RUN;
        return;
    }
    iso = sIso;
    args[n++] = "python3";
    args[n++] = script.c_str();
    args[n++] = "--machine";
    args[n++] = "--iso";
    args[n++] = iso.c_str();
    if (sIsoEu[0] != '\0') {
        euIso = sIsoEu;
        args[n++] = "--eu-iso";
        args[n++] = euIso.c_str();
    }
    if (getenv("BT3_SETUP_ARGS") != NULL) { // one extra argument for testing, such as --skip-test
        args[n++] = getenv("BT3_SETUP_ARGS");
    }
    args[n] = NULL;
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, sRoot.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    reset_rows();
    sMissing.clear();
    sError.clear();
    sLine.clear();
    sLauncher.clear();
    sProc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    sRunStart = SDL_GetTicks();
    if (sProc == NULL) {
        sError = std::string("Could not start python3: ") + SDL_GetError() + "\nPython 3 has to be installed to set the game up.";
        sPage = PAGE_FAILED;
        return;
    }
    sPage = PAGE_RUN;
}

static StepRow *current() {
    for (auto &r : sRows) {
        if (r.state == RUNNING) {
            return &r;
        }
    }
    return NULL;
}

static std::string unescape(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out += '\n';
            i++;
            while (i + 1 < s.size() && s[i + 1] == ' ') { // the terminal version indents continuation lines
                i++;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

static void event(const std::string &line) {
    if (line.empty() || line[0] != '@') {
        return;
    }
    size_t sp = line.find(' ');
    std::string kind = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
    std::string text = sp == std::string::npos ? "" : unescape(line.substr(sp + 1));
    StepRow *cur = current();

    if (kind == "step") {
        int i = atoi(text.c_str());
        if (i >= 1 && i <= (int)sRows.size()) {
            sRows[i - 1].state = RUNNING;
            sRows[i - 1].start = SDL_GetTicks();
            sRows[i - 1].detail.clear();
            news(sRows[i - 1].title + ", step " + std::to_string(i) + " of " + std::to_string(sRows.size()));
            sNotePercent = 0;
        }
    } else if (cur != NULL && kind == "note") {
        cur->detail = text;
        // a progress note ("37%, 1200 files") comes for every percent: said every 10
        int percent = text.find('%') != std::string::npos ? atoi(text.c_str()) : -1, part, whole;
        if (percent < 0 && sscanf(text.c_str(), "%d of %d", &part, &whole) == 2 && whole > 0) {
            percent = part * 100 / whole; // BT3 a11y: the European step's "12 of 410" (one per file), every 10 too
        }
        if (percent < 0) {
            news(text);
        } else if (percent / 10 > sNotePercent / 10) {
            news(text);
            sNotePercent = percent;
        }
    } else if (cur != NULL && (kind == "ok" || kind == "skip" || kind == "fail")) {
        cur->state = kind == "ok" ? OK : kind == "skip" ? SKIPPED : FAILED;
        cur->end = SDL_GetTicks();
        cur->detail = kind == "fail" ? "" : text;
        if (kind != "fail") { // (a failure is said by the page that comes up)
            news(cur->title + (kind == "ok" ? " done" : " skipped") + (text.empty() ? "" : ": " + text));
        }
    } else if (kind == "missing") {
        Missing m;
        size_t a = text.find('|'), b = text.find('|', a == std::string::npos ? 0 : a + 1);
        if (a != std::string::npos && b != std::string::npos) {
            m.name = text.substr(0, a);
            m.why = text.substr(a + 1, b - a - 1);
            m.pkg = text.substr(b + 1);
            sMissing.push_back(m);
        }
    } else if (kind == "detail") {
        sErrorDetail = line.substr(sp + 1); // (as it came: a Windows path's "\\n" is no line break)
    } else if (kind == "stopped") {
        sError = text;
    } else if (kind == "done") {
        sLauncher = text;
    }
}

static void pump_install() {
    char buf[4096];
    SDL_IOStream *out;
    int code = 0;

    if (sNative) {
        std::string line;
        bool ended = !Native_Running(); // asked before the queue is emptied: nothing is left behind
        while (Native_Poll(line)) {
            event(line);
        }
        if (ended) {
            sNative = false;
            sRunEnd = SDL_GetTicks();
            for (auto &r : sRows) {
                if (r.state == RUNNING) {
                    r.state = FAILED;
                    r.end = sRunEnd;
                }
            }
            if (!sLauncher.empty()) {
                sPage = PAGE_DONE;
            } else {
                if (sError.empty()) {
                    sError = "The setup stopped unexpectedly.";
                }
                sPage = PAGE_FAILED;
            }
        }
        return;
    }
    if (sProc == NULL) {
        return;
    }
    out = SDL_GetProcessOutput(sProc);
    for (;;) {
        size_t n = out != NULL ? SDL_ReadIO(out, buf, sizeof(buf)) : 0;
        if (n == 0) {
            break;
        }
        for (size_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                event(sLine);
                sLine.clear();
            } else if (buf[i] != '\r') {
                sLine += buf[i];
            }
        }
    }
    if (SDL_WaitProcess(sProc, false, &code)) {
        event(sLine);
        SDL_DestroyProcess(sProc);
        sProc = NULL;
        sRunEnd = SDL_GetTicks();
        for (auto &r : sRows) {
            if (r.state == RUNNING) {
                r.state = FAILED;
                r.end = sRunEnd;
            }
        }
        if (code == 0 && !sLauncher.empty()) {
            sPage = PAGE_DONE;
        } else {
            if (sError.empty()) {
                sError = "The setup script stopped unexpectedly. The details are in install.log.";
            }
            sPage = PAGE_FAILED;
        }
    }
}

// --------------------------------------------------------------------------------------------------------- pages

static void SDLCALL dialog_done(void *userdata, const char *const *files, int filter) {
    (void)filter;
    std::lock_guard<std::mutex> lock(sDialogLock);
    if (files != NULL && files[0] != NULL) {
        sDialogResult = files[0];
        sDialogFor = userdata != NULL; // (NULL: the USA image's field)
        sDialogReady = true;
    }
    sDialogOpen = false;
}

static void heading(const char *title, const char *sub) {
    page_words(std::string(title) + ".");
    page_words(sub);
    ImGui::PushFont(NULL, 30.0f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("%s", sub);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
}

static bool big_button(const char *label, bool primary, bool enabled = true) {
    bool pressed;
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.55f, 0.13f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.63f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.06f, 0.03f, 1.0f));
    }
    ImGui::BeginDisabled(!enabled);
    pressed = ImGui::Button(label, ImVec2(150.0f, 40.0f));
    ImGui::EndDisabled();
    if (primary) {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

// Buttons of a page sit on one line at the bottom right.
static void bottom_row(int buttons) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float w = buttons * 150.0f + (buttons - 1) * ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + avail.x - w, ImGui::GetCursorPosY() + avail.y - 40.0f));
}

// The European disc image's field (optional). Returns false when something is in it that is not a file.
static bool eu_field() {
    static const SDL_DialogFileFilter filters[] = {{"PS2 disc image", "iso;bin;img"}, {"All files", "*"}};
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::TextUnformatted("European disc image (optional)");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 130.0f);
    ImGui::InputTextWithHint("##isoeu", "the European release (SLES-54945)", sIsoEu, sizeof(sIsoEu));
    UiA11y_Field("European disc image (optional)", sIsoEu);
    ImGui::SameLine();
    if (ImGui::Button("Browse...##eu", ImVec2(120.0f, 0.0f))) {
        {
            std::lock_guard<std::mutex> lock(sDialogLock);
            sDialogOpen = true; // (the folder is not looked at meanwhile: watch_folder)
        }
        SDL_ShowOpenFileDialog(dialog_done, (void *)1, sWindow, filters, 2, NULL, false);
    }
    if (sIsoEu[0] != '\0' && !file_exists(sIsoEu)) {
        ImGui::TextColored(kRed, "That file does not exist.");
        sProblem += Voice_Words("European disc image: that file does not exist.") + " "; // BT3 a11y: Voice_Words
        return false;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("Adds Spanish, German, French and Italian: menus, battle screen and text, as the European release has them. F1 > UI > Language picks one.");
    ImGui::PopStyleColor();
    return true;
}

// What Enter and Escape do on the page now (page_key), shown and said with it: for a player who cannot see the buttons.
static const char *page_keys() {
    switch (sPage) {
    case PAGE_PICK:
        return !sRelease ? "Escape closes." : sChosen && file_exists(sIso) ? "Enter installs it, Escape closes." : "Enter looks for it again, Escape closes.";
    case PAGE_RUN: return "Still installing. Please wait.";
    case PAGE_DONE:
        return sAutoPlayAt != 0 ? "The game opens by itself in a few seconds: Enter opens it now, Escape stays here." : "Enter opens the game, Escape closes.";
    case PAGE_FAILED: return sRelease ? "Press Enter to try again, or Escape to close." : "Escape closes.";
    }
    return "";
}

// The page's keys as a line of it (not on the running page: its Cancel button is the only thing to do there).
static void keys_line() {
    ImGui::TextColored(kDim, "%s", page_keys());
    page_words(page_keys());
}

static void page_pick() {
    static const SDL_DialogFileFilter filters[] = {{"PS2 disc image", "iso;bin;img"}, {"All files", "*"}};
    bool ok = file_exists(sIso);

    heading(GAME, "This sets the game up on your computer from your own PlayStation 2 disc.");
    std::string intro = std::string("Choose your disc image of Dragon Ball Z: Budokai Tenkaichi 3, USA release (SLUS-21678), as an .iso file. "
                                    "The game's data is taken from it and stays on this computer. ") +
                        (sRelease ? "Nothing is downloaded." : "Nothing is downloaded except one small tool.");
    ImGui::TextWrapped("%s", intro.c_str());
    page_words(intro);
    if (sRelease) {
        static const char *where = "Or put it next to the zip you unpacked, or in this program's folder: it is found there by itself.";
        ImGui::TextWrapped("%s", where);
        page_words(where);
        if (sNoDisc && !(sChosen && ok)) {
            ImGui::TextColored(kRed, "No disc image was found.");
            page_words("No disc image was found.");
        }
        keys_line();
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextUnformatted("Disc image");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 130.0f);
    if (ImGui::InputTextWithHint("##iso", "drop the file on this window, or browse", sIso, sizeof(sIso))) {
        sChosen = true;
    }
    UiA11y_Field("Disc image", sIso);
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(120.0f, 0.0f))) {
        {
            std::lock_guard<std::mutex> lock(sDialogLock);
            sDialogOpen = true;
        }
        SDL_ShowOpenFileDialog(dialog_done, NULL, sWindow, filters, 2, NULL, false);
    }
    if (sIso[0] != '\0' && !ok) {
        ImGui::TextColored(kRed, "That file does not exist.");
        sProblem += Voice_Words("Disc image: that file does not exist.") + " "; // BT3 a11y: Voice_Words
    } else if (sRoot.empty()) {
        static const char *none = "Neither the game (" GAME ", " GAME ".dat) nor install.py was found next to this program.";
        ImGui::TextColored(kRed, "%s", none);
        sProblem += Voice_Words(none); // BT3 a11y: Voice_Words
    } else {
        const char *need = sRelease ? "Needs about 4 GB of free space and takes a minute or two." : "Needs about 4 GB of free space and takes 5 to 10 minutes.";
        ImGui::TextColored(kDim, "%s", need);
        page_words(need);
    }
    bool euOk = eu_field();
    bottom_row(2);
    if (big_button("Close", false)) {
        sQuit = true;
    }
    ImGui::SameLine();
    if (big_button("Install", true, ok && euOk && !sRoot.empty())) {
        sEuOnly = false;
        start_install();
    }
}

static void state_icon(State st, float t) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c(p.x + 11.0f, p.y + 11.0f);

    if (st == OK || st == SKIPPED) {
        ImU32 col = ImGui::GetColorU32(kGreen);
        dl->AddCircle(c, 10.0f, col, 32, 1.6f);
        dl->AddLine(ImVec2(c.x - 5.0f, c.y), ImVec2(c.x - 1.5f, c.y + 4.0f), col, 2.0f);
        dl->AddLine(ImVec2(c.x - 1.5f, c.y + 4.0f), ImVec2(c.x + 5.5f, c.y - 4.0f), col, 2.0f);
    } else if (st == FAILED) {
        ImU32 col = ImGui::GetColorU32(kRed);
        dl->AddCircle(c, 10.0f, col, 32, 1.6f);
        dl->AddLine(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f), col, 2.0f);
        dl->AddLine(ImVec2(c.x - 4.0f, c.y + 4.0f), ImVec2(c.x + 4.0f, c.y - 4.0f), col, 2.0f);
    } else if (st == RUNNING) {
        float a = t * 5.0f;
        dl->AddCircle(c, 10.0f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.12f)), 32, 2.0f);
        dl->PathArcTo(c, 10.0f, a, a + 1.9f, 24);
        dl->PathStroke(ImGui::GetColorU32(kAccent), 0, 2.4f);
    } else {
        dl->AddCircle(c, 10.0f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.18f)), 32, 1.4f);
    }
    ImGui::Dummy(ImVec2(22.0f, 22.0f));
}

static void step_list() {
    float t = (float)SDL_GetTicks() / 1000.0f;
    for (size_t i = 0; i < sRows.size(); i++) {
        StepRow &r = sRows[i];
        char clock[32] = "";
        state_icon(r.state, t);
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 1.0f));
        ImGui::TextColored(r.state == PENDING ? kDim : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", r.title.c_str());
        ImGui::TextColored(kDim, "%s", !r.detail.empty() ? r.detail.c_str() : step_help(r.title));
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        if (r.state == RUNNING) {
            snprintf(clock, sizeof(clock), "%d s", (int)((SDL_GetTicks() - r.start) / 1000));
        } else if ((r.state == OK || r.state == FAILED) && r.end - r.start >= 2000) {
            snprintf(clock, sizeof(clock), "%d s", (int)((r.end - r.start) / 1000));
        }
        if (clock[0] != '\0') {
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(clock).x);
            ImGui::TextColored(kDim, "%s", clock);
        }
    }
}

static void page_run() {
    int done = 0;
    heading("Setting up", "You can leave this running. Finished steps are kept if it is interrupted.");
    step_list();
    for (auto &r : sRows) {
        done += r.state == OK || r.state == SKIPPED;
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::ProgressBar((float)done / (float)(sRows.empty() ? 1 : sRows.size()), ImVec2(-FLT_MIN, 8.0f), "");
    bottom_row(1);
    if (big_button("Cancel", false)) {
        if (sProc != NULL) {
            SDL_KillProcess(sProc, false);
        }
        Native_Cancel();
        sStopAsked = true;
        sError = "Cancelled. Finished steps are kept; Install continues from where it stopped.";
    }
}

static void launch_game() {
    const char *args[2] = {sLauncher.c_str(), NULL};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Environment *env = SDL_CreateEnvironment(true);
    SDL_SetEnvironmentVariable(env, "BT3_GS", "gpu", true); // the window and the GPU renderer
    SDL_SetEnvironmentVariable(env, "BT3_FROM_SETUP", "1", true); // (a game without its data does not open the setup again)
    SDL_UnsetEnvironmentVariable(env, "BT3_DEMO");
    SDL_UnsetEnvironmentVariable(env, "BT3_REPLAY");
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, sRoot.c_str());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process *game = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (game != NULL) {
        SDL_DestroyProcess(game);
        sQuit = true;
    } else {
        Voice_Say(Voice_Tr("The game could not be opened."), true);
    }
}

static void page_done() {
    char sub[128];
    if (sRunEnd > sRunStart) {
        snprintf(sub, sizeof(sub), "The game is installed and its demo fight ran correctly.  (%d min %d s)", (int)((sRunEnd - sRunStart) / 60000),
                 (int)((sRunEnd - sRunStart) / 1000 % 60));
    } else {
        snprintf(sub, sizeof(sub), "The game is installed.");
    }
    heading("Ready to play", sub);
    keys_line();
    step_list();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::TextColored(kDim, "In the game, F1 opens the settings: resolution, widescreen, controls and sound.");
    page_words("In the game, F1 opens the settings: resolution, widescreen, controls and sound.");
    {
        static const char *access = "Their Accessibility tab has the fight's sounds and the blind player profile, which turns on by itself with NVDA or JAWS.";
        ImGui::TextColored(kDim, "%s", access);
        page_words(access);
    }
    if (sRelease && !Native_EuInstalled(sRoot)) { // the languages can be added at any time
        static const SDL_DialogFileFilter filters[] = {{"PS2 disc image", "iso;bin;img"}, {"All files", "*"}};
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextWrapped("Spanish, German, French and Italian, plus the opening soundtrack: add a European disc image (SLES-54945).");
        if (ImGui::Button("Add languages and music...")) {
            sAutoPlayAt = 0; // (the game is not opened while the image is chosen)
            SDL_ShowOpenFileDialog(dialog_done, (void *)1, sWindow, filters, 2, NULL, false);
        }
    }
    bottom_row(2);
    if (big_button("Close", false)) {
        sQuit = true;
    }
    ImGui::SameLine();
    if (big_button("Play", true)) {
        launch_game();
    }
}

static void page_failed() {
    heading("Setup stopped", "Nothing is broken: finished steps are kept, and Install continues from where it stopped.");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.93f, 0.36f, 0.33f, 0.10f));
    ImGui::BeginChild("why", ImVec2(0.0f, sMissing.empty() ? 150.0f : 90.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextWrapped("%s", sError.c_str());
    page_words(sError);
    for (auto &m : sMissing) {
        page_words("Missing: " + m.name + ", used for " + m.why + ", package to install: " + m.pkg + ".");
    }
    if (!sErrorDetail.empty()) {
        ImGui::TextColored(kDim, "%s", sErrorDetail.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    keys_line();
    if (!sMissing.empty() && ImGui::BeginTable("missing", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Missing");
        ImGui::TableSetupColumn("Used for");
        ImGui::TableSetupColumn("Package to install");
        ImGui::TableHeadersRow();
        for (auto &m : sMissing) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(m.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", m.why.c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", m.pkg.c_str());
        }
        ImGui::EndTable();
    }
    bottom_row(3);
    if (big_button("Open the log", false)) {
        SDL_OpenURL(("file://" + sRoot + "install.log").c_str());
    }
    ImGui::SameLine();
    if (big_button("Close", false)) {
        sQuit = true;
    }
    ImGui::SameLine();
    if (big_button("Back", true)) {
        sPage = PAGE_PICK;
    }
}

static void build_ui() {
    ImGuiIO &io = ImGui::GetIO();
    int drawn = sPage; // (a button can change the page while it is drawn: its words are this one's)
    sPageWords.clear();
    sProblem.clear();
    UiA11y_Frame(1, 0);
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    // (no name before "###": the screen reader says the page's heading, not the window's name)
    ImGui::Begin("###setup", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    static int navPage = -1;
    if (sPage != navPage) { // the keyboard's focus to the new page's first control (it was on one of the page before: Tab
        navPage = sPage;    // and the arrows then went nowhere)
        ImGui::NavInitWindow(ImGui::GetCurrentWindow(), true);
    }
    switch (sPage) {
    case PAGE_PICK: page_pick(); break;
    case PAGE_RUN: page_run(); break;
    case PAGE_DONE: page_done(); break;
    case PAGE_FAILED: page_failed(); break;
    }
    ImGui::End();
    UiA11y_End();
    // a page that came up: all its words, interrupting; then what happened (queued), then a new problem under a field
    // once the field is left (not while a path is typed into it)
    if (drawn != sSaidPage && drawn == sPage) {
        sSaidPage = drawn;
        sSaidProblem.clear();
        PortA11y_Say(sPageWords.c_str(), A11Y_INTERRUPT);
        UiA11y_QueueNext(); // (the control the focus comes to is said after the page, not over it)
        if (drawn != PAGE_RUN) {
            sNews.clear(); // (the last step's end: "Ready to play" or "Setup stopped" says how it went)
        }
    }
    if (!sNews.empty()) {
        PortA11y_Say(sNews.c_str(), 0);
        sNews.clear();
    }
    if (sProblem != sSaidProblem && !ImGui::IsAnyItemActive()) {
        sSaidProblem = sProblem;
        PortA11y_Say(sProblem.c_str(), 0);
    }
}

// ------------------------------------------------------------------------------------------ the setup by itself
// For a player who cannot see this window, or a child: put the disc image in this program's folder and open the
// program (or the game, which opens it when its data is missing). It finds the image (setup_voice.cpp), installs,
// says how it goes through the screen reader (Prism, the game's own), and opens the game. Enter (or the controller's
// Cross / A) does the next thing of the page, Escape (Circle / B) closes; the mouse and Tab work as before.

static std::string base_name(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// The next disc image found in the folder, installed; false when none is left. Its name is said after the running
// page's words (news).
static bool start_found() {
    if (sCandidates.empty()) {
        return false;
    }
    SDL_strlcpy(sIso, sCandidates.front().c_str(), sizeof(sIso));
    sCandidates.erase(sCandidates.begin());
    sChosen = false;
    sEuOnly = false;
    sNoDisc = false;
    news("Found your disc image, " + base_name(sIso)); // (the running page says it is being set up)
    start_install();
    return true;
}

// 1 when the last run stopped at its first step, reading the disc image: the image was not the game's (or not one).
static bool disc_failed() {
    return sRelease && !sRows.empty() && sRows[0].state == FAILED;
}

// The folder's images, tried in turn; those that failed already last (the player may have put the right one there).
// None there: the first page, which says so; when it is up already (Enter on it), only that is said.
static void look_for_disc() {
    sBest = Best();
    sCandidates = Voice_FindIsos(sRoot);
    std::stable_partition(sCandidates.begin(), sCandidates.end(), [](const std::string &p) { return !listed(sBad, p); });
    for (auto &p : sCandidates) {
        remember(sKnown, p);
    }
    if (!start_found()) {
        bool shown = sPage == PAGE_PICK && sSaidPage == PAGE_PICK;
        sPage = PAGE_PICK;
        sNoDisc = true;
        if (shown) {
            Voice_Say(std::string(Voice_Tr("No disc image was found.")) + " " + Voice_Tr(page_keys()), true);
        }
    }
}

// While the setup waits for a disc image (the first page, or after the folder's images were not the game's), the
// folder is looked at every two seconds, and an image that is new there, or changed, since the setup last looked at
// or tried it is installed by itself once its copy has ended: the same size and time at two looks in a row, and it
// opens for reading (Windows refuses that while the copy still writes it). A child is told to copy the image there,
// and Enter in the file window opens the image instead of reaching this one. Not after Cancel on that page, not
// while the Browse dialog is open, and not over an image the player chose.
static Uint64 sWatchAt;

static void watch_folder() {
    static std::vector<Stamp> seen;
    bool waiting = sPage == PAGE_PICK || (sPage == PAGE_FAILED && disc_failed() && !sStopAsked), dialog;
    std::vector<Stamp> now;
    std::vector<std::string> ready;

    {
        std::lock_guard<std::mutex> lock(sDialogLock);
        dialog = sDialogOpen;
    }
    if (!sRelease || !waiting || dialog || (sChosen && file_exists(sIso)) || SDL_GetTicks() < sWatchAt) {
        return;
    }
    sWatchAt = SDL_GetTicks() + 2000;
    for (auto &path : Voice_FindIsos(sRoot)) {
        Stamp st = stamp(path);
        if (st.size <= 0 || listed(sKnown, path)) {
            continue;
        }
        now.push_back(st);
        for (auto &before : seen) {
            SDL_IOStream *io;
            if (before.path == path && before.size == st.size && before.time == st.time &&
                (io = SDL_IOFromFile(path.c_str(), "rb")) != NULL) {
                SDL_CloseIO(io);
                ready.push_back(path);
            }
        }
    }
    seen = now;
    if (!ready.empty()) {
        seen.clear();
        sBest = Best();
        sCandidates = ready;
        start_found();
    }
}

// Where the setup is, said again when its window comes back to the front (a second setup brings it there): short,
// the page's own words were said already.
static void say_page() {
    std::string said = Voice_Tr(page_keys());
    if (sPage == PAGE_PICK && sNoDisc && !(sChosen && file_exists(sIso))) {
        said = std::string(Voice_Tr("No disc image was found.")) + " " + said;
    } else if (sPage == PAGE_FAILED) {
        said = Voice_Words(sError) + " " + said;
    }
    Voice_Say(said, false);
}

// Where the next disc image of the folder is tried; and in a release, the game opened by itself once installed. The
// page's words (build_ui) say what changed.
static void page_changed(Page from) {
    if (sPage == PAGE_DONE) {
        if (sRelease && !sStopAsked) { // (a source checkout shows its result; a player who cancelled decides)
            // the page is said first: the first time it also has where the game's accessibility is
            sAutoPlayAt = SDL_GetTicks() + (from == PAGE_RUN ? 15000 : 8000);
        }
    } else if (sPage == PAGE_FAILED) {
        if (disc_failed() && !sStopAsked) {
            // the more an error says the image is the game's (its programs changed, then cut short, then another
            // PS2 game, then no disc image at all), the more it helps the player: that one is said at the end
            int rank = sError.find("checksums") != std::string::npos ? 3 : sError.find("read to its end") != std::string::npos ? 2
                     : sError.find("USA release") != std::string::npos ? 1 : 0;
            remember(sBad, sIso);
            if (rank > sBest.rank) {
                sBest = {rank, sError, sErrorDetail, sIso};
            }
            // only an image that is not the game's makes the next one of the folder worth a try: a full disk or a
            // self-test that could not run would fail the same with any image, and its reason has to be heard
            if (!sCandidates.empty()) {
                news("That disc image is not the right one; trying the next one.");
                start_found();
                return;
            }
            if (sBest.iso != sIso) {
                sError = sBest.error;
                sErrorDetail = sBest.detail;
                SDL_strlcpy(sIso, sBest.iso.c_str(), sizeof(sIso));
            }
        }
    }
}

// Enter / Escape and the controller's two buttons; true when the event was taken here. Enter on a button the keyboard
// moved to (Tab) is that button's, as ImGui has it.
static bool page_key(const SDL_Event &ev) {
    bool enter = false, escape = false;
    if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
        enter = (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) && !ImGui::IsAnyItemFocused();
        escape = ev.key.key == SDLK_ESCAPE;
    } else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        enter = ev.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH;
        escape = ev.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
    } else if (ev.type == SDL_EVENT_GAMEPAD_ADDED) {
        SDL_OpenGamepad(ev.gdevice.which);
    }
    if (escape) {
        if (sAutoPlayAt != 0) {
            sAutoPlayAt = 0;
            Voice_Say(Voice_Tr("The game does not open. Enter opens it, Escape closes."), true);
        } else if (sPage == PAGE_RUN) {
            Voice_Say(Voice_Tr("Still installing. Please wait."), true);
        } else {
            sQuit = true;
        }
        return true;
    }
    if (!enter || (!sRelease && sPage != PAGE_DONE)) { // (in a source checkout Enter only opens the game)
        return false;
    }
    switch (sPage) {
    case PAGE_PICK: // the image the player chose, else the folder looked at again (one may have been put there)
        if (sChosen && file_exists(sIso)) {
            sEuOnly = false;
            start_install();
        } else {
            look_for_disc();
        }
        break;
    case PAGE_RUN:
        Voice_Say(Voice_Tr("Still installing. Please wait."), true);
        break;
    case PAGE_DONE:
        sAutoPlayAt = 0;
        launch_game();
        break;
    case PAGE_FAILED: // the same image when it was the right one (a full disk, a blocked test); else the folder again
        if (!disc_failed() && file_exists(sIso)) {
            start_install();
        } else {
            look_for_disc();
        }
        break;
    }
    return true;
}

// One setup at a time for this computer: a second one (a double click more, or the game opened while it installs,
// which opens its setup) would write the same files at once. Kept until the program ends; the system lets it go
// then, however it ends.
#ifdef _WIN32
static bool only_one(const std::string &root) { // (CreateMutexW: windows.h, which imgui_impl_opengl3_loader.h includes)
    (void)root;
    void *mutex = CreateMutexW(NULL, 0, L"Local\\" GAME "-setup");
    return mutex != NULL && GetLastError() != 183; // ERROR_ALREADY_EXISTS
}

// The setup that is open, brought to the front (it says where it is then); false when it has no window. Windows lets
// a program started by the one in front (the file window, the game) do that; without it the player only hears that a
// setup is open, and Alt+Tab finds it.
static bool bring_forward() {
    HWND w = FindWindowW(NULL, L"" GAME " - Setup");
    if (w == NULL) {
        return false;
    }
    if (IsIconic(w)) {
        ShowWindow(w, SW_RESTORE);
    }
    SetForegroundWindow(w);
    return true;
}
#else
static bool only_one(const std::string &root) {
    int fd = open((root.empty() ? std::string(".setup-lock") : root + ".setup-lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    // (no lock file can be made, or the file system has no locks: as before, no guard)
    return fd < 0 || flock(fd, LOCK_EX | LOCK_NB) == 0 || errno != EWOULDBLOCK;
}
#endif

// ---------------------------------------------------------------------------------------------------- the window

// BT3_SETUP_SHOT: the picture drawn a second time into a texture and written as a PPM file.
static void shot(ImDrawData *dd, Uint32 w, Uint32 h, int number) {
    SDL_GPUTextureFormat fmt = SDL_GetGPUSwapchainTextureFormat(sDevice, sWindow);
    bool bgr = fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM || fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    SDL_GPUTextureCreateInfo ci;
    SDL_GPUTransferBufferCreateInfo ti;
    SDL_GPUColorTargetInfo target;
    SDL_GPUTextureRegion src;
    SDL_GPUTextureTransferInfo dst;
    char name[512];

    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = fmt;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(sDevice, &ci);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sDevice);
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
    SDL_zero(target);
    target.texture = tex;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, pass);
    SDL_EndGPURenderPass(pass);
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = w * h * 4;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(sDevice, &ti);
    SDL_zero(src);
    src.texture = tex;
    src.w = w;
    src.h = h;
    src.d = 1;
    SDL_zero(dst);
    dst.transfer_buffer = tb;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(sDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(sDevice, fence);
    const Uint8 *px = (const Uint8 *)SDL_MapGPUTransferBuffer(sDevice, tb, false);
    snprintf(name, sizeof(name), "%s_%d.ppm", getenv("BT3_SETUP_SHOT"), number);
    FILE *fp = fopen(name, "wb");
    if (fp != NULL && px != NULL) {
        fprintf(fp, "P6\n%u %u\n255\n", w, h);
        for (Uint32 i = 0; i < w * h; i++) {
            Uint8 rgb[3] = {px[i * 4 + (bgr ? 2 : 0)], px[i * 4 + 1], px[i * 4 + (bgr ? 0 : 2)]};
            fwrite(rgb, 1, 3, fp);
        }
        fclose(fp);
    }
    SDL_UnmapGPUTransferBuffer(sDevice, tb);
    SDL_ReleaseGPUTransferBuffer(sDevice, tb);
    SDL_ReleaseGPUTexture(sDevice, tex);
}

#ifdef _WIN32
// The few functions of Windows used below, declared here: its headers define names this file has (FAILED, ...).
extern "C" {
__declspec(dllimport) wchar_t *__stdcall GetCommandLineW(void);
__declspec(dllimport) wchar_t **__stdcall CommandLineToArgvW(const wchar_t *line, int *count);
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned page, unsigned long flags, const wchar_t *wide, int wideLen, char *out, int outLen,
                                                        const char *def, int *usedDef);
__declspec(dllimport) void *__stdcall LocalFree(void *mem);
}

// The program's arguments in UTF-8. Those main() gets on Windows are in the system's code page: a disc image whose
// path has letters outside it arrives with '?' in their place, and one with letters inside it as bytes that are not
// UTF-8, which is what SDL opens files by. Either way the image was "not found". NULL when Windows gives none.
static char **utf8_args(int *count) {
    int n = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &n);
    char **out = wide != NULL ? (char **)SDL_calloc((size_t)n + 1, sizeof(char *)) : NULL;

    for (int i = 0; out != NULL && i < n; i++) {
        int len = WideCharToMultiByte(65001 /* CP_UTF8 */, 0, wide[i], -1, NULL, 0, NULL, NULL);
        out[i] = (char *)SDL_calloc((size_t)(len > 0 ? len : 1), 1);
        if (out[i] == NULL) {
            out = NULL; // (out of memory at start-up: main's own arguments are used)
            break;
        }
        if (len > 0) {
            WideCharToMultiByte(65001, 0, wide[i], -1, out[i], len, NULL, NULL);
        }
    }
    if (wide != NULL) {
        LocalFree(wide);
    }
    *count = n;
    return out;
}
#endif

int main(int argc, char **argv) {
    static const char *fonts[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Geneva.ttf", // (macOS)
    };
    ImGui_ImplSDLGPU3_InitInfo info;
    int shots = 0, shotKey = -1;

#ifdef _WIN32
    {
        int count = 0;
        char **args = utf8_args(&count); // (kept for the whole run)
        if (args != NULL) {
            argc = count;
            argv = args;
        }
    }
#endif
#ifdef __APPLE__
    {
        // Started from Finder, a program gets only /usr/bin:/bin:/usr/sbin:/sbin: install.py would not find Homebrew's
        // tools (glslc, spirv-cross, 7z) and call them missing. Homebrew's folders first, as in a Terminal.
        const char *path = getenv("PATH");
        std::string more = std::string("/opt/homebrew/bin:/usr/local/bin:") + (path != NULL && path[0] != '\0' ? path : "/usr/bin:/bin:/usr/sbin:/sbin");
        setenv("PATH", more.c_str(), 1);
    }
#endif
    if (argc > 2 && strcmp(argv[1], "--install") == 0) {
        // Without a window: Tenkaichi3Decomp-setup --install <disc.iso> [<European disc.iso>] (an empty first one: only
        // the languages, for a folder set up before). A release folder only; prints the event lines.
        std::string line;
        bool ok = false;
        SDL_Init(0);
        sRoot = find_root();
        if (!sRelease) {
            fprintf(stderr, GAME "-setup --install works in a release folder (" GAME " and " GAME ".dat next to it).\nIn a source checkout: python3 install.py --iso <file>\n");
            return 2;
        }
        if (!only_one(sRoot)) {
            fprintf(stderr, GAME "-setup: another setup is running\n");
            return 1;
        }
        Native_Start(sRoot, argv[2], argc > 3 ? argv[3] : "");
        for (bool last = false; !last;) {
            last = !Native_Running(); // one more round after the worker has ended: nothing is left behind
            while (Native_Poll(line)) {
                printf("%s\n", line.c_str());
                fflush(stdout);
                ok = ok || line.rfind("@done", 0) == 0;
            }
            SDL_Delay(50);
        }
        Native_Join();
        return ok ? 0 : 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, GAME "-setup: %s\n", SDL_GetError());
        return 1;
    }
    sRoot = find_root();
    Voice_Start(sRoot);
    if (!only_one(sRoot)) {
        Voice_Say(Voice_Tr("The setup is open already."), true);
#ifdef _WIN32
        bring_forward(); // (which then says where it is, when it gets the focus)
#endif
        fprintf(stderr, GAME "-setup: another setup is running\n");
        SDL_Delay(1500); // (time to say it: the system's voice ends with the program)
        SDL_Quit();
        return 0;
    }
    const char *glsl = "#version 330";
    {
        const char *api = getenv("BT3_GPU_API");
        bool wantGl = api != NULL && (strcmp(api, "gl") == 0 || strcmp(api, "opengl") == 0);
        std::string why;
        if (!wantGl) {
            sWindow = SDL_CreateWindow(GAME " - Setup", 760, 560, SDL_WINDOW_HIGH_PIXEL_DENSITY);
            sDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false, NULL);
            if (sWindow == NULL || sDevice == NULL || !SDL_ClaimWindowForGPUDevice(sDevice, sWindow)) {
                why = SDL_GetError();
                if (sDevice != NULL) {
                    SDL_DestroyGPUDevice(sDevice);
                    sDevice = NULL;
                }
                if (sWindow != NULL) {
                    SDL_DestroyWindow(sWindow);
                    sWindow = NULL;
                }
            }
        }
        if (sDevice == NULL) {
            // OpenGL 3.3 core, and failing that whatever 3.0 context the driver gives
            for (int attempt = 0; attempt < 2 && sGl == NULL; attempt++) {
                SDL_GL_ResetAttributes();
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
#ifdef __APPLE__
                // (macOS gives only a 3.2 or later core context, and only forward-compatible: GLSL 150)
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
                glsl = "#version 150";
#else
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attempt == 0 ? 3 : 0);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, attempt == 0 ? SDL_GL_CONTEXT_PROFILE_CORE : 0);
                glsl = attempt == 0 ? "#version 330" : "#version 130";
#endif
                SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
                sWindow = SDL_CreateWindow(GAME " - Setup", 760, 560, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_OPENGL);
                sGl = sWindow != NULL ? SDL_GL_CreateContext(sWindow) : NULL;
                if (sGl == NULL && sWindow != NULL) {
                    SDL_DestroyWindow(sWindow);
                    sWindow = NULL;
                }
            }
            if (sGl == NULL) {
                fprintf(stderr, GAME "-setup: no window: %s%s%s\nThe same setup runs without a window: " GAME "-setup --install <disc image>\n",
                        why.c_str(), why.empty() ? "" : "; OpenGL: ", SDL_GetError());
                return 1;
            }
            if (!wantGl) {
                fprintf(stderr, GAME "-setup: %s; the window uses OpenGL\n", why.c_str());
            }
            SDL_GL_MakeCurrent(sWindow, sGl);
            SDL_GL_SetSwapInterval(1);
            sUseGl = true;
        } else {
            SDL_SetGPUSwapchainParameters(sDevice, sWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
        }
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    // The keyboard reaches every control: Tab and the arrows move, Space or Enter presses. Without this, Tab stops at
    // the text field only and no button can be pressed without a mouse.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    style();
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        if (file_exists(fonts[i])) {
            io.Fonts->AddFontFromFileTTF(fonts[i], 18.0f);
            break;
        }
    }
    if (sUseGl) {
        ImGui_ImplSDL3_InitForOpenGL(sWindow, sGl);
        ImGui_ImplOpenGL3_Init(glsl);
    } else {
        ImGui_ImplSDL3_InitForSDLGPU(sWindow);
        info.Device = sDevice;
        info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(sDevice, sWindow);
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        ImGui_ImplSDLGPU3_Init(&info);
    }
#ifdef __APPLE__
    if (sRelease) {
        // A folder downloaded from the internet is marked "quarantined" by macOS, and a quarantined program started by
        // another one is refused (posix_spawn: Bad file descriptor), the game included. This setup has been let through
        // already (it runs): the mark is taken off the whole folder, as README.txt's step 0 does by hand.
        const char *args[] = {"/usr/bin/xattr", "-dr", "com.apple.quarantine", sRoot.c_str(), NULL};
        SDL_Process *x = SDL_CreateProcess(args, false);
        if (x != NULL) {
            SDL_WaitProcess(x, true, NULL);
            SDL_DestroyProcess(x);
        }
    }
#endif
    if (sRelease && Native_Installed(sRoot) && getenv("BT3_SETUP_ISO") == NULL) { // nothing left to do: offer Play
        reset_rows();
        for (auto &r : sRows) {
            r.state = SKIPPED;
            r.detail = "already done";
        }
        sLauncher = sRoot + (file_exists((sRoot + GAME ".exe").c_str()) ? GAME ".exe" : GAME);
        sPage = PAGE_DONE;
    }
    if (argc > 1) {
        SDL_strlcpy(sIso, argv[1], sizeof(sIso));
        sChosen = true;
    }
    if (getenv("BT3_SETUP_EU_ISO") != NULL) { // (testing, with BT3_SETUP_ISO: the European image too)
        SDL_strlcpy(sIsoEu, getenv("BT3_SETUP_EU_ISO"), sizeof(sIsoEu));
    }
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (getenv("BT3_SETUP_ISO") != NULL) {
        SDL_strlcpy(sIso, getenv("BT3_SETUP_ISO"), sizeof(sIso));
        if (!sRoot.empty() && file_exists(sIso)) {
            start_install();
        }
    } else if (sRelease && sPage == PAGE_PICK) { // (by itself: the image given, else the one in the folder)
        if (file_exists(sIso)) {
            start_install();
        } else {
            look_for_disc();
        }
    }
    if (sPage == PAGE_DONE) {
        sShownPage = PAGE_PICK; // (installed already: told as a change, at the first frame)
    } else if (sPage != PAGE_RUN) {
        sShownPage = sPage;
    }
    while (!sQuit) {
        SDL_Event ev;
        PortA11y_Begin(); // (the first call starts the speaker, when prism.dll is there)
        while (SDL_PollEvent(&ev)) {
            if (page_key(ev)) {
                continue;
            }
            ImGui_ImplSDL3_ProcessEvent(&ev);
            PortA11y_Event(&ev); // (F2 says the last words again)
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                sQuit = true;
            } else if (ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                static bool first = true; // (the window's own first focus: its page was said already)
                if (!first) {
                    say_page();
                    sWatchAt = 0; // (and the folder looked at now: an image may have been copied meanwhile)
                }
                first = false;
            } else if (ev.type == SDL_EVENT_DROP_FILE && sPage == PAGE_PICK && ev.drop.data != NULL) {
                // (into the USA image's field, unless that one is filled already: then into the European one's)
                if (file_exists(sIso) && strcmp(sIso, ev.drop.data) != 0) {
                    SDL_strlcpy(sIsoEu, ev.drop.data, sizeof(sIsoEu));
                    news("European disc image: " + file_name(ev.drop.data));
                } else {
                    SDL_strlcpy(sIso, ev.drop.data, sizeof(sIso));
                    sChosen = true;
                    news("Disc image: " + file_name(ev.drop.data));
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(sDialogLock);
            if (sDialogReady) {
                if (sDialogFor) { // the European image: its field on the first page, or "Add languages..." on the last
                    SDL_strlcpy(sIsoEu, sDialogResult.c_str(), sizeof(sIsoEu));
                    news("European disc image: " + file_name(sDialogResult.c_str()));
                } else if (sPage == PAGE_PICK && !sNative && sProc == NULL) {
                    // (the USA image on the first page only: an install that started meanwhile is not credited to it)
                    SDL_strlcpy(sIso, sDialogResult.c_str(), sizeof(sIso));
                    sChosen = true;
                    news("Disc image: " + file_name(sDialogResult.c_str()));
                }
                sDialogReady = false;
                if (sDialogFor && sPage == PAGE_DONE && sRelease && !Native_Running()) { // "Add languages...": only that step
                    sEuOnly = true;
                    start_install();
                }
            }
        }
        pump_install();
        if (sPage != sShownPage) {
            Page from = sShownPage;
            sShownPage = sPage;
            ImGui::SetNavCursorVisible(false); // (a button Tab reached on the page before is not there: Enter is the page's)
            page_changed(from);
        }
        if (sPage == sShownPage) { // (never on a run's end that has not been told and counted yet)
            watch_folder();
        }
        if (sAutoPlayAt != 0 && SDL_GetTicks() >= sAutoPlayAt) {
            sAutoPlayAt = 0;
            if (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_INPUT_FOCUS) {
                launch_game();
            } else { // (Windows would not bring the game's window to the front from a setup behind others)
                Voice_Say(Voice_Tr("Enter opens the game, Escape closes."), true);
            }
        }
        if (sUseGl) {
            ImGui_ImplOpenGL3_NewFrame();
        } else {
            ImGui_ImplSDLGPU3_NewFrame();
        }
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        build_ui();
        ImGui::Render();
        ImDrawData *dd = ImGui::GetDrawData();
        if (sUseGl) {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(sWindow, &pw, &ph);
            glViewport(0, 0, pw, ph);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(dd);
            if (getenv("BT3_SETUP_SHOT") != NULL && ImGui::GetFrameCount() == 20 && pw > 0 && ph > 0) {
                // testing: the picture of the OpenGL window, <BT3_SETUP_SHOT>_gl.ppm (rows come bottom first)
                std::vector<unsigned char> px((size_t)pw * ph * 4);
                char name[512];
                glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                snprintf(name, sizeof(name), "%s_gl.ppm", getenv("BT3_SETUP_SHOT"));
                FILE *fp = fopen(name, "wb");
                if (fp != NULL) {
                    fprintf(fp, "P6\n%d %d\n255\n", pw, ph);
                    for (int y = ph - 1; y >= 0; y--) {
                        for (int x = 0; x < pw; x++) {
                            fwrite(&px[((size_t)y * pw + x) * 4], 1, 3, fp);
                        }
                    }
                    fclose(fp);
                }
            }
            SDL_GL_SwapWindow(sWindow);
            if (getenv("BT3_SETUP_QUIT") != NULL && ImGui::GetFrameCount() > 30) {
                sQuit = true; // testing: a few frames of the OpenGL window, then leave
            }
            continue;
        }
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sDevice);
        SDL_GPUTexture *swap = NULL;
        Uint32 w = 0, h = 0;
        if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, sWindow, &swap, &w, &h) && swap != NULL) {
            SDL_GPUColorTargetInfo target;
            ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
            SDL_zero(target);
            target.texture = swap;
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
            ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, pass);
            SDL_EndGPURenderPass(pass);
        }
        SDL_SubmitGPUCommandBuffer(cmd);
        if (getenv("BT3_SETUP_SHOT") != NULL && w != 0 && ImGui::GetFrameCount() > 3) {
            int running = 0;
            for (size_t i = 0; i < sRows.size(); i++) {
                running += sRows[i].state != PENDING;
            }
            int key = (int)sPage * 100 + running;
            if (key != shotKey) {
                shotKey = key;
                shot(dd, w, h, shots++);
            }
            if (getenv("BT3_SETUP_QUIT") != NULL && (sPage == PAGE_DONE || sPage == PAGE_FAILED || (sPage == PAGE_PICK && sProc == NULL && shots > 0 && getenv("BT3_SETUP_ISO") == NULL))) {
                shot(dd, w, h, shots++);
                sQuit = true; // testing: leave once the result is on screen
            }
        }
    }
    if (sProc != NULL) {
        SDL_KillProcess(sProc, false);
        SDL_DestroyProcess(sProc);
    }
    Native_Join();
    if (sUseGl) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(sGl);
        SDL_DestroyWindow(sWindow);
        SDL_Quit();
        return 0;
    }
    SDL_WaitForGPUIdle(sDevice);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(sDevice, sWindow);
    SDL_DestroyGPUDevice(sDevice);
    SDL_DestroyWindow(sWindow);
    SDL_Quit();
    return 0;
}
