#pragma once

#include <cstddef>

extern "C" void th07_psp_fileio_set_launch_path(const char *argv0);
extern "C" void th07_psp_fileio_init();
extern "C" const char *th07_psp_game_dir();
extern "C" const char *th07_psp_data_dir();
extern "C" int th07_psp_original_data_ready();
extern "C" const char *th07_psp_resolve_path(const char *path, char *out, std::size_t outSize);
extern "C" void th07_psp_boot_note(const char *message);
extern "C" void th07_psp_boot_notef(const char *format, ...);
extern "C" void th07_psp_heap_note(const char *label);
extern "C" void th07_psp_perf_note(const char *message);
extern "C" void th07_psp_perf_log_flush();
extern "C" int th07_psp_perf_log_valid();
extern "C" void th07_psp_perf_set_gameplay_active(int active);
extern "C" void th07_psp_perf_set_window_id(unsigned int windowId);
#if defined(TH07_PSP_PERF_DIAG) && !defined(TH07_PSP_1000)
extern "C" void th07_psp_perf_set_stage_load_active(int active);
#endif
extern "C" void th07_psp_fileio_shutdown();

#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
// One-shot, RAM-only timing probe for the first title-logo release through
// text/font initialization.  Phase transitions and sub-operation samples do
// not write the boot log; th07_psp_boot_jitter_finish() emits the sole report
// only after it has closed the measured interval.
enum Th07PspBootJitterPhase
{
    TH07_PSP_BOOT_JITTER_RELEASE = 0,
    TH07_PSP_BOOT_JITTER_INPUT,
    TH07_PSP_BOOT_JITTER_MIDI,
    TH07_PSP_BOOT_JITTER_SFX,
    TH07_PSP_BOOT_JITTER_ANM,
    TH07_PSP_BOOT_JITTER_ASCII,
    TH07_PSP_BOOT_JITTER_VERTEX,
    TH07_PSP_BOOT_JITTER_TTF_INIT,
    TH07_PSP_BOOT_JITTER_FONT,
    TH07_PSP_BOOT_JITTER_TEXT_POST,
    TH07_PSP_BOOT_JITTER_PHASE_COUNT,
};

enum Th07PspBootJitterArchiveOp
{
    TH07_PSP_BOOT_JITTER_ARCHIVE_OPEN = 0,
    TH07_PSP_BOOT_JITTER_ARCHIVE_READ,
    TH07_PSP_BOOT_JITTER_ARCHIVE_SEEK,
    TH07_PSP_BOOT_JITTER_ARCHIVE_META,
    TH07_PSP_BOOT_JITTER_ARCHIVE_CLOSE,
    TH07_PSP_BOOT_JITTER_ARCHIVE_OP_COUNT,
};

extern "C" unsigned long long th07_psp_boot_jitter_now();
extern "C" void th07_psp_boot_jitter_begin();
extern "C" void th07_psp_boot_jitter_advance(unsigned int phase);
extern "C" void th07_psp_boot_jitter_record_sfx(
    unsigned int index, unsigned long long archiveUs,
    unsigned long long convertUs);
extern "C" void th07_psp_boot_jitter_record_font(
    unsigned int candidate, unsigned int result,
    unsigned long long openUs, unsigned long long readUs,
    unsigned long long coverageUs);
extern "C" void th07_psp_boot_jitter_record_archive_io(
    unsigned int operation, unsigned long long elapsedUs);
extern "C" void th07_psp_boot_jitter_finish();
#endif
