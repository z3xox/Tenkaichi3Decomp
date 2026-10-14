/* Test driver for the WebAssembly software-float lowering (port/tools/wasm_sf.py). Built WITHOUT the lowering; it
   compares the plain C operations of ops.c (built WITH it) against
     1. the routines of port/src/softfloat_ps2.c called directly, bit for bit, on special patterns (every pair),
        on random pairs, and on every predicate and conversion;
     2. PCSX2's own results (port/tests/ps2float/pcsx2_v2.7.303.txt, generated into vectors.h by run.py);
     3. a few hand-worked PS2 results that differ from IEEE.
   Prints a summary per operation and exits with 1 on any difference. */
#include <stdint.h>
#include <stdio.h>
#include "vectors.h"

uint32_t __addsf3(uint32_t, uint32_t), __subsf3(uint32_t, uint32_t), __mulsf3(uint32_t, uint32_t), __divsf3(uint32_t, uint32_t);
int __ltsf2(uint32_t, uint32_t);
int32_t __fixsfsi(uint32_t);
uint32_t __fixunssfsi(uint32_t), __floatsisf(int32_t), __floatunsisf(uint32_t);

uint32_t op_add(uint32_t, uint32_t), op_sub(uint32_t, uint32_t), op_mul(uint32_t, uint32_t), op_div(uint32_t, uint32_t);
uint32_t op_madd(uint32_t, uint32_t), op_neg(uint32_t), op_fabs(uint32_t), op_cmp(uint32_t, uint32_t), op_min(uint32_t, uint32_t);
int32_t op_ftoi(uint32_t), op_ftos16(uint32_t), op_ftou8(uint32_t);
uint32_t op_ftou(uint32_t), op_itof(int32_t), op_utof(uint32_t), op_s16tof(int32_t), op_u8tof(int32_t), op_pass(uint32_t, int);
uint32_t op_kadd(uint32_t), op_ksub(uint32_t), op_kmul(uint32_t), op_kdiv(uint32_t), op_kcmp(uint32_t);
int32_t op_kftoi(uint32_t);

static const char *const kName[] = {"add", "sub", "mul", "div", "madd", "neg", "fabs", "cmp", "min", "ftoi", "ftou",
                                    "ftos16", "ftou8", "itof", "utof", "s16tof", "u8tof", "pass", "const"};
enum { N_OPS = sizeof(kName) / sizeof(kName[0]) };
static unsigned long sTests[N_OPS], sBad[N_OPS];

static void check(int op, uint32_t a, uint32_t b, uint32_t got, uint32_t want) {
    sTests[op]++;
    if (got != want && sBad[op]++ < 3) {
        printf("  MISMATCH %s a=%08x b=%08x: lowered %08x, routine %08x\n", kName[op], a, b, got, want);
    }
}

static uint32_t want_cmp(uint32_t a, uint32_t b) {
    int c = __ltsf2(a, b);
    return (c < 0) | (c <= 0) << 1 | (c > 0) << 2 | (c >= 0) << 3 | (c == 0) << 4 | (c != 0) << 5 | !(c < 0) << 6 |
           !(c <= 0) << 7 | !(c > 0) << 8 | !(c >= 0) << 9 | 0 << 10 | 1 << 11 | (c < 0) << 12 | (c >= 0) << 13;
}

static void pair(uint32_t a, uint32_t b) {
    check(0, a, b, op_add(a, b), __addsf3(a, b));
    check(1, a, b, op_sub(a, b), __subsf3(a, b));
    check(2, a, b, op_mul(a, b), __mulsf3(a, b));
    check(3, a, b, op_div(a, b), __divsf3(a, b));
    check(4, a, b, op_madd(a, b), __addsf3(a, __mulsf3(a, b)));
    check(7, a, b, op_cmp(a, b), want_cmp(a, b));
    check(8, a, b, op_min(a, b), __ltsf2(a, b) < 0 ? a : b);
}

static void single(uint32_t a) {
    int32_t v = (int32_t)a;
    check(5, a, 0, op_neg(a), a ^ 0x80000000u);
    check(6, a, 0, op_fabs(a), a & 0x7FFFFFFFu);
    check(9, a, 0, (uint32_t)op_ftoi(a), (uint32_t)__fixsfsi(a));
    check(10, a, 0, op_ftou(a), __fixunssfsi(a));
    check(11, a, 0, (uint32_t)op_ftos16(a), (uint32_t)(int32_t)(int16_t)__fixsfsi(a));
    check(12, a, 0, (uint32_t)op_ftou8(a), (uint32_t)(uint8_t)__fixsfsi(a));
    check(13, a, 0, op_itof(v), __floatsisf(v));
    check(14, a, 0, op_utof(a), __floatunsisf(a));
    check(15, a, 0, op_s16tof(v), __floatsisf((int16_t)v));
    check(16, a, 0, op_u8tof(v), __floatsisf((uint8_t)v));
    {
        int c1 = __ltsf2(0x3A83124Eu, a), c2 = __ltsf2(a, 0xC0C90FDAu);
        check(18, a, 0, op_kadd(a), __addsf3(a, 0x3DCCCCCDu));
        check(18, a, 1, op_ksub(a), __subsf3(0x41200000u, a));
        check(18, a, 2, op_kmul(a), __mulsf3(a, 0x3FC90FDAu));
        check(18, a, 3, op_kdiv(a), __divsf3(a, 0x40400000u));
        check(18, a, 4, op_kcmp(a), (c1 < 0) | (c2 == 0) << 1 | (c2 > 0) << 2);
        check(18, a, 5, (uint32_t)op_kftoi(a), (uint32_t)__fixsfsi(__mulsf3(a, 0x41200000u)));
    }
    check(17, a, 0, op_pass(a, 1), a);
    check(17, a, 0, op_pass(a, 0), a ^ 1);
}

static const uint32_t kSpecial[] = {
    0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x00800000, 0x80800000, 0x3F800000, 0xBF800000, 0x3F7FFFFF,
    0x3F800001, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001, 0xFFFFFFFF, 0x7FFFFFFF,
    0x4B000000, 0x4AFFFFFF, 0x4F000000, 0x4F800000, 0xCF000000, 0xCF000001, 0x4EFFFFFF, 0x3F000000, 0x3EAAAAAB,
    0x40490FDB, 0x3DCCCCCC, 0x47000000, 0x477FFF00, 0x437F0000, 0x43800000, 0xC3800000, 0xC7000080, 0x30800000,
    0xB0800000, 0x01000000, 0x7E800000, 0x3FFFFFFF, 0xBFFFFFFF, 0x34000000, 0x33FFFFFF,
};
enum { N_SPECIAL = sizeof(kSpecial) / sizeof(kSpecial[0]) };

static uint32_t sSeed = 0x2545F491u;
static uint32_t rnd(void) { sSeed ^= sSeed << 13; sSeed ^= sSeed >> 17; sSeed ^= sSeed << 5; return sSeed; }

int main(void) {
    int i, j, op, fail = 0;
    unsigned long pcsx2 = 0, pcsx2Bad = 0;

    /* 1. special patterns, every pair and every single */
    for (i = 0; i < N_SPECIAL; i++) {
        single(kSpecial[i]);
        for (j = 0; j < N_SPECIAL; j++) {
            pair(kSpecial[i], kSpecial[j]);
        }
    }
    /* random: half with exponents near each other (real rounding work), some with any bits at all */
    for (i = 0; i < 1000000; i++) {
        uint32_t a = rnd(), b = rnd();
        if (i % 4 != 3) {
            uint32_t ea = 96 + rnd() % 64;
            a = (a & 0x807FFFFFu) | ea << 23;
            b = (b & 0x807FFFFFu) | (ea + rnd() % 40 - 20) << 23;
        }
        pair(a, b);
        single(a);
        single(rnd() >> (rnd() % 32)); /* integers of every size for the conversions */
    }
    /* 2. PCSX2: a b n fpu_add fpu_sub fpu_mul fpu_div fpu_sqrt fpu_madd fpu_itof fpu_ftoi (columns of check.py) */
    for (i = 0; i < N_VECTORS; i++) {
        const uint32_t *v = kVectors[i];
        uint32_t a = v[0], b = v[1], ea = (a >> 23) & 0xFF;
        uint32_t ftoiIn = (a & 0x807FFFFFu) | ((120 + (ea & 31)) << 23);
        uint32_t got[6] = {op_add(a, b), op_sub(a, b), op_mul(a, b), op_div(a, b), op_madd(a, b), op_itof((int32_t)v[2])};
        uint32_t want[6] = {v[3], v[4], v[5], v[6], v[8], v[9]};
        for (j = 0; j < 6; j++) {
            pcsx2++;
            if (got[j] != want[j] && pcsx2Bad++ < 5) {
                printf("  PCSX2 MISMATCH case %d column %d: lowered %08x, PCSX2 %08x\n", i, j, got[j], want[j]);
            }
        }
        pcsx2++;
        if ((uint32_t)op_ftoi(ftoiIn) != v[10] && pcsx2Bad++ < 5) {
            printf("  PCSX2 MISMATCH case %d ftoi: lowered %08x, PCSX2 %08x\n", i, (uint32_t)op_ftoi(ftoiIn), v[10]);
        }
    }
    /* 3. hand-worked PS2 results (each differs from what IEEE arithmetic gives) */
    {
        struct { const char *what; uint32_t got, want; } t[] = {
            {"1 / 0 = +max", op_div(0x3F800000, 0), 0x7F7FFFFF},
            {"-1 / 0 = -max", op_div(0xBF800000, 0), 0xFF7FFFFF},
            {"0 / 0 = +max", op_div(0, 0), 0x7F7FFFFF},
            {"inf pattern + 0 = max", op_add(0x7F800000, 0), 0x7F7FFFFF},
            {"max * 2 = max (no overflow to inf)", op_mul(0x7F7FFFFF, 0x40000000), 0x7F7FFFFF},
            {"denormal + 0 = +0", op_add(0x00000001, 0), 0x00000000},
            {"-0 + -0 = -0", op_add(0x80000000, 0x80000000), 0x80000000},
            {"FPU add, gap 24: 1 - 1.5*2^-24 = 0x3F7FFFFF (IEEE: 0x3F7FFFFE)", op_add(0x3F800000, 0xB3C00000), 0x3F7FFFFF},
            {"FPU add, gap >= 25 drops the small operand: 1 - 2^-30 = 1.0", op_add(0x3F800000, 0xB0800000), 0x3F800000},
            {"1 / 3 rounds to nearest (div.s)", op_div(0x3F800000, 0x40400000), 0x3EAAAAAB},
            {"NaN pattern > 1 is true (an ordinary number)", (op_cmp(0x7FC00000, 0x3F800000) >> 2) & 1, 1},
            {"NaN pattern == itself", (op_cmp(0x7FC00000, 0x7FC00000) >> 4) & 1, 1},
            {"isunordered(NaN pattern, 1) = 0", (op_cmp(0x7FC00000, 0x3F800000) >> 10) & 1, 0},
            {"-0 == +0", (op_cmp(0x80000000, 0) >> 4) & 1, 1},
            {"(int) NaN pattern = INT_MAX", (uint32_t)op_ftoi(0x7FC00000), 0x7FFFFFFF},
            {"(int) -2^31 = INT_MIN", (uint32_t)op_ftoi(0xCF000000), 0x80000000},
            {"(int) 2^31 saturates", (uint32_t)op_ftoi(0x4F000000), 0x7FFFFFFF},
            {"(float) 0x7FFFFFFF truncates (IEEE: 2^31)", op_itof(0x7FFFFFFF), 0x4EFFFFFF},
            {"signalling NaN pattern passes unchanged", op_pass(0x7F800001, 1), 0x7F800001},
        };
        for (i = 0; i < (int)(sizeof(t) / sizeof(t[0])); i++) {
            int ok = t[i].got == t[i].want;
            printf("  %s %s (got %08x)\n", ok ? "ok  " : "FAIL", t[i].what, t[i].got);
            fail |= !ok;
        }
    }
    for (op = 0; op < N_OPS; op++) {
        printf("%-7s %9lu cases, %lu differ from the routines\n", kName[op], sTests[op], sBad[op]);
        fail |= sBad[op] != 0;
    }
    printf("PCSX2   %9lu values (1500 cases x 7 FPU columns), %lu differ\n", pcsx2, pcsx2Bad);
    fail |= pcsx2Bad != 0;
    printf(fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return fail;
}
