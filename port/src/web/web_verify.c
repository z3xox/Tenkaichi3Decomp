/*
 * Browser build: the check that the game data is the original disc's (plat_verify.c) is not run, and that is said.
 *
 * Natively the check runs on a thread of its own: a quick pass over every data file's size and time (stat), then,
 * unless a result kept in <root>/.verified still matches, a CRC-32 of all 68,605 files (about 3 GB). In the browser
 *   - there is no thread to run it on (pthread_create fails: the build has no threads), and before this file the
 *     failure left the check off without a word;
 *   - the data folder is on the HTTP server (web_fs.c), not in the program's own file system: stat finds none of
 *     its files, so the quick pass, run anyway, would call every file missing and the original data "modified";
 *   - the full pass would fetch the whole 3 GB at every start (nothing to keep a result against: no file times).
 * So Port_VerifyStart is linked wrapped (wasm_build.py) and does not start it. The log says the data is not
 * checked and why; the crash report and the page (web_ui.c: data_check) say the same. BT3_VERIFY=0 still means
 * "turned off", as natively.
 */
#include <stdio.h>
#include <stdlib.h>

const char *__real_Port_DataSummary(void);

/* The page's view of it (web_ui.c "data_check"): plat_verify.c's "off" (3), and states of its own. */
enum { CHECK_NOT_STARTED = -1, CHECK_OFF = 3, CHECK_NOT_IN_BROWSER = 4 };
static int sCheck = CHECK_NOT_STARTED;

static const char kNotRun[] = "game data: not checked against the original disc (the browser version cannot run the "
                              "check: it reads every file of the data folder, about 3 GB, and there are no threads to "
                              "run it on); the native build checks it";

void __wrap_Port_VerifyStart(void) {
    const char *v = getenv("BT3_VERIFY");
    static int once;

    if (once) { /* (the caller runs again when the game restarts itself) */
        return;
    }
    once = 1;
    if (v != NULL && atoi(v) == 0) {
        sCheck = CHECK_OFF;
        fprintf(stderr, "bt3: game data: check turned off (BT3_VERIFY=0)\n");
        return;
    }
    sCheck = CHECK_NOT_IN_BROWSER;
    fprintf(stderr, "bt3: %s\n", kNotRun);
}

/* What a crash report adds about the data: natively "" for the original disc's; here that is not known. */
const char *__wrap_Port_DataSummary(void) {
    const char *s = __real_Port_DataSummary();
    return s[0] == '\0' && sCheck == CHECK_NOT_IN_BROWSER ? "game data: not checked (browser version)" : s;
}

int WebVerify_State(void) {
    return sCheck;
}
