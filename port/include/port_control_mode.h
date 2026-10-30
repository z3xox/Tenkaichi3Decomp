#ifndef PORT_CONTROL_MODE_H
#define PORT_CONTROL_MODE_H

/* The last pressure byte is unused by the PC pad; keep the layout with its sample. */
#define PORT_CONTROL_MODE_BYTE 17
#define PORT_CONTROL_MODE_WII 0x57
#define PORT_CONTROL_MODE_SPLIT 0x58
#define PORT_CONTROL_IS_WII(data) ((data)[PORT_CONTROL_MODE_BYTE] == PORT_CONTROL_MODE_WII || \
                                 (data)[PORT_CONTROL_MODE_BYTE] == PORT_CONTROL_MODE_SPLIT)
/* Dedicated held actions, in an otherwise unused pressure byte of the same 18-byte sample. */
#define PORT_CONTROL_EXTRA_BYTE 6
#define PORT_CONTROL_PREV 1
#define PORT_CONTROL_NEXT 2
#define PORT_CONTROL_BURST 4
/* The unused battle-button bit keeps the layout with recorded battle input. */
#define PORT_BTL_WII 0x80000000u
#define PORT_BTL_SPLIT 0x00010000u /* RS_UP is unused after Wii conversion. */

#endif
