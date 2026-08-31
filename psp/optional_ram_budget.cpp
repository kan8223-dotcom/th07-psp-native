#include "optional_ram_budget.hpp"

#if defined(TH07_PSP) && !defined(TH07_PSP_1000)

#include <cstdlib>

#include "TextHelper.hpp"
#include "fileio.hpp"

namespace
{
constexpr unsigned int kGuardBytes = 2u * 1024u * 1024u;
constexpr unsigned int kTextPoolLadderBytes[] = {
    1536u * 1024u,
    768u * 1024u,
    384u * 1024u,
    256u * 1024u,
};

struct OptionalRamBudgetState
{
    void *guard;
    void *textPool;
    unsigned int textPoolBytes;
    bool guardAllocated;
    bool reportPending;
    bool stagePrepared;
    bool statsReported;
};

OptionalRamBudgetState g_OptionalRamBudget = {};

struct ProcessOptionalRamState
{
    void *fontBuffer;
    std::size_t fontBytes;
};

ProcessOptionalRamState g_ProcessOptionalRam = {};

void ReportTextStats()
{
    if (!g_OptionalRamBudget.stagePrepared || g_OptionalRamBudget.statsReported)
    {
        return;
    }
    StageTextCacheStats stats = {};
    const bool statsValid = TextHelper::GetStageTextCacheStats(&stats);
    th07_psp_boot_notef(
        "i1text cap=%uK entries=%u cov=%u/%u hit=%u miss=%u full=%u ready=%u valid=%u",
        statsValid ? stats.capacityBytes / 1024u : 0u, stats.entryCount,
        stats.coveredKeyCount, stats.expectedKeyCount, stats.hitCount, stats.missCount,
        stats.fullCount, stats.ready ? 1u : 0u, statsValid ? 1u : 0u);
    g_OptionalRamBudget.statsReported = true;
}

void ReleaseTextPool()
{
    // This is the only I1 result I/O.  Runtime hits/misses merely increment
    // RAM counters; the line is emitted during load rollback or after the
    // gameplay window has ended at normal teardown.
    ReportTextStats();
    // The consumer must stop using its borrowed pointer before the owner
    // releases the allocation.
    TextHelper::DetachStageTextCache();
    std::free(g_OptionalRamBudget.textPool);
    g_OptionalRamBudget.textPool = nullptr;
    g_OptionalRamBudget.textPoolBytes = 0;
}

void ReleaseGuard()
{
    std::free(g_OptionalRamBudget.guard);
    g_OptionalRamBudget.guard = nullptr;
    g_OptionalRamBudget.guardAllocated = false;
}

enum StageAllocationResult
{
    STAGE_ALLOCATION_READY,
    STAGE_ALLOCATION_GUARD_FAILED,
    STAGE_ALLOCATION_TEXT_POOL_FAILED,
};

StageAllocationResult TryPrepareStageAllocations()
{
    // This actual allocation is the admission test. Free-memory query APIs
    // deliberately do not participate in the decision.
    g_OptionalRamBudget.guard = std::malloc(kGuardBytes);
    if (!g_OptionalRamBudget.guard)
    {
        return STAGE_ALLOCATION_GUARD_FAILED;
    }
    g_OptionalRamBudget.guardAllocated = true;

    for (unsigned int bytes : kTextPoolLadderBytes)
    {
        void *pool = std::malloc(bytes);
        if (!pool)
        {
            continue;
        }
        if (TextHelper::AttachStageTextCache(pool, bytes))
        {
            g_OptionalRamBudget.textPool = pool;
            g_OptionalRamBudget.textPoolBytes = bytes;
            return STAGE_ALLOCATION_READY;
        }
        std::free(pool);
    }
    return STAGE_ALLOCATION_TEXT_POOL_FAILED;
}
} // namespace

void *Th07PspOptionalRamAcquireFontBuffer(std::size_t bytes)
{
    if (!bytes || g_ProcessOptionalRam.fontBuffer)
    {
        return nullptr;
    }
    void *buffer = std::malloc(bytes);
    if (!buffer)
    {
        return nullptr;
    }
    g_ProcessOptionalRam.fontBuffer = buffer;
    g_ProcessOptionalRam.fontBytes = bytes;
    return buffer;
}

void Th07PspOptionalRamReleaseFontBuffer(const void *borrowedBuffer)
{
    if (!borrowedBuffer || borrowedBuffer != g_ProcessOptionalRam.fontBuffer)
    {
        return;
    }
    std::free(g_ProcessOptionalRam.fontBuffer);
    g_ProcessOptionalRam = {};
}

bool Th07PspOptionalRamPrepareStage()
{
    // A new stage is also the recovery path for an interrupted prior load.
    // Teardown order is consumer detach, lower-priority pool, then guard.
    ReleaseTextPool();
    ReleaseGuard();
    g_OptionalRamBudget = {};
    g_OptionalRamBudget.reportPending = true;
    g_OptionalRamBudget.stagePrepared = true;

    const StageAllocationResult firstAttempt = TryPrepareStageAllocations();
    if (firstAttempt == STAGE_ALLOCATION_READY)
    {
        return true;
    }

    // A process-lifetime RAM font is lower priority than the 2 MiB gameplay
    // headroom gate and complete stage text cache. If it caused admission to
    // fail (for example without ARK high memory), restore the exact same
    // file-backed font, release the failed attempt and retry exactly once.
    if (firstAttempt == STAGE_ALLOCATION_GUARD_FAILED &&
        TextHelper::IsDefaultFontInMainRam())
    {
        // Give the file-backed replacement enough room to open before asking
        // it to release the 4-8 MiB process allocation. No text pool can be
        // attached on a failed TryPrepareStageAllocations() call.
        ReleaseGuard();
        if (TextHelper::DemoteDefaultFontToFile())
        {
            return TryPrepareStageAllocations() == STAGE_ALLOCATION_READY;
        }
    }
    return false;
}

bool Th07PspOptionalRamEnterGameplay(bool textCoverageComplete)
{
    const bool textReady = textCoverageComplete && g_OptionalRamBudget.textPool &&
                           TextHelper::IsStageTextCacheReady();
    if (!textReady)
    {
        // Coverage/full failures disable the whole pool; partial caches are
        // never published to gameplay.
        ReleaseTextPool();
    }

    if (g_OptionalRamBudget.reportPending)
    {
        th07_psp_boot_notef("optram guard=%s text=%uK bgmpre=0 anm=0",
                           g_OptionalRamBudget.guardAllocated ? "ok" : "fail",
                           g_OptionalRamBudget.textPoolBytes / 1024u);
        g_OptionalRamBudget.reportPending = false;
    }
    // Pre-render, rollback and all entry diagnostics completed while the guard
    // was still resident.  Its release is the final side effect before the
    // caller enters gameplay.
    ReleaseGuard();
    return textReady;
}

void Th07PspOptionalRamEndStage()
{
    // Reverse priority teardown.  BGM/ANM pools are still zero in I1; their
    // future detach/free steps belong before the text pool here.
    ReleaseTextPool();
    ReleaseGuard();
    g_OptionalRamBudget = {};
}

#else

void *Th07PspOptionalRamAcquireFontBuffer(std::size_t)
{
    return nullptr;
}

void Th07PspOptionalRamReleaseFontBuffer(const void *)
{
}

bool Th07PspOptionalRamPrepareStage()
{
    return false;
}

bool Th07PspOptionalRamEnterGameplay(bool)
{
    return false;
}

void Th07PspOptionalRamEndStage()
{
}

#endif
