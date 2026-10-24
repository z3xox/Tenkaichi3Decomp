#include "../src/ra_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* A native global points to a relocated heap object. An ordinary integer
     * deliberately has the same value and must stay unchanged. */
    uint8_t global[] = {0x02, 0x00, 0x00, 0x30, 0x02, 0x00, 0x00, 0x30};
    uint8_t heap[] = {10, 20, 30, 40};
    uint8_t adjacent[] = {50, 60};
    uint8_t out[16];
    const uint32_t pointers[] = {0};
    RaMemoryRegion regions[] = {
        {0x31e000, 0x20001000, sizeof(global), global, pointers, 1},
        {0x6af000, 0x30000000, sizeof(heap), heap, NULL, 0},
        {0x6af004, 0x30000004, sizeof(adjacent), adjacent, NULL, 0}
    };
    RaMemory memory = {regions, 3};
    RaMemory empty = {NULL, 0};
    uint32_t bad_pointers[] = {0, 0};
    assert(RaMemory_Validate(&memory));
    assert(RaMemory_Validate(&empty));
    assert(RaMemory_Read(&memory, 0x31e000, out, 8) == 8);
    assert(memcmp(out, "\x02\xf0\x6a\x00\x02\x00\x00\x30", 8) == 0);
    assert(RaMemory_Read(&memory, 0x31e001, out, 2) == 2);
    assert(out[0] == 0xf0 && out[1] == 0x6a);
    assert(RaMemory_Read(&memory, 0x6af002, out, 4) == 4);
    assert(memcmp(out, "\x1e\x28\x32\x3c", 4) == 0);
    memset(out, 0xaa, sizeof(out));
    assert(RaMemory_Read(&memory, 0x6af002, out, 5) == 0);
    assert(out[0] == 0xaa && out[4] == 0xaa);
    assert(RaMemory_Read(&empty, 0x6af000, out, 1) == 0);
    assert(RaMemory_Read(&memory, 0xfffffff0, out, 32) == 0);
    assert(RaMemory_Read(&memory, 0x1ffffff, out, 2) == 0);
    assert(RaMemory_Read(&memory, 0x6af000, NULL, 1) == 0);
    /* A pointer outside all mapped regions is unavailable, not zero-filled. */
    global[3] = 0x40;
    assert(RaMemory_Read(&memory, 0x31e000, out, 4) == 0);
    assert(out[0] == 0xaa);
    memset(global, 0, 4);
    assert(RaMemory_Read(&memory, 0x31e000, out, 4) == 4);
    assert(out[0] == 0 && out[3] == 0);
    regions[2].ps2_address--;
    assert(!RaMemory_Validate(&memory));
    regions[2].ps2_address++;
    regions[2].game_address--;
    assert(!RaMemory_Validate(&memory));
    regions[2].game_address++;
    regions[0].pointers = bad_pointers;
    regions[0].pointer_count = 2;
    assert(!RaMemory_Validate(&memory));
    bad_pointers[1] = 5;
    assert(!RaMemory_Validate(&memory));
    bad_pointers[1] = 8;
    assert(!RaMemory_Validate(&memory));
    regions[0].pointers = pointers;
    regions[0].pointer_count = 1;
    regions[2].ps2_address = 0x1ffffff;
    assert(!RaMemory_Validate(&memory));
    regions[2].ps2_address = 0x6af004;
    regions[2].game_address = 0xffffffff;
    assert(!RaMemory_Validate(&memory));
    assert(!RaMemory_Validate(NULL));
    puts("ra_memory: all checks passed");
    return 0;
}
