#include "discord_ipc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <cstdarg>
#include <stdint.h>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <process.h>
  static HANDLE sPipeHandle = INVALID_HANDLE_VALUE;
#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
  #include <unistd.h>
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <fcntl.h>
  #ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0
  #endif
  static int sSockFd = -1;
#endif

/* BT3_DISCORD_DEBUG=1 (any value): what happens on the connection goes to bt3_discord.log in the current directory */
static void DiscordLog(const char *fmt, ...) {
    static int sOn = -1;
    if (sOn < 0) sOn = getenv("BT3_DISCORD_DEBUG") != NULL;
    if (!sOn) return;
    FILE *fp = fopen("bt3_discord.log", "a");
    if (!fp) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

static bool sConnected = false;
static char sAppId[64] = {0};
static time_t sStartTime = 0;

// Discord IPC standard opcodes
enum DiscordOpcode {
    OP_HANDSHAKE = 0,
    OP_FRAME     = 1,
    OP_CLOSE     = 2,
    OP_PING      = 3,
    OP_PONG      = 4
};

// Binary header prefix prepended to all Discord IPC frames
#pragma pack(push, 1)
struct DiscordHeader {
    int32_t opcode;
    int32_t length;
};
#pragma pack(pop)

static bool WritePacket(int32_t opcode, const char *payload) {
    if (!sConnected) return false;

    /* Header and payload go out as ONE write: a Windows named pipe in message mode takes each write as a message,
     * and Discord would read the header alone as a whole frame. */
    static char frame[sizeof(DiscordHeader) + 2048];
    size_t len = strlen(payload);
    if (len > sizeof(frame) - sizeof(DiscordHeader)) return false;

    DiscordHeader header;
    header.opcode = opcode;
    header.length = (int32_t)len;
    memcpy(frame, &header, sizeof(header));
    memcpy(frame + sizeof(header), payload, len);
    size_t total = sizeof(header) + len;

#if defined(_WIN32)
    DWORD written = 0;
    if (!WriteFile(sPipeHandle, frame, (DWORD)total, &written, NULL) || written != total) {
        DiscordLog("write failed (opcode %d, %u of %u bytes, error %lu)", (int)opcode, (unsigned)written, (unsigned)total, (unsigned long)GetLastError());
        sConnected = false;
        CloseHandle(sPipeHandle);
        sPipeHandle = INVALID_HANDLE_VALUE;
        return false;
    }
    DiscordLog("wrote opcode %d, %u bytes", (int)opcode, (unsigned)total);
    return true;
#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    if (send(sSockFd, frame, total, MSG_NOSIGNAL) != (ssize_t)total) {
        sConnected = false;
        close(sSockFd);
        sSockFd = -1;
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool Discord_IsConnected(void) {
    return sConnected;
}

void Discord_Init(const char *application_id) {
    if (sConnected) return;

    // Skip initialization during headless builds or automated demo runs
    if (getenv("BT3_GS") && strcmp(getenv("BT3_GS"), "none") == 0) return;
    if (getenv("BT3_DEMO") != NULL) return;

    if (application_id && application_id[0] != '\0') {
        strncpy(sAppId, application_id, sizeof(sAppId) - 1);
        sAppId[sizeof(sAppId) - 1] = '\0';
    }
    if (sAppId[0] == '\0') return;
    sStartTime = time(NULL);

#if defined(_WIN32)
    char pipeName[64];
    for (int i = 0; i < 10; i++) {
        snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\discord-ipc-%d", i);
        sPipeHandle = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (sPipeHandle != INVALID_HANDLE_VALUE) {
            sConnected = true;
            DiscordLog("opened %s", pipeName);
            break;
        }
        DWORD err = GetLastError();
        DiscordLog("cannot open %s (error %lu)", pipeName, (unsigned long)err);
        if (err == ERROR_ACCESS_DENIED) { /* the pipe is there but not ours to open */
            DiscordLog("access denied: Discord probably runs as administrator and the game does not (or the reverse)");
        }
    }
#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    const char *runtimeDir = getenv("XDG_RUNTIME_DIR");
    const char *tmpDir = getenv("TMPDIR");
    if (!tmpDir) tmpDir = "/tmp";

    char flatpakDir[256] = {0};
    char snapDir[256] = {0};
    if (runtimeDir) {
        snprintf(flatpakDir, sizeof(flatpakDir), "%s/app/com.discordapp.Discord", runtimeDir);
        snprintf(snapDir, sizeof(snapDir), "%s/snap.discord", runtimeDir);
    }

    const char *candidateDirs[4];
    int numCandidates = 0;
    if (runtimeDir) candidateDirs[numCandidates++] = runtimeDir;
    if (flatpakDir[0]) candidateDirs[numCandidates++] = flatpakDir;
    if (snapDir[0]) candidateDirs[numCandidates++] = snapDir;
    candidateDirs[numCandidates++] = tmpDir;

    for (int d = 0; d < numCandidates && !sConnected; d++) {
        for (int i = 0; i < 10; i++) {
            char sockPath[512];
            snprintf(sockPath, sizeof(sockPath), "%s/discord-ipc-%d", candidateDirs[d], i);

            sSockFd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sSockFd < 0) continue;

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, sockPath, sizeof(addr.sun_path) - 1);

            if (connect(sSockFd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
#if defined(SO_NOSIGPIPE)
                int opt = 1;
                setsockopt(sSockFd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
                sConnected = true;
                break;
            }
            close(sSockFd);
            sSockFd = -1;
        }
    }
#endif

    if (!sConnected) {
        DiscordLog("no connection (Discord not running, or another user/permission level)");
        return;
    }

    // Send initial handshake JSON
    char handshakePayload[256];
    snprintf(handshakePayload, sizeof(handshakePayload),
             "{\"v\":1,\"client_id\":\"%s\"}", sAppId);

    WritePacket(OP_HANDSHAKE, handshakePayload);
}

void Discord_Update(const char *state, const char *details, const char *large_image_key, const char *large_image_text) {
    Discord_UpdateEx(state, details, large_image_key, large_image_text, (long long)sStartTime);
}

void Discord_UpdateEx(const char *state, const char *details, const char *large_image_key, const char *large_image_text, long long start_timestamp) {
    Discord_UpdateEx2(state, details, large_image_key, large_image_text, NULL, NULL, start_timestamp);
}

void Discord_UpdateEx2(const char *state, const char *details,
                       const char *large_image_key, const char *large_image_text,
                       const char *small_image_key, const char *small_image_text,
                       long long start_timestamp) {
    if (!sConnected) return;

    static int sNonce = 1;
    char json[1024];

#if defined(_WIN32)
    int currentPid = (int)GetCurrentProcessId();
#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    int currentPid = (int)getpid();
#else
    int currentPid = 0;
#endif

    char assetsJson[512];
    if (small_image_key && small_image_key[0] != '\0') {
        snprintf(assetsJson, sizeof(assetsJson),
            "\"assets\":{"
                "\"large_image\":\"%s\","
                "\"large_text\":\"%s\","
                "\"small_image\":\"%s\","
                "\"small_text\":\"%s\""
            "}",
            large_image_key ? large_image_key : "",
            large_image_text ? large_image_text : "",
            small_image_key,
            small_image_text ? small_image_text : ""
        );
    } else {
        snprintf(assetsJson, sizeof(assetsJson),
            "\"assets\":{"
                "\"large_image\":\"%s\","
                "\"large_text\":\"%s\""
            "}",
            large_image_key ? large_image_key : "",
            large_image_text ? large_image_text : ""
        );
    }

    if (start_timestamp > 0) {
        snprintf(json, sizeof(json),
            "{"
                "\"cmd\":\"SET_ACTIVITY\","
                "\"args\":{"
                    "\"pid\":%d,"
                    "\"activity\":{"
                        "\"state\":\"%s\","
                        "\"details\":\"%s\","
                        "\"timestamps\":{\"start\":%lld},"
                        "%s"
                    "}"
                "},"
                "\"nonce\":\"%d\""
            "}",
            currentPid,
            state ? state : "",
            details ? details : "",
            start_timestamp,
            assetsJson,
            sNonce++
        );
    } else {
        snprintf(json, sizeof(json),
            "{"
                "\"cmd\":\"SET_ACTIVITY\","
                "\"args\":{"
                    "\"pid\":%d,"
                    "\"activity\":{"
                        "\"state\":\"%s\","
                        "\"details\":\"%s\","
                        "%s"
                    "}"
                "},"
                "\"nonce\":\"%d\""
            "}",
            currentPid,
            state ? state : "",
            details ? details : "",
            assetsJson,
            sNonce++
        );
    }

    WritePacket(OP_FRAME, json);
}

void Discord_RunCallbacks(void) {
    if (!sConnected) return;

    char buffer[512];
#if defined(_WIN32)
    DWORD bytesAvailable = 0;
    if (PeekNamedPipe(sPipeHandle, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
        DWORD readBytes = 0;
        ReadFile(sPipeHandle, buffer, sizeof(buffer) - 1, &readBytes, NULL);
        if (readBytes > 8) {
            buffer[readBytes] = '\0';
            DiscordLog("discord says: %s", buffer + 8); /* (after the 8-byte header) */
        }
    }
#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    ssize_t n = recv(sSockFd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (n == 0) {
        sConnected = false;
        close(sSockFd);
        sSockFd = -1;
    }
#endif
}

void Discord_Shutdown(void) {
    if (!sConnected) return;

    WritePacket(OP_CLOSE, "{}");

#if defined(_WIN32)
    if (sPipeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(sPipeHandle);
        sPipeHandle = INVALID_HANDLE_VALUE;
    }
#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    if (sSockFd >= 0) {
        close(sSockFd);
        sSockFd = -1;
    }
#endif
    sConnected = false;
}