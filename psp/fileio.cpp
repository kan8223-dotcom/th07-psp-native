#include "fileio.hpp"

#if defined(TH07_PSP_SHIKIGAMI) && defined(TH07_PSP_PERF_DIAG)
#include "perf_log_telemetry.h"
#endif

#include <pspiofilemgr.h>
#include <pspthreadman.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <malloc.h>

namespace
{
char gGameDir[512] = "ms0:/PSP/GAME/TH07PSP";
char gDataDir[512] = "ms0:/PSP/GAME/TH07PSP";
char gBootLog[560] = "ms0:/TH07PSP_BOOT.LOG";
char gLaunchDevice[8] = "ms0:";
bool gLaunchDirSet;
bool gInitialized;
bool gOriginalDataReady;

constexpr unsigned int kMaxDataCandidates = 16;
constexpr unsigned long long kTh07DatBytes = 23829135ull;
constexpr unsigned long long kThBgmDatBytes = 444516656ull;
char gDataCandidates[kMaxDataCandidates][sizeof(gDataDir)]{};
unsigned int gDataCandidateCount;

#if defined(TH07_PSP_PERF_DIAG)
SceUID gBootLogSema = -1;

bool LockBootLog()
{
    return gBootLogSema >= 0 && sceKernelWaitSema(gBootLogSema, 1, nullptr) >= 0;
}

void UnlockBootLog(bool locked)
{
    if (locked)
    {
        sceKernelSignalSema(gBootLogSema, 1);
    }
}

// PERF windows are generated while timing the game. Writing each one through
// the Memory Stick synchronously perturbs the very frame times being measured.
// Keep a fixed diagnostic-only buffer and commit it at a non-gameplay boundary.
// The PSP-1000 diagnostic keeps the buffer smaller because it has no high-memory
// partition; both sizes cover a complete six-stage run measured in this repo.
#if defined(TH07_PSP_1000)
constexpr std::size_t kPerfLogBufferBytes = 128u * 1024u;
#elif defined(TH07_PSP_PERF_ACCEPT)
// RID29 missed the end of a six-stage ACCEPT run by eleven lines.  Keep the
// PSP-1000 contract at 128 KiB, but give the high-memory target one bounded
// 32-KiB diagnostic margin so the user's single hardware run is conclusive.
constexpr std::size_t kPerfLogBufferBytes = 160u * 1024u;
#else
constexpr std::size_t kPerfLogBufferBytes = 512u * 1024u;
#endif
char gPerfLogBuffer[kPerfLogBufferBytes];
std::size_t gPerfLogUsed;
unsigned int gPerfLogDroppedLines;
bool gPerfLogInvalid;
bool gPerfGameplayActive;
#if !defined(TH07_PSP_1000)
bool gPerfStageLoadActive;
#endif
unsigned int gPerfLogRunId;
unsigned int gPerfLogWindowId;
#if defined(TH07_PSP_SHIKIGAMI)
uint32_t gPerfLogBufferGeneration;
#endif

#if defined(TH07_PSP_SHIKIGAMI)
uint32_t PerfLogCrc32(const char *data, std::size_t bytes)
{
    uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < bytes; ++i)
    {
        crc ^= static_cast<unsigned char>(data[i]);
        for (unsigned int bit = 0; bit < 8u; ++bit)
        {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}
#endif

const char *PerfProfileToken()
{
#if defined(TH07_PSP_PERF_ATTRIB) && defined(TH07_PSP_PERF_M2)
    return "M2";
#elif defined(TH07_PSP_PERF_ATTRIB) && defined(TH07_PSP_PERF_M3)
    return "M3";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_PERF_EMPTY_TIMERS) && \
    defined(TH07_PSP_PERF_M2)
    return "EMPTY_M2";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_PERF_EMPTY_TIMERS) && \
    defined(TH07_PSP_PERF_M3)
    return "EMPTY_M3";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_PERF_DENSE_SLICE) && \
    defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    return "ENP5D";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_PERF_DENSE_SLICE) && \
    defined(TH07_PSP_BULLET_WARM_QUEUE)
    return "WARMD";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_PERF_DENSE_SLICE) && \
    defined(TH07_PSP_BULLET_STATIC_PROXY)
    return "SPRXD";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_PERF_DENSE_SLICE) && \
    defined(TH07_PSP_ME_RENDER_WORKER)
    return "MERW0";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_PERF_DENSE_SLICE)
    return "DENSE";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_BULLET_WARM_QUEUE)
    return "WARMQ";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_BULLET_HOT_PREFETCH)
    return "PREFETCH";
#elif defined(TH07_PSP_PERF_ACCEPT) && defined(TH07_PSP_BULLET_STATIC_PROXY)
    return "SPRX";
#elif defined(TH07_PSP_PERF_ACCEPT)
    return "ACCEPT";
#else
    return "UNKNOWN";
#endif
}
#endif

int FormatUptimeStamp(char *out, std::size_t outSize)
{
    const unsigned int uptimeMs = sceKernelGetSystemTimeLow() / 1000u;
    return std::snprintf(out, outSize, "[%u.%03u] ", uptimeMs / 1000u,
                         uptimeMs % 1000u);
}

std::size_t WriteAvailable(SceUID fd, const char *data, std::size_t bytes)
{
    std::size_t written = 0;
    while (written < bytes)
    {
        const int result = sceIoWrite(fd, data + written,
                                      static_cast<unsigned int>(bytes - written));
        if (result <= 0)
        {
            break;
        }
        written += static_cast<std::size_t>(result);
    }
    return written;
}

bool JoinPath(char *out, std::size_t outSize, const char *root, const char *relative)
{
    const int length = std::snprintf(out, outSize, "%s/%s", root, relative);
    return length >= 0 && static_cast<std::size_t>(length) < outSize;
}

bool HasHeaderAndSize(const char *path, const char magic[4], unsigned long long bytes)
{
    SceIoStat stat{};
    if (sceIoGetstat(path, &stat) < 0 ||
        static_cast<unsigned long long>(stat.st_size) != bytes)
    {
        return false;
    }
    const SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
    {
        return false;
    }
    char header[4]{};
    const int read = sceIoRead(fd, header, sizeof(header));
    sceIoClose(fd);
    return read == static_cast<int>(sizeof(header)) && std::memcmp(header, magic, 4) == 0;
}

bool IsCompleteDataRoot(const char *root)
{
    char th07Path[640];
    char bgmPath[640];
    return JoinPath(th07Path, sizeof(th07Path), root, "th07.dat") &&
           JoinPath(bgmPath, sizeof(bgmPath), root, "thbgm.dat") &&
           HasHeaderAndSize(th07Path, "PBG4", kTh07DatBytes) &&
           HasHeaderAndSize(bgmPath, "ZWAV", kThBgmDatBytes);
}

bool AsciiPathEqual(const char *left, const char *right)
{
    while (*left && *right)
    {
        unsigned char a = static_cast<unsigned char>(*left++);
        unsigned char b = static_cast<unsigned char>(*right++);
        if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == *right;
}

void AddDataCandidate(const char *path)
{
    if (!path || !path[0] || std::strlen(path) >= sizeof(gDataCandidates[0]) ||
        gDataCandidateCount >= kMaxDataCandidates)
    {
        return;
    }
    for (unsigned int i = 0; i < gDataCandidateCount; ++i)
    {
        if (AsciiPathEqual(gDataCandidates[i], path)) return;
    }
    if (!IsCompleteDataRoot(path)) return;
    std::snprintf(gDataCandidates[gDataCandidateCount], sizeof(gDataCandidates[0]), "%s", path);
    ++gDataCandidateCount;
}

void ScanDeviceRoot(const char *device)
{
    if (!device || !device[0]) return;
    AddDataCandidate(device);
    constexpr const char *knownNames[] = {"th7", "TH07", "youyoumu", "PerfectCherryBlossom"};
    for (const char *name : knownNames)
    {
        char candidate[sizeof(gDataDir)];
        if (JoinPath(candidate, sizeof(candidate), device, name)) AddDataCandidate(candidate);
    }

    char root[16];
    std::snprintf(root, sizeof(root), "%s/", device);
    const SceUID directory = sceIoDopen(root);
    if (directory < 0) return;
    SceIoDirent entry{};
    while (sceIoDread(directory, &entry) > 0)
    {
        if (entry.d_name[0] && std::strcmp(entry.d_name, ".") != 0 &&
            std::strcmp(entry.d_name, "..") != 0 && FIO_S_ISDIR(entry.d_stat.st_mode))
        {
            char candidate[sizeof(gDataDir)];
            if (JoinPath(candidate, sizeof(candidate), device, entry.d_name))
            {
                AddDataCandidate(candidate);
            }
        }
        std::memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(directory);
}

void ScanSiblingGameInstalls(const char *device)
{
    if (!device || !device[0]) return;

    char gameRoot[64];
    const int rootLength = std::snprintf(gameRoot, sizeof(gameRoot), "%s/PSP/GAME", device);
    if (rootLength < 0 || static_cast<std::size_t>(rootLength) >= sizeof(gameRoot)) return;

    const SceUID directory = sceIoDopen(gameRoot);
    if (directory < 0) return;
    SceIoDirent entry{};
    while (sceIoDread(directory, &entry) > 0)
    {
        if (entry.d_name[0] && std::strcmp(entry.d_name, ".") != 0 &&
            std::strcmp(entry.d_name, "..") != 0 && FIO_S_ISDIR(entry.d_stat.st_mode))
        {
            char installDir[sizeof(gDataDir)];
            char nestedDataDir[sizeof(gDataDir)];
            if (JoinPath(installDir, sizeof(installDir), gameRoot, entry.d_name) &&
                JoinPath(nestedDataDir, sizeof(nestedDataDir), installDir, "th7"))
            {
                // This lets PSP-1000 and PSP-2000+ EBOOT folders coexist
                // without duplicating the user's roughly 469 MiB original
                // DAT files. Local data remains first priority.
                AddDataCandidate(nestedDataDir);
            }
        }
        std::memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(directory);
}

bool IsAbsolutePath(const char *path)
{
    if (!path || !path[0])
    {
        return false;
    }
    return path[0] == '/' || std::strchr(path, ':') != nullptr;
}

const char *SkipDotSlash(const char *path)
{
    while (path[0] == '.' && path[1] == '/')
    {
        path += 2;
    }
    return path;
}
} // namespace

extern "C" void th07_psp_fileio_set_launch_path(const char *argv0)
{
    if (gInitialized || !argv0)
    {
        return;
    }

    const char *colon = std::strchr(argv0, ':');
    const char *slash = std::strrchr(argv0, '/');
    if (!colon || !slash || colon > slash)
    {
        return;
    }

    const std::size_t dirLength = static_cast<std::size_t>(slash - argv0);
    if (dirLength == 0 || dirLength >= sizeof(gGameDir))
    {
        return;
    }
    std::memcpy(gGameDir, argv0, dirLength);
    gGameDir[dirLength] = '\0';

    const int deviceLength = static_cast<int>(colon - argv0 + 1);
    std::snprintf(gBootLog, sizeof(gBootLog), "%.*s/TH07PSP_BOOT.LOG", deviceLength, argv0);
    if (deviceLength > 0 && static_cast<std::size_t>(deviceLength) < sizeof(gLaunchDevice))
    {
        std::memcpy(gLaunchDevice, argv0, static_cast<std::size_t>(deviceLength));
        gLaunchDevice[deviceLength] = '\0';
    }
    gLaunchDirSet = true;
}

extern "C" void th07_psp_fileio_init()
{
    if (gInitialized)
    {
        return;
    }

    // Match TH06 PSP's storage contract: original data may live in an
    // untouched PC folder, while cfg/score/replays always stay beside EBOOT.
    char nestedDataDir[sizeof(gDataDir)];
    if (JoinPath(nestedDataDir, sizeof(nestedDataDir), gGameDir, "th7"))
    {
        AddDataCandidate(nestedDataDir);
    }
    AddDataCandidate(gGameDir); // legacy flat development layout
    ScanSiblingGameInstalls(gLaunchDevice);
    ScanDeviceRoot(gLaunchDevice);
    const char *otherDevice = std::strcmp(gLaunchDevice, "ef0:") == 0 ? "ms0:" : "ef0:";
    ScanSiblingGameInstalls(otherDevice);
    ScanDeviceRoot(otherDevice);
    if (gDataCandidateCount > 0)
    {
        std::snprintf(gDataDir, sizeof(gDataDir), "%s", gDataCandidates[0]);
        gOriginalDataReady = true;
    }
    else
    {
        std::snprintf(gDataDir, sizeof(gDataDir), "%s", gGameDir);
    }

    // sceIoChdir also keeps the decompilation's remaining stdio and
    // std::filesystem state paths EBOOT-local.
    sceIoChdir(gGameDir);
    char stateDir[640];
    if (JoinPath(stateDir, sizeof(stateDir), gGameDir, "replay")) sceIoMkdir(stateDir, 0777);
    if (JoinPath(stateDir, sizeof(stateDir), gGameDir, "snapshot")) sceIoMkdir(stateDir, 0777);
    sceIoRemove(gBootLog);
    const SceUID fd = sceIoOpen(gBootLog, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0)
    {
        sceIoClose(fd);
    }
#if defined(TH07_PSP_PERF_DIAG)
    // This was already proven in the 2026-08-24 delayed logger: one lock must
    // serialize a bulk PERF commit with rare audio-worker failure notes.
    gBootLogSema = sceKernelCreateSema("th07_boot_log", 0, 1, 1, nullptr);
    // The boot log is truncated once per process, so a low uptime nonce is
    // sufficient to distinguish a new hardware run inside that file. Window
    // IDs then remain monotonic across stage teardown/re-registration.
    gPerfLogRunId = sceKernelGetSystemTimeLow();
    if (gPerfLogRunId == 0u)
    {
        gPerfLogRunId = 1u;
    }
#endif
    gInitialized = true;
    th07_psp_boot_note(gLaunchDirSet ? gGameDir : "launch path fallback");
    th07_psp_boot_note(gOriginalDataReady ? gDataDir : "original TH07 1.00b data not found");
}

extern "C" const char *th07_psp_game_dir()
{
    th07_psp_fileio_init();
    return gGameDir;
}

extern "C" const char *th07_psp_data_dir()
{
    th07_psp_fileio_init();
    return gDataDir;
}

extern "C" int th07_psp_original_data_ready()
{
    th07_psp_fileio_init();
    return gOriginalDataReady ? 1 : 0;
}

extern "C" const char *th07_psp_resolve_path(const char *path, char *out, std::size_t outSize)
{
    th07_psp_fileio_init();
    if (!path || !out || outSize == 0)
    {
        return path;
    }
    if (IsAbsolutePath(path))
    {
        std::snprintf(out, outSize, "%s", path);
    }
    else
    {
        const char *relative = SkipDotSlash(path);
        const bool originalData = std::strcmp(relative, "th07.dat") == 0 ||
                                  std::strcmp(relative, "thbgm.dat") == 0;
        std::snprintf(out, outSize, "%s/%s", originalData ? gDataDir : gGameDir, relative);
    }
    return out;
}

extern "C" void th07_psp_boot_note(const char *message)
{
    if (!gInitialized)
    {
        th07_psp_fileio_init();
    }
    if (!message)
    {
        return;
    }
#if defined(TH07_PSP_PERF_DIAG)
#if !defined(TH07_PSP_1000)
    if (__atomic_load_n(&gPerfStageLoadActive, __ATOMIC_ACQUIRE))
    {
        // Stage registration emits dense ANM/texture diagnostics before the
        // gameplay logger normally becomes active. Keep those routine notes
        // in the same RAM buffer so repeated append/open/close cycles cannot
        // dominate the load they are intended to measure.
        th07_psp_perf_note(message);
        return;
    }
#endif
    if (__atomic_load_n(&gPerfGameplayActive, __ATOMIC_ACQUIRE))
    {
        // All normal/event diagnostics reached during gameplay share the RAM
        // logger. In particular, bomb/spell/BGM-open notes must never inject a
        // synchronous Memory Stick open/write/close into a timed frame.
        th07_psp_perf_note(message);
        return;
    }
    const bool bootLogLockAvailable = gBootLogSema >= 0;
    const bool bootLogLocked = LockBootLog();
    if (bootLogLockAvailable && !bootLogLocked)
    {
        return;
    }
#endif
    const SceUID fd = sceIoOpen(gBootLog, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
    {
#if defined(TH07_PSP_PERF_DIAG)
        UnlockBootLog(bootLogLocked);
#endif
        return;
    }
    // Millisecond uptime prefix: the log doubles as a coarse profile of the
    // menu/Music Room transitions without a separate diagnostic build.
    char stamp[16];
    const int stampLength = FormatUptimeStamp(stamp, sizeof(stamp));
    if (stampLength > 0)
    {
        sceIoWrite(fd, stamp, static_cast<unsigned int>(stampLength));
    }
    sceIoWrite(fd, message, std::strlen(message));
    sceIoWrite(fd, "\n", 1);
    sceIoClose(fd);
#if defined(TH07_PSP_PERF_DIAG)
    UnlockBootLog(bootLogLocked);
#endif
}

extern "C" void th07_psp_perf_note(const char *message)
{
    if (!message)
    {
        return;
    }
#if defined(TH07_PSP_PERF_DIAG)
    if (!gInitialized)
    {
        th07_psp_fileio_init();
    }
    const bool perfLogLockAvailable = gBootLogSema >= 0;
    const bool perfLogLocked = LockBootLog();
    if (perfLogLockAvailable && !perfLogLocked)
    {
        return;
    }
    char stamp[16];
    const int rawStampLength = FormatUptimeStamp(stamp, sizeof(stamp));
    const std::size_t stampLength = rawStampLength > 0 &&
                                            static_cast<std::size_t>(rawStampLength) < sizeof(stamp)
                                        ? static_cast<std::size_t>(rawStampLength)
                                        : 0u;
    char taggedMessage[640];
    const char *storedMessage = message;
    if (std::strncmp(message, "PERF ", 5u) == 0)
    {
        const int taggedLength = std::snprintf(
            taggedMessage, sizeof(taggedMessage), "PERF PF%s RID%08X W%u %s",
            PerfProfileToken(), gPerfLogRunId,
            __atomic_load_n(&gPerfLogWindowId, __ATOMIC_ACQUIRE), message + 5u);
        if (taggedLength < 0 || static_cast<std::size_t>(taggedLength) >= sizeof(taggedMessage))
        {
            ++gPerfLogDroppedLines;
            gPerfLogInvalid = true;
            UnlockBootLog(perfLogLocked);
            return;
        }
        storedMessage = taggedMessage;
    }
    const std::size_t messageLength = std::strlen(storedMessage);
    const std::size_t lineBytes = stampLength + messageLength + 1u;
    if (lineBytes > kPerfLogBufferBytes - gPerfLogUsed)
    {
        ++gPerfLogDroppedLines;
        gPerfLogInvalid = true;
        UnlockBootLog(perfLogLocked);
        return;
    }
    std::memcpy(gPerfLogBuffer + gPerfLogUsed, stamp, stampLength);
    gPerfLogUsed += stampLength;
    std::memcpy(gPerfLogBuffer + gPerfLogUsed, storedMessage, messageLength);
    gPerfLogUsed += messageLength;
    gPerfLogBuffer[gPerfLogUsed++] = '\n';
    UnlockBootLog(perfLogLocked);
#else
    // Release builds must never turn a stray PERF call into synchronous
    // Memory Stick I/O on the gameplay path.
    (void)message;
#endif
}

extern "C" void th07_psp_perf_log_flush()
{
#if defined(TH07_PSP_PERF_DIAG)
    if (!gInitialized)
    {
        return;
    }
    char endMarker[80];
    std::snprintf(endMarker, sizeof(endMarker), "PERF END VALID=%u DROP=%u",
                  gPerfLogInvalid ? 0u : 1u, gPerfLogDroppedLines);
    th07_psp_perf_note(endMarker);
    if (gPerfLogUsed == 0 && gPerfLogDroppedLines == 0)
    {
        return;
    }
    const bool bootLogLockAvailable = gBootLogSema >= 0;
    const bool bootLogLocked = LockBootLog();
    if (bootLogLockAvailable && !bootLogLocked)
    {
        return;
    }
    const SceUID fd = sceIoOpen(gBootLog, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
    {
        UnlockBootLog(bootLogLocked);
        return;
    }
    const std::size_t written = WriteAvailable(fd, gPerfLogBuffer, gPerfLogUsed);
    if (written > 0)
    {
        const std::size_t remaining = gPerfLogUsed - written;
        std::memmove(gPerfLogBuffer, gPerfLogBuffer + written, remaining);
        gPerfLogUsed = remaining;
#if defined(TH07_PSP_SHIKIGAMI)
        // A telemetry snapshot is an immutable prefix.  Generation validation
        // makes a concurrent/future fallback flush fail closed instead of
        // silently sending bytes from shifted offsets.
        ++gPerfLogBufferGeneration;
#endif
    }
    if (gPerfLogUsed == 0 && gPerfLogDroppedLines != 0)
    {
        char droppedMessage[96];
        char stamp[16];
        const int stampLength = FormatUptimeStamp(stamp, sizeof(stamp));
        const int messageLength = std::snprintf(
            droppedMessage, sizeof(droppedMessage),
            "PERF PROFILE INVALID OVERFLOW %u LINES\n", gPerfLogDroppedLines);
        const bool stampOk = stampLength > 0 &&
                             static_cast<std::size_t>(stampLength) < sizeof(stamp);
        const bool messageOk = messageLength > 0 &&
                               static_cast<std::size_t>(messageLength) < sizeof(droppedMessage);
        if (stampOk && messageOk &&
            WriteAvailable(fd, stamp, static_cast<std::size_t>(stampLength)) ==
                static_cast<std::size_t>(stampLength) &&
            WriteAvailable(fd, droppedMessage, static_cast<std::size_t>(messageLength)) ==
                static_cast<std::size_t>(messageLength))
        {
            gPerfLogDroppedLines = 0;
        }
    }
    sceIoClose(fd);
    UnlockBootLog(bootLogLocked);
#endif
}

#if defined(TH07_PSP_SHIKIGAMI) && defined(TH07_PSP_PERF_DIAG)
extern "C" void th07_psp_perf_log_seal(void)
{
    unsigned int valid;
    unsigned int dropped;
    const bool lockAvailable = gBootLogSema >= 0;
    const bool locked = LockBootLog();
    if (lockAvailable && !locked)
    {
        return;
    }
    valid = gPerfLogInvalid ? 0u : 1u;
    dropped = gPerfLogDroppedLines;
    UnlockBootLog(locked);

    char endMarker[80];
    std::snprintf(endMarker, sizeof(endMarker), "PERF END VALID=%u DROP=%u",
                  valid, dropped);
    // This is a RAM append using the same semaphore as normal PERF lines.  It
    // is called only after the renderer has ended the gameplay timing window.
    th07_psp_perf_note(endMarker);
}

extern "C" int th07_psp_perf_log_snapshot_begin(
    Th07PspPerfLogSnapshot *snapshot)
{
    if (!snapshot || !gInitialized ||
        __atomic_load_n(&gPerfGameplayActive, __ATOMIC_ACQUIRE)
#if !defined(TH07_PSP_1000)
        || __atomic_load_n(&gPerfStageLoadActive, __ATOMIC_ACQUIRE)
#endif
    )
    {
        return 0;
    }

    const bool lockAvailable = gBootLogSema >= 0;
    const bool locked = LockBootLog();
    if (lockAvailable && !locked)
    {
        return 0;
    }
    if (__atomic_load_n(&gPerfGameplayActive, __ATOMIC_ACQUIRE)
#if !defined(TH07_PSP_1000)
        || __atomic_load_n(&gPerfStageLoadActive, __ATOMIC_ACQUIRE)
#endif
    )
    {
        UnlockBootLog(locked);
        return 0;
    }

    snapshot->run_id = gPerfLogRunId;
    snapshot->window_id =
        __atomic_load_n(&gPerfLogWindowId, __ATOMIC_ACQUIRE);
    snapshot->total_bytes = static_cast<uint32_t>(gPerfLogUsed);
    snapshot->log_crc32 = PerfLogCrc32(gPerfLogBuffer, gPerfLogUsed);
    snapshot->dropped_lines = gPerfLogDroppedLines;
    snapshot->valid = gPerfLogInvalid ? 0u : 1u;
    snapshot->buffer_generation = gPerfLogBufferGeneration;
    UnlockBootLog(locked);
    return 1;
}

extern "C" uint32_t th07_psp_perf_log_snapshot_read(
    const Th07PspPerfLogSnapshot *snapshot, uint32_t offset,
    void *destination, uint32_t capacity)
{
    if (!snapshot || !destination || capacity == 0u ||
        offset >= snapshot->total_bytes ||
        __atomic_load_n(&gPerfGameplayActive, __ATOMIC_ACQUIRE)
#if !defined(TH07_PSP_1000)
        || __atomic_load_n(&gPerfStageLoadActive, __ATOMIC_ACQUIRE)
#endif
    )
    {
        return 0u;
    }

    const bool lockAvailable = gBootLogSema >= 0;
    const bool locked = LockBootLog();
    if (lockAvailable && !locked)
    {
        return 0u;
    }
    if (__atomic_load_n(&gPerfGameplayActive, __ATOMIC_ACQUIRE)
#if !defined(TH07_PSP_1000)
        || __atomic_load_n(&gPerfStageLoadActive, __ATOMIC_ACQUIRE)
#endif
        || snapshot->run_id != gPerfLogRunId
        || snapshot->buffer_generation != gPerfLogBufferGeneration
        || snapshot->total_bytes > gPerfLogUsed)
    {
        UnlockBootLog(locked);
        return 0u;
    }

    const uint32_t remaining = snapshot->total_bytes - offset;
    const uint32_t bytes = remaining < capacity ? remaining : capacity;
    std::memcpy(destination, gPerfLogBuffer + offset, bytes);
    UnlockBootLog(locked);
    return bytes;
}
#endif

extern "C" int th07_psp_perf_log_valid()
{
#if defined(TH07_PSP_PERF_DIAG)
    const bool perfLogLockAvailable = gBootLogSema >= 0;
    const bool perfLogLocked = LockBootLog();
    if (perfLogLockAvailable && !perfLogLocked)
    {
        return 0;
    }
    const int valid = gPerfLogInvalid ? 0 : 1;
    UnlockBootLog(perfLogLocked);
    return valid;
#else
    return 1;
#endif
}

extern "C" void th07_psp_perf_set_gameplay_active(int active)
{
#if defined(TH07_PSP_PERF_DIAG)
    __atomic_store_n(&gPerfGameplayActive, active != 0, __ATOMIC_RELEASE);
#else
    (void)active;
#endif
}

#if defined(TH07_PSP_PERF_DIAG) && !defined(TH07_PSP_1000)
extern "C" void th07_psp_perf_set_stage_load_active(int active)
{
    __atomic_store_n(&gPerfStageLoadActive, active != 0, __ATOMIC_RELEASE);
}
#endif

extern "C" void th07_psp_perf_set_window_id(unsigned int windowId)
{
#if defined(TH07_PSP_PERF_DIAG)
    __atomic_store_n(&gPerfLogWindowId, windowId, __ATOMIC_RELEASE);
#else
    (void)windowId;
#endif
}

extern "C" void th07_psp_fileio_shutdown()
{
#if defined(TH07_PSP_PERF_DIAG)
    if (gBootLogSema >= 0)
    {
        // main calls this after the observer and all audio workers have
        // stopped. Take the final token before deleting the kernel object.
        if (LockBootLog())
        {
            sceKernelDeleteSema(gBootLogSema);
            gBootLogSema = -1;
        }
    }
#endif
}

extern "C" void th07_psp_boot_notef(const char *format, ...)
{
    if (!format)
    {
        return;
    }
    char message[192];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    th07_psp_boot_note(message);
}

extern "C" void th07_psp_heap_note(const char *label)
{
    const struct mallinfo heap = mallinfo();
    char message[160];
    std::snprintf(message, sizeof(message), "%s HEAP A%uK U%uK F%uK TOP%uK B%u",
                  label ? label : "heap", static_cast<unsigned int>(heap.arena) / 1024u,
                  static_cast<unsigned int>(heap.uordblks) / 1024u,
                  static_cast<unsigned int>(heap.fordblks) / 1024u,
                  static_cast<unsigned int>(heap.keepcost) / 1024u,
                  static_cast<unsigned int>(heap.ordblks));
    th07_psp_boot_note(message);
}
