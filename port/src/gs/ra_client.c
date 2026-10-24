/* Online RA integration. rc_client is exclusively owned by the game thread.
 * A worker performs HTTPS, but only Tick dispatches responses to rcheevos.
 * Credentials and response bodies are never logged or written to disk. */
#include "ra_client.h"
#include "../port_release.h"
#include <SDL3/SDL.h>
#include <rc_client.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(_WIN32) && !defined(RA_CLIENT_TEST)
#include <windows.h>
#include <winhttp.h>
#endif

#define RA_BODY_LIMIT (8u * 1024u * 1024u)
#define RA_QUEUE_LIMIT 64
typedef struct RaRequest {
    struct RaRequest *next;
    char *url, *post, *content_type, *body;
    size_t length, capacity;
    int status;
    rc_client_server_callback_t callback;
    void *callback_data;
} RaRequest;
static struct {
    SDL_Mutex *lock;
    SDL_Condition *wake;
    SDL_Thread *worker;
    int stop, queued, command;
    char username[128], password[256];
    char user_agent[192];
    RaRequest *pending, *pending_tail, *done, *done_tail;
    rc_client_t *client;
    rc_client_async_handle_t *login, *load;
    RaClientStatus st;
    RaClientReadMemory read;
    void *read_userdata;
    int eligible;
    char attempted_hash[33];
} sRA;

static void erase(void *data, size_t size) {
    volatile unsigned char *p = (volatile unsigned char *)data;
    while (size--) *p++ = 0;
}
static void free_request(RaRequest *r) {
    if (r->post) { erase(r->post, strlen(r->post)); free(r->post); }
    if (r->body) { erase(r->body, r->capacity); free(r->body); }
    free(r->url); free(r->content_type); free(r);
}
static char *copy_string(const char *text) {
    size_t len;
    char *copy;
    if (!text) return NULL;
    len = strlen(text) + 1;
    copy = (char *)malloc(len);
    if (copy) memcpy(copy, text, len);
    return copy;
}
static int append_body(RaRequest *r, const void *data, size_t bytes) {
    size_t needed, capacity;
    char *body;
    if (bytes > RA_BODY_LIMIT - r->length) return 0;
    needed = r->length + bytes + 1;
    if (needed > r->capacity) {
        capacity = r->capacity ? r->capacity * 2 : 8192;
        if (capacity < needed) capacity = needed;
        body = (char *)realloc(r->body, capacity);
        if (!body) return 0;
        r->body = body; r->capacity = capacity;
    }
    memcpy(r->body + r->length, data, bytes);
    r->length += bytes; r->body[r->length] = 0;
    return 1;
}

#if defined(RA_CLIENT_TEST)
static RaClientTestTransport test_transport;
static void *test_transport_userdata;
static uint64_t test_time;
void RaClient_TestTransport(RaClientTestTransport transport, void *userdata) {
    test_transport = transport; test_transport_userdata = userdata;
}
void RaClient_TestTime(uint64_t milliseconds) { test_time = milliseconds; }
static void http_request(RaRequest *r) {
    const char *body;
    r->status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
    body = test_transport ? test_transport(r->url, r->post, &r->status, test_transport_userdata) : NULL;
    if (body && !append_body(r, body, strlen(body))) r->status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
}
#elif defined(_WIN32)
static void http_request(RaRequest *r) {
    wchar_t url[2048], host[256], path[2048], headers[256], agent[192];
    URL_COMPONENTS uc;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    DWORD status = 0, size = sizeof(status), got;
    char buffer[8192], header[256];
    int ok = 0;
    r->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, r->url, -1, url, 2048)) return;
    memset(&uc, 0, sizeof(uc)); uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(url, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return;
    host[uc.dwHostNameLength] = 0; path[uc.dwUrlPathLength] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, sRA.user_agent, -1, agent, 192)) return;
    session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) {
        WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
        connection = WinHttpConnect(session, host, uc.nPort, 0);
    }
    if (connection) request = WinHttpOpenRequest(connection, r->post ? L"POST" : L"GET", path,
                      NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request) {
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        snprintf(header, sizeof(header), "Content-Type: %s\r\n", r->content_type ? r->content_type : "application/x-www-form-urlencoded");
        MultiByteToWideChar(CP_UTF8, 0, header, -1, headers, 256);
        if (WinHttpSendRequest(request, headers, (DWORD)-1,
                               r->post ? r->post : WINHTTP_NO_REQUEST_DATA,
                               r->post ? (DWORD)strlen(r->post) : 0,
                               r->post ? (DWORD)strlen(r->post) : 0, 0) && WinHttpReceiveResponse(request, NULL) &&
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
            ok = 1;
            for (;;) {
                if (!WinHttpReadData(request, buffer, sizeof(buffer), &got)) { ok = 0; break; }
                if (!got) break;
                if (!append_body(r, buffer, got)) { ok = 0; break; }
            }
        }
    }
    if (ok) r->status = (int)status;
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
}
#else
static size_t curl_write(char *ptr, size_t size, size_t count, void *userdata) {
    size_t bytes;
    if (count && size > RA_BODY_LIMIT / count) return 0;
    bytes = size * count;
    return append_body((RaRequest *)userdata, ptr, bytes) ? bytes : 0;
}
static void http_request(RaRequest *r) {
    typedef void *(*init_fn)(void);
    typedef int (*setopt_fn)(void *, int, ...);
    typedef int (*perform_fn)(void *);
    typedef int (*getinfo_fn)(void *, int, ...);
    typedef void (*cleanup_fn)(void *);
    typedef void *(*append_fn)(void *, const char *);
    typedef void (*free_fn)(void *);
    SDL_SharedObject *lib = NULL;
    init_fn init; setopt_fn option; perform_fn perform; getinfo_fn info;
    cleanup_fn cleanup; append_fn append; free_fn free_headers;
    static const char *const names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so", "libcurl.4.dylib"};
    size_t i; void *handle, *headers; long status = 0; char header[256];
    r->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    for (i = 0; !lib && i < sizeof(names) / sizeof(names[0]); ++i) lib = SDL_LoadObject(names[i]);
    if (!lib) return;
    init = (init_fn)SDL_LoadFunction(lib, "curl_easy_init");
    option = (setopt_fn)SDL_LoadFunction(lib, "curl_easy_setopt");
    perform = (perform_fn)SDL_LoadFunction(lib, "curl_easy_perform");
    info = (getinfo_fn)SDL_LoadFunction(lib, "curl_easy_getinfo");
    cleanup = (cleanup_fn)SDL_LoadFunction(lib, "curl_easy_cleanup");
    append = (append_fn)SDL_LoadFunction(lib, "curl_slist_append");
    free_headers = (free_fn)SDL_LoadFunction(lib, "curl_slist_free_all");
    if (!init || !option || !perform || !info || !cleanup || !append || !free_headers) goto finish;
    handle = init(); if (!handle) goto finish;
    snprintf(header, sizeof(header), "Content-Type: %s", r->content_type ? r->content_type : "application/x-www-form-urlencoded");
    headers = append(NULL, header);
    if (!headers) { cleanup(handle); goto finish; }
    option(handle, 10002 /* URL */, r->url);
    option(handle, 10018 /* USERAGENT */, sRA.user_agent);
    option(handle, 10023 /* HTTPHEADER */, headers);
    option(handle, 20011 /* WRITEFUNCTION */, curl_write);
    option(handle, 10001 /* WRITEDATA */, r);
    option(handle, 181 /* PROTOCOLS */, 2L /* HTTPS only */);
    option(handle, 52 /* FOLLOWLOCATION */, 0L);
    option(handle, 64 /* SSL_VERIFYPEER */, 1L);
    option(handle, 81 /* SSL_VERIFYHOST */, 2L);
    option(handle, 99 /* NOSIGNAL */, 1L);
    option(handle, 78 /* CONNECTTIMEOUT */, 5L);
    option(handle, 13 /* TIMEOUT */, 15L);
    if (r->post) option(handle, 10015 /* POSTFIELDS */, r->post);
    if (!perform(handle) && !info(handle, 0x200002 /* RESPONSE_CODE */, &status)) r->status = (int)status;
    free_headers(headers); cleanup(handle);
finish:
    SDL_UnloadObject(lib);
}
#endif

static int worker(void *unused) {
    (void)unused;
    for (;;) {
        RaRequest *r;
        SDL_LockMutex(sRA.lock);
        while (!sRA.stop && !sRA.pending) SDL_WaitCondition(sRA.wake, sRA.lock);
        if (sRA.stop) { SDL_UnlockMutex(sRA.lock); break; }
        r = sRA.pending; sRA.pending = r->next;
        if (!sRA.pending) sRA.pending_tail = NULL;
        r->next = NULL;
        SDL_UnlockMutex(sRA.lock);
        http_request(r);
        SDL_LockMutex(sRA.lock);
        if (sRA.done_tail) sRA.done_tail->next = r; else sRA.done = r;
        sRA.done_tail = r;
        SDL_UnlockMutex(sRA.lock);
    }
    return 0;
}
static void RC_CCONV server_call(const rc_api_request_t *request,
    rc_client_server_callback_t callback, void *data, rc_client_t *client) {
    RaRequest *r = (RaRequest *)calloc(1, sizeof(*r));
    rc_api_server_response_t failure = {"", 0, RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR};
    (void)client;
    if (!r) { callback(&failure, data); return; }
    r->url = copy_string(request->url); r->post = copy_string(request->post_data);
    r->content_type = copy_string(request->content_type);
    r->callback = callback; r->callback_data = data;
    if (!r->url || (request->post_data && !r->post) ||
        (request->content_type && !r->content_type) || strncmp(r->url, "https://", 8)) {
        free_request(r); callback(&failure, data); return;
    }
    SDL_LockMutex(sRA.lock);
    if (sRA.stop || sRA.queued >= RA_QUEUE_LIMIT) {
        SDL_UnlockMutex(sRA.lock); free_request(r); callback(&failure, data); return;
    }
    ++sRA.queued;
    if (sRA.pending_tail) sRA.pending_tail->next = r; else sRA.pending = r;
    sRA.pending_tail = r;
    SDL_SignalCondition(sRA.wake);
    SDL_UnlockMutex(sRA.lock);
}
static uint32_t RC_CCONV read_memory(uint32_t address, uint8_t *buffer, uint32_t bytes, rc_client_t *client) {
    (void)client;
    return sRA.eligible && sRA.read ? sRA.read(address, buffer, bytes, sRA.read_userdata) : 0;
}
static rc_clock_t RC_CCONV time_ms(const rc_client_t *client) {
    (void)client;
#ifdef RA_CLIENT_TEST
    return test_time;
#else
    return SDL_GetTicks();
#endif
}
static void RC_CCONV login_done(int result, const char *error, rc_client_t *client, void *data) {
    const rc_client_user_t *user = rc_client_get_user_info(client);
    (void)error; (void)data;
    sRA.login = NULL;
    SDL_LockMutex(sRA.lock);
    sRA.st.logging_in = 0; sRA.st.logged_in = result == RC_OK && user != NULL;
    if (sRA.st.logged_in) SDL_strlcpy(sRA.st.username, user->display_name, sizeof(sRA.st.username));
    else SDL_strlcpy(sRA.st.error, "RetroAchievements login failed. Check your account details and connection.", sizeof(sRA.st.error));
    SDL_UnlockMutex(sRA.lock);
}
static void RC_CCONV load_done(int result, const char *error, rc_client_t *client, void *data) {
    const rc_client_game_t *game = rc_client_get_game_info(client);
    (void)error; (void)data;
#ifdef RA_CLIENT_TEST
    if (result != RC_OK) fprintf(stderr, "fixture load failed (%d): %s\n", result, error ? error : "");
#endif
    sRA.load = NULL;
    SDL_LockMutex(sRA.lock);
    sRA.st.loading = 0;
    sRA.st.game_loaded = result == RC_OK && game && game->id;
    if (sRA.st.game_loaded) {
        sRA.st.game_id = game->id;
        SDL_strlcpy(sRA.st.game_title, game->title, sizeof(sRA.st.game_title));
    } else SDL_strlcpy(sRA.st.error, "Could not load achievements for this executable. Verify game support and connection.", sizeof(sRA.st.error));
    SDL_UnlockMutex(sRA.lock);
}
static void RC_CCONV event_handler(const rc_client_event_t *event, rc_client_t *client) {
    (void)client;
    SDL_LockMutex(sRA.lock);
    if (event->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED && event->achievement) {
        sRA.st.notification_time_ms = SDL_GetTicks();
        SDL_strlcpy(sRA.st.notification_title, event->achievement->title, sizeof(sRA.st.notification_title));
        SDL_strlcpy(sRA.st.notification_description, event->achievement->description, sizeof(sRA.st.notification_description));
        sRA.st.notification_points = event->achievement->points;
    } else if (event->type == RC_CLIENT_EVENT_DISCONNECTED) {
        SDL_strlcpy(sRA.st.error, "Offline: achievement submissions are pending.", sizeof(sRA.st.error));
    } else if (event->type == RC_CLIENT_EVENT_RECONNECTED) {
        sRA.st.error[0] = 0;
    } else if (event->type == RC_CLIENT_EVENT_SERVER_ERROR) {
        SDL_strlcpy(sRA.st.error, "RetroAchievements server rejected a request.", sizeof(sRA.st.error));
    }
    SDL_UnlockMutex(sRA.lock);
}

int RaClient_Init(void) {
    char clause[128];
    if (sRA.lock) return sRA.client != NULL;
    sRA.lock = SDL_CreateMutex(); sRA.wake = SDL_CreateCondition();
    if (!sRA.lock || !sRA.wake) { RaClient_Shutdown(); return 0; }
    sRA.client = rc_client_create(read_memory, server_call);
    if (!sRA.client) { RaClient_Shutdown(); return 0; }
    rc_client_set_host(sRA.client, "https://retroachievements.org");
    rc_client_set_hardcore_enabled(sRA.client, 0); /* also disables leaderboards */
    rc_client_set_unofficial_enabled(sRA.client, 0);
    rc_client_set_allow_background_memory_reads(sRA.client, 0);
    rc_client_set_get_time_millisecs_function(sRA.client, time_ms);
    rc_client_set_event_handler(sRA.client, event_handler);
    rc_client_get_user_agent_clause(sRA.client, clause, sizeof(clause));
    snprintf(sRA.user_agent, sizeof(sRA.user_agent), "Tenkaichi3Decomp/0.%d.%d %s",
             PORT_RELEASE / 100, PORT_RELEASE % 100, clause);
    sRA.worker = SDL_CreateThread(worker, "bt3-retroachievements", NULL);
    if (!sRA.worker) { RaClient_Shutdown(); return 0; }
    sRA.st.available = 1;
    return 1;
}
void RaClient_Shutdown(void) {
    RaRequest *r, *next;
    if (sRA.lock) {
        SDL_LockMutex(sRA.lock); sRA.stop = 1;
        if (sRA.wake) SDL_SignalCondition(sRA.wake);
        SDL_UnlockMutex(sRA.lock);
    }
    if (sRA.worker) SDL_WaitThread(sRA.worker, NULL);
    /* Complete callbacks before destroying rc_client so their userdata is freed.
     * Cancellation ensures no login/game callback can start another operation. */
    if (sRA.client) {
        if (sRA.login) rc_client_abort_async(sRA.client, sRA.login);
        if (sRA.load) rc_client_abort_async(sRA.client, sRA.load);
        rc_client_logout(sRA.client);
    }
    for (r = sRA.pending; r; r = next) {
        rc_api_server_response_t failure = {"", 0, RC_API_SERVER_RESPONSE_CLIENT_ERROR};
        next = r->next; r->callback(&failure, r->callback_data); free_request(r);
    }
    for (r = sRA.done; r; r = next) {
        rc_api_server_response_t failure = {"", 0, RC_API_SERVER_RESPONSE_CLIENT_ERROR};
        next = r->next; r->callback(&failure, r->callback_data); free_request(r);
    }
    if (sRA.client) rc_client_destroy(sRA.client);
    if (sRA.wake) SDL_DestroyCondition(sRA.wake);
    if (sRA.lock) SDL_DestroyMutex(sRA.lock);
    erase(&sRA, sizeof(sRA));
}
int RaClient_RequestLogin(const char *username, const char *password) {
    int ok = 0;
    if (!sRA.lock || !username || !password || !*username || !*password ||
        strlen(username) >= sizeof(sRA.username) || strlen(password) >= sizeof(sRA.password)) return 0;
    SDL_LockMutex(sRA.lock);
    if (sRA.st.available && !sRA.command && !sRA.st.logging_in && !sRA.st.logged_in) {
        SDL_strlcpy(sRA.username, username, sizeof(sRA.username));
        SDL_strlcpy(sRA.password, password, sizeof(sRA.password));
        sRA.command = 1; sRA.st.enabled = 1; sRA.st.logging_in = 1; sRA.st.error[0] = 0; ok = 1;
    }
    SDL_UnlockMutex(sRA.lock);
    return ok;
}
void RaClient_RequestLogout(void) {
    if (!sRA.lock) return;
    SDL_LockMutex(sRA.lock); sRA.command = 2;
    erase(sRA.password, sizeof(sRA.password));
    SDL_UnlockMutex(sRA.lock);
}
int RaClient_RequestRetry(void) {
    int ok = 0;
    if (!sRA.lock) return 0;
    SDL_LockMutex(sRA.lock);
    if (!sRA.command && sRA.st.logged_in && sRA.st.eligible &&
        !sRA.st.loading && !sRA.st.game_loaded) {
        sRA.command = 3; ok = 1;
    }
    SDL_UnlockMutex(sRA.lock);
    return ok;
}
void RaClient_GetStatus(RaClientStatus *status) {
    if (!status) return;
    if (!sRA.lock) { memset(status, 0, sizeof(*status)); return; }
    SDL_LockMutex(sRA.lock); *status = sRA.st; SDL_UnlockMutex(sRA.lock);
}
static int valid_hash(const char *hash) {
    size_t i;
    if (!hash || strlen(hash) != 32) return 0;
    for (i = 0; i < 32; ++i) if (!((hash[i] >= '0' && hash[i] <= '9') ||
        (hash[i] >= 'a' && hash[i] <= 'f') || (hash[i] >= 'A' && hash[i] <= 'F'))) return 0;
    return 1;
}
void RaClient_Tick(RaClientReadMemory read, void *userdata, const char *hash,
                   int eligible, const char *reason) {
    RaRequest *r;
    int command;
    char username[128], password[256];
    rc_client_user_game_summary_t summary;
    if (!sRA.client) return;
    sRA.read = read; sRA.read_userdata = userdata;
    sRA.eligible = eligible && read && valid_hash(hash);
    SDL_LockMutex(sRA.lock);
    command = sRA.command; sRA.command = 0;
    memcpy(username, sRA.username, sizeof(username)); memcpy(password, sRA.password, sizeof(password));
    erase(sRA.password, sizeof(sRA.password));
    sRA.st.eligible = sRA.eligible;
    SDL_strlcpy(sRA.st.blocked_reason, sRA.eligible ? "" : reason ? reason : "Verified game memory mapping unavailable.", sizeof(sRA.st.blocked_reason));
    SDL_UnlockMutex(sRA.lock);
    if (command == 2) {
        if (sRA.login) rc_client_abort_async(sRA.client, sRA.login);
        if (sRA.load) rc_client_abort_async(sRA.client, sRA.load);
        sRA.login = sRA.load = NULL;
        rc_client_logout(sRA.client); sRA.attempted_hash[0] = 0;
        SDL_LockMutex(sRA.lock);
        memset(&sRA.st, 0, sizeof(sRA.st)); sRA.st.available = 1;
        SDL_UnlockMutex(sRA.lock);
    } else if (command == 1) {
        int waiting;
        /* An aborted login still owns a response callback inside rc_client.
         * Drain the previous account's requests before starting another one. */
        SDL_LockMutex(sRA.lock);
        waiting = sRA.queued != 0;
        if (waiting && sRA.command != 2) {
            sRA.command = 1;
            SDL_strlcpy(sRA.password, password, sizeof(sRA.password));
        }
        SDL_UnlockMutex(sRA.lock);
        if (!waiting) {
            sRA.attempted_hash[0] = 0;
            sRA.login = rc_client_begin_login_with_password(sRA.client, username, password, login_done, NULL);
        }
    }
    if (command == 3) {
        sRA.attempted_hash[0] = 0;
        SDL_LockMutex(sRA.lock); sRA.st.error[0] = 0; SDL_UnlockMutex(sRA.lock);
    }
    erase(password, sizeof(password));
    /* Stop evaluation before delivering a load response from a now unsafe frame. */
    if (!sRA.eligible || (sRA.attempted_hash[0] && strcmp(sRA.attempted_hash, hash))) {
        if (sRA.load) rc_client_abort_async(sRA.client, sRA.load);
        sRA.load = NULL;
        rc_client_unload_game(sRA.client); sRA.attempted_hash[0] = 0;
        SDL_LockMutex(sRA.lock);
        sRA.st.loading = sRA.st.game_loaded = 0; sRA.st.game_id = 0;
        sRA.st.game_title[0] = 0; sRA.st.unlocked = sRA.st.total = sRA.st.unsupported = 0;
        SDL_UnlockMutex(sRA.lock);
    }
    for (;;) {
        rc_api_server_response_t response;
        SDL_LockMutex(sRA.lock);
        r = sRA.done;
        if (r) { sRA.done = r->next; if (!sRA.done) sRA.done_tail = NULL; --sRA.queued; }
        SDL_UnlockMutex(sRA.lock);
        if (!r) break;
        response.body = r->body ? r->body : ""; response.body_length = r->length;
        response.http_status_code = r->status;
        r->callback(&response, r->callback_data); free_request(r);
    }
    if (sRA.eligible && !sRA.attempted_hash[0] && rc_client_get_user_info(sRA.client)) {
        SDL_strlcpy(sRA.attempted_hash, hash, sizeof(sRA.attempted_hash));
        SDL_LockMutex(sRA.lock); sRA.st.loading = 1; sRA.st.error[0] = 0; SDL_UnlockMutex(sRA.lock);
        sRA.load = rc_client_begin_load_game(sRA.client, hash, load_done, NULL);
    }
    if (sRA.eligible && rc_client_is_game_loaded(sRA.client)) rc_client_do_frame(sRA.client);
    else rc_client_idle(sRA.client);
    if (rc_client_is_game_loaded(sRA.client)) {
        rc_client_get_user_game_summary(sRA.client, &summary);
        SDL_LockMutex(sRA.lock);
        sRA.st.total = summary.num_core_achievements;
        sRA.st.unlocked = summary.num_unlocked_achievements;
        sRA.st.unsupported = summary.num_unsupported_achievements;
        SDL_UnlockMutex(sRA.lock);
    }
}
