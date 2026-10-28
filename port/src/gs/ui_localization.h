// Shared presentation layer for the settings and setup windows.
// Catalogs are compiled so the setup can be translated before any game data exists.
#ifndef BT3_UI_LOCALIZATION_H
#define BT3_UI_LOCALIZATION_H
#include "imgui.h"
#include "ui_catalog_generated.h"
#include <atomic>
#include <cstring>
#include <utility>

namespace Bt3Ui {
inline std::atomic<int> &LanguageIndex() { static std::atomic<int> value{0}; return value; }
inline int LanguageCount() { return int(sizeof(kBt3UiLanguages) / sizeof(kBt3UiLanguages[0])); }
inline const char *Locale() { return kBt3UiLanguages[LanguageIndex().load()].locale; }
inline bool SetLocale(const char *locale) {
    if (locale) for (int i = 0; i < LanguageCount(); i++) {
        if (std::strcmp(locale, kBt3UiLanguages[i].locale) == 0) { LanguageIndex().store(i); return true; }
    }
    return false;
}
inline const Bt3UiRow *Find(const char *original) {
    if (!original) return nullptr;
    int lo = 0, hi = int(sizeof(kBt3UiRows) / sizeof(kBt3UiRows[0]));
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2, comparison = std::strcmp(original, kBt3UiRows[mid].original);
        if (!comparison) return &kBt3UiRows[mid];
        if (comparison < 0) hi = mid; else lo = mid + 1;
    }
    return nullptr;
}
inline const char *T(const char *original) {
    const auto *row = Find(original);
    return row ? row->text[LanguageIndex().load()] : original;
}
inline const char *Label(const char *original) {
    const auto *row = Find(original);
    return row ? row->label[LanguageIndex().load()] : original;
}

// Never translate printf arguments: player names, paths and messages typed in chat are data.
template<typename... Args> inline void Text(const char *fmt, Args... args) { ImGui::Text(T(fmt), args...); }
template<typename... Args> inline void TextWrapped(const char *fmt, Args... args) { ImGui::TextWrapped(T(fmt), args...); }
template<typename... Args> inline void TextDisabled(const char *fmt, Args... args) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(T(fmt), args...);
    ImGui::PopTextWrapPos();
}
template<typename... Args> inline void TextColored(const ImVec4 &color, const char *fmt, Args... args) { ImGui::TextColored(color, T(fmt), args...); }
template<typename... Args> inline void SetItemTooltip(const char *fmt, Args... args) { ImGui::SetItemTooltip(T(fmt), args...); }
template<typename... Args> inline void SetTooltip(const char *fmt, Args... args) { ImGui::SetTooltip(T(fmt), args...); }
inline void TextUnformatted(const char *text) { ImGui::TextUnformatted(T(text)); }
inline ImVec2 CalcTextSize(const char *text) { return ImGui::CalcTextSize(T(text)); }

inline float ButtonWidth(const char *label, float minimum = 0.0f) {
    float width = ImGui::CalcTextSize(Label(label), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    return width > minimum ? width : minimum;
}
inline bool Button(const char *label, ImVec2 size = ImVec2(0, 0)) {
    if (size.x > 0) size.x = ButtonWidth(label, size.x);
    return ImGui::Button(Label(label), size);
}

inline void FitItem(const char *label) {
    float textWidth = ImGui::CalcTextSize(Label(label), nullptr, true).x;
    float maximum = ImGui::GetContentRegionAvail().x - textWidth - ImGui::GetStyle().ItemInnerSpacing.x;
    if (maximum >= 80.0f && ImGui::CalcItemWidth() > maximum) ImGui::SetNextItemWidth(maximum);
}

#define BT3_UI_WIDGET(name, result) \
template<typename... Args> inline result name(const char *label, Args&&... args) { \
    return ImGui::name(Label(label), std::forward<Args>(args)...); \
}
BT3_UI_WIDGET(Checkbox, bool)
BT3_UI_WIDGET(RadioButton, bool)
BT3_UI_WIDGET(Selectable, bool)
BT3_UI_WIDGET(CollapsingHeader, bool)
BT3_UI_WIDGET(IsPopupOpen, bool)
BT3_UI_WIDGET(TableSetupColumn, void)
#undef BT3_UI_WIDGET

#define BT3_UI_SIZED_WIDGET(name) \
template<typename... Args> inline bool name(const char *label, Args&&... args) { \
    FitItem(label); return ImGui::name(Label(label), std::forward<Args>(args)...); \
}
BT3_UI_SIZED_WIDGET(SliderInt)
BT3_UI_SIZED_WIDGET(SliderFloat)
BT3_UI_SIZED_WIDGET(InputInt)
BT3_UI_SIZED_WIDGET(InputText)
#undef BT3_UI_SIZED_WIDGET

inline bool Begin(const char *name, bool *open = nullptr, ImGuiWindowFlags flags = 0) {
    return ImGui::Begin(Label(name), open, flags);
}
inline bool BeginPopupModal(const char *name, bool *open = nullptr, ImGuiWindowFlags flags = 0) {
    return ImGui::BeginPopupModal(Label(name), open, flags);
}
inline bool BeginTabItem(const char *name, bool *open = nullptr, ImGuiTabItemFlags flags = 0) {
    return ImGui::BeginTabItem(Label(name), open, flags);
}
inline bool OpenPopup(const char *name, ImGuiPopupFlags flags = 0) { return ImGui::OpenPopup(Label(name), flags); }

template<typename... Args> inline bool BeginCombo(const char *label, const char *preview, Args&&... args) {
    FitItem(label);
    return ImGui::BeginCombo(Label(label), T(preview), std::forward<Args>(args)...);
}
template<typename... Args> inline bool InputTextWithHint(const char *label, const char *hint, Args&&... args) {
    FitItem(label);
    return ImGui::InputTextWithHint(Label(label), T(hint), std::forward<Args>(args)...);
}
struct ComboItems { const char *const *array; const char *zeroes; };
inline const char *ComboItem(void *data, int index) {
    auto *items = static_cast<ComboItems *>(data);
    if (items->array) return T(items->array[index]);
    const char *text = items->zeroes;
    while (index-- > 0) text += std::strlen(text) + 1;
    return T(text);
}
inline bool Combo(const char *label, int *choice, const char *const *items, int count, int height = -1) {
    FitItem(label);
    ComboItems data{items, nullptr};
    return ImGui::Combo(Label(label), choice, ComboItem, &data, count, height);
}
inline bool Combo(const char *label, int *choice, const char *items, int height = -1) {
    FitItem(label);
    int count = 0;
    for (const char *p = items; *p; p += std::strlen(p) + 1) count++;
    ComboItems data{nullptr, items};
    return ImGui::Combo(Label(label), choice, ComboItem, &data, count, height);
}
} // namespace Bt3Ui
#endif
