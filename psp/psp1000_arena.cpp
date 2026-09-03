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
#if defined(TH07_PSP_1000_ENEMY_MANIFEST)
// Exact occupied end offset of the established PSP-1000 pools, including
// their 16-byte allocation alignment:
//   Bullet 2276*1024, Enemy 20296*64, Item 648*1101, Effect 728*409.
// The corresponding manager translation units static-assert every payload
// size. Keep this audited value explicit so an Enemy overflow reservation
// consumes the existing 77,816-byte arena tail before asking the fragmented
// 32 MiB heap to grow the contiguous block.
constexpr std::size_t kStageBasePoolBytes = 4640776u;
constexpr std::size_t kManifestMaxEnemyExtraBytes = 893024u;
constexpr std::size_t kPoolIdleArenaBytes =
    kStageBasePoolBytes + kManifestMaxEnemyExtraBytes;
static_assert(kStageBasePoolBytes <= kStageArenaBytes,
              "PSP-1000 base pools exceed the stage arena");
static_assert(kPoolIdleArenaBytes == 5533800u,
              "PSP-1000 fixed-replay arena budget drifted");
static_assert(kPoolIdleArenaBytes <= kInitialArenaBytes,
              "PSP-1000 fixed-replay arena exceeds its early reservation");
#else
constexpr std::size_t kPoolIdleArenaBytes = kStageArenaBytes;
#endif

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
        if (gAnmBytes > kPoolIdleArenaBytes &&
            gArenaBytes > kPoolIdleArenaBytes)
        {
            void *smaller = std::realloc(gArena, kPoolIdleArenaBytes);
            if (smaller)
            {
                gArena = static_cast<unsigned char *>(smaller);
                gArenaBytes = kPoolIdleArenaBytes;
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
    if (!gArena || gMode != ArenaMode::Idle ||
        gArenaBytes <= kPoolIdleArenaBytes)
        return;
    void *smaller = std::realloc(gArena, kPoolIdleArenaBytes);
    if (smaller)
    {
        gArena = static_cast<unsigned char *>(smaller);
        gArenaBytes = kPoolIdleArenaBytes;
        th07_psp_boot_notef("PSP1000 arena trimmed to %uK",
                            static_cast<unsigned int>(gArenaBytes / 1024u));
    }
}

#if defined(TH07_PSP_1000_ENEMY_MANIFEST)
bool th07_psp_1000_begin_pools(std::size_t stageExtraBytes)
#else
bool th07_psp_1000_begin_pools()
#endif
{
    if (!gArena || gMode != ArenaMode::Idle)
    {
        th07_psp_boot_note("PSP1000 arena is still busy before pools");
        return false;
    }
#if defined(TH07_PSP_1000_ENEMY_MANIFEST)
    if (stageExtraBytes > kInitialArenaBytes - kStageBasePoolBytes)
    {
        th07_psp_boot_notef("PSP1000 pool arena request too large used%uK extra%uK max%uK",
                            static_cast<unsigned int>(kStageBasePoolBytes / 1024u),
                            static_cast<unsigned int>(stageExtraBytes / 1024u),
                            static_cast<unsigned int>(kInitialArenaBytes / 1024u));
        return false;
    }
    const std::size_t requiredBytes = kStageBasePoolBytes + stageExtraBytes;
    if (gArenaBytes < requiredBytes)
    {
        void *larger = std::realloc(gArena, requiredBytes);
        if (!larger)
        {
            th07_psp_boot_notef("PSP1000 pool arena reserve failed have%uK need%uK",
                                static_cast<unsigned int>(gArenaBytes / 1024u),
                                static_cast<unsigned int>(requiredBytes / 1024u));
            return false;
        }
        gArena = static_cast<unsigned char *>(larger);
        gArenaBytes = requiredBytes;
        th07_psp_boot_notef("PSP1000 pool arena reserved once %uK used%uK extra%uK",
                            static_cast<unsigned int>(gArenaBytes / 1024u),
                            static_cast<unsigned int>(kStageBasePoolBytes / 1024u),
                            static_cast<unsigned int>(stageExtraBytes / 1024u));
    }
#endif
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
#if defined(TH07_PSP_1000_ENEMY_MANIFEST)
        if (gArenaBytes > kPoolIdleArenaBytes)
        {
            void *smaller = std::realloc(gArena, kPoolIdleArenaBytes);
            if (smaller)
            {
                gArena = static_cast<unsigned char *>(smaller);
                gArenaBytes = kPoolIdleArenaBytes;
                th07_psp_boot_notef("PSP1000 pool arena returned %uK",
                                    static_cast<unsigned int>(gArenaBytes / 1024u));
            }
            else
            {
                th07_psp_boot_notef("PSP1000 pool arena return deferred %uK",
                                    static_cast<unsigned int>(gArenaBytes / 1024u));
            }
        }
#endif
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
