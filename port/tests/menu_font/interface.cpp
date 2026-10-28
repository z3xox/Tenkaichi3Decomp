// Actual ImGui widgets: UTF-8, stable IDs while switching languages, and untouched user input.
// Build after interface_strings.py and port/setup/build.sh:
// g++ -std=c++17 -g -fsanitize=address,undefined -Iport/third_party/imgui -Iport/src/gs \
//   -Iport/build/gen/gs port/tests/menu_font/interface.cpp port/build/setup/imgui.o \
//   port/build/setup/imgui_draw.o port/build/setup/imgui_tables.o port/build/setup/imgui_widgets.o \
//   -o port/build/interface_test
#include "ui_localization.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #test); std::exit(1); } } while (0)

int main() {
    static const char *play[] = {"Play", "Jugar", "Jouer", "Spielen", "Gioca", "Juga"};
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 800);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(700, 700));
    ImGui::Begin("test");
    ImGuiID browse = ImGui::GetID(Bt3Ui::Label("Browse...##eu"));
    ImGuiID settings = ImGui::GetID(Bt3Ui::Label("Settings"));
    char deadzoneLabel[128];
    std::snprintf(deadzoneLabel, sizeof(deadzoneLabel), Bt3Ui::Label("Deadzone##%d"), 0);
    ImGuiID deadzone = ImGui::GetID(deadzoneLabel);
    for (int i = 0; i < Bt3Ui::LanguageCount(); i++) {
        CHECK(Bt3Ui::SetLocale(kBt3UiLanguages[i].locale));
        CHECK(std::strcmp(Bt3Ui::T("Play"), play[i]) == 0);
        CHECK(ImGui::GetID(Bt3Ui::Label("Browse...##eu")) == browse);
        CHECK(ImGui::GetID(Bt3Ui::Label("Settings")) == settings);
        std::snprintf(deadzoneLabel, sizeof(deadzoneLabel), Bt3Ui::Label("Deadzone##%d"), 0);
        CHECK(ImGui::GetID(deadzoneLabel) == deadzone);
        CHECK(std::strcmp(Bt3Ui::T("a player name not in the catalog"), "a player name not in the catalog") == 0);
        CHECK(!Bt3Ui::SetLocale("unknown") && std::strcmp(Bt3Ui::Locale(), kBt3UiLanguages[i].locale) == 0);
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(600);
        float available = ImGui::GetContentRegionAvail().x;
        int value = 0;
        Bt3Ui::InputInt("Dialogue / UI font", &value);
        CHECK(ImGui::GetItemRectSize().x <= available + 1);
        char input[32] = "Play";
        Bt3Ui::InputTextWithHint("Name", "Player 1", input, sizeof(input));
        CHECK(std::strcmp(input, "Play") == 0);
        Bt3Ui::Button("Play", ImVec2(1, 0));
        CHECK(ImGui::GetItemRectSize().x >= ImGui::CalcTextSize(play[i]).x + ImGui::GetStyle().FramePadding.x * 2);
        ImGui::LogToBuffer();
        Bt3Ui::Text("Looking for %s...", "Play");
        char expected[512];
        std::snprintf(expected, sizeof(expected), Bt3Ui::T("Looking for %s..."), "Play");
        CHECK(std::strstr(GImGui->LogBuffer.c_str(), expected) != nullptr);
        ImGui::LogFinish();
        const char *items[] = {"Original", "Smooth"};
        Bt3Ui::ComboItems array{items, nullptr}, zeroes{nullptr, "Original\0Smooth\0"};
        CHECK(std::strcmp(Bt3Ui::ComboItem(&array, 1), Bt3Ui::T("Smooth")) == 0);
        CHECK(std::strcmp(Bt3Ui::ComboItem(&zeroes, 1), Bt3Ui::T("Smooth")) == 0);
        CHECK(Bt3Ui::OpenPopup("Exit game"));
        CHECK(Bt3Ui::IsPopupOpen("Exit game"));
        if (Bt3Ui::BeginPopupModal("Exit game")) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
        ImGui::PopID();
    }
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
    std::puts("six interface languages: UTF-8, widget IDs, names/input preserved, combo choices, and popup identity OK");
}
