#pragma once

#include <cstddef>

enum
{
    // The currently executing outer PBP could not be opened for writing.
    // The launcher may retry from its non-canonical helper copy.
    TH07_UNIFIED_SELFWRAP_DEFERRED = -7,
};

// Locate a complete, unmodified TH07 1.00b data pair in the same order used
// by the game runtime.  Returns 1 and writes the containing directory to out,
// 0 when no valid pair exists, or a negative value for invalid arguments.
extern "C" int th07_unified_find_original_data(
    const char *appdir, const char *launch_device, char *out,
    std::size_t out_size);

// Query the fixed media slots without modifying the PBP. Returns 1 only when
// valid original data is present and ICON0/PIC1 still need to be generated,
// 0 when no generation is needed, or a negative self-wrap error. Callers use
// this to show a power-off warning only around real first-run work.
extern "C" int th07_unified_selfwrap_needs_generation(
    const char *eboot_path, const char *data_root);

// Generate ICON0/PIC1 from the user's own th07.dat inside immutable fixed PBP
// media slots. The packer must have emitted the TH07XMB2 marker and transparent
// owned placeholders. Passing null/empty data_root repairs a torn owned slot
// back to deterministic transparent placeholders without original data.
//
// Returns 1 after committing newly generated fixed-slot media, 0 when the PBP was
// already wrapped (or already neutral with no data), and a negative value on
// safe failure. Runtime never writes the PBP header or DATA.PSP/PSAR regions.
extern "C" int th07_unified_try_selfwrap(
    const char *appdir, const char *eboot_path, const char *data_root);
