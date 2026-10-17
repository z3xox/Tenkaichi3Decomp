/* Table context keeps repeated English wording independent between UI screens.
   cc -std=gnu99 -Wall -Wextra -fsanitize=address,undefined -Iport/src/gs \
      port/tests/menu_font/ui_translation.c port/src/gs/ui_text_translate.c -o ui_translation_test
   ./ui_translation_test */
#include "ui_text_translate.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

/* Match the game's low-address contract without linking the full memory mapper. */
void *Port_LowAlloc(size_t size) {
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
void Port_LowFree(void *p) {
    if (p) munmap(p, 4096);
}

static void table(uint8_t *buf, const char *second) {
    uint32_t *words = (uint32_t *)buf;
    uint16_t *first = (uint16_t *)(buf + 16), *last = (uint16_t *)(buf + 32);
    size_t i;
    memset(buf, 0, 64);
    words[0] = 2; words[1] = 16; words[2] = 32; words[3] = 64;
    first[0] = 0xfeff;
    for (i = 0; i < 4; i++) first[i + 1] = (uint16_t)"Play"[i];
    last[0] = 0xfeff;
    for (i = 0; second[i]; i++) last[i + 1] = (uint16_t)second[i];
}
int main(void) {
    static const char catalog[] =
        "{\"schema_version\":1,\"locale\":\"es\",\"name\":\"Español\",\"labels\":{"
        "\"one/0\":{\"original\":\"Play\",\"text\":\"Jugar\"},"
        "\"one/1\":{\"original\":\"Start\",\"text\":\"Inicio\"},"
        "\"two/0\":{\"original\":\"Play\",\"text\":\"Reproducir\"},"
        "\"two/1\":{\"original\":\"Quit\",\"text\":\"Salir\"}}}";
    char dir[] = "/tmp/bt3-ui-translation-XXXXXX", path[256];
    uint8_t one[64], two[64];
    const uint16_t *a, *b, *plain;
    FILE *fp;
    int result = 1;
    if (!mkdtemp(dir)) return 1;
    snprintf(path, sizeof(path), "%s/translations", dir);
    if (mkdir(path, 0700)) goto done;
    snprintf(path, sizeof(path), "%s/translations/ui", dir);
    if (mkdir(path, 0700)) goto done;
    snprintf(path, sizeof(path), "%s/translations/ui/es.json", dir);
    fp = fopen(path, "wb");
    if (!fp) goto done;
    if (fwrite(catalog, 1, sizeof(catalog) - 1, fp) != sizeof(catalog) - 1) { fclose(fp); goto done; }
    if (fclose(fp) || !UiText_Load(dir, "es")) goto done;
    table(one, "Start"); table(two, "Quit");
    plain = (const uint16_t *)(one + 16);
    a = UiText_TranslateLine((const uint32_t *)one, 0, plain);
    b = UiText_TranslateLine((const uint32_t *)two, 0, (const uint16_t *)(two + 16));
    if (a != plain && (uintptr_t)a <= UINT32_MAX && (uintptr_t)b <= UINT32_MAX &&
        a[1] == 'J' && b[1] == 'R' &&
        UiText_TranslateString(plain) == plain && UiText_TranslateString(a) == a) result = 0;
done:
    snprintf(path, sizeof(path), "%s/translations/ui/es.json", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/translations/ui", dir); rmdir(path);
    snprintf(path, sizeof(path), "%s/translations", dir); rmdir(path);
    rmdir(dir);
    if (result) fputs("UI translation context test failed\n", stderr);
    return result;
}
