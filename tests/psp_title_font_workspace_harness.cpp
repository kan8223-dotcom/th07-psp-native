#include "TextHelper.hpp"
#include "optional_ram_budget.hpp"

#include <cassert>
#include <cstdarg>

namespace
{
void *gAttachedArena;
unsigned int gAttachedBytes;
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

extern "C" void th07_psp_boot_note(const char *) {}
extern "C" void th07_psp_boot_notef(const char *, ...) {}

int main()
{
    constexpr std::size_t kTitleBytes = 5411u * 1024u;
    constexpr std::size_t kFontBytes = 4386u * 1024u;

    assert(Th07PspOptionalRamReserveTitleFontWorkspace(kTitleBytes));
    void *font = Th07PspOptionalRamAcquireFontBuffer(kFontBytes);
    assert(font);
    assert(!Th07PspOptionalRamIsArchiveWorkspace(font));
    assert(!Th07PspOptionalRamAcquireTitleArchive(kTitleBytes));

    // Unified archive release must recognize the shared address without
    // stealing the active FONT lease.
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(font));
    assert(!Th07PspOptionalRamAcquireTitleArchive(kTitleBytes));

    Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(font);
    void *title = Th07PspOptionalRamAcquireTitleArchive(kTitleBytes);
    assert(title == font);
    assert(Th07PspOptionalRamIsArchiveWorkspace(title));
    assert(!Th07PspOptionalRamAcquireFontBuffer(kFontBytes));
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(title));

    // Promotion failure returns a shared FONT lease to IDLE but keeps the
    // process arena alive for the next title cycle.
    void *failedPromotionFont = Th07PspOptionalRamAcquireFontBuffer(kFontBytes);
    assert(failedPromotionFont == font);
    Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(failedPromotionFont);
    void *afterPromotionFailureTitle =
        Th07PspOptionalRamAcquireTitleArchive(kTitleBytes);
    assert(afterPromotionFailureTitle == font);
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(afterPromotionFailureTitle));

    void *restoredFont = Th07PspOptionalRamAcquireFontBuffer(kFontBytes);
    assert(restoredFont == font);

    // FONT owns the shared arena throughout gameplay, so stage text must use
    // its normal independent allocation rather than aliasing font bytes.
    assert(Th07PspOptionalRamPrepareStage());
    assert(gAttachedArena);
    assert(gAttachedArena != restoredFont);
    assert(gAttachedBytes == 1536u * 1024u);
    assert(Th07PspOptionalRamEnterGameplay(true));
    Th07PspOptionalRamEndStage();

    // The ordinary stage-guard/teardown release retains its old semantics:
    // shared FONT closes and the entire arena is actually freed.
    Th07PspOptionalRamReleaseFontBuffer(restoredFont);
    void *newTitle = Th07PspOptionalRamAcquireTitleArchive(kTitleBytes);
    assert(newTitle);
    assert(Th07PspOptionalRamReleaseArchiveWorkspace(newTitle));
    void *finalFont = Th07PspOptionalRamAcquireFontBuffer(kFontBytes);
    assert(finalFont == newTitle);
    Th07PspOptionalRamReleaseFontBuffer(finalFont);
    return 0;
}
