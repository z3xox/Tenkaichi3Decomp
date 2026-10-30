#ifndef PORT_CONTROL_BINDINGS_H
#define PORT_CONTROL_BINDINGS_H
#include <SDL3/SDL.h>
#define PORT_WII_ACTIONS 27
#define PORT_WII_AXIS 200 /* signed stick directions, in SDL axis order */
#ifdef __cplusplus
extern "C" {
#endif
void PortWii_Init(int player, const int *keys, const int *pads);
void PortWii_Reset(int player, int split, int device, const int *keys, const int *pads);
void PortWii_Save(void);
int PortWii_Get(int player, int split, int device, int action);
void PortWii_Set(int player, int split, int device, int action, int input);
const char *PortWii_InputName(int device, int input);
const char *PortWii_ActionLabel(int split, int action);
void PortWii_Read(int player, int split, SDL_Gamepad *pad, const bool *keys, const unsigned char *sticks,
                  int (*blocked_key)(int), float *values);
int PortWii_KeysHeld(int player, int split, const bool *keys);
void PortWii_PreviewBindings(int player, int split, int device, float *values);
void PortWii_BindingButton(int player, int split, int device, int action, float value);
void PortWii_MouseEvent(const SDL_Event *ev);
void PortWii_MouseFrame(void);
int PortWii_Event(const SDL_Event *ev);
void PortWii_Cancel(void);
#ifdef __cplusplus
}
#endif
#endif
