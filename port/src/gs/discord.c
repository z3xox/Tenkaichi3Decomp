/*
 * Discord: what the player is doing shown on their Discord profile (Rich Presence), and its Join button, which
 * brings a friend into the lobby without typing its code.
 *
 * Nothing of it touches the game or the match: it reads the lobby's view (Port_LobbyView) and, for a Join, starts
 * the search by code exactly as the window does (Port_LobbyStartCode, Port_LobbyPoll). No library: the Discord
 * program on this computer listens on a local socket (discord-ipc-0 .. 9; a named pipe on Windows), and a message
 * is two little-endian 32-bit numbers (what it is, its length) and JSON. Nothing is sent over the network from
 * here, and without a running Discord nothing happens at all.
 *
 * It needs the number of a Discord application (free, discord.com/developers: the name shown as "Playing ..."):
 * DISCORD_APP_DEFAULT below (this fork's), the text setting discord_app_id, or BT3_DISCORD_APP ("0": none, which
 * turns all of this off). Settings: discord (1; 0 off),
 * discord_join (1; 0: the presence without the Join button, so the lobby's code is not handed to Discord).
 *
 * What Discord is given with the Join button: the lobby's room code, as the activity's join secret. Discord passes
 * it only to someone the player invites or who is let in ("Ask to Join"); it comes back here as ACTIVITY_JOIN in
 * that friend's game, which has to be running. A lobby by address has no code and so no button.
 *
 * Threads: the socket is a thread's of its own (discord_thread), so the game never waits for Discord. The game's
 * thread says what to show (Discord_Pump, once a blank, from plat_stub.c) and takes the code of a Join; both under
 * the lock.
 *
 * What comes in is not trusted: a frame's length is checked against the buffer (a longer one is passed over), and
 * of a Join only a secret of this file's own shape is taken ("c:" and a code of 4 to 7 capitals and digits).
 *
 *   BT3_DISCORD_APP=<number>   the application (over the setting)
 *   BT3_DISCORD_IPC=<path>     the socket to use instead of looking for Discord's (tests; not on Windows)
 *   BT3_DISCORD_LOG=1          each message sent and received, in the log
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include "net_lobby.h"

extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingText(const char *name, const char *def, char *out, int size);
extern void Port_UiNotice(const char *text);         /* ui.cpp: a line over the picture for a few seconds */
extern int Port_NetSession(void);                    /* gs/net.c */
extern void Port_NetOptions(int rollback, int delay);
extern void Port_NetNameSet(const char *name);
extern const char *Port_NetNameGet(void);
extern int Port_LobbyStartCode(int host, const char *code);
extern int Port_LobbyPoll(void);
extern void Port_LobbyCancel(void);
extern void Port_LobbyLaunch(void);
extern const char *Port_LobbyError(void);
extern int gPortResim;                               /* gs/state.c */

#define DISCORD_APP_DEFAULT "871835728198373456" /* (public: it names the application, it is no secret) */

enum {
    OP_HANDSHAKE = 0, OP_FRAME, OP_CLOSE, OP_PING, OP_PONG,
    RX = 16384,            /* a message from Discord at most (READY, the longest, is about 2 KB) */
    TEXT = 128,            /* a line of the presence (Discord's own limit) */
    RETRY_MS = 10000,      /* looking for Discord again */
    SEND_EVERY_MS = 4000,  /* Discord takes five changes of the activity in twenty seconds */
    VIEW_EVERY_MS = 1000,  /* the game's thread looks at the lobby this often */
    JOIN_WAIT_MS = 60000,  /* a Join that found nobody */
    CODE_MIN = 4, CODE_MAX = 7
};

typedef struct {
    char details[TEXT], state[TEXT];
    char party[40];   /* "" none */
    char secret[24];  /* "" no Join button */
    int size, max;
} Presence;

static SDL_SpinLock sLock;
static Presence sWant;       /* what the game's thread wants shown */
static unsigned sWantGen;    /* ... counted up with each change */
static char sJoinCode[CODE_MAX + 1]; /* a Join's code, until the game's thread takes it */
static char sApp[24];
static int sLog;
static char sAvatar[64];     /* this player's Discord picture, "<user's number>/<picture's hash>" ("" none, or not known yet) */
static long long sStarted;   /* the program's start (unix seconds): "elapsed" on the profile */

/* ------------------------------------------------------------------------------------------------ the socket */
#ifdef _WIN32
static HANDLE sPipe = INVALID_HANDLE_VALUE;

static int io_open(void) {
    int i;
    for (i = 0; i < 10; i++) {
        char path[64];
        snprintf(path, sizeof(path), "\\\\.\\pipe\\discord-ipc-%d", i);
        sPipe = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (sPipe != INVALID_HANDLE_VALUE) {
            return 1;
        }
    }
    return 0;
}

static void io_close(void) {
    if (sPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(sPipe);
        sPipe = INVALID_HANDLE_VALUE;
    }
}

static int io_write(const void *buf, int n) {
    const char *p = (const char *)buf;
    while (n > 0) {
        DWORD done = 0;
        if (!WriteFile(sPipe, p, (DWORD)n, &done, NULL) || done == 0) {
            return 0;
        }
        p += done;
        n -= (int)done;
    }
    return 1;
}

/* Up to n bytes that are there now (waiting `ms` at most for the first): their number, 0 none, -1 closed. */
static int io_read(void *buf, int n, int ms) {
    DWORD avail = 0, done = 0;
    if (!PeekNamedPipe(sPipe, NULL, 0, NULL, &avail, NULL)) {
        return -1;
    }
    if (avail == 0) {
        SDL_Delay((Uint32)ms);
        if (!PeekNamedPipe(sPipe, NULL, 0, NULL, &avail, NULL)) {
            return -1;
        }
        if (avail == 0) {
            return 0;
        }
    }
    if (!ReadFile(sPipe, buf, avail < (DWORD)n ? avail : (DWORD)n, &done, NULL)) {
        return -1;
    }
    return (int)done;
}

static int io_pid(void) { return (int)GetCurrentProcessId(); }
#else
static int sFd = -1;

static int io_try(const char *path) {
    struct sockaddr_un a;
    int fd;

    if (strlen(path) >= sizeof(a.sun_path)) {
        return 0;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd);
        return 0;
    }
#ifdef SO_NOSIGPIPE
    {
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)); /* (macOS: a Discord that went away is an error, not a signal) */
    }
#endif
    sFd = fd;
    return 1;
}

static int io_open(void) {
    /* where Discord puts its socket: the runtime folder (Linux), the temporary one (macOS); under them also the
       folders of its Flatpak and Snap packages */
    static const char *const kEnv[] = {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"};
    static const char *const kSub[] = {"", "app/com.discordapp.Discord/", "snap.discord/"};
    const char *given = getenv("BT3_DISCORD_IPC");
    unsigned e, s;
    int i;

    if (given != NULL && given[0] != '\0') {
        return io_try(given);
    }
    for (e = 0; e <= sizeof(kEnv) / sizeof(kEnv[0]); e++) {
        const char *dir = e < sizeof(kEnv) / sizeof(kEnv[0]) ? getenv(kEnv[e]) : "/tmp";
        if (dir == NULL || dir[0] == '\0') {
            continue;
        }
        for (s = 0; s < sizeof(kSub) / sizeof(kSub[0]); s++) {
            for (i = 0; i < 10; i++) {
                char path[256];
                snprintf(path, sizeof(path), "%s%s%s" "discord-ipc-%d", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", kSub[s], i);
                if (io_try(path)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void io_close(void) {
    if (sFd >= 0) {
        close(sFd);
        sFd = -1;
    }
}

static int io_write(const void *buf, int n) {
    const char *p = (const char *)buf;
    while (n > 0) {
#ifdef MSG_NOSIGNAL
        ssize_t done = send(sFd, p, (size_t)n, MSG_NOSIGNAL);
#else
        ssize_t done = send(sFd, p, (size_t)n, 0);
#endif
        if (done < 0 && errno == EINTR) {
            continue;
        }
        if (done <= 0) {
            return 0;
        }
        p += done;
        n -= (int)done;
    }
    return 1;
}

/* Up to n bytes that are there now (waiting `ms` at most for the first): their number, 0 none, -1 closed. */
static int io_read(void *buf, int n, int ms) {
    struct pollfd p;
    ssize_t got;

    p.fd = sFd;
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, ms) <= 0) {
        return 0;
    }
    got = recv(sFd, buf, (size_t)n, 0);
    if (got < 0 && (errno == EINTR || errno == EAGAIN)) {
        return 0;
    }
    return got <= 0 ? -1 : (int)got;
}

static int io_pid(void) { return (int)getpid(); }
#endif

/* -------------------------------------------------------------------------------------------------- messages */
static int frame_send(uint32_t op, const char *json) {
    uint8_t head[8];
    uint32_t len = (uint32_t)strlen(json);

    head[0] = (uint8_t)op; head[1] = head[2] = head[3] = 0; /* (little-endian whatever the machine) */
    head[4] = (uint8_t)len; head[5] = (uint8_t)(len >> 8); head[6] = (uint8_t)(len >> 16); head[7] = (uint8_t)(len >> 24);
    if (sLog) {
        fprintf(stderr, "bt3: discord: > %u %s\n", (unsigned)op, json);
    }
    return io_write(head, 8) && io_write(json, (int)len);
}

/* Text into a JSON string: quotes and backslashes escaped, control characters left out, and only whole, well-formed
   UTF-8 characters (a player's name is 20 bytes of anything; cut in the middle of a character it would make the
   whole message one Discord refuses). */
static void json_text(char *out, int size, const char *in) {
    const unsigned char *s = (const unsigned char *)in;
    int o = 0;

    while (*s != 0 && o < size - 7) {
        int n = *s < 0x80 ? 1 : (*s & 0xE0) == 0xC0 && *s >= 0xC2 ? 2 : (*s & 0xF0) == 0xE0 ? 3 : (*s & 0xF8) == 0xF0 && *s <= 0xF4 ? 4 : 0;
        int k, ok = n > 0;
        for (k = 1; ok && k < n; k++) {
            ok = (s[k] & 0xC0) == 0x80;
        }
        if (!ok) {
            s++;
            continue;
        }
        if (n == 1) {
            if (*s == '"' || *s == '\\') {
                out[o++] = '\\';
                out[o++] = (char)*s;
            } else if (*s >= 32 && *s != 127) {
                out[o++] = (char)*s;
            }
        } else {
            for (k = 0; k < n; k++) {
                out[o++] = (char)s[k];
            }
        }
        s += n;
    }
    out[o] = 0;
}

/* The string value of "key" in a message, unescaped as far as a code or a name of an event needs (\" and \\);
   0 when it is not there or does not fit. */
static int json_get(const char *msg, const char *key, char *out, int size) {
    char want[40];
    const char *p = msg;
    int o = 0;

    snprintf(want, sizeof(want), "\"%s\"", key);
    while ((p = strstr(p, want)) != NULL) {
        p += strlen(want);
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p != ':') {
            continue;
        }
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p != '"') {
            continue;
        }
        for (p++; *p != 0 && *p != '"'; p++) {
            if (*p == '\\' && p[1] != 0) {
                p++;
            }
            if (o >= size - 1) {
                return 0;
            }
            out[o++] = *p;
        }
        out[o] = 0;
        return *p == '"';
    }
    return 0;
}

/* A Join's secret is this file's own: "c:" and a room code. Anything else is not taken. */
static int secret_code(const char *secret, char code[CODE_MAX + 1]) {
    int n;
    if (secret[0] != 'c' || secret[1] != ':') {
        return 0;
    }
    secret += 2;
    for (n = 0; secret[n] != 0; n++) {
        if (n >= CODE_MAX || !((secret[n] >= 'A' && secret[n] <= 'Z') || (secret[n] >= '0' && secret[n] <= '9'))) {
            return 0;
        }
        code[n] = secret[n];
    }
    code[n] = 0;
    return n >= CODE_MIN;
}

static int activity_send(const Presence *p, unsigned nonce) {
    char msg[1024], details[TEXT * 2], state[TEXT * 2];
    int n;

    json_text(details, sizeof(details), p->details);
    json_text(state, sizeof(state), p->state);
    n = snprintf(msg, sizeof(msg), "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":{\"details\":\"%s\"", io_pid(), details);
    if (state[0] != 0 && state[1] != 0) { /* (Discord wants two characters at least) */
        n += snprintf(msg + n, sizeof(msg) - (size_t)n, ",\"state\":\"%s\"", state);
    }
    n += snprintf(msg + n, sizeof(msg) - (size_t)n, ",\"timestamps\":{\"start\":%lld},\"assets\":{\"large_image\":\"logo\"}", sStarted);
    if (p->party[0] != 0) {
        n += snprintf(msg + n, sizeof(msg) - (size_t)n, ",\"party\":{\"id\":\"%s\",\"size\":[%d,%d]}", p->party, p->size, p->max);
        if (p->secret[0] != 0) {
            n += snprintf(msg + n, sizeof(msg) - (size_t)n, ",\"secrets\":{\"join\":\"%s\"}", p->secret);
        }
    }
    n += snprintf(msg + n, sizeof(msg) - (size_t)n, ",\"instance\":true}},\"nonce\":\"a%u\"}", nonce);
    return n < (int)sizeof(msg) && frame_send(OP_FRAME, msg);
}

/* One message from Discord. 0: the connection is over. */
static int frame_take(uint32_t op, char *json, int *ready) {
    char evt[32], secret[64], code[CODE_MAX + 1];

    if (sLog) {
        fprintf(stderr, "bt3: discord: < %u %.300s\n", (unsigned)op, json);
    }
    if (op == OP_PING) {
        return frame_send(OP_PONG, json);
    }
    if (op == OP_CLOSE) {
        fprintf(stderr, "bt3: discord: closed by Discord: %.200s\n", json); /* (a wrong application number is said here) */
        return 0;
    }
    if (op != OP_FRAME || !json_get(json, "evt", evt, sizeof(evt))) {
        return 1;
    }
    if (strcmp(evt, "READY") == 0) {
        const char *user = strstr(json, "\"user\"");
        char id[24], hash[40], both[64];
        /* who is signed in to Discord here: the number and the picture's hash name the profile picture, which the
           lobby shows beside this player's name (gs/avatar.c, which takes nothing but digits and hex) */
        if (user != NULL && json_get(user, "id", id, sizeof(id)) && json_get(user, "avatar", hash, sizeof(hash))) {
            snprintf(both, sizeof(both), "%s/%s", id, hash);
            SDL_LockSpinlock(&sLock);
            snprintf(sAvatar, sizeof(sAvatar), "%s", both);
            SDL_UnlockSpinlock(&sLock);
        }
        *ready = 1;
        fprintf(stderr, "bt3: discord: connected\n");
        return frame_send(OP_FRAME, "{\"cmd\":\"SUBSCRIBE\",\"evt\":\"ACTIVITY_JOIN\",\"nonce\":\"s1\"}");
    }
    if (strcmp(evt, "ACTIVITY_JOIN") == 0 && json_get(json, "secret", secret, sizeof(secret)) && secret_code(secret, code)) {
        SDL_LockSpinlock(&sLock);
        memcpy(sJoinCode, code, sizeof(sJoinCode));
        SDL_UnlockSpinlock(&sLock);
    }
    return 1;
}

static void wait_ms(int ms) {
    SDL_Delay((Uint32)ms);
}

static int SDLCALL discord_thread(void *unused) {
    static uint8_t rx[RX + 1];
    (void)unused;

    for (;;) {
        char hello[80];
        int have = 0, ready = 0;
        uint32_t skip = 0; /* bytes still to pass over, of a message longer than the buffer */
        unsigned sentGen = 0, nonce = 0;
        int sentAny = 0;
        Uint64 sentAt = 0;

        if (!io_open()) {
            wait_ms(RETRY_MS);
            continue;
        }
        snprintf(hello, sizeof(hello), "{\"v\":1,\"client_id\":\"%s\"}", sApp);
        if (frame_send(OP_HANDSHAKE, hello)) {
            for (;;) {
                int got = io_read(rx + have, RX - have, 200);
                Presence want;
                unsigned gen;
                int alive = 1;

                if (got < 0) {
                    break;
                }
                have += got;
                while (alive && have > 0) {
                    uint32_t op, len;
                    if (skip > 0) {
                        uint32_t n = skip < (uint32_t)have ? skip : (uint32_t)have;
                        memmove(rx, rx + n, (size_t)have - n);
                        have -= (int)n;
                        skip -= n;
                        continue;
                    }
                    if (have < 8) {
                        break;
                    }
                    op = rx[0] | (uint32_t)rx[1] << 8 | (uint32_t)rx[2] << 16 | (uint32_t)rx[3] << 24;
                    len = rx[4] | (uint32_t)rx[5] << 8 | (uint32_t)rx[6] << 16 | (uint32_t)rx[7] << 24;
                    if (len > RX - 8) { /* longer than anything this file reads: passed over */
                        skip = len;
                        memmove(rx, rx + 8, (size_t)have - 8);
                        have -= 8;
                        continue;
                    }
                    if ((uint32_t)have < 8 + len) {
                        break;
                    }
                    {
                        uint8_t after = rx[8 + len];
                        rx[8 + len] = 0; /* (the JSON as a C string; the byte is the next message's first) */
                        alive = frame_take(op, (char *)rx + 8, &ready);
                        rx[8 + len] = after;
                    }
                    memmove(rx, rx + 8 + len, (size_t)have - 8 - len);
                    have -= (int)(8 + len);
                }
                if (!alive) {
                    break;
                }
                SDL_LockSpinlock(&sLock);
                want = sWant;
                gen = sWantGen;
                SDL_UnlockSpinlock(&sLock);
                if (ready && gen != 0 && (!sentAny || gen != sentGen) && (!sentAny || SDL_GetTicks() - sentAt >= SEND_EVERY_MS)) {
                    if (!activity_send(&want, ++nonce)) {
                        break;
                    }
                    sentAny = 1;
                    sentGen = gen;
                    sentAt = SDL_GetTicks();
                }
            }
        }
        io_close();
        wait_ms(RETRY_MS);
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------ the game's thread */
/* A name for the party that is the same in every member's game and does not give the code away (FNV-1a). */
static void party_id(char out[40], const char *code) {
    uint32_t h = 2166136261u;
    const char *salt = "bt3-lobby:";
    for (; *salt != 0; salt++) {
        h = (h ^ (uint8_t)*salt) * 16777619u;
    }
    for (; *code != 0; code++) {
        h = (h ^ (uint8_t)*code) * 16777619u;
    }
    snprintf(out, 40, "bt3-%08x", (unsigned)h);
}

static void presence_now(Presence *p) {
    static PortLobbyView v; /* (static: it is large) */
    int i;

    memset(p, 0, sizeof(*p));
    if (Port_LobbyView(&v) && v.state == 2) {
        const char *one = v.name[0], *two = v.name[1];
        for (i = 0; i < LOBBY_SLOTS; i++) {
            p->size += v.taken[i] != 0;
        }
        p->max = LOBBY_SLOTS;
        if (v.mode == 1 && v.tour == 2 && v.tourChamp >= 0 && v.tourChamp < LOBBY_TOUR) {
            snprintf(p->details, sizeof(p->details), "Tournament over");
            snprintf(p->state, sizeof(p->state), "Champion: %s", v.tourName[v.tourChamp]);
        } else if (v.mode == 1 && v.tour == 1) {
            snprintf(p->details, sizeof(p->details), "Tournament: %d entrants", v.tourCount);
            if (v.phase == 1) {
                snprintf(p->state, sizeof(p->state), "%s vs %s", one, two);
            } else {
                snprintf(p->state, sizeof(p->state), "Between matches");
            }
        } else {
            snprintf(p->details, sizeof(p->details), "%s", v.mode == 1 ? "Tournament lobby" : "Online lobby");
            if (v.phase == 1) {
                snprintf(p->state, sizeof(p->state), "%s vs %s", one, two);
            } else {
                snprintf(p->state, sizeof(p->state), "%s", "Waiting for players");
            }
        }
        if (v.code[0] != 0) {
            char code[CODE_MAX + 1];
            char secret[sizeof(p->secret)];
            party_id(p->party, v.code);
            snprintf(secret, sizeof(secret), "c:%s", v.code);
            /* (the button only while someone can still come in, and only for a code of the shape a Join takes) */
            if (p->size < p->max && secret_code(secret, code) && Port_Setting("discord_join", 1) != 0) {
                snprintf(p->secret, sizeof(p->secret), "%s", secret); /* (not its bytes after the end: the presence is compared whole) */
            }
        }
    } else if (Port_NetSession()) {
        snprintf(p->details, sizeof(p->details), "Online match");
    } else {
        snprintf(p->details, sizeof(p->details), "In the menus");
    }
}

/* A Join from Discord: the code is looked up as the window's Join does; the main menu goes to the lobby's screen
   by itself once this game is its member (Port_LobbyWantsScreen). */
static void join_pump(void) {
    static int joining;
    static Uint64 since;
    static PortLobbyView v;
    char code[CODE_MAX + 1];
    int state;

    SDL_LockSpinlock(&sLock);
    memcpy(code, sJoinCode, sizeof(code));
    sJoinCode[0] = 0;
    SDL_UnlockSpinlock(&sLock);
    if (code[0] != 0) {
        if (Port_NetSession() || (Port_LobbyView(&v) && v.state != 0)) {
            Port_UiNotice("Discord: leave your lobby or match first, then press Join again.");
        } else {
            Port_LobbyCancel(); /* (a search of the screens' that was under way) */
            Port_NetOptions(-1, -1);
            Port_NetNameSet(Port_NetNameGet());
            if (Port_LobbyStartCode(0, code) < 0) {
                Port_UiNotice("Discord: the network could not be opened on this PC.");
            } else {
                fprintf(stderr, "bt3: discord: joining a lobby by its code\n");
                joining = 1;
                since = SDL_GetTicks();
            }
        }
    }
    if (!joining) {
        return;
    }
    state = Port_LobbyPoll();
    if (state == 5) { /* this game is the lobby's member (net_lobby.c has the socket now) */
        Port_LobbyCancel();
        Port_UiNotice("You are in the lobby. Its screen opens from the main menu.");
        joining = 0;
    } else if (state == 2) { /* (a game of two, of a release before lobbies) */
        Port_LobbyLaunch();
        joining = 0;
    } else if (state <= 0 || SDL_GetTicks() - since > JOIN_WAIT_MS) {
        if (state == -3 && Port_LobbyError()[0] != 0) {
            Port_UiNotice(Port_LobbyError());
        } else if (state == -2) {
            Port_UiNotice("Discord: the other game is another version: both need the same release.");
        } else if (state != 0) { /* (0: the screens took the search over and ended it themselves) */
            Port_UiNotice("Discord: the lobby could not be joined.");
        }
        if (state != 0) {
            Port_LobbyCancel();
        }
        joining = 0;
    }
}

/* This player's picture as the lobby hands it on ("" none): BT3_AVATAR (tests), else the Discord profile's. */
void Discord_Avatar(char *out, int size) {
    const char *env = getenv("BT3_AVATAR");
    if (env != NULL) {
        snprintf(out, (size_t)size, "%s", env);
        return;
    }
    SDL_LockSpinlock(&sLock);
    snprintf(out, (size_t)size, "%s", sAvatar);
    SDL_UnlockSpinlock(&sLock);
}

/* Once a blank (plat_stub.c: Port_VBlank). */
void Discord_Pump(void) {
    static int started; /* 0 not looked at yet, 1 running, -1 off */
    static Uint64 viewed;
    Presence now;

    if (started < 0 || gPortResim) {
        return;
    }
    if (!started) {
        const char *env = getenv("BT3_DISCORD_APP");
        SDL_Thread *t;
        size_t i, n;

        started = -1;
        if (env != NULL) {
            snprintf(sApp, sizeof(sApp), "%s", env);
        } else {
            Port_SettingText("discord_app_id", DISCORD_APP_DEFAULT, sApp, sizeof(sApp));
        }
        n = strlen(sApp);
        for (i = 0; i < n && sApp[i] >= '0' && sApp[i] <= '9'; i++) {
        }
        if (n < 15 || i != n || Port_Setting("discord", 1) == 0) { /* (an application's number: 17 to 20 digits) */
            return;
        }
        sLog = getenv("BT3_DISCORD_LOG") != NULL;
        sStarted = (long long)time(NULL);
        t = SDL_CreateThread(discord_thread, "bt3-discord", NULL);
        if (t == NULL) {
            return;
        }
        SDL_DetachThread(t);
        started = 1;
    }
    join_pump();
    if (viewed != 0 && SDL_GetTicks() - viewed < VIEW_EVERY_MS) {
        return;
    }
    viewed = SDL_GetTicks() | 1;
    presence_now(&now);
    SDL_LockSpinlock(&sLock);
    if (sWantGen == 0 || memcmp(&now, &sWant, sizeof(now)) != 0) {
        sWant = now;
        sWantGen++;
    }
    SDL_UnlockSpinlock(&sLock);
}
