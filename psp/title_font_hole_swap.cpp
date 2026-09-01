#include "title_font_hole_swap.hpp"

#include "TextHelper.hpp"
#include "fileio.hpp"

#if defined(TH07_PSP)
#include <pspkernel.h>
#else
#include <chrono>
#endif

namespace
{
unsigned long long TitleFontSwapNowUs()
{
#if defined(TH07_PSP)
    return sceKernelGetSystemTimeWide();
#else
    using Clock = std::chrono::steady_clock;
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch())
            .count());
#endif
}
} // namespace

Th07PspTitleFontHoleSwap Th07PspBeginTitleFontHoleSwap()
{
    Th07PspTitleFontHoleSwap swap = {};
    swap.startUs = TitleFontSwapNowUs();
    swap.wasMainRam = TextHelper::IsDefaultFontInMainRam();
    if (!swap.wasMainRam)
    {
        th07_psp_boot_note("A6V3 FONT HOLE begin state=file action=none");
        return swap;
    }

    const unsigned long long demoteStartUs = TitleFontSwapNowUs();
    swap.demoteCallSucceeded = TextHelper::DemoteDefaultFontToFileForTitleLoad();
    swap.demoteUs = TitleFontSwapNowUs() - demoteStartUs;
    const bool remainsMainRam = TextHelper::IsDefaultFontInMainRam();
    // The current demoter is transactional, but deriving restoration need
    // from the observed state also contains a future partial-failure bug.
    swap.restoreRequired = !remainsMainRam;
    const char *demoteState =
        swap.demoteCallSucceeded ? (remainsMainRam ? "inconsistent" : "ok")
                                 : (remainsMainRam ? "failed-safe" : "partial");
    th07_psp_boot_notef("A6V3 FONT HOLE begin state=ram demote=%s loadstate=%s",
                        demoteState, remainsMainRam ? "ram" : "file");
    return swap;
}

void Th07PspEndTitleFontHoleSwap(const Th07PspTitleFontHoleSwap &swap,
                                bool titleLoadSucceeded)
{
    const char *loadState = titleLoadSucceeded ? "ok" : "failed";
    const bool currentlyMainRam = TextHelper::IsDefaultFontInMainRam();
    if (!titleLoadSucceeded)
    {
        // Preserve the contiguous hole across Supervisor's trim-and-retry.
        // The successful retry promotes even though that retry did not itself
        // demote the already-file-backed font.
        const unsigned long long totalUs = TitleFontSwapNowUs() - swap.startUs;
        th07_psp_boot_notef(
            "A6V3 FONT HOLE end load=failed hole=%s promote=deferred state=%s retry=ready demote_us=%llu promote_us=0 total_us=%llu",
            swap.restoreRequired ? "opened" : (swap.wasMainRam ? "unavailable" : "preexisting"),
            currentlyMainRam ? "ram" : "file", swap.demoteUs, totalUs);
        return;
    }
    if (currentlyMainRam)
    {
        const unsigned long long totalUs = TitleFontSwapNowUs() - swap.startUs;
        th07_psp_boot_notef(
            "A6V3 FONT HOLE end load=%s restore=none state=ram demote_us=%llu promote_us=0 total_us=%llu",
            loadState, swap.demoteUs, totalUs);
        return;
    }

    // Also runs on a successful retry whose Begin() observed file state.  The
    // previous failed attempt deliberately left that hole open for the retry.
    const unsigned long long promoteStartUs = TitleFontSwapNowUs();
    const bool promoteCallSucceeded = TextHelper::PromoteDefaultFontToMainRam();
    const unsigned long long promoteUs = TitleFontSwapNowUs() - promoteStartUs;
    const unsigned long long totalUs = TitleFontSwapNowUs() - swap.startUs;
    const bool restoredMainRam = TextHelper::IsDefaultFontInMainRam();
    if (promoteCallSucceeded && restoredMainRam)
    {
        th07_psp_boot_notef(
            "A6V3 FONT HOLE end load=%s promote=ok state=ram demote_us=%llu promote_us=%llu total_us=%llu",
            loadState, swap.demoteUs, promoteUs, totalUs);
        return;
    }

    // Promotion is transactional: failure leaves the already-open file font
    // active.  Menu registration must continue when the title itself loaded.
    th07_psp_boot_notef(
        "A6V3 FONT HOLE end load=%s promote=%s state=%s fallback=file demote_us=%llu promote_us=%llu total_us=%llu",
        loadState, promoteCallSucceeded ? "inconsistent" : "failed",
        restoredMainRam ? "ram" : "file", swap.demoteUs, promoteUs, totalUs);
}
