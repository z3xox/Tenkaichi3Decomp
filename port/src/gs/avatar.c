/*
 * The players' pictures: each player's Discord profile picture, shown beside their name on a lobby's bracket.
 *
 * A picture is named by a reference, "<Discord user's number>/<picture's hash>", which a player's own game learns
 * from the Discord program on its computer (gs/discord.c) and the lobby hands to its other members (gs/net_lobby.c).
 * Nothing but that reference travels between the games: each game fetches the picture itself from Discord's own
 * picture server, and only from there. A reference is taken only when it is digits, a slash and a hash of hex
 * digits (an animated picture's "a_" before it), so what another game sends can name a picture of Discord's and
 * nothing else: no address, no path.
 *
 * The screens (src/menu/port_tbo.c, the game's thread) ask for a reference's picture number each frame they draw it
 * (PortAvatar_Id): -1 until it is here, and the first asking starts the fetch, on a thread of this file's own, so
 * the game never waits for the network. The renderer asks for a number's pixels (Avatar_Pixels) and makes its
 * texture, as it does a line of letters' (gs_draw.c, tbo_texture). A picture is kept as a round cut of 96 x 96.
 * AVATAR_SLOTS are kept; the one longest unasked gives its place to a new one.
 *
 * Nothing here is game state: it is a picture on a menu.
 *
 *   BT3_AVATAR=<reference>     this player's own reference, instead of Discord's (tests; gs/discord.c)
 *   BT3_AVATAR_BASE=<address>  fetched from <address>/<reference>.png?size=128 instead of Discord's server (tests)
 */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../include/menu/tbo_art.h"

extern int Port_HttpGetMem(const char *url, char **mem, size_t *memLen, size_t max); /* gs/net_update.c */

enum { REF = 64, SIDE = 96, FILE_MAX = 512 * 1024, RETRY_MS = 60000 };
enum { EMPTY = 0, WANTED, READY, FAILED };

typedef struct {
    char ref[REF];
    int state;
    uint32_t version;       /* counted up when the place gets another picture: the renderer's texture is old then */
    Uint64 asked, failedAt;
    uint32_t px[SIDE * SIDE]; /* as the renderer takes them: R, G, B and the GS's alpha (0x80 opaque) */
} Slot;

static Slot sSlot[TBO_AVATAR_SLOTS];
static SDL_Mutex *sLock;
static SDL_Condition *sWake;
static SDL_Thread *sThread;
static SDL_InitState sInit;

/* 1: "<15 to 22 digits>/<32 hex digits>", or the same with "a_" before the hash. */
int Avatar_RefOk(const char *ref) {
    int n = 0, k;
    if (ref == NULL) {
        return 0;
    }
    while (ref[n] >= '0' && ref[n] <= '9') {
        n++;
    }
    if (n < 15 || n > 22 || ref[n] != '/') {
        return 0;
    }
    ref += n + 1;
    if (ref[0] == 'a' && ref[1] == '_') {
        ref += 2;
    }
    for (k = 0; k < 32; k++) {
        if (!((ref[k] >= '0' && ref[k] <= '9') || (ref[k] >= 'a' && ref[k] <= 'f'))) {
            return 0;
        }
    }
    return ref[32] == '\0';
}

/* The fetched file into a place's pixels: scaled to SIDE x SIDE, cut round with a soft edge. */
static int decode(const char *mem, size_t len, uint32_t *out) {
    SDL_Surface *png = SDL_LoadPNG_IO(SDL_IOFromConstMem(mem, len), true), *rgba, *small;
    int x, y;

    if (png == NULL) {
        return 0;
    }
    rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(png);
    if (rgba == NULL) {
        return 0;
    }
    small = rgba->w > 0 && rgba->h > 0 && rgba->w <= 4096 && rgba->h <= 4096 ? SDL_ScaleSurface(rgba, SIDE, SIDE, SDL_SCALEMODE_LINEAR) : NULL;
    SDL_DestroySurface(rgba);
    if (small == NULL) {
        return 0;
    }
    for (y = 0; y < SIDE; y++) {
        const uint8_t *row = (const uint8_t *)small->pixels + (size_t)y * (size_t)small->pitch;
        for (x = 0; x < SIDE; x++) {
            const uint8_t *c = row + x * 4;
            float dx = (float)x + 0.5f - SIDE * 0.5f, dy = (float)y + 0.5f - SIDE * 0.5f;
            float edge = SIDE * 0.5f - SDL_sqrtf(dx * dx + dy * dy); /* inside the circle by this many pixels */
            float cover = edge >= 1.0f ? 1.0f : edge <= 0.0f ? 0.0f : edge;
            uint32_t a = (uint32_t)((float)c[3] * cover * 128.0f / 255.0f + 0.5f);
            out[y * SIDE + x] = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | (a << 24);
        }
    }
    SDL_DestroySurface(small);
    return 1;
}

static int SDLCALL fetch_thread(void *unused) {
    static uint32_t px[SIDE * SIDE];
    (void)unused;

    SDL_LockMutex(sLock);
    for (;;) {
        char ref[REF], url[256], *mem = NULL;
        const char *base = SDL_getenv("BT3_AVATAR_BASE");
        size_t len = 0;
        int i, ok;

        for (i = 0; i < TBO_AVATAR_SLOTS && sSlot[i].state != WANTED; i++) {
        }
        if (i == TBO_AVATAR_SLOTS) {
            SDL_WaitCondition(sWake, sLock);
            continue;
        }
        SDL_strlcpy(ref, sSlot[i].ref, sizeof(ref));
        SDL_UnlockMutex(sLock);
        SDL_snprintf(url, sizeof(url), "%s/%s.png?size=128", base != NULL && base[0] != '\0' ? base : "https://cdn.discordapp.com/avatars", ref);
        ok = Port_HttpGetMem(url, &mem, &len, FILE_MAX) && len > 8 && decode(mem, len, px);
        SDL_free(mem);
        SDL_LockMutex(sLock);
        if (SDL_strcmp(sSlot[i].ref, ref) == 0 && sSlot[i].state == WANTED) { /* (the place still wants this one) */
            if (ok) {
                SDL_memcpy(sSlot[i].px, px, sizeof(px));
                sSlot[i].version++;
                sSlot[i].state = READY;
            } else {
                sSlot[i].state = FAILED;
                sSlot[i].failedAt = SDL_GetTicks();
                fprintf(stderr, "bt3: avatar: a player's picture could not be fetched\n");
            }
        }
    }
    return 0;
}

/* The picture number of a reference (TBO_AVATAR_BASE and on), -1 while it is not here (or never will be). */
int PortAvatar_Id(const char *ref) {
    Uint64 now = SDL_GetTicks();
    int i, spare = -1, id = -1;

    if (!Avatar_RefOk(ref)) {
        return -1;
    }
    if (SDL_ShouldInit(&sInit)) {
        sLock = SDL_CreateMutex();
        sWake = SDL_CreateCondition();
        sThread = sLock != NULL && sWake != NULL ? SDL_CreateThread(fetch_thread, "bt3-avatar", NULL) : NULL;
        if (sThread != NULL) {
            SDL_DetachThread(sThread);
        }
        SDL_SetInitialized(&sInit, sThread != NULL);
    }
    if (sThread == NULL) {
        return -1;
    }
    SDL_LockMutex(sLock);
    for (i = 0; i < TBO_AVATAR_SLOTS; i++) {
        Slot *s = &sSlot[i];
        if (s->state != EMPTY && SDL_strcmp(s->ref, ref) == 0) {
            s->asked = now;
            if (s->state == FAILED && now - s->failedAt > RETRY_MS) {
                s->state = WANTED;
                SDL_SignalCondition(sWake);
            }
            id = s->state == READY ? TBO_AVATAR_BASE + i : -1;
            break;
        }
        if (s->state == EMPTY) { /* (for a new one: an empty place, else the one longest unasked) */
            if (spare < 0 || sSlot[spare].state != EMPTY) {
                spare = i;
            }
        } else if (spare < 0 || (sSlot[spare].state != EMPTY && s->asked < sSlot[spare].asked)) {
            spare = i;
        }
    }
    if (i == TBO_AVATAR_SLOTS && spare >= 0) {
        Slot *s = &sSlot[spare];
        SDL_strlcpy(s->ref, ref, sizeof(s->ref));
        s->state = WANTED;
        s->asked = now;
        SDL_SignalCondition(sWake);
    }
    SDL_UnlockMutex(sLock);
    return id;
}

/* For the renderer: which picture a number shows now, and a copy of its pixels (the caller's to free), NULL none. */
uint32_t Avatar_Version(uint32_t id) {
    uint32_t v = 0;
    if (sLock != NULL && id >= TBO_AVATAR_BASE && id < TBO_AVATAR_BASE + TBO_AVATAR_SLOTS) {
        SDL_LockMutex(sLock);
        v = sSlot[id - TBO_AVATAR_BASE].version;
        SDL_UnlockMutex(sLock);
    }
    return v;
}

uint32_t *Avatar_Pixels(uint32_t id, uint32_t *version, uint32_t *w, uint32_t *h) {
    uint32_t *px = NULL;
    if (sLock == NULL || id < TBO_AVATAR_BASE || id >= TBO_AVATAR_BASE + TBO_AVATAR_SLOTS) {
        return NULL;
    }
    SDL_LockMutex(sLock);
    if (sSlot[id - TBO_AVATAR_BASE].state == READY && (px = malloc(sizeof(sSlot[0].px))) != NULL) {
        memcpy(px, sSlot[id - TBO_AVATAR_BASE].px, sizeof(sSlot[0].px));
        *version = sSlot[id - TBO_AVATAR_BASE].version;
        *w = SIDE;
        *h = SIDE;
    }
    SDL_UnlockMutex(sLock);
    return px;
}
