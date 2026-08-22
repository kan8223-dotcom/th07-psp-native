#include "fileio.hpp"

#include <pspiofilemgr.h>

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
    const SceUID fd = sceIoOpen(gBootLog, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
    {
        return;
    }
    sceIoWrite(fd, message, std::strlen(message));
    sceIoWrite(fd, "\n", 1);
    sceIoClose(fd);
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
