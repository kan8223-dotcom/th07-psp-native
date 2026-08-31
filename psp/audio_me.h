#pragma once

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME) && \
    !defined(TH07_PSP_ME_RENDER_CORRECTNESS)
#error "ME render GE consumption requires the correctness stream owner"
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME) && \
    !defined(TH07_PSP_ME_RENDER_WORKER)
#error "ME render GE consumption requires the ME render worker"
#endif
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST) && \
    !defined(TH07_PSP_ME_RENDER_RAW_LIVE)
#error "I-ME5 direct-list traversal requires the I-ME4 raw-live profile"
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE) && \
    !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#error "I-ME6 bullet fast update requires the I-ME5 direct-list profile"
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE) && \
    !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#error "compact bullet update requires the I-ME5 direct-list profile"
#endif
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM) && \
    !defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
#error "effect render stream shares the fixed I-ME7 auxiliary pool"
#endif
#if defined(TH07_PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP)
#error "lean render cache ownership is hardware-rejected; keep the proven full cache fences"
#endif
#if (defined(TH07_PSP_ME_RENDER_UV16) || \
     defined(TH07_PSP_ME_RENDER_XYZ16)) && \
    !defined(TH07_PSP_ME_RENDER_CORRECTNESS)
#error "C1 packed vertices require the correctness stream owner"
#endif
#if (defined(TH07_PSP_ME_RENDER_UV16) || \
     defined(TH07_PSP_ME_RENDER_XYZ16)) && \
    defined(TH07_PSP_1000)
#error "C1 packed vertices are PSP-2000+ research only"
#endif
#if (defined(TH07_PSP_ME_RENDER_UV16) || \
     defined(TH07_PSP_ME_RENDER_XYZ16)) && \
    defined(TH07_PSP_ME_RENDER_GE_CONSUME) && \
    !defined(TH07_PSP_ME_RENDER_16BIT_GE_EXPERIMENT)
#error "C1 packed GE consumption requires the explicit readback experiment gate"
#endif
#if defined(TH07_PSP_ME_RENDER_16BIT_GE_EXPERIMENT) && \
    !defined(TH07_PSP_ME_RENDER_UV16) && \
    !defined(TH07_PSP_ME_RENDER_XYZ16)
#error "C1 packed GE experiment has no packed vertex component"
#endif
#if (defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM) || \
     defined(TH07_PSP_ME_BULLET_SEED_SLIM) || \
     defined(TH07_PSP_ME_ITEM_SEED_SLIM)) && \
    !defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
#error "C2 compact arenas require the compact bullet worker"
#endif
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM) && \
    !defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
#error "C2 Item seed packing requires the Item motion sidecar"
#endif
#if (defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM) || \
     defined(TH07_PSP_ME_BULLET_SEED_SLIM) || \
     defined(TH07_PSP_ME_ITEM_SEED_SLIM)) && \
    defined(TH07_PSP_1000)
#error "C2 compact arenas are PSP-2000+ research only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    TH07_PSP_ME_MAX_MIX_INPUTS = 64,
    TH07_PSP_ME_MAX_MIX_FRAMES = 1024,
    TH07_PSP_MIX_S16 = 0,
    TH07_PSP_MIX_MULAW8 = 1
};

typedef struct Th07PspMixInput
{
    const void *samples;
    // Total source frames.  Effects remain at their native sample rate, so ME
    // performs the same 16.16 nearest-neighbour stepping as the SC fallback.
    // PSP-1000 stores mono SFX as full-rate G.711 mu-law; BGM and the standard
    // PSP build use signed 16-bit PCM.
    unsigned int frames;
    unsigned int destinationFrame;
    unsigned int channels;
    unsigned int sourceFrame;
    unsigned int sourceFraction;
    unsigned int stepFixed;
    unsigned int gainQ16;
    // Set only for mutable input (the freshly assembled BGM block).  Loaded
    // SFX are immutable and are written back once by LoadSound().
    unsigned int needsWriteback;
    unsigned int sampleFormat;
} Th07PspMixInput;

typedef struct Th07PspMixJob
{
    unsigned int frames;
    unsigned int inputCount;
    unsigned int mixDivisor;
    Th07PspMixInput inputs[TH07_PSP_ME_MAX_MIX_INPUTS];
} Th07PspMixJob;

// Returns 1 when ME produced this block, 0 when the identical SC fallback did.
// Either return value leaves `output` ready for the existing software ring.
int th07_psp_me_audio_mix(const Th07PspMixJob *job, short *output);
// Run the identical integer mixer synchronously on the main CPU.  TH07's
// audio output thread has only one 512-frame block of deadline slack, so SFX
// jobs must not pay a blocking ME round trip before submitting that block.
// Accumulate SFX in the internal 32-bit bus and add them directly to `io`.
// Only the SFX contribution is limited against each untouched BGM sample's
// remaining signed-16-bit headroom; no intermediate 16-bit clip is possible.
// This entry point uses one internal wide bus and is intentionally
// non-reentrant; call it only from TH07's single audio-output thread.
int th07_psp_sc_audio_mix_into(const Th07PspMixJob *job, short *io,
                               unsigned int *limitedSamples);
int th07_psp_me_audio_init(void);
void th07_psp_me_audio_shutdown(void);
void th07_psp_me_audio_diag_window(unsigned int *jobs, unsigned int *fallbacks,
                                    unsigned int *timeouts, unsigned int *maxWaitUs);

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
// PSP-3000 diagnostic backend: the custom ME worker owns the exact
// profile-specific local-eDRAM extent.  The calls return 1 only when the
// command completed.  A generation change may cancel an in-flight
// upload/fetch without making the worker unsafe.
int th07_psp_me_bgm_reset(unsigned int generation);
int th07_psp_me_bgm_upload(const void *source, unsigned int bytes,
                           unsigned int generation, unsigned int ringOffset);
int th07_psp_me_bgm_fetch(void *destination, unsigned int bytes,
                          unsigned int generation, unsigned int ringOffset);
void th07_psp_me_bgm_commit_owned(void);
int th07_psp_me_bgm_is_active(void);
void th07_psp_me_bgm_extent(unsigned int *base, unsigned int *bytes);
int th07_psp_me_audio_faulted(void);
int th07_psp_me_audio_reset_committed(void);
void th07_psp_me_audio_suspend_latch(void);
void th07_psp_me_audio_diag_snapshot(unsigned int *jobs, unsigned int *fallbacks,
                                     unsigned int *timeouts, unsigned int *maxWaitUs);
#endif

#if defined(TH07_PSP_MECC_AUDIO_4M)
enum
{
    TH07_PSP_ME_SFX_MAX_VOICES = 16,
    TH07_PSP_ME_SFX_MAX_MIX_FRAMES = 512
};

typedef struct Th07PspMeSfxVoice
{
    // One logical mono-s16 sound may concatenate a duplicated-prefix replica
    // and its canonical suffix.  Canonical playback uses segment1Frames=0.
    // Offsets are byte offsets in local-eDRAM's lower 2 MiB SFX atlas; zero is
    // a valid segment0 offset.
    unsigned int segment0Offset;
    unsigned int segment0Frames;
    unsigned int segment1Offset;
    unsigned int segment1Frames;
    unsigned int sourceFrame;
    unsigned int sourceFraction;
    unsigned int stepFixed;
    unsigned int gainQ16;
} Th07PspMeSfxVoice;

typedef struct Th07PspMeSfxMixJob
{
    unsigned int frames;
    unsigned int voiceCount;
    Th07PspMeSfxVoice voices[TH07_PSP_ME_SFX_MAX_VOICES];
} Th07PspMeSfxMixJob;

// Upload one cache-line-complete block to the lower 2 MiB SFX atlas.
int th07_psp_me_sfx_upload(const void *source, unsigned int bytes,
                           unsigned int atlasOffset);
// Gather one or two even-byte atlas segments into consecutive, 64-byte-aligned
// Main RAM.  Their combined byte count must be cache-line complete.  Set
// bytes1 and atlasOffset1 to zero to omit the second segment.
int th07_psp_me_sfx_gather(void *destination,
                           unsigned int bytes0, unsigned int atlasOffset0,
                           unsigned int bytes1, unsigned int atlasOffset1);
// Mix at most 16 atlas-backed voices directly on ME.  Preserve the resulting
// stereo-s32 bus (at most 4 KiB) in aligned Main RAM so opposite-polarity BGM
// can cancel it before the one and only signed-16-bit saturation.  The
// requested frame count must be a multiple of 16.
int th07_psp_me_sfx_mix(const Th07PspMeSfxMixJob *job, int *output);
void th07_psp_me_sfx_extent(unsigned int *base, unsigned int *bytes);
int th07_psp_me_audio_stack_guard_ok(void);
int th07_psp_me_audio_power_locked(void);
#endif

// Pack one engine vertex stream into the native interleaved GE layout on ME.
// A successful output remains valid until th07_psp_me_vertex_frame_begin(),
// which the renderer calls only after the previous GE list has completed.
typedef struct Th07PspMeVertexPack
{
    const void *position;
    const void *texcoord;
    const void *diffuse;
    unsigned int positionStride;
    unsigned int texcoordStride;
    unsigned int diffuseStride;
    unsigned int count;
    unsigned int textured;
    unsigned int colored;
} Th07PspMeVertexPack;

void th07_psp_me_vertex_frame_begin(void);
int th07_psp_me_vertex_pack(const Th07PspMeVertexPack *job, const void **output);

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
// I-ME6 synchronous negative-collision assist.  ME reads the frozen live
// Bullet/sprite/generation authority but never writes an engine object.  Its
// only output is this process-lifetime, slot-indexed result arena, so every
// completed reject can fall back to the untouched canonical SC update.
enum
{
    TH07_PSP_ME_BULLET_FAST_UPDATE_VERSION = 0x4d453136u, // "ME16"
    TH07_PSP_ME_BULLET_FAST_LAYOUT_VERSION = 0x42463131u, // "BF11"
    TH07_PSP_ME_BULLET_FAST_MAX_SLOTS = 1024,
    TH07_PSP_ME_BULLET_FAST_ACTIVE_WORDS = 32,

    TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE = 1u << 0,
    TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS = 1u << 1,
    TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION = 1u << 2,

    TH07_PSP_ME_BULLET_FAST_JOB_OK = 0,
    TH07_PSP_ME_BULLET_FAST_JOB_VERSION = 1,
    TH07_PSP_ME_BULLET_FAST_JOB_BOUNDS = 2,
    TH07_PSP_ME_BULLET_FAST_JOB_RECORD = 3,
    TH07_PSP_ME_BULLET_FAST_JOB_PROTOCOL = 4,
    TH07_PSP_ME_BULLET_FAST_JOB_GUARD = 5
};

// All offsets are supplied explicitly, then admitted only when they equal the
// frozen PSP-2000+ engine ABI.  Dynamic bases remain stage/frame authority.
typedef struct Th07PspMeBulletFastLayout
{
    unsigned int layoutVersion;
    unsigned int layoutBytes;
    unsigned int bulletBasePhys;
    unsigned int bulletStride;
    unsigned int bulletCount;
    unsigned int generationBasePhys;
    unsigned int generationStride;
    unsigned int generationCount;
    unsigned int activeBitsPhys;
    unsigned int activeBitsWordCount;
    unsigned int spriteBasePhys;
    unsigned int spriteStride;
    unsigned int spriteCount;
    unsigned int bulletStateOffset;
    unsigned int bulletPosXOffset;
    unsigned int bulletPosYOffset;
    unsigned int bulletPosZOffset;
    unsigned int bulletVelocityXOffset;
    unsigned int bulletVelocityYOffset;
    unsigned int bulletVelocityZOffset;
    unsigned int bulletSpawnDelayOffset;
    unsigned int bulletExFlagsOffset;
    unsigned int bulletOutOfBoundsTimeOffset;
    unsigned int bulletCurrentCommandIndexOffset;
    unsigned int bulletCommandsOffset;
    unsigned int bulletCommandStride;
    unsigned int bulletCommandTypeOffset;
    unsigned int bulletGrazeSizeXOffset;
    unsigned int bulletGrazeSizeYOffset;
    unsigned int vmSpriteOffset;
    unsigned int spriteWidthOffset;
    unsigned int spriteHeightOffset;
    unsigned int bombClearStride;
    unsigned int bombClearPosXOffset;
    unsigned int bombClearPosYOffset;
    unsigned int bombClearPosZOffset;
    unsigned int bombClearSizeXOffset;
    unsigned int bombClearSizeYOffset;
} Th07PspMeBulletFastLayout;

typedef struct Th07PspMeBulletFastJob
{
    unsigned int version;
    unsigned int frameSeq;
    Th07PspMeBulletFastLayout layout;
    unsigned int playerState;
    unsigned int playerGrazeLeftBits;
    unsigned int playerGrazeTopBits;
    unsigned int playerGrazeRightBits;
    unsigned int playerGrazeBottomBits;
    unsigned int playerHitboxLeftBits;
    unsigned int playerHitboxTopBits;
    unsigned int playerHitboxRightBits;
    unsigned int playerHitboxBottomBits;
    unsigned int bombClearBasePhys;
    unsigned int bombClearHighWater;
    unsigned int bombClearCapacity;
    unsigned int playfieldRightBits;
    unsigned int playfieldBottomBits;
} Th07PspMeBulletFastJob;

// The generation is intentionally a synchronous low-16 echo.  SC must still
// recheck the live active bit and `(unsigned short)generation` immediately
// before using a slot.  Position includes Z: canonical `pos += velocity`
// updates all three components even though collision consumes only X/Y.
typedef struct Th07PspMeBulletFastSlotResult
{
    unsigned int posXBits;
    unsigned int posYBits;
    unsigned int posZBits;
    unsigned short generation;
    unsigned short flags;
} Th07PspMeBulletFastSlotResult;

typedef struct Th07PspMeBulletFastOutput
{
    unsigned int candidateBits[TH07_PSP_ME_BULLET_FAST_ACTIVE_WORDS];
    Th07PspMeBulletFastSlotResult slots[TH07_PSP_ME_BULLET_FAST_MAX_SLOTS];
} Th07PspMeBulletFastOutput;

typedef struct Th07PspMeBulletFastCompletion
{
    unsigned int version;
    unsigned int frameSeq;
    unsigned int result;
    unsigned int activeCount;
    unsigned int candidateCount;
    unsigned int inBoundsCount;
    unsigned int noCollisionCount;
    unsigned int firstBadSlot;
    unsigned int scWritebackUs;
    unsigned int dispatchWaitUs;
    unsigned int scInvalidateUs;
    unsigned int meInvalidateCycles;
    unsigned int meKernelCycles;
    unsigned int meWritebackCycles;
    unsigned int meFcr31Before;
    unsigned int meFcr31Effective;
    unsigned int meFcr31After;
} Th07PspMeBulletFastCompletion;

// Return 1 only for an intact, completed result arena.  Return 0 means ME was
// not started (including audio priority) or completed with a safe reject, so
// canonical SC update may run.  Return -1 means a published live-reader did
// not prove completion; the caller must fail-stop and must not mutate/free the
// supplied authority.
int th07_psp_me_bullet_fast_update_run(
    const Th07PspMeBulletFastJob *job,
    Th07PspMeBulletFastCompletion *completion,
    const Th07PspMeBulletFastOutput **output);
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
// I-ME7 removes I-ME6's second scattered traversal.  I-ME5 emits this
// immutable, contiguous seed as a side effect of the direct-list render walk;
// a following asynchronous ME job consumes it without touching Bullet or
// AnmVm.  Main RAM is the required v1 backend so SC can compare every input
// bit at the canonical adoption point.  ME-local eDRAM is deliberately only a
// future backend flag, never an implicit authority change.
enum
{
#if defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
    TH07_PSP_ME_BULLET_OUTPUT_ABI_BIAS = 0x00000100u,
#else
    TH07_PSP_ME_BULLET_OUTPUT_ABI_BIAS = 0u,
#endif
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    TH07_PSP_ME_BULLET_SEED_ABI_BIAS = 0x00000200u,
#else
    TH07_PSP_ME_BULLET_SEED_ABI_BIAS = 0u,
#endif
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    TH07_PSP_ME_ITEM_SEED_ABI_BIAS = 0x00000400u,
#else
    TH07_PSP_ME_ITEM_SEED_ABI_BIAS = 0u,
#endif
    TH07_PSP_ME_BULLET_COMPACT_VERSION =
        0x4d453137u + TH07_PSP_ME_BULLET_OUTPUT_ABI_BIAS +
        TH07_PSP_ME_BULLET_SEED_ABI_BIAS +
        TH07_PSP_ME_ITEM_SEED_ABI_BIAS,
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION = 0x42533132u, // "BS12"
#else
    TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION = 0x42533131u, // "BS11"
#endif
    TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS = 1024,
    TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS = 32,
    TH07_PSP_ME_BULLET_COMPACT_BANKS = 2,
    TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM = 0,
    TH07_PSP_ME_BULLET_COMPACT_BACKEND_ME_EDRAM = 1,
    TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED = 0x434f4d4du, // "COMM"

    TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID = 1u << 0,
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID = 1u << 1,
#endif

    TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE = 1u << 0,
    TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS = 1u << 1,
    TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION = 1u << 2,

    TH07_PSP_ME_BULLET_COMPACT_RESULT_OK = 0,
    TH07_PSP_ME_BULLET_COMPACT_RESULT_VERSION = 1,
    TH07_PSP_ME_BULLET_COMPACT_RESULT_BOUNDS = 2,
    TH07_PSP_ME_BULLET_COMPACT_RESULT_SEED = 3,
    TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD = 4,
    TH07_PSP_ME_BULLET_COMPACT_RESULT_PROTOCOL = 5,
    TH07_PSP_ME_BULLET_COMPACT_RESULT_GUARD = 6
};

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
// A1-MOVE is a segment-local sidecar of command 12.  Command 10 produces a
// compact, double-buffered snapshot while it is already traversing the
// accepted Item prefix.  The next frame's priority-9 command computes only
// pure motion.  SC remains authoritative for lifetime, collision, rewards,
// timers, ANM and list publication.
enum
{
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    TH07_PSP_ME_ITEM_MOTION_VERSION = 0x494d3032u, // "IM02"
#else
    TH07_PSP_ME_ITEM_MOTION_VERSION = 0x494d3031u, // "IM01"
#endif
    TH07_PSP_ME_ITEM_MOTION_OUTPUT_VERSION = 0x494f3031u, // "IO01"
    TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS = 1100,
    TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS = 35,
    // Pad the bitmap to three full cache lines so every slot begins aligned.
    TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS = 48,
    TH07_PSP_ME_ITEM_MOTION_BANKS = 2,
    TH07_PSP_ME_ITEM_MOTION_COMMITTED = 0x434f4d4du, // "COMM"

    TH07_PSP_ME_ITEM_MOTION_INPUT_STATE_MASK = 0x000000ffu,
    TH07_PSP_ME_ITEM_MOTION_INPUT_AUTOCOLLECT_SHIFT = 8,
    TH07_PSP_ME_ITEM_MOTION_INPUT_INUSE_SHIFT = 16,

    TH07_PSP_ME_ITEM_MOTION_RESULT_STATE_MASK = 0x000000ffu,
    TH07_PSP_ME_ITEM_MOTION_RESULT_AUTOCOLLECT_SHIFT = 8,
    TH07_PSP_ME_ITEM_MOTION_RESULT_CANDIDATE = 1u << 16,
    TH07_PSP_ME_ITEM_MOTION_RESULT_GOTO_COLLISION = 1u << 17,
    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_SHIFT = 24,
    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_FALL = 1u << 24,
    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_HOME = 2u << 24,
    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_SPAWN = 3u << 24,
    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_INTERP = 4u << 24,
    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_STATE2_60 = 5u << 24,
    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_STATE2_LATE = 6u << 24,
    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_MASK = 0x0f000000u,

    TH07_PSP_ME_ITEM_MOTION_RESULT_OK = 0,
    TH07_PSP_ME_ITEM_MOTION_RESULT_DISABLED = 1,
    TH07_PSP_ME_ITEM_MOTION_RESULT_VERSION = 2,
    TH07_PSP_ME_ITEM_MOTION_RESULT_SEED = 3,
    TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD = 4,
    TH07_PSP_ME_ITEM_MOTION_RESULT_GUARD = 5,
    TH07_PSP_ME_ITEM_MOTION_RESULT_BOUNDS = 6,
    TH07_PSP_ME_ITEM_MOTION_RESULT_PROTOCOL = 7
};

typedef struct Th07PspMeItemMotionSeedHeader
{
    unsigned int version;
    unsigned int headerBytes;
    unsigned int seedBytes;
    unsigned int bank;
    unsigned int frameSeq;
    unsigned int targetDrawSeq;
    unsigned int stageEpoch;
    unsigned int managerEpoch;
    unsigned int itemPrepareSerial;
    unsigned int recordCount;
    unsigned int totalCount;
    unsigned int candidateCount;
    unsigned int commitSequence;
    unsigned int reserved0;
    unsigned int reserved1;
    unsigned int committed;
} Th07PspMeItemMotionSeedHeader;

// The Item tail is contiguous in the canonical 648-byte object.  Capturing
// all motion inputs costs one compact cache-line per live slot and lets SC
// compare raw bits immediately before adopting a result.
typedef struct Th07PspMeItemMotionSeedSlot
{
    unsigned int generation;
    unsigned int posXBits;
    unsigned int posYBits;
    unsigned int posZBits;
    unsigned int startXBits;
    unsigned int startYBits;
    unsigned int startZBits;
    unsigned int targetXBits;
    unsigned int targetYBits;
    unsigned int targetZBits;
    int timerCurrent;
    unsigned int timerSubFrameBits;
#if !defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    unsigned int stateAndFlags;
    unsigned int reserved0;
    unsigned int reserved1;
    unsigned int reserved2;
#endif
} Th07PspMeItemMotionSeedSlot;

typedef struct Th07PspMeItemMotionSeed
{
    Th07PspMeItemMotionSeedHeader header;
    unsigned int candidateBits[TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS];
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    // C2c stores the three-valued state and autoCollect as dense planes.
    // candidateBits itself proves inUse == 1; capture rejects every other
    // value before publishing the slot, so a redundant fifth plane is not
    // part of the ABI.
    // Every plane has the same padded cache-line shape as candidateBits so a
    // malformed tail or a bit outside candidateBits can fail closed on ME.
    unsigned int stateBit0[TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS];
    unsigned int stateBit1[TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS];
    unsigned int autoCollectBits[TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS];
#endif
    Th07PspMeItemMotionSeedSlot slots[TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS];
} Th07PspMeItemMotionSeed;

typedef struct Th07PspMeItemMotionOutputHeader
{
    unsigned int version;
    unsigned int headerBytes;
    unsigned int outputBytes;
    unsigned int bank;
    unsigned int frameSeq;
    unsigned int seedFrameSeq;
    unsigned int seedTargetDrawSeq;
    unsigned int result;
    unsigned int candidateLimit;
    unsigned int candidateCount;
    unsigned int processedCount;
    unsigned int firstBadSlot;
    unsigned int reserved0;
    unsigned int reserved1;
    unsigned int reserved2;
    unsigned int committed;
} Th07PspMeItemMotionOutputHeader;

typedef struct Th07PspMeItemMotionSlotResult
{
    unsigned int generation;
    unsigned int posXBits;
    unsigned int posYBits;
    unsigned int posZBits;
    unsigned int startXBits;
    unsigned int startYBits;
    unsigned int startZBits;
    unsigned int stateAndRoute;
} Th07PspMeItemMotionSlotResult;

typedef struct Th07PspMeItemMotionOutput
{
    Th07PspMeItemMotionOutputHeader header;
    unsigned int candidateBits[TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS];
    Th07PspMeItemMotionSlotResult slots[TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS];
} Th07PspMeItemMotionOutput;
#endif

// Slot is implicit in the array index.  SC compares the generation and all
// ten raw input words below against the live Bullet/sprite at JIT adoption;
// no hash or manager-wide epoch is accepted as a substitute for those exact
// bit checks.  I-ME5 also computes motion plus the static playfield bound in
// the same already-paid live-list traversal.  Thus these results remain
// usable after exact JIT validation even when the following collision-only
// command has not completed by priority 12.
typedef struct Th07PspMeBulletCompactSeedSlot
{
    unsigned int generation;
    unsigned int posXBits;
    unsigned int posYBits;
    unsigned int posZBits;
    unsigned int velocityXBits;
    unsigned int velocityYBits;
    unsigned int velocityZBits;
    unsigned int spriteWidthBits;
    unsigned int spriteHeightBits;
    unsigned int grazeSizeXBits;
    unsigned int grazeSizeYBits;
    unsigned int nextPosXBits;
    unsigned int nextPosYBits;
    unsigned int nextPosZBits;
#if !defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    unsigned int staticFlags;
    unsigned int reserved;
#endif
} Th07PspMeBulletCompactSeedSlot;

// Exactly one cache line. `committed` is written last by ME and is accepted
// only after the owning I-ME5 render transaction completed successfully.
typedef struct Th07PspMeBulletCompactSeedHeader
{
    unsigned int version;
    unsigned int headerBytes;
    unsigned int seedBytes;
    unsigned int backend;
    unsigned int bank;
    unsigned int frameSeq;
    unsigned int targetDrawSeq;
    unsigned int stageEpoch;
    unsigned int managerEpoch;
    // Capture-time replay frame echo only. ReplayManager::frameId normally
    // advances before next-frame adoption and must not be compared to current.
    unsigned int replayEpoch;
    unsigned int recordCount;
    unsigned int candidateCount;
    unsigned int payloadHash;
    unsigned int commitSequence;
    unsigned int reserved;
    unsigned int committed;
} Th07PspMeBulletCompactSeedHeader;

typedef struct Th07PspMeBulletCompactSeed
{
    Th07PspMeBulletCompactSeedHeader header;
    unsigned int candidateBits[TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS];
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    unsigned int inBoundsBits[TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS];
#endif
    Th07PspMeBulletCompactSeedSlot
        slots[TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS];
} Th07PspMeBulletCompactSeed;

// Captured after Player priority 8.  A priority-9 launcher may overlap Enemy,
// Effect and Item; SC must clear use of NO_COLLISION if these exact global
// fields no longer match at priority 12.  Per-slot motion remains independently
// usable after its seed/live raw-bit comparison succeeds.
typedef struct Th07PspMeBulletCompactJob
{
    unsigned int version;
    unsigned int frameSeq;
    unsigned int flags;
    unsigned int seedBank;
    unsigned int seedFrameSeq;
    unsigned int seedTargetDrawSeq;
    unsigned int stageEpoch;
    unsigned int managerEpoch;
    unsigned int replayEpoch;
    unsigned int playerState;
    unsigned int playerGrazeLeftBits;
    unsigned int playerGrazeTopBits;
    unsigned int playerGrazeRightBits;
    unsigned int playerGrazeBottomBits;
    unsigned int playerHitboxLeftBits;
    unsigned int playerHitboxTopBits;
    unsigned int playerHitboxRightBits;
    unsigned int playerHitboxBottomBits;
    unsigned int bombClearBasePhys;
    unsigned int bombClearHighWater;
    unsigned int bombClearCapacity;
    unsigned int playfieldRightBits;
    unsigned int playfieldBottomBits;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    // Captured after Player priority 8.  Every value is checked again at the
    // slot's canonical Item update point; an earlier Item acquisition can
    // therefore close adoption for the remaining suffix without waiting.
    unsigned int itemMotionCandidateLimit;
    unsigned int itemPlayerPosXBits;
    unsigned int itemPlayerPosYBits;
    unsigned int itemCollectSpeedBits;
    unsigned int itemPocYBits;
    unsigned int itemFramerateMultiplierBits;
    int itemCurrentPowerClass;
    int itemDifficulty;
    unsigned int itemHasBorder;
#endif
} Th07PspMeBulletCompactJob;

typedef struct Th07PspMeBulletCompactSlotResult
{
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
    unsigned int posXBits;
    unsigned int posYBits;
    unsigned int posZBits;
#endif
    unsigned short generation;
    unsigned short flags;
} Th07PspMeBulletCompactSlotResult;

typedef struct Th07PspMeBulletCompactOutput
{
    unsigned int candidateBits[TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS];
    Th07PspMeBulletCompactSlotResult
        slots[TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS];
} Th07PspMeBulletCompactOutput;

typedef struct Th07PspMeBulletCompactCompletion
{
    unsigned int version;
    unsigned int frameSeq;
    unsigned int seedFrameSeq;
    unsigned int seedTargetDrawSeq;
    unsigned int result;
    unsigned int candidateCount;
    unsigned int inBoundsCount;
    unsigned int noCollisionCount;
    unsigned int firstBadSlot;
    unsigned int dispatchAgeUs;
    unsigned int scSeedInvalidateUs;
    unsigned int scOutputInvalidateUs;
    unsigned int meInvalidateCycles;
    unsigned int meKernelCycles;
    unsigned int meWritebackCycles;
    unsigned int meFcr31Before;
    unsigned int meFcr31Effective;
    unsigned int meFcr31After;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    unsigned int itemResult;
    unsigned int itemCandidateCount;
    unsigned int itemProcessedCount;
    unsigned int itemFirstBadSlot;
#endif
} Th07PspMeBulletCompactCompletion;

// Nonblocking two-phase API. begin() returns one only after publishing the
// compact command. poll() returns 0 while it is still running, 1 for a valid
// result, -1 for a completed safe reject/no active command, and -2 only for a
// poisoned/timeout protocol state.  On return 1 both pointers remain stable
// until the next successful begin() or I-ME5 production of the same bank.
// A caller retaining seed_bank() across a pending poll may still adopt its
// precomputed motion/static bound.  For every adoption it must first compare
// live full-u32 generation == seed full-u32 generation, then all ten raw seed
// inputs; the u16 generation in output is only a post-seed echo.
int th07_psp_me_bullet_compact_begin(
    const Th07PspMeBulletCompactJob *job);
int th07_psp_me_bullet_compact_poll(
    Th07PspMeBulletCompactCompletion *completion,
    const Th07PspMeBulletCompactOutput **output,
    const Th07PspMeBulletCompactSeed **seed);
const Th07PspMeBulletCompactSeed *
th07_psp_me_bullet_compact_seed_bank(unsigned int bank);
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
const Th07PspMeItemMotionSeed *
th07_psp_me_item_motion_seed_bank(unsigned int bank);
const Th07PspMeItemMotionOutput *
th07_psp_me_item_motion_last_output(void);
// Startup-gated optional segment.  Zero means command 10 does not capture the
// sidecar and ItemManager remains entirely canonical on SC; Bullet and Item
// draw availability are independent.
int th07_psp_me_item_motion_available(void);

enum
{
    TH07_PSP_ME_ITEM_MOTION_DIAG_SCHEMA = 1,

    TH07_PSP_ME_ITEM_MOTION_STATE_UNAVAILABLE = 0,
    TH07_PSP_ME_ITEM_MOTION_STATE_TESTING = 1,
    TH07_PSP_ME_ITEM_MOTION_STATE_ENABLED = 2,
    TH07_PSP_ME_ITEM_MOTION_STATE_SAFE_FALLBACK = 3,
    TH07_PSP_ME_ITEM_MOTION_STATE_FAILED = 4,

    TH07_PSP_ME_ITEM_MOTION_REASON_NONE = 0,
    TH07_PSP_ME_ITEM_MOTION_REASON_ME_UNAVAILABLE = 1,
    TH07_PSP_ME_ITEM_MOTION_REASON_ITEM_DRAW_UNAVAILABLE = 2,
    TH07_PSP_ME_ITEM_MOTION_REASON_SELFTEST_PASS = 3,
    TH07_PSP_ME_ITEM_MOTION_REASON_BEGIN = 4,
    TH07_PSP_ME_ITEM_MOTION_REASON_BULLET_CONTRACT = 5,
    TH07_PSP_ME_ITEM_MOTION_REASON_ITEM_CONTRACT = 6,
    TH07_PSP_ME_ITEM_MOTION_REASON_BIT_MISMATCH = 7,
    TH07_PSP_ME_ITEM_MOTION_REASON_COMMON_FATAL = 8,
    TH07_PSP_ME_ITEM_MOTION_REASON_BULLET_RETRY_FAILED = 9,
};

typedef struct Th07PspMeItemMotionDiag
{
    unsigned int state;
    unsigned int reason;
    unsigned int selftestRuns;
    unsigned int selftestFailures;
    unsigned int bulletRetryRuns;
    unsigned int bulletRetryPasses;
    int lastPollResult;
    unsigned int lastBulletResult;
    unsigned int lastItemResult;
    unsigned int firstMismatchSlot;
} Th07PspMeItemMotionDiag;

void th07_psp_me_item_motion_diag_snapshot(
    Th07PspMeItemMotionDiag *snapshot);
#endif
// Stage teardown only. Unlike gameplay poll(), this may wait until the live
// compact owner is released; failure means its Main-RAM authority can no
// longer be reused safely and requires the existing cold-reboot fail-stop.
int th07_psp_me_bullet_compact_drain_live(void);
#endif

#if defined(TH07_PSP_ME_RENDER_WORKER)
// M-ME0A/B research ABI.  This worker is deliberately disconnected from the
// renderer: it expands immutable Main-RAM records into a separate Main-RAM
// vertex stream, and only the shadow/benchmark caller may inspect that stream.
// No pointer in a record is ever dereferenced by ME.
enum
{
    TH07_PSP_ME_RENDER_VERSION = 0x4d455230u, // "MER0"
    TH07_PSP_ME_RENDER_MAX_RECORDS = 1024,
    TH07_PSP_ME_RENDER_VERTICES_PER_RECORD = 4,
    TH07_PSP_ME_RENDER_VERTEX_BYTES = 24,
    TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD =
        TH07_PSP_ME_RENDER_VERTICES_PER_RECORD *
        TH07_PSP_ME_RENDER_VERTEX_BYTES,
    TH07_PSP_ME_RENDER_BENCH_CASES = 30,
    TH07_PSP_ME_RENDER_CACHE_COLD = 0,
    TH07_PSP_ME_RENDER_CACHE_WARM = 1,
    TH07_PSP_ME_RENDER_RECORD_ROTATED = 1u << 0,
    TH07_PSP_ME_RENDER_JOB_COLD_CACHE = 1u << 0
};

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
// I-ME1 correctness-stream ABI.  This is deliberately separate from MER0:
// the boot bench and the M0 shadow caller retain their fixed four-vertex
// output, while this ABI owns three process-lifetime Main-RAM slots and emits
// the accepted mixed GU_SPRITES/UQ stream.  The standard ME11 mode carries no
// address, GE handle or live engine object; I-ME4's explicitly versioned RAW
// flag adds the separately validated live Main-RAM authority below.
enum
{
#if defined(TH07_PSP_ME_RENDER_UV16) && \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS = 0x00000300u,
    TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES = 16,
#elif defined(TH07_PSP_ME_RENDER_UV16) || \
      defined(TH07_PSP_ME_RENDER_XYZ16)
#if defined(TH07_PSP_ME_RENDER_UV16)
    TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS = 0x00000100u,
#else
    TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS = 0x00000200u,
#endif
    TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES = 20,
#else
    TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS = 0u,
    TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES = 24,
#endif
    TH07_PSP_ME_RENDER_STREAM_VERSION =
        0x4d453131u + TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS,
    TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT = 3,
    TH07_PSP_ME_RENDER_STREAM_RECORD_BYTES = 64,
    TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS = 1024,
    TH07_PSP_ME_RENDER_STREAM_MAX_RUNS = 1024,
    TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES =
        TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS * 4 *
        TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES,

    TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH = 1u << 0,
    TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT = 1u << 1,
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    // I-ME4: records carry stable scalar inputs plus a Main-RAM AnmVm
    // physical address.  ME reconstructs the ordinary 64-byte semantic
    // record before entering the unchanged cull/run/vertex kernel.
    TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE = 1u << 2,

#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    TH07_PSP_ME_RENDER_STREAM_RAW_VERSION =
        0x4d453134u + TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS,
#else
    TH07_PSP_ME_RENDER_STREAM_RAW_VERSION = 0x4d453134u,
#endif
    TH07_PSP_ME_RENDER_RAW_LAYOUT_VERSION = 0x524c3031u, // "RL01"
    TH07_PSP_ME_RENDER_STREAM_RAW_RECORD_BYTES = 32,
    TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT = 264,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    // I-ME5: no per-bullet input record crosses to ME.  The worker follows the
    // six post-calc BulletManager lists and reconstructs the same semantic
    // record directly from strictly owned Bullet/VM/sprite Main RAM.
    TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST = 1u << 3,
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    TH07_PSP_ME_RENDER_STREAM_LIST_VERSION =
        0x4d453135u + TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS,
#else
    TH07_PSP_ME_RENDER_STREAM_LIST_VERSION = 0x4d453135u,
#endif
    TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION = 0x4c4c3031u, // "LL01"
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    // I-ME7: an independently fail-closed Item list is expanded before the
    // existing Bullet stream. Item failure publishes an empty prefix and can
    // never reject the Bullet suffix or its compact-update sidecar.
    TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST = 1u << 4,
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    // A1-MOVE is strictly optional even when Item geometry remains on ME.
    // Command 10 captures/publishes its next-frame motion sidecar only when
    // SC has observed the process-lifetime startup gate as enabled.
    TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED = 1u << 6,
#endif
    TH07_PSP_ME_RENDER_STREAM_ITEM_VERSION =
        0x4d453141u + TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS,
    TH07_PSP_ME_RENDER_ITEM_LAYOUT_VERSION = 0x494c3032u, // "IL02"
    TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS = 1100,
    TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS = 1100,
    TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES =
        TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS * 4 *
        TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES,
    TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_RUNS =
        TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS +
        TH07_PSP_ME_RENDER_STREAM_MAX_RUNS,
    TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_VERTEX_BYTES =
        TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES +
        TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES,
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    // I-ME8: layer 0 and layer 3 effects share I-ME7's fixed 1,100-record
    // auxiliary prefix.  The command-10 physical pool and its 2,124-record
    // Bullet+auxiliary ceiling do not grow.
    TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST = 1u << 5,
    TH07_PSP_ME_RENDER_STREAM_EFFECT_VERSION =
        0x4d453139u + TH07_PSP_ME_RENDER_STREAM_VERTEX_VERSION_BIAS,
    TH07_PSP_ME_RENDER_EFFECT_LAYOUT_VERSION = 0x454c3031u, // "EL01"
    TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS = 408,
#endif
#endif
#endif
#endif

    TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE = 1u << 0,
    // SC supplies canonical sin/cos bits for every rotated record.  ME never
    // evaluates trigonometric functions.
    TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED = 1u << 1,
    TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD = 1u << 2,
    TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE = 1u << 3,
    TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT = 4,
    TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK =
        3u << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT,
    // Force a logical run boundary without changing draw order or state.
    TH07_PSP_ME_RENDER_STREAM_RECORD_RUN_BREAK = 1u << 6,

    // Semantic primitive identifiers.  The SC renderer maps these to its
    // existing GU_SPRITES or indexed-UQ submission path; they are not raw GU
    // constants and ME never submits either primitive itself.
    TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES = 2,
    TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS = 4,

    TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD = 1u << 0,
    TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE = 1u << 1,

    TH07_PSP_ME_RENDER_STREAM_CONFIG_COLOR_MUL = 1u << 0,
    TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z = 1u << 1,

    TH07_PSP_ME_RENDER_STREAM_STATE_FREE = 0,
    TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD = 1,
    TH07_PSP_ME_RENDER_STREAM_STATE_ME_RUNNING = 2,
    TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC = 3,
    TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT = 4,
    TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED = 5,
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // Private ownership interlock.  A slot in this state is unavailable to
    // acquire, compare, GE submission and release while SC revalidates the
    // generation after its state CAS.  It is never a stable caller-visible
    // state and shutdown treats a stranded transition as unsafe.
    TH07_PSP_ME_RENDER_STREAM_STATE_SC_TRANSITION = 6,
#endif

    TH07_PSP_ME_RENDER_STREAM_RESULT_OK = 0,
    TH07_PSP_ME_RENDER_STREAM_RESULT_VERSION = 1,
    TH07_PSP_ME_RENDER_STREAM_RESULT_BOUNDS = 2,
    TH07_PSP_ME_RENDER_STREAM_RESULT_PROTOCOL = 3,
    TH07_PSP_ME_RENDER_STREAM_RESULT_INPUT_HASH = 4,
    TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD = 5,
    TH07_PSP_ME_RENDER_STREAM_RESULT_OUTPUT_OVERFLOW = 6,
    TH07_PSP_ME_RENDER_STREAM_RESULT_RUN_OVERFLOW = 7,

    TH07_PSP_ME_RENDER_STREAM_MISMATCH_NONE = 0,
    TH07_PSP_ME_RENDER_STREAM_MISMATCH_SIZE = 1,
    TH07_PSP_ME_RENDER_STREAM_MISMATCH_VERTEX = 2,
    TH07_PSP_ME_RENDER_STREAM_MISMATCH_RUN = 3,
    TH07_PSP_ME_RENDER_STREAM_MISMATCH_HASH = 4,

#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    // SC retire diagnostics.  These bits refine a fail-closed completion;
    // they never relax ownership, bounds, cache or hash validation.  The
    // expected/actual fields below describe the first set bit in evaluation
    // order, while the mask preserves every independently observed fault.
    TH07_PSP_ME_RENDER_STREAM_RETIRE_COMMAND = 1u << 0,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_RESULT = 1u << 1,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_TOKEN = 1u << 2,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_VERSION = 1u << 3,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_FLAGS = 1u << 4,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_FRAME = 1u << 5,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_TARGET = 1u << 6,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_STAGE = 1u << 7,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_MANAGER = 1u << 8,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_REPLAY = 1u << 9,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_SIGNATURE = 1u << 10,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_RECORD_COUNT = 1u << 11,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_PAYLOAD_HASH = 1u << 12,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_BUCKET = 1u << 13,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_VERTEX_COUNT = 1u << 14,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_OUTPUT_BYTES = 1u << 15,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_RUN_COUNT = 1u << 16,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_FCR_EFFECTIVE = 1u << 17,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_FCR_RESTORE = 1u << 18,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_STACK = 1u << 19,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_OUTPUT_BOUNDS = 1u << 20,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_RUN_BOUNDS = 1u << 21,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_INPUT = 1u << 22,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_OUTPUT = 1u << 23,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_RUN = 1u << 24,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_OUTPUT_HASH = 1u << 25,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_RUN_HASH = 1u << 26,
    TH07_PSP_ME_RENDER_STREAM_RETIRE_ECHO_OTHER = 1u << 27
#endif
};

typedef struct Th07PspMeRenderStreamToken
{
    unsigned int slot;
    unsigned int generation;
} Th07PspMeRenderStreamToken;

// Feature-owned cache-line record derived from PspBulletRenderRecord
// semantics.  `sourceAndState` stores sourceFileIndex in bits 0..15 and the
// normalized logical bullet/VM state in bits 16..31.  `slot` is the stable
// Bullet slot index; `slotGeneration` prevents stale-slot acceptance.
typedef struct Th07PspMeRenderStreamRecord
{
    unsigned int posXBits;
    unsigned int posYBits;
    unsigned int posZBits;
    unsigned int halfWidthBits;
    unsigned int halfHeightBits;
    unsigned int sinBits;
    unsigned int cosBits;
    unsigned int u0Bits;
    unsigned int u1Bits;
    unsigned int v0Bits;
    unsigned int v1Bits;
    unsigned int color;
    unsigned int sourceAndState;
    unsigned int flags;
    unsigned int slot;
    unsigned int slotGeneration;
} Th07PspMeRenderStreamRecord;

#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
// I-ME4 direct-submit input.  Positions and canonical sin/cos remain scalar
// snapshots; the render-only VM/sprite fields are read by ME from Main RAM.
// `vmPhys` is a physical address, never a process virtual pointer.
typedef struct Th07PspMeRenderRawRecord
{
    unsigned int posXBits;
    unsigned int posYBits;
    unsigned int sinBits;
    unsigned int cosBits;
    unsigned int vmPhys;
    unsigned int logicalState;
    unsigned int slot;
    unsigned int generation;
} Th07PspMeRenderRawRecord;

// Runtime C++ object layout supplied by the SC producer.  Keeping every
// consumed field explicit lets the C ME kernel validate the complete access
// span before dereferencing either engine object.  Representative entries are
// unsigned 16-bit source indices in Main RAM.
typedef struct Th07PspMeRenderRawLayout
{
    unsigned int rawLayoutVersion;
    unsigned int rawRecordBytes;
    unsigned int bulletBasePhys;
    unsigned int bulletStride;
    unsigned int bulletCount;
    unsigned int spriteBasePhys;
    unsigned int spriteStride;
    unsigned int spriteCount;
    unsigned int representativePhys;
    unsigned int representativeStride;
    unsigned int representativeCount;
    unsigned int vmBytes;
    unsigned int vmRotationZOffset;
    unsigned int vmScaleXOffset;
    unsigned int vmScaleYOffset;
    unsigned int vmUvScrollXOffset;
    unsigned int vmUvScrollYOffset;
    unsigned int vmColorOffset;
    unsigned int vmColor2Offset;
    unsigned int vmFlagsOffset;
    unsigned int vmSpriteOffset;
    unsigned int spriteBytes;
    unsigned int spriteSourceOffset;
    unsigned int spriteUvStartXOffset;
    unsigned int spriteUvStartYOffset;
    unsigned int spriteUvEndXOffset;
    unsigned int spriteUvEndYOffset;
    unsigned int spriteHeightOffset;
    unsigned int spriteWidthOffset;
} Th07PspMeRenderRawLayout;

#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
// I-ME5 direct-list authority.  Every offset is frozen to the accepted PSP
// object ABI by both the SC producer and ME validator.  Per-frame arcade bits
// live here so position reconstruction needs no SC record and ME never writes
// an engine object.
typedef struct Th07PspMeRenderListLayout
{
    unsigned int listLayoutVersion;
    unsigned int listLayoutBytes;
    unsigned int bulletBasePhys;
    unsigned int bulletStride;
    unsigned int bulletCount;
    unsigned int generationBasePhys;
    unsigned int generationStride;
    unsigned int generationCount;
    unsigned int activeBitsPhys;
    unsigned int activeBitsWordCount;
    unsigned int bucketHeadPhys[6];
    unsigned int bulletNextOffset;
    unsigned int bulletStateOffset;
    unsigned int bulletCollisionTypeOffset;
    unsigned int bulletPosXOffset;
    unsigned int bulletPosYOffset;
    unsigned int bulletRenderAngleOffset;
    unsigned int bulletSinOffset;
    unsigned int bulletCosOffset;
    unsigned int bulletRotationValidOffset;
    unsigned int bulletVmOffsets[5];
    unsigned int arcadeLeftBits;
    unsigned int arcadeTopBits;
} Th07PspMeRenderListLayout;

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
// Post-update Item draw authority. The live Item/AnmVm objects remain SC
// owned and read-only to ME; generation/bitmap/list/serial brackets prevent a
// reused slot or partially prepared list from publishing geometry.
typedef struct Th07PspMeRenderItemLayout
{
    unsigned int itemLayoutVersion;
    unsigned int itemLayoutBytes;
    unsigned int itemBasePhys;
    unsigned int itemStride;
    unsigned int itemCount;
    unsigned int generationBasePhys;
    unsigned int generationStride;
    unsigned int generationCount;
    unsigned int activeBitsPhys;
    unsigned int activeBitsWordCount;
    unsigned int sinBasePhys;
    unsigned int sinStride;
    unsigned int cosBasePhys;
    unsigned int cosStride;
    unsigned int headPhys;
    unsigned int tailPhys;
    unsigned int itemNextOffset;
    unsigned int itemInUseOffset;
    unsigned int itemTypeOffset;
    unsigned int itemVmOffset;
    unsigned int vmPosXOffset;
    unsigned int vmPosYOffset;
    unsigned int vmPosZOffset;
    unsigned int prepareSerialPhys;
    unsigned int preparedSerialPhys;
    unsigned int preparedCountPhys;
    unsigned int expectedPrepareSerial;
    // IL02: ME owns [head, suffixHead); SC owns [suffixHead, end).  The
    // preparedCount authority always names the complete canonical list.
    unsigned int expectedItemCount;
    unsigned int expectedTotalCount;
    unsigned int suffixHeadPhys;
    unsigned int reserved[2];
} Th07PspMeRenderItemLayout;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
// Post-update Effect layer 0/3 authority.  Presentation (including the
// observable vm.pos writes and canonical SC sin/cos bits) is committed before
// publication.  The two lists are one atomic optional segment: either both
// validate and publish, or both remain canonical SC work.
typedef struct Th07PspMeRenderEffectLayout
{
    unsigned int effectLayoutVersion;
    unsigned int effectLayoutBytes;
    unsigned int effectBasePhys;
    unsigned int effectStride;
    unsigned int effectCount;
    unsigned int generationBasePhys;
    unsigned int generationStride;
    unsigned int generationCount;
    unsigned int activeBitsPhys;
    unsigned int activeBitsWordCount;
    unsigned int sinBasePhys;
    unsigned int sinStride;
    unsigned int cosBasePhys;
    unsigned int cosStride;
    unsigned int layer0HeadPhys;
    unsigned int layer0TailPhys;
    unsigned int layer3HeadPhys;
    unsigned int layer3TailPhys;
    unsigned int effectNextOffset;
    unsigned int effectInUseOffset;
    unsigned int effectIs2DOffset;
    unsigned int effectVmOffset;
    unsigned int vmPosXOffset;
    unsigned int vmPosYOffset;
    unsigned int vmPosZOffset;
    unsigned int prepareSerialPhys;
    unsigned int preparedSerialPhys;
    unsigned int preparedCountsPhys;
    unsigned int expectedPrepareSerial;
    unsigned int expectedLayer0Count;
    unsigned int expectedLayer3Count;
    unsigned int reserved;
} Th07PspMeRenderEffectLayout;
#endif
#endif
#endif
#endif

typedef struct Th07PspMeRenderStreamVertex
{
#if defined(TH07_PSP_ME_RENDER_UV16)
    // PSP GE texture 16-bit components are unsigned and decoded as q/32768.
    unsigned short u;
    unsigned short v;
#else
    unsigned int uBits;
    unsigned int vBits;
#endif
    unsigned int color;
#if defined(TH07_PSP_ME_RENDER_XYZ16)
    short x;
    short y;
    short z;
    // GU consumes only XYZ.  An explicit zero word makes the 4-byte ABI
    // stride deterministic for hashes, cache traffic and host-side oracles.
    unsigned short reserved;
#else
    unsigned int xBits;
    unsigned int yBits;
    unsigned int zBits;
#endif
} Th07PspMeRenderStreamVertex;

// One descriptor per contiguous logical draw run.  Records are never sorted;
// a run extends only while source, blend, effective z-write and primitive are
// unchanged in emitted-record order.
typedef struct Th07PspMeRenderStreamRun
{
    unsigned int firstRecord;
    unsigned int recordCount;
    unsigned int firstVertex;
    unsigned int vertexCount;
    unsigned int primitive;
    unsigned int sourceFileIndex;
    unsigned int logicalState;
    unsigned int renderStateFlags;
} Th07PspMeRenderStreamRun;

typedef struct Th07PspMeRenderStreamBuild
{
    Th07PspMeRenderStreamToken token;
    Th07PspMeRenderStreamRecord *records;
    unsigned int recordCapacity;
} Th07PspMeRenderStreamBuild;

// All floating/global fields are raw IEEE-754 or engine bit patterns captured
// at post-calc.  In particular offset is not pre-added to record positions:
// the accepted rotated path requires local*cos-local*sin+pos+offset, followed
// by anchor adjustment, in that exact order.
typedef struct Th07PspMeRenderStreamJob
{
    Th07PspMeRenderStreamToken token;
    unsigned int version;
    unsigned int flags;
    unsigned int frameSeq;
    unsigned int targetDrawSeq;
    unsigned int stageEpoch;
    unsigned int managerEpoch;
    unsigned int replayEpoch;
    unsigned int globalSignature;
    unsigned int bucketEnds[6];
    unsigned int recordCount;
    unsigned int payloadHash;
    unsigned int offsetXBits;
    unsigned int offsetYBits;
    unsigned int viewportLeftBits;
    unsigned int viewportTopBits;
    unsigned int viewportRightBits;
    unsigned int viewportBottomBits;
    unsigned int globalColor;
    unsigned int configFlags;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    Th07PspMeRenderRawLayout rawLayout;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    Th07PspMeRenderListLayout listLayout;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    Th07PspMeRenderItemLayout itemLayout;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    Th07PspMeRenderEffectLayout effectLayout;
#endif
#endif
#endif
#endif
} Th07PspMeRenderStreamJob;

typedef struct Th07PspMeRenderStreamCompletion
{
    Th07PspMeRenderStreamToken token;
    unsigned int version;
    unsigned int flags;
    unsigned int frameSeq;
    unsigned int targetDrawSeq;
    unsigned int stageEpoch;
    unsigned int managerEpoch;
    unsigned int replayEpoch;
    unsigned int globalSignature;
    unsigned int bucketEnds[6];
    unsigned int recordCount;
    unsigned int payloadHash;
    unsigned int outputBytes;
    unsigned int vertexCount;
    unsigned int runCount;
    unsigned int outputHash;
    unsigned int runHash;
    unsigned int firstBadRecord;
    unsigned int result;
    unsigned int scWritebackUs;
    unsigned int scOutputPrepareUs;
    unsigned int scSubmitUs;
    unsigned int dispatchWaitUs;
    unsigned int scInvalidateUs;
    unsigned int meInvalidateCycles;
    unsigned int meKernelCycles;
    unsigned int meWritebackCycles;
    unsigned int meFcr31Before;
    unsigned int meFcr31Effective;
    unsigned int meFcr31After;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    unsigned int itemResult;
    unsigned int itemRecordCount;
    unsigned int itemVertexCount;
    unsigned int itemRunCount;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    unsigned int effectResult;
    unsigned int effectLayer0RecordCount;
    unsigned int effectLayer0VertexCount;
    unsigned int effectLayer0RunCount;
    unsigned int effectLayer3RecordCount;
    unsigned int effectLayer3VertexCount;
    unsigned int effectLayer3RunCount;
#endif
#endif
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    unsigned int retireFaultMask;
    unsigned int retireFaultDetail;
    unsigned int retireFaultExpected;
    unsigned int retireFaultActual;
#endif
} Th07PspMeRenderStreamCompletion;

typedef struct Th07PspMeRenderStreamReady
{
    Th07PspMeRenderStreamToken token;
    const Th07PspMeRenderStreamVertex *vertices;
    unsigned int vertexBytes;
    const Th07PspMeRenderStreamRun *runs;
    unsigned int runCount;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    unsigned int itemResult;
    unsigned int itemRecordCount;
    unsigned int itemVertexCount;
    unsigned int itemRunCount;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    unsigned int effectResult;
    unsigned int effectLayer0RecordCount;
    unsigned int effectLayer0VertexCount;
    unsigned int effectLayer0RunCount;
    unsigned int effectLayer3RecordCount;
    unsigned int effectLayer3VertexCount;
    unsigned int effectLayer3RunCount;
#endif
#endif
} Th07PspMeRenderStreamReady;

typedef struct Th07PspMeRenderStreamMismatch
{
    unsigned int kind;
    unsigned int wordIndex;
    unsigned int expected;
    unsigned int actual;
} Th07PspMeRenderStreamMismatch;
#endif

// The first 32 bytes are common to the 32/48/64-byte M0 input variants.
// Floats cross the processor boundary as exact IEEE-754 words.
typedef struct Th07PspMeRenderRecord32
{
    unsigned int centerXBits;
    unsigned int centerYBits;
    unsigned int halfWidthBits;
    unsigned int halfHeightBits;
    unsigned int sinBits;
    unsigned int cosBits;
    unsigned int color;
    unsigned int flags;
} Th07PspMeRenderRecord32;

typedef struct Th07PspMeRenderJob
{
    unsigned int version;
    unsigned int flags;
    unsigned int frameSeq;
    unsigned int targetDrawSeq;
    unsigned int stageEpoch;
    unsigned int managerEpoch;
    unsigned int replayEpoch;
    const void *input;
    unsigned int inputBytes;
    unsigned int inputStride;
    unsigned int recordCount;
    void *output;
    unsigned int outputBytes;
} Th07PspMeRenderJob;

typedef struct Th07PspMeRenderCompletion
{
    unsigned int version;
    unsigned int flags;
    unsigned int frameSeq;
    unsigned int targetDrawSeq;
    unsigned int stageEpoch;
    unsigned int managerEpoch;
    unsigned int replayEpoch;
    unsigned int recordCount;
    unsigned int inputStride;
    unsigned int outputBytes;
    unsigned int result;
    unsigned int scWritebackUs;
    unsigned int scOutputPrepareUs;
    unsigned int scSubmitUs;
    unsigned int dispatchWaitUs;
    unsigned int scInvalidateUs;
    unsigned int meInvalidateCycles;
    unsigned int meKernelCycles;
    unsigned int meWritebackCycles;
    unsigned int meFcr31Before;
    unsigned int meFcr31Effective;
    unsigned int meFcr31After;
} Th07PspMeRenderCompletion;

typedef struct Th07PspMeRenderBenchCase
{
    unsigned int recordCount;
    unsigned int inputStride;
    unsigned int cacheMode;
    unsigned int outputBytes;
    unsigned int mismatchWords;
    unsigned int result;
    unsigned int scWritebackUs;
    unsigned int scOutputPrepareUs;
    unsigned int scSubmitUs;
    unsigned int dispatchWaitUs;
    unsigned int scInvalidateUs;
    unsigned int scCopyUs;
    unsigned int meInvalidateCycles;
    unsigned int meKernelCycles;
    unsigned int meWritebackCycles;
} Th07PspMeRenderBenchCase;

typedef struct Th07PspMeRenderBenchSummary
{
    unsigned int version;
    unsigned int caseCount;
    unsigned int passedCases;
    unsigned int failedCases;
    unsigned int mismatchWords;
    unsigned int timeouts;
    unsigned int boundsFaults;
    unsigned int guardFaults;
    unsigned int protocolFaults;
    unsigned int performanceFaults;
    unsigned int takeoverUs;
    unsigned int prxBytes;
    unsigned int prxWriteUs;
    unsigned int prxLoadUs;
    int prxWriteResult;
    int prxLoadResult;
    unsigned int maxOutputBytes;
    unsigned int scFcr31Before;
    unsigned int scFcr31After;
    unsigned int meFcr31Before;
    unsigned int meFcr31After;
    unsigned int meEdramBytes;
    unsigned int throughputSamples;
    unsigned int kernelP99Cycles;
    unsigned int roundTripP99Us;
    unsigned int ready;
} Th07PspMeRenderBenchSummary;

// Nonblocking one-job API for M-ME0B shadow work.  begin performs the required
// SC cache handoff but never waits for ME.  probe is a single status read.
// A merely late job remains owned until DONE and a successful retire.  Only a
// caller-confirmed protocol hang may call hard_fault.
int th07_psp_me_render_begin(const Th07PspMeRenderJob *job);
int th07_psp_me_render_probe(Th07PspMeRenderCompletion *completion);
int th07_psp_me_render_retire(Th07PspMeRenderCompletion *completion);
void th07_psp_me_render_hard_fault(void);
void th07_psp_me_render_diag_snapshot(unsigned int *meRenderSubmitted,
                                      unsigned int *meRenderCompleted);
// The low-level owner supplies the only runtime pool accepted by both the SC
// begin path and the independent ME-side bounds check. The shadow caller may
// fill/inspect these buffers, but cannot substitute an arbitrary address.
void *th07_psp_me_render_runtime_input(void);
void *th07_psp_me_render_runtime_output(void);
void th07_psp_me_render_bench_snapshot(
    Th07PspMeRenderBenchSummary *summary,
    Th07PspMeRenderBenchCase *cases,
    unsigned int caseCapacity);

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
// I-ME1 low-level ownership API.  Without GE_CONSUME this layer can build and
// compare a real stream but remains shadow-only.  In GE_CONSUME builds, a
// successful retire returns immutable Main-RAM vertices/runs in READY_SC.
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
// Runtime admission gate for the optional Item prefix.  Startup proves the
// live handoff independently; a failed Item test leaves Bullet ME available
// and makes this return zero for the rest of the process.
int th07_psp_me_item_render_available(void);

/*
 * Process-lifetime render-worker state for the send-only SHIKIGAMI observer.
 * This is deliberately separate from the frozen audio/status packet schemas:
 * one snapshot must say whether Bullet ME survived, whether Item ME was
 * admitted, and why the optional Item path failed closed.
 */
enum
{
    TH07_PSP_ME_ITEM_DIAG_SCHEMA = 1,

    TH07_PSP_ME_ITEM_STATE_UNAVAILABLE = 0,
    TH07_PSP_ME_ITEM_STATE_TESTING = 1,
    TH07_PSP_ME_ITEM_STATE_ENABLED = 2,
    TH07_PSP_ME_ITEM_STATE_SAFE_FALLBACK = 3,
    TH07_PSP_ME_ITEM_STATE_FAILED = 4,

    TH07_PSP_ME_ITEM_REASON_NONE = 0,
    TH07_PSP_ME_ITEM_REASON_ME_UNAVAILABLE = 1,
    TH07_PSP_ME_ITEM_REASON_SELFTEST_PASS = 2,
    TH07_PSP_ME_ITEM_REASON_LIVE_ACQUIRE = 3,
    TH07_PSP_ME_ITEM_REASON_LIVE_SUBMIT = 4,
    TH07_PSP_ME_ITEM_REASON_LIVE_CONTRACT = 5,
    TH07_PSP_ME_ITEM_REASON_AUTH_ACQUIRE = 6,
    TH07_PSP_ME_ITEM_REASON_AUTH_SUBMIT = 7,
    TH07_PSP_ME_ITEM_REASON_AUTH_CONTRACT = 8,
    TH07_PSP_ME_ITEM_REASON_REJECT_ACQUIRE = 9,
    TH07_PSP_ME_ITEM_REASON_REJECT_SUBMIT = 10,
    TH07_PSP_ME_ITEM_REASON_REJECT_CONTRACT = 11,
    TH07_PSP_ME_ITEM_REASON_BULLET_RETRY_FAILED = 12,
};

typedef struct Th07PspMeItemRenderDiag
{
    unsigned int itemState;
    unsigned int itemReason;
    unsigned int itemSelftestRuns;
    unsigned int itemSelftestFailures;
    unsigned int bulletRetryRuns;
    unsigned int bulletRetryPasses;
    int lastWaitResult;
    unsigned int lastStreamResult;
    unsigned int lastItemResult;
} Th07PspMeItemRenderDiag;

void th07_psp_me_item_render_diag_snapshot(Th07PspMeItemRenderDiag *snapshot);
#endif
int th07_psp_me_render_stream_acquire(Th07PspMeRenderStreamBuild *build);
int th07_psp_me_render_stream_cancel_build(
    const Th07PspMeRenderStreamToken *token);
unsigned int th07_psp_me_render_stream_hash(const void *data,
                                             unsigned int bytes);
int th07_psp_me_render_stream_submit(const Th07PspMeRenderStreamJob *job);
int th07_psp_me_render_stream_probe(
    const Th07PspMeRenderStreamToken *token,
    Th07PspMeRenderStreamCompletion *completion);
int th07_psp_me_render_stream_retire(
    const Th07PspMeRenderStreamToken *token,
    Th07PspMeRenderStreamCompletion *completion,
    Th07PspMeRenderStreamReady *ready);
int th07_psp_me_render_stream_compare(
    const Th07PspMeRenderStreamToken *token,
    const void *expectedVertices, unsigned int expectedVertexBytes,
    const Th07PspMeRenderStreamRun *expectedRuns,
    unsigned int expectedRunCount,
    Th07PspMeRenderStreamMismatch *mismatch);
int th07_psp_me_render_stream_release_ready(
    const Th07PspMeRenderStreamToken *token);
// GE_CONSUME contract: call mark only after validating/capturing every run and
// before enqueuing the first GE command that references `ready.vertices`.
// After mark succeeds the vertex bytes are immutable and the slot cannot be
// reused.  The SC may still read the immutable run descriptors while emitting
// commands.  Call release_after_ge only after the caller has observed the GE
// list/fence completion; this low-level API deliberately does not call sceGu
// and therefore cannot manufacture or verify that hardware fence itself.
// Without GE_CONSUME both functions remain fail-closed shadow-only stubs.
int th07_psp_me_render_stream_mark_ge_in_flight(
    const Th07PspMeRenderStreamToken *token);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
// Prove that a caller-held READY view names this token's exact fixed pools and
// completion extents.  Alignment or an address merely inside either pool is
// insufficient.  Call before mark while the slot is still READY_SC.
int th07_psp_me_render_stream_ready_view_matches(
    const Th07PspMeRenderStreamToken *token,
    const Th07PspMeRenderStreamVertex *vertices,
    unsigned int vertexBytes,
    const Th07PspMeRenderStreamRun *runs,
    unsigned int runCount);
// Roll back a successful mark only if the caller has not enqueued any GE
// command.  This exists solely for failure to store/track the fence token;
// after any enqueue, only release_after_ge following real completion is legal.
int th07_psp_me_render_stream_abort_ge_mark(
    const Th07PspMeRenderStreamToken *token);
#endif
int th07_psp_me_render_stream_release_after_ge(
    const Th07PspMeRenderStreamToken *token);
void th07_psp_me_render_stream_hard_fault(
    const Th07PspMeRenderStreamToken *token);
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
// Abort/teardown barrier for live VM/sprite authority.  Waits for the one
// possible ME stream, releases READY_SC, cancels SC_BUILD and verifies every
// slot is FREE.  The caller must first run its existing GE fence/release path
// if a slot can be GE-owned.  On success no live engine pointer remains
// readable by ME, so the caller may clear a missed deadline or destroy VMs.
// Failure does not prove that ME stopped; a caller about to mutate/destroy the
// live authority must retain ownership and fail-stop instead of falling back.
int th07_psp_me_render_stream_drain_live(void);
#endif
#endif
#endif

#ifdef __cplusplus
}
#endif
