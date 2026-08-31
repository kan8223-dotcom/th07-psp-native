#include "SoundPlayer.hpp"

#include <pspaudio.h>
#include <pspkernel.h>
#include <pspthreadman.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "FileSystem.hpp"
#include "Supervisor.hpp"
#if defined(TH07_PSP_ME_RENDER_WORKER)
#include "BulletManager.hpp"
#endif
#include "audio_me.h"
#include "fileio.hpp"
#if defined(TH07_PSP_SHIKIGAMI)
#include "shikigami_th07.h"
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M)
#include "audio4m_sfx.h"
#endif

#if defined(TH07_PSP_MECC_BGM_384K) || \
    (defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_BGM_MAIN_RAM))
#define TH07_PSP_MECC_LOCAL_BGM 1
#endif

SoundBufferIdxVolume SOUND_BUFFER_IDX_VOL[38] = {
    {0, -2000, 0},   {0, -2500, 0},   {1, -1200, 5},   {1, -1500, 5},   {2, -1000, 100},
    {3, -400, 100},  {4, -400, 100},  {5, -1500, 50},  {6, -1700, 50},  {7, -1900, 50},
    {8, -1000, 100}, {9, -1000, 100}, {10, -1700, 10}, {11, -1200, 10}, {12, -900, 100},
    {5, -1500, 50},  {13, -900, 50},  {14, -900, 50},  {15, -900, 100}, {16, -200, 100},
    {17, -1400, 0},  {18, -1300, 0},  {5, -100, 20},   {6, -1800, 20},  {7, -1800, 20},
    {19, -800, 50},  {20, -1000, 50}, {21, -1300, 50}, {22, -300, 140}, {23, -900, 100},
    {24, -900, 20},  {25, -500, 90},  {26, -300, 100}, {27, -300, 100}, {24, -300, 20},
    {19, 0, 50},     {28, -300, 100}, {29, -300, 100}};

const char *g_SFXList[30] = {
    "data/wav/se_plst00.wav",   "data/wav/se_enep00.wav",   "data/wav/se_pldead00.wav",
    "data/wav/se_power0.wav",   "data/wav/se_power1.wav",   "data/wav/se_tan00.wav",
    "data/wav/se_tan01.wav",    "data/wav/se_tan02.wav",    "data/wav/se_ok00.wav",
    "data/wav/se_cancel00.wav", "data/wav/se_select00.wav", "data/wav/se_gun00.wav",
    "data/wav/se_cat00.wav",    "data/wav/se_lazer00.wav",  "data/wav/se_lazer01.wav",
    "data/wav/se_enep01.wav",   "data/wav/se_nep00.wav",    "data/wav/se_damage00.wav",
    "data/wav/se_item00.wav",   "data/wav/se_kira00.wav",   "data/wav/se_kira01.wav",
    "data/wav/se_kira02.wav",   "data/wav/se_extend.wav",   "data/wav/se_timeout.wav",
    "data/wav/se_graze.wav",    "data/wav/se_powerup.wav",  "data/wav/se_border.wav",
    "data/wav/se_bonus.wav",    "data/wav/se_bonus2.wav",   "data/wav/se_pause.wav",
};

namespace
{
constexpr u32 kChannels = 2;
constexpr u32 kBytesPerFrame = sizeof(i16) * kChannels;
constexpr u32 kFramesPerOutput = 512;
constexpr u32 kDacBufferCount = 2;
constexpr u32 kDacBufferBytes = kFramesPerOutput * kBytesPerFrame;
constexpr u32 kPrefillFrames = 4096;
constexpr u32 kIoFrames = 16 * 1024;
// Keep the canonical 384 KiB capacity for all PSP profiles.  AUDIO4M R19 uses
// the original SC-direct Main-RAM ring: both upper and lower ME eDRAM failed
// real-hardware pause/retention listening tests.  The standalone historical
// MECC_BGM_384K diagnostic remains the only local-eDRAM ring profile.
constexpr u32 kRingFrames = 96 * 1024;
constexpr u32 kSfxBufferCount = 30;
constexpr u32 kSfxLogicalCount = 38;
constexpr u32 kSfxVoiceCount = 16;
constexpr u32 kUnityGainQ16 = 65536u;
constexpr int kBgmIoUrgentPriority = 0x1c;
constexpr int kBgmIoBackgroundPriority = 0x21;
static_assert(kDacBufferCount == 2,
              "PSP DAC output must alternate two live PCM buffers");
static_assert(kDacBufferBytes == 2048,
              "PSP DAC output block must remain 512-frame stereo s16");

#if defined(TH07_PSP_MECC_LOCAL_BGM)
constexpr u32 kRingBytes = kRingFrames * kBytesPerFrame;
constexpr u32 kMeccFifoBlocks = kPrefillFrames / kFramesPerOutput;
constexpr u32 kMeccFifoBlockBytes = kFramesPerOutput * kBytesPerFrame;
constexpr u32 kMeccScResetTimeoutUs = 3000000;
static_assert(kIoFrames * kBytesPerFrame == 65536,
              "MECC upload staging must remain exactly 64 KiB");
static_assert(kMeccFifoBlockBytes == 2048,
              "MECC fetch must remain exactly one 512-frame stereo block");
static_assert(kMeccFifoBlocks == 8,
              "MECC FIFO must preserve the existing 4096-frame prefill");
static_assert(kRingFrames % kIoFrames == 0 && kRingFrames % kFramesPerOutput == 0,
              "MECC ring commands must never cross the fixed local extent");
static_assert(kRingBytes == 384 * 1024,
              "MECC profiles must retain the proven 384 KiB ring");
#else
static_assert(kRingFrames * kChannels * sizeof(i16) == 393216,
              "TH07 PSP BGM ring must remain exactly 384 KiB");
#endif

#if defined(TH07_PSP_1000)
using PspSfxSample = u8;
#else
using PspSfxSample = i16;
#endif

struct PspSfxBuffer
{
    PspSfxSample *samples;
    u32 frames;
    // 16.16 source frames advanced by each 44.1 kHz output frame.  Compute it
    // once while loading; 64-bit division in the priority-0x10 output thread
    // is both unnecessary and extremely expensive on Allegrex.
    u32 stepFixed;
};

struct PspSfxVoice
{
    i32 logicalIdx;
    u32 positionFrame;
    u32 positionFraction;
    bool active;
};

#if defined(TH07_PSP_MECC_LOCAL_BGM)
i16 *gBgmRing;
struct alignas(64) MeccBgmFifoBlock
{
    i16 samples[kFramesPerOutput * kChannels];
    u32 generation;
};
alignas(64) MeccBgmFifoBlock gMeccBgmFifo[kMeccFifoBlocks];
#if defined(TH07_PSP_MECC_AUDIO_4M)
// R8 noise diagnostic: hash every 2 KiB block on the SC before it is uploaded
// to the eDRAM ring, and verify each fetched block against that hash.  Any
// mismatch proves the SC->eDRAM->SC round trip corrupts PCM, which is the
// remaining suspect for the crackle the plain Main-RAM-ring build never had.
struct BgmBlockCheck
{
    u32 hash;
    u32 generation;
};
BgmBlockCheck gBgmRingCheck[kRingBytes / kMeccFifoBlockBytes];
volatile u32 gBgmCrcChecks;
volatile u32 gBgmCrcMismatches;
constexpr u32 kBgmCrcDetailLimit = 8;
struct BgmCrcMismatchRecord
{
    u32 ordinal;
    u32 ringOffset;
    u32 expectedHash;
    u32 fetchedHash;
    u32 generation;
    u32 eventTimeUs;
};
BgmCrcMismatchRecord gBgmCrcMismatchRecords[kBgmCrcDetailLimit];
volatile u32 gBgmCrcMismatchRecordCount;

u32 HashBgmBlock(const void *data)
{
    const u32 *words = static_cast<const u32 *>(data);
    u32 hash = 2166136261u;
    for (u32 index = 0; index < kMeccFifoBlockBytes / sizeof(u32); ++index)
    {
        hash = (hash ^ words[index]) * 16777619u;
    }
    return hash;
}

void FlushBgmCrcMismatchRecords()
{
    // Called only after StopThreads joined the feeder.  Keeping detail in RAM
    // until this point prevents synchronous Memory Stick writes from stealing
    // the feeder's 92.9 ms FIFO headroom and manufacturing an underrun.
    const u32 count = std::min(
        __atomic_load_n(&gBgmCrcMismatchRecordCount, __ATOMIC_ACQUIRE),
        kBgmCrcDetailLimit);
    for (u32 index = 0; index < count; ++index)
    {
        const BgmCrcMismatchRecord &record = gBgmCrcMismatchRecords[index];
        th07_psp_boot_notef(
            "bgm crc mismatch %lu ring %08x want %08x got %08x gen %lu at %luus",
            static_cast<unsigned long>(record.ordinal), record.ringOffset,
            record.expectedHash, record.fetchedHash,
            static_cast<unsigned long>(record.generation),
            static_cast<unsigned long>(record.eventTimeUs));
    }
}
#endif
#else
alignas(64) i16 gBgmRing[kRingFrames * kChannels];
#endif
alignas(64) i16 gIoBuffer[kIoFrames * kChannels];
PspSfxBuffer gSfxBuffers[kSfxBufferCount];
PspSfxVoice gSfxVoices[kSfxVoiceCount];
u16 gSfxGainQ15[kSfxLogicalCount];
volatile u32 gPendingSfxMaskLow;
volatile u32 gPendingSfxMaskHigh;
volatile u32 gStopSfxMaskLow;
volatile u32 gStopSfxMaskHigh;
volatile u32 gSfxTriggerCount;
volatile u32 gSfxMixedBlocks;
volatile u32 gSfxHeadroomLimitedSamples;
volatile u32 gSfxScTotalMixUs;
volatile u32 gSfxScMaxMixUs;
volatile u32 gSePowerStarts;
volatile u32 gSePowerEnds;
volatile u32 gSePowerIgnored;
volatile u32 gSePowerActive;
u32 gSfxRequestCounts[kSfxLogicalCount];
u32 gSfxCooldown[kSfxLogicalCount];

volatile u32 gReadFrame;
volatile u32 gWriteFrame;
volatile u32 gGeneration;
volatile u32 gFadeFramesRemaining;
volatile u32 gFadeFramesTotal;
volatile u32 gUnderruns;
volatile bool gAudioAlive;
volatile bool gBgmPlaying;
volatile bool gBgmPaused;
volatile bool gBgmStageLoadBlocked;
volatile bool gSystemSuspended;
#if defined(TH07_PSP_MECC_LOCAL_BGM)
volatile bool gMeccLocalRing;
volatile u32 gMeccFetchFrame;
volatile u32 gMeccFifoRead;
volatile u32 gMeccFifoWrite;
volatile u32 gMeccProducerAckGeneration;
volatile u32 gMeccFeederAckGeneration;
volatile u32 gMeccOutputAckGeneration;
#endif
#if defined(TH07_PSP_MECC_LOCAL_BGM) || defined(TH07_PSP_ME_RENDER_WORKER)
volatile bool gMeccSuspendLogPending;
#endif
#if defined(TH07_PSP_MECC_LOCAL_BGM) || defined(TH07_PSP_MECC_AUDIO_4M)
volatile bool gMeccFatal;
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M)
// Wire names are retained for schema compatibility.  On the Main-RAM backend
// these count producer writes, consumer reads and successful DAC submissions.
volatile u32 gBgmUploadWraps;
volatile u32 gBgmFetchWraps;
volatile u32 gBgmOutputWraps;
#endif

SceUID gFileSema = -1;
SceUID gProducerThread = -1;
SceUID gOutputThread = -1;
#if defined(TH07_PSP_MECC_LOCAL_BGM)
SceUID gMeccFeederThread = -1;
#endif
int gAudioChannel = -1;
FILE *gBgmFile;
u32 gTrackBase;
u32 gTrackCursor;
u32 gTrackIntroBytes;
u32 gTrackTotalBytes;
u32 gFmtCount;

u32 RingCount()
{
    const u32 write = __atomic_load_n(&gWriteFrame, __ATOMIC_ACQUIRE);
    const u32 read = __atomic_load_n(&gReadFrame, __ATOMIC_ACQUIRE);
    return write >= read ? write - read : kRingFrames - read + write;
}

#if defined(TH07_PSP_MECC_LOCAL_BGM) || defined(TH07_PSP_MECC_AUDIO_4M)
void LatchMeccFatal(const char *message)
{
    if (__atomic_exchange_n(&gMeccFatal, true, __ATOMIC_ACQ_REL))
    {
        return;
    }
    __atomic_store_n(&gBgmPlaying, false, __ATOMIC_RELEASE);
    th07_psp_me_audio_suspend_latch();
    th07_psp_boot_note(message);
#if defined(TH07_PSP_SHIKIGAMI)
    th07_shikigami_record_fatal(message);
#endif
}
#endif

#if defined(TH07_PSP_MECC_LOCAL_BGM)
u32 MeccFifoCount()
{
    const u32 write = __atomic_load_n(&gMeccFifoWrite, __ATOMIC_ACQUIRE);
    const u32 read = __atomic_load_n(&gMeccFifoRead, __ATOMIC_ACQUIRE);
    return write - read;
}

bool WaitForMeccScWorkers(u32 generation)
{
    const u32 startUs = sceKernelGetSystemTimeLow();
    for (;;)
    {
        const bool producerReady =
            gProducerThread < 0 ||
            __atomic_load_n(&gMeccProducerAckGeneration, __ATOMIC_ACQUIRE) == generation;
        const bool feederReady =
            gMeccFeederThread < 0 ||
            __atomic_load_n(&gMeccFeederAckGeneration, __ATOMIC_ACQUIRE) == generation;
        const bool outputReady =
            gOutputThread < 0 ||
            __atomic_load_n(&gMeccOutputAckGeneration, __ATOMIC_ACQUIRE) == generation;
        if (producerReady && feederReady && outputReady)
        {
            return true;
        }
        if (sceKernelGetSystemTimeLow() - startUs >= kMeccScResetTimeoutUs)
        {
            LatchMeccFatal("MECC BGM SC RESET ACK TIMEOUT -> COLD REBOOT");
            return false;
        }
        sceKernelDelayThread(100);
    }
}

u8 MeccSelftestByte(u32 absoluteOffset, u32 uploadBlock)
{
    return static_cast<u8>((absoluteOffset * 37u) ^
                           (absoluteOffset >> 7u) ^
                           (uploadBlock * 0x5bu) ^ 0xa5u);
}

bool SelftestMeccBgmRing(u32 generation)
{
    if (!th07_psp_me_bgm_reset(generation))
    {
        return false;
    }

    u8 *const upload = reinterpret_cast<u8 *>(gIoBuffer);
    u8 *const fetch = reinterpret_cast<u8 *>(gMeccBgmFifo[0].samples);
    constexpr u32 uploadBytes = kIoFrames * kBytesPerFrame;
    constexpr u32 fetchBytes = kMeccFifoBlockBytes;
    constexpr u32 uploadBlocks = kRingBytes / uploadBytes;
    constexpr u32 fetchesPerUpload = uploadBytes / fetchBytes;
    for (u32 block = 0; block < uploadBlocks; ++block)
    {
        const u32 blockOffset = block * uploadBytes;
        for (u32 byte = 0; byte < uploadBytes; ++byte)
        {
            upload[byte] = MeccSelftestByte(blockOffset + byte, block);
        }
        if (!th07_psp_me_bgm_upload(upload, uploadBytes, generation, blockOffset))
        {
            return false;
        }
        for (u32 part = 0; part < fetchesPerUpload; ++part)
        {
            const u32 fetchOffset = blockOffset + part * fetchBytes;
            if (!th07_psp_me_bgm_fetch(fetch, fetchBytes, generation, fetchOffset))
            {
                return false;
            }
            for (u32 byte = 0; byte < fetchBytes; ++byte)
            {
                if (fetch[byte] != MeccSelftestByte(fetchOffset + byte, block))
                {
                    return false;
                }
            }
        }
    }
    return th07_psp_me_bgm_reset(generation) != 0;
}
#endif

void LockFile()
{
    if (gFileSema >= 0)
    {
        sceKernelWaitSema(gFileSema, 1, nullptr);
    }
}

void UnlockFile()
{
    if (gFileSema >= 0)
    {
        sceKernelSignalSema(gFileSema, 1);
    }
}

void ResetRing()
{
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    const u32 generation = __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE);
    if (!WaitForMeccScWorkers(generation))
    {
        return;
    }
    if (__atomic_load_n(&gMeccLocalRing, __ATOMIC_ACQUIRE) &&
        !__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE) &&
        !th07_psp_me_bgm_reset(generation))
    {
        LatchMeccFatal("MECC BGM RESET FAILED -> COLD REBOOT");
        return;
    }
#endif
    __atomic_store_n(&gReadFrame, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gWriteFrame, 0u, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    __atomic_store_n(&gMeccFetchFrame, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeccFifoRead, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeccFifoWrite, 0u, __ATOMIC_RELEASE);
#endif
}

u16 ReadLe16(const u8 *bytes)
{
    return static_cast<u16>(bytes[0] | (static_cast<u16>(bytes[1]) << 8));
}

u32 ReadLe32(const u8 *bytes)
{
    return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
           (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24);
}

#if defined(TH07_PSP_1000)
u8 EncodeMuLaw8(i32 sample)
{
    constexpr i32 kBias = 0x84;
    constexpr i32 kClip = 32635;
    const u32 sign = sample < 0 ? 0x80u : 0u;
    if (sample < 0)
    {
        sample = -sample;
    }
    sample = std::min(sample, kClip) + kBias;

    u32 exponent = 7;
    for (u32 mask = 0x4000u; exponent != 0 &&
                                 (static_cast<u32>(sample) & mask) == 0;
         mask >>= 1, --exponent)
    {
    }
    const u32 mantissa = (static_cast<u32>(sample) >> (exponent + 3u)) & 0x0fu;
    return static_cast<u8>(~(sign | (exponent << 4) | mantissa));
}
#endif

void ResetSfxVoices()
{
    __atomic_store_n(&gPendingSfxMaskLow, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gPendingSfxMaskHigh, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gStopSfxMaskLow, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gStopSfxMaskHigh, 0, __ATOMIC_RELEASE);
    for (PspSfxVoice &voice : gSfxVoices)
    {
        voice = {-1, 0, 0, false};
    }
}

#if !defined(TH07_PSP_MECC_AUDIO_4M) || defined(TH07_PSP_SFX_MAIN_RAM)
__attribute__((noinline)) bool MixSfxBlock(i16 *block, u32 frames)
{
    // Allegrex is a 32-bit CPU.  Two native atomic masks avoid libatomic's
    // 64-bit emulation in this real-time thread.
    const u32 pendingLow =
        __atomic_exchange_n(&gPendingSfxMaskLow, 0, __ATOMIC_ACQ_REL);
    const u32 pendingHigh =
        __atomic_exchange_n(&gPendingSfxMaskHigh, 0, __ATOMIC_ACQ_REL);
    const u32 stopLow = __atomic_exchange_n(&gStopSfxMaskLow, 0, __ATOMIC_ACQ_REL);
    const u32 stopHigh = __atomic_exchange_n(&gStopSfxMaskHigh, 0, __ATOMIC_ACQ_REL);
    if (stopLow || stopHigh)
    {
        for (PspSfxVoice &voice : gSfxVoices)
        {
            if (!voice.active || voice.logicalIdx < 0)
            {
                continue;
            }
            const u32 logicalIdx = static_cast<u32>(voice.logicalIdx);
            const bool stop = logicalIdx < 32
                                  ? (stopLow & (1u << logicalIdx)) != 0
                                  : (stopHigh & (1u << (logicalIdx - 32))) != 0;
            if (stop)
            {
                if (voice.logicalIdx == SOUND_BOMB_SAKUYA_A)
                {
                    __atomic_store_n(&gSePowerActive, 0u, __ATOMIC_RELEASE);
                    __atomic_fetch_add(&gSePowerEnds, 1u, __ATOMIC_RELAXED);
                }
                voice.active = false;
            }
        }
    }
    for (u32 logicalIdx = 0; logicalIdx < kSfxLogicalCount; ++logicalIdx)
    {
        const bool pending = logicalIdx < 32
                                 ? (pendingLow & (1u << logicalIdx)) != 0
                                 : (pendingHigh & (1u << (logicalIdx - 32))) != 0;
        if (!pending)
        {
            continue;
        }

        PspSfxVoice *selected = nullptr;
        for (PspSfxVoice &voice : gSfxVoices)
        {
            if (voice.active && voice.logicalIdx == static_cast<i32>(logicalIdx))
            {
                selected = &voice;
                break;
            }
            if (!voice.active && !selected)
            {
                selected = &voice;
            }
        }
        if (!selected)
        {
            // All voices are occupied.  Steal the one closest to completion.
            u32 shortestRemaining = 0xffffffffu;
            for (PspSfxVoice &voice : gSfxVoices)
            {
                const u32 bufferIdx = SOUND_BUFFER_IDX_VOL[voice.logicalIdx].bufferIdx;
                const u32 remaining =
                    gSfxBuffers[bufferIdx].frames - voice.positionFrame;
                if (remaining < shortestRemaining)
                {
                    shortestRemaining = remaining;
                    selected = &voice;
                }
            }
        }
        selected->logicalIdx = static_cast<i32>(logicalIdx);
        selected->positionFrame = 0;
        selected->positionFraction = 0;
        selected->active = true;
        if (logicalIdx == static_cast<u32>(SOUND_BOMB_SAKUYA_A))
        {
            __atomic_store_n(&gSePowerActive, 1u, __ATOMIC_RELEASE);
            __atomic_fetch_add(&gSePowerStarts, 1u, __ATOMIC_RELAXED);
        }
    }

    Th07PspMixJob mixJob{};
    mixJob.frames = frames;

    bool mixed = false;
    for (PspSfxVoice &voice : gSfxVoices)
    {
        if (!voice.active || voice.logicalIdx < 0 ||
            voice.logicalIdx >= static_cast<i32>(kSfxLogicalCount))
        {
            continue;
        }
        const u32 bufferIdx = SOUND_BUFFER_IDX_VOL[voice.logicalIdx].bufferIdx;
        const PspSfxBuffer &sound = gSfxBuffers[bufferIdx];
        if (!sound.samples || voice.positionFrame >= sound.frames)
        {
            voice.active = false;
            continue;
        }

        const unsigned long long sourceFixed =
            (static_cast<unsigned long long>(voice.positionFrame) << 16) |
            voice.positionFraction;
        const unsigned long long endFixed =
            static_cast<unsigned long long>(sound.frames) << 16;
        const unsigned long long remainingFixed = endFixed - sourceFixed;
        const u32 framesUntilEnd = static_cast<u32>(
            (remainingFixed + sound.stepFixed - 1u) / sound.stepFixed);
        const u32 mixedFrames = std::min(frames, framesUntilEnd);
        if (mixedFrames != 0 && mixJob.inputCount < TH07_PSP_ME_MAX_MIX_INPUTS)
        {
            Th07PspMixInput &input = mixJob.inputs[mixJob.inputCount++];
            input.samples = sound.samples;
            input.frames = sound.frames;
            input.channels = 1;
            input.sourceFrame = voice.positionFrame;
            input.sourceFraction = voice.positionFraction;
            input.stepFixed = sound.stepFixed;
            input.gainQ16 = static_cast<u32>(gSfxGainQ15[voice.logicalIdx]) << 1;
#if defined(TH07_PSP_1000)
            input.sampleFormat = TH07_PSP_MIX_MULAW8;
#else
            input.sampleFormat = TH07_PSP_MIX_S16;
#endif
            mixed = true;
        }

        const unsigned long long nextFixed =
            static_cast<unsigned long long>(voice.positionFraction) +
            static_cast<unsigned long long>(sound.stepFixed) * mixedFrames;
        voice.positionFrame += static_cast<u32>(nextFixed >> 16);
        voice.positionFraction = static_cast<u32>(nextFixed) & 0xffffu;
        if (voice.positionFrame >= sound.frames)
        {
            if (voice.logicalIdx == SOUND_BOMB_SAKUYA_A)
            {
                __atomic_store_n(&gSePowerActive, 0u, __ATOMIC_RELEASE);
                __atomic_fetch_add(&gSePowerEnds, 1u, __ATOMIC_RELAXED);
            }
            voice.active = false;
        }
    }

    if (!mixed)
    {
        // Preserve BGM-only blocks bit-for-bit.  TH07 submits directly from
        // this thread and has no TH06-style multi-block output queue, so even
        // a successful 8-10 ms ME wait can miss the 11.6 ms DAC deadline.
        return false;
    }

    // Mix effects into a wide bus at their original DirectSound gains.  TH06
    // divides BGM and SFX together, but applying that divisor only to TH07's
    // SFX would make every effect 18 dB too quiet relative to untouched BGM.
    // Running synchronously on SC is also more predictable than a blocking ME
    // round trip in this deadline-critical output thread.
    mixJob.mixDivisor = 1;
    const u32 mixStartUs = sceKernelGetSystemTimeLow();
    unsigned int limitedSamples = 0;
    if (!th07_psp_sc_audio_mix_into(&mixJob, block, &limitedSamples))
    {
        return false;
    }
    const u32 mixElapsedUs = sceKernelGetSystemTimeLow() - mixStartUs;
    if (mixElapsedUs > gSfxScMaxMixUs)
    {
        gSfxScMaxMixUs = mixElapsedUs;
    }
    gSfxScTotalMixUs += mixElapsedUs;
    gSfxHeadroomLimitedSamples += limitedSamples;
    ++gSfxMixedBlocks;
    return true;
}
#endif

void CloseTrackFile()
{
    LockFile();
    if (gBgmFile)
    {
        fclose(gBgmFile);
        gBgmFile = nullptr;
    }
    gTrackBase = 0;
    gTrackCursor = 0;
    gTrackIntroBytes = 0;
    gTrackTotalBytes = 0;
    UnlockFile();
}

u32 ReadTrackFrames(i16 *out, u32 requestedFrames, u32 generation)
{
    u32 framesRead = 0;
    LockFile();
    while (gBgmFile && generation == __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE) &&
           framesRead < requestedFrames)
    {
        if (gTrackCursor >= gTrackTotalBytes)
        {
            gTrackCursor = gTrackIntroBytes;
            if (fseek(gBgmFile, static_cast<long>(gTrackBase + gTrackCursor), SEEK_SET) != 0)
            {
                break;
            }
        }

        const u32 bytesWanted = (requestedFrames - framesRead) * kBytesPerFrame;
        const u32 bytesUntilLoop = gTrackTotalBytes - gTrackCursor;
        const u32 bytesToRead = std::min(bytesWanted, bytesUntilLoop);
        const size_t bytesActuallyRead =
            fread(out + framesRead * kChannels, 1, bytesToRead, gBgmFile);
        const u32 completeBytes = static_cast<u32>(bytesActuallyRead) & ~(kBytesPerFrame - 1u);
        const u32 completeFrames = completeBytes / kBytesPerFrame;
        framesRead += completeFrames;
        gTrackCursor += completeBytes;
        if (completeBytes != bytesToRead)
        {
            break;
        }
    }
    UnlockFile();
    return framesRead;
}

int BgmProducerThread(SceSize, void *)
{
    while (__atomic_load_n(&gAudioAlive, __ATOMIC_ACQUIRE))
    {
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        __atomic_store_n(&gMeccProducerAckGeneration,
                         __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE),
                         __ATOMIC_RELEASE);
#endif
        if (__atomic_load_n(&gSystemSuspended, __ATOMIC_ACQUIRE))
        {
            sceKernelDelayThread(10000);
            continue;
        }
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE))
        {
            sceKernelDelayThread(10000);
            continue;
        }
#endif
        if (!__atomic_load_n(&gBgmPlaying, __ATOMIC_ACQUIRE))
        {
            sceKernelDelayThread(2000);
            continue;
        }

        const u32 queued = RingCount();
        const u32 freeFrames = kRingFrames - 1u - queued;
        if (freeFrames < kIoFrames)
        {
            sceKernelDelayThread(2000);
            continue;
        }

        const u32 generation = __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE);
        const u32 framesRead = ReadTrackFrames(gIoBuffer, kIoFrames, generation);
        if (framesRead == 0)
        {
            if (generation == __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE))
            {
                __atomic_store_n(&gBgmPlaying, false, __ATOMIC_RELEASE);
                th07_psp_boot_note("bgm stream read failed");
            }
            continue;
        }
        if (generation != __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE))
        {
            continue;
        }

        u32 write = __atomic_load_n(&gWriteFrame, __ATOMIC_RELAXED);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (__atomic_load_n(&gMeccLocalRing, __ATOMIC_ACQUIRE))
        {
            if (framesRead != kIoFrames ||
                !th07_psp_me_bgm_upload(gIoBuffer, kIoFrames * kBytesPerFrame,
                                        generation, write * kBytesPerFrame))
            {
                if (generation == __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE))
                {
                    LatchMeccFatal("MECC BGM UPLOAD FAILED -> COLD REBOOT");
                }
                continue;
            }
#if defined(TH07_PSP_MECC_AUDIO_4M)
            // Record the pre-upload hashes before gWriteFrame publishes the
            // region, so the feeder can never fetch a block whose expected
            // hash is not in place yet.
            {
                const u32 firstBlock =
                    write * kBytesPerFrame / kMeccFifoBlockBytes;
                const u32 blockCount =
                    kIoFrames * kBytesPerFrame / kMeccFifoBlockBytes;
                for (u32 blockIdx = 0; blockIdx < blockCount; ++blockIdx)
                {
                    BgmBlockCheck &check = gBgmRingCheck[firstBlock + blockIdx];
                    check.hash = HashBgmBlock(
                        reinterpret_cast<const u8 *>(gIoBuffer) +
                        blockIdx * kMeccFifoBlockBytes);
                    check.generation = generation;
                }
                __asm__ volatile("sync");
            }
#endif
        }
        else
#endif
        {
        for (u32 frame = 0; frame < framesRead; ++frame)
        {
            const u32 dst = ((write + frame) % kRingFrames) * kChannels;
            gBgmRing[dst] = gIoBuffer[frame * kChannels];
            gBgmRing[dst + 1] = gIoBuffer[frame * kChannels + 1];
        }
        }
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (generation != __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE))
        {
            continue;
        }
#endif
        __atomic_store_n(&gWriteFrame, (write + framesRead) % kRingFrames,
                         __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_AUDIO_4M)
        if (write + framesRead == kRingFrames)
        {
            __atomic_fetch_add(&gBgmUploadWraps, 1u, __ATOMIC_RELAXED);
        }
#endif
        if (RingCount() >= kIoFrames * 2u)
        {
            sceKernelChangeThreadPriority(sceKernelGetThreadId(),
                                          kBgmIoBackgroundPriority);
        }
    }
    sceKernelExitThread(0);
    return 0;
}

#if defined(TH07_PSP_MECC_LOCAL_BGM)
int BgmMeccFeederThread(SceSize, void *)
{
    while (__atomic_load_n(&gAudioAlive, __ATOMIC_ACQUIRE))
    {
        __atomic_store_n(&gMeccFeederAckGeneration,
                         __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE),
                         __ATOMIC_RELEASE);
        if (__atomic_load_n(&gSystemSuspended, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE) ||
            !__atomic_load_n(&gMeccLocalRing, __ATOMIC_ACQUIRE) ||
            !__atomic_load_n(&gBgmPlaying, __ATOMIC_ACQUIRE))
        {
            sceKernelDelayThread(2000);
            continue;
        }

        const u32 generation = __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE);
        const u32 fifoWrite = __atomic_load_n(&gMeccFifoWrite, __ATOMIC_RELAXED);
        const u32 fifoRead = __atomic_load_n(&gMeccFifoRead, __ATOMIC_ACQUIRE);
        if (fifoWrite - fifoRead >= kMeccFifoBlocks)
        {
            sceKernelDelayThread(500);
            continue;
        }

        const u32 fetch = __atomic_load_n(&gMeccFetchFrame, __ATOMIC_RELAXED);
        const u32 write = __atomic_load_n(&gWriteFrame, __ATOMIC_ACQUIRE);
        const u32 available = write >= fetch ? write - fetch
                                             : kRingFrames - fetch + write;
        if (available < kFramesPerOutput)
        {
            sceKernelDelayThread(500);
            continue;
        }

        MeccBgmFifoBlock &slot = gMeccBgmFifo[fifoWrite % kMeccFifoBlocks];
        if (!th07_psp_me_bgm_fetch(slot.samples, kMeccFifoBlockBytes,
                                   generation, fetch * kBytesPerFrame))
        {
            if (generation == __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE))
            {
                LatchMeccFatal("MECC BGM FETCH FAILED -> COLD REBOOT");
            }
            continue;
        }
        if (generation != __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE))
        {
            continue;
        }
#if defined(TH07_PSP_MECC_AUDIO_4M)
        {
            const BgmBlockCheck &check =
                gBgmRingCheck[fetch * kBytesPerFrame / kMeccFifoBlockBytes];
            if (check.generation == generation)
            {
                __atomic_fetch_add(&gBgmCrcChecks, 1u, __ATOMIC_RELAXED);
                const u32 fetched = HashBgmBlock(slot.samples);
                if (fetched != check.hash)
                {
                    const u32 misses = __atomic_add_fetch(&gBgmCrcMismatches, 1u,
                                                          __ATOMIC_RELAXED);
                    if (misses <= kBgmCrcDetailLimit)
                    {
                        BgmCrcMismatchRecord &record =
                            gBgmCrcMismatchRecords[misses - 1u];
                        record.ordinal = misses;
                        record.ringOffset = fetch * kBytesPerFrame;
                        record.expectedHash = check.hash;
                        record.fetchedHash = fetched;
                        record.generation = generation;
                        record.eventTimeUs = sceKernelGetSystemTimeLow();
                        __atomic_store_n(&gBgmCrcMismatchRecordCount, misses,
                                         __ATOMIC_RELEASE);
                    }
                    // Never submit known-corrupt PCM to the DAC.  The lower
                    // R18 ring is expected to keep this path at zero hits;
                    // fail silent for one 512-frame block if hardware still
                    // reports a retention fault.
                    std::memset(slot.samples, 0, kMeccFifoBlockBytes);
                }
            }
        }
#endif
        slot.generation = generation;
        __asm__ volatile("sync");
        __atomic_store_n(&gMeccFetchFrame,
                         (fetch + kFramesPerOutput) % kRingFrames,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&gMeccFifoWrite, fifoWrite + 1u, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_AUDIO_4M)
        if (fetch + kFramesPerOutput == kRingFrames)
        {
            __atomic_fetch_add(&gBgmFetchWraps, 1u, __ATOMIC_RELAXED);
        }
#endif
    }
    sceKernelExitThread(0);
    return 0;
}
#endif

int BgmOutputThread(SceSize, void *)
{
    // sceAudioOutputBlocking queues this address; it does not make the buffer
    // reusable when the call returns.  Build the next block in the other slot
    // so the DAC never observes a buffer while the SC is rewriting it.
    alignas(64) i16 blocks[kDacBufferCount][kFramesPerOutput * kChannels];
    u32 outputIndex = 0;
    u32 seenGeneration = __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE);
    bool primed = false;
    while (__atomic_load_n(&gAudioAlive, __ATOMIC_ACQUIRE))
    {
        i16 *const block = blocks[outputIndex];
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        __atomic_store_n(&gMeccOutputAckGeneration,
                         __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE),
                         __ATOMIC_RELEASE);
#endif
        if (__atomic_load_n(&gSystemSuspended, __ATOMIC_ACQUIRE))
        {
            primed = false;
            sceKernelDelayThread(10000);
            continue;
        }
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE))
        {
            primed = false;
            sceKernelDelayThread(10000);
            continue;
        }
#endif
        const u32 generation = __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE);
        if (generation != seenGeneration)
        {
            seenGeneration = generation;
            primed = false;
        }
        bool haveBgm = false;
#if defined(TH07_PSP_MECC_AUDIO_4M)
        bool bgmOutputWrap = false;
#endif
        const bool wantsBgm =
            __atomic_load_n(&gBgmPlaying, __ATOMIC_ACQUIRE) &&
            !__atomic_load_n(&gBgmPaused, __ATOMIC_ACQUIRE) &&
            !__atomic_load_n(&gBgmStageLoadBlocked, __ATOMIC_ACQUIRE);
        if (wantsBgm)
        {
#if defined(TH07_PSP_MECC_LOCAL_BGM)
            const bool localRing =
                __atomic_load_n(&gMeccLocalRing, __ATOMIC_ACQUIRE);
            const u32 available = localRing
                                      ? MeccFifoCount() * kFramesPerOutput
                                      : RingCount();
#else
            const u32 available = RingCount();
#endif
            if ((!primed && available >= kPrefillFrames) ||
                (primed && available >= kFramesPerOutput))
            {
                primed = true;
                haveBgm = true;
                const u32 read = __atomic_load_n(&gReadFrame, __ATOMIC_RELAXED);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
                if (localRing)
                {
                    const u32 fifoRead =
                        __atomic_load_n(&gMeccFifoRead, __ATOMIC_RELAXED);
                    const MeccBgmFifoBlock &slot =
                        gMeccBgmFifo[fifoRead % kMeccFifoBlocks];
                    if (slot.generation != generation)
                    {
                        haveBgm = false;
                        primed = false;
                        LatchMeccFatal("MECC BGM FIFO EPOCH MISMATCH -> COLD REBOOT");
                    }
                    else
                    {
                        std::memcpy(block, slot.samples, kDacBufferBytes);
                        __atomic_store_n(&gMeccFifoRead, fifoRead + 1u,
                                         __ATOMIC_RELEASE);
                    }
                }
                else
#endif
                {
                for (u32 frame = 0; frame < kFramesPerOutput; ++frame)
                {
                    const u32 src = ((read + frame) % kRingFrames) * kChannels;
                    block[frame * kChannels] = gBgmRing[src];
                    block[frame * kChannels + 1] = gBgmRing[src + 1];
                }
                }
                if (haveBgm)
                {
#if defined(TH07_PSP_MECC_AUDIO_4M)
                    if (read + kFramesPerOutput == kRingFrames)
                    {
                        bgmOutputWrap = true;
#if !defined(TH07_PSP_MECC_LOCAL_BGM)
                        // The SC-direct read replaces the old ME feeder fetch.
                        // Preserve the frozen wire counter as a consumer-wrap
                        // counter so R19 can still prove every ring boundary.
                        __atomic_fetch_add(&gBgmFetchWraps, 1u,
                                           __ATOMIC_RELAXED);
#endif
                    }
#endif
                __atomic_store_n(&gReadFrame, (read + kFramesPerOutput) % kRingFrames,
                                 __ATOMIC_RELEASE);
                }
            }
            else
            {
                if (primed && available < kFramesPerOutput)
                {
                    __atomic_add_fetch(&gUnderruns, 1u, __ATOMIC_RELAXED);
                }
                primed = false;
                // The normal I/O priority stays below the game thread so a
                // 64 KiB memory-stick read cannot steal a render frame.  If
                // the ring nevertheless reaches the restart threshold, lend
                // the producer an urgent priority until it publishes a block.
                if (available < kPrefillFrames && gProducerThread >= 0)
                {
                    sceKernelChangeThreadPriority(gProducerThread,
                                                  kBgmIoUrgentPriority);
                }
            }
        }
        else
        {
            primed = false;
        }

        if (!haveBgm)
        {
            std::memset(block, 0, kDacBufferBytes);
        }
        else
        {
            const u32 fadeRemaining =
                __atomic_load_n(&gFadeFramesRemaining, __ATOMIC_ACQUIRE);
            const u32 fadeTotal = __atomic_load_n(&gFadeFramesTotal, __ATOMIC_ACQUIRE);
            if (fadeRemaining && fadeTotal)
            {
                for (u32 sample = 0; sample < kFramesPerOutput * kChannels; ++sample)
                {
                    block[sample] = static_cast<i16>(
                        (static_cast<long long>(block[sample]) * fadeRemaining) / fadeTotal);
                }
                const u32 next = fadeRemaining > kFramesPerOutput
                                     ? fadeRemaining - kFramesPerOutput
                                     : 0;
                __atomic_store_n(&gFadeFramesRemaining, next, __ATOMIC_RELEASE);
                if (next == 0)
                {
                    __atomic_store_n(&gBgmPlaying, false, __ATOMIC_RELEASE);
                }
            }
        }

#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE))
        {
            primed = false;
            sceKernelDelayThread(10000);
            continue;
        }
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
        unsigned int limitedSamples = 0u;
        const unsigned int sfxToken =
            th07_audio4m_sfx_consume(block, kFramesPerOutput, &limitedSamples);
        const bool haveSfx = sfxToken != 0u;
        __atomic_fetch_add(&gSfxHeadroomLimitedSamples, limitedSamples,
                           __ATOMIC_RELAXED);
#else
        const bool haveSfx = MixSfxBlock(block, kFramesPerOutput);
#endif
        if (!haveBgm && !haveSfx)
        {
            sceKernelDelayThread(1000);
            continue;
        }
        // Both channels use the same volume.  Keep this on the exact output
        // path proven by TH06 PSP; PPSSPP's panned-blocking path can return
        // with the worker's saved state corrupted after the first block.
#if defined(TH07_PSP_MECC_AUDIO_4M)
        const int outputResult =
            sceAudioOutputBlocking(gAudioChannel, PSP_AUDIO_VOLUME_MAX, block);
#if !defined(TH07_PSP_SFX_MAIN_RAM)
        if (sfxToken != 0u)
        {
            th07_audio4m_sfx_output_committed(sfxToken, outputResult >= 0);
        }
#endif
        if (outputResult < 0)
        {
            LatchMeccFatal("MECC AUDIO4M DAC OUTPUT FAILED -> COLD REBOOT");
        }
        else
        {
            outputIndex ^= 1u;
            if (bgmOutputWrap)
            {
                __atomic_fetch_add(&gBgmOutputWraps, 1u, __ATOMIC_RELAXED);
            }
        }
#else
        if (sceAudioOutputBlocking(gAudioChannel, PSP_AUDIO_VOLUME_MAX, block) >= 0)
        {
            outputIndex ^= 1u;
        }
#endif
    }
    sceKernelExitThread(0);
    return 0;
}

void StopThreads()
{
    if (!__atomic_load_n(&gAudioAlive, __ATOMIC_ACQUIRE))
    {
        return;
    }
    __atomic_store_n(&gAudioAlive, false, __ATOMIC_RELEASE);
    if (gProducerThread >= 0)
    {
        sceKernelWaitThreadEnd(gProducerThread, nullptr);
        sceKernelDeleteThread(gProducerThread);
        gProducerThread = -1;
    }
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    if (gMeccFeederThread >= 0)
    {
        sceKernelWaitThreadEnd(gMeccFeederThread, nullptr);
        sceKernelDeleteThread(gMeccFeederThread);
        gMeccFeederThread = -1;
    }
#endif
    if (gOutputThread >= 0)
    {
        sceKernelWaitThreadEnd(gOutputThread, nullptr);
        sceKernelDeleteThread(gOutputThread);
        gOutputThread = -1;
    }
}

bool StartThreads()
{
    gFileSema = sceKernelCreateSema("th07_bgm_file", 0, 1, 1, nullptr);
    if (gFileSema < 0)
    {
        return false;
    }
    gAudioChannel =
        sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, kFramesPerOutput, PSP_AUDIO_FORMAT_STEREO);
    if (gAudioChannel < 0)
    {
        sceKernelDeleteSema(gFileSema);
        gFileSema = -1;
        return false;
    }

    __atomic_store_n(&gAudioAlive, true, __ATOMIC_RELEASE);
    gProducerThread = sceKernelCreateThread("th07_bgm_io", BgmProducerThread,
                                             kBgmIoUrgentPriority, 0x4000,
                                             PSP_THREAD_ATTR_USER, nullptr);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    if (__atomic_load_n(&gMeccLocalRing, __ATOMIC_ACQUIRE))
    {
        gMeccFeederThread = sceKernelCreateThread("th07_bgm_mecc_feed",
                                                   BgmMeccFeederThread,
                                                   0x12, 0x4000,
                                                   PSP_THREAD_ATTR_USER, nullptr);
    }
#endif
    gOutputThread = sceKernelCreateThread("th07_bgm_out", BgmOutputThread, 0x10, 0x4000,
                                           PSP_THREAD_ATTR_USER, nullptr);
    if (gProducerThread < 0 || gOutputThread < 0
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        || (__atomic_load_n(&gMeccLocalRing, __ATOMIC_ACQUIRE) &&
            gMeccFeederThread < 0)
#endif
    )
    {
        __atomic_store_n(&gAudioAlive, false, __ATOMIC_RELEASE);
        if (gProducerThread >= 0)
        {
            sceKernelDeleteThread(gProducerThread);
            gProducerThread = -1;
        }
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (gMeccFeederThread >= 0)
        {
            sceKernelDeleteThread(gMeccFeederThread);
            gMeccFeederThread = -1;
        }
#endif
        if (gOutputThread >= 0)
        {
            sceKernelDeleteThread(gOutputThread);
            gOutputThread = -1;
        }
        sceAudioChRelease(gAudioChannel);
        gAudioChannel = -1;
        sceKernelDeleteSema(gFileSema);
        gFileSema = -1;
        return false;
    }
    if (sceKernelStartThread(gProducerThread, 0, nullptr) < 0)
    {
        __atomic_store_n(&gAudioAlive, false, __ATOMIC_RELEASE);
        sceKernelDeleteThread(gProducerThread);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (gMeccFeederThread >= 0)
        {
            sceKernelDeleteThread(gMeccFeederThread);
            gMeccFeederThread = -1;
        }
#endif
        sceKernelDeleteThread(gOutputThread);
        gProducerThread = -1;
        gOutputThread = -1;
        sceAudioChRelease(gAudioChannel);
        gAudioChannel = -1;
        sceKernelDeleteSema(gFileSema);
        gFileSema = -1;
        return false;
    }
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    if (gMeccFeederThread >= 0 &&
        sceKernelStartThread(gMeccFeederThread, 0, nullptr) < 0)
    {
        __atomic_store_n(&gAudioAlive, false, __ATOMIC_RELEASE);
        sceKernelWaitThreadEnd(gProducerThread, nullptr);
        sceKernelDeleteThread(gProducerThread);
        sceKernelDeleteThread(gMeccFeederThread);
        sceKernelDeleteThread(gOutputThread);
        gProducerThread = -1;
        gMeccFeederThread = -1;
        gOutputThread = -1;
        sceAudioChRelease(gAudioChannel);
        gAudioChannel = -1;
        sceKernelDeleteSema(gFileSema);
        gFileSema = -1;
        return false;
    }
#endif
    if (sceKernelStartThread(gOutputThread, 0, nullptr) < 0)
    {
        __atomic_store_n(&gAudioAlive, false, __ATOMIC_RELEASE);
        sceKernelWaitThreadEnd(gProducerThread, nullptr);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (gMeccFeederThread >= 0)
            sceKernelWaitThreadEnd(gMeccFeederThread, nullptr);
#endif
        sceKernelDeleteThread(gProducerThread);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        if (gMeccFeederThread >= 0)
        {
            sceKernelDeleteThread(gMeccFeederThread);
            gMeccFeederThread = -1;
        }
#endif
        sceKernelDeleteThread(gOutputThread);
        gProducerThread = -1;
        gOutputThread = -1;
        sceAudioChRelease(gAudioChannel);
        gAudioChannel = -1;
        sceKernelDeleteSema(gFileSema);
        gFileSema = -1;
        return false;
    }
    return true;
}

void RemoveFirstCommand(SoundPlayer *player)
{
    for (int i = 0; i < 31; ++i)
    {
        player->commandQueue[i] = player->commandQueue[i + 1];
        if (player->commandQueue[i].opcode == 0)
        {
            break;
        }
    }
    player->commandQueue[31] = {};
}
} // namespace

#if defined(TH07_PSP_MECC_AUDIO_4M)
extern "C" void th07_psp_audio4m_latch_fatal(const char *message)
{
    LatchMeccFatal(message);
}
#endif

extern "C" void th07_psp_audio_set_system_suspended(int suspended)
{
#if defined(TH07_PSP_MECC_LOCAL_BGM) || defined(TH07_PSP_ME_RENDER_WORKER)
    if (suspended && th07_psp_me_audio_reset_committed())
    {
        // Power callbacks must not touch Memory Stick I/O.  Latch the
        // irreversible boundary here and defer the human-readable boot log to
        // ProcessQueues on the game thread.  Observer fatal publication is
        // atomic-only and is safe in callback context.
#if defined(TH07_PSP_ME_RENDER_WORKER)
        // Reset ownership makes resume unsupported. Publish that irreversible
        // worker disable even when another ME fault already latched fatal.
        Th07PspMeRenderSetAvailable(false);
#endif
        th07_psp_me_audio_suspend_latch();
        if (!__atomic_exchange_n(&gMeccFatal, true, __ATOMIC_ACQ_REL))
        {
            __atomic_store_n(&gBgmPlaying, false, __ATOMIC_RELEASE);
            __atomic_store_n(&gMeccSuspendLogPending, true, __ATOMIC_RELEASE);
#if defined(TH07_PSP_SHIKIGAMI)
#if defined(TH07_PSP_ME_RENDER_WORKER)
            th07_shikigami_record_fatal("MERW SUSPEND -> COLD REBOOT");
#else
            th07_shikigami_record_fatal("MECC BGM SUSPEND -> COLD REBOOT");
#endif
#endif
        }
    }
#endif
    __atomic_store_n(&gSystemSuspended, suspended != 0, __ATOMIC_RELEASE);
}

SoundPlayer g_SoundPlayer;

SoundPlayer::SoundPlayer()
{
    std::memset(this, 0, sizeof(*this));
    for (i32 &volume : unusedSoundVolRelated)
    {
        volume = -1;
    }
    for (i32 &queued : soundQueue)
    {
        queued = -1;
    }
    curBgmIdx = -1;
}

ZunResult SoundPlayer::InitializeSound()
{
#if defined(TH07_PSP_ME_RENDER_WORKER) && !defined(TH07_PSP_MECC_LOCAL_BGM)
    gMeccSuspendLogPending = false;
#endif
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    gBgmRing = nullptr;
    gMeccLocalRing = false;
    gMeccSuspendLogPending = false;
    gGeneration = 1;
    gMeccProducerAckGeneration = gGeneration;
    gMeccFeederAckGeneration = gGeneration;
    gMeccOutputAckGeneration = gGeneration;
#if defined(TH07_PSP_MECC_AUDIO_4M)
    gBgmCrcChecks = 0u;
    gBgmCrcMismatches = 0u;
    gBgmCrcMismatchRecordCount = 0u;
    std::memset(gBgmCrcMismatchRecords, 0, sizeof(gBgmCrcMismatchRecords));
#endif
#if defined(TH07_PSP_MECC_LOCAL_BGM) || defined(TH07_PSP_MECC_AUDIO_4M)
    gMeccFatal = false;
#endif
    ResetRing();
#else
    ResetRing();
    gGeneration = 1;
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M)
    gBgmUploadWraps = 0u;
    gBgmFetchWraps = 0u;
    gBgmOutputWraps = 0u;
#endif
    gFadeFramesRemaining = 0;
    gFadeFramesTotal = 0;
    gUnderruns = 0;
    gBgmPlaying = false;
    gBgmPaused = false;
    gBgmStageLoadBlocked = false;
    gSystemSuspended = false;
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    const int meInit = th07_psp_me_audio_init();
    if (meInit < 0)
    {
#if defined(TH07_PSP_MECC_AUDIO_4M)
        th07_psp_boot_note("MECC AUDIO INIT UNSAFE -> COLD REBOOT");
#else
        th07_psp_boot_note("MECC BGM INIT UNSAFE -> COLD REBOOT");
#endif
        return ZUN_ERROR;
    }
    if (meInit > 0)
    {
        if (!SelftestMeccBgmRing(gGeneration))
        {
#if defined(TH07_PSP_MECC_AUDIO_4M)
            LatchMeccFatal("MECC AUDIO4M BGM SELFTEST FAILED -> COLD REBOOT");
#else
            LatchMeccFatal("MECC BGM 384K SELFTEST FAILED -> COLD REBOOT");
#endif
            th07_psp_me_audio_shutdown();
            return ZUN_ERROR;
        }
        th07_psp_me_bgm_commit_owned();
        if (!th07_psp_me_bgm_is_active())
        {
            LatchMeccFatal("MECC BGM OWNERSHIP COMMIT FAILED -> COLD REBOOT");
            th07_psp_me_audio_shutdown();
            return ZUN_ERROR;
        }
        __atomic_store_n(&gMeccLocalRing, true, __ATOMIC_RELEASE);
        ResetRing();
        if (__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE))
        {
            __atomic_store_n(&gMeccLocalRing, false, __ATOMIC_RELEASE);
            th07_psp_me_audio_shutdown();
            return ZUN_ERROR;
        }
        unsigned int ignoredJobs, ignoredFallbacks, ignoredTimeouts, ignoredMaxWait;
        th07_psp_me_audio_diag_window(&ignoredJobs, &ignoredFallbacks,
                                      &ignoredTimeouts, &ignoredMaxWait);
#if defined(TH07_PSP_MECC_AUDIO_4M)
        th07_psp_boot_note("MECC AUDIO4M BGM 384K LOWER ON (LOCAL EDRAM)");
#else
        th07_psp_boot_note("MECC BGM 384K ON (LOCAL EDRAM)");
#endif
    }
    else
    {
#if defined(TH07_PSP_MECC_AUDIO_4M)
        th07_psp_boot_note("MECC AUDIO4M REQUIRES REAL PSP-3000 -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return ZUN_ERROR;
#else
        gBgmRing = new (std::nothrow) i16[kRingFrames * kChannels];
        if (!gBgmRing)
        {
            th07_psp_boot_note("MECC SAFE SC RING ALLOC FAILED");
            return ZUN_ERROR;
        }
        std::memset(gBgmRing, 0, kRingBytes);
        th07_psp_boot_note("MECC PROFILE SAFE SC RING FALLBACK");
#endif
    }
#else
#if defined(TH07_PSP_MECC_AUDIO_4M) && defined(TH07_PSP_BGM_MAIN_RAM)
    // Do not start ME Custom Core merely to move PCM between two Main-RAM
    // buffers.  The producer and DAC worker use the long-proven SC-direct ring,
    // leaving every byte of ME eDRAM unowned and untouched by TH07.
    th07_psp_boot_note("BGM MAIN RAM 384K SC-DIRECT; ME EDRAM UNUSED");
#else
    th07_psp_me_audio_init();
#endif
#endif
    if (!StartThreads())
    {
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        __atomic_store_n(&gMeccLocalRing, false, __ATOMIC_RELEASE);
#endif
        th07_psp_me_audio_shutdown();
#if defined(TH07_PSP_MECC_LOCAL_BGM)
        delete[] gBgmRing;
        gBgmRing = nullptr;
#endif
        th07_psp_boot_note("bgm audio init failed");
        return ZUN_ERROR;
    }
    th07_psp_boot_note("bgm audio ready");
    return ZUN_SUCCESS;
}

ZunResult SoundPlayer::Release()
{
    __atomic_store_n(&gBgmStageLoadBlocked, false, __ATOMIC_RELEASE);
    StopBGM();
    StopThreads();
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
    th07_audio4m_sfx_shutdown();
#endif
    const u32 mixedBlocks = __atomic_load_n(&gSfxMixedBlocks, __ATOMIC_ACQUIRE);
    const u32 totalMixUs = __atomic_load_n(&gSfxScTotalMixUs, __ATOMIC_ACQUIRE);
    th07_psp_boot_notef("audio stats U%lu T%lu M%lu L%lu SA%lu SM%lu",
                        static_cast<unsigned long>(
                            __atomic_load_n(&gUnderruns, __ATOMIC_ACQUIRE)),
                        static_cast<unsigned long>(
                            __atomic_load_n(&gSfxTriggerCount, __ATOMIC_ACQUIRE)),
                        static_cast<unsigned long>(mixedBlocks),
                        static_cast<unsigned long>(
                            __atomic_load_n(&gSfxHeadroomLimitedSamples,
                                            __ATOMIC_ACQUIRE)),
                        static_cast<unsigned long>(mixedBlocks ? totalMixUs / mixedBlocks : 0u),
                        static_cast<unsigned long>(
                            __atomic_load_n(&gSfxScMaxMixUs, __ATOMIC_ACQUIRE)));
#if defined(TH07_PSP_MECC_AUDIO_4M) && defined(TH07_PSP_MECC_LOCAL_BGM)
    FlushBgmCrcMismatchRecords();
    th07_psp_boot_notef("bgm crc checks %lu mismatch %lu",
                        static_cast<unsigned long>(
                            __atomic_load_n(&gBgmCrcChecks, __ATOMIC_ACQUIRE)),
                        static_cast<unsigned long>(
                            __atomic_load_n(&gBgmCrcMismatches, __ATOMIC_ACQUIRE)));
#endif
    th07_psp_me_audio_shutdown();
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    if (th07_psp_me_audio_faulted())
    {
        LatchMeccFatal("MECC BGM STOP NOT CONFIRMED -> COLD REBOOT");
    }
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (th07_psp_me_audio_power_locked())
    {
        th07_psp_boot_note("MECC AUDIO4M EXIT BLOCKED; HOLD POWER FOR COLD OFF");
        for (;;)
        {
            sceKernelDelayThread(1000u * 1000u);
        }
    }
#endif
    __atomic_store_n(&gMeccLocalRing, false, __ATOMIC_RELEASE);
    delete[] gBgmRing;
    gBgmRing = nullptr;
#endif
    if (gAudioChannel >= 0)
    {
        sceAudioChRelease(gAudioChannel);
        gAudioChannel = -1;
    }
    if (gFileSema >= 0)
    {
        sceKernelDeleteSema(gFileSema);
        gFileSema = -1;
    }
    if (bgmFmtData)
    {
        free(bgmFmtData);
        bgmFmtData = nullptr;
    }
    for (PspSfxBuffer &sound : gSfxBuffers)
    {
        delete[] sound.samples;
        sound = {};
    }
    ResetSfxVoices();
    return ZUN_SUCCESS;
}

i32 SoundPlayer::GetFmtIndexByName(const char *path)
{
    if (!bgmFmtData || !path)
    {
        return -1;
    }
    const char *name = std::strrchr(path, '/');
    const char *backslash = std::strrchr(path, '\\');
    if (!name || (backslash && backslash > name))
    {
        name = backslash;
    }
    name = name ? name + 1 : path;
    for (u32 i = 0; i < gFmtCount && bgmFmtData[i].name[0]; ++i)
    {
        if (std::strncmp(bgmFmtData[i].name, name, sizeof(bgmFmtData[i].name)) == 0)
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

ZunResult SoundPlayer::LoadSound(i32 idx, const char *path)
{
    if (idx < 0 || idx >= static_cast<i32>(kSfxBufferCount) || !path)
    {
        return ZUN_ERROR;
    }

    u8 *wav = static_cast<u8 *>(FileSystem::OpenFile(path, 0));
    const u32 wavSize = g_LastFileSize;
    if (!wav || wavSize < 44 || std::memcmp(wav, "RIFF", 4) != 0 ||
        std::memcmp(wav + 8, "WAVE", 4) != 0)
    {
        free(wav);
        return ZUN_ERROR;
    }

    const u8 *fmt = nullptr;
    u32 fmtSize = 0;
    const u8 *pcm = nullptr;
    u32 pcmBytes = 0;
    for (u32 offset = 12; offset + 8 <= wavSize;)
    {
        const u8 *chunk = wav + offset;
        const u32 chunkBytes = ReadLe32(chunk + 4);
        const u32 payload = offset + 8;
        if (payload > wavSize || chunkBytes > wavSize - payload)
        {
            break;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0)
        {
            fmt = wav + payload;
            fmtSize = chunkBytes;
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            pcm = wav + payload;
            pcmBytes = chunkBytes;
        }
        const u32 padded = (chunkBytes + 1u) & ~1u;
        if (padded > wavSize - payload)
        {
            break;
        }
        offset = payload + padded;
    }

    if (!fmt || fmtSize < 16 || !pcm || ReadLe16(fmt) != 1)
    {
        free(wav);
        return ZUN_ERROR;
    }
    const u32 channels = ReadLe16(fmt + 2);
    const u32 sampleRate = ReadLe32(fmt + 4);
    const u32 blockAlign = ReadLe16(fmt + 12);
    const u32 bits = ReadLe16(fmt + 14);
    if ((channels != 1 && channels != 2) || sampleRate == 0 || blockAlign == 0 ||
        (bits != 8 && bits != 16))
    {
        free(wav);
        return ZUN_ERROR;
    }

    const u32 sourceFrames = pcmBytes / blockAlign;
    if (sourceFrames == 0)
    {
        free(wav);
        return ZUN_ERROR;
    }
#if defined(TH07_PSP_1000)
    // Keep every original source frame. G.711 mu-law uses one byte per mono
    // frame, the same RAM as the old half-rate s16 path, without throwing away
    // the upper half of the SFX frequency range.
    const u32 storedFrames = sourceFrames;
    const u32 storedSampleRate = sampleRate;
#else
    const u32 storedFrames = sourceFrames;
    const u32 storedSampleRate = sampleRate;
#endif
    PspSfxSample *samples = new (std::nothrow) PspSfxSample[storedFrames];
    if (!samples)
    {
        free(wav);
        return ZUN_ERROR;
    }

    const auto readMono = [&](u32 frame) -> i32 {
        frame = std::min(frame, sourceFrames - 1u);
        const u8 *source = pcm + frame * blockAlign;
        if (bits == 8)
        {
            i32 value = (static_cast<i32>(source[0]) - 128) << 8;
            if (channels == 2)
            {
                value = (value + ((static_cast<i32>(source[1]) - 128) << 8)) / 2;
            }
            return value;
        }
        i32 value = static_cast<i16>(ReadLe16(source));
        if (channels == 2)
        {
            value = (value + static_cast<i16>(ReadLe16(source + 2))) / 2;
        }
        return value;
    };

    for (u32 frame = 0; frame < storedFrames; ++frame)
    {
#if defined(TH07_PSP_1000)
        samples[frame] = EncodeMuLaw8(readMono(frame));
#else
        samples[frame] = static_cast<i16>(readMono(frame));
#endif
    }
    free(wav);

    delete[] gSfxBuffers[idx].samples;
    gSfxBuffers[idx].samples = samples;
    gSfxBuffers[idx].frames = storedFrames;
    gSfxBuffers[idx].stepFixed = std::max<u32>(
        1, static_cast<u32>((static_cast<unsigned long long>(storedSampleRate) << 16) / 44100u));
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
    if (!th07_audio4m_sfx_upload_buffer(
            static_cast<unsigned int>(idx), samples, storedFrames,
            gSfxBuffers[idx].stepFixed))
    {
        delete[] samples;
        gSfxBuffers[idx] = {};
        return ZUN_ERROR;
    }
    delete[] samples;
    gSfxBuffers[idx].samples = nullptr;
#else
    sceKernelDcacheWritebackRange(samples, storedFrames * sizeof(PspSfxSample));
#endif
    return ZUN_SUCCESS;
}

ZunResult SoundPlayer::LoadFmt(const char *path)
{
    if (bgmFmtData)
    {
        free(bgmFmtData);
        bgmFmtData = nullptr;
    }
    bgmFmtData = reinterpret_cast<ThBgmFormat *>(FileSystem::OpenFile(path, 0));
    if (!bgmFmtData)
    {
        return ZUN_ERROR;
    }
    gFmtCount = g_LastFileSize / sizeof(ThBgmFormat);
    if (gFmtCount == 0)
    {
        free(bgmFmtData);
        bgmFmtData = nullptr;
        return ZUN_ERROR;
    }
    char message[64];
    std::snprintf(message, sizeof(message), "bgm fmt entries %lu",
                  static_cast<unsigned long>(gFmtCount));
    th07_psp_boot_note(message);
    return ZUN_SUCCESS;
}

ZunResult SoundPlayer::StartBGM(const char *path)
{
    char resolved[768];
    th07_psp_resolve_path(path, resolved, sizeof(resolved));
    if (std::strlen(resolved) >= sizeof(bgmArchivePath))
    {
        th07_psp_boot_note("bgm archive path too long");
        return ZUN_ERROR;
    }
    std::snprintf(bgmArchivePath, sizeof(bgmArchivePath), "%s", resolved);
    FILE *probe = fopen(bgmArchivePath, "rb");
    if (!probe)
    {
        th07_psp_boot_note("bgm archive missing: thbgm.dat");
        return ZUN_ERROR;
    }
    fclose(probe);
    th07_psp_boot_note("bgm archive ready");
    return ZUN_SUCCESS;
}

void SoundPlayer::SetBgmStageLoadBlocked(bool blocked)
{
    // This gate is independent of pause/menu state and track generation. The
    // producer keeps filling Main RAM, but OutputThread cannot consume or
    // advance gReadFrame until gameplay is ready.
    th07_psp_boot_note(blocked ? "bgm stage-load gate on" : "bgm stage-load gate off");
    // In particular, the OFF note must finish before the release-store. A
    // release build writes boot notes synchronously, and logging after OFF
    // would let BGM advance while AddedCallback was still blocked in I/O.
    __atomic_store_n(&gBgmStageLoadBlocked, blocked, __ATOMIC_RELEASE);
}

ZunResult SoundPlayer::ReopenBGM(const char *name)
{
    const i32 fmtIdx = GetFmtIndexByName(name);
    if (fmtIdx < 0 || !bgmArchivePath[0])
    {
        th07_psp_boot_note("bgm track not found in fmt");
        return ZUN_ERROR;
    }
    const ThBgmFormat &fmt = bgmFmtData[fmtIdx];
    if (fmt.format.wFormatTag != 1 || fmt.format.nChannels != 2 ||
        fmt.format.nSamplesPerSec != 44100 || fmt.format.wBitsPerSample != 16 ||
        fmt.format.nBlockAlign != kBytesPerFrame || fmt.introLength < 0 ||
        fmt.totalLength <= 0 || fmt.introLength >= fmt.totalLength)
    {
        th07_psp_boot_note("bgm track format invalid");
        return ZUN_ERROR;
    }

    FILE *file = fopen(bgmArchivePath, "rb");
    if (!file || fseek(file, bgmSeekOffset + fmt.startOffset, SEEK_SET) != 0)
    {
        if (file)
        {
            fclose(file);
        }
        th07_psp_boot_note("bgm track open failed");
        return ZUN_ERROR;
    }

    __atomic_store_n(&gBgmPlaying, false, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    // Drain any old-generation reader before publishing the new generation
    // and replacing the file under the same semaphore.  A producer can then
    // only read old-file/old-epoch or new-file/new-epoch.
    LockFile();
    __atomic_add_fetch(&gGeneration, 1u, __ATOMIC_ACQ_REL);
#else
    __atomic_add_fetch(&gGeneration, 1u, __ATOMIC_ACQ_REL);
    LockFile();
#endif
    if (gBgmFile)
    {
        fclose(gBgmFile);
    }
    gBgmFile = file;
    gTrackBase = bgmSeekOffset + fmt.startOffset;
    gTrackCursor = 0;
    gTrackIntroBytes = static_cast<u32>(fmt.introLength) & ~(kBytesPerFrame - 1u);
    gTrackTotalBytes = static_cast<u32>(fmt.totalLength) & ~(kBytesPerFrame - 1u);
    UnlockFile();
    ResetRing();
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    if (__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE))
    {
        CloseTrackFile();
        return ZUN_ERROR;
    }
#endif
    gFadeFramesRemaining = 0;
    gFadeFramesTotal = 0;
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    __atomic_store_n(&gBgmPaused, false, __ATOMIC_RELEASE);
    __atomic_store_n(&gBgmPlaying, true, __ATOMIC_RELEASE);
#else
    gBgmPaused = false;
    gBgmPlaying = true;
#endif
    curBgmIdx = fmtIdx;

    char message[128];
    std::snprintf(message, sizeof(message), "bgm open %.16s off %d intro %d total %d",
                  fmt.name, fmt.startOffset, fmt.introLength, fmt.totalLength);
    th07_psp_boot_note(message);
    return ZUN_SUCCESS;
}

ZunResult SoundPlayer::PreloadBGM(i32 idx, const char *path)
{
    if (idx < 0 || idx >= 16 || !path)
    {
        return ZUN_ERROR;
    }
    std::snprintf(bgmFileNames[idx], sizeof(bgmFileNames[idx]), "%s", path);
    return ZUN_SUCCESS;
}

ZunResult SoundPlayer::LoadBGM(i32 idx)
{
    if (idx < 0 || idx >= 16 || !bgmFileNames[idx][0])
    {
        return ZUN_ERROR;
    }
    return ReopenBGM(bgmFileNames[idx]);
}

void SoundPlayer::StopBGM()
{
    __atomic_store_n(&gBgmPlaying, false, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    LockFile();
    __atomic_add_fetch(&gGeneration, 1u, __ATOMIC_ACQ_REL);
    if (gBgmFile)
    {
        fclose(gBgmFile);
        gBgmFile = nullptr;
    }
    gTrackBase = 0;
    gTrackCursor = 0;
    gTrackIntroBytes = 0;
    gTrackTotalBytes = 0;
    UnlockFile();
    ResetRing();
    gFadeFramesRemaining = 0;
    gFadeFramesTotal = 0;
#else
    __atomic_add_fetch(&gGeneration, 1u, __ATOMIC_ACQ_REL);
    ResetRing();
    gFadeFramesRemaining = 0;
    gFadeFramesTotal = 0;
    CloseTrackFile();
#endif
}

ZunResult SoundPlayer::InitSoundBuffers()
{
    for (i32 &queued : soundQueue)
    {
        queued = -1;
    }
    ResetSfxVoices();
    gSfxTriggerCount = 0;
    gSfxMixedBlocks = 0;
    gSfxHeadroomLimitedSamples = 0;
    gSfxScTotalMixUs = 0;
    gSfxScMaxMixUs = 0;
    gSePowerStarts = 0;
    gSePowerEnds = 0;
    gSePowerIgnored = 0;
    gSePowerActive = 0;
    std::memset(gSfxRequestCounts, 0, sizeof(gSfxRequestCounts));
    std::memset(gSfxCooldown, 0, sizeof(gSfxCooldown));
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
    if (!th07_audio4m_sfx_begin(gIoBuffer, sizeof(gIoBuffer)))
    {
        LatchMeccFatal("MECC AUDIO4M SFX ATLAS BEGIN FAILED -> COLD REBOOT");
        return ZUN_ERROR;
    }
#endif

    u32 loaded = 0;
    u32 totalFrames = 0;
    for (u32 idx = 0; idx < kSfxBufferCount; ++idx)
    {
        if (LoadSound(static_cast<i32>(idx), g_SFXList[idx]) == ZUN_SUCCESS)
        {
            loaded++;
            totalFrames += gSfxBuffers[idx].frames;
        }
        else
        {
            char message[96];
            std::snprintf(message, sizeof(message), "sfx load failed %lu %.40s",
                          static_cast<unsigned long>(idx), g_SFXList[idx]);
            th07_psp_boot_note(message);
        }
    }
    for (u32 idx = 0; idx < kSfxLogicalCount; ++idx)
    {
        const float gain = std::pow(10.0f, SOUND_BUFFER_IDX_VOL[idx].volume / 2000.0f);
        gSfxGainQ15[idx] = static_cast<u16>(
            std::max(0, std::min(32768, static_cast<int>(gain * 32768.0f + 0.5f))));
    }
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
    if (loaded != kSfxBufferCount || !th07_audio4m_sfx_finalize())
    {
        LatchMeccFatal("MECC AUDIO4M SFX ATLAS FINALIZE FAILED -> COLD REBOOT");
        return ZUN_ERROR;
    }
#endif
    char message[80];
#if defined(TH07_PSP_1000)
    constexpr const char *storageName = "mulaw";
#else
    constexpr const char *storageName = "pcm";
#endif
    std::snprintf(message, sizeof(message), "sfx ready %lu/%lu %s %luKB",
                  static_cast<unsigned long>(loaded),
                  static_cast<unsigned long>(kSfxBufferCount),
                  storageName,
                  static_cast<unsigned long>(totalFrames * sizeof(PspSfxSample) / 1024u));
    th07_psp_boot_note(message);
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
    th07_psp_boot_note("MECC AUDIO4M SFX ATLAS 2048KB PCM (NO PADDING)");
    th07_psp_boot_note("audio mix ME local SFX -> 2x4KB WIDE FIFO / ONE FINAL CLAMP");
    th07_psp_boot_note("AUDIO4M READY: GAME-REQUESTED SE ONLY");
#elif defined(TH07_PSP_SFX_MAIN_RAM)
    th07_psp_boot_note("SE Main RAM PCM / SC wide mixer");
#if defined(TH07_PSP_BGM_MAIN_RAM)
    th07_psp_boot_note("BGM Main RAM 384K; ME eDRAM disabled");
#else
    th07_psp_boot_note("MECC BGM 384K lower; ME upper/SFX FIFO disabled");
#endif
#else
    th07_psp_boot_note("audio mix SC wide SFX residual headroom");
#endif
    return ZUN_SUCCESS;
}

void SoundPlayer::PlaySoundByIdx(i32 idx, u32)
{
    if (idx < 0 || idx >= static_cast<i32>(kSfxLogicalCount))
    {
        return;
    }
#if defined(TH07_PSP_DIRECT_GAME)
    if (idx == SOUND_BOMB_SAKUYA_A)
    {
        static u32 sePowerTraceCount;
        if (sePowerTraceCount++ < 32)
        {
            char message[80];
            std::snprintf(message, sizeof(message), "se_power0 caller %p",
                          __builtin_return_address(0));
            th07_psp_boot_note(message);
        }
    }
#endif
    ++gSfxRequestCounts[idx];
    // Full-power auto-fire can hit several enemies in the same update.  The
    // original single DirectSound buffer then restarts se_damage00.wav over
    // and over; on PSP that turns the 150 ms hit into a continuous low drone.
    // Keep the effect audible, but allow each instance to finish first.
    if ((idx == SOUND_BOMB_MARISA_A_FOCUS || idx == SOUND_20 || idx == SOUND_25) &&
        gSfxCooldown[idx] != 0)
    {
        return;
    }
    if (idx == SOUND_BOMB_MARISA_A_FOCUS)
    {
        // Reimu-A's full-power debug stream (and some enemy patterns) can
        // request se_tan00.wav hundreds of times per ten seconds.  Its sample
        // lasts about 0.34 s, so repeated restarts become the reported drone.
        gSfxCooldown[idx] = 21;
    }
    else if (idx == SOUND_20)
    {
        gSfxCooldown[idx] = 18;
    }
    else if (idx == SOUND_25)
    {
        // Cirno's pattern requests the 1.59 s se_kira00.wav about 24 times a
        // second.  Let one complete instead of continually restarting its
        // first few milliseconds as a low drone.
        gSfxCooldown[idx] = 96;
    }
    i32 queueIdx = 0;
    for (; queueIdx < 5; ++queueIdx)
    {
        if (soundQueue[queueIdx] < 0)
        {
            break;
        }
        if (soundQueue[queueIdx] == idx)
        {
            return;
        }
    }
    if (queueIdx >= 5)
    {
        return;
    }
    soundQueue[queueIdx] = idx;
    unusedSoundVolRelated[idx] = SOUND_BUFFER_IDX_VOL[idx].field2_0x6;
}

void SoundPlayer::StopSoundByIdx(i32 idx)
{
    if (idx < 0 || idx >= static_cast<i32>(kSfxLogicalCount))
    {
        return;
    }
    for (i32 &queued : soundQueue)
    {
        if (queued == idx)
        {
            queued = -1;
        }
    }
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
    th07_audio4m_sfx_stop_logical(static_cast<unsigned int>(idx));
#else
    if (idx < 32)
    {
        const u32 bit = 1u << idx;
        __atomic_fetch_and(&gPendingSfxMaskLow, ~bit, __ATOMIC_RELEASE);
        __atomic_fetch_or(&gStopSfxMaskLow, bit, __ATOMIC_RELEASE);
    }
    else
    {
        const u32 bit = 1u << (idx - 32);
        __atomic_fetch_and(&gPendingSfxMaskHigh, ~bit, __ATOMIC_RELEASE);
        __atomic_fetch_or(&gStopSfxMaskHigh, bit, __ATOMIC_RELEASE);
    }
#endif
    gSfxCooldown[idx] = 0;
}

i32 SoundPlayer::ProcessQueues()
{
#if defined(TH07_PSP_MECC_LOCAL_BGM) || defined(TH07_PSP_ME_RENDER_WORKER)
    if (__atomic_exchange_n(&gMeccSuspendLogPending, false, __ATOMIC_ACQ_REL))
    {
#if defined(TH07_PSP_ME_RENDER_WORKER)
        th07_psp_boot_note("MERW SUSPEND -> COLD REBOOT");
#else
        th07_psp_boot_note("MECC BGM SUSPEND -> COLD REBOOT");
#endif
    }
#endif
    for (u32 &cooldown : gSfxCooldown)
    {
        if (cooldown != 0)
        {
            --cooldown;
        }
    }
#if defined(TH07_PSP_DIRECT_GAME)
    static unsigned int sfxReportTicks;
    if (++sfxReportTicks >= 600)
    {
        sfxReportTicks = 0;
        char sePowerMessage[112];
        std::snprintf(sePowerMessage, sizeof(sePowerMessage),
                      "se_power0 mix starts %lu ends %lu ignored %lu active %lu",
                      static_cast<unsigned long>(
                          __atomic_load_n(&gSePowerStarts, __ATOMIC_ACQUIRE)),
                      static_cast<unsigned long>(
                          __atomic_load_n(&gSePowerEnds, __ATOMIC_ACQUIRE)),
                      static_cast<unsigned long>(
                          __atomic_load_n(&gSePowerIgnored, __ATOMIC_ACQUIRE)),
                      static_cast<unsigned long>(
                          __atomic_load_n(&gSePowerActive, __ATOMIC_ACQUIRE)));
        th07_psp_boot_note(sePowerMessage);
        for (u32 idx = 0; idx < kSfxLogicalCount; ++idx)
        {
            if (gSfxRequestCounts[idx] == 0)
            {
                continue;
            }
            char message[64];
            std::snprintf(message, sizeof(message), "sfx requests idx %lu count %lu",
                          static_cast<unsigned long>(idx),
                          static_cast<unsigned long>(gSfxRequestCounts[idx]));
            th07_psp_boot_note(message);
            gSfxRequestCounts[idx] = 0;
        }
    }
#endif
    SoundPlayerCommand &command = commandQueue[0];
    if (command.opcode)
    {
        switch (command.opcode)
        {
        case AUDIO_PRELOAD:
            PreloadBGM(command.arg1, command.string);
            break;
        case AUDIO_START:
            if (command.arg1 >= 0)
            {
                LoadBGM(command.arg1);
            }
            else
            {
                ReopenBGM(command.string);
            }
            break;
        case AUDIO_STOP:
        case AUDIO_SHUTDOWN:
            StopBGM();
            break;
        case AUDIO_FADEOUT: {
            const u32 frames = static_cast<u32>(std::max(command.arg1, 1)) * 44100u;
            __atomic_store_n(&gFadeFramesTotal, frames, __ATOMIC_RELEASE);
            __atomic_store_n(&gFadeFramesRemaining, frames, __ATOMIC_RELEASE);
            break;
        }
        case AUDIO_PAUSE:
            __atomic_store_n(&gBgmPaused, true, __ATOMIC_RELEASE);
            break;
        case AUDIO_UNPAUSE:
            __atomic_store_n(&gBgmPaused, false, __ATOMIC_RELEASE);
            break;
        default:
            break;
        }
        RemoveFirstCommand(this);
    }

    if (g_Supervisor.cfg.playSounds)
    {
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
        for (i32 &queued : soundQueue)
        {
            if (queued >= 0 && queued < static_cast<i32>(kSfxLogicalCount))
            {
                const unsigned int logical = static_cast<unsigned int>(queued);
                th07_audio4m_sfx_request(
                    logical,
                    static_cast<unsigned int>(SOUND_BUFFER_IDX_VOL[logical].bufferIdx),
                    static_cast<unsigned int>(gSfxGainQ15[logical]) << 1);
                queued = -1;
                ++gSfxTriggerCount;
            }
        }
#else
        u32 pendingLow = 0;
        u32 pendingHigh = 0;
        for (i32 &queued : soundQueue)
        {
            if (queued >= 0 && queued < static_cast<i32>(kSfxLogicalCount))
            {
                if (queued < 32)
                {
                    pendingLow |= 1u << queued;
                }
                else
                {
                    pendingHigh |= 1u << (queued - 32);
                }
                queued = -1;
            }
        }
        if (pendingLow || pendingHigh)
        {
            if (pendingLow)
            {
                __atomic_fetch_or(&gPendingSfxMaskLow, pendingLow, __ATOMIC_RELEASE);
            }
            if (pendingHigh)
            {
                __atomic_fetch_or(&gPendingSfxMaskHigh, pendingHigh, __ATOMIC_RELEASE);
            }
            // Only the game thread writes this diagnostic counter.
            gSfxTriggerCount += static_cast<u32>(__builtin_popcount(pendingLow) +
                                                  __builtin_popcount(pendingHigh));
        }
#endif
    }
    return commandQueue[0].opcode;
}

void SoundPlayer::PushCommand(AudioOpcode opcode, i32 arg1, const char *arg2)
{
    for (i32 i = 0; i < 31; ++i)
    {
        if (commandQueue[i].opcode)
        {
            continue;
        }
        commandQueue[i].opcode = opcode;
        commandQueue[i].arg1 = arg1;
        commandQueue[i].arg2 = 0;
        std::snprintf(commandQueue[i].string, sizeof(commandQueue[i].string), "%s",
                      arg2 ? arg2 : "");
        break;
    }
}

#if defined(TH07_PSP_SHIKIGAMI)
extern "C" void th07_psp_audio_shikigami_snapshot(
    Th07ShikigamiAudioSnapshot *snapshot)
{
    if (!snapshot)
    {
        return;
    }
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    snapshot->ring_bytes = kRingBytes;
#else
    snapshot->ring_bytes = sizeof(gBgmRing);
#endif
    snapshot->ring_fill_frames = RingCount();
    snapshot->underruns = __atomic_load_n(&gUnderruns, __ATOMIC_ACQUIRE);
    snapshot->generation = __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE);
    snapshot->bgm_index = g_SoundPlayer.curBgmIdx;
    snapshot->playing =
        __atomic_load_n(&gBgmPlaying, __ATOMIC_ACQUIRE) ? 1u : 0u;
    snapshot->paused =
        __atomic_load_n(&gBgmPaused, __ATOMIC_ACQUIRE) ? 1u : 0u;
    unsigned int meRingBase = 0;
    unsigned int meRingBytes = 0;
    unsigned int meAudioJobs = 0;
    unsigned int meFallbacks = 0;
    unsigned int meTimeouts = 0;
    unsigned int meMaxWaitUs = 0;
#if defined(TH07_PSP_MECC_LOCAL_BGM)
    // Report only the extent the ME actually owns and uses.  R18 places the
    // complete 384 KiB BGM ring at 0x00010000 in lower eDRAM and owns no byte
    // at or above the 2 MiB upper-half boundary.
    th07_psp_me_bgm_extent(&meRingBase, &meRingBytes);
    th07_psp_me_audio_diag_snapshot(&meAudioJobs, &meFallbacks,
                                    &meTimeouts, &meMaxWaitUs);
#endif
    // Wire field names predate the lower-ring migration.  Zero base/bytes in
    // R19 is the factual assertion that TH07 owns no ME eDRAM at runtime.
    snapshot->me_upper_base = meRingBase;
    snapshot->me_upper_bytes = meRingBytes;
    snapshot->me_jobs = meAudioJobs;
    snapshot->me_fallbacks = meFallbacks;
    snapshot->me_timeouts = meTimeouts;
    snapshot->me_max_wait_us = meMaxWaitUs;
#if defined(TH07_PSP_MECC_AUDIO_4M)
    Th07Audio4mSfxSnapshot sfx{};
#if !defined(TH07_PSP_SFX_MAIN_RAM)
    th07_audio4m_sfx_snapshot(&sfx);
#endif
    snapshot->sfx_atlas_bytes = sfx.atlas_bytes;
    snapshot->sfx_canonical_bytes = sfx.canonical_bytes;
    snapshot->sfx_replica_bytes = sfx.replica_bytes;
    snapshot->sfx_canonical_output_mask = sfx.canonical_output_mask;
    snapshot->sfx_replica_output_mask = sfx.replica_output_mask;
    snapshot->sfx_mix_jobs = sfx.mix_jobs;
    snapshot->sfx_output_blocks = sfx.output_blocks;
    snapshot->sfx_fifo_misses = sfx.fifo_misses;
    snapshot->sfx_fatal = sfx.fatal;
    snapshot->sfx_coverage_active = sfx.coverage_active;
    snapshot->sfx_coverage_complete = sfx.coverage_complete;
    snapshot->sfx_coverage_pass = sfx.coverage_pass;
    snapshot->sfx_coverage_buffer = sfx.coverage_buffer;
    snapshot->bgm_upload_wraps =
        __atomic_load_n(&gBgmUploadWraps, __ATOMIC_ACQUIRE);
    snapshot->bgm_fetch_wraps =
        __atomic_load_n(&gBgmFetchWraps, __ATOMIC_ACQUIRE);
    snapshot->bgm_output_wraps =
        __atomic_load_n(&gBgmOutputWraps, __ATOMIC_ACQUIRE);

    constexpr u32 requiredSfxMask = (1u << kSfxBufferCount) - 1u;
    u32 proof = 0u;
#if !defined(TH07_PSP_BGM_MAIN_RAM)
    if (meRingBase == 0x00010000u && meRingBytes == 384u * 1024u)
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_FULL_EXTENT;
#endif
    if (sfx.atlas_bytes == 2u * 1024u * 1024u &&
        sfx.canonical_bytes != 0u && sfx.replica_bytes != 0u &&
        sfx.canonical_bytes + sfx.replica_bytes == sfx.atlas_bytes)
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_ATLAS_EXACT;
    if (sfx.canonical_output_mask == requiredSfxMask)
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_CANONICAL_DAC;
    if (sfx.replica_output_mask == requiredSfxMask)
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_REPLICA_DAC;
    if (snapshot->bgm_upload_wraps != 0u)
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_BGM_UPLOAD_WRAP;
    if (snapshot->bgm_fetch_wraps != 0u)
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_BGM_FETCH_WRAP;
    if (snapshot->bgm_output_wraps != 0u)
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_BGM_OUTPUT_WRAP;
    // PROVEN remains an exhaustive diagnostic claim.  Normal startup never
    // schedules the audible 30x2 sweep, so ordinary gameplay reports factual
    // runtime activity without being pressured to exercise every atlas entry.
#if defined(TH07_PSP_SFX_MAIN_RAM)
    // SE mixes on the SC from Main RAM; the eDRAM SFX FIFO/coverage machinery
    // never runs.  The R19 BGM backend also remains wholly on the SC, so its
    // zero-fault gate has no ME ownership, cache or retention precondition.
#if defined(TH07_PSP_BGM_MAIN_RAM)
    if (snapshot->underruns == 0u && meFallbacks == 0u && meTimeouts == 0u &&
        !__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE))
    {
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_ZERO_FAULTS;
    }
#else
    if (snapshot->underruns == 0u && meFallbacks == 0u && meTimeouts == 0u &&
        __atomic_load_n(&gBgmCrcMismatches, __ATOMIC_ACQUIRE) == 0u &&
        !__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE) &&
        !th07_psp_me_audio_faulted() && th07_psp_me_audio_stack_guard_ok() &&
        th07_psp_me_audio_power_locked() && th07_psp_me_bgm_is_active())
    {
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_ZERO_FAULTS;
    }
#endif
#else
    if (sfx.fifo_misses == 0u && sfx.fatal == 0u &&
        sfx.coverage_complete != 0u && sfx.coverage_active == 0u &&
        sfx.coverage_pass == 1u && sfx.coverage_buffer == kSfxBufferCount &&
        snapshot->underruns == 0u && meFallbacks == 0u && meTimeouts == 0u &&
        __atomic_load_n(&gBgmCrcMismatches, __ATOMIC_ACQUIRE) == 0u &&
        !__atomic_load_n(&gMeccFatal, __ATOMIC_ACQUIRE) &&
        !th07_psp_me_audio_faulted() && th07_psp_me_audio_stack_guard_ok() &&
        th07_psp_me_audio_power_locked() && th07_psp_me_bgm_is_active())
    {
        proof |= TH07_SHIKIGAMI_AUDIO4M_PROOF_ZERO_FAULTS;
    }
#endif
    snapshot->audio4m_proof_flags = proof;
#endif
}
#endif
