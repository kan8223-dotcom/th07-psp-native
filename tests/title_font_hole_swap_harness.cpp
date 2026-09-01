#include "TextHelper.hpp"
#include "title_font_hole_swap.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
bool gFontInMainRam;
bool gDemoteSucceeds;
bool gPromoteSucceeds;
unsigned int gDemoteCalls;
unsigned int gPromoteCalls;
std::vector<std::string> gNotes;

bool HasNote(const char *needle)
{
    for (const std::string &note : gNotes)
    {
        if (note.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

void Reset(bool inMainRam)
{
    gFontInMainRam = inMainRam;
    gDemoteSucceeds = true;
    gPromoteSucceeds = true;
    gDemoteCalls = 0;
    gPromoteCalls = 0;
    gNotes.clear();
}
} // namespace

bool TextHelper::IsDefaultFontInMainRam()
{
    return gFontInMainRam;
}

bool TextHelper::DemoteDefaultFontToFileForTitleLoad()
{
    ++gDemoteCalls;
    if (!gDemoteSucceeds)
        return false;
    gFontInMainRam = false;
    return true;
}

bool TextHelper::PromoteDefaultFontToMainRam()
{
    ++gPromoteCalls;
    if (!gPromoteSucceeds)
        return false;
    gFontInMainRam = true;
    return true;
}

extern "C" void th07_psp_boot_note(const char *message)
{
    gNotes.emplace_back(message ? message : "");
}

extern "C" void th07_psp_boot_notef(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    gNotes.emplace_back(message);
}

int main()
{
    // Normal and repeated title cycles restore the original RAM-font state.
    Reset(true);
    for (unsigned int cycle = 0; cycle < 2; ++cycle)
    {
        const Th07PspTitleFontHoleSwap swap = Th07PspBeginTitleFontHoleSwap();
        assert(swap.wasMainRam);
        assert(swap.restoreRequired);
        assert(!gFontInMainRam);
        Th07PspEndTitleFontHoleSwap(swap, true);
        assert(gFontInMainRam);
    }
    assert(gDemoteCalls == 2);
    assert(gPromoteCalls == 2);
    assert(HasNote("load=ok promote=ok state=ram"));

    // A failed first load deliberately leaves the contiguous font hole open.
    // The retry begins file-backed, performs no second demotion, and promotes
    // only after its title load succeeds.
    Reset(true);
    const Th07PspTitleFontHoleSwap failedAttempt = Th07PspBeginTitleFontHoleSwap();
    assert(!gFontInMainRam);
    Th07PspEndTitleFontHoleSwap(failedAttempt, false);
    assert(!gFontInMainRam);
    assert(gPromoteCalls == 0);
    assert(HasNote("load=failed hole=opened promote=deferred state=file retry=ready"));
    const Th07PspTitleFontHoleSwap retryAttempt = Th07PspBeginTitleFontHoleSwap();
    assert(!retryAttempt.wasMainRam);
    assert(gDemoteCalls == 1);
    Th07PspEndTitleFontHoleSwap(retryAttempt, true);
    assert(gFontInMainRam);
    assert(gPromoteCalls == 1);

    // Promotion failure is nonfatal: the already-open file font remains the
    // valid fallback for this menu and future attempts.
    Reset(true);
    gPromoteSucceeds = false;
    const Th07PspTitleFontHoleSwap promoteFailure = Th07PspBeginTitleFontHoleSwap();
    Th07PspEndTitleFontHoleSwap(promoteFailure, true);
    assert(!gFontInMainRam);
    assert(gDemoteCalls == 1);
    assert(gPromoteCalls == 1);
    assert(HasNote("load=ok promote=failed state=file fallback=file"));

    // A safe demotion failure preserves RAM mode and the title load follows
    // the unchanged RID30 path. No unnecessary promotion is attempted.
    Reset(true);
    gDemoteSucceeds = false;
    const Th07PspTitleFontHoleSwap demoteFailure = Th07PspBeginTitleFontHoleSwap();
    assert(gFontInMainRam);
    assert(!demoteFailure.restoreRequired);
    Th07PspEndTitleFontHoleSwap(demoteFailure, true);
    assert(gFontInMainRam);
    assert(gDemoteCalls == 1);
    assert(gPromoteCalls == 0);
    assert(HasNote("demote=failed-safe loadstate=ram"));

    // A naturally file-backed profile never demotes, but a successful title
    // load still tries to recover the preferred Main-RAM mode.
    Reset(false);
    const Th07PspTitleFontHoleSwap initiallyFile = Th07PspBeginTitleFontHoleSwap();
    assert(!initiallyFile.wasMainRam);
    assert(gDemoteCalls == 0);
    Th07PspEndTitleFontHoleSwap(initiallyFile, true);
    assert(gFontInMainRam);
    assert(gPromoteCalls == 1);
    return 0;
}
