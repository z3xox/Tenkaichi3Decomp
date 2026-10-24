#ifndef BT3_RA_GAME_H
#define BT3_RA_GAME_H
#ifdef __cplusplus
extern "C" {
#endif
void PortRa_Init(void); /* before the render thread starts */
void PortRa_Tick(void); /* game thread only, once per vertical blank */
/* Game-thread object lifecycle hook, including address reuse between frames. */
void PortRa_TransientReset(void);
/* Game thread: must succeed BEFORE Unlock Everything mutates the save.
 * Refuses active RA sessions and requires a durable taint marker in BT3_SAVES. */
int PortRa_BeforeUnlockAll(void);
#ifdef __cplusplus
}
#endif
#endif
