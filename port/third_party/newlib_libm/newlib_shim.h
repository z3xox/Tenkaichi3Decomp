/*
 * Forced in front of the newlib 1.10.0 float maths sources (the library the PS2 executable was linked with:
 * same functions, thresholds and constants in its disassembly). The sources are unmodified; this supplies the
 * few newlib-internal macros and types they expect, so they build with the host's headers. They are compiled
 * with software float like the game, so their arithmetic is the PS2 float model.
 */
#ifndef NEWLIB_SHIM_H
#define NEWLIB_SHIM_H
#include <stdint.h>
#include <math.h>
#include "port_libm.h"
#define _IEEE_LIBM 1
#define _DEFUN(name, arglist, args) name(args)
#define _AND ,
#define _CONST const
#define _PARAMS(paramlist) paramlist
#define _EXFUN(name, proto) name proto
#ifndef __int32_t
#define __int32_t int32_t
#define __uint32_t uint32_t
#endif
/* internal names used across the files */
#define fabsf Port_fabsf
#define scalbnf Port_scalbnf
#define copysignf Port_copysignf
#ifdef __EMSCRIPTEN__
/* <math.h> above declares scalbnf before the rename, so calls of Port_scalbnf (ef_pow.c, kf_rem_pio2.c) are implicit
   `int Port_scalbnf()` declarations: the float argument is passed as a double and the result read as an int. The
   PS2's newlib had the prototype. The browser build needs it (a call must match its definition there); the native
   builds are left as they are (see BROWSER_PORT_STATUS.md). */
float Port_scalbnf(float, int);
#endif
#endif
