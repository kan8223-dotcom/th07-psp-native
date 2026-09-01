#include "optional_ram_budget.hpp"

#if defined(TH07_PSP) && !defined(TH07_PSP_1000)

#include <cstdint>
#include <cstdlib>

#include "TextHelper.hpp"
#include "fileio.hpp"

namespace
{
constexpr unsigned int kGuardBytes = 2u * 1024u * 1024u;
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    defined(TH07_PSP_TITLE_FONT_HOLE_SWAP) && \
    defined(TH07_PSP_LOCAL_FONT_SUBSET)
constexpr std::size_t kFontTailAlignment = 64u;
#endif
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
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE)
    // True for either the legacy whole-workspace prefix loan or A6v4's
    // disjoint FONT-tail loan.  In both cases this owner must not call free().
    bool textPoolBorrowsTitleWorkspace;
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP) && \
    defined(TH07_PSP_LOCAL_FONT_SUBSET)
    bool textPoolBorrowsFontTail;
    std::size_t fontTailOffsetBytes;
#endif
#endif
    bool guardAllocated;
    bool reportPending;
    bool stagePrepared;
    bool statsReported;
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
    bool sharedArenaGuardPreserved;
#endif
};

OptionalRamBudgetState g_OptionalRamBudget = {};

struct ProcessOptionalRamState
{
    void *fontBuffer;
    std::size_t fontBytes;
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE)
    enum ArchiveWorkspaceLease
    {
        ARCHIVE_WORKSPACE_IDLE,
        ARCHIVE_WORKSPACE_FONT,
        ARCHIVE_WORKSPACE_TITLE,
        ARCHIVE_WORKSPACE_TRANSIENT,
    };

    void *titleWorkspace;
    std::size_t titleWorkspaceBytes;
    ArchiveWorkspaceLease archiveWorkspaceLease;
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
    bool fontBufferBorrowsTitleWorkspace;
#endif
#endif
};

ProcessOptionalRamState g_ProcessOptionalRam = {};

#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE)
bool EnsureTitleArchiveWorkspace(std::size_t bytes)
{
    if (!bytes)
    {
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
        th07_psp_boot_note("A6V3 ARENA reserve=failed reason=size-zero state=none");
#endif
        return false;
    }
    if (g_ProcessOptionalRam.titleWorkspace)
    {
        if (bytes <= g_ProcessOptionalRam.titleWorkspaceBytes)
            return true;
        th07_psp_boot_notef("title workspace too small have%uK need%uK",
                            static_cast<unsigned int>(
                                g_ProcessOptionalRam.titleWorkspaceBytes / 1024u),
                            static_cast<unsigned int>(bytes / 1024u));
        return false;
    }

    void *workspace = std::malloc(bytes);
    if (!workspace)
    {
        th07_psp_boot_notef("title workspace reserve failed %uK",
                            static_cast<unsigned int>(bytes / 1024u));
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
        th07_psp_boot_notef("A6V3 ARENA reserve=failed reason=alloc need=%uK state=none",
                            static_cast<unsigned int>(bytes / 1024u));
#endif
        return false;
    }
    g_ProcessOptionalRam.titleWorkspace = workspace;
    g_ProcessOptionalRam.titleWorkspaceBytes = bytes;
    th07_psp_boot_notef("title workspace ready %uK",
                        static_cast<unsigned int>(bytes / 1024u));
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
    th07_psp_boot_notef("A6V3 ARENA reserve=ok cap=%uK state=idle",
                        static_cast<unsigned int>(bytes / 1024u));
#endif
    return true;
}
#endif

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
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE)
    if (!g_OptionalRamBudget.textPoolBorrowsTitleWorkspace)
#endif
        std::free(g_OptionalRamBudget.textPool);
    g_OptionalRamBudget.textPool = nullptr;
    g_OptionalRamBudget.textPoolBytes = 0;
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE)
    g_OptionalRamBudget.textPoolBorrowsTitleWorkspace = false;
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP) && \
    defined(TH07_PSP_LOCAL_FONT_SUBSET)
    g_OptionalRamBudget.textPoolBorrowsFontTail = false;
    g_OptionalRamBudget.fontTailOffsetBytes = 0;
#endif
#endif
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
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    defined(TH07_PSP_TITLE_FONT_HOLE_SWAP) && \
    defined(TH07_PSP_LOCAL_FONT_SUBSET)
        // A6v4 keeps the small, coverage-checked subset font at the front of
        // the fixed title workspace.  The remaining aligned tail is disjoint
        // storage, so gameplay may borrow it without a second heap allocation.
        // All arithmetic is bounded by the arena capacity before pointer
        // formation.  Any inconsistent state simply falls through to the
        // established independent-allocation path below.
        if (g_ProcessOptionalRam.titleWorkspace &&
            g_ProcessOptionalRam.fontBufferBorrowsTitleWorkspace &&
            g_ProcessOptionalRam.fontBuffer == g_ProcessOptionalRam.titleWorkspace &&
            g_ProcessOptionalRam.archiveWorkspaceLease ==
                ProcessOptionalRamState::ARCHIVE_WORKSPACE_FONT &&
            g_ProcessOptionalRam.fontBytes <=
                g_ProcessOptionalRam.titleWorkspaceBytes)
        {
            unsigned char *workspace =
                static_cast<unsigned char *>(g_ProcessOptionalRam.titleWorkspace);
            unsigned char *unalignedTail =
                workspace + g_ProcessOptionalRam.fontBytes;
            const std::size_t misalignment =
                reinterpret_cast<std::uintptr_t>(unalignedTail) %
                kFontTailAlignment;
            const std::size_t padding =
                misalignment ? kFontTailAlignment - misalignment : 0u;
            if (padding <=
                    g_ProcessOptionalRam.titleWorkspaceBytes -
                        g_ProcessOptionalRam.fontBytes &&
                static_cast<std::size_t>(bytes) <=
                    g_ProcessOptionalRam.titleWorkspaceBytes -
                        g_ProcessOptionalRam.fontBytes - padding)
            {
                const std::size_t tailOffset =
                    g_ProcessOptionalRam.fontBytes + padding;
                unsigned char *tail = workspace + tailOffset;
                if (TextHelper::AttachStageTextCache(tail, bytes))
                {
                    g_OptionalRamBudget.textPool = tail;
                    g_OptionalRamBudget.textPoolBytes = bytes;
                    g_OptionalRamBudget.textPoolBorrowsTitleWorkspace = true;
                    g_OptionalRamBudget.textPoolBorrowsFontTail = true;
                    g_OptionalRamBudget.fontTailOffsetBytes = tailOffset;
                    return STAGE_ALLOCATION_READY;
                }
            }
        }
#endif
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE)
        // title01.anm needs this workspace only while the menu is being
        // registered.  GameManager has released the title source before this
        // stage admission point, so lend the idle prefix to the text cache.
        // This makes the persistent anti-fragmentation reservation replace,
        // rather than merely add to, the largest optional stage allocation.
        if (g_ProcessOptionalRam.titleWorkspace &&
            g_ProcessOptionalRam.archiveWorkspaceLease ==
                ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE &&
            bytes <= g_ProcessOptionalRam.titleWorkspaceBytes &&
            TextHelper::AttachStageTextCache(g_ProcessOptionalRam.titleWorkspace, bytes))
        {
            g_OptionalRamBudget.textPool = g_ProcessOptionalRam.titleWorkspace;
            g_OptionalRamBudget.textPoolBytes = bytes;
            g_OptionalRamBudget.textPoolBorrowsTitleWorkspace = true;
            return STAGE_ALLOCATION_READY;
        }
#endif

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
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE)
    // A stage text-cache loan uses either the workspace base or A6v4's
    // disjoint tail without changing the archive lease enum. Refuse a late
    // font promotion while either loan is attached.
    if (g_OptionalRamBudget.textPoolBorrowsTitleWorkspace)
    {
        return nullptr;
    }
#endif
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
    if (g_ProcessOptionalRam.titleWorkspace)
    {
        if (g_ProcessOptionalRam.archiveWorkspaceLease !=
                ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE ||
            bytes > g_ProcessOptionalRam.titleWorkspaceBytes)
        {
            th07_psp_boot_notef(
                "A6V3 ARENA lease=FONT failed reason=%s need=%uK cap=%uK",
                g_ProcessOptionalRam.archiveWorkspaceLease ==
                        ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE
                    ? "size"
                    : "busy",
                static_cast<unsigned int>(bytes / 1024u),
                static_cast<unsigned int>(g_ProcessOptionalRam.titleWorkspaceBytes / 1024u));
            return nullptr;
        }
        g_ProcessOptionalRam.archiveWorkspaceLease =
            ProcessOptionalRamState::ARCHIVE_WORKSPACE_FONT;
        g_ProcessOptionalRam.fontBuffer = g_ProcessOptionalRam.titleWorkspace;
        g_ProcessOptionalRam.fontBytes = bytes;
        g_ProcessOptionalRam.fontBufferBorrowsTitleWorkspace = true;
        th07_psp_boot_notef("A6V3 ARENA lease=FONT bytes=%uK cap=%uK",
                            static_cast<unsigned int>(bytes / 1024u),
                            static_cast<unsigned int>(
                                g_ProcessOptionalRam.titleWorkspaceBytes / 1024u));
        return g_ProcessOptionalRam.fontBuffer;
    }
#endif

    void *buffer = std::malloc(bytes);
    if (!buffer)
    {
        return nullptr;
    }
    g_ProcessOptionalRam.fontBuffer = buffer;
    g_ProcessOptionalRam.fontBytes = bytes;
    return buffer;
}

namespace
{
void ReleaseFontBufferOwned(const void *borrowedBuffer, bool preserveTitleWorkspace)
{
#if !defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) || \
    !defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
    (void)preserveTitleWorkspace;
#endif
    if (!borrowedBuffer || borrowedBuffer != g_ProcessOptionalRam.fontBuffer)
    {
        return;
    }
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
    if (g_ProcessOptionalRam.fontBufferBorrowsTitleWorkspace)
    {
#if defined(TH07_PSP_LOCAL_FONT_SUBSET)
        // Normal title entry and process teardown detach the stage consumer
        // first.  Refuse an out-of-order FONT release so TITLE can never
        // acquire the same arena while the disjoint text tail is still live.
        if (g_OptionalRamBudget.textPoolBorrowsFontTail)
        {
            th07_psp_boot_note(
                "A6V4 ARENA transition=FONT->IDLE refused reason=text-tail-attached");
            return;
        }
#endif
        if (g_ProcessOptionalRam.archiveWorkspaceLease ==
            ProcessOptionalRamState::ARCHIVE_WORKSPACE_FONT)
        {
            g_ProcessOptionalRam.archiveWorkspaceLease =
                ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE;
            th07_psp_boot_notef("A6V3 ARENA transition=FONT->IDLE preserve=%u",
                                preserveTitleWorkspace ? 1u : 0u);
        }
        else
        {
            th07_psp_boot_note("A6V3 ARENA release=FONT ignored reason=lease-mismatch");
        }
        g_ProcessOptionalRam.fontBuffer = nullptr;
        g_ProcessOptionalRam.fontBytes = 0;
        g_ProcessOptionalRam.fontBufferBorrowsTitleWorkspace = false;
        if (!preserveTitleWorkspace &&
            g_ProcessOptionalRam.archiveWorkspaceLease ==
                ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE)
        {
            std::free(g_ProcessOptionalRam.titleWorkspace);
            g_ProcessOptionalRam.titleWorkspace = nullptr;
            g_ProcessOptionalRam.titleWorkspaceBytes = 0;
            th07_psp_boot_note("A6V3 ARENA transition=IDLE->NONE reason=font-release");
        }
        return;
    }
#endif
    std::free(g_ProcessOptionalRam.fontBuffer);
    g_ProcessOptionalRam.fontBuffer = nullptr;
    g_ProcessOptionalRam.fontBytes = 0;
}
} // namespace

void Th07PspOptionalRamReleaseFontBuffer(const void *borrowedBuffer)
{
    ReleaseFontBufferOwned(borrowedBuffer, false);
}

void Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(
    const void *borrowedBuffer)
{
    ReleaseFontBufferOwned(borrowedBuffer, true);
}

bool Th07PspOptionalRamReserveTitleFontWorkspace(std::size_t titleBytes)
{
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
    if (g_ProcessOptionalRam.archiveWorkspaceLease !=
        ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE)
    {
        th07_psp_boot_note("A6V3 ARENA reserve=failed reason=busy");
        return false;
    }
    return EnsureTitleArchiveWorkspace(titleBytes);
#else
    (void)titleBytes;
    return false;
#endif
}

#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE)
void *Th07PspOptionalRamAcquireTitleArchive(std::size_t bytes)
{
    if (!bytes ||
        g_ProcessOptionalRam.archiveWorkspaceLease !=
            ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE ||
        g_OptionalRamBudget.textPoolBorrowsTitleWorkspace)
    {
        return nullptr;
    }

    if (!EnsureTitleArchiveWorkspace(bytes))
        return nullptr;

    g_ProcessOptionalRam.archiveWorkspaceLease =
        ProcessOptionalRamState::ARCHIVE_WORKSPACE_TITLE;
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
    th07_psp_boot_notef("A6V3 ARENA transition=IDLE->TITLE bytes=%uK",
                        static_cast<unsigned int>(bytes / 1024u));
#endif
    th07_psp_boot_notef("title workspace acquire %uK",
                        static_cast<unsigned int>(bytes / 1024u));
    return g_ProcessOptionalRam.titleWorkspace;
}

#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT)
void *Th07PspOptionalRamAcquireTransientArchive(std::size_t bytes)
{
    if (!bytes || !g_ProcessOptionalRam.titleWorkspace ||
        g_ProcessOptionalRam.archiveWorkspaceLease !=
            ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE ||
        g_OptionalRamBudget.textPoolBorrowsTitleWorkspace)
    {
        return nullptr;
    }

    if (bytes > g_ProcessOptionalRam.titleWorkspaceBytes)
    {
        return nullptr;
    }

    g_ProcessOptionalRam.archiveWorkspaceLease =
        ProcessOptionalRamState::ARCHIVE_WORKSPACE_TRANSIENT;
    th07_psp_boot_notef("archive workspace transient acquire %uK cap%uK",
                        static_cast<unsigned int>(bytes / 1024u),
                        static_cast<unsigned int>(
                            g_ProcessOptionalRam.titleWorkspaceBytes / 1024u));
    return g_ProcessOptionalRam.titleWorkspace;
}

bool Th07PspOptionalRamIsTransientArchive(const void *borrowedBuffer)
{
    return borrowedBuffer && borrowedBuffer == g_ProcessOptionalRam.titleWorkspace &&
           g_ProcessOptionalRam.archiveWorkspaceLease ==
               ProcessOptionalRamState::ARCHIVE_WORKSPACE_TRANSIENT;
}
#else
void *Th07PspOptionalRamAcquireTransientArchive(std::size_t)
{
    return nullptr;
}

bool Th07PspOptionalRamIsTransientArchive(const void *)
{
    return false;
}
#endif

bool Th07PspOptionalRamIsArchiveWorkspace(const void *borrowedBuffer)
{
    return borrowedBuffer && borrowedBuffer == g_ProcessOptionalRam.titleWorkspace &&
           (g_ProcessOptionalRam.archiveWorkspaceLease ==
                ProcessOptionalRamState::ARCHIVE_WORKSPACE_TITLE ||
            g_ProcessOptionalRam.archiveWorkspaceLease ==
                ProcessOptionalRamState::ARCHIVE_WORKSPACE_TRANSIENT);
}

bool Th07PspOptionalRamReleaseArchiveWorkspace(const void *borrowedBuffer)
{
    if (!borrowedBuffer || borrowedBuffer != g_ProcessOptionalRam.titleWorkspace)
    {
        return false;
    }

    const ProcessOptionalRamState::ArchiveWorkspaceLease releasedLease =
        g_ProcessOptionalRam.archiveWorkspaceLease;
    if (releasedLease == ProcessOptionalRamState::ARCHIVE_WORKSPACE_TITLE)
    {
        g_ProcessOptionalRam.archiveWorkspaceLease =
            ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE;
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
        th07_psp_boot_note("A6V3 ARENA transition=TITLE->IDLE");
#endif
        th07_psp_boot_note("title workspace released");
    }
    else if (releasedLease == ProcessOptionalRamState::ARCHIVE_WORKSPACE_TRANSIENT)
    {
        g_ProcessOptionalRam.archiveWorkspaceLease =
            ProcessOptionalRamState::ARCHIVE_WORKSPACE_IDLE;
        th07_psp_boot_note("archive workspace transient released");
    }
    // Even an inactive same-address pointer belongs to this process owner.
    // Returning true prevents FileSystem's generic libc-free fallback from
    // destroying the permanent anti-fragmentation reservation.
    return true;
}
#endif

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
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
        // In A6v3 this allocation is primarily the process-lifetime title
        // recovery arena; the font is only its current borrower.  Freeing it
        // here would make the next menu return ask the already-fragmented
        // heap for title01.anm's 5.4 MiB block and recreate A6.  Disable this
        // stage's optional text prewarm instead. Gameplay remains available.
        if (g_ProcessOptionalRam.fontBufferBorrowsTitleWorkspace &&
            g_ProcessOptionalRam.archiveWorkspaceLease ==
                ProcessOptionalRamState::ARCHIVE_WORKSPACE_FONT)
        {
            ReleaseGuard();
            // Defer diagnostics to EnterGameplay so PrepareStage retains the
            // existing one-summary/no-I/O admission contract.
            g_OptionalRamBudget.sharedArenaGuardPreserved = true;
            return false;
        }
#endif
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
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    defined(TH07_PSP_TITLE_FONT_HOLE_SWAP) && \
    defined(TH07_PSP_LOCAL_FONT_SUBSET)
        if (g_OptionalRamBudget.textPoolBorrowsFontTail)
        {
            th07_psp_boot_notef(
                "A6V4 TEXT loan=font-tail offset=%uK bytes=%uK font=%uK cap=%uK",
                static_cast<unsigned int>(
                    g_OptionalRamBudget.fontTailOffsetBytes / 1024u),
                g_OptionalRamBudget.textPoolBytes / 1024u,
                static_cast<unsigned int>(g_ProcessOptionalRam.fontBytes / 1024u),
                static_cast<unsigned int>(
                    g_ProcessOptionalRam.titleWorkspaceBytes / 1024u));
        }
#endif
#if defined(TH07_PSP_TITLE_FONT_HOLE_SWAP)
        if (g_OptionalRamBudget.sharedArenaGuardPreserved)
        {
            th07_psp_boot_note(
                "A6V3 STAGE guard=failed action=text-off arena=preserved state=font");
        }
#endif
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

void Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(const void *)
{
}

bool Th07PspOptionalRamReserveTitleFontWorkspace(std::size_t)
{
    return false;
}

void *Th07PspOptionalRamAcquireTitleArchive(std::size_t)
{
    return nullptr;
}

void *Th07PspOptionalRamAcquireTransientArchive(std::size_t)
{
    return nullptr;
}

bool Th07PspOptionalRamIsTransientArchive(const void *)
{
    return false;
}

bool Th07PspOptionalRamIsArchiveWorkspace(const void *)
{
    return false;
}

bool Th07PspOptionalRamReleaseArchiveWorkspace(const void *)
{
    return false;
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
