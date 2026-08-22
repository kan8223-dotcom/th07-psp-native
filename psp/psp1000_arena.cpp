#include "psp1000_arena.hpp"

#if defined(TH07_PSP_1000)

#include "fileio.hpp"

#include <cstdint>
#include <cstdlib>

namespace
{
// title01.anm is roughly 5.29 MiB and is the largest decompressed ANM in the
// original archive. After it is compacted the block shrinks to 4 MiB + 512
// KiB, enough for face_08_00.anm (slightly over 4 MiB) and the reduced
// PSP-1000 gameplay pools.
constexpr std::size_t kInitialArenaBytes = 5u * 1024u * 1024u + 512u * 1024u;
// Compact bullet payloads allow all 1,024 original bullets to fit. Keep all
// 1,100 Items and 400 normal Effects too: their slot reuse and update order are
// gameplay-visible in replays. This leaves roughly 76 KiB of pool headroom.
constexpr std::size_t kStageArenaBytes = 4u * 1024u * 1024u + 512u * 1024u;

enum class ArenaMode
{
    Unavailable,
    Idle,
    Anm,
    Pools,
};

unsigned char *gArena;
std::size_t gArenaBytes;
std::size_t gAnmBytes;
std::size_t gPoolOffset;
ArenaMode gMode = ArenaMode::Unavailable;
}

bool th07_psp_1000_arena_init()
{
    if (gArena)
        return true;

    gArena = static_cast<unsigned char *>(std::malloc(kInitialArenaBytes));
    if (!gArena)
    {
        th07_psp_boot_notef("PSP1000 arena allocation failed %uK",
                            static_cast<unsigned int>(kInitialArenaBytes / 1024u));
        return false;
    }
    gMode = ArenaMode::Idle;
    gArenaBytes = kInitialArenaBytes;
    gAnmBytes = 0;
    gPoolOffset = 0;
    th07_psp_boot_notef("PSP1000 shared ANM/pool arena %uK ready",
                        static_cast<unsigned int>(gArenaBytes / 1024u));
    return true;
}

void *th07_psp_1000_acquire_anm(std::size_t bytes)
{
    if (!gArena || gMode != ArenaMode::Idle || bytes > kInitialArenaBytes)
        return nullptr;
    if (bytes > gArenaBytes)
    {
        void *larger = std::realloc(gArena, kInitialArenaBytes);
        if (!larger)
            return nullptr;
        gArena = static_cast<unsigned char *>(larger);
        gArenaBytes = kInitialArenaBytes;
        th07_psp_boot_notef("PSP1000 arena regrown to %uK for title",
                            static_cast<unsigned int>(gArenaBytes / 1024u));
    }
    gMode = ArenaMode::Anm;
    gAnmBytes = bytes;
    return gArena;
}

bool th07_psp_1000_release_anm(void *ptr)
{
    if (!th07_psp_1000_arena_owns(ptr))
        return false;
    if (gMode == ArenaMode::Anm)
    {
        gMode = ArenaMode::Idle;
        // The title archive is the only source larger than the later stage
        // scratch requirement.  Return the excess as soon as it is compacted so
        // SDL_image can decode the 640x480 title JPEG without a white screen.
        if (gAnmBytes > kStageArenaBytes && gArenaBytes > kStageArenaBytes)
        {
            void *smaller = std::realloc(gArena, kStageArenaBytes);
            if (smaller)
            {
                gArena = static_cast<unsigned char *>(smaller);
                gArenaBytes = kStageArenaBytes;
                th07_psp_boot_notef("PSP1000 arena shrunk to %uK after title",
                                    static_cast<unsigned int>(gArenaBytes / 1024u));
            }
        }
        gAnmBytes = 0;
    }
    return true;
}

void th07_psp_1000_trim_to_stage()
{
    if (!gArena || gMode != ArenaMode::Idle || gArenaBytes <= kStageArenaBytes)
        return;
    void *smaller = std::realloc(gArena, kStageArenaBytes);
    if (smaller)
    {
        gArena = static_cast<unsigned char *>(smaller);
        gArenaBytes = kStageArenaBytes;
        th07_psp_boot_notef("PSP1000 arena trimmed to %uK",
                            static_cast<unsigned int>(gArenaBytes / 1024u));
    }
}

bool th07_psp_1000_begin_pools()
{
    if (!gArena || gMode != ArenaMode::Idle)
    {
        th07_psp_boot_note("PSP1000 arena is still busy before pools");
        return false;
    }
    gMode = ArenaMode::Pools;
    gPoolOffset = 0;
    return true;
}

void *th07_psp_1000_alloc_pool(std::size_t bytes, std::size_t alignment)
{
    if (!gArena || gMode != ArenaMode::Pools || alignment == 0 ||
        (alignment & (alignment - 1u)) != 0)
        return nullptr;

    const std::size_t alignedOffset = (gPoolOffset + alignment - 1u) & ~(alignment - 1u);
    if (alignedOffset > gArenaBytes || bytes > gArenaBytes - alignedOffset)
        return nullptr;
    void *result = gArena + alignedOffset;
    gPoolOffset = alignedOffset + bytes;
    return result;
}

void th07_psp_1000_end_pools()
{
    if (gMode == ArenaMode::Pools)
    {
        gPoolOffset = 0;
        gMode = ArenaMode::Idle;
    }
}

bool th07_psp_1000_arena_owns(const void *ptr)
{
    const auto address = reinterpret_cast<std::uintptr_t>(ptr);
    const auto start = reinterpret_cast<std::uintptr_t>(gArena);
    return gArena && address >= start && address < start + gArenaBytes;
}

std::size_t th07_psp_1000_arena_capacity()
{
    return gArenaBytes;
}

std::size_t th07_psp_1000_pool_bytes_used()
{
    return gPoolOffset;
}

#endif
