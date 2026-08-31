#include "graphics/PspGuGraphics.hpp"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>

#include "../usage_meter.h" /* [FABLE] SC/ME使用率メーター（未定義時は空マクロ） */
#include <pspgum.h>
#include <pspkernel.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "AnmManager.hpp"
#if defined(TH07_PSP_PERF_DETAIL) || defined(TH07_PSP_ME_RENDER_WORKER)
#include "BulletManager.hpp"
#endif
#if defined(TH07_PSP_PERF_DETAIL)
#include "EffectManager.hpp"
#if defined(TH07_PSP_PERF_DENSE_SLICE)
#include "EnemyManager.hpp"
#endif
#endif
#include "GameManager.hpp"
#include "Supervisor.hpp"
#include "audio_me.h"
#include "fileio.hpp"
#include "ge_portrait_telemetry.h"
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
#include "ge4_game_bridge.hpp"
#endif

namespace
{
constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 272;
constexpr int kLogicalWidth = 640;
constexpr int kLogicalHeight = 480;
constexpr int kFitWidth = 362;
constexpr int kFitLeft = (kScreenWidth - kFitWidth) / 2;
constexpr int kBufferWidth = 512;
// The LCD accepts RGB565 natively.  Two 16-bit framebuffers halve full-screen
// fill/blend traffic for every stage and leave more EDRAM headroom for the
// persistent full-screen surface cache.
constexpr int kFramePsm = GU_PSM_5650;
constexpr unsigned int kFrameBytesPerPixel = 2;
constexpr unsigned int kFrameBytes = kBufferWidth * kScreenHeight * kFrameBytesPerPixel;
constexpr unsigned int kDepthOffset = kFrameBytes * 2;
constexpr unsigned int kDepthBytes = kBufferWidth * kScreenHeight * 2;
constexpr unsigned int kEdramBytes = kDepthOffset + kDepthBytes;
constexpr unsigned int kLowerEdramBytes = 2u * 1024u * 1024u;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
// Portraits use only the added aperture. CPU conversion and verification use
// the otherwise-unused final 512 KiB of the lower aperture; the CPU never
// writes an upper-eDRAM byte directly.
constexpr unsigned int kPortraitStagingOffset = 0x00180000u;
constexpr unsigned int kPortraitStagingBytes = 512u * 1024u;
constexpr unsigned int kUpperPortraitPoolOffset = 2u * 1024u * 1024u;
constexpr unsigned int kUpperPortraitPoolBytes = 2u * 1024u * 1024u;
constexpr unsigned int kUpperPortraitRawBase = 0x04200000u;
constexpr unsigned int kUpperPortraitPageBytes = 4u * 1024u;
constexpr unsigned int kUpperPortraitPoolPages =
    kUpperPortraitPoolBytes / kUpperPortraitPageBytes;
constexpr unsigned int kPlayerPortraitAtlasBytes = 512u * 512u * 2u;
constexpr unsigned int kStagePortraitAtlasBytes = 256u * 256u * 2u;
constexpr unsigned int kMaxPlayerPortraitAtlases = 2u;
constexpr unsigned int kMaxStagePortraitAtlases = 4u;
constexpr unsigned int kPortraitBudgetBytes =
    kMaxPlayerPortraitAtlases * kPlayerPortraitAtlasBytes +
    kMaxStagePortraitAtlases * kStagePortraitAtlasBytes;
static_assert(kPortraitBudgetBytes == 1536u * 1024u,
              "FACE + FACE_STAGE portrait budget changed");
static_assert(kPortraitBudgetBytes <= kUpperPortraitPoolBytes,
              "portrait cache exceeds the upper GE aperture");
static_assert(kPortraitStagingOffset + kPortraitStagingBytes == kLowerEdramBytes,
              "portrait staging must end at the lower-aperture boundary");
// Half-resolution stage-background pass.  Hardware GPU attribution (stage-4
// Lily section, PERF2 log) showed the stage/spell background alone consumes
// 9-11 ms of GE time while bullets/effects/GUI stay near zero.  Rendering the
// whole stage pass at half resolution into the upper-pool spare and upscaling
// once cuts that fill by ~4x for every stage and spell background.
constexpr unsigned int kLowResBufferWidth = 256;
constexpr int kLowResWidth = kScreenWidth / 2;
constexpr int kLowResHeight = kScreenHeight / 2;
constexpr unsigned int kLowResColorOffset =
    kUpperPortraitPoolOffset + kPortraitBudgetBytes;
constexpr unsigned int kLowResTargetBytes =
    kLowResBufferWidth * static_cast<unsigned int>(kLowResHeight) * 2u;
constexpr unsigned int kLowResDepthOffset =
    (kLowResColorOffset + kLowResTargetBytes + 63u) & ~63u;
static_assert(kLowResDepthOffset + kLowResTargetBytes <=
                  kUpperPortraitPoolOffset + kUpperPortraitPoolBytes,
              "low-res stage targets exceed the upper-pool spare");
#endif
constexpr unsigned int kSurfaceCacheMaxBytes = 512u * 1024u;
static_assert(kEdramBytes + kSurfaceCacheMaxBytes <= kLowerEdramBytes,
              "surface cache crosses the unchanged lower 2 MiB aperture");
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
static_assert(kEdramBytes + kSurfaceCacheMaxBytes <= kPortraitStagingOffset,
              "surface cache overlaps the portrait staging window");
#endif
// PSP-1000 traces across every TH07 stage peak below 64 KiB.  Keep four times
// that measured high-water mark while avoiding a permanently resident 1 MiB
// list on the 32 MiB model.
#if defined(TH07_PSP_1000)
constexpr unsigned int kListBytes = 256 * 1024;
#else
constexpr unsigned int kListBytes = 1024 * 1024;
#endif
constexpr unsigned int kListReserve = 16 * 1024;
constexpr unsigned int kMaxTextures = 256;
constexpr unsigned int kCachedQuadIndexCount = 2048;
constexpr unsigned int MatrixDirtyBit(TransformMatrix type)
{
    return 1u << static_cast<unsigned int>(type);
}
constexpr unsigned int kAll3dMatrixDirtyBits = MatrixDirtyBit(MATRIX_MODEL) |
                                               MatrixDirtyBit(MATRIX_VIEW) |
                                               MatrixDirtyBit(MATRIX_PROJECTION);
// As in TH06, reserve the high end of AnmManager's per-frame sprite buffer for
// GE-ready vertices.  Consecutive engine flushes with identical render state
// can then become one primitive command without allocating another large PSP
// heap block.
constexpr unsigned int kDeferredSpriteArenaBytes = 384 * 1024;
// Keep the first real-hardware text-quality build on the texture allocation
// path which already completed all six stages.  Static-atlas RGBA8888 and
// whole-texture swizzling are reintroduced separately after boot stability is
// measured; combining them hid which optimization caused a kernel shutdown
// while title.anm was loading.
constexpr bool kFastSurfaceTextures = true;
constexpr bool kFastStaticTextureFormats = false;
constexpr bool kSwizzleSurfaceTextures = true;
constexpr bool kSwizzleStaticTextures = true;
#if defined(TH07_PSP_PERF_DIAG)
constexpr unsigned int kPerfWindowFrames = 120;
constexpr unsigned int kFrameBudgetUs = 16667;
constexpr unsigned int kPerfHistogramLimitsUs[] = {
    8000, 10000, 12000, 14000, 15700, 16667, 20000, 25000, 33334,
};
constexpr unsigned int kPerfHistogramBuckets =
    sizeof(kPerfHistogramLimitsUs) / sizeof(kPerfHistogramLimitsUs[0]) + 1u;
#endif

const ScePspFMatrix4 kIdentityMatrix = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};
#if defined(TH07_PSP_ME_RENDER_XYZ16)
// Signed-16 XYZ is normalized by GE.  X/Y were packed with five fractional
// bits, so 1024 restores logical pixels; Z was packed at 32768 and therefore
// keeps scale 1 to preserve the engine's fine depth separation.
const ScePspFMatrix4 kMeRenderXyz16ModelMatrix = {
    {1024.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1024.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};
#endif

alignas(16) unsigned int gCommandList[kListBytes / sizeof(unsigned int)];
alignas(64) u16 gQuadIndices[kCachedQuadIndexCount * 6];
#if defined(TH07_PSP_PERF_DIAG)
unsigned long long gPerfCalcChainUs = 0;
unsigned long long gPerfDrawChainUs = 0;
unsigned long long gPerfStageDrawUs = 0;
unsigned long long gPerfCalcJobUs[18] = {};
unsigned long long gPerfDrawJobUs[18] = {};
unsigned long long gPerfDrawChainOverheadUs = 0;
unsigned int gPerfDrawOutOfRange = 0;
#if defined(TH07_PSP_PERF_PLAYER_SHOT)
// Raw window totals.  They are a strict subset of the already-accounted
// Player draw callback (P06), never another contribution to R or CPU.
unsigned long long gPerfPlayerShotFrontendUs = 0;
unsigned long long gPerfPlayerShotActiveCount = 0;
unsigned int gPerfPlayerShotFrontendCalls = 0;
#endif
#if defined(TH07_PSP_PERF_M2)
constexpr unsigned int kPerfDrawOwnerSlots = 32u;
constexpr unsigned int kPerfInternalCategoryCount = 5u;
struct PerfDrawOwnerSlot
{
    unsigned long callbackAddress = 0;
    int priority = -1;
    unsigned long long elapsedUs = 0;
    unsigned long long internalUs[kPerfInternalCategoryCount] = {};
    unsigned int calls = 0;
    bool announced = false;
};
PerfDrawOwnerSlot gPerfDrawOwners[kPerfDrawOwnerSlots]{};
unsigned int gPerfDrawOwnerCount = 0;
unsigned int gPerfDrawOwnerOverflow = 0;
unsigned int gPerfInternalMismatch = 0;
#if defined(TH07_PSP_PERF_ATTRIB)
unsigned int gPerfDrawOwnerLogTruncated = 0;
#endif

struct PerfInternalTracker
{
    int ownerIndex = -1;
    int category = -1;
    int stack[8] = {};
    unsigned int depth = 0;
    unsigned long long lastUs = 0;
};

PerfInternalTracker gPerfInternalTracker{};

int FindOrCreatePerfDrawOwner(int priority, unsigned long callbackAddress)
{
    for (unsigned int ownerIndex = 0; ownerIndex < gPerfDrawOwnerCount; ++ownerIndex)
    {
        const PerfDrawOwnerSlot &owner = gPerfDrawOwners[ownerIndex];
        if (owner.priority == priority && owner.callbackAddress == callbackAddress)
        {
            return static_cast<int>(ownerIndex);
        }
    }
    if (gPerfDrawOwnerCount >= kPerfDrawOwnerSlots)
    {
        ++gPerfDrawOwnerOverflow;
        return -1;
    }
    PerfDrawOwnerSlot &owner = gPerfDrawOwners[gPerfDrawOwnerCount];
    owner.callbackAddress = callbackAddress;
    owner.priority = priority;
    return static_cast<int>(gPerfDrawOwnerCount++);
}

void PerfInternalAccumulate(unsigned long long nowUs)
{
    if (gPerfInternalTracker.ownerIndex >= 0 && gPerfInternalTracker.category >= 0 &&
        gPerfInternalTracker.category < static_cast<int>(kPerfInternalCategoryCount))
    {
        gPerfDrawOwners[gPerfInternalTracker.ownerIndex]
            .internalUs[gPerfInternalTracker.category] += nowUs - gPerfInternalTracker.lastUs;
    }
    gPerfInternalTracker.lastUs = nowUs;
}

class PerfInternalScope
{
  public:
    explicit PerfInternalScope(unsigned int category) : mCategory(category)
    {
        Th07PspPerfInternalBegin(mCategory);
    }
    ~PerfInternalScope()
    {
        End();
    }
    void End()
    {
        if (mActive)
        {
            Th07PspPerfInternalEnd(mCategory);
            mActive = false;
        }
    }

  private:
    unsigned int mCategory;
    bool mActive = true;
};
#endif
// GPU attribution: list-flush + sync time right after each draw job, so the
// fill-heavy phase shows up by priority.  Serializes the GE pipeline, so this
// mode's absolute FPS reads lower than the plain diagnostic build.
unsigned long long gPerfDrawJobGpuUs[18] = {};
#endif
#if defined(TH07_PSP_PERF_M3)
int gPerfCurrentDrawOwner = -1;
bool gPerfM3BulletLoopActive = false;
bool gPerfM3CarryInPending = false;
unsigned long long gPerfM3SpriteBackendUs = 0;
unsigned long long gPerfM3DcacheUs = 0;
unsigned long long gPerfM3CarryInFlushUs = 0;
unsigned long long gPerfM3CarryInDcacheUs = 0;
unsigned long long gPerfM3CarryOutFlushUs = 0;
unsigned long long gPerfM3CarryOutDcacheUs = 0;
unsigned int gPerfM3SpriteBackendCalls = 0;
unsigned int gPerfM3DcacheCalls = 0;
unsigned long long gPerfM3PendingBytes = 0;
unsigned long long gPerfM3CarryInBytes = 0;
unsigned long long gPerfM3CarryOutBytes = 0;
unsigned int gPerfM3TransferMixed = 0;
unsigned int gPerfM3TransferUnresolved = 0;
int gPerfM3IncomingBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
int gPerfM3DeferredBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
int gPerfM3WrapperOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
bool gPerfM3WrapperActive = false;
unsigned long long gPerfM3WrapperDcacheUs = 0;
// sceKernelGetSystemTimeWide() is not free on real hardware. M3 samples one
// emitter in 32, so blindly extrapolating the timestamp calls themselves
// charges their cost to all 32 emitters. Keep the per-window read cost in Q8
// microseconds and subtract only the probes that the sampled path executed.
unsigned long long gPerfM3TimerReadQ8 = 0;

unsigned long long PerfM3CalibrateTimerReadQ8()
{
    constexpr unsigned int kSamples = 64u;
    unsigned long long deltas[kSamples];
    unsigned long long previousUs = sceKernelGetSystemTimeWide();
    for (unsigned int index = 0; index < kSamples; ++index)
    {
        const unsigned long long currentUs = sceKernelGetSystemTimeWide();
        deltas[index] = currentUs - previousUs;
        previousUs = currentUs;
    }
    std::sort(deltas, deltas + kSamples);
    // Drop both tails so interrupts and microsecond quantisation cannot turn
    // one unusually long/short pair into a 32x extrapolation error.
    unsigned long long trimmedUs = 0;
    for (unsigned int index = 8u; index < 56u; ++index)
    {
        trimmedUs += deltas[index];
    }
    return (trimmedUs * 256ull + 24ull) / 48ull;
}

void PerfM3NoteDeferredAppend(int origin)
{
    if (origin == TH07_PSP_PERF_M3_BATCH_NONE)
    {
        origin = TH07_PSP_PERF_M3_BATCH_PRE;
    }
    if (gPerfM3DeferredBatchOrigin == TH07_PSP_PERF_M3_BATCH_NONE)
    {
        gPerfM3DeferredBatchOrigin = origin;
    }
    else if (gPerfM3DeferredBatchOrigin != origin)
    {
        // The delayed GU_SPRITES command now spans more than one source
        // owner.  A later whole-range writeback cannot be split by timing
        // without changing the workload, so this window is not provable.
        gPerfM3DeferredBatchOrigin = TH07_PSP_PERF_M3_BATCH_MIXED;
    }
}

void PerfM3RecordDeferredFlush(int origin, unsigned long long flushUs,
                               unsigned long long dcacheUs)
{
    if (origin == TH07_PSP_PERF_M3_BATCH_MIXED ||
        origin == TH07_PSP_PERF_M3_BATCH_NONE)
    {
        ++gPerfM3TransferMixed;
        return;
    }

    // A wrapper that is itself an exact PRE carry-in or BULLET carry-out
    // already owns all nested work of the same source.  Suppress the nested
    // entry to avoid double counting; a different source makes that wrapper
    // impure and is rejected by its end gate.
    if (gPerfM3WrapperActive)
    {
        const bool wrapperIsCarryIn =
            gPerfM3BulletLoopActive &&
            gPerfM3WrapperOrigin == TH07_PSP_PERF_M3_BATCH_PRE;
        const bool wrapperIsCarryOut =
            !gPerfM3BulletLoopActive &&
            gPerfM3WrapperOrigin == TH07_PSP_PERF_M3_BATCH_BULLET;
        if (wrapperIsCarryIn || wrapperIsCarryOut)
        {
            if (origin != gPerfM3WrapperOrigin)
            {
                ++gPerfM3TransferMixed;
            }
            return;
        }
    }

    if (gPerfM3BulletLoopActive &&
        origin == TH07_PSP_PERF_M3_BATCH_PRE)
    {
        gPerfM3CarryInFlushUs += flushUs;
        gPerfM3CarryInDcacheUs += dcacheUs;
    }
    else if (!gPerfM3BulletLoopActive &&
             origin == TH07_PSP_PERF_M3_BATCH_BULLET)
    {
        gPerfM3CarryOutFlushUs += flushUs;
        gPerfM3CarryOutDcacheUs += dcacheUs;
    }
}

void PerfM3ClearWindowTransferCounters()
{
    gPerfM3SpriteBackendUs = 0;
    gPerfM3DcacheUs = 0;
    gPerfM3CarryInFlushUs = 0;
    gPerfM3CarryInDcacheUs = 0;
    gPerfM3CarryOutFlushUs = 0;
    gPerfM3CarryOutDcacheUs = 0;
    gPerfM3SpriteBackendCalls = 0;
    gPerfM3DcacheCalls = 0;
    gPerfM3PendingBytes = 0;
    gPerfM3CarryInBytes = 0;
    gPerfM3CarryOutBytes = 0;
    gPerfM3TransferMixed = 0;
    gPerfM3TransferUnresolved = 0;
}
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
#define TH07_PSP_DENSE_WARM_VALID(dense)                                      \
    &&((dense).warmQueueReadyFrames == (dense).drawFrames &&                  \
       (dense).warmQueueFallbackFrames == 0u)
#define TH07_PSP_DENSE_WARM_FORMAT " WQR%u WQF%u"
#define TH07_PSP_DENSE_WARM_ARGS(dense)                                       \
    , (dense).warmQueueReadyFrames, (dense).warmQueueFallbackFrames
#define TH07_PSP_DENSE_WARM_ZERO(dense)                                       \
    &&((dense).warmQueueReadyFrames == 0u &&                                  \
       (dense).warmQueueFallbackFrames == 0u)
#else
#define TH07_PSP_DENSE_WARM_VALID(dense)
#define TH07_PSP_DENSE_WARM_FORMAT
#define TH07_PSP_DENSE_WARM_ARGS(dense)
#define TH07_PSP_DENSE_WARM_ZERO(dense)
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
#define TH07_PSP_DENSE_STATIC_PROXY_VALID(dense)                              \
    &&((dense).staticProxyReadyFrames == (dense).drawFrames &&                \
       (dense).staticProxyFallbackFrames == 0u &&                             \
       (dense).staticProxyVisitHits == 0ull &&                                \
       (dense).staticProxyCanonicalFallbacks == 0ull)
#else
#define TH07_PSP_DENSE_STATIC_PROXY_VALID(dense)                              \
    &&((dense).staticProxyReadyFrames == (dense).drawFrames &&                \
       (dense).staticProxyFallbackFrames == 0u &&                             \
       (dense).staticProxyVisitHits > 0ull &&                                 \
       (dense).staticProxyVisitHits <= (dense).onePassAccepts &&              \
       (dense).staticProxyCanonicalFallbacks ==                               \
           (dense).canonicalDrawCalls)
#endif
#define TH07_PSP_DENSE_STATIC_PROXY_FORMAT                                   \
    " SPR%u SPF%u SPVIS%llu SPCAN%llu"
#define TH07_PSP_DENSE_STATIC_PROXY_ARGS(dense)                              \
    , (dense).staticProxyReadyFrames, (dense).staticProxyFallbackFrames,      \
        (dense).staticProxyVisitHits,                                         \
        (dense).staticProxyCanonicalFallbacks
#define TH07_PSP_DENSE_STATIC_PROXY_ZERO(dense)                              \
    &&((dense).staticProxyReadyFrames == 0u &&                                \
       (dense).staticProxyFallbackFrames == 0u &&                             \
       (dense).staticProxyVisitHits == 0ull &&                                \
       (dense).staticProxyCanonicalFallbacks == 0ull)
#else
#define TH07_PSP_DENSE_STATIC_PROXY_VALID(dense)
#define TH07_PSP_DENSE_STATIC_PROXY_FORMAT
#define TH07_PSP_DENSE_STATIC_PROXY_ARGS(dense)
#define TH07_PSP_DENSE_STATIC_PROXY_ZERO(dense)
#endif
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
#define TH07_PSP_DENSE_ENEMY_P5_VALID(enemyP5, dense)                         \
    &&((enemyP5).readyFrames == (dense).drawFrames &&                         \
       (enemyP5).fallbackFrames == 0u &&                                      \
       (enemyP5).recordVisits > 0ull &&                                       \
       (enemyP5).fastEnemyDraws > 0ull &&                                     \
       (enemyP5).recordVisits ==                                              \
           (enemyP5).fastEnemyDraws + (enemyP5).canonicalEnemyDraws)
#define TH07_PSP_DENSE_ENEMY_P5_FORMAT " EQR%u EQF%u EVIS%llu EFAST%llu ECAN%llu"
#define TH07_PSP_DENSE_ENEMY_P5_ARGS(enemyP5)                                 \
    , (enemyP5).readyFrames, (enemyP5).fallbackFrames,                        \
        (enemyP5).recordVisits, (enemyP5).fastEnemyDraws,                     \
        (enemyP5).canonicalEnemyDraws
#define TH07_PSP_DENSE_ENEMY_P5_ZERO(enemyP5)                                 \
    &&((enemyP5).readyFrames == 0u && (enemyP5).fallbackFrames == 0u &&       \
       (enemyP5).recordVisits == 0ull && (enemyP5).fastEnemyDraws == 0ull &&  \
       (enemyP5).canonicalEnemyDraws == 0ull)
#else
#define TH07_PSP_DENSE_ENEMY_P5_VALID(enemyP5, dense)
#define TH07_PSP_DENSE_ENEMY_P5_FORMAT
#define TH07_PSP_DENSE_ENEMY_P5_ARGS(enemyP5)
#define TH07_PSP_DENSE_ENEMY_P5_ZERO(enemyP5)
#endif
unsigned long long gPerfDensePostFlushUs = 0;
unsigned long long gPerfDenseSwapSubmitUs = 0;
unsigned long long gPerfDenseTimerReadQ8 = 0;
unsigned int gPerfDenseCalcFrames = 0;
unsigned int gPerfDenseDrawFrames = 0;
unsigned int gPerfDensePostFlushFrames = 0;
unsigned int gPerfDenseSwapFrames = 0;
#if defined(TH07_PSP_ME_RENDER_WORKER)
unsigned long long gPerfMerwPostCalcUs = 0;
unsigned int gPerfMerwPostCalcFrames = 0;
#endif

unsigned long long PerfDenseCalibrateTimerReadQ8()
{
    constexpr unsigned int kSamples = 64u;
    unsigned long long deltas[kSamples];
    unsigned long long previousUs = sceKernelGetSystemTimeWide();
    for (unsigned int index = 0; index < kSamples; ++index)
    {
        const unsigned long long currentUs = sceKernelGetSystemTimeWide();
        deltas[index] = currentUs - previousUs;
        previousUs = currentUs;
    }
    std::sort(deltas, deltas + kSamples);
    unsigned long long trimmedUs = 0;
    for (unsigned int index = 8u; index < 56u; ++index)
    {
        trimmedUs += deltas[index];
    }
    return (trimmedUs * 256ull + 24ull) / 48ull;
}
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
volatile unsigned int gPortraitSnapshotSequence = 0;
Th07PspPortraitCacheSnapshot gPortraitSnapshot{};

bool IsPlayerPortraitRole(Th07PspPortraitTextureRole role)
{
    return role == TH07_PSP_PORTRAIT_SELF || role == TH07_PSP_PORTRAIT_BOMB;
}

bool IsStagePortraitRole(Th07PspPortraitTextureRole role)
{
    return role >= TH07_PSP_PORTRAIT_STAGE_0 &&
           role <= TH07_PSP_PORTRAIT_STAGE_3;
}

unsigned int PortraitSlotIndex(Th07PspPortraitTextureRole role)
{
    return static_cast<unsigned int>(role) - 1u;
}

unsigned int PortraitTextureSlot(Th07PspPortraitTextureRole role)
{
    if (role == TH07_PSP_PORTRAIT_SELF)
        return 25u;
    if (role == TH07_PSP_PORTRAIT_BOMB)
        return 26u;
    return 28u + static_cast<unsigned int>(role - TH07_PSP_PORTRAIT_STAGE_0);
}

bool PortraitRoleMatchesSlot(Th07PspPortraitTextureRole role, unsigned int textureSlot)
{
    return role != TH07_PSP_PORTRAIT_NONE &&
           static_cast<unsigned int>(role) <= TH07_PSP_PORTRAIT_SLOT_COUNT &&
           PortraitTextureSlot(role) == textureSlot;
}

void PublishPortraitSnapshotSeqlock(const Th07PspPortraitCacheSnapshot &snapshot)
{
    __atomic_fetch_add(&gPortraitSnapshotSequence, 1u, __ATOMIC_ACQ_REL);
    std::memcpy(&gPortraitSnapshot, &snapshot, sizeof(snapshot));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_fetch_add(&gPortraitSnapshotSequence, 1u, __ATOMIC_RELEASE);
}
#endif

struct GuTexture
{
    void *pixels = nullptr;
    // Dynamic text atlases alternate backing addresses.  PPSSPP (and the
    // earlier TH07 PS3 backend) can otherwise keep sampling the first cached
    // image even after a sub-image upload and cache flush.
    void *updatePixels = nullptr;
    int psm = GU_PSM_8888;
    unsigned int logicalWidth = 0;
    unsigned int logicalHeight = 0;
    unsigned int storageWidth = 0;
    unsigned int storageHeight = 0;
    unsigned int contentWidth = 0;
    unsigned int contentHeight = 0;
    unsigned int bytes = 0;
    unsigned int allocationBytes = 0;
    unsigned int updateAllocationBytes = 0;
    float sampleScaleX = 1.0f;
    float sampleScaleY = 1.0f;
    bool used = false;
    bool swizzled = false;
    bool textAtlas = false;
    bool surfaceCandidate = false;
    bool borrowedSurfaceCache = false;
    bool textBatchPending = false;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    Th07PspPortraitTextureRole portraitRole = TH07_PSP_PORTRAIT_NONE;
    unsigned short portraitTextureSlot = 0;
    bool upperPortraitOwned = false;
    bool upperPortraitVerified = false;
    unsigned short upperPortraitFirstPage = 0;
    unsigned short upperPortraitPageCount = 0;
    unsigned int upperPortraitUploadGeneration = 0;
#endif
};

struct SurfaceCache
{
    void *pixels = nullptr;
    const void *source = nullptr;
    unsigned int bytes = 0;
    unsigned int allocationBytes = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    bool valid = false;
    bool edram = false;
};

struct FreeTextureBlock
{
    void *pixels = nullptr;
    unsigned int bytes = 0;
};

struct GuVertexTexColor
{
    float u;
    float v;
    unsigned int color;
    float x;
    float y;
    float z;
};

struct GuVertexTex
{
    float u;
    float v;
    float x;
    float y;
    float z;
};

struct GuVertexColor
{
    unsigned int color;
    float x;
    float y;
    float z;
};

struct GuVertexPosition
{
    float x;
    float y;
    float z;
};

static_assert(sizeof(GuVertexTexColor) == 24, "unexpected PSP textured-color vertex size");
static_assert(sizeof(Th07PspSpriteVertex) == sizeof(GuVertexTexColor),
              "TH07 sprite/deferred vertex layouts must remain the same size");
static_assert(sizeof(GuVertexTex) == 20, "unexpected PSP textured vertex size");
static_assert(sizeof(GuVertexColor) == 16, "unexpected PSP color vertex size");
static_assert(sizeof(GuVertexPosition) == 12, "unexpected PSP position vertex size");

unsigned int NextPowerOfTwo(unsigned int value)
{
    unsigned int result = 1;
    while (result < value)
    {
        result <<= 1;
    }
    return result;
}

// PSPSDK's GU samples store textures as consecutive 16-byte by 8-row blocks.
// SetTextureImage uses this fast path for complete RGBA8 textures just as the
// final TH06 backend does.
void SwizzleCopy(unsigned char *destination, const unsigned char *source,
                 unsigned int widthBytes, unsigned int height)
{
    const unsigned int widthBlocks = widthBytes / 16u;
    const unsigned int heightBlocks = height / 8u;
    for (unsigned int blockY = 0; blockY < heightBlocks; ++blockY)
    {
        for (unsigned int blockX = 0; blockX < widthBlocks; ++blockX)
        {
            unsigned char *block =
                destination + (blockY * widthBlocks + blockX) * 128u;
            const unsigned char *row =
                source + blockY * 8u * widthBytes + blockX * 16u;
            for (unsigned int y = 0; y < 8u; ++y)
            {
                std::memcpy(block + y * 16u, row + y * widthBytes, 16u);
            }
        }
    }
}

struct SurfaceAxisSample
{
    unsigned short first;
    unsigned short second;
    unsigned short fraction;
};

// Match the proven TH06 PSP surface path: filter an immutable 640-wide image
// once to the selected LCD output size instead of asking GE to minify the
// original 480 logical rows every frame.
void BuildSurfaceAxisSamples(SurfaceAxisSample *samples, unsigned int destinationSize,
                             unsigned int sourceSize)
{
    if (!samples || destinationSize == 0 || sourceSize == 0)
    {
        return;
    }
    for (unsigned int i = 0; i < destinationSize; ++i)
    {
        long long fixed = ((static_cast<long long>(i) * 2 + 1) * sourceSize * 128) /
                              destinationSize -
                          128;
        const long long last = static_cast<long long>(sourceSize - 1u) * 256;
        fixed = std::max(0ll, std::min(last, fixed));
        samples[i].first = static_cast<unsigned short>(fixed >> 8);
        samples[i].second = static_cast<unsigned short>(
            std::min(sourceSize - 1u, static_cast<unsigned int>(samples[i].first) + 1u));
        samples[i].fraction = static_cast<unsigned short>(fixed & 255);
    }
}

unsigned int ToGuColor(ZunColor color)
{
    return (color.color & 0xff00ff00u) | ((color.color & 0x00ff0000u) >> 16) |
           ((color.color & 0x000000ffu) << 16);
}

ScePspFMatrix4 ToGuMatrix(const ZunMatrix &matrix)
{
    ScePspFMatrix4 result;
    float *dst = &result.x.x;
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            dst[column * 4 + row] = matrix.m[column][row];
        }
    }
    return result;
}

unsigned int ToHardwareDepth(float value)
{
    value = std::max(0.0f, std::min(1.0f, value));
    return static_cast<unsigned int>((1.0f - value) * 65535.0f);
}

class PspGuGraphics;
PspGuGraphics *gPspGuBackend = nullptr;

class PspGuGraphics final : public ZunGraphics
{
  public:
    PspGuGraphics()
    {
        for (ZunMatrix &matrix : mTransforms)
        {
            matrix.Identity();
        }
        mViewport = {0, 0, kLogicalWidth, kLogicalHeight, 0.0f, 1.0f};
    }

    ~PspGuGraphics() override
    {
        Exit();
    }

    bool Init()
    {
        if (kEdramBytes > sceGeEdramGetSize() || sceGuInit() < 0)
        {
            return false;
        }

        // A quad's topology never changes.  Keep one cache-coherent index table
        // instead of allocating and filling thousands of identical u16 values
        // in the display-list arena every frame.
        for (unsigned int sprite = 0; sprite < kCachedQuadIndexCount; ++sprite)
        {
            const u16 base = static_cast<u16>(sprite * 4);
            gQuadIndices[sprite * 6 + 0] = base;
            gQuadIndices[sprite * 6 + 1] = base + 1;
            gQuadIndices[sprite * 6 + 2] = base + 2;
            gQuadIndices[sprite * 6 + 3] = base + 1;
            gQuadIndices[sprite * 6 + 4] = base + 2;
            gQuadIndices[sprite * 6 + 5] = base + 3;
        }
        sceKernelDcacheWritebackRange(gQuadIndices, sizeof(gQuadIndices));

        sceGuStart(GU_DIRECT, gCommandList);
        mListOpen = true;
        sceGuDrawBuffer(kFramePsm, reinterpret_cast<void *>(0), kBufferWidth);
        sceGuDispBuffer(kScreenWidth, kScreenHeight, reinterpret_cast<void *>(kFrameBytes),
                        kBufferWidth);
        sceGuDepthBuffer(reinterpret_cast<void *>(kDepthOffset), kBufferWidth);
        ApplyViewport();
        sceGuDepthRange(65535, 0);
        sceGuClearDepth(0);
        sceGuShadeModel(GU_SMOOTH);
        sceGuFrontFace(GU_CW);
        sceGuDisable(GU_CULL_FACE);
        sceGuDisable(GU_LIGHTING);
        sceGuDisable(GU_DITHER);
        sceGuDisable(GU_STENCIL_TEST);
        sceGuEnable(GU_CLIP_PLANES);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GEQUAL, 4, 0xff);
        sceGuDisable(GU_DEPTH_TEST);
        sceGuDepthMask(GU_FALSE);
        sceGuDepthFunc(GU_GEQUAL);
        sceGuTexWrap(GU_REPEAT, GU_REPEAT);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
        sceGuTexProjMapMode(GU_UV);
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_FOG);
        sceGuClearColor(0xff000000u);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        // TH07 alternates 2D and 3D matrices several times inside one draw
        // chain.  Keep the backend independent from libGUM's global matrix
        // stack and submit matrices directly to GU.
        for (int mode = GU_PROJECTION; mode <= GU_TEXTURE; ++mode)
        {
            sceGuSetMatrix(mode, &kIdentityMatrix);
        }

#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        const int initialFinishResult = sceGuFinish();
#else
        sceGuFinish();
#endif
        mListOpen = false;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        const int initialSyncResult = sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        InitializeUpperPortraitTelemetry();
        if (initialFinishResult < 0 || initialSyncResult < 0 ||
            sceGeDrawSync(1) != PSP_GE_LIST_DONE)
        {
            UpperPortraitInvariantFailure("initial-gu-idle-unknown");
        }
        // The bridge deliberately widens the aperture only after the first GU
        // list has completed at the firmware-default 2 MiB size.
        const bool ge4Enabled = th07_psp_ge4_enable_after_gu_idle() != 0;
#else
        sceGuSync(0, 0);
#endif

        const unsigned int edram = reinterpret_cast<unsigned int>(sceGeEdramGetAddr());
        auto *display = reinterpret_cast<volatile u16 *>((0x40000000u | edram) + kFrameBytes);
        for (int i = 0; i < kBufferWidth * kScreenHeight; ++i)
        {
            display[i] = 0;
        }

        sceDisplayWaitVblankStart();
        sceGuDisplay(GU_TRUE);
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        if (ge4Enabled)
        {
            InitializeUpperPortraitPool();
        }
#endif
#if defined(TH07_PSP_PERF_DIAG)
        mFrameStartUs = sceKernelGetSystemTimeWide();
#endif
        StartList();
        th07_psp_boot_note(g_Supervisor.cfg.windowed ? "display 4:3 fit 362x272 rgb565"
                                                     : "display full stretch 480x272 rgb565");
        g_Supervisor.cfg.noVertexBuffers = true;
        mInitialized = true;
        return true;
    }

    void Exit() override
    {
        if (!mInitialized && !mListOpen)
        {
            return;
        }
        if (mListOpen)
        {
            FlushDeferredSpriteDraw();
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
            const int finishResult = sceGuFinish();
            const int syncResult = sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
            if (finishResult < 0 || syncResult < 0 ||
                sceGeDrawSync(1) != PSP_GE_LIST_DONE)
            {
                UpperPortraitInvariantFailure("gu-term-idle-unknown");
            }
#else
            sceGuFinish();
            sceGuSync(0, 0);
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
            ReleaseMeRenderGeTokenAfterSync();
#endif
            mListOpen = false;
        }
        for (GuTexture &texture : mTextures)
        {
            if (!texture.borrowedSurfaceCache)
            {
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
                if (texture.upperPortraitOwned)
                {
                    ReleaseUpperPortraitAllocation(texture);
                }
                else
#endif
                std::free(texture.pixels);
            }
            std::free(texture.updatePixels);
            texture = GuTexture{};
        }
        for (FreeTextureBlock &block : mFreeTextureBlocks)
        {
            std::free(block.pixels);
            block = FreeTextureBlock{};
        }
        if (!mSurfaceCache.edram)
        {
            std::free(mSurfaceCache.pixels);
        }
        mSurfaceCache = SurfaceCache{};
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        bool upperPagesClear = true;
        for (unsigned int word : mUpperPortraitPageBits)
        {
            if (word != 0u)
            {
                upperPagesClear = false;
                break;
            }
        }
        if (mUpperPortraitUsedPages != 0 || mUpperPortraitActivePlayers != 0 ||
            mUpperPortraitActiveStages != 0 || !upperPagesClear)
        {
            // Never shrink the aperture while any renderer metadata still
            // describes a live upper allocation.
            UpperPortraitInvariantFailure("portrait-pool-not-empty-at-gu-term");
        }
        if (mUpperPortraitPoolPages != 0)
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                          "GE portrait stats peak%uK fallback%u migrate%u allocfail%u player%u stage%u",
                          mUpperPortraitPeakPages * kUpperPortraitPageBytes / 1024u,
                          mUpperPortraitFallbacks, mUpperPortraitMigrations,
                          mUpperPortraitAllocationFailures,
                          mUpperPortraitPlayerAllocations,
                          mUpperPortraitStageAllocations);
            th07_psp_boot_note(message);
        }
        mUpperPortraitPoolBase = 0;
        mUpperPortraitPoolPages = 0;
        mPortraitTelemetry.flags &= ~TH07_PSP_PORTRAIT_CACHE_POOL_INITIALIZED;
        mPortraitTelemetry.pool_raw_base = 0;
        mPortraitTelemetry.pool_bytes = 0;
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();
#endif
        sceGuTerm();
        if (gPspGuBackend == this)
        {
            gPspGuBackend = nullptr;
        }
        mInitialized = false;
    }

    RendererType GetType() override
    {
        return RENDERER_OPENGLES;
    }

    void SetFogRange(f32 nearPlane, f32 farPlane) override
    {
        if (mFogParamsKnown && mFogNear == nearPlane && mFogFar == farPlane)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mFogNear = nearPlane;
        mFogFar = farPlane;
        sceGuFog(-mFogNear, -mFogFar, ToGuColor(mFogColor));
        mFogParamsKnown = true;
    }

    void SetFogColor(ZunColor color) override
    {
        if (mFogParamsKnown && mFogColor.color == color.color)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mFogColor = color;
        sceGuFog(-mFogNear, -mFogFar, ToGuColor(mFogColor));
        mFogParamsKnown = true;
    }

    void SetColorOp(TextureOpComponent component, ColorOp op) override
    {
        if (component == COMPONENT_RGB)
        {
            if (mColorOpRgb == op)
            {
                return;
            }
            FlushDeferredSpriteDraw();
            mColorOpRgb = op;
        }
        else
        {
            if (mColorOpAlpha == op)
            {
                return;
            }
            FlushDeferredSpriteDraw();
            mColorOpAlpha = op;
        }
    }

    void SetTextureFactor(ZunColor factor) override
    {
        if (mTextureFactor.color == factor.color)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mTextureFactor = factor;
    }

    void SetTextureArg(TextureArg arg) override
    {
        if (mTextureArg == arg)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mTextureArg = arg;
    }

    void SetTransformMatrix(TransformMatrix type, const ZunMatrix &matrix) override
    {
        if (type < MATRIX_MODEL || type > MATRIX_TEXTURE)
        {
            char message[64];
            std::snprintf(message, sizeof(message), "invalid matrix slot %d", type);
            th07_psp_boot_note(message);
            mError = true;
            return;
        }
        if (std::memcmp(&mTransforms[type], &matrix, sizeof(matrix)) == 0)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mTransforms[type] = matrix;
        // DrawPrimitiveUP bakes the texture transform into each UV. Only the
        // three spatial transforms correspond to persistent GE matrix state.
        if (type <= MATRIX_PROJECTION)
        {
            mMatrixDirtyMask |= MatrixDirtyBit(type);
        }
    }

    void SetTextureFilter() override
    {
        FlushDeferredSpriteDraw();
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        mAppliedTexture = ~0u;
    }

    void GetViewport(ZunViewport &viewport) override
    {
        viewport = mViewport;
    }

    void SetViewport(const ZunViewport &viewport) override
    {
        if (std::memcmp(&mViewport, &viewport, sizeof(viewport)) == 0)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mViewport = viewport;
        ApplyViewport();
        // The screen-space projection depends on the viewport. Preserve the
        // old full re-submit at this infrequent state boundary.
        mAppliedMatrixMode = -1;
        mMatrixDirtyMask = kAll3dMatrixDirtyBits;
    }

    void Enable(Capabilities cap) override
    {
        switch (cap)
        {
        case CAPS_BLEND:
            if (mBlendEnabled) return;
            FlushDeferredSpriteDraw();
            mBlendEnabled = true;
            sceGuEnable(GU_BLEND);
            break;
        case CAPS_ALPHA_TEST:
            if (mAlphaTestEnabled) return;
            FlushDeferredSpriteDraw();
            mAlphaTestEnabled = true;
            sceGuEnable(GU_ALPHA_TEST);
            break;
        case CAPS_DEPTH_TEST:
            if (mDepthTestEnabled) return;
            FlushDeferredSpriteDraw();
            mDepthTestEnabled = true;
            sceGuEnable(GU_DEPTH_TEST);
            break;
        case CAPS_FOG:
            if (mFogEnabled) return;
            FlushDeferredSpriteDraw();
            mFogEnabled = true;
            sceGuEnable(GU_FOG);
            break;
        }
    }

    void Disable(Capabilities cap) override
    {
        switch (cap)
        {
        case CAPS_BLEND:
            if (!mBlendEnabled) return;
            FlushDeferredSpriteDraw();
            mBlendEnabled = false;
            sceGuDisable(GU_BLEND);
            break;
        case CAPS_ALPHA_TEST:
            if (!mAlphaTestEnabled) return;
            FlushDeferredSpriteDraw();
            mAlphaTestEnabled = false;
            sceGuDisable(GU_ALPHA_TEST);
            break;
        case CAPS_DEPTH_TEST:
            if (!mDepthTestEnabled) return;
            FlushDeferredSpriteDraw();
            mDepthTestEnabled = false;
            sceGuDisable(GU_DEPTH_TEST);
            break;
        case CAPS_FOG:
            if (!mFogEnabled) return;
            FlushDeferredSpriteDraw();
            mFogEnabled = false;
            sceGuDisable(GU_FOG);
            break;
        }
    }

    void SetBlendMode(BlendMode srcMode, BlendMode dstMode) override
    {
        if (mBlendModeKnown && mBlendSrc == srcMode && mBlendDst == dstMode)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        // BLEND_NONE means GL_ONE on the source side in the portable
        // renderer.  Surface-to-backbuffer copies use NONE/NONE, so treating
        // it as SRC_ALPHA subtly changes what is meant to be a plain blit.
        const bool sourceIsOne = srcMode == BLEND_ONE || srcMode == BLEND_NONE;
        int src = sourceIsOne ? GU_FIX : GU_SRC_ALPHA;
        int dst = GU_ONE_MINUS_SRC_ALPHA;
        unsigned int srcFix = sourceIsOne ? 0xffffffffu : 0;
        unsigned int dstFix = 0;
        if (dstMode == BLEND_ONE)
        {
            dst = GU_FIX;
            dstFix = 0xffffffffu;
        }
        else if (dstMode == BLEND_NONE)
        {
            dst = GU_FIX;
            dstFix = 0;
        }
        sceGuBlendFunc(GU_ADD, src, dst, srcFix, dstFix);
        mBlendSrc = srcMode;
        mBlendDst = dstMode;
        mBlendModeKnown = true;
    }

    void SetDepthMask(bool enable) override
    {
        if (mDepthWrite == enable)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mDepthWrite = enable;
        sceGuDepthMask(enable ? GU_FALSE : GU_TRUE);
    }

    void SetDepthFunc(DepthFunc func) override
    {
        if (mDepthFuncKnown && mDepthFunc == func)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        sceGuDepthFunc(func == DEPTH_FUNC_LEQUAL ? GU_GEQUAL : GU_ALWAYS);
        mDepthFunc = func;
        mDepthFuncKnown = true;
    }

    void SetClearDepth(f32 depth) override
    {
        FlushDeferredSpriteDraw();
        mClearDepth = depth;
        sceGuClearDepth(ToHardwareDepth(depth));
    }

    void SetClearColor(ZunColor color) override
    {
        FlushDeferredSpriteDraw();
        mClearColor = color;
        sceGuClearColor(ToGuColor(color));
    }

    void SetAlphaTestRef(u8 ref) override
    {
        if (mAlphaRefKnown && mAlphaRef == ref)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        sceGuAlphaFunc(GU_GEQUAL, ref, 0xff);
        mAlphaRef = ref;
        mAlphaRefKnown = true;
    }

    void Clear(u32 clearBits) override
    {
        FlushDeferredSpriteDraw();
        unsigned int bits = 0;
        if (clearBits & CLEAR_COLOR_BUFFER)
        {
            bits |= GU_COLOR_BUFFER_BIT;
        }
        if (clearBits & CLEAR_DEPTH_BUFFER)
        {
            bits |= GU_DEPTH_BUFFER_BIT;
        }
        sceGuClearColor(ToGuColor(mClearColor));
        sceGuClearDepth(ToHardwareDepth(mClearDepth));
        sceGuClear(bits);
    }

    GfxTextureHandle CreateTexture() override
    {
        for (unsigned int i = 1; i < kMaxTextures; ++i)
        {
            if (!mTextures[i].used)
            {
                mTextures[i].used = true;
                return GfxTextureHandle(i);
            }
        }
        mError = true;
        return {};
    }

    void BindTexture(GfxTextureHandle handle) override
    {
        CheckBoundTexture("bind");
        const unsigned int texture = handle.id < kMaxTextures ? handle.id : 0;
        if (mBoundTexture == texture)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mBoundTexture = texture;
    }

    void DeleteTexture(GfxTextureHandle handle) override
    {
        if (handle.id == 0 || handle.id >= kMaxTextures || !mTextures[handle.id].used)
        {
            return;
        }
        GuTexture &texture = mTextures[handle.id];
        if (texture.pixels && !texture.borrowedSurfaceCache)
        {
            // Owned texture memory can be returned only after its queued draw
            // has completed.  Cached surface memory remains stable across
            // frames and needs no per-frame GE synchronization.
            SubmitAndRestart();
            RecycleTexturePixels(texture);
        }
        RecyclePixels(texture.updatePixels, texture.updateAllocationBytes);
        texture = GuTexture{};
        if (mAppliedTexture == handle.id)
        {
            mAppliedTexture = ~0u;
        }
        if (mBoundTexture == handle.id)
        {
            mBoundTexture = 0;
        }
    }

    void SetTextureImage(u32 width, u32 height, PixelFormat fmt, PixelDataType type,
                         const void *data) override
    {
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        const Th07PspPortraitTextureRole portraitRole = mNextPortraitRole;
        const unsigned int portraitTextureSlot = mNextPortraitTextureSlot;
        void *upperPortraitDestination = nullptr;
        mNextPortraitRole = TH07_PSP_PORTRAIT_NONE;
        mNextPortraitTextureSlot = 0;
#endif
        if (mBoundTexture == 0 || width == 0 || height == 0)
        {
            mError = true;
            return;
        }
        GuTexture &texture = mTextures[mBoundTexture];
        if (mAppliedTexture == mBoundTexture)
        {
            mAppliedTexture = ~0u;
        }
        if (texture.pixels && !texture.borrowedSurfaceCache)
        {
            SubmitAndRestart();
            RecycleTexturePixels(texture);
        }
        RecyclePixels(texture.updatePixels, texture.updateAllocationBytes);
        texture.pixels = nullptr;
        texture.updatePixels = nullptr;
        texture.borrowedSurfaceCache = false;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        texture.portraitRole = portraitRole;
        texture.portraitTextureSlot = static_cast<unsigned short>(portraitTextureSlot);
#endif

        texture.logicalWidth = static_cast<unsigned int>(width);
        texture.logicalHeight = static_cast<unsigned int>(height);
        const bool fullScreenSurface =
            data && width == 640 && height >= 480 &&
            ((fmt == PIXEL_RGBA && (type == PIXEL_UNSIGNED_BYTE ||
                                    type == PIXEL_UNSIGNED_SHORT_5_6_5)) ||
             (fmt == PIXEL_RGB && type == PIXEL_UNSIGNED_BYTE));
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        // R4 preserves the selected protagonist/bomb FACE atlases at their
        // native size. Only the current-stage FACE_STAGE archive is minified.
        const bool minifyStagePortrait =
            IsStagePortraitRole(portraitRole) && data && !fullScreenSurface;
        const unsigned int textureWidthLimit = minifyStagePortrait ? 256u : 512u;
        const unsigned int textureHeightLimit = minifyStagePortrait ? 256u : 512u;
#elif defined(TH07_PSP_1000)
        // Static 512px ANM atlases consume 512 KiB each in 16-bit form, while
        // the PSP LCD is only 480x272.  A 256px ceiling saves several MiB in
        // portrait-heavy later stages.  A small set of title/select UI atlases
        // keeps its native 512px width, but still uses a 256px height so that
        // improving menu lettering costs 128 KiB rather than 384 KiB per
        // atlas. Dynamic glyph atlases and full-screen JPEG surfaces retain
        // their established paths.
        const bool allowWideStaticTexture = mAllowNextWideStaticTexture;
        mAllowNextWideStaticTexture = false;
        const unsigned int textureWidthLimit =
            data && !fullScreenSurface ? (allowWideStaticTexture ? 512u : 256u) : 512u;
        const unsigned int textureHeightLimit =
            data && !fullScreenSurface ? 256u : 512u;
#else
        const unsigned int textureWidthLimit = 512u;
        const unsigned int textureHeightLimit = 512u;
#endif
        texture.storageWidth =
            NextPowerOfTwo(std::min(texture.logicalWidth, textureWidthLimit));
        texture.storageHeight =
            NextPowerOfTwo(std::min(texture.logicalHeight, textureHeightLimit));
        texture.contentWidth = std::min(texture.logicalWidth, texture.storageWidth);
        texture.contentHeight = std::min(texture.logicalHeight, texture.storageHeight);
        texture.sampleScaleX = static_cast<float>(texture.contentWidth) / texture.storageWidth;
        texture.sampleScaleY = static_cast<float>(texture.contentHeight) / texture.storageHeight;
        // Full-screen RGB565 and static-atlas RGBA8888 are desirable for
        // bandwidth/quality, but the combined fast-path build shut a real PSP
        // down inside title.anm registration.  Use the previously proven
        // allocation formats here; dynamic text is promoted independently by
        // MarkTextTexture after boot.
        const bool surfaceCandidate = fullScreenSurface;
        if (surfaceCandidate)
        {
            texture.contentWidth = std::min(
                texture.storageWidth,
                static_cast<unsigned int>(g_Supervisor.cfg.windowed ? kFitWidth : kScreenWidth));
            const unsigned int scaledHeight =
                (static_cast<unsigned int>(height) * kScreenHeight + kLogicalHeight - 1u) /
                kLogicalHeight;
            texture.contentHeight = std::min(texture.storageHeight, scaledHeight);
            texture.sampleScaleX =
                static_cast<float>(texture.contentWidth) / texture.storageWidth;
            texture.sampleScaleY =
                static_cast<float>(texture.contentHeight) / texture.storageHeight;
        }
        if (type == PIXEL_UNSIGNED_SHORT_5_6_5)
        {
            texture.psm = GU_PSM_5650;
        }
        else if (type == PIXEL_UNSIGNED_SHORT_5_5_5_1)
        {
            texture.psm = GU_PSM_5551;
        }
        else if (type == PIXEL_UNSIGNED_SHORT_4_4_4_4)
        {
            texture.psm = GU_PSM_4444;
        }
        else if (surfaceCandidate && kFastSurfaceTextures)
        {
            texture.psm = GU_PSM_5650;
        }
        else if (type == PIXEL_UNSIGNED_BYTE)
        {
            texture.psm = surfaceCandidate
                              ? GU_PSM_8888
                              : (kFastStaticTextureFormats ? GU_PSM_8888 : GU_PSM_4444);
        }
        else
        {
            texture.psm = GU_PSM_8888;
        }
        const unsigned int bytesPerPixel = texture.psm == GU_PSM_8888 ? 4u : 2u;
        texture.bytes = texture.storageWidth * texture.storageHeight * bytesPerPixel;
        texture.swizzled = (surfaceCandidate ? kSwizzleSurfaceTextures
                                             : kSwizzleStaticTextures) &&
                           texture.storageWidth * bytesPerPixel >= 16u &&
                           texture.storageHeight >= 8u;
        texture.surfaceCandidate = surfaceCandidate;
        // Surface frames reuse one handle every draw; tracing those would add
        // synchronous Memory Stick writes to the title loop.  The shutdown we
        // are isolating occurs in static title.anm registration.
        const bool traceTexture = !surfaceCandidate && mTextureCreateTraces < 64;
        if (traceTexture)
        {
            char message[144];
            std::snprintf(message, sizeof(message),
                          "teximg %u begin h%u %ux%u -> %ux%u p%d b%u free%uK",
                          mTextureCreateTraces, mBoundTexture,
                          static_cast<unsigned int>(width),
                          static_cast<unsigned int>(height), texture.storageWidth,
                          texture.storageHeight, texture.psm, texture.bytes,
                          sceKernelTotalFreeMemSize() / 1024u);
            th07_psp_boot_note(message);
            ++mTextureCreateTraces;
        }
        // Only the game's ending/title JPEG and backbuffer-sized copies may
        // borrow the one-entry surface cache.  Ending story images are
        // 640x480, while staff00.jpg is 640x627 and is scrolled through a
        // 640x480 viewport.  Excluding the latter caused all ~400k source
        // pixels to be resampled, allocated and freed on every staff frame.
        // The dialogue atlas is 1024 pixels wide
        // and starts as an RGBA zero buffer, so the old broad `width > 512`
        // test accidentally aliased it with that cache.  Subsequent text
        // uploads then had no private/update backing and PPSSPP kept showing
        // the first rendered line ("さむ～").
        if (surfaceCandidate && mSurfaceCache.valid && mSurfaceCache.source == data &&
            mSurfaceCache.width == width && mSurfaceCache.height == height &&
            mSurfaceCache.bytes == texture.bytes)
        {
#if defined(TH07_PSP_PERF_DETAIL)
            ++mSurfaceCacheHits;
#endif
            texture.pixels = mSurfaceCache.pixels;
            texture.borrowedSurfaceCache = true;
            return;
        }

        if (surfaceCandidate)
        {
#if defined(TH07_PSP_PERF_DETAIL)
            ++mSurfaceCacheMisses;
#endif
            // A previous frame may still sample this allocation.  Synchronize
            // only when replacing the cached image, never on cache hits.
            SubmitAndRestart();
            if (mSurfaceCache.bytes != texture.bytes)
            {
                if (!mSurfaceCache.edram)
                {
                    RecyclePixels(mSurfaceCache.pixels, mSurfaceCache.allocationBytes);
                }
                else
                {
                    mSurfaceCache.pixels = nullptr;
                    mSurfaceCache.allocationBytes = 0;
                }
                mSurfaceCache.edram = false;

                // The two framebuffers and depth buffer leave room for one
                // 512x512 RGB565 full-screen image.  sceGuTexImage expects an
                // absolute texture address here (unlike the relative buffer
                // offsets passed to sceGuDrawBuffer/sceGuDepthBuffer).
                const unsigned int lowerAperture =
                    std::min(static_cast<unsigned int>(sceGeEdramGetSize()),
                             kLowerEdramBytes);
                if (texture.bytes <= kSurfaceCacheMaxBytes &&
                    kEdramBytes + texture.bytes <= lowerAperture)
                {
                    const uintptr_t edram = reinterpret_cast<uintptr_t>(sceGeEdramGetAddr());
                    mSurfaceCache.pixels = reinterpret_cast<void *>(
                        (0x40000000u | edram) + kEdramBytes);
                    mSurfaceCache.edram = true;
                    th07_psp_boot_note("surface cache EDRAM");
                }
                else
                {
                    mSurfaceCache.pixels =
                        AcquireTexturePixels(texture.bytes, &mSurfaceCache.allocationBytes);
                }
                mSurfaceCache.bytes = mSurfaceCache.pixels ? texture.bytes : 0;
            }
            texture.pixels = mSurfaceCache.pixels;
            texture.borrowedSurfaceCache = true;
        }
        else
        {
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
            const unsigned int expectedPortraitBytes =
                IsPlayerPortraitRole(portraitRole)
                    ? kPlayerPortraitAtlasBytes
                : IsStagePortraitRole(portraitRole)
                    ? kStagePortraitAtlasBytes
                    : 0u;
            if (data && expectedPortraitBytes != 0u &&
                texture.bytes == expectedPortraitBytes)
            {
                upperPortraitDestination = AcquireUpperPortraitPixels(texture.bytes, texture);
                if (upperPortraitDestination)
                {
                    // All conversion below targets lower eDRAM. The upper raw
                    // address is installed in texture.pixels only after GE
                    // upload and readback verification both complete.
                    texture.pixels = UpperPortraitStagingCpuAddress();
                }
                else
                {
                    ++mUpperPortraitFallbacks;
                    MarkUpperPortraitFallback();
                    th07_psp_boot_notef("GE portrait role%u fallback Main RAM bytes%u",
                                        static_cast<unsigned int>(portraitRole), texture.bytes);
                }
            }
            else if (expectedPortraitBytes != 0u)
            {
                ++mUpperPortraitAllocationFailures;
                ++mUpperPortraitFallbacks;
                MarkUpperPortraitFallback();
                th07_psp_boot_notef("GE portrait role%u invalid upload -> Main RAM bytes%u",
                                    static_cast<unsigned int>(portraitRole), texture.bytes);
            }
#endif
            if (!texture.pixels)
            {
                texture.pixels =
                    AcquireTexturePixels(texture.bytes, &texture.allocationBytes);
            }
        }
        if (!texture.pixels)
        {
            const struct mallinfo heap = mallinfo();
            char message[176];
            std::snprintf(message, sizeof(message),
                          "texture allocation failed %u %ux%u bytes %u KFREE%u HFREE%u TOP%u",
                          mBoundTexture, texture.storageWidth, texture.storageHeight,
                          texture.bytes, sceKernelTotalFreeMemSize(),
                          static_cast<unsigned int>(heap.fordblks),
                          static_cast<unsigned int>(heap.keepcost));
            th07_psp_boot_note(message);
            mError = true;
            return;
        }
        std::memset(texture.pixels, 0, texture.bytes);

        if (data)
        {
            if (surfaceCandidate)
            {
                SurfaceAxisSample xSamples[512];
                SurfaceAxisSample ySamples[512];
                BuildSurfaceAxisSamples(xSamples, texture.contentWidth,
                                        static_cast<unsigned int>(width));
                BuildSurfaceAxisSamples(ySamples, texture.contentHeight,
                                        static_cast<unsigned int>(height));
                for (unsigned int y = 0; y < texture.contentHeight; ++y)
                {
                    const SurfaceAxisSample sy = ySamples[y];
                    const unsigned int wy0 = 256u - sy.fraction;
                    const unsigned int wy1 = sy.fraction;
                    for (unsigned int x = 0; x < texture.contentWidth; ++x)
                    {
                        const SurfaceAxisSample sx = xSamples[x];
                        const unsigned int wx0 = 256u - sx.fraction;
                        const unsigned int wx1 = sx.fraction;
                        unsigned int p00[4], p10[4], p01[4], p11[4];
                        ReadSourcePixel(fmt, type, data,
                                        static_cast<unsigned int>(sy.first) * width + sx.first,
                                        p00[0], p00[1], p00[2], p00[3]);
                        ReadSourcePixel(fmt, type, data,
                                        static_cast<unsigned int>(sy.first) * width + sx.second,
                                        p10[0], p10[1], p10[2], p10[3]);
                        ReadSourcePixel(fmt, type, data,
                                        static_cast<unsigned int>(sy.second) * width + sx.first,
                                        p01[0], p01[1], p01[2], p01[3]);
                        ReadSourcePixel(fmt, type, data,
                                        static_cast<unsigned int>(sy.second) * width + sx.second,
                                        p11[0], p11[1], p11[2], p11[3]);
                        unsigned int rgba[4];
                        for (unsigned int component = 0; component < 4; ++component)
                        {
                            const unsigned int top =
                                p00[component] * wx0 + p10[component] * wx1;
                            const unsigned int bottom =
                                p01[component] * wx0 + p11[component] * wx1;
                            rgba[component] =
                                (top * wy0 + bottom * wy1 + 32768u) >> 16;
                        }
                        WriteTexturePixel(texture, y * texture.storageWidth + x,
                                          rgba[0], rgba[1], rgba[2], rgba[3]);
                    }
                }
                char surfaceMessage[96];
                std::snprintf(surfaceMessage, sizeof(surfaceMessage),
                              "surface scaled %ux%u>%ux%u rgb565",
                              static_cast<unsigned int>(width),
                              static_cast<unsigned int>(height), texture.contentWidth,
                              texture.contentHeight);
                th07_psp_boot_note(surfaceMessage);
            }
            else if (fmt == PIXEL_RGBA && type == PIXEL_UNSIGNED_BYTE &&
                texture.psm == GU_PSM_8888 && texture.contentWidth == width &&
                texture.contentHeight == height && texture.storageWidth == width &&
                (!texture.swizzled || (height & 7u) == 0))
            {
                // RGBA8 already has the exact byte layout sampled by GE.  For
                // complete swizzle blocks, rearrange 16-byte x 8-row tiles;
                // otherwise the linear case is one memcpy.
                const auto *source = static_cast<const unsigned char *>(data);
                if (texture.swizzled)
                {
                    SwizzleCopy(static_cast<unsigned char *>(texture.pixels), source, width * 4u,
                                height);
                }
                else
                {
                    std::memcpy(texture.pixels, source, width * height * 4u);
                }
            }
            else if (texture.contentWidth == width && texture.contentHeight == height)
            {
                // Most atlases do not need minification.  Keep their original
                // one-read/one-write conversion path; the alpha-aware area
                // filter below is intentionally reserved for actual scaling.
                for (unsigned int y = 0; y < texture.contentHeight; ++y)
                {
                    for (unsigned int x = 0; x < texture.contentWidth; ++x)
                    {
                        unsigned int r, g, b, a;
                        ReadSourcePixel(fmt, type, data, y * width + x, r, g, b, a);
                        WriteTexturePixel(texture, y * texture.storageWidth + x,
                                          r, g, b, a);
                    }
                }
            }
            else
            {
                // PSP-1000 minifies most 512px atlases to fit the 32 MiB
                // address space.  Nearest-neighbour decimation made diagonal
                // edges and small lettering visibly jagged.  Average every
                // covered source texel in premultiplied-alpha space so
                // transparent atlas cells cannot bleed dark RGB into sprites.
                unsigned int sourceXFirst[512];
                unsigned int sourceXEnd[512];
                for (unsigned int x = 0; x < texture.contentWidth; ++x)
                {
                    sourceXFirst[x] = static_cast<unsigned int>(
                        static_cast<unsigned long long>(x) * width / texture.contentWidth);
                    sourceXEnd[x] = std::max(
                        sourceXFirst[x] + 1u,
                        static_cast<unsigned int>(
                            static_cast<unsigned long long>(x + 1u) * width /
                            texture.contentWidth));
                }
                for (unsigned int y = 0; y < texture.contentHeight; ++y)
                {
                    const unsigned int sourceY0 = static_cast<unsigned int>(
                        static_cast<unsigned long long>(y) * height / texture.contentHeight);
                    const unsigned int sourceY1 = std::max(
                        sourceY0 + 1u,
                        static_cast<unsigned int>(
                            static_cast<unsigned long long>(y + 1u) * height /
                            texture.contentHeight));
                    for (unsigned int x = 0; x < texture.contentWidth; ++x)
                    {
                        const unsigned int sourceX0 = sourceXFirst[x];
                        const unsigned int sourceX1 = sourceXEnd[x];
                        unsigned int sumA = 0;
                        unsigned int sumPremultipliedR = 0;
                        unsigned int sumPremultipliedG = 0;
                        unsigned int sumPremultipliedB = 0;
                        unsigned int samples = 0;
                        for (unsigned int sourceY = sourceY0; sourceY < sourceY1; ++sourceY)
                        {
                            for (unsigned int sourceX = sourceX0; sourceX < sourceX1; ++sourceX)
                            {
                                unsigned int r, g, b, a;
                                ReadSourcePixel(fmt, type, data, sourceY * width + sourceX,
                                                r, g, b, a);
                                sumA += a;
                                sumPremultipliedR += r * a;
                                sumPremultipliedG += g * a;
                                sumPremultipliedB += b * a;
                                ++samples;
                            }
                        }
                        const unsigned int a = sumA / samples;
                        const unsigned int r = sumA ? sumPremultipliedR / sumA : 0u;
                        const unsigned int g = sumA ? sumPremultipliedG / sumA : 0u;
                        const unsigned int b = sumA ? sumPremultipliedB / sumA : 0u;
                        WriteTexturePixel(texture, y * texture.storageWidth + x, r, g, b, a);
                    }
                }
            }
        }
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        if (upperPortraitDestination)
        {
            CompleteUpperPortraitUpload(texture, upperPortraitDestination);
        }
#endif
        if ((!mSurfaceCache.edram || !surfaceCandidate)
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
            && !texture.upperPortraitOwned
#endif
        )
        {
            sceKernelDcacheWritebackRange(texture.pixels, texture.bytes);
        }
        sceGuTexFlush();
        if (traceTexture)
        {
            char message[96];
            std::snprintf(message, sizeof(message), "teximg end h%u free%uK",
                          mBoundTexture, sceKernelTotalFreeMemSize() / 1024u);
            th07_psp_boot_note(message);
        }
#if defined(TH07_PSP_PERF_DETAIL)
        mTextureUploadBytes += texture.bytes;
#endif
        if (surfaceCandidate)
        {
            mSurfaceCache.source = data;
            mSurfaceCache.width = width;
            mSurfaceCache.height = height;
            mSurfaceCache.valid = true;
        }
    }

    void ForgetSurface(const void *pixels)
    {
        if (!pixels || !mSurfaceCache.valid || mSurfaceCache.source != pixels)
        {
            return;
        }
        // ReleaseSurface can run immediately after a frame which referenced
        // the cache.  Finish that list before the allocation is made eligible
        // for a different JPEG at the same SDL heap address.
        SubmitAndRestart();
        mSurfaceCache.source = nullptr;
        mSurfaceCache.valid = false;

        // TH06 PSP hit the same title -> attract-demo boundary: retaining a
        // full-screen conversion after the SDL surface was gone left about
        // 1 MiB unavailable while the stage, GUI and replay were registered.
        // A temporary surface texture is deleted before ReleaseSurface calls
        // this hook, so reclaim the unborrowed cache at that lifetime edge.
        for (const GuTexture &texture : mTextures)
        {
            if (texture.used && texture.borrowedSurfaceCache &&
                texture.pixels == mSurfaceCache.pixels)
            {
                return;
            }
        }
        if (!mSurfaceCache.edram)
        {
            RecyclePixels(mSurfaceCache.pixels, mSurfaceCache.allocationBytes);
        }
        mSurfaceCache = SurfaceCache{};
        th07_psp_boot_note("surface cache released");
    }

    unsigned int TrimTextureCache()
    {
        // Deleted textures are normally retained for quick reuse.  That is
        // useful inside a stage, but after several large 3D stages the mix of
        // atlas sizes can leave enough total memory free while no single heap
        // block is large enough for the next stage.  All blocks in this list
        // are detached from live textures and safe to return to libc.
        SubmitAndRestart();
        unsigned int releasedBytes = 0;
        for (FreeTextureBlock &block : mFreeTextureBlocks)
        {
            releasedBytes += block.bytes;
            std::free(block.pixels);
            block = FreeTextureBlock{};
        }
        return releasedBytes;
    }

    void MarkTextTexture(GfxTextureHandle handle)
    {
        if (handle.id == 0 || handle.id >= kMaxTextures)
        {
            return;
        }
        GuTexture &texture = mTextures[handle.id];
        if (!texture.used || texture.textAtlas)
        {
            return;
        }

        if (!texture.pixels || texture.borrowedSurfaceCache)
        {
            texture.textAtlas = true;
            mAppliedTexture = ~0u;
            return;
        }
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        // Portrait allocations are immutable. Preserve correctness if a future
        // caller unexpectedly promotes one to a dynamic text atlas.
        if (texture.upperPortraitOwned)
        {
            SubmitAndRestart();
            if (!MoveUpperPortraitToMain(texture))
            {
                return;
            }
        }
#endif

        if (texture.psm == GU_PSM_8888)
        {
            // TH06 keeps its dynamic RGBA atlas swizzled and patches glyphs in
            // swizzled address order.  TH07's previous linear promotion made
            // the 512x512 Music Room atlas dominate steady-state GE time.
            const bool canSwizzle = texture.storageWidth * 4u >= 16u &&
                                    texture.storageHeight >= 8u;
            if (canSwizzle && !texture.swizzled)
            {
                SubmitAndRestart();
                const unsigned int swizzledBytes = texture.bytes;
                unsigned int swizzledAllocationBytes = 0;
                void *swizzledPixels =
                    AcquireTexturePixels(swizzledBytes, &swizzledAllocationBytes);
                if (!swizzledPixels)
                {
                    th07_psp_boot_note("text atlas swizzle allocation failed");
                    mError = true;
                    return;
                }
                SwizzleCopy(static_cast<unsigned char *>(swizzledPixels),
                            static_cast<const unsigned char *>(texture.pixels),
                            texture.storageWidth * 4u, texture.storageHeight);
                sceKernelDcacheWritebackRange(swizzledPixels, swizzledBytes);
                RecycleTexturePixels(texture);
                texture.pixels = swizzledPixels;
                texture.allocationBytes = swizzledAllocationBytes;
                texture.bytes = swizzledBytes;
                texture.swizzled = true;
                sceGuTexFlush();
            }
            texture.textAtlas = true;
            mAppliedTexture = ~0u;
            return;
        }

        // TH06's final text path promotes its initially empty dynamic atlas to
        // RGBA8888 before the first glyph upload.  TH07 previously kept the
        // ANM-declared RGBA4444 allocation, reducing the antialiased outline
        // to sixteen alpha levels before the 640x480 -> PSP shrink.
        SubmitAndRestart();
        const unsigned int rgbaBytes = texture.storageWidth * texture.storageHeight * 4u;
        unsigned int rgbaAllocationBytes = 0;
        void *rgbaPixels = AcquireTexturePixels(rgbaBytes, &rgbaAllocationBytes);
        if (!rgbaPixels)
        {
            th07_psp_boot_note("text atlas RGBA8888 allocation failed");
            mError = true;
            return;
        }
        std::memset(rgbaPixels, 0, rgbaBytes);
        RecycleTexturePixels(texture);
        RecyclePixels(texture.updatePixels, texture.updateAllocationBytes);
        texture.pixels = rgbaPixels;
        texture.allocationBytes = rgbaAllocationBytes;
        texture.updatePixels = nullptr;
        texture.psm = GU_PSM_8888;
        texture.bytes = rgbaBytes;
        texture.textAtlas = true;
        texture.swizzled = texture.storageWidth * 4u >= 16u &&
                           texture.storageHeight >= 8u;
        sceKernelDcacheWritebackRange(texture.pixels, texture.bytes);
        sceGuTexFlush();
        mAppliedTexture = ~0u;

        char message[96];
        std::snprintf(message, sizeof(message), "text atlas %u RGBA8888 %ux%u swz%d",
                      static_cast<unsigned int>(handle.id),
                      texture.storageWidth, texture.storageHeight,
                      texture.swizzled ? 1 : 0);
        th07_psp_boot_note(message);
    }

    void AllowNextWideStaticTexture()
    {
        mAllowNextWideStaticTexture = true;
    }

#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    void PrepareUpperPortraitTexture(Th07PspPortraitTextureRole portraitRole,
                                     unsigned int textureSlot)
    {
        if (PortraitRoleMatchesSlot(portraitRole, textureSlot))
        {
            mNextPortraitRole = portraitRole;
            mNextPortraitTextureSlot = textureSlot;
        }
        else
        {
            mNextPortraitRole = TH07_PSP_PORTRAIT_NONE;
            mNextPortraitTextureSlot = 0;
        }
    }

    void CompleteUpperPortraitPrewarm(unsigned int stagePortraitCount)
    {
        if (stagePortraitCount == 0 || stagePortraitCount > kMaxStagePortraitAtlases)
        {
            ++mUpperPortraitInvariantFailures;
            mPortraitTelemetry.required_mask = 0;
            mPortraitTelemetryDirty = true;
            PublishUpperPortraitTelemetry();
            th07_psp_boot_notef("GE portrait PREWARM FAIL stage%u count%u invalid",
                                static_cast<unsigned int>(g_GameManager.currentStage),
                                stagePortraitCount);
            return;
        }

        const unsigned int playerMask = (1u << kMaxPlayerPortraitAtlases) - 1u;
        const unsigned int stageMask =
            ((1u << stagePortraitCount) - 1u) << kMaxPlayerPortraitAtlases;
        const unsigned int expectedMask = playerMask | stageMask;
        mPortraitTelemetry.required_mask = expectedMask;
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();

        const bool complete = mPortraitTelemetry.owned_mask == expectedMask &&
                              mPortraitTelemetry.verified_mask == expectedMask;
        if (complete)
        {
            th07_psp_boot_notef("GE portrait PREWARM COMPLETE stage%u count%u mask%02X",
                                static_cast<unsigned int>(g_GameManager.currentStage),
                                stagePortraitCount, expectedMask);
        }
        else
        {
            th07_psp_boot_notef(
                "GE portrait PREWARM FAIL stage%u count%u mask%02X owned%02X verified%02X",
                static_cast<unsigned int>(g_GameManager.currentStage), stagePortraitCount,
                expectedMask, mPortraitTelemetry.owned_mask,
                mPortraitTelemetry.verified_mask);
        }
    }
#endif

    bool GetTextureContentSize(GfxTextureHandle handle, unsigned int *width,
                               unsigned int *height) const
    {
        if (!width || !height || handle.id == 0 || handle.id >= kMaxTextures ||
            !mTextures[handle.id].used || !mTextures[handle.id].pixels)
        {
            return false;
        }
        *width = mTextures[handle.id].contentWidth;
        *height = mTextures[handle.id].contentHeight;
        return *width != 0 && *height != 0;
    }

    void CompactTextTexture(GfxTextureHandle handle)
    {
        if (handle.id == 0 || handle.id >= kMaxTextures)
        {
            return;
        }
        GuTexture &texture = mTextures[handle.id];
        if (!texture.used || !texture.textAtlas || !texture.updatePixels)
        {
            return;
        }

        // SetTextureSubImage synchronizes before writing and leaves the newest
        // atlas in `pixels`; the alternate allocation exists only to force a
        // new address on the next dynamic update.  Static menu text can return
        // that often-1-MiB buffer now and allocate it again only when the user
        // actually selects another track.
        std::free(texture.updatePixels);
        texture.updatePixels = nullptr;
        texture.updateAllocationBytes = 0;
    }

    void BeginTextUploadBatch()
    {
        if (mTextUploadBatchActive)
        {
            EndTextUploadBatch();
        }

        // No previously queued draw may still sample an atlas that is about
        // to become the source of the batch's alternate copy.  The music
        // room then performs only CPU writes until EndTextUploadBatch.
        SubmitAndRestart();
        mTextUploadBatchActive = true;
    }

    void EndTextUploadBatch()
    {
        if (!mTextUploadBatchActive)
        {
            return;
        }

        bool updated = false;
        for (GuTexture &texture : mTextures)
        {
            if (!texture.textBatchPending)
            {
                continue;
            }

            void *writePixels = texture.updatePixels ? texture.updatePixels : texture.pixels;
            if (writePixels)
            {
                sceKernelDcacheWritebackRange(writePixels, texture.bytes);
                if (texture.updatePixels)
                {
                    std::swap(texture.pixels, texture.updatePixels);
                    std::swap(texture.allocationBytes, texture.updateAllocationBytes);
                    // Re-establish the non-batch two-address invariant.  The
                    // next ordinary row update patches both allocations and
                    // may swap either one into use.
                    std::memcpy(texture.updatePixels, texture.pixels, texture.bytes);
                    sceKernelDcacheWritebackRange(texture.updatePixels, texture.bytes);
                }
                updated = true;
            }
            texture.textBatchPending = false;
        }
        if (updated)
        {
            sceGuTexFlush();
            mAppliedTexture = ~0u;
        }
        mTextUploadBatchActive = false;
    }

    void SetTextureSubImage(i32 xoffset, i32 yoffset, i32 width, i32 height,
                            const void *data) override
    {
        if (mBoundTexture == 0 || !data || width <= 0 || height <= 0)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        GuTexture &texture = mTextures[mBoundTexture];
        if (mAppliedTexture == mBoundTexture)
        {
            mAppliedTexture = ~0u;
        }
        if (!texture.pixels || xoffset < 0 || yoffset < 0 ||
            xoffset + width > static_cast<i32>(texture.logicalWidth) ||
            yoffset + height > static_cast<i32>(texture.logicalHeight))
        {
            mError = true;
            return;
        }
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        if (texture.upperPortraitOwned)
        {
            SubmitAndRestart();
            if (!MoveUpperPortraitToMain(texture))
            {
                return;
            }
        }
#endif
#if defined(TH07_PSP_DIRECT_GAME)
        {
            static unsigned int subImageLogCount;
            if (subImageLogCount < 80)
            {
                const auto *hashBytes = static_cast<const unsigned char *>(data);
                const unsigned int hashSize = static_cast<unsigned int>(width * height * 4);
                unsigned int hash = 2166136261u;
                for (unsigned int i = 0; i < hashSize; ++i)
                {
                    hash = (hash ^ hashBytes[i]) * 16777619u;
                }
                char message[160];
                std::snprintf(message, sizeof(message),
                              "subimg %u tex %u %u,%u %dx%d hash %08x dst %p alt %p",
                              subImageLogCount, mBoundTexture, xoffset, yoffset, width, height,
                              hash, texture.pixels, texture.updatePixels);
                th07_psp_boot_note(message);
                ++subImageLogCount;
            }
        }
#endif
        const bool batchedTextUpdate =
            mTextUploadBatchActive && texture.textAtlas && !texture.borrowedSurfaceCache;
        const bool synchronizedTextUpdate =
            !batchedTextUpdate && texture.textAtlas && !texture.borrowedSurfaceCache;
        if (!batchedTextUpdate)
        {
            SubmitAndRestart();
        }
        const auto *src = static_cast<const unsigned char *>(data);
        void *writePixels = texture.pixels;
        bool initializedAlternate = false;
        if (!texture.borrowedSurfaceCache)
        {
            if (batchedTextUpdate)
            {
                if (!texture.textBatchPending)
                {
                    if (!texture.updatePixels)
                    {
                        texture.updatePixels =
                            AcquireTexturePixels(texture.bytes, &texture.updateAllocationBytes);
                        initializedAlternate = texture.updatePixels != nullptr;
                    }
                    if (initializedAlternate)
                    {
                        // EndTextUploadBatch mirrors the newest atlas back to
                        // the alternate.  Re-copy the full atlas only when the
                        // alternate was just allocated; existing pairs are
                        // already identical before their dirty rows diverge.
                        std::memcpy(texture.updatePixels, texture.pixels, texture.bytes);
                    }
                    texture.textBatchPending = true;
                }
                writePixels = texture.updatePixels ? texture.updatePixels : texture.pixels;
            }
            else
            {
                if (!texture.updatePixels)
                {
                    texture.updatePixels =
                        AcquireTexturePixels(texture.bytes, &texture.updateAllocationBytes);
                    initializedAlternate = texture.updatePixels != nullptr;
                }
                if (texture.updatePixels)
                {
                    // Text atlases keep both alternating addresses in sync.
                    // Only the first allocation needs a whole-atlas copy;
                    // later glyph uploads patch the same rows in both copies.
                    if (!synchronizedTextUpdate || initializedAlternate)
                    {
                        std::memcpy(texture.updatePixels, texture.pixels, texture.bytes);
                    }
                    writePixels = texture.updatePixels;
                }
            }
        }
        const int dstLeft = static_cast<int>(static_cast<unsigned long long>(xoffset) *
                                             texture.contentWidth / texture.logicalWidth);
        const int dstTop = static_cast<int>(static_cast<unsigned long long>(yoffset) *
                                            texture.contentHeight / texture.logicalHeight);
        const int dstRight = static_cast<int>((static_cast<unsigned long long>(xoffset + width) *
                                               texture.contentWidth + texture.logicalWidth - 1) /
                                              texture.logicalWidth);
        const int dstBottom = static_cast<int>((static_cast<unsigned long long>(yoffset + height) *
                                                texture.contentHeight + texture.logicalHeight - 1) /
                                               texture.logicalHeight);
        // Complete block rows in a swizzled texture occupy one contiguous
        // address range. Publishing may round outward to 8-row boundaries;
        // directly swizzling the source still requires exact alignment and a
        // 1:1 mapping.
        const bool fullWidthSwizzledUpdate =
            texture.textAtlas && texture.psm == GU_PSM_8888 && texture.swizzled &&
            dstLeft == 0 && dstRight == static_cast<int>(texture.storageWidth) &&
            dstTop >= 0 && dstTop < dstBottom &&
            dstBottom <= static_cast<int>(texture.storageHeight);
        const int publishTop = dstTop & ~7;
        const int publishBottom = (dstBottom + 7) & ~7;
        const bool swizzledBandPublish =
            synchronizedTextUpdate && fullWidthSwizzledUpdate && publishTop >= 0 &&
            publishTop < publishBottom &&
            publishBottom <= static_cast<int>(texture.storageHeight);
        const bool directSwizzledBandCopy =
            fullWidthSwizzledUpdate && (dstTop & 7) == 0 && (dstBottom & 7) == 0 &&
            texture.logicalWidth == texture.contentWidth &&
            texture.logicalHeight == texture.contentHeight &&
            texture.logicalWidth == texture.storageWidth &&
            texture.logicalHeight == texture.storageHeight && xoffset == 0 &&
            width == static_cast<int>(texture.storageWidth);
        const auto writeRegion = [&](void *destination) {
            if (directSwizzledBandCopy)
            {
                SwizzleCopy(static_cast<unsigned char *>(destination) +
                                static_cast<unsigned int>(yoffset) * texture.storageWidth * 4u,
                            src, texture.storageWidth * 4u,
                            static_cast<unsigned int>(height));
                return;
            }
            // Dynamic text atlases are RGBA8888 with a 1:1 logical/content
            // mapping.  Avoid two 64-bit coordinate divisions and a format
            // repack for every pixel; the SDL source bytes already match the
            // little-endian GU_PSM_8888 layout exactly.
            if (texture.psm == GU_PSM_8888 && !texture.swizzled &&
                texture.contentWidth == texture.logicalWidth &&
                texture.contentHeight == texture.logicalHeight)
            {
                auto *dstBytes = static_cast<unsigned char *>(destination);
                const unsigned int dstStride = texture.storageWidth * 4u;
                const unsigned int rowBytes = static_cast<unsigned int>(width) * 4u;
                for (int row = 0; row < height; ++row)
                {
                    std::memcpy(dstBytes +
                                    (static_cast<unsigned int>(yoffset + row) * dstStride +
                                     static_cast<unsigned int>(xoffset) * 4u),
                                src + static_cast<unsigned int>(row) * rowBytes, rowBytes);
                }
                return;
            }

            GuTexture writeTexture = texture;
            writeTexture.pixels = destination;
            for (int y = dstTop; y < dstBottom; ++y)
            {
                const int sourceY = std::min(height - 1, std::max(0, static_cast<int>(
                    static_cast<unsigned long long>(y) * texture.logicalHeight /
                        texture.contentHeight - yoffset)));
                for (int x = dstLeft; x < dstRight; ++x)
                {
                    const int sourceX = std::min(width - 1, std::max(0, static_cast<int>(
                        static_cast<unsigned long long>(x) * texture.logicalWidth /
                            texture.contentWidth - xoffset)));
                    const unsigned char *pixel = src + (sourceY * width + sourceX) * 4;
                    WriteTexturePixel(writeTexture, y * texture.storageWidth + x,
                                      pixel[0], pixel[1], pixel[2], pixel[3]);
                }
            }
        };
        writeRegion(writePixels);
        if (synchronizedTextUpdate && texture.updatePixels &&
            texture.pixels != writePixels)
        {
            writeRegion(texture.pixels);
        }
        if (!batchedTextUpdate)
        {
            if (synchronizedTextUpdate)
            {
                if (texture.swizzled)
                {
                    // A single logical row is not contiguous in swizzled
                    // layout, but a full-width, 8-row-aligned block band is.
                    // Publish only that band for the Music Room's row atlas.
                    const unsigned int dirtyOffset =
                        swizzledBandPublish
                            ? static_cast<unsigned int>(publishTop) *
                                  texture.storageWidth * 4u
                            : 0u;
                    const unsigned int dirtyBytes =
                        swizzledBandPublish
                            ? static_cast<unsigned int>(publishBottom - publishTop) *
                                  texture.storageWidth * 4u
                            : texture.bytes;
                    sceKernelDcacheWritebackRange(
                        static_cast<unsigned char *>(texture.pixels) + dirtyOffset,
                        dirtyBytes);
                    if (texture.updatePixels)
                    {
                        const unsigned int alternateOffset =
                            initializedAlternate ? 0u : dirtyOffset;
                        const unsigned int alternateBytes =
                            initializedAlternate ? texture.bytes : dirtyBytes;
                        sceKernelDcacheWritebackRange(
                            static_cast<unsigned char *>(texture.updatePixels) +
                                alternateOffset,
                            alternateBytes);
                    }
                }
                else
                {
                    const unsigned int dirtyOffset =
                        static_cast<unsigned int>(dstTop) * texture.storageWidth * 4u;
                    const unsigned int dirtyBytes =
                        static_cast<unsigned int>(dstBottom - dstTop) * texture.storageWidth * 4u;
                    auto *currentBytes = static_cast<unsigned char *>(texture.pixels);
                    sceKernelDcacheWritebackRange(currentBytes + dirtyOffset, dirtyBytes);
                    if (texture.updatePixels)
                    {
                        if (initializedAlternate)
                        {
                            sceKernelDcacheWritebackRange(texture.updatePixels, texture.bytes);
                        }
                        else
                        {
                            auto *alternateBytes =
                                static_cast<unsigned char *>(texture.updatePixels);
                            sceKernelDcacheWritebackRange(alternateBytes + dirtyOffset, dirtyBytes);
                        }
                    }
                }
            }
            else
            {
                sceKernelDcacheWritebackRange(writePixels, texture.bytes);
            }
        }
#if defined(TH07_PSP_PERF_DETAIL)
        mTextureUploadBytes += static_cast<unsigned long long>(width) * height * 4u;
#endif
        if (!batchedTextUpdate && writePixels == texture.updatePixels)
        {
            std::swap(texture.pixels, texture.updatePixels);
            std::swap(texture.allocationBytes, texture.updateAllocationBytes);
        }
        if (!batchedTextUpdate)
        {
            sceGuTexFlush();
        }
    }

    void ReadPixels(i32 x, i32 y, i32 width, i32 height, void *pixels) override
    {
        if (!pixels || width <= 0 || height <= 0)
        {
            return;
        }
        SubmitAndRestart();
        const unsigned int edram = reinterpret_cast<unsigned int>(sceGeEdramGetAddr());
        // The PSP pause menu uses the direct pre-swap capture path below.
        // ReadPixels remains for the HOME snapshot taken after SwapBuffers,
        // when mCurrentDrawBuffer already names the new draw target and the
        // frame just presented is the opposite buffer.
        const unsigned int captureFrameOffset =
            mCurrentDrawBuffer ? 0u : kFrameBytes;
        const auto *frame = reinterpret_cast<const u16 *>(
            (0x40000000u | edram) + captureFrameOffset);
        auto *dst = static_cast<unsigned char *>(pixels);
        for (int row = 0; row < height; ++row)
        {
            const int logicalY = y + row;
            const int sourceY = logicalY * kScreenHeight / kLogicalHeight;
            for (int column = 0; column < width; ++column)
            {
                const int logicalX = x + column;
                const int contentWidth = g_Supervisor.cfg.windowed ? kFitWidth : kScreenWidth;
                const int contentLeft = g_Supervisor.cfg.windowed ? kFitLeft : 0;
                const int sourceX = contentLeft + logicalX * contentWidth / kLogicalWidth;
                u16 color = 0;
                if (sourceX >= 0 && sourceX < kScreenWidth && sourceY >= 0 &&
                    sourceY < kScreenHeight)
                {
                    color = frame[sourceY * kBufferWidth + sourceX];
                }
                unsigned char *out = dst + (row * width + column) * 4;
                const unsigned int r = color & 0x1fu;
                const unsigned int g = (color >> 5) & 0x3fu;
                const unsigned int b = (color >> 11) & 0x1fu;
                out[0] = static_cast<unsigned char>((r << 3) | (r >> 2));
                out[1] = static_cast<unsigned char>((g << 2) | (g >> 4));
                out[2] = static_cast<unsigned char>((b << 3) | (b >> 2));
                out[3] = 0xff;
            }
        }
    }

    bool CaptureFramebufferToTexture(GfxTextureHandle handle, int srcLeft, int srcTop,
                                     int srcWidth, int srcHeight, int dstLeft, int dstTop,
                                     int dstWidth, int dstHeight)
    {
        if (handle.id == 0 || handle.id >= kMaxTextures ||
            !mTextures[handle.id].used || srcWidth <= 0 || srcHeight <= 0 ||
            dstWidth <= 0 || dstHeight <= 0)
        {
            return false;
        }

        GuTexture &texture = mTextures[handle.id];
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        if (texture.upperPortraitOwned)
        {
            SubmitAndRestart();
            if (!MoveUpperPortraitToMain(texture))
            {
                return false;
            }
        }
#endif
        if (!texture.pixels || texture.logicalWidth == 0 || texture.logicalHeight == 0 ||
            texture.contentWidth == 0 || texture.contentHeight == 0 || dstLeft < 0 ||
            dstTop < 0 || dstLeft + dstWidth > static_cast<int>(texture.logicalWidth) ||
            dstTop + dstHeight > static_cast<int>(texture.logicalHeight))
        {
            return false;
        }

        // Pause/retry captures used to allocate a 384x448 RGBA readback plus
        // an SDL scaling surface and an alternate texture.  That transient
        // peak exceeds the fragmented heap late in a PSP-1000 run.  Stage the
        // RGB565 playfield with GE in unused rows of the existing 512x512
        // capture texture, then shrink it in place into the 128x128 sprite.
        // This avoids both the transient heap and CPU reads from GPU-owned
        // EDRAM, which are not coherent under every PPSSPP backend.
        SubmitAndRestart();
        const unsigned int edram = reinterpret_cast<unsigned int>(sceGeEdramGetAddr());
        const int contentWidth = g_Supervisor.cfg.windowed ? kFitWidth : kScreenWidth;
        const int contentLeft = g_Supervisor.cfg.windowed ? kFitLeft : 0;
        const int sourcePhysicalLeft =
            contentLeft + srcLeft * contentWidth / kLogicalWidth;
        const int sourcePhysicalRight =
            contentLeft + ((srcLeft + srcWidth) * contentWidth + kLogicalWidth - 1) /
                              kLogicalWidth;
        const int sourcePhysicalTop = srcTop * kScreenHeight / kLogicalHeight;
        const int sourcePhysicalBottom =
            ((srcTop + srcHeight) * kScreenHeight + kLogicalHeight - 1) /
            kLogicalHeight;
        const int sourcePhysicalWidth = sourcePhysicalRight - sourcePhysicalLeft;
        const int sourcePhysicalHeight = sourcePhysicalBottom - sourcePhysicalTop;
        const int textureLeft = static_cast<int>(
            static_cast<unsigned long long>(dstLeft) * texture.contentWidth /
            texture.logicalWidth);
        const int textureTop = static_cast<int>(
            static_cast<unsigned long long>(dstTop) * texture.contentHeight /
            texture.logicalHeight);
        const int textureRight = static_cast<int>(
            (static_cast<unsigned long long>(dstLeft + dstWidth) * texture.contentWidth +
             texture.logicalWidth - 1u) /
            texture.logicalWidth);
        const int textureBottom = static_cast<int>(
            (static_cast<unsigned long long>(dstTop + dstHeight) * texture.contentHeight +
             texture.logicalHeight - 1u) /
            texture.logicalHeight);
        if (texture.psm != GU_PSM_5650 || sourcePhysicalLeft < 0 ||
            sourcePhysicalTop < 0 || sourcePhysicalRight > kScreenWidth ||
            sourcePhysicalBottom > kScreenHeight || sourcePhysicalWidth <= 0 ||
            sourcePhysicalHeight <= 0 ||
            sourcePhysicalWidth > static_cast<int>(texture.storageWidth) ||
            sourcePhysicalHeight > static_cast<int>(texture.storageHeight) ||
            textureBottom > static_cast<int>(texture.storageHeight))
        {
            return false;
        }

        const auto mappedSourceY = [&](int textureY) {
            const int scaledY = std::min(
                dstHeight - 1,
                std::max(0, static_cast<int>(
                    static_cast<unsigned long long>(textureY) * texture.logicalHeight /
                        texture.contentHeight -
                    dstTop)));
            const int logicalY =
                srcTop + static_cast<int>(static_cast<unsigned long long>(scaledY) * srcHeight /
                                          dstHeight);
            return logicalY * kScreenHeight / kLogicalHeight;
        };

        // The capture atlas begins empty and every useful output texel is
        // replaced below, so its backing can stay linear for GE staging and
        // subsequent texture sampling.
        texture.swizzled = false;
        const int stagingTop = static_cast<int>(texture.storageHeight) - sourcePhysicalHeight;
        const bool overlapsHorizontally = textureLeft < sourcePhysicalWidth && textureRight > 0;
        const int overlapTop = std::max(textureTop, stagingTop);
        const int overlapBottom =
            std::min(textureBottom, stagingTop + sourcePhysicalHeight);
        if (overlapsHorizontally && overlapTop < overlapBottom)
        {
            // A 384x448 Stage Clear capture occupies y=0..447 while its
            // 254-row physical source is staged at y=258..511 in the same
            // atlas. Expanding from top to bottom is nevertheless safe: every
            // overlapped staging row has already been consumed before that
            // output row overwrites it. Reject any other layout unless it has
            // the same strict read-before-overwrite ordering.
            for (int textureY = overlapTop; textureY < overlapBottom; ++textureY)
            {
                const int overwrittenSourceRow = textureY - stagingTop;
                const int sampledSourceRow = mappedSourceY(textureY) - sourcePhysicalTop;
                if (overwrittenSourceRow >= sampledSourceRow)
                {
                    return false;
                }
            }
        }

        // Horizontal mapping is constant for every output row. Computing its
        // 64-bit divisions in the inner loop made a full 384x448 Stage Clear
        // capture needlessly expensive on the SC; a 1 KiB stack table reduces
        // those divisions from 172,032 to at most 512 without changing a
        // sampled pixel.
        if (textureRight > kBufferWidth)
        {
            return false;
        }
        short sourceColumnOffsets[kBufferWidth];
        for (int textureX = textureLeft; textureX < textureRight; ++textureX)
        {
            const int scaledX = std::min(
                dstWidth - 1,
                std::max(0, static_cast<int>(
                    static_cast<unsigned long long>(textureX) * texture.logicalWidth /
                        texture.contentWidth -
                    dstLeft)));
            const int logicalX =
                srcLeft + static_cast<int>(static_cast<unsigned long long>(scaledX) * srcWidth /
                                           dstWidth);
            const int sourceX = contentLeft + logicalX * contentWidth / kLogicalWidth;
            sourceColumnOffsets[textureX] =
                sourceX >= sourcePhysicalLeft && sourceX < sourcePhysicalRight
                    ? static_cast<short>(sourceX - sourcePhysicalLeft)
                    : static_cast<short>(-1);
        }
        sceKernelDcacheWritebackInvalidateRange(texture.pixels, texture.bytes);
        auto *drawBuffer = reinterpret_cast<void *>(
            edram + (mCurrentDrawBuffer ? kFrameBytes : 0u));
        sceGuCopyImage(GU_PSM_5650, sourcePhysicalLeft, sourcePhysicalTop,
                       sourcePhysicalWidth, sourcePhysicalHeight, kBufferWidth, drawBuffer,
                       0, stagingTop, texture.storageWidth, texture.pixels);
        sceGuTexSync();
        SubmitAndRestart();
        sceKernelDcacheInvalidateRange(texture.pixels, texture.bytes);
        const auto *staging = static_cast<const u16 *>(texture.pixels) +
                              stagingTop * texture.storageWidth;
        auto *capturePixels = static_cast<u16 *>(texture.pixels);

        for (int textureY = textureTop; textureY < textureBottom; ++textureY)
        {
            const int sourceY = mappedSourceY(textureY);
            for (int textureX = textureLeft; textureX < textureRight; ++textureX)
            {
                u16 color = 0;
                const int sourceColumnOffset = sourceColumnOffsets[textureX];
                if (sourceColumnOffset >= 0 && sourceY >= sourcePhysicalTop &&
                    sourceY < sourcePhysicalBottom)
                {
                    color = staging[(sourceY - sourcePhysicalTop) * texture.storageWidth +
                                    sourceColumnOffset];
                }
                // Both the GE staging copy and capture atlas use GU_PSM_5650,
                // so the word is already in the exact sampled layout.
                capturePixels[textureY * texture.storageWidth + textureX] = color;
            }
        }

        sceKernelDcacheWritebackRange(texture.pixels, texture.bytes);
        sceGuTexFlush();
        mAppliedTexture = ~0u;
#if defined(TH07_PSP_DIRECT_GAME)
        th07_psp_boot_notef("pause capture direct %dx%d", textureRight - textureLeft,
                            textureBottom - textureTop);
#endif
        return true;
    }

    void DrawPrimitive(PrimitiveType, i32, i32) override
    {
        // PSP forces the engine's noVertexBuffers path, which supplies the
        // actual vertices to DrawPrimitiveUP.
        mError = true;
    }

    void DrawPrimitiveUP(PrimitiveType type, i32 primitiveCount, const void *vertexData,
                         i32 vertexStride) override
    {
        // A general primitive must not overtake a delayed AnmManager batch.
        FlushDeferredSpriteDraw();
        if (!vertexData || primitiveCount <= 0)
        {
            return;
        }
        int vertexCount = 0;
        int primitive = GU_TRIANGLES;
        const bool quadBatch = type == PRIM_QUADS;
        if (quadBatch)
        {
            vertexCount = primitiveCount * 4;
        }
        else if (type == PRIM_TRIANGLES)
        {
            vertexCount = primitiveCount * 3;
        }
        else if (type == PRIM_TRIANGLE_STRIP)
        {
            vertexCount = primitiveCount + 2;
            primitive = GU_TRIANGLE_STRIP;
        }
        else
        {
            vertexCount = primitiveCount + 2;
            primitive = GU_TRIANGLE_FAN;
        }

        const bool screenSpace = vertexStride == static_cast<i32>(sizeof(VertexTex1DiffuseXyzrhw)) ||
                                 vertexStride == static_cast<i32>(sizeof(VertexDiffuseXyzrhw));
        const bool textured = vertexStride == static_cast<i32>(sizeof(VertexTex1DiffuseXyzrhw)) ||
                              vertexStride == static_cast<i32>(sizeof(VertexTex1DiffuseXyz));
        if (!screenSpace && !textured)
        {
            mError = true;
            return;
        }

        ApplyMatrices(screenSpace);
        ApplyTexture(textured, screenSpace);

#if defined(TH07_PSP_PERF_M2)
        PerfInternalScope packScope(TH07_PSP_PERF_INTERNAL_PACK);
#endif

        const auto *source = static_cast<const unsigned char *>(vertexData);
        const auto colorAt = [&](int index) {
            const unsigned char *raw = source + index * vertexStride;
            ZunColor diffuse;
            if (textured)
            {
                if (screenSpace)
                {
                    diffuse = reinterpret_cast<const VertexTex1DiffuseXyzrhw *>(raw)->color;
                }
                else
                {
                    diffuse = reinterpret_cast<const VertexTex1DiffuseXyz *>(raw)->diffuse;
                }
            }
            else
            {
                diffuse = reinterpret_cast<const VertexDiffuseXyzrhw *>(raw)->diffuse;
            }
            return SelectVertexColor(diffuse);
        };

        // Most TH07 sprite batches carry a diffuse field even though every
        // vertex is the same colour.  GE can take that colour once as state,
        // reducing the native vertex from 24 to 20 bytes (16 to 12 without a
        // texture).  Keep the full layout for genuinely varying batches.
        unsigned int constantColor = colorAt(0);
        bool constantColorBatch = true;
        const int colorStep = quadBatch ? 4 : 1;
        for (int i = colorStep; i < vertexCount; i += colorStep)
        {
            if (colorAt(i) != constantColor)
            {
                constantColorBatch = false;
                break;
            }
        }

        // PSP AnmManager submits four unique quad corners. Axis-aligned batches
        // become two-corner GU sprites; any rotated quad keeps those four
        // vertices and uses a compact shared index stream for two triangles.
        bool collapseSprites =
            quadBatch && screenSpace && textured && vertexCount >= 4 &&
            vertexStride == static_cast<i32>(sizeof(VertexTex1DiffuseXyzrhw));
        const VertexTex1DiffuseXyzrhw *spriteSource = nullptr;
        if (collapseSprites)
        {
            spriteSource = static_cast<const VertexTex1DiffuseXyzrhw *>(vertexData);
            const int spriteCount = primitiveCount;
            for (int sprite = 0; sprite < spriteCount; ++sprite)
            {
                const VertexTex1DiffuseXyzrhw *q = spriteSource + sprite * 4;
                if (q[0].pos.x != q[2].pos.x || q[1].pos.x != q[3].pos.x ||
                    q[0].pos.y != q[1].pos.y || q[2].pos.y != q[3].pos.y ||
                    q[0].pos.z != q[1].pos.z || q[0].pos.z != q[2].pos.z ||
                    q[0].pos.z != q[3].pos.z || q[0].pos.x > q[3].pos.x ||
                    q[0].pos.y > q[3].pos.y ||
                    q[0].textureUV.x != q[2].textureUV.x ||
                    q[1].textureUV.x != q[3].textureUV.x ||
                    q[0].textureUV.y != q[1].textureUV.y ||
                    q[2].textureUV.y != q[3].textureUV.y)
                {
                    collapseSprites = false;
                    spriteSource = nullptr;
                    break;
                }
            }
        }

        const bool indexedQuads = quadBatch && !collapseSprites;
        const int submittedVertexCount = collapseSprites ? primitiveCount * 2 : vertexCount;
        const int indexCount = indexedQuads ? primitiveCount * 6 : 0;
        const unsigned int nativeVertexBytes =
            textured ? (constantColorBatch ? sizeof(GuVertexTex) : sizeof(GuVertexTexColor))
                     : (constantColorBatch ? sizeof(GuVertexPosition) : sizeof(GuVertexColor));
        const unsigned int vertexBytes =
            static_cast<unsigned int>(submittedVertexCount) * nativeVertexBytes;
        const bool cachedQuadIndices =
            indexedQuads && primitiveCount <= static_cast<int>(kCachedQuadIndexCount);
        const unsigned int dynamicIndexBytes =
            cachedQuadIndices ? 0u : static_cast<unsigned int>(indexCount) * sizeof(u16);
        if (!EnsureListSpace(vertexBytes + dynamicIndexBytes))
        {
            mError = true;
            return;
        }
        void *packed = sceGuGetMemory(vertexBytes);
        if (!packed)
        {
            mError = true;
            return;
        }
        u16 *indices = nullptr;
        if (indexedQuads)
        {
            if (cachedQuadIndices)
            {
                indices = gQuadIndices;
#if defined(TH07_PSP_PERF_DETAIL)
                ++mCachedQuadIndexBatches;
#endif
            }
            else
            {
                indices = static_cast<u16 *>(sceGuGetMemory(dynamicIndexBytes));
                if (!indices)
                {
                    mError = true;
                    return;
                }
                for (int sprite = 0; sprite < primitiveCount; ++sprite)
                {
                    const u16 base = static_cast<u16>(sprite * 4);
                    indices[sprite * 6 + 0] = base;
                    indices[sprite * 6 + 1] = base + 1;
                    indices[sprite * 6 + 2] = base + 2;
                    indices[sprite * 6 + 3] = base + 1;
                    indices[sprite * 6 + 4] = base + 2;
                    indices[sprite * 6 + 5] = base + 3;
                }
            }
        }

        const GuTexture *texture = mBoundTexture ? &mTextures[mBoundTexture] : nullptr;
        if (constantColorBatch &&
            (!mPrimitiveColorKnown || mAppliedPrimitiveColor != constantColor))
        {
            sceGuColor(constantColor);
            mPrimitiveColorKnown = true;
            mAppliedPrimitiveColor = constantColor;
        }

        if (textured)
        {
            if (collapseSprites && constantColorBatch)
            {
                auto *out = static_cast<GuVertexTex *>(packed);
                const int spriteCount = primitiveCount;
                for (int sprite = 0; sprite < spriteCount; ++sprite)
                {
                    const VertexTex1DiffuseXyzrhw *q = spriteSource + sprite * 4;
                    const VertexTex1DiffuseXyzrhw *corners[2] = {&q[0], &q[3]};
                    for (int corner = 0; corner < 2; ++corner)
                    {
                        const int outIndex = sprite * 2 + corner;
                        out[outIndex].u = texture && texture->pixels ? corners[corner]->textureUV.x
                                                                     : 0.0f;
                        out[outIndex].v = texture && texture->pixels ? corners[corner]->textureUV.y
                                                                     : 0.0f;
                        out[outIndex].x = corners[corner]->pos.x;
                        out[outIndex].y = corners[corner]->pos.y;
                        out[outIndex].z = corners[corner]->pos.z;
                    }
                }
            }
            else if (collapseSprites)
            {
                auto *out = static_cast<GuVertexTexColor *>(packed);
                const int spriteCount = primitiveCount;
                for (int sprite = 0; sprite < spriteCount; ++sprite)
                {
                    const VertexTex1DiffuseXyzrhw *q = spriteSource + sprite * 4;
                    const VertexTex1DiffuseXyzrhw *corners[2] = {&q[0], &q[3]};
                    const unsigned int color = colorAt(sprite * 4);
                    for (int corner = 0; corner < 2; ++corner)
                    {
                        const int outIndex = sprite * 2 + corner;
                        out[outIndex].u = texture && texture->pixels ? corners[corner]->textureUV.x
                                                                     : 0.0f;
                        out[outIndex].v = texture && texture->pixels ? corners[corner]->textureUV.y
                                                                     : 0.0f;
                        out[outIndex].color = color;
                        out[outIndex].x = corners[corner]->pos.x;
                        out[outIndex].y = corners[corner]->pos.y;
                        out[outIndex].z = corners[corner]->pos.z;
                    }
                }
            }
            else if (constantColorBatch)
            {
                auto *out = static_cast<GuVertexTex *>(packed);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const unsigned char *raw = source + i * vertexStride;
                    float u;
                    float v;
                    if (screenSpace)
                    {
                        const auto *vertex =
                            reinterpret_cast<const VertexTex1DiffuseXyzrhw *>(raw);
                        out[i].x = vertex->pos.x;
                        out[i].y = vertex->pos.y;
                        out[i].z = vertex->pos.z;
                        u = vertex->textureUV.x;
                        v = vertex->textureUV.y;
                    }
                    else
                    {
                        const auto *vertex = reinterpret_cast<const VertexTex1DiffuseXyz *>(raw);
                        out[i].x = vertex->position.x;
                        out[i].y = vertex->position.y;
                        out[i].z = vertex->position.z;
                        u = vertex->textureUV.x * mTransforms[MATRIX_TEXTURE].m[0][0] +
                            vertex->textureUV.y * mTransforms[MATRIX_TEXTURE].m[1][0] +
                            mTransforms[MATRIX_TEXTURE].m[2][0];
                        v = vertex->textureUV.x * mTransforms[MATRIX_TEXTURE].m[0][1] +
                            vertex->textureUV.y * mTransforms[MATRIX_TEXTURE].m[1][1] +
                            mTransforms[MATRIX_TEXTURE].m[2][1];
                    }
                    out[i].u = texture && texture->pixels ? u : 0.0f;
                    out[i].v = texture && texture->pixels ? v : 0.0f;
                }
            }
            else
            {
                auto *out = static_cast<GuVertexTexColor *>(packed);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const unsigned char *raw = source + i * vertexStride;
                    float u;
                    float v;
                    if (screenSpace)
                    {
                        const auto *vertex =
                            reinterpret_cast<const VertexTex1DiffuseXyzrhw *>(raw);
                        out[i].x = vertex->pos.x;
                        out[i].y = vertex->pos.y;
                        out[i].z = vertex->pos.z;
                        u = vertex->textureUV.x;
                        v = vertex->textureUV.y;
                    }
                    else
                    {
                        const auto *vertex = reinterpret_cast<const VertexTex1DiffuseXyz *>(raw);
                        out[i].x = vertex->position.x;
                        out[i].y = vertex->position.y;
                        out[i].z = vertex->position.z;
                        u = vertex->textureUV.x * mTransforms[MATRIX_TEXTURE].m[0][0] +
                            vertex->textureUV.y * mTransforms[MATRIX_TEXTURE].m[1][0] +
                            mTransforms[MATRIX_TEXTURE].m[2][0];
                        v = vertex->textureUV.x * mTransforms[MATRIX_TEXTURE].m[0][1] +
                            vertex->textureUV.y * mTransforms[MATRIX_TEXTURE].m[1][1] +
                            mTransforms[MATRIX_TEXTURE].m[2][1];
                    }
                    out[i].u = texture && texture->pixels ? u : 0.0f;
                    out[i].v = texture && texture->pixels ? v : 0.0f;
                    out[i].color = colorAt(i);
                }
            }
            // D3D's XYZRHW/screen-space path does not participate in vertex
            // fog.  TH07's title JPEG reaches the backbuffer through exactly
            // this path; leaving GU_FOG enabled replaces it with the default
            // grey fog colour.  The PS3 native backend makes the same split:
            // CopyRects/native_blit has fog=false and only clip-space draws
            // can select its fog fragment program.
            if (screenSpace)
            {
                sceGuDisable(GU_FOG);
            }
#if defined(TH07_PSP_PERF_M2)
            packScope.End();
#endif
            const int vertexType = GU_TEXTURE_32BITF |
                                   (constantColorBatch ? 0 : GU_COLOR_8888) |
                                   GU_VERTEX_32BITF | GU_TRANSFORM_3D |
                                   (indexedQuads ? GU_INDEX_16BIT : 0);
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
            NoteBoundUpperPortraitDraw();
#endif
            sceGuDrawArray(collapseSprites ? GU_SPRITES : primitive, vertexType,
                           indexedQuads ? indexCount : submittedVertexCount,
                           indices, packed);
#if defined(TH07_PSP_PERF_DETAIL)
            ++mDraws;
            mInputVertices += static_cast<unsigned int>(vertexCount);
            mVertices += static_cast<unsigned int>(submittedVertexCount);
#endif
            if (screenSpace && mFogEnabled)
            {
                sceGuEnable(GU_FOG);
            }
        }
        else
        {
            if (constantColorBatch)
            {
                auto *out = static_cast<GuVertexPosition *>(packed);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const auto *vertex = reinterpret_cast<const VertexDiffuseXyzrhw *>(
                        source + i * vertexStride);
                    out[i].x = vertex->pos.x;
                    out[i].y = vertex->pos.y;
                    out[i].z = vertex->pos.z;
                }
            }
            else
            {
                auto *out = static_cast<GuVertexColor *>(packed);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const auto *vertex = reinterpret_cast<const VertexDiffuseXyzrhw *>(
                        source + i * vertexStride);
                    out[i].x = vertex->pos.x;
                    out[i].y = vertex->pos.y;
                    out[i].z = vertex->pos.z;
                    out[i].color = colorAt(i);
                }
            }
            if (screenSpace)
            {
                sceGuDisable(GU_FOG);
            }
#if defined(TH07_PSP_PERF_M2)
            packScope.End();
#endif
            sceGuDrawArray(primitive,
                           (constantColorBatch ? 0 : GU_COLOR_8888) | GU_VERTEX_32BITF |
                               GU_TRANSFORM_3D | (indexedQuads ? GU_INDEX_16BIT : 0),
                           indexedQuads ? indexCount : vertexCount, indices, packed);
#if defined(TH07_PSP_PERF_DETAIL)
            ++mDraws;
            mInputVertices += static_cast<unsigned int>(vertexCount);
            mVertices += static_cast<unsigned int>(vertexCount);
#endif
            if (screenSpace && mFogEnabled)
            {
                sceGuEnable(GU_FOG);
            }
        }
    }

    void DrawSpriteQuads(const Th07PspSpriteVertex *vertices, unsigned int spriteCount,
                         bool allowAxisCollapse)
    {
        if (!vertices || spriteCount == 0)
        {
            return;
        }
        if (!EnsureListSpace(0))
        {
            mError = true;
            return;
        }

#if defined(TH07_PSP_PERF_M2)
        PerfInternalScope packScope(TH07_PSP_PERF_INTERNAL_PACK);
#endif

        ApplyMatrices(true);
        ApplyTexture(true, true);

        const auto canCollapse = [](const Th07PspSpriteVertex *q) {
            return q[0].x == q[2].x && q[1].x == q[3].x &&
                   q[0].y == q[1].y && q[2].y == q[3].y &&
                   q[0].z == q[1].z && q[0].z == q[2].z && q[0].z == q[3].z &&
                   q[0].x <= q[3].x && q[0].y <= q[3].y &&
                   q[0].u == q[2].u && q[1].u == q[3].u &&
                   q[0].v == q[1].v && q[2].v == q[3].v;
        };

        unsigned int sprite = 0;
        while (sprite < spriteCount)
        {
            const bool collapsed =
                allowAxisCollapse && canCollapse(vertices + sprite * 4u);
            unsigned int runEnd = sprite + 1u;
            while (runEnd < spriteCount &&
                   (allowAxisCollapse && canCollapse(vertices + runEnd * 4u)) ==
                       collapsed)
            {
                ++runEnd;
            }

            unsigned int remaining = runEnd - sprite;
            const Th07PspSpriteVertex *batch = vertices + sprite * 4u;

            while (remaining)
            {
                const unsigned int batchSprites =
                    std::min(remaining, kCachedQuadIndexCount);
                if (collapsed)
                {
                    unsigned char *arenaBase = DeferredSpriteArenaBase();
                    const auto *engineWrite = g_AnmManager
                                                  ? reinterpret_cast<const unsigned char *>(
                                                        g_AnmManager->vertexBufferCurPtr)
                                                  : nullptr;
                    const unsigned int packedBytes =
                        batchSprites * 2u * sizeof(GuVertexTexColor);
                    const bool arenaSafe = arenaBase && engineWrite && engineWrite < arenaBase;
                    if (arenaSafe &&
                        mDeferredSpriteArenaUsed + packedBytes <= kDeferredSpriteArenaBytes)
                    {
                        auto *packed = reinterpret_cast<GuVertexTexColor *>(
                            arenaBase + mDeferredSpriteArenaUsed);
                        if (mDeferredSpriteVertices &&
                            mDeferredSpriteVertices + mDeferredSpriteVertexCount != packed)
                        {
                            FlushDeferredSpriteDraw();
                        }
                        // TH06's delayed path publishes render state when a
                        // group begins, then folds later engine flushes into
                        // the same GE primitive.  Every state mutation below
                        // flushes this group before it can change that state.
                        if (!mDeferredSpriteVertices)
                        {
                            if (!EnsureListSpace(0))
                            {
                                mError = true;
                                return;
                            }
                            ApplyMatrices(true);
                            ApplyTexture(true, true);
                            sceGuDisable(GU_FOG);
                            mDeferredSpriteVertices =
                                reinterpret_cast<GuVertexTexColor *>(
                                    arenaBase + mDeferredSpriteArenaUsed);
                        }
#if defined(TH07_PSP_PERF_M3)
                        PerfM3NoteDeferredAppend(gPerfM3IncomingBatchOrigin);
#endif

                        for (unsigned int i = 0; i < batchSprites; ++i)
                        {
                            const Th07PspSpriteVertex *q = batch + i * 4u;
                            const Th07PspSpriteVertex *corners[2] = {&q[0], &q[3]};
                            for (unsigned int corner = 0; corner < 2u; ++corner)
                            {
                                GuVertexTexColor &out = packed[i * 2u + corner];
                                out.u = corners[corner]->u;
                                out.v = corners[corner]->v;
                                out.color = corners[corner]->color;
                                out.x = corners[corner]->x;
                                out.y = corners[corner]->y;
                                out.z = corners[corner]->z;
                            }
                        }
                        mDeferredSpriteArenaUsed += packedBytes;
                        mDeferredSpriteVertexCount += batchSprites * 2u;
                        mDeferredSpriteInputVertexCount += batchSprites * 4u;
                    }
                    else
                    {
                        // The engine grew into the reserved tail or this
                        // frame exhausted it.  Preserve correctness with the
                        // previous live-list packing path for this batch.
                        FlushDeferredSpriteDraw();
                        if (!EnsureListSpace(packedBytes))
                        {
                            mError = true;
                            return;
                        }
                        auto *packed = static_cast<GuVertexTexColor *>(
                            sceGuGetMemory(static_cast<int>(packedBytes)));
                        if (!packed)
                        {
                            mError = true;
                            return;
                        }
                        for (unsigned int i = 0; i < batchSprites; ++i)
                        {
                            const Th07PspSpriteVertex *q = batch + i * 4u;
                            const Th07PspSpriteVertex *corners[2] = {&q[0], &q[3]};
                            for (unsigned int corner = 0; corner < 2u; ++corner)
                            {
                                GuVertexTexColor &out = packed[i * 2u + corner];
                                out.u = corners[corner]->u;
                                out.v = corners[corner]->v;
                                out.color = corners[corner]->color;
                                out.x = corners[corner]->x;
                                out.y = corners[corner]->y;
                                out.z = corners[corner]->z;
                            }
                        }
                        sceGuDisable(GU_FOG);
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
                        NoteBoundUpperPortraitDraw();
#endif
                        sceGuDrawArray(GU_SPRITES,
                                       GU_TEXTURE_32BITF | GU_COLOR_8888 |
                                           GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                                       static_cast<int>(batchSprites * 2u), nullptr, packed);
#if defined(TH07_PSP_PERF_DETAIL)
                        ++mDraws;
                        mInputVertices += batchSprites * 4u;
                        mVertices += batchSprites * 2u;
                        ++mCachedQuadIndexBatches;
#endif
                        if (mFogEnabled)
                        {
                            sceGuEnable(GU_FOG);
                        }
                    }
                }
                else
                {
                    // General rotated or mirrored quads retain the shared
                    // indexed path and delimit delayed axis-aligned groups.
                    FlushDeferredSpriteDraw();
#if defined(TH07_PSP_PERF_M2)
                    PerfInternalScope dcacheScope(TH07_PSP_PERF_INTERNAL_DCACHE);
#endif
#if defined(TH07_PSP_PERF_M3)
                    const unsigned long long dcacheStartUs =
                        sceKernelGetSystemTimeWide();
#endif
                    sceKernelDcacheWritebackRange(
                        const_cast<Th07PspSpriteVertex *>(batch),
                        batchSprites * sizeof(*vertices) * 4u);
#if defined(TH07_PSP_PERF_M2)
                    dcacheScope.End();
#endif
#if defined(TH07_PSP_PERF_M3)
                    const unsigned long long m3DcacheUs =
                        sceKernelGetSystemTimeWide() - dcacheStartUs;
                    if (gPerfM3BulletLoopActive)
                    {
                        gPerfM3DcacheUs += m3DcacheUs;
                        ++gPerfM3DcacheCalls;
                    }
                    if (gPerfM3WrapperActive)
                    {
                        gPerfM3WrapperDcacheUs += m3DcacheUs;
                    }
#endif
                    sceGuDisable(GU_FOG);
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
                    NoteBoundUpperPortraitDraw();
#endif
                    sceGuDrawArray(GU_TRIANGLES,
                                   GU_TEXTURE_32BITF | GU_COLOR_8888 |
                                       GU_VERTEX_32BITF | GU_TRANSFORM_3D |
                                       GU_INDEX_16BIT,
                                   static_cast<int>(batchSprites * 6u),
                                   gQuadIndices, batch);
#if defined(TH07_PSP_PERF_DETAIL)
                    ++mDraws;
                    mInputVertices += batchSprites * 4u;
                    mVertices += batchSprites * 4u;
#endif
                    if (mFogEnabled)
                    {
                        sceGuEnable(GU_FOG);
                    }
                }
                batch += batchSprites * 4u;
                remaining -= batchSprites;
            }
            sprite = runEnd;
        }
#if defined(TH07_PSP_PERF_M2)
        packScope.End();
#endif
    }

    void DrawSpritePairs(const Th07PspSpriteVertex *vertices, unsigned int spriteCount)
    {
        if (!vertices || spriteCount == 0)
        {
            return;
        }
        if (!EnsureListSpace(0))
        {
            mError = true;
            return;
        }

        ApplyMatrices(true);
        ApplyTexture(true, true);

        auto *pairs = reinterpret_cast<GuVertexTexColor *>(
            const_cast<Th07PspSpriteVertex *>(vertices));
        if (mDeferredSpriteVertices &&
            mDeferredSpriteVertices + mDeferredSpriteVertexCount != pairs)
        {
            FlushDeferredSpriteDraw();
        }
        if (!mDeferredSpriteVertices)
        {
            // AnmManager owns this per-frame memory until SwapBuffers has
            // synchronized the GE.  Publish it directly and let consecutive
            // compatible bullet/effect flushes extend one GU_SPRITES command.
            ApplyMatrices(true);
            ApplyTexture(true, true);
            sceGuDisable(GU_FOG);
            mDeferredSpriteVertices = pairs;
        }
#if defined(TH07_PSP_PERF_M3)
        PerfM3NoteDeferredAppend(gPerfM3IncomingBatchOrigin);
#endif
        mDeferredSpriteVertexCount += spriteCount * 2u;
        mDeferredSpriteInputVertexCount += spriteCount * 2u;
    }

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    bool BeginMeRenderGeSubmission(unsigned int slot,
                                   unsigned int generation)
    {
        if (!mInitialized || !mListOpen || mMeRenderGeTokenPending ||
            mMeRenderGeSubmissionOpen || generation == 0u)
        {
            return false;
        }

        // Any restart here precedes ownership promotion and therefore cannot
        // expose the ME slot to GE. Flush the older AnmManager batch before
        // storing the token so its completion can never be mistaken for the
        // fence of the stream about to be appended.
        if (!EnsureListSpace(0))
        {
            return false;
        }
        FlushDeferredSpriteDraw();

        mMeRenderGeToken.slot = slot;
        mMeRenderGeToken.generation = generation;
        mMeRenderGeTokenPending = true;
        mMeRenderGeSubmissionOpen = true;
        if (th07_psp_me_render_stream_mark_ge_in_flight(
                &mMeRenderGeToken) != 1)
        {
            mMeRenderGeSubmissionOpen = false;
            mMeRenderGeTokenPending = false;
            mMeRenderGeToken = Th07PspMeRenderStreamToken{};
            return false;
        }
        return true;
    }

#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    void DrawMeRenderStreamRun(
        const Th07PspMeRenderStreamVertex *vertices,
        unsigned int vertexCount, unsigned int primitive)
#else
    void DrawMeRenderStreamRun(const Th07PspSpriteVertex *vertices,
                               unsigned int vertexCount,
                               unsigned int primitive)
#endif
    {
        // Begin's caller has already validated the complete immutable stream.
        // EnsureListSpace(0) cannot fail with this backend's fixed reserve; it
        // may synchronize/restart a full command list. The open-span interlock
        // deliberately retains the token across such an intermediate fence.
        (void)EnsureListSpace(0);

        // An ME pool is not AnmManager's per-frame arena and must never be
        // coalesced into the deferred owner. Emit it immediately.
        FlushDeferredSpriteDraw();
        ApplyMatrices(true);
        ApplyTexture(true, true);
        sceGuDisable(GU_FOG);
#if defined(TH07_PSP_ME_RENDER_XYZ16)
        sceGuSetMatrix(GU_MODEL, &kMeRenderXyz16ModelMatrix);
#if defined(TH07_PSP_PERF_DETAIL)
        ++mMatrixSubmissions;
#endif
#endif

#if defined(TH07_PSP_ME_RENDER_UV16)
        constexpr int textureFormat = GU_TEXTURE_16BIT;
#else
        constexpr int textureFormat = GU_TEXTURE_32BITF;
#endif
#if defined(TH07_PSP_ME_RENDER_XYZ16)
        constexpr int positionFormat = GU_VERTEX_16BIT;
#else
        constexpr int positionFormat = GU_VERTEX_32BITF;
#endif

        if (primitive == TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES)
        {
            sceGuDrawArray(GU_SPRITES,
                           textureFormat | GU_COLOR_8888 |
                               positionFormat | GU_TRANSFORM_3D,
                           static_cast<int>(vertexCount), nullptr, vertices);
#if defined(TH07_PSP_PERF_DETAIL)
            ++mDraws;
            mInputVertices += vertexCount;
            mVertices += vertexCount;
#endif
        }
        else
        {
            unsigned int remaining = vertexCount / 4u;
            const auto *batch = vertices;
            while (remaining != 0u)
            {
                const unsigned int sprites =
                    std::min(remaining, kCachedQuadIndexCount);
                sceGuDrawArray(GU_TRIANGLES,
                               textureFormat | GU_COLOR_8888 |
                                   positionFormat | GU_TRANSFORM_3D |
                                   GU_INDEX_16BIT,
                               static_cast<int>(sprites * 6u), gQuadIndices,
                               batch);
#if defined(TH07_PSP_PERF_DETAIL)
                ++mDraws;
                mInputVertices += sprites * 4u;
                mVertices += sprites * 4u;
                ++mCachedQuadIndexBatches;
#endif
                batch += sprites * 4u;
                remaining -= sprites;
            }
        }
#if defined(TH07_PSP_ME_RENDER_XYZ16)
        // ApplyMatrices(true) caches identity MODEL for screen-space draws.
        // Restore that exact state before returning so ordinary AnmManager
        // vertices can never inherit C1's fixed-point scale.
        sceGuSetMatrix(GU_MODEL, &kIdentityMatrix);
#if defined(TH07_PSP_PERF_DETAIL)
        ++mMatrixSubmissions;
#endif
#endif
        if (mFogEnabled)
        {
            sceGuEnable(GU_FOG);
        }
    }

    void EndMeRenderGeSubmission()
    {
        // No operation after Begin can fail: all pointers, primitives and run
        // extents were validated before ownership promotion. Closing the span
        // makes the next real sceGuSync the release fence, even if an earlier
        // list-space restart occurred while runs were being appended.
        mMeRenderGeSubmissionOpen = false;
    }

    void FenceMeRenderBeforeMeShutdown()
    {
        if (!mMeRenderGeTokenPending)
        {
            return;
        }
        if (mMeRenderGeSubmissionOpen || !mListOpen)
        {
            // Shutdown is serialized after callbacks. Reaching it inside an
            // open submission (or without its owning list) is an ownership
            // violation, not authority to free the slot without a GE fence.
            Th07PspMeRenderGeReleaseFault();
            return;
        }
        SubmitAndRestart();
    }
#endif

#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    void BeginLowResStagePass()
    {
        if (mLowResStagePass || !mListOpen || !th07_psp_ge4_active())
        {
            return;
        }
        FlushDeferredSpriteDraw();
        sceGuDrawBuffer(kFramePsm, reinterpret_cast<void *>(kLowResColorOffset),
                        kLowResBufferWidth);
        sceGuDepthBuffer(reinterpret_cast<void *>(kLowResDepthOffset),
                         kLowResBufferWidth);
        mLowResStagePass = true;
        // Stale-pixel safety: the upscale blit covers the whole screen, so a
        // stage pass that draws little must still leave a defined image.
        sceGuOffset(2048 - kLowResWidth / 2, 2048 - kLowResHeight / 2);
        sceGuViewport(2048, 2048, kLowResWidth, kLowResHeight);
        sceGuScissor(0, 0, kLowResWidth, kLowResHeight);
        sceGuClearColor(0xff000000u);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        sceGuClearColor(ToGuColor(mClearColor));
        ApplyViewport();
    }

    void EndLowResStagePass()
    {
        if (!mLowResStagePass)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        sceGuDrawBuffer(kFramePsm,
                        reinterpret_cast<void *>(mCurrentDrawBuffer ? 0u : kFrameBytes),
                        kBufferWidth);
        sceGuDepthBuffer(reinterpret_cast<void *>(kDepthOffset), kBufferWidth);
        mLowResStagePass = false;
        sceGuOffset(2048 - kScreenWidth / 2, 2048 - kScreenHeight / 2);
        sceGuViewport(2048, 2048, kScreenWidth, kScreenHeight);
        sceGuScissor(0, 0, kScreenWidth, kScreenHeight);
        // The stage pass cleared depth into the low-res buffer; the real frame
        // still needs its per-frame depth clear.
        sceGuClear(GU_DEPTH_BUFFER_BIT);
        // Upscale blit, raw GU state; caches are invalidated afterwards.
        sceGuDisable(GU_DEPTH_TEST);
        sceGuDisable(GU_BLEND);
        sceGuDisable(GU_FOG);
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexMode(kFramePsm, 0, 0, GU_FALSE);
        sceGuTexImage(0, kLowResBufferWidth, 256, kLowResBufferWidth,
                      reinterpret_cast<const void *>(
                          reinterpret_cast<uintptr_t>(sceGeEdramGetAddr()) +
                          kLowResColorOffset));
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        sceGuTexWrap(GU_CLAMP, GU_CLAMP);
        sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
        struct BlitVertex
        {
            short u, v;
            short x, y, z;
        };
        auto *vertices =
            static_cast<BlitVertex *>(sceGuGetMemory(2 * sizeof(BlitVertex)));
        vertices[0] = {0, 0, 0, 0, 0};
        vertices[1] = {static_cast<short>(kLowResWidth),
                       static_cast<short>(kLowResHeight),
                       static_cast<short>(kScreenWidth),
                       static_cast<short>(kScreenHeight), 0};
        sceGuDrawArray(GU_SPRITES,
                       GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2,
                       nullptr, vertices);
        // Wrap and filter are set once at init and never re-applied per
        // texture, so the blit must restore those globals itself.
        sceGuTexWrap(GU_REPEAT, GU_REPEAT);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        // Reconcile the state caches with the raw calls above.
        mDepthTestEnabled = false;
        mBlendEnabled = false;
        mFogEnabled = false;
        mBlendModeKnown = false;
        mDepthFuncKnown = false;
        mBoundTexture = 0;
        mAppliedTexture = ~0u;
        mTextureEnableKnown = false;
        mAppliedColorOpKnown = false;
        mAppliedMatrixMode = -1;
        mMatrixDirtyMask = kAll3dMatrixDirtyBits;
        ApplyViewport();
    }
#endif

    void BeginStagePlayfieldScissor()
    {
        if (mStagePlayfieldScissor)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mStagePlayfieldScissor = true;
        ApplyViewport();
    }

    void EndStagePlayfieldScissor()
    {
        if (!mStagePlayfieldScissor)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mStagePlayfieldScissor = false;
        ApplyViewport();
    }

    void SwapBuffers() override
    {
        if (!mListOpen)
        {
            return;
        }
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        const bool denseSliceActive = gTh07PspPerfDenseSliceActive != 0;
        const unsigned long long denseSwapStartUs =
            denseSliceActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        // Fail-safe: a stage pass that never reached its end hook must not
        // leave the frame rendering into the low-res target.
        EndLowResStagePass();
#endif
        // Fail-safe: never leave the playfield clip active across frames.
        EndStagePlayfieldScissor();
        FlushDeferredSpriteDraw();
#if defined(TH07_PSP_USAGE_METER)
        // The overlay allocates raw GU vertices, so it must participate in
        // the backend's list-capacity contract.  The original Fable wiring
        // appended 1776 bytes after the final flush without this guard.
        if (EnsureListSpace(TH07_PSP_USAGE_METER_VERTEX_BYTES))
        {
            th07_usage_meter_draw();

            // usage_meter.c uses raw GU calls.  Restore the *actual cached
            // backend state*, not Init's fixed defaults: a frame may finish
            // with depth writes, blending, fog or texturing in either state.
            // Leaving GU at a default while the cache retained the opposite
            // value made the following frame skip required state commands.
            if (mBlendEnabled)
                sceGuEnable(GU_BLEND);
            else
                sceGuDisable(GU_BLEND);
            if (mDepthTestEnabled)
                sceGuEnable(GU_DEPTH_TEST);
            else
                sceGuDisable(GU_DEPTH_TEST);
            if (mFogEnabled)
                sceGuEnable(GU_FOG);
            else
                sceGuDisable(GU_FOG);
            sceGuDepthMask(mDepthWrite ? GU_FALSE : GU_TRUE);
            if (mTextureEnableKnown && mTextureEnabled)
                sceGuEnable(GU_TEXTURE_2D);
            else
                sceGuDisable(GU_TEXTURE_2D);
        }
#endif
        const int listBytes = sceGuFinish();
        mListOpen = false;
#if defined(TH07_PSP_PERF_DIAG)
        // M2 closes the CPU interval after sceGuFinish so list finalization is
        // part of the frame budget.
        const unsigned long long finishEndUs = sceKernelGetSystemTimeWide();
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        if (denseSliceActive)
        {
            gPerfDenseSwapSubmitUs += finishEndUs - denseSwapStartUs;
            ++gPerfDenseSwapFrames;
        }
#endif
#if defined(TH07_PSP_PERF_DETAIL)
        if (listBytes > 0)
        {
            mMaxListBytes = std::max(mMaxListBytes, static_cast<unsigned int>(listBytes));
        }
#endif
#endif
        // Overlap the GE tail with the vblank wait: the GPU keeps draining the
        // submitted list while the CPU sleeps until vsync, so a frame whose
        // GPU work trails the CPU by a few ms no longer pays that tail twice.
        // The sync after the wait is then normally instant; only a frame whose
        // GPU is still busy past vsync swaps late.
        sceDisplayWaitVblankStart();
#if defined(TH07_PSP_PERF_DIAG)
        const unsigned long long vblankEndUs = sceKernelGetSystemTimeWide();
        const unsigned int vblankCount = sceDisplayGetVcount();
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        const int syncResult = sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        if (listBytes < 0 || syncResult < 0 ||
            sceGeDrawSync(1) != PSP_GE_LIST_DONE)
        {
            UpperPortraitInvariantFailure("frame-gu-state-unknown");
        }
#if defined(TH07_PSP_PERF_DIAG)
        // Portrait telemetry is CPU bookkeeping. Timestamp GE completion
        // before publishing it so it cannot contaminate post-vblank GE tail.
        const unsigned long long geEndUs = sceKernelGetSystemTimeWide();
#endif
        PublishUpperPortraitTelemetry();
#else
        sceGuSync(0, 0);
#if defined(TH07_PSP_PERF_DIAG)
        const unsigned long long geEndUs = sceKernelGetSystemTimeWide();
#endif
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        ReleaseMeRenderGeTokenAfterSync();
#endif
#if defined(TH07_PSP_PERF_DIAG)
        AccumulateAndReportPerf(finishEndUs, vblankEndUs, geEndUs, vblankCount);
#if defined(TH07_PSP_PERF_DETAIL)
        mListsThisFrame = 0;
        mFrameBlockingGeUs = 0;
#endif
        if (mPerfGameplayPending)
        {
            // GameManager registration runs inside the calc-chain scope.  Arm
            // there, but reset/activate only at this completed swap boundary
            // so stage-load work and a partial first frame cannot enter M2/M3.
            ResetPerfWindowCounters();
            mPerfGameplayActive = true;
            mPerfGameplayPending = false;
            mPerfWindowStage = mPerfPendingStage;
            mPerfWindowState = 2;
            mLastVblankCount = vblankCount;
#if defined(TH07_PSP_PERF_DENSE_SLICE)
            ConfigurePerfDenseSliceForNextWindow();
#endif
        }
        // Window formatting and RAM-log bookkeeping are profiler overhead,
        // not game work.  Timestamp after that bookkeeping but still before
        // swap/list setup, preserving the inclusive game-frame boundary.
        const unsigned long long nextFrameStartUs = sceKernelGetSystemTimeWide();
        // This is the next frame's inclusive CPU boundary: after the previous
        // GE completed, but before swap/list setup/preservation/pillar clear.
        mFrameStartUs = nextFrameStartUs;
#endif
        sceGuSwapBuffers();
        mCurrentDrawBuffer ^= 1;
        StartList();
        PreserveLatestPlayfield();
        ClearPillarboxes();
    }

  private:
    void PreserveLatestPlayfield()
    {
        // The original windowed D3D8 renderer uses D3DSWAPEFFECT_COPY, so
        // effects which deliberately skip the colour clear always blend over
        // the immediately preceding frame.  PSP alternates two framebuffers;
        // without this copy each target instead contains the frame from two
        // presents ago.  Stage 3's translucent-background effect then makes
        // the intended shot trails decay only every other present, leaving
        // them substantially longer and darker than the original.  The trails
        // (including a max-power player's shot columns) remain visible by
        // design.  Copy only the playfield which can retain colour; pillarboxes
        // are cleared below and GameWindow clears the HUD bands next draw.
        const int contentWidth = g_Supervisor.cfg.windowed ? kFitWidth : kScreenWidth;
        const int contentLeft = g_Supervisor.cfg.windowed ? kFitLeft : 0;
        const int left = contentLeft + 32 * contentWidth / kLogicalWidth;
        const int right = contentLeft + 416 * contentWidth / kLogicalWidth;
        const int top = 16 * kScreenHeight / kLogicalHeight;
        const int bottom = (464 * kScreenHeight + kLogicalHeight - 1) / kLogicalHeight;
        const unsigned int edram = reinterpret_cast<unsigned int>(sceGeEdramGetAddr());
        void *displayBuffer = reinterpret_cast<void *>(
            edram + (mCurrentDrawBuffer ? 0u : kFrameBytes));
        void *drawBuffer = reinterpret_cast<void *>(
            edram + (mCurrentDrawBuffer ? kFrameBytes : 0u));
        sceGuCopyImage(GU_PSM_5650, left, top, right - left, bottom - top,
                       kBufferWidth, displayBuffer, left, top, kBufferWidth, drawBuffer);
        sceGuTexSync();
    }

    GuTexture mTextures[kMaxTextures];
    FreeTextureBlock mFreeTextureBlocks[kMaxTextures];
    SurfaceCache mSurfaceCache;
    ZunMatrix mTransforms[4];
    ZunViewport mViewport{};
    ZunColor mTextureFactor{0xffffffffu};
    ZunColor mFogColor{0xffa0a0a0u};
    ZunColor mClearColor{0xff000000u};
    unsigned int mBoundTexture = 0;
    float mFogNear = 1000.0f;
    float mFogFar = 5000.0f;
    float mClearDepth = 1.0f;
    TextureArg mTextureArg = TEX_ARG_DIFFUSE;
    ColorOp mColorOpRgb = COLOR_OP_MODULATE;
    ColorOp mColorOpAlpha = COLOR_OP_MODULATE;
    int mAppliedMatrixMode = -1;
    int mCurrentDrawBuffer = 0;
    unsigned int mMatrixDirtyMask = kAll3dMatrixDirtyBits;
    bool mDepthWrite = true;
    bool mDepthFuncKnown = false;
    DepthFunc mDepthFunc = DEPTH_FUNC_LEQUAL;
    bool mBlendEnabled = true;
    bool mAlphaTestEnabled = true;
    bool mDepthTestEnabled = false;
    bool mFogEnabled = false;
    bool mFogParamsKnown = false;
    bool mBlendModeKnown = false;
    BlendMode mBlendSrc = BLEND_ALPHA;
    BlendMode mBlendDst = BLEND_NONE;
    bool mAlphaRefKnown = true;
    u8 mAlphaRef = 4;
    bool mTextureEnableKnown = false;
    bool mTextureEnabled = false;
    unsigned int mAppliedTexture = ~0u;
    unsigned int mTextureCreateTraces = 0;
    bool mAppliedScreenSpace = false;
    bool mAppliedColorOpKnown = false;
    ColorOp mAppliedColorOpRgb = COLOR_OP_MODULATE;
    bool mPrimitiveColorKnown = false;
    unsigned int mAppliedPrimitiveColor = 0xffffffffu;
    bool mListOpen = false;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    bool mLowResStagePass = false;
#endif
    // Clip the stage/spell background pass to the playfield.  The GUI frame
    // covers everything outside it every frame, so the ~44% of background
    // fill spent there is invisible; skipping it is visually lossless.
    bool mStagePlayfieldScissor = false;
    bool mInitialized = false;
    bool mError = false;
    bool mTextUploadBatchActive = false;
    bool mAllowNextWideStaticTexture = false;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    Th07PspPortraitTextureRole mNextPortraitRole = TH07_PSP_PORTRAIT_NONE;
    unsigned int mNextPortraitTextureSlot = 0;
    uintptr_t mUpperPortraitPoolBase = 0;
    unsigned int mUpperPortraitPoolPages = 0;
    unsigned int mUpperPortraitUsedPages = 0;
    unsigned int mUpperPortraitPeakPages = 0;
    unsigned int mUpperPortraitActivePlayers = 0;
    unsigned int mUpperPortraitActiveStages = 0;
    unsigned int mUpperPortraitFallbacks = 0;
    unsigned int mUpperPortraitMigrations = 0;
    unsigned int mUpperPortraitAllocationFailures = 0;
    unsigned int mUpperPortraitInvariantFailures = 0;
    unsigned int mUpperPortraitPlayerAllocations = 0;
    unsigned int mUpperPortraitStageAllocations = 0;
    unsigned int mUpperPortraitUploadGeneration = 0;
    unsigned int mUpperPortraitPageBits[kUpperPortraitPoolPages / 32u] = {};
    Th07PspPortraitCacheSnapshot mPortraitTelemetry{};
    bool mPortraitTelemetryDirty = false;
#endif
    GuVertexTexColor *mDeferredSpriteVertices = nullptr;
    unsigned int mDeferredSpriteVertexCount = 0;
    unsigned int mDeferredSpriteInputVertexCount = 0;
    unsigned int mDeferredSpriteArenaUsed = 0;
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    Th07PspMeRenderStreamToken mMeRenderGeToken{};
    bool mMeRenderGeTokenPending = false;
    bool mMeRenderGeSubmissionOpen = false;
#endif
#if defined(TH07_PSP_PERF_DIAG)
    unsigned long long mFrameStartUs = 0;
    unsigned long long mPerfStartUs = 0;
    unsigned long long mPerfLastGeEndUs = 0;
    unsigned long long mPerfCpuUs = 0;
    unsigned long long mPerfGeUs = 0;
    unsigned long long mPerfVblankUs = 0;
    unsigned long long mPerfBlockingGeUs = 0;
    unsigned long long mPerfMaxFrameUs = 0;
    unsigned long long mFrameBlockingGeUs = 0;
    unsigned long long mTextureUploadBytes = 0;
    unsigned int mPerfFrames = 0;
    unsigned int mDraws = 0;
    unsigned int mInputVertices = 0;
    unsigned int mVertices = 0;
    unsigned int mPerfStartDraws = 0;
    unsigned int mPerfStartInputVertices = 0;
    unsigned int mPerfStartVertices = 0;
    unsigned int mListsThisFrame = 0;
    unsigned int mPerfLists = 0;
    unsigned int mMaxListBytes = 0;
    unsigned int mSurfaceCacheHits = 0;
    unsigned int mSurfaceCacheMisses = 0;
    unsigned int mPerfMaxBullets = 0;
    unsigned int mPerfMaxEffects = 0;
    unsigned int mMatrixSubmissions = 0;
    unsigned int mCachedQuadIndexBatches = 0;
    unsigned int mPerfCriticalSamplesUs[kPerfWindowFrames] = {};
    unsigned int mPerfCriticalHistogram[kPerfHistogramBuckets] = {};
    unsigned int mPerfOverBudgetFrames = 0;
    unsigned int mPerfVsyncMisses = 0;
    unsigned int mPerfWindowSerial = 0;
    unsigned int mLastVblankCount = 0;
    bool mPerfGameplayActive = false;
    bool mPerfGameplayPending = false;
    int mPerfPendingStage = 0;
    int mPerfWindowStage = 0;
    int mPerfWindowState = 0;
#endif

#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    void InitializeUpperPortraitTelemetry()
    {
        std::memset(&mPortraitTelemetry, 0, sizeof(mPortraitTelemetry));
        mPortraitTelemetry.flags = TH07_PSP_PORTRAIT_CACHE_LEDGER_VALID;
        // This is a completion marker, not the six-slot capacity mask. It is
        // committed only after FACE_STAGE reports its actual child count.
        mPortraitTelemetry.required_mask = 0;
        for (unsigned int i = 0; i < TH07_PSP_PORTRAIT_SLOT_COUNT; ++i)
        {
            const auto role = static_cast<Th07PspPortraitTextureRole>(i + 1u);
            mPortraitTelemetry.slots[i].role = static_cast<unsigned int>(role);
            mPortraitTelemetry.slots[i].texture_slot = PortraitTextureSlot(role);
        }
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();
    }

    void PublishUpperPortraitTelemetry()
    {
        if (!mPortraitTelemetryDirty)
            return;
        ++mPortraitTelemetry.cache_generation;
        mPortraitTelemetry.stage = static_cast<unsigned int>(g_GameManager.currentStage);
        mPortraitTelemetry.live_bytes =
            mUpperPortraitUsedPages * kUpperPortraitPageBytes;
        mPortraitTelemetry.fallback_count = mUpperPortraitFallbacks;
        mPortraitTelemetry.migration_count = mUpperPortraitMigrations;
        mPortraitTelemetry.allocation_failure_count =
            mUpperPortraitAllocationFailures;
        mPortraitTelemetry.invariant_failure_count =
            mUpperPortraitInvariantFailures;
        PublishPortraitSnapshotSeqlock(mPortraitTelemetry);
        mPortraitTelemetryDirty = false;
    }

    [[noreturn]] void UpperPortraitInvariantFailure(const char *reason)
    {
        ++mUpperPortraitInvariantFailures;
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();
        th07_psp_ge4_fail_closed(reason);
        __builtin_unreachable();
    }

    void MarkUpperPortraitFallback()
    {
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();
    }

    uintptr_t UpperPortraitStagingRawAddress() const
    {
        return reinterpret_cast<uintptr_t>(sceGeEdramGetAddr()) +
               kPortraitStagingOffset;
    }

    void *UpperPortraitStagingCpuAddress() const
    {
        return reinterpret_cast<void *>(0x40000000u |
                                        UpperPortraitStagingRawAddress());
    }

    static unsigned int UpperPortraitWordHash(const volatile unsigned int *words,
                                              unsigned int bytes)
    {
        unsigned int hash = 2166136261u;
        for (unsigned int i = 0; i < bytes / sizeof(unsigned int); ++i)
        {
            hash ^= words[i];
            hash *= 16777619u;
            hash ^= hash >> 13;
        }
        return hash;
    }

    void PoisonUpperPortraitStaging(unsigned int bytes, unsigned int salt)
    {
        auto *words = static_cast<volatile unsigned int *>(
            UpperPortraitStagingCpuAddress());
        for (unsigned int i = 0; i < bytes / sizeof(unsigned int); ++i)
            words[i] = 0xa5a50000u ^ salt ^ (i * 0x9e3779b9u);
        // The staging alias is uncached, but MIPS may still leave stores
        // pending when GE starts reading the raw lower-eDRAM address.
        asm volatile("sync" ::: "memory");
    }

    void CopyUpperPortraitImageAndWait(uintptr_t sourceRaw, uintptr_t destinationRaw,
                                       unsigned int width, unsigned int height,
                                       const char *failureReason)
    {
        if (!mListOpen || width == 0 || height == 0 || width > 512u ||
            height > 512u)
        {
            UpperPortraitInvariantFailure(failureReason);
        }
        // A delayed sprite command logically precedes this transfer and must be
        // emitted before the copy is appended to the current list.
        FlushDeferredSpriteDraw();
        // Source and destination have identical PSM, stride and full extents.
        // CopyImage therefore preserves every backing byte, including the
        // established static-texture swizzle layout.
        sceGuCopyImage(GU_PSM_4444, 0, 0, static_cast<int>(width),
                       static_cast<int>(height), static_cast<int>(width),
                       reinterpret_cast<void *>(sourceRaw), 0, 0,
                       static_cast<int>(width),
                       reinterpret_cast<void *>(destinationRaw));
        sceGuTexSync();
        const int finishResult = sceGuFinish();
        mListOpen = false;
        const int syncResult = sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        ReleaseMeRenderGeTokenAfterSync();
#endif
        const int geState = sceGeDrawSync(1);
        if (finishResult < 0 || syncResult < 0 || geState != PSP_GE_LIST_DONE)
        {
            UpperPortraitInvariantFailure(failureReason);
        }
        // Order a completed GE write before any subsequent CPU read through
        // the uncached staging alias.
        asm volatile("sync" ::: "memory");
        StartList();
    }

    void InitializeUpperPortraitPool()
    {
        mUpperPortraitPoolBase = 0;
        mUpperPortraitPoolPages = 0;
        mUpperPortraitUsedPages = 0;
        mUpperPortraitPeakPages = 0;
        mUpperPortraitActivePlayers = 0;
        mUpperPortraitActiveStages = 0;
        std::memset(mUpperPortraitPageBits, 0, sizeof(mUpperPortraitPageBits));

        const uintptr_t edram = reinterpret_cast<uintptr_t>(sceGeEdramGetAddr());
        if (!th07_psp_ge4_active() || sceGeEdramGetSize() != 4u * 1024u * 1024u ||
            edram != 0x04000000u)
        {
            UpperPortraitInvariantFailure("portrait-pool-runtime-gate");
        }

        mUpperPortraitPoolBase = edram + kUpperPortraitPoolOffset;
        mUpperPortraitPoolPages = kUpperPortraitPoolPages;
        if (mUpperPortraitPoolBase != kUpperPortraitRawBase)
            UpperPortraitInvariantFailure("portrait-pool-raw-base");
        mPortraitTelemetry.flags |= TH07_PSP_PORTRAIT_CACHE_POOL_INITIALIZED;
        mPortraitTelemetry.pool_raw_base = kUpperPortraitRawBase;
        mPortraitTelemetry.pool_bytes = kUpperPortraitPoolBytes;
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();
        th07_psp_boot_note("GE portrait pool 2048K raw04200000 upper-only");
    }

    bool UpperPortraitPageUsed(unsigned int page) const
    {
        return (mUpperPortraitPageBits[page / 32u] &
                (1u << (page & 31u))) != 0;
    }

    void SetUpperPortraitPageUsed(unsigned int page, bool used)
    {
        const unsigned int mask = 1u << (page & 31u);
        if (used)
            mUpperPortraitPageBits[page / 32u] |= mask;
        else
            mUpperPortraitPageBits[page / 32u] &= ~mask;
    }

    void *AcquireUpperPortraitPixels(unsigned int bytes, GuTexture &texture)
    {
        const bool player = IsPlayerPortraitRole(texture.portraitRole);
        const bool stage = IsStagePortraitRole(texture.portraitRole);
        if (!mUpperPortraitPoolBase ||
            !PortraitRoleMatchesSlot(texture.portraitRole,
                                     texture.portraitTextureSlot) ||
            (player && (bytes != kPlayerPortraitAtlasBytes ||
                        mUpperPortraitActivePlayers >= kMaxPlayerPortraitAtlases)) ||
            (stage && (bytes != kStagePortraitAtlasBytes ||
                       mUpperPortraitActiveStages >= kMaxStagePortraitAtlases)) ||
            (!player && !stage))
        {
            ++mUpperPortraitAllocationFailures;
            mPortraitTelemetryDirty = true;
            PublishUpperPortraitTelemetry();
            return nullptr;
        }
        const unsigned int slot = PortraitSlotIndex(texture.portraitRole);
        const unsigned int slotMask = 1u << slot;
        if ((mPortraitTelemetry.owned_mask & slotMask) != 0)
        {
            ++mUpperPortraitAllocationFailures;
            mPortraitTelemetryDirty = true;
            PublishUpperPortraitTelemetry();
            return nullptr;
        }

        const unsigned int pageCount =
            (bytes + kUpperPortraitPageBytes - 1u) / kUpperPortraitPageBytes;
        if (pageCount == 0 || pageCount > mUpperPortraitPoolPages)
        {
            ++mUpperPortraitAllocationFailures;
            mPortraitTelemetryDirty = true;
            PublishUpperPortraitTelemetry();
            return nullptr;
        }

        for (unsigned int first = 0;
             first + pageCount <= mUpperPortraitPoolPages; ++first)
        {
            unsigned int page = 0;
            for (; page < pageCount; ++page)
            {
                if (UpperPortraitPageUsed(first + page))
                {
                    first += page;
                    break;
                }
            }
            if (page != pageCount)
                continue;

            for (page = 0; page < pageCount; ++page)
                SetUpperPortraitPageUsed(first + page, true);
            texture.upperPortraitOwned = true;
            texture.upperPortraitVerified = false;
            texture.upperPortraitFirstPage = static_cast<unsigned short>(first);
            texture.upperPortraitPageCount = static_cast<unsigned short>(pageCount);
            texture.allocationBytes = pageCount * kUpperPortraitPageBytes;
            mUpperPortraitUsedPages += pageCount;
            mUpperPortraitPeakPages =
                std::max(mUpperPortraitPeakPages, mUpperPortraitUsedPages);
            if (player)
            {
                ++mUpperPortraitActivePlayers;
                ++mUpperPortraitPlayerAllocations;
            }
            else
            {
                ++mUpperPortraitActiveStages;
                ++mUpperPortraitStageAllocations;
            }
            Th07PspPortraitSlotSnapshot &telemetry =
                mPortraitTelemetry.slots[slot];
            telemetry.raw_address = static_cast<unsigned int>(
                mUpperPortraitPoolBase + first * kUpperPortraitPageBytes);
            telemetry.allocation_bytes = texture.allocationBytes;
            telemetry.width = texture.storageWidth;
            telemetry.height = texture.storageHeight;
            telemetry.psm = static_cast<unsigned int>(texture.psm);
            telemetry.source_hash = 0;
            telemetry.readback_hash = 0;
            telemetry.upload_generation = 0;
            telemetry.draw_count = 0;
            mPortraitTelemetry.owned_mask |= slotMask;
            mPortraitTelemetry.verified_mask &= ~slotMask;
            mPortraitTelemetry.sampled_mask &= ~slotMask;
            mPortraitTelemetryDirty = true;
            PublishUpperPortraitTelemetry();
            th07_psp_boot_notef("GE portrait role%u upper %uK used%uK",
                                static_cast<unsigned int>(texture.portraitRole),
                                bytes / 1024u,
                                mUpperPortraitUsedPages *
                                    kUpperPortraitPageBytes / 1024u);
            return reinterpret_cast<void *>(
                mUpperPortraitPoolBase + first * kUpperPortraitPageBytes);
        }
        ++mUpperPortraitAllocationFailures;
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();
        return nullptr;
    }

    void CompleteUpperPortraitUpload(GuTexture &texture, void *upperDestination)
    {
        const unsigned int bytes = texture.bytes;
        const uintptr_t upperRaw = reinterpret_cast<uintptr_t>(upperDestination);
        const uintptr_t stagingRaw = UpperPortraitStagingRawAddress();
        const auto *stagingWords = static_cast<const volatile unsigned int *>(
            UpperPortraitStagingCpuAddress());
        if (!texture.upperPortraitOwned || texture.upperPortraitVerified ||
            texture.pixels != UpperPortraitStagingCpuAddress() ||
            !PortraitRoleMatchesSlot(texture.portraitRole,
                                     texture.portraitTextureSlot) ||
            bytes == 0 || bytes > kPortraitStagingBytes ||
            texture.psm != GU_PSM_4444 ||
            texture.storageWidth * texture.storageHeight * 2u != bytes ||
            upperRaw < kUpperPortraitRawBase ||
            upperRaw + bytes > kUpperPortraitRawBase + kUpperPortraitPoolBytes)
        {
            UpperPortraitInvariantFailure("portrait-upload-precondition");
        }

        // SetTextureImage has just finished CPU conversion into the uncached
        // staging alias. Make all stores visible before hashing and before GE
        // consumes the corresponding raw lower-eDRAM address.
        asm volatile("sync" ::: "memory");
        const unsigned int sourceHash = UpperPortraitWordHash(stagingWords, bytes);
        CopyUpperPortraitImageAndWait(stagingRaw, upperRaw, texture.storageWidth,
                                      texture.storageHeight,
                                      "portrait-upload-ge-state");
        PoisonUpperPortraitStaging(bytes, sourceHash ^ mUpperPortraitUploadGeneration);
        CopyUpperPortraitImageAndWait(upperRaw, stagingRaw, texture.storageWidth,
                                      texture.storageHeight,
                                      "portrait-readback-ge-state");
        const unsigned int readbackHash = UpperPortraitWordHash(stagingWords, bytes);
        if (sourceHash != readbackHash)
            UpperPortraitInvariantFailure("portrait-readback-hash");

        texture.pixels = upperDestination;
        texture.upperPortraitVerified = true;
        texture.upperPortraitUploadGeneration = ++mUpperPortraitUploadGeneration;
        const unsigned int slot = PortraitSlotIndex(texture.portraitRole);
        const unsigned int slotMask = 1u << slot;
        Th07PspPortraitSlotSnapshot &telemetry = mPortraitTelemetry.slots[slot];
        telemetry.source_hash = sourceHash;
        telemetry.readback_hash = readbackHash;
        telemetry.upload_generation = texture.upperPortraitUploadGeneration;
        mPortraitTelemetry.verified_mask |= slotMask;
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();
    }

    void ValidateUpperPortraitAllocation(const GuTexture &texture)
    {
        if (!PortraitRoleMatchesSlot(texture.portraitRole,
                                     texture.portraitTextureSlot))
        {
            UpperPortraitInvariantFailure("portrait-role-slot-invariant");
        }
        const unsigned int first = texture.upperPortraitFirstPage;
        const unsigned int count = texture.upperPortraitPageCount;
        const bool player = IsPlayerPortraitRole(texture.portraitRole);
        const bool stage = IsStagePortraitRole(texture.portraitRole);
        const unsigned int expectedBytes =
            player ? kPlayerPortraitAtlasBytes
                   : stage ? kStagePortraitAtlasBytes : 0u;
        bool pagesOwned = count != 0 && first + count <= mUpperPortraitPoolPages;
        for (unsigned int page = 0; pagesOwned && page < count; ++page)
            pagesOwned = UpperPortraitPageUsed(first + page);
        const uintptr_t expectedPixels =
            mUpperPortraitPoolBase + first * kUpperPortraitPageBytes;
        const unsigned int slot = PortraitSlotIndex(texture.portraitRole);
        const unsigned int slotMask = 1u << slot;
        const Th07PspPortraitSlotSnapshot &telemetry =
            mPortraitTelemetry.slots[slot];
        if (!texture.upperPortraitOwned || !texture.upperPortraitVerified ||
            !mUpperPortraitPoolBase || !pagesOwned ||
            reinterpret_cast<uintptr_t>(texture.pixels) != expectedPixels ||
            texture.allocationBytes != count * kUpperPortraitPageBytes ||
            texture.bytes != texture.allocationBytes ||
            texture.bytes != expectedBytes ||
            count > mUpperPortraitUsedPages || (!player && !stage) ||
            (player && mUpperPortraitActivePlayers == 0) ||
            (stage && mUpperPortraitActiveStages == 0) ||
            (mPortraitTelemetry.owned_mask & slotMask) == 0 ||
            (mPortraitTelemetry.verified_mask & slotMask) == 0 ||
            telemetry.raw_address != expectedPixels ||
            telemetry.allocation_bytes != texture.allocationBytes ||
            telemetry.upload_generation != texture.upperPortraitUploadGeneration ||
            telemetry.source_hash != telemetry.readback_hash)
        {
            // Do not clear or repair metadata: the bridge must retain the 4 MiB
            // aperture and power lock until a physical cold-off.
            UpperPortraitInvariantFailure("portrait-allocation-invariant");
        }
    }

    void ReleaseUpperPortraitAllocation(GuTexture &texture)
    {
        if (!texture.upperPortraitOwned)
            return;
        ValidateUpperPortraitAllocation(texture);
        const unsigned int first = texture.upperPortraitFirstPage;
        const unsigned int count = texture.upperPortraitPageCount;
        const bool player = IsPlayerPortraitRole(texture.portraitRole);
        const unsigned int slot = PortraitSlotIndex(texture.portraitRole);
        const unsigned int slotMask = 1u << slot;
        // Invalidate the committed stage set before publishing any partial
        // release. The next FACE_STAGE load will atomically commit its own
        // exact child prefix once every upload/readback has succeeded.
        mPortraitTelemetry.required_mask = 0;
        for (unsigned int page = 0; page < count; ++page)
            SetUpperPortraitPageUsed(first + page, false);
        mUpperPortraitUsedPages -= count;
        if (player)
            --mUpperPortraitActivePlayers;
        else
            --mUpperPortraitActiveStages;
        texture.pixels = nullptr;
        texture.allocationBytes = 0;
        texture.upperPortraitOwned = false;
        texture.upperPortraitVerified = false;
        texture.upperPortraitFirstPage = 0;
        texture.upperPortraitPageCount = 0;
        texture.upperPortraitUploadGeneration = 0;
        Th07PspPortraitSlotSnapshot &telemetry = mPortraitTelemetry.slots[slot];
        const unsigned int role = telemetry.role;
        const unsigned int textureSlot = telemetry.texture_slot;
        telemetry = Th07PspPortraitSlotSnapshot{};
        telemetry.role = role;
        telemetry.texture_slot = textureSlot;
        mPortraitTelemetry.owned_mask &= ~slotMask;
        mPortraitTelemetry.verified_mask &= ~slotMask;
        mPortraitTelemetry.sampled_mask &= ~slotMask;
        mPortraitTelemetryDirty = true;
        PublishUpperPortraitTelemetry();
    }

    bool MoveUpperPortraitToMain(GuTexture &texture)
    {
        if (!texture.upperPortraitOwned)
            return true;
        ValidateUpperPortraitAllocation(texture);
        unsigned int allocationBytes = 0;
        void *pixels = AcquireTexturePixels(texture.bytes, &allocationBytes);
        if (!pixels)
        {
            ++mUpperPortraitAllocationFailures;
            mPortraitTelemetryDirty = true;
            PublishUpperPortraitTelemetry();
            th07_psp_boot_note("GE portrait migration allocation failed");
            mError = true;
            return false;
        }
        const unsigned int expectedHash =
            mPortraitTelemetry.slots[PortraitSlotIndex(texture.portraitRole)]
                .source_hash;
        PoisonUpperPortraitStaging(texture.bytes,
                                   expectedHash ^ mUpperPortraitMigrations);
        CopyUpperPortraitImageAndWait(
            reinterpret_cast<uintptr_t>(texture.pixels),
            UpperPortraitStagingRawAddress(), texture.storageWidth,
            texture.storageHeight, "portrait-migration-readback");
        const auto *stagingWords = static_cast<const volatile unsigned int *>(
            UpperPortraitStagingCpuAddress());
        if (UpperPortraitWordHash(stagingWords, texture.bytes) != expectedHash)
            UpperPortraitInvariantFailure("portrait-migration-hash");
        std::memcpy(pixels, UpperPortraitStagingCpuAddress(), texture.bytes);
        sceKernelDcacheWritebackRange(pixels, texture.bytes);
        ++mUpperPortraitMigrations;
        ReleaseUpperPortraitAllocation(texture);
        texture.pixels = pixels;
        texture.allocationBytes = allocationBytes;
        sceGuTexFlush();
        return true;
    }

    void NoteBoundUpperPortraitDraw()
    {
        if (mBoundTexture == 0 || mBoundTexture >= kMaxTextures)
            return;
        GuTexture &texture = mTextures[mBoundTexture];
        if (!texture.upperPortraitOwned || !texture.upperPortraitVerified)
            return;
        if (!PortraitRoleMatchesSlot(texture.portraitRole,
                                     texture.portraitTextureSlot))
            UpperPortraitInvariantFailure("portrait-draw-role-slot");
        if (mColorOpRgb == COLOR_OP_DISABLE &&
            mColorOpAlpha == COLOR_OP_DISABLE)
            return;
        const unsigned int slot = PortraitSlotIndex(texture.portraitRole);
        ++mPortraitTelemetry.slots[slot].draw_count;
        mPortraitTelemetry.sampled_mask |= 1u << slot;
        mPortraitTelemetryDirty = true;
    }
#endif

    void RecyclePixels(void *&pixels, unsigned int &allocationBytes)
    {
        if (!pixels)
        {
            allocationBytes = 0;
            return;
        }
        for (FreeTextureBlock &block : mFreeTextureBlocks)
        {
            if (!block.pixels)
            {
                block.pixels = pixels;
                block.bytes = allocationBytes;
                pixels = nullptr;
                allocationBytes = 0;
                return;
            }
        }
        std::free(pixels);
        pixels = nullptr;
        allocationBytes = 0;
    }

    void RecycleTexturePixels(GuTexture &texture)
    {
        if (!texture.pixels || texture.borrowedSurfaceCache)
        {
            return;
        }
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        if (texture.upperPortraitOwned)
        {
            ReleaseUpperPortraitAllocation(texture);
            texture.bytes = 0;
            return;
        }
#endif
        RecyclePixels(texture.pixels, texture.allocationBytes);
        texture.bytes = 0;
    }

    void *AcquireTexturePixels(unsigned int bytes, unsigned int *allocationBytes)
    {
        int best = -1;
        for (unsigned int i = 0; i < kMaxTextures; ++i)
        {
            if (!mFreeTextureBlocks[i].pixels || mFreeTextureBlocks[i].bytes < bytes)
            {
                continue;
            }
            if (best < 0 || mFreeTextureBlocks[i].bytes < mFreeTextureBlocks[best].bytes)
            {
                best = static_cast<int>(i);
            }
        }
        if (best >= 0)
        {
            void *pixels = mFreeTextureBlocks[best].pixels;
            *allocationBytes = mFreeTextureBlocks[best].bytes;
            mFreeTextureBlocks[best] = FreeTextureBlock{};
            return pixels;
        }

        void *pixels = memalign(16, bytes);
        *allocationBytes = pixels ? bytes : 0;
        return pixels;
    }

    unsigned char *DeferredSpriteArenaBase() const
    {
        if (!g_AnmManager)
        {
            return nullptr;
        }
        auto *end = reinterpret_cast<unsigned char *>(
            g_AnmManager->spriteVertexBuffer +
            sizeof(g_AnmManager->spriteVertexBuffer) /
                sizeof(g_AnmManager->spriteVertexBuffer[0]));
        return end - kDeferredSpriteArenaBytes;
    }

    void FlushDeferredSpriteDraw()
    {
        if (!mDeferredSpriteVertices || mDeferredSpriteVertexCount == 0)
        {
            return;
        }

#if defined(TH07_PSP_PERF_M3)
        const int m3DeferredOrigin = gPerfM3DeferredBatchOrigin;
        const bool m3StandaloneBackend = !gPerfM3WrapperActive;
        Th07PspM3EmitterBackendBegin();
        const unsigned long long m3FlushStartUs = sceKernelGetSystemTimeWide();
#endif

#if defined(TH07_PSP_PERF_M2)
        PerfInternalScope flushScope(TH07_PSP_PERF_INTERNAL_DEFERRED_FLUSH);
#endif

        const unsigned int bytes =
            mDeferredSpriteVertexCount * sizeof(GuVertexTexColor);
#if defined(TH07_PSP_PERF_M2)
        PerfInternalScope dcacheScope(TH07_PSP_PERF_INTERNAL_DCACHE);
#endif
#if defined(TH07_PSP_PERF_M3)
        const unsigned long long dcacheStartUs = sceKernelGetSystemTimeWide();
#endif
        sceKernelDcacheWritebackRange(mDeferredSpriteVertices, bytes);
#if defined(TH07_PSP_PERF_M2)
        dcacheScope.End();
#endif
#if defined(TH07_PSP_PERF_M3)
        const unsigned long long m3DcacheUs =
            sceKernelGetSystemTimeWide() - dcacheStartUs;
        if (gPerfM3BulletLoopActive)
        {
            gPerfM3DcacheUs += m3DcacheUs;
            ++gPerfM3DcacheCalls;
        }
        if (gPerfM3WrapperActive)
        {
            gPerfM3WrapperDcacheUs += m3DcacheUs;
        }
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        NoteBoundUpperPortraitDraw();
#endif
        sceGuDrawArray(GU_SPRITES,
                       GU_TEXTURE_32BITF | GU_COLOR_8888 |
                           GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                       static_cast<int>(mDeferredSpriteVertexCount), nullptr,
                       mDeferredSpriteVertices);
#if defined(TH07_PSP_PERF_DETAIL)
        ++mDraws;
        mInputVertices += mDeferredSpriteInputVertexCount;
        mVertices += mDeferredSpriteVertexCount;
        ++mCachedQuadIndexBatches;
#endif
        mDeferredSpriteVertices = nullptr;
        mDeferredSpriteVertexCount = 0;
        mDeferredSpriteInputVertexCount = 0;
        if (mFogEnabled)
        {
            sceGuEnable(GU_FOG);
        }
#if defined(TH07_PSP_PERF_M3)
        const unsigned long long m3FlushUs =
            sceKernelGetSystemTimeWide() - m3FlushStartUs;
        if (gPerfM3BulletLoopActive && m3StandaloneBackend)
        {
            // State synchronization can flush an older deferred group before
            // Th07PspDrawSprite* is entered.  Count that exclusive backend
            // interval once and exclude it from the sampled emitter phase.
            gPerfM3SpriteBackendUs += m3FlushUs;
            ++gPerfM3SpriteBackendCalls;
        }
        PerfM3RecordDeferredFlush(m3DeferredOrigin, m3FlushUs, m3DcacheUs);
        gPerfM3DeferredBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
        gPerfM3CarryInPending = false;
        Th07PspM3EmitterBackendEnd();
#endif
    }

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    void ReleaseMeRenderGeTokenAfterSync()
    {
        if (!mMeRenderGeTokenPending || mMeRenderGeSubmissionOpen)
        {
            return;
        }
        const Th07PspMeRenderStreamToken token = mMeRenderGeToken;
        mMeRenderGeTokenPending = false;
        mMeRenderGeToken = Th07PspMeRenderStreamToken{};
        if (th07_psp_me_render_stream_release_after_ge(&token) != 1)
        {
            Th07PspMeRenderGeReleaseFault();
        }
    }
#endif

    void StartList()
    {
#if defined(TH07_PSP_PERF_M3)
        if (gPerfM3DeferredBatchOrigin != TH07_PSP_PERF_M3_BATCH_NONE)
        {
            ++gPerfM3TransferUnresolved;
        }
        gPerfM3DeferredBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
#endif
        mDeferredSpriteVertices = nullptr;
        mDeferredSpriteVertexCount = 0;
        mDeferredSpriteInputVertexCount = 0;
        mDeferredSpriteArenaUsed = 0;
        sceGuStart(GU_DIRECT, gCommandList);
        mListOpen = true;
#if defined(TH07_PSP_PERF_DETAIL)
        ++mListsThisFrame;
#endif
        mAppliedMatrixMode = -1;
        // A fresh GE list inherits no matrix state from the previous list.
        mMatrixDirtyMask = kAll3dMatrixDirtyBits;
    }

    void SubmitAndRestart()
    {
        if (!mListOpen)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        const int listBytes = sceGuFinish();
        mListOpen = false;
#if defined(TH07_PSP_PERF_DETAIL)
        if (listBytes > 0)
        {
            mMaxListBytes = std::max(mMaxListBytes, static_cast<unsigned int>(listBytes));
        }
        const unsigned long long geStartUs = sceKernelGetSystemTimeWide();
#endif
        sceGuSync(0, 0);
#if defined(TH07_PSP_PERF_DETAIL)
        mFrameBlockingGeUs += sceKernelGetSystemTimeWide() - geStartUs;
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        ReleaseMeRenderGeTokenAfterSync();
#endif
        StartList();
    }

#if defined(TH07_PSP_PERF_DIAG)
  public:
    void PerfBeginGameplayWindow(int stage)
    {
        mPerfPendingStage = stage;
        mPerfGameplayPending = true;
    }

    void PerfFinalizeGameplayWindow()
    {
        mPerfGameplayPending = false;
#if defined(TH07_PSP_PERF_M3)
        if (mPerfGameplayActive && mPerfFrames != 0u)
        {
            // PSP renders before calc. If calc removes GameManager, that last
            // draw is never presented and therefore has no matching completed
            // frame sample. A partial M3 report would mix N completed frames
            // with N+1 draw callbacks. Keep only exact 120-frame windows.
            th07_psp_boot_notef("m3 partial omitted N%u", mPerfFrames);
        }
        ResetPerfWindowCounters();
#else
        if (mPerfGameplayActive && mPerfFrames != 0u)
        {
            ReportPerfWindow(mPerfLastGeEndUs ? mPerfLastGeEndUs
                                              : sceKernelGetSystemTimeWide());
        }
        else
        {
            ResetPerfWindowCounters();
        }
#endif
        mPerfGameplayActive = false;
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        gTh07PspPerfDenseSliceActive = 0;
#endif
    }

    void PerfPhaseGpuSync(int priority)
    {
        if (!mListOpen)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        const int listBytes = sceGuFinish();
        mListOpen = false;
        if (listBytes > 0)
        {
            mMaxListBytes = std::max(mMaxListBytes, static_cast<unsigned int>(listBytes));
        }
        const unsigned long long syncStartUs = sceKernelGetSystemTimeWide();
        sceGuSync(0, 0);
        const unsigned long long syncUs = sceKernelGetSystemTimeWide() - syncStartUs;
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        ReleaseMeRenderGeTokenAfterSync();
#endif
        if (priority >= 0 && priority < 18)
        {
            gPerfDrawJobGpuUs[priority] += syncUs;
        }
        StartList();
    }

#if defined(TH07_PSP_PERF_M3)
    void PerfSetM3BulletLoop(bool active)
    {
        if (active)
        {
            gPerfM3BulletLoopActive = true;
            gPerfM3CarryInPending =
                mDeferredSpriteVertices && mDeferredSpriteVertexCount != 0;
            if (gPerfM3CarryInPending)
            {
                const unsigned long long bytes =
                    mDeferredSpriteVertexCount * sizeof(GuVertexTexColor);
                gPerfM3CarryInBytes += bytes;
                if (gPerfM3DeferredBatchOrigin == TH07_PSP_PERF_M3_BATCH_MIXED ||
                    gPerfM3DeferredBatchOrigin == TH07_PSP_PERF_M3_BATCH_BULLET)
                {
                    ++gPerfM3TransferMixed;
                }
            }
            return;
        }

        if (mDeferredSpriteVertices && mDeferredSpriteVertexCount != 0)
        {
            const unsigned long long bytes =
                mDeferredSpriteVertexCount * sizeof(GuVertexTexColor);
            if (gPerfM3DeferredBatchOrigin == TH07_PSP_PERF_M3_BATCH_BULLET)
            {
                gPerfM3CarryOutBytes += bytes;
                gPerfM3PendingBytes += bytes;
            }
            else if (gPerfM3DeferredBatchOrigin == TH07_PSP_PERF_M3_BATCH_MIXED)
            {
                ++gPerfM3TransferMixed;
            }
        }
        gPerfM3BulletLoopActive = false;
        gPerfM3CarryInPending = false;
    }

    void PerfEndDrawOwner(int priority)
    {
        if (gPerfM3BulletLoopActive)
        {
            // BulletManager normally closes the boundary itself.  Reaching
            // the owner boundary while it is still armed means ownership is
            // incomplete; close safely and reject this window.
            ++gPerfM3TransferUnresolved;
            PerfSetM3BulletLoop(false);
        }
        if (priority == 10 &&
            gPerfM3DeferredBatchOrigin == TH07_PSP_PERF_M3_BATCH_MIXED)
        {
            ++gPerfM3TransferMixed;
        }
    }
#endif

  private:
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    void ConfigurePerfDenseSliceForNextWindow()
    {
        const unsigned int nextWindow = mPerfWindowSerial + 1u;
        const bool active = mPerfGameplayActive && mPerfWindowState == 2 &&
                            mPerfWindowStage == 6 && nextWindow >= 12u &&
                            nextWindow <= 15u;
        gTh07PspPerfDenseSliceActive = active ? 1 : 0;
        gPerfDenseTimerReadQ8 = active ? PerfDenseCalibrateTimerReadQ8() : 0ull;
    }
#endif

    void AccumulateAndReportPerf(unsigned long long finishEndUs,
                                 unsigned long long vblankEndUs,
                                 unsigned long long geEndUs,
                                 unsigned int vblankCount)
    {
        if (!mPerfGameplayActive)
        {
            return;
        }
        mPerfLastGeEndUs = geEndUs;
        if (mPerfStartUs == 0)
        {
            mPerfStartUs = mFrameStartUs ? mFrameStartUs : finishEndUs;
        }
        if (mFrameStartUs)
        {
            const unsigned long long cpuUs = finishEndUs - mFrameStartUs;
            const unsigned long long geTailUs = geEndUs - vblankEndUs;
            const unsigned long long criticalUs = cpuUs + geTailUs;
            mPerfCpuUs += cpuUs;
            mPerfGeUs += geTailUs;
#if defined(TH07_PSP_PERF_DETAIL)
            mPerfBlockingGeUs += mFrameBlockingGeUs;
#endif
            mPerfMaxFrameUs = std::max(mPerfMaxFrameUs, criticalUs);
            const unsigned int sampleUs = static_cast<unsigned int>(
                std::min<unsigned long long>(criticalUs, 0xffffffffull));
            mPerfCriticalSamplesUs[mPerfFrames] = sampleUs;
            unsigned int bucket = 0;
            while (bucket + 1u < kPerfHistogramBuckets &&
                   sampleUs >= kPerfHistogramLimitsUs[bucket])
            {
                ++bucket;
            }
            ++mPerfCriticalHistogram[bucket];
            if (sampleUs > kFrameBudgetUs)
            {
                ++mPerfOverBudgetFrames;
            }
            th07_usage_meter_frame(sampleUs); /* [FABLE] ACCEPT統計と同一値 */
        }
#if defined(TH07_PSP_PERF_DETAIL)
        // Blocking SubmitAndRestart time is already inside the CPU wall clock;
        // keep it as a reference counter and never add it to GE again.
        mPerfVblankUs += vblankEndUs - finishEndUs;
#endif
        if (mLastVblankCount != 0)
        {
            const unsigned int vblankDelta = vblankCount - mLastVblankCount;
            if (vblankDelta > 1u)
            {
                mPerfVsyncMisses += vblankDelta - 1u;
            }
        }
        mLastVblankCount = vblankCount;
#if defined(TH07_PSP_PERF_DETAIL)
        mPerfLists += mListsThisFrame;
        mPerfMaxBullets = std::max(mPerfMaxBullets,
                                   static_cast<unsigned int>(g_BulletManager.bulletCount));
        mPerfMaxEffects = std::max(mPerfMaxEffects,
                                   static_cast<unsigned int>(g_EffectManager.activeEffectsCount));
#endif
        ++mPerfFrames;
        if (mPerfFrames < kPerfWindowFrames)
        {
            return;
        }

        ReportPerfWindow(geEndUs);
    }

    void ReportPerfWindow(unsigned long long geEndUs)
    {
        if (mPerfFrames == 0u)
        {
            return;
        }

        th07_psp_perf_set_window_id(++mPerfWindowSerial);

#if defined(TH07_PSP_PERF_ATTRIB)
        const unsigned long long elapsedUs = geEndUs - mPerfStartUs;
        const unsigned int fps10 = elapsedUs
                                       ? static_cast<unsigned int>(mPerfFrames * 10000000ull /
                                                                   elapsedUs)
                                       : 0;
#endif
        const unsigned int cpu10 = static_cast<unsigned int>(mPerfCpuUs / mPerfFrames / 100u);
        const unsigned int ge10 = static_cast<unsigned int>(mPerfGeUs / mPerfFrames / 100u);
#if defined(TH07_PSP_PERF_ATTRIB)
        const unsigned int vb10 = static_cast<unsigned int>(mPerfVblankUs / mPerfFrames / 100u);
#endif
        const unsigned int max10 = static_cast<unsigned int>(mPerfMaxFrameUs / 100u);
#if defined(TH07_PSP_PERF_ATTRIB)
        const unsigned int blocking10 =
            static_cast<unsigned int>(mPerfBlockingGeUs / mPerfFrames / 100u);
#endif
        unsigned int sortedCriticalUs[kPerfWindowFrames];
        std::memcpy(sortedCriticalUs, mPerfCriticalSamplesUs,
                    mPerfFrames * sizeof(sortedCriticalUs[0]));
        std::sort(sortedCriticalUs, sortedCriticalUs + mPerfFrames);
        const unsigned int p99Index =
            (mPerfFrames * 99u + 99u) / 100u - 1u;
        const unsigned int p99Us = sortedCriticalUs[p99Index];
        const unsigned int p9910 = p99Us / 100u;
#if defined(TH07_PSP_PERF_ATTRIB)
        const unsigned int draws = (mDraws - mPerfStartDraws) / mPerfFrames;
        const unsigned int inputVertices =
            (mInputVertices - mPerfStartInputVertices) / mPerfFrames;
        const unsigned int vertices = (mVertices - mPerfStartVertices) / mPerfFrames;
        const unsigned int lists10 = mPerfLists * 10u / mPerfFrames;
        const unsigned int calc10 =
            static_cast<unsigned int>(gPerfCalcChainUs / mPerfFrames / 100u);
        const unsigned int draw10 =
            static_cast<unsigned int>(gPerfDrawChainUs / mPerfFrames / 100u);
        const unsigned int stage10 =
            static_cast<unsigned int>(gPerfStageDrawUs / mPerfFrames / 100u);
        const auto calcJob10 = [this](unsigned int priority) {
            return static_cast<unsigned int>(gPerfCalcJobUs[priority] / mPerfFrames / 100u);
        };
        const unsigned int menu10 = calcJob10(3);
        const unsigned int stageUpdate10 = calcJob10(7);
        const unsigned int player10 = calcJob10(8);
        const unsigned int enemy10 = calcJob10(10);
        const unsigned int effect10 = calcJob10(11);
        const unsigned int bulletItem10 = calcJob10(12);
        const unsigned int matrices10 = mMatrixSubmissions * 10u / mPerfFrames;
        const unsigned int cachedQuads10 = mCachedQuadIndexBatches * 10u / mPerfFrames;
        unsigned int meJobs = 0;
        unsigned int meFallbacks = 0;
        unsigned int meTimeouts = 0;
        unsigned int meMaxWaitUs = 0;
        th07_psp_me_audio_diag_window(&meJobs, &meFallbacks, &meTimeouts,
                                      &meMaxWaitUs);
        const struct mallinfo heap = mallinfo();
        char message[512];
        std::snprintf(message, sizeof(message),
                      "PERF S%d ST%d N%u B%d/%u E%d/%u %u.%uFPS CPU%u.%u GE%u.%u VB%u.%u "
                      "MAX%u.%u P99%u.%u OVR%u MISS%u BLK%u.%u "
                      "C%u.%u R%u.%u BG%u.%u D%u VI%u V%u L%u.%u M%u.%u Q%u.%u "
                      "J3%u.%u J7%u.%u J8%u.%u J10%u.%u J11%u.%u J12%u.%u "
                      "LK%u UP%lluK TC%u/%u HF%uK "
                      "ME%u SC%u TO%u MW%u",
                      mPerfWindowState, mPerfWindowStage, mPerfFrames,
                      g_BulletManager.bulletCount, mPerfMaxBullets,
                      g_EffectManager.activeEffectsCount, mPerfMaxEffects,
                      fps10 / 10, fps10 % 10,
                      cpu10 / 10, cpu10 % 10, ge10 / 10, ge10 % 10, vb10 / 10, vb10 % 10,
                      max10 / 10, max10 % 10, p9910 / 10, p9910 % 10,
                      mPerfOverBudgetFrames, mPerfVsyncMisses,
                      blocking10 / 10, blocking10 % 10,
                      calc10 / 10, calc10 % 10,
                      draw10 / 10, draw10 % 10, stage10 / 10, stage10 % 10,
                      draws, inputVertices, vertices, lists10 / 10, lists10 % 10,
                      matrices10 / 10, matrices10 % 10,
                      cachedQuads10 / 10, cachedQuads10 % 10,
                      menu10 / 10, menu10 % 10, stageUpdate10 / 10, stageUpdate10 % 10,
                      player10 / 10, player10 % 10, enemy10 / 10, enemy10 % 10,
                      effect10 / 10, effect10 % 10, bulletItem10 / 10,
                      bulletItem10 % 10,
                      mMaxListBytes / 1024u, mTextureUploadBytes / 1024u, mSurfaceCacheHits,
                      mSurfaceCacheMisses, static_cast<unsigned int>(heap.fordblks) / 1024u,
                      meJobs, meFallbacks, meTimeouts, meMaxWaitUs);
        th07_psp_perf_note(message);

        char histogramMessage[192];
        std::snprintf(histogramMessage, sizeof(histogramMessage),
                      "PERF HIST H0%u H1%u H2%u H3%u H4%u H5%u H6%u H7%u H8%u H9%u",
                      mPerfCriticalHistogram[0], mPerfCriticalHistogram[1],
                      mPerfCriticalHistogram[2], mPerfCriticalHistogram[3],
                      mPerfCriticalHistogram[4], mPerfCriticalHistogram[5],
                      mPerfCriticalHistogram[6], mPerfCriticalHistogram[7],
                      mPerfCriticalHistogram[8], mPerfCriticalHistogram[9]);
        th07_psp_perf_note(histogramMessage);

#if defined(TH07_PSP_PERF_M2)
        const auto drawJob10 = [this](unsigned int priority) {
            return static_cast<unsigned int>(gPerfDrawJobUs[priority] / mPerfFrames / 100u);
        };
        unsigned int bulletAxisEligible = 0;
        unsigned int bulletFallbackEligible = 0;
        unsigned int bulletCullRejects = 0;
        Th07PspTakeBulletDrawPerf(&bulletAxisEligible, &bulletFallbackEligible,
                                 &bulletCullRejects);
        const unsigned int bulletAxis10 = bulletAxisEligible * 10u / mPerfFrames;
        const unsigned int bulletFallback10 = bulletFallbackEligible * 10u / mPerfFrames;
        const unsigned int bulletCull10 = bulletCullRejects * 10u / mPerfFrames;
#if defined(TH07_PSP_ASCII_POPUP_BATCH)
        unsigned int popupBatchCalls = 0;
        unsigned int popupBatchDigits = 0;
        unsigned int popupBatchFallbacks = 0;
        Th07PspTakeAsciiPopupBatchPerf(&popupBatchCalls, &popupBatchDigits,
                                       &popupBatchFallbacks);
        if (popupBatchCalls != 0u || popupBatchDigits != 0u || popupBatchFallbacks != 0u)
        {
            char popupBatchMessage[96];
            std::snprintf(popupBatchMessage, sizeof(popupBatchMessage),
                          "PERF APB CALL%u DIG%u FB%u", popupBatchCalls,
                          popupBatchDigits, popupBatchFallbacks);
            th07_psp_perf_note(popupBatchMessage);
        }
#endif
        unsigned long long drawJobsUs = 0;
        for (const unsigned long long jobUs : gPerfDrawJobUs)
        {
            drawJobsUs += jobUs;
        }
#if defined(TH07_PSP_PERF_M2)
        unsigned long long drawOwnerUs = 0;
        unsigned int maxOwnerIndex = ~0u;
        for (unsigned int ownerIndex = 0; ownerIndex < gPerfDrawOwnerCount; ++ownerIndex)
        {
            PerfDrawOwnerSlot &owner = gPerfDrawOwners[ownerIndex];
            drawOwnerUs += owner.elapsedUs;
            if (owner.calls != 0u &&
                (maxOwnerIndex == ~0u ||
                 owner.elapsedUs > gPerfDrawOwners[maxOwnerIndex].elapsedUs))
            {
                maxOwnerIndex = ownerIndex;
            }
            if (!owner.announced)
            {
                char ownerMapMessage[80];
                std::snprintf(ownerMapMessage, sizeof(ownerMapMessage),
                              "PERF OWNMAP I%u P%02d A%08lx", ownerIndex,
                              owner.priority, owner.callbackAddress);
                th07_psp_perf_note(ownerMapMessage);
                owner.announced = true;
            }
        }
        const unsigned long long ownerClosureErrorUs = drawJobsUs >= drawOwnerUs
                                                           ? drawJobsUs - drawOwnerUs
                                                           : drawOwnerUs - drawJobsUs;
        char ownerMessage[512];
        unsigned int ownerMessageUsed = static_cast<unsigned int>(
            std::snprintf(ownerMessage, sizeof(ownerMessage), "PERF OWN"));
        bool hasAmbiguousOwner = false;
        for (unsigned int ownerIndex = 0; ownerIndex < gPerfDrawOwnerCount; ++ownerIndex)
        {
            const PerfDrawOwnerSlot &owner = gPerfDrawOwners[ownerIndex];
            if (owner.calls == 0)
            {
                continue;
            }
            unsigned int samePriorityOwners = 0;
            for (unsigned int other = 0; other < gPerfDrawOwnerCount; ++other)
            {
                if (gPerfDrawOwners[other].priority == owner.priority)
                {
                    ++samePriorityOwners;
                }
            }
            if (samePriorityOwners < 2u)
            {
                continue;
            }
            hasAmbiguousOwner = true;
            const unsigned int owner10 = static_cast<unsigned int>(
                owner.elapsedUs / mPerfFrames / 100u);
            const int appended = std::snprintf(
                ownerMessage + std::min<unsigned int>(ownerMessageUsed,
                                                       sizeof(ownerMessage) - 1u),
                ownerMessageUsed < sizeof(ownerMessage)
                    ? sizeof(ownerMessage) - ownerMessageUsed
                    : 0u,
                " I%u=%u.%u/%u", ownerIndex, owner10 / 10, owner10 % 10,
                owner.calls);
            if (appended < 0 || ownerMessageUsed + static_cast<unsigned int>(appended) >=
                                    sizeof(ownerMessage))
            {
                ++gPerfDrawOwnerLogTruncated;
                break;
            }
            ownerMessageUsed += static_cast<unsigned int>(appended);
        }
        if (hasAmbiguousOwner && gPerfDrawOwnerLogTruncated == 0u)
        {
            th07_psp_perf_note(ownerMessage);
        }
#endif
        const unsigned long long accountedDrawUs = drawJobsUs + gPerfDrawChainOverheadUs;
        const unsigned long long closureErrorUs = gPerfDrawChainUs >= accountedDrawUs
                                                      ? gPerfDrawChainUs - accountedDrawUs
                                                      : accountedDrawUs - gPerfDrawChainUs;
        // These counters cover the whole window.  The fixed side of the
        // closure gate is 0.2 ms *per frame*, not 0.2 ms per 120-frame
        // window; keep the raw total and the displayed per-frame limit in
        // the same unit system.
        const unsigned long long closureLimitUs = std::max<unsigned long long>(
            200ull * mPerfFrames, gPerfDrawChainUs / 50ull);
        const unsigned int drawJobs10 =
            static_cast<unsigned int>(drawJobsUs / mPerfFrames / 100u);
        const unsigned int overhead10 = static_cast<unsigned int>(
            gPerfDrawChainOverheadUs / mPerfFrames / 100u);
        const unsigned int closureErrorPerFrameUs =
            static_cast<unsigned int>(closureErrorUs / mPerfFrames);
        const unsigned int closureLimitPerFrameUs =
            static_cast<unsigned int>(closureLimitUs / mPerfFrames);
        char drawMessage[512];
        std::snprintf(drawMessage, sizeof(drawMessage),
                      "PERF DRAW P00MM%u.%u P01ED%u.%u P02GM%u.%u P03SH%u.%u "
                      "P04SL%u.%u P05EH%u.%u P06PH%u.%u P07EL%u.%u P08PL%u.%u "
                      "P09FX%u.%u P10BU%u.%u P11AP%u.%u P12GUI%u.%u P13RS%u.%u "
                      "P14RP%u.%u P15SV%u.%u P16AM%u.%u P17SE%u.%u "
                      "SUM%u.%u R%u.%u OH%u.%u ERR%u LIM%u OOR%u "
                      "OE%u OWNOV%u OWNTR%u G%u "
                      "BAX%u.%u BFB%u.%u BCU%u.%u",
                      drawJob10(0) / 10, drawJob10(0) % 10,
                      drawJob10(1) / 10, drawJob10(1) % 10,
                      drawJob10(2) / 10, drawJob10(2) % 10,
                      drawJob10(3) / 10, drawJob10(3) % 10,
                      drawJob10(4) / 10, drawJob10(4) % 10,
                      drawJob10(5) / 10, drawJob10(5) % 10,
                      drawJob10(6) / 10, drawJob10(6) % 10,
                      drawJob10(7) / 10, drawJob10(7) % 10,
                      drawJob10(8) / 10, drawJob10(8) % 10,
                      drawJob10(9) / 10, drawJob10(9) % 10,
                      drawJob10(10) / 10, drawJob10(10) % 10,
                      drawJob10(11) / 10, drawJob10(11) % 10,
                      drawJob10(12) / 10, drawJob10(12) % 10,
                      drawJob10(13) / 10, drawJob10(13) % 10,
                      drawJob10(14) / 10, drawJob10(14) % 10,
                      drawJob10(15) / 10, drawJob10(15) % 10,
                      drawJob10(16) / 10, drawJob10(16) % 10,
                      drawJob10(17) / 10, drawJob10(17) % 10,
                      drawJobs10 / 10, drawJobs10 % 10,
                      draw10 / 10, draw10 % 10,
                      overhead10 / 10, overhead10 % 10,
                      closureErrorPerFrameUs, closureLimitPerFrameUs,
                      gPerfDrawOutOfRange,
                      static_cast<unsigned int>(ownerClosureErrorUs),
                      gPerfDrawOwnerOverflow, gPerfDrawOwnerLogTruncated,
                      closureErrorUs <= closureLimitUs && ownerClosureErrorUs == 0u &&
                              gPerfDrawOutOfRange == 0u &&
                              gPerfDrawOwnerOverflow == 0u &&
                              gPerfDrawOwnerLogTruncated == 0u
                          ? 1u
                          : 0u,
                      bulletAxis10 / 10, bulletAxis10 % 10,
                      bulletFallback10 / 10, bulletFallback10 % 10,
                      bulletCull10 / 10, bulletCull10 % 10);
        th07_psp_perf_note(drawMessage);

        char internalMessage[320];
        if (maxOwnerIndex != ~0u)
        {
            const PerfDrawOwnerSlot &owner = gPerfDrawOwners[maxOwnerIndex];
            unsigned long long categorizedUs = 0;
            for (unsigned int category = 0; category < kPerfInternalCategoryCount;
                 ++category)
            {
                categorizedUs += owner.internalUs[category];
            }
            const unsigned long long otherUs = owner.elapsedUs >= categorizedUs
                                                       ? owner.elapsedUs - categorizedUs
                                                       : 0ull;
            const unsigned long long internalSumUs = categorizedUs + otherUs;
            const unsigned long long internalErrorUs =
                owner.elapsedUs >= internalSumUs ? owner.elapsedUs - internalSumUs
                                                 : internalSumUs - owner.elapsedUs;
            const unsigned long long internalLimitUs = std::max<unsigned long long>(
                200ull * mPerfFrames, owner.elapsedUs / 50ull);
            std::snprintf(
                internalMessage, sizeof(internalMessage),
                "PERF M2I I%u P%02d TOTUS%llu PKUS%llu MXUS%llu STUS%llu "
                "FLUS%llu DCUS%llu OTUS%llu SUMUS%llu ERR%llu LIM%llu MM%u G%u",
                maxOwnerIndex, owner.priority, owner.elapsedUs,
                owner.internalUs[TH07_PSP_PERF_INTERNAL_PACK],
                owner.internalUs[TH07_PSP_PERF_INTERNAL_MATRIX],
                owner.internalUs[TH07_PSP_PERF_INTERNAL_STATE],
                owner.internalUs[TH07_PSP_PERF_INTERNAL_DEFERRED_FLUSH],
                owner.internalUs[TH07_PSP_PERF_INTERNAL_DCACHE], otherUs,
                internalSumUs, internalErrorUs, internalLimitUs, gPerfInternalMismatch,
                internalErrorUs <= internalLimitUs && gPerfInternalMismatch == 0u ? 1u
                                                                                   : 0u);
        }
        else
        {
            std::snprintf(internalMessage, sizeof(internalMessage),
                          "PERF M2I I999 P-1 TOTUS0 PKUS0 MXUS0 STUS0 FLUS0 DCUS0 "
                          "OTUS0 SUMUS0 ERR0 LIM%llu MM%u G0",
                          200ull * mPerfFrames, gPerfInternalMismatch);
        }
        th07_psp_perf_note(internalMessage);
#if defined(TH07_PSP_PERF_GPU_ATTRIB)
        const auto gpuJob10 = [this](unsigned int priority) {
            return static_cast<unsigned int>(gPerfDrawJobGpuUs[priority] /
                                             mPerfFrames / 100u);
        };
        char gpuMessage[192];
        std::snprintf(gpuMessage, sizeof(gpuMessage),
                      "PERF GPU ST%u.%u/%u.%u EN%u.%u/%u.%u PL%u.%u/%u.%u "
                      "FX%u.%u BU%u.%u GUI%u.%u",
                      gpuJob10(3) / 10, gpuJob10(3) % 10,
                      gpuJob10(4) / 10, gpuJob10(4) % 10,
                      gpuJob10(5) / 10, gpuJob10(5) % 10,
                      gpuJob10(7) / 10, gpuJob10(7) % 10,
                      gpuJob10(6) / 10, gpuJob10(6) % 10,
                      gpuJob10(8) / 10, gpuJob10(8) % 10,
                      gpuJob10(9) / 10, gpuJob10(9) % 10,
                      gpuJob10(10) / 10, gpuJob10(10) % 10,
                      gpuJob10(12) / 10, gpuJob10(12) % 10);
        th07_psp_perf_note(gpuMessage);
#endif
#elif defined(TH07_PSP_PERF_M3)
        Th07PspM3PerfWindow m3{};
        Th07PspTakeM3PerfWindow(&m3);
        const unsigned long long buUs = gPerfDrawJobUs[10];
        const unsigned long long phaseSumUs = m3.laserUs + m3.itemUs + m3.bulletUs;
        const unsigned long long internalErrorUs = m3.callbackUs >= phaseSumUs
                                                       ? m3.callbackUs - phaseSumUs
                                                       : phaseSumUs - m3.callbackUs;
        const unsigned long long closureErrorUs = buUs >= phaseSumUs
                                                      ? buUs - phaseSumUs
                                                      : phaseSumUs - buUs;
        const unsigned long long closureLimitUs = std::max<unsigned long long>(
            200ull * mPerfFrames, buUs / 50ull);
        const unsigned int laserCount10 = m3.activeLasers * 10u / mPerfFrames;
        const unsigned int itemCount10 = m3.activeItems * 10u / mPerfFrames;
        const unsigned int bulletCount10 = m3.bulletVisits * 10u / mPerfFrames;
        char m3Message[256];
        std::snprintf(
            m3Message, sizeof(m3Message),
            "PERF M3 BUUS%llu LZUS%llu ITUS%llu BTUS%llu SUMUS%llu "
            "ERR%llu IERR%llu LIM%llu F%u/%u NL%u.%u NI%u.%u NB%u.%u "
            "VIS%u OOR%u G%u",
            buUs, m3.laserUs, m3.itemUs, m3.bulletUs, phaseSumUs,
            closureErrorUs, internalErrorUs, closureLimitUs,
            m3.frames, mPerfFrames,
            laserCount10 / 10, laserCount10 % 10,
            itemCount10 / 10, itemCount10 % 10,
            bulletCount10 / 10, bulletCount10 % 10,
            m3.bulletVisits,
            gPerfDrawOutOfRange,
            closureErrorUs <= closureLimitUs && internalErrorUs <= closureLimitUs &&
                    m3.frames == mPerfFrames && gPerfDrawOutOfRange == 0u
                ? 1u
                : 0u);
        th07_psp_perf_note(m3Message);

        Th07PspM3EmitterWindow emitter{};
        Th07PspTakeM3EmitterPerf(&emitter);
        const auto correctedPhaseWindowUs = [&emitter](unsigned int phase) {
            if (emitter.samples == 0)
            {
                return 0ull;
            }
            const unsigned long long probeUs =
                (gPerfM3TimerReadQ8 * emitter.phaseRecords[phase] + 128ull) / 256ull;
            const unsigned long long correctedSampleUs =
                emitter.phaseUs[phase] >= probeUs
                    ? emitter.phaseUs[phase] - probeUs
                    : 0ull;
            return correctedSampleUs * emitter.emitterCalls / emitter.samples;
        };
        const unsigned long long descriptorWindowUs = correctedPhaseWindowUs(0);
        const unsigned long long cullRotationWindowUs = correctedPhaseWindowUs(1);
        const unsigned long long stateWindowUs = correctedPhaseWindowUs(2);
        const unsigned long long vertexStoreWindowUs = correctedPhaseWindowUs(3);
        const unsigned long long emitterWindowUs = descriptorWindowUs + cullRotationWindowUs +
                                                   stateWindowUs + vertexStoreWindowUs;
        const unsigned long long sampledDrawWindowUs = m3.sampledBulletDraws
                                                            ? m3.sampledBulletDrawUs *
                                                                  emitter.emitterCalls /
                                                                  m3.sampledBulletDraws
                                                            : 0ull;
        const unsigned long long excludedBackendWindowUs = emitter.samples
                                                                ? emitter.excludedBackendUs *
                                                                      emitter.emitterCalls /
                                                                      emitter.samples
                                                                : 0ull;
        const bool rawFrontendValid = sampledDrawWindowUs >= excludedBackendWindowUs;
        const unsigned long long rawFrontendWindowUs = rawFrontendValid
                                                           ? sampledDrawWindowUs -
                                                                 excludedBackendWindowUs
                                                           : 0ull;
        unsigned long long phaseRecordCount = 0;
        for (unsigned int phase = 0; phase < 4u; ++phase)
        {
            phaseRecordCount += emitter.phaseRecords[phase];
        }
        // Besides each recorded phase boundary, the sampled outer draw owns
        // one emitter-constructor timestamp and one outer end timestamp.
        const unsigned long long probeWindowUs = emitter.samples
                                                     ? (gPerfM3TimerReadQ8 *
                                                            (phaseRecordCount +
                                                             2ull * emitter.samples) *
                                                            emitter.emitterCalls +
                                                        128ull * emitter.samples) /
                                                           (256ull * emitter.samples)
                                                     : 0ull;
        const bool frontendValid = rawFrontendValid &&
                                   rawFrontendWindowUs >= probeWindowUs;
        const unsigned long long frontendWindowUs = frontendValid
                                                        ? rawFrontendWindowUs - probeWindowUs
                                                        : 0ull;
        const bool emitterValid = frontendWindowUs >= emitterWindowUs;
        const unsigned long long vmWindowUs = emitterValid
                                                  ? frontendWindowUs - emitterWindowUs
                                                  : 0ull;
        const bool backendValid = gPerfM3SpriteBackendUs >= gPerfM3DcacheUs;
        const unsigned long long rawKnownBulletUs =
            frontendWindowUs + gPerfM3SpriteBackendUs;
        const bool knownFitsBullet = m3.bulletUs >= rawKnownBulletUs;
        const unsigned long long linkWindowUs = knownFitsBullet
                                                    ? m3.bulletUs - rawKnownBulletUs
                                                    : 0ull;
        const bool carryInValid =
            gPerfM3CarryInFlushUs <= m3.bulletUs &&
            gPerfM3CarryInFlushUs <= gPerfM3SpriteBackendUs &&
            gPerfM3CarryInDcacheUs <= gPerfM3CarryInFlushUs &&
            gPerfM3CarryInDcacheUs <= gPerfM3DcacheUs;
        const bool carryOutValid =
            gPerfM3CarryOutDcacheUs <= gPerfM3CarryOutFlushUs;
        const bool transferValid = carryInValid && carryOutValid;
        const unsigned long long btxUs = transferValid
                                             ? m3.bulletUs - gPerfM3CarryInFlushUs +
                                                   gPerfM3CarryOutFlushUs
                                             : 0ull;
        const unsigned long long sourceBackendUs =
            transferValid
                ? gPerfM3SpriteBackendUs - gPerfM3CarryInFlushUs +
                      gPerfM3CarryOutFlushUs
                : 0ull;
        const unsigned long long sourceDcacheUs =
            transferValid
                ? gPerfM3DcacheUs - gPerfM3CarryInDcacheUs +
                      gPerfM3CarryOutDcacheUs
                : 0ull;
        const bool sourceBackendValid = sourceBackendUs >= sourceDcacheUs;
        const unsigned long long sourceRepackUs =
            sourceBackendValid ? sourceBackendUs - sourceDcacheUs : 0ull;
        const unsigned long long detailSumUs =
            linkWindowUs + vmWindowUs + emitterWindowUs + sourceRepackUs +
            sourceDcacheUs;
        const unsigned long long detailErrorUs = btxUs >= detailSumUs
                                                     ? btxUs - detailSumUs
                                                     : detailSumUs - btxUs;
        const unsigned long long detailLimitUs = std::max<unsigned long long>(
            200ull * mPerfFrames, btxUs / 50ull);
        const bool samplesValid =
            Th07PspM3EmitterPopulationValid(&emitter, m3.sampledBulletDraws,
                                            m3.bulletVisits);
        const unsigned int pendingKiB =
            static_cast<unsigned int>(gPerfM3PendingBytes / mPerfFrames / 1024u);
        unsigned int unresolved = gPerfM3TransferUnresolved;
        unresolved += Th07PspM3FrontBatchUnresolved();
        if (gPerfM3DeferredBatchOrigin != TH07_PSP_PERF_M3_BATCH_NONE ||
            gPerfM3WrapperActive || gPerfM3CarryInPending)
        {
            ++unresolved;
        }
        const bool detailValid = frontendValid && emitterValid && backendValid &&
                                 knownFitsBullet && transferValid && sourceBackendValid &&
                                 samplesValid && gPerfM3TransferMixed == 0u &&
                                 unresolved == 0u &&
                                 emitter.phaseMismatches == 0u &&
                                 (emitter.samples == 0u ||
                                  (gPerfM3TimerReadQ8 != 0u &&
                                   gPerfM3TimerReadQ8 <= 4096ull)) &&
                                 detailErrorUs <= detailLimitUs;
        char emitterMessage[576];
        std::snprintf(
            emitterMessage, sizeof(emitterMessage),
            "PERF M3S RAWUS%llu BTXUS%llu LKUS%llu VMUS%llu VDUS%llu "
            "CRUS%llu STUS%llu VSUS%llu RPUS%llu DCUS%llu SUMUS%llu "
            "ERR%llu LIM%llu CIUS%llu CIDCUS%llu COUS%llu CODCUS%llu "
            "CINB%llu COUTB%llu SAMP%u/%u/%u CULL%u BC%u DCN%u "
            "PEND%uK EXUS%llu TMRQ8%llu FRAWUS%llu POVUS%llu "
            "REC%u/%u/%u/%u MM%u MIX%u UNRES%u G%u",
            m3.bulletUs, btxUs, linkWindowUs, vmWindowUs, descriptorWindowUs,
            cullRotationWindowUs, stateWindowUs, vertexStoreWindowUs,
            sourceRepackUs, sourceDcacheUs, detailSumUs, detailErrorUs,
            detailLimitUs, gPerfM3CarryInFlushUs, gPerfM3CarryInDcacheUs,
            gPerfM3CarryOutFlushUs, gPerfM3CarryOutDcacheUs,
            gPerfM3CarryInBytes, gPerfM3CarryOutBytes,
            m3.sampledBulletDraws, emitter.samples, emitter.emitterCalls,
            emitter.sampledCulls, gPerfM3SpriteBackendCalls, gPerfM3DcacheCalls,
            pendingKiB, excludedBackendWindowUs, gPerfM3TimerReadQ8,
            rawFrontendWindowUs, probeWindowUs,
            emitter.phaseRecords[0], emitter.phaseRecords[1],
            emitter.phaseRecords[2], emitter.phaseRecords[3], emitter.phaseMismatches,
            gPerfM3TransferMixed, unresolved, detailValid ? 1u : 0u);
        th07_psp_perf_note(emitterMessage);
        PerfM3ClearWindowTransferCounters();
#endif
#elif defined(TH07_PSP_PERF_ACCEPT)
        int acceptProfileValid = th07_psp_perf_log_valid();
#if defined(TH07_PSP_ME_RENDER_WORKER)
        Th07PspMeRenderShadowWindow merw{};
        Th07PspTakeMeRenderShadowWindow(&merw);
        unsigned int meAudioJobs = 0u;
        unsigned int meAudioFallbacks = 0u;
        unsigned int meAudioTimeouts = 0u;
        unsigned int meAudioMaxWaitUs = 0u;
        th07_psp_me_audio_diag_window(
            &meAudioJobs, &meAudioFallbacks, &meAudioTimeouts,
            &meAudioMaxWaitUs);
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        Th07PspDenseSliceWindow dense{};
        Th07PspTakeDenseSliceWindow(&dense);
        Th07PspEnemyP5WarmWindow enemyP5{};
        Th07PspTakeEnemyP5WarmWindow(&enemyP5);
        const bool denseTarget = mPerfWindowState == 2 && mPerfWindowStage == 6 &&
                                 mPerfWindowSerial >= 12u &&
                                 mPerfWindowSerial <= 15u;
        const unsigned long long updatePhaseSumUs =
            dense.updateItemUs + dense.updateBulletUs + dense.updateTailUs;
        const unsigned long long drawPhaseSumUs =
            dense.drawLaserUs + dense.drawItemUs + dense.drawBulletUs;
        const unsigned long long updateErrorUs =
            dense.updateCallbackUs >= updatePhaseSumUs
                ? dense.updateCallbackUs - updatePhaseSumUs
                : updatePhaseSumUs - dense.updateCallbackUs;
        const unsigned long long drawErrorUs =
            dense.drawCallbackUs >= drawPhaseSumUs
                ? dense.drawCallbackUs - drawPhaseSumUs
                : drawPhaseSumUs - dense.drawCallbackUs;
        const unsigned long long accountedCpuUs =
            gPerfCalcChainUs + gPerfDrawChainUs + gPerfDensePostFlushUs +
            gPerfDenseSwapSubmitUs
#if defined(TH07_PSP_ME_RENDER_WORKER)
            + gPerfMerwPostCalcUs
#endif
            ;
        const unsigned long long cpuResidualUs =
            mPerfCpuUs >= accountedCpuUs ? mPerfCpuUs - accountedCpuUs : 0ull;
        const unsigned long long outerErrorUs =
            accountedCpuUs > mPerfCpuUs ? accountedCpuUs - mPerfCpuUs : 0ull;
        const unsigned long long outerLimitUs = std::max<unsigned long long>(
            200ull * mPerfFrames, mPerfCpuUs / 50ull);
        const unsigned long long calcLimitUs = std::max<unsigned long long>(
            200ull * mPerfFrames, gPerfCalcChainUs / 50ull);
        const unsigned long long drawLimitUs = std::max<unsigned long long>(
            200ull * mPerfFrames, gPerfDrawChainUs / 50ull);
        constexpr unsigned long long kDenseProbeReadsPerFrame = 15ull;
        constexpr unsigned long long kDenseProbeLimitQ8 = 50ull * 256ull;
        bool denseValid = false;
        if (denseTarget)
        {
            denseValid = mPerfFrames == kPerfWindowFrames &&
                         gPerfDenseCalcFrames == mPerfFrames &&
                         gPerfDenseDrawFrames == mPerfFrames &&
                         dense.updateFrames == mPerfFrames &&
                         dense.drawFrames == mPerfFrames &&
                         gPerfDensePostFlushFrames == mPerfFrames &&
                         gPerfDenseSwapFrames == mPerfFrames &&
                         updateErrorUs == 0ull && drawErrorUs == 0ull &&
                         dense.updateCallbackUs <= gPerfCalcChainUs + calcLimitUs &&
                         dense.drawCallbackUs <= gPerfDrawChainUs + drawLimitUs &&
                         outerErrorUs <= outerLimitUs &&
                         dense.bulletVisits == dense.onePassAccepts +
                                                   dense.onePassFallbacks &&
                         dense.onePassFallbacks == dense.canonicalDrawCalls
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
                         && dense.meBulletFastAttempts == dense.updateFrames &&
                         dense.meBulletFastCompleted +
                                 dense.meBulletFastFallbacks ==
                             dense.meBulletFastAttempts
#endif
                             TH07_PSP_DENSE_WARM_VALID(dense)
                                 TH07_PSP_DENSE_STATIC_PROXY_VALID(dense)
                                 TH07_PSP_DENSE_ENEMY_P5_VALID(enemyP5, dense) &&
                         gPerfDenseTimerReadQ8 * kDenseProbeReadsPerFrame <=
                             kDenseProbeLimitQ8 &&
                         gPerfDrawOutOfRange == 0u;

            char denseMessage[512];
            std::snprintf(
                denseMessage, sizeof(denseMessage),
                "PERF DENSE S%d ST%d N%u CUS%llu RUS%llu OUTUS%llu "
                "UCUS%llu UIUS%llu UBUS%llu ULUS%llu "
                "DCUS%llu DLUS%llu DIUS%llu DBUS%llu "
                "POSTUS%llu SWAPUS%llu POP%llu VIS%llu OPA%llu OPF%llu "
                "DRAWFB%llu"
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
                " ME16F%u/%u/%u ME16A%llu C%llu NC%llu WB%llu WAIT%llu "
                "INV%llu KC%llu"
#endif
                TH07_PSP_DENSE_WARM_FORMAT
                TH07_PSP_DENSE_STATIC_PROXY_FORMAT TH07_PSP_DENSE_ENEMY_P5_FORMAT
                " F%u/%u/%u/%u/%u/%u "
                "TMRQ8%llu ERR%llu/%llu/%llu G%d",
                mPerfWindowState, mPerfWindowStage, mPerfFrames,
                gPerfCalcChainUs, gPerfDrawChainUs, cpuResidualUs,
                dense.updateCallbackUs, dense.updateItemUs,
                dense.updateBulletUs, dense.updateTailUs,
                dense.drawCallbackUs, dense.drawLaserUs,
                dense.drawItemUs, dense.drawBulletUs,
                gPerfDensePostFlushUs, gPerfDenseSwapSubmitUs,
                dense.updateBulletPopulation, dense.bulletVisits,
                dense.onePassAccepts, dense.onePassFallbacks,
                dense.canonicalDrawCalls
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
                , dense.meBulletFastAttempts,
                dense.meBulletFastCompleted, dense.meBulletFastFallbacks,
                dense.meBulletFastActive, dense.meBulletFastCandidates,
                dense.meBulletFastNoCollision,
                dense.meBulletFastScWritebackUs,
                dense.meBulletFastDispatchWaitUs,
                dense.meBulletFastScInvalidateUs,
                dense.meBulletFastKernelCycles
#endif
                TH07_PSP_DENSE_WARM_ARGS(dense)
                TH07_PSP_DENSE_STATIC_PROXY_ARGS(dense)
                TH07_PSP_DENSE_ENEMY_P5_ARGS(enemyP5),
                gPerfDenseCalcFrames,
                gPerfDenseDrawFrames, dense.updateFrames, dense.drawFrames,
                gPerfDensePostFlushFrames, gPerfDenseSwapFrames,
                gPerfDenseTimerReadQ8, updateErrorUs, drawErrorUs,
                outerErrorUs, denseValid ? 1 : 0);
            th07_psp_perf_note(denseMessage);
        }
        else
        {
            denseValid = gPerfDenseCalcFrames == 0u &&
                         gPerfDenseDrawFrames == 0u && dense.updateFrames == 0u &&
                         dense.drawFrames == 0u &&
                         gPerfDensePostFlushFrames == 0u &&
                         gPerfDenseSwapFrames == 0u &&
                         dense.bulletVisits == 0ull &&
                         dense.onePassAccepts == 0ull &&
                         dense.onePassFallbacks == 0ull
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
                         && dense.meBulletFastAttempts == 0u &&
                         dense.meBulletFastCompleted == 0u &&
                         dense.meBulletFastFallbacks == 0u &&
                         dense.meBulletFastActive == 0ull &&
                         dense.meBulletFastCandidates == 0ull &&
                         dense.meBulletFastNoCollision == 0ull &&
                         dense.meBulletFastScWritebackUs == 0ull &&
                         dense.meBulletFastDispatchWaitUs == 0ull &&
                         dense.meBulletFastScInvalidateUs == 0ull &&
                         dense.meBulletFastKernelCycles == 0ull
#endif
                             TH07_PSP_DENSE_WARM_ZERO(dense)
                                 TH07_PSP_DENSE_STATIC_PROXY_ZERO(dense)
                                 TH07_PSP_DENSE_ENEMY_P5_ZERO(enemyP5);
        }
        acceptProfileValid = acceptProfileValid && denseValid;
#if defined(TH07_PSP_ME_RENDER_WORKER)
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
        const bool merwObservationActivity =
            gPerfMerwPostCalcFrames != 0u || gPerfMerwPostCalcUs != 0ull ||
            merw.snapshotUs != 0ull || merw.inputBytes != 0ull ||
            merw.outputBytes != 0ull || merw.scWritebackUs != 0ull ||
            merw.scOutputPrepareUs != 0ull || merw.scSubmitUs != 0ull ||
            merw.scInvalidateUs != 0ull || merw.dispatchUs != 0ull ||
            merw.meInvalidateCycles != 0ull || merw.meKernelCycles != 0ull ||
            merw.meWritebackCycles != 0ull || merw.records != 0ull ||
            merw.targetRecords != 0ull || merw.targetOutputBytes != 0ull ||
            merw.deadlineProbeUs != 0ull || merw.eligible != 0u ||
            merw.meRenderSubmitted != 0u || merw.deadlines != 0u ||
            merw.meRenderCompleted != 0u || merw.wouldConsume != 0u ||
            merw.fallbackFrames != 0u || merw.sampleCount != 0u ||
            merw.kernelSampleCount != 0u || merw.streamSubmitted != 0u ||
            merw.streamReady != 0u || merw.streamCompared != 0u ||
            merw.streamMixedPrimitiveFrames != 0u
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
            || merw.streamGeFrames != 0u || merw.streamGeRuns != 0u ||
            merw.streamGeVertices != 0u
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            || merw.streamItemEligible != 0u ||
            merw.streamItemAccepted != 0u ||
            merw.streamItemFallback != 0u ||
            merw.streamItemReject != 0u || merw.streamItemRecords != 0ull ||
            merw.streamItemRuns != 0ull || merw.streamItemVertices != 0ull ||
            merw.streamItemSuffixRecords != 0ull
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
            || merw.streamItemCandidates != 0u ||
            merw.streamItemBudgetReject != 0u ||
            merw.streamItemBusyVeto != 0u ||
            merw.streamItemCandidateRecords != 0ull ||
            merw.streamItemCandidateMax != 0u ||
            merw.streamItemBudgetRejectMax != 0u ||
            merw.streamItemPredictedTicksMax != 0ull
#endif
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
            || merw.compactLaunchAttempts != 0u ||
            merw.compactLaunchBegun != 0u ||
            merw.compactLaunchBusy != 0u || merw.compactReady != 0u ||
            merw.compactReject != 0u ||
            merw.compactSeedCandidates != 0ull ||
            merw.compactMotionHits != 0ull ||
            merw.compactBroadphaseHits != 0ull ||
            merw.compactProtocolFault != 0u
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
            || merw.compactItemMotionLaunch != 0u ||
            merw.compactItemMotionReady != 0u ||
            merw.compactItemMotionCandidates != 0ull ||
            merw.compactItemMotionProcessed != 0ull ||
            merw.compactItemMotionAdopted != 0ull
#endif
#endif
            ;
        const bool merwObservationFault =
            merw.sampleOverflow != 0u || merw.notReady != 0u ||
            merw.lateRetired != 0u || merw.signatureDrop != 0u ||
            merw.fcrDrop != 0u || merw.epochDrop != 0u ||
            merw.stageEpochDrop != 0u || merw.managerEpochDrop != 0u ||
            merw.replayEpochDrop != 0u || merw.generationDrop != 0u ||
            merw.boundsDrop != 0u || merw.busy != 0u ||
            merw.timeouts != 0u || merw.quarantined != 0u ||
            merw.coverageDrop != 0u || merw.beginFail != 0u ||
            merw.deadlineFault != 0u || merw.protocolFault != 0u ||
            merw.streamMismatch != 0u ||
            merw.streamSizeMismatch != 0u ||
            merw.streamVertexMismatch != 0u ||
            merw.streamRunMismatch != 0u ||
            merw.streamHashMismatch != 0u ||
            merw.streamHeaderDrop != 0u ||
            merw.streamIdentityDrop != 0u ||
            merw.streamReleaseFault != 0u || merw.scCopyUs != 0ull ||
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
            merw.compactBlockedRender != 0u ||
            merw.compactProtocolFault != 0u ||
#endif
            meAudioJobs != 0u || meAudioFallbacks != 0u ||
            meAudioTimeouts != 0u;
        if (denseTarget)
        {
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
            const bool merwItemValid =
                merw.streamItemAccepted <= merw.streamItemEligible &&
                merw.streamItemFallback <= merw.streamItemEligible &&
                merw.streamItemReject <= merw.streamItemEligible &&
                merw.streamItemBudgetReject + merw.streamItemBusyVeto <=
                    merw.streamItemCandidates &&
                merw.streamItemCandidateMax <=
                    merw.streamItemCandidateRecords &&
                merw.streamItemBudgetRejectMax <=
                    merw.streamItemCandidateMax &&
                (merw.streamItemVertices & 3ull) == 0ull &&
                merw.streamItemVertices <= merw.streamItemRecords * 4ull &&
                merw.streamItemRuns * 4ull <= merw.streamItemVertices;
#else
            const bool merwItemValid =
                merw.streamItemEligible == mPerfFrames &&
                merw.streamItemAccepted == mPerfFrames &&
                merw.streamItemFallback == 0u &&
                merw.streamItemReject == 0u &&
                (merw.streamItemVertices & 3ull) == 0ull &&
                merw.streamItemVertices <= merw.streamItemRecords * 4ull &&
                merw.streamItemRuns * 4ull <= merw.streamItemVertices;
#endif
#else
            constexpr bool merwItemValid = true;
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
            const u32 merwCompactRetired =
                merw.compactReady + merw.compactReject;
            const u32 merwCompactClosureError =
                merwCompactRetired >= merw.compactLaunchBegun
                    ? merwCompactRetired - merw.compactLaunchBegun
                    : merw.compactLaunchBegun - merwCompactRetired;
            const bool merwCompactValid =
                merw.compactProtocolFault == 0u &&
                merw.compactReject == 0u &&
                merw.compactBlockedRender == 0u &&
                merw.compactLaunchBegun != 0u &&
                merw.compactLaunchBegun <= merw.compactLaunchAttempts &&
                merw.compactLaunchAttempts - merw.compactLaunchBegun <=
                    merw.compactLaunchBusy &&
                merwCompactClosureError <= 1u &&
                merw.compactSeedCandidates != 0ull &&
                merw.compactMotionHits != 0ull &&
                merw.compactMotionHits <= merw.compactSeedCandidates &&
                merw.compactBoundsHits <= merw.compactMotionHits &&
                merw.compactCollisionHits <= merw.compactMotionHits &&
                merw.compactJitReject <= merw.compactSeedCandidates &&
                merw.compactKernelCycles != 0ull
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                && merw.compactItemMotionReady <=
                    merw.compactItemMotionLaunch &&
                merw.compactItemMotionProcessed <=
                    merw.compactItemMotionCandidates &&
                merw.compactItemMotionAdopted <=
                    merw.compactItemMotionJitCandidates &&
                static_cast<unsigned long long>(
                    merw.compactItemMotionSlotReject) +
                    merw.compactItemMotionGlobalReject <=
                    merw.compactItemMotionJitCandidates
#endif
                ;
#else
            constexpr bool merwCompactValid = true;
#endif
            const bool merwCoreValid =
                mPerfFrames == kPerfWindowFrames &&
                gPerfMerwPostCalcFrames == mPerfFrames &&
                merw.eligible == mPerfFrames &&
                merw.meRenderSubmitted == mPerfFrames &&
                merw.streamSubmitted == mPerfFrames &&
                merw.deadlines == mPerfFrames &&
                merw.meRenderCompleted == mPerfFrames &&
                merw.streamReady == mPerfFrames &&
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                merw.streamCompared == 0u &&
                merw.wouldConsume == mPerfFrames &&
                merw.fallbackFrames == 0u &&
                merw.streamGeFrames == mPerfFrames &&
                merw.streamGeRuns >= mPerfFrames &&
                merw.streamGeVertices != 0u &&
#else
                merw.streamCompared == mPerfFrames &&
                merw.wouldConsume == mPerfFrames &&
                merw.fallbackFrames == mPerfFrames &&
#endif
                merw.targetRecords == dense.bulletVisits &&
                merw.inputBytes ==
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
                                       0u &&
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
                                       merw.records *
                                       sizeof(Th07PspMeRenderRawRecord) &&
#else
                                       merw.records *
                                       sizeof(Th07PspMeRenderStreamRecord) &&
#endif
                merw.sampleCount == mPerfFrames &&
                merw.kernelSampleCount == mPerfFrames &&
                merw.sampleOverflow == 0u && merw.notReady == 0u &&
                merw.lateRetired == 0u && merw.signatureDrop == 0u &&
                merw.fcrDrop == 0u && merw.epochDrop == 0u &&
                merw.stageEpochDrop == 0u &&
                merw.managerEpochDrop == 0u &&
                merw.replayEpochDrop == 0u &&
                merw.generationDrop == 0u && merw.boundsDrop == 0u &&
                merw.busy == 0u && merw.timeouts == 0u &&
                merw.quarantined == 0u && merw.coverageDrop == 0u &&
                merw.beginFail == 0u && merw.deadlineFault == 0u &&
                merw.protocolFault == 0u && merw.streamMismatch == 0u &&
                merw.streamSizeMismatch == 0u &&
                merw.streamVertexMismatch == 0u &&
                merw.streamRunMismatch == 0u &&
                merw.streamHashMismatch == 0u &&
                merw.streamHeaderDrop == 0u &&
                merw.streamIdentityDrop == 0u &&
#if !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                merw.streamMixedPrimitiveFrames != 0u &&
#endif
                merw.streamReleaseFault == 0u && merw.scCopyUs == 0ull &&
                meAudioJobs == 0u && meAudioFallbacks == 0u &&
                meAudioTimeouts == 0u && merwItemValid &&
                merwCompactValid;

            char merwMessage[512];
            const int merwLength = std::snprintf(
                merwMessage, sizeof(merwMessage),
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                "PERF MERW I7 S%d ST%d N%u EL%u PUB%u DL%u RD%u CMP%u "
#else
                "PERF MERW I5 S%d ST%d N%u EL%u PUB%u DL%u RD%u CMP%u "
#endif
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
                "PERF MERW I4 S%d ST%d N%u EL%u PUB%u DL%u RD%u CMP%u "
#elif defined(TH07_PSP_ME_RENDER_PERFORMANCE)
                "PERF MERW I3 S%d ST%d N%u EL%u PUB%u DL%u RD%u CMP%u "
#elif defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                "PERF MERW I2 S%d ST%d N%u EL%u PUB%u DL%u RD%u CMP%u "
#else
                "PERF MERW I1 S%d ST%d N%u EL%u PUB%u DL%u RD%u CMP%u "
#endif
                "MM%u SZ%u VX%u RN%u HH%u ID%u MIX%u FB%u REC%llu "
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                "GFR%u GSR%u GVX%u "
#endif
                "DREC%llu NR%u LATE%u SIG%u ED%u BUSY%u TO%u COV%u "
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                "BFAIL%u PF%u REL%u AJ%u AF%u AT%u SC_DRAW=0 "
                "SC_FALLBACK=1 GE_CONSUME=1 G%u",
#else
                "BFAIL%u PF%u REL%u AJ%u AF%u AT%u SC_DRAW=1 "
                "GE_CONSUME=0 G%u",
#endif
                mPerfWindowState, mPerfWindowStage, mPerfFrames,
                merw.eligible, merw.streamSubmitted, merw.deadlines,
                merw.streamReady, merw.streamCompared,
                merw.streamMismatch, merw.streamSizeMismatch,
                merw.streamVertexMismatch, merw.streamRunMismatch,
                merw.streamHashMismatch, merw.streamIdentityDrop,
                merw.streamMixedPrimitiveFrames, merw.fallbackFrames,
                merw.records,
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                merw.streamGeFrames, merw.streamGeRuns,
                merw.streamGeVertices,
#endif
                merw.targetRecords, merw.notReady,
                merw.lateRetired, merw.signatureDrop, merw.epochDrop,
                merw.busy, merw.timeouts, merw.coverageDrop,
                merw.beginFail, merw.protocolFault,
                merw.streamReleaseFault, meAudioJobs, meAudioFallbacks,
                meAudioTimeouts, merwCoreValid ? 1u : 0u);
            char merwTimingMessage[512];
            const int merwTimingLength = std::snprintf(
                merwTimingMessage, sizeof(merwTimingMessage),
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                "PERF MERWT I7 S%d ST%d N%u SNAPUS%llu POSTUS%llu "
#else
                "PERF MERWT I5 S%d ST%d N%u SNAPUS%llu POSTUS%llu "
#endif
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
                "PERF MERWT I4 S%d ST%d N%u SNAPUS%llu POSTUS%llu "
#elif defined(TH07_PSP_ME_RENDER_PERFORMANCE)
                "PERF MERWT I3 S%d ST%d N%u SNAPUS%llu POSTUS%llu "
#elif defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                "PERF MERWT I2 S%d ST%d N%u SNAPUS%llu POSTUS%llu "
#else
                "PERF MERWT I1 S%d ST%d N%u SNAPUS%llu POSTUS%llu "
#endif
                "PROBEUS%llu INB%llu OUTB%llu WB%llu OPREP%llu SUB%llu "
                "DISP%llu INV%llu DEAD%u/%u/%u/%u KCYC%u/%u/%u/%u "
                "MIC%llu MKC%llu MWC%llu FIRST%u/%u/%08x/%08x G%u",
                mPerfWindowState, mPerfWindowStage, mPerfFrames,
                merw.snapshotUs, gPerfMerwPostCalcUs,
                merw.deadlineProbeUs, merw.inputBytes, merw.outputBytes,
                merw.scWritebackUs, merw.scOutputPrepareUs,
                merw.scSubmitUs, merw.dispatchUs, merw.scInvalidateUs,
                merw.slackMinUs, merw.slackP50Us, merw.slackP95Us,
                merw.slackP99Us, merw.kernelCycleMin,
                merw.kernelCycleP50, merw.kernelCycleP95,
                merw.kernelCycleP99, merw.meInvalidateCycles,
                merw.meKernelCycles, merw.meWritebackCycles,
                merw.streamFirstMismatchKind,
                merw.streamFirstMismatchWord,
                merw.streamFirstMismatchExpected,
                merw.streamFirstMismatchActual,
                merwCoreValid ? 1u : 0u);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            char merwItemMessage[512];
            const int merwItemLength = std::snprintf(
                merwItemMessage, sizeof(merwItemMessage),
                "PERF MERWI I7 S%d ST%d N%u EL%u AC%u FB%u RJ%u "
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
                "CAND%u BR%u BV%u CREC%llu CMAX%u BRMAX%u PTMAX%llu "
#endif
                "REC%llu SREC%llu RUN%llu VX%llu G%u",
                mPerfWindowState, mPerfWindowStage, mPerfFrames,
                merw.streamItemEligible, merw.streamItemAccepted,
                merw.streamItemFallback, merw.streamItemReject,
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
                merw.streamItemCandidates,
                merw.streamItemBudgetReject,
                merw.streamItemBusyVeto,
                merw.streamItemCandidateRecords,
                merw.streamItemCandidateMax,
                merw.streamItemBudgetRejectMax,
                merw.streamItemPredictedTicksMax,
#endif
                merw.streamItemRecords, merw.streamItemSuffixRecords,
                merw.streamItemRuns,
                merw.streamItemVertices, merwItemValid ? 1u : 0u);
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
            char merwCompactMessage[512];
            const int merwCompactLength = std::snprintf(
                merwCompactMessage, sizeof(merwCompactMessage),
                "PERF MERWC I7 S%d ST%d N%u LA%u LB%u LBUSY%u RD%u "
                "RJ%u CAND%llu MOT%llu BND%llu COL%llu BPH%llu SO%u "
                "JIT%u LAT%u P12H%u P12T%u RBLK%u PF%u KC%llu "
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                "A1L%u A1RD%u A1C%llu A1P%llu A1J%llu A1A%llu "
                "A1SR%u A1GR%u A1PN%u "
#endif
                "SIUS%llu OIUS%llu G%u",
                mPerfWindowState, mPerfWindowStage, mPerfFrames,
                merw.compactLaunchAttempts, merw.compactLaunchBegun,
                merw.compactLaunchBusy, merw.compactReady,
                merw.compactReject, merw.compactSeedCandidates,
                merw.compactMotionHits, merw.compactBoundsHits,
                merw.compactCollisionHits, merw.compactBroadphaseHits,
                merw.compactSeedOnlyFrames, merw.compactJitReject,
                merw.compactCollisionLatch,
                merw.compactP12HeadPending,
                merw.compactP12TailPending,
                merw.compactBlockedRender,
                merw.compactProtocolFault, merw.compactKernelCycles,
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                merw.compactItemMotionLaunch,
                merw.compactItemMotionReady,
                merw.compactItemMotionCandidates,
                merw.compactItemMotionProcessed,
                merw.compactItemMotionJitCandidates,
                merw.compactItemMotionAdopted,
                merw.compactItemMotionSlotReject,
                merw.compactItemMotionGlobalReject,
                merw.compactItemMotionPendingAtItem,
#endif
                merw.compactSeedInvalidateUs,
                merw.compactOutputInvalidateUs,
                merwCompactValid ? 1u : 0u);
#endif
            const bool merwFormatValid =
                merwLength > 0 &&
                static_cast<std::size_t>(merwLength) < sizeof(merwMessage) &&
                merwTimingLength > 0 &&
                static_cast<std::size_t>(merwTimingLength) <
                    sizeof(merwTimingMessage)
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
                && merwItemLength > 0 &&
                static_cast<std::size_t>(merwItemLength) <
                    sizeof(merwItemMessage)
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                && merwCompactLength > 0 &&
                static_cast<std::size_t>(merwCompactLength) <
                    sizeof(merwCompactMessage)
#endif
                ;
            if (merwFormatValid)
            {
                th07_psp_perf_note(merwMessage);
                th07_psp_perf_note(merwTimingMessage);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
                th07_psp_perf_note(merwItemMessage);
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                th07_psp_perf_note(merwCompactMessage);
#endif
            }
            else
            {
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                th07_psp_perf_note("PERF MERW I7 FORMAT_OVERFLOW G0");
#else
                th07_psp_perf_note("PERF MERW I5 FORMAT_OVERFLOW G0");
#endif
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
                th07_psp_perf_note("PERF MERW I4 FORMAT_OVERFLOW G0");
#elif defined(TH07_PSP_ME_RENDER_PERFORMANCE)
                th07_psp_perf_note("PERF MERW I3 FORMAT_OVERFLOW G0");
#elif defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                th07_psp_perf_note("PERF MERW I2 FORMAT_OVERFLOW G0");
#else
                th07_psp_perf_note("PERF MERW I1 FORMAT_OVERFLOW G0");
#endif
            }
            acceptProfileValid =
                acceptProfileValid && merwCoreValid && merwFormatValid;
        }
        else if (merwObservationActivity || merwObservationFault)
        {
            // I-ME1 is an all-path correctness experiment.  Do not discard a
            // pause/transition/boss-window mismatch merely because that window
            // is outside the W12-W15 performance coverage gate.
            const bool observationValid =
                merw.sampleOverflow == 0u && merw.notReady == 0u &&
                merw.lateRetired == 0u && merw.signatureDrop == 0u &&
                merw.fcrDrop == 0u && merw.epochDrop == 0u &&
                merw.stageEpochDrop == 0u &&
                merw.managerEpochDrop == 0u &&
                merw.replayEpochDrop == 0u &&
                merw.generationDrop == 0u && merw.boundsDrop == 0u &&
                merw.busy == 0u && merw.timeouts == 0u &&
                merw.quarantined == 0u &&
                merw.coverageDrop == 0u && merw.beginFail == 0u &&
                merw.deadlineFault == 0u &&
                merw.protocolFault == 0u && merw.streamMismatch == 0u &&
                merw.streamSizeMismatch == 0u &&
                merw.streamVertexMismatch == 0u &&
                merw.streamRunMismatch == 0u &&
                merw.streamHashMismatch == 0u &&
                merw.streamHeaderDrop == 0u &&
                merw.streamIdentityDrop == 0u &&
                merw.streamReleaseFault == 0u && merw.scCopyUs == 0ull &&
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                merw.compactProtocolFault == 0u &&
                merw.compactBlockedRender == 0u &&
#endif
                meAudioJobs == 0u && meAudioFallbacks == 0u &&
                meAudioTimeouts == 0u;
            char merwObservation[512];
            const int merwObservationLength = std::snprintf(
                merwObservation, sizeof(merwObservation),
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                "PERF MERW I7 OBS S%d ST%d N%u EL%u PUB%u DL%u RD%u "
#else
                "PERF MERW I5 OBS S%d ST%d N%u EL%u PUB%u DL%u RD%u "
#endif
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
                "PERF MERW I4 OBS S%d ST%d N%u EL%u PUB%u DL%u RD%u "
#elif defined(TH07_PSP_ME_RENDER_PERFORMANCE)
                "PERF MERW I3 OBS S%d ST%d N%u EL%u PUB%u DL%u RD%u "
#elif defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                "PERF MERW I2 OBS S%d ST%d N%u EL%u PUB%u DL%u RD%u "
#else
                "PERF MERW I1 OBS S%d ST%d N%u EL%u PUB%u DL%u RD%u "
#endif
                "CMP%u MM%u SZ%u VX%u RN%u HH%u HD%u ID%u NR%u LATE%u "
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                "GFR%u GSR%u GVX%u "
#endif
                "SIG%u FCR%u ED%u SD%u MD%u RPD%u GEN%u BND%u BUSY%u "
                "TO%u Q%u COV%u BFAIL%u DFAIL%u PF%u REL%u OV%u "
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
                "IFB%u IRJ%u "
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                "CPF%u RBLK%u "
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                "SC_DRAW=0 SC_FALLBACK=1 GE_CONSUME=1 G%u",
#else
                "SC_DRAW=1 GE_CONSUME=0 G%u",
#endif
                mPerfWindowState, mPerfWindowStage, mPerfFrames,
                merw.eligible, merw.streamSubmitted, merw.deadlines,
                merw.streamReady, merw.streamCompared,
                merw.streamMismatch, merw.streamSizeMismatch,
                merw.streamVertexMismatch, merw.streamRunMismatch,
                merw.streamHashMismatch, merw.streamHeaderDrop,
                merw.streamIdentityDrop, merw.notReady,
                merw.lateRetired,
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                merw.streamGeFrames, merw.streamGeRuns,
                merw.streamGeVertices,
#endif
                merw.signatureDrop, merw.fcrDrop,
                merw.epochDrop, merw.stageEpochDrop,
                merw.managerEpochDrop, merw.replayEpochDrop,
                merw.generationDrop, merw.boundsDrop, merw.busy,
                merw.timeouts, merw.quarantined, merw.coverageDrop,
                merw.beginFail, merw.deadlineFault, merw.protocolFault,
                merw.streamReleaseFault, merw.sampleOverflow,
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
                merw.streamItemFallback, merw.streamItemReject,
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                merw.compactProtocolFault, merw.compactBlockedRender,
#endif
                observationValid ? 1u : 0u);
            const bool observationFormatValid =
                merwObservationLength > 0 &&
                static_cast<std::size_t>(merwObservationLength) <
                    sizeof(merwObservation);
            th07_psp_perf_note(observationFormatValid
                                   ? merwObservation
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                                   : "PERF MERW I7 OBS FORMAT_OVERFLOW G0");
#else
                                   : "PERF MERW I5 OBS FORMAT_OVERFLOW G0");
#endif
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
                                   : "PERF MERW I4 OBS FORMAT_OVERFLOW G0");
#elif defined(TH07_PSP_ME_RENDER_PERFORMANCE)
                                   : "PERF MERW I3 OBS FORMAT_OVERFLOW G0");
#elif defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                                   : "PERF MERW I2 OBS FORMAT_OVERFLOW G0");
#else
                                   : "PERF MERW I1 OBS FORMAT_OVERFLOW G0");
#endif
            acceptProfileValid = acceptProfileValid && observationValid &&
                                 observationFormatValid;
        }
#else
        if (denseTarget)
        {
            const bool merwOverheadValid =
                mPerfWindowSerial != 15u ||
                (gPerfMerwPostCalcUs + merw.deadlineProbeUs) /
                        std::max(mPerfFrames, 1u) <=
                    800ull;
            const bool merwCoreValid =
                mPerfFrames == kPerfWindowFrames &&
                gPerfMerwPostCalcFrames == mPerfFrames &&
                merw.eligible == mPerfFrames &&
                merw.meRenderSubmitted == mPerfFrames &&
                merw.deadlines == mPerfFrames &&
                merw.meRenderCompleted == mPerfFrames &&
                merw.wouldConsume == mPerfFrames &&
                merw.fallbackFrames == mPerfFrames &&
                merw.targetRecords == dense.bulletVisits &&
                merw.targetOutputBytes ==
                    merw.targetRecords *
                        TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD &&
                merw.inputBytes == merw.records * 64ull &&
                merw.outputBytes ==
                    merw.records *
                        TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD &&
                merw.sampleCount == mPerfFrames &&
                merw.kernelSampleCount == mPerfFrames &&
                merw.sampleOverflow == 0u && merw.notReady == 0u &&
                merw.lateRetired == 0u && merw.signatureDrop == 0u &&
                merw.fcrDrop == 0u && merw.epochDrop == 0u &&
                merw.stageEpochDrop == 0u &&
                merw.managerEpochDrop == 0u &&
                merw.replayEpochDrop == 0u &&
                merw.generationDrop == 0u && merw.boundsDrop == 0u &&
                merw.busy == 0u && merw.timeouts == 0u &&
                merw.quarantined == 0u && merw.coverageDrop == 0u &&
                merw.beginFail == 0u && merw.deadlineFault == 0u &&
                merw.protocolFault == 0u && merw.scCopyUs == 0ull &&
                meAudioJobs == 0u && meAudioFallbacks == 0u &&
                meAudioTimeouts == 0u && merwOverheadValid;

            char merwMessage[512];
            const int merwLength = std::snprintf(
                merwMessage, sizeof(merwMessage),
                "PERF MERW SYNTH4 S%d ST%d N%u EL%u PUB%u DL%u RD%u "
                "WC%u FB%u PREC%llu DREC%llu NR%u LATE%u ED%u SD%u "
                "MD%u RPD%u SIG%u FCR%u GEN%u BND%u BUSY%u TO%u Q%u "
                "COV%u BFAIL%u DFAIL%u PF%u AJ%u AF%u AT%u AMAX%u "
                "FIXED4=1 GE_CONSUME=0 G%u",
                mPerfWindowState, mPerfWindowStage, mPerfFrames,
                merw.eligible, merw.meRenderSubmitted, merw.deadlines,
                merw.meRenderCompleted,
                merw.wouldConsume, merw.fallbackFrames, merw.records,
                merw.targetRecords, merw.notReady, merw.lateRetired,
                merw.epochDrop, merw.stageEpochDrop,
                merw.managerEpochDrop, merw.replayEpochDrop,
                merw.signatureDrop, merw.fcrDrop, merw.generationDrop,
                merw.boundsDrop, merw.busy, merw.timeouts,
                merw.quarantined, merw.coverageDrop, merw.beginFail,
                merw.deadlineFault, merw.protocolFault, meAudioJobs,
                meAudioFallbacks, meAudioTimeouts, meAudioMaxWaitUs,
                merwCoreValid ? 1u : 0u);

            char merwTimingMessage[512];
            const int merwTimingLength = std::snprintf(
                merwTimingMessage, sizeof(merwTimingMessage),
                "PERF MERWT SYNTH4 S%d ST%d N%u SNAPUS%llu POSTUS%llu "
                "PROBEUS%llu INB%llu OUTB%llu TOUTB%llu WB%llu OPREP%llu "
                "SUB%llu DISP%llu INV%llu COPY%llu "
                "DEAD%u/%u/%u/%u KCYC%u/%u/%u/%u MIC%llu MKC%llu "
                "MWC%llu SAMP%u/%u OV%u G%u",
                mPerfWindowState, mPerfWindowStage, mPerfFrames,
                merw.snapshotUs, gPerfMerwPostCalcUs,
                merw.deadlineProbeUs, merw.inputBytes, merw.outputBytes,
                merw.targetOutputBytes, merw.scWritebackUs,
                merw.scOutputPrepareUs, merw.scSubmitUs, merw.dispatchUs,
                merw.scInvalidateUs, merw.scCopyUs, merw.slackMinUs,
                merw.slackP50Us, merw.slackP95Us, merw.slackP99Us,
                merw.kernelCycleMin, merw.kernelCycleP50,
                merw.kernelCycleP95, merw.kernelCycleP99,
                merw.meInvalidateCycles, merw.meKernelCycles,
                merw.meWritebackCycles, merw.sampleCount,
                merw.kernelSampleCount, merw.sampleOverflow,
                merwCoreValid ? 1u : 0u);
            const bool merwFormatValid =
                merwLength > 0 &&
                static_cast<std::size_t>(merwLength) < sizeof(merwMessage) &&
                merwTimingLength > 0 &&
                static_cast<std::size_t>(merwTimingLength) <
                    sizeof(merwTimingMessage);
            if (merwFormatValid)
            {
                th07_psp_perf_note(merwMessage);
                th07_psp_perf_note(merwTimingMessage);
            }
            else
            {
                th07_psp_perf_note("PERF MERW FORMAT_OVERFLOW G0");
            }
            acceptProfileValid =
                acceptProfileValid && merwCoreValid && merwFormatValid;
        }
#endif
#endif
#endif
#if defined(TH07_PSP_PERF_EMPTY_TIMERS) && defined(TH07_PSP_PERF_M2)
        unsigned int emptyAxisEligible = 0;
        unsigned int emptyFallbackEligible = 0;
        unsigned int emptyCullRejects = 0;
        Th07PspTakeBulletDrawPerf(&emptyAxisEligible, &emptyFallbackEligible,
                                 &emptyCullRejects);
        unsigned long long emptyDrawJobsUs = 0;
        for (const unsigned long long jobUs : gPerfDrawJobUs)
        {
            emptyDrawJobsUs += jobUs;
        }
        unsigned long long emptyDrawOwnerUs = 0;
        unsigned int emptyMaxOwnerIndex = ~0u;
        for (unsigned int ownerIndex = 0; ownerIndex < gPerfDrawOwnerCount; ++ownerIndex)
        {
            const PerfDrawOwnerSlot &owner = gPerfDrawOwners[ownerIndex];
            emptyDrawOwnerUs += owner.elapsedUs;
            if (owner.calls != 0u &&
                (emptyMaxOwnerIndex == ~0u ||
                 owner.elapsedUs > gPerfDrawOwners[emptyMaxOwnerIndex].elapsedUs))
            {
                emptyMaxOwnerIndex = ownerIndex;
            }
        }
        const unsigned long long emptyAccountedDrawUs =
            emptyDrawJobsUs + gPerfDrawChainOverheadUs;
        const unsigned long long emptyDrawClosureErrorUs =
            gPerfDrawChainUs >= emptyAccountedDrawUs
                ? gPerfDrawChainUs - emptyAccountedDrawUs
                : emptyAccountedDrawUs - gPerfDrawChainUs;
        const unsigned long long emptyDrawClosureLimitUs =
            std::max<unsigned long long>(200ull * mPerfFrames,
                                         gPerfDrawChainUs / 50ull);
        const unsigned long long emptyOwnerClosureErrorUs =
            emptyDrawJobsUs >= emptyDrawOwnerUs
                ? emptyDrawJobsUs - emptyDrawOwnerUs
                : emptyDrawOwnerUs - emptyDrawJobsUs;
        bool emptyInternalValid = emptyMaxOwnerIndex != ~0u;
        if (emptyInternalValid)
        {
            const PerfDrawOwnerSlot &owner = gPerfDrawOwners[emptyMaxOwnerIndex];
            unsigned long long categorizedUs = 0;
            for (unsigned int category = 0; category < kPerfInternalCategoryCount;
                 ++category)
            {
                categorizedUs += owner.internalUs[category];
            }
            const unsigned long long otherUs =
                owner.elapsedUs >= categorizedUs ? owner.elapsedUs - categorizedUs : 0ull;
            const unsigned long long internalSumUs = categorizedUs + otherUs;
            const unsigned long long internalErrorUs =
                owner.elapsedUs >= internalSumUs ? owner.elapsedUs - internalSumUs
                                                 : internalSumUs - owner.elapsedUs;
            const unsigned long long internalLimitUs =
                std::max<unsigned long long>(200ull * mPerfFrames,
                                             owner.elapsedUs / 50ull);
            emptyInternalValid = internalErrorUs <= internalLimitUs;
        }
        acceptProfileValid =
            acceptProfileValid && emptyDrawClosureErrorUs <= emptyDrawClosureLimitUs &&
            emptyOwnerClosureErrorUs == 0u && emptyInternalValid &&
            gPerfInternalMismatch == 0u && gPerfDrawOutOfRange == 0u &&
            gPerfDrawOwnerOverflow == 0u;
#elif defined(TH07_PSP_PERF_EMPTY_TIMERS) && defined(TH07_PSP_PERF_M3)
        Th07PspM3PerfWindow emptyM3{};
        Th07PspM3EmitterWindow emptyEmitter{};
        Th07PspTakeM3PerfWindow(&emptyM3);
        Th07PspTakeM3EmitterPerf(&emptyEmitter);
        const unsigned long long emptyBuUs = gPerfDrawJobUs[10];
        const unsigned long long emptyPhaseSumUs =
            emptyM3.laserUs + emptyM3.itemUs + emptyM3.bulletUs;
        const unsigned long long emptyInternalErrorUs =
            emptyM3.callbackUs >= emptyPhaseSumUs
                ? emptyM3.callbackUs - emptyPhaseSumUs
                : emptyPhaseSumUs - emptyM3.callbackUs;
        const unsigned long long emptyClosureErrorUs =
            emptyBuUs >= emptyPhaseSumUs ? emptyBuUs - emptyPhaseSumUs
                                         : emptyPhaseSumUs - emptyBuUs;
        const unsigned long long emptyClosureLimitUs =
            std::max<unsigned long long>(200ull * mPerfFrames,
                                         emptyBuUs / 50ull);
        const bool emptySamplesValid =
            Th07PspM3EmitterPopulationValid(&emptyEmitter,
                                            emptyM3.sampledBulletDraws,
                                            emptyM3.bulletVisits);
        const auto correctedEmptyPhaseWindowUs = [&emptyEmitter](unsigned int phase) {
            if (emptyEmitter.samples == 0u)
            {
                return 0ull;
            }
            const unsigned long long probeUs =
                (gPerfM3TimerReadQ8 * emptyEmitter.phaseRecords[phase] + 128ull) /
                256ull;
            const unsigned long long correctedSampleUs =
                emptyEmitter.phaseUs[phase] >= probeUs
                    ? emptyEmitter.phaseUs[phase] - probeUs
                    : 0ull;
            return correctedSampleUs * emptyEmitter.emitterCalls /
                   emptyEmitter.samples;
        };
        const unsigned long long emptyEmitterWindowUs =
            correctedEmptyPhaseWindowUs(0) + correctedEmptyPhaseWindowUs(1) +
            correctedEmptyPhaseWindowUs(2) + correctedEmptyPhaseWindowUs(3);
        const unsigned long long emptySampledDrawWindowUs =
            emptyM3.sampledBulletDraws == 0u
                ? 0ull
                : emptyM3.sampledBulletDrawUs * emptyEmitter.emitterCalls /
                      emptyM3.sampledBulletDraws;
        const unsigned long long emptyExcludedBackendWindowUs =
            emptyEmitter.samples == 0u
                ? 0ull
                : emptyEmitter.excludedBackendUs * emptyEmitter.emitterCalls /
                      emptyEmitter.samples;
        const bool emptyRawFrontendValid =
            emptySampledDrawWindowUs >= emptyExcludedBackendWindowUs;
        const unsigned long long emptyRawFrontendWindowUs =
            emptyRawFrontendValid
                ? emptySampledDrawWindowUs - emptyExcludedBackendWindowUs
                : 0ull;
        unsigned long long emptyPhaseRecordCount = 0;
        for (unsigned int phase = 0; phase < 4u; ++phase)
        {
            emptyPhaseRecordCount += emptyEmitter.phaseRecords[phase];
        }
        const unsigned long long emptyProbeWindowUs = emptyEmitter.samples
            ? (gPerfM3TimerReadQ8 *
                   (emptyPhaseRecordCount + 2ull * emptyEmitter.samples) *
                   emptyEmitter.emitterCalls +
               128ull * emptyEmitter.samples) /
                  (256ull * emptyEmitter.samples)
            : 0ull;
        const bool emptyFrontendValid = emptyRawFrontendValid &&
                                        emptyRawFrontendWindowUs >= emptyProbeWindowUs;
        const unsigned long long emptyFrontendWindowUs = emptyFrontendValid
            ? emptyRawFrontendWindowUs - emptyProbeWindowUs
            : 0ull;
        const bool emptyEmitterValid =
            emptyFrontendWindowUs >= emptyEmitterWindowUs;
        const unsigned long long emptyVmWindowUs =
            emptyEmitterValid ? emptyFrontendWindowUs - emptyEmitterWindowUs : 0ull;
        const bool emptyBackendValid = gPerfM3SpriteBackendUs >= gPerfM3DcacheUs;
        const unsigned long long emptyRawKnownBulletUs =
            emptyFrontendWindowUs + gPerfM3SpriteBackendUs;
        const bool emptyKnownFitsBullet =
            emptyM3.bulletUs >= emptyRawKnownBulletUs;
        const unsigned long long emptyLinkWindowUs =
            emptyKnownFitsBullet ? emptyM3.bulletUs - emptyRawKnownBulletUs : 0ull;
        const bool emptyCarryInValid =
            gPerfM3CarryInFlushUs <= emptyM3.bulletUs &&
            gPerfM3CarryInFlushUs <= gPerfM3SpriteBackendUs &&
            gPerfM3CarryInDcacheUs <= gPerfM3CarryInFlushUs &&
            gPerfM3CarryInDcacheUs <= gPerfM3DcacheUs;
        const bool emptyCarryOutValid =
            gPerfM3CarryOutDcacheUs <= gPerfM3CarryOutFlushUs;
        const bool emptyTransferValid = emptyCarryInValid && emptyCarryOutValid;
        const unsigned long long emptyBtxUs =
            emptyTransferValid
                ? emptyM3.bulletUs - gPerfM3CarryInFlushUs +
                      gPerfM3CarryOutFlushUs
                : 0ull;
        const unsigned long long emptySourceBackendUs =
            emptyTransferValid
                ? gPerfM3SpriteBackendUs - gPerfM3CarryInFlushUs +
                      gPerfM3CarryOutFlushUs
                : 0ull;
        const unsigned long long emptySourceDcacheUs =
            emptyTransferValid
                ? gPerfM3DcacheUs - gPerfM3CarryInDcacheUs +
                      gPerfM3CarryOutDcacheUs
                : 0ull;
        const bool emptySourceBackendValid =
            emptySourceBackendUs >= emptySourceDcacheUs;
        const unsigned long long emptySourceRepackUs =
            emptySourceBackendValid ? emptySourceBackendUs - emptySourceDcacheUs
                                    : 0ull;
        const unsigned long long emptyDetailSumUs =
            emptyLinkWindowUs + emptyVmWindowUs + emptyEmitterWindowUs +
            emptySourceRepackUs + emptySourceDcacheUs;
        const unsigned long long emptyDetailErrorUs =
            emptyBtxUs >= emptyDetailSumUs ? emptyBtxUs - emptyDetailSumUs
                                           : emptyDetailSumUs - emptyBtxUs;
        const unsigned long long emptyDetailLimitUs =
            std::max<unsigned long long>(200ull * mPerfFrames,
                                         emptyBtxUs / 50ull);
        unsigned int emptyUnresolved = gPerfM3TransferUnresolved;
        emptyUnresolved += Th07PspM3FrontBatchUnresolved();
        if (gPerfM3DeferredBatchOrigin != TH07_PSP_PERF_M3_BATCH_NONE ||
            gPerfM3WrapperActive || gPerfM3CarryInPending)
        {
            ++emptyUnresolved;
        }
        acceptProfileValid =
            acceptProfileValid && emptyClosureErrorUs <= emptyClosureLimitUs &&
            emptyInternalErrorUs <= emptyClosureLimitUs &&
            emptyM3.frames == mPerfFrames && emptySamplesValid &&
            emptyFrontendValid && emptyEmitterValid && emptyBackendValid &&
            emptyKnownFitsBullet && emptyTransferValid &&
            emptySourceBackendValid && emptyDetailErrorUs <= emptyDetailLimitUs &&
            (emptyEmitter.samples == 0u ||
             (gPerfM3TimerReadQ8 != 0u && gPerfM3TimerReadQ8 <= 4096ull)) &&
            emptyEmitter.phaseMismatches == 0u &&
            gPerfM3TransferMixed == 0u && emptyUnresolved == 0u &&
            gPerfDrawOutOfRange == 0u;
        PerfM3ClearWindowTransferCounters();
#endif
        const unsigned int critical10 = cpu10 + ge10;
        const unsigned int criticalAverageUs = static_cast<unsigned int>(
            (mPerfCpuUs + mPerfGeUs) / mPerfFrames);
        // This entire observer extension is feature-owned so frozen RID30,
        // C1 and C2 builds retain their byte-for-byte legacy ACCEPT format.
#if defined(TH07_PSP_PERF_PLAYER_SHOT)
        // M is the existing post-cache matrix submission count, normalized
        // per frame.  The player-shot fields are raw window totals: PSD is
        // microseconds, PSN active state-1 visits, PSF frontend calls.
        const unsigned int matrices10 =
            mMatrixSubmissions * 10u / mPerfFrames;
#endif
#if defined(TH07_PSP_PERF_PLAYER_SHOT)
        char acceptMessage[320];
#else
        char acceptMessage[256];
#endif
        std::snprintf(
            acceptMessage, sizeof(acceptMessage),
            "PERF ACCEPT S%d ST%d N%u AVG%u.%u MAX%u.%u P99%u.%u OVR%u MISS%u "
            "AVGUS%u MAXUS%u P99US%u "
#if defined(TH07_PSP_PERF_PLAYER_SHOT)
            "M%u.%u PSD%llu PSN%llu PSF%u "
#endif
            "H%u/%u/%u/%u/%u/%u/%u/%u/%u/%u V%d",
            mPerfWindowState, mPerfWindowStage, mPerfFrames,
            critical10 / 10, critical10 % 10, max10 / 10, max10 % 10,
            p9910 / 10, p9910 % 10, mPerfOverBudgetFrames, mPerfVsyncMisses,
            criticalAverageUs, static_cast<unsigned int>(mPerfMaxFrameUs), p99Us,
#if defined(TH07_PSP_PERF_PLAYER_SHOT)
            matrices10 / 10, matrices10 % 10,
            gPerfPlayerShotFrontendUs, gPerfPlayerShotActiveCount,
            gPerfPlayerShotFrontendCalls,
#endif
            mPerfCriticalHistogram[0], mPerfCriticalHistogram[1],
            mPerfCriticalHistogram[2], mPerfCriticalHistogram[3],
            mPerfCriticalHistogram[4], mPerfCriticalHistogram[5],
            mPerfCriticalHistogram[6], mPerfCriticalHistogram[7],
            mPerfCriticalHistogram[8], mPerfCriticalHistogram[9],
            acceptProfileValid);
        th07_psp_perf_note(acceptMessage);
#endif

        ResetPerfWindowCounters();
    }

    void ResetPerfWindowCounters()
    {
        mPerfStartUs = 0;
        mPerfLastGeEndUs = 0;
        mPerfCpuUs = 0;
        mPerfGeUs = 0;
        mPerfVblankUs = 0;
        mPerfBlockingGeUs = 0;
        mPerfMaxFrameUs = 0;
#if defined(TH07_PSP_PERF_DETAIL)
        // Snapshot cumulative renderer counters at the window boundary.
        // Taking this snapshot on the first completed frame would silently
        // omit that frame and under-report D/VI/V by 1/N.
        mPerfStartDraws = mDraws;
        mPerfStartInputVertices = mInputVertices;
        mPerfStartVertices = mVertices;
#endif
        mPerfFrames = 0;
        mPerfLists = 0;
        mMaxListBytes = 0;
        mTextureUploadBytes = 0;
        mSurfaceCacheHits = 0;
        mSurfaceCacheMisses = 0;
        mPerfMaxBullets = 0;
        mPerfMaxEffects = 0;
        mMatrixSubmissions = 0;
        mCachedQuadIndexBatches = 0;
        std::memset(mPerfCriticalSamplesUs, 0, sizeof(mPerfCriticalSamplesUs));
        std::memset(mPerfCriticalHistogram, 0, sizeof(mPerfCriticalHistogram));
        mPerfOverBudgetFrames = 0;
        mPerfVsyncMisses = 0;
        gPerfCalcChainUs = 0;
        gPerfDrawChainUs = 0;
        gPerfDrawChainOverheadUs = 0;
        gPerfStageDrawUs = 0;
#if defined(TH07_PSP_PERF_PLAYER_SHOT)
        gPerfPlayerShotFrontendUs = 0;
        gPerfPlayerShotActiveCount = 0;
        gPerfPlayerShotFrontendCalls = 0;
#endif
        for (unsigned long long &jobUs : gPerfCalcJobUs)
        {
            jobUs = 0;
        }
        for (unsigned long long &jobUs : gPerfDrawJobUs)
        {
            jobUs = 0;
        }
        for (unsigned long long &jobUs : gPerfDrawJobGpuUs)
        {
            jobUs = 0;
        }
#if defined(TH07_PSP_PERF_M2)
        for (unsigned int ownerIndex = 0; ownerIndex < gPerfDrawOwnerCount; ++ownerIndex)
        {
            gPerfDrawOwners[ownerIndex].elapsedUs = 0;
            gPerfDrawOwners[ownerIndex].calls = 0;
            std::memset(gPerfDrawOwners[ownerIndex].internalUs, 0,
                        sizeof(gPerfDrawOwners[ownerIndex].internalUs));
        }
        gPerfInternalMismatch = 0;
        gPerfInternalTracker = PerfInternalTracker{};
#endif
#if defined(TH07_PSP_PERF_M2)
        unsigned int discardedAxisEligible = 0;
        unsigned int discardedFallbackEligible = 0;
        unsigned int discardedCullRejects = 0;
        Th07PspTakeBulletDrawPerf(&discardedAxisEligible,
                                 &discardedFallbackEligible,
                                 &discardedCullRejects);
#elif defined(TH07_PSP_PERF_M3)
        Th07PspM3PerfWindow discardedM3{};
        Th07PspM3EmitterWindow discardedEmitter{};
        Th07PspTakeM3PerfWindow(&discardedM3);
        Th07PspTakeM3EmitterPerf(&discardedEmitter);
        const bool dirtyM3Boundary =
            gPerfM3BulletLoopActive || gPerfM3CarryInPending ||
            gPerfM3WrapperActive ||
            gPerfM3DeferredBatchOrigin != TH07_PSP_PERF_M3_BATCH_NONE ||
            (mDeferredSpriteVertices && mDeferredSpriteVertexCount != 0) ||
            Th07PspM3FrontBatchUnresolved() != 0u;
        PerfM3ClearWindowTransferCounters();
        gPerfM3CarryInPending = false;
        gPerfM3IncomingBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
        if (!mDeferredSpriteVertices || mDeferredSpriteVertexCount == 0)
        {
            gPerfM3DeferredBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
        }
        if (!gPerfM3WrapperActive)
        {
            gPerfM3WrapperOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
            gPerfM3WrapperDcacheUs = 0;
        }
        if (dirtyM3Boundary)
        {
            // A stage-load batch crossing into a gameplay window must not be
            // silently reclassified.  Latch the next window invalid.
            gPerfM3TransferUnresolved = 1;
        }
        gPerfM3TimerReadQ8 = PerfM3CalibrateTimerReadQ8();
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        Th07PspDenseSliceWindow discardedDense{};
        Th07PspTakeDenseSliceWindow(&discardedDense);
        Th07PspEnemyP5WarmWindow discardedEnemyP5{};
        Th07PspTakeEnemyP5WarmWindow(&discardedEnemyP5);
        gPerfDensePostFlushUs = 0;
        gPerfDenseSwapSubmitUs = 0;
        gPerfDenseCalcFrames = 0;
        gPerfDenseDrawFrames = 0;
        gPerfDensePostFlushFrames = 0;
        gPerfDenseSwapFrames = 0;
#if defined(TH07_PSP_ME_RENDER_WORKER)
        Th07PspMeRenderShadowWindow discardedMerw{};
        Th07PspTakeMeRenderShadowWindow(&discardedMerw);
        unsigned int discardedMeAudioJobs = 0u;
        unsigned int discardedMeAudioFallbacks = 0u;
        unsigned int discardedMeAudioTimeouts = 0u;
        unsigned int discardedMeAudioMaxWaitUs = 0u;
        th07_psp_me_audio_diag_window(
            &discardedMeAudioJobs, &discardedMeAudioFallbacks,
            &discardedMeAudioTimeouts, &discardedMeAudioMaxWaitUs);
        gPerfMerwPostCalcUs = 0ull;
        gPerfMerwPostCalcFrames = 0u;
#endif
        ConfigurePerfDenseSliceForNextWindow();
#endif
        gPerfDrawOutOfRange = 0;
    }
#endif

    bool EnsureListSpace(unsigned int vertexBytes)
    {
        if (vertexBytes + kListReserve >= kListBytes)
        {
            return false;
        }
        const unsigned int used = static_cast<unsigned int>(std::max(sceGuCheckList(), 0));
        if (used + ((vertexBytes + 3u) & ~3u) + kListReserve >= kListBytes)
        {
            SubmitAndRestart();
        }
        return true;
    }

    void ApplyViewport()
    {
        const int logicalX = static_cast<int>(mViewport.x);
        const int logicalY = static_cast<int>(mViewport.y);
        const int logicalWidth = std::max(1, static_cast<int>(mViewport.width));
        const int logicalHeight = std::max(1, static_cast<int>(mViewport.height));
        const int contentWidth = g_Supervisor.cfg.windowed ? kFitWidth : kScreenWidth;
        const int contentLeft = g_Supervisor.cfg.windowed ? kFitLeft : 0;
        int x = contentLeft + logicalX * contentWidth / kLogicalWidth;
        int right = contentLeft +
                    (logicalX + logicalWidth) * contentWidth / kLogicalWidth;
        int top = logicalY * kScreenHeight / kLogicalHeight;
        int bottom = ((logicalY + logicalHeight) * kScreenHeight + kLogicalHeight - 1) /
                     kLogicalHeight;
        x = std::max(0, std::min(kScreenWidth - 1, x));
        right = std::max(x + 1, std::min(kScreenWidth, right));
        top = std::max(0, std::min(kScreenHeight - 1, top));
        bottom = std::max(top + 1, std::min(kScreenHeight, bottom));
        int width = right - x;
        int height = bottom - top;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        if (mLowResStagePass)
        {
            // The whole stage pass renders into the half-resolution target;
            // every pixel mapping shrinks uniformly.
            x /= 2;
            top /= 2;
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
        }
#endif
        int scissorX = x;
        int scissorTop = top;
        int scissorWidth = width;
        int scissorHeight = height;
        if (mStagePlayfieldScissor)
        {
            // Logical playfield: x 32..416, y 16..464 (the exact rectangle the
            // stage's own background clear uses).  Mapped with the identical
            // arithmetic as the viewport above.
            const int fieldLeft =
                contentLeft + 32 * contentWidth / kLogicalWidth;
            const int fieldRight =
                contentLeft + 416 * contentWidth / kLogicalWidth;
            const int fieldTop = 16 * kScreenHeight / kLogicalHeight;
            const int fieldBottom =
                (464 * kScreenHeight + kLogicalHeight - 1) / kLogicalHeight;
            const int clippedLeft = std::max(scissorX, fieldLeft);
            const int clippedTop = std::max(scissorTop, fieldTop);
            const int clippedRight =
                std::min(scissorX + scissorWidth, fieldRight);
            const int clippedBottom =
                std::min(scissorTop + scissorHeight, fieldBottom);
            scissorX = clippedLeft;
            scissorTop = clippedTop;
            scissorWidth = std::max(0, clippedRight - clippedLeft);
            scissorHeight = std::max(0, clippedBottom - clippedTop);
        }
        sceGuOffset(2048 - (x + width / 2), 2048 - (top + height / 2));
        sceGuViewport(2048, 2048, width, height);
        sceGuScissor(scissorX, scissorTop, scissorWidth, scissorHeight);
        sceGuEnable(GU_SCISSOR_TEST);
    }

    void ClearPillarboxes()
    {
        if (!g_Supervisor.cfg.windowed)
        {
            return;
        }
        sceGuOffset(2048 - kScreenWidth / 2, 2048 - kScreenHeight / 2);
        sceGuViewport(2048, 2048, kScreenWidth, kScreenHeight);
        sceGuClearColor(0xff000000u);
        sceGuEnable(GU_SCISSOR_TEST);
        sceGuScissor(0, 0, kFitLeft, kScreenHeight);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuScissor(kFitLeft + kFitWidth, 0,
                     kScreenWidth - (kFitLeft + kFitWidth), kScreenHeight);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuClearColor(ToGuColor(mClearColor));
        ApplyViewport();
    }

    void ApplyMatrices(bool screenSpace)
    {
        const int mode = screenSpace ? 1 : 0;
        const bool modeChanged = mAppliedMatrixMode != mode;
        if (screenSpace && !modeChanged)
        {
            // Pending 3D changes do not affect already-transformed XYZRHW
            // vertices. Keep their bits for the next 3D mode switch.
            return;
        }
        if (!screenSpace && !modeChanged && mMatrixDirtyMask == 0)
        {
            return;
        }
#if defined(TH07_PSP_PERF_M2)
        PerfInternalScope matrixScope(TH07_PSP_PERF_INTERNAL_MATRIX);
#endif
        if (screenSpace)
        {
            sceGuSetMatrix(GU_MODEL, &kIdentityMatrix);
            sceGuSetMatrix(GU_VIEW, &kIdentityMatrix);
#if defined(TH07_PSP_PERF_DETAIL)
            mMatrixSubmissions += 2;
#endif

            const float left = static_cast<float>(mViewport.x);
            const float right = static_cast<float>(mViewport.x + mViewport.width);
            const float bottom = static_cast<float>(mViewport.y + mViewport.height);
            const float top = static_cast<float>(mViewport.y);
            constexpr float nearPlane = -10000.0f;
            constexpr float farPlane = 10000.0f;
            const float dx = right - left;
            const float dy = top - bottom;
            const float dz = farPlane - nearPlane;
            ScePspFMatrix4 projection;
            projection.x.x = 2.0f / dx;
            projection.x.y = 0.0f;
            projection.x.z = 0.0f;
            projection.x.w = 0.0f;
            projection.y.x = 0.0f;
            projection.w.x = -(right + left) / dx;
            projection.y.y = 2.0f / dy;
            projection.y.z = 0.0f;
            projection.y.w = 0.0f;
            projection.z.x = 0.0f;
            projection.z.y = 0.0f;
            projection.w.y = -(top + bottom) / dy;
            projection.z.z = -2.0f / dz;
            projection.z.w = 0.0f;
            projection.w.x = -(right + left) / dx;
            projection.w.y = -(top + bottom) / dy;
            projection.w.z = -(farPlane + nearPlane) / dz;
            projection.w.w = 1.0f;
            sceGuSetMatrix(GU_PROJECTION, &projection);
#if defined(TH07_PSP_PERF_DETAIL)
            ++mMatrixSubmissions;
#endif
        }
        else
        {
            if (modeChanged)
            {
                // Screen-space installed identity model/view and an
                // orthographic projection, so restore all three 3D matrices.
                mMatrixDirtyMask |= kAll3dMatrixDirtyBits;
            }
            static const int modes[3] = {GU_MODEL, GU_VIEW, GU_PROJECTION};
            for (int i = 0; i < 3; ++i)
            {
                if ((mMatrixDirtyMask & (1u << static_cast<unsigned int>(i))) == 0)
                {
                    continue;
                }
                const ScePspFMatrix4 matrix = ToGuMatrix(mTransforms[i]);
                sceGuSetMatrix(modes[i], &matrix);
#if defined(TH07_PSP_PERF_DETAIL)
                ++mMatrixSubmissions;
#endif
            }
            mMatrixDirtyMask = 0;
        }
        // The PSP path CPU-bakes UVs. Keep GE's texture matrix at identity,
        // but emit it only at a new-list/mode boundary, not for every card.
        if (modeChanged)
        {
            sceGuSetMatrix(GU_TEXTURE, &kIdentityMatrix);
#if defined(TH07_PSP_PERF_DETAIL)
            ++mMatrixSubmissions;
#endif
        }
        mAppliedMatrixMode = mode;
    }

    void ApplyTexture(bool textured, bool screenSpace)
    {
        CheckBoundTexture("draw");
        const bool enabled = textured && mBoundTexture != 0 &&
                             mTextures[mBoundTexture].pixels &&
                             !(mColorOpRgb == COLOR_OP_DISABLE &&
                               mColorOpAlpha == COLOR_OP_DISABLE);
        if (!enabled)
        {
            if (!mTextureEnableKnown || mTextureEnabled)
            {
                sceGuDisable(GU_TEXTURE_2D);
                mTextureEnableKnown = true;
                mTextureEnabled = false;
            }
            return;
        }
        const GuTexture &texture = mTextures[mBoundTexture];
        if (!mTextureEnableKnown || !mTextureEnabled)
        {
            sceGuEnable(GU_TEXTURE_2D);
            mTextureEnableKnown = true;
            mTextureEnabled = true;
        }
        if (mAppliedTexture != mBoundTexture || mAppliedScreenSpace != screenSpace)
        {
            sceGuTexMode(texture.psm, 0, 0, texture.swizzled ? GU_TRUE : GU_FALSE);
            sceGuTexImage(0, texture.storageWidth, texture.storageHeight, texture.storageWidth,
                          texture.pixels);
            sceGuTexScale(texture.sampleScaleX, texture.sampleScaleY);
            sceGuTexOffset(0.0f, 0.0f);
            // Match the final TH06 PSP backend and TH07's portable GLES
            // renderer.  The atlas UVs are inset to texel centers in
            // AnmManager::LoadSprite, which prevents linear filtering from
            // leaking adjacent HUD cells while retaining antialiased text
            // during the 640x480 -> PSP viewport shrink.
            sceGuTexFilter(GU_LINEAR, GU_LINEAR);
            mAppliedTexture = mBoundTexture;
            mAppliedScreenSpace = screenSpace;
        }
        int function = GU_TFX_MODULATE;
        if (mColorOpRgb == COLOR_OP_ADD)
        {
            function = GU_TFX_ADD;
        }
        else if (mColorOpRgb == COLOR_OP_REPLACE)
        {
            function = GU_TFX_REPLACE;
        }
        if (!mAppliedColorOpKnown || mAppliedColorOpRgb != mColorOpRgb)
        {
            sceGuTexFunc(function, GU_TCC_RGBA);
            mAppliedColorOpRgb = mColorOpRgb;
            mAppliedColorOpKnown = true;
        }
    }

    void CheckBoundTexture(const char *where)
    {
        if (mBoundTexture < kMaxTextures)
        {
            return;
        }

        const unsigned int corrupted = mBoundTexture;
        const unsigned int expected =
            g_AnmManager && g_AnmManager->currentTexture.id < kMaxTextures
                ? g_AnmManager->currentTexture.id
                : 0;
        char message[96];
        std::snprintf(message, sizeof(message), "texture bound corrupt %s %08x -> %u", where,
                      corrupted, expected);
        th07_psp_boot_note(message);
        mBoundTexture = expected;
        mError = true;
    }

    unsigned int SelectVertexColor(ZunColor diffuse) const
    {
        if (mTextureArg == TEX_ARG_TFACTOR)
        {
            return ToGuColor(mTextureFactor);
        }
        if (mTextureArg == TEX_ARG_TEXTURE)
        {
            return 0xffffffffu;
        }
        return ToGuColor(diffuse);
    }

    static void ReadSourcePixel(PixelFormat fmt, PixelDataType type, const void *data,
                                unsigned int index, unsigned int &r, unsigned int &g,
                                unsigned int &b, unsigned int &a)
    {
        const auto *bytes = static_cast<const unsigned char *>(data);
        const auto *words = static_cast<const unsigned short *>(data);
        r = g = b = 0;
        a = 255;
        if (type == PIXEL_UNSIGNED_BYTE)
        {
            const unsigned int components = fmt == PIXEL_RGB ? 3u : 4u;
            r = bytes[index * components + (fmt == PIXEL_BGRA ? 2u : 0u)];
            g = bytes[index * components + 1];
            b = bytes[index * components + (fmt == PIXEL_BGRA ? 0u : 2u)];
            if (components == 4)
            {
                a = bytes[index * components + 3];
            }
        }
        else if (type == PIXEL_UNSIGNED_SHORT_5_6_5)
        {
            const unsigned int value = words[index];
            r = ((value >> 11) & 31u) * 255u / 31u;
            g = ((value >> 5) & 63u) * 255u / 63u;
            b = (value & 31u) * 255u / 31u;
        }
        else if (type == PIXEL_UNSIGNED_SHORT_5_5_5_1)
        {
            const unsigned int value = words[index];
            r = ((value >> 11) & 31u) * 255u / 31u;
            g = ((value >> 6) & 31u) * 255u / 31u;
            b = ((value >> 1) & 31u) * 255u / 31u;
            a = (value & 1u) ? 255u : 0u;
        }
        else
        {
            const unsigned int value = words[index];
            // TH07 embeds D3DFMT_A4R4G4B4 words, not GL's RGBA4444
            // ordering implied by the portable enum name.
            a = ((value >> 12) & 15u) * 17u;
            r = ((value >> 8) & 15u) * 17u;
            g = ((value >> 4) & 15u) * 17u;
            b = (value & 15u) * 17u;
        }
    }

    static void WriteTexturePixel(GuTexture &texture, unsigned int index, unsigned int r,
                                  unsigned int g, unsigned int b, unsigned int a)
    {
        const unsigned int bytesPerPixel = texture.psm == GU_PSM_8888 ? 4u : 2u;
        const unsigned int x = index % texture.storageWidth;
        const unsigned int y = index / texture.storageWidth;
        const unsigned int rowBytes = texture.storageWidth * bytesPerPixel;
        const unsigned int byteX = x * bytesPerPixel;
        const unsigned int byteOffset = texture.swizzled
                                            ? (((y >> 3) * (rowBytes >> 4) + (byteX >> 4)) << 7) +
                                                  ((y & 7u) << 4) + (byteX & 15u)
                                            : y * rowBytes + byteX;
        auto *destination = static_cast<unsigned char *>(texture.pixels) + byteOffset;
        if (texture.psm == GU_PSM_8888)
        {
            *reinterpret_cast<unsigned int *>(destination) = r | (g << 8) | (b << 16) | (a << 24);
        }
        else if (texture.psm == GU_PSM_5650)
        {
            *reinterpret_cast<unsigned short *>(destination) = static_cast<unsigned short>(
                (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11));
        }
        else if (texture.psm == GU_PSM_5551)
        {
            *reinterpret_cast<unsigned short *>(destination) = static_cast<unsigned short>(
                (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >= 128u) << 15));
        }
        else
        {
            *reinterpret_cast<unsigned short *>(destination) = static_cast<unsigned short>(
                (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12));
        }
    }
};
} // namespace

#if defined(TH07_PSP_PERF_DENSE_SLICE)
int gTh07PspPerfDenseSliceActive = 0;
#endif

extern "C" int th07_psp_portrait_cache_snapshot(
    Th07PspPortraitCacheSnapshot *snapshot)
{
    if (!snapshot)
        return 0;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    Th07PspPortraitCacheSnapshot local{};
    for (unsigned int attempt = 0; attempt < 16u; ++attempt)
    {
        const unsigned int before =
            __atomic_load_n(&gPortraitSnapshotSequence, __ATOMIC_ACQUIRE);
        if (before & 1u)
            continue;
        std::memcpy(&local, &gPortraitSnapshot, sizeof(local));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const unsigned int after =
            __atomic_load_n(&gPortraitSnapshotSequence, __ATOMIC_ACQUIRE);
        if (before == after && !(after & 1u))
        {
            std::memcpy(snapshot, &local, sizeof(local));
            return (local.flags & TH07_PSP_PORTRAIT_CACHE_LEDGER_VALID) != 0;
        }
    }
    return 0;
#else
    std::memset(snapshot, 0, sizeof(*snapshot));
    return 0;
#endif
}

ZunGraphics *Th07CreatePspGuBackend()
{
    auto *backend = new PspGuGraphics();
    if (!backend->Init())
    {
        delete backend;
        return nullptr;
    }
    gPspGuBackend = backend;
    return backend;
}

void Th07PspDrawSpriteQuads(const Th07PspSpriteVertex *vertices,
                            unsigned int spriteCount)
{
#if defined(TH07_PSP_PERF_M3)
    Th07PspM3EmitterBackendBegin();
    const int batchOrigin = gPerfM3IncomingBatchOrigin;
    const bool bulletLoop = gPerfM3BulletLoopActive;
    const unsigned int mixedBefore = gPerfM3TransferMixed;
    const unsigned long long backendStartUs = sceKernelGetSystemTimeWide();
    gPerfM3WrapperActive = true;
    gPerfM3WrapperOrigin = batchOrigin;
    gPerfM3WrapperDcacheUs = 0;
#endif
    if (gPspGuBackend)
    {
        gPspGuBackend->DrawSpriteQuads(vertices, spriteCount, true);
    }
#if defined(TH07_PSP_PERF_M3)
    const unsigned long long backendUs =
        sceKernelGetSystemTimeWide() - backendStartUs;
    const unsigned long long wrapperDcacheUs = gPerfM3WrapperDcacheUs;
    gPerfM3WrapperActive = false;
    gPerfM3WrapperOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
    gPerfM3WrapperDcacheUs = 0;
    if (bulletLoop)
    {
        gPerfM3SpriteBackendUs += backendUs;
        ++gPerfM3SpriteBackendCalls;
    }
    if (batchOrigin == TH07_PSP_PERF_M3_BATCH_MIXED ||
        (batchOrigin == TH07_PSP_PERF_M3_BATCH_NONE && bulletLoop))
    {
        ++gPerfM3TransferMixed;
    }
    else if (gPerfM3TransferMixed == mixedBefore && bulletLoop &&
             batchOrigin == TH07_PSP_PERF_M3_BATCH_PRE)
    {
        gPerfM3CarryInFlushUs += backendUs;
        gPerfM3CarryInDcacheUs += wrapperDcacheUs;
    }
    else if (gPerfM3TransferMixed == mixedBefore && !bulletLoop &&
             batchOrigin == TH07_PSP_PERF_M3_BATCH_BULLET)
    {
        gPerfM3CarryOutFlushUs += backendUs;
        gPerfM3CarryOutDcacheUs += wrapperDcacheUs;
    }
    Th07PspM3EmitterBackendEnd();
#endif
}

#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
void Th07PspDrawSpriteQuadsUnified(const Th07PspSpriteVertex *vertices,
                                   unsigned int spriteCount)
{
    // This validation profile is intentionally incompatible with M3: changing
    // the primitive boundary invalidates its ownership model. Keep the release
    // wrapper above byte-for-byte on its original, collapse-enabled route and
    // expose a separate constant-false entry only to the bullet experiment.
    if (gPspGuBackend)
    {
        gPspGuBackend->DrawSpriteQuads(vertices, spriteCount, false);
    }
}
#endif

void Th07PspDrawSpritePairs(const Th07PspSpriteVertex *vertices, unsigned int spriteCount)
{
#if defined(TH07_PSP_PERF_M3)
    Th07PspM3EmitterBackendBegin();
    const int batchOrigin = gPerfM3IncomingBatchOrigin;
    const bool bulletLoop = gPerfM3BulletLoopActive;
    const unsigned int mixedBefore = gPerfM3TransferMixed;
    const unsigned long long backendStartUs = sceKernelGetSystemTimeWide();
    gPerfM3WrapperActive = true;
    gPerfM3WrapperOrigin = batchOrigin;
    gPerfM3WrapperDcacheUs = 0;
#endif
    if (gPspGuBackend)
    {
        gPspGuBackend->DrawSpritePairs(vertices, spriteCount);
    }
#if defined(TH07_PSP_PERF_M3)
    const unsigned long long backendUs =
        sceKernelGetSystemTimeWide() - backendStartUs;
    const unsigned long long wrapperDcacheUs = gPerfM3WrapperDcacheUs;
    gPerfM3WrapperActive = false;
    gPerfM3WrapperOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
    gPerfM3WrapperDcacheUs = 0;
    if (bulletLoop)
    {
        gPerfM3SpriteBackendUs += backendUs;
        ++gPerfM3SpriteBackendCalls;
    }
    if (batchOrigin == TH07_PSP_PERF_M3_BATCH_MIXED ||
        (batchOrigin == TH07_PSP_PERF_M3_BATCH_NONE && bulletLoop))
    {
        ++gPerfM3TransferMixed;
    }
    else if (gPerfM3TransferMixed == mixedBefore && bulletLoop &&
             batchOrigin == TH07_PSP_PERF_M3_BATCH_PRE)
    {
        gPerfM3CarryInFlushUs += backendUs;
        gPerfM3CarryInDcacheUs += wrapperDcacheUs;
    }
    else if (gPerfM3TransferMixed == mixedBefore && !bulletLoop &&
             batchOrigin == TH07_PSP_PERF_M3_BATCH_BULLET)
    {
        gPerfM3CarryOutFlushUs += backendUs;
        gPerfM3CarryOutDcacheUs += wrapperDcacheUs;
    }
    Th07PspM3EmitterBackendEnd();
#endif
}

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
bool Th07PspBeginMeRenderGeSubmission(unsigned int slot,
                                      unsigned int generation)
{
    return gPspGuBackend &&
           gPspGuBackend->BeginMeRenderGeSubmission(slot, generation);
}

#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
void Th07PspDrawMeRenderStreamRun(
    const Th07PspMeRenderStreamVertex *vertices,
    unsigned int vertexCount, unsigned int primitive)
#else
void Th07PspDrawMeRenderStreamRun(const Th07PspSpriteVertex *vertices,
                                  unsigned int vertexCount,
                                  unsigned int primitive)
#endif
{
    if (gPspGuBackend)
    {
        gPspGuBackend->DrawMeRenderStreamRun(vertices, vertexCount,
                                             primitive);
    }
}

void Th07PspEndMeRenderGeSubmission()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->EndMeRenderGeSubmission();
    }
}

void Th07PspFenceMeRenderBeforeMeShutdown()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->FenceMeRenderBeforeMeShutdown();
    }
}
#endif

#if defined(TH07_PSP_PERF_M2) || defined(TH07_PSP_PERF_M3)
void Th07PspPerfSetDrawOwner(int priority, unsigned long callbackAddress)
{
#if defined(TH07_PSP_PERF_M2)
    if (gPerfInternalTracker.ownerIndex >= 0)
    {
        PerfInternalAccumulate(sceKernelGetSystemTimeWide());
        if (gPerfInternalTracker.depth != 0u || gPerfInternalTracker.category != -1)
        {
            ++gPerfInternalMismatch;
        }
        gPerfInternalTracker = PerfInternalTracker{};
    }
    if (priority >= 0)
    {
        gPerfInternalTracker.ownerIndex =
            FindOrCreatePerfDrawOwner(priority, callbackAddress);
        gPerfInternalTracker.lastUs = sceKernelGetSystemTimeWide();
    }
#else
    (void)callbackAddress;
#endif
#if defined(TH07_PSP_PERF_M3)
    if (gPerfCurrentDrawOwner >= 0 && priority != gPerfCurrentDrawOwner && gPspGuBackend)
    {
        gPspGuBackend->PerfEndDrawOwner(gPerfCurrentDrawOwner);
    }
    gPerfCurrentDrawOwner = priority;
#endif
}
#endif

#if defined(TH07_PSP_PERF_M3)
void Th07PspPerfSetM3BulletLoop(int active)
{
    if (active)
    {
        Th07PspM3BulletBatchBegin();
        if (gPspGuBackend)
        {
            gPspGuBackend->PerfSetM3BulletLoop(true);
        }
        else
        {
            gPerfM3BulletLoopActive = true;
        }
    }
    else
    {
        Th07PspM3BulletBatchEnd();
        if (gPspGuBackend)
        {
            gPspGuBackend->PerfSetM3BulletLoop(false);
        }
        else
        {
            gPerfM3BulletLoopActive = false;
        }
    }
}

void Th07PspPerfSetM3BatchOrigin(int origin)
{
    if (origin < TH07_PSP_PERF_M3_BATCH_NONE ||
        origin > TH07_PSP_PERF_M3_BATCH_MIXED)
    {
        origin = TH07_PSP_PERF_M3_BATCH_MIXED;
    }
    gPerfM3IncomingBatchOrigin = origin;
}

void Th07PspPerfM3LatchUnresolved()
{
    ++gPerfM3TransferUnresolved;
}
#endif

#if defined(TH07_PSP_PERF_M2)
void Th07PspPerfInternalBegin(unsigned int category)
{
    if (gPerfInternalTracker.ownerIndex < 0)
    {
        return;
    }
    if (category >= kPerfInternalCategoryCount ||
        gPerfInternalTracker.depth >= sizeof(gPerfInternalTracker.stack) /
                                          sizeof(gPerfInternalTracker.stack[0]))
    {
        ++gPerfInternalMismatch;
        return;
    }
    PerfInternalAccumulate(sceKernelGetSystemTimeWide());
    gPerfInternalTracker.stack[gPerfInternalTracker.depth++] =
        gPerfInternalTracker.category;
    gPerfInternalTracker.category = static_cast<int>(category);
}

void Th07PspPerfInternalEnd(unsigned int category)
{
    if (gPerfInternalTracker.ownerIndex < 0)
    {
        return;
    }
    if (gPerfInternalTracker.depth == 0u ||
        gPerfInternalTracker.category != static_cast<int>(category))
    {
        ++gPerfInternalMismatch;
        return;
    }
    PerfInternalAccumulate(sceKernelGetSystemTimeWide());
    gPerfInternalTracker.category =
        gPerfInternalTracker.stack[--gPerfInternalTracker.depth];
}
#endif

void Th07PspForgetSurface(const void *pixels)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->ForgetSurface(pixels);
    }
}

void Th07PspAllowNextWideStaticTexture()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->AllowNextWideStaticTexture();
    }
}

#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
void Th07PspPrepareUpperPortraitTexture(Th07PspPortraitTextureRole portraitRole,
                                        unsigned int textureSlot)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->PrepareUpperPortraitTexture(portraitRole, textureSlot);
    }
}

void Th07PspCompleteUpperPortraitPrewarm(unsigned int stagePortraitCount)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->CompleteUpperPortraitPrewarm(stagePortraitCount);
    }
}

void Th07PspBeginLowResStagePass()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->BeginLowResStagePass();
    }
}

void Th07PspEndLowResStagePass()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->EndLowResStagePass();
    }
}
#endif

void Th07PspBeginStagePlayfieldScissor()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->BeginStagePlayfieldScissor();
    }
}

void Th07PspEndStagePlayfieldScissor()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->EndStagePlayfieldScissor();
    }
}

bool Th07PspGetTextureContentSize(GfxTextureHandle texture, unsigned int *width,
                                  unsigned int *height)
{
    return gPspGuBackend && gPspGuBackend->GetTextureContentSize(texture, width, height);
}

void Th07PspMarkTextTexture(GfxTextureHandle texture)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->MarkTextTexture(texture);
    }
}

void Th07PspBeginTextUploadBatch()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->BeginTextUploadBatch();
    }
}

void Th07PspEndTextUploadBatch()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->EndTextUploadBatch();
    }
}

void Th07PspCompactTextTexture(GfxTextureHandle texture)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->CompactTextTexture(texture);
    }
}

unsigned int Th07PspTrimTextureCache()
{
    return gPspGuBackend ? gPspGuBackend->TrimTextureCache() : 0;
}

bool Th07PspCaptureFramebufferToTexture(GfxTextureHandle texture, int srcLeft, int srcTop,
                                        int srcWidth, int srcHeight, int dstLeft, int dstTop,
                                        int dstWidth, int dstHeight)
{
    return gPspGuBackend &&
           gPspGuBackend->CaptureFramebufferToTexture(texture, srcLeft, srcTop, srcWidth,
                                                      srcHeight, dstLeft, dstTop, dstWidth,
                                                      dstHeight);
}

#if defined(TH07_PSP_PERF_DIAG)
void Th07PspPerfAddCalcTime(unsigned long long elapsedUs)
{
    gPerfCalcChainUs += elapsedUs;
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    ++gPerfDenseCalcFrames;
#endif
}

void Th07PspPerfAddDrawTime(unsigned long long elapsedUs)
{
    gPerfDrawChainUs += elapsedUs;
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    ++gPerfDenseDrawFrames;
#endif
}

#if defined(TH07_PSP_PERF_DENSE_SLICE)
void Th07PspPerfAddDensePostFlushTime(unsigned long long elapsedUs)
{
    gPerfDensePostFlushUs += elapsedUs;
    ++gPerfDensePostFlushFrames;
}
#if defined(TH07_PSP_ME_RENDER_WORKER)
void Th07PspPerfAddMerwPostCalcTime(unsigned long long elapsedUs)
{
    gPerfMerwPostCalcUs += elapsedUs;
    ++gPerfMerwPostCalcFrames;
}
#endif
#endif

void Th07PspPerfAddStageTime(unsigned long long elapsedUs)
{
    gPerfStageDrawUs += elapsedUs;
}

void Th07PspPerfAddCalcJobTime(int priority, unsigned long long elapsedUs)
{
    if (priority >= 0 && priority < static_cast<int>(sizeof(gPerfCalcJobUs) /
                                                     sizeof(gPerfCalcJobUs[0])))
    {
        gPerfCalcJobUs[priority] += elapsedUs;
    }
}

void Th07PspPerfPhaseGpuSync(int priority)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->PerfPhaseGpuSync(priority);
    }
}

void Th07PspPerfBeginGameplayWindow(int stage)
{
    th07_psp_perf_set_gameplay_active(1);
    if (gPspGuBackend)
    {
        gPspGuBackend->PerfBeginGameplayWindow(stage);
    }
}

void Th07PspPerfFinalizeGameplayWindow()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->PerfFinalizeGameplayWindow();
    }
    th07_psp_perf_set_gameplay_active(0);
}

void Th07PspPerfAddDrawJobTime(int priority, unsigned long callbackAddress,
                               unsigned long long elapsedUs)
{
    if (priority >= 0 && priority < static_cast<int>(sizeof(gPerfDrawJobUs) /
                                                     sizeof(gPerfDrawJobUs[0])))
    {
        gPerfDrawJobUs[priority] += elapsedUs;
    }
    else
    {
        ++gPerfDrawOutOfRange;
    }
#if defined(TH07_PSP_PERF_M2)
    const int ownerIndex = FindOrCreatePerfDrawOwner(priority, callbackAddress);
    if (ownerIndex >= 0)
    {
        PerfDrawOwnerSlot &owner = gPerfDrawOwners[ownerIndex];
        owner.elapsedUs += elapsedUs;
        ++owner.calls;
    }
#else
    (void)callbackAddress;
#endif
}

void Th07PspPerfAddDrawChainOverheadTime(unsigned long long elapsedUs)
{
    gPerfDrawChainOverheadUs += elapsedUs;
}

#if defined(TH07_PSP_PERF_PLAYER_SHOT)
void Th07PspPerfAddPlayerShotFrontendTime(unsigned long long elapsedUs,
                                          unsigned int activeShotCount)
{
    gPerfPlayerShotFrontendUs += elapsedUs;
    gPerfPlayerShotActiveCount += activeShotCount;
    ++gPerfPlayerShotFrontendCalls;
}
#endif
#endif
