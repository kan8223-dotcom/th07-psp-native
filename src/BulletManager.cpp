#include "BulletManager.hpp"

#include "AnmIdx.hpp"
#include "AnmManager.hpp"
#include "AsciiManager.hpp"
#include "Chain.hpp"
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
#include "EffectManager.hpp"
#endif
#include "GameManager.hpp"
#if defined(TH07_PSP_BULLET_WARM_QUEUE) || \
    defined(TH07_PSP_ME_RENDER_PERFORMANCE)
#include "GameWindow.hpp"
#endif
#include "ItemManager.hpp"
#include "Player.hpp"
#include "PspBulletCollisionBroadphase.hpp"
#include "ReplayManager.hpp"
#include "PspBulletRender.hpp"
#include "Rng.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "ZunMath.hpp"
#include "utils.hpp"

#if defined(TH07_PSP)
#include <pspmath.h>
#if defined(TH07_PSP_PERF_M3) || defined(TH07_PSP_PERF_DENSE_SLICE) || \
    defined(TH07_PSP_ME_RENDER_WORKER)
#include <pspkernel.h>
#include "../psp/usage_meter.h" /* [FABLE] ME実測cycle供給（未定義時は空マクロ） */
#include "../psp/graphics/PspGuGraphics.hpp"
#endif

#if defined(TH07_PSP_ME_RENDER_WORKER)
#include "../psp/audio_me.h"
#include <cstring>
#endif

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME) && \
    !defined(TH07_PSP_ME_RENDER_CORRECTNESS)
#error TH07_PSP_ME_RENDER_GE_CONSUME requires the exact I-ME1 stream ABI
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME) && \
    !defined(TH07_PSP_BULLET_UNIFIED_QUADS)
#error TH07_PSP_ME_RENDER_GE_CONSUME requires callback-wide UQ semantics
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE) && \
    !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
#error TH07_PSP_ME_RENDER_PERFORMANCE requires direct GE consumption
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE) && \
    !defined(TH07_PSP_ME_RENDER_CORRECTNESS)
#error TH07_PSP_ME_RENDER_PERFORMANCE requires the exact stream owner
#endif
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE) && \
    !defined(TH07_PSP_ME_RENDER_PERFORMANCE)
#error TH07_PSP_ME_RENDER_RAW_LIVE requires the I-ME3 performance owner
#endif
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE) && defined(TH07_PSP_1000)
#error TH07_PSP_ME_RENDER_RAW_LIVE requires the contiguous PSP-2000+ Bullet pool
#endif
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST) && \
    !defined(TH07_PSP_ME_RENDER_RAW_LIVE)
#error TH07_PSP_ME_RENDER_DIRECT_LIST requires the I-ME4 raw-live owner
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE) && \
    !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#error TH07_PSP_ME_BULLET_FAST_UPDATE requires the I-ME5 direct-list owner
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE) && \
    !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#error TH07_PSP_ME_BULLET_COMPACT_UPDATE requires the I-ME5 direct-list owner
#endif
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY) && \
    !defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
#error TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY requires I-ME7 compact seeds
#endif
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY) && \
    !defined(TH07_PSP_ME_RENDER_PERFORMANCE)
#error TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY requires the mutation epoch owner
#endif
#if defined(TH07_PSP_BULLET_COLLISION_BROADPHASE) && \
    !defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
#error TH07_PSP_BULLET_COLLISION_BROADPHASE is reviewed only with I-ME7 compact
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM) && \
    !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#error TH07_PSP_ME_ITEM_RENDER_STREAM requires the I-ME5 direct-list owner
#endif

#if defined(TH07_PSP_BULLET_QUIESCENT_ANM) || \
    defined(TH07_PSP_BULLET_WARM_QUEUE) || \
    defined(TH07_PSP_BULLET_STATIC_PROXY) || \
    defined(TH07_PSP_ME_RENDER_WORKER)
#include "../psp/fileio.hpp"
#endif

#if defined(TH07_PSP_1000)
#if !defined(TH07_PSP_BULLET_QUIESCENT_ANM) && \
    !defined(TH07_PSP_BULLET_WARM_QUEUE)
#include "../psp/fileio.hpp"
#endif
#include "../psp/psp1000_arena.hpp"
#endif

#if defined(TH07_PSP_1000) || defined(TH07_PSP_BULLET_WARM_QUEUE) || \
    defined(TH07_PSP_BULLET_STATIC_PROXY)
#include <cstdlib>
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE) || \
    defined(TH07_PSP_BULLET_STATIC_PROXY)
#include <malloc.h>
#endif

namespace
{
#if defined(TH07_PSP_PERF_M3)
Th07PspM3PerfWindow gPspM3PerfWindow{};
unsigned int gPspM3BulletDrawOrdinal = 0;
constexpr unsigned int kPspM3BulletDrawSampleStride = 32u;
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
Th07PspDenseSliceWindow gPspDenseSliceWindow{};
#endif
#if defined(TH07_PSP_ME_RENDER_WORKER)
// M-ME0B owns one process-lifetime Main-RAM shadow slot.  This is sufficient
// for deadline measurement because output is never handed to GE.  I-ME1 must
// replace SYNTH4 with the separately reviewed triple-buffered 2/4-vertex UQ
// ABI before any rendered output can be consumed.
struct alignas(64) PspMeRenderShadowRecord64
{
    Th07PspMeRenderRecord32 geometry;
    u32 uv0Bits;
    u32 uv1Bits;
    u32 vv0Bits;
    u32 vv1Bits;
    u32 zBits;
    u32 sourceAndState;
    u32 observableFlags;
    u32 slot;
};

static_assert(sizeof(PspMeRenderShadowRecord64) == 64u,
              "M0B shadow record must model the 64-byte input candidate");

constexpr u32 kPspMeRenderSlackSamples = 256u;

struct PspMeRenderShadowState
{
    u32 available;
    bool managerActive;
    bool pending;
    bool deadlineAccounted;
    u32 calcCompleteSerial;
    u32 frameSeq;
    u32 drawSeq;
    u32 stageEpoch;
    u32 managerEpoch;
    u32 pendingTargetDrawSeq;
    u32 pendingFrameSeq;
    u32 pendingStageEpoch;
    u32 pendingManagerEpoch;
    u32 pendingReplayEpoch;
    u32 pendingRecordCount;
    u32 pendingInputStride;
    u32 pendingOutputBytes;
    u32 pendingSignature;
    u32 pendingSubmitUs;
    u32 hardFaulted;
};

PspMeRenderShadowState gPspMeRenderShadow{};
Th07PspMeRenderShadowWindow gPspMeRenderShadowWindow{};
u32 gPspMeRenderSlackUs[kPspMeRenderSlackSamples]{};
u32 gPspMeRenderSlackCount = 0u;
u32 gPspMeRenderSlackOverflow = 0u;
u32 gPspMeRenderKernelCycles[kPspMeRenderSlackSamples]{};
u32 gPspMeRenderKernelCycleCount = 0u;
u32 gPspMeRenderKernelCycleOverflow = 0u;
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
struct PspMeRenderCorrectnessState
{
    bool pending;
    bool deadlineAccounted;
    bool compareActive;
    bool hardFaulted;
    Th07PspMeRenderStreamToken token;
    Th07PspMeRenderStreamJob job;
    const Th07PspMeRenderStreamRecord *records;
    Th07PspMeRenderStreamReady ready;
    u32 targetDrawSeq;
    u32 frameSeq;
    u32 stageEpoch;
    u32 managerEpoch;
    u32 replayEpoch;
    u32 recordCount;
    u32 globalSignature;
    u32 arcadeLeftBits;
    u32 arcadeTopBits;
    u32 viewportMinZBits;
    u32 viewportMaxZBits;
    u32 submitUs;
    u32 compareRecordIndex;
    u32 compareVertexCount;
    u32 compareRunCount;
    Th07PspSpriteVertex *canonicalStart;
    bool compareIdentityFault;
    bool identityDetailSet;
    u32 identityWord;
    u32 identityExpected;
    u32 identityActual;
    bool sawPairs;
    bool sawQuads;
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    bool geConsumeActive;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    // A priority-9 Effect prefix or priority-10 Item prefix may promote the
    // same token before the Bullet suffix.  One shared owner preserves the
    // exact Effect -> Item -> Bullet command order without asking a token in
    // GE_IN_FLIGHT to satisfy READY_SC validation again.
    bool prefixGeSubmissionOpen;
    u32 prefixValidatedTokenSlot;
    u32 prefixValidatedTokenGeneration;
    u32 prefixValidatedDrawSeq;
#endif
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    i32 managerBulletCount;
    i32 managerUpdateCount;
    i32 managerTimePrevious;
    u32 managerTimeSubFrameBits;
    i32 managerTimeCurrent;
    u32 managerMutationEpoch;
    u32 representativeSourceGeneration;
    Bullet *managerBucketHeads[6];
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    u32 itemPrepareSerial;
    u32 itemRecordCount;
    u32 itemTotalCount;
    Item *itemHead;
    Item *itemTail;
    Item *itemSuffixHead;
    Item *itemListTail;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    u32 effectPrepareSerial;
    u32 effectLayer0RecordCount;
    u32 effectLayer3RecordCount;
#endif
#endif
#endif
};

#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
// Conservative fixed-point load model.  RID22 established that the combined
// render+compact workload reaches the frame ceiling near 884 live bullets.
// Keep admission deterministic (current record counts only) and reserve 20%
// for run churn, cache variance and the audio owner.  The lagging meter is an
// OFF-only guard; it can never turn optional work on.
constexpr u32 kPspMeFrameCountTicks = 2775055u;
constexpr u32 kPspMeAdaptiveBudgetTicks =
    (kPspMeFrameCountTicks * 80u) / 100u;
constexpr u32 kPspMeAdaptiveFixedTicks = 120000u;
constexpr u32 kPspMeAdaptiveBulletTicks = 2400u;
constexpr u32 kPspMeAdaptiveItemTicks = 2800u;
constexpr u32 kPspMeAdaptiveEffectTicks = 3000u;

enum PspMeAdaptiveAuxAdmission
{
    PSP_ME_ADAPTIVE_AUX_ADMIT = 0,
    PSP_ME_ADAPTIVE_AUX_REJECT_BUDGET,
    PSP_ME_ADAPTIVE_AUX_REJECT_BUSY
};

PspMeAdaptiveAuxAdmission PspMeAdaptiveAuxAdmissionFor(
    u32 bulletRecords, u32 itemRecords, u32 effectRecords,
    unsigned long long *outPredictedTicks)
{
    const unsigned long long predicted =
        static_cast<unsigned long long>(kPspMeAdaptiveFixedTicks) +
        static_cast<unsigned long long>(bulletRecords) *
            kPspMeAdaptiveBulletTicks +
        static_cast<unsigned long long>(itemRecords) *
            kPspMeAdaptiveItemTicks +
        static_cast<unsigned long long>(effectRecords) *
            kPspMeAdaptiveEffectTicks;
    if (outPredictedTicks)
    {
        *outPredictedTicks = predicted;
    }
    if (predicted > kPspMeAdaptiveBudgetTicks)
    {
        return PSP_ME_ADAPTIVE_AUX_REJECT_BUDGET;
    }
    // One-frame delayed hardware measurement: veto only.  A stale low value
    // cannot admit work because the deterministic current-frame model above
    // has already had to pass.
    return th07_usage_meter_last_me_percent() < 85u
        ? PSP_ME_ADAPTIVE_AUX_ADMIT
        : PSP_ME_ADAPTIVE_AUX_REJECT_BUSY;
}

u32 PspMeAdaptiveItemPrefixCount(
    u32 bulletRecords, u32 itemRecords,
    PspMeAdaptiveAuxAdmission *outAdmission,
    unsigned long long *outFullPredictedTicks)
{
    unsigned long long fullPredicted = 0ull;
#if !defined(TH07_PSP_ME_ITEM_PREFIX_SPLIT)
    const PspMeAdaptiveAuxAdmission fullAdmission =
        PspMeAdaptiveAuxAdmissionFor(
            bulletRecords, itemRecords, 0u, &fullPredicted);
    if (outFullPredictedTicks)
    {
        *outFullPredictedTicks = fullPredicted;
    }
    if (fullAdmission == PSP_ME_ADAPTIVE_AUX_REJECT_BUSY)
    {
        if (outAdmission)
        {
            *outAdmission = fullAdmission;
        }
        return 0u;
    }
    if (outAdmission)
    {
        *outAdmission = fullAdmission;
    }
    return fullAdmission == PSP_ME_ADAPTIVE_AUX_ADMIT ? itemRecords : 0u;
#else
    fullPredicted =
        static_cast<unsigned long long>(kPspMeAdaptiveFixedTicks) +
        static_cast<unsigned long long>(bulletRecords) *
            kPspMeAdaptiveBulletTicks +
        static_cast<unsigned long long>(itemRecords) *
            kPspMeAdaptiveItemTicks;
    if (outFullPredictedTicks)
    {
        *outFullPredictedTicks = fullPredicted;
    }
    const unsigned long long base =
        static_cast<unsigned long long>(kPspMeAdaptiveFixedTicks) +
        static_cast<unsigned long long>(bulletRecords) *
            kPspMeAdaptiveBulletTicks;
    const u32 affordable = base < kPspMeAdaptiveBudgetTicks
        ? static_cast<u32>((kPspMeAdaptiveBudgetTicks - base) /
                           kPspMeAdaptiveItemTicks)
        : 0u;
    const u32 prefix = affordable < itemRecords ? affordable : itemRecords;
    if (prefix == 0u)
    {
        if (outAdmission)
        {
            *outAdmission = PSP_ME_ADAPTIVE_AUX_REJECT_BUDGET;
        }
        return 0u;
    }
    // Busy is an OFF-only veto even when the full list was over budget.  It
    // must guard the affordable prefix too; otherwise a 100%-busy prior frame
    // could still admit partial Item work.
    if (th07_usage_meter_last_me_percent() >= 85u)
    {
        if (outAdmission)
        {
            *outAdmission = PSP_ME_ADAPTIVE_AUX_REJECT_BUSY;
        }
        return 0u;
    }
    if (outAdmission)
    {
        *outAdmission = prefix == itemRecords
            ? PSP_ME_ADAPTIVE_AUX_ADMIT
            : PSP_ME_ADAPTIVE_AUX_REJECT_BUDGET;
    }
    return prefix;
#endif
}
#endif

PspMeRenderCorrectnessState gPspMeRenderCorrectness{};
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
// A RAW-LIVE job may never survive into the next BulletManager calc: ME reads
// the authoritative AnmVm/sprite objects that calc is about to mutate.  A
// normal late completion is waited at the draw deadline; a genuinely stuck or
// ownership-corrupt ME cannot safely return to gameplay and requires process
// teardown/cold boot.
constexpr u32 kPspMeRenderRawDeadlineTimeoutUs = 100000u;

[[noreturn]] void PspMeRenderRawFailStop(const char *reason)
{
    __atomic_store_n(&gPspMeRenderShadow.available, 0u, __ATOMIC_RELEASE);
    gPspMeRenderCorrectness.hardFaulted = true;
    th07_psp_boot_note(reason);
    sceKernelExitGame();
    for (;;)
    {
        sceKernelDelayThread(1000000u);
    }
}
#endif
Th07PspMeRenderStreamRun
    gPspMeRenderCanonicalRuns[TH07_PSP_ME_RENDER_STREAM_MAX_RUNS]{};
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
// Correctness-only SC reference output.  This is deliberately separate from
// the ME pool and from its pack helper: command-10 bytes must agree with an
// independently evaluated canonical draw before a C1 hardware experiment can
// claim even format correctness.
Th07PspMeRenderStreamVertex gPspMeRenderCanonicalPacked[
    TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES /
    TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES]{};
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
// I-ME3 captures the final calc-12 state while each Bullet is already hot.
// OnUpdate encounters slots in 0,1023..1 order and prepends each one to its
// collision bucket.  Writing each bucket backwards makes its occupied tail
// byte-for-byte equal to the final canonical linked-list order, so post-calc
// needs only six contiguous copies and keeps the existing 64-byte ME ABI.
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
using PspMeRenderCaptureRecord = Th07PspMeRenderRawRecord;
static_assert(sizeof(void *) == 4u,
              "I-ME4 raw-live ABI requires 32-bit PSP pointers");
static_assert(sizeof(AnmVm) == 588u,
              "I-ME4 AnmVm layout changed; re-audit the ME reader");
static_assert(sizeof(AnmLoadedSprite) == 64u,
              "I-ME4 sprite layout changed; re-audit the ME reader");
static_assert(sizeof(Bullet) == 3452u,
              "I-ME4 Bullet stride changed; re-audit raw ownership");
static_assert(sizeof(Th07PspMeRenderRawRecord) == 32u,
              "I-ME4 capture record must remain 32 bytes");
static_assert(__builtin_offsetof(AnmVm, rotation.z) == 8u &&
                  __builtin_offsetof(AnmVm, scale.x) == 24u &&
                  __builtin_offsetof(AnmVm, scale.y) == 28u &&
                  __builtin_offsetof(AnmVm, uvScrollPos.x) == 40u &&
                  __builtin_offsetof(AnmVm, uvScrollPos.y) == 44u &&
                  __builtin_offsetof(AnmVm, color) == 440u &&
                  __builtin_offsetof(AnmVm, color2) == 444u &&
                  __builtin_offsetof(AnmVm, flags) == 448u &&
                  __builtin_offsetof(AnmVm, sprite) == 484u,
              "I-ME4 AnmVm field offsets changed");
static_assert(__builtin_offsetof(AnmLoadedSprite, sourceFileIndex) == 0u &&
                  __builtin_offsetof(AnmLoadedSprite, uvStart.x) == 28u &&
                  __builtin_offsetof(AnmLoadedSprite, uvStart.y) == 32u &&
                  __builtin_offsetof(AnmLoadedSprite, uvEnd.x) == 36u &&
                  __builtin_offsetof(AnmLoadedSprite, uvEnd.y) == 40u &&
                  __builtin_offsetof(AnmLoadedSprite, heightPx) == 44u &&
                  __builtin_offsetof(AnmLoadedSprite, widthPx) == 48u,
              "I-ME4 sprite field offsets changed");
static_assert(__builtin_offsetof(Bullet, sprites.spriteBullet) == 0u &&
                  __builtin_offsetof(
                      Bullet, sprites.spriteSpawnEffectFast) == 588u &&
                  __builtin_offsetof(
                      Bullet, sprites.spriteSpawnEffectNormal) == 1176u &&
                  __builtin_offsetof(
                      Bullet, sprites.spriteSpawnEffectSlow) == 1764u &&
                  __builtin_offsetof(
                      Bullet, sprites.spriteSpawnEffectDonut) == 2352u,
              "I-ME4 embedded Bullet VM ownership changed");
static_assert(__builtin_offsetof(Th07PspMeRenderRawRecord, vmPhys) == 16u &&
                  __builtin_offsetof(
                      Th07PspMeRenderRawRecord, logicalState) == 20u &&
                  __builtin_offsetof(Th07PspMeRenderRawRecord, slot) == 24u &&
                  __builtin_offsetof(
                      Th07PspMeRenderRawRecord, generation) == 28u,
              "I-ME4 raw record field offsets changed");
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
static_assert(sizeof(Th07PspMeRenderListLayout) == 128u,
              "I-ME5 direct-list layout ABI changed");
static_assert(__builtin_offsetof(Bullet, sprites.collisionType) == 2954u &&
                  __builtin_offsetof(Bullet, pos.x) == 2956u &&
                  __builtin_offsetof(Bullet, pos.y) == 2960u &&
                  __builtin_offsetof(Bullet, state) == 3068u &&
                  __builtin_offsetof(Bullet, next) == 3076u &&
                  __builtin_offsetof(Bullet, pspRenderAngle) == 3436u &&
                  __builtin_offsetof(Bullet, pspRenderSin) == 3440u &&
                  __builtin_offsetof(Bullet, pspRenderCos) == 3444u &&
                  __builtin_offsetof(Bullet, pspRenderRotationValid) == 3448u,
              "I-ME5 Bullet field offsets changed; re-audit list traversal");
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
static_assert(sizeof(Item) == 648u,
              "I-ME7 Item stride changed; re-audit list traversal");
static_assert(sizeof(Th07PspMeRenderItemLayout) == 128u,
              "I-ME7 Item direct-list layout ABI changed");
static_assert(__builtin_offsetof(Item, sprite) == 0u &&
                  __builtin_offsetof(Item, isInUse) == 637u &&
                  __builtin_offsetof(Item, itemType) == 636u &&
                  __builtin_offsetof(Item, next) == 644u &&
                  __builtin_offsetof(AnmVm, pos.x) == 456u &&
                  __builtin_offsetof(AnmVm, pos.y) == 460u &&
                  __builtin_offsetof(AnmVm, pos.z) == 464u,
              "I-ME7 Item/VM field offsets changed; re-audit Item reader");
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
static_assert(sizeof(Th07PspMeBulletFastLayout) == 152u &&
                  sizeof(Th07PspMeBulletFastJob) == 216u &&
                  sizeof(Th07PspMeBulletFastSlotResult) == 16u &&
                  sizeof(Th07PspMeBulletFastOutput) == 16512u &&
                  sizeof(Th07PspMeBulletFastCompletion) == 68u,
              "I-ME6 fast-update ABI size changed");
static_assert(TH07_PSP_ME_BULLET_FAST_MAX_SLOTS ==
                      BulletManager::kBulletCapacity &&
                  TH07_PSP_ME_BULLET_FAST_ACTIVE_WORDS == 32u &&
                  sizeof(BombClearBox) == 32u &&
                  sizeof(BulletCommand) == 24u,
              "I-ME6 owner stride/count changed");
static_assert(__builtin_offsetof(Bullet, state) == 3068u &&
                  __builtin_offsetof(Bullet, pos.x) == 2956u &&
                  __builtin_offsetof(Bullet, pos.y) == 2960u &&
                  __builtin_offsetof(Bullet, pos.z) == 2964u &&
                  __builtin_offsetof(Bullet, velocity.x) == 2968u &&
                  __builtin_offsetof(Bullet, velocity.y) == 2972u &&
                  __builtin_offsetof(Bullet, velocity.z) == 2976u &&
                  __builtin_offsetof(Bullet, spawnDelay) == 3056u &&
                  __builtin_offsetof(Bullet, exFlags) == 3060u &&
                  __builtin_offsetof(Bullet, outOfBoundsTime) == 3070u &&
                  __builtin_offsetof(Bullet, curCmdIdx) == 3088u &&
                  __builtin_offsetof(Bullet, commands) == 3092u &&
                  __builtin_offsetof(Bullet, sprites.grazeSize.x) == 2940u &&
                  __builtin_offsetof(Bullet, sprites.grazeSize.y) == 2944u &&
                  __builtin_offsetof(Bullet, sprites.spriteBullet.sprite) ==
                      484u,
              "I-ME6 Bullet field offsets changed; re-audit fast update");
static_assert(__builtin_offsetof(BulletCommand, type) == 16u &&
                  __builtin_offsetof(AnmLoadedSprite, heightPx) == 44u &&
                  __builtin_offsetof(AnmLoadedSprite, widthPx) == 48u &&
                  __builtin_offsetof(BombClearBox, pos.x) == 0u &&
                  __builtin_offsetof(BombClearBox, pos.y) == 4u &&
                  __builtin_offsetof(BombClearBox, pos.z) == 8u &&
                  __builtin_offsetof(BombClearBox, size.x) == 12u &&
                  __builtin_offsetof(BombClearBox, size.y) == 16u,
              "I-ME6 command/sprite/bomb layout changed");
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
static_assert(sizeof(Th07PspMeBulletCompactSeedHeader) == 64u &&
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
                  sizeof(Th07PspMeBulletCompactSeedSlot) == 64u &&
                  TH07_PSP_ME_BULLET_COMPACT_SOA_PLANE_STRIDE == 1040u &&
                  sizeof(Th07PspMeBulletCompactSeed) == 58560u &&
#elif defined(TH07_PSP_ME_BULLET_SEED_SLIM)
                  sizeof(Th07PspMeBulletCompactSeedSlot) == 56u &&
                  sizeof(Th07PspMeBulletCompactSeed) == 57664u &&
#else
                  sizeof(Th07PspMeBulletCompactSeedSlot) == 64u &&
                  sizeof(Th07PspMeBulletCompactSeed) == 65728u &&
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                  sizeof(Th07PspMeBulletCompactJob) == 128u &&
                  sizeof(Th07PspMeBulletCompactCompletion) == 88u &&
#else
                  sizeof(Th07PspMeBulletCompactJob) == 92u &&
                  sizeof(Th07PspMeBulletCompactCompletion) == 72u &&
#endif
#if defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
                  sizeof(Th07PspMeBulletCompactSlotResult) == 4u &&
                  sizeof(Th07PspMeBulletCompactOutput) == 4224u,
#else
                  sizeof(Th07PspMeBulletCompactSlotResult) == 16u &&
                  sizeof(Th07PspMeBulletCompactOutput) == 16512u,
#endif
              "I-ME7 compact-update ABI size changed");
static_assert(TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS ==
                      BulletManager::kBulletCapacity &&
                  TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS == 32u &&
                  sizeof(BombClearBox) == 32u &&
                  sizeof(BulletCommand) == 24u,
              "I-ME7 compact owner stride/count changed");
static_assert(__builtin_offsetof(Bullet, pos.x) == 2956u &&
                  __builtin_offsetof(Bullet, pos.y) == 2960u &&
                  __builtin_offsetof(Bullet, pos.z) == 2964u &&
                  __builtin_offsetof(Bullet, velocity.x) == 2968u &&
                  __builtin_offsetof(Bullet, velocity.y) == 2972u &&
                  __builtin_offsetof(Bullet, velocity.z) == 2976u &&
                  __builtin_offsetof(Bullet, spawnDelay) == 3056u &&
                  __builtin_offsetof(Bullet, exFlags) == 3060u &&
                  __builtin_offsetof(Bullet, curCmdIdx) == 3088u &&
                  __builtin_offsetof(Bullet, commands) == 3092u &&
                  __builtin_offsetof(Bullet, sprites.grazeSize.x) == 2940u &&
                  __builtin_offsetof(Bullet, sprites.grazeSize.y) == 2944u &&
                  __builtin_offsetof(Bullet, sprites.spriteBullet.sprite) ==
                      484u &&
                  __builtin_offsetof(BulletCommand, type) == 16u &&
                  __builtin_offsetof(AnmLoadedSprite, heightPx) == 44u &&
                  __builtin_offsetof(AnmLoadedSprite, widthPx) == 48u,
              "I-ME7 compact Bullet/sprite offsets changed");
static_assert(__builtin_offsetof(BombClearBox, pos.x) == 0u &&
                  __builtin_offsetof(BombClearBox, pos.y) == 4u &&
                  __builtin_offsetof(BombClearBox, pos.z) == 8u &&
                  __builtin_offsetof(BombClearBox, size.x) == 12u &&
                  __builtin_offsetof(BombClearBox, size.y) == 16u,
              "I-ME7 compact bomb-clear offsets changed");
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
static_assert(sizeof(Th07PspMeItemMotionSeedHeader) == 64u &&
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
                  sizeof(Th07PspMeItemMotionSeedSlot) == 48u &&
                  sizeof(Th07PspMeItemMotionSeed) == 53632u &&
#else
                  sizeof(Th07PspMeItemMotionSeedSlot) == 64u &&
                  sizeof(Th07PspMeItemMotionSeed) == 70656u &&
#endif
                  sizeof(Th07PspMeItemMotionOutputHeader) == 64u &&
                  sizeof(Th07PspMeItemMotionSlotResult) == 32u &&
                  sizeof(Th07PspMeItemMotionOutput) == 35456u,
              "A1-MOVE Item sidecar ABI size changed");
static_assert(__builtin_offsetof(Item, currentPosition.x) == 588u &&
                  __builtin_offsetof(Item, currentPosition.y) == 592u &&
                  __builtin_offsetof(Item, currentPosition.z) == 596u &&
                  __builtin_offsetof(Item, startPosition.x) == 600u &&
                  __builtin_offsetof(Item, startPosition.y) == 604u &&
                  __builtin_offsetof(Item, startPosition.z) == 608u &&
                  __builtin_offsetof(Item, targetPosition.x) == 612u &&
                  __builtin_offsetof(Item, targetPosition.y) == 616u &&
                  __builtin_offsetof(Item, targetPosition.z) == 620u &&
                  __builtin_offsetof(Item, timer.subFrame) == 628u &&
                  __builtin_offsetof(Item, timer.current) == 632u &&
                  __builtin_offsetof(Item, isInUse) == 637u &&
                  __builtin_offsetof(Item, state) == 639u &&
                  __builtin_offsetof(Item, autoCollect) == 640u,
              "A1-MOVE Item motion offsets changed; re-audit authority");
#endif
#endif
#endif
#else
using PspMeRenderCaptureRecord = Th07PspMeRenderStreamRecord;
#endif

struct alignas(64) PspMeRenderFusedCapture
{
    bool building;
    bool complete;
    u32 published;
    BulletManager *manager;
    const AnmManager *anmManager;
    u32 mutationEpoch;
    u32 representativeSourceGeneration;
    float arcadeLeft;
    float arcadeTop;
    u32 bucketCounts[6];
    Bullet *bucketHeads[6];
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    u32 itemPrepareSerial;
    u32 itemCount;
    u32 itemTotalCount;
    Item *itemHead;
    Item *itemTail;
    Item *itemSuffixHead;
    Item *itemListTail;
#endif
#if !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    alignas(64) PspMeRenderCaptureRecord
        records[6][TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS];
#endif
};

#if !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
static_assert(
    sizeof(PspMeRenderFusedCapture::records) ==
        6u * TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS *
            sizeof(PspMeRenderCaptureRecord),
    "ME staging must remain six independent 1,024-record buckets");
#endif

PspMeRenderFusedCapture gPspMeRenderFusedCapture{};

struct PspMeRenderRepresentativeSourceCache
{
    const AnmManager *owner;
    u32 stageEpoch;
    u32 generation;
    bool ready;
    u16 representative[264];
    u32 textureIds[264];
};

PspMeRenderRepresentativeSourceCache gPspMeRenderRepresentativeSourceCache{};
#endif
#endif
#endif
#if defined(TH07_PSP_BULLET_SNAPSHOT_EMITTER)
constexpr u32 kPspBulletRenderTileSize = 64u;
alignas(64) PspBulletRenderRecord gPspBulletRenderTile[kPspBulletRenderTileSize];
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
constexpr u16 kPspBulletWarmQueueEnd = 0xffffu;
constexpr u32 kPspBulletWarmPrepared = 1u << 0;
constexpr u32 kPspBulletWarmDrawable = 1u << 1;
constexpr u32 kPspBulletWarmAnchorShift = 2u;
constexpr u32 kPspBulletWarmAnchorMask = 3u << kPspBulletWarmAnchorShift;
constexpr u32 kPspBulletWarmBlendAdd = 1u << 4;
constexpr u32 kPspBulletWarmZWriteDisable = 1u << 5;
static_assert(BulletManager::kBulletCapacity < kPspBulletWarmQueueEnd,
              "u16 queue sentinel must not alias a Bullet slot");

// A record is indexed by its stable Bullet slot.  It deliberately carries no
// live Bullet/AnmVm pointer: the accepted draw path reads only this compact
// array and current renderer globals.  Position/offset, color multiplication,
// viewport and culling remain draw-time inputs.
struct PspBulletWarmRecord
{
    float baseX[4];
    float baseY[4];
    float u0;
    float u1;
    float v0;
    float v1;
    float posX;
    float posY;
    float halfWidth;
    float halfHeight;
    float bound;
    u32 baseColor;
    u16 sourceFileIndex;
    u16 nextIndex;
    u32 flags;
};

struct alignas(64) PspBulletWarmQueue
{
    u16 heads[6];
    u16 recordCount;
    u16 reserved;
    u32 mutationEpoch;
    u32 published;
    u32 writtenBits[(BulletManager::kBulletCapacity + 31) / 32];
    alignas(64) PspBulletWarmRecord records[BulletManager::kBulletCapacity];
};

static_assert(sizeof(PspBulletWarmRecord) == 80,
              "warm queue record growth must be reviewed against the 128 KiB cap");
static_assert(sizeof(PspBulletWarmQueue) <= 128u * 1024u,
              "warm queue must remain a single <=128 KiB stage allocation");

inline PspBulletWarmQueue *PspGetBulletWarmQueue(BulletManager *manager)
{
    return static_cast<PspBulletWarmQueue *>(manager->pspBulletWarmQueue);
}

inline const PspBulletWarmQueue *PspGetBulletWarmQueue(const BulletManager *manager)
{
    return static_cast<const PspBulletWarmQueue *>(manager->pspBulletWarmQueue);
}
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
constexpr u32 kPspBulletStaticProxyPrepared = 1u << 0;
constexpr u32 kPspBulletStaticProxyDrawable = 1u << 1;
constexpr u32 kPspBulletStaticProxyAnchorShift = 2u;
constexpr u32 kPspBulletStaticProxyAnchorMask =
    3u << kPspBulletStaticProxyAnchorShift;
constexpr u32 kPspBulletStaticProxyBlendAdd = 1u << 4;
constexpr u32 kPspBulletStaticProxyZWriteDisable = 1u << 5;
constexpr u32 kPspBulletStaticProxySourceShift = 8u;
constexpr u32 kPspBulletStaticProxySourceMask =
    0x1ffu << kPspBulletStaticProxySourceShift;

// Static geometry/signature fields are rebuilt only when the sprite or angle
// changes. Update writes only posX/posY on an ordinary frame; draw therefore
// consumes an 80-byte render view instead of the 3+ KiB Bullet/AnmVm payload.
struct PspBulletStaticProxyRecord
{
    float localX[4];
    float localY[4];
    float u0;
    float u1;
    float v0;
    float v1;
    float posX;
    float posY;
    float halfWidth;
    float halfHeight;
    u32 baseColor;
    u32 sourceAngleBits;
    u32 flags;
    u16 generation;
    u16 reserved;
};

struct PspBulletStaticProxyIdentity
{
    const AnmLoadedSprite *sprite;
    const AnmRawInstr *currentInstruction;
    u32 scaleXBits;
    u32 scaleYBits;
    u32 uvXBits;
    u32 uvYBits;
    u32 baseColor;
    u32 renderFlags;
    u32 sourceAngleBits;
    i16 activeSpriteIdx;
    i16 autoRotate;
};

struct alignas(64) PspBulletStaticProxyPool
{
    alignas(64) PspBulletStaticProxyRecord records[BulletManager::kBulletCapacity];
    u16 generations[BulletManager::kBulletCapacity];
    PspBulletStaticProxyIdentity identities[BulletManager::kBulletCapacity];
    u32 publishedMutationEpoch;
};

static_assert(sizeof(PspBulletStaticProxyRecord) == 80,
              "static proxy record must stay compact");
static_assert(sizeof(PspBulletStaticProxyIdentity) == 40,
              "static proxy exact identity must stay compact");
static_assert(alignof(PspBulletStaticProxyPool) == 64,
              "static proxy pool must remain cache-line aligned");
static_assert(sizeof(PspBulletStaticProxyPool) <= 128u * 1024u,
              "static proxy must remain a single <=128 KiB stage allocation");

inline PspBulletStaticProxyPool *PspGetBulletStaticProxyPool(BulletManager *manager)
{
    return static_cast<PspBulletStaticProxyPool *>(
        manager->pspBulletStaticProxyPool);
}

inline const PspBulletStaticProxyPool *
PspGetBulletStaticProxyPool(const BulletManager *manager)
{
    return static_cast<const PspBulletStaticProxyPool *>(
        manager->pspBulletStaticProxyPool);
}

inline u32 PspBulletStaticProxyFloatBits(float value)
{
    u32 bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline u32 PspBulletStaticProxyRenderFlags(const AnmVm *vm)
{
    const u32 staticFlags = vm->flags &
        ((1u << 0) | (1u << 1) | (1u << 4) | (3u << 10) |
         (1u << 12) | (1u << 16));
    // Draw's canonical early-out always tests color.a, even when useColor2
    // later selects color2 for the emitted vertex. Pack that independent gate
    // byte into otherwise-unused high identity bits.
    return staticFlags | (static_cast<u32>(vm->color.bytes.a) << 24u);
}

inline u32 PspBulletStaticProxyBaseColor(const AnmVm *vm)
{
    return vm->useColor2
               ? vm->color2.color
               : ((vm->color.color & 0xff000000u) | 0x00ffffffu);
}

inline void PspCaptureBulletStaticProxyIdentity(
    PspBulletStaticProxyIdentity *identity, const AnmVm *vm, u32 sourceAngleBits)
{
    identity->sprite = vm->sprite;
    identity->currentInstruction = vm->currentInstruction;
    identity->scaleXBits = PspBulletStaticProxyFloatBits(vm->scale.x);
    identity->scaleYBits = PspBulletStaticProxyFloatBits(vm->scale.y);
    identity->uvXBits = PspBulletStaticProxyFloatBits(vm->uvScrollPos.x);
    identity->uvYBits = PspBulletStaticProxyFloatBits(vm->uvScrollPos.y);
    identity->baseColor = PspBulletStaticProxyBaseColor(vm);
    identity->renderFlags = PspBulletStaticProxyRenderFlags(vm);
    identity->sourceAngleBits = sourceAngleBits;
    identity->activeSpriteIdx = vm->activeSpriteIdx;
    identity->autoRotate = vm->autoRotate;
}

inline bool PspBulletStaticProxyIdentityMatches(
    const PspBulletStaticProxyIdentity &identity, const AnmVm *vm,
    u32 sourceAngleBits)
{
    return identity.sprite == vm->sprite &&
           identity.currentInstruction == vm->currentInstruction &&
           identity.scaleXBits == PspBulletStaticProxyFloatBits(vm->scale.x) &&
           identity.scaleYBits == PspBulletStaticProxyFloatBits(vm->scale.y) &&
           identity.uvXBits == PspBulletStaticProxyFloatBits(vm->uvScrollPos.x) &&
           identity.uvYBits == PspBulletStaticProxyFloatBits(vm->uvScrollPos.y) &&
           identity.baseColor == PspBulletStaticProxyBaseColor(vm) &&
           identity.renderFlags == PspBulletStaticProxyRenderFlags(vm) &&
           identity.sourceAngleBits == sourceAngleBits &&
           identity.activeSpriteIdx == vm->activeSpriteIdx &&
           identity.autoRotate == vm->autoRotate;
}
#endif
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
u32 gPspBulletQuiescentTemplateCount = 0;
u32 gPspBulletQuiescentEligible = 0;
u32 gPspBulletQuiescentHits = 0;
u32 gPspBulletQuiescentFallbacks = 0;
u32 gPspBulletQuiescentInvalidations = 0;

inline u32 PspFloatRawBits(float value)
{
    u32 bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Adding +0.0f is bit-preserving for +0 and positive normal values below one.
// Exclude negative zero, subnormals, non-finite values and the wrap boundary so
// the fast path may omit ExecuteScript's UV add/wrap with byte-identical state.
inline bool PspStableUnitUv(float value)
{
    const u32 bits = PspFloatRawBits(value);
    return bits == 0u || (bits >= 0x00800000u && bits < 0x3f800000u);
}

inline bool PspClassifyQuiescentBulletAnm(const AnmVm *vm)
{
    if (!vm || !vm->currentInstruction || vm->pendingInterrupt != 0)
    {
        return false;
    }

    const i16 opcode = vm->currentInstruction->opcode;
    if (opcode != ANM_STOP && opcode != ANM_STOP_HIDE)
    {
        return false;
    }
    if (vm->angleVel.x != 0.0f || vm->angleVel.y != 0.0f || vm->angleVel.z != 0.0f ||
        vm->scaleGrowth.x != 0.0f || vm->scaleGrowth.y != 0.0f)
    {
        return false;
    }
    for (i32 i = 0; i < 5; ++i)
    {
        if (vm->interpEndTimes[i].current > 0)
        {
            return false;
        }
    }
    if (PspFloatRawBits(vm->uvScrollVel.x) != 0u ||
        PspFloatRawBits(vm->uvScrollVel.y) != 0u || !PspStableUnitUv(vm->uvScrollPos.x) ||
        !PspStableUnitUv(vm->uvScrollPos.y))
    {
        return false;
    }
    return true;
}

// Execute the exact NORMAL-speed STOP/STOP_HIDE state transition. The helper
// revalidates every property whose later mutation could make the template
// classification stale. Any uncertainty clears the bit and returns to the
// unmodified ExecuteScript path.
__attribute__((always_inline)) inline bool
PspExecuteQuiescentBulletAnm(AnmVm *vm, u8 *classification)
{
    if (!classification || !*classification)
    {
        return false;
    }
    if (__builtin_expect(!std::isfinite(g_Supervisor.effectiveFramerateMultiplier) ||
                             g_Supervisor.effectiveFramerateMultiplier <= 0.99f ||
                             (g_Supervisor.flags & 0x20u) != 0,
                         0))
    {
        // Slow-time and Supervisor's special timer mode are temporary. Keep
        // the classification but let the canonical timer implementation run.
        return false;
    }
    if (__builtin_expect(!PspClassifyQuiescentBulletAnm(vm), 0))
    {
        *classification = 0;
        return false;
    }

    AnmRawInstr *instr = vm->currentInstruction;
    const i32 current = vm->currentTimeInScript.current;
    if (instr->time <= current)
    {
        if (instr->opcode == ANM_STOP_HIDE)
        {
            vm->visible = 0;
        }
        vm->isStopped = 1;
        // Legacy order is Decrement(1), then postfix Tick(). At normal speed
        // with flag 0x20 clear, current returns to its old value while previous
        // records oldCurrent-1 and subFrame is untouched.
        vm->currentTimeInScript.previous = current - 1;
    }
    else
    {
        // Future STOP instruction: only the postfix timer tick is observable.
        vm->currentTimeInScript.previous = current;
        vm->currentTimeInScript.current = current + 1;
    }
    ++g_AnmManager->scriptTicksThisFrame;
    return true;
}
#endif

inline void PspBulletRenderSinCos(float angle, float *outSin, float *outCos)
{
    if (std::isfinite(angle) && angle >= -16.0f * ZUN_PI && angle <= 16.0f * ZUN_PI)
    {
        vfpu_sincos(angle, outSin, outCos);
        return;
    }
    sincosf(outSin, outCos, angle);
}

#if defined(TH07_PSP_ME_RENDER_WORKER)
inline u32 PspMeRenderFloatBits(float value)
{
    u32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE) || \
    defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
bool PspMeBulletFastIsEligible(const Bullet *bullet)
{
    if (!bullet || bullet->state != BULLET_NORMAL || bullet->exFlags != 0u ||
        bullet->spawnDelay != 0)
    {
        return false;
    }

    // RunCommands returns without mutation only at one of these two gates.
    // A negative index is not a valid engine state and must never authorize a
    // read before commands[0].
    const i32 commandIndex = bullet->curCmdIdx;
    return commandIndex >= 5 ||
           (commandIndex >= 0 && bullet->commands[commandIndex].type == 0u);
}
#endif

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
bool PspMeBulletFastBuildJob(BulletManager *manager,
                             Th07PspMeBulletFastJob *job)
{
    if (!manager || !job || !g_AnmManager ||
        g_Player.pspBombClearHighWater < 0 ||
        g_Player.pspBombClearHighWater > 96)
    {
        return false;
    }

    const auto physicalAddress = [](const void *pointer) -> u32 {
        return static_cast<u32>(reinterpret_cast<uintptr_t>(pointer) &
                                0x1fffffffu);
    };
    static u32 frameSeq = 0u;
    if (++frameSeq == 0u)
    {
        ++frameSeq;
    }

    *job = Th07PspMeBulletFastJob{};
    job->version = TH07_PSP_ME_BULLET_FAST_UPDATE_VERSION;
    job->frameSeq = frameSeq;

    Th07PspMeBulletFastLayout &layout = job->layout;
    layout.layoutVersion = TH07_PSP_ME_BULLET_FAST_LAYOUT_VERSION;
    layout.layoutBytes = sizeof(layout);
    layout.bulletBasePhys = physicalAddress(&manager->bullets[0]);
    layout.bulletStride = sizeof(Bullet);
    layout.bulletCount = BulletManager::kBulletCapacity;
    layout.generationBasePhys = physicalAddress(
        &manager->pspMeRenderSlotGenerations[0]);
    layout.generationStride = sizeof(
        manager->pspMeRenderSlotGenerations[0]);
    layout.generationCount = BulletManager::kBulletCapacity;
    layout.activeBitsPhys = physicalAddress(&manager->pspActiveBulletBits[0]);
    layout.activeBitsWordCount =
        sizeof(manager->pspActiveBulletBits) /
        sizeof(manager->pspActiveBulletBits[0]);
    layout.spriteBasePhys = physicalAddress(&g_AnmManager->sprites[0]);
    layout.spriteStride = sizeof(AnmLoadedSprite);
    layout.spriteCount = 2560u;
    layout.bulletStateOffset = __builtin_offsetof(Bullet, state);
    layout.bulletPosXOffset = __builtin_offsetof(Bullet, pos.x);
    layout.bulletPosYOffset = __builtin_offsetof(Bullet, pos.y);
    layout.bulletPosZOffset = __builtin_offsetof(Bullet, pos.z);
    layout.bulletVelocityXOffset = __builtin_offsetof(Bullet, velocity.x);
    layout.bulletVelocityYOffset = __builtin_offsetof(Bullet, velocity.y);
    layout.bulletVelocityZOffset = __builtin_offsetof(Bullet, velocity.z);
    layout.bulletSpawnDelayOffset = __builtin_offsetof(Bullet, spawnDelay);
    layout.bulletExFlagsOffset = __builtin_offsetof(Bullet, exFlags);
    layout.bulletOutOfBoundsTimeOffset =
        __builtin_offsetof(Bullet, outOfBoundsTime);
    layout.bulletCurrentCommandIndexOffset =
        __builtin_offsetof(Bullet, curCmdIdx);
    layout.bulletCommandsOffset = __builtin_offsetof(Bullet, commands);
    layout.bulletCommandStride = sizeof(BulletCommand);
    layout.bulletCommandTypeOffset = __builtin_offsetof(BulletCommand, type);
    layout.bulletGrazeSizeXOffset =
        __builtin_offsetof(Bullet, sprites.grazeSize.x);
    layout.bulletGrazeSizeYOffset =
        __builtin_offsetof(Bullet, sprites.grazeSize.y);
    layout.vmSpriteOffset = __builtin_offsetof(AnmVm, sprite);
    layout.spriteWidthOffset = __builtin_offsetof(AnmLoadedSprite, widthPx);
    layout.spriteHeightOffset = __builtin_offsetof(AnmLoadedSprite, heightPx);
    layout.bombClearStride = sizeof(BombClearBox);
    layout.bombClearPosXOffset = __builtin_offsetof(BombClearBox, pos.x);
    layout.bombClearPosYOffset = __builtin_offsetof(BombClearBox, pos.y);
    layout.bombClearPosZOffset = __builtin_offsetof(BombClearBox, pos.z);
    layout.bombClearSizeXOffset = __builtin_offsetof(BombClearBox, size.x);
    layout.bombClearSizeYOffset = __builtin_offsetof(BombClearBox, size.y);

    job->playerState = static_cast<u32>(
        static_cast<u8>(g_Player.playerState));
    job->playerGrazeLeftBits =
        PspMeRenderFloatBits(g_Player.grazeTopLeft.x);
    job->playerGrazeTopBits =
        PspMeRenderFloatBits(g_Player.grazeTopLeft.y);
    job->playerGrazeRightBits =
        PspMeRenderFloatBits(g_Player.grazeBottomRight.x);
    job->playerGrazeBottomBits =
        PspMeRenderFloatBits(g_Player.grazeBottomRight.y);
    job->playerHitboxLeftBits =
        PspMeRenderFloatBits(g_Player.hitboxTopLeft.x);
    job->playerHitboxTopBits =
        PspMeRenderFloatBits(g_Player.hitboxTopLeft.y);
    job->playerHitboxRightBits =
        PspMeRenderFloatBits(g_Player.hitboxBottomRight.x);
    job->playerHitboxBottomBits =
        PspMeRenderFloatBits(g_Player.hitboxBottomRight.y);
    job->bombClearBasePhys = physicalAddress(&g_Player.bombClearBoxes[0]);
    job->bombClearHighWater =
        static_cast<u32>(g_Player.pspBombClearHighWater);
    job->bombClearCapacity = 96u;
    // GameManager::IsInBounds uses these exact inclusive playfield limits.
    job->playfieldRightBits = PspMeRenderFloatBits(384.0f);
    job->playfieldBottomBits = PspMeRenderFloatBits(448.0f);
    return true;
}

bool PspMeBulletFastOutputMatches(
    BulletManager *manager, const Th07PspMeBulletFastJob &job,
    const Th07PspMeBulletFastCompletion &completion,
    const Th07PspMeBulletFastOutput *output)
{
    if (!manager || !output ||
        (reinterpret_cast<uintptr_t>(output) & 63u) != 0u ||
        completion.version != job.version ||
        completion.frameSeq != job.frameSeq ||
        completion.result != TH07_PSP_ME_BULLET_FAST_JOB_OK ||
        completion.firstBadSlot != 0xffffffffu ||
        completion.activeCount > TH07_PSP_ME_BULLET_FAST_MAX_SLOTS ||
        completion.candidateCount > completion.activeCount ||
        completion.inBoundsCount > completion.candidateCount ||
        completion.noCollisionCount > completion.candidateCount)
    {
        return false;
    }

    u32 activeCount = 0u;
    u32 candidateCount = 0u;
    u32 inBoundsCount = 0u;
    u32 noCollisionCount = 0u;
    constexpr u16 allowedFlags =
        TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE |
        TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS |
        TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION;
    for (u32 word = 0u; word < TH07_PSP_ME_BULLET_FAST_ACTIVE_WORDS;
         ++word)
    {
        activeCount += static_cast<u32>(__builtin_popcount(
            manager->pspActiveBulletBits[word]));
    }

    for (u32 slot = 0u; slot < TH07_PSP_ME_BULLET_FAST_MAX_SLOTS; ++slot)
    {
        const Th07PspMeBulletFastSlotResult &result = output->slots[slot];
        const bool candidate =
            (output->candidateBits[slot >> 5u] &
             (1u << (slot & 31u))) != 0u;
        if (((result.flags & TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE) !=
             0u) != candidate)
        {
            return false;
        }
        if (!candidate)
        {
            continue;
        }
        if ((result.flags & ~allowedFlags) != 0u ||
            !manager->PspIsBulletSlotTracked(static_cast<i32>(slot)))
        {
            return false;
        }

        const Bullet *bullet = manager->BulletAt(static_cast<i32>(slot));
        const u32 generation = manager->pspMeRenderSlotGenerations[slot];
        if (static_cast<u16>(generation) != result.generation ||
            !PspMeBulletFastIsEligible(bullet) ||
            ((result.flags & TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION) != 0u &&
             g_Player.playerState == PLAYER_STATE_BORDER))
        {
            return false;
        }

        ++candidateCount;
        if ((result.flags & TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS) != 0u)
        {
            ++inBoundsCount;
        }
        if ((result.flags & TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION) != 0u)
        {
            ++noCollisionCount;
        }
    }

    return activeCount == completion.activeCount &&
           candidateCount == completion.candidateCount &&
           inBoundsCount == completion.inBoundsCount &&
           noCollisionCount == completion.noCollisionCount;
}

const Th07PspMeBulletFastOutput *
PspMeBulletFastRunSynchronous(BulletManager *manager)
{
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    const bool measure = gTh07PspPerfDenseSliceActive != 0;
    if (measure)
    {
        ++gPspDenseSliceWindow.meBulletFastAttempts;
    }
#endif
    Th07PspMeBulletFastJob job{};
    if (!PspMeBulletFastBuildJob(manager, &job))
    {
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        if (measure)
        {
            ++gPspDenseSliceWindow.meBulletFastFallbacks;
        }
#endif
        return nullptr;
    }

    Th07PspMeBulletFastCompletion completion{};
    const Th07PspMeBulletFastOutput *output = nullptr;
    const int result = th07_psp_me_bullet_fast_update_run(
        &job, &completion, &output);
    if (result < 0)
    {
        // The live reader did not prove completion.  Returning to canonical
        // Bullet mutation would race ME, so this shares I-ME4's cold-reboot
        // fail-stop contract.
        PspMeRenderRawFailStop("ME16 UPDATE LIVE TIMEOUT -> COLD REBOOT");
    }
    th07_usage_meter_add_me_cycles(
        completion.meInvalidateCycles + completion.meKernelCycles +
        completion.meWritebackCycles);
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    if (measure)
    {
        gPspDenseSliceWindow.meBulletFastScWritebackUs +=
            completion.scWritebackUs;
        gPspDenseSliceWindow.meBulletFastDispatchWaitUs +=
            completion.dispatchWaitUs;
        gPspDenseSliceWindow.meBulletFastScInvalidateUs +=
            completion.scInvalidateUs;
        gPspDenseSliceWindow.meBulletFastKernelCycles +=
            completion.meKernelCycles;
    }
#endif
    if (result != 1 ||
        !PspMeBulletFastOutputMatches(manager, job, completion, output))
    {
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        if (measure)
        {
            ++gPspDenseSliceWindow.meBulletFastFallbacks;
        }
#endif
        return nullptr;
    }
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    if (measure)
    {
        ++gPspDenseSliceWindow.meBulletFastCompleted;
        gPspDenseSliceWindow.meBulletFastActive += completion.activeCount;
        gPspDenseSliceWindow.meBulletFastCandidates +=
            completion.candidateCount;
        gPspDenseSliceWindow.meBulletFastNoCollision +=
            completion.noCollisionCount;
    }
#endif
    return output;
}
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
struct PspMeBulletCompactIdentity
{
    u32 bank;
    u32 frameSeq;
    u32 targetDrawSeq;
    u32 stageEpoch;
    u32 managerEpoch;
    u32 managerMutationEpoch;
    i32 managerUpdateCount;
};

struct PspMeBulletCompactScState
{
    bool pending;
    bool currentSeedValid;
    bool currentOutputValid;
    u32 lastLaunchDrawSeq;
    u32 lastLaunchManagerEpoch;
    u32 blockedRenderCalcSerial;
    PspMeBulletCompactIdentity currentSeedIdentity;
    PspMeBulletCompactIdentity pendingCommandIdentity;
    Th07PspMeBulletCompactJob job;
    const Th07PspMeBulletCompactSeed *seed;
    const Th07PspMeBulletCompactOutput *output;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    bool currentItemMotionValid;
    const Th07PspMeItemMotionSeed *itemMotionSeed;
    const Th07PspMeItemMotionOutput *itemMotionOutput;
#endif
};

PspMeBulletCompactScState gPspMeBulletCompactSc{};

inline bool PspMeBulletCompactIdentityMatches(
    const PspMeBulletCompactIdentity &left,
    const PspMeBulletCompactIdentity &right)
{
    return left.bank == right.bank && left.frameSeq == right.frameSeq &&
           left.targetDrawSeq == right.targetDrawSeq &&
           left.stageEpoch == right.stageEpoch &&
           left.managerEpoch == right.managerEpoch &&
           left.managerMutationEpoch == right.managerMutationEpoch &&
           left.managerUpdateCount == right.managerUpdateCount;
}

inline bool PspMeBulletCompactSeedMatchesIdentity(
    const Th07PspMeBulletCompactSeed *seed,
    const PspMeBulletCompactIdentity &identity)
{
    return seed &&
           seed->header.version == TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION &&
           seed->header.headerBytes ==
               sizeof(Th07PspMeBulletCompactSeedHeader) &&
           seed->header.seedBytes == sizeof(Th07PspMeBulletCompactSeed) &&
           seed->header.backend ==
               TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM &&
           seed->header.bank == identity.bank &&
           seed->header.frameSeq == identity.frameSeq &&
           seed->header.targetDrawSeq == identity.targetDrawSeq &&
           seed->header.frameSeq + 1u == seed->header.targetDrawSeq &&
           seed->header.stageEpoch == identity.stageEpoch &&
           seed->header.managerEpoch == identity.managerEpoch &&
           seed->header.recordCount <=
               TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS &&
           seed->header.candidateCount <= seed->header.recordCount &&
           seed->header.payloadHash == 0u && seed->header.reserved == 0u &&
           seed->header.commitSequence == seed->header.frameSeq &&
           seed->header.committed ==
               TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED;
}

inline void PspMeBulletCompactClearCurrentView()
{
    gPspMeBulletCompactSc.currentSeedValid = false;
    gPspMeBulletCompactSc.currentOutputValid = false;
    gPspMeBulletCompactSc.currentSeedIdentity =
        PspMeBulletCompactIdentity{};
    gPspMeBulletCompactSc.seed = nullptr;
    gPspMeBulletCompactSc.output = nullptr;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    gPspMeBulletCompactSc.currentItemMotionValid = false;
    gPspMeBulletCompactSc.itemMotionSeed = nullptr;
    gPspMeBulletCompactSc.itemMotionOutput = nullptr;
#endif
}

bool PspMeBulletCompactPlayerSnapshotMatches(
    const Th07PspMeBulletCompactJob &job)
{
    return static_cast<u32>(static_cast<u8>(g_Player.playerState)) ==
               job.playerState &&
           g_Player.pspBombClearHighWater == 0 &&
           PspMeRenderFloatBits(g_Player.grazeTopLeft.x) ==
               job.playerGrazeLeftBits &&
           PspMeRenderFloatBits(g_Player.grazeTopLeft.y) ==
               job.playerGrazeTopBits &&
           PspMeRenderFloatBits(g_Player.grazeBottomRight.x) ==
               job.playerGrazeRightBits &&
           PspMeRenderFloatBits(g_Player.grazeBottomRight.y) ==
               job.playerGrazeBottomBits &&
           PspMeRenderFloatBits(g_Player.hitboxTopLeft.x) ==
               job.playerHitboxLeftBits &&
           PspMeRenderFloatBits(g_Player.hitboxTopLeft.y) ==
               job.playerHitboxTopBits &&
           PspMeRenderFloatBits(g_Player.hitboxBottomRight.x) ==
               job.playerHitboxRightBits &&
           PspMeRenderFloatBits(g_Player.hitboxBottomRight.y) ==
               job.playerHitboxBottomBits;
}

bool PspMeBulletCompactTryAdoptSeed(
    BulletManager *manager, Bullet *bullet, u32 slot,
    const Th07PspMeBulletCompactSeed *seed,
    const Th07PspMeBulletCompactOutput *output, u16 *outFlags)
{
    if (outFlags)
    {
        *outFlags = 0u;
    }
    if (!manager || !bullet || !seed || !outFlags ||
        slot >= TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS ||
        !manager->PspIsBulletSlotTracked(static_cast<i32>(slot)) ||
        (seed->candidateBits[slot >> 5u] &
         (1u << (slot & 31u))) == 0u ||
        !PspMeBulletFastIsEligible(bullet) ||
        !bullet->sprites.spriteBullet.sprite)
    {
        return false;
    }

#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
    const u32 seedGeneration =
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, generation);
    const u32 seedFlags = TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
        (((seed->inBoundsBits[slot >> 5u] &
           (1u << (slot & 31u))) != 0u)
             ? TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS : 0u);
    if (manager->pspMeRenderSlotGenerations[slot] != seedGeneration ||
        seedGeneration == 0u ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posXBits) !=
            PspMeRenderFloatBits(bullet->pos.x) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posYBits) !=
            PspMeRenderFloatBits(bullet->pos.y) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posZBits) !=
            PspMeRenderFloatBits(bullet->pos.z) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityXBits) !=
            PspMeRenderFloatBits(bullet->velocity.x) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityYBits) !=
            PspMeRenderFloatBits(bullet->velocity.y) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityZBits) !=
            PspMeRenderFloatBits(bullet->velocity.z) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteWidthBits) !=
            PspMeRenderFloatBits(
            bullet->sprites.spriteBullet.sprite->widthPx) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteHeightBits) !=
            PspMeRenderFloatBits(
            bullet->sprites.spriteBullet.sprite->heightPx) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeXBits) !=
            PspMeRenderFloatBits(bullet->sprites.grazeSize.x) ||
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeYBits) !=
            PspMeRenderFloatBits(bullet->sprites.grazeSize.y))
    {
        return false;
    }

    u16 flags = static_cast<u16>(seedFlags);
    if (output &&
        (output->candidateBits[slot >> 5u] &
         (1u << (slot & 31u))) != 0u)
    {
        const Th07PspMeBulletCompactSlotResult &result =
            output->slots[slot];
        constexpr u16 allowedOutputFlags =
            TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
            TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS |
            TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION;
        if (result.generation == static_cast<u16>(seedGeneration) &&
            (result.flags & ~allowedOutputFlags) == 0u &&
            (result.flags &
             TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) != 0u &&
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
            result.posXBits == TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosXBits) &&
            result.posYBits == TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosYBits) &&
            result.posZBits == TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosZBits) &&
#endif
            (result.flags &
             TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS) ==
                (seedFlags &
                 TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS))
        {
            flags = result.flags;
        }
    }

#else
    const Th07PspMeBulletCompactSeedSlot &seedSlot = seed->slots[slot];
#if !defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    constexpr u32 allowedSeedFlags =
        TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
        TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS;
#endif
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    const u32 seedFlags = TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
        (((seed->inBoundsBits[slot >> 5u] &
           (1u << (slot & 31u))) != 0u)
             ? TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS : 0u);
    if (manager->pspMeRenderSlotGenerations[slot] != seedSlot.generation ||
        seedSlot.generation == 0u ||
#else
    const u32 seedFlags = seedSlot.staticFlags;
    if (manager->pspMeRenderSlotGenerations[slot] != seedSlot.generation ||
        seedSlot.generation == 0u || seedSlot.reserved != 0u ||
        (seedSlot.staticFlags & ~allowedSeedFlags) != 0u ||
        (seedSlot.staticFlags &
         TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) == 0u ||
#endif
        seedSlot.posXBits != PspMeRenderFloatBits(bullet->pos.x) ||
        seedSlot.posYBits != PspMeRenderFloatBits(bullet->pos.y) ||
        seedSlot.posZBits != PspMeRenderFloatBits(bullet->pos.z) ||
        seedSlot.velocityXBits !=
            PspMeRenderFloatBits(bullet->velocity.x) ||
        seedSlot.velocityYBits !=
            PspMeRenderFloatBits(bullet->velocity.y) ||
        seedSlot.velocityZBits !=
            PspMeRenderFloatBits(bullet->velocity.z) ||
        seedSlot.spriteWidthBits != PspMeRenderFloatBits(
            bullet->sprites.spriteBullet.sprite->widthPx) ||
        seedSlot.spriteHeightBits != PspMeRenderFloatBits(
            bullet->sprites.spriteBullet.sprite->heightPx) ||
        seedSlot.grazeSizeXBits !=
            PspMeRenderFloatBits(bullet->sprites.grazeSize.x) ||
        seedSlot.grazeSizeYBits !=
            PspMeRenderFloatBits(bullet->sprites.grazeSize.y))
    {
        return false;
    }

    u16 flags = static_cast<u16>(seedFlags);
    if (output &&
        (output->candidateBits[slot >> 5u] &
         (1u << (slot & 31u))) != 0u)
    {
        const Th07PspMeBulletCompactSlotResult &result =
            output->slots[slot];
        constexpr u16 allowedOutputFlags =
            TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
            TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS |
            TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION;
        if (result.generation == static_cast<u16>(seedSlot.generation) &&
            (result.flags & ~allowedOutputFlags) == 0u &&
            (result.flags &
             TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) != 0u &&
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
            result.posXBits == seedSlot.nextPosXBits &&
            result.posYBits == seedSlot.nextPosYBits &&
            result.posZBits == seedSlot.nextPosZBits &&
#endif
            (result.flags &
             TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS) ==
                (seedFlags &
                 TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS))
        {
            flags = result.flags;
        }
    }

#endif
#if !defined(TH07_PSP_ME_BULLET_SEED_SOA)
    std::memcpy(&bullet->pos.x, &seedSlot.nextPosXBits,
                sizeof(bullet->pos.x));
    std::memcpy(&bullet->pos.y, &seedSlot.nextPosYBits,
                sizeof(bullet->pos.y));
    std::memcpy(&bullet->pos.z, &seedSlot.nextPosZBits,
                sizeof(bullet->pos.z));
#else
    const u32 nextPosXBits =
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosXBits);
    const u32 nextPosYBits =
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosYBits);
    const u32 nextPosZBits =
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosZBits);
    std::memcpy(&bullet->pos.x, &nextPosXBits, sizeof(bullet->pos.x));
    std::memcpy(&bullet->pos.y, &nextPosYBits, sizeof(bullet->pos.y));
    std::memcpy(&bullet->pos.z, &nextPosZBits, sizeof(bullet->pos.z));
#endif
    *outFlags = flags;
    return true;
}

#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
bool PspMeBulletCompactTryAdoptTrustedSeed(
    BulletManager *manager, Bullet *bullet, u32 slot,
    const Th07PspMeBulletCompactSeed *seed,
    const Th07PspMeBulletCompactOutput *output, u16 *outFlags)
{
    if (outFlags)
    {
        *outFlags = 0u;
    }
    if (!manager || !bullet || !seed || !outFlags ||
        bullet->state != BULLET_NORMAL ||
        slot >= TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS ||
        !manager->PspIsBulletSlotTracked(static_cast<i32>(slot)) ||
        (seed->candidateBits[slot >> 5u] &
         (1u << (slot & 31u))) == 0u)
    {
        return false;
    }

    // currentSeedIdentity.managerMutationEpoch was captured after the exact
    // calc-12 pass that produced this seed and was matched once immediately
    // before entering this update loop.  Every supported out-of-band Bullet
    // writer advances that epoch.  Therefore a NORMAL candidate's capture
    // gates (exFlags == 0, spawnDelay == 0 and an idle command cursor), its
    // motion operands and its sprite/graze geometry are still authoritative.
    // Keep only the compact record's structural proof and the full-u32 slot
    // generation here; the ten scattered AoS reloads belong solely to the
    // canonical/JIT fallback path.
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
    const u32 seedGeneration =
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, generation);
    const u32 seedFlags = TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
        (((seed->inBoundsBits[slot >> 5u] &
           (1u << (slot & 31u))) != 0u)
             ? TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS : 0u);
    if (manager->pspMeRenderSlotGenerations[slot] != seedGeneration ||
        seedGeneration == 0u)
    {
        return false;
    }

    u16 flags = static_cast<u16>(seedFlags);
    if (output &&
        (output->candidateBits[slot >> 5u] &
         (1u << (slot & 31u))) != 0u)
    {
        const Th07PspMeBulletCompactSlotResult &result =
            output->slots[slot];
        constexpr u16 allowedOutputFlags =
            TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
            TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS |
            TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION;
        if (result.generation == static_cast<u16>(seedGeneration) &&
            (result.flags & ~allowedOutputFlags) == 0u &&
            (result.flags &
             TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) != 0u &&
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
            result.posXBits == TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosXBits) &&
            result.posYBits == TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosYBits) &&
            result.posZBits == TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosZBits) &&
#endif
            (result.flags &
             TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS) ==
                (seedFlags &
                 TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS))
        {
            flags = result.flags;
        }
    }

#else
    const Th07PspMeBulletCompactSeedSlot &seedSlot = seed->slots[slot];
#if !defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    constexpr u32 allowedSeedFlags =
        TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
        TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS;
#endif
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    const u32 seedFlags = TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
        (((seed->inBoundsBits[slot >> 5u] &
           (1u << (slot & 31u))) != 0u)
             ? TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS : 0u);
    if (manager->pspMeRenderSlotGenerations[slot] != seedSlot.generation ||
        seedSlot.generation == 0u)
#else
    const u32 seedFlags = seedSlot.staticFlags;
    if (manager->pspMeRenderSlotGenerations[slot] != seedSlot.generation ||
        seedSlot.generation == 0u || seedSlot.reserved != 0u ||
        (seedSlot.staticFlags & ~allowedSeedFlags) != 0u ||
        (seedSlot.staticFlags &
         TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) == 0u)
#endif
    {
        return false;
    }

    u16 flags = static_cast<u16>(seedFlags);
    if (output &&
        (output->candidateBits[slot >> 5u] &
         (1u << (slot & 31u))) != 0u)
    {
        const Th07PspMeBulletCompactSlotResult &result =
            output->slots[slot];
        constexpr u16 allowedOutputFlags =
            TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
            TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS |
            TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION;
        if (result.generation == static_cast<u16>(seedSlot.generation) &&
            (result.flags & ~allowedOutputFlags) == 0u &&
            (result.flags &
             TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) != 0u &&
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
            result.posXBits == seedSlot.nextPosXBits &&
            result.posYBits == seedSlot.nextPosYBits &&
            result.posZBits == seedSlot.nextPosZBits &&
#endif
            (result.flags &
             TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS) ==
                (seedFlags &
                 TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS))
        {
            flags = result.flags;
        }
    }

#endif
#if !defined(TH07_PSP_ME_BULLET_SEED_SOA)
    std::memcpy(&bullet->pos.x, &seedSlot.nextPosXBits,
                sizeof(bullet->pos.x));
    std::memcpy(&bullet->pos.y, &seedSlot.nextPosYBits,
                sizeof(bullet->pos.y));
    std::memcpy(&bullet->pos.z, &seedSlot.nextPosZBits,
                sizeof(bullet->pos.z));
#else
    const u32 nextPosXBits =
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosXBits);
    const u32 nextPosYBits =
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosYBits);
    const u32 nextPosZBits =
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosZBits);
    std::memcpy(&bullet->pos.x, &nextPosXBits, sizeof(bullet->pos.x));
    std::memcpy(&bullet->pos.y, &nextPosYBits, sizeof(bullet->pos.y));
    std::memcpy(&bullet->pos.z, &nextPosZBits, sizeof(bullet->pos.z));
#endif
    *outFlags = flags;
    return true;
}
#endif

int PspMeBulletCompactPollForUpdate(BulletManager *manager)
{
    PspMeBulletCompactScState &state = gPspMeBulletCompactSc;
    if (!state.pending)
    {
        return -1;
    }

    Th07PspMeBulletCompactCompletion completion{};
    const Th07PspMeBulletCompactOutput *output = nullptr;
    const Th07PspMeBulletCompactSeed *seed = nullptr;
    const int pollResult = th07_psp_me_bullet_compact_poll(
        &completion, &output, &seed);
    if (pollResult == -2)
    {
        ++gPspMeRenderShadowWindow.compactProtocolFault;
        PspMeRenderRawFailStop(
            "ME17 COMPACT POLL FAULT -> COLD REBOOT");
    }
    if (pollResult == 0)
    {
        return 0;
    }

    const PspMeBulletCompactIdentity completedIdentity =
        state.pendingCommandIdentity;
    state.pending = false;
    state.pendingCommandIdentity = PspMeBulletCompactIdentity{};
    gPspMeRenderShadowWindow.compactSeedInvalidateUs +=
        completion.scSeedInvalidateUs;
    gPspMeRenderShadowWindow.compactOutputInvalidateUs +=
        completion.scOutputInvalidateUs;
    gPspMeRenderShadowWindow.compactKernelCycles +=
        completion.meKernelCycles;
    th07_usage_meter_add_me_cycles(
        completion.meInvalidateCycles + completion.meKernelCycles +
        completion.meWritebackCycles);

    if (pollResult == -1)
    {
        ++gPspMeRenderShadowWindow.compactReject;
        if (state.currentSeedValid &&
            PspMeBulletCompactIdentityMatches(
                completedIdentity, state.currentSeedIdentity))
        {
            PspMeBulletCompactClearCurrentView();
        }
        return -1;
    }

    const bool currentIdentity = state.currentSeedValid && manager &&
        manager->updateCount == completedIdentity.managerUpdateCount &&
        manager->pspMeRenderMutationEpoch ==
            completedIdentity.managerMutationEpoch &&
        gPspMeRenderShadow.drawSeq == completedIdentity.targetDrawSeq &&
        gPspMeRenderShadow.stageEpoch == completedIdentity.stageEpoch &&
        gPspMeRenderShadow.managerEpoch == completedIdentity.managerEpoch &&
        PspMeBulletCompactIdentityMatches(
            completedIdentity, state.currentSeedIdentity) &&
        completion.seedTargetDrawSeq == gPspMeRenderShadow.drawSeq &&
        PspMeBulletCompactSeedMatchesIdentity(seed, completedIdentity);
    if (!currentIdentity)
    {
        PspMeBulletCompactClearCurrentView();
        ++gPspMeRenderShadowWindow.compactReject;
        return -1;
    }

    state.seed = seed;
    state.output = output;
    state.currentOutputValid = output != nullptr;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    state.currentItemMotionValid = false;
    state.itemMotionSeed = nullptr;
    state.itemMotionOutput = nullptr;
    if ((state.job.flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u &&
        completion.itemResult == TH07_PSP_ME_ITEM_MOTION_RESULT_OK)
    {
        const Th07PspMeItemMotionSeed *itemSeed =
            th07_psp_me_item_motion_seed_bank(completedIdentity.bank);
        const Th07PspMeItemMotionOutput *itemOutput =
            th07_psp_me_item_motion_last_output();
        if (itemSeed && itemOutput &&
            itemSeed->header.version == TH07_PSP_ME_ITEM_MOTION_VERSION &&
            itemSeed->header.headerBytes ==
                sizeof(Th07PspMeItemMotionSeedHeader) &&
            itemSeed->header.seedBytes == sizeof(*itemSeed) &&
            itemSeed->header.bank == completedIdentity.bank &&
            itemSeed->header.frameSeq == completedIdentity.frameSeq &&
            itemSeed->header.targetDrawSeq ==
                completedIdentity.targetDrawSeq &&
            itemSeed->header.stageEpoch == completedIdentity.stageEpoch &&
            itemSeed->header.managerEpoch ==
                completedIdentity.managerEpoch &&
            itemSeed->header.itemPrepareSerial != 0u &&
            itemSeed->header.committed ==
                TH07_PSP_ME_ITEM_MOTION_COMMITTED &&
            itemOutput->header.version ==
                TH07_PSP_ME_ITEM_MOTION_OUTPUT_VERSION &&
            itemOutput->header.frameSeq == state.job.frameSeq &&
            itemOutput->header.seedFrameSeq == completedIdentity.frameSeq &&
            itemOutput->header.seedTargetDrawSeq ==
                completedIdentity.targetDrawSeq &&
            itemOutput->header.result ==
                TH07_PSP_ME_ITEM_MOTION_RESULT_OK &&
            itemOutput->header.committed ==
                TH07_PSP_ME_ITEM_MOTION_COMMITTED)
        {
            state.currentItemMotionValid = true;
            state.itemMotionSeed = itemSeed;
            state.itemMotionOutput = itemOutput;
            ++gPspMeRenderShadowWindow.compactItemMotionReady;
            gPspMeRenderShadowWindow.compactItemMotionProcessed +=
                itemOutput->header.processedCount;
        }
    }
#endif
    ++gPspMeRenderShadowWindow.compactReady;
    return 1;
}

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
u32 PspMeItemMotionCandidateLimitFor(
    const Th07PspMeBulletCompactSeed *bulletSeed,
    const Th07PspMeItemMotionSeed *itemSeed)
{
    const i32 currentPowerClass = g_GameManager.globals
        ? static_cast<i32>(g_GameManager.globals->currentPower) : -1;
    if (!bulletSeed || !itemSeed || !g_Player.shooterData ||
        currentPowerClass < 0 || currentPowerClass > 128 ||
        g_GameManager.difficulty < 0 || g_GameManager.difficulty > 5 ||
        th07_usage_meter_last_me_percent() >= 85u)
        return 0u;

    // Deterministic admission.  Scalar atan2+cos+sin is deliberately charged
    // far above a fall/interpolation record; measured busy is only an OFF veto.
    constexpr u32 kItemTrigTicks = 13000u;
    const unsigned long long base =
        static_cast<unsigned long long>(kPspMeAdaptiveFixedTicks) +
        static_cast<unsigned long long>(bulletSeed->header.candidateCount) *
            kPspMeAdaptiveBulletTicks;
    if (base >= kPspMeAdaptiveBudgetTicks)
        return 0u;

    // Do not inspect the full seed payload here: command 10 produced it on
    // ME and this p9 gate intentionally invalidates only guards/header.  Use
    // the worst-case trig charge for every header-authenticated candidate;
    // DONE poll performs the one required full invalidation before JIT use.
    const u32 affordable = static_cast<u32>(
        (kPspMeAdaptiveBudgetTicks - base) / kItemTrigTicks);
    return affordable < itemSeed->header.candidateCount
        ? affordable : itemSeed->header.candidateCount;
}
#endif

u32 PspMeBulletCompactEarlyLaunch(BulletManager *manager)
{
    PspMeBulletCompactScState &state = gPspMeBulletCompactSc;
    PspMeBulletCompactClearCurrentView();
    if (state.pending)
    {
        (void)PspMeBulletCompactPollForUpdate(manager);
    }

    if (!manager || !gPspMeRenderShadow.managerActive ||
        g_GameManager.isTimeStopped ||
        !__atomic_load_n(&gPspMeRenderShadow.available,
                         __ATOMIC_ACQUIRE))
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
    if (state.lastLaunchManagerEpoch == gPspMeRenderShadow.managerEpoch &&
        state.lastLaunchDrawSeq == gPspMeRenderShadow.drawSeq)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
    state.lastLaunchManagerEpoch = gPspMeRenderShadow.managerEpoch;
    state.lastLaunchDrawSeq = gPspMeRenderShadow.drawSeq;
    if (state.pending || gPspMeRenderShadow.drawSeq < 2u)
    {
        ++gPspMeRenderShadowWindow.compactLaunchBusy;
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

    const u32 bank = (gPspMeRenderShadow.drawSeq - 1u) &
        (TH07_PSP_ME_BULLET_COMPACT_BANKS - 1u);
    const u32 seedInvalidateStartUs = sceKernelGetSystemTimeLow();
    const Th07PspMeBulletCompactSeed *seed =
        th07_psp_me_bullet_compact_seed_bank(bank);
    gPspMeRenderShadowWindow.compactSeedInvalidateUs +=
        sceKernelGetSystemTimeLow() - seedInvalidateStartUs;
    if (!seed ||
        seed->header.version != TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION ||
        seed->header.backend !=
            TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM ||
        seed->header.committed !=
            TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED ||
        seed->header.targetDrawSeq != gPspMeRenderShadow.drawSeq ||
        seed->header.frameSeq + 1u != seed->header.targetDrawSeq ||
        seed->header.stageEpoch != gPspMeRenderShadow.stageEpoch ||
        seed->header.managerEpoch != gPspMeRenderShadow.managerEpoch ||
        gPspMeRenderCorrectness.job.frameSeq != seed->header.frameSeq ||
        gPspMeRenderCorrectness.job.targetDrawSeq !=
            seed->header.targetDrawSeq ||
        gPspMeRenderCorrectness.job.stageEpoch != seed->header.stageEpoch ||
        gPspMeRenderCorrectness.job.managerEpoch !=
            seed->header.managerEpoch ||
        gPspMeRenderCorrectness.managerUpdateCount != manager->updateCount ||
        gPspMeRenderCorrectness.managerMutationEpoch !=
            manager->pspMeRenderMutationEpoch)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

    const PspMeBulletCompactIdentity identity = {
        bank, seed->header.frameSeq, seed->header.targetDrawSeq,
        seed->header.stageEpoch, seed->header.managerEpoch,
        gPspMeRenderCorrectness.managerMutationEpoch,
        gPspMeRenderCorrectness.managerUpdateCount};
    if (!PspMeBulletCompactSeedMatchesIdentity(seed, identity))
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
    state.currentSeedValid = true;
    state.currentSeedIdentity = identity;
    state.seed = seed;
    state.output = nullptr;
    state.currentOutputValid = false;
    gPspMeRenderShadowWindow.compactSeedCandidates +=
        seed->header.candidateCount;

    const i32 highWater = g_Player.pspBombClearHighWater;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    const Th07PspMeItemMotionSeed *itemMotionSeed = nullptr;
    u32 itemMotionCandidateLimit = 0u;
    if (th07_psp_me_item_motion_available())
    {
        const Th07PspMeItemMotionSeed *candidate =
            th07_psp_me_item_motion_seed_bank(bank);
        if (candidate &&
            candidate->header.frameSeq == identity.frameSeq &&
            candidate->header.targetDrawSeq == identity.targetDrawSeq &&
            candidate->header.stageEpoch == identity.stageEpoch &&
            candidate->header.managerEpoch == identity.managerEpoch &&
            candidate->header.itemPrepareSerial != 0u &&
            candidate->header.itemPrepareSerial ==
                g_ItemManager.pspMeItemPreparedSerial &&
            candidate->header.recordCount <=
                static_cast<u32>(ItemManager::kItemCapacity) &&
            candidate->header.totalCount >= candidate->header.recordCount &&
            candidate->header.candidateCount <=
                candidate->header.recordCount &&
            candidate->header.committed ==
                TH07_PSP_ME_ITEM_MOTION_COMMITTED)
        {
            itemMotionCandidateLimit =
                PspMeItemMotionCandidateLimitFor(seed, candidate);
            if (itemMotionCandidateLimit != 0u)
                itemMotionSeed = candidate;
        }
    }
#endif

    const auto physicalAddress = [](const void *pointer) -> u32 {
        return static_cast<u32>(reinterpret_cast<uintptr_t>(pointer) &
                                0x1fffffffu);
    };
    Th07PspMeBulletCompactJob job{};
    job.version = TH07_PSP_ME_BULLET_COMPACT_VERSION;
    job.frameSeq = gPspMeRenderShadow.drawSeq;
    const bool collisionSnapshotValid =
        highWater == 0 && g_Player.playerState != PLAYER_STATE_BORDER;
    job.flags = collisionSnapshotValid
        ? TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID : 0u;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    if (itemMotionSeed)
        job.flags |= TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID;
#endif
    job.seedBank = identity.bank;
    job.seedFrameSeq = identity.frameSeq;
    job.seedTargetDrawSeq = identity.targetDrawSeq;
    // The retained seed was produced after this exact manager update.  Keep
    // that authority adjacent to command publication so a calc-chain RESTART
    // cannot submit the same draw with a different Bullet state.
    if (g_BulletManager.updateCount != identity.managerUpdateCount ||
        manager->updateCount != identity.managerUpdateCount ||
        g_BulletManager.pspMeRenderMutationEpoch !=
            identity.managerMutationEpoch ||
        manager->pspMeRenderMutationEpoch !=
            identity.managerMutationEpoch)
    {
        PspMeBulletCompactClearCurrentView();
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
    job.stageEpoch = identity.stageEpoch;
    job.managerEpoch = identity.managerEpoch;
    job.replayEpoch = seed->header.replayEpoch;
    job.playerState = static_cast<u32>(
        static_cast<u8>(g_Player.playerState));
    job.playerGrazeLeftBits =
        PspMeRenderFloatBits(g_Player.grazeTopLeft.x);
    job.playerGrazeTopBits =
        PspMeRenderFloatBits(g_Player.grazeTopLeft.y);
    job.playerGrazeRightBits =
        PspMeRenderFloatBits(g_Player.grazeBottomRight.x);
    job.playerGrazeBottomBits =
        PspMeRenderFloatBits(g_Player.grazeBottomRight.y);
    job.playerHitboxLeftBits =
        PspMeRenderFloatBits(g_Player.hitboxTopLeft.x);
    job.playerHitboxTopBits =
        PspMeRenderFloatBits(g_Player.hitboxTopLeft.y);
    job.playerHitboxRightBits =
        PspMeRenderFloatBits(g_Player.hitboxBottomRight.x);
    job.playerHitboxBottomBits =
        PspMeRenderFloatBits(g_Player.hitboxBottomRight.y);
    job.bombClearBasePhys = physicalAddress(&g_Player.bombClearBoxes[0]);
    job.bombClearHighWater = 0u;
    job.bombClearCapacity = 96u;
    job.playfieldRightBits = PspMeRenderFloatBits(384.0f);
    job.playfieldBottomBits = PspMeRenderFloatBits(448.0f);
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    if (itemMotionSeed)
    {
        job.itemMotionCandidateLimit = itemMotionCandidateLimit;
        job.itemPlayerPosXBits =
            PspMeRenderFloatBits(g_Player.positionCenter.x);
        job.itemPlayerPosYBits =
            PspMeRenderFloatBits(g_Player.positionCenter.y);
        job.itemCollectSpeedBits = PspMeRenderFloatBits(
            g_Player.shooterData->itemCollectSpeed);
        job.itemPocYBits =
            PspMeRenderFloatBits(g_Player.shooterData->pocY);
        job.itemFramerateMultiplierBits = PspMeRenderFloatBits(
            g_Supervisor.effectiveFramerateMultiplier);
        job.itemCurrentPowerClass =
            static_cast<i32>(g_GameManager.globals->currentPower);
        job.itemDifficulty = g_GameManager.difficulty;
        job.itemHasBorder = static_cast<u32>(g_Player.hasBorder == 1);
    }
#endif

    if (job.flags == 0u)
        return CHAIN_CALLBACK_RESULT_CONTINUE;

    ++gPspMeRenderShadowWindow.compactLaunchAttempts;
    // A completed command may have published an Item view above.  Command 12
    // owns one shared Item output arena, so a new begin would make that view
    // point at memory the ME is actively rewriting.  Bullet seed authority is
    // banked and remains valid; retire only the unbanked Item sidecar here.
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    state.currentItemMotionValid = false;
    state.itemMotionSeed = nullptr;
    state.itemMotionOutput = nullptr;
#endif
    if (th07_psp_me_bullet_compact_begin(&job))
    {
        state.pending = true;
        state.pendingCommandIdentity = identity;
        state.job = job;
        ++gPspMeRenderShadowWindow.compactLaunchBegun;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        if ((job.flags &
             TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u)
        {
            ++gPspMeRenderShadowWindow.compactItemMotionLaunch;
            gPspMeRenderShadowWindow.compactItemMotionCandidates +=
                job.itemMotionCandidateLimit;
        }
#endif
    }
    else
    {
        ++gPspMeRenderShadowWindow.compactLaunchBusy;
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

void PspMeBulletCompactFinishFrame(BulletManager *manager)
{
    if (gPspMeBulletCompactSc.pending)
    {
        const int result = PspMeBulletCompactPollForUpdate(manager);
        if (result == 0)
        {
            ++gPspMeRenderShadowWindow.compactP12TailPending;
        }
    }
    PspMeBulletCompactClearCurrentView();
}

bool PspMeBulletCompactRetireBeforeRender()
{
    if (!gPspMeBulletCompactSc.pending)
    {
        if (gPspMeBulletCompactSc.blockedRenderCalcSerial ==
            gPspMeRenderShadow.calcCompleteSerial)
        {
            gPspMeBulletCompactSc.blockedRenderCalcSerial = 0u;
        }
        return true;
    }
    const int result = PspMeBulletCompactPollForUpdate(&g_BulletManager);
    if (result == 0)
    {
        gPspMeBulletCompactSc.blockedRenderCalcSerial =
            gPspMeRenderShadow.calcCompleteSerial;
        ++gPspMeRenderShadowWindow.compactBlockedRender;
        return false;
    }
    gPspMeBulletCompactSc.blockedRenderCalcSerial = 0u;
    return true;
}

void PspMeBulletCompactDrainForManagerDelete()
{
    if (th07_psp_me_bullet_compact_drain_live() != 1)
    {
        ++gPspMeRenderShadowWindow.compactProtocolFault;
        PspMeRenderRawFailStop(
            "ME17 COMPACT DRAIN FAIL -> COLD REBOOT");
    }
    ++gPspMeRenderShadowWindow.compactTeardownDrain;
    gPspMeBulletCompactSc = PspMeBulletCompactScState{};
}
#endif

inline u32 PspMeRenderHashWord(u32 hash, u32 word)
{
    hash ^= word;
    return hash * 16777619u;
}

u32 PspMeRenderGlobalSignature()
{
    // M0B-only conservative change detector. FNV32 is not an authority hash;
    // I-ME1 must carry and compare the raw header fields individually before
    // any ME stream can be rendered.
    u32 hash = 2166136261u;
    hash = PspMeRenderHashWord(
        hash, PspMeRenderFloatBits(g_GameManager.arcadeRegionTopLeftPos.x));
    hash = PspMeRenderHashWord(
        hash, PspMeRenderFloatBits(g_GameManager.arcadeRegionTopLeftPos.y));
    hash = PspMeRenderHashWord(hash, PspMeRenderFloatBits(g_AnmManager->offset.x));
    hash = PspMeRenderHashWord(hash, PspMeRenderFloatBits(g_AnmManager->offset.y));
    hash = PspMeRenderHashWord(hash, static_cast<u32>(g_Supervisor.viewport.x));
    hash = PspMeRenderHashWord(hash, static_cast<u32>(g_Supervisor.viewport.y));
    hash = PspMeRenderHashWord(hash, static_cast<u32>(g_Supervisor.viewport.width));
    hash = PspMeRenderHashWord(hash, static_cast<u32>(g_Supervisor.viewport.height));
    hash = PspMeRenderHashWord(
        hash, PspMeRenderFloatBits(g_Supervisor.viewport.minZ));
    hash = PspMeRenderHashWord(
        hash, PspMeRenderFloatBits(g_Supervisor.viewport.maxZ));
    hash = PspMeRenderHashWord(hash, g_AnmManager->color.color);
    hash = PspMeRenderHashWord(hash,
                               g_AnmManager->colorMulEnabled ? 1u : 0u);
    hash = PspMeRenderHashWord(hash,
                               g_Supervisor.cfg.disableZBuffer ? 1u : 0u);
    return hash;
}

AnmVm *PspMeRenderSelectVm(Bullet *bullet)
{
    switch (bullet->state)
    {
    case BULLET_SPAWNING_FAST:
        return bullet->SpawnEffectVm(BULLET_SPAWNING_FAST);
    case BULLET_SPAWNING_NORMAL:
        return bullet->SpawnEffectVm(BULLET_SPAWNING_NORMAL);
    case BULLET_SPAWNING_SLOW:
        return bullet->SpawnEffectVm(BULLET_SPAWNING_SLOW);
    case BULLET_DESPAWN:
        return &bullet->sprites.spriteSpawnEffectDonut;
    default:
        return &bullet->sprites.spriteBullet;
    }
}

bool PspMeRenderBuildShadowSnapshot(BulletManager *manager, u32 *recordCount,
                                    u32 *signature)
{
    auto *const shadowInput = static_cast<PspMeRenderShadowRecord64 *>(
        th07_psp_me_render_runtime_input());
    if (!manager || !recordCount || !signature || !g_AnmManager ||
        !shadowInput)
    {
        return false;
    }

    u32 seen[(BulletManager::kBulletCapacity + 31u) / 32u] = {};
    const uintptr_t bulletBase =
        reinterpret_cast<uintptr_t>(&manager->bullets[0]);
    const uintptr_t bulletEnd = reinterpret_cast<uintptr_t>(
        &manager->bullets[BulletManager::kBulletCapacity]);
    u32 count = 0u;
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        Bullet *bullet = manager->bulletsPtrs[bucket];
        while (bullet)
        {
            const uintptr_t bulletAddress = reinterpret_cast<uintptr_t>(bullet);
            if (count >= TH07_PSP_ME_RENDER_MAX_RECORDS ||
                bulletAddress < bulletBase || bulletAddress >= bulletEnd ||
                ((bulletAddress - bulletBase) % sizeof(Bullet)) != 0u)
            {
                return false;
            }
            const u32 slot = static_cast<u32>(
                (bulletAddress - bulletBase) / sizeof(Bullet));
            const u32 bit = 1u << (slot & 31u);
            u32 &seenWord = seen[slot >> 5u];
            if ((seenWord & bit) != 0u)
            {
                return false;
            }
            seenWord |= bit;

            AnmVm *vm = PspMeRenderSelectVm(bullet);
            PspMeRenderShadowRecord64 &record = shadowInput[count];
            std::memset(&record, 0, sizeof(record));

            const float halfWidth = vm && vm->sprite
                                        ? vm->sprite->widthPx * vm->scale.x * 0.5f
                                        : 0.0f;
            const float halfHeight = vm && vm->sprite
                                         ? vm->sprite->heightPx * vm->scale.y * 0.5f
                                         : 0.0f;
            float centerX = g_GameManager.arcadeRegionTopLeftPos.x +
                            bullet->pos.x + g_AnmManager->offset.x;
            float centerY = g_GameManager.arcadeRegionTopLeftPos.y +
                            bullet->pos.y + g_AnmManager->offset.y;
            if (vm && (vm->anchor & 1u))
            {
                centerX += halfWidth;
            }
            if (vm && (vm->anchor & 2u))
            {
                centerY += halfHeight;
            }

            float sine = 0.0f;
            float cosine = 1.0f;
            float renderAngle = 0.0f;
            if (vm && vm->autoRotate)
            {
                if (bullet->pspRenderRotationValid &&
                    bullet->pspRenderSourceAngle == bullet->angle)
                {
                    renderAngle = bullet->pspRenderAngle;
                    sine = bullet->pspRenderSin;
                    cosine = bullet->pspRenderCos;
                }
                else
                {
                    renderAngle =
                        utils::AddNormalizeAngle(1.5707964f + bullet->angle,
                                                 0.0f);
                    PspBulletRenderSinCos(renderAngle, &sine, &cosine);
                }
            }

            record.geometry.centerXBits = PspMeRenderFloatBits(centerX);
            record.geometry.centerYBits = PspMeRenderFloatBits(centerY);
            record.geometry.halfWidthBits = PspMeRenderFloatBits(halfWidth);
            record.geometry.halfHeightBits = PspMeRenderFloatBits(halfHeight);
            record.geometry.sinBits = PspMeRenderFloatBits(sine);
            record.geometry.cosBits = PspMeRenderFloatBits(cosine);
            record.geometry.color =
                vm ? (vm->useColor2 ? vm->color2.color : vm->color.color)
                   : 0u;
            record.geometry.flags =
                renderAngle != 0.0f ? TH07_PSP_ME_RENDER_RECORD_ROTATED : 0u;
            if (vm && vm->sprite)
            {
                record.uv0Bits = PspMeRenderFloatBits(
                    vm->sprite->uvStart.x + vm->uvScrollPos.x);
                record.uv1Bits = PspMeRenderFloatBits(
                    vm->sprite->uvEnd.x + vm->uvScrollPos.x);
                record.vv0Bits = PspMeRenderFloatBits(
                    vm->sprite->uvStart.y + vm->uvScrollPos.y);
                record.vv1Bits = PspMeRenderFloatBits(
                    vm->sprite->uvEnd.y + vm->uvScrollPos.y);
                record.sourceAndState =
                    (static_cast<u32>(vm->sprite->sourceFileIndex) & 0xffffu) |
                    (static_cast<u32>(bullet->state) << 16u);
            }
            record.zBits = PspMeRenderFloatBits(0.05f);
            if (vm)
            {
                record.observableFlags =
                    (vm->visible ? 1u : 0u) |
                    (vm->active ? 2u : 0u) |
                    (vm->autoRotate ? 4u : 0u) |
                    (vm->useColor2 ? 8u : 0u) |
                    ((static_cast<u32>(vm->anchor) & 3u) << 4u) |
                    ((static_cast<u32>(vm->blendMode) & 1u) << 6u) |
                    ((static_cast<u32>(vm->zWriteDisable) & 1u) << 7u);
            }
            record.slot = slot;
            ++count;
            bullet = bullet->next;
        }
    }

    *recordCount = count;
    *signature = PspMeRenderGlobalSignature();
    return true;
}

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
void PspMeRenderResetRepresentativeSourceCache()
{
    gPspMeRenderRepresentativeSourceCache.owner = nullptr;
    gPspMeRenderRepresentativeSourceCache.stageEpoch = 0u;
    gPspMeRenderRepresentativeSourceCache.ready = false;
}

bool PspMeRenderBuildRepresentativeSourceCache()
{
    if (!g_AnmManager)
    {
        return false;
    }

    PspMeRenderRepresentativeSourceCache &cache =
        gPspMeRenderRepresentativeSourceCache;
    for (u32 source = 0u; source < 264u; ++source)
    {
        cache.representative[source] = static_cast<u16>(source);
        const u32 textureId = g_AnmManager->textures[source].id;
        cache.textureIds[source] = textureId;
        for (u32 candidate = 0u; candidate < source; ++candidate)
        {
            if (g_AnmManager->textures[candidate].id == textureId)
            {
                cache.representative[source] = static_cast<u16>(candidate);
                break;
            }
        }
    }
    cache.owner = g_AnmManager;
    cache.stageEpoch = gPspMeRenderShadow.stageEpoch;
    if (++cache.generation == 0u)
    {
        ++cache.generation;
    }
    cache.ready = true;
    return true;
}

bool PspMeRenderRepresentativeSourceCacheMatches()
{
    const PspMeRenderRepresentativeSourceCache &cache =
        gPspMeRenderRepresentativeSourceCache;
    if (!cache.ready || !g_AnmManager || cache.owner != g_AnmManager ||
        cache.stageEpoch != gPspMeRenderShadow.stageEpoch)
    {
        return false;
    }
    for (u32 source = 0u; source < 264u; ++source)
    {
        if (cache.textureIds[source] != g_AnmManager->textures[source].id)
        {
            return false;
        }
    }
    return true;
}

bool PspMeRenderRepresentativeSourceCacheIdentityMatches(u32 generation)
{
    const PspMeRenderRepresentativeSourceCache &cache =
        gPspMeRenderRepresentativeSourceCache;
    return generation != 0u && cache.ready && g_AnmManager &&
           cache.owner == g_AnmManager &&
           cache.stageEpoch == gPspMeRenderShadow.stageEpoch &&
           cache.generation == generation;
}

bool PspMeRenderEnsureRepresentativeSourceCache()
{
    return PspMeRenderRepresentativeSourceCacheMatches() ||
           PspMeRenderBuildRepresentativeSourceCache();
}
#endif

u32 PspMeRenderRepresentativeSource(i32 sourceFileIndex)
{
    if (!g_AnmManager || sourceFileIndex < 0 || sourceFileIndex >= 264)
    {
        return 0u;
    }
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    PspMeRenderRepresentativeSourceCache &cache =
        gPspMeRenderRepresentativeSourceCache;
    if (!cache.ready || cache.owner != g_AnmManager ||
        cache.stageEpoch != gPspMeRenderShadow.stageEpoch)
    {
        if (!PspMeRenderEnsureRepresentativeSourceCache())
        {
            return 0u;
        }
    }
    return cache.representative[sourceFileIndex];
#else
    const u32 textureId = g_AnmManager->textures[sourceFileIndex].id;
    for (u32 candidate = 0u;
         candidate < static_cast<u32>(sourceFileIndex); ++candidate)
    {
        if (g_AnmManager->textures[candidate].id == textureId)
        {
            return candidate;
        }
    }
    return static_cast<u32>(sourceFileIndex);
#endif
}

#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
void PspMeRenderBeginFusedCapture(BulletManager *manager)
{
    PspMeRenderFusedCapture &capture = gPspMeRenderFusedCapture;
    // Invalidate first.  A time-stop return, replay calc restart, fixed-30
    // update-only pass, partial record or later priority mutation can therefore
    // never expose the preceding frame's bytes.
    capture.published = 0u;
    capture.building = false;
    capture.complete = false;
    capture.manager = manager;
    capture.anmManager = g_AnmManager;
    capture.mutationEpoch = manager ? manager->pspMeRenderMutationEpoch : 0u;
    capture.arcadeLeft = g_GameManager.arcadeRegionTopLeftPos.x;
    capture.arcadeTop = g_GameManager.arcadeRegionTopLeftPos.y;
    std::memset(capture.bucketCounts, 0, sizeof(capture.bucketCounts));
    std::memset(capture.bucketHeads, 0, sizeof(capture.bucketHeads));
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    capture.itemPrepareSerial = 0u;
    capture.itemCount = 0u;
    capture.itemTotalCount = 0u;
    capture.itemHead = nullptr;
    capture.itemTail = nullptr;
    capture.itemSuffixHead = nullptr;
    capture.itemListTail = nullptr;
#endif

    // Reject non-draw frames before touching the 264-entry texture table.
    // This also prevents pause/time-stop paths from paying a capture tax.
    const bool eligible = manager && g_AnmManager &&
        !g_GameManager.isTimeStopped && gPspMeRenderShadow.managerActive &&
        __atomic_load_n(&gPspMeRenderShadow.available, __ATOMIC_ACQUIRE) &&
        Th07PspCanCommitBulletWarmQueue();
    if (!eligible)
    {
        capture.representativeSourceGeneration = 0u;
        return;
    }

    const bool representativeCacheReady =
        PspMeRenderEnsureRepresentativeSourceCache();
    capture.representativeSourceGeneration = representativeCacheReady
        ? gPspMeRenderRepresentativeSourceCache.generation : 0u;

    // This is the already-reviewed WARM_QUEUE proof gate: calc-12 render-only
    // VM writes move early only on a guaranteed 60-Hz draw, with no SELECT
    // fixed-30 transition and no replay path that can restart calc afterward.
    capture.building = representativeCacheReady;
    capture.complete = capture.building;
}

__attribute__((always_inline)) inline bool
PspMeRenderCaptureFusedRecord(BulletManager *manager, Bullet *bullet,
                              u32 slot, u32 bucket)
{
    PspMeRenderFusedCapture &capture = gPspMeRenderFusedCapture;
    if (!capture.building || !capture.complete)
    {
        return false;
    }
    if (!manager || manager != capture.manager || !bullet || bucket >= 6u ||
        slot >= static_cast<u32>(BulletManager::kBulletCapacity) ||
        capture.bucketCounts[bucket] >=
            TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS ||
        !manager->PspIsBulletSlotTracked(static_cast<i32>(slot)))
    {
        capture.complete = false;
        return false;
    }

    AnmVm *vm = PspMeRenderSelectVm(bullet);
    const u32 generation = manager->pspMeRenderSlotGenerations[slot];
    if (!vm || generation == 0u)
    {
        capture.complete = false;
        return false;
    }

    // Bullet::Draw's observable VM writes are idempotent.  The WARM_QUEUE
    // audit proved Enemy ECL (priority 10) is the last external bullet-VM
    // reader/writer; priorities 13..17 have no such observer.  The manager-wide
    // epoch below still cancels the publication if any later public mutation
    // clears, despawns, stops or reuses a bullet slot.
    vm->pos.x = capture.arcadeLeft + bullet->pos.x;
    vm->pos.y = capture.arcadeTop + bullet->pos.y;
    vm->pos.z = 0.05f;
    vm->color.color = (vm->color.color & 0xff000000u) | 0x00ffffffu;

#if !defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    u32 flags = 0u;
#endif
    float rotation = vm->rotation.z;
    float sine = 0.0f;
    float cosine = 1.0f;
    if (vm->autoRotate)
    {
        if (!bullet->pspRenderRotationValid ||
            bullet->pspRenderSourceAngle != bullet->angle)
        {
            const float renderAngle = utils::AddNormalizeAngle(
                1.5707964f + bullet->angle, 0.0f);
            PspBulletRenderSinCos(renderAngle, &bullet->pspRenderSin,
                                 &bullet->pspRenderCos);
            bullet->pspRenderSourceAngle = bullet->angle;
            bullet->pspRenderAngle = renderAngle;
            bullet->pspRenderRotationValid = 1u;
        }
        vm->SetRotationZ(bullet->pspRenderAngle);
        vm->updateRotation = 1;
        rotation = bullet->pspRenderAngle;
        sine = bullet->pspRenderSin;
        cosine = bullet->pspRenderCos;
    }
    else if (rotation != 0.0f)
    {
        PspBulletRenderSinCos(rotation, &sine, &cosine);
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        // I-ME5 follows the live Bullet lists and therefore has no capture
        // record carrying the canonical SC trigonometric result.  Publish the
        // exact bits in the render-only Bullet tail.  SourceAngle is made a
        // quiet NaN so a later switch to autoRotate cannot mistake this
        // non-auto cache for the angle-derived auto-rotation cache.
        const u32 nonAutoSourceInvalidBits = 0x7fc00000u;
        std::memcpy(&bullet->pspRenderSourceAngle,
                    &nonAutoSourceInvalidBits,
                    sizeof(bullet->pspRenderSourceAngle));
        bullet->pspRenderAngle = rotation;
        bullet->pspRenderSin = sine;
        bullet->pspRenderCos = cosine;
        bullet->pspRenderRotationValid = 1u;
#endif
    }
    if (rotation != 0.0f)
    {
#if !defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED;
#endif
    }

#if !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const u32 tailIndex = TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS - 1u -
                          capture.bucketCounts[bucket];
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    Th07PspMeRenderRawRecord &record = capture.records[bucket][tailIndex];
    record = Th07PspMeRenderRawRecord{};
    record.posXBits = PspMeRenderFloatBits(vm->pos.x);
    record.posYBits = PspMeRenderFloatBits(vm->pos.y);
    record.sinBits = PspMeRenderFloatBits(sine);
    record.cosBits = PspMeRenderFloatBits(cosine);
    record.vmPhys = static_cast<u32>(
        reinterpret_cast<uintptr_t>(vm) & 0x1fffffffu);
    record.logicalState = static_cast<u32>(bullet->state);
    record.slot = slot;
    record.generation = generation;
#else
    Th07PspMeRenderStreamRecord &record =
        capture.records[bucket][tailIndex];
    record = Th07PspMeRenderStreamRecord{};
    record.posXBits = PspMeRenderFloatBits(vm->pos.x);
    record.posYBits = PspMeRenderFloatBits(vm->pos.y);
    record.posZBits = PspMeRenderFloatBits(vm->pos.z);
    record.sinBits = PspMeRenderFloatBits(sine);
    record.cosBits = PspMeRenderFloatBits(cosine);
    record.slot = slot;
    record.slotGeneration = generation;

    ZunColor baseColor;
    if (vm->useColor2)
    {
        baseColor = vm->color2;
    }
    else
    {
        baseColor = vm->color;
    }
    record.color = baseColor.color;
    flags |= (static_cast<u32>(vm->anchor)
              << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT) &
             TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK;
    if (vm->blendMode)
    {
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD;
    }
    if (vm->zWriteDisable)
    {
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE;
    }

    u32 source = 0u;
    const bool drawable = vm->sprite && vm->visible && vm->active &&
                          vm->color.bytes.a;
    if (drawable)
    {
        record.halfWidthBits = PspMeRenderFloatBits(
            vm->sprite->widthPx * vm->scale.x * 0.5f);
        record.halfHeightBits = PspMeRenderFloatBits(
            vm->sprite->heightPx * vm->scale.y * 0.5f);
        record.u0Bits = PspMeRenderFloatBits(
            vm->sprite->uvStart.x + vm->uvScrollPos.x);
        record.u1Bits = PspMeRenderFloatBits(
            vm->sprite->uvEnd.x + vm->uvScrollPos.x);
        record.v0Bits = PspMeRenderFloatBits(
            vm->sprite->uvStart.y + vm->uvScrollPos.y);
        record.v1Bits = PspMeRenderFloatBits(
            vm->sprite->uvEnd.y + vm->uvScrollPos.y);

        const i32 originalSource = vm->sprite->sourceFileIndex;
        if (originalSource < 0 || originalSource >= 264)
        {
            // Canonical DrawPspBullet rejects visibility first. Never touch
            // sprite geometry/source for a nondrawable VM; a drawable invalid
            // source must fail closed before ME/GE can observe it.
            capture.complete = false;
            return false;
        }
        source = PspMeRenderRepresentativeSource(originalSource);
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
    }
    record.sourceAndState =
        (source & 0xffffu) |
        ((static_cast<u32>(bullet->state) & 0xffffu) << 16u);
    record.flags = flags;
#endif
#endif
    ++capture.bucketCounts[bucket];
    return true;
}

void PspMeRenderPublishFusedCapture(BulletManager *manager)
{
    PspMeRenderFusedCapture &capture = gPspMeRenderFusedCapture;
    capture.building = false;
    if (!capture.complete || !manager || manager != capture.manager ||
        capture.anmManager != g_AnmManager ||
        !PspMeRenderRepresentativeSourceCacheIdentityMatches(
            capture.representativeSourceGeneration) ||
        PspMeRenderFloatBits(capture.arcadeLeft) != PspMeRenderFloatBits(
            g_GameManager.arcadeRegionTopLeftPos.x) ||
        PspMeRenderFloatBits(capture.arcadeTop) != PspMeRenderFloatBits(
            g_GameManager.arcadeRegionTopLeftPos.y))
    {
        capture.published = 0u;
        return;
    }

    u32 total = 0u;
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        if (capture.bucketCounts[bucket] >
            TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS - total)
        {
            capture.published = 0u;
            capture.complete = false;
            return;
        }
        total += capture.bucketCounts[bucket];
    }
    if (manager->bulletCount < 0 ||
        static_cast<u32>(manager->bulletCount) < total)
    {
        capture.published = 0u;
        capture.complete = false;
        return;
    }
    capture.mutationEpoch = manager->pspMeRenderMutationEpoch;
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        capture.bucketHeads[bucket] = manager->bulletsPtrs[bucket];
    }
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
    // Item has first claim on optional ME budget because boss-clear item rain
    // is the observed SC-heavy/ME-idle case.  Preparation itself is deferred
    // until this gate passes so dense bullet frames pay neither its list walk
    // nor its trigonometry.  Failure leaves preparedSerial zero and preserves
    // the canonical Item draw in full.
    if (th07_psp_me_item_render_available() &&
        g_ItemManager.pspMeItemListCount != 0u)
    {
        ++gPspMeRenderShadowWindow.streamItemCandidates;
        gPspMeRenderShadowWindow.streamItemCandidateRecords +=
            g_ItemManager.pspMeItemListCount;
        if (g_ItemManager.pspMeItemListCount >
            gPspMeRenderShadowWindow.streamItemCandidateMax)
        {
            gPspMeRenderShadowWindow.streamItemCandidateMax =
                g_ItemManager.pspMeItemListCount;
        }
        unsigned long long predictedTicks = 0ull;
        PspMeAdaptiveAuxAdmission admission =
            PSP_ME_ADAPTIVE_AUX_REJECT_BUDGET;
        const u32 prefixCount = PspMeAdaptiveItemPrefixCount(
            total, g_ItemManager.pspMeItemListCount,
            &admission, &predictedTicks);
        if (predictedTicks >
            gPspMeRenderShadowWindow.streamItemPredictedTicksMax)
        {
            gPspMeRenderShadowWindow.streamItemPredictedTicksMax =
                predictedTicks;
        }
        if (prefixCount != 0u)
        {
            g_ItemManager.pspMeItemRequestedPrefixCount = prefixCount;
            (void)g_ItemManager.PspPrepareMeItemRenderStream();
        }
        if (admission == PSP_ME_ADAPTIVE_AUX_REJECT_BUDGET)
        {
            ++gPspMeRenderShadowWindow.streamItemBudgetReject;
            const u32 overflow =
                g_ItemManager.pspMeItemListCount - prefixCount;
            if (overflow >
                gPspMeRenderShadowWindow.streamItemBudgetRejectMax)
            {
                gPspMeRenderShadowWindow.streamItemBudgetRejectMax =
                    overflow;
            }
        }
        else if (admission == PSP_ME_ADAPTIVE_AUX_REJECT_BUSY)
        {
            ++gPspMeRenderShadowWindow.streamItemBusyVeto;
        }
    }
#endif
    // Item preparation is independently fail-closed. A missing/invalid Item
    // segment never prevents publication of the already accepted I-ME5 Bullet
    // segment; ItemManager::OnDraw remains the complete fallback.
    if (th07_psp_me_item_render_available() &&
        g_ItemManager.PspMeItemRenderStreamPrepared() &&
        g_ItemManager.pspMeItemPreparedCount <=
            static_cast<u32>(ItemManager::kItemCapacity))
    {
        capture.itemPrepareSerial =
            g_ItemManager.pspMeItemPreparedSerial;
        capture.itemCount = g_ItemManager.pspMeItemPreparedPrefixCount;
        capture.itemTotalCount = g_ItemManager.pspMeItemPreparedCount;
        capture.itemHead = g_ItemManager.listHead.next;
        capture.itemTail = g_ItemManager.pspMeItemPreparedPrefixTail;
        capture.itemSuffixHead = g_ItemManager.pspMeItemPreparedSuffixHead;
        capture.itemListTail = g_ItemManager.listTail;
    }
#endif
    capture.published = 1u;
}

bool PspMeRenderBuildFusedSnapshot(
    BulletManager *manager, const Th07PspMeRenderStreamBuild &build,
    Th07PspMeRenderStreamJob *job)
{
    const PspMeRenderFusedCapture &capture = gPspMeRenderFusedCapture;
    if (!manager || !job || !g_AnmManager ||
#if !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        !build.records ||
        build.recordCapacity < TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS ||
#endif
        capture.published == 0u || capture.manager != manager ||
        capture.anmManager != g_AnmManager ||
        capture.mutationEpoch != manager->pspMeRenderMutationEpoch ||
        !PspMeRenderRepresentativeSourceCacheIdentityMatches(
            capture.representativeSourceGeneration) ||
        PspMeRenderFloatBits(capture.arcadeLeft) != PspMeRenderFloatBits(
            g_GameManager.arcadeRegionTopLeftPos.x) ||
        PspMeRenderFloatBits(capture.arcadeTop) != PspMeRenderFloatBits(
            g_GameManager.arcadeRegionTopLeftPos.y))
    {
        return false;
    }

    u32 bucketEnds[6] = {};
    u32 count = 0u;
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        const u32 bucketCount = capture.bucketCounts[bucket];
        if (capture.bucketHeads[bucket] != manager->bulletsPtrs[bucket] ||
            bucketCount > TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS - count)
        {
            return false;
        }
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        // I-ME5 consumes the six authoritative list heads below.  Counting is
        // retained for the completion contract, but no per-bullet record is
        // staged or copied on SC.
        (void)bucketCount;
#else
        if (bucketCount != 0u)
        {
            const u32 tail =
                TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS - bucketCount;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
            auto *rawRecords = reinterpret_cast<Th07PspMeRenderRawRecord *>(
                build.records);
            std::memcpy(rawRecords + count,
                        capture.records[bucket] + tail,
                        bucketCount * sizeof(Th07PspMeRenderRawRecord));
#else
            std::memcpy(build.records + count, capture.records[bucket] + tail,
                        bucketCount * sizeof(Th07PspMeRenderStreamRecord));
#endif
        }
#endif
        count += bucketCount;
        bucketEnds[bucket] = count;
    }
    if (manager->bulletCount < 0 ||
        static_cast<u32>(manager->bulletCount) < count)
    {
        return false;
    }

    const ZunViewport &viewport = g_Supervisor.viewport;
    *job = Th07PspMeRenderStreamJob{};
    job->token = build.token;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    job->version = TH07_PSP_ME_RENDER_STREAM_LIST_VERSION;
    job->flags = TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (th07_psp_me_item_render_available() &&
        capture.itemPrepareSerial != 0u &&
        capture.itemPrepareSerial ==
            g_ItemManager.pspMeItemPreparedSerial &&
        capture.itemPrepareSerial ==
            g_ItemManager.pspMeItemPrepareSerial &&
        capture.itemCount ==
            g_ItemManager.pspMeItemPreparedPrefixCount &&
        capture.itemTotalCount == g_ItemManager.pspMeItemPreparedCount &&
        capture.itemHead == g_ItemManager.listHead.next &&
        capture.itemTail == g_ItemManager.pspMeItemPreparedPrefixTail &&
        capture.itemSuffixHead ==
            g_ItemManager.pspMeItemPreparedSuffixHead &&
        capture.itemListTail == g_ItemManager.listTail &&
        capture.itemCount != 0u &&
        capture.itemCount <= capture.itemTotalCount &&
        capture.itemTail &&
        capture.itemTail->next == capture.itemSuffixHead)
    {
        job->version = TH07_PSP_ME_RENDER_STREAM_ITEM_VERSION;
        job->flags |= TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        // Capture the next-frame motion sidecar only after its independent
        // startup gate passes.  A downgraded A1 build is therefore command-10
        // byte/cost equivalent to RID29 Item rendering.
        if (th07_psp_me_item_motion_available())
        {
            job->flags |= TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED;
        }
#endif
    }
#endif
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    job->version = TH07_PSP_ME_RENDER_STREAM_RAW_VERSION;
    job->flags = TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE;
#else
    job->version = TH07_PSP_ME_RENDER_STREAM_VERSION;
    job->flags = 0u;
#endif
    job->frameSeq = gPspMeRenderShadow.drawSeq;
    job->targetDrawSeq = job->frameSeq + 1u;
    job->stageEpoch = gPspMeRenderShadow.stageEpoch;
    job->managerEpoch = gPspMeRenderShadow.managerEpoch;
    job->replayEpoch = static_cast<u32>(
        g_ReplayManager ? g_ReplayManager->frameId : 0);
    job->globalSignature = PspMeRenderGlobalSignature();
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        job->bucketEnds[bucket] = bucketEnds[bucket];
    }
    job->recordCount = count;
    job->payloadHash = 0u;
    job->offsetXBits = PspMeRenderFloatBits(g_AnmManager->offset.x);
    job->offsetYBits = PspMeRenderFloatBits(g_AnmManager->offset.y);
    job->viewportLeftBits =
        PspMeRenderFloatBits(static_cast<float>(viewport.x));
    job->viewportTopBits =
        PspMeRenderFloatBits(static_cast<float>(viewport.y));
    job->viewportRightBits = PspMeRenderFloatBits(
        static_cast<float>(viewport.x + viewport.width));
    job->viewportBottomBits = PspMeRenderFloatBits(
        static_cast<float>(viewport.y + viewport.height));
    job->globalColor = g_AnmManager->color.color;
    if (g_AnmManager->colorMulEnabled)
    {
        job->configFlags |= TH07_PSP_ME_RENDER_STREAM_CONFIG_COLOR_MUL;
    }
    if (g_Supervisor.cfg.disableZBuffer)
    {
        job->configFlags |= TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z;
    }
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    const AnmVm *layoutVm = &manager->bullets[0].sprites.spriteBullet;
    const AnmLoadedSprite *layoutSprite = &g_AnmManager->sprites[0];
    const auto memberOffset = [](const void *base, const void *field) -> u32 {
        return static_cast<u32>(reinterpret_cast<uintptr_t>(field) -
                                reinterpret_cast<uintptr_t>(base));
    };
    const auto physicalAddress = [](const void *pointer) -> u32 {
        return static_cast<u32>(reinterpret_cast<uintptr_t>(pointer) &
                                0x1fffffffu);
    };
    Th07PspMeRenderRawLayout &layout = job->rawLayout;
    layout.rawLayoutVersion = TH07_PSP_ME_RENDER_RAW_LAYOUT_VERSION;
    layout.rawRecordBytes = sizeof(Th07PspMeRenderRawRecord);
    layout.bulletBasePhys = physicalAddress(&manager->bullets[0]);
    layout.bulletStride = sizeof(Bullet);
    layout.bulletCount = BulletManager::kBulletCapacity;
    layout.spriteBasePhys = physicalAddress(&g_AnmManager->sprites[0]);
    layout.spriteStride = sizeof(AnmLoadedSprite);
    layout.spriteCount = 2560u;
    layout.representativePhys = physicalAddress(
        &gPspMeRenderRepresentativeSourceCache.representative[0]);
    layout.representativeStride = sizeof(
        gPspMeRenderRepresentativeSourceCache.representative[0]);
    layout.representativeCount = 264u;
    layout.vmBytes = sizeof(AnmVm);
    layout.vmRotationZOffset = memberOffset(layoutVm,
                                            &layoutVm->rotation.z);
    layout.vmScaleXOffset = memberOffset(layoutVm, &layoutVm->scale.x);
    layout.vmScaleYOffset = memberOffset(layoutVm, &layoutVm->scale.y);
    layout.vmUvScrollXOffset = memberOffset(
        layoutVm, &layoutVm->uvScrollPos.x);
    layout.vmUvScrollYOffset = memberOffset(
        layoutVm, &layoutVm->uvScrollPos.y);
    layout.vmColorOffset = memberOffset(layoutVm, &layoutVm->color);
    layout.vmColor2Offset = memberOffset(layoutVm, &layoutVm->color2);
    layout.vmFlagsOffset = memberOffset(layoutVm, &layoutVm->flags);
    layout.vmSpriteOffset = memberOffset(layoutVm, &layoutVm->sprite);
    layout.spriteBytes = sizeof(AnmLoadedSprite);
    layout.spriteSourceOffset = memberOffset(
        layoutSprite, &layoutSprite->sourceFileIndex);
    layout.spriteUvStartXOffset = memberOffset(
        layoutSprite, &layoutSprite->uvStart.x);
    layout.spriteUvStartYOffset = memberOffset(
        layoutSprite, &layoutSprite->uvStart.y);
    layout.spriteUvEndXOffset = memberOffset(
        layoutSprite, &layoutSprite->uvEnd.x);
    layout.spriteUvEndYOffset = memberOffset(
        layoutSprite, &layoutSprite->uvEnd.y);
    layout.spriteHeightOffset = memberOffset(
        layoutSprite, &layoutSprite->heightPx);
    layout.spriteWidthOffset = memberOffset(
        layoutSprite, &layoutSprite->widthPx);
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    job->listLayout.listLayoutVersion =
        TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION;
    job->listLayout.listLayoutBytes = sizeof(Th07PspMeRenderListLayout);
    job->listLayout.bulletBasePhys = physicalAddress(&manager->bullets[0]);
    job->listLayout.bulletStride = sizeof(Bullet);
    job->listLayout.bulletCount = BulletManager::kBulletCapacity;
    job->listLayout.generationBasePhys = physicalAddress(
        &manager->pspMeRenderSlotGenerations[0]);
    job->listLayout.generationStride = sizeof(
        manager->pspMeRenderSlotGenerations[0]);
    job->listLayout.generationCount = BulletManager::kBulletCapacity;
    job->listLayout.activeBitsPhys = physicalAddress(
        &manager->pspActiveBulletBits[0]);
    job->listLayout.activeBitsWordCount =
        sizeof(manager->pspActiveBulletBits) /
        sizeof(manager->pspActiveBulletBits[0]);
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        job->listLayout.bucketHeadPhys[bucket] =
            physicalAddress(capture.bucketHeads[bucket]);
    }
    job->listLayout.bulletNextOffset = __builtin_offsetof(Bullet, next);
    job->listLayout.bulletStateOffset = __builtin_offsetof(Bullet, state);
    job->listLayout.bulletCollisionTypeOffset =
        __builtin_offsetof(Bullet, sprites.collisionType);
    job->listLayout.bulletPosXOffset = __builtin_offsetof(Bullet, pos.x);
    job->listLayout.bulletPosYOffset = __builtin_offsetof(Bullet, pos.y);
    job->listLayout.bulletRenderAngleOffset =
        __builtin_offsetof(Bullet, pspRenderAngle);
    job->listLayout.bulletSinOffset =
        __builtin_offsetof(Bullet, pspRenderSin);
    job->listLayout.bulletCosOffset =
        __builtin_offsetof(Bullet, pspRenderCos);
    job->listLayout.bulletRotationValidOffset =
        __builtin_offsetof(Bullet, pspRenderRotationValid);
    job->listLayout.bulletVmOffsets[0] =
        __builtin_offsetof(Bullet, sprites.spriteBullet);
    job->listLayout.bulletVmOffsets[1] =
        __builtin_offsetof(Bullet, sprites.spriteSpawnEffectFast);
    job->listLayout.bulletVmOffsets[2] =
        __builtin_offsetof(Bullet, sprites.spriteSpawnEffectNormal);
    job->listLayout.bulletVmOffsets[3] =
        __builtin_offsetof(Bullet, sprites.spriteSpawnEffectSlow);
    job->listLayout.bulletVmOffsets[4] =
        __builtin_offsetof(Bullet, sprites.spriteSpawnEffectDonut);
    job->listLayout.arcadeLeftBits = PspMeRenderFloatBits(capture.arcadeLeft);
    job->listLayout.arcadeTopBits = PspMeRenderFloatBits(capture.arcadeTop);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if ((job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u)
    {
        Th07PspMeRenderItemLayout &itemLayout = job->itemLayout;
        itemLayout.itemLayoutVersion =
            TH07_PSP_ME_RENDER_ITEM_LAYOUT_VERSION;
        itemLayout.itemLayoutBytes = sizeof(Th07PspMeRenderItemLayout);
        itemLayout.itemBasePhys = physicalAddress(g_ItemManager.ItemAt(0));
        itemLayout.itemStride = sizeof(Item);
        itemLayout.itemCount = ItemManager::kItemCapacity;
        itemLayout.generationBasePhys = physicalAddress(
            &g_ItemManager.pspMeItemSlotGenerations[0]);
        itemLayout.generationStride = sizeof(
            g_ItemManager.pspMeItemSlotGenerations[0]);
        itemLayout.generationCount = ItemManager::kItemCapacity;
        itemLayout.activeBitsPhys = physicalAddress(
            &g_ItemManager.pspActiveItemBits[0]);
        itemLayout.activeBitsWordCount =
            sizeof(g_ItemManager.pspActiveItemBits) /
            sizeof(g_ItemManager.pspActiveItemBits[0]);
        itemLayout.sinBasePhys = physicalAddress(
            &g_ItemManager.pspMeItemRenderSin[0]);
        itemLayout.sinStride = sizeof(g_ItemManager.pspMeItemRenderSin[0]);
        itemLayout.cosBasePhys = physicalAddress(
            &g_ItemManager.pspMeItemRenderCos[0]);
        itemLayout.cosStride = sizeof(g_ItemManager.pspMeItemRenderCos[0]);
        itemLayout.headPhys = physicalAddress(capture.itemHead);
        itemLayout.tailPhys = physicalAddress(capture.itemTail);
        itemLayout.itemNextOffset = __builtin_offsetof(Item, next);
        itemLayout.itemInUseOffset = __builtin_offsetof(Item, isInUse);
        itemLayout.itemTypeOffset = __builtin_offsetof(Item, itemType);
        itemLayout.itemVmOffset = __builtin_offsetof(Item, sprite);
        itemLayout.vmPosXOffset = __builtin_offsetof(AnmVm, pos.x);
        itemLayout.vmPosYOffset = __builtin_offsetof(AnmVm, pos.y);
        itemLayout.vmPosZOffset = __builtin_offsetof(AnmVm, pos.z);
        itemLayout.prepareSerialPhys = physicalAddress(
            &g_ItemManager.pspMeItemPrepareSerial);
        itemLayout.preparedSerialPhys = physicalAddress(
            &g_ItemManager.pspMeItemPreparedSerial);
        itemLayout.preparedCountPhys = physicalAddress(
            &g_ItemManager.pspMeItemPreparedCount);
        itemLayout.expectedPrepareSerial = capture.itemPrepareSerial;
        itemLayout.expectedItemCount = capture.itemCount;
        itemLayout.expectedTotalCount = capture.itemTotalCount;
        itemLayout.suffixHeadPhys = physicalAddress(capture.itemSuffixHead);
        for (unsigned int &reserved : itemLayout.reserved)
        {
            reserved = 0u;
        }
    }
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    // Effect is independently optional and shares the fixed auxiliary pool.
    // It may be present even when Item preparation rejected: in that case its
    // physical prefix starts at record/vertex/run zero.
    const u32 effectItemCount =
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u
            ? job->itemLayout.expectedItemCount : 0u;
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
    const u32 adaptiveEffectCount =
        g_EffectManager.pspMeEffectListCounts[0] +
        g_EffectManager.pspMeEffectListCounts[1];
    // Item owns first refusal.  Do not let a cheaper Effect segment displace
    // a present Item list that the conservative budget rejected.
    const bool adaptiveItemPrioritySatisfied =
        g_ItemManager.pspMeItemListCount == 0u || effectItemCount != 0u;
    const bool effectPrepared = adaptiveEffectCount != 0u &&
        adaptiveItemPrioritySatisfied &&
        PspMeAdaptiveAuxAdmissionFor(count, effectItemCount,
                                     adaptiveEffectCount, nullptr) ==
            PSP_ME_ADAPTIVE_AUX_ADMIT &&
        g_EffectManager.PspPrepareMeEffectRenderStream();
#else
    const bool effectPrepared = true;
#endif
    if (effectPrepared &&
        g_EffectManager.PspBuildMeEffectRenderLayout(
            &job->effectLayout, effectItemCount))
    {
        job->version = TH07_PSP_ME_RENDER_STREAM_EFFECT_VERSION;
        job->flags |= TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST;
    }
#endif
#endif
#endif
#endif
    return bucketEnds[5] == count;
}
#endif

bool PspMeRenderBuildCorrectnessSnapshot(
    BulletManager *manager, const Th07PspMeRenderStreamBuild &build,
    Th07PspMeRenderStreamJob *job)
{
    if (!manager || !build.records || !job || !g_AnmManager ||
        build.recordCapacity < TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS)
    {
        return false;
    }

    u32 seen[(BulletManager::kBulletCapacity + 31u) / 32u] = {};
    const uintptr_t bulletBase =
        reinterpret_cast<uintptr_t>(&manager->bullets[0]);
    const uintptr_t bulletEnd = reinterpret_cast<uintptr_t>(
        &manager->bullets[BulletManager::kBulletCapacity]);
    u32 bucketEnds[6] = {};
    u32 count = 0u;

    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        Bullet *bullet = manager->bulletsPtrs[bucket];
        while (bullet)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(bullet);
            if (count >= TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS ||
                address < bulletBase || address >= bulletEnd ||
                ((address - bulletBase) % sizeof(Bullet)) != 0u)
            {
                return false;
            }
            const u32 slot =
                static_cast<u32>((address - bulletBase) / sizeof(Bullet));
            const u32 bit = 1u << (slot & 31u);
            u32 &seenWord = seen[slot >> 5u];
            if ((seenWord & bit) != 0u ||
                !manager->PspIsBulletSlotTracked(static_cast<i32>(slot)))
            {
                return false;
            }
            seenWord |= bit;

            AnmVm *vm = PspMeRenderSelectVm(bullet);
            if (!vm)
            {
                return false;
            }
            Th07PspMeRenderStreamRecord &record = build.records[count];
            std::memset(&record, 0, sizeof(record));

            const float posX =
                g_GameManager.arcadeRegionTopLeftPos.x + bullet->pos.x;
            const float posY =
                g_GameManager.arcadeRegionTopLeftPos.y + bullet->pos.y;
            record.posXBits = PspMeRenderFloatBits(posX);
            record.posYBits = PspMeRenderFloatBits(posY);
            record.posZBits = PspMeRenderFloatBits(0.05f);
            record.slot = slot;
            record.slotGeneration = manager->pspMeRenderSlotGenerations[slot];
            if (record.slotGeneration == 0u)
            {
                return false;
            }

            u32 flags = 0u;
            float rotation = vm->rotation.z;
            float sine = 0.0f;
            float cosine = 1.0f;
            if (vm->autoRotate)
            {
                if (bullet->pspRenderRotationValid &&
                    bullet->pspRenderSourceAngle == bullet->angle)
                {
                    rotation = bullet->pspRenderAngle;
                    sine = bullet->pspRenderSin;
                    cosine = bullet->pspRenderCos;
                }
                else
                {
                    rotation = utils::AddNormalizeAngle(
                        1.5707964f + bullet->angle, 0.0f);
                    PspBulletRenderSinCos(rotation, &sine, &cosine);
                    // This is the same render-only cache that Bullet::Draw
                    // would finalize a few milliseconds later.  Publishing
                    // the snapshot owns that canonical VFPU work so the SC
                    // visible path does not repeat it, while VM mutations
                    // remain in their original draw-time order.
                    bullet->pspRenderSin = sine;
                    bullet->pspRenderCos = cosine;
                    bullet->pspRenderSourceAngle = bullet->angle;
                    bullet->pspRenderAngle = rotation;
                    bullet->pspRenderRotationValid = 1;
                }
            }
            else if (rotation != 0.0f)
            {
                PspBulletRenderSinCos(rotation, &sine, &cosine);
            }
            if (rotation != 0.0f)
            {
                flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED;
            }
            record.sinBits = PspMeRenderFloatBits(sine);
            record.cosBits = PspMeRenderFloatBits(cosine);

            ZunColor baseColor;
            if (vm->useColor2)
            {
                baseColor = vm->color2;
            }
            else
            {
                baseColor.color =
                    (vm->color.color & 0xff000000u) | 0x00ffffffu;
            }
            record.color = baseColor.color;
            flags |= (static_cast<u32>(vm->anchor)
                      << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT) &
                     TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK;
            if (vm->blendMode)
            {
                flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD;
            }
            if (vm->zWriteDisable)
            {
                flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE;
            }

            u32 source = 0u;
            if (vm->sprite)
            {
                const i32 originalSource = vm->sprite->sourceFileIndex;
                const bool drawable =
                    vm->visible && vm->active && vm->color.bytes.a;
                if (drawable &&
                    (originalSource < 0 || originalSource >= 264))
                {
                    return false;
                }
                if (originalSource >= 0 && originalSource < 264)
                {
                    source = PspMeRenderRepresentativeSource(originalSource);
                }
                record.halfWidthBits = PspMeRenderFloatBits(
                    vm->sprite->widthPx * vm->scale.x * 0.5f);
                record.halfHeightBits = PspMeRenderFloatBits(
                    vm->sprite->heightPx * vm->scale.y * 0.5f);
                record.u0Bits = PspMeRenderFloatBits(
                    vm->sprite->uvStart.x + vm->uvScrollPos.x);
                record.u1Bits = PspMeRenderFloatBits(
                    vm->sprite->uvEnd.x + vm->uvScrollPos.x);
                record.v0Bits = PspMeRenderFloatBits(
                    vm->sprite->uvStart.y + vm->uvScrollPos.y);
                record.v1Bits = PspMeRenderFloatBits(
                    vm->sprite->uvEnd.y + vm->uvScrollPos.y);
                if (drawable)
                {
                    flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
                }
            }
            record.sourceAndState =
                (source & 0xffffu) |
                ((static_cast<u32>(bullet->state) & 0xffffu) << 16u);
            record.flags = flags;
            ++count;
            bullet = bullet->next;
        }
        bucketEnds[bucket] = count;
    }

    const ZunViewport &viewport = g_Supervisor.viewport;
    *job = Th07PspMeRenderStreamJob{};
    job->token = build.token;
    job->version = TH07_PSP_ME_RENDER_STREAM_VERSION;
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    // Hardware-performance authority comes from the fixed token-owned pools,
    // completion echo and the draw-time header/run preflight.  Stream hashing
    // remains available in I-ME1 and diagnostic I-ME2, but is deliberately not
    // paid once per frame by this explicit performance profile.
    job->flags = 0u;
#else
    job->flags = TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH |
                 TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
#endif
    // `frameSeq` names the draw whose completed calc produced this snapshot;
    // `targetDrawSeq` is the immediately following visible draw.  Deriving
    // both from the draw serial prevents one skipped snapshot from permanently
    // drifting two independent counters apart.
    job->frameSeq = gPspMeRenderShadow.drawSeq;
    job->targetDrawSeq = job->frameSeq + 1u;
    job->stageEpoch = gPspMeRenderShadow.stageEpoch;
    job->managerEpoch = gPspMeRenderShadow.managerEpoch;
    job->replayEpoch = static_cast<u32>(
        g_ReplayManager ? g_ReplayManager->frameId : 0);
    job->globalSignature = PspMeRenderGlobalSignature();
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        job->bucketEnds[bucket] = bucketEnds[bucket];
    }
    job->recordCount = count;
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    job->payloadHash = 0u;
#else
    job->payloadHash = th07_psp_me_render_stream_hash(
        build.records, count * sizeof(Th07PspMeRenderStreamRecord));
#endif
    job->offsetXBits = PspMeRenderFloatBits(g_AnmManager->offset.x);
    job->offsetYBits = PspMeRenderFloatBits(g_AnmManager->offset.y);
    job->viewportLeftBits =
        PspMeRenderFloatBits(static_cast<float>(viewport.x));
    job->viewportTopBits =
        PspMeRenderFloatBits(static_cast<float>(viewport.y));
    job->viewportRightBits = PspMeRenderFloatBits(
        static_cast<float>(viewport.x + viewport.width));
    job->viewportBottomBits = PspMeRenderFloatBits(
        static_cast<float>(viewport.y + viewport.height));
    job->globalColor = g_AnmManager->color.color;
    if (g_AnmManager->colorMulEnabled)
    {
        job->configFlags |= TH07_PSP_ME_RENDER_STREAM_CONFIG_COLOR_MUL;
    }
    if (g_Supervisor.cfg.disableZBuffer)
    {
        job->configFlags |= TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z;
    }
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    return bucketEnds[5] == count && manager->bulletCount >= 0 &&
           static_cast<u32>(manager->bulletCount) == count;
#else
    return bucketEnds[5] == count;
#endif
}
#endif

void PspMeRenderRecordSlack(u32 slackUs)
{
    if (gPspMeRenderSlackCount < kPspMeRenderSlackSamples)
    {
        gPspMeRenderSlackUs[gPspMeRenderSlackCount++] = slackUs;
    }
    else
    {
        ++gPspMeRenderSlackOverflow;
    }
}

void PspMeRenderRecordKernelCycles(u32 cycles)
{
    if (gPspMeRenderKernelCycleCount < kPspMeRenderSlackSamples)
    {
        gPspMeRenderKernelCycles[gPspMeRenderKernelCycleCount++] = cycles;
    }
    else
    {
        ++gPspMeRenderKernelCycleOverflow;
    }
}

int PspMeRenderRetirePending(bool atDeadline)
{
    Th07PspMeRenderCompletion completion{};
    const int probe = th07_psp_me_render_probe(&completion);
    if (probe <= 0)
    {
        if (probe < 0)
        {
            ++gPspMeRenderShadowWindow.protocolFault;
            __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                             __ATOMIC_RELEASE);
        }
        return probe;
    }

    const bool generationValid =
        completion.frameSeq == gPspMeRenderShadow.pendingFrameSeq &&
        completion.targetDrawSeq ==
            gPspMeRenderShadow.pendingTargetDrawSeq;
    const bool boundsValid =
        completion.recordCount == gPspMeRenderShadow.pendingRecordCount &&
        completion.inputStride == gPspMeRenderShadow.pendingInputStride &&
        completion.outputBytes == gPspMeRenderShadow.pendingOutputBytes &&
        completion.outputBytes <=
            TH07_PSP_ME_RENDER_MAX_RECORDS *
                TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
    if (!generationValid)
    {
        ++gPspMeRenderShadowWindow.generationDrop;
    }
    if (!boundsValid)
    {
        ++gPspMeRenderShadowWindow.boundsDrop;
    }

    if (th07_psp_me_render_retire(&completion) != 1)
    {
        ++gPspMeRenderShadowWindow.protocolFault;
        if (atDeadline)
        {
            ++gPspMeRenderShadowWindow.deadlineFault;
        }
        __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                         __ATOMIC_RELEASE);
        return -1;
    }

    gPspMeRenderShadowWindow.scWritebackUs += completion.scWritebackUs;
    gPspMeRenderShadowWindow.scOutputPrepareUs +=
        completion.scOutputPrepareUs;
    gPspMeRenderShadowWindow.scSubmitUs += completion.scSubmitUs;
    gPspMeRenderShadowWindow.scInvalidateUs += completion.scInvalidateUs;
    gPspMeRenderShadowWindow.dispatchUs += completion.dispatchWaitUs;
    gPspMeRenderShadowWindow.meInvalidateCycles +=
        completion.meInvalidateCycles;
    gPspMeRenderShadowWindow.meKernelCycles += completion.meKernelCycles;
    th07_usage_meter_add_me_cycles(
        completion.meInvalidateCycles + completion.meKernelCycles +
        completion.meWritebackCycles);
    gPspMeRenderShadowWindow.meWritebackCycles +=
        completion.meWritebackCycles;
    const bool fcrValid = completion.meFcr31Effective == 0u &&
                          completion.meFcr31Before ==
                              completion.meFcr31After;
    if (!fcrValid)
    {
        ++gPspMeRenderShadowWindow.protocolFault;
        ++gPspMeRenderShadowWindow.fcrDrop;
        __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                         __ATOMIC_RELEASE);
    }

    if (atDeadline)
    {
        ++gPspMeRenderShadowWindow.meRenderCompleted;
        PspMeRenderRecordKernelCycles(completion.meKernelCycles);
        if (!__atomic_load_n(&gPspMeRenderShadow.available,
                             __ATOMIC_ACQUIRE))
        {
            // Suspend/fault publication wins even if DONE raced the deadline.
        }
        else if (!generationValid || !boundsValid)
        {
            // Echo/bounds mismatches were counted above and can never qualify
            // as a hypothetical consumer frame.
        }
        else if (gPspMeRenderShadow.pendingStageEpoch !=
            gPspMeRenderShadow.stageEpoch)
        {
            ++gPspMeRenderShadowWindow.epochDrop;
            ++gPspMeRenderShadowWindow.stageEpochDrop;
        }
        else if (gPspMeRenderShadow.pendingManagerEpoch !=
                 gPspMeRenderShadow.managerEpoch)
        {
            ++gPspMeRenderShadowWindow.epochDrop;
            ++gPspMeRenderShadowWindow.managerEpochDrop;
        }
        else if (gPspMeRenderShadow.pendingReplayEpoch !=
                 static_cast<u32>(g_ReplayManager ? g_ReplayManager->frameId
                                                   : 0))
        {
            ++gPspMeRenderShadowWindow.epochDrop;
            ++gPspMeRenderShadowWindow.replayEpochDrop;
        }
        else if (!fcrValid)
        {
        }
        else if (gPspMeRenderShadow.pendingSignature ==
                 PspMeRenderGlobalSignature())
        {
            ++gPspMeRenderShadowWindow.wouldConsume;
        }
        else
        {
            ++gPspMeRenderShadowWindow.signatureDrop;
        }
    }
    else
    {
        ++gPspMeRenderShadowWindow.lateRetired;
    }
    gPspMeRenderShadow.pending = false;
    gPspMeRenderShadow.deadlineAccounted = false;
    return 1;
}

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
static_assert(alignof(Th07PspMeRenderStreamVertex) == 4u,
              "C1 SC stream vertex must retain 32-bit alignment");
bool PspMeRenderPackS16Reference(float value, float scale, short *packed)
{
    const float scaled = value * scale;
    if (!(scaled >= -32768.0f && scaled < 32767.5f))
    {
        return false;
    }
    i32 rounded;
    if (scaled <= -32767.5f)
    {
        rounded = -32768;
    }
    else
    {
        rounded = static_cast<i32>(
            scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
    }
    *packed = static_cast<short>(rounded);
    return true;
}

#if defined(TH07_PSP_ME_RENDER_UV16)
bool PspMeRenderPackU16Reference(float value, float scale,
                                 unsigned short *packed)
{
    const float scaled = value * scale;
    if (!(scaled >= 0.0f && scaled < 65535.5f))
    {
        return false;
    }
    *packed = static_cast<unsigned short>(scaled + 0.5f);
    return true;
}
#endif

bool PspMeRenderPackReferenceVertex(
    const Th07PspSpriteVertex &source,
    Th07PspMeRenderStreamVertex *destination)
{
    if (!destination)
    {
        return false;
    }
#if defined(TH07_PSP_ME_RENDER_UV16)
    unsigned short u;
    unsigned short v;
    if (!PspMeRenderPackU16Reference(source.u, 32768.0f, &u) ||
        !PspMeRenderPackU16Reference(source.v, 32768.0f, &v))
    {
        return false;
    }
#endif
#if defined(TH07_PSP_ME_RENDER_XYZ16)
    short x;
    short y;
    short z;
    if (!PspMeRenderPackS16Reference(source.x, 32.0f, &x) ||
        !PspMeRenderPackS16Reference(source.y, 32.0f, &y) ||
        !PspMeRenderPackS16Reference(source.z, 32768.0f, &z))
    {
        return false;
    }
#endif
#if defined(TH07_PSP_ME_RENDER_UV16)
    destination->u = u;
    destination->v = v;
#else
    std::memcpy(&destination->uBits, &source.u, sizeof(source.u));
    std::memcpy(&destination->vBits, &source.v, sizeof(source.v));
#endif
    destination->color = source.color;
#if defined(TH07_PSP_ME_RENDER_XYZ16)
    destination->x = x;
    destination->y = y;
    destination->z = z;
    destination->reserved = 0u;
#else
    std::memcpy(&destination->xBits, &source.x, sizeof(source.x));
    std::memcpy(&destination->yBits, &source.y, sizeof(source.y));
    std::memcpy(&destination->zBits, &source.z, sizeof(source.z));
#endif
    return true;
}
#else
static_assert(sizeof(Th07PspSpriteVertex) ==
                  sizeof(Th07PspMeRenderStreamVertex),
              "I-ME1 must compare the native 24-byte vertex ABI directly");
#endif

bool PspMeRenderCorrectnessCompletionMatches(
    const Th07PspMeRenderStreamCompletion &completion)
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    constexpr u32 maxVertexBytes =
        TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_VERTEX_BYTES;
    constexpr u32 maxRuns = TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_RUNS;
#else
    constexpr u32 maxVertexBytes =
        TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES;
    constexpr u32 maxRuns = TH07_PSP_ME_RENDER_STREAM_MAX_RUNS;
#endif
    if (completion.token.slot != state.token.slot ||
        completion.token.generation != state.token.generation ||
        completion.version != state.job.version ||
        completion.flags != state.job.flags ||
        completion.frameSeq != state.job.frameSeq ||
        completion.targetDrawSeq != state.job.targetDrawSeq ||
        completion.stageEpoch != state.job.stageEpoch ||
        completion.managerEpoch != state.job.managerEpoch ||
        completion.replayEpoch != state.job.replayEpoch ||
        completion.globalSignature != state.job.globalSignature ||
        completion.recordCount != state.job.recordCount ||
#if !defined(TH07_PSP_ME_RENDER_PERFORMANCE)
        completion.payloadHash != state.job.payloadHash ||
#endif
        completion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        completion.outputBytes > maxVertexBytes ||
        completion.vertexCount * sizeof(Th07PspMeRenderStreamVertex) !=
            completion.outputBytes ||
        completion.runCount > maxRuns
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        || completion.itemRecordCount >
               TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS
        || completion.itemVertexCount > completion.vertexCount
        || completion.itemRunCount > completion.runCount
        || (completion.itemResult !=
                TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
            (completion.itemVertexCount != 0u ||
             completion.itemRunCount != 0u))
        || (((state.job.flags &
              TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u) &&
            completion.itemRecordCount != state.itemRecordCount)
        || (((state.job.flags &
              TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) == 0u) &&
            (completion.itemRecordCount != 0u ||
             completion.itemVertexCount != 0u ||
             completion.itemRunCount != 0u))
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        || completion.effectLayer0RecordCount >
               TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS
        || completion.effectLayer3RecordCount >
               TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS
        || completion.effectLayer0VertexCount >
               completion.vertexCount - completion.itemVertexCount
        || completion.effectLayer3VertexCount >
               completion.vertexCount - completion.itemVertexCount -
                   completion.effectLayer0VertexCount
        || completion.effectLayer0RunCount >
               completion.runCount - completion.itemRunCount
        || completion.effectLayer3RunCount >
               completion.runCount - completion.itemRunCount -
                   completion.effectLayer0RunCount
        || (completion.effectResult !=
                TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
            (completion.effectLayer0VertexCount != 0u ||
             completion.effectLayer0RunCount != 0u ||
             completion.effectLayer3VertexCount != 0u ||
             completion.effectLayer3RunCount != 0u))
        || (((state.job.flags &
              TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST) != 0u) &&
            (completion.effectLayer0RecordCount !=
                 state.effectLayer0RecordCount ||
             completion.effectLayer3RecordCount !=
                 state.effectLayer3RecordCount))
        || (((state.job.flags &
              TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST) == 0u) &&
            (completion.effectLayer0RecordCount != 0u ||
             completion.effectLayer0VertexCount != 0u ||
             completion.effectLayer0RunCount != 0u ||
             completion.effectLayer3RecordCount != 0u ||
             completion.effectLayer3VertexCount != 0u ||
             completion.effectLayer3RunCount != 0u))
#endif
#endif
        )
    {
        return false;
    }
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        if (completion.bucketEnds[bucket] != state.job.bucketEnds[bucket])
        {
            return false;
        }
    }
    return completion.meFcr31Effective == 0u &&
           completion.meFcr31Before == completion.meFcr31After;
}

#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
bool PspMeRenderRawRecordSoftRejectMatches(
    const Th07PspMeRenderStreamCompletion &completion)
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const u32 expectedFlags =
        TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        | (state.itemPrepareSerial != 0u
               ? TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST : 0u)
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        | ((state.job.flags &
            TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED) != 0u
               ? TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED : 0u)
#endif
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        | (state.effectPrepareSerial != 0u
               ? TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST : 0u)
#endif
#endif
        ;
    const u32 expectedVersion =
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        state.effectPrepareSerial != 0u
            ? TH07_PSP_ME_RENDER_STREAM_EFFECT_VERSION :
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        state.itemPrepareSerial != 0u
            ? TH07_PSP_ME_RENDER_STREAM_ITEM_VERSION :
#endif
              TH07_PSP_ME_RENDER_STREAM_LIST_VERSION;
#else
    constexpr u32 expectedVersion = TH07_PSP_ME_RENDER_STREAM_RAW_VERSION;
    constexpr u32 expectedFlags = TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE;
#endif
    if (state.job.version != expectedVersion ||
        state.job.flags != expectedFlags ||
        completion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD ||
        completion.firstBadRecord >= state.job.recordCount ||
        completion.outputBytes != 0u || completion.vertexCount != 0u ||
        completion.runCount != 0u ||
        completion.token.slot != state.token.slot ||
        completion.token.generation != state.token.generation ||
        completion.version != state.job.version ||
        completion.flags != state.job.flags ||
        completion.frameSeq != state.job.frameSeq ||
        completion.targetDrawSeq != state.job.targetDrawSeq ||
        completion.stageEpoch != state.job.stageEpoch ||
        completion.managerEpoch != state.job.managerEpoch ||
        completion.replayEpoch != state.job.replayEpoch ||
        completion.globalSignature != state.job.globalSignature ||
        completion.recordCount != state.job.recordCount ||
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        // A Bullet RECORD reject may recycle the token only when no Item
        // prefix bytes/runs can be submitted.  itemRecordCount/result remain
        // diagnostic echoes, but the worker must have discarded the prefix.
        completion.itemVertexCount != 0u ||
        completion.itemRunCount != 0u ||
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        completion.effectLayer0VertexCount != 0u ||
        completion.effectLayer0RunCount != 0u ||
        completion.effectLayer3VertexCount != 0u ||
        completion.effectLayer3RunCount != 0u ||
#endif
#endif
        completion.meFcr31Effective != 0u ||
        completion.meFcr31Before != completion.meFcr31After)
    {
        return false;
    }
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        if (completion.bucketEnds[bucket] != state.job.bucketEnds[bucket])
        {
            return false;
        }
    }
    return true;
}
#endif

bool PspMeRenderCorrectnessGlobalsMatch()
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    const ZunViewport &viewport = g_Supervisor.viewport;
    u32 configFlags = 0u;
    if (g_AnmManager->colorMulEnabled)
    {
        configFlags |= TH07_PSP_ME_RENDER_STREAM_CONFIG_COLOR_MUL;
    }
    if (g_Supervisor.cfg.disableZBuffer)
    {
        configFlags |= TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z;
    }
    return state.job.offsetXBits ==
               PspMeRenderFloatBits(g_AnmManager->offset.x) &&
           state.job.offsetYBits ==
               PspMeRenderFloatBits(g_AnmManager->offset.y) &&
           state.job.viewportLeftBits ==
               PspMeRenderFloatBits(static_cast<float>(viewport.x)) &&
           state.job.viewportTopBits ==
               PspMeRenderFloatBits(static_cast<float>(viewport.y)) &&
           state.job.viewportRightBits == PspMeRenderFloatBits(
               static_cast<float>(viewport.x + viewport.width)) &&
           state.job.viewportBottomBits == PspMeRenderFloatBits(
               static_cast<float>(viewport.y + viewport.height)) &&
           state.job.globalColor == g_AnmManager->color.color &&
           state.job.configFlags == configFlags &&
           state.arcadeLeftBits == PspMeRenderFloatBits(
               g_GameManager.arcadeRegionTopLeftPos.x) &&
           state.arcadeTopBits == PspMeRenderFloatBits(
               g_GameManager.arcadeRegionTopLeftPos.y) &&
           state.viewportMinZBits == PspMeRenderFloatBits(viewport.minZ) &&
           state.viewportMaxZBits == PspMeRenderFloatBits(viewport.maxZ);
}

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
bool PspMeRenderReadyAuthorityMatches(u32 expectedDrawSeq)
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const u32 expectedFlags =
        TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        | (state.itemPrepareSerial != 0u
               ? TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST : 0u)
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        | ((state.job.flags &
            TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED) != 0u
               ? TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED : 0u)
#endif
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        | (state.effectPrepareSerial != 0u
               ? TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST : 0u)
#endif
#endif
        ;
    const u32 expectedVersion =
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        state.effectPrepareSerial != 0u
            ? TH07_PSP_ME_RENDER_STREAM_EFFECT_VERSION :
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        state.itemPrepareSerial != 0u
            ? TH07_PSP_ME_RENDER_STREAM_ITEM_VERSION :
#endif
              TH07_PSP_ME_RENDER_STREAM_LIST_VERSION;
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    constexpr u32 expectedVersion = TH07_PSP_ME_RENDER_STREAM_RAW_VERSION;
    constexpr u32 expectedFlags =
        TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE;
#else
    constexpr u32 expectedVersion = TH07_PSP_ME_RENDER_STREAM_VERSION;
    constexpr u32 expectedFlags = 0u;
#endif
    if (!g_AnmManager || !gPspMeRenderShadow.managerActive ||
        !__atomic_load_n(&gPspMeRenderShadow.available, __ATOMIC_ACQUIRE) ||
        state.job.token.slot != state.token.slot ||
        state.job.token.generation != state.token.generation ||
        state.job.version != expectedVersion ||
        state.job.frameSeq != state.frameSeq ||
        state.job.targetDrawSeq != state.targetDrawSeq ||
        state.job.targetDrawSeq != expectedDrawSeq ||
        state.job.stageEpoch != state.stageEpoch ||
        state.job.stageEpoch != gPspMeRenderShadow.stageEpoch ||
        state.job.managerEpoch != state.managerEpoch ||
        state.job.managerEpoch != gPspMeRenderShadow.managerEpoch ||
        state.job.replayEpoch != state.replayEpoch ||
        state.job.replayEpoch != static_cast<u32>(
            g_ReplayManager ? g_ReplayManager->frameId : 0) ||
        state.job.globalSignature != state.globalSignature ||
        state.job.globalSignature != PspMeRenderGlobalSignature() ||
        state.job.recordCount != state.recordCount ||
        state.recordCount > TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS ||
        g_BulletManager.bulletCount != state.managerBulletCount ||
        state.managerBulletCount < 0 ||
        static_cast<u32>(state.managerBulletCount) < state.recordCount ||
        g_BulletManager.updateCount != state.managerUpdateCount ||
        g_BulletManager.time.previous != state.managerTimePrevious ||
        PspMeRenderFloatBits(g_BulletManager.time.subFrame) !=
            state.managerTimeSubFrameBits ||
        g_BulletManager.time.current != state.managerTimeCurrent ||
        g_BulletManager.pspMeRenderMutationEpoch !=
            state.managerMutationEpoch ||
        state.representativeSourceGeneration == 0u ||
        state.representativeSourceGeneration !=
            gPspMeRenderRepresentativeSourceCache.generation ||
        !PspMeRenderRepresentativeSourceCacheMatches() ||
        !PspMeRenderCorrectnessGlobalsMatch())
    {
        return false;
    }
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    if (state.job.flags != expectedFlags || state.job.payloadHash != 0u)
    {
        return false;
    }
#endif

    // BulletManager::OnUpdate increments bulletCount before processing each
    // live slot.  A bullet that despawns during that processing is counted but
    // deliberately omitted from the rebuilt draw lists, so bulletCount may be
    // greater than this snapshot's linked-record count.  It may never be
    // smaller.  The equality against the captured manager count above still
    // detects any draw-time mutation without rejecting those canonical
    // same-update removals.
    //
    // Priority 18 is the final ordinary calc-chain owner (all production
    // callbacks are priorities 0..17).  Its serial is therefore the lifetime
    // proof that this post-calc job names the immediately following draw.  The
    // six bucket ends still need a cheap monotonic bounds proof here because
    // PERFORMANCE intentionally does not walk the live lists a second time.
    u32 previousEnd = 0u;
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        const u32 end = state.job.bucketEnds[bucket];
        if (g_BulletManager.bulletsPtrs[bucket] !=
                state.managerBucketHeads[bucket] ||
            end < previousEnd || end > state.recordCount)
        {
            return false;
        }
        previousEnd = end;
    }
    return previousEnd == state.recordCount;
}

bool PspMeRenderReadyRunsValid()
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    const Th07PspMeRenderStreamReady &ready = state.ready;
    if ((ready.runCount == 0u) != (ready.vertexBytes == 0u))
    {
        return false;
    }

    const u32 vertexCapacity =
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_VERTEX_BYTES /
#else
        TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES /
#endif
        sizeof(Th07PspMeRenderStreamVertex);
    const u32 allowedStateFlags =
        TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD |
        TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE;
    u32 totalRecords = 0u;
    u32 totalVertices =
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        ready.itemVertexCount;
#else
        0u;
#endif
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    if (ready.effectLayer0VertexCount > vertexCapacity - totalVertices)
    {
        return false;
    }
    totalVertices += ready.effectLayer0VertexCount;
    if (ready.effectLayer3VertexCount > vertexCapacity - totalVertices)
    {
        return false;
    }
    totalVertices += ready.effectLayer3VertexCount;
#endif
    u32 previousFirstRecord = 0u;
    u32 previousRecordEnd = 0u;
    bool generalMode = false;

    const u32 firstBulletRun =
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        ready.itemRunCount
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        + ready.effectLayer0RunCount + ready.effectLayer3RunCount
#endif
        ;
#else
        0u;
#endif
    if (firstBulletRun > ready.runCount)
    {
        return false;
    }
    for (u32 runIndex = firstBulletRun; runIndex < ready.runCount;
         ++runIndex)
    {
        const Th07PspMeRenderStreamRun &run = ready.runs[runIndex];
        if (run.recordCount == 0u || run.firstRecord >= state.recordCount ||
            (runIndex != firstBulletRun &&
             run.firstRecord <= previousFirstRecord) ||
            run.recordCount > state.recordCount - run.firstRecord ||
            run.firstRecord < previousRecordEnd ||
            run.recordCount > state.recordCount - totalRecords ||
            run.firstVertex != totalVertices ||
            run.sourceFileIndex >= 264u ||
            PspMeRenderRepresentativeSource(
                static_cast<i32>(run.sourceFileIndex)) !=
                run.sourceFileIndex ||
            g_AnmManager->textures[run.sourceFileIndex].id == 0u ||
            (run.logicalState != 0xffffffffu &&
             run.logicalState >= static_cast<u32>(BULLET_END_ARRAY)) ||
            (run.renderStateFlags & ~allowedStateFlags) != 0u ||
            ((state.job.configFlags &
              TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z) != 0u &&
             (run.renderStateFlags &
              TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE) != 0u))
        {
            return false;
        }

        u32 verticesPerRecord = 0u;
        if (run.primitive ==
            TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES)
        {
            // UQ is callback-wide and sticky: once any record requires quads,
            // no later run may return to GU_SPRITES in this same bullet layer.
            if (generalMode)
            {
                return false;
            }
            verticesPerRecord = 2u;
        }
        else if (run.primitive ==
                 TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS)
        {
            generalMode = true;
            verticesPerRecord = 4u;
        }
        else
        {
            return false;
        }

        if (run.recordCount > vertexCapacity / verticesPerRecord)
        {
            return false;
        }
        const u32 expectedVertices = run.recordCount * verticesPerRecord;
        if (run.vertexCount != expectedVertices ||
            run.vertexCount > vertexCapacity - totalVertices)
        {
            return false;
        }
        totalRecords += run.recordCount;
        totalVertices += run.vertexCount;
        previousFirstRecord = run.firstRecord;
        previousRecordEnd = run.firstRecord + run.recordCount;
    }

    return totalVertices <= vertexCapacity &&
           totalVertices * sizeof(Th07PspMeRenderStreamVertex) ==
               ready.vertexBytes;
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
bool PspMeRenderItemAuthorityMatches()
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    return (state.job.flags &
            TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u &&
           state.itemPrepareSerial != 0u &&
           state.itemPrepareSerial ==
               g_ItemManager.pspMeItemPrepareSerial &&
           state.itemPrepareSerial ==
               g_ItemManager.pspMeItemPreparedSerial &&
           state.itemTotalCount ==
               g_ItemManager.pspMeItemPreparedCount &&
           state.itemRecordCount ==
               g_ItemManager.pspMeItemPreparedPrefixCount &&
           state.itemHead == g_ItemManager.listHead.next &&
           state.itemTail == g_ItemManager.pspMeItemPreparedPrefixTail &&
           state.itemSuffixHead ==
               g_ItemManager.pspMeItemPreparedSuffixHead &&
           state.itemListTail == g_ItemManager.listTail &&
           state.itemRecordCount <= state.itemTotalCount &&
           state.itemTail != nullptr &&
           state.itemTail->next == state.itemSuffixHead &&
           state.itemListTail != nullptr &&
           state.itemListTail->next == nullptr &&
           ((state.itemRecordCount == state.itemTotalCount) ==
                (state.itemSuffixHead == nullptr)) &&
           state.job.itemLayout.expectedPrepareSerial ==
               state.itemPrepareSerial &&
           state.job.itemLayout.expectedItemCount == state.itemRecordCount &&
           state.job.itemLayout.expectedTotalCount == state.itemTotalCount &&
           state.job.itemLayout.suffixHeadPhys ==
               static_cast<u32>(
                   reinterpret_cast<uintptr_t>(state.itemSuffixHead) &
                   0x1fffffffu);
}

bool PspMeRenderReadyItemRunsValid()
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    const Th07PspMeRenderStreamReady &ready = state.ready;
    if (!PspMeRenderItemAuthorityMatches() ||
        ready.itemResult != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        ready.itemRecordCount != state.itemRecordCount ||
        ready.itemRecordCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS ||
        ready.itemRunCount > ready.runCount ||
        ready.itemVertexCount * sizeof(Th07PspMeRenderStreamVertex) >
            ready.vertexBytes)
    {
        return false;
    }

    const u32 allowedStateFlags =
        TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD |
        TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE;
    u32 totalRecords = 0u;
    u32 totalVertices = 0u;
    u32 previousFirstRecord = 0u;
    u32 previousRecordEnd = 0u;
    for (u32 runIndex = 0u; runIndex < ready.itemRunCount; ++runIndex)
    {
        const Th07PspMeRenderStreamRun &run = ready.runs[runIndex];
        if (totalRecords > ready.itemRecordCount ||
            totalVertices > ready.itemVertexCount ||
            run.recordCount == 0u ||
            run.firstRecord >= ready.itemRecordCount ||
            (runIndex != 0u && run.firstRecord <= previousFirstRecord) ||
            run.recordCount > ready.itemRecordCount - run.firstRecord ||
            run.firstRecord < previousRecordEnd ||
            run.recordCount > ready.itemRecordCount - totalRecords ||
            run.firstVertex != totalVertices ||
            run.vertexCount != run.recordCount * 4u ||
            run.vertexCount > ready.itemVertexCount - totalVertices ||
            run.primitive != TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS ||
            run.sourceFileIndex >= 264u ||
            PspMeRenderRepresentativeSource(
                static_cast<i32>(run.sourceFileIndex)) !=
                run.sourceFileIndex ||
            g_AnmManager->textures[run.sourceFileIndex].id == 0u ||
            (run.logicalState != 0xffffffffu && run.logicalState > 9u) ||
            (run.renderStateFlags & ~allowedStateFlags) != 0u ||
            ((state.job.configFlags &
              TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z) != 0u &&
             (run.renderStateFlags &
              TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE) != 0u))
        {
            return false;
        }
        totalRecords += run.recordCount;
        totalVertices += run.vertexCount;
        previousFirstRecord = run.firstRecord;
        previousRecordEnd = run.firstRecord + run.recordCount;
    }
    return totalVertices == ready.itemVertexCount;
}
#endif

#else
inline float PspMeRenderBitsFloat(u32 bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline float PspMeRenderFloor(float value)
{
    float result;
    __asm__ volatile("floor.w.s %0, %1\n\t"
                     "cvt.s.w %0, %0"
                     : "=&f"(result)
                     : "f"(value));
    return result;
}

bool PspMeRenderBuildLiveRecord(BulletManager *manager, Bullet *bullet,
                                u32 slot,
                                Th07PspMeRenderStreamRecord *record)
{
    if (!manager || !bullet || !record || !g_AnmManager ||
        slot >= static_cast<u32>(BulletManager::kBulletCapacity) ||
        !manager->PspIsBulletSlotTracked(static_cast<i32>(slot)))
    {
        return false;
    }

    AnmVm *vm = PspMeRenderSelectVm(bullet);
    if (!vm)
    {
        return false;
    }
    *record = Th07PspMeRenderStreamRecord{};
    record->posXBits = PspMeRenderFloatBits(
        g_GameManager.arcadeRegionTopLeftPos.x + bullet->pos.x);
    record->posYBits = PspMeRenderFloatBits(
        g_GameManager.arcadeRegionTopLeftPos.y + bullet->pos.y);
    record->posZBits = PspMeRenderFloatBits(0.05f);
    record->slot = slot;
    record->slotGeneration = manager->pspMeRenderSlotGenerations[slot];
    if (record->slotGeneration == 0u)
    {
        return false;
    }

    u32 flags = 0u;
    float rotation = vm->rotation.z;
    float sine = 0.0f;
    float cosine = 1.0f;
    if (vm->autoRotate)
    {
        // Snapshot publication owns this render-only cache fill. A mismatch
        // here means calc/draw identity changed and must take canonical SC.
        if (!bullet->pspRenderRotationValid ||
            bullet->pspRenderSourceAngle != bullet->angle)
        {
            return false;
        }
        rotation = bullet->pspRenderAngle;
        sine = bullet->pspRenderSin;
        cosine = bullet->pspRenderCos;
    }
    else if (rotation != 0.0f)
    {
        PspBulletRenderSinCos(rotation, &sine, &cosine);
    }
    if (rotation != 0.0f)
    {
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED;
    }
    record->sinBits = PspMeRenderFloatBits(sine);
    record->cosBits = PspMeRenderFloatBits(cosine);

    ZunColor baseColor;
    if (vm->useColor2)
    {
        baseColor = vm->color2;
    }
    else
    {
        baseColor.color =
            (vm->color.color & 0xff000000u) | 0x00ffffffu;
    }
    record->color = baseColor.color;
    flags |= (static_cast<u32>(vm->anchor)
              << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT) &
             TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK;
    if (vm->blendMode)
    {
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD;
    }
    if (vm->zWriteDisable)
    {
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE;
    }

    u32 source = 0u;
    if (vm->sprite)
    {
        const i32 originalSource = vm->sprite->sourceFileIndex;
        const bool drawable =
            vm->visible && vm->active && vm->color.bytes.a;
        if (drawable && (originalSource < 0 || originalSource >= 264))
        {
            return false;
        }
        if (originalSource >= 0 && originalSource < 264)
        {
            source = PspMeRenderRepresentativeSource(originalSource);
        }
        record->halfWidthBits = PspMeRenderFloatBits(
            vm->sprite->widthPx * vm->scale.x * 0.5f);
        record->halfHeightBits = PspMeRenderFloatBits(
            vm->sprite->heightPx * vm->scale.y * 0.5f);
        record->u0Bits = PspMeRenderFloatBits(
            vm->sprite->uvStart.x + vm->uvScrollPos.x);
        record->u1Bits = PspMeRenderFloatBits(
            vm->sprite->uvEnd.x + vm->uvScrollPos.x);
        record->v0Bits = PspMeRenderFloatBits(
            vm->sprite->uvStart.y + vm->uvScrollPos.y);
        record->v1Bits = PspMeRenderFloatBits(
            vm->sprite->uvEnd.y + vm->uvScrollPos.y);
        if (drawable)
        {
            flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
        }
    }
    record->sourceAndState =
        (source & 0xffffu) |
        ((static_cast<u32>(bullet->state) & 0xffffu) << 16u);
    record->flags = flags;
    return true;
}

bool PspMeRenderRunMatches(const Th07PspMeRenderStreamRun &run,
                           u32 firstRecord, u32 recordCount,
                           u32 firstVertex, u32 vertexCount, u32 primitive,
                           u32 source, u32 logicalState, u32 stateFlags)
{
    return run.firstRecord == firstRecord &&
           run.recordCount == recordCount &&
           run.firstVertex == firstVertex &&
           run.vertexCount == vertexCount &&
           run.primitive == primitive &&
           run.sourceFileIndex == source &&
           run.logicalState == logicalState &&
           run.renderStateFlags == stateFlags;
}
#endif

bool PspMeRenderValidateReadyStream(BulletManager *manager,
                                    u32 expectedDrawSeq)
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    const Th07PspMeRenderStreamReady &ready = state.ready;
    if (!state.geConsumeActive || !manager || !state.records ||
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
        !PspMeRenderReadyAuthorityMatches(expectedDrawSeq) ||
#endif
        ready.token.slot != state.token.slot ||
        ready.token.generation != state.token.generation ||
        th07_psp_me_render_stream_ready_view_matches(
            &state.token, ready.vertices, ready.vertexBytes, ready.runs,
            ready.runCount) != 1 ||
        state.recordCount > TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS ||
        ready.runCount >
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_RUNS ||
        ready.vertexBytes >
            TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_VERTEX_BYTES ||
        ready.itemRecordCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS ||
        ready.itemVertexCount * sizeof(Th07PspMeRenderStreamVertex) >
            ready.vertexBytes ||
        ready.itemRunCount > ready.runCount ||
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        ready.effectLayer0RecordCount >
            TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS ||
        ready.effectLayer3RecordCount >
            TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS ||
        ready.effectLayer0VertexCount >
            ready.vertexBytes / sizeof(Th07PspMeRenderStreamVertex) -
                ready.itemVertexCount ||
        ready.effectLayer3VertexCount >
            ready.vertexBytes / sizeof(Th07PspMeRenderStreamVertex) -
                ready.itemVertexCount - ready.effectLayer0VertexCount ||
        ready.effectLayer0RunCount >
            ready.runCount - ready.itemRunCount ||
        ready.effectLayer3RunCount >
            ready.runCount - ready.itemRunCount -
                ready.effectLayer0RunCount ||
#endif
#else
            TH07_PSP_ME_RENDER_STREAM_MAX_RUNS ||
        ready.vertexBytes > TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES ||
#endif
        (ready.vertexBytes % sizeof(Th07PspMeRenderStreamVertex)) != 0u ||
        (ready.vertexBytes != 0u &&
         (!ready.vertices ||
          (reinterpret_cast<uintptr_t>(ready.vertices) & 63u) != 0u)) ||
        (ready.runCount != 0u &&
         (!ready.runs ||
          (reinterpret_cast<uintptr_t>(ready.runs) & 63u) != 0u)))
    {
        return false;
    }

#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    // PERFORMANCE trusts only the exact low-level READY pool/token/completion
    // plus current frame/epoch/global authority above, then validates every
    // descriptor before Begin.  It intentionally omits the diagnostic build's
    // O(bullets) snapshot memcmp and duplicate cull/run reconstruction.
    return PspMeRenderReadyRunsValid();
#else

    // Phase one: prove that the complete live linked-list identity and every
    // VM-derived input still equal the immutable snapshot. No observable VM
    // field is mutated until this and the run proof below both succeed.
    u32 seen[(BulletManager::kBulletCapacity + 31u) / 32u] = {};
    const uintptr_t bulletBase =
        reinterpret_cast<uintptr_t>(&manager->bullets[0]);
    const uintptr_t bulletEnd = reinterpret_cast<uintptr_t>(
        &manager->bullets[BulletManager::kBulletCapacity]);
    u32 recordIndex = 0u;
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        Bullet *bullet = manager->bulletsPtrs[bucket];
        while (bullet)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(bullet);
            if (recordIndex >= state.recordCount || address < bulletBase ||
                address >= bulletEnd ||
                ((address - bulletBase) % sizeof(Bullet)) != 0u)
            {
                return false;
            }
            const u32 slot = static_cast<u32>(
                (address - bulletBase) / sizeof(Bullet));
            const u32 bit = 1u << (slot & 31u);
            if ((seen[slot >> 5u] & bit) != 0u)
            {
                return false;
            }
            seen[slot >> 5u] |= bit;
            Th07PspMeRenderStreamRecord live{};
            if (!PspMeRenderBuildLiveRecord(manager, bullet, slot, &live) ||
                std::memcmp(&live, &state.records[recordIndex],
                            sizeof(live)) != 0)
            {
                return false;
            }
            ++recordIndex;
            bullet = bullet->next;
        }
        if (state.job.bucketEnds[bucket] != recordIndex)
        {
            return false;
        }
    }
    if (recordIndex != state.recordCount)
    {
        return false;
    }

    // Phase two: reconstruct the cheap run authority (visibility/cull/state
    // and callback-wide UQ latch), not the vertices themselves. This catches
    // a corrupt descriptor before any GE command makes full-frame fallback
    // impossible; the low-level retire hash already protects vertex bytes.
    const float offsetX = PspMeRenderBitsFloat(state.job.offsetXBits);
    const float offsetY = PspMeRenderBitsFloat(state.job.offsetYBits);
    const float viewportLeft =
        PspMeRenderBitsFloat(state.job.viewportLeftBits);
    const float viewportTop =
        PspMeRenderBitsFloat(state.job.viewportTopBits);
    const float viewportRight =
        PspMeRenderBitsFloat(state.job.viewportRightBits);
    const float viewportBottom =
        PspMeRenderBitsFloat(state.job.viewportBottomBits);
    bool generalMode = false;
    bool haveRun = false;
    u32 runIndex = 0u;
    u32 vertexCount = 0u;
    u32 firstRecord = 0u;
    u32 runRecords = 0u;
    u32 firstVertex = 0u;
    u32 runVertices = 0u;
    u32 primitive = 0u;
    u32 source = 0u;
    u32 logicalState = 0u;
    u32 stateFlags = 0u;
    const auto finishRun = [&]() -> bool {
        if (!haveRun || runIndex >= ready.runCount ||
            !PspMeRenderRunMatches(ready.runs[runIndex], firstRecord,
                                   runRecords, firstVertex, runVertices,
                                   primitive, source, logicalState,
                                   stateFlags))
        {
            return false;
        }
        ++runIndex;
        return true;
    };

    for (u32 index = 0u; index < state.recordCount; ++index)
    {
        const Th07PspMeRenderStreamRecord &record = state.records[index];
        if ((record.flags & TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE) == 0u)
        {
            continue;
        }
        const float posX = PspMeRenderBitsFloat(record.posXBits);
        const float posY = PspMeRenderBitsFloat(record.posYBits);
        const float halfWidth = PspMeRenderBitsFloat(record.halfWidthBits);
        const float halfHeight = PspMeRenderBitsFloat(record.halfHeightBits);
        const u32 anchor =
            (record.flags & TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK) >>
            TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT;
        const float centerX =
            posX + offsetX + ((anchor & 1u) ? halfWidth : 0.0f);
        const float centerY =
            posY + offsetY + ((anchor & 2u) ? halfHeight : 0.0f);
        const float bound = fabsf(halfWidth) + fabsf(halfHeight);
        if (centerX + bound < viewportLeft ||
            centerY + bound < viewportTop ||
            centerX - bound > viewportRight ||
            centerY - bound > viewportBottom)
        {
            continue;
        }

        const bool rotated =
            (record.flags & TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED) != 0u;
        bool usePairs = false;
        if (!rotated)
        {
            const float rawLeft =
                (anchor & 1u) ? posX : posX - halfWidth;
            const float rawRight = (anchor & 1u)
                ? posX + halfWidth * 2.0f : posX + halfWidth;
            const float rawTop =
                (anchor & 2u) ? posY : posY - halfHeight;
            const float rawBottom = (anchor & 2u)
                ? posY + halfHeight * 2.0f : posY + halfHeight;
            const float left = PspMeRenderFloor(rawLeft + offsetX + 0.5f);
            const float right = PspMeRenderFloor(rawRight + offsetX + 0.5f);
            const float top = PspMeRenderFloor(rawTop + offsetY + 0.5f);
            const float bottom =
                PspMeRenderFloor(rawBottom + offsetY + 0.5f);
            usePairs = left <= right && top <= bottom;
        }
        if (!usePairs)
        {
            generalMode = true;
        }
        const u32 expectedPrimitive = generalMode
            ? TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS
            : TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES;
        const u32 verticesThisRecord = generalMode ? 4u : 2u;
        const u32 expectedSource = record.sourceAndState & 0xffffu;
        if (expectedSource >= 264u ||
            PspMeRenderRepresentativeSource(
                static_cast<i32>(expectedSource)) != expectedSource ||
            g_AnmManager->textures[expectedSource].id == 0u)
        {
            return false;
        }
        u32 expectedStateFlags = 0u;
        if (record.flags & TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD)
        {
            expectedStateFlags |=
                TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD;
        }
        if ((state.job.configFlags &
             TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z) == 0u &&
            (record.flags &
             TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE) != 0u)
        {
            expectedStateFlags |=
                TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE;
        }
        const u32 expectedLogicalState = record.sourceAndState >> 16u;
        const bool forceBreak =
            (record.flags & TH07_PSP_ME_RENDER_STREAM_RECORD_RUN_BREAK) != 0u;
        const bool newRun = !haveRun || forceBreak ||
                            source != expectedSource ||
                            stateFlags != expectedStateFlags ||
                            primitive != expectedPrimitive;
        if (newRun)
        {
            if (haveRun && !finishRun())
            {
                return false;
            }
            haveRun = true;
            firstRecord = index;
            runRecords = 0u;
            firstVertex = vertexCount;
            runVertices = 0u;
            primitive = expectedPrimitive;
            source = expectedSource;
            logicalState = expectedLogicalState;
            stateFlags = expectedStateFlags;
        }
        else if (logicalState != expectedLogicalState)
        {
            logicalState = 0xffffffffu;
        }
        ++runRecords;
        runVertices += verticesThisRecord;
        vertexCount += verticesThisRecord;
    }
    if ((haveRun && !finishRun()) || runIndex != ready.runCount ||
        vertexCount * sizeof(Th07PspMeRenderStreamVertex) !=
            ready.vertexBytes)
    {
        return false;
    }
    return true;
#endif
}

void PspMeRenderCommitVmSideEffects(BulletManager *manager)
{
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        for (Bullet *bullet = manager->bulletsPtrs[bucket]; bullet;
             bullet = bullet->next)
        {
            AnmVm *vm = PspMeRenderSelectVm(bullet);
            vm->pos.x =
                g_GameManager.arcadeRegionTopLeftPos.x + bullet->pos.x;
            vm->pos.y =
                g_GameManager.arcadeRegionTopLeftPos.y + bullet->pos.y;
            vm->pos.z = 0.05f;
            vm->color.color =
                (vm->color.color & 0xff000000u) | 0x00ffffffu;
            if (vm->autoRotate)
            {
                vm->SetRotationZ(bullet->pspRenderAngle);
                vm->updateRotation = 1;
            }
        }
    }
}

void PspMeRenderSubmitRunRange(u32 firstRun, u32 endRun,
                               bool bulletSegment)
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (bulletSegment)
    {
        // Item is an independent forced-quad layer. Reset UQ's sticky latch at
        // the canonical Item/Bullet boundary so an axis Bullet may still use
        // the accepted GU_SPRITES primitive.
        g_AnmManager->pspUnifiedBulletGeneralMode = 0;
    }
    for (u32 runIndex = firstRun; runIndex < endRun; ++runIndex)
    {
        const Th07PspMeRenderStreamRun &run = state.ready.runs[runIndex];
        const GfxTextureHandle texture =
            g_AnmManager->textures[run.sourceFileIndex];
        if (g_AnmManager->currentTexture != texture)
        {
            g_AnmManager->Flush();
            g_AnmManager->currentTexture = texture;
            g_Supervisor.gfxDevice->BindTexture(texture);
        }
        if (g_AnmManager->currentVertexShader != 1)
        {
            g_AnmManager->Flush();
            g_AnmManager->currentVertexShader = 1;
        }
        const u32 blend =
            (run.renderStateFlags &
             TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD) != 0u;
        if (static_cast<u32>(g_AnmManager->currentBlendMode) != blend)
        {
            g_AnmManager->Flush();
            g_AnmManager->currentBlendMode = static_cast<u8>(blend);
            g_Supervisor.gfxDevice->SetBlendMode(
                BLEND_ALPHA, blend ? BLEND_ONE : BLEND_ALPHA);
        }
        const u32 zWriteDisable =
            (run.renderStateFlags &
             TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE) != 0u;
        if (!g_Supervisor.cfg.disableZBuffer &&
            static_cast<u32>(g_AnmManager->currentZWriteDisable) !=
                zWriteDisable)
        {
            g_AnmManager->Flush();
            g_AnmManager->currentZWriteDisable =
                static_cast<u8>(zWriteDisable);
            g_Supervisor.gfxDevice->SetDepthMask(!zWriteDisable);
        }
        g_AnmManager->renderStateChangesThisFrame += run.recordCount;

        const u8 pairs = run.primitive ==
            TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES;
        if (g_AnmManager->pspSpriteBatchUsesPairs != pairs)
        {
            g_AnmManager->Flush();
            g_AnmManager->pspSpriteBatchUsesPairs = pairs;
        }
        if (!pairs)
        {
            g_AnmManager->pspUnifiedBulletGeneralMode = 1;
        }
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
        const auto *vertices = state.ready.vertices + run.firstVertex;
#else
        const auto *vertices =
            reinterpret_cast<const Th07PspSpriteVertex *>(
                state.ready.vertices + run.firstVertex);
#endif
        Th07PspDrawMeRenderStreamRun(vertices, run.vertexCount,
                                     run.primitive);
    }
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
void PspMeRenderReleaseReadyForFallback(bool identityFault);
bool PspMeRenderReusePrefixGeValidation(BulletManager *manager,
                                        u32 expectedDrawSeq);

bool PspMeRenderTryGeConsumeItem(BulletManager *manager)
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    const u32 expectedDrawSeq = gPspMeRenderShadow.drawSeq + 1u;
    const bool streamValidated = state.prefixGeSubmissionOpen
        ? PspMeRenderReusePrefixGeValidation(manager, expectedDrawSeq)
        : PspMeRenderValidateReadyStream(manager, expectedDrawSeq);
    if (!state.geConsumeActive ||
        !streamValidated ||
        !PspMeRenderReadyItemRunsValid())
    {
        if (state.geConsumeActive)
        {
            ++gPspMeRenderShadowWindow.streamItemFallback;
            ++gPspMeRenderShadowWindow.streamItemReject;
        }
        // Segment-local rejection: retain READY and let the existing Bullet
        // deadline/consumer use the independently valid Bullet suffix.
        return false;
    }

    if (state.ready.itemRunCount != 0u)
    {
        g_AnmManager->Flush();
        if (!state.prefixGeSubmissionOpen &&
            !Th07PspBeginMeRenderGeSubmission(
                state.token.slot, state.token.generation))
        {
            ++gPspMeRenderShadowWindow.beginFail;
            ++gPspMeRenderShadowWindow.streamItemFallback;
            // Begin failed before any Item command became visible. Keep the
            // independently valid READY stream so canonical Item drawing can
            // overlap it and the existing Bullet consumer can retry ownership
            // at its original deadline.
            return false;
        }
        if (!state.prefixGeSubmissionOpen)
        {
            state.prefixGeSubmissionOpen = true;
            state.prefixValidatedTokenSlot = state.token.slot;
            state.prefixValidatedTokenGeneration = state.token.generation;
            state.prefixValidatedDrawSeq = expectedDrawSeq;
        }
        g_Supervisor.gfxDevice->SetTextureArg(TEX_ARG_DIFFUSE);
        g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA,
                                           COLOR_OP_MODULATE);
        g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB,
                                           COLOR_OP_MODULATE);
        PspMeRenderSubmitRunRange(0u, state.ready.itemRunCount, false);
    }
    ++gPspMeRenderShadowWindow.streamItemAccepted;
    gPspMeRenderShadowWindow.streamItemRuns += state.ready.itemRunCount;
    gPspMeRenderShadowWindow.streamItemVertices +=
        state.ready.itemVertexCount;
    gPspMeRenderShadowWindow.streamItemSuffixRecords +=
        state.itemTotalCount - state.itemRecordCount;
    // The direct-list prefix is already ordered in the current GU list.
    // Append the authenticated canonical suffix before returning to Bullet;
    // no Item can be duplicated or omitted at the split boundary.
    g_ItemManager.PspDrawCanonicalItemSuffix(state.itemSuffixHead);
    return true;
}

bool PspMeRenderReusePrefixGeValidation(BulletManager *manager,
                                        u32 expectedDrawSeq)
{
    const PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (!state.prefixGeSubmissionOpen || !state.geConsumeActive || !manager ||
        !state.records ||
        state.prefixValidatedTokenSlot != state.token.slot ||
        state.prefixValidatedTokenGeneration != state.token.generation ||
        state.prefixValidatedDrawSeq != expectedDrawSeq ||
        state.ready.token.slot != state.token.slot ||
        state.ready.token.generation != state.token.generation ||
        state.job.targetDrawSeq != expectedDrawSeq)
    {
        return false;
    }
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    // Effect or Item validation ran while the token was READY_SC, immediately
    // before Begin promoted this exact token to GE_IN_FLIGHT.  Priority-9/10
    // consumers preserve one command span and only append validated prefix
    // ranges. Recheck mutable game/frame authority, but do not ask
    // ready_view_matches for a state the token intentionally no longer owns.
    return PspMeRenderReadyAuthorityMatches(expectedDrawSeq);
#else
    return true;
#endif
}
#endif

void PspMeRenderReleaseReadyForFallback(bool identityFault)
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (identityFault)
    {
        ++gPspMeRenderShadowWindow.streamIdentityDrop;
    }
    ++gPspMeRenderShadowWindow.fallbackFrames;
    if (th07_psp_me_render_stream_release_ready(&state.token) != 1)
    {
        ++gPspMeRenderShadowWindow.streamReleaseFault;
        ++gPspMeRenderShadowWindow.protocolFault;
        state.hardFaulted = true;
        __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                         __ATOMIC_RELEASE);
    }
    state.geConsumeActive = false;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    state.prefixGeSubmissionOpen = false;
#endif
    state.records = nullptr;
    state.ready = Th07PspMeRenderStreamReady{};
}

bool PspMeRenderTryGeConsume(BulletManager *manager, u32 *recordCount)
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (recordCount)
    {
        *recordCount = 0u;
    }
    if (!state.geConsumeActive)
    {
        return false;
    }
    const bool streamValidated =
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        state.prefixGeSubmissionOpen
            ? PspMeRenderReusePrefixGeValidation(
                  manager, gPspMeRenderShadow.drawSeq)
            : PspMeRenderValidateReadyStream(
                  manager, gPspMeRenderShadow.drawSeq);
#else
        PspMeRenderValidateReadyStream(
            manager, gPspMeRenderShadow.drawSeq);
#endif
    if (!streamValidated)
    {
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        if (state.prefixGeSubmissionOpen)
        {
            // Effect/Item prefix commands are already authoritative and
            // cannot be undone. Close their GE ownership, then draw only the
            // rejected Bullet layer canonically; never release a GE-owned
            // slot as READY_SC.
            Th07PspEndMeRenderGeSubmission();
            state.prefixGeSubmissionOpen = false;
            ++gPspMeRenderShadowWindow.fallbackFrames;
            state.geConsumeActive = false;
            state.records = nullptr;
            state.ready = Th07PspMeRenderStreamReady{};
            return false;
        }
#endif
        PspMeRenderReleaseReadyForFallback(true);
        return false;
    }

    const u32 records = state.recordCount;
    const u32 firstBulletRun =
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        state.ready.itemRunCount
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        + state.ready.effectLayer0RunCount +
            state.ready.effectLayer3RunCount
#endif
        ;
#else
        0u;
#endif
    const u32 bulletRunCount = state.ready.runCount - firstBulletRun;
    if (bulletRunCount == 0u)
    {
#if !defined(TH07_PSP_ME_RENDER_PERFORMANCE)
        PspMeRenderCommitVmSideEffects(manager);
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        if (state.prefixGeSubmissionOpen)
        {
            Th07PspEndMeRenderGeSubmission();
            state.prefixGeSubmissionOpen = false;
        }
        else
#endif
        if (th07_psp_me_render_stream_release_ready(&state.token) != 1)
        {
            ++gPspMeRenderShadowWindow.streamReleaseFault;
            ++gPspMeRenderShadowWindow.protocolFault;
            state.hardFaulted = true;
            __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                             __ATOMIC_RELEASE);
        }
    }
    else
    {
        // Preserve priority-10 ordering: lasers/items and any backend-deferred
        // pair command precede the first ME bullet command.
        g_AnmManager->Flush();
        if (
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            !state.prefixGeSubmissionOpen &&
#endif
            !Th07PspBeginMeRenderGeSubmission(
                state.token.slot, state.token.generation))
        {
            ++gPspMeRenderShadowWindow.beginFail;
            PspMeRenderReleaseReadyForFallback(false);
            return false;
        }

#if !defined(TH07_PSP_ME_RENDER_PERFORMANCE)
        PspMeRenderCommitVmSideEffects(manager);
#endif
        // AnmManager::Flush normally establishes these before a sprite draw,
        // but it is a no-op when the ME stream is the first visible batch.
        g_Supervisor.gfxDevice->SetTextureArg(TEX_ARG_DIFFUSE);
        g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA,
                                           COLOR_OP_MODULATE);
        g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB,
                                           COLOR_OP_MODULATE);

        for (u32 runIndex = firstBulletRun;
             runIndex < state.ready.runCount; ++runIndex)
        {
            const Th07PspMeRenderStreamRun &run = state.ready.runs[runIndex];
            const GfxTextureHandle texture =
                g_AnmManager->textures[run.sourceFileIndex];
            if (g_AnmManager->currentTexture != texture)
            {
                g_AnmManager->Flush();
                g_AnmManager->currentTexture = texture;
                g_Supervisor.gfxDevice->BindTexture(texture);
            }
            if (g_AnmManager->currentVertexShader != 1)
            {
                g_AnmManager->Flush();
                g_AnmManager->currentVertexShader = 1;
            }
            const u32 blend =
                (run.renderStateFlags &
                 TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD) != 0u;
            if (static_cast<u32>(g_AnmManager->currentBlendMode) != blend)
            {
                g_AnmManager->Flush();
                g_AnmManager->currentBlendMode = static_cast<u8>(blend);
                g_Supervisor.gfxDevice->SetBlendMode(
                    BLEND_ALPHA, blend ? BLEND_ONE : BLEND_ALPHA);
            }
            const u32 zWriteDisable =
                (run.renderStateFlags &
                 TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE) != 0u;
            if (!g_Supervisor.cfg.disableZBuffer &&
                static_cast<u32>(g_AnmManager->currentZWriteDisable) !=
                    zWriteDisable)
            {
                g_AnmManager->Flush();
                g_AnmManager->currentZWriteDisable =
                    static_cast<u8>(zWriteDisable);
                g_Supervisor.gfxDevice->SetDepthMask(!zWriteDisable);
            }
            g_AnmManager->renderStateChangesThisFrame += run.recordCount;

            const u8 pairs = run.primitive ==
                TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES;
            if (g_AnmManager->pspSpriteBatchUsesPairs != pairs)
            {
                g_AnmManager->Flush();
                g_AnmManager->pspSpriteBatchUsesPairs = pairs;
            }
            if (!pairs)
            {
                g_AnmManager->pspUnifiedBulletGeneralMode = 1;
            }
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
            const auto *vertices = state.ready.vertices + run.firstVertex;
#else
            const auto *vertices = reinterpret_cast<const Th07PspSpriteVertex *>(
                state.ready.vertices + run.firstVertex);
#endif
            Th07PspDrawMeRenderStreamRun(vertices, run.vertexCount,
                                         run.primitive);
        }
        // The direct commands already consumed the UQ stream; no pending
        // AnmManager arena range remains for the next owner to force to quads.
        g_AnmManager->pspForceSpriteQuads = 0;
        Th07PspEndMeRenderGeSubmission();
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        state.prefixGeSubmissionOpen = false;
#endif
        ++gPspMeRenderShadowWindow.streamGeFrames;
        gPspMeRenderShadowWindow.streamGeRuns += bulletRunCount;
        gPspMeRenderShadowWindow.streamGeVertices +=
            state.ready.vertexBytes /
                sizeof(Th07PspMeRenderStreamVertex)
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            - state.ready.itemVertexCount
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            - state.ready.effectLayer0VertexCount
            - state.ready.effectLayer3VertexCount
#endif
#endif
            ;
        bool sawPairs = false;
        bool sawQuads = false;
        for (u32 runIndex = firstBulletRun;
             runIndex < state.ready.runCount; ++runIndex)
        {
            sawPairs |= state.ready.runs[runIndex].primitive ==
                TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES;
            sawQuads |= state.ready.runs[runIndex].primitive ==
                TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS;
        }
        if (sawPairs && sawQuads)
        {
            ++gPspMeRenderShadowWindow.streamMixedPrimitiveFrames;
        }
    }

    ++gPspMeRenderShadowWindow.wouldConsume;
    state.geConsumeActive = false;
    state.records = nullptr;
    state.ready = Th07PspMeRenderStreamReady{};
    if (recordCount)
    {
        *recordCount = records;
    }
    return true;
}
#endif

int PspMeRenderCorrectnessRetire(bool atDeadline, u32 expectedDrawSeq)
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (!state.pending)
    {
        return 1;
    }
    Th07PspMeRenderStreamCompletion completion{};
    const int probe = th07_psp_me_render_stream_probe(&state.token, &completion);
    if (probe <= 0)
    {
        return probe;
    }
    Th07PspMeRenderStreamReady ready{};
    if (th07_psp_me_render_stream_retire(&state.token, &completion, &ready) != 1)
    {
        ++gPspMeRenderShadowWindow.protocolFault;
        __atomic_store_n(&gPspMeRenderShadow.available, 0u, __ATOMIC_RELEASE);
        return -1;
    }

    gPspMeRenderShadowWindow.scWritebackUs += completion.scWritebackUs;
    gPspMeRenderShadowWindow.scOutputPrepareUs +=
        completion.scOutputPrepareUs;
    gPspMeRenderShadowWindow.scSubmitUs += completion.scSubmitUs;
    gPspMeRenderShadowWindow.scInvalidateUs += completion.scInvalidateUs;
    gPspMeRenderShadowWindow.dispatchUs += completion.dispatchWaitUs;
    gPspMeRenderShadowWindow.meInvalidateCycles +=
        completion.meInvalidateCycles;
    gPspMeRenderShadowWindow.meKernelCycles += completion.meKernelCycles;
    th07_usage_meter_add_me_cycles(
        completion.meInvalidateCycles + completion.meKernelCycles +
        completion.meWritebackCycles);
    gPspMeRenderShadowWindow.meWritebackCycles +=
        completion.meWritebackCycles;
    ++gPspMeRenderShadowWindow.meRenderCompleted;
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    gPspMeRenderShadowWindow.outputBytes += completion.outputBytes;
    if (atDeadline)
    {
        gPspMeRenderShadowWindow.targetOutputBytes += completion.outputBytes;
    }
#endif
    PspMeRenderRecordKernelCycles(completion.meKernelCycles);

#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    // A live VM/sprite record can cease to satisfy the strict raw ownership
    // proof without indicating ME corruption.  The low-level retire accepts
    // only the narrowly checked RECORD result into READY_SC; recycle that
    // empty view and render this frame canonically.  Protocol, echo, FCR,
    // guard and stack failures still return -1 above and disable the worker.
    if (PspMeRenderRawRecordSoftRejectMatches(completion))
    {
        ++gPspMeRenderShadowWindow.coverageDrop;
        if (th07_psp_me_render_stream_release_ready(&state.token) != 1)
        {
            ++gPspMeRenderShadowWindow.streamReleaseFault;
            ++gPspMeRenderShadowWindow.protocolFault;
            __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                             __ATOMIC_RELEASE);
            state.hardFaulted = true;
            return -1;
        }
        state.pending = false;
        state.deadlineAccounted = false;
        state.geConsumeActive = false;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        state.prefixGeSubmissionOpen = false;
#endif
        state.ready = Th07PspMeRenderStreamReady{};
        return 1;
    }
#endif

    const bool completionValid =
        PspMeRenderCorrectnessCompletionMatches(completion);
    const bool globalsValid = PspMeRenderCorrectnessGlobalsMatch();
    const bool deadlineValid =
        atDeadline && state.job.targetDrawSeq == expectedDrawSeq &&
        state.job.stageEpoch == gPspMeRenderShadow.stageEpoch &&
        state.job.managerEpoch == gPspMeRenderShadow.managerEpoch &&
        state.job.replayEpoch == static_cast<u32>(
            g_ReplayManager ? g_ReplayManager->frameId : 0) &&
        globalsValid;
    if (atDeadline && completionValid && deadlineValid &&
        __atomic_load_n(&gPspMeRenderShadow.available, __ATOMIC_ACQUIRE))
    {
        state.ready = ready;
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        state.geConsumeActive = true;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        state.prefixGeSubmissionOpen = false;
#endif
#else
        state.compareActive = true;
#endif
        ++gPspMeRenderShadowWindow.streamReady;
#if !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        ++gPspMeRenderShadowWindow.wouldConsume;
#endif
    }
    else
    {
        if (atDeadline)
        {
            ++gPspMeRenderShadowWindow.streamHeaderDrop;
            if (!completionValid)
            {
                ++gPspMeRenderShadowWindow.protocolFault;
            }
            else if (!globalsValid)
            {
                ++gPspMeRenderShadowWindow.signatureDrop;
            }
            else
            {
                ++gPspMeRenderShadowWindow.epochDrop;
            }
        }
        else
        {
            ++gPspMeRenderShadowWindow.lateRetired;
        }
        if (th07_psp_me_render_stream_release_ready(&state.token) != 1)
        {
            ++gPspMeRenderShadowWindow.streamReleaseFault;
            ++gPspMeRenderShadowWindow.protocolFault;
            __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                             __ATOMIC_RELEASE);
            state.hardFaulted = true;
            return -1;
        }
    }
    state.pending = false;
    state.deadlineAccounted = false;
    return 1;
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
bool PspMeRenderTryEarlyItemRetire()
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    const u32 expectedDrawSeq = gPspMeRenderShadow.drawSeq + 1u;
    if (!state.pending || state.deadlineAccounted ||
        state.targetDrawSeq != expectedDrawSeq)
    {
        return state.geConsumeActive;
    }

    // Probe exactly once before Item drawing. Never wait here: when ME is
    // still running, canonical Item drawing is useful overlap and the existing
    // Bullet deadline retains its bounded live-owner wait.
    const int retired = PspMeRenderCorrectnessRetire(true, expectedDrawSeq);
    if (retired < 0)
    {
        PspMeRenderRawFailStop(
            "MERW I7 EARLY ITEM OWNER FAULT -> COLD REBOOT");
    }
    return retired > 0 && state.geConsumeActive;
}
#endif

void PspMeRenderCorrectnessDrawDeadline()
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    ++gPspMeRenderShadow.drawSeq;
    if (!state.pending || state.deadlineAccounted ||
        state.targetDrawSeq != gPspMeRenderShadow.drawSeq)
    {
        return;
    }
    ++gPspMeRenderShadowWindow.deadlines;
#if !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    ++gPspMeRenderShadowWindow.fallbackFrames;
#endif
    gPspMeRenderShadowWindow.targetRecords += state.recordCount;
    const u32 nowUs = sceKernelGetSystemTimeLow();
    PspMeRenderRecordSlack(nowUs - state.submitUs);
    int retired = PspMeRenderCorrectnessRetire(
        true, gPspMeRenderShadow.drawSeq);
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    if (retired == 0)
    {
        // I-ME3 could carry an immutable semantic snapshot past its target
        // draw. I-ME4 cannot: the next calc mutates the live VM/sprite bytes
        // ME is reading. Preserve the useful asynchronous fast path, but turn
        // a rare late completion into a bounded deadline wait and still use
        // its output in this frame when it completes normally.
        ++gPspMeRenderShadowWindow.notReady;
        const u32 waitStartUs = sceKernelGetSystemTimeLow();
        do
        {
            sceKernelDelayThread(20u);
            retired = PspMeRenderCorrectnessRetire(
                true, gPspMeRenderShadow.drawSeq);
        } while (retired == 0 &&
                 sceKernelGetSystemTimeLow() - waitStartUs <
                     kPspMeRenderRawDeadlineTimeoutUs);

        if (retired == 0)
        {
            th07_psp_me_render_stream_hard_fault(&state.token);
            ++gPspMeRenderShadowWindow.timeouts;
            ++gPspMeRenderShadowWindow.protocolFault;
            PspMeRenderRawFailStop(
                "MERW I4 DEADLINE HANG -> COLD REBOOT");
        }
    }
    if (retired < 0)
    {
        // A negative probe/retire result does not distinguish a completed
        // corrupt mailbox from a still-running poisoned ME. Never let the
        // following calc reuse the live owner in either case.
        PspMeRenderRawFailStop(
            "MERW I4 DEADLINE OWNER FAULT -> COLD REBOOT");
    }
#endif
    if (retired == 0)
    {
#if !defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        ++gPspMeRenderShadowWindow.notReady;
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        ++gPspMeRenderShadowWindow.fallbackFrames;
#endif
        state.deadlineAccounted = true;
    }
    else if (retired < 0)
    {
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        ++gPspMeRenderShadowWindow.fallbackFrames;
#endif
        state.deadlineAccounted = true;
        state.hardFaulted = true;
    }
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    else if (!state.geConsumeActive)
    {
        // Retire completed but rejected stale header/global identity and
        // released READY internally. The visible path remains canonical.
        ++gPspMeRenderShadowWindow.fallbackFrames;
    }
#endif
}

void PspMeRenderCorrectnessBeginCapture()
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (!state.compareActive)
    {
        return;
    }
    state.compareRecordIndex = 0u;
    state.compareVertexCount = 0u;
    state.compareRunCount = 0u;
    state.compareIdentityFault = false;
    state.identityDetailSet = false;
    state.identityWord = 0u;
    state.identityExpected = 0u;
    state.identityActual = 0u;
    state.sawPairs = false;
    state.sawQuads = false;
    state.canonicalStart = g_AnmManager->vertexBufferCurPtr;
}

void PspMeRenderCorrectnessIdentityFault(u32 recordIndex, u32 field,
                                         u32 expected, u32 actual)
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    state.compareIdentityFault = true;
    if (!state.identityDetailSet)
    {
        state.identityDetailSet = true;
        // Sixteen diagnostic words are reserved per record.  `field` names
        // slot/generation/state/bucket/count/source/pointer failures without
        // changing the public mismatch ABI.
        state.identityWord = recordIndex * 16u + field;
        state.identityExpected = expected;
        state.identityActual = actual;
    }
}

void PspMeRenderCorrectnessNoteRecord(Bullet *bullet, u32 bucket,
                                      Th07PspSpriteVertex *before,
                                      Th07PspSpriteVertex *after)
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (!state.compareActive)
    {
        return;
    }
    const u32 recordIndex = state.compareRecordIndex++;
    if (!bullet)
    {
        PspMeRenderCorrectnessIdentityFault(recordIndex, 1u, 1u, 0u);
        return;
    }
    if (recordIndex >= state.recordCount)
    {
        PspMeRenderCorrectnessIdentityFault(
            recordIndex, 2u, state.recordCount, recordIndex + 1u);
        return;
    }
    if (!state.records)
    {
        PspMeRenderCorrectnessIdentityFault(recordIndex, 3u, 1u, 0u);
        return;
    }
    if (after < before)
    {
        PspMeRenderCorrectnessIdentityFault(recordIndex, 4u, 0u,
                                            0xffffffffu);
        return;
    }
    const Th07PspMeRenderStreamRecord &record = state.records[recordIndex];
    const uintptr_t bulletBase =
        reinterpret_cast<uintptr_t>(&g_BulletManager.bullets[0]);
    const uintptr_t address = reinterpret_cast<uintptr_t>(bullet);
    const uintptr_t bulletEnd = reinterpret_cast<uintptr_t>(
        &g_BulletManager.bullets[BulletManager::kBulletCapacity]);
    if (address < bulletBase || address >= bulletEnd ||
        ((address - bulletBase) % sizeof(Bullet)) != 0u)
    {
        PspMeRenderCorrectnessIdentityFault(recordIndex, 5u, record.slot,
                                            0xffffffffu);
        return;
    }
    const u32 slot = static_cast<u32>((address - bulletBase) / sizeof(Bullet));
    u32 expectedBucket = 0u;
    while (expectedBucket < 5u &&
           recordIndex >= state.job.bucketEnds[expectedBucket])
    {
        ++expectedBucket;
    }
    if (slot != record.slot)
    {
        PspMeRenderCorrectnessIdentityFault(recordIndex, 6u, record.slot,
                                            slot);
    }
    const u32 generation = g_BulletManager.pspMeRenderSlotGenerations[slot];
    if (record.slotGeneration != generation)
    {
        PspMeRenderCorrectnessIdentityFault(
            recordIndex, 7u, record.slotGeneration, generation);
    }
    const u32 bulletState = static_cast<u32>(bullet->state) & 0xffffu;
    if ((record.sourceAndState >> 16u) != bulletState)
    {
        PspMeRenderCorrectnessIdentityFault(
            recordIndex, 8u, record.sourceAndState >> 16u, bulletState);
    }
    if (bucket != expectedBucket)
    {
        PspMeRenderCorrectnessIdentityFault(recordIndex, 9u, expectedBucket,
                                            bucket);
    }

    const ptrdiff_t added = after - before;
    if (added != 0 && added != 2 && added != 4)
    {
        PspMeRenderCorrectnessIdentityFault(
            recordIndex, 10u, 0x00040002u, static_cast<u32>(added));
        return;
    }
    if (added == 0)
    {
        return;
    }

    AnmVm *vm = PspMeRenderSelectVm(bullet);
    if (!vm || !vm->sprite)
    {
        PspMeRenderCorrectnessIdentityFault(recordIndex, 11u, 1u, 0u);
        return;
    }
    const u32 source = PspMeRenderRepresentativeSource(
        vm->sprite->sourceFileIndex);
    if ((record.sourceAndState & 0xffffu) != source)
    {
        PspMeRenderCorrectnessIdentityFault(
            recordIndex, 12u, record.sourceAndState & 0xffffu, source);
    }
    const u32 primitive = added == 2
        ? TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES
        : TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS;
    state.sawPairs |= added == 2;
    state.sawQuads |= added == 4;
    u32 renderStateFlags = 0u;
    if (g_AnmManager->currentBlendMode)
    {
        renderStateFlags |= TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD;
    }
    if (!g_Supervisor.cfg.disableZBuffer &&
        g_AnmManager->currentZWriteDisable)
    {
        renderStateFlags |=
            TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE;
    }

    Th07PspMeRenderStreamRun *run = state.compareRunCount
        ? &gPspMeRenderCanonicalRuns[state.compareRunCount - 1u]
        : nullptr;
    if (!run || run->sourceFileIndex != source ||
        run->renderStateFlags != renderStateFlags ||
        run->primitive != primitive)
    {
        if (state.compareRunCount >= TH07_PSP_ME_RENDER_STREAM_MAX_RUNS)
        {
            PspMeRenderCorrectnessIdentityFault(
                recordIndex, 13u, TH07_PSP_ME_RENDER_STREAM_MAX_RUNS,
                state.compareRunCount + 1u);
            return;
        }
        run = &gPspMeRenderCanonicalRuns[state.compareRunCount++];
        *run = Th07PspMeRenderStreamRun{};
        run->firstRecord = recordIndex;
        run->firstVertex = state.compareVertexCount;
        run->primitive = primitive;
        run->sourceFileIndex = source;
        run->logicalState = static_cast<u32>(bullet->state);
        run->renderStateFlags = renderStateFlags;
    }
    else if (run->logicalState != static_cast<u32>(bullet->state))
    {
        run->logicalState = 0xffffffffu;
    }
    ++run->recordCount;
    run->vertexCount += static_cast<u32>(added);
    state.compareVertexCount += static_cast<u32>(added);
}

void PspMeRenderCorrectnessEndCapture()
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (!state.compareActive)
    {
        return;
    }
    Th07PspMeRenderStreamMismatch mismatch{};
    const u32 vertexBytes = state.compareVertexCount *
                            sizeof(Th07PspMeRenderStreamVertex);
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    const u32 packedCapacity = static_cast<u32>(
        sizeof(gPspMeRenderCanonicalPacked) /
        sizeof(gPspMeRenderCanonicalPacked[0]));
    if (state.compareVertexCount > packedCapacity)
    {
        PspMeRenderCorrectnessIdentityFault(
            state.compareRecordIndex, 13u, packedCapacity,
            state.compareVertexCount);
    }
    else
    {
        for (u32 vertex = 0u; vertex < state.compareVertexCount; ++vertex)
        {
            if (!PspMeRenderPackReferenceVertex(
                    state.canonicalStart[vertex],
                    &gPspMeRenderCanonicalPacked[vertex]))
            {
                PspMeRenderCorrectnessIdentityFault(
                    state.compareRecordIndex, 13u, vertex,
                    0xffffffffu);
                break;
            }
        }
    }
    const Th07PspMeRenderStreamVertex *compareVertices =
        gPspMeRenderCanonicalPacked;
#else
    const auto *compareVertices =
        reinterpret_cast<const Th07PspMeRenderStreamVertex *>(
            state.canonicalStart);
#endif
    if (state.compareRecordIndex != state.recordCount)
    {
        PspMeRenderCorrectnessIdentityFault(
            state.compareRecordIndex, 15u, state.recordCount,
            state.compareRecordIndex);
    }
    const ptrdiff_t arenaVertices =
        g_AnmManager->vertexBufferCurPtr - state.canonicalStart;
    if (arenaVertices != static_cast<ptrdiff_t>(state.compareVertexCount))
    {
        PspMeRenderCorrectnessIdentityFault(
            state.compareRecordIndex, 14u, state.compareVertexCount,
            static_cast<u32>(arenaVertices));
    }
    int matched = 0;
    if (!state.compareIdentityFault)
    {
        matched = th07_psp_me_render_stream_compare(
            &state.token, compareVertices, vertexBytes,
            gPspMeRenderCanonicalRuns, state.compareRunCount, &mismatch);
    }
    else
    {
        ++gPspMeRenderShadowWindow.streamIdentityDrop;
        mismatch.kind = TH07_PSP_ME_RENDER_STREAM_MISMATCH_SIZE;
        mismatch.wordIndex = state.identityWord;
        mismatch.expected = state.identityExpected;
        mismatch.actual = state.identityActual;
    }

    if (matched == 1)
    {
        ++gPspMeRenderShadowWindow.streamCompared;
        if (state.sawPairs && state.sawQuads)
        {
            ++gPspMeRenderShadowWindow.streamMixedPrimitiveFrames;
        }
    }
    else
    {
        ++gPspMeRenderShadowWindow.streamMismatch;
        switch (mismatch.kind)
        {
        case TH07_PSP_ME_RENDER_STREAM_MISMATCH_SIZE:
            ++gPspMeRenderShadowWindow.streamSizeMismatch;
            break;
        case TH07_PSP_ME_RENDER_STREAM_MISMATCH_VERTEX:
            ++gPspMeRenderShadowWindow.streamVertexMismatch;
            break;
        case TH07_PSP_ME_RENDER_STREAM_MISMATCH_RUN:
            ++gPspMeRenderShadowWindow.streamRunMismatch;
            break;
        default:
            ++gPspMeRenderShadowWindow.streamHashMismatch;
            break;
        }
        if (gPspMeRenderShadowWindow.streamFirstMismatchKind == 0u)
        {
            gPspMeRenderShadowWindow.streamFirstMismatchKind = mismatch.kind;
            gPspMeRenderShadowWindow.streamFirstMismatchWord =
                mismatch.wordIndex;
            gPspMeRenderShadowWindow.streamFirstMismatchExpected =
                mismatch.expected;
            gPspMeRenderShadowWindow.streamFirstMismatchActual = mismatch.actual;
            th07_psp_boot_notef(
                "MERW I1 FIRST MISMATCH K%u W%u E%08x A%08x SC_DRAW=1",
                mismatch.kind, mismatch.wordIndex, mismatch.expected,
                mismatch.actual);
        }
    }
    if (th07_psp_me_render_stream_release_ready(&state.token) != 1)
    {
        ++gPspMeRenderShadowWindow.streamReleaseFault;
        ++gPspMeRenderShadowWindow.protocolFault;
        __atomic_store_n(&gPspMeRenderShadow.available, 0u, __ATOMIC_RELEASE);
        state.hardFaulted = true;
    }
    state.compareActive = false;
    state.records = nullptr;
    state.ready = Th07PspMeRenderStreamReady{};
}

void PspMeRenderCorrectnessAfterCalc(u32 serialBefore, bool nextDraw)
{
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    // Priority 18 already gave the compact command its one nonblocking final
    // probe. GameWindow calls this entry again after the chain; do not let the
    // second entry race a still-live command through the shared ME owner.
    if (gPspMeBulletCompactSc.blockedRenderCalcSerial ==
        gPspMeRenderShadow.calcCompleteSerial)
    {
        return;
    }
#endif
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (state.pending && state.deadlineAccounted)
    {
        if (state.hardFaulted)
        {
            return;
        }
        int retired = PspMeRenderCorrectnessRetire(
            false, gPspMeRenderShadow.drawSeq);
        if (retired < 0)
        {
            state.hardFaulted = true;
            return;
        }
        if (retired == 0)
        {
            if (sceKernelGetSystemTimeLow() - state.submitUs < 100000u)
            {
                return;
            }
            // As with the corrected M0 transition path, observe DONE once more
            // at the watchdog boundary before declaring a real hang.
            retired = PspMeRenderCorrectnessRetire(
                false, gPspMeRenderShadow.drawSeq);
            if (retired < 0)
            {
                state.hardFaulted = true;
                return;
            }
            if (retired == 0)
            {
                th07_psp_me_render_stream_hard_fault(&state.token);
                state.hardFaulted = true;
                ++gPspMeRenderShadowWindow.timeouts;
                ++gPspMeRenderShadowWindow.protocolFault;
                __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                                 __ATOMIC_RELEASE);
                return;
            }
        }
    }

    if (gPspMeRenderShadow.calcCompleteSerial == serialBefore ||
        !__atomic_load_n(&gPspMeRenderShadow.available, __ATOMIC_ACQUIRE) ||
        !gPspMeRenderShadow.managerActive || !nextDraw || state.pending ||
        state.compareActive ||
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        state.geConsumeActive ||
#endif
        gPspMeRenderShadow.drawSeq == 0u)
    {
        return;
    }

    ++gPspMeRenderShadowWindow.eligible;
    const u32 snapshotStartUs = sceKernelGetSystemTimeLow();
    Th07PspMeRenderStreamBuild build{};
    if (th07_psp_me_render_stream_acquire(&build) != 1)
    {
        gPspMeRenderShadowWindow.snapshotUs +=
            sceKernelGetSystemTimeLow() - snapshotStartUs;
        ++gPspMeRenderShadowWindow.busy;
        ++gPspMeRenderShadowWindow.beginFail;
        return;
    }
    Th07PspMeRenderStreamJob job{};
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    const bool snapshotBuilt =
        PspMeRenderBuildFusedSnapshot(&g_BulletManager, build, &job);
#else
    const bool snapshotBuilt =
        PspMeRenderBuildCorrectnessSnapshot(&g_BulletManager, build, &job);
#endif
    if (!snapshotBuilt)
    {
        if (th07_psp_me_render_stream_cancel_build(&build.token) != 1)
        {
            ++gPspMeRenderShadowWindow.streamReleaseFault;
            ++gPspMeRenderShadowWindow.protocolFault;
            state.hardFaulted = true;
            __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                             __ATOMIC_RELEASE);
        }
        gPspMeRenderShadowWindow.snapshotUs +=
            sceKernelGetSystemTimeLow() - snapshotStartUs;
        ++gPspMeRenderShadowWindow.coverageDrop;
        return;
    }
    gPspMeRenderShadowWindow.snapshotUs +=
        sceKernelGetSystemTimeLow() - snapshotStartUs;
    if (th07_psp_me_render_stream_submit(&job) != 1)
    {
        if (th07_psp_me_render_stream_cancel_build(&build.token) != 1)
        {
            ++gPspMeRenderShadowWindow.streamReleaseFault;
            ++gPspMeRenderShadowWindow.protocolFault;
            state.hardFaulted = true;
            __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                             __ATOMIC_RELEASE);
        }
        ++gPspMeRenderShadowWindow.beginFail;
        ++gPspMeRenderShadowWindow.busy;
        return;
    }

    state.pending = true;
    state.deadlineAccounted = false;
    state.hardFaulted = false;
    state.token = build.token;
    state.job = job;
    state.records = build.records;
    state.targetDrawSeq = job.targetDrawSeq;
    state.frameSeq = job.frameSeq;
    state.stageEpoch = job.stageEpoch;
    state.managerEpoch = job.managerEpoch;
    state.replayEpoch = job.replayEpoch;
    state.recordCount = job.recordCount;
    state.globalSignature = job.globalSignature;
    state.arcadeLeftBits = PspMeRenderFloatBits(
        g_GameManager.arcadeRegionTopLeftPos.x);
    state.arcadeTopBits = PspMeRenderFloatBits(
        g_GameManager.arcadeRegionTopLeftPos.y);
    state.viewportMinZBits =
        PspMeRenderFloatBits(g_Supervisor.viewport.minZ);
    state.viewportMaxZBits =
        PspMeRenderFloatBits(g_Supervisor.viewport.maxZ);
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    state.managerBulletCount = g_BulletManager.bulletCount;
    state.managerUpdateCount = g_BulletManager.updateCount;
    state.managerTimePrevious = g_BulletManager.time.previous;
    state.managerTimeSubFrameBits =
        PspMeRenderFloatBits(g_BulletManager.time.subFrame);
    state.managerTimeCurrent = g_BulletManager.time.current;
    state.managerMutationEpoch =
        g_BulletManager.pspMeRenderMutationEpoch;
    state.representativeSourceGeneration =
        gPspMeRenderFusedCapture.representativeSourceGeneration;
    for (u32 bucket = 0u; bucket < 6u; ++bucket)
    {
        state.managerBucketHeads[bucket] =
            g_BulletManager.bulletsPtrs[bucket];
    }
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    state.itemPrepareSerial =
        (job.flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u
            ? job.itemLayout.expectedPrepareSerial : 0u;
    state.itemRecordCount =
        (job.flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u
            ? job.itemLayout.expectedItemCount : 0u;
    state.itemTotalCount =
        (job.flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u
            ? job.itemLayout.expectedTotalCount : 0u;
    state.itemHead = (job.flags &
                      TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u
        ? gPspMeRenderFusedCapture.itemHead : nullptr;
    state.itemTail = (job.flags &
                      TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u
        ? gPspMeRenderFusedCapture.itemTail : nullptr;
    state.itemSuffixHead = (job.flags &
                      TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u
        ? gPspMeRenderFusedCapture.itemSuffixHead : nullptr;
    state.itemListTail = (job.flags &
                      TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u
        ? gPspMeRenderFusedCapture.itemListTail : nullptr;
    if (state.itemPrepareSerial != 0u)
    {
        ++gPspMeRenderShadowWindow.streamItemEligible;
        gPspMeRenderShadowWindow.streamItemRecords += state.itemRecordCount;
    }
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    state.effectPrepareSerial =
        (job.flags & TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST) != 0u
            ? job.effectLayout.expectedPrepareSerial : 0u;
    state.effectLayer0RecordCount =
        state.effectPrepareSerial != 0u
            ? job.effectLayout.expectedLayer0Count : 0u;
    state.effectLayer3RecordCount =
        state.effectPrepareSerial != 0u
            ? job.effectLayout.expectedLayer3Count : 0u;
#endif
#endif
#endif
    state.submitUs = sceKernelGetSystemTimeLow();
    ++gPspMeRenderShadowWindow.meRenderSubmitted;
    ++gPspMeRenderShadowWindow.streamSubmitted;
    gPspMeRenderShadowWindow.records += job.recordCount;
#if !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    gPspMeRenderShadowWindow.inputBytes +=
        static_cast<unsigned long long>(job.recordCount) *
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        sizeof(Th07PspMeRenderRawRecord);
#else
        sizeof(Th07PspMeRenderStreamRecord);
#endif
#endif
}
#endif

void PspMeRenderDrawDeadline()
{
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    PspMeRenderCorrectnessDrawDeadline();
    return;
#else
    ++gPspMeRenderShadow.drawSeq;
    if (!gPspMeRenderShadow.pending ||
        gPspMeRenderShadow.deadlineAccounted ||
        gPspMeRenderShadow.pendingTargetDrawSeq !=
            gPspMeRenderShadow.drawSeq)
    {
        return;
    }

    ++gPspMeRenderShadowWindow.deadlines;
    // M0B never renders the ME stream. Every matching deadline therefore
    // displays the canonical DENSE fallback even when it also qualifies as a
    // would-consume sample for a later correctness increment.
    ++gPspMeRenderShadowWindow.fallbackFrames;
    gPspMeRenderShadowWindow.targetRecords +=
        gPspMeRenderShadow.pendingRecordCount;
    gPspMeRenderShadowWindow.targetOutputBytes +=
        static_cast<unsigned long long>(
            gPspMeRenderShadow.pendingRecordCount) *
        TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
    const u32 nowUs = sceKernelGetSystemTimeLow();
    PspMeRenderRecordSlack(nowUs - gPspMeRenderShadow.pendingSubmitUs);
    const int retired = PspMeRenderRetirePending(true);
    if (retired == 0)
    {
        ++gPspMeRenderShadowWindow.notReady;
        ++gPspMeRenderShadowWindow.quarantined;
        gPspMeRenderShadow.deadlineAccounted = true;
    }
    else if (retired < 0)
    {
        gPspMeRenderShadow.deadlineAccounted = true;
    }
#endif
}

u32 PspMeRenderCalcCompleteSentinel(void *)
{
    if (gPspMeRenderShadow.managerActive)
    {
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        const u32 previousSerial = gPspMeRenderShadow.calcCompleteSerial;
#endif
        ++gPspMeRenderShadow.calcCompleteSerial;
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        const bool compactBlockedRender =
            !PspMeBulletCompactRetireBeforeRender();
#else
        constexpr bool compactBlockedRender = false;
#endif
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        // I-ME5 has no post-calc record copy.  Once the final priority-18
        // sentinel advances, all ordinary calc owners (0..17) are finished and
        // the published live lists are immutable until their target draw.  Start
        // ME here so SoundPlayer queue work runs inside the available slack;
        // GameWindow's ordinary post-calc call observes pending and is a no-op.
        // The priority-12 capture gate leaves published clear for fixed-30,
        // FPS-toggle and replay-restart paths, so those never submit early.
        if (!compactBlockedRender)
        {
            if (gPspMeRenderFusedCapture.published != 0u)
            {
                PspMeRenderCorrectnessAfterCalc(previousSerial, true);
            }
        }
#endif
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

void PspMeRenderManagerAdded()
{
    if (++gPspMeRenderShadow.stageEpoch == 0u)
        ++gPspMeRenderShadow.stageEpoch;
    if (++gPspMeRenderShadow.managerEpoch == 0u)
        ++gPspMeRenderShadow.managerEpoch;
    gPspMeRenderShadow.managerActive = true;
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    gPspMeBulletCompactSc = PspMeBulletCompactScState{};
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    gPspMeRenderFusedCapture.published = 0u;
    PspMeRenderResetRepresentativeSourceCache();
    PspMeRenderBuildRepresentativeSourceCache();
#endif
}

bool PspMeRenderManagerDeleted()
{
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    // The compact command owns Main-RAM seed/output banks independently of
    // the render stream. Release it before managerActive drops and before
    // Initialize can reuse any generation or Bullet storage.
    PspMeBulletCompactDrainForManagerDelete();
#endif
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    // RAW jobs follow live VM/sprite/table pointers.  No stage-owned ANM byte
    // may be reset or released until ME has completed and every stream slot
    // is back in FREE.  First fence a stream already handed to GE: besides
    // satisfying the slot drain, this also keeps ReleaseAnm from racing a GE
    // texture read.  This runs only at manager teardown, never in gameplay.
    Th07PspFenceMeRenderBeforeMeShutdown();
    const bool rawOwnersDrained =
        th07_psp_me_render_stream_drain_live() == 1;
    if (!rawOwnersDrained)
    {
        ++gPspMeRenderShadowWindow.protocolFault;
        gPspMeRenderCorrectness.hardFaulted = true;
        __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                         __ATOMIC_RELEASE);
        // DeletedCallback is followed by BulletManager::Initialize(), which
        // clears the same live Bullet/AnmVm storage. Merely leaking the ANM
        // files is therefore insufficient if a poisoned ME still owns a read.
        // Stop the process before any owner can be reused.
        PspMeRenderRawFailStop(
            "MERW I4 LIVE DRAIN FAIL -> COLD REBOOT");
    }
    else
    {
        gPspMeRenderCorrectness.pending = false;
        gPspMeRenderCorrectness.deadlineAccounted = false;
        gPspMeRenderCorrectness.compareActive = false;
        gPspMeRenderCorrectness.geConsumeActive = false;
        gPspMeRenderCorrectness.records = nullptr;
        gPspMeRenderCorrectness.ready = Th07PspMeRenderStreamReady{};
    }
#else
    constexpr bool rawOwnersDrained = true;
#endif
    gPspMeRenderShadow.managerActive = false;
    if (++gPspMeRenderShadow.stageEpoch == 0u)
        ++gPspMeRenderShadow.stageEpoch;
    if (++gPspMeRenderShadow.managerEpoch == 0u)
        ++gPspMeRenderShadow.managerEpoch;
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    gPspMeRenderFusedCapture.published = 0u;
    if (rawOwnersDrained)
    {
        PspMeRenderResetRepresentativeSourceCache();
    }
#endif
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    if (gPspMeRenderCorrectness.geConsumeActive)
    {
        // Teardown can follow an exceptional chain break after the deadline
        // but before priority 10 consumed READY. No GE command was enqueued.
        if (th07_psp_me_render_stream_release_ready(
                &gPspMeRenderCorrectness.token) != 1)
        {
            ++gPspMeRenderShadowWindow.streamReleaseFault;
            ++gPspMeRenderShadowWindow.protocolFault;
            gPspMeRenderCorrectness.hardFaulted = true;
            __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                             __ATOMIC_RELEASE);
        }
        gPspMeRenderCorrectness.geConsumeActive = false;
        gPspMeRenderCorrectness.records = nullptr;
        gPspMeRenderCorrectness.ready = Th07PspMeRenderStreamReady{};
    }
#endif
    if (gPspMeRenderCorrectness.compareActive)
    {
        // A normal draw ends the comparison before stage teardown.  If an
        // exceptional chain break reaches here first, release the READY slot
        // without ever exposing it to GE.
        if (th07_psp_me_render_stream_release_ready(
                &gPspMeRenderCorrectness.token) != 1)
        {
            ++gPspMeRenderShadowWindow.streamReleaseFault;
            ++gPspMeRenderShadowWindow.protocolFault;
            gPspMeRenderCorrectness.hardFaulted = true;
            __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                             __ATOMIC_RELEASE);
        }
        gPspMeRenderCorrectness.compareActive = false;
        gPspMeRenderCorrectness.records = nullptr;
        gPspMeRenderCorrectness.ready = Th07PspMeRenderStreamReady{};
    }
    if (gPspMeRenderCorrectness.pending &&
        !gPspMeRenderCorrectness.deadlineAccounted)
    {
        gPspMeRenderCorrectness.deadlineAccounted = true;
        ++gPspMeRenderShadowWindow.fallbackFrames;
    }
#else
    if (gPspMeRenderShadow.pending &&
        !gPspMeRenderShadow.deadlineAccounted)
    {
        ++gPspMeRenderShadowWindow.quarantined;
        gPspMeRenderShadow.deadlineAccounted = true;
    }
#endif
    return rawOwnersDrained;
}
#endif

#if defined(TH07_PSP_BULLET_ONEPASS_ROTATED)
inline unsigned int PspBulletOnePassGuColor(ZunColor color)
{
    return (color.color & 0xff00ff00u) | ((color.color & 0x00ff0000u) >> 16) |
           ((color.color & 0x000000ffu) << 16);
}

__attribute__((always_inline)) inline void
PspBulletOnePassWriteVertex(Th07PspSpriteVertex &out, float x, float y, float z,
                            float u, float v, ZunColor color)
{
    out.u = u;
    out.v = v;
    out.color = PspBulletOnePassGuColor(color);
    out.x = x;
    out.y = y;
    out.z = z;
}

#if defined(TH07_PSP_BULLET_ONEPASS_ROTATED) && \
    defined(TH07_PSP_BULLET_STATIC_PROXY)
inline bool PspRebuildBulletStaticProxy(BulletManager *manager, Bullet *bullet,
                                        u32 slot)
{
    PspBulletStaticProxyPool *pool = PspGetBulletStaticProxyPool(manager);
    if (!pool || slot >= static_cast<u32>(BulletManager::kBulletCapacity))
    {
        return false;
    }

    PspBulletStaticProxyRecord &record = pool->records[slot];
    record.flags = 0u;
    AnmVm *vm = &bullet->sprites.spriteBullet;
    if (bullet->state != BULLET_NORMAL || !vm->autoRotate ||
        vm->currentInstruction || !bullet->pspRenderRotationValid ||
        bullet->pspRenderSourceAngle != bullet->angle ||
        bullet->pspRenderAngle == 0.0f)
    {
        return false;
    }

    record.posX = g_GameManager.arcadeRegionTopLeftPos.x + bullet->pos.x;
    record.posY = g_GameManager.arcadeRegionTopLeftPos.y + bullet->pos.y;
    record.sourceAngleBits = PspBulletStaticProxyFloatBits(bullet->angle);
    record.generation = pool->generations[slot];
    record.reserved = 0u;
    PspCaptureBulletStaticProxyIdentity(
        &pool->identities[slot], vm, record.sourceAngleBits);
    u32 flags = kPspBulletStaticProxyPrepared;
    if (!vm->sprite || !vm->visible || !vm->active || !vm->color.bytes.a)
    {
        record.flags = flags;
        return true;
    }

    const i32 sourceFileIndex = vm->sprite->sourceFileIndex;
    if (sourceFileIndex < 0 || sourceFileIndex >= 264)
    {
        return false;
    }
    const float halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    const float halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
    const float sine = bullet->pspRenderSin;
    const float cosine = bullet->pspRenderCos;
    const float localX[4] = {-halfWidth, halfWidth, -halfWidth, halfWidth};
    const float localY[4] = {-halfHeight, -halfHeight, halfHeight, halfHeight};
    for (u32 corner = 0; corner < 4u; ++corner)
    {
        // Cache only the position-independent product sum. Draw preserves the
        // accepted ONEPASS grouping: local + livePosition + liveManagerOffset.
        record.localX[corner] =
            localX[corner] * cosine - localY[corner] * sine;
        record.localY[corner] =
            localX[corner] * sine + localY[corner] * cosine;
    }
    record.u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    record.u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    record.v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    record.v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    record.halfWidth = halfWidth;
    record.halfHeight = halfHeight;
    record.baseColor = PspBulletStaticProxyBaseColor(vm);
    flags |= (static_cast<u32>(vm->anchor)
              << kPspBulletStaticProxyAnchorShift) &
             kPspBulletStaticProxyAnchorMask;
    if (vm->blendMode)
    {
        flags |= kPspBulletStaticProxyBlendAdd;
    }
    if (vm->zWriteDisable)
    {
        flags |= kPspBulletStaticProxyZWriteDisable;
    }
    flags |= (static_cast<u32>(sourceFileIndex)
              << kPspBulletStaticProxySourceShift) &
             kPspBulletStaticProxySourceMask;
    record.flags = flags | kPspBulletStaticProxyDrawable;
    return true;
}

// Called from calc 12 after gameplay/ANM updates and before the canonical six
// bucket links are published. An ordinary stable frame writes only posX/posY;
// no static descriptor or per-frame alternate list is produced.
__attribute__((always_inline)) inline bool
PspSyncBulletStaticProxy(BulletManager *manager, Bullet *bullet, u32 slot)
{
    PspBulletStaticProxyPool *pool = PspGetBulletStaticProxyPool(manager);
    if (!pool || slot >= static_cast<u32>(BulletManager::kBulletCapacity) ||
        bullet->state != BULLET_NORMAL)
    {
        return false;
    }
    bullet->pspStaticProxySlot = static_cast<u16>(slot);

    AnmVm *vm = &bullet->sprites.spriteBullet;
    if (!vm->autoRotate)
    {
        pool->records[slot].flags = 0u;
        return false;
    }

    const bool angleChanged =
        !bullet->pspRenderRotationValid ||
        bullet->pspRenderSourceAngle != bullet->angle;
    if (angleChanged)
    {
        const f32 renderAngle =
            utils::AddNormalizeAngle(1.5707964f + bullet->angle, 0.0f);
        PspBulletRenderSinCos(renderAngle, &bullet->pspRenderSin,
                             &bullet->pspRenderCos);
        bullet->pspRenderSourceAngle = bullet->angle;
        bullet->pspRenderAngle = renderAngle;
        bullet->pspRenderRotationValid = 1u;
    }
    PspBulletStaticProxyRecord &record = pool->records[slot];
    const bool prepared =
        (record.flags & kPspBulletStaticProxyPrepared) != 0u &&
        record.generation == pool->generations[slot];
    if (!prepared || angleChanged)
    {
        return PspRebuildBulletStaticProxy(manager, bullet, slot);
    }

    record.posX = g_GameManager.arcadeRegionTopLeftPos.x + bullet->pos.x;
    record.posY = g_GameManager.arcadeRegionTopLeftPos.y + bullet->pos.y;
    return true;
}

// Validate live state before making Draw's observable VM writes. This catches
// calc jobs after priority 12 (notably GUI message bullet clears) and exact
// static changes without relying on a collision-prone hash. Once admitted,
// geometry emission reads only the compact proxy plus renderer globals.
__attribute__((always_inline)) inline bool
PspDrawBulletStaticProxy(BulletManager *owner, Bullet *bullet, u32 slot,
                         float viewportLeft,
                         float viewportTop, float viewportRight,
                         float viewportBottom)
{
    PspBulletStaticProxyPool *pool =
        PspGetBulletStaticProxyPool(owner);
    if (!pool || slot >= static_cast<u32>(BulletManager::kBulletCapacity) ||
        bullet->state != BULLET_NORMAL ||
        pool->publishedMutationEpoch !=
            owner->pspBulletStaticProxyMutationEpoch)
    {
        return false;
    }
    PspBulletStaticProxyRecord &record = pool->records[slot];
    if (!(record.flags & kPspBulletStaticProxyPrepared) ||
        record.generation != pool->generations[slot])
    {
        return false;
    }

    AnmVm *vm = &bullet->sprites.spriteBullet;
    const u32 sourceAngleBits = PspBulletStaticProxyFloatBits(bullet->angle);
    const float expectedPosX =
        g_GameManager.arcadeRegionTopLeftPos.x + bullet->pos.x;
    const float expectedPosY =
        g_GameManager.arcadeRegionTopLeftPos.y + bullet->pos.y;
    if (!bullet->pspRenderRotationValid ||
        bullet->pspRenderSourceAngle != bullet->angle ||
        bullet->pspRenderAngle == 0.0f ||
        record.sourceAngleBits != sourceAngleBits ||
        PspBulletStaticProxyFloatBits(record.posX) !=
            PspBulletStaticProxyFloatBits(expectedPosX) ||
        PspBulletStaticProxyFloatBits(record.posY) !=
            PspBulletStaticProxyFloatBits(expectedPosY) ||
        !PspBulletStaticProxyIdentityMatches(pool->identities[slot], vm,
                                             sourceAngleBits))
    {
        record.flags = 0u;
        return false;
    }

    // Exactly the current Bullet::Draw/ONEPASS observable mutation order. No
    // proxy path writes these fields during calc, so a post-calc state change
    // can still fall back without leaving an extra mutation behind.
    vm->pos.x = expectedPosX;
    vm->pos.y = expectedPosY;
    vm->pos.z = 0.05f;
    vm->color.color = (vm->color.color & 0xff000000u) | 0x00ffffffu;
    vm->SetRotationZ(bullet->pspRenderAngle);
    vm->updateRotation = 1;

    if (!(record.flags & kPspBulletStaticProxyDrawable))
    {
        return true;
    }

    AnmManager *manager = g_AnmManager;
    const u32 anchor =
        (record.flags & kPspBulletStaticProxyAnchorMask) >>
        kPspBulletStaticProxyAnchorShift;
    const float offsetX = manager->offset.x;
    const float offsetY = manager->offset.y;
    const float centerX = record.posX + offsetX +
        ((anchor & 1u) ? record.halfWidth : 0.0f);
    const float centerY = record.posY + offsetY +
        ((anchor & 2u) ? record.halfHeight : 0.0f);
    const float bound = fabsf(record.halfWidth) + fabsf(record.halfHeight);
    if (centerX + bound < viewportLeft || centerY + bound < viewportTop ||
        centerX - bound > viewportRight || centerY - bound > viewportBottom)
    {
        return true;
    }

    const u32 sourceFileIndex =
        (record.flags & kPspBulletStaticProxySourceMask) >>
        kPspBulletStaticProxySourceShift;
    const GfxTextureHandle texture = manager->textures[sourceFileIndex];
    const bool rendererStateMatches = manager->currentTexture == texture &&
        manager->currentVertexShader == 1 &&
        manager->pspSpriteBatchUsesPairs == 0 &&
        static_cast<u32>(manager->currentBlendMode) ==
            ((record.flags & kPspBulletStaticProxyBlendAdd) ? 1u : 0u) &&
        (g_Supervisor.cfg.disableZBuffer ||
         static_cast<u32>(manager->currentZWriteDisable) ==
             ((record.flags & kPspBulletStaticProxyZWriteDisable) ? 1u : 0u));
    if (__builtin_expect(!rendererStateMatches, 0))
    {
        return false;
    }

    ZunColor color{record.baseColor};
    if (manager->colorMulEnabled)
    {
        color.bytes.r = ZunColor::Multiply(color.bytes.r, manager->color.bytes.r);
        color.bytes.g = ZunColor::Multiply(color.bytes.g, manager->color.bytes.g);
        color.bytes.b = ZunColor::Multiply(color.bytes.b, manager->color.bytes.b);
        color.bytes.a = ZunColor::Multiply(color.bytes.a, manager->color.bytes.a);
    }
    ++manager->renderStateChangesThisFrame;
    manager->pspUnifiedBulletGeneralMode = 1;
    manager->pspForceSpriteQuads = 1;

    Th07PspSpriteVertex *out = manager->vertexBufferCurPtr;
    float x0 = record.localX[0] + record.posX + offsetX;
    float y0 = record.localY[0] + record.posY + offsetY;
    float x1 = record.localX[1] + record.posX + offsetX;
    float y1 = record.localY[1] + record.posY + offsetY;
    float x2 = record.localX[2] + record.posX + offsetX;
    float y2 = record.localY[2] + record.posY + offsetY;
    float x3 = record.localX[3] + record.posX + offsetX;
    float y3 = record.localY[3] + record.posY + offsetY;
    if (anchor & 1u)
    {
        x0 += record.halfWidth;
        x1 += record.halfWidth;
        x2 += record.halfWidth;
        x3 += record.halfWidth;
    }
    if (anchor & 2u)
    {
        y0 += record.halfHeight;
        y1 += record.halfHeight;
        y2 += record.halfHeight;
        y3 += record.halfHeight;
    }
    PspBulletOnePassWriteVertex(out[0], x0, y0, 0.05f, record.u0,
                                record.v0, color);
    PspBulletOnePassWriteVertex(out[1], x1, y1, 0.05f, record.u1,
                                record.v0, color);
    PspBulletOnePassWriteVertex(out[2], x2, y2, 0.05f, record.u0,
                                record.v1, color);
    PspBulletOnePassWriteVertex(out[3], x3, y3, 0.05f, record.u1,
                                record.v1, color);
    manager->vertexBufferCurPtr += 4;
    ++manager->spritesToDraw;
    return true;
}

__attribute__((always_inline)) inline bool
PspBulletStaticProxyFrameReady(const BulletManager *manager)
{
    const PspBulletStaticProxyPool *pool =
        PspGetBulletStaticProxyPool(manager);
    return pool && pool->publishedMutationEpoch ==
                       manager->pspBulletStaticProxyMutationEpoch;
}

__attribute__((always_inline)) inline bool
PspTryBulletStaticProxy(BulletManager *manager, Bullet *bullet,
                        float viewportLeft, float viewportTop,
                        float viewportRight, float viewportBottom)
{
    if (!PspBulletStaticProxyFrameReady(manager))
    {
        return false;
    }
    return PspDrawBulletStaticProxy(
        manager, bullet, static_cast<u32>(bullet->pspStaticProxySlot),
        viewportLeft, viewportTop, viewportRight, viewportBottom);
}
#else
__attribute__((always_inline)) inline bool
PspBulletStaticProxyFrameReady(const BulletManager *)
{
    return false;
}

__attribute__((always_inline)) inline bool
PspTryBulletStaticProxy(BulletManager *, Bullet *, float, float, float, float)
{
    return false;
}
#endif

// Consume the stable NORMAL+autoRotate case directly from the live Bullet.
// Every condition that can require a renderer call falls back to Bullet::Draw;
// the accepted path therefore has no nested Draw/Flush/state call and writes
// the same final four vertices in the same list order as DrawPspRotatedBullet.
__attribute__((always_inline)) inline bool
PspDrawNormalAutoRotatedOnePass(Bullet *bullet, float viewportLeft,
                                float viewportTop, float viewportRight,
                                float viewportBottom)
{
    if (__builtin_expect(bullet->state != BULLET_NORMAL, 0))
    {
        return false;
    }

    AnmVm *vm = &bullet->sprites.spriteBullet;
    if (__builtin_expect(!vm->autoRotate || !bullet->pspRenderRotationValid ||
                             bullet->pspRenderSourceAngle != bullet->angle ||
                             bullet->pspRenderAngle == 0.0f,
                         0))
    {
        return false;
    }

    // Preserve Bullet::Draw's observable VM mutations even when the sprite is
    // invisible or culled below.
    vm->pos.x = g_GameManager.arcadeRegionTopLeftPos.x + bullet->pos.x;
    vm->pos.y = g_GameManager.arcadeRegionTopLeftPos.y + bullet->pos.y;
    vm->pos.z = 0.05f;
    vm->color.color = (vm->color.color & 0xff000000u) | 0x00ffffffu;
    vm->SetRotationZ(bullet->pspRenderAngle);
    vm->updateRotation = 1;

    if (__builtin_expect(!vm->sprite || !vm->visible || !vm->active ||
                             !vm->color.bytes.a,
                         0))
    {
        return true;
    }

    const float halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    const float halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
    AnmManager *manager = g_AnmManager;
    const float centerX =
        vm->pos.x + manager->offset.x + ((vm->anchor & 1) ? halfWidth : 0.0f);
    const float centerY =
        vm->pos.y + manager->offset.y + ((vm->anchor & 2) ? halfHeight : 0.0f);
    const float bound = fabsf(halfWidth) + fabsf(halfHeight);
    if (__builtin_expect(centerX + bound < viewportLeft ||
                             centerY + bound < viewportTop ||
                             centerX - bound > viewportRight ||
                             centerY - bound > viewportBottom,
                         0))
    {
        return true;
    }

    const GfxTextureHandle texture = manager->textures[vm->sprite->sourceFileIndex];
    const bool rendererStateMatches =
        manager->currentTexture == texture && manager->currentVertexShader == 1 &&
        manager->pspSpriteBatchUsesPairs == 0 &&
        static_cast<u32>(manager->currentBlendMode) == vm->blendMode &&
        (g_Supervisor.cfg.disableZBuffer ||
         static_cast<u32>(manager->currentZWriteDisable) == vm->zWriteDisable);
    if (__builtin_expect(!rendererStateMatches, 0))
    {
        return false;
    }

    ZunColor color = vm->useColor2 ? vm->color2 : vm->color;
    if (manager->colorMulEnabled)
    {
        color.bytes.r = ZunColor::Multiply(color.bytes.r, manager->color.bytes.r);
        color.bytes.g = ZunColor::Multiply(color.bytes.g, manager->color.bytes.g);
        color.bytes.b = ZunColor::Multiply(color.bytes.b, manager->color.bytes.b);
        color.bytes.a = ZunColor::Multiply(color.bytes.a, manager->color.bytes.a);
    }
    ++manager->renderStateChangesThisFrame;

#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
    manager->pspUnifiedBulletGeneralMode = 1;
    manager->pspForceSpriteQuads = 1;
#endif

    const float u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    const float u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    const float v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    const float v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    const float z = vm->pos.z;
    const float posX = vm->pos.x;
    const float posY = vm->pos.y;
    const float offsetX = manager->offset.x;
    const float offsetY = manager->offset.y;
    const u32 anchor = vm->anchor;
    const float cachedSin = bullet->pspRenderSin;
    const float cachedCos = bullet->pspRenderCos;
    Th07PspSpriteVertex *out = manager->vertexBufferCurPtr;

#if defined(TH07_PSP_BULLET_HOT_PREFETCH)
    // The linked-list invariant makes every non-null next pointer a live slot
    // in BulletManager's static PSP-2000+ array. Fetch only the next VM's hot
    // predicate line, then hide its latency under this bullet's corner math.
    Bullet *const nextBullet = bullet->next;
    if (__builtin_expect(nextBullet != NULL, 1))
    {
        __builtin_allegrex_cache(
            0x1e, (int)(uintptr_t)&nextBullet->sprites.spriteBullet.autoRotate);
    }
#endif

    const float localX0 = -halfWidth;
    const float localY0 = -halfHeight;
    float x0 = localX0 * cachedCos - localY0 * cachedSin + posX + offsetX;
    float y0 = localX0 * cachedSin + localY0 * cachedCos + posY + offsetY;
    if (anchor & 1)
    {
        x0 += halfWidth;
    }
    if (anchor & 2)
    {
        y0 += halfHeight;
    }
    PspBulletOnePassWriteVertex(out[0], x0, y0, z, u0, v0, color);

    const float localX1 = halfWidth;
    const float localY1 = -halfHeight;
    float x1 = localX1 * cachedCos - localY1 * cachedSin + posX + offsetX;
    float y1 = localX1 * cachedSin + localY1 * cachedCos + posY + offsetY;
    if (anchor & 1)
    {
        x1 += halfWidth;
    }
    if (anchor & 2)
    {
        y1 += halfHeight;
    }
    PspBulletOnePassWriteVertex(out[1], x1, y1, z, u1, v0, color);

    const float localX2 = -halfWidth;
    const float localY2 = halfHeight;
    float x2 = localX2 * cachedCos - localY2 * cachedSin + posX + offsetX;
    float y2 = localX2 * cachedSin + localY2 * cachedCos + posY + offsetY;
    if (anchor & 1)
    {
        x2 += halfWidth;
    }
    if (anchor & 2)
    {
        y2 += halfHeight;
    }
    PspBulletOnePassWriteVertex(out[2], x2, y2, z, u0, v1, color);

    const float localX3 = halfWidth;
    const float localY3 = halfHeight;
    float x3 = localX3 * cachedCos - localY3 * cachedSin + posX + offsetX;
    float y3 = localX3 * cachedSin + localY3 * cachedCos + posY + offsetY;
    if (anchor & 1)
    {
        x3 += halfWidth;
    }
    if (anchor & 2)
    {
        y3 += halfHeight;
    }
    PspBulletOnePassWriteVertex(out[3], x3, y3, z, u1, v1, color);

    manager->vertexBufferCurPtr += 4;
    ++manager->spritesToDraw;
    return true;
}

#if defined(TH07_PSP_BULLET_WARM_QUEUE)
inline bool PspBeginBulletWarmQueue(BulletManager *manager)
{
    PspBulletWarmQueue *queue = PspGetBulletWarmQueue(manager);
    if (!queue)
    {
        return false;
    }

    // Invalidate the old publication first.  Any early return or incomplete
    // capture therefore makes the next draw use the complete existing list.
    queue->published = 0u;
    queue->recordCount = 0u;
    for (u32 bucket = 0; bucket < 6u; ++bucket)
    {
        queue->heads[bucket] = kPspBulletWarmQueueEnd;
    }
    memset(queue->writtenBits, 0, sizeof(queue->writtenBits));
    return Th07PspCanCommitBulletWarmQueue();
}

// Capture the exact slot/list topology while Bullet is still hot in calc 12.
// The strong record commits only Bullet::Draw's idempotent PSP render state;
// gameplay state, manager offset/color, viewport and culling stay live.
__attribute__((always_inline)) inline bool
PspCaptureBulletWarmRecord(BulletManager *manager, Bullet *bullet, u32 slotIndex,
                           u32 collisionType)
{
    PspBulletWarmQueue *queue = PspGetBulletWarmQueue(manager);
    if (!queue || slotIndex >= static_cast<u32>(BulletManager::kBulletCapacity) ||
        collisionType >= 6u || queue->recordCount >= BulletManager::kBulletCapacity)
    {
        return false;
    }

    const u32 bit = 1u << (slotIndex & 31u);
    u32 &word = queue->writtenBits[slotIndex >> 5];
    if (word & bit)
    {
        return false;
    }
    word |= bit;

    PspBulletWarmRecord &record = queue->records[slotIndex];
    record.flags = 0u;
    record.nextIndex = queue->heads[collisionType];
    queue->heads[collisionType] = static_cast<u16>(slotIndex);
    ++queue->recordCount;

    if (bullet->state != BULLET_NORMAL)
    {
        return true;
    }

    AnmVm *vm = &bullet->sprites.spriteBullet;
    if (!vm->autoRotate)
    {
        return true;
    }

    // This is Bullet::Draw's observable mutation order.  Enemy ECL is calc 10
    // and is the only external reader/writer of spriteBullet fields; repository
    // audit found no such observer after this calc-12 callback.  The move is
    // additionally guarded by curFrame/fixed30/input and the mutation epoch.
    vm->pos.x = g_GameManager.arcadeRegionTopLeftPos.x + bullet->pos.x;
    vm->pos.y = g_GameManager.arcadeRegionTopLeftPos.y + bullet->pos.y;
    vm->pos.z = 0.05f;
    vm->color.color = (vm->color.color & 0xff000000u) | 0x00ffffffu;
    if (!bullet->pspRenderRotationValid ||
        bullet->pspRenderSourceAngle != bullet->angle)
    {
        const f32 renderAngle =
            utils::AddNormalizeAngle(1.5707964f + bullet->angle, 0.0f);
        PspBulletRenderSinCos(renderAngle, &bullet->pspRenderSin,
                             &bullet->pspRenderCos);
        bullet->pspRenderSourceAngle = bullet->angle;
        bullet->pspRenderAngle = renderAngle;
        bullet->pspRenderRotationValid = 1u;
    }
    vm->SetRotationZ(bullet->pspRenderAngle);
    vm->updateRotation = 1;

    // A zero normalized rotation takes DrawPspBullet's canonical axis path.
    // Keep it canonical rather than expanding this increment's semantics.
    if (bullet->pspRenderAngle == 0.0f)
    {
        return true;
    }

    u32 flags = kPspBulletWarmPrepared;
    if (!vm->sprite || !vm->visible || !vm->active || !vm->color.bytes.a)
    {
        record.flags = flags;
        return true;
    }

    const float halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    const float halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
    const float posX = vm->pos.x;
    const float posY = vm->pos.y;
    const float sine = bullet->pspRenderSin;
    const float cosine = bullet->pspRenderCos;
    const float localX[4] = {-halfWidth, halfWidth, -halfWidth, halfWidth};
    const float localY[4] = {-halfHeight, -halfHeight, halfHeight, halfHeight};
    for (u32 corner = 0; corner < 4u; ++corner)
    {
        // Preserve the accepted ONEPASS grouping; draw adds live offset and
        // anchor after reloading this single-precision base value.
        record.baseX[corner] =
            localX[corner] * cosine - localY[corner] * sine + posX;
        record.baseY[corner] =
            localX[corner] * sine + localY[corner] * cosine + posY;
    }

    record.u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    record.u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    record.v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    record.v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    record.posX = posX;
    record.posY = posY;
    record.halfWidth = halfWidth;
    record.halfHeight = halfHeight;
    record.bound = fabsf(halfWidth) + fabsf(halfHeight);
    record.baseColor = (vm->useColor2 ? vm->color2 : vm->color).color;
    const i32 sourceFileIndex = vm->sprite->sourceFileIndex;
    if (sourceFileIndex < 0 || sourceFileIndex >= 264)
    {
        return false;
    }
    record.sourceFileIndex = static_cast<u16>(sourceFileIndex);
    flags |= (static_cast<u32>(vm->anchor) << kPspBulletWarmAnchorShift) &
             kPspBulletWarmAnchorMask;
    if (vm->blendMode)
    {
        flags |= kPspBulletWarmBlendAdd;
    }
    if (vm->zWriteDisable)
    {
        flags |= kPspBulletWarmZWriteDisable;
    }
    record.flags = flags | kPspBulletWarmDrawable;
    return true;
}

inline void PspPublishBulletWarmQueue(BulletManager *manager, bool complete)
{
    PspBulletWarmQueue *queue = PspGetBulletWarmQueue(manager);
    if (!queue)
    {
        return;
    }
    u32 writtenCount = 0u;
    for (u32 word = 0;
         word < static_cast<u32>((BulletManager::kBulletCapacity + 31) / 32);
         ++word)
    {
        writtenCount += static_cast<u32>(__builtin_popcount(queue->writtenBits[word]));
    }
    for (u32 bucket = 0; bucket < 6u; ++bucket)
    {
        if (queue->heads[bucket] != kPspBulletWarmQueueEnd &&
            queue->heads[bucket] >= BulletManager::kBulletCapacity)
        {
            complete = false;
        }
    }
    queue->mutationEpoch = manager->pspBulletMutationEpoch;
    queue->published = complete &&
                               queue->recordCount <= BulletManager::kBulletCapacity &&
                               writtenCount == queue->recordCount
                           ? 1u
                           : 0u;
}

inline bool PspBulletWarmQueueReady(const BulletManager *manager)
{
    const PspBulletWarmQueue *queue = PspGetBulletWarmQueue(manager);
    if (!queue || queue->published != 1u ||
        queue->recordCount > BulletManager::kBulletCapacity ||
        queue->mutationEpoch != manager->pspBulletMutationEpoch)
    {
        return false;
    }

    // Capture validates each unique slot/link and publish checks its bitmap
    // count.  Keep the hot draw gate O(6): rereading all ~80 KiB before the
    // real walk would evict the exact prepared data this feature is meant to
    // keep warm.
    for (u32 bucket = 0; bucket < 6u; ++bucket)
    {
        if (queue->heads[bucket] != kPspBulletWarmQueueEnd &&
            queue->heads[bucket] >= BulletManager::kBulletCapacity)
        {
            return false;
        }
    }
    return true;
}

// Return false only for a renderer-state mismatch.  The caller then performs
// canonical Bullet::Draw from the stored slot.  A prepared hit never reads the
// Bullet or AnmVm and never calls into the renderer.
__attribute__((always_inline)) inline bool
PspDrawBulletWarmRecord(const PspBulletWarmRecord &record, float viewportLeft,
                        float viewportTop, float viewportRight,
                        float viewportBottom)
{
    if (!(record.flags & kPspBulletWarmPrepared))
    {
        return false;
    }
    if (!(record.flags & kPspBulletWarmDrawable))
    {
        return true;
    }

    AnmManager *manager = g_AnmManager;
    const u32 anchor =
        (record.flags & kPspBulletWarmAnchorMask) >> kPspBulletWarmAnchorShift;
    const float offsetX = manager->offset.x;
    const float offsetY = manager->offset.y;
    const float centerX =
        record.posX + offsetX + ((anchor & 1u) ? record.halfWidth : 0.0f);
    const float centerY =
        record.posY + offsetY + ((anchor & 2u) ? record.halfHeight : 0.0f);
    if (centerX + record.bound < viewportLeft ||
        centerY + record.bound < viewportTop ||
        centerX - record.bound > viewportRight ||
        centerY - record.bound > viewportBottom)
    {
        return true;
    }

    const GfxTextureHandle texture = manager->textures[record.sourceFileIndex];
    const bool rendererStateMatches = manager->currentTexture == texture &&
        manager->currentVertexShader == 1 && manager->pspSpriteBatchUsesPairs == 0 &&
        static_cast<u32>(manager->currentBlendMode) ==
            ((record.flags & kPspBulletWarmBlendAdd) ? 1u : 0u) &&
        (g_Supervisor.cfg.disableZBuffer ||
         static_cast<u32>(manager->currentZWriteDisable) ==
             ((record.flags & kPspBulletWarmZWriteDisable) ? 1u : 0u));
    if (__builtin_expect(!rendererStateMatches, 0))
    {
        return false;
    }

    ZunColor color{record.baseColor};
    if (manager->colorMulEnabled)
    {
        color.bytes.r = ZunColor::Multiply(color.bytes.r, manager->color.bytes.r);
        color.bytes.g = ZunColor::Multiply(color.bytes.g, manager->color.bytes.g);
        color.bytes.b = ZunColor::Multiply(color.bytes.b, manager->color.bytes.b);
        color.bytes.a = ZunColor::Multiply(color.bytes.a, manager->color.bytes.a);
    }
    ++manager->renderStateChangesThisFrame;
    manager->pspUnifiedBulletGeneralMode = 1;
    manager->pspForceSpriteQuads = 1;

    Th07PspSpriteVertex *out = manager->vertexBufferCurPtr;
    const float u[4] = {record.u0, record.u1, record.u0, record.u1};
    const float v[4] = {record.v0, record.v0, record.v1, record.v1};
    for (u32 corner = 0; corner < 4u; ++corner)
    {
        float x = record.baseX[corner] + offsetX;
        float y = record.baseY[corner] + offsetY;
        if (anchor & 1u)
        {
            x += record.halfWidth;
        }
        if (anchor & 2u)
        {
            y += record.halfHeight;
        }
        PspBulletOnePassWriteVertex(out[corner], x, y, 0.05f, u[corner],
                                    v[corner], color);
    }
    manager->vertexBufferCurPtr += 4;
    ++manager->spritesToDraw;
    return true;
}
#endif
#endif
} // namespace
#endif

#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
bool Th07PspTryConsumeMeEffectStream()
{
    PspMeRenderCorrectnessState &state = gPspMeRenderCorrectness;
    if (!PspMeRenderTryEarlyItemRetire() || !state.geConsumeActive)
    {
        return false;
    }

    const u32 expectedDrawSeq = gPspMeRenderShadow.drawSeq + 1u;
    if (!PspMeRenderValidateReadyStream(&g_BulletManager, expectedDrawSeq) ||
        !g_EffectManager.PspValidateMeEffectRenderStream(
            &state.job, &state.ready))
    {
        // Effect-local rejection must not consume or release the independently
        // valid Item/Bullet stream. Priority 10 retries its ordinary owner.
        return false;
    }

    const bool hasEffectRuns =
        state.ready.effectLayer0RunCount != 0u ||
        state.ready.effectLayer3RunCount != 0u;
    bool openedPrefixOwner = false;
    if (hasEffectRuns)
    {
        g_AnmManager->Flush();
        if (!Th07PspBeginMeRenderGeSubmission(
                state.token.slot, state.token.generation))
        {
            ++gPspMeRenderShadowWindow.beginFail;
            return false;
        }
        openedPrefixOwner = true;
        state.prefixGeSubmissionOpen = true;
        state.prefixValidatedTokenSlot = state.token.slot;
        state.prefixValidatedTokenGeneration = state.token.generation;
        state.prefixValidatedDrawSeq = expectedDrawSeq;
        g_Supervisor.gfxDevice->SetTextureArg(TEX_ARG_DIFFUSE);
        g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA,
                                           COLOR_OP_MODULATE);
        g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB,
                                           COLOR_OP_MODULATE);
    }

    const auto submitRuns = [](void *, unsigned int firstRun,
                               unsigned int endRun) {
        PspMeRenderSubmitRunRange(firstRun, endRun, false);
    };
    if (!g_EffectManager.PspConsumeMeEffectRenderStream(
            &state.job, &state.ready, submitRuns, nullptr))
    {
        // Consume revalidates before its first callback. This path therefore
        // owns no visible Effect command; if Begin already promoted the token,
        // close the empty span and let the completed-list fence recycle it.
        if (openedPrefixOwner)
        {
            Th07PspEndMeRenderGeSubmission();
            state.prefixGeSubmissionOpen = false;
            state.geConsumeActive = false;
            state.records = nullptr;
            state.ready = Th07PspMeRenderStreamReady{};
        }
        return false;
    }
    return true;
}
#endif

#if defined(TH07_PSP_ME_RENDER_WORKER)
void Th07PspMeRenderSetAvailable(bool available)
{
    __atomic_store_n(&gPspMeRenderShadow.available, available ? 1u : 0u,
                     __ATOMIC_RELEASE);
}

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
void Th07PspMeRenderGeReleaseFault()
{
    ++gPspMeRenderShadowWindow.streamReleaseFault;
    ++gPspMeRenderShadowWindow.protocolFault;
    gPspMeRenderCorrectness.hardFaulted = true;
    __atomic_store_n(&gPspMeRenderShadow.available, 0u, __ATOMIC_RELEASE);
}
#endif

unsigned int Th07PspMeRenderCaptureCalcSerial()
{
    return gPspMeRenderShadow.calcCompleteSerial;
}

void Th07PspMeRenderAfterCalc(unsigned int serialBefore, bool nextDraw)
{
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    PspMeRenderCorrectnessAfterCalc(serialBefore, nextDraw);
    return;
#else
    // A deadline miss is not a worker fault.  Retire its isolated shadow slot
    // only after ME reports DONE, then allow this same post-calc boundary to
    // submit a fresh frame.  There is intentionally no wait or polling loop.
    if (gPspMeRenderShadow.pending &&
        gPspMeRenderShadow.deadlineAccounted)
    {
        if (gPspMeRenderShadow.hardFaulted)
        {
            return;
        }

        // A long SC-only transition can keep this completion unobserved for
        // seconds even though ME finished in time.  Probe/retire first; age is
        // evidence of a hang only while the mailbox still reports RUNNING.
        int retired = PspMeRenderRetirePending(false);
        if (retired < 0)
        {
            // The low-level layer already latched the process-lifetime fault.
            // Stop probing it every frame and inflating protocol telemetry.
            gPspMeRenderShadow.hardFaulted = 1u;
            return;
        }
        if (retired == 0)
        {
            if (sceKernelGetSystemTimeLow() -
                    gPspMeRenderShadow.pendingSubmitUs >=
                100000u)
            {
                // Close the probe-to-timeout race once before poisoning the
                // process-lifetime ME owner.
                retired = PspMeRenderRetirePending(false);
                if (retired < 0)
                {
                    gPspMeRenderShadow.hardFaulted = 1u;
                    return;
                }
                if (retired == 0)
                {
                    th07_psp_me_render_hard_fault();
                    gPspMeRenderShadow.hardFaulted = 1u;
                    ++gPspMeRenderShadowWindow.timeouts;
                    ++gPspMeRenderShadowWindow.protocolFault;
                    __atomic_store_n(&gPspMeRenderShadow.available, 0u,
                                     __ATOMIC_RELEASE);
                    return;
                }
            }
            else
            {
                return;
            }
        }
    }

    if (gPspMeRenderShadow.calcCompleteSerial == serialBefore)
    {
        return;
    }

    if (!__atomic_load_n(&gPspMeRenderShadow.available, __ATOMIC_ACQUIRE) ||
        !gPspMeRenderShadow.managerActive || !nextDraw ||
        gPspMeRenderShadow.pending)
    {
        return;
    }

    ++gPspMeRenderShadowWindow.eligible;
    const u32 snapshotStartUs = sceKernelGetSystemTimeLow();
    u32 recordCount = 0u;
    u32 signature = 0u;
    if (!PspMeRenderBuildShadowSnapshot(&g_BulletManager, &recordCount,
                                        &signature))
    {
        gPspMeRenderShadowWindow.snapshotUs +=
            sceKernelGetSystemTimeLow() - snapshotStartUs;
        ++gPspMeRenderShadowWindow.coverageDrop;
        return;
    }
    gPspMeRenderShadowWindow.snapshotUs +=
        sceKernelGetSystemTimeLow() - snapshotStartUs;

    Th07PspMeRenderJob job{};
    job.version = TH07_PSP_ME_RENDER_VERSION;
    job.frameSeq = ++gPspMeRenderShadow.frameSeq;
    job.targetDrawSeq = gPspMeRenderShadow.drawSeq + 1u;
    job.stageEpoch = gPspMeRenderShadow.stageEpoch;
    job.managerEpoch = gPspMeRenderShadow.managerEpoch;
    job.replayEpoch = static_cast<u32>(
        g_ReplayManager ? g_ReplayManager->frameId : 0);
    job.input = th07_psp_me_render_runtime_input();
    job.inputBytes = recordCount * sizeof(PspMeRenderShadowRecord64);
    job.inputStride = sizeof(PspMeRenderShadowRecord64);
    job.recordCount = recordCount;
    job.output = th07_psp_me_render_runtime_output();
    job.outputBytes = TH07_PSP_ME_RENDER_MAX_RECORDS *
                      TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
    if (!th07_psp_me_render_begin(&job))
    {
        ++gPspMeRenderShadowWindow.beginFail;
        ++gPspMeRenderShadowWindow.busy;
        return;
    }

    ++gPspMeRenderShadowWindow.meRenderSubmitted;
    gPspMeRenderShadowWindow.records += recordCount;
    gPspMeRenderShadowWindow.inputBytes += job.inputBytes;
    gPspMeRenderShadowWindow.outputBytes +=
        recordCount * TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
    gPspMeRenderShadow.pending = true;
    gPspMeRenderShadow.hardFaulted = 0u;
    gPspMeRenderShadow.deadlineAccounted = false;
    gPspMeRenderShadow.pendingTargetDrawSeq = job.targetDrawSeq;
    gPspMeRenderShadow.pendingFrameSeq = job.frameSeq;
    gPspMeRenderShadow.pendingStageEpoch = job.stageEpoch;
    gPspMeRenderShadow.pendingManagerEpoch = job.managerEpoch;
    gPspMeRenderShadow.pendingReplayEpoch = job.replayEpoch;
    gPspMeRenderShadow.pendingRecordCount = job.recordCount;
    gPspMeRenderShadow.pendingInputStride = job.inputStride;
    gPspMeRenderShadow.pendingOutputBytes =
        recordCount * TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
    gPspMeRenderShadow.pendingSignature = signature;
    gPspMeRenderShadow.pendingSubmitUs = sceKernelGetSystemTimeLow();
#endif
}

void Th07PspTakeMeRenderShadowWindow(Th07PspMeRenderShadowWindow *window)
{
    if (!window)
    {
        return;
    }

    *window = gPspMeRenderShadowWindow;
    window->sampleCount = gPspMeRenderSlackCount;
    window->kernelSampleCount = gPspMeRenderKernelCycleCount;
    window->sampleOverflow = gPspMeRenderSlackOverflow +
                             gPspMeRenderKernelCycleOverflow;
    if (gPspMeRenderSlackCount != 0u)
    {
        u32 sorted[kPspMeRenderSlackSamples];
        std::memcpy(sorted, gPspMeRenderSlackUs,
                    gPspMeRenderSlackCount * sizeof(sorted[0]));
        for (u32 index = 1u; index < gPspMeRenderSlackCount; ++index)
        {
            const u32 value = sorted[index];
            u32 cursor = index;
            while (cursor != 0u && sorted[cursor - 1u] > value)
            {
                sorted[cursor] = sorted[cursor - 1u];
                --cursor;
            }
            sorted[cursor] = value;
        }
        const u32 last = gPspMeRenderSlackCount - 1u;
        window->slackMinUs = sorted[0];
        window->slackP50Us = sorted[(last * 50u) / 100u];
        window->slackP95Us = sorted[(last * 95u) / 100u];
        window->slackP99Us = sorted[(last * 99u) / 100u];
    }

    if (gPspMeRenderKernelCycleCount != 0u)
    {
        u32 sorted[kPspMeRenderSlackSamples];
        std::memcpy(sorted, gPspMeRenderKernelCycles,
                    gPspMeRenderKernelCycleCount * sizeof(sorted[0]));
        for (u32 index = 1u; index < gPspMeRenderKernelCycleCount; ++index)
        {
            const u32 value = sorted[index];
            u32 cursor = index;
            while (cursor != 0u && sorted[cursor - 1u] > value)
            {
                sorted[cursor] = sorted[cursor - 1u];
                --cursor;
            }
            sorted[cursor] = value;
        }
        const u32 last = gPspMeRenderKernelCycleCount - 1u;
        window->kernelCycleMin = sorted[0];
        window->kernelCycleP50 = sorted[(last * 50u) / 100u];
        window->kernelCycleP95 = sorted[(last * 95u) / 100u];
        window->kernelCycleP99 = sorted[(last * 99u) / 100u];
    }

    // Correctness faults are process-run verdicts, not merely PERF window
    // samples.  Preserve them across ordinary window resets and transition
    // discards so a mismatch outside W12-W15 cannot disappear before logging.
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    const Th07PspMeRenderShadowWindow sticky = gPspMeRenderShadowWindow;
#endif
    gPspMeRenderShadowWindow = Th07PspMeRenderShadowWindow{};
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    gPspMeRenderShadowWindow.notReady = sticky.notReady;
    gPspMeRenderShadowWindow.lateRetired = sticky.lateRetired;
    gPspMeRenderShadowWindow.signatureDrop = sticky.signatureDrop;
    gPspMeRenderShadowWindow.fcrDrop = sticky.fcrDrop;
    gPspMeRenderShadowWindow.epochDrop = sticky.epochDrop;
    gPspMeRenderShadowWindow.stageEpochDrop = sticky.stageEpochDrop;
    gPspMeRenderShadowWindow.managerEpochDrop = sticky.managerEpochDrop;
    gPspMeRenderShadowWindow.replayEpochDrop = sticky.replayEpochDrop;
    gPspMeRenderShadowWindow.generationDrop = sticky.generationDrop;
    gPspMeRenderShadowWindow.boundsDrop = sticky.boundsDrop;
    gPspMeRenderShadowWindow.busy = sticky.busy;
    gPspMeRenderShadowWindow.timeouts = sticky.timeouts;
    gPspMeRenderShadowWindow.quarantined = sticky.quarantined;
    gPspMeRenderShadowWindow.deadlineFault = sticky.deadlineFault;
    gPspMeRenderShadowWindow.coverageDrop = sticky.coverageDrop;
    gPspMeRenderShadowWindow.beginFail = sticky.beginFail;
    gPspMeRenderShadowWindow.protocolFault = sticky.protocolFault;
    gPspMeRenderShadowWindow.streamMismatch = sticky.streamMismatch;
    gPspMeRenderShadowWindow.streamSizeMismatch =
        sticky.streamSizeMismatch;
    gPspMeRenderShadowWindow.streamVertexMismatch =
        sticky.streamVertexMismatch;
    gPspMeRenderShadowWindow.streamRunMismatch = sticky.streamRunMismatch;
    gPspMeRenderShadowWindow.streamHashMismatch = sticky.streamHashMismatch;
    gPspMeRenderShadowWindow.streamHeaderDrop = sticky.streamHeaderDrop;
    gPspMeRenderShadowWindow.streamIdentityDrop = sticky.streamIdentityDrop;
    gPspMeRenderShadowWindow.streamMixedPrimitiveFrames =
        sticky.streamMixedPrimitiveFrames;
    gPspMeRenderShadowWindow.streamReleaseFault = sticky.streamReleaseFault;
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    gPspMeRenderShadowWindow.compactProtocolFault =
        sticky.compactProtocolFault;
#endif
    gPspMeRenderShadowWindow.streamFirstMismatchKind =
        sticky.streamFirstMismatchKind;
    gPspMeRenderShadowWindow.streamFirstMismatchWord =
        sticky.streamFirstMismatchWord;
    gPspMeRenderShadowWindow.streamFirstMismatchExpected =
        sticky.streamFirstMismatchExpected;
    gPspMeRenderShadowWindow.streamFirstMismatchActual =
        sticky.streamFirstMismatchActual;
#endif
    gPspMeRenderSlackCount = 0u;
#if !defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    gPspMeRenderSlackOverflow = 0u;
#endif
    gPspMeRenderKernelCycleCount = 0u;
#if !defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    gPspMeRenderKernelCycleOverflow = 0u;
#endif
}
#endif

const BulletTypeInfo g_BulletTypeInfos[11] = {
    {0x200, 0x212, 0x213, 0x214, 0x20f}, {0x201, 0x215, 0x216, 0x217, 0x210},
    {0x202, 0x215, 0x216, 0x217, 0x210}, {0x203, 0x215, 0x216, 0x217, 0x210},
    {0x204, 0x215, 0x216, 0x217, 0x210}, {0x205, 0x215, 0x216, 0x217, 0x210},
    {0x206, 0x215, 0x216, 0x217, 0x210}, {0x207, 0x218, 0x218, 0x218, 0x211},
    {0x208, 0x218, 0x218, 0x218, 0x211}, {0x209, 0x218, 0x218, 0x218, 0x211},
    {0x2a8, 0x2aa, 0x2aa, 0x2aa, 0x2a9},
};

u32 g_BulletColorsArray[28] = {
    0xFF000000, 0xFF303030, 0xFF606060, 0xFF500000, 0xFF900000, 0xFFFF2020, 0xFF400040,
    0xFF800080, 0xFFFF30FF, 0xFF000050, 0xFF000090, 0xFF2020FF, 0xFF203060, 0xFF304090,
    0xFF3080FF, 0xFF005000, 0xFF009000, 0xFF20FF20, 0xFF206000, 0xFF409010, 0xFF80FF20,
    0xFF505000, 0xFF909000, 0xFFFFFF20, 0xFF603000, 0xFF904010, 0xFFF08020, 0xFFFFFFFF};

u32 g_DefaultBulletColors[28] = {
    0xFFF0F0F0, 0xFFF0F0F0, 0xFFFFFFFF, 0xFFFFE0E0, 0xFFFFE0E0, 0xFFFFE0E0, 0xFFFFE0FF,
    0xFFFFE0FF, 0xFFFFE0FF, 0xFFE0E0FF, 0xFFE0E0FF, 0xFFE0E0FF, 0xFFE0FFFF, 0xFFE0FFFF,
    0xFFE0FFFF, 0xFFE0FFE0, 0xFFE0FFE0, 0xFFE0FFE0, 0xFFE0FFE0, 0xFFE0FFE0, 0xFFE0FFE0,
    0xFFFFFFE0, 0xFFFFFFE0, 0xFFFFFFE0, 0xFFFFE0E0, 0xFFFFE0E0, 0xFFFFE0E0, 0xFFFFFFFF};

u32 *g_BulletColor = g_BulletColorsArray;

i32 g_BulletSpriteOffset16Px[16] = {0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 0};

i32 g_BulletSpriteOffset32Px[8] = {0, 1, 1, 2, 2, 3, 4, 0};

ChainElem g_BulletManagerDrawChain;

BulletManager g_BulletManager;

ChainElem g_BulletManagerCalcChain;
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
ChainElem g_PspMeBulletCompactLaunchChain;
#endif
#if defined(TH07_PSP_ME_RENDER_WORKER)
ChainElem g_PspMeRenderCalcCompleteChain;
#endif

void BulletManager::Initialize()
{
#if defined(TH07_PSP_1000)
    Bullet *chunks[kBulletChunkCount];
    memcpy(chunks, this->bulletChunks, sizeof(chunks));
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    // RegisterChain normally initializes only after DeletedCallback freed the
    // stage allocation.  Preserve-and-invalidate defensively so an exceptional
    // direct reinitialize cannot orphan a live queue allocation.
    void *warmQueue = this->pspBulletWarmQueue;
    const u32 warmMutationEpoch = this->pspBulletMutationEpoch;
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    const u32 meRenderMutationEpoch = this->pspMeRenderMutationEpoch;
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    // Preserve ownership across the defensive direct-reinitialize path. The
    // records themselves are stage state and are invalidated below.
    void *staticProxyPool = this->pspBulletStaticProxyPool;
    const u32 staticProxyMutationEpoch =
        this->pspBulletStaticProxyMutationEpoch;
#endif
    memset(this, 0, sizeof(BulletManager));
#if defined(TH07_PSP_1000)
    memcpy(this->bulletChunks, chunks, sizeof(chunks));
    for (i32 i = 0; i < kBulletChunkCount; i++)
    {
        if (this->bulletChunks[i])
        {
            memset(this->bulletChunks[i], 0,
                   sizeof(Bullet) * static_cast<size_t>(kBulletChunkCapacity));
        }
    }
    this->pspNextBulletIndex = 0;
#else
    this->bulletsStart = this->bullets;
    this->bullets[kBulletCapacity].state = BULLET_END_ARRAY;
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    this->pspBulletWarmQueue = warmQueue;
    this->pspBulletMutationEpoch = warmMutationEpoch + (warmQueue ? 1u : 0u);
    if (warmQueue)
    {
        PspGetBulletWarmQueue(this)->published = 0u;
    }
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    this->pspMeRenderMutationEpoch = meRenderMutationEpoch;
    this->PspMarkMeRenderMutation();
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    this->pspBulletStaticProxyPool = staticProxyPool;
    this->pspBulletStaticProxyMutationEpoch =
        staticProxyMutationEpoch + (staticProxyPool ? 1u : 0u);
    if (staticProxyPool)
    {
        memset(staticProxyPool, 0, sizeof(PspBulletStaticProxyPool));
    }
#endif
    this->itemType = ITEM_POINT_BULLET;
}

BulletManager::BulletManager()
{
#if defined(TH07_PSP_1000)
    memset(this->bulletChunks, 0, sizeof(this->bulletChunks));
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    this->pspBulletWarmQueue = NULL;
    this->pspBulletMutationEpoch = 0u;
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    this->pspMeRenderMutationEpoch = 0u;
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    this->pspBulletStaticProxyPool = NULL;
    this->pspBulletStaticProxyMutationEpoch = 0u;
#endif
    Initialize();
}

#if defined(TH07_PSP_BULLET_WARM_QUEUE)
bool BulletManager::PspEnsureBulletWarmQueue()
{
    if (this->pspBulletWarmQueue)
    {
        return true;
    }

    void *allocation = memalign(64, sizeof(PspBulletWarmQueue));
    if (!allocation)
    {
        return false;
    }
    memset(allocation, 0, sizeof(PspBulletWarmQueue));
    this->pspBulletWarmQueue = allocation;
    return true;
}

void BulletManager::PspReleaseBulletWarmQueue()
{
    if (!this->pspBulletWarmQueue)
    {
        return;
    }
    std::free(this->pspBulletWarmQueue);
    this->pspBulletWarmQueue = NULL;
}
#endif

#if defined(TH07_PSP_BULLET_STATIC_PROXY)
bool BulletManager::PspEnsureBulletStaticProxyPool()
{
    if (this->pspBulletStaticProxyPool)
    {
        return true;
    }

    void *allocation = memalign(64, sizeof(PspBulletStaticProxyPool));
    if (!allocation)
    {
        return false;
    }
    memset(allocation, 0, sizeof(PspBulletStaticProxyPool));
    this->pspBulletStaticProxyPool = allocation;
    return true;
}

void BulletManager::PspReleaseBulletStaticProxyPool()
{
    if (!this->pspBulletStaticProxyPool)
    {
        return;
    }
    std::free(this->pspBulletStaticProxyPool);
    this->pspBulletStaticProxyPool = NULL;
}

void BulletManager::PspInvalidateBulletStaticProxy(Bullet *bullet)
{
    PspBulletStaticProxyPool *pool = PspGetBulletStaticProxyPool(this);
    if (!pool || !bullet)
    {
        return;
    }
    const u32 slot = static_cast<u32>(bullet - this->bullets);
    if (slot >= static_cast<u32>(kBulletCapacity))
    {
        return;
    }
    ++pool->generations[slot];
    pool->records[slot].flags = 0u;
    PspMarkBulletStaticProxyMutation();
}
#endif

#if defined(TH07_PSP_1000)
bool BulletManager::PspEnsureBulletPool()
{
    for (i32 i = 0; i < kBulletChunkCount; i++)
    {
        if (!this->bulletChunks[i])
        {
            this->bulletChunks[i] = static_cast<Bullet *>(th07_psp_1000_alloc_pool(
                sizeof(Bullet) * static_cast<size_t>(kBulletChunkCapacity)));
            if (this->bulletChunks[i])
            {
                memset(this->bulletChunks[i], 0,
                       sizeof(Bullet) * static_cast<size_t>(kBulletChunkCapacity));
            }
        }
        if (!this->bulletChunks[i])
        {
            th07_psp_boot_notef("PSP1000 bullet chunk %d/%d allocation failed", i + 1,
                                kBulletChunkCount);
            PspReleaseBulletPool();
            return false;
        }
    }
    memset(this->pspActiveBulletBits, 0, sizeof(this->pspActiveBulletBits));
    this->pspNextBulletIndex = 0;
    th07_psp_boot_notef("PSP1000 bullet pool %d slots in %d chunks %uK",
                        kBulletCapacity, kBulletChunkCount,
                        static_cast<unsigned int>(sizeof(Bullet) * kBulletCapacity / 1024u));
    return true;
}

void BulletManager::PspReleaseBulletPool()
{
    for (i32 i = 0; i < kBulletChunkCount; i++)
    {
        this->bulletChunks[i] = nullptr;
    }
    this->pspNextBulletIndex = 0;
    memset(this->pspActiveBulletBits, 0, sizeof(this->pspActiveBulletBits));
}
#endif

AnmVm *Bullet::SpawnEffectVm(u16 spawnState)
{
#if defined(TH07_PSP_1000)
    (void)spawnState;
    return &this->sprites.spriteSpawnEffect;
#else
    switch (spawnState)
    {
    case BULLET_SPAWNING_NORMAL:
        return &this->sprites.spriteSpawnEffectNormal;
    case BULLET_SPAWNING_SLOW:
        return &this->sprites.spriteSpawnEffectSlow;
    case BULLET_SPAWNING_FAST:
    default:
        return &this->sprites.spriteSpawnEffectFast;
    }
#endif
}

void Bullet::AssignTypeSprites(const BulletTypeSprites &source)
{
#if defined(TH07_PSP_1000)
    // ECL instructions can retheme an already-active bullet. Preserve the VM
    // which corresponds to its current mutually-exclusive spawn state; the
    // common runtime VM is irrelevant once the bullet reaches NORMAL.
    this->sprites.spriteBullet = source.spriteBullet;
    this->sprites.spriteSpawnEffectDonut = source.spriteSpawnEffectDonut;
    switch (this->state)
    {
    case BULLET_SPAWNING_NORMAL:
        this->sprites.spriteSpawnEffect = source.spriteSpawnEffectNormal;
        break;
    case BULLET_SPAWNING_SLOW:
        this->sprites.spriteSpawnEffect = source.spriteSpawnEffectSlow;
        break;
    case BULLET_SPAWNING_FAST:
    default:
        this->sprites.spriteSpawnEffect = source.spriteSpawnEffectFast;
        break;
    }
    this->sprites.grazeSize = source.grazeSize;
    this->sprites.unused_b88 = source.unused_b88;
    this->sprites.bulletHeight = source.bulletHeight;
    this->sprites.collisionType = source.collisionType;
#else
    this->sprites = source;
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
    this->sprites.pspQuiescentAnm =
        PspClassifyQuiescentBulletAnm(&this->sprites.spriteBullet) ? 1u : 0u;
#endif
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    g_BulletManager.PspInvalidateBulletStaticProxy(this);
#endif
}

void BulletManager::SetActiveBulletSprite(Bullet *bullet, i32 spriteIdx)
{
    if (!bullet)
    {
        return;
    }
    g_AnmManager->SetActiveSprite(&bullet->sprites.spriteBullet, spriteIdx);
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    g_BulletManager.PspInvalidateBulletStaticProxy(bullet);
#endif
}

void BulletManager::SetActiveSpriteByResolution(AnmVm *sprite, AnmVm *bulletTypeTemplate,
                                                Bullet *bullet, i32 spriteOffset)
{
    if (sprite->activeSpriteIdx != bulletTypeTemplate->activeSpriteIdx + spriteOffset)
    {
        if (bullet->sprites.spriteBullet.sprite->heightPx <= 16.0f)
        {
            if (sprite == &bullet->sprites.spriteBullet)
            {
                SetActiveBulletSprite(
                    bullet, bulletTypeTemplate->activeSpriteIdx +
                                g_BulletSpriteOffset16Px[spriteOffset]);
            }
            else
            {
                g_AnmManager->SetActiveSprite(
                    sprite, bulletTypeTemplate->activeSpriteIdx +
                                g_BulletSpriteOffset16Px[spriteOffset]);
            }
        }
        else
        {
            if (bullet->sprites.spriteBullet.sprite->heightPx <= 32.0f)
            {
                if (sprite == &bullet->sprites.spriteBullet)
                {
                    SetActiveBulletSprite(
                        bullet, bulletTypeTemplate->activeSpriteIdx +
                                    g_BulletSpriteOffset32Px[spriteOffset]);
                }
                else
                {
                    g_AnmManager->SetActiveSprite(
                        sprite, bulletTypeTemplate->activeSpriteIdx +
                                    g_BulletSpriteOffset32Px[spriteOffset]);
                }
            }
            else
            {
                if (sprite == &bullet->sprites.spriteBullet)
                {
                    SetActiveBulletSprite(
                        bullet, bulletTypeTemplate->activeSpriteIdx + spriteOffset);
                }
                else
                {
                    g_AnmManager->SetActiveSprite(
                        sprite, bulletTypeTemplate->activeSpriteIdx + spriteOffset);
                }
            }
        }
    }
}

i32 BulletManager::SpawnSingleBullet(EnemyBulletShooter *bulletProps, i32 x, i32 y, f32 angle)
{
    f32 bulletAngle;
    Bullet *bullet;
    i32 i;
    f32 bulletSpeed;

    i32 bulletIndex =
#if defined(TH07_PSP_1000)
        this->pspNextBulletIndex;
#else
        static_cast<i32>(this->bulletsStart - this->bullets);
#endif
    for (i = 0; i < kBulletCapacity; i++)
    {
        bullet = this->BulletAt(bulletIndex);
#if defined(TH07_PSP)
        if (!this->PspIsBulletSlotTracked(bulletIndex))
        {
            break;
        }
        if (bullet->state == BULLET_INACTIVE)
        {
            this->PspForgetBulletSlot(bulletIndex);
            break;
        }
#else
        if (bullet->state == BULLET_INACTIVE)
        {
            break;
        }
#endif
        bulletIndex++;
        if (bulletIndex >= kBulletCapacity)
            bulletIndex = 0;
    }
    if (i >= kBulletCapacity)
    {
        return 1;
    }

    bulletAngle = 0.0f;
    if (bulletProps->count2 > 1)
    {
        bulletSpeed = bulletProps->speed1 - (bulletProps->speed1 - bulletProps->speed2) * (f32)y /
                                                (f32)(i32)bulletProps->count2;
    }
    else
    {
        bulletSpeed = bulletProps->speed1;
    }
    switch (bulletProps->aimMode)
    {
    case 0:
    case 1:
        if ((bulletProps->count1 & 1U) != 0)
        {
            bulletAngle += bulletProps->angle2 * (f32)((i32)((x + 1) / 2));
        }
        else
        {
            bulletAngle += (f32)(i32)(x / 2) * bulletProps->angle2 + bulletProps->angle2 * 0.5f;
        }
        if ((x & 1U) != 0)
        {
            bulletAngle *= -1.0f;
        }
        if (bulletProps->aimMode == 0)
        {
            bulletAngle += angle;
        }
        bulletAngle += bulletProps->angle1;
        break;
    case 2:
        bulletAngle += angle;
    case 3:
        bulletAngle += (f32)x * ZUN_2PI / (f32)(i32)bulletProps->count1;
        bulletAngle += (f32)y * bulletProps->angle2 + bulletProps->angle1;
        break;
    case 4:
        bulletAngle += angle;
    case 5:
        bulletAngle += ZUN_PI / (f32)(i32)bulletProps->count1;
        bulletAngle += (f32)x * ZUN_2PI / (f32)(i32)bulletProps->count1;
        bulletAngle += bulletProps->angle1;
        break;
    case 6:
        bulletAngle = g_Rng.GetRandomFloatInRange(bulletProps->angle1 - bulletProps->angle2) +
                      bulletProps->angle2;
        break;
    case 7:
        bulletSpeed = g_Rng.GetRandomFloatInRange(bulletProps->speed1 - bulletProps->speed2) +
                      bulletProps->speed2;
        bulletAngle += (f32)x * ZUN_2PI / (f32)(i32)bulletProps->count1;
        bulletAngle += (f32)y * bulletProps->angle2 + bulletProps->angle1;
        break;
    case 8:
        bulletAngle = g_Rng.GetRandomFloatInRange(bulletProps->angle1 - bulletProps->angle2) +
                      bulletProps->angle2;
        bulletSpeed = g_Rng.GetRandomFloatInRange(bulletProps->speed1 - bulletProps->speed2) +
                      bulletProps->speed2;
    }
    bullet->state = BULLET_NORMAL;
#if defined(TH07_PSP)
    this->PspTrackBulletSlot(bulletIndex);
#endif
    bullet->spawned = 1;
    bullet->grazed = 0;
    bullet->timer1 = 0;
    bullet->timer2 = 0;
    bullet->speed = bulletSpeed;
    bullet->angle = utils::AddNormalizeAngle(bulletAngle, 0.0f);
    bullet->pos = bulletProps->position;
    bullet->pos.z = 0.1f;
    AngleToVector(&bullet->velocity, bulletAngle,
                  bulletSpeed * g_Supervisor.effectiveFramerateMultiplier);
    bullet->exFlags = (i16)bulletProps->flags;
    bullet->spriteOffset = bulletProps->spriteOffset;
    bullet->state2 = 0;
#if defined(TH07_PSP)
    bullet->pspRenderRotationValid = 0;
#endif
    AnmVm::AssignVm(&bullet->sprites.spriteBullet, &bulletProps->sprites->spriteBullet);
    AnmVm::AssignVm(&bullet->sprites.spriteSpawnEffectDonut,
                    &bulletProps->sprites->spriteSpawnEffectDonut);
    bullet->sprites.grazeSize = bulletProps->sprites->grazeSize;
    bullet->sprites.unused_b88 = bulletProps->sprites->unused_b88;
    bullet->sprites.bulletHeight = bulletProps->sprites->bulletHeight;
    bullet->sprites.collisionType = bulletProps->sprites->collisionType;
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
    bullet->sprites.pspQuiescentAnm =
        PspClassifyQuiescentBulletAnm(&bullet->sprites.spriteBullet) ? 1u : 0u;
#endif
    bullet->soundIdx = bulletProps->soundOverride;
    bullet->spawnDelay = 0;
    if ((i32)bullet->sprites.spriteBullet.activeSpriteIdx !=
        (i32)bulletProps->sprites->spriteBullet.activeSpriteIdx + (i32)bulletProps->spriteOffset)
    {
        SetActiveBulletSprite(
            bullet, (i32)bulletProps->sprites->spriteBullet.activeSpriteIdx +
                        (i32)bulletProps->spriteOffset);
    }
    if ((i32)bullet->sprites.spriteSpawnEffectDonut.activeSpriteIdx !=
        (i32)bulletProps->sprites->spriteSpawnEffectDonut.activeSpriteIdx +
            (i32)bulletProps->spriteOffset)
    {
        if (bullet->sprites.spriteBullet.sprite->heightPx <= 16.0f)
        {
            g_AnmManager->SetActiveSprite(
                &bullet->sprites.spriteSpawnEffectDonut,
                (i32)bulletProps->sprites->spriteSpawnEffectDonut.activeSpriteIdx +
                    g_BulletSpriteOffset16Px[bulletProps->spriteOffset]);
        }
        else
        {
            if (bullet->sprites.spriteBullet.sprite->heightPx <= 32.0f)
            {
                g_AnmManager->SetActiveSprite(
                    &bullet->sprites.spriteSpawnEffectDonut,
                    (i32)bulletProps->sprites->spriteSpawnEffectDonut.activeSpriteIdx +
                        g_BulletSpriteOffset32Px[bulletProps->spriteOffset]);
            }
            else
            {
                g_AnmManager->SetActiveSprite(
                    &bullet->sprites.spriteSpawnEffectDonut,
                    (i32)bulletProps->sprites->spriteSpawnEffectDonut.activeSpriteIdx +
                        (i32)bulletProps->spriteOffset);
            }
        }
    }

    if (bulletProps->flags & 2)
    {
        AnmVm *spawnEffect = bullet->SpawnEffectVm(BULLET_SPAWNING_FAST);
#if defined(TH07_PSP_1000)
        *spawnEffect = bulletProps->sprites->spriteSpawnEffectFast;
#else
        AnmVm::AssignVm(spawnEffect, &bulletProps->sprites->spriteSpawnEffectFast);
#endif
        SetActiveSpriteByResolution(spawnEffect,
                                    &bulletProps->sprites->spriteSpawnEffectFast, bullet,
                                    bulletProps->spriteOffset);
        bullet->state = BULLET_SPAWNING_FAST;
        bullet->pos -= bullet->velocity * 4.0f;
    }
    else if (bulletProps->flags & 4)
    {
        AnmVm *spawnEffect = bullet->SpawnEffectVm(BULLET_SPAWNING_NORMAL);
#if defined(TH07_PSP_1000)
        *spawnEffect = bulletProps->sprites->spriteSpawnEffectNormal;
#else
        AnmVm::AssignVm(spawnEffect, &bulletProps->sprites->spriteSpawnEffectNormal);
#endif
        SetActiveSpriteByResolution(spawnEffect,
                                    &bulletProps->sprites->spriteSpawnEffectNormal, bullet,
                                    (i32)bulletProps->spriteOffset);
        bullet->state = BULLET_SPAWNING_NORMAL;
        bullet->pos -= bullet->velocity * 4.0f;
    }
    else if (bulletProps->flags & 8)
    {
        AnmVm *spawnEffect = bullet->SpawnEffectVm(BULLET_SPAWNING_SLOW);
#if defined(TH07_PSP_1000)
        *spawnEffect = bulletProps->sprites->spriteSpawnEffectSlow;
#else
        AnmVm::AssignVm(spawnEffect, &bulletProps->sprites->spriteSpawnEffectSlow);
#endif
        SetActiveSpriteByResolution(spawnEffect,
                                    &bulletProps->sprites->spriteSpawnEffectSlow, bullet,
                                    (i32)bulletProps->spriteOffset);
        bullet->state = BULLET_SPAWNING_SLOW;
        bullet->pos -= bullet->velocity * 4.0f;
    }
    memcpy(bullet->commands, bulletProps->commands, sizeof(bullet->commands));
    bullet->moreFlags = bulletProps->flags;
    bullet->exFlags = 0;
    bullet->curCmdIdx = 0;
    bullet->RunCommands();
    if (this->screenClearTime != 0 && (bullet->moreFlags & 0x1000) == 0)
    {
        bullet->state = BULLET_DESPAWN;
    }
    bulletIndex++;
    if (bulletIndex >= kBulletCapacity)
        bulletIndex = 0;
#if defined(TH07_PSP_1000)
    this->pspNextBulletIndex = bulletIndex;
#else
    this->bulletsStart = this->BulletAt(bulletIndex);
#endif
    return 0;
}

void Bullet::RunCommands()
{
    BulletCommand *cmd;

    for (;;)
    {
        if (this->curCmdIdx >= 5)
        {
            return;
        }

        cmd = &this->commands[this->curCmdIdx];
        if (cmd->type == 0)
        {
            return;
        }
        if (cmd->flag == 0 && this->exFlags != 0)
        {
            return;
        }
        if (((u32)this->moreFlags & cmd->type) == 0)
        {
            this->curCmdIdx++;
            continue;
        }

        switch (cmd->type)
        {
        case 1:
            this->exFlags |= 1;
            this->commandStates[0].timer = 0;
            this->commandStates[0].vec3.z = 0.0f;
            break;
        case 0x10:
            this->exFlags |= 0x10;
            this->commandStates[1].speed = cmd->speed;
            this->commandStates[1].angle = cmd->angle > -990.0f ? cmd->angle : this->angle;
            this->commandStates[1].timer = 0;
            this->commandStates[1].duration = cmd->duration;
            AngleToVector(&this->commandStates[1].vec3, this->commandStates[1].angle,
                          g_Supervisor.effectiveFramerateMultiplier * this->commandStates[1].speed);
            if (this->curCmdIdx != 0 && this->soundIdx >= 0)
            {
                g_SoundPlayer.PlaySoundByIdx(this->soundIdx, 0);
            }
            break;
        case 0x20:
            this->exFlags |= 0x20;
            this->commandStates[2].speed = cmd->speed;
            this->commandStates[2].angle = cmd->angle;
            this->commandStates[2].timer = 0;
            this->commandStates[2].duration = cmd->duration;
            if (this->curCmdIdx != 0 && this->soundIdx >= 0)
            {
                g_SoundPlayer.PlaySoundByIdx(this->soundIdx, 0);
            }
            break;
        case 0x40:
        case 0x80:
        case 0x100:
            this->exFlags |= cmd->type;
            // ZUN quirk: Using the BulletCommand's speed for BulletCommandState's angle?
            this->commandStates[3].angle = cmd->speed;
            this->commandStates[3].speed = cmd->angle > -999.0f ? cmd->angle : this->speed;
            this->commandStates[3].timer = 0;
            this->commandStates[3].duration = cmd->duration;
            this->commandStates[3].maxTimes = cmd->loopCount;
            this->commandStates[3].minTimes = 0;
            break;
        case 0x400:
        case 0x800:
            this->exFlags |= cmd->type;
            if (cmd->speed >= 0.0f)
            {
                this->commandStates[4].speed = cmd->speed;
            }
            else
            {
                this->commandStates[4].speed = this->speed;
            }
            this->commandStates[4].maxTimes = cmd->duration;
            this->commandStates[4].duration = 0;
            break;
        case 0x2000:
            this->spawnDelay = cmd->duration;
            this->curCmdIdx++;
            continue;
        }
        this->curCmdIdx++;
        return;
    }
}

void BulletManager::RemoveAllBullets(i32 param_1)
{
    f32 local_28;
    f32 local_24;
    Laser *laser;
    Bullet *bullet;
    f32 local_18;
    i32 i;
    ZunVec3 local_10;

#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    this->PspMarkMeRenderMutation();
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    // Current MsgRead callers run before calc 12, but keep this future-proof:
    // some modes only change state to DESPAWN and never touch the occupancy
    // bitmap, so every public bulk mutation advances the publication epoch.
    this->PspMarkBulletMutation();
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    this->PspMarkBulletStaticProxyMutation();
#endif

    for (i = 0; i < kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
#if defined(TH07_PSP)
        if (!this->PspIsBulletSlotTracked(i))
        {
            continue;
        }
#endif
        if (bullet->state == BULLET_INACTIVE || bullet->state == BULLET_DESPAWN)
        {
            continue;
        }
        if (param_1 != 0 && param_1 < 9)
        {
            if (param_1 < 3)
            {
                g_ItemManager.SpawnItem(&bullet->pos, this->itemType, param_1);
            }
            else
            {
                g_ItemManager.SpawnItem(&bullet->pos, ITEM_CHERRY_SMALL, 1);
            }
            memset(bullet, 0, sizeof(Bullet));
#if defined(TH07_PSP)
            this->PspForgetBulletSlot(i);
#endif
        }
        else
        {
            bullet->state = BULLET_DESPAWN;
        }
    }
    laser = this->lasers;
    for (i = 0; i < 64; i++, laser++)
    {
        if (!laser->inUse)
        {
            continue;
        }
        if ((laser->flags & 4) != 0 && param_1 != 10)
        {
            continue;
        }

        if (laser->state < LASER_DESPAWNING)
        {
            laser->state = LASER_DESPAWNING;
            laser->timer = 0;
            laser->width = laser->targetWidth;
            if (param_1 != 0 && param_1 < 9)
            {
                local_28 = laser->startOffset;
                sincosf(&local_18, &local_24, laser->angle);
                while (laser->endOffset > local_28)
                {
                    local_10.x = local_24 * local_28 + laser->pos.x;
                    local_10.y = local_18 * local_28 + laser->pos.y;
                    local_10.z = 0.0f;
                    if (param_1 < 3)
                    {
                        g_ItemManager.SpawnItem(&local_10, this->itemType, param_1);
                    }
                    else
                    {
                        g_ItemManager.SpawnItem(&local_10, ITEM_CHERRY_SMALL, 1);
                    }
                    local_28 += 32.0f;
                }
            }
        }
        laser->hitboxEndTime = 0;
    }
    this->screenClearTime = 10;
}

i32 BulletManager::DespawnBullets(i32 param_1, i32 turnIntoItem)
{
    f32 local_34;
    f32 local_30;
    Laser *laser;
    ZunVec3 local_28;
    Bullet *bullet;
    f32 local_18;
    i32 i;
    i32 local_c;
    i32 local_8;

    local_c = 0;
    local_8 = 2000;
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    this->PspMarkMeRenderMutation();
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    this->PspMarkBulletMutation();
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    this->PspMarkBulletStaticProxyMutation();
#endif
#if defined(TH07_PSP)
    // A dense cancel can otherwise leave hundreds of multi-digit labels alive
    // for a full second.  Score, point items and bullet state are independent;
    // cap only these redundant labels, matching the proven TH06 PSP policy.
    unsigned int activeBullets = 0;
    for (unsigned int bulletIdx = 0; bulletIdx < static_cast<unsigned int>(kBulletCapacity);
         ++bulletIdx)
    {
        if (g_BulletManager.PspIsBulletSlotTracked(static_cast<i32>(bulletIdx)) &&
            g_BulletManager.BulletAt(static_cast<i32>(bulletIdx))->state != BULLET_INACTIVE)
        {
            ++activeBullets;
        }
    }
    constexpr unsigned int kMassPopupLimit = 48;
    const unsigned int popupStride =
        activeBullets > kMassPopupLimit
            ? (activeBullets + kMassPopupLimit - 1u) / kMassPopupLimit
            : 1u;
    unsigned int popupIndex = 0;
#endif
    for (i = 0; i < kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
#if defined(TH07_PSP)
        if (!this->PspIsBulletSlotTracked(i))
        {
            continue;
        }
#endif
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        g_ItemManager.SpawnItem(&bullet->pos, this->itemType, 1);
#if defined(TH07_PSP)
        if ((popupIndex++ % popupStride) == 0u)
#endif
        {
            g_AsciiManager.CreatePopup1(&bullet->pos, local_8,
                                        local_8 >= param_1 ? 0xFFFFFF00 : 0xFFFFFFFF);
        }
        local_c += local_8;
        local_8 += 20;
        if (local_8 > param_1)
        {
            local_8 = param_1;
        }
        bullet->state = BULLET_DESPAWN;
    }
    laser = this->lasers;
    for (i = 0; i < 64; i++, laser++)
    {
        if (!laser->inUse)
        {
            continue;
        }
        if (laser->state < LASER_DESPAWNING)
        {
            laser->state = LASER_DESPAWNING;
            laser->timer = 0;
            laser->width = laser->targetWidth;
            if (turnIntoItem)
            {
                g_ItemManager.SpawnItem(&laser->pos, this->itemType, 1);
                local_34 = laser->startOffset;
                sincosf(&local_18, &local_30, laser->angle);
                while (laser->endOffset > local_34)
                {
                    local_28.x = local_30 * local_34 + laser->pos.x;
                    local_28.y = local_18 * local_34 + laser->pos.y;
                    local_28.z = 0.0f;
                    g_ItemManager.SpawnItem(&local_28, this->itemType, 1);
                    local_34 += 32.0f;
                }
            }
        }
        laser->hitboxEndTime = 0;
    }
    this->screenClearTime = 10;
    return local_c;
}

void BulletManager::RemoveBulletsInRadius(ZunVec3 *centerPos, f32 radius)
{
    ZunVec3 diff;
    Bullet *bullet;
    i32 i;

#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    this->PspMarkMeRenderMutation();
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    this->PspMarkBulletMutation();
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    this->PspMarkBulletStaticProxyMutation();
#endif

    radius *= radius;
    for (i = 0; i < kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
#if defined(TH07_PSP)
        if (!this->PspIsBulletSlotTracked(i))
        {
            continue;
        }
#endif
        if (bullet->state == BULLET_INACTIVE || bullet->state == BULLET_DESPAWN)
        {
            continue;
        }

        diff = bullet->pos - *centerPos;

        if (diff.LengthSq() > radius)
        {
            continue;
        }

        g_ItemManager.SpawnItem(&bullet->pos, ITEM_POINT_BULLET, 1);
        memset(bullet, 0, sizeof(Bullet));
#if defined(TH07_PSP)
        this->PspForgetBulletSlot(i);
#endif
    }
}

i32 BulletManager::SpawnBulletPattern(EnemyBulletShooter *bulletProps)
{
    f32 angle;
    i32 x;
    i32 y;

    if (g_BulletManager.bulletCount >= kBulletCapacity)
    {
        return 0;
    }

    bulletProps->sprites = this->bulletTypeTemplates + bulletProps->sprite;
    angle = g_Player.AngleToPlayer(&bulletProps->position);
    for (x = 0; x < bulletProps->count2; x++)
    {
        for (y = 0; y < bulletProps->count1; y++)
        {
            if (SpawnSingleBullet(bulletProps, y, x, angle))
            {
                goto stop;
            }
        }
    }
stop:
    if ((bulletProps->flags & 0x200) != 0)
    {
        g_SoundPlayer.PlaySoundByIdx(bulletProps->soundIdx, 0);
    }
    return 0;
}

Laser *BulletManager::SpawnLaserPattern(EnemyLaserShooter *laserShooter)
{
    Laser *laser;
    i32 i;

    laser = this->lasers;
    if (this->screenClearTime != 0 && (laserShooter->flags & 4) == 0)
    {
        return laser;
    }

    for (i = 0; i < 64; i++, laser++)
    {
        if (laser->inUse)
        {
            continue;
        }

        g_AnmManager->SetAnmIdxAndExecuteScript(&laser->vm0, laserShooter->sprite + 522);
        g_AnmManager->SetActiveSprite(&laser->vm0, (i32)laser->vm0.activeSpriteIdx +
                                                       (i32)laserShooter->spriteOffset);
        g_AnmManager->InitializeAndSetActiveSprite(
            &laser->vm1, g_BulletSpriteOffset16Px[laserShooter->spriteOffset] + 658);
        laser->vm1.blendMode = 1;
        laser->pos = laserShooter->position;
        laser->color = laserShooter->spriteOffset;
        laser->inUse = 1;
        laser->angle = laserShooter->angle1;
        if (laserShooter->type == 0)
        {
            laser->angle = g_Player.AngleToPlayer(&laserShooter->position) + laser->angle;
        }
        laser->flags = laserShooter->flags;
        laser->timer = 0;
        laser->startOffset = laserShooter->startOffset;
        laser->endOffset = laserShooter->endOffset;
        laser->startLength = laserShooter->startLength;
        laser->width = laserShooter->width;
        laser->speed = laserShooter->speed1;
        laser->startTime = laserShooter->startTime;
        laser->duration = laserShooter->duration;
        laser->endTime = laserShooter->endTime;
        laser->hitboxStartTime = laserShooter->hitboxStartTime;
        laser->hitboxEndTime = laserShooter->hitboxEndTime;
        laser->hideWarning = 0;
        if (laser->startTime == 0)
        {
            laser->state = LASER_ACTIVE;
            break;
        }
        laser->state = LASER_SPAWNING;
        break;
    }
    return laser;
}

void Bullet::UpdateBulletBurstSpeed()
{
    if (this->commandStates[0].timer <= 16)
    {
        f32 local_8 = 5.0f - this->commandStates[0].timer.AsFloat() * 5.0f / 16.0f;
        AngleToVector(&this->velocity, this->angle,
                      (local_8 + this->speed) * g_Supervisor.effectiveFramerateMultiplier);
    }
    else
    {
        this->exFlags ^= 1;
    }
    this->commandStates[0].timer++;
}

void Bullet::UpdateBulletTargetVelocity()
{
    if (this->commandStates[1].timer >= this->commandStates[1].duration)
    {
        this->exFlags = this->exFlags & 0xffffffef;
    }
    else
    {
        this->velocity += this->commandStates[1].vec3 * g_Supervisor.effectiveFramerateMultiplier;
        if (fabsf(this->velocity.x) > 0.0001f || fabsf(this->velocity.y) > 0.0001f)
        {
            this->angle = atan2f(this->velocity.y, this->velocity.x);
        }
    }
    this->commandStates[1].timer++;
}

void Bullet::UpdateBulletTargetAngle()
{
    if (this->commandStates[2].timer >= this->commandStates[2].duration)
    {
        this->exFlags = this->exFlags & 0xffffffdf;
    }
    else
    {
        this->angle = utils::AddNormalizeAngle(
            this->angle, this->commandStates[2].angle * g_Supervisor.effectiveFramerateMultiplier);
        this->speed += this->commandStates[2].speed * g_Supervisor.effectiveFramerateMultiplier;
        AngleToVector(&this->velocity, this->angle,
                      this->speed * g_Supervisor.effectiveFramerateMultiplier);
    }
    this->commandStates[2].timer++;
}

void Bullet::UpdateBulletDirChangeAndResume()
{
    f32 local_8;

    if (this->commandStates[3].timer >= this->commandStates[3].duration)
    {
        if (this->soundIdx >= 0)
        {
            g_SoundPlayer.PlaySoundByIdx(this->soundIdx, 0);
        }
        this->commandStates[3].minTimes++;
        if (this->commandStates[3].minTimes >= this->commandStates[3].maxTimes)
        {
            this->exFlags = this->exFlags & 0xffffffbf;
        }
        this->angle += this->commandStates[3].angle;
        this->speed = this->commandStates[3].speed;
        local_8 = this->speed;
        this->commandStates[3].timer = 0;
    }
    else
    {
        local_8 = this->speed - this->commandStates[3].timer.AsFloat() * this->speed /
                                    (f32)this->commandStates[3].duration;
    }
    AngleToVector(&this->velocity, this->angle,
                  local_8 * g_Supervisor.effectiveFramerateMultiplier);
    this->commandStates[3].timer++;
}

void Bullet::UpdateBulletDirChangeAbsoluteAndResume()
{
    f32 local_8;

    if (this->commandStates[3].timer >= this->commandStates[3].duration)
    {
        if (this->soundIdx >= 0)
        {
            g_SoundPlayer.PlaySoundByIdx(this->soundIdx, 0);
        }
        this->commandStates[3].minTimes++;
        if (this->commandStates[3].minTimes >= this->commandStates[3].maxTimes)
        {
            this->exFlags = this->exFlags & 0xfffffeff;
        }
        this->angle = this->commandStates[3].angle;
        this->speed = this->commandStates[3].speed;
        local_8 = this->speed;
        this->commandStates[3].timer = 0;
    }
    else
    {
        local_8 = this->speed - this->commandStates[3].timer.AsFloat() * this->speed /
                                    (f32)this->commandStates[3].duration;
    }
    AngleToVector(&this->velocity, this->angle,
                  local_8 * g_Supervisor.effectiveFramerateMultiplier);
    this->commandStates[3].timer++;
}

void Bullet::UpdateBulletDirChangeAimAtPlayer()
{
    f32 local_8;

    if (this->commandStates[3].timer >= this->commandStates[3].duration)
    {
        if (this->soundIdx >= 0)
        {
            g_SoundPlayer.PlaySoundByIdx(this->soundIdx, 0);
        }
        this->commandStates[3].minTimes++;
        if (this->commandStates[3].minTimes >= this->commandStates[3].maxTimes)
        {
            this->exFlags = this->exFlags & 0xffffff7f;
        }
        this->angle = utils::AddNormalizeAngle(g_Player.AngleToPlayer(&this->pos),
                                               this->commandStates[3].angle);
        this->speed = this->commandStates[3].speed;
        local_8 = this->speed;
        this->commandStates[3].timer = 0;
    }
    else
    {
        local_8 = this->speed - this->commandStates[3].timer.AsFloat() * this->speed /
                                    (f32)this->commandStates[3].duration;
    }
    AngleToVector(&this->velocity, this->angle,
                  local_8 * g_Supervisor.effectiveFramerateMultiplier);
    this->commandStates[3].timer++;
}

void Bullet::UpdateBulletBounce()
{
    f32 speed;

    if (g_GameManager.IsInBounds(this->pos.x, this->pos.y,
                                 this->sprites.spriteBullet.sprite->widthPx,
                                 this->sprites.spriteBullet.sprite->heightPx) == 0)
    {
        if (this->soundIdx >= 0)
        {
            g_SoundPlayer.PlaySoundByIdx(this->soundIdx, 0);
        }
        if (this->pos.x < 0.0f || this->pos.x >= 384.0f)
        {
            this->angle = -this->angle - ZUN_PI;
            this->angle = utils::AddNormalizeAngle(this->angle, 0.0f);
        }
        if (this->pos.y < 0.0f || (this->pos.y >= 448.0f && (this->exFlags & 0x400U) != 0))
        {
            this->angle = -this->angle;
        }
        this->speed = this->commandStates[4].speed;
        speed = this->speed;
        AngleToVector(&this->velocity, this->angle,
                      speed * g_Supervisor.effectiveFramerateMultiplier);
        this->commandStates[4].duration++;
        if (this->commandStates[4].duration >= this->commandStates[4].maxTimes)
        {
            this->exFlags = this->exFlags & 0xfffff3ff;
        }
    }
}

u32 BulletManager::OnUpdate(BulletManager *arg)
{
    ZunVec3 laserCenter;
    Laser *laser;
    i32 alpha;
    Bullet *bullet;
    ZunVec3 laserHitbox;
    i32 blockIdx;
    f32 width;
    i32 i;
    i32 collisionRes;
    bool bombCollisionChecked;
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    const Th07PspMeBulletFastOutput *pspMeBulletFastOutput = nullptr;
    u16 pspMeBulletFastFlags = 0u;
    i32 pspMeBulletFastInBounds = 0;
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    const Th07PspMeBulletCompactSeed *pspMeBulletCompactSeed = nullptr;
    const Th07PspMeBulletCompactOutput *pspMeBulletCompactOutput = nullptr;
    u16 pspMeBulletCompactFlags = 0u;
    i32 pspMeBulletCompactInBounds = 0;
    bool pspMeBulletCompactCollisionAllowed = false;
    u32 pspMeBulletCompactMotionHits = 0u;
    u32 pspMeBulletCompactBoundsHits = 0u;
    u32 pspMeBulletCompactCollisionHits = 0u;
    u32 pspMeBulletCompactBroadphaseHits = 0u;
    u32 pspMeBulletCompactJitRejects = 0u;
    u32 pspMeBulletCompactCollisionLatches = 0u;
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
    bool pspMeBulletCompactTrustedAuthority = false;
    bool pspMeBulletCompactTrustedMotion = false;
#endif
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    const bool pspDenseActive = gTh07PspPerfDenseSliceActive != 0;
    const unsigned long long pspDenseUpdateStartUs =
        pspDenseActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
    u32 pspQuiescentEligible = 0;
    u32 pspQuiescentHits = 0;
    u32 pspQuiescentFallbacks = 0;
    u32 pspQuiescentInvalidations = 0;
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    bool pspWarmQueueBuilding = PspBeginBulletWarmQueue(arg);
    bool pspWarmQueueComplete = pspWarmQueueBuilding;
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    PspMeRenderBeginFusedCapture(arg);
#endif

    blockIdx = 0;
    if (g_GameManager.isTimeStopped)
    {
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        // A SELECT pause still advances the calc chain. Observe/release the
        // asynchronous owner before returning, but never wait in gameplay.
        PspMeBulletCompactFinishFrame(arg);
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        if (pspDenseActive)
        {
            const unsigned long long endUs = sceKernelGetSystemTimeWide();
            gPspDenseSliceWindow.updateCallbackUs += endUs - pspDenseUpdateStartUs;
            gPspDenseSliceWindow.updateTailUs += endUs - pspDenseUpdateStartUs;
            ++gPspDenseSliceWindow.updateFrames;
        }
#endif
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    // A1 is opportunistic and never waits.  If command 12 has not completed
    // at the canonical Item boundary, this entire Item pass remains RID29 SC.
    if (gPspMeBulletCompactSc.pending)
    {
        if ((gPspMeBulletCompactSc.job.flags &
             TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u)
        {
            ++gPspMeRenderShadowWindow.compactItemMotionPendingAtItem;
        }
        (void)PspMeBulletCompactPollForUpdate(arg);
    }
    if (gPspMeBulletCompactSc.currentItemMotionValid)
    {
        PspSetMeItemMotionView(
            &gPspMeBulletCompactSc.job,
            gPspMeBulletCompactSc.itemMotionSeed,
            gPspMeBulletCompactSc.itemMotionOutput);
    }
#endif
    g_ItemManager.OnUpdate();
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    {
        u32 active = 0u;
        u32 candidates = 0u;
        u32 adopted = 0u;
        u32 slotRejects = 0u;
        u32 globalRejects = 0u;
        PspTakeMeItemMotionFrameStats(
            &active, &candidates, &adopted, &slotRejects, &globalRejects);
        (void)active;
        gPspMeRenderShadowWindow.compactItemMotionJitCandidates +=
            candidates;
        gPspMeRenderShadowWindow.compactItemMotionAdopted += adopted;
        gPspMeRenderShadowWindow.compactItemMotionSlotReject += slotRejects;
        gPspMeRenderShadowWindow.compactItemMotionGlobalReject +=
            globalRejects;
    }
    PspClearMeItemMotionView();
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    const unsigned long long pspDenseItemEndUs =
        pspDenseActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    // Item update may synchronously clear/reuse Bullet slots.  Publish only
    // after it returns, then synchronously retire the read-only ME traversal
    // before this callback performs the first Bullet write.  No live reader
    // is permitted to cross the priority-12 callback boundary.
    pspMeBulletFastOutput = PspMeBulletFastRunSynchronous(arg);
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    // Item update is the last priority-12 owner allowed to reuse Bullet slots.
    // Probe exactly once here, before the first Bullet write. Seed-only motion
    // remains valid when the collision command is still running.
    const int pspMeBulletCompactHeadPoll =
        PspMeBulletCompactPollForUpdate(arg);
    if (pspMeBulletCompactHeadPoll == 0)
    {
        ++gPspMeRenderShadowWindow.compactP12HeadPending;
    }
    const PspMeBulletCompactIdentity &pspMeBulletCompactIdentity =
        gPspMeBulletCompactSc.currentSeedIdentity;
    if (gPspMeBulletCompactSc.currentSeedValid &&
        pspMeBulletCompactIdentity.managerUpdateCount == arg->updateCount &&
        pspMeBulletCompactIdentity.targetDrawSeq ==
            gPspMeRenderShadow.drawSeq &&
        pspMeBulletCompactIdentity.stageEpoch ==
            gPspMeRenderShadow.stageEpoch &&
        pspMeBulletCompactIdentity.managerEpoch ==
            gPspMeRenderShadow.managerEpoch &&
        pspMeBulletCompactIdentity.managerMutationEpoch ==
            arg->pspMeRenderMutationEpoch)
    {
        pspMeBulletCompactSeed = gPspMeBulletCompactSc.seed;
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
        // Latch authority once before the first Bullet write.  Changes made by
        // this callback itself are ordered and slot-local; no other calc owner
        // can run concurrently with this loop.
        pspMeBulletCompactTrustedAuthority =
            pspMeBulletCompactSeed != nullptr;
#endif
        if (gPspMeBulletCompactSc.currentOutputValid)
        {
            pspMeBulletCompactOutput = gPspMeBulletCompactSc.output;
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
            pspMeBulletCompactCollisionAllowed =
                pspMeBulletCompactOutput != nullptr &&
                PspMeBulletCompactPlayerSnapshotMatches(
                    gPspMeBulletCompactSc.job);
            if (pspMeBulletCompactOutput &&
                !pspMeBulletCompactCollisionAllowed)
            {
                ++pspMeBulletCompactCollisionLatches;
            }
#else
            pspMeBulletCompactCollisionAllowed =
                pspMeBulletCompactOutput != nullptr &&
                (gPspMeBulletCompactSc.job.flags &
                 TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID) !=
                    0u;
#endif
        }
        else if (pspMeBulletCompactSeed)
        {
            ++gPspMeRenderShadowWindow.compactSeedOnlyFrames;
        }
    }
#endif
    arg->bulletCount = 0;
    arg->bulletsPtrs[5] = NULL;
    arg->bulletsPtrs[4] = NULL;
    arg->bulletsPtrs[3] = NULL;
    arg->bulletsPtrs[2] = NULL;
    arg->bulletsPtrs[1] = NULL;
    arg->bulletsPtrs[0] = NULL;

    for (i = 0; i < kBulletCapacity; i++)
    {
        bullet = arg->BulletAt(blockIdx);
#if defined(TH07_PSP)
        // GCC otherwise keeps re-deriving this large-stride array address
        // from blockIdx throughout the hot state switch.  The empty read/write
        // register barrier emits no instruction, but makes the already
        // computed Bullet pointer the canonical value for this iteration.
        asm volatile("" : "+r"(bullet));
        if (!arg->PspIsBulletSlotTracked(blockIdx))
        {
            goto bullet_loop_continue;
        }
#endif
        if (bullet->state == BULLET_INACTIVE)
        {
#if defined(TH07_PSP)
            arg->PspForgetBulletSlot(blockIdx);
#endif
            goto bullet_loop_continue;
        }
        arg->bulletCount++;
        bombCollisionChecked = false;
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
        pspMeBulletFastFlags = 0u;
        pspMeBulletFastInBounds = 0;
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        pspMeBulletCompactFlags = 0u;
        pspMeBulletCompactInBounds = 0;
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
        pspMeBulletCompactTrustedMotion = false;
#endif
#endif

        switch (bullet->state)
        {
        switch_break:
            bullet->state = BULLET_NORMAL;
            bullet->timer1 = 0;
        case BULLET_NORMAL:
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
            if (pspMeBulletCompactTrustedAuthority)
            {
                pspMeBulletCompactTrustedMotion =
                    PspMeBulletCompactTryAdoptTrustedSeed(
                        arg, bullet, static_cast<u32>(blockIdx),
                        pspMeBulletCompactSeed,
                        pspMeBulletCompactOutput,
                        &pspMeBulletCompactFlags);
                if (pspMeBulletCompactTrustedMotion)
                {
                    ++pspMeBulletCompactMotionHits;
                }
            }
            if (!pspMeBulletCompactTrustedMotion)
#endif
            {
            bullet->RunCommands();
            if (bullet->exFlags != 0)
            {
                if ((bullet->exFlags & 1) != 0)
                {
                    bullet->UpdateBulletBurstSpeed();
                }
                if ((bullet->exFlags & 0x10) != 0)
                {
                    bullet->UpdateBulletTargetVelocity();
                }
                if ((bullet->exFlags & 0x20) != 0)
                {
                    bullet->UpdateBulletTargetAngle();
                }
                if ((bullet->exFlags & 0x40) != 0)
                {
                    bullet->UpdateBulletDirChangeAndResume();
                }
                if ((bullet->exFlags & 0x100) != 0)
                {
                    bullet->UpdateBulletDirChangeAbsoluteAndResume();
                }
                if ((bullet->exFlags & 0x80) != 0)
                {
                    bullet->UpdateBulletDirChangeAimAtPlayer();
                }
                if ((bullet->exFlags & 0xc00) != 0)
                {
                    bullet->UpdateBulletBounce();
                }
            }

            if (bullet->spawnDelay != 0)
            {
                bullet->spawnDelay--;
            }
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
            {
                const u32 compactSlot = static_cast<u32>(blockIdx);
                const bool compactSeedCandidate =
                    pspMeBulletCompactSeed &&
                    (pspMeBulletCompactSeed->candidateBits[
                         compactSlot >> 5u] &
                     (1u << (compactSlot & 31u))) != 0u;
                if (PspMeBulletCompactTryAdoptSeed(
                        arg, bullet, compactSlot, pspMeBulletCompactSeed,
                        pspMeBulletCompactOutput,
                        &pspMeBulletCompactFlags))
                {
                    ++pspMeBulletCompactMotionHits;
                }
                else
                {
                    if (compactSeedCandidate)
                    {
                        ++pspMeBulletCompactJitRejects;
                    }
                    bullet->pos += bullet->velocity;
                }
            }
#elif defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
            if (pspMeBulletFastOutput)
            {
                const u32 slot = static_cast<u32>(blockIdx);
                if ((pspMeBulletFastOutput->candidateBits[slot >> 5u] &
                     (1u << (slot & 31u))) != 0u)
                {
                    const Th07PspMeBulletFastSlotResult &result =
                        pspMeBulletFastOutput->slots[slot];
                    // Recheck immediately at the adoption point as well as in
                    // the all-or-nothing preflight.  No callback can mutate a
                    // later slot between those checks, but this keeps a future
                    // refactor from turning the low-16 generation echo into an
                    // stale-slot authority.
                    if (arg->PspIsBulletSlotTracked(blockIdx) &&
                        static_cast<u16>(
                            arg->pspMeRenderSlotGenerations[slot]) ==
                            result.generation &&
                        PspMeBulletFastIsEligible(bullet))
                    {
                        std::memcpy(&bullet->pos.x, &result.posXBits,
                                    sizeof(bullet->pos.x));
                        std::memcpy(&bullet->pos.y, &result.posYBits,
                                    sizeof(bullet->pos.y));
                        std::memcpy(&bullet->pos.z, &result.posZBits,
                                    sizeof(bullet->pos.z));
                        pspMeBulletFastFlags = result.flags;
                    }
                }
            }
            if ((pspMeBulletFastFlags &
                 TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE) == 0u)
#endif
#if !defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
            bullet->pos += bullet->velocity;
#endif
            }

#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
            if (pspMeBulletCompactTrustedMotion || bullet->spawnDelay == 0)
#else
            if (bullet->spawnDelay == 0)
#endif
            {
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                pspMeBulletCompactInBounds =
                    (pspMeBulletCompactFlags &
                     TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) != 0u
                        ? ((pspMeBulletCompactFlags &
                            TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS) != 0u)
                        : g_GameManager.IsInBounds(
                              bullet->pos.x, bullet->pos.y,
                              bullet->sprites.spriteBullet.sprite->widthPx,
                              bullet->sprites.spriteBullet.sprite->heightPx);
                if ((pspMeBulletCompactFlags &
                     TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) != 0u)
                {
                    ++pspMeBulletCompactBoundsHits;
                }
                if (!pspMeBulletCompactInBounds)
#elif defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
                pspMeBulletFastInBounds =
                    (pspMeBulletFastFlags &
                     TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE) != 0u
                        ? ((pspMeBulletFastFlags &
                            TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS) != 0u)
                        : g_GameManager.IsInBounds(
                              bullet->pos.x, bullet->pos.y,
                              bullet->sprites.spriteBullet.sprite->widthPx,
                              bullet->sprites.spriteBullet.sprite->heightPx);
                if (!pspMeBulletFastInBounds)
#else
                if (!g_GameManager.IsInBounds(
                        bullet->pos.x, bullet->pos.y,
                        bullet->sprites.spriteBullet.sprite->widthPx,
                        bullet->sprites.spriteBullet.sprite->heightPx))
#endif
                {
                    if ((bullet->exFlags & 0xdc0) != 0)
                    {
                        bullet->outOfBoundsTime++;
                        if (bullet->outOfBoundsTime >= 128)
                        {
                            bullet->Initialize();
                            goto bullet_loop_continue;
                        }
                    }
                    else
                    {
                        if (bullet->outOfBoundsTime == 0)
                        {
                            bullet->Initialize();
                            goto bullet_loop_continue;
                        }
                        bullet->outOfBoundsTime--;
                    }
                    goto do_collision;
                }
                bullet->outOfBoundsTime = 0;
                goto do_collision;
            }

        do_collision:
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
            if (pspMeBulletCompactCollisionAllowed &&
                (pspMeBulletCompactFlags &
                 TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION) != 0u)
            {
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
                // The snapshot was validated once before this loop and is
                // revalidated immediately after every canonical Player
                // collision call below.  The common negative path therefore
                // pays no scattered ten-field Player reload per Bullet.
                ++pspMeBulletCompactCollisionHits;
                g_Player.itemType = ITEM_POINT_BULLET;
                goto do_sprite_anim;
#else
                // Legacy JIT authority validates on each accepted slot.
                if (!PspMeBulletCompactPlayerSnapshotMatches(
                        gPspMeBulletCompactSc.job))
                {
                    pspMeBulletCompactCollisionAllowed = false;
                    ++pspMeBulletCompactCollisionLatches;
                }
                else
                {
                    ++pspMeBulletCompactCollisionHits;
                    g_Player.itemType = ITEM_POINT_BULLET;
                    goto do_sprite_anim;
                }
#endif
            }
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
            if ((pspMeBulletFastFlags &
                 TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION) != 0u)
            {
                // Both canonical negative paths set this before returning.
                // Preserve the visible SpawnItem type even though neither
                // collision routine needs to run.
                g_Player.itemType = ITEM_POINT_BULLET;
                goto do_sprite_anim;
            }
#endif
#if defined(TH07_PSP_BULLET_COLLISION_BROADPHASE)
            {
                const bool grazeCallCanObserve =
                    !bullet->grazed &&
                    bullet->timer2.GetCurrent() >= 16 &&
                    g_Player.playerState != PLAYER_STATE_DEAD &&
                    g_Player.playerState != PLAYER_STATE_SPAWNING;
                if (Th07PspBulletCollisionDefinitelyClear(
                        bullet->pos.x, bullet->pos.y,
                        bullet->sprites.grazeSize.x,
                        bullet->sprites.grazeSize.y,
                        grazeCallCanObserve,
                        g_Player.playerState == PLAYER_STATE_BORDER,
                        g_Player.pspBombClearHighWater,
                        g_Player.hitboxTopLeft.x,
                        g_Player.hitboxTopLeft.y,
                        g_Player.hitboxBottomRight.x,
                        g_Player.hitboxBottomRight.y,
                        g_Player.grazeTopLeft.x,
                        g_Player.grazeTopLeft.y,
                        g_Player.grazeBottomRight.x,
                        g_Player.grazeBottomRight.y))
                {
                    ++pspMeBulletCompactBroadphaseHits;
                    g_Player.itemType = ITEM_POINT_BULLET;
                    goto do_sprite_anim;
                }
            }
#endif
            if (!bullet->grazed && bullet->timer2.GetCurrent() >= 16)
            {
                collisionRes = g_Player.CheckGraze(&bullet->pos, &bullet->sprites.grazeSize);
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
                if (pspMeBulletCompactCollisionAllowed &&
                    !PspMeBulletCompactPlayerSnapshotMatches(
                        gPspMeBulletCompactSc.job))
                {
                    pspMeBulletCompactCollisionAllowed = false;
                    ++pspMeBulletCompactCollisionLatches;
                }
#endif
                bombCollisionChecked = true;
                if (collisionRes == 1)
                {
                    bullet->grazed = 1;
                    goto do_player_collision;
                }
                else if (collisionRes == 2)
                {
                    if ((bullet->moreFlags & 0x1000) == 0)
                    {
                        bullet->state = BULLET_DESPAWN;
                        g_ItemManager.SpawnItem(&bullet->pos, g_Player.itemType, 1);
                    }
                }
                goto do_sprite_anim;
            }

        do_player_collision:
            // When CheckGraze ran immediately above it already tested this
            // bullet against the active bomb-clear volumes. Dense patterns
            // must not scan the same list twice per bullet. Already-grazed and
            // newly spawned bullets still take the normal bomb test.
            collisionRes =
                g_Player.CalcKillboxCollision(&bullet->pos, &bullet->sprites.grazeSize,
                                              !bombCollisionChecked);
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
            if (pspMeBulletCompactCollisionAllowed &&
                !PspMeBulletCompactPlayerSnapshotMatches(
                    gPspMeBulletCompactSc.job))
            {
                pspMeBulletCompactCollisionAllowed = false;
                ++pspMeBulletCompactCollisionLatches;
            }
#endif
            if (collisionRes != 0)
            {
                if (collisionRes != 2 || (bullet->moreFlags & 0x1000) == 0)
                {
                    bullet->state = BULLET_DESPAWN;
                    if (collisionRes == 2)
                    {
                        g_ItemManager.SpawnItem(&bullet->pos, g_Player.itemType, 1);
                    }
                }
            }

        do_sprite_anim:
            if (bullet->sprites.spriteBullet.currentInstruction)
            {
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
                if (bullet->sprites.pspQuiescentAnm)
                {
                    ++pspQuiescentEligible;
                    if (PspExecuteQuiescentBulletAnm(
                            &bullet->sprites.spriteBullet,
                            &bullet->sprites.pspQuiescentAnm))
                    {
                        ++pspQuiescentHits;
                    }
                    else
                    {
                        ++pspQuiescentFallbacks;
                        if (!bullet->sprites.pspQuiescentAnm)
                        {
                            ++pspQuiescentInvalidations;
                        }
                        g_AnmManager->ExecuteScript(&bullet->sprites.spriteBullet);
                    }
                }
                else
#endif
                {
                g_AnmManager->ExecuteScript(&bullet->sprites.spriteBullet);
                }
            }
            goto update_timers;

        case BULLET_SPAWNING_FAST:
            bullet->timer2--;
            bullet->pos += bullet->velocity / 2.0f;
            if (!g_AnmManager->ExecuteScript(
                    bullet->SpawnEffectVm(BULLET_SPAWNING_FAST)))
            {
                goto update_timers;
            }
            goto switch_break;

        case BULLET_SPAWNING_NORMAL:
            bullet->timer2--;
            bullet->pos += bullet->velocity / 2.5f;
            if (!g_AnmManager->ExecuteScript(
                    bullet->SpawnEffectVm(BULLET_SPAWNING_NORMAL)))
            {
                goto update_timers;
            }
            goto switch_break;

        case BULLET_SPAWNING_SLOW:
            bullet->timer2--;
            bullet->pos += bullet->velocity / 3.0f;
            if (!g_AnmManager->ExecuteScript(
                    bullet->SpawnEffectVm(BULLET_SPAWNING_SLOW)))
            {
                goto update_timers;
            }
            goto switch_break;

        case BULLET_DESPAWN:
            bullet->pos += bullet->velocity / 2.0f;
            if (g_AnmManager->ExecuteScript(&bullet->sprites.spriteSpawnEffectDonut))
            {
                bullet->Initialize();
                goto bullet_loop_continue;
            }
            goto update_timers;

        default:
            goto update_timers;
        }

    update_timers:
        bullet->timer1++;
        bullet->timer2++;
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
        PspSyncBulletStaticProxy(arg, bullet, static_cast<u32>(blockIdx));
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
        if (pspWarmQueueBuilding &&
            !PspCaptureBulletWarmRecord(
                arg, bullet, static_cast<u32>(blockIdx),
                static_cast<u32>(bullet->sprites.collisionType)))
        {
            pspWarmQueueBuilding = false;
            pspWarmQueueComplete = false;
        }
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
        if (gPspMeRenderFusedCapture.complete)
        {
            PspMeRenderCaptureFusedRecord(
                arg, bullet, static_cast<u32>(blockIdx),
                static_cast<u32>(bullet->sprites.collisionType));
        }
#endif
        bullet->next = arg->bulletsPtrs[bullet->sprites.collisionType];
        arg->bulletsPtrs[bullet->sprites.collisionType] = bullet;

    bullet_loop_continue:
#if defined(TH07_PSP)
        if (arg->PspIsBulletSlotTracked(blockIdx) && bullet->state == BULLET_INACTIVE)
        {
            arg->PspForgetBulletSlot(blockIdx);
        }
#endif
        blockIdx--;
        if (blockIdx < 0)
        {
            blockIdx = kBulletCapacity - 1;
        }
    }

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    // One tail probe only; a still-running command is left owned until p18 or
    // teardown. Commit aggregate counters once, outside the per-Bullet loop.
    PspMeBulletCompactFinishFrame(arg);
    gPspMeRenderShadowWindow.compactMotionHits +=
        pspMeBulletCompactMotionHits;
    gPspMeRenderShadowWindow.compactBoundsHits +=
        pspMeBulletCompactBoundsHits;
    gPspMeRenderShadowWindow.compactCollisionHits +=
        pspMeBulletCompactCollisionHits;
    gPspMeRenderShadowWindow.compactBroadphaseHits +=
        pspMeBulletCompactBroadphaseHits;
    gPspMeRenderShadowWindow.compactJitReject +=
        pspMeBulletCompactJitRejects;
    gPspMeRenderShadowWindow.compactCollisionLatch +=
        pspMeBulletCompactCollisionLatches;
#endif

#if defined(TH07_PSP_PERF_DENSE_SLICE)
    const unsigned long long pspDenseBulletEndUs =
        pspDenseActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif

    laser = arg->lasers;
    for (i = 0; i < 64; i++, laser++)
    {
        if (!laser->inUse)
        {
            continue;
        }

        laser->endOffset =
            g_Supervisor.effectiveFramerateMultiplier * laser->speed + laser->endOffset;
        if (laser->startLength < laser->endOffset - laser->startOffset)
        {
            laser->startOffset = laser->endOffset - laser->startLength;
        }
        if (laser->startOffset < 0.0f)
        {
            laser->startOffset = 0.0f;
        }
        laserHitbox.y = laser->width / 2.0f;
        laserHitbox.x = laser->endOffset - laser->startOffset;
        laserCenter.x =
            (laser->endOffset - laser->startOffset) / 2.0f + laser->startOffset + laser->pos.x;
        laserCenter.y = laser->pos.y;
        laser->vm0.scale.x = laser->width / laser->vm0.sprite->widthPx;
        width = laser->endOffset - laser->startOffset; // width is used as length here
        laser->vm0.scale.y = width / laser->vm0.sprite->heightPx;
        laser->UpdateRotationZFromAngle();
        laser->vm0.flags |= 4;

        switch (laser->state)
        {
        case LASER_SPAWNING:
            if ((laser->flags & 1) != 0)
            {
                alpha = laser->timer.AsFloat() * 255.0f / (f32)laser->startTime;
                if (alpha > 255)
                {
                    alpha = 255;
                }
                laser->vm0.color.color = alpha << 24;
            }
            else
            {
                i32 waitTime = laser->startTime > 30 ? 30 : laser->startTime;
                if (laser->startTime - waitTime < laser->timer.GetCurrent())
                {
                    width = laser->timer.AsFloat() * laser->width / (f32)laser->startTime;
                }
                else
                {
                    width = 1.2f;
                }
                laser->targetWidth = width;
                laser->vm0.scale.x = width / 16.0f;

                // ZUN bug: ZUN stores width / 2.0f in laserHitbox.x as though
                // it controlled the width of the hitbox, even though it's
                // actually the length of the laser as in
                // laser->endOffset - laser->startOffset. As a result, when a
                // laser is in its spawning state and its hitbox is set to
                // start, it'll only have a small hitbox on its midpoint
                // equal to the halfwidth.
                laserHitbox.x = width / 2.0f;
            }
            if (laser->timer >= laser->hitboxStartTime)
            {
                g_Player.CalcLaserHitbox(&laserCenter, &laserHitbox, &laser->pos, laser->angle,
                                         laser->timer.GetCurrent() % 12 == 0);
            }
            if (laser->timer < laser->startTime)
            {
                break;
            }
            laser->timer = 0;
            laser->state++;
            laser->targetWidth = laser->width;
        case LASER_ACTIVE:
            g_Player.CalcLaserHitbox(&laserCenter, &laserHitbox, &laser->pos, laser->angle,
                                     laser->timer.GetCurrent() % 12 == 0);
            if (laser->timer < laser->duration)
            {
                break;
            }
            laser->timer = 0;
            laser->state++;
            if (laser->endTime == 0)
            {
                laser->inUse = 0;
                continue;
            }
        case LASER_DESPAWNING:
            if ((laser->flags & 1) != 0)
            {
                alpha = laser->timer.AsFloat() * 255.0f / (f32)laser->startTime;
                if (alpha > 255)
                {
                    alpha = 255;
                }
                laser->vm0.color.color = alpha << 24;
            }
            else
            {
                if (laser->endTime > 0)
                {
                    width =
                        laser->width - laser->timer.AsFloat() * laser->width / (f32)laser->endTime;
                    laser->vm0.scale.x = width / 16.0f;

                    // ZUN bug: Same bug as in the laser spawning. The laser
                    // will only have a hitbox on its midpoint.
                    laserHitbox.x = width / 2.0f;
                }
            }
            if (laser->timer < laser->hitboxEndTime)
            {
                g_Player.CalcLaserHitbox(&laserCenter, &laserHitbox, &laser->pos, laser->angle,
                                         laser->timer.GetCurrent() % 12 == 0);
            }
            if (laser->timer < laser->endTime)
            {
                break;
            }
            laser->inUse = 0;
            continue;
        }
        if (laser->startOffset >= 640.0f)
        {
            laser->inUse = 0;
        }
        laser->timer++;
        g_AnmManager->ExecuteScript(&laser->vm0);
    }

    if (arg->screenClearTime != 0)
    {
        arg->screenClearTime--;
    }

    arg->time++;
    arg->updateCount++;
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    PspMeRenderPublishFusedCapture(arg);
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    if (PspBulletStaticProxyPool *pool = PspGetBulletStaticProxyPool(arg))
    {
        pool->publishedMutationEpoch =
            arg->pspBulletStaticProxyMutationEpoch;
    }
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    PspPublishBulletWarmQueue(arg, pspWarmQueueComplete);
#endif
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
    gPspBulletQuiescentEligible += pspQuiescentEligible;
    gPspBulletQuiescentHits += pspQuiescentHits;
    gPspBulletQuiescentFallbacks += pspQuiescentFallbacks;
    gPspBulletQuiescentInvalidations += pspQuiescentInvalidations;
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    if (pspDenseActive)
    {
        const unsigned long long endUs = sceKernelGetSystemTimeWide();
        gPspDenseSliceWindow.updateCallbackUs += endUs - pspDenseUpdateStartUs;
        gPspDenseSliceWindow.updateItemUs += pspDenseItemEndUs - pspDenseUpdateStartUs;
        gPspDenseSliceWindow.updateBulletUs += pspDenseBulletEndUs - pspDenseItemEndUs;
        gPspDenseSliceWindow.updateTailUs += endUs - pspDenseBulletEndUs;
        gPspDenseSliceWindow.updateBulletPopulation +=
            static_cast<unsigned int>(arg->bulletCount);
        ++gPspDenseSliceWindow.updateFrames;
    }
#endif
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

void Bullet::Draw()
{
    AnmVm *vm;

    switch (this->state)
    {
    case BULLET_SPAWNING_FAST:
        vm = this->SpawnEffectVm(BULLET_SPAWNING_FAST);
        break;
    case BULLET_SPAWNING_NORMAL:
        vm = this->SpawnEffectVm(BULLET_SPAWNING_NORMAL);
        break;
    case BULLET_SPAWNING_SLOW:
        vm = this->SpawnEffectVm(BULLET_SPAWNING_SLOW);
        break;
    case BULLET_DESPAWN:
        vm = &this->sprites.spriteSpawnEffectDonut;
        break;
    default:
        vm = &this->sprites.spriteBullet;
        break;
    }
    vm->pos.x = g_GameManager.arcadeRegionTopLeftPos.x + this->pos.x;
    vm->pos.y = g_GameManager.arcadeRegionTopLeftPos.y + this->pos.y;
    vm->pos.z = 0.05f;
    vm->color.color = (vm->color.color & 0xff000000) | 0xffffff;
#if defined(TH07_PSP)
    if (vm->autoRotate)
    {
        if (!this->pspRenderRotationValid || this->pspRenderSourceAngle != this->angle)
        {
            const f32 renderAngle =
                utils::AddNormalizeAngle(1.5707964f + this->angle, 0.0f);
            PspBulletRenderSinCos(renderAngle, &this->pspRenderSin, &this->pspRenderCos);
            this->pspRenderSourceAngle = this->angle;
            this->pspRenderAngle = renderAngle;
            this->pspRenderRotationValid = 1;
        }
        vm->SetRotationZ(this->pspRenderAngle);
        vm->updateRotation = 1;
#if defined(TH07_PSP_BULLET_ROTATED_DIRECT)
        if (vm->rotation.z != 0.0f)
        {
            g_AnmManager->DrawPspRotatedBullet(vm, this->pspRenderSin,
                                               this->pspRenderCos);
        }
        else
        {
            g_AnmManager->DrawPspBullet(vm, &this->pspRenderSin,
                                        &this->pspRenderCos);
        }
#else
        g_AnmManager->DrawPspBullet(vm, &this->pspRenderSin, &this->pspRenderCos);
#endif
    }
    else
    {
        g_AnmManager->DrawPspBullet(vm);
    }
#else
    if (vm->autoRotate)
    {
        vm->SetRotationZ(utils::AddNormalizeAngle(1.5707964f + this->angle, 0.0f));
        vm->updateRotation = 1;
    }
    g_AnmManager->Draw(vm);
#endif
}

#if defined(TH07_PSP_BULLET_SNAPSHOT_EMITTER)
__attribute__((always_inline)) inline void
Bullet::PreparePspBulletRenderRecord(PspBulletRenderRecord *record)
{
    // Select and mutate the live VM in precisely the same order as Draw().
    // These are observable render-side effects and must not be deferred with
    // the compact record itself.
    AnmVm *vm;
    switch (this->state)
    {
    case BULLET_SPAWNING_FAST:
        vm = this->SpawnEffectVm(BULLET_SPAWNING_FAST);
        break;
    case BULLET_SPAWNING_NORMAL:
        vm = this->SpawnEffectVm(BULLET_SPAWNING_NORMAL);
        break;
    case BULLET_SPAWNING_SLOW:
        vm = this->SpawnEffectVm(BULLET_SPAWNING_SLOW);
        break;
    case BULLET_DESPAWN:
        vm = &this->sprites.spriteSpawnEffectDonut;
        break;
    default:
        vm = &this->sprites.spriteBullet;
        break;
    }

    vm->pos.x = g_GameManager.arcadeRegionTopLeftPos.x + this->pos.x;
    vm->pos.y = g_GameManager.arcadeRegionTopLeftPos.y + this->pos.y;
    vm->pos.z = 0.05f;
    vm->color.color = (vm->color.color & 0xff000000) | 0xffffff;

    u32 flags = 0u;
    if (vm->autoRotate)
    {
        if (!this->pspRenderRotationValid || this->pspRenderSourceAngle != this->angle)
        {
            const f32 renderAngle =
                utils::AddNormalizeAngle(1.5707964f + this->angle, 0.0f);
            PspBulletRenderSinCos(renderAngle, &this->pspRenderSin, &this->pspRenderCos);
            this->pspRenderSourceAngle = this->angle;
            this->pspRenderAngle = renderAngle;
            this->pspRenderRotationValid = 1;
        }
        vm->SetRotationZ(this->pspRenderAngle);
        vm->updateRotation = 1;
        flags |= PSP_BULLET_RECORD_CACHED_SINCOS;
    }

    // DrawPspBullet rejects these records before dereferencing sprite data.
    // Publish flags last so the emitter never observes partially-filled data.
    record->flags = 0u;
    if (!vm->sprite || !vm->visible || !vm->active || !vm->color.bytes.a)
    {
        return;
    }

    record->posX = vm->pos.x;
    record->posY = vm->pos.y;
    record->posZ = vm->pos.z;
    record->halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    record->halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
    record->rotationZ = vm->rotation.z;
    record->sine = (flags & PSP_BULLET_RECORD_CACHED_SINCOS) ? this->pspRenderSin : 0.0f;
    record->cosine = (flags & PSP_BULLET_RECORD_CACHED_SINCOS) ? this->pspRenderCos : 0.0f;
    record->u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    record->u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    record->v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    record->v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    record->color = vm->useColor2 ? vm->color2 : vm->color;
    record->sourceFileIndex = vm->sprite->sourceFileIndex;
    flags |= PSP_BULLET_RECORD_DRAWABLE;
    flags |= (static_cast<u32>(vm->anchor) << PSP_BULLET_RECORD_ANCHOR_SHIFT) &
             PSP_BULLET_RECORD_ANCHOR_MASK;
    if (vm->blendMode)
    {
        flags |= PSP_BULLET_RECORD_BLEND_ADD;
    }
    if (vm->zWriteDisable)
    {
        flags |= PSP_BULLET_RECORD_ZWRITE_DISABLE;
    }
    record->flags = flags;
}
#endif

#if defined(TH07_PSP_PERF_DENSE_SLICE)
#define TH07_PSP_DENSE_NOTE_BULLET_VISIT()                                           \
    do                                                                               \
    {                                                                                \
        if (pspDenseActive)                                                          \
        {                                                                            \
            ++pspDenseBulletVisits;                                                  \
        }                                                                            \
    } while (0)
#define TH07_PSP_DENSE_NOTE_ONEPASS(accepted)                                        \
    do                                                                               \
    {                                                                                \
        if (pspDenseActive)                                                          \
        {                                                                            \
            if (accepted)                                                            \
            {                                                                        \
                ++pspDenseOnePassAccepts;                                            \
            }                                                                        \
            else                                                                     \
            {                                                                        \
                ++pspDenseOnePassFallbacks;                                          \
            }                                                                        \
        }                                                                            \
    } while (0)
#define TH07_PSP_DENSE_NOTE_CANONICAL_DRAW()                                         \
    do                                                                               \
    {                                                                                \
        if (pspDenseActive)                                                          \
        {                                                                            \
            ++pspDenseCanonicalDrawCalls;                                            \
        }                                                                            \
    } while (0)
#else
#define TH07_PSP_DENSE_NOTE_BULLET_VISIT() ((void)0)
#define TH07_PSP_DENSE_NOTE_ONEPASS(accepted) ((void)(accepted))
#define TH07_PSP_DENSE_NOTE_CANONICAL_DRAW() ((void)0)
#endif

#if defined(TH07_PSP_PERF_DENSE_SLICE) && \
    defined(TH07_PSP_BULLET_STATIC_PROXY)
#define TH07_PSP_DENSE_NOTE_STATIC_PROXY_HIT(accepted)                          \
    do                                                                          \
    {                                                                           \
        if (pspDenseActive && (accepted))                                       \
        {                                                                       \
            ++pspDenseStaticProxyVisitHits;                                     \
        }                                                                       \
    } while (0)
#define TH07_PSP_DENSE_NOTE_STATIC_PROXY_CANONICAL(ready)                       \
    do                                                                          \
    {                                                                           \
        if (pspDenseActive && (ready))                                          \
        {                                                                       \
            ++pspDenseStaticProxyCanonicalFallbacks;                            \
        }                                                                       \
    } while (0)
#else
#define TH07_PSP_DENSE_NOTE_STATIC_PROXY_HIT(accepted) ((void)(accepted))
#define TH07_PSP_DENSE_NOTE_STATIC_PROXY_CANONICAL(ready) ((void)(ready))
#endif

u32 BulletManager::OnDraw(BulletManager *arg)
{
    Bullet *bullet;
    f32 local_18;
    f32 local_14;
    Laser *laser;
    f32 local_c;
    i32 i;

#if defined(TH07_PSP_PERF_M3)
    const unsigned long long callbackStartUs = sceKernelGetSystemTimeWide();
    unsigned int activeLasers = 0;
    unsigned int bulletVisits = 0;
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    const bool pspDenseActive = gTh07PspPerfDenseSliceActive != 0;
    const unsigned long long pspDenseDrawStartUs =
        pspDenseActive ? sceKernelGetSystemTimeWide() : 0ull;
    unsigned int pspDenseBulletVisits = 0u;
    unsigned int pspDenseOnePassAccepts = 0u;
    unsigned int pspDenseOnePassFallbacks = 0u;
    unsigned int pspDenseCanonicalDrawCalls = 0u;
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    unsigned int pspDenseStaticProxyVisitHits = 0u;
    unsigned int pspDenseStaticProxyCanonicalFallbacks = 0u;
#endif
#endif

    laser = arg->lasers;
    for (i = 0; i < 64; i++, laser++)
    {
        if (!laser->inUse)
        {
            continue;
        }
#if defined(TH07_PSP_PERF_M3)
        ++activeLasers;
#endif
#if defined(TH07_PSP)
        PspBulletRenderSinCos(laser->angle, &local_c, &local_18);
#else
        sincosf(&local_c, &local_18, laser->angle);
#endif
        local_14 = (laser->endOffset - laser->startOffset) / 2.0f + laser->startOffset;
        laser->vm0.pos.x = local_18 * local_14 + laser->pos.x;
        laser->vm0.pos.y = local_c * local_14 + laser->pos.y;
        laser->vm0.pos.z = 0.05f;
        laser->color = (laser->color & 0xff000000) | 0xffffff;
        laser->vm0.pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
        laser->vm0.pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
        g_AnmManager->Draw(&laser->vm0);
        if ((laser->startOffset < 16.0f || laser->speed == 0.0f) &&
            (laser->hideWarning == 0 || laser->state != LASER_SPAWNING))
        {
            laser->vm1.pos.x = local_18 * laser->startOffset + laser->pos.x;
            laser->vm1.pos.y = local_c * laser->startOffset + laser->pos.y;
            laser->vm1.pos.z = 0.05f;
            laser->vm1.color.color = laser->vm0.color.color;
            laser->vm1.flag6 = 1;
            laser->vm1.color.color = (laser->vm1.color.color & 0xffffff) | 0xff000000;
            laser->vm1.scale.x = laser->width / 10.0f * ((16.0f - laser->startOffset) / 16.0f);
            laser->vm1.scale.y = laser->vm1.scale.x;
            if (laser->vm1.scale.y <= 0.0f)
            {
                laser->vm1.scale.x = laser->width / 10.0f;
                laser->vm1.scale.y = laser->vm1.scale.x;
            }
            laser->vm1.pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
            laser->vm1.pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
            g_AnmManager->Draw(&laser->vm1);
        }
    }
#if defined(TH07_PSP_PERF_M3)
    const unsigned long long laserEndUs = sceKernelGetSystemTimeWide();
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    const unsigned long long pspDenseLaserEndUs =
        pspDenseActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    bool pspMeItemConsumed = false;
    if (PspMeRenderTryEarlyItemRetire())
    {
        pspMeItemConsumed = PspMeRenderTryGeConsumeItem(arg);
    }
    if (!pspMeItemConsumed)
    {
#endif
    g_ItemManager.OnDraw();
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    }
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    // Close the canonical item phase before the MERW probe. The probe remains
    // inside priority 10 / drawBulletUs and is logged separately, never added
    // to the outer CPU closure a second time.
    const unsigned long long pspDenseItemDrawEndUs =
        pspDenseActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif
#if defined(TH07_PSP_ME_RENDER_WORKER)
    // Exact priority-10 deadline.  M0B only probes/retires its isolated
    // SYNTH4 stream here; accepted DENSE still performs every visible draw.
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    const unsigned long long pspMeRenderProbeStartUs =
        pspDenseActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif
    PspMeRenderDrawDeadline();
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    if (pspDenseActive)
    {
        gPspMeRenderShadowWindow.deadlineProbeUs +=
            sceKernelGetSystemTimeWide() - pspMeRenderProbeStartUs;
    }
#endif
#endif
#if defined(TH07_PSP_PERF_M3)
    const unsigned long long itemEndUs = sceKernelGetSystemTimeWide();
    Th07PspPerfSetM3BulletLoop(1);
#endif
#if defined(TH07_PSP_BULLET_ROTATED_DIRECT)
    g_AnmManager->BeginPspRotatedBulletBatch();
#endif
#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
    g_AnmManager->BeginPspUnifiedBulletBatch();
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    u32 pspMeGeRecordCount = 0u;
    const bool pspMeGeConsumed =
        PspMeRenderTryGeConsume(arg, &pspMeGeRecordCount);
    if (pspMeGeConsumed)
    {
#if defined(TH07_PSP_PERF_M3)
        bulletVisits += pspMeGeRecordCount;
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        if (pspDenseActive)
        {
            pspDenseBulletVisits += pspMeGeRecordCount;
            pspDenseOnePassAccepts += pspMeGeRecordCount;
        }
#endif
    }
#endif
#if defined(TH07_PSP_BULLET_ONEPASS_ROTATED)
    // Cache the viewport once for the inlined NORMAL+autoRotate path.  Keep
    // the legacy integer-add-then-float conversion used by the direct path.
    const ZunViewport &onePassViewport = g_Supervisor.viewport;
    const float onePassViewportLeft = static_cast<float>(onePassViewport.x);
    const float onePassViewportTop = static_cast<float>(onePassViewport.y);
    const float onePassViewportRight =
        static_cast<float>(onePassViewport.x + onePassViewport.width);
    const float onePassViewportBottom =
        static_cast<float>(onePassViewport.y + onePassViewport.height);
    const bool pspBulletStaticProxyReady =
        PspBulletStaticProxyFrameReady(arg);
#endif
#if defined(TH07_PSP_BULLET_SNAPSHOT_EMITTER)
    u32 renderRecordCount = 0u;
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE) && \
    defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    const bool pspBulletWarmQueueReady = PspBulletWarmQueueReady(arg);
#endif
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS) && \
    !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // I-ME1 observes the accepted DENSE arena in place.  It neither flushes
    // the front batch nor copies/replaces a visible vertex.
    PspMeRenderCorrectnessBeginCapture();
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    if (!pspMeGeConsumed)
    {
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
#if !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    const bool pspBulletWarmQueueReady = PspBulletWarmQueueReady(arg);
#endif
    if (pspBulletWarmQueueReady)
    {
        const PspBulletWarmQueue *queue = PspGetBulletWarmQueue(arg);
        for (i = 0; i < 6; ++i)
        {
            u16 recordIndex = queue->heads[i];
            while (recordIndex != kPspBulletWarmQueueEnd)
            {
                const PspBulletWarmRecord &record = queue->records[recordIndex];
                const u16 nextIndex = record.nextIndex;
                TH07_PSP_DENSE_NOTE_BULLET_VISIT();
                const bool warmAccepted = PspDrawBulletWarmRecord(
                    record, onePassViewportLeft, onePassViewportTop,
                    onePassViewportRight, onePassViewportBottom);
                TH07_PSP_DENSE_NOTE_ONEPASS(warmAccepted);
                if (__builtin_expect(!warmAccepted, 0))
                {
                    // Noneligible records and renderer mismatches use the
                    // untouched canonical draw from their stable slot.
                    TH07_PSP_DENSE_NOTE_CANONICAL_DRAW();
                    arg->BulletAt(recordIndex)->Draw();
                }
                recordIndex = nextIndex;
            }
        }
    }
    else
#endif
    {
    for (i = 0; i < 6; i++)
    {
        bullet = arg->bulletsPtrs[i];
        while (bullet)
        {
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS) && \
    !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
            Th07PspSpriteVertex *const pspMeCanonicalBefore =
                g_AnmManager->vertexBufferCurPtr;
#endif
            TH07_PSP_DENSE_NOTE_BULLET_VISIT();
#if defined(TH07_PSP_PERF_M3)
            ++bulletVisits;
            const bool sampleBulletDraw =
                (gPspM3BulletDrawOrdinal++ % kPspM3BulletDrawSampleStride) == 0u;
            const unsigned long long bulletDrawStartUs =
                sampleBulletDraw ? sceKernelGetSystemTimeWide() : 0;
#endif
#if defined(TH07_PSP_BULLET_SNAPSHOT_EMITTER)
            bullet->PreparePspBulletRenderRecord(&gPspBulletRenderTile[renderRecordCount++]);
            if (renderRecordCount == kPspBulletRenderTileSize)
            {
                g_AnmManager->DrawPspBulletRecords(gPspBulletRenderTile, renderRecordCount);
                renderRecordCount = 0u;
            }
#else
#if defined(TH07_PSP_BULLET_ONEPASS_ROTATED)
            bool onePassAccepted = PspTryBulletStaticProxy(
                arg, bullet, onePassViewportLeft, onePassViewportTop,
                onePassViewportRight, onePassViewportBottom);
            TH07_PSP_DENSE_NOTE_STATIC_PROXY_HIT(onePassAccepted);
            if (!onePassAccepted)
            {
                onePassAccepted = PspDrawNormalAutoRotatedOnePass(
                    bullet, onePassViewportLeft, onePassViewportTop,
                    onePassViewportRight, onePassViewportBottom);
            }
            TH07_PSP_DENSE_NOTE_ONEPASS(onePassAccepted);
            if (__builtin_expect(!onePassAccepted, 0))
            {
                TH07_PSP_DENSE_NOTE_CANONICAL_DRAW();
                TH07_PSP_DENSE_NOTE_STATIC_PROXY_CANONICAL(
                    pspBulletStaticProxyReady);
                bullet->Draw();
            }
#else
            bullet->Draw();
#endif
#endif
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS) && \
    !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
            PspMeRenderCorrectnessNoteRecord(
                bullet, static_cast<u32>(i), pspMeCanonicalBefore,
                g_AnmManager->vertexBufferCurPtr);
#endif
#if defined(TH07_PSP_PERF_M3)
            if (sampleBulletDraw)
            {
                gPspM3PerfWindow.sampledBulletDrawUs +=
                    sceKernelGetSystemTimeWide() - bulletDrawStartUs;
                ++gPspM3PerfWindow.sampledBulletDraws;
            }
#endif
            bullet = bullet->next;
        }
    }
    }
#if defined(TH07_PSP_BULLET_SNAPSHOT_EMITTER)
    if (renderRecordCount != 0u)
    {
        g_AnmManager->DrawPspBulletRecords(gPspBulletRenderTile, renderRecordCount);
    }
#endif
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS) && \
    !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    PspMeRenderCorrectnessEndCapture();
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    }
#endif
#if defined(TH07_PSP_PERF_M3)
    Th07PspPerfSetM3BulletLoop(0);
    const unsigned long long callbackEndUs = sceKernelGetSystemTimeWide();
    gPspM3PerfWindow.callbackUs += callbackEndUs - callbackStartUs;
    gPspM3PerfWindow.laserUs += laserEndUs - callbackStartUs;
    gPspM3PerfWindow.itemUs += itemEndUs - laserEndUs;
    gPspM3PerfWindow.bulletUs += callbackEndUs - itemEndUs;
    ++gPspM3PerfWindow.frames;
    gPspM3PerfWindow.activeLasers += activeLasers;
    gPspM3PerfWindow.activeItems +=
        static_cast<unsigned int>(g_ItemManager.activeItemCount);
    gPspM3PerfWindow.bulletVisits += bulletVisits;
#endif
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    if (pspDenseActive)
    {
        const unsigned long long endUs = sceKernelGetSystemTimeWide();
        gPspDenseSliceWindow.drawCallbackUs += endUs - pspDenseDrawStartUs;
        gPspDenseSliceWindow.drawLaserUs += pspDenseLaserEndUs - pspDenseDrawStartUs;
        gPspDenseSliceWindow.drawItemUs += pspDenseItemDrawEndUs - pspDenseLaserEndUs;
        gPspDenseSliceWindow.drawBulletUs += endUs - pspDenseItemDrawEndUs;
        gPspDenseSliceWindow.bulletVisits += pspDenseBulletVisits;
        gPspDenseSliceWindow.onePassAccepts += pspDenseOnePassAccepts;
        gPspDenseSliceWindow.onePassFallbacks += pspDenseOnePassFallbacks;
        gPspDenseSliceWindow.canonicalDrawCalls += pspDenseCanonicalDrawCalls;
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
        if (pspBulletStaticProxyReady)
        {
            ++gPspDenseSliceWindow.staticProxyReadyFrames;
        }
        else
        {
            ++gPspDenseSliceWindow.staticProxyFallbackFrames;
        }
        gPspDenseSliceWindow.staticProxyVisitHits +=
            pspDenseStaticProxyVisitHits;
        gPspDenseSliceWindow.staticProxyCanonicalFallbacks +=
            pspDenseStaticProxyCanonicalFallbacks;
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
        if (pspBulletWarmQueueReady)
        {
            ++gPspDenseSliceWindow.warmQueueReadyFrames;
        }
        else
        {
            ++gPspDenseSliceWindow.warmQueueFallbackFrames;
        }
#endif
        ++gPspDenseSliceWindow.drawFrames;
    }
#endif
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#undef TH07_PSP_DENSE_NOTE_BULLET_VISIT
#undef TH07_PSP_DENSE_NOTE_ONEPASS
#undef TH07_PSP_DENSE_NOTE_CANONICAL_DRAW
#undef TH07_PSP_DENSE_NOTE_STATIC_PROXY_HIT
#undef TH07_PSP_DENSE_NOTE_STATIC_PROXY_CANONICAL

#if defined(TH07_PSP_PERF_M3)
void Th07PspTakeM3PerfWindow(Th07PspM3PerfWindow *window)
{
    if (!window)
    {
        return;
    }
    *window = gPspM3PerfWindow;
    gPspM3PerfWindow = Th07PspM3PerfWindow{};
}
#endif

#if defined(TH07_PSP_PERF_DENSE_SLICE)
void Th07PspTakeDenseSliceWindow(Th07PspDenseSliceWindow *window)
{
    if (!window)
    {
        return;
    }
    *window = gPspDenseSliceWindow;
    gPspDenseSliceWindow = Th07PspDenseSliceWindow{};
}
#endif

ZunResult BulletManager::AddedCallback(BulletManager *arg)
{
    u32 i;

    if ((u32)(g_Supervisor.curState != 3 && g_Supervisor.curState != 11 &&
              g_Supervisor.curState != 12))
    {
        if (g_AnmManager->LoadAnms(ANM_FILE_BULLETS, "data/etama.anm", ANM_OFFSET_BULLETS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
    }

    for (i = 0; i < 11; i++)
    {
        g_AnmManager->SetAnmIdxAndExecuteScript(&arg->bulletTypeTemplates[i].spriteBullet,
                                                g_BulletTypeInfos[i].anmFileIdx);
        g_AnmManager->SetAnmIdxAndExecuteScript(&arg->bulletTypeTemplates[i].spriteSpawnEffectFast,
                                                g_BulletTypeInfos[i].spawnFastIdx);
        g_AnmManager->SetAnmIdxAndExecuteScript(
            &arg->bulletTypeTemplates[i].spriteSpawnEffectNormal,
            g_BulletTypeInfos[i].spawnNormalIdx);
        g_AnmManager->SetAnmIdxAndExecuteScript(&arg->bulletTypeTemplates[i].spriteSpawnEffectSlow,
                                                g_BulletTypeInfos[i].spawnSlowIdx);
        g_AnmManager->SetAnmIdxAndExecuteScript(&arg->bulletTypeTemplates[i].spriteSpawnEffectDonut,
                                                g_BulletTypeInfos[i].spawnDonutIdx);
        arg->bulletTypeTemplates[i].spriteBullet.zWriteDisable = 1;
        arg->bulletTypeTemplates[i].spriteSpawnEffectFast.zWriteDisable = 1;
        arg->bulletTypeTemplates[i].spriteSpawnEffectNormal.zWriteDisable = 1;
        arg->bulletTypeTemplates[i].spriteSpawnEffectSlow.zWriteDisable = 1;
        arg->bulletTypeTemplates[i].spriteSpawnEffectDonut.zWriteDisable = 1;
        arg->bulletTypeTemplates[i].spriteBullet.baseSpriteIdx =
            arg->bulletTypeTemplates[i].spriteBullet.activeSpriteIdx;
        arg->bulletTypeTemplates[i].bulletHeight =
            (u8)arg->bulletTypeTemplates[i].spriteBullet.sprite->heightPx;
        if (arg->bulletTypeTemplates[i].spriteBullet.sprite->heightPx <= 8.0f)
        {
            arg->bulletTypeTemplates[i].grazeSize.x = 4.0f;
            arg->bulletTypeTemplates[i].grazeSize.y = 4.0f;
            arg->bulletTypeTemplates[i].collisionType = 5;
        }
        else
        {
            if (arg->bulletTypeTemplates[i].spriteBullet.sprite->heightPx <= 16.0f)
            {
                switch (g_BulletTypeInfos[i].anmFileIdx)
                {
                case 514:
                    arg->bulletTypeTemplates[i].grazeSize.x = 4.0f;
                    arg->bulletTypeTemplates[i].grazeSize.y = 4.0f;
                    arg->bulletTypeTemplates[i].collisionType = 4;
                    break;
                case 516:
                case 518:
                    arg->bulletTypeTemplates[i].grazeSize.x = 4.0f;
                    arg->bulletTypeTemplates[i].grazeSize.y = 4.0f;
                    arg->bulletTypeTemplates[i].collisionType = 4;
                    break;
                case 517:
                    arg->bulletTypeTemplates[i].grazeSize.x = 4.0f;
                    arg->bulletTypeTemplates[i].grazeSize.y = 4.0f;
                    arg->bulletTypeTemplates[i].collisionType = 4;
                    break;
                default:
                    arg->bulletTypeTemplates[i].grazeSize.x = 6.0f;
                    arg->bulletTypeTemplates[i].grazeSize.y = 6.0f;
                    arg->bulletTypeTemplates[i].collisionType = 3;
                }
            }
            else
            {
                if (arg->bulletTypeTemplates[i].spriteBullet.sprite->heightPx <= 32.0f)
                {
                    switch (g_BulletTypeInfos[i].anmFileIdx)
                    {
                    case 520:
                        arg->bulletTypeTemplates[i].grazeSize.x = 5.0f;
                        arg->bulletTypeTemplates[i].grazeSize.y = 5.0f;
                        arg->bulletTypeTemplates[i].collisionType = 1;
                        break;
                    case 521:
                        arg->bulletTypeTemplates[i].grazeSize.x = 8.0f;
                        arg->bulletTypeTemplates[i].grazeSize.y = 8.0f;
                        arg->bulletTypeTemplates[i].collisionType = 2;
                        break;
                    default:
                        arg->bulletTypeTemplates[i].grazeSize.x = 10.0f;
                        arg->bulletTypeTemplates[i].grazeSize.y = 10.0f;
                        arg->bulletTypeTemplates[i].collisionType = 2;
                    }
                }
                else
                {
                    arg->bulletTypeTemplates[i].collisionType = 0;
                    arg->bulletTypeTemplates[i].grazeSize.x = 24.0f;
                    arg->bulletTypeTemplates[i].grazeSize.y = 24.0f;
                }
            }
        }
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
        arg->bulletTypeTemplates[i].pspQuiescentAnm =
            PspClassifyQuiescentBulletAnm(
                &arg->bulletTypeTemplates[i].spriteBullet)
                ? 1u
                : 0u;
#endif
    }
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
    gPspBulletQuiescentTemplateCount = 0;
    gPspBulletQuiescentEligible = 0;
    gPspBulletQuiescentHits = 0;
    gPspBulletQuiescentFallbacks = 0;
    gPspBulletQuiescentInvalidations = 0;
    for (i = 0; i < 11; ++i)
    {
        gPspBulletQuiescentTemplateCount +=
            arg->bulletTypeTemplates[i].pspQuiescentAnm ? 1u : 0u;
    }
    th07_psp_boot_notef("bullet qanm templates %u/11",
                        gPspBulletQuiescentTemplateCount);
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    if (arg->PspEnsureBulletWarmQueue())
    {
        th07_psp_boot_notef("bullet warm queue ready %u bytes",
                            static_cast<unsigned int>(sizeof(PspBulletWarmQueue)));
    }
    else
    {
        th07_psp_boot_note("bullet warm queue unavailable fallback");
    }
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    if (arg->PspEnsureBulletStaticProxyPool())
    {
        th07_psp_boot_notef(
            "bullet static proxy ready %u bytes",
            static_cast<unsigned int>(sizeof(PspBulletStaticProxyPool)));
    }
    else
    {
        th07_psp_boot_note("bullet static proxy unavailable fallback");
    }
#endif
#if defined(TH07_PSP_ME_RENDER_WORKER)
    PspMeRenderManagerAdded();
#endif
    g_ItemManager.Reset();
    return ZUN_SUCCESS;
}

ZunResult BulletManager::DeletedCallback(BulletManager *arg)
{
#if defined(TH07_PSP_ME_RENDER_WORKER)
    const bool pspMeRenderOwnersSafe = PspMeRenderManagerDeleted();
#else
    constexpr bool pspMeRenderOwnersSafe = true;
#endif
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
    th07_psp_boot_notef("bullet qanm eligible %u hit %u fallback %u invalid %u",
                        gPspBulletQuiescentEligible, gPspBulletQuiescentHits,
                        gPspBulletQuiescentFallbacks,
                        gPspBulletQuiescentInvalidations);
#endif
    if (pspMeRenderOwnersSafe &&
        (u32)(g_Supervisor.curState != 3 && g_Supervisor.curState != 11 &&
              g_Supervisor.curState != 12))
    {
        g_AnmManager->ReleaseAnm(11);
        g_AnmManager->ReleaseAnm(12);
        g_AnmManager->ReleaseAnm(13);
        g_AnmManager->ReleaseAnm(14);
    }
#if defined(TH07_PSP_1000)
    g_ItemManager.PspReleaseItemPool();
    arg->PspReleaseBulletPool();
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    arg->PspReleaseBulletWarmQueue();
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    arg->PspReleaseBulletStaticProxyPool();
#endif
#if !defined(TH07_PSP_1000) && !defined(TH07_PSP_BULLET_WARM_QUEUE) && \
    !defined(TH07_PSP_BULLET_STATIC_PROXY)
    (void)arg;
#endif
    return ZUN_SUCCESS;
}

ZunResult BulletManager::RegisterChain(const char *etamaAnmPath)
{
    BulletManager *mgr = &g_BulletManager;
    g_BulletColor = g_DefaultBulletColors;
    mgr->Initialize();
    mgr->etamaAnmPath = etamaAnmPath;
    g_BulletManagerCalcChain.callback = (ChainCallback)OnUpdate;
    g_BulletManagerCalcChain.addedCallback = NULL;
    g_BulletManagerCalcChain.deletedCallback = NULL;
    g_BulletManagerCalcChain.addedCallback = (ChainLifecycleCallback)AddedCallback;
    g_BulletManagerCalcChain.deletedCallback = (ChainLifecycleCallback)DeletedCallback;
    g_BulletManagerCalcChain.arg = mgr;
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    g_PspMeBulletCompactLaunchChain.callback =
        (ChainCallback)PspMeBulletCompactEarlyLaunch;
    g_PspMeBulletCompactLaunchChain.addedCallback = NULL;
    g_PspMeBulletCompactLaunchChain.deletedCallback = NULL;
    g_PspMeBulletCompactLaunchChain.arg = mgr;
    if (g_Chain.AddToCalcChain(&g_PspMeBulletCompactLaunchChain, 9))
    {
        return ZUN_ERROR;
    }
#endif
    if (g_Chain.AddToCalcChain(&g_BulletManagerCalcChain, 12))
    {
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        g_Chain.Cut(&g_PspMeBulletCompactLaunchChain);
#endif
        return ZUN_ERROR;
    }

#if defined(TH07_PSP_ME_RENDER_WORKER)
    g_PspMeRenderCalcCompleteChain.callback =
        (ChainCallback)PspMeRenderCalcCompleteSentinel;
    g_PspMeRenderCalcCompleteChain.addedCallback = NULL;
    g_PspMeRenderCalcCompleteChain.deletedCallback = NULL;
    g_PspMeRenderCalcCompleteChain.arg = mgr;
    // Priority 18 must remain the final calc callback: all ordinary owners are
    // 0..17. GameWindow publishes a performance stream only when this exact
    // sentinel advances, so no later mutation may sit behind it.
    if (g_Chain.AddToCalcChain(&g_PspMeRenderCalcCompleteChain, 18))
    {
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        g_Chain.Cut(&g_PspMeBulletCompactLaunchChain);
#endif
        g_Chain.Cut(&g_BulletManagerCalcChain);
        return ZUN_ERROR;
    }
#endif

    g_BulletManagerDrawChain.callback = (ChainCallback)OnDraw;
    g_BulletManagerDrawChain.addedCallback = NULL;
    g_BulletManagerDrawChain.deletedCallback = NULL;
    g_BulletManagerDrawChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_BulletManagerDrawChain, 10);
    return ZUN_SUCCESS;
}

void BulletManager::CutChain()
{
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    g_Chain.Cut(&g_PspMeBulletCompactLaunchChain);
#endif
    g_Chain.Cut(&g_BulletManagerCalcChain);
#if defined(TH07_PSP_ME_RENDER_WORKER)
    g_Chain.Cut(&g_PspMeRenderCalcCompleteChain);
#endif
    g_Chain.Cut(&g_BulletManagerDrawChain);
    g_BulletManager.Initialize();
}

void BulletManager::StopBulletMovement()
{
    Bullet *bullet;
    i32 i;

#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    this->PspMarkMeRenderMutation();
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    this->PspMarkBulletMutation();
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    this->PspMarkBulletStaticProxyMutation();
#endif

    for (i = 0; i < kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
#if defined(TH07_PSP)
        if (!this->PspIsBulletSlotTracked(i))
        {
            continue;
        }
#endif
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        bullet->velocity = ZunVec3(0.0f, 0.0f, 0.0f);
        bullet->unused_ba4 = ZunVec3(0.0f, 0.0f, 0.0f);
        bullet->angularVelocity = 0.0f;
        bullet->acceleration = 0.0f;
        bullet->speed = 0.0f;
        bullet->spriteOffset = 0;
        SetActiveBulletSprite(
            bullet, (i32)bullet->sprites.spriteBullet.baseSpriteIdx +
                        (i32)bullet->spriteOffset);
    }
}

BulletCommand *Bullet::AddCommand(i32 command, i32 flag, u32 type)
{
    BulletCommand *bulletCommand = &this->commands[command];
    bulletCommand->type = type;
    bulletCommand->flag = flag;
    this->moreFlags |= type;
    this->curCmdIdx = 0;
    return bulletCommand;
}

BulletCommand *EnemyBulletShooter::AddCommand(i32 command, i32 flag, u32 type)
{
    BulletCommand *bulletCommand = &this->commands[command];
    bulletCommand->type = type;
    bulletCommand->flag = flag;
    this->flags |= type;
    return bulletCommand;
}

void Bullet::AddAngleAccelCommand(i32 command, i32 flag, i32 duration, f32 angle, f32 speed)
{
    BulletCommand *bulletCommand;

    bulletCommand = AddCommand(command, flag, 0x20);
    bulletCommand->duration = duration;
    bulletCommand->speed = speed;
    bulletCommand->angle = angle;
}

void Bullet::AddTargetVelocityCommand(i32 command, i32 flag, i32 duration, f32 speed, f32 angle)
{
    BulletCommand *bulletCommand;

    bulletCommand = AddCommand(command, flag, 0x10);
    bulletCommand->duration = duration;
    bulletCommand->speed = speed;
    bulletCommand->angle = angle;
}

void EnemyBulletShooter::AddAngleAccelCommand(i32 command, i32 flag, i32 duration, f32 angle,
                                              f32 speed)
{
    BulletCommand *bulletCommand = AddCommand(command, flag, 0x20);
    bulletCommand->duration = duration;
    bulletCommand->speed = speed;
    bulletCommand->angle = angle;
}

void EnemyBulletShooter::AddDirChangeCommand(i32 command, i32 flag, i32 duration, i32 loopCount,
                                             f32 speed, f32 angle)
{
    BulletCommand *bulletCommand = AddCommand(command, flag, 0x80);
    bulletCommand->duration = duration;
    bulletCommand->loopCount = loopCount;
    bulletCommand->speed = speed;
    bulletCommand->angle = angle;
}

void EnemyBulletShooter::AddTargetVelocityCommand(i32 command, i32 flag, i32 duration, f32 speed,
                                                  f32 angle)
{
    BulletCommand *bulletCommand = AddCommand(command, flag, 0x10);
    bulletCommand->duration = duration;
    bulletCommand->speed = speed;
    bulletCommand->angle = angle;
}

void EnemyBulletShooter::AddSpawnDelayCommand(i32 command, i32 flag, i32 duration)
{
    BulletCommand *bulletCommand = AddCommand(command, flag, 0x2000);
    bulletCommand->duration = duration;
}
