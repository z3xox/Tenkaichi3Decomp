/* Explicit PS2 address view for the RetroAchievements memory callback.
 * No process addresses may be read merely because an achievement requests them.
 * The owner supplies only regions whose PS2 layout has been verified. */
#ifndef BT3_RA_MEMORY_H
#define BT3_RA_MEMORY_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t ps2_address;
    uint32_t game_address; /* Port_Addr(data), for translating stored game pointers */
    uint32_t size;
    const uint8_t *data;
    /* Offsets of verified, aligned, four-byte pointer fields. Sorted, unique.
     * Other four-byte values are never guessed to be pointers. */
    const uint32_t *pointers;
    size_t pointer_count;
} RaMemoryRegion;

typedef struct {
    const RaMemoryRegion *regions;
    size_t count;
} RaMemory;

/* Reject overlapping/overflowing ranges and invalid pointer descriptors.
 * Descriptors and memory must remain alive and unchanged during a read.
 * Memory is read on the game thread, between simulation ticks. */
int RaMemory_Validate(const RaMemory *memory);

/* rc_client read_memory semantics: return bytes actually read. Unknown addresses,
 * incomplete ranges and untranslatable pointers return 0, never fabricated zeros.
 * A failed read leaves the destination untouched. The view must be validated first.
 * PS2 RAM is little endian, including when only part of a pointer is requested. */
uint32_t RaMemory_Read(const RaMemory *memory, uint32_t address,
                       uint8_t *buffer, uint32_t size);
#endif
