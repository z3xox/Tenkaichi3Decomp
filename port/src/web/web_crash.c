/*
 * Browser build: the crash report's context. Natively port/src/plat_crash.c installs the crash handlers (signals,
 * Windows exceptions); that file is not built for the browser (wasm_build.py, WASM_SKIP). Here a crash is a
 * WebAssembly trap (or an abort), caught by the page, which prints the JavaScript stack (with function names when the
 * build keeps them). This hands the page what the native report adds to the stack: how far the start got, and whether
 * the data is the original disc's (port/web/index.html calls it).
 *
 * How far the start got: plat_mem.c's gPortStage names the steps before the game's main. Only the Windows build's
 * start (its __wrap_main) then says "the game is running"; the browser's main is not wrapped, so gPortStage stays at
 * "memory ready, before the program's main" for good. Once the game has counted a vertical blank (plat_stub.c), it is
 * running, and that is what is said, with the count.
 */
#include <emscripten.h>
#include <stdio.h>
extern const char *Port_DataSummary(void); /* plat_verify.c (web_verify.c in the browser) */
extern const char *volatile gPortStage;   /* plat_mem.c */
extern unsigned gPortVBlanks;             /* plat_stub.c: vertical blanks since the start */

EMSCRIPTEN_KEEPALIVE const char *Port_CrashContext(void) {
    static char buf[512], running[96];
    const char *stage = gPortStage;
    if (gPortVBlanks > 0) {
        snprintf(running, sizeof(running), "the game is running (vertical blank %u)", gPortVBlanks);
        stage = running;
    }
    snprintf(buf, sizeof(buf), "stage: %s%s%s", stage, Port_DataSummary()[0] != '\0' ? "; " : "", Port_DataSummary());
    return buf;
}
