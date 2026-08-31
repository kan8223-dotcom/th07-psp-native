#pragma once

#include "ItemManager.hpp"
#include "ZunMath.hpp"
#include "ZunResult.hpp"
#include "utils.hpp"

extern u32 *g_BulletColor;

typedef enum BulletState
{
    BULLET_INACTIVE = 0,
    BULLET_NORMAL = 1,
    BULLET_SPAWNING_FAST = 2,
    BULLET_SPAWNING_NORMAL = 3,
    BULLET_SPAWNING_SLOW = 4,
    BULLET_DESPAWN = 5,
    BULLET_END_ARRAY = 6
} BulletState;

typedef enum LaserState
{
    LASER_SPAWNING = 0,
    LASER_ACTIVE = 1,
    LASER_DESPAWNING = 2
} LaserState;

struct BulletTypeInfo
{
    i32 anmFileIdx;
    i32 spawnFastIdx;
    i32 spawnNormalIdx;
    i32 spawnSlowIdx;
    i32 spawnDonutIdx;
};

struct BulletTypeSprites
{
    AnmVm spriteBullet;
    AnmVm spriteSpawnEffectFast;
    AnmVm spriteSpawnEffectNormal;
    AnmVm spriteSpawnEffectSlow;
    AnmVm spriteSpawnEffectDonut;
    ZunVec3 grazeSize;
    u8 unused_b88;
    u8 bulletHeight;
    u8 collisionType;
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
    // The original structure has one byte of tail padding here. Reuse it for
    // a PSP-2000+ validation-only classification bit without changing the
    // Bullet payload stride or the 1,024-slot replay layout.
    u8 pspQuiescentAnm;
#else
    // pad 1
#endif
};
#if defined(TH07_PSP_BULLET_QUIESCENT_ANM)
static_assert(sizeof(BulletTypeSprites) == 0xb8c,
              "quiescent ANM flag must only consume BulletTypeSprites tail padding");
#endif

#if defined(TH07_PSP_1000)
// Fast, normal, and slow spawn animations are mutually exclusive states of a
// bullet. Keeping three complete AnmVm instances in every PSP-1000 payload
// wastes over 1 MiB at the original 1,024-slot capacity, so the low-memory
// runtime stores only the selected spawn animation. The 16 type templates
// above remain unchanged and provide the appropriate source VM at spawn time.
struct Psp1000BulletSprites
{
    AnmVm spriteBullet;
    AnmVm spriteSpawnEffect;
    AnmVm spriteSpawnEffectDonut;
    ZunVec3 grazeSize;
    u8 unused_b88;
    u8 bulletHeight;
    u8 collisionType;
    // pad 1
};
#endif

struct BulletCommand
{
    f32 speed;
    f32 angle;
    i32 duration;
    i32 loopCount;
    u32 type;
    i32 flag;
};

struct BulletCommandState
{
    ZunTimer timer;
    f32 speed;
    f32 angle;
    ZunVec3 vec3;
    i32 duration;
    i32 maxTimes;
    i32 minTimes;
};

struct EnemyBulletShooter
{
    EnemyBulletShooter()
    {
        memset(this, 0, sizeof(EnemyBulletShooter));
        this->soundOverride = -1;
    }

    BulletCommand *AddCommand(i32 command, i32 flag, u32 type);
    void AddAngleAccelCommand(i32 command, i32 flag, i32 duration, f32 angle, f32 speed);
    void AddDirChangeCommand(i32 command, i32 flag, i32 duration, i32 loopCount, f32 speed,
                             f32 angle);
    void AddSpawnDelayCommand(i32 command, i32 flag, i32 duration);
    void AddTargetVelocityCommand(i32 command, i32 flag, i32 duration, f32 speed, f32 angle);

    i16 sprite;
    i16 spriteOffset;
    ZunVec3 position;
    f32 angle1;
    f32 angle2;
    f32 speed1;
    f32 speed2;
    BulletCommand commands[6];
    i32 unused_b0[3];
    i16 count1;
    i16 count2;
    u16 aimMode;
    i16 unused_c2;
    u32 flags;
    i32 soundIdx;
    i32 soundOverride;
    BulletTypeSprites *sprites;
};

struct EnemyLaserShooter
{
    EnemyLaserShooter()
    {
        memset(this, 0, sizeof(EnemyLaserShooter));
        this->soundOverride = -1;
    }

    i16 sprite;
    i16 spriteOffset;
    ZunVec3 position;
    f32 angle1;
    f32 angle2;
    f32 speed1;
    f32 speed2;
    BulletCommand commands[5];
    f32 startOffset;
    f32 endOffset;
    f32 startLength;
    f32 width;
    i32 startTime;
    i32 duration;
    i32 endTime;
    i32 hitboxStartTime;
    i32 hitboxEndTime;
    i32 unused_bc;
    u16 type;
    i16 unused_c2;
    u32 flags;
    i32 unused_c8;
    i32 soundOverride;
    i32 unused_d0;
};

struct Laser
{
    void UpdateRotationZFromAngle()
    {
        f32 angle = utils::AddNormalizeAngle(ZUN_PI / 2.0f + this->angle, 0.0f);
        this->vm0.rotation.z = angle;
    }

    struct AnmVm vm0;
    struct AnmVm vm1;
    ZunVec3 pos;
    f32 angle;
    f32 startOffset;
    f32 endOffset;
    f32 startLength;
    f32 width;
    f32 targetWidth;
    f32 speed;
    i32 startTime;
    i32 hitboxStartTime;
    i32 duration;
    i32 endTime;
    i32 hitboxEndTime;
    i32 inUse;
    ZunTimer timer;
    u16 flags;
    i16 color;
    u8 state;
    u8 hideWarning;
    // pad 2
};

struct Bullet
{
    BulletCommand *AddCommand(i32 command, i32 flag, u32 type);
    void AddAngleAccelCommand(i32 command, i32 flag, i32 duration, f32 angle, f32 speed);
    void AddTargetVelocityCommand(i32 command, i32 flag, i32 duration, f32 speed, f32 angle);
    void RunCommands();

    void UpdateBulletBurstSpeed();
    void UpdateBulletTargetVelocity();
    void UpdateBulletTargetAngle();
    void UpdateBulletDirChangeAndResume();
    void UpdateBulletDirChangeAbsoluteAndResume();
    void UpdateBulletDirChangeAimAtPlayer();
    void UpdateBulletBounce();

    void Draw();
#if defined(TH07_PSP_BULLET_SNAPSHOT_EMITTER)
    __attribute__((always_inline)) inline void
    PreparePspBulletRenderRecord(struct PspBulletRenderRecord *record);
#endif
    AnmVm *SpawnEffectVm(u16 spawnState);
    void AssignTypeSprites(const BulletTypeSprites &source);

    void Initialize()
    {
        this->state = BULLET_INACTIVE;
        this->timer1 = 0;
        this->timer2 = 0;
    }

    void ClearCommand(i32 idx)
    {
        this->commands[idx].type = 0;
    }

#if defined(TH07_PSP_1000)
    Psp1000BulletSprites sprites;
#else
    BulletTypeSprites sprites;
#endif
    ZunVec3 pos;
    ZunVec3 velocity;
    ZunVec3 unused_ba4;
    f32 speed;
    f32 acceleration;
    f32 angularVelocity;
    f32 angle;
    f32 unused_bc0;
    f32 unused_bc4;
    ZunTimer timer1;
    ZunTimer timer2;
    i32 unused_be0[4];
    i32 spawnDelay;
    u16 exFlags;
    u16 moreFlags;
    i16 spriteOffset;
    i16 unused_bfa;
    u16 state;
    u16 outOfBoundsTime;
    u8 spawned;
    u8 grazed;
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    // Reuse the original two-byte padding so the draw walk can address its
    // compact stage record without dividing a 3+ KiB Bullet pointer stride.
    u16 pspStaticProxySlot;
#else
    // pad 2
#endif
    Bullet *next;
    i32 state2;
    i32 soundIdx;
    i32 curCmdIdx;
    BulletCommand commands[5];
    BulletCommandState commandStates[5];
#if defined(TH07_PSP)
    // Auto-rotating bullets normally keep the same heading for many frames.
    // Cache the render sin/cos so the PSP does not run one VFPU trig pair per
    // visible bullet on every draw.  Gameplay continues to use angle exactly
    // as before; these values are render-only and are refreshed on a change.
    f32 pspRenderSourceAngle;
    f32 pspRenderAngle;
    f32 pspRenderSin;
    f32 pspRenderCos;
    u32 pspRenderRotationValid;
#endif
};

#if defined(TH07_PSP_1000)
static_assert(sizeof(Bullet) == 2276,
              "PSP-1000 Bullet growth requires re-auditing the stage pool arena");
#endif

struct BulletManager
{
    // Replay state depends on the original 1,024 slot IDs, including the
    // next-slot cursor and reverse update order. PSP-1000 compacts each
    // payload but preserves the complete logical and physical slot space.
    static constexpr i32 kBulletCapacity = 1024;

    BulletManager();

    static ZunResult RegisterChain(const char *etamaAnmPath);
    static void CutChain();

    static ZunResult AddedCallback(BulletManager *arg);
    static ZunResult DeletedCallback(BulletManager *arg);
    static u32 OnUpdate(BulletManager *arg);
    static u32 OnDraw(BulletManager *arg);

    void Initialize();

    i32 DespawnBullets(i32 param_1, i32 turnIntoItem);
    void RemoveAllBullets(i32 param_1);
    void RemoveBulletsInRadius(ZunVec3 *centerPos, f32 radius);
    static void SetActiveBulletSprite(Bullet *bullet, i32 spriteIdx);
    static void SetActiveSpriteByResolution(AnmVm *sprite, AnmVm *bulletTypeTemplate,
                                            Bullet *bullet, i32 spriteOffset);
    i32 SpawnBulletPattern(struct EnemyBulletShooter *bulletProps);
    Laser *SpawnLaserPattern(struct EnemyLaserShooter *laserProps);
    i32 SpawnSingleBullet(EnemyBulletShooter *bulletProps, i32 x, i32 y, f32 angle);
    void StopBulletMovement();

    BulletTypeSprites bulletTypeTemplates[16];
#if defined(TH07_PSP_1000)
    // The compact PSP-1000 runtime payload keeps only the one active spawn VM,
    // allowing all 1,024 original logical slots to have stable physical
    // storage. Allocate it in small chunks because a multi-megabyte contiguous
    // block is unavailable after several ANM archives have loaded.
    static constexpr i32 kBulletChunkCapacity = 64;
    static constexpr i32 kBulletChunkCount =
        (kBulletCapacity + kBulletChunkCapacity - 1) / kBulletChunkCapacity;
    Bullet *bulletChunks[kBulletChunkCount];
    bool PspEnsureBulletPool();
    void PspReleaseBulletPool();
#else
    Bullet bullets[kBulletCapacity + 1];
#endif

    Bullet *BulletAt(i32 index)
    {
#if defined(TH07_PSP_1000)
        return bulletChunks[index / kBulletChunkCapacity] + index % kBulletChunkCapacity;
#else
        return &bullets[index];
#endif
    }
    Laser lasers[64];
    i32 bulletCount;
    i32 screenClearTime;
    ZunTimer time;
    i32 updateCount;
    const char *etamaAnmPath;
    Bullet *bulletsPtrs[6];
#if defined(TH07_PSP_1000)
    i32 pspNextBulletIndex;
#else
    Bullet *bulletsStart;
#endif
    ItemType itemType;
#if defined(TH07_PSP)
    // Even the compact PSP-1000 Bullet payload is large. Avoid reading state
    // from every empty slot and evicting active bullets from Allegrex's small
    // cache; keep the original update order but consult this occupancy map
    // first.
    u32 pspActiveBulletBits[(kBulletCapacity + 31) / 32];

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    // I-ME1 owns a render-only generation for every stable bullet slot.  A
    // slot is bumped on both acquisition and release so an ME result can never
    // be compared against a later occupant with the same address.
    u32 pspMeRenderSlotGenerations[kBulletCapacity];
#endif

#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
    // I-ME3 publishes a calc-12 fused render snapshot.  Track every manager
    // mutation that can occur after calc 12 so priority-13+ clears, late slot
    // reuse and public bulk operations can only force canonical fallback.
    u32 pspMeRenderMutationEpoch;

    void PspMarkMeRenderMutation()
    {
        if (++pspMeRenderMutationEpoch == 0u)
        {
            ++pspMeRenderMutationEpoch;
        }
    }
#endif

#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    // Opaque, stage-lifetime CPU queue.  The implementation owns one aligned
    // allocation containing all 1,024 records and the six list heads.
    void *pspBulletWarmQueue;
    u32 pspBulletMutationEpoch;

    bool PspEnsureBulletWarmQueue();
    void PspReleaseBulletWarmQueue();

    void PspMarkBulletMutation()
    {
        ++pspBulletMutationEpoch;
    }
#endif

#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    // Opaque 64-byte-aligned, stage-lifetime pool. Records own render-only
    // memoized state; gameplay authority remains in Bullet/AnmVm.
    void *pspBulletStaticProxyPool;
    u32 pspBulletStaticProxyMutationEpoch;

    bool PspEnsureBulletStaticProxyPool();
    void PspReleaseBulletStaticProxyPool();
    void PspInvalidateBulletStaticProxy(Bullet *bullet);

    void PspMarkBulletStaticProxyMutation()
    {
        ++pspBulletStaticProxyMutationEpoch;
    }
#endif

    bool PspIsBulletSlotTracked(i32 index) const
    {
        return (pspActiveBulletBits[index >> 5] & (1u << (index & 31))) != 0;
    }

    void PspTrackBulletSlot(i32 index)
    {
        pspActiveBulletBits[index >> 5] |= 1u << (index & 31);
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
        if (++pspMeRenderSlotGenerations[index] == 0u)
        {
            ++pspMeRenderSlotGenerations[index];
        }
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
        PspMarkMeRenderMutation();
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
        PspMarkBulletMutation();
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
        Bullet *bullet = BulletAt(index);
        bullet->pspStaticProxySlot = static_cast<u16>(index);
        PspInvalidateBulletStaticProxy(bullet);
#endif
    }

    void PspForgetBulletSlot(i32 index)
    {
        pspActiveBulletBits[index >> 5] &= ~(1u << (index & 31));
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
        if (++pspMeRenderSlotGenerations[index] == 0u)
        {
            ++pspMeRenderSlotGenerations[index];
        }
#endif
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
        PspMarkMeRenderMutation();
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
        PspMarkBulletMutation();
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
        PspInvalidateBulletStaticProxy(BulletAt(index));
#endif
    }
#endif
};

extern BulletManager g_BulletManager;

#if defined(TH07_PSP_PERF_M3)
struct Th07PspM3PerfWindow
{
    unsigned long long callbackUs;
    unsigned long long laserUs;
    unsigned long long itemUs;
    unsigned long long bulletUs;
    unsigned long long sampledBulletDrawUs;
    unsigned int frames;
    unsigned int activeLasers;
    unsigned int activeItems;
    unsigned int bulletVisits;
    unsigned int sampledBulletDraws;
};

void Th07PspTakeM3PerfWindow(Th07PspM3PerfWindow *window);
#endif

#if defined(TH07_PSP_PERF_DENSE_SLICE)
struct Th07PspDenseSliceWindow
{
    unsigned long long updateCallbackUs;
    unsigned long long updateItemUs;
    unsigned long long updateBulletUs;
    unsigned long long updateTailUs;
    unsigned long long drawCallbackUs;
    unsigned long long drawLaserUs;
    unsigned long long drawItemUs;
    unsigned long long drawBulletUs;
    unsigned long long updateBulletPopulation;
    unsigned long long bulletVisits;
    unsigned long long onePassAccepts;
    unsigned long long onePassFallbacks;
    unsigned long long canonicalDrawCalls;
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    // I-ME6 aggregate-only attribution.  These counters are updated once per
    // frame, never from the per-Bullet loop, so the acceptance measurement
    // does not become a new hot-path cost.
    unsigned long long meBulletFastActive;
    unsigned long long meBulletFastCandidates;
    unsigned long long meBulletFastNoCollision;
    unsigned long long meBulletFastScWritebackUs;
    unsigned long long meBulletFastDispatchWaitUs;
    unsigned long long meBulletFastScInvalidateUs;
    unsigned long long meBulletFastKernelCycles;
    unsigned int meBulletFastAttempts;
    unsigned int meBulletFastCompleted;
    unsigned int meBulletFastFallbacks;
#endif
#if defined(TH07_PSP_BULLET_WARM_QUEUE)
    unsigned int warmQueueReadyFrames;
    unsigned int warmQueueFallbackFrames;
#endif
#if defined(TH07_PSP_BULLET_STATIC_PROXY)
    unsigned int staticProxyReadyFrames;
    unsigned int staticProxyFallbackFrames;
    unsigned long long staticProxyVisitHits;
    unsigned long long staticProxyCanonicalFallbacks;
#endif
    unsigned int updateFrames;
    unsigned int drawFrames;
};

void Th07PspTakeDenseSliceWindow(Th07PspDenseSliceWindow *window);
#endif

#if defined(TH07_PSP_ME_RENDER_WORKER)
// M-ME0B shadow-only telemetry.  The ME stream is never submitted to GE;
// `wouldConsume` means that it passed every gate at the canonical bullet draw
// deadline and therefore could have been consumed by a later correctness
// increment.  SYNTH4 deliberately overproduces four vertices per record and
// is not the accepted UQ 2/4-vertex runtime ABI.
struct Th07PspMeRenderShadowWindow
{
    unsigned long long snapshotUs;
    unsigned long long inputBytes;
    unsigned long long outputBytes;
    unsigned long long scWritebackUs;
    unsigned long long scOutputPrepareUs;
    unsigned long long scSubmitUs;
    unsigned long long scInvalidateUs;
    unsigned long long scCopyUs;
    unsigned long long dispatchUs;
    unsigned long long meInvalidateCycles;
    unsigned long long meKernelCycles;
    unsigned long long meWritebackCycles;
    unsigned long long records;
    unsigned long long targetRecords;
    unsigned long long targetOutputBytes;
    unsigned long long deadlineProbeUs;
    unsigned int eligible;
    unsigned int meRenderSubmitted;
    unsigned int deadlines;
    unsigned int meRenderCompleted;
    unsigned int wouldConsume;
    unsigned int notReady;
    unsigned int lateRetired;
    unsigned int signatureDrop;
    unsigned int fcrDrop;
    unsigned int epochDrop;
    unsigned int stageEpochDrop;
    unsigned int managerEpochDrop;
    unsigned int replayEpochDrop;
    unsigned int generationDrop;
    unsigned int boundsDrop;
    unsigned int busy;
    unsigned int timeouts;
    unsigned int quarantined;
    unsigned int fallbackFrames;
    unsigned int deadlineFault;
    unsigned int coverageDrop;
    unsigned int beginFail;
    unsigned int protocolFault;
    unsigned int sampleCount;
    unsigned int kernelSampleCount;
    unsigned int sampleOverflow;
    unsigned int slackMinUs;
    unsigned int slackP50Us;
    unsigned int slackP95Us;
    unsigned int slackP99Us;
    unsigned int kernelCycleMin;
    unsigned int kernelCycleP50;
    unsigned int kernelCycleP95;
    unsigned int kernelCycleP99;
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    unsigned int streamSubmitted;
    unsigned int streamReady;
    unsigned int streamCompared;
    unsigned int streamMismatch;
    unsigned int streamSizeMismatch;
    unsigned int streamVertexMismatch;
    unsigned int streamRunMismatch;
    unsigned int streamHashMismatch;
    unsigned int streamHeaderDrop;
    unsigned int streamIdentityDrop;
    unsigned int streamMixedPrimitiveFrames;
    unsigned int streamReleaseFault;
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    unsigned int streamGeFrames;
    unsigned int streamGeRuns;
    unsigned int streamGeVertices;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    unsigned long long streamItemRecords;
    unsigned long long streamItemRuns;
    unsigned long long streamItemVertices;
    unsigned long long streamItemSuffixRecords;
    unsigned int streamItemEligible;
    unsigned int streamItemAccepted;
    unsigned int streamItemFallback;
    unsigned int streamItemReject;
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
    // Keep admission refusals distinct from worker/consumer rejection.
    unsigned int streamItemCandidates;
    unsigned int streamItemBudgetReject;
    unsigned int streamItemBusyVeto;
    unsigned long long streamItemCandidateRecords;
    unsigned long long streamItemPredictedTicksMax;
    unsigned int streamItemCandidateMax;
    unsigned int streamItemBudgetRejectMax;
#endif
#endif
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    // I-ME7 attribution is aggregate-only. No timer is read in the per-Bullet
    // loop; local counters are committed once after the canonical pass.
    unsigned long long compactSeedCandidates;
    unsigned long long compactMotionHits;
    unsigned long long compactBoundsHits;
    unsigned long long compactCollisionHits;
    unsigned long long compactBroadphaseHits;
    unsigned long long compactKernelCycles;
    unsigned long long compactSeedInvalidateUs;
    unsigned long long compactOutputInvalidateUs;
    unsigned int compactLaunchAttempts;
    unsigned int compactLaunchBegun;
    unsigned int compactLaunchBusy;
    unsigned int compactReady;
    unsigned int compactSeedOnlyFrames;
    unsigned int compactReject;
    unsigned int compactJitReject;
    unsigned int compactCollisionLatch;
    unsigned int compactP12HeadPending;
    unsigned int compactP12TailPending;
    unsigned int compactBlockedRender;
    unsigned int compactTeardownDrain;
    unsigned int compactProtocolFault;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    unsigned long long compactItemMotionCandidates;
    unsigned long long compactItemMotionProcessed;
    unsigned long long compactItemMotionJitCandidates;
    unsigned long long compactItemMotionAdopted;
    unsigned int compactItemMotionLaunch;
    unsigned int compactItemMotionReady;
    unsigned int compactItemMotionPendingAtItem;
    unsigned int compactItemMotionSlotReject;
    unsigned int compactItemMotionGlobalReject;
#endif
#endif
    unsigned int streamFirstMismatchKind;
    unsigned int streamFirstMismatchWord;
    unsigned int streamFirstMismatchExpected;
    unsigned int streamFirstMismatchActual;
#endif
};

// Process-level availability is set only after the model-3 custom core and
// M-ME0A boot bench have succeeded. GameWindow captures the priority-18
// completion serial immediately before each ordinary RunCalcChain pass and
// gives it back to AfterCalc. This prevents a warm-up or early BREAK from
// publishing stale work. Fixed-30 update-only passes pass nextDraw=false.
void Th07PspMeRenderSetAvailable(bool available);
unsigned int Th07PspMeRenderCaptureCalcSerial();
void Th07PspMeRenderAfterCalc(unsigned int serialBefore, bool nextDraw);
void Th07PspTakeMeRenderShadowWindow(Th07PspMeRenderShadowWindow *window);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
// Called only after the renderer's list-completion fence.  A failed ownership
// transition is process-fatal for this experimental path and disables future
// ME publications without touching the already completed GE list.
void Th07PspMeRenderGeReleaseFault();
#endif
#endif
