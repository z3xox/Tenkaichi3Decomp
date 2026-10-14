/*
 * Browser build: online play (port/src/gs/net.c, net_match.c, net_relay.c) is not part of it. Those files use UDP
 * sockets, threads and a matchmaking client, none of which exist in a browser in that form; the native builds keep
 * them unchanged. This is their interface in the state native builds are in when no match is running (sMode 0):
 * nothing is active, no names, no rollback, both views side by side, the lobby reports that it cannot work. The two
 * functions with logic that does not depend on a connection (Port_NetView, Port_HudSwap) keep that logic.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int Port_Setting(const char *name, int def);

static const char kUnavailable[] = "Online play is not available in the browser version.";

int Port_NetActive(void) { return 0; }
int Port_NetSession(void) { return 0; }
int Port_NetWarp(void) { return 0; }
int Port_NetResim(void) { return 0; }
int Port_NetMe(void) { return 0; }
int Port_NetVoiceAlt(void) { return 0; }
unsigned Port_NetSeed(void) { return 0; }       /* (0: the game's generator is left as it is) */
int Port_NetFixedStart(void) { return 0; }      /* (no session: the start-up follows the player's settings) */
void Port_TestSlow(int what, int amount) { (void)what; (void)amount; } /* (net.c's slow-machine testing: not here) */
const char *Port_NetName(int player) { (void)player; return ""; }
int Port_NetStats(int *out) { (void)out; return 0; }
void Port_NetBeginTick(unsigned tick) { (void)tick; }
void Port_NetArrived(void) {}
void Port_NetLeave(void) {}
void Port_NetOptions(int rollback, int delay) { (void)rollback; (void)delay; }
void Port_NetNameSet(const char *name) { (void)name; }
void Port_NetRulesSet(int type, int dp, int time) { (void)type; (void)dp; (void)time; }

/* net.c outside a session: the game's own speed (100%, plat_stub.c then uses the player's own game speed) */
int Port_NetSpeedMilli(void) { return 1000; }

/* net.c outside a match: no per-side camera type and screen shake (each side keeps its own save's, as before) */
int Port_NetSideOptions(int *camera, int *shake) { (void)camera; (void)shake; return 0; }

/* net.c with no rules set (sRules 0): single battle, no DP limit, time limit setting 0 */
void Port_NetRules(int *type, int *dp, int *time) { *type = 0; *dp = 0; *time = 0; }

/* Only called while a match is active (plat_stub.c checks Port_NetActive first): never here. */
void Port_NetInput(int player, unsigned char *data) {
    (void)player; (void)data;
    fprintf(stderr, "bt3: Port_NetInput called without online play\n");
    abort();
}

/* net.c: BT3_VIEW=0|1 shows one player's view full screen without a connection; otherwise both (-1). */
int Port_NetView(void) {
    static int forced = -2;
    if (forced == -2) {
        const char *e = getenv("BT3_VIEW");
        forced = e != NULL ? atoi(e) : -1;
    }
    return forced >= 0 ? (forced & 1) : -1;
}

/* net.c: BT3_HUD_SWAP=1 swaps the HUD's sides without a connection; the setting only acts in a match. */
int Port_HudSwap(void) {
    static int forced = -1;
    if (forced < 0) {
        forced = getenv("BT3_HUD_SWAP") != NULL ? atoi(getenv("BT3_HUD_SWAP")) != 0 : 0;
        (void)Port_Setting("net_hud_own", 1);
    }
    return forced;
}

/* The lobby (the Dragon Net Battle window): it says it cannot work. */
int Port_LobbyStart(int host, const char *address, int port) { (void)host; (void)address; (void)port; return -1; }
int Port_LobbyStartCode(int host, const char *code) { (void)host; (void)code; return -1; }
int Port_LobbyPoll(void) { return -1; }
void Port_LobbyCancel(void) {}
void Port_LobbyLaunch(void) {}
void Port_LobbyAuto(void) {}
int Port_LobbyRelayed(void) { return 0; }
int Port_LobbyOther(void) { return 0; }
const char *Port_LobbyCode(void) { return ""; }
const char *Port_LobbyError(void) { return kUnavailable; }
