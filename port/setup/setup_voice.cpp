// BT3 a11y: what the accessibility branch adds to the setup's screen reader, and the disc image found by itself.
//
// Speech: the game's screen reader (port/src/gs/a11y.c), which the setup is built with (build.sh) and which the
// setup's own reader (port/src/gs/ui_a11y.cpp) speaks through: one voice, its language and its on/off (a11y=0 in
// bt3_settings.txt or $BT3_A11Y=0). Voice_* only says what that reader does not: where to put the disc image, which
// one was found, the keys of each page.
//
// Words: an English text in the language spoken, from port/lang/a11y_<code>.txt (PortA11y_Tr). A text with a part of
// its own (a step's name and number, a file's name) has its line as a printf pattern, kPatterns, and its parts are
// said in the language too, as the game's settings window has them (port/src/gs/a11y_ui.cpp, tr).
//
// The disc image: the .iso files in the setup's folder and in the two folders above it (the zip unpacks into a folder
// of its own, and Windows' "Extract All" into one more: an image kept next to the zip is two folders up), those whose
// name says Tenkaichi or BT3 first, then the largest (the game's disc is the largest file a player keeps there).
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h" // the item hooks' types (port/src/gs/ui_a11y.cpp)
#include "setup_voice.h"

extern "C" int PortA11y_Begin(void);
extern "C" int PortA11y_On(void);
extern "C" void PortA11y_Say(const char *text, int flags);
extern "C" const char *PortA11y_Tr(const char *label);

// What the game links and the setup does not have (a11y.c asks for it): no controller names the buttons here, no
// online match, no game text with icon tags, no glossary of the fight's sounds.
extern "C" void PortA11y_PadUsed(const SDL_Event *ev) { (void)ev; }
extern "C" int Port_NetActive(void) { return 0; }
extern "C" int Port_NetMe(void) { return 0; }
extern "C" int PortA11y_GameTag(const uint16_t *at, char *out, int cap) { (void)at; (void)out; (void)cap; return 0; }
extern "C" void PortA11y_GlossaryStep(int step) { (void)step; }

// The settings window's hooks in the author's reader (ui_a11y.cpp: gs/a11y_ui.cpp in the game): the setup has no
// settings window, so they do nothing; and its words through Voice_Words. Four buffers in turn, as in the game.
void A11yUi_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &bb, const ImGuiLastItemData *data) {
    (void)ctx; (void)id; (void)bb; (void)data;
}

void A11yUi_ItemInfo(ImGuiContext *ctx, ImGuiID id, const char *label, ImGuiItemStatusFlags flags) {
    (void)ctx; (void)id; (void)label; (void)flags;
}

const char *A11yUi_Words(const char *en) {
    static std::string buf[4];
    static int next;
    std::string &out = buf[next++ % 4];
    out = en == NULL ? "" : Voice_Words(en);
    return out.c_str();
}

void Voice_Start(const std::string &root) {
    (void)root; // (a11y.c finds its files from the program's folder)
    PortA11y_Begin();
}

bool Voice_On(void) {
    return PortA11y_On() != 0;
}

void Voice_Say(const std::string &text, bool interrupt) {
    PortA11y_Say(text.c_str(), interrupt ? 1 : 0); // (A11Y_INTERRUPT)
}

const char *Voice_Tr(const char *en) {
    return PortA11y_Tr(en);
}

// The setup's texts with a part of their own (setup.cpp, native.cpp): each one's line is in a11y_<code>.txt. words:
// its %s parts are setup texts too (a step's name, what it found), said in the language; else names, said as they are.
static const struct { const char *en; bool words; } kPatterns[] = {
    {"%s, step %d of %d", true},
    {"%s done: %s", true},
    {"%s done", true},
    {"%s skipped: %s", true},
    {"%s skipped", true},
    {"%d%%, %d files", false},
    {"%d of %d", false},
    {"%d files in gamedata/", false},
    {"European disc image: %s", false},
    {"Disc image: %s", false},
    {"Found your disc image, %s", false},
    {"The game is installed and its demo fight ran correctly.  (%d min %d s)", false},
    {"Missing: %s, used for %s, package to install: %s.", false},
    {"Could not write %s.", false},
    {"Could not start python3: %s\nPython 3 has to be installed to set the game up.", false},
};

static const char *conversion_end(const char *p) { // p at the '%'
    p += 1 + strspn(p + 1, "-+ #0123456789.hlz");
    return *p ? p + 1 : p;
}

// Text against a pattern: cap[] gets what each %s (as little as lets the rest match) or %d matched; %% is a '%'.
static bool match(const char *p, const char *t, std::string *cap, char *kind, int *n) {
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
            if (saved >= 8) {
                return false;
            }
            p = conversion_end(p);
            kind[saved] = conv;
            if (conv == 's') {
                for (const char *e = t;; e++) {
                    *n = saved + 1;
                    if (match(p, e, cap, kind, n)) {
                        cap[saved].assign(t, e - t);
                        return true;
                    }
                    if (*e == '\0') {
                        *n = saved;
                        return false;
                    }
                }
            }
            size_t k = strspn(t + (*t == '-'), "0123456789");
            if (k == 0) {
                return false;
            }
            k += *t == '-';
            cap[saved].assign(t, k);
            *n = saved + 1;
            t += k;
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

std::string Voice_Words(const std::string &en) {
    std::string cap[8], out;
    char kind[8];
    int n;
    if (!Voice_On() || en.empty()) {
        return en;
    }
    for (auto &p : kPatterns) {
        if ((n = 0, !match(p.en, en.c_str(), cap, kind, &n))) {
            continue;
        }
        // the pattern's line (PortA11y_Tr takes it only with the same conversions, in the same order), its parts in
        // it: a setup text in the language too, a name or a number as it is
        int k = 0;
        for (const char *t = PortA11y_Tr(p.en); *t != '\0';) {
            if (t[0] == '%' && t[1] == '%') {
                out += '%';
                t += 2;
            } else if (t[0] == '%') {
                t = conversion_end(t);
                if (k < n) {
                    out += p.words && kind[k] == 's' ? Voice_Words(cap[k]) : cap[k];
                }
                k++;
            } else {
                out += *t++;
            }
        }
        return out;
    }
    return PortA11y_Tr(en.c_str());
}

std::vector<std::string> Voice_FindIsos(const std::string &root) {
    struct Found { std::string path; bool named; Sint64 size; };
    std::vector<Found> found;
    std::vector<std::string> out;
    std::string dir = root;

    for (int up = 0; up < 3 && !dir.empty(); up++) {
        int n = 0;
        char **names = SDL_GlobDirectory(dir.c_str(), "*.iso", SDL_GLOB_CASEINSENSITIVE, &n);
        for (int i = 0; names != NULL && i < n; i++) {
            std::string lower = names[i];
            SDL_PathInfo info;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
            if (SDL_GetPathInfo((dir + names[i]).c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
                found.push_back({dir + names[i], lower.find("tenkaichi") != std::string::npos || lower.find("bt3") != std::string::npos,
                                 (Sint64)info.size});
            }
        }
        SDL_free(names);
        // the folder above: dir without its last name ("C:\\Games\\BT3\\" -> "C:\\Games\\"; a root stays a root and ends it)
        size_t last = dir.find_last_of("/\\", dir.size() - 2);
        dir = last == std::string::npos || dir.size() < 2 ? "" : dir.substr(0, last + 1);
    }
    std::stable_sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        return a.named != b.named ? a.named : a.size > b.size;
    });
    for (auto &f : found) {
        out.push_back(f.path);
    }
    return out;
}
