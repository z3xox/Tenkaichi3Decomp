#ifndef SYS_INT128_H
#define SYS_INT128_H

#include "types.h"

/* EE integer quadwords; the host keeps the same packed storage. */
#ifdef PORT
/* PC build: no 128-bit integer on a 32-bit host; the game only copies and stores these. */
typedef struct { u64 lo, hi; } __attribute__((aligned(16))) s128;
typedef struct { u64 lo, hi; } __attribute__((aligned(16))) u128;
#else
typedef int s128 __attribute__((mode(TI)));
typedef unsigned int u128 __attribute__((mode(TI)));
#endif

#endif
