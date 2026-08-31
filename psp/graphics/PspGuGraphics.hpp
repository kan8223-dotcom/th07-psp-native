#pragma once

#include "graphics/ZunGraphics.hpp"
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
#include "ge_portrait_telemetry.h"
#endif

struct Th07PspSpriteVertex
{
    float u;
    float v;
    unsigned int color;
    float x;
    float y;
    float z;
};

ZunGraphics *Th07CreatePspGuBackend();
void Th07PspDrawSpriteQuads(const Th07PspSpriteVertex *vertices, unsigned int spriteCount);
#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
void Th07PspDrawSpriteQuadsUnified(const Th07PspSpriteVertex *vertices,
                                   unsigned int spriteCount);
#endif
void Th07PspDrawSpritePairs(const Th07PspSpriteVertex *vertices, unsigned int spriteCount);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
struct Th07PspMeRenderStreamVertex;
// I-ME2 hands an already cache-coherent Main-RAM vertex stream directly to
// GE. Begin stores ownership before promoting READY -> GE_IN_FLIGHT. While a
// submission is open, an internal list-space restart may synchronize an
// earlier list but cannot release the slot: End closes the command span and
// the first subsequent completed-list fence releases it. The caller must
// validate every run before Begin; Draw therefore has no recoverable failure
// path after the first command is enqueued.
bool Th07PspBeginMeRenderGeSubmission(unsigned int slot,
                                      unsigned int generation);
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
void Th07PspDrawMeRenderStreamRun(
    const Th07PspMeRenderStreamVertex *vertices,
    unsigned int vertexCount, unsigned int primitive);
#else
void Th07PspDrawMeRenderStreamRun(const Th07PspSpriteVertex *vertices,
                                 unsigned int vertexCount,
                                 unsigned int primitive);
#endif
void Th07PspEndMeRenderGeSubmission();
// Process teardown must fence a GE-owned ME slot before the custom ME worker
// is drained. This is a no-op when no direct stream is outstanding.
void Th07PspFenceMeRenderBeforeMeShutdown();
#endif
void Th07PspForgetSurface(const void *pixels);
void Th07PspAllowNextWideStaticTexture();
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
enum Th07PspPortraitTextureRole
{
    TH07_PSP_PORTRAIT_NONE = 0,
    TH07_PSP_PORTRAIT_SELF = TH07_PSP_PORTRAIT_ROLE_SELF,
    TH07_PSP_PORTRAIT_BOMB = TH07_PSP_PORTRAIT_ROLE_BOMB,
    TH07_PSP_PORTRAIT_STAGE_0 = TH07_PSP_PORTRAIT_ROLE_STAGE_0,
    TH07_PSP_PORTRAIT_STAGE_1 = TH07_PSP_PORTRAIT_ROLE_STAGE_1,
    TH07_PSP_PORTRAIT_STAGE_2 = TH07_PSP_PORTRAIT_ROLE_STAGE_2,
    TH07_PSP_PORTRAIT_STAGE_3 = TH07_PSP_PORTRAIT_ROLE_STAGE_3,
};

// One-shot hint consumed by the immediately following immutable ANM upload.
void Th07PspPrepareUpperPortraitTexture(Th07PspPortraitTextureRole portraitRole,
                                        unsigned int textureSlot);
// Commit the exact current-stage FACE_STAGE child set after every atlas has
// been uploaded and read back. Until this call, required_mask remains zero so
// observers cannot mistake an in-progress stage load for a cache failure.
void Th07PspCompleteUpperPortraitPrewarm(unsigned int stagePortraitCount);
// Half-resolution stage-background pass (upper-pool spare). Begin at the
// start of the stage draw jobs, End after the last one; End upscales the
// low-res image onto the backbuffer. No-ops when the GE bridge is inactive.
void Th07PspBeginLowResStagePass();
void Th07PspEndLowResStagePass();
#endif
// Visually lossless: clip the stage/spell background pass to the playfield
// rectangle the GUI frame covers anyway (~44% of its fill is off-field).
void Th07PspBeginStagePlayfieldScissor();
void Th07PspEndStagePlayfieldScissor();
bool Th07PspGetTextureContentSize(GfxTextureHandle texture, unsigned int *width,
                                  unsigned int *height);
void Th07PspMarkTextTexture(GfxTextureHandle texture);
void Th07PspBeginTextUploadBatch();
void Th07PspEndTextUploadBatch();
void Th07PspCompactTextTexture(GfxTextureHandle texture);
unsigned int Th07PspTrimTextureCache();
bool Th07PspCaptureFramebufferToTexture(GfxTextureHandle texture, int srcLeft, int srcTop,
                                        int srcWidth, int srcHeight, int dstLeft, int dstTop,
                                        int dstWidth, int dstHeight);
#if defined(TH07_PSP_PERF_DIAG)
void Th07PspPerfAddCalcTime(unsigned long long elapsedUs);
void Th07PspPerfAddDrawTime(unsigned long long elapsedUs);
void Th07PspPerfAddStageTime(unsigned long long elapsedUs);
void Th07PspPerfAddCalcJobTime(int priority, unsigned long long elapsedUs);
void Th07PspPerfAddDrawJobTime(int priority, unsigned long callbackAddress,
                               unsigned long long elapsedUs);
void Th07PspPerfAddDrawChainOverheadTime(unsigned long long elapsedUs);
#if defined(TH07_PSP_PERF_PLAYER_SHOT)
// Diagnostic subset of Player draw priority 6.  This is observational only:
// callers must not add it to R/CPU or force a deferred-render flush.
void Th07PspPerfAddPlayerShotFrontendTime(unsigned long long elapsedUs,
                                          unsigned int activeShotCount);
#endif
void Th07PspPerfPhaseGpuSync(int priority);
void Th07PspPerfBeginGameplayWindow(int stage);
void Th07PspPerfFinalizeGameplayWindow();
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
// Latched only for the four Stage-6 dense-wave windows.  Hot callbacks read
// this once per frame; no timer or global counter is touched elsewhere.
extern int gTh07PspPerfDenseSliceActive;
void Th07PspPerfAddDensePostFlushTime(unsigned long long elapsedUs);
#if defined(TH07_PSP_ME_RENDER_WORKER)
void Th07PspPerfAddMerwPostCalcTime(unsigned long long elapsedUs);
#endif
#endif
#if defined(TH07_PSP_PERF_M2)
enum Th07PspPerfInternalCategory
{
    TH07_PSP_PERF_INTERNAL_PACK = 0,
    TH07_PSP_PERF_INTERNAL_MATRIX = 1,
    TH07_PSP_PERF_INTERNAL_STATE = 2,
    TH07_PSP_PERF_INTERNAL_DEFERRED_FLUSH = 3,
    TH07_PSP_PERF_INTERNAL_DCACHE = 4,
};
void Th07PspPerfInternalBegin(unsigned int category);
void Th07PspPerfInternalEnd(unsigned int category);
#endif
#if defined(TH07_PSP_PERF_M2) || defined(TH07_PSP_PERF_M3)
void Th07PspPerfSetDrawOwner(int priority, unsigned long callbackAddress);
#endif
#if defined(TH07_PSP_PERF_M3)
enum Th07PspPerfM3BatchOrigin
{
    TH07_PSP_PERF_M3_BATCH_NONE = 0,
    TH07_PSP_PERF_M3_BATCH_PRE = 1,
    TH07_PSP_PERF_M3_BATCH_BULLET = 2,
    TH07_PSP_PERF_M3_BATCH_MIXED = 3,
};
void Th07PspPerfSetM3BulletLoop(int active);
void Th07PspPerfSetM3BatchOrigin(int origin);
void Th07PspPerfM3LatchUnresolved();
#endif
