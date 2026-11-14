/*
 * Squads: the team a player plays for, within a group of people who play together (a company, a Discord server).
 *
 * A player chooses a group and one of its squads on the Player Card (src/menu/port_tbo.c); the squad's tag then goes
 * before the name the others see, "[APR] Henrique". That is all that travels: the name is the one the netcode had,
 * so nothing in a lobby's or a match's packets changes, and a copy without squads shows the tag as part of the name.
 * Every copy reads the tag back out of a name (PortSquad_NameColour) to colour it and to count a tournament's score
 * by squad. A player with no group has no tag: friends from outside any group play as they always did.
 *
 * The groups and their squads are a list anyone can change without a new build: the matchmaking service keeps it
 * (port/matchmaking/supabase: GET <service>/squads, a line per squad, "group <tab> its name <tab> tag <tab> squad's
 * name <tab> colour as RRGGBB"). It is fetched once per start, on a thread, and kept in squads.tsv beside the
 * program for the next start and for playing without the service; before the first fetch the list is the one built
 * in below. What is fetched is only ever text shown and a colour: a tag is 1 to 4 capitals or digits, a name 23
 * bytes without control characters, anything else is left out.
 *
 * Settings: squad_group (the group's id, "" none), squad_tag.
 *   BT3_SQUAD=<TAG>       the tag for this start, whatever the settings say (tests; "" none)
 *   BT3_SQUADS_NOFETCH=1  the service is not asked (tests)
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Port_SettingText(const char *name, const char *def, char *out, int size); /* plat_settings.c */
extern void Port_SettingTextSave(const char *name, const char *value);
extern void Port_SettingsWrite(void);
extern int Port_HttpGetMem(const char *url, char **mem, size_t *memLen, size_t max);  /* gs/net_update.c */
extern const char *Match_Url(void);                                                    /* gs/net_match.c */

enum { GROUPS = 8, SQUADS = 8, TAG = 5, NAME = 24, ID = 12 };

typedef struct {
    char tag[TAG], name[NAME];
    int rgb;
} Squad;

typedef struct {
    char id[ID], name[NAME];
    int count;
    Squad sq[SQUADS];
} Group;

typedef struct {
    int count;
    Group g[GROUPS];
} List;

static const char kBuiltIn[] =
    "onfly\tOnfly\tAPR\tApproval\t009EFB\n"
    "onfly\tOnfly\tEXP\tExpense\t1AAD6F\n"
    "onfly\tOnfly\tTRV\tTravel\t6C63FF\n"
    "onfly\tOnfly\tMGT\tManagement\tFF5A05\n"
    "bsk\tBSK\tBSK\tBSK\tE0245E\n";

static List sList;            /* the game's thread's */
static List sFetched;         /* the fetch thread's, handed over when sFetchedReady is set */
static SDL_AtomicInt sFetchedReady;
static int sLoaded;

static int tag_ok(const char *t) {
    int n;
    for (n = 0; t[n] != '\0'; n++) {
        if (n >= TAG - 1 || !((t[n] >= 'A' && t[n] <= 'Z') || (t[n] >= '0' && t[n] <= '9'))) {
            return 0;
        }
    }
    return n > 0;
}

static int text_ok(const char *t, int max) {
    int n;
    for (n = 0; t[n] != '\0'; n++) {
        if (n >= max - 1 || (unsigned char)t[n] < 32 || t[n] == 127) {
            return 0;
        }
    }
    return n > 0;
}

/* The list's text into `out` (emptied first). Lines that are not a squad's are passed over. */
static void parse(const char *text, List *out) {
    memset(out, 0, sizeof(*out));
    while (*text != '\0') {
        char line[160], *f[5], *p;
        const char *end = strchr(text, '\n');
        size_t len = end != NULL ? (size_t)(end - text) : strlen(text);
        int n = 0, gi, k;
        unsigned rgb = 0;
        Group *g;

        snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line) - 1 ? len : sizeof(line) - 1), text);
        text += len + (end != NULL ? 1 : 0);
        for (p = line; n < 5; n++) {
            f[n] = p;
            p = strchr(p, '\t');
            if (p == NULL) {
                n++;
                break;
            }
            *p++ = '\0';
        }
        if (n != 5) {
            continue;
        }
        f[4][strcspn(f[4], "\r")] = '\0';
        if (!text_ok(f[0], ID) || !text_ok(f[1], NAME) || !tag_ok(f[2]) || !text_ok(f[3], NAME) || strlen(f[4]) != 6 ||
            strspn(f[4], "0123456789abcdefABCDEF") != 6) {
            continue;
        }
        sscanf(f[4], "%x", &rgb);
        for (gi = 0; gi < out->count && strcmp(out->g[gi].id, f[0]) != 0; gi++) {
        }
        if (gi == out->count) {
            if (out->count == GROUPS) {
                continue;
            }
            out->count++;
            snprintf(out->g[gi].id, ID, "%s", f[0]);
            snprintf(out->g[gi].name, NAME, "%s", f[1]);
        }
        g = &out->g[gi];
        for (k = 0; k < g->count && strcmp(g->sq[k].tag, f[2]) != 0; k++) {
        }
        if (k < g->count || g->count == SQUADS) {
            continue;
        }
        snprintf(g->sq[k].tag, TAG, "%s", f[2]);
        snprintf(g->sq[k].name, NAME, "%s", f[3]);
        g->sq[k].rgb = (int)rgb;
        g->count++;
    }
}

static int SDLCALL fetch_thread(void *unused) {
    char url[512], *mem = NULL;
    size_t len = 0;
    (void)unused;

    snprintf(url, sizeof(url), "%s/squads", Match_Url());
    if (Port_HttpGetMem(url, &mem, &len, 16384) && mem != NULL) {
        parse(mem, &sFetched);
        if (sFetched.count > 0) {
            FILE *f = fopen("squads.tsv", "wb");
            if (f != NULL) {
                fwrite(mem, 1, len, f);
                fclose(f);
            }
            SDL_SetAtomicInt(&sFetchedReady, 1);
        }
    }
    SDL_free(mem);
    return 0;
}

/* The list as it is now (the game's thread): built in or the file's at first, the service's once it has answered. */
static List *list(void) {
    if (!sLoaded) {
        size_t len = 0;
        char *file = (char *)SDL_LoadFile("squads.tsv", &len);
        SDL_Thread *t;

        sLoaded = 1;
        if (file != NULL) {
            parse(file, &sList);
            SDL_free(file);
        }
        if (sList.count == 0) {
            parse(kBuiltIn, &sList);
        }
        if (getenv("BT3_SQUADS_NOFETCH") == NULL && getenv("BT3_PAD_PLAY") == NULL && getenv("BT3_SYNCTEST") == NULL) {
            t = SDL_CreateThread(fetch_thread, "bt3-squads", NULL);
            if (t != NULL) {
                SDL_DetachThread(t);
            }
        }
    }
    if (SDL_GetAtomicInt(&sFetchedReady) == 1) {
        sList = sFetched;
        SDL_SetAtomicInt(&sFetchedReady, 2);
    }
    return &sList;
}

/* ---- for the Player Card: group 0 is "no group", group g > 0 is the list's g - 1 ---- */
int PortSquad_Groups(void) {
    return list()->count + 1;
}

const char *PortSquad_GroupName(int g) {
    List *l = list();
    return g >= 1 && g <= l->count ? l->g[g - 1].name : "None";
}

int PortSquad_Count(int g) {
    List *l = list();
    return g >= 1 && g <= l->count ? l->g[g - 1].count : 0;
}

const char *PortSquad_Name(int g, int s) {
    List *l = list();
    return g >= 1 && g <= l->count && s >= 0 && s < l->g[g - 1].count ? l->g[g - 1].sq[s].name : "";
}

const char *PortSquad_Tag(int g, int s) {
    List *l = list();
    return g >= 1 && g <= l->count && s >= 0 && s < l->g[g - 1].count ? l->g[g - 1].sq[s].tag : "";
}

/* This player's group and squad (0, 0: none), by what the settings name; a squad the list no longer has is none. */
void PortSquad_Mine(int *g, int *s) {
    List *l = list();
    char id[ID], tag[TAG + 3];
    int gi, k;

    *g = 0;
    *s = 0;
    Port_SettingText("squad_group", "", id, sizeof(id));
    Port_SettingText("squad_tag", "", tag, sizeof(tag));
    for (gi = 0; gi < l->count; gi++) {
        if (strcmp(l->g[gi].id, id) == 0) {
            *g = gi + 1;
            for (k = 0; k < l->g[gi].count; k++) {
                if (strcmp(l->g[gi].sq[k].tag, tag) == 0) {
                    *s = k;
                }
            }
        }
    }
}

void PortSquad_Choose(int g, int s) {
    List *l = list();
    int in = g >= 1 && g <= l->count && l->g[g - 1].count > 0;

    if (in && (s < 0 || s >= l->g[g - 1].count)) {
        s = 0;
    }
    Port_SettingTextSave("squad_group", in ? l->g[g - 1].id : "");
    Port_SettingTextSave("squad_tag", in ? l->g[g - 1].sq[s].tag : "");
    Port_SettingsWrite();
}

/* The tag that goes before this player's name, "" none. */
const char *PortSquad_MyTag(void) {
    static char tag[TAG];
    const char *env = getenv("BT3_SQUAD");
    int g, s;

    if (env != NULL) {
        snprintf(tag, sizeof(tag), "%s", tag_ok(env) ? env : "");
        return tag;
    }
    PortSquad_Mine(&g, &s);
    snprintf(tag, sizeof(tag), "%s", PortSquad_Tag(g, s));
    return tag;
}

/* The tag a name begins with ("[TAG] name"): copied to out (TAG bytes), 1; 0 and "" when it has none. */
int PortSquad_NameTag(const char *name, char *out) {
    int n;

    out[0] = '\0';
    if (name == NULL || name[0] != '[') {
        return 0;
    }
    for (n = 0; n < TAG - 1 && ((name[1 + n] >= 'A' && name[1 + n] <= 'Z') || (name[1 + n] >= '0' && name[1 + n] <= '9')); n++) {
    }
    if (n == 0 || name[1 + n] != ']' || name[2 + n] != ' ') {
        return 0;
    }
    memcpy(out, name + 1, (size_t)n);
    out[n] = '\0';
    return 1;
}

/* The colour of the squad a name's tag names, as 0xRRGGBB; -1 when the name has no tag or no group has that squad.
   This player's own group is looked in first (two groups may use one tag). */
int PortSquad_NameColour(const char *name) {
    List *l = list();
    char tag[TAG];
    int mine, s, pass, gi, k;

    if (!PortSquad_NameTag(name, tag)) {
        return -1;
    }
    PortSquad_Mine(&mine, &s);
    for (pass = 0; pass < 2; pass++) {
        for (gi = 0; gi < l->count; gi++) {
            if ((pass == 0) != (gi + 1 == mine)) {
                continue;
            }
            for (k = 0; k < l->g[gi].count; k++) {
                if (strcmp(l->g[gi].sq[k].tag, tag) == 0) {
                    return l->g[gi].sq[k].rgb;
                }
            }
        }
    }
    return -1;
}

/* This player's group's id for a result sent to the service ("" none). */
const char *PortSquad_MyGroupId(void) {
    List *l = list();
    int g, s;
    PortSquad_Mine(&g, &s);
    return g >= 1 ? l->g[g - 1].id : "";
}
