#include "TextHelper.hpp"
#include "optional_ram_budget.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdint>

namespace
{
void *gAttachedArena = nullptr;
unsigned int gAttachedBytes = 0;
bool gStageReady = true;
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
    outStats->ready = gStageReady;
    return true;
}

bool TextHelper::IsStageTextCacheReady()
{
    return gStageReady;
}

bool TextHelper::IsDefaultFontInMainRam()
{
    return false;
}

bool TextHelper::DemoteDefaultFontToFile()
{
    return false;
}

extern "C" void th07_psp_boot_note(const char *)
{
}

extern "C" void th07_psp_boot_notef(const char *, ...)
{
}

int main()
{
    constexpr std::size_t kTitleBytes = 5411u * 1024u;
    void *first = Th07PspOptionalRamAcquireTitleArchive(kTitleBytes);
    assert(first);
    static_cast<std::uint8_t *>(first)[0] = 0x17u;
    static_cast<std::uint8_t *>(first)[kTitleBytes - 1u] = 0x71u;
    assert(Th07PspOptionalRamIsArchiveWorkspace(first));
    assert(!Th07PspOptionalRamIsTransientArchive(first));

    // A checked-out workspace cannot be handed out twice.
    assert(!Th07PspOptionalRamAcquireTitleArchive(kTitleBytes));
    assert(!Th07PspOptionalRamAcquireTransientArchive(1u));
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(first));
    assert(!Th07PspOptionalRamIsArchiveWorkspace(first));

    // Gui::RegisterChain loads every face archive before PrepareStage attaches
    // the stage text cache.  A6v2 therefore lends the whole idle block.  Use a
    // >4 MiB case to cover face_08, which cannot fit in the abandoned tail
    // partition design.
    constexpr std::size_t kFaceBytes = 4600u * 1024u;
    void *face = Th07PspOptionalRamAcquireTransientArchive(kFaceBytes);
    assert(face == first);
    assert(Th07PspOptionalRamIsTransientArchive(face));
    assert(Th07PspOptionalRamIsArchiveWorkspace(face));
    static_cast<std::uint8_t *>(face)[0] = 0x35u;
    static_cast<std::uint8_t *>(face)[kFaceBytes - 1u] = 0x53u;
    assert(!Th07PspOptionalRamAcquireTransientArchive(1u));
    assert(!Th07PspOptionalRamAcquireTitleArchive(kTitleBytes));

    // Defensive fallback: even if a caller violates the proven load order and
    // starts stage admission while the serial face lease is live, text must
    // use an independent allocation rather than overlap the archive bytes.
    assert(Th07PspOptionalRamPrepareStage());
    assert(gAttachedArena);
    assert(gAttachedArena != first);
    assert(Th07PspOptionalRamEnterGameplay(true));
    Th07PspOptionalRamEndStage();
    assert(Th07PspOptionalRamIsTransientArchive(face));

    assert(!Th07PspOptionalRamReleaseArchiveWorkspace(
        static_cast<std::uint8_t *>(face) + 1u));
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(face));
    assert(!Th07PspOptionalRamIsTransientArchive(face));
    assert(!Th07PspOptionalRamIsArchiveWorkspace(face));
    assert(!Th07PspOptionalRamAcquireTransientArchive(kTitleBytes + 1u));

    // Inactive duplicate release is still recognized as process-owned.  This
    // is what prevents FileSystem from passing the permanent block to free().
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(face));

    // Only after all GUI ANMs release their serial loans may gameplay borrow
    // the existing prefix instead of allocating a second 1536 KiB arena.
    assert(Th07PspOptionalRamPrepareStage());
    assert(gAttachedArena == first);
    assert(gAttachedBytes == 1536u * 1024u);
    assert(!Th07PspOptionalRamAcquireTitleArchive(kTitleBytes));
    assert(!Th07PspOptionalRamAcquireTransientArchive(1u));

    assert(Th07PspOptionalRamEnterGameplay(true));
    Th07PspOptionalRamEndStage();
    assert(!gAttachedArena);

    void *second = Th07PspOptionalRamAcquireTitleArchive(kTitleBytes);
    assert(second == first);
    static_cast<std::uint8_t *>(second)[0] = 0x2au;
    static_cast<std::uint8_t *>(second)[kTitleBytes - 1u] = 0xa2u;
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(second));

    // Font demotion owns only the font allocation; it must not discard or
    // release the process title workspace.
    void *font = Th07PspOptionalRamAcquireFontBuffer(64u * 1024u);
    assert(font);
    Th07PspOptionalRamReleaseFontBuffer(font);
    void *third = Th07PspOptionalRamAcquireTitleArchive(kTitleBytes);
    assert(third == first);
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(third));
    return 0;
}
