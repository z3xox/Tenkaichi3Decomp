#ifndef SYS_COLOR_H
#define SYS_COLOR_H

#include "types.h"

/* Four integer colour channels, with named and indexed component access. */
typedef union Color4i {
    struct {
        s32 r, g, b, a;
    };
    s32 v[4];
} Color4i;

#endif
