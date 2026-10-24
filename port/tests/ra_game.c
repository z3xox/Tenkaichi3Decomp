/* Production game adapter with local game/client stubs, never a network client. */
#include "../src/gs/ra_client.h"
#include "../src/gs/ra_game.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

uint32_t gProgress, gSaveData, gBtlChars, gSimDay;
unsigned char gLoadScreen[0x1ac];
int gPortResim;
volatile int gPortUnlockAll, gPortUnlockDone;
static int data_state = 1, netplay, speed = 100, clash, eligible, calls;
static char hash[33], reason[192];
static const char *data_root;
static RaClientReadMemory reader;
static void *reader_data;
static RaClientStatus client_status;
int Port_DataState(void) { return data_state; }
int Port_NetSession(void) { return netplay; }
int Port_GameSpeed(void) { return speed; }
int Port_ClashSquare(void) { return clash; }
const char *Port_FileRoot(void) { return data_root; }
int RaClient_Init(void) { return 1; }
void RaClient_Shutdown(void) {}
void RaClient_GetStatus(RaClientStatus *out) { *out = client_status; }
void RaClient_Tick(RaClientReadMemory read, void *data, const char *game_hash, int allowed, const char *blocked) {
    reader = read; reader_data = data; eligible = allowed; ++calls;
    snprintf(hash, sizeof(hash), "%s", game_hash);
    snprintf(reason, sizeof(reason), "%s", blocked ? blocked : "");
}
int main(int argc, char **argv) {
    uint8_t *memory, result[8];
    uint32_t saved_progress;
    int before;
    assert(argc == 3);
    data_root = argv[1];
#ifdef _WIN32
    memory = VirtualAlloc((void *)(uintptr_t)0x31000000, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    memory = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(memory != MAP_FAILED);
#endif
    assert(memory && (uintptr_t)memory + 0x10000 <= UINT64_C(0x100000000));
    memset(memory, 0, 0x10000);
    saved_progress = gProgress = (uint32_t)(uintptr_t)memory;
    gSaveData = (uint32_t)(uintptr_t)(memory + 0x1000);
    memory[0x18] = 4;
    memory[0x1000 + 0x3028] = 0x78;
    memory[0x1000 + 0x3029] = 0x56;
    gLoadScreen[0xa] = 0x55;
    PortRa_Init(); PortRa_Tick();
    if (!strcmp(argv[2], "root-switch")) {
        char saves[1200];
        assert(eligible);
        snprintf(saves, sizeof(saves), "%s/saves", data_root);
#ifdef _WIN32
        assert(_putenv_s("BT3_SAVES", saves) == 0);
#else
        assert(setenv("BT3_SAVES", saves, 1) == 0);
#endif
        PortRa_Tick();
        assert(!eligible && strstr(reason, "save folder"));
        puts("ra_game: restored save folder is checked for a cheat marker"); return 0;
    }
    if (!strcmp(argv[2], "write-failure")) {
        assert(!PortRa_BeforeUnlockAll());
        puts("ra_game: unwritable marker cancels Unlock Everything"); return 0;
    }
    if (!strcmp(argv[2], "tainted")) {
        assert(!eligible && strstr(reason, "save folder"));
        puts("ra_game: cheat marker blocks achievements after process restart"); return 0;
    }
    if (!strcmp(argv[2], "invalid")) {
        assert(!eligible && !hash[0] && strstr(reason, "identify"));
        puts("ra_game: invalid/empty executable rejected"); return 0;
    }
    assert(eligible && !reason[0] && !strcmp(hash, argv[2]));
    assert(reader(0x6af198, result, 1, reader_data) == 1 && result[0] == 4);
    assert(reader(0x6bc668, result, 4, reader_data) == 4 && result[0] == 0x78 && result[1] == 0x56);
    assert(reader(0x31e592, result, 2, reader_data) == 2 && result[0] == 0x55);
    memset(result, 0xaa, sizeof(result));
    assert(reader(0x1871064, result, 1, reader_data) == RA_MEMORY_UNAVAILABLE && result[0] == 0xaa);
    assert(reader(0x8c0880, result, 1, reader_data) == RA_MEMORY_UNAVAILABLE);
    gBtlChars = (uint32_t)(uintptr_t)(memory + 0x6000);
    gSimDay = (uint32_t)(uintptr_t)(memory + 0xa000);
    {
        uint32_t count = 2, fighters = (uint32_t)(uintptr_t)(memory + 0x7000);
        memcpy(memory + 0x6000, &count, 4);
        memcpy(memory + 0x6004, &fighters, 4);
    }
    memory[0x7000 + 0x9a4] = 73;
    memory[0x7000 + 0x1600 + 0x9e4] = 99;
    memory[0xa000 + 0x5a0] = 89;
    assert(reader(0x1871064, result, 4, reader_data) == 4 && result[0] == 73);
    assert(reader(0x18726a4, result, 4, reader_data) == 4 && result[0] == 99);
    assert(reader(0x8c0880, result, 4, reader_data) == 4 && result[0] == 89);
    assert(reader(0x1871068, result, 1, reader_data) == 0); /* adjacent unverified fields */
    assert(reader(0x8c0883, result, 2, reader_data) == 0); /* crosses verified field */
    PortRa_TransientReset(); /* same pointer can be reused between two ticks */
    assert(reader(0x18726a4, result, 4, reader_data) == RA_MEMORY_UNAVAILABLE);
    PortRa_Tick();
    assert(reader(0x18726a4, result, 4, reader_data) == 4);
    gBtlChars = gSimDay = 0;
    assert(reader(0x18726a4, result, 4, reader_data) == RA_MEMORY_UNAVAILABLE);
    assert(reader(0x6af188, result, 4, reader_data) == 0); /* pointer fields stay hidden */
    netplay = 1; PortRa_Tick(); assert(!eligible); netplay = 0;
    speed = 120; PortRa_Tick(); assert(!eligible); speed = 100;
    clash = 1; PortRa_Tick(); assert(!eligible); clash = 0;
    data_state = 0; PortRa_Tick(); assert(!eligible); data_state = 1;
    gProgress = 0; PortRa_Tick(); assert(!eligible); gProgress = saved_progress;
    before = calls; gPortResim = 1; PortRa_Tick(); assert(calls == before); gPortResim = 0;
    PortRa_Tick(); assert(eligible);
    client_status.logged_in = 1;
    assert(!PortRa_BeforeUnlockAll()); /* cannot mutate a save in an active RA session */
    client_status.logged_in = 0;
    assert(PortRa_BeforeUnlockAll()); /* marker persisted BEFORE the debug mutation */
    PortRa_Tick(); assert(!eligible);
    gPortUnlockAll = 1; PortRa_Tick(); assert(!eligible);
    gPortUnlockAll = 0; PortRa_Tick(); assert(!eligible && strstr(reason, "Unlock Everything"));
    puts("ra_game: identity mapping/hash/lifecycle/unsupported/cheat latch passed");
    return 0;
}
