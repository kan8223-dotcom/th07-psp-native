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
