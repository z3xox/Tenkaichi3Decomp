/*
 * Browser build: the game's data read on demand over HTTP.
 *
 * The native port reads its data folder (gamedata/, about 4 GB unpacked) with fopen / fread from several places:
 * plat_fcache.c (the game's own file reads), snd_adx.c / snd_se.c (sound files), plat_mpeg2.c (movies), plat_mem.c
 * (the two programs at start). All of them are kept as they are: the program is linked with --wrap=fopen, and here
 *   - a file that exists in Emscripten's own file system (saves, settings, the two programs the page fetches before
 *     start-up) is opened there, as always;
 *   - a file opened for reading under the data root (BT3_DATA or "gamedata") that is not there is opened as a stream
 *     whose reads ask the page (web/bt3_data.js: HTTP Range requests to the server the page names, a block cache).
 *     Those reads WAIT for the network without blocking the browser: the game runs under JSPI (wasm_build.py:
 *     -sJSPI), so a read suspends the game's thread of execution until the data is there.
 * Every such read happens on the game's own thread (the sound players load a whole file when they start and the
 * audio callbacks only decode from memory). A read that cannot wait (outside the game's thread, e.g. before main)
 * fails with a message instead of returning wrong data.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <emscripten.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

extern const char *Port_FileRoot(void); /* plat_file.c */
FILE *__real_fopen(const char *path, const char *mode);

/* The page's loader (port/web/bt3_data.js): size of a data file (-1: there is none, -2: the server failed). */
EM_ASYNC_JS(double, web_data_size, (const char *rel), {
    return await Module.bt3Data.size(UTF8ToString(rel));
});
/* Reads `n` bytes at `off` into `dst`; returns how many (fewer at the end of the file), or -1 on an error. */
EM_ASYNC_JS(int, web_data_read, (const char *rel, double off, char *dst, int n), {
    return await Module.bt3Data.read(UTF8ToString(rel), off, dst, n);
});

typedef struct { char rel[256]; long long size, pos; } Remote;

static ssize_t remote_read(void *c, char *buf, size_t n) {
    Remote *r = c;
    long long left = r->size - r->pos;
    int got;

    if (left <= 0 || n == 0) {
        return 0;
    }
    if ((long long)n > left) {
        n = (size_t)left;
    }
    got = web_data_read(r->rel, (double)r->pos, buf, (int)n);
    if (got < 0) {
        fprintf(stderr, "bt3: reading %s at %lld failed (the data server: see the page's log)\n", r->rel, r->pos);
        errno = EIO;
        return -1;
    }
    r->pos += got;
    return got;
}

static int remote_seek(void *c, off_t *off, int whence) {
    Remote *r = c;
    long long to = whence == SEEK_SET ? *off : whence == SEEK_CUR ? r->pos + *off : r->size + *off;

    if (to < 0) {
        errno = EINVAL;
        return -1;
    }
    r->pos = to;
    *off = (off_t)to;
    return 0;
}

static int remote_close(void *c) {
    free(c);
    return 0;
}

/* The path relative to the data root, or NULL when the path is not under it. */
static const char *under_root(const char *path) {
    const char *root = Port_FileRoot();
    size_t n = strlen(root);

    while (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    if (strncmp(path, root, n) == 0 && path[n] == '/') {
        return path + n + 1;
    }
    return NULL;
}

FILE *__wrap_fopen(const char *path, const char *mode) {
    static const cookie_io_functions_t io = {remote_read, NULL, remote_seek, remote_close};
    FILE *fp = __real_fopen(path, mode);
    const char *rel;
    double size;
    Remote *r;

    if (fp != NULL || strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL ||
        (rel = under_root(path)) == NULL) {
        return fp;
    }
    size = web_data_size(rel);
    if (size < 0) {
        errno = size == -1 ? ENOENT : EIO;
        return NULL;
    }
    r = calloc(1, sizeof(*r));
    if (r == NULL || strlen(rel) >= sizeof(r->rel)) {
        free(r);
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(r->rel, rel);
    r->size = (long long)size;
    fp = fopencookie(r, "rb", io);
    if (fp == NULL) {
        free(r);
    }
    return fp;
}

/* ---- looking through the data folder: stat, opendir / readdir / closedir --------------------------------------
   Natively plat_extras.c (added stages and songs) and gs_texpack.c through SDL (texture packs: SDL_GetPathInfo,
   SDL_EnumerateDirectory) find their files by looking through folders. A path under the data root that Emscripten's
   own file system does not have is answered from the data server's folder listings (bt3_data.js: canList, list,
   statOf; port/web/devserver.py: <folder>/?list); everything else is Emscripten's, as always. A server that does not
   list folders is said once by the page, and then nothing is found there (web_extras.c reports added content as
   not available); a server error is logged and reported to the caller as an I/O error, not as "nothing there". */
int __real_stat(const char *path, struct stat *st);
DIR *__real_opendir(const char *path);
struct dirent *__real_readdir(DIR *d);
int __real_closedir(DIR *d);

EM_ASYNC_JS(int, web_data_can_list, (void), {
    return await Module.bt3Data.canList() ? 1 : 0;
});
/* Whether the data server lists its folders (asked once, on the game's own run: it waits for the server; also for
   web_extras.c, and the page through web_ui.c) */
extern void WebUi_NoteListable(int can);
int WebFs_CanList(void) {
    static int can = -1;
    if (can < 0) {
        can = web_data_can_list();
        WebUi_NoteListable(can);
    }
    return can;
}
/* What is at `rel`: a file's size; -1 nothing; -3 a folder; -2 the server failed. */
EM_ASYNC_JS(double, web_data_stat, (const char *rel), {
    return await Module.bt3Data.statOf(UTF8ToString(rel));
});
/* A folder's entries as lines "d<name>" / "f<name>" (malloc'd), NULL if there is no such folder; `*failed` is set
   to 1 when the server failed. */
EM_ASYNC_JS(char *, web_data_list, (const char *rel, int *failed), {
    let entries;
    try {
        entries = await Module.bt3Data.list(UTF8ToString(rel));
    } catch (e) {
        Module.bt3Data.log(`data: ${e.message}`);
        Module.HEAPU8[failed] = 1;
        return 0;
    }
    if (entries === null) return 0;
    const bytes = new TextEncoder().encode(entries.map((e) => (e.dir ? 'd' : 'f') + e.name + '\n').join('') + '\0');
    const p = Module._malloc(bytes.length);
    if (p) Module.HEAPU8.set(bytes, p);
    return p;
});

/* The path relative to the data root, "" for the root itself, or NULL when it is not under it. */
static const char *at_or_under_root(const char *path) {
    const char *root = Port_FileRoot();
    while (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    if (strcmp(path, root) == 0 || (strncmp(path, root, strlen(root)) == 0 && strcmp(path + strlen(root), "/") == 0)) {
        return "";
    }
    return under_root(path);
}

/* (a trailing "/", as SDL_EnumerateDirectory adds, is not part of the name the server lists) */
static void without_slash(const char *rel, char *out, size_t size) {
    size_t n = strlen(rel);
    snprintf(out, size, "%.*s", (int)(n > 0 && rel[n - 1] == '/' ? n - 1 : n), rel);
}

int __wrap_stat(const char *path, struct stat *st) {
    const char *rel;
    char name[256];
    double v;

    int err;

    if (__real_stat(path, st) == 0) {
        return 0;
    }
    err = errno;
    if ((rel = at_or_under_root(path)) == NULL || !WebFs_CanList()) {
        errno = err;
        return -1;
    }
    without_slash(rel, name, sizeof(name));
    v = web_data_stat(name);
    if (v == -1 || v == -2) {
        errno = v == -1 ? ENOENT : EIO;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = v == -3 ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    st->st_size = v == -3 ? 0 : (off_t)v;
    st->st_nlink = 1;
    return 0;
}

/* A listed folder, handed out as a DIR * (the real ones are told apart by the list of these) */
typedef struct RemoteDir {
    struct RemoteDir *next;
    char *lines, *at;
    struct dirent ent;
} RemoteDir;
static RemoteDir *sDirs;

static RemoteDir *remote_dir(DIR *d) {
    RemoteDir *r;
    for (r = sDirs; r != NULL && (DIR *)r != d; r = r->next) {
    }
    return r;
}

DIR *__wrap_opendir(const char *path) {
    DIR *d = __real_opendir(path);
    const char *rel;
    char name[256];
    int failed = 0;
    RemoteDir *r;

    if (d != NULL || (rel = at_or_under_root(path)) == NULL || !WebFs_CanList()) {
        return d;
    }
    without_slash(rel, name, sizeof(name));
    r = calloc(1, sizeof(*r));
    if (r == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    r->lines = web_data_list(name, &failed);
    if (r->lines == NULL) {
        free(r);
        errno = failed ? EIO : ENOENT;
        if (failed) {
            fprintf(stderr, "bt3: looking through %s failed (the data server: see the page's log)\n", path);
        }
        return NULL;
    }
    r->at = r->lines;
    r->next = sDirs;
    sDirs = r;
    return (DIR *)r;
}

struct dirent *__wrap_readdir(DIR *d) {
    RemoteDir *r = remote_dir(d);
    char *end;

    if (r == NULL) {
        return __real_readdir(d);
    }
    if (*r->at == '\0') {
        return NULL;
    }
    end = strchr(r->at, '\n');
    r->ent.d_type = r->at[0] == 'd' ? DT_DIR : DT_REG;
    snprintf(r->ent.d_name, sizeof(r->ent.d_name), "%.*s", (int)(end - r->at - 1), r->at + 1);
    r->at = end + 1;
    return &r->ent;
}

int __wrap_closedir(DIR *d) {
    RemoteDir *r = remote_dir(d), **p;

    if (r == NULL) {
        return __real_closedir(d);
    }
    for (p = &sDirs; *p != r; p = &(*p)->next) {
    }
    *p = r->next;
    free(r->lines);
    free(r);
    return 0;
}
