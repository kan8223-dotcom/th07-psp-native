#pragma once

#include <cstddef>

// Process-lifetime backing for the optional Main-RAM font stream. TextHelper
// receives a borrowed pointer; this module remains the sole allocator/owner.
// Stage teardown deliberately does not release it.
void *Th07PspOptionalRamAcquireFontBuffer(std::size_t bytes);
void Th07PspOptionalRamReleaseFontBuffer(const void *borrowedBuffer);
void Th07PspOptionalRamReleaseFontBufferPreserveWorkspace(
    const void *borrowedBuffer);

// A6v3 reserves the exact title01.anm decompressed size before the startup
// RAM-font promotion. The font then borrows that arena rather than owning a
// second process allocation. Under A6v4 (LOCAL_FONT_SUBSET), the stage text
// cache may borrow a 64-byte-aligned, disjoint tail after the actual font
// bytes. The cache must be ended/detached before a FONT->TITLE transition;
// the owner refuses the transition if this lifecycle contract is violated.
bool Th07PspOptionalRamReserveTitleFontWorkspace(std::size_t titleBytes);

// Process-lifetime workspace for the decompressed title01.anm source.  The
// first title load reserves it while the heap is still contiguous; later title
// returns reuse the same address instead of asking a fragmented heap for one
// 5.4 MiB block.  Release returns the workspace to this owner, not to libc.
void *Th07PspOptionalRamAcquireTitleArchive(std::size_t bytes);

// A6v2 lends the whole idle workspace. With FONT_TAIL_ARCHIVE enabled, A6v4
// keeps the subset font in the prefix and lends only the aligned, disjoint
// tail. Stage GUI registration happens before text-cache admission, and every
// accepted face_*.anm source is compacted and released synchronously. Nested
// checkouts and checkouts while the text cache owns arena bytes fail closed.
void *Th07PspOptionalRamAcquireTransientArchive(std::size_t bytes);
bool Th07PspOptionalRamIsTransientArchive(const void *borrowedBuffer);
bool Th07PspOptionalRamIsArchiveWorkspace(const void *borrowedBuffer);

// Unified release is required because title and transient loans intentionally
// use the same address.  It consumes whichever lease is active and also
// recognizes inactive/stale base and interior workspace pointers so libc
// never frees any address owned by the process-lifetime allocation.
bool Th07PspOptionalRamReleaseArchiveWorkspace(const void *borrowedBuffer);

// Stage-scoped optional Main RAM owner for the PSP-2000+ profile.  Consumers
// receive borrowed storage only; this module is the sole allocator and owner.
// The PSP-1000 and non-PSP implementations are strict no-ops.
bool Th07PspOptionalRamPrepareStage();
bool Th07PspOptionalRamEnterGameplay(bool textCoverageComplete);
void Th07PspOptionalRamEndStage();
