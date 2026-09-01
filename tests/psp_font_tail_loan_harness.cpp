#include "TextHelper.hpp"
#include "optional_ram_budget.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstring>

namespace
{
void *gAttachedArena;
unsigned int gAttachedBytes;
unsigned int gRefusedTransitions;
}

bool TextHelper::AttachStageTextCache(void *arena, u32 capacityBytes)
{
    gAttachedArena = arena;
    gAttachedBytes = capacityBytes;
    return arena && capacityBytes >= 256u * 1024u;
}

void TextHelper::DetachStageTextCache()
{
    gAttachedArena = nullptr;
    gAttachedBytes = 0;
}

bool TextHelper::GetStageTextCacheStats(StageTextCacheStats *outStats)
{
    if (!outStats || !gAttachedArena)
        return false;
    *outStats = {};
    outStats->capacityBytes = gAttachedBytes;
    outStats->ready = true;
    return true;
}

bool TextHelper::IsStageTextCacheReady() { return true; }
bool TextHelper::IsDefaultFontInMainRam() { return false; }
bool TextHelper::DemoteDefaultFontToFile() { return false; }

extern "C" void th07_psp_boot_note(const char *message)
{
    if (message &&
        std::strstr(message, "transition=FONT->IDLE refused") != nullptr)
    {
        ++gRefusedTransitions;
    }
}

extern "C" void th07_psp_boot_notef(const char *, ...) {}

int main()
{
    constexpr std::size_t kTitleBytes = 5411u * 1024u;
    constexpr std::size_t kSmallFontBytes = 301u * 1024u + 17u;
    constexpr std::size_t kAlignment = 64u;
    assert(Th07PspOptionalRamReserveTitleFontWorkspace(kTitleBytes));
    void *font = Th07PspOptionalRamAcquireFontBuffer(kSmallFontBytes);
    assert(font);
    static_cast<std::uint8_t *>(font)[kSmallFontBytes - 1u] = 0x5au;
    const std::uintptr_t unalignedTail =
        reinterpret_cast<std::uintptr_t>(font) + kSmallFontBytes;
    const std::uintptr_t expectedTail =
        (unalignedTail + kAlignment - 1u) & ~(kAlignment - 1u);

    assert(Th07PspOptionalRamPrepareStage());
    assert(reinterpret_cast<std::uintptr_t>(gAttachedArena) == expectedTail);
    assert(reinterpret_cast<std::uintptr_t>(gAttachedArena) % kAlignment == 0u);
    assert(gAttachedBytes == 1536u * 1024u);
    static_cast<std::uint8_t *>(gAttachedArena)[0] = 0xa5u;
    assert(static_cast<std::uint8_t *>(font)[kSmallFontBytes - 1u] == 0x5au);

    // An abnormal demotion order is contained by the owner.  TITLE remains
    // unavailable until the stage cache consumer has detached.
    Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(font);
    assert(gRefusedTransitions == 1u);
    assert(!Th07PspOptionalRamAcquireTitleArchive(kTitleBytes));
    assert(!Th07PspOptionalRamAcquireFontBuffer(kSmallFontBytes));

    assert(Th07PspOptionalRamEnterGameplay(true));
    Th07PspOptionalRamEndStage();
    assert(!gAttachedArena);

    // After stage teardown and before the next stage text-cache admission,
    // face_*.anm may use the same disjoint tail as synchronous decode scratch.
    // The 3 MiB stage-6 portrait source is the real-hardware regression case.
    constexpr std::size_t kStage6FaceBytes = 3072u * 1024u;
    void *face = Th07PspOptionalRamAcquireTransientArchive(kStage6FaceBytes);
    assert(face == reinterpret_cast<void *>(expectedTail));
    assert(Th07PspOptionalRamIsTransientArchive(face));
    assert(Th07PspOptionalRamIsArchiveWorkspace(face));
    static_cast<std::uint8_t *>(face)[0] = 0x36u;
    static_cast<std::uint8_t *>(face)[kStage6FaceBytes - 1u] = 0x63u;
    assert(static_cast<std::uint8_t *>(font)[kSmallFontBytes - 1u] == 0x5au);
    assert(!Th07PspOptionalRamAcquireTransientArchive(1u));
    assert(!Th07PspOptionalRamAcquireTitleArchive(kTitleBytes));

    // Defensive abnormal ordering: if stage admission occurs before the
    // synchronous face source is returned, its text cache must use a separate
    // allocation rather than overlap either the font or archive tail.
    assert(Th07PspOptionalRamPrepareStage());
    const std::uintptr_t activeArchivePool =
        reinterpret_cast<std::uintptr_t>(gAttachedArena);
    const std::uintptr_t workspaceBase =
        reinterpret_cast<std::uintptr_t>(font);
    assert(activeArchivePool < workspaceBase ||
           activeArchivePool >= workspaceBase + kTitleBytes);
    assert(Th07PspOptionalRamEnterGameplay(true));
    Th07PspOptionalRamEndStage();
    assert(Th07PspOptionalRamIsTransientArchive(face));

    assert(Th07PspOptionalRamReleaseArchiveWorkspace(face));
    assert(!Th07PspOptionalRamIsTransientArchive(face));
    assert(!Th07PspOptionalRamIsArchiveWorkspace(face));
    // Duplicate release remains owned and cannot fall through to libc free.
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(face));

    // Real ordering: release the face source, then let stage text reuse the
    // same tail.  Also prove the known 4.6 MiB portrait upper case fits.
    assert(Th07PspOptionalRamPrepareStage());
    assert(reinterpret_cast<std::uintptr_t>(gAttachedArena) == expectedTail);
    assert(Th07PspOptionalRamEnterGameplay(true));
    Th07PspOptionalRamEndStage();
    constexpr std::size_t kLargeFaceBytes = 4600u * 1024u;
    void *largeFace =
        Th07PspOptionalRamAcquireTransientArchive(kLargeFaceBytes);
    assert(largeFace == reinterpret_cast<void *>(expectedTail));
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(largeFace));

    Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(font);
    // The sublease bookkeeping is gone after FONT demotion, but the old tail
    // address is still an interior pointer owned by the live process arena.
    // A delayed duplicate release must remain absorbed rather than reaching
    // libc free().
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(largeFace));
    void *title = Th07PspOptionalRamAcquireTitleArchive(kTitleBytes);
    assert(title == font);
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(title));

    // Preserve full-cache coverage when a 1536 KiB independent allocation is
    // available.  A smaller tail must not silently downgrade the established
    // first ladder rung merely to force arena reuse.
    constexpr std::size_t kMediumFontBytes = kTitleBytes - 500u * 1024u;
    void *mediumFont = Th07PspOptionalRamAcquireFontBuffer(kMediumFontBytes);
    assert(mediumFont == font);
    assert(Th07PspOptionalRamPrepareStage());
    assert(gAttachedArena);
    const std::uintptr_t mediumBase =
        reinterpret_cast<std::uintptr_t>(mediumFont);
    const std::uintptr_t mediumPool =
        reinterpret_cast<std::uintptr_t>(gAttachedArena);
    assert(mediumPool < mediumBase || mediumPool >= mediumBase + kTitleBytes);
    assert(gAttachedBytes == 1536u * 1024u);
    assert(Th07PspOptionalRamEnterGameplay(true));
    Th07PspOptionalRamEndStage();
    Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(mediumFont);
    title = Th07PspOptionalRamAcquireTitleArchive(kTitleBytes);
    assert(title == font);
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(title));

    // If the font leaves less than the minimum 256 KiB tail, fail closed to
    // the established separately-owned allocation instead of aliasing bytes.
    constexpr std::size_t kLargeFontBytes = kTitleBytes - 128u * 1024u;
    void *largeFont = Th07PspOptionalRamAcquireFontBuffer(kLargeFontBytes);
    assert(largeFont == font);
    const std::uintptr_t unalignedLargeTail =
        reinterpret_cast<std::uintptr_t>(largeFont) + kLargeFontBytes;
    const std::uintptr_t expectedLargeTail =
        (unalignedLargeTail + kAlignment - 1u) & ~(kAlignment - 1u);
    assert(Th07PspOptionalRamPrepareStage());
    assert(gAttachedArena);
    assert(reinterpret_cast<std::uintptr_t>(gAttachedArena) != expectedLargeTail);
    assert(gAttachedBytes == 1536u * 1024u);
    assert(Th07PspOptionalRamEnterGameplay(true));
    Th07PspOptionalRamEndStage();
    Th07PspOptionalRamReleaseFontBuffer(largeFont);
    return 0;
}
