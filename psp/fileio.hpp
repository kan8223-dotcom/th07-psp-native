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
