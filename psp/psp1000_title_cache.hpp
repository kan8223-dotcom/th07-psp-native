#pragma once

#include <cstddef>

#if defined(TH07_PSP_1000)

#define TH07_PSP_1000_TITLE_IMAGE_MARKER 0x314b5053
#define TH07_PSP_1000_TITLE_HIRES_IMAGE_MARKER 0x324b5053

// title01.anm contains more than 5 MiB of BGRA pixels.  Build a user-local
// mostly-256px A4R4G4B4 copy on first use so later title returns fit the shared
// 4.125 MiB PSP-1000 arena.  The title/menu lettering atlas stays native-size
// so its small text survives the final 640x480 -> PSP LCD reduction.  The
// cache is derived at runtime from the user's own DAT; it is never part of the
// release package.
unsigned char *th07_psp_1000_load_title_cache(std::size_t sourceBytes,
                                               std::size_t *cacheBytes);
bool th07_psp_1000_build_title_cache(const void *source, std::size_t sourceBytes);

#endif
