/* Memory-model test of the WebAssembly build (see BROWSER_PORT_STATUS.md). Built twice by run.py:
     LAYOUT_B: the program (data, stack, C heap) starts above the GS registers (GLOBAL_BASE 0x12010000) and memory
               ends at 0x70000000: the scratchpad address is never inside it, so any access to it traps.
     LAYOUT_A: the program starts above the scratchpad (GLOBAL_BASE 0x70010000), everything at PS2 addresses.
   Memory is capped at 2 GB: the port's DMA code takes address bit 31 for "scratchpad", so no address the game can
   see may have it set.
   Each check prints "ok" or "FAIL" and a summary line; the page (memtest.html) reports the output. */
#include <emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/stack.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LAYOUT_A
#define FIXED_TOP 0x70004000u
#define NAME "A (program above the scratchpad)"
#else
#define FIXED_TOP 0x12002000u
#define NAME "B (program above the GS registers)"
#endif

static int sFail, sChecks;
static void check(int ok, const char *what, unsigned long long v) {
    sChecks++;
    sFail += !ok;
    printf("  %s %s (0x%llX)\n", ok ? "ok  " : "FAIL", what, v);
}

/* The PS2's layouts: what -malign-double gives the 32-bit x86 build. */
typedef struct { uint32_t a; uint64_t b; } S1;
typedef struct { uint8_t a; float b; uint16_t c; double d; uint8_t e; } S2;
typedef struct { uint32_t a : 3, b : 29; uint8_t c; } S3;
typedef struct { uint8_t a; uint64_t b : 40; uint16_t c; } S4;

static uint32_t sCanary = 0xC0FFEE11u; /* a global, to see that writes to the fixed regions disturb nothing */

static uint32_t sum(const uint8_t *p, uint32_t n) {
    uint32_t s = 0, i;
    for (i = 0; i < n; i++) {
        s = s * 31 + p[i];
    }
    return s;
}

/* writes a pattern over [base, base + size) and reads it back; returns whether it held and the region started zero */
static void region(uint32_t base, uint32_t size, const char *what) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)base;
    uint32_t i, zero = 1, ok = 1;
    char buf[160];

    for (i = 0; i < size; i += 4096) {
        zero &= p[i] == 0;
    }
    for (i = 0; i < size; i++) {
        p[i] = (uint8_t)(i * 7 + (base >> 12));
    }
    for (i = 0; i < size; i++) {
        ok &= p[i] == (uint8_t)(i * 7 + (base >> 12));
    }
    snprintf(buf, sizeof(buf), "%s 0x%08X..0x%08X: starts zero, holds a pattern", what, base, base + size);
    check(zero && ok, buf, base);
    memset((void *)(uintptr_t)base, 0, size); /* the port's hardware registers read 0 = "transfer finished" */
}

EMSCRIPTEN_KEEPALIVE int run_tests(void) {
    int local = 0;
    uint8_t *block = malloc(1 << 20);
    uint32_t blockSum, memSize = (uint32_t)emscripten_get_heap_size();
    S1 s1; S2 s2;

    printf("memtest layout %s\n", NAME);
    printf("  data %p, stack %p (base 0x%X end 0x%X), malloc %p, heap size 0x%X, max 0x%X\n", (void *)&sCanary,
           (void *)&local, (unsigned)emscripten_stack_get_base(), (unsigned)emscripten_stack_get_end(), (void *)block,
           memSize, (unsigned)emscripten_get_heap_max());
    /* 1. types and layouts */
    check(sizeof(void *) == 4 && sizeof(long) == 4 && sizeof(long long) == 8, "pointer 4, long 4, long long 8 bytes", sizeof(void *));
    check(_Alignof(long long) == 8 && _Alignof(double) == 8, "long long and double aligned to 8", _Alignof(double));
    check(offsetof(S1, b) == 8 && sizeof(S1) == 16, "{u32; u64}: u64 at 8, size 16", sizeof(S1));
    check(offsetof(S2, b) == 4 && offsetof(S2, c) == 8 && offsetof(S2, d) == 16 && offsetof(S2, e) == 24 && sizeof(S2) == 32,
          "{u8; f32; u16; f64; u8}: 0 4 8 16 24, size 32", sizeof(S2));
    check(sizeof(S3) == 8, "{u32 a:3, b:29; u8}: size 8", sizeof(S3));
    check(offsetof(S4, c) == 6 && sizeof(S4) == 8, "{u8; u64 b:40; u16}: b packed in the first unit, c at 6, size 8", offsetof(S4, c));
    (void)s1; (void)s2;
    /* 2. where the program lies: above every PS2 region, below 2 GB */
    check((uintptr_t)&sCanary >= FIXED_TOP, "global data above the fixed regions", (uintptr_t)&sCanary);
    check(emscripten_stack_get_end() >= FIXED_TOP && (uintptr_t)&local >= FIXED_TOP, "stack above the fixed regions", emscripten_stack_get_end());
    check(block != NULL && (uintptr_t)block >= FIXED_TOP, "malloc above the fixed regions", (uintptr_t)block);
    check(emscripten_get_heap_max() <= 0x80000000u, "memory capped at 2 GB (no address with bit 31)", emscripten_get_heap_max());
#ifndef LAYOUT_A
    check(emscripten_get_heap_max() <= 0x70000000u, "memory ends below the scratchpad address", emscripten_get_heap_max());
#endif
    /* 3. the fixed regions */
    memset(block, 0x5A, 1 << 20);
    blockSum = sum(block, 1 << 20);
    region(0x003BE000u, 0x02000000u - 0x003BE000u, "game heap");
    region(0x10000000u, 0x10000u, "hardware registers");
    region(0x12000000u, 0x2000u, "GS registers");
#ifdef LAYOUT_A
    region(0x70000000u, 0x4000u, "scratchpad");
#endif
    check(sCanary == 0xC0FFEE11u && sum(block, 1 << 20) == blockSum, "global and malloc block untouched by those writes", sCanary);
    /* 4. pointers in 32-bit fields, as the game keeps them */
    {
        uint32_t field = (uint32_t)(uintptr_t)block;
        uint8_t *back = (uint8_t *)(uintptr_t)field;
        back[17] = 0xA5;
        check(back == block && block[17] == 0xA5 && (int32_t)field > 0, "pointer -> u32 -> pointer, positive as s32", field);
        /* a heap pointer at its PS2 value, as Port_Malloc hands them out */
        *(volatile uint32_t *)(uintptr_t)0x003BE730u = 0x12345678u;
        check(*(volatile uint32_t *)(uintptr_t)0x003BE730u == 0x12345678u, "PS2 heap address 0x003BE730 usable as a pointer", 0x003BE730u);
        *(volatile uint32_t *)(uintptr_t)0x003BE730u = 0;
    }
    free(block);
    printf("memtest summary: %d checks, %d failed\n", sChecks, sFail);
    return sFail;
}

/* A load from any address (the page calls it on the scratchpad to see what happens where nothing is mapped). */
EMSCRIPTEN_KEEPALIVE uint32_t touch(uint32_t addr) { return *(volatile uint32_t *)(uintptr_t)addr; }

/* Grows the C heap in blocks of `mb` MB until malloc returns NULL; writes every block's first and last byte.
   Returns the total in MB; prints the memory size reached. Frees everything afterwards. */
EMSCRIPTEN_KEEPALIVE int grow_test(int mb) {
    static void *blocks[1024];
    int n = 0, i, ok = 1;
    uint32_t top = 0;

    while (n < 1024) {
        uint8_t *p = malloc((size_t)mb << 20);
        if (p == NULL) {
            break;
        }
        p[0] = 1;
        p[((size_t)mb << 20) - 1] = 2;
        if ((uintptr_t)p + ((size_t)mb << 20) > top) {
            top = (uint32_t)((uintptr_t)p + ((size_t)mb << 20));
        }
        ok &= ((uintptr_t)p & 0x80000000u) == 0;
        blocks[n++] = p;
    }
    printf("  grow: %d blocks of %d MB before malloc returned NULL; highest address 0x%X; heap size now 0x%X; no block above 2 GB: %s\n",
           n, mb, top, (unsigned)emscripten_get_heap_size(), ok ? "yes" : "NO");
    for (i = 0; i < n; i++) {
        free(blocks[i]);
    }
    /* after growth the scratchpad address exists in layout B too (inside the C heap's range: see the notes) */
    return ok ? n * mb : -1;
}
