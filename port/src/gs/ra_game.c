/* The existing PS2 set addresses stable objects at their console addresses.
 * Resolve those objects by identity, not by assuming a heap relocation applies
 * to every allocation. Missing temporary objects suspend dependent achievements
 * without supplying a fabricated RAM value. */
#include "ra_game.h"
#include "ra_client.h"
#include "../ra_memory.h"
#include "../port_addr.h"
#include "../../third_party/rcheevos/src/rhash/md5.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

/* The game's global pointer slots are exactly four bytes on every port ABI.
 * Character aliases avoid loading them as the host's eight-byte pointers. */
#ifdef __APPLE__
#define GAME_SYMBOL(name) "_" name
#else
#define GAME_SYMBOL(name) name
#endif
extern const unsigned char raProgressSlot[4] __asm__(GAME_SYMBOL("gProgress"));
extern const unsigned char raSaveSlot[4] __asm__(GAME_SYMBOL("gSaveData"));
extern const unsigned char raFightersSlot[4] __asm__(GAME_SYMBOL("gBtlChars"));
extern const unsigned char raSimDaySlot[4] __asm__(GAME_SYMBOL("gSimDay"));
extern const unsigned char raLoadScreen[] __asm__(GAME_SYMBOL("gLoadScreen"));
extern int Port_DataState(void), Port_NetSession(void), Port_GameSpeed(void);
extern int Port_ClashSquare(void);
extern const char *Port_FileRoot(void);
extern int gPortResim;
extern volatile int gPortUnlockAll, gPortUnlockDone;

static RaMemoryRegion sRegions[12];
static RaMemory sMemory = {sRegions, 0};
static char sHash[33];
static int sHashTried, sTainted, sTaintChecked;
static char sTaintPath[1200];
static int sTransientReset = 1;

void PortRa_TransientReset(void) { sTransientReset = 1; }

static int taint_path(char *path, size_t size) {
    const char *root = getenv("BT3_SAVES");
    int n;
    if (!root) root = "saves";
    n = snprintf(path, size, "%s/.retroachievements-tainted", root);
    return n >= 0 && (size_t)n < size;
}
static void read_taint(void) {
    char path[1200];
    FILE *file;
    if (!taint_path(path, sizeof(path))) { sTainted = 1; return; }
    /* Netplay temporarily changes BT3_SAVES. A check made in its temporary
     * folder must not hide a marker in the restored local save folder. */
    if (sTaintChecked && !strcmp(path, sTaintPath)) return;
    sTaintChecked = 1;
    memcpy(sTaintPath, path, strlen(path) + 1);
    errno = 0;
    file = fopen(path, "rb");
    if (file) { sTainted = 1; fclose(file); }
    else if (errno != ENOENT) sTainted = 1; /* cannot establish a clean save folder */
}
int PortRa_BeforeUnlockAll(void) {
    static const char marker[] = "Unlock Everything: RetroAchievements disabled for this save folder.\n";
    RaClientStatus status;
    const char *root = getenv("BT3_SAVES");
    char path[1200];
    FILE *file;
    int ok;
    RaClient_GetStatus(&status);
    if (status.enabled || status.logging_in || status.logged_in) return 0;
    if (!root) root = "saves";
    if (!taint_path(path, sizeof(path))) return 0;
#ifdef _WIN32
    if (_mkdir(root) != 0 && errno != EEXIST) return 0;
#else
    if (mkdir(root, 0755) != 0 && errno != EEXIST) return 0;
#endif
    file = fopen(path, "wb");
    if (!file) return 0;
    /* Even a partial marker fails closed. Do not mutate the save unless the
     * complete marker was written and closed successfully. */
    sTainted = 1;
    sTaintChecked = 1;
    ok = fwrite(marker, 1, sizeof(marker) - 1, file) == sizeof(marker) - 1;
    if (fflush(file) != 0) ok = 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static uint32_t slot(const unsigned char *p) {
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}
static void region(uint32_t ps2, const void *data, uint32_t size) {
    RaMemoryRegion *r = &sRegions[sMemory.count++];
    r->ps2_address = ps2;
    r->game_address = Port_Addr(data);
    r->size = size;
    r->data = (const uint8_t *)data;
    r->pointers = NULL;
    r->pointer_count = 0;
}
static int map_memory(void) {
    static const uint32_t progress_offsets[] = {0x18, 0x2c, 0x3c, 0x84, 0x90, 0x620, 0x624, 0x64c, 0x674};
    uint32_t progress = slot(raProgressSlot), save = slot(raSaveSlot);
    size_t i;
    sMemory.count = 0;
    if (!progress || !save) return 0;
    /* SaveData is the pointer-free, serialized SAVE_SIZE block (sys/save.h).
     * Progress scalar offsets are shared by view_a.h and the menu views. */
    region(0x006b9640, Port_Ptr(save), 0x4000);
    for (i = 0; i < sizeof(progress_offsets) / sizeof(progress_offsets[0]); ++i) {
        uint32_t offset = progress_offsets[i];
        region(0x006af180 + offset, (const uint8_t *)Port_Ptr(progress) + offset, 4);
    }
    /* Static loading minigame score and type, both u16 (sys/loading.h).
     * The rest of LoadScreen contains pointers and is deliberately not exposed. */
    region(0x0031e592, raLoadScreen + 0xa, 2);
    region(0x0031e59a, raLoadScreen + 0x12, 2);
    return RaMemory_Validate(&sMemory);
}
static uint32_t read_memory(uint32_t address, uint8_t *buffer, uint32_t bytes, void *unused) {
    uint32_t object, field;
    size_t side, member, kind;
    (void)unused;
    if (!buffer || !bytes) return 0;
    /* Only pointer-free s32 member IDs/health gauges are exposed. The roster
     * pointer is a four-byte game pointer, not a host pointer. */
    for (side = 0; side < 2; ++side) {
        for (member = 0; member < 5; ++member) {
            for (kind = 0; kind < 2; ++kind) {
                uint32_t offset = (uint32_t)(side * 0x1600 + 0x9a4 + member * 0xa4 + kind * 0x40);
                field = 0x018706c0 + offset;
                if (address < field || address - field >= 4 || bytes > 4 - (address - field)) continue;
                object = slot(raFightersSlot);
                if (object && slot(Port_Ptr(object)) >= 2)
                    object = slot((const unsigned char *)Port_Ptr(object) + 4);
                else object = 0;
                if (!object || sTransientReset) return RA_MEMORY_UNAVAILABLE;
                memcpy(buffer, (const unsigned char *)Port_Ptr(object) + offset + address - field, bytes);
                return bytes;
            }
        }
    }
    field = 0x008c0880; /* SimDay.msgLine, +0x5a0 (menu/menu_q.h). */
    if (address >= field && address - field < 4 && bytes <= 4 - (address - field)) {
        object = slot(raSimDaySlot);
        if (!object || sTransientReset) return RA_MEMORY_UNAVAILABLE;
        memcpy(buffer, (const unsigned char *)Port_Ptr(object) + 0x5a0 + address - field, bytes);
        return bytes;
    }
    return RaMemory_Read(&sMemory, address, buffer, bytes);
}
static int hash_executable(void) {
    /* Exactly the PS2 hash algorithm in rcheevos/src/rhash/hash_disc.c:
     * MD5(BOOT2 executable basename, then its bytes, capped at 64 MiB).
     * This port accepts only the USA disc, whose BOOT2 is SLUS_216.78. */
    static const char boot[] = "SLUS_216.78";
    char path[1200];
    unsigned char buffer[8192], digest[16];
    md5_state_t md5;
    FILE *file;
    size_t bytes, total = 0, i;
    int n = snprintf(path, sizeof(path), "%s/disc/%s", Port_FileRoot(), boot);
    if (n < 0 || n >= (int)sizeof(path)) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    md5_init(&md5);
    md5_append(&md5, (const md5_byte_t *)boot, (int)strlen(boot));
    while (total < 64u * 1024u * 1024u && (bytes = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if (total == 0 && (bytes < 4 || memcmp(buffer, "\177ELF", 4))) { fclose(file); return 0; }
        md5_append(&md5, buffer, (int)bytes);
        total += bytes;
    }
    if (ferror(file) || !total) { fclose(file); return 0; }
    fclose(file);
    md5_finish(&md5, digest);
    for (i = 0; i < 16; ++i) snprintf(sHash + i * 2, 3, "%02x", digest[i]);
    return 1;
}
void PortRa_Init(void) {
    if (RaClient_Init()) atexit(RaClient_Shutdown);
}
void PortRa_Tick(void) {
    const char *reason = NULL;
    if (gPortResim) return;
    read_taint();
    /* These flags are checked before this tick can evaluate an unlock. Taint
     * stays outside rollback memory and survives logging out for this process. */
    if (gPortUnlockAll || gPortUnlockDone) sTainted = 1;
    if (sTainted) reason = "Unlock Everything was used in this save folder. Use a separate clean save folder for achievements.";
    else if (Port_NetSession()) reason = "Achievements are suspended during netplay.";
    else if (getenv("BT3_REPLAY") || getenv("BT3_DEMO") || getenv("BT3_PAD_PLAY") || getenv("BT3_PAD") ||
             getenv("BT3_SYNCTEST") || getenv("BT3_FILE_ALIAS")) reason = "Achievements are suspended for replay, scripted input or diagnostic runs.";
    else if (Port_GameSpeed() != 100 || Port_ClashSquare()) reason = "Use 100% game speed and disable clash assistance for achievements.";
    else if (Port_DataState() != 1) reason = "Waiting for the original game data check (BT3_VERIFY must be enabled).";
    else if (!map_memory()) reason = "Waiting for verified game memory.";
    if (!reason && !sHashTried) {
        sHashTried = 1;
        hash_executable();
    }
    if (!reason && !sHash[0]) reason = "Cannot identify the original SLUS_216.78 executable.";
    RaClient_Tick(read_memory, NULL, sHash, reason == NULL, reason);
    sTransientReset = 0;
}
