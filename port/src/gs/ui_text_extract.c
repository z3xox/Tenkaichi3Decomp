/* Extract the game's UTF-16 text tables from menu archives at startup. */
#include "ui_text_extract.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif

#define UT_MAX_FILE (16u * 1024 * 1024)
#define UT_MAX_UNPACKED (64u * 1024 * 1024)
#define UT_MAX_LINES 16384u

typedef struct UtBuf { const uint8_t *p; size_t n; } UtBuf;
typedef struct UtJob { char data[1200], output[1200]; } UtJob;
typedef struct UtWriter { FILE *fp; unsigned count; int file; } UtWriter;
static pthread_mutex_t sUtLock = PTHREAD_MUTEX_INITIALIZER;
static int sUtStarted;
static const int kUtMenuFiles[] = {5, 448, 449, 450, 451, 452, 453, 459, 460, 461, 463, 472};

static uint32_t ut_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t ut_u16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static int ut_offset(UtBuf b, uint32_t i, uint32_t *out) {
    if ((uint64_t)i * 4 + 4 > b.n) return 0;
    *out = ut_u32(b.p + (size_t)i * 4) & ~3u;
    return *out <= b.n;
}
static int ut_archive(UtBuf b, uint32_t *count) {
    uint32_t n, prev, off, i;
    if (b.n < 12 || (n = ut_u32(b.p)) == 0 || n > UT_MAX_LINES ||
        (uint64_t)(n + 2) * 4 > b.n || !ut_offset(b, 1, &prev) || prev < (n + 2) * 4) return 0;
    for (i = 2; i <= n + 1; i++) {
        if (!ut_offset(b, i, &off) || off < prev) return 0;
        prev = off;
    }
    *count = n;
    return 1;
}
static int ut_section(UtBuf b, uint32_t i, UtBuf *out) {
    uint32_t first, last;
    if (!ut_offset(b, i, &first) || !ut_offset(b, i + 1, &last) || last < first) return 0;
    out->p = b.p + first;
    out->n = last - first;
    return 1;
}
/* Same bounded byte-pair stream format as menu_labels.c's ml_unpack_bytes. */
static int ut_unpack(UtBuf b, UtBuf *out) {
    size_t p = 8, end, n = 0;
    uint32_t size, packed;
    uint8_t *dst;
    if (b.n < 8 || (size = ut_u32(b.p)) == 0 || size > UT_MAX_UNPACKED ||
        (packed = ut_u32(b.p + 4)) == 0 || packed > b.n - 8) return 0;
    end = 8 + packed;
    dst = malloc(size);
    if (dst == NULL) return 0;
    while (p < end) {
        uint8_t left[256], right[256], stack[256];
        size_t block, k;
        int ch = 0, i, sp;
        for (i = 0; i < 256; i++) { left[i] = (uint8_t)i; right[i] = 0; }
        while (ch < 256) {
            int count;
            if (p >= end) goto fail;
            count = b.p[p++];
            if (count > 127) { ch += count - 127; count = 0; }
            if (ch == 256) break;
            for (i = 0; i <= count; i++) {
                if (ch >= 256 || p >= end) goto fail;
                left[ch] = b.p[p++];
                if (ch != left[ch]) { if (p >= end) goto fail; right[ch] = b.p[p++]; }
                ch++;
            }
        }
        if (end - p < 2) goto fail;
        block = (size_t)b.p[p] * 256 + b.p[p + 1]; p += 2;
        if (end - p < block) goto fail;
        for (k = 0; k < block; k++) {
            sp = 0; stack[sp++] = b.p[p + k];
            while (sp) {
                uint8_t v = stack[--sp];
                if (left[v] == v) { if (n >= size) goto fail; dst[n++] = v; }
                else { if (sp + 2 > 256) goto fail; stack[sp++] = right[v]; stack[sp++] = left[v]; }
            }
        }
        p += block;
    }
    if (p != end || n != size) goto fail;
    out->p = dst; out->n = n;
    return 1;
fail:
    free(dst);
    return 0;
}
static int ut_line(UtBuf b) {
    size_t p;
    if (b.n < 4 || b.p[0] != 0xff || b.p[1] != 0xfe) return 0;
    for (p = 2; p + 1 < b.n; p += 2) {
        uint16_t c = ut_u16(b.p + p);
        if (c == 0) return 1;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (p + 3 >= b.n || ut_u16(b.p + p + 2) < 0xdc00 || ut_u16(b.p + p + 2) > 0xdfff) return 0;
            p += 2;
        } else if (c >= 0xdc00 && c <= 0xdfff) return 0;
    }
    return 0;
}
static void ut_json_string(FILE *fp, UtBuf b) {
    size_t p;
    fputc('"', fp);
    for (p = 2; p + 1 < b.n; p += 2) {
        uint32_t c = ut_u16(b.p + p);
        if (!c) break;
        if (c >= 0xd800 && c <= 0xdbff) { c = 0x10000 + ((c - 0xd800) << 10) + ut_u16(b.p + p + 2) - 0xdc00; p += 2; }
        if (c == '"' || c == '\\') { fputc('\\', fp); fputc((int)c, fp); }
        else if (c < 0x20) fprintf(fp, "\\u%04x", c);
        else if (c < 0x80) fputc((int)c, fp);
        else if (c < 0x800) { fputc(0xc0 | (c >> 6), fp); fputc(0x80 | (c & 63), fp); }
        else if (c < 0x10000) { fputc(0xe0 | (c >> 12), fp); fputc(0x80 | ((c >> 6) & 63), fp); fputc(0x80 | (c & 63), fp); }
        else { fputc(0xf0 | (c >> 18), fp); fputc(0x80 | ((c >> 12) & 63), fp); fputc(0x80 | ((c >> 6) & 63), fp); fputc(0x80 | (c & 63), fp); }
    }
    fputc('"', fp);
}
static void ut_walk(UtWriter *w, UtBuf b, const char *path, int depth) {
    uint32_t n, i;
    UtBuf line;
    if (depth > 4 || !ut_archive(b, &n)) return;
    for (i = 1; i <= n; i++) if (!ut_section(b, i, &line) || !ut_line(line)) break;
    if (i > n) {
        for (i = 1; i <= n; i++) {
            char id[128];
            ut_section(b, i, &line);
            if (snprintf(id, sizeof(id), "%s/%u", path, i - 1) >= (int)sizeof(id)) return;
            fprintf(w->fp, "%s    \"%s\": {\"original\": ", w->count++ ? ",\n" : "", id);
            ut_json_string(w->fp, line);
            fputs(", \"text\": ", w->fp);
            ut_json_string(w->fp, line);
            fputc('}', w->fp);
        }
        return;
    }
    for (i = 1; i <= n; i++) {
        UtBuf part, unpacked;
        uint32_t childCount;
        char child[128];
        if (!ut_section(b, i, &part) || part.n < 12 ||
            snprintf(child, sizeof(child), "%s/%u", path, i) >= (int)sizeof(child)) continue;
        if (ut_archive(part, &childCount)) { ut_walk(w, part, child, depth + 1); }
        else if (ut_unpack(part, &unpacked)) { ut_walk(w, unpacked, child, depth + 1); free((void *)unpacked.p); }
    }
}
static uint8_t *ut_read(const char *path, size_t *size) {
    FILE *fp = fopen(path, "rb");
    long n;
    uint8_t *data;
    if (fp == NULL) return NULL;
    if (fseek(fp, 0, SEEK_END) || (n = ftell(fp)) < 12 || n > UT_MAX_FILE || fseek(fp, 0, SEEK_SET)) { fclose(fp); return NULL; }
    data = malloc((size_t)n);
    if (data == NULL) { fclose(fp); return NULL; }
    if (fread(data, 1, (size_t)n, fp) != (size_t)n) { free(data); fclose(fp); return NULL; }
    fclose(fp); *size = (size_t)n; return data;
}
static void *ut_worker(void *arg) {
    UtJob *job = arg;
    char temp[1280], path[1280];
    UtWriter w = {0};
    size_t size;
    int i;
    if (access(job->output, 0) == 0 || snprintf(temp, sizeof(temp), "%s.part", job->output) >= (int)sizeof(temp)) goto done;
    w.fp = fopen(temp, "wb");
    if (w.fp == NULL) goto done;
    fputs("{\n  \"schema_version\": 1,\n  \"labels_version\": 1,\n  \"translation_version\": 1,\n  \"locale\": \"en\",\n  \"name\": \"English\",\n  \"source\": \"extracted USA game text\",\n  \"labels\": {\n", w.fp);
    for (i = 0; i < (int)(sizeof(kUtMenuFiles) / sizeof(kUtMenuFiles[0])); i++) {
        uint8_t *data;
        UtBuf b;
        if (snprintf(path, sizeof(path), "%s/pzs3us1/%05d.bin", job->data, kUtMenuFiles[i]) >= (int)sizeof(path)) continue;
        data = ut_read(path, &size);
        if (data == NULL) continue;
        b.p = data; b.n = size; w.file = kUtMenuFiles[i];
        { char id[32]; snprintf(id, sizeof(id), "pzs3us1/%05d", w.file); ut_walk(&w, b, id, 0); }
        free(data);
    }
    fprintf(w.fp, "\n  },\n  \"labels_count\": %u\n}\n", w.count);
    if (fclose(w.fp) == 0 && w.count > 0) {
#ifdef _WIN32
        if (access(job->output, 0) != 0 && rename(temp, job->output) == 0) goto done;
#else
        /* link is atomic and cannot overwrite a user's catalog created meanwhile. */
        if (link(temp, job->output) == 0) { remove(temp); goto done; }
#endif
    }
    remove(temp);
done:
    free(job);
    return NULL;
}
void UiText_ExtractOnStartup(const char *gameTextDir) {
    const char *data = getenv("BT3_DATA");
    UtJob *job;
    pthread_t thread;
    char parent[1200], dir[1200];
    if (gameTextDir == NULL) return;
    pthread_mutex_lock(&sUtLock);
    if (sUtStarted) { pthread_mutex_unlock(&sUtLock); return; }
    sUtStarted = 1;
    pthread_mutex_unlock(&sUtLock);
    job = calloc(1, sizeof(*job));
    if (job == NULL) return;
    if (snprintf(job->data, sizeof(job->data), "%s", data && data[0] ? data : "gamedata") >= (int)sizeof(job->data) ||
        snprintf(parent, sizeof(parent), "%s/translations", gameTextDir) >= (int)sizeof(parent) ||
        snprintf(dir, sizeof(dir), "%s/ui", parent) >= (int)sizeof(dir) ||
        snprintf(job->output, sizeof(job->output), "%s/en.json", dir) >= (int)sizeof(job->output)) { free(job); return; }
#ifdef _WIN32
    _mkdir(gameTextDir); _mkdir(parent); _mkdir(dir);
#else
    mkdir(gameTextDir, 0755); mkdir(parent, 0755); mkdir(dir, 0755);
#endif
    if (access(job->output, 0) == 0 || pthread_create(&thread, NULL, ut_worker, job) != 0) { free(job); return; }
    pthread_detach(thread);
}
