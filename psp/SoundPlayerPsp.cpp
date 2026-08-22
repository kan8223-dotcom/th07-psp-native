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
#include "audio_me.h"
#include "fileio.hpp"

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
constexpr u32 kPrefillFrames = 4096;
constexpr u32 kIoFrames = 16 * 1024;
constexpr u32 kRingFrames = 96 * 1024;
constexpr u32 kSfxBufferCount = 30;
constexpr u32 kSfxLogicalCount = 38;
constexpr u32 kSfxVoiceCount = 16;
constexpr u32 kUnityGainQ16 = 65536u;
// Restore at 1/128 full scale per 512-frame block (about 1.5 seconds from
// silence to unity).  Gain reductions remain immediate so a new burst can
// never hard-clip while the slower release avoids audible block pumping.
constexpr u32 kMixGainReleaseQ16 = 512u;
constexpr int kBgmIoUrgentPriority = 0x1c;
constexpr int kBgmIoBackgroundPriority = 0x21;

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

alignas(64) i16 gBgmRing[kRingFrames * kChannels];
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
volatile u32 gMixMasterGainQ16 = kUnityGainQ16;
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
volatile bool gSystemSuspended;

SceUID gFileSema = -1;
SceUID gProducerThread = -1;
SceUID gOutputThread = -1;
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
    __atomic_store_n(&gReadFrame, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gWriteFrame, 0u, __ATOMIC_RELEASE);
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
    __atomic_store_n(&gMixMasterGainQ16, kUnityGainQ16, __ATOMIC_RELEASE);
    for (PspSfxVoice &voice : gSfxVoices)
    {
        voice = {-1, 0, 0, false};
    }
}

__attribute__((noinline)) bool MixSfxBlock(i16 *block, u32 frames, bool haveBackground)
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
    mixJob.mixDivisor = 1;
    u32 totalInputGainQ16 = 0;
    if (haveBackground)
    {
        // Input zero preserves the BGM/fade block already assembled by the
        // SC. It is the only mutable source and therefore the only input
        // requiring a per-job cache writeback before ME reads it.
        Th07PspMixInput &background = mixJob.inputs[mixJob.inputCount++];
        background.samples = block;
        background.frames = frames;
        background.channels = 2;
        background.stepFixed = kUnityGainQ16;
        background.gainQ16 = kUnityGainQ16;
        background.needsWriteback = 1;
        background.sampleFormat = TH07_PSP_MIX_S16;
        totalInputGainQ16 = kUnityGainQ16;
    }

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
            totalInputGainQ16 += input.gainQ16;
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

    // TH06 avoids multi-channel saturation by dividing its complete mix by
    // max(8, playingChannels). TH07 previously left the divisor at one, so a
    // dense SFX burst was added to full-scale BGM and hard-clipped at s16.
    // Use the same headroom principle but weight it by the effects' actual
    // DirectSound gains. This preserves the BGM/SE balance, leaves solo BGM
    // bit-exact at unity, and guarantees that the worst-case absolute sum is
    // inside the signed-16-bit range. Reductions attack immediately; release
    // is deliberately gradual to avoid an 18 dB on/off pump.
    u32 targetMasterGainQ16 = kUnityGainQ16;
    if (mixed && totalInputGainQ16 != 0)
    {
        // apply_gain_q16 rounds each negative input down by at most one LSB.
        // Reserve one output LSB per descriptor so even equal-phase -FS
        // sources cannot cross -32768 after those independent shifts.
        const u32 roundingMargin =
            std::min<u32>(static_cast<u32>(mixJob.inputCount), kUnityGainQ16 - 1u);
        const unsigned long long numerator =
            static_cast<unsigned long long>(kUnityGainQ16 - roundingMargin) << 16;
        targetMasterGainQ16 = static_cast<u32>(
            std::min<unsigned long long>(kUnityGainQ16,
                                         numerator / totalInputGainQ16));
    }

    u32 masterGainQ16 =
        __atomic_load_n(&gMixMasterGainQ16, __ATOMIC_ACQUIRE);
    if (!mixed && !haveBackground)
    {
        // No audible source exists, so there is nothing across which to hear
        // a release ramp. Make the next isolated sound start at its own gain.
        masterGainQ16 = kUnityGainQ16;
    }
    else if (targetMasterGainQ16 < masterGainQ16)
    {
        masterGainQ16 = targetMasterGainQ16;
    }
    else if (masterGainQ16 < targetMasterGainQ16)
    {
        masterGainQ16 = std::min(targetMasterGainQ16,
                                 masterGainQ16 + kMixGainReleaseQ16);
    }
    __atomic_store_n(&gMixMasterGainQ16, masterGainQ16, __ATOMIC_RELEASE);

    if (masterGainQ16 != kUnityGainQ16)
    {
        for (u32 inputIndex = 0; inputIndex < mixJob.inputCount; ++inputIndex)
        {
            Th07PspMixInput &input = mixJob.inputs[inputIndex];
            input.gainQ16 = static_cast<u32>(
                static_cast<unsigned long long>(input.gainQ16) * masterGainQ16 >> 16);
        }
    }

    if (mixed || (haveBackground && masterGainQ16 != kUnityGainQ16))
    {
        // The API deliberately performs an identical SC mix if ME is absent,
        // disabled or times out.  Audio correctness is independent of MECC.
        th07_psp_me_audio_mix(&mixJob, block);
        // Only count blocks which actually contain an SFX voice. During the
        // limiter release, BGM-only blocks also pass through this mixer.
        if (mixed)
        {
            ++gSfxMixedBlocks;
        }
    }
    return mixed;
}

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
        if (__atomic_load_n(&gSystemSuspended, __ATOMIC_ACQUIRE))
        {
            sceKernelDelayThread(10000);
            continue;
        }
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
        for (u32 frame = 0; frame < framesRead; ++frame)
        {
            const u32 dst = ((write + frame) % kRingFrames) * kChannels;
            gBgmRing[dst] = gIoBuffer[frame * kChannels];
            gBgmRing[dst + 1] = gIoBuffer[frame * kChannels + 1];
        }
        __atomic_store_n(&gWriteFrame, (write + framesRead) % kRingFrames,
                         __ATOMIC_RELEASE);
        if (RingCount() >= kIoFrames * 2u)
        {
            sceKernelChangeThreadPriority(sceKernelGetThreadId(),
                                          kBgmIoBackgroundPriority);
        }
    }
    sceKernelExitThread(0);
    return 0;
}

int BgmOutputThread(SceSize, void *)
{
    alignas(64) i16 block[kFramesPerOutput * kChannels];
    u32 seenGeneration = __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE);
    bool primed = false;
    while (__atomic_load_n(&gAudioAlive, __ATOMIC_ACQUIRE))
    {
        if (__atomic_load_n(&gSystemSuspended, __ATOMIC_ACQUIRE))
        {
            primed = false;
            sceKernelDelayThread(10000);
            continue;
        }
        const u32 generation = __atomic_load_n(&gGeneration, __ATOMIC_ACQUIRE);
        if (generation != seenGeneration)
        {
            seenGeneration = generation;
            primed = false;
        }
        bool haveBgm = false;
        const bool wantsBgm = __atomic_load_n(&gBgmPlaying, __ATOMIC_ACQUIRE) &&
                              !__atomic_load_n(&gBgmPaused, __ATOMIC_ACQUIRE);
        if (wantsBgm)
        {
            const u32 available = RingCount();
            if ((!primed && available >= kPrefillFrames) ||
                (primed && available >= kFramesPerOutput))
            {
                primed = true;
                haveBgm = true;
                const u32 read = __atomic_load_n(&gReadFrame, __ATOMIC_RELAXED);
                for (u32 frame = 0; frame < kFramesPerOutput; ++frame)
                {
                    const u32 src = ((read + frame) % kRingFrames) * kChannels;
                    block[frame * kChannels] = gBgmRing[src];
                    block[frame * kChannels + 1] = gBgmRing[src + 1];
                }
                __atomic_store_n(&gReadFrame, (read + kFramesPerOutput) % kRingFrames,
                                 __ATOMIC_RELEASE);
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
            std::memset(block, 0, sizeof(block));
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

        const bool haveSfx = MixSfxBlock(block, kFramesPerOutput, haveBgm);
        if (!haveBgm && !haveSfx)
        {
            sceKernelDelayThread(1000);
            continue;
        }
        // Both channels use the same volume.  Keep this on the exact output
        // path proven by TH06 PSP; PPSSPP's panned-blocking path can return
        // with the worker's saved state corrupted after the first block.
        sceAudioOutputBlocking(gAudioChannel, PSP_AUDIO_VOLUME_MAX, block);
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
    gOutputThread = sceKernelCreateThread("th07_bgm_out", BgmOutputThread, 0x10, 0x4000,
                                           PSP_THREAD_ATTR_USER, nullptr);
    if (gProducerThread < 0 || gOutputThread < 0)
    {
        __atomic_store_n(&gAudioAlive, false, __ATOMIC_RELEASE);
        if (gProducerThread >= 0)
        {
            sceKernelDeleteThread(gProducerThread);
            gProducerThread = -1;
        }
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
        sceKernelDeleteThread(gOutputThread);
        gProducerThread = -1;
        gOutputThread = -1;
        sceAudioChRelease(gAudioChannel);
        gAudioChannel = -1;
        sceKernelDeleteSema(gFileSema);
        gFileSema = -1;
        return false;
    }
    if (sceKernelStartThread(gOutputThread, 0, nullptr) < 0)
    {
        __atomic_store_n(&gAudioAlive, false, __ATOMIC_RELEASE);
        sceKernelWaitThreadEnd(gProducerThread, nullptr);
        sceKernelDeleteThread(gProducerThread);
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

extern "C" void th07_psp_audio_set_system_suspended(int suspended)
{
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
    ResetRing();
    gGeneration = 1;
    gFadeFramesRemaining = 0;
    gFadeFramesTotal = 0;
    gUnderruns = 0;
    gBgmPlaying = false;
    gBgmPaused = false;
    gSystemSuspended = false;
    th07_psp_me_audio_init();
    if (!StartThreads())
    {
        th07_psp_me_audio_shutdown();
        th07_psp_boot_note("bgm audio init failed");
        return ZUN_ERROR;
    }
    th07_psp_boot_note("bgm audio ready");
    return ZUN_SUCCESS;
}

ZunResult SoundPlayer::Release()
{
    StopBGM();
    StopThreads();
    th07_psp_me_audio_shutdown();
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
    sceKernelDcacheWritebackRange(samples, storedFrames * sizeof(PspSfxSample));
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
    __atomic_add_fetch(&gGeneration, 1u, __ATOMIC_ACQ_REL);
    LockFile();
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
    gFadeFramesRemaining = 0;
    gFadeFramesTotal = 0;
    gBgmPaused = false;
    gBgmPlaying = true;
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
    __atomic_add_fetch(&gGeneration, 1u, __ATOMIC_ACQ_REL);
    ResetRing();
    gFadeFramesRemaining = 0;
    gFadeFramesTotal = 0;
    CloseTrackFile();
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
    gSePowerStarts = 0;
    gSePowerEnds = 0;
    gSePowerIgnored = 0;
    gSePowerActive = 0;
    std::memset(gSfxRequestCounts, 0, sizeof(gSfxRequestCounts));
    std::memset(gSfxCooldown, 0, sizeof(gSfxCooldown));

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
    gSfxCooldown[idx] = 0;
}

i32 SoundPlayer::ProcessQueues()
{
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
