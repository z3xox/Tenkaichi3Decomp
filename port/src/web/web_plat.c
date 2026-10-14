/*
 * Browser build only (port/tools/wasm_build.py): definitions the WebAssembly target needs and the native ones do not.
 */
#include <stdint.h>

/* Overlay_Load (src/main.c) reads the menu overlay to the address of its first function, MainMenu_StartBallLoops,
   declared there as a byte array. The port links the overlay in and its sceRead (headless.c) does not write, so only
   the address is used. On WebAssembly a function is not addressable as data (wasm-ld: "symbol type mismatch"), so
   main.c is compiled with that name mapped to this array. */
uint8_t gPortOverlayLoadArea[16];

/* src/sys/heap.c sizes the game heap from the address of D_3BE71C, a symbol the native link defines at the absolute
   address 0x3BE71C (link.py: --defsym). wasm-ld cannot place a symbol at an absolute address, so heap.c is compiled
   with D_3BE71C mapped to (*gPortAbs3BE71C): the declaration `extern u8 D_3BE71C[]` then declares this pointer to an
   array, and every use yields the address 0x3BE71C as before. */
uint8_t (*gPortAbs3BE71C)[] = (uint8_t (*)[])(uintptr_t)0x3BE71Cu;
