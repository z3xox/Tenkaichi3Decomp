/* Plain C float operations, compiled the way the game is in the WebAssembly build (through port/tools/wasm_sf.py).
   Every function takes and returns bit patterns, so the harness (driver.c) can compare them with the routines. */
#include <stdint.h>
#include <string.h>

static float F(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t U(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

uint32_t op_add(uint32_t a, uint32_t b) { return U(F(a) + F(b)); }
uint32_t op_sub(uint32_t a, uint32_t b) { return U(F(a) - F(b)); }
uint32_t op_mul(uint32_t a, uint32_t b) { return U(F(a) * F(b)); }
uint32_t op_div(uint32_t a, uint32_t b) { return U(F(a) / F(b)); }
uint32_t op_madd(uint32_t a, uint32_t b) { return U(F(a) + F(a) * F(b)); } /* fp-contract off: two operations */
uint32_t op_neg(uint32_t a) { return U(-F(a)); }
uint32_t op_fabs(uint32_t a) { return U(__builtin_fabsf(F(a))); }

/* comparisons: one bit each, so the harness checks all predicates in one call */
uint32_t op_cmp(uint32_t a, uint32_t b) {
    float x = F(a), y = F(b);
    return (x < y) | (x <= y) << 1 | (x > y) << 2 | (x >= y) << 3 | (x == y) << 4 | (x != y) << 5 |
           !(x < y) << 6 | !(x <= y) << 7 | !(x > y) << 8 | !(x >= y) << 9 |      /* the unordered predicates */
           __builtin_isunordered(x, y) << 10 | !__builtin_isunordered(x, y) << 11 |
           __builtin_isless(x, y) << 12 | __builtin_isgreaterequal(x, y) << 13;
}
uint32_t op_min(uint32_t a, uint32_t b) { return U(F(a) < F(b) ? F(a) : F(b)); } /* select on a comparison */

/* constant operands (the game's code is full of them; LLVM spells them differently from values) */
uint32_t op_kadd(uint32_t a) { return U(F(a) + 0x1.99999ap-4f); }  /* 0x3DCCCCCD */
uint32_t op_ksub(uint32_t a) { return U(0x1.4p+3f - F(a)); }       /* 0x41200000 - a */
uint32_t op_kmul(uint32_t a) { return U(F(a) * 0x1.921fb4p+0f); }  /* 0x3FC90FDA */
uint32_t op_kdiv(uint32_t a) { return U(F(a) / 0x1.8p+1f); }       /* 0x40400000 */
uint32_t op_kcmp(uint32_t a) {
    float x = F(a);
    return (0x1.06249cp-10f < x) | (x == -0x1.921fb4p+2f) << 1 | (x > -0x1.921fb4p+2f) << 2; /* 0x3A83124E, 0xC0C90FDA */
}
int32_t op_kftoi(uint32_t a) { return (int32_t)(F(a) * 0x1.4p+3f); }

/* conversions */
int32_t op_ftoi(uint32_t a) { return (int32_t)F(a); }
uint32_t op_ftou(uint32_t a) { return (uint32_t)F(a); }
int32_t op_ftos16(uint32_t a) { return (int16_t)F(a); }
int32_t op_ftou8(uint32_t a) { return (uint8_t)F(a); }
uint32_t op_itof(int32_t v) { return U((float)v); }
uint32_t op_utof(uint32_t v) { return U((float)v); }
uint32_t op_s16tof(int32_t v) { return U((float)(int16_t)v); }
uint32_t op_u8tof(int32_t v) { return U((float)(uint8_t)v); }

/* bit patterns must travel unchanged through memory, locals, select and calls (no arithmetic) */
static volatile float sKeep;
__attribute__((noinline)) static float pass2(float x, float y, int c) { return c ? x : y; }
uint32_t op_pass(uint32_t a, int c) {
    sKeep = F(a);
    return U(pass2(sKeep, F(a ^ 1), c));
}
