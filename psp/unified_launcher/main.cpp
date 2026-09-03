#include "container_format.h"
#include "model_dispatch.hpp"
#include "xmb_selfwrap.hpp"

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psploadexec.h>
#include <systemctrl.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("TH07UNIFIED", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-256);

namespace {

const uint32_t kPbpMagic = 0x50425000u; /* "\0PBP", little-endian. */
const unsigned int kPbpSectionCount = 8u;
const unsigned int kPbpPsarSection = 7u;
const unsigned int kIoBufferBytes = 32u * 1024u;
const unsigned int kPathBytes = 640u;
const int kPspErrorNoEnt = static_cast<int>(0x80010002u);
const char kSelfwrapHelperToken[] = "--th07-xmb-helper-v2";
const char kNoOriginalDataToken[] = "-";

struct PbpHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t offsets[kPbpSectionCount];
};

static_assert(sizeof(PbpHeader) == 40u, "PBP header layout changed");

struct ContainerSelection {
    SceUID source;
    uint64_t source_file_size;
    uint64_t psar_start;
    uint64_t psar_size;
    Th07UnifiedPsarEntry runtime_entry;
    Th07UnifiedPsarEntry companion_entry;
    bool has_companion;
};

alignas(64) unsigned char g_io_buffer[kIoBufferBytes];
volatile int g_running = 1;
SceUID g_log = -1;
char g_log_path[kPathBytes];

extern "C" int th07_unified_find_original_data(
    const char *appdir, const char *device, char *data_root,
    size_t data_root_size) __attribute__((weak));
extern "C" int th07_unified_try_selfwrap(
    const char *appdir, const char *eboot_path,
    const char *data_root) __attribute__((weak));
extern "C" int th07_unified_selfwrap_needs_generation(
    const char *eboot_path,
    const char *data_root) __attribute__((weak));

int exit_callback(int, int, void *)
{
    g_running = 0;
    return 0;
}

int callback_thread(SceSize, void *)
{
    const SceUID callback =
        sceKernelCreateCallback("TH07UnifiedExit", exit_callback, NULL);
    if (callback >= 0)
        sceKernelRegisterExitCallback(callback);
    sceKernelSleepThreadCB();
    return 0;
}

void setup_callbacks()
{
    const SceUID thread = sceKernelCreateThread(
        "TH07UnifiedCallbacks", callback_thread, 0x11, 0x1000, 0, NULL);
    if (thread >= 0)
        sceKernelStartThread(thread, 0, NULL);
}

uint32_t crc32_update(uint32_t crc, const unsigned char *data,
                      unsigned int bytes)
{
    for (unsigned int index = 0; index < bytes; ++index) {
        crc ^= data[index];
        for (unsigned int bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

int write_all(SceUID file, const void *data, unsigned int bytes)
{
    const unsigned char *cursor = static_cast<const unsigned char *>(data);
    unsigned int remaining = bytes;
    while (remaining != 0u) {
        const int written = sceIoWrite(file, cursor, remaining);
        if (written <= 0)
            return written < 0 ? written : -1;
        cursor += static_cast<unsigned int>(written);
        remaining -= static_cast<unsigned int>(written);
    }
    return 0;
}

int read_exact(SceUID file, void *data, unsigned int bytes)
{
    unsigned char *cursor = static_cast<unsigned char *>(data);
    unsigned int remaining = bytes;
    while (remaining != 0u) {
        const int got = sceIoRead(file, cursor, remaining);
        if (got <= 0)
            return got < 0 ? got : -1;
        cursor += static_cast<unsigned int>(got);
        remaining -= static_cast<unsigned int>(got);
    }
    return 0;
}

int seek_absolute(SceUID file, uint64_t offset)
{
    if (offset > static_cast<uint64_t>(INT64_MAX))
        return -1;
    const SceOff result =
        sceIoLseek(file, static_cast<SceOff>(offset), PSP_SEEK_SET);
    return result == static_cast<SceOff>(offset) ? 0
                                                 : (result < 0 ? (int)result : -1);
}

void close_log()
{
    if (g_log >= 0) {
        sceIoClose(g_log);
        g_log = -1;
    }
}

void log_line(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    int bytes = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (bytes < 0)
        return;
    if (static_cast<unsigned int>(bytes) >= sizeof(line))
        bytes = sizeof(line) - 1u;
    if (bytes == 0 || line[bytes - 1] != '\n') {
        if (static_cast<unsigned int>(bytes) + 1u < sizeof(line)) {
            line[bytes++] = '\n';
            line[bytes] = '\0';
        }
    }
    if (g_log < 0 && g_log_path[0] != '\0')
        g_log = sceIoOpen(g_log_path,
                          PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (g_log >= 0 && write_all(g_log, line, static_cast<unsigned int>(bytes)) < 0) {
        close_log();
    }
}

int fail_closed(const char *step, int result)
{
    log_line("FAIL step=%s result=0x%08X", step,
             static_cast<unsigned int>(result));
    close_log();
    pspDebugScreenSetTextColor(0xff8080ffu);
    pspDebugScreenPrintf("\nTH07 unified launch stopped.\n");
    pspDebugScreenPrintf("%s (0x%08X)\n", step,
                         static_cast<unsigned int>(result));
    pspDebugScreenSetTextColor(0xffffffffu);
    pspDebugScreenPrintf("No runtime was started. Press X to exit.\n");
    while (g_running) {
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlReadBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_CROSS) != 0)
            break;
        sceKernelDelayThread(20000);
    }
    sceKernelExitGame();
    return result;
}

void show_xmb_generation_notice()
{
    pspDebugScreenClear();
    pspDebugScreenSetTextColor(0xffffffffu);
    pspDebugScreenPrintf("Touhou 7 PSP\n\n");
    pspDebugScreenSetTextColor(0xff80ffffu);
    pspDebugScreenPrintf("Generating XMB icon and background...\n");
    pspDebugScreenSetTextColor(0xff8080ffu);
    pspDebugScreenPrintf("Do not turn off the PSP.\n");
    pspDebugScreenSetTextColor(0xffffffffu);
}

void show_xmb_generation_notice_if_needed(const char *eboot_path,
                                          const char *data_root)
{
    if (th07_unified_selfwrap_needs_generation == NULL)
        return;
    const int needed = th07_unified_selfwrap_needs_generation(
        eboot_path, data_root);
    log_line("XMB generation query result=0x%08X target=%s",
             static_cast<unsigned int>(needed), eboot_path);
    if (needed == 1)
        show_xmb_generation_notice();
}

int make_paths(const char *eboot_path, char *appdir, char *device,
               char *runtime, char *temporary, char *backup,
               char *companion, char *companion_temporary,
               char *companion_backup,
               char *helper, char *helper_temporary)
{
    if (eboot_path == NULL || eboot_path[0] == '\0')
        return -10;
    const size_t path_bytes = strlen(eboot_path);
    if (path_bytes >= kPathBytes)
        return -11;
    memcpy(appdir, eboot_path, path_bytes + 1u);
    char *slash = strrchr(appdir, '/');
    if (slash == NULL || slash == appdir)
        return -12;
    *slash = '\0';

    const char *colon = strchr(eboot_path, ':');
    if (colon == NULL || colon == eboot_path ||
        static_cast<size_t>(colon - eboot_path) + 2u > 8u)
        return -13;
    const size_t device_bytes = static_cast<size_t>(colon - eboot_path) + 1u;
    memcpy(device, eboot_path, device_bytes);
    device[device_bytes] = '\0';

    if (snprintf(runtime, kPathBytes, "%s/TH07RUNTIME.PBP", appdir) >=
            static_cast<int>(kPathBytes) ||
        snprintf(temporary, kPathBytes, "%s/TH07RUNTIME.TMP", appdir) >=
            static_cast<int>(kPathBytes) ||
        snprintf(backup, kPathBytes, "%s/TH07RUNTIME.BAK", appdir) >=
            static_cast<int>(kPathBytes) ||
        snprintf(companion, kPathBytes, "%s/ge4wrap_texv1.prx", appdir) >=
            static_cast<int>(kPathBytes) ||
        snprintf(companion_temporary, kPathBytes,
                 "%s/ge4wrap_texv1.tmp", appdir) >=
            static_cast<int>(kPathBytes) ||
        snprintf(companion_backup, kPathBytes,
                 "%s/ge4wrap_texv1.bak", appdir) >=
            static_cast<int>(kPathBytes) ||
        snprintf(helper, kPathBytes, "%s/TH07XMBHELPER.PBP", appdir) >=
            static_cast<int>(kPathBytes) ||
        snprintf(helper_temporary, kPathBytes, "%s/TH07XMBHELPER.TMP", appdir) >=
            static_cast<int>(kPathBytes) ||
        snprintf(g_log_path, sizeof(g_log_path), "%s/TH07UNIFIED.LOG", appdir) >=
            static_cast<int>(sizeof(g_log_path)))
        return -14;
    return 0;
}

int stat_regular(const char *path, uint64_t *size, bool *exists)
{
    SceIoStat stat;
    memset(&stat, 0, sizeof(stat));
    const int result = sceIoGetstat(path, &stat);
    if (result == kPspErrorNoEnt) {
        *exists = false;
        *size = 0;
        return 0;
    }
    if (result < 0)
        return result;
    if (!FIO_S_ISREG(stat.st_mode) || stat.st_size < 0)
        return -20;
    *exists = true;
    *size = static_cast<uint64_t>(stat.st_size);
    return 0;
}

int hash_region(SceUID file, uint64_t offset, uint32_t bytes,
                uint32_t *crc_out)
{
    int result = seek_absolute(file, offset);
    if (result < 0)
        return result;
    uint32_t remaining = bytes;
    uint32_t crc = 0xffffffffu;
    while (remaining != 0u) {
        const unsigned int chunk = remaining < kIoBufferBytes
                                       ? remaining
                                       : kIoBufferBytes;
        result = read_exact(file, g_io_buffer, chunk);
        if (result < 0)
            return result;
        crc = crc32_update(crc, g_io_buffer, chunk);
        remaining -= chunk;
    }
    *crc_out = crc ^ 0xffffffffu;
    return 0;
}

int hash_regular_file(const char *path, uint32_t expected_size,
                      uint32_t *crc_out, bool *matches_size)
{
    uint64_t size = 0;
    bool exists = false;
    int result = stat_regular(path, &size, &exists);
    if (result < 0)
        return result;
    if (!exists || size != expected_size) {
        *matches_size = false;
        *crc_out = 0;
        return 0;
    }
    const SceUID file = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (file < 0)
        return file;
    result = hash_region(file, 0, expected_size, crc_out);
    const int close_result = sceIoClose(file);
    if (result == 0 && close_result < 0)
        result = close_result;
    *matches_size = result == 0;
    return result;
}

int validate_entry(const Th07UnifiedPsarEntry &entry, uint64_t psar_size)
{
    const uint64_t end =
        static_cast<uint64_t>(entry.payload_offset) + entry.payload_size;
    if (entry.payload_size == 0u ||
        entry.payload_offset < sizeof(Th07UnifiedPsarHeader) ||
        end > psar_size)
        return -31;
    return 0;
}

int open_container(const char *eboot_path,
                   const Th07UnifiedModelSelection &model,
                   ContainerSelection *selection)
{
    uint64_t file_size = 0;
    bool exists = false;
    int result = stat_regular(eboot_path, &file_size, &exists);
    if (result < 0 || !exists || file_size < sizeof(PbpHeader))
        return result < 0 ? result : -30;

    const SceUID source = sceIoOpen(eboot_path, PSP_O_RDONLY, 0);
    if (source < 0)
        return source;
    PbpHeader pbp;
    result = read_exact(source, &pbp, sizeof(pbp));
    if (result < 0 || pbp.magic != kPbpMagic) {
        sceIoClose(source);
        return result < 0 ? result : -32;
    }
    for (unsigned int index = 0; index < kPbpSectionCount; ++index) {
        if (pbp.offsets[index] < sizeof(PbpHeader) ||
            static_cast<uint64_t>(pbp.offsets[index]) > file_size ||
            (index != 0u && pbp.offsets[index] < pbp.offsets[index - 1u])) {
            sceIoClose(source);
            return -33;
        }
    }
    const uint64_t psar_start = pbp.offsets[kPbpPsarSection];
    const uint64_t psar_size = file_size - psar_start;
    if (psar_size < sizeof(Th07UnifiedPsarHeader)) {
        sceIoClose(source);
        return -34;
    }

    Th07UnifiedPsarHeader header;
    result = seek_absolute(source, psar_start);
    if (result == 0)
        result = read_exact(source, &header, sizeof(header));
    if (result < 0 ||
        memcmp(header.magic, TH07_UNIFIED_PSAR_MAGIC,
               TH07_UNIFIED_PSAR_MAGIC_BYTES) != 0 ||
        header.version != TH07_UNIFIED_PSAR_VERSION ||
        header.entry_count != TH07_UNIFIED_PSAR_ENTRY_COUNT) {
        sceIoClose(source);
        return result < 0 ? result : -35;
    }

    const Th07UnifiedPsarEntry *psp1000 = &header.entries[0];
    const Th07UnifiedPsarEntry *psp2000 = &header.entries[1];
    const Th07UnifiedPsarEntry *ge4 = &header.entries[2];
    uint64_t expected_offset = sizeof(Th07UnifiedPsarHeader);
    for (unsigned int index = 0; index < TH07_UNIFIED_PSAR_ENTRY_COUNT;
         ++index) {
        const Th07UnifiedPsarEntry &entry = header.entries[index];
        result = validate_entry(entry, psar_size);
        if (result < 0 || entry.payload_offset != expected_offset) {
            sceIoClose(source);
            return result < 0 ? result : -36;
        }
        expected_offset += entry.payload_size;
    }
    if (expected_offset != psar_size ||
        psp1000->profile_id != TH07_UNIFIED_PROFILE_PSP1000 ||
        psp1000->model_min != 0u || psp1000->model_max != 0u ||
        psp2000->profile_id != TH07_UNIFIED_PROFILE_PSP2000_PLUS ||
        psp2000->model_min != 1u || psp2000->model_max != UINT32_MAX ||
        ge4->profile_id != TH07_UNIFIED_COMPANION_GE4 ||
        ge4->model_min != 1u || ge4->model_max != UINT32_MAX ||
        ge4->payload_size != TH07_UNIFIED_GE4_SIZE ||
        ge4->payload_crc32 != TH07_UNIFIED_GE4_CRC32) {
        sceIoClose(source);
        return -37;
    }
    const Th07UnifiedPsarEntry *chosen =
        model.profile_id == TH07_UNIFIED_PROFILE_PSP1000 ? psp1000 : psp2000;
    if (model.effective_model < chosen->model_min ||
        model.effective_model > chosen->model_max) {
        sceIoClose(source);
        return -38;
    }

    uint32_t embedded_crc = 0;
    result = hash_region(source, psar_start + chosen->payload_offset,
                         chosen->payload_size, &embedded_crc);
    if (result < 0 || embedded_crc != chosen->payload_crc32) {
        sceIoClose(source);
        return result < 0 ? result : -39;
    }
    const bool has_companion = model.effective_model >= 1u;
    if (has_companion) {
        result = hash_region(source, psar_start + ge4->payload_offset,
                             ge4->payload_size, &embedded_crc);
        if (result < 0 || embedded_crc != ge4->payload_crc32) {
            sceIoClose(source);
            return result < 0 ? result : -43;
        }
    }
    selection->source = source;
    selection->source_file_size = file_size;
    selection->psar_start = psar_start;
    selection->psar_size = psar_size;
    selection->runtime_entry = *chosen;
    selection->has_companion = has_companion;
    if (selection->has_companion)
        selection->companion_entry = *ge4;
    else
        memset(&selection->companion_entry, 0,
               sizeof(selection->companion_entry));
    return 0;
}

int remove_regular_if_present(const char *path)
{
    uint64_t size = 0;
    bool exists = false;
    const int result = stat_regular(path, &size, &exists);
    if (result < 0)
        return result;
    if (!exists)
        return 0;
    return sceIoRemove(path);
}

int payload_matches(const char *path, const Th07UnifiedPsarEntry &entry,
                    bool *matches)
{
    uint32_t crc = 0;
    bool matches_size = false;
    const int result =
        hash_regular_file(path, entry.payload_size, &crc, &matches_size);
    if (result < 0)
        return result;
    *matches = matches_size && crc == entry.payload_crc32;
    return 0;
}

struct PayloadUpdate {
    const Th07UnifiedPsarEntry *entry;
    const char *final_path;
    const char *temporary;
    const char *backup;
    bool reused;
    bool moved_old;
    bool installed_new;
};

void init_payload_update(PayloadUpdate *update,
                         const Th07UnifiedPsarEntry &entry,
                         const char *final_path, const char *temporary,
                         const char *backup)
{
    update->entry = &entry;
    update->final_path = final_path;
    update->temporary = temporary;
    update->backup = backup;
    update->reused = false;
    update->moved_old = false;
    update->installed_new = false;
}

int discard_staged_payload(PayloadUpdate *update, const char *device)
{
    const int result = remove_regular_if_present(update->temporary);
    if (result < 0)
        return result;
    return sceIoSync(device, 0);
}

int stage_payload(const ContainerSelection &container, PayloadUpdate *update,
                  const char *device)
{
    bool matches = false;
    int result = payload_matches(
        update->final_path, *update->entry, &matches);
    if (result < 0)
        return result;
    if (matches) {
        update->reused = true;
        result = remove_regular_if_present(update->temporary);
        if (result == 0)
            result = remove_regular_if_present(update->backup);
        if (result == 0)
            result = sceIoSync(device, 0);
        return result;
    }

    result = remove_regular_if_present(update->temporary);
    if (result < 0)
        return result;
    const SceUID output = sceIoOpen(
        update->temporary, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (output < 0)
        return output;
    result = seek_absolute(
        container.source,
        container.psar_start + update->entry->payload_offset);
    uint32_t remaining = update->entry->payload_size;
    uint32_t crc = 0xffffffffu;
    while (result == 0 && remaining != 0u) {
        const unsigned int chunk = remaining < kIoBufferBytes
                                       ? remaining
                                       : kIoBufferBytes;
        result = read_exact(container.source, g_io_buffer, chunk);
        if (result == 0)
            result = write_all(output, g_io_buffer, chunk);
        if (result == 0)
            crc = crc32_update(crc, g_io_buffer, chunk);
        remaining -= result == 0 ? chunk : 0u;
    }
    const int close_result = sceIoClose(output);
    if (result == 0 && close_result < 0)
        result = close_result;
    crc ^= 0xffffffffu;
    if (result < 0 || remaining != 0u ||
        crc != update->entry->payload_crc32) {
        const int cleanup = remove_regular_if_present(update->temporary);
        return result < 0 ? result : (cleanup < 0 ? cleanup : -40);
    }
    result = sceIoSync(device, 0);
    if (result < 0)
        return result;
    result = payload_matches(update->temporary, *update->entry, &matches);
    if (result < 0 || !matches) {
        const int cleanup = remove_regular_if_present(update->temporary);
        return result < 0 ? result : (cleanup < 0 ? cleanup : -41);
    }
    return 0;
}

int commit_staged_payload(PayloadUpdate *update, const char *device)
{
    if (update->reused)
        return 0;
    int result = remove_regular_if_present(update->backup);
    if (result < 0)
        return result;
    uint64_t final_size = 0;
    bool final_exists = false;
    result = stat_regular(update->final_path, &final_size, &final_exists);
    if (result < 0)
        return result;
    if (final_exists) {
        result = sceIoRename(update->final_path, update->backup);
        if (result < 0)
            return result;
        update->moved_old = true;
    }
    result = sceIoRename(update->temporary, update->final_path);
    if (result < 0)
        return result;
    update->installed_new = true;
    result = sceIoSync(device, 0);
    if (result < 0)
        return result;
    bool matches = false;
    result = payload_matches(update->final_path, *update->entry, &matches);
    return result < 0 ? result : (matches ? 0 : -42);
}

int rollback_payload(PayloadUpdate *update, const char *device)
{
    int first_error = 0;
    if (update->installed_new) {
        const int result = remove_regular_if_present(update->final_path);
        if (result < 0)
            first_error = result;
        else
            update->installed_new = false;
    }
    if (update->moved_old && first_error == 0) {
        const int result = sceIoRename(update->backup, update->final_path);
        if (result < 0)
            first_error = result;
        else
            update->moved_old = false;
    }
    const int temporary_result =
        remove_regular_if_present(update->temporary);
    if (first_error == 0 && temporary_result < 0)
        first_error = temporary_result;
    const int sync_result = sceIoSync(device, 0);
    if (first_error == 0 && sync_result < 0)
        first_error = sync_result;
    return first_error;
}

int finish_payload_update(PayloadUpdate *update, const char *device)
{
    int result = remove_regular_if_present(update->temporary);
    if (result == 0)
        result = remove_regular_if_present(update->backup);
    if (result == 0)
        result = sceIoSync(device, 0);
    return result;
}

int update_runtime_and_companion(
    const ContainerSelection &container, const char *device,
    const char *runtime, const char *runtime_temporary,
    const char *runtime_backup, const char *companion,
    const char *companion_temporary, const char *companion_backup,
    bool *runtime_reused, bool *companion_reused)
{
    PayloadUpdate runtime_update;
    init_payload_update(&runtime_update, container.runtime_entry, runtime,
                        runtime_temporary, runtime_backup);
    PayloadUpdate companion_update;
    if (container.has_companion)
        init_payload_update(&companion_update, container.companion_entry,
                            companion, companion_temporary, companion_backup);

    int result = 0;
    if (container.has_companion)
        result = stage_payload(container, &companion_update, device);
    if (result == 0)
        result = stage_payload(container, &runtime_update, device);
    if (result < 0) {
        const int cleanup = container.has_companion
                                ? discard_staged_payload(
                                      &companion_update, device)
                                : 0;
        discard_staged_payload(&runtime_update, device);
        return cleanup < 0 ? cleanup : result;
    }

    if (container.has_companion)
        result = commit_staged_payload(&companion_update, device);
    if (result == 0)
        result = commit_staged_payload(&runtime_update, device);
    if (result < 0) {
        const int runtime_rollback = rollback_payload(&runtime_update, device);
        const int companion_rollback = container.has_companion
                                           ? rollback_payload(
                                                 &companion_update, device)
                                           : 0;
        if (runtime_rollback < 0)
            return runtime_rollback;
        if (companion_rollback < 0)
            return companion_rollback;
        return result;
    }

    result = finish_payload_update(&runtime_update, device);
    if (container.has_companion) {
        const int companion_finish =
            finish_payload_update(&companion_update, device);
        if (result == 0 && companion_finish < 0)
            result = companion_finish;
    }
    *runtime_reused = runtime_update.reused;
    *companion_reused = container.has_companion
                            ? companion_update.reused
                            : true;
    return result;
}

int copy_regular_file_verified(const char *source_path,
                               const char *destination_path,
                               const char *device)
{
    uint64_t source_size = 0;
    bool source_exists = false;
    int result = stat_regular(source_path, &source_size, &source_exists);
    if (result < 0 || !source_exists || source_size == 0u ||
        source_size > UINT32_MAX)
        return result < 0 ? result : -50;

    result = remove_regular_if_present(destination_path);
    if (result < 0)
        return result;
    const SceUID source = sceIoOpen(source_path, PSP_O_RDONLY, 0);
    if (source < 0)
        return source;
    const SceUID destination = sceIoOpen(
        destination_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (destination < 0) {
        sceIoClose(source);
        return destination;
    }

    uint64_t remaining = source_size;
    uint32_t crc = 0xffffffffu;
    while (result == 0 && remaining != 0u) {
        const unsigned int chunk = remaining < kIoBufferBytes
                                       ? static_cast<unsigned int>(remaining)
                                       : kIoBufferBytes;
        result = read_exact(source, g_io_buffer, chunk);
        if (result == 0)
            result = write_all(destination, g_io_buffer, chunk);
        if (result == 0) {
            crc = crc32_update(crc, g_io_buffer, chunk);
            remaining -= chunk;
        }
    }
    const int source_close = sceIoClose(source);
    const int destination_close = sceIoClose(destination);
    if (result == 0 && source_close < 0)
        result = source_close;
    if (result == 0 && destination_close < 0)
        result = destination_close;
    if (result == 0 && sceIoSync(device, 0) < 0)
        result = -51;
    if (result < 0 || remaining != 0u) {
        remove_regular_if_present(destination_path);
        return result < 0 ? result : -52;
    }

    uint32_t copied_crc = 0;
    bool matches_size = false;
    result = hash_regular_file(destination_path,
                               static_cast<uint32_t>(source_size),
                               &copied_crc, &matches_size);
    crc ^= 0xffffffffu;
    if (result < 0 || !matches_size || copied_crc != crc) {
        remove_regular_if_present(destination_path);
        return result < 0 ? result : -53;
    }
    return 0;
}

int prepare_selfwrap_helper(const char *canonical, const char *helper,
                            const char *helper_temporary,
                            const char *device)
{
    int result = remove_regular_if_present(helper_temporary);
    if (result == 0)
        result = remove_regular_if_present(helper);
    if (result == 0)
        result = copy_regular_file_verified(
            canonical, helper_temporary, device);
    if (result == 0)
        result = sceIoRename(helper_temporary, helper);
    if (result == 0 && sceIoSync(device, 0) < 0)
        result = -54;
    if (result < 0) {
        remove_regular_if_present(helper_temporary);
        remove_regular_if_present(helper);
        return result;
    }

    uint64_t canonical_size = 0;
    bool canonical_exists = false;
    result = stat_regular(canonical, &canonical_size, &canonical_exists);
    if (result < 0 || !canonical_exists || canonical_size > UINT32_MAX) {
        remove_regular_if_present(helper);
        return result < 0 ? result : -55;
    }
    uint32_t canonical_crc = 0;
    uint32_t helper_crc = 0;
    bool canonical_size_ok = false;
    bool helper_size_ok = false;
    result = hash_regular_file(canonical, static_cast<uint32_t>(canonical_size),
                               &canonical_crc, &canonical_size_ok);
    if (result == 0)
        result = hash_regular_file(helper, static_cast<uint32_t>(canonical_size),
                                   &helper_crc, &helper_size_ok);
    if (result < 0 || !canonical_size_ok || !helper_size_ok ||
        canonical_crc != helper_crc) {
        remove_regular_if_present(helper);
        return result < 0 ? result : -56;
    }
    return 0;
}

bool append_argument(char *block, size_t capacity, size_t *used,
                     const char *argument)
{
    if (block == NULL || used == NULL || argument == NULL)
        return false;
    const size_t bytes = strlen(argument) + 1u;
    if (*used > capacity || bytes > capacity - *used)
        return false;
    memcpy(block + *used, argument, bytes);
    *used += bytes;
    return true;
}

int load_exec_path(const char *path, const char *device,
                   void *arguments, size_t argument_bytes)
{
    if (path == NULL || device == NULL || arguments == NULL ||
        argument_bytes == 0u || argument_bytes > UINT32_MAX)
        return -57;
    SceKernelLoadExecVSHParam parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.size = sizeof(parameters);
    parameters.args = static_cast<SceSize>(argument_bytes);
    parameters.argp = arguments;
    parameters.key = "game";
    return strcmp(device, "ef0:") == 0
               ? sctrlKernelLoadExecVSHEf2(path, &parameters)
               : sctrlKernelLoadExecVSHMs2(path, &parameters);
}

int try_selfwrap(const char *appdir, const char *eboot_path,
                 const char *device, char *data_root,
                 size_t data_root_size)
{
    if (data_root == NULL || data_root_size == 0u)
        return -58;
    data_root[0] = '\0';
    if (th07_unified_find_original_data == NULL ||
        th07_unified_try_selfwrap == NULL) {
        log_line("XMB selfwrap hook unavailable; neutral media retained");
        return 0;
    }
    const int found = th07_unified_find_original_data(
        appdir, device, data_root, data_root_size);
    if (found < 0) {
        log_line("XMB original-data validation failed result=0x%08X",
                 static_cast<unsigned int>(found));
        return found;
    } else if (found == 0) {
        const int repaired =
            th07_unified_try_selfwrap(appdir, eboot_path, NULL);
        log_line("XMB original data absent; neutral repair result=0x%08X",
                 static_cast<unsigned int>(repaired));
        return repaired;
    } else {
        show_xmb_generation_notice_if_needed(eboot_path, data_root);
        const int wrapped =
            th07_unified_try_selfwrap(appdir, eboot_path, data_root);
        log_line("XMB selfwrap result=0x%08X data_root=%s",
                 static_cast<unsigned int>(wrapped), data_root);
        return wrapped;
    }
}

int run_selfwrap_helper(int argc, char **argv, const char *appdir,
                        const char *device, const char *runtime,
                        const char *helper)
{
    char canonical[kPathBytes];
    if (snprintf(canonical, sizeof(canonical), "%s/EBOOT.PBP", appdir) >=
        static_cast<int>(sizeof(canonical)))
        return fail_closed("selfwrap helper canonical path", -60);
    if (argc != 5 || argv == NULL ||
        strcmp(argv[0], helper) != 0 ||
        strcmp(argv[1], kSelfwrapHelperToken) != 0 ||
        strcmp(argv[2], canonical) != 0 ||
        strcmp(argv[3], runtime) != 0 || argv[4] == NULL ||
        strlen(argv[4]) >= kPathBytes)
        return fail_closed("selfwrap helper arguments", -61);

    const char *data_root = strcmp(argv[4], kNoOriginalDataToken) == 0
                                ? NULL
                                : argv[4];
    show_xmb_generation_notice_if_needed(canonical, data_root);
    int wrap_result = -62;
    if (th07_unified_try_selfwrap != NULL)
        wrap_result = th07_unified_try_selfwrap(
            appdir, canonical, data_root);
    log_line("XMB helper canonical result=0x%08X target=%s data=%s",
             static_cast<unsigned int>(wrap_result), canonical,
             data_root != NULL ? data_root : "(neutral-repair)");

    char arguments[kPathBytes];
    size_t argument_bytes = 0u;
    if (!append_argument(arguments, sizeof(arguments), &argument_bytes,
                         runtime))
        return fail_closed("selfwrap helper runtime arguments", -63);
    log_line("XMB helper continuing runtime path=%s", runtime);
    close_log();
    const int result = load_exec_path(
        runtime, device, arguments, argument_bytes);
    return fail_closed("selfwrap helper runtime LoadExec returned", result);
}

int launch_selfwrap_helper(const char *canonical, const char *runtime,
                           const char *helper,
                           const char *helper_temporary,
                           const char *device, const char *data_root)
{
    int result = prepare_selfwrap_helper(
        canonical, helper, helper_temporary, device);
    if (result < 0)
        return result;

    char arguments[kPathBytes * 4u + 64u];
    size_t argument_bytes = 0u;
    const char *data_argument = data_root != NULL && data_root[0] != '\0'
                                    ? data_root
                                    : kNoOriginalDataToken;
    if (!append_argument(arguments, sizeof(arguments), &argument_bytes, helper) ||
        !append_argument(arguments, sizeof(arguments), &argument_bytes,
                         kSelfwrapHelperToken) ||
        !append_argument(arguments, sizeof(arguments), &argument_bytes,
                         canonical) ||
        !append_argument(arguments, sizeof(arguments), &argument_bytes,
                         runtime) ||
        !append_argument(arguments, sizeof(arguments), &argument_bytes,
                         data_argument))
    {
        remove_regular_if_present(helper);
        return -64;
    }
    log_line("XMB selfwrap deferred; launching helper=%s", helper);
    close_log();
    return load_exec_path(helper, device, arguments, argument_bytes);
}

} // namespace

int main(int argc, char **argv)
{
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0x00000000u);
    pspDebugScreenSetTextColor(0xffffffffu);
    pspDebugScreenClear();
    pspDebugScreenPrintf("Touhou 7 PSP\nStarting universal runtime...\n");

    if (argc <= 0 || argv == NULL || argv[0] == NULL)
        return fail_closed("missing launcher argv0", -1);

    char appdir[kPathBytes];
    char device[8];
    char runtime[kPathBytes];
    char temporary[kPathBytes];
    char backup[kPathBytes];
    char companion[kPathBytes];
    char companion_temporary[kPathBytes];
    char companion_backup[kPathBytes];
    char helper[kPathBytes];
    char helper_temporary[kPathBytes];
    int result = make_paths(argv[0], appdir, device, runtime, temporary, backup,
                            companion, companion_temporary, companion_backup,
                            helper, helper_temporary);
    if (result < 0)
        return fail_closed("invalid launcher path", result);

    // A helper is a byte-exact temporary copy of the outer PBP. It runs only
    // so the canonical EBOOT is no longer the executing file and its immutable
    // fixed PNG slots can be opened by VFS implementations such as PPSSPP.
    if (strcmp(argv[0], helper) == 0)
        return run_selfwrap_helper(
            argc, argv, appdir, device, runtime, helper);

    const Th07UnifiedModelSelection model = th07_unified_select_model();
    log_line("BOOT raw_model=%d effective_model=%lu profile=0x%08lX fallback=%u",
             model.raw_model, static_cast<unsigned long>(model.effective_model),
             static_cast<unsigned long>(model.profile_id),
             model.safe_fallback ? 1u : 0u);
    log_line("MODEL reason=%s", model.reason);

    ContainerSelection container;
    memset(&container, 0, sizeof(container));
    container.source = -1;
    result = open_container(argv[0], model, &container);
    if (result < 0)
        return fail_closed("DATA.PSAR validation", result);
    log_line("PSAR valid offset=%lu size=%lu crc32=%08lX",
             static_cast<unsigned long>(container.runtime_entry.payload_offset),
             static_cast<unsigned long>(container.runtime_entry.payload_size),
             static_cast<unsigned long>(container.runtime_entry.payload_crc32));

    bool reused = false;
    bool companion_reused = true;
    result = update_runtime_and_companion(
        container, device, runtime, temporary, backup, companion,
        companion_temporary, companion_backup, &reused, &companion_reused);
    const int source_close_result = sceIoClose(container.source);
    container.source = -1;
    if (result == 0 && source_close_result < 0)
        result = source_close_result;
    if (result < 0)
        return fail_closed(container.has_companion
                               ? "runtime/GE4 companion transaction"
                               : "runtime transaction",
                           result);
    if (container.has_companion)
        log_line("GE4 COMPANION %s path=%s size=%lu crc32=%08lX",
                 companion_reused ? "reused" : "extracted", companion,
                 static_cast<unsigned long>(
                     container.companion_entry.payload_size),
                 static_cast<unsigned long>(
                     container.companion_entry.payload_crc32));
    else
        log_line("GE4 COMPANION skipped model0");
    log_line("RUNTIME %s path=%s", reused ? "reused" : "extracted", runtime);

    /*
     * The optional hook writes only the fixed ICON0/PIC1 slots after its own
     * validator finds a legal local install. The distributable carries fully
     * transparent neutral placeholders and remains usable when writes defer.
     */
    // A prior helper cannot be removed while it is executing. Its next normal
    // canonical launch owns this conservative cleanup.
    const int stale_helper = remove_regular_if_present(helper);
    const int stale_temporary = remove_regular_if_present(helper_temporary);
    if (stale_helper < 0 || stale_temporary < 0)
        log_line("XMB stale helper cleanup result=0x%08X/0x%08X",
                 static_cast<unsigned int>(stale_helper),
                 static_cast<unsigned int>(stale_temporary));

    char data_root[kPathBytes];
    const int wrap_result = try_selfwrap(
        appdir, argv[0], device, data_root, sizeof(data_root));
    if (wrap_result == TH07_UNIFIED_SELFWRAP_DEFERRED) {
        const int helper_result = launch_selfwrap_helper(
            argv[0], runtime, helper, helper_temporary, device, data_root);
        // Success does not return. Any helper preparation/LoadExec failure is
        // non-fatal to gameplay; the already verified runtime remains usable.
        log_line("XMB helper deferred/failure result=0x%08X; continuing runtime",
                 static_cast<unsigned int>(helper_result));
    }

    const bool internal_storage = strcmp(device, "ef0:") == 0;
    log_line("LOADEXEC api=%s path=%s argv0=%s",
             internal_storage ? "sctrlKernelLoadExecVSHEf2"
                              : "sctrlKernelLoadExecVSHMs2",
             runtime, runtime);
    close_log();
    result = load_exec_path(runtime, device, runtime, strlen(runtime) + 1u);
    return fail_closed("systemctrl LoadExec returned", result);
}
