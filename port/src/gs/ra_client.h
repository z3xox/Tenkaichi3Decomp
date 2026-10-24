#ifndef BT3_RA_CLIENT_H
#define BT3_RA_CLIENT_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Init before starting render/UI threads. Init, Tick and Shutdown belong to the
 * game thread. All other functions copy data under a mutex and are UI-safe.
 * Nothing (including account tokens) is persisted. Disabled until login. */
typedef uint32_t (*RaClientReadMemory)(uint32_t address, uint8_t *buffer,
                                      uint32_t bytes, void *userdata);
/* Verified temporary object is absent. No destination bytes are written. */
#define RA_MEMORY_UNAVAILABLE UINT32_MAX
typedef struct RaClientStatus {
    int available, enabled, logging_in, logged_in, loading, game_loaded, eligible;
    uint32_t game_id, unlocked, total, unsupported;
    char username[128], game_title[160], error[256], blocked_reason[192];
    uint64_t notification_time_ms;
    char notification_title[160], notification_description[256];
    uint32_t notification_points;
} RaClientStatus;

int RaClient_Init(void);
void RaClient_Shutdown(void);
/* Returns 0 if unavailable, an operation is already queued, or fields too long.
 * Caller should clear its own password buffer after this call. */
int RaClient_RequestLogin(const char *username, const char *password);
void RaClient_RequestLogout(void);
/* Retry a failed game load without resending the player's password. */
int RaClient_RequestRetry(void);
void RaClient_GetStatus(RaClientStatus *status);
/* Call once per emulated frame, including idle frames. hash must be the verified
 * 32-digit RA hash of the player's original executable. eligible must remain 0
 * until the memory mapping/content/cheat checks have passed. The callback must
 * return 0 for unknown addresses and RA_MEMORY_UNAVAILABLE for verified fields
 * of an absent temporary object; never fabricate a zero-filled RAM image.
 * An ineligible frame unloads the game; no achievement evaluation takes place. */
void RaClient_Tick(RaClientReadMemory read_memory, void *userdata,
                   const char *hash, int eligible, const char *blocked_reason);

#ifdef RA_CLIENT_TEST
/* Test builds have no native HTTP implementation. Set before Init, not while
 * worker is running. Response strings are copied before the worker returns. */
typedef const char *(*RaClientTestTransport)(const char *url, const char *post,
                                           int *status, void *userdata);
void RaClient_TestTransport(RaClientTestTransport transport, void *userdata);
void RaClient_TestTime(uint64_t milliseconds);
#endif

#ifdef __cplusplus
}
#endif
#endif
