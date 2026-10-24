/* Optional reference-set validation: real adapter/client/runtime, fake HTTPS.
 * Uses only synthetic game objects and a caller-supplied public patch file. */
#ifndef _WIN32
#define _GNU_SOURCE 1 /* mmap MAP_32BIT on the x86-64 Linux test host */
#endif
#include "ra_client.h"
#include "ra_game.h"
#include <SDL3/SDL.h>
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
static const char *data_root, *patch;
static SDL_AtomicInt allow_award, awards;
static unsigned expected_award;
int Port_DataState(void) { return 1; }
int Port_NetSession(void) { return 0; }
int Port_GameSpeed(void) { return 100; }
int Port_ClashSquare(void) { return 0; }
const char *Port_FileRoot(void) { return data_root; }
static const char *transport(const char *url, const char *post, int *status, void *unused) {
    static char response[200];
    (void)unused;
    assert(strstr(url, "https://retroachievements.org/") == url && post);
    *status = 200;
    if (strstr(post, "r=login2"))
        return "{\"Success\":true,\"User\":\"Fixture\",\"Token\":\"fake\",\"Score\":0,\"SoftcoreScore\":0,\"Messages\":0}";
    if (strstr(post, "r=achievementsets")) return patch;
    if (strstr(post, "r=startsession")) return "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}";
    if (strstr(post, "r=ping")) return "{\"Success\":true}";
    if (strstr(post, "r=awardachievement")) {
        const char *id = strstr(post, "&a=");
        assert(SDL_GetAtomicInt(&allow_award) && id && (unsigned)strtoul(id + 3, NULL, 10) == expected_award);
        SDL_AddAtomicInt(&awards, 1);
        snprintf(response, sizeof(response), "{\"Success\":true,\"AchievementID\":%u,\"Score\":0,\"SoftcoreScore\":1,\"AchievementsRemaining\":1}", expected_award);
        return response;
    }
    assert(!"unexpected reference fixture request"); return "";
}
static RaClientStatus tick(void) {
    RaClientStatus status;
    PortRa_Tick(); RaClient_GetStatus(&status); return status;
}
static void word(uint8_t *at, uint32_t value) { memcpy(at, &value, 4); }
int main(int argc, char **argv) {
    uint8_t *memory;
    char *body;
    FILE *file;
    long length;
    int i;
    RaClientStatus status;
    assert(argc == 5);
    data_root = argv[1]; expected_award = (unsigned)strtoul(argv[4], NULL, 10);
    file = fopen(argv[2], "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); length = ftell(file); assert(length > 0);
    rewind(file); body = malloc((size_t)length + 1); assert(body);
    assert(fread(body, 1, (size_t)length, file) == (size_t)length); fclose(file);
    body[length] = 0; patch = body;
#ifdef _WIN32
    memory = VirtualAlloc((void *)(uintptr_t)0x31000000, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    memory = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(memory != MAP_FAILED);
#endif
    assert(memory && (uintptr_t)memory + 0x10000 < UINT64_C(0x100000000));
    memset(memory, 0, 0x10000);
    gProgress = (uint32_t)(uintptr_t)memory;
    gSaveData = (uint32_t)(uintptr_t)(memory + 0x1000);
    RaClient_TestTransport(transport, NULL); RaClient_TestTime(1000);
    PortRa_Init(); assert(RaClient_RequestLogin("Fixture", "fake"));
    for (i = 0; i < 3000; ++i) {
        status = tick(); if (status.game_loaded) break; SDL_Delay(1);
    }
    printf("reference set: %u core, %u unsupported (temporary objects absent)\n", status.total, status.unsupported);
    assert(status.game_loaded && status.total == (unsigned)strtoul(argv[3], NULL, 10) && !status.unsupported);
    /* The actual tournament definition must not see freeing/recreating the
     * opponent as a kill, even while its mode/result guards remain true. */
    word(memory + 0x18, 35); word(memory + 0x90, 4);
    word(memory + 0x6000, 2); word(memory + 0x6004, (uint32_t)(uintptr_t)(memory + 0x7000));
    gBtlChars = (uint32_t)(uintptr_t)(memory + 0x6000);
    gSimDay = (uint32_t)(uintptr_t)(memory + 0xa000);
    word(memory + 0x7000 + 0x1600 + 0x9e4, 100);
    PortRa_TransientReset(); for (i = 0; i < 4; ++i) tick();
    PortRa_TransientReset(); gBtlChars = 0;
    for (i = 0; i < 4; ++i) tick();
    word(memory + 0x7000 + 0x1600 + 0x9e4, 0);
    gBtlChars = (uint32_t)(uintptr_t)(memory + 0x6000); PortRa_TransientReset();
    for (i = 0; i < 4; ++i) tick();
    assert(!SDL_GetAtomicInt(&awards));
    word(memory + 0x7000 + 0x1600 + 0x9e4, 100);
    for (i = 0; i < 4; ++i) tick();
    SDL_SetAtomicInt(&allow_award, 1);
    word(memory + 0x7000 + 0x1600 + 0x9e4, 0);
    for (i = 0; i < 3000 && !SDL_GetAtomicInt(&awards); ++i) { tick(); SDL_Delay(1); }
    assert(SDL_GetAtomicInt(&awards) == 1);
    status = tick(); assert(!status.unsupported);
    RaClient_Shutdown(); free(body);
    puts("reference set: production memory coverage/lifetime safety/actual tournament definition passed (offline)");
    return 0;
}
