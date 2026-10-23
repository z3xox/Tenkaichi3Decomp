#ifndef PORT_DISCORD_IPC_H
#define PORT_DISCORD_IPC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the connection to the local Discord client via IPC.
 * Connects via named pipes on Windows or Unix domain sockets on POSIX.
 *
 * @param application_id Discord Application Client ID string.
 */
void Discord_Init(const char *application_id);

/**
 * Sends a SET_ACTIVITY command payload to update current Rich Presence status.
 * Uses the session start time as the elapsed timer (since Discord_Init).
 *
 * @param state       Short description of current activity (shown below username).
 * @param details     Detailed context (shown above state).
 * @param large_image_key  Asset key from the Discord Developer Portal.
 * @param large_image_text Hover tooltip text for the large image.
 */
void Discord_Update(const char *state, const char *details,
                    const char *large_image_key, const char *large_image_text);

/**
 * Like Discord_Update but accepts a caller-provided Unix timestamp for the
 * elapsed timer.  Pass 0 to show no elapsed time.
 *
 * @param start_timestamp  Unix time_t of when the current activity started,
 *                         or 0 to omit the elapsed-time field entirely.
 */
void Discord_UpdateEx(const char *state, const char *details,
                      const char *large_image_key, const char *large_image_text,
                      long long start_timestamp);

/**
 * Like Discord_UpdateEx but also accepts small_image asset key and tooltip text.
 * Pass NULL or "" for small_image_key to omit small image.
 */
void Discord_UpdateEx2(const char *state, const char *details,
                       const char *large_image_key, const char *large_image_text,
                       const char *small_image_key, const char *small_image_text,
                       long long start_timestamp);

/**
 * Flushes incoming IPC responses and keeps socket buffers clear.
 * Should be called once per game tick / VBlank.
 */
void Discord_RunCallbacks(void);

/**
 * Returns true if currently connected to Discord IPC socket/pipe.
 */
bool Discord_IsConnected(void);

/**
 * Closes the IPC connection gracefully and frees socket/pipe descriptors.
 */
void Discord_Shutdown(void);


#ifdef __cplusplus
}
#endif

#endif /* PORT_DISCORD_IPC_H */