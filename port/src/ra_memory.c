#include "ra_memory.h"

#define PS2_RAM_SIZE 0x02000000u

static int overlap(uint32_t a, uint32_t as, uint32_t b, uint32_t bs) {
    return (uint64_t)a < (uint64_t)b + bs && (uint64_t)b < (uint64_t)a + as;
}

int RaMemory_Validate(const RaMemory *memory) {
    size_t i, j;
    if (memory == NULL || (memory->count && memory->regions == NULL)) return 0;
    for (i = 0; i < memory->count; ++i) {
        const RaMemoryRegion *r = &memory->regions[i];
        if (r->data == NULL || r->size == 0 || r->ps2_address >= PS2_RAM_SIZE ||
            r->size > PS2_RAM_SIZE - r->ps2_address ||
            (uint64_t)r->game_address + r->size > UINT64_C(0x100000000) ||
            (r->pointer_count && r->pointers == NULL)) return 0;
        for (j = 0; j < r->pointer_count; ++j) {
            uint32_t p = r->pointers[j];
            if ((p & 3) || r->size < 4 || p > r->size - 4 ||
                (j && p <= r->pointers[j - 1])) return 0;
        }
        for (j = 0; j < i; ++j) {
            const RaMemoryRegion *other = &memory->regions[j];
            if (overlap(r->ps2_address, r->size, other->ps2_address, other->size) ||
                overlap(r->game_address, r->size, other->game_address, other->size)) return 0;
        }
    }
    return 1;
}

static const RaMemoryRegion *find_region(const RaMemory *m, uint32_t address) {
    size_t i;
    for (i = 0; i < m->count; ++i) {
        const RaMemoryRegion *r = &m->regions[i];
        if (address >= r->ps2_address && address - r->ps2_address < r->size) return r;
    }
    return NULL;
}

static int translate_pointer(const RaMemory *m, uint32_t game, uint32_t *ps2) {
    size_t i;
    if (game == 0) { *ps2 = 0; return 1; }
    for (i = 0; i < m->count; ++i) {
        const RaMemoryRegion *r = &m->regions[i];
        if (game >= r->game_address && game - r->game_address < r->size) {
            *ps2 = r->ps2_address + (game - r->game_address);
            return 1;
        }
    }
    return 0;
}

static int read_byte(const RaMemory *m, uint32_t address, uint8_t *value) {
    const RaMemoryRegion *r = find_region(m, address);
    uint32_t offset;
    size_t lo, hi;
    if (r == NULL) return 0;
    offset = address - r->ps2_address;
    /* Binary search for the final pointer offset at or before this byte. */
    lo = 0;
    hi = r->pointer_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (r->pointers[mid] <= offset) lo = mid + 1;
        else hi = mid;
    }
    if (lo && offset - r->pointers[lo - 1] < 4) {
        const uint8_t *p = r->data + r->pointers[lo - 1];
        uint32_t ps2, game = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        if (!translate_pointer(m, game, &ps2)) return 0;
        *value = (uint8_t)(ps2 >> (8 * (offset - r->pointers[lo - 1])));
    } else {
        *value = r->data[offset];
    }
    return 1;
}

uint32_t RaMemory_Read(const RaMemory *memory, uint32_t address,
                       uint8_t *buffer, uint32_t size) {
    uint32_t i;
    uint8_t value;
    if (memory == NULL || buffer == NULL || address >= PS2_RAM_SIZE ||
        size > PS2_RAM_SIZE - address) return 0;
    /* Preflight before writing anything: a read may cross regions or a hole.
     * The caller holds the game thread, so memory cannot change between passes. */
    for (i = 0; i < size; ++i) {
        if (!read_byte(memory, address + i, &value)) return 0;
    }
    for (i = 0; i < size; ++i) {
        if (!read_byte(memory, address + i, &buffer[i])) return 0;
    }
    return size;
}
