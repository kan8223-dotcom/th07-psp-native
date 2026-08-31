#pragma once

#include <cstddef>

// Process-lifetime backing for the optional Main-RAM font stream. TextHelper
// receives a borrowed pointer; this module remains the sole allocator/owner.
// Stage teardown deliberately does not release it.
void *Th07PspOptionalRamAcquireFontBuffer(std::size_t bytes);
void Th07PspOptionalRamReleaseFontBuffer(const void *borrowedBuffer);

// Stage-scoped optional Main RAM owner for the PSP-2000+ profile.  Consumers
// receive borrowed storage only; this module is the sole allocator and owner.
// The PSP-1000 and non-PSP implementations are strict no-ops.
bool Th07PspOptionalRamPrepareStage();
bool Th07PspOptionalRamEnterGameplay(bool textCoverageComplete);
void Th07PspOptionalRamEndStage();
