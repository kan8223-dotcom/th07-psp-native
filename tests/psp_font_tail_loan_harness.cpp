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

    Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(font);
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
