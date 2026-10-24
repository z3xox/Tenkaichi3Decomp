// Wii action bindings. Keyboard/mouse and controller inputs are saved independently.
#include "control_bindings.h"
#include "ui.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <pthread.h>

extern "C" void Port_SettingSave(const char *name, int value);
namespace {
int sBindings[2][2][2][PORT_WII_ACTIONS]; /* player, split controls, device, action */
pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
struct Lock { Lock() { pthread_mutex_lock(&sLock); } ~Lock() { pthread_mutex_unlock(&sLock); } };
float sMouse[13], sMotion[2], sWheel[2];
int sPlayer, sSplit, sDevice, sAction;
bool sDetect;
Uint64 sArm, sUntil;
const int kMouseButtons[] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE, SDL_BUTTON_X1, SDL_BUTTON_X2};
const char *const kMouseNames[] = {"Mouse left", "Mouse right", "Mouse middle", "Mouse X1", "Mouse X2",
    "Mouse leftward", "Mouse rightward", "Mouse upward", "Mouse downward",
    "Wheel left", "Wheel right", "Wheel down", "Wheel up"};
const char *const kPadNames[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "Left stick click", "Right stick click",
    "Left shoulder", "Right shoulder", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Misc 1", "Right paddle 1", "Left paddle 1", "Right paddle 2", "Left paddle 2", "Touchpad"};
const char *const kAxisNames[] = {"Left stick left", "Left stick right", "Left stick up", "Left stick down",
    "Right stick left", "Right stick right", "Right stick up", "Right stick down"};

bool valid(int device, int input) {
    return input == -1 || (device ? (input >= 0 && input < SDL_GAMEPAD_BUTTON_COUNT) ||
        input == PORT_PAD_LT || input == PORT_PAD_RT || (input >= PORT_WII_AXIS && input < PORT_WII_AXIS + 8) :
        input > 0 && input < SDL_SCANCODE_COUNT + 13);
}
void defaults(int player, int split, int device, const int *keys, const int *pads) {
    static const int directions[] = {2, 3, 0, 1, 6, 7, 4, 5};
    for (int a = 0; a < PORT_WII_ACTIONS; a++) {
        sBindings[player][split][device][a] = a >= PORT_ACTIONS ? -1 : device ?
            (a < PORT_PAD_BUTTONS ? pads[a] : PORT_WII_AXIS + directions[a - PORT_PAD_BUTTONS]) :
            (keys[a] > 0 ? keys[a] : -1);
    }
}
void setting_name(char *name, int player, int split, int device, int action) {
    snprintf(name, 32, "wii_p%d_s%d_%c%02d", player + 1, split, device ? 'p' : 'k', action);
}
float value(int device, int input, SDL_Gamepad *pad, const bool *keys, const unsigned char *sticks, int (*blocked)(int)) {
    if (input < 0) { return 0; }
    if (!device) {
        if (!keys) { return 0; }
        if (input >= SDL_SCANCODE_COUNT) { return std::clamp(sMouse[input - SDL_SCANCODE_COUNT], 0.0f, 1.0f); }
        return keys[input] && !(blocked && blocked(input)) ? 1.0f : 0.0f;
    }
    if (!pad) { return 0; }
    if (input == PORT_PAD_LT || input == PORT_PAD_RT) {
        return std::max(0.0f, SDL_GetGamepadAxis(pad, input == PORT_PAD_LT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f);
    }
    if (input >= PORT_WII_AXIS) {
        int axis = input - PORT_WII_AXIS;
        return sticks ? std::max(0.0f, (sticks[axis / 2] - 128) / (axis % 2 ? 127.0f : -128.0f)) : 0;
    }
    return SDL_GetGamepadButton(pad, (SDL_GamepadButton)input) ? 1.0f : 0.0f;
}
void read(int player, int split, int device, SDL_Gamepad *pad, const bool *keys, const unsigned char *sticks,
          int (*blocked)(int), float *values) {
    for (int a = 0; a < PORT_WII_ACTIONS; a++) {
        values[a] = 0;
        for (int d = 0; d < 2; d++) {
            if (device < 0 || device == d) {
                values[a] = std::max(values[a], value(d, sBindings[player][split][d][a], pad, keys, sticks, blocked));
            }
        }
    }
}
SDL_Gamepad *pad_of(int player) {
    int count, slot = *PortInput_PadSlot(player);
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_Gamepad *pad = slot >= 0 && slot < count ? SDL_GetGamepadFromID(ids[slot]) : nullptr;
    SDL_free(ids);
    return pad;
}
void selected(int input) {
    PortWii_Set(sPlayer, sSplit, sDevice, sAction, input);
    PortInput_Save();
    PortInput_Swallow();
    PortInput_SwallowKeys();
    PortWii_Cancel();
}
}

extern "C" void PortWii_Init(int player, const int *keys, const int *pads) {
    Lock lock;
    char name[32];
    for (int split = 0; split < 2; split++) for (int device = 0; device < 2; device++) {
        defaults(player, split, device, keys, pads);
        for (int a = 0; a < PORT_WII_ACTIONS; a++) {
            setting_name(name, player, split, device, a);
            int input = Port_Setting(name, sBindings[player][split][device][a]);
            if (valid(device, input)) { sBindings[player][split][device][a] = input; }
        }
    }
}
extern "C" void PortWii_Reset(int player, int split, int device, const int *keys, const int *pads) {
    Lock lock;
    defaults(player, split, device, keys, pads);
}
extern "C" void PortWii_Save(void) {
    Lock lock;
    char name[32];
    for (int p = 0; p < 2; p++) for (int s = 0; s < 2; s++) for (int d = 0; d < 2; d++) {
        for (int a = 0; a < PORT_WII_ACTIONS; a++) {
            setting_name(name, p, s, d, a);
            Port_SettingSave(name, sBindings[p][s][d][a]);
        }
    }
}
extern "C" int PortWii_Get(int player, int split, int device, int action) {
    Lock lock;
    return sBindings[player][split][device][action];
}
extern "C" void PortWii_Set(int player, int split, int device, int action, int input) {
    Lock lock;
    if (valid(device, input)) { sBindings[player][split][device][action] = input; }
}
extern "C" const char *PortWii_InputName(int device, int input) {
    static char other[32];
    if (input < 0) { return "Unbound"; }
    if (!device) { return input < SDL_SCANCODE_COUNT ? SDL_GetScancodeName((SDL_Scancode)input) : kMouseNames[input - SDL_SCANCODE_COUNT]; }
    if (input == PORT_PAD_LT) { return "Left trigger"; }
    if (input == PORT_PAD_RT) { return "Right trigger"; }
    if (input >= PORT_WII_AXIS) { return kAxisNames[input - PORT_WII_AXIS]; }
    if (input < (int)(sizeof(kPadNames) / sizeof(*kPadNames))) { return kPadNames[input]; }
    snprintf(other, sizeof(other), "Button %d", input);
    return other;
}
extern "C" const char *PortWii_ActionLabel(int split, int action) {
    static const char *const labels[] = {"D-pad up", "D-pad down", "D-pad left", "D-pad right",
        "Flight", "Guard", "Attack", "Ki blast", "Descend", "Ascend", "Fast descend", "Fast ascend",
        "Stick click", "Charge ki", "Start / pause", "Select", "Up", "Down", "Left", "Right",
        "Tag", "Transform", "Camera left", "Camera right", "Previous teammate", "Next teammate", "Burst (double-tap)"};
    return labels[action];
}
extern "C" void PortWii_Read(int player, int split, SDL_Gamepad *pad, const bool *keys, const unsigned char *sticks,
                              int (*blocked)(int), float *values) {
    Lock lock;
    read(player, split, -1, pad, keys, sticks, blocked, values);
}
extern "C" int PortWii_KeysHeld(int player, int split, const bool *keys) {
    Lock lock;
    for (int a = 0; a < PORT_WII_ACTIONS; a++) {
        if (value(0, sBindings[player][split][0][a], nullptr, keys, nullptr, nullptr) > 0) { return 1; }
    }
    return 0;
}
extern "C" void PortWii_PreviewBindings(int player, int split, int device, float *values) {
    SDL_Gamepad *pad = pad_of(player);
    PortStickView view;
    PortInput_StickView(player, &view);
    unsigned char sticks[4] = {view.bytes[0][0], view.bytes[0][1], view.bytes[1][0], view.bytes[1][1]};
    Lock lock;
    read(player, split, device, pad, SDL_GetKeyboardState(nullptr), sticks, nullptr, values);
}
extern "C" void PortWii_MouseEvent(const SDL_Event *ev) {
    Lock lock;
    if (ev->type == SDL_EVENT_MOUSE_MOTION) { sMotion[0] += ev->motion.xrel; sMotion[1] += ev->motion.yrel; }
    if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        float sign = ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        sWheel[0] += ev->wheel.x * sign; sWheel[1] += ev->wheel.y * sign;
    }
}
extern "C" void PortWii_MouseFrame(void) {
    Lock lock;
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
    for (int i = 0; i < 5; i++) { sMouse[i] = (buttons & SDL_BUTTON_MASK(kMouseButtons[i])) ? 1 : 0; }
    for (int i = 0; i < 2; i++) {
        sMouse[5 + i * 2] = std::max(0.0f, -sMotion[i] / 8);
        sMouse[6 + i * 2] = std::max(0.0f, sMotion[i] / 8);
        sMouse[9 + i * 2] = std::max(0.0f, -sWheel[i]);
        sMouse[10 + i * 2] = std::max(0.0f, sWheel[i]);
        sMotion[i] = sWheel[i] = 0;
    }
}
extern "C" void PortWii_Cancel(void) {
    if (sDetect) { gPortInputCapture = 0; }
    sDetect = false;
}
extern "C" void PortWii_BindingButton(int player, int split, int device, int action, float value) {
    if (sDetect && SDL_GetTicks() > sUntil) { PortWii_Cancel(); }
    bool waiting = sDetect && sPlayer == player && sSplit == split && sDevice == device && sAction == action;
    char label[96];
    snprintf(label, sizeof(label), "%s##wii%d", waiting ? "Press an input..." : PortWii_InputName(device, PortWii_Get(player, split, device, action)), action);
    if (value > 0.5f) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.52f, 0.30f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.38f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.46f, 0.25f, 1));
    }
    if (ImGui::Button(label, ImVec2(-FLT_MIN, 0))) {
        sPlayer = player; sSplit = split; sDevice = device; sAction = action;
        sDetect = true; sArm = SDL_GetTicks() + 150; sUntil = sArm + 5000; gPortInputCapture = 1;
    }
    if (value > 0.5f) { ImGui::PopStyleColor(3); }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
        PortWii_Cancel(); PortWii_Set(player, split, device, action, -1); PortInput_Save();
    }
    ImGui::SetItemTooltip("Left-click: bind an input. Middle-click: clear. Esc cancels; Delete clears.\nInput: %.0f%% (active above 50%%).", value * 100);
    if (value > 0) {
        ImVec2 at = ImGui::GetItemRectMin(), end = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, end.y - 2), ImVec2(at.x + (end.x - at.x) * value, end.y),
                                               ImGui::GetColorU32(ImVec4(0.35f, 0.95f, 0.55f, 1)));
    }
}
extern "C" int PortWii_Event(const SDL_Event *ev) {
    if (!sDetect) { return 0; }
    if (SDL_GetTicks() > sUntil) { PortWii_Cancel(); return 0; }
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
        if (ev->key.scancode == SDL_SCANCODE_ESCAPE) { PortWii_Cancel(); return 1; }
        if (ev->key.scancode == SDL_SCANCODE_DELETE) { selected(-1); return 1; }
        if (!sDevice) { selected(ev->key.scancode); return 1; }
    }
    if (SDL_GetTicks() >= sArm) {
        if (!sDevice) {
            if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                for (int i = 0; i < 5; i++) if (ev->button.button == kMouseButtons[i]) { selected(SDL_SCANCODE_COUNT + i); break; }
            } else if (ev->type == SDL_EVENT_MOUSE_MOTION) {
                if (fabsf(ev->motion.xrel) > 8) { selected(SDL_SCANCODE_COUNT + 5 + (ev->motion.xrel > 0)); }
                else if (fabsf(ev->motion.yrel) > 8) { selected(SDL_SCANCODE_COUNT + 7 + (ev->motion.yrel > 0)); }
            } else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
                float sign = ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                if (ev->wheel.y) { selected(SDL_SCANCODE_COUNT + 11 + (ev->wheel.y * sign > 0)); }
                else if (ev->wheel.x) { selected(SDL_SCANCODE_COUNT + 9 + (ev->wheel.x * sign > 0)); }
            }
        } else {
            SDL_Gamepad *pad = pad_of(sPlayer);
            SDL_JoystickID id = pad ? SDL_GetGamepadID(pad) : 0;
            if (pad && ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev->gbutton.which == id) { selected(ev->gbutton.button); }
            if (pad && ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && ev->gaxis.which == id) {
                int axis = ev->gaxis.axis, v = ev->gaxis.value;
                if (axis < 4 && (v > 20000 || v < -20000)) { selected(PORT_WII_AXIS + axis * 2 + (v > 0)); }
                else if (axis >= 4 && axis < 6 && v > 20000) { selected(axis == 4 ? PORT_PAD_LT : PORT_PAD_RT); }
            }
        }
    }
    return ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT ||
           ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || ev->type == SDL_EVENT_GAMEPAD_BUTTON_UP || ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION ||
           ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL;
}
