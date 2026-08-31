#include "audio4m_sfx.h"

#if defined(TH07_PSP_MECC_AUDIO_4M)

#include <pspkernel.h>
#include <pspthreadman.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "audio_me.h"

extern "C" void th07_psp_audio4m_latch_fatal(const char *message);

namespace
{
constexpr unsigned int kAtlasBytes = 2u * 1024u * 1024u;
constexpr unsigned int kTransferBytes = 64u * 1024u;
constexpr unsigned int kCacheLineBytes = 64u;
constexpr unsigned int kBufferCount = 30u;
constexpr unsigned int kLogicalCount = 38u;
constexpr unsigned int kVoiceCount = 16u;
constexpr unsigned int kFramesPerBlock = 512u;
constexpr unsigned int kChannels = 2u;
constexpr unsigned int kBlockSamples = kFramesPerBlock * kChannels;
// Keep capacity and startup policy independent even though both are currently
// two blocks.  A deeper always-filled SFX FIFO would queue newly requested game
// sounds behind stale audio (and the observed 32 KiB heap leaves little room
// for it); the ME-owner priority ceiling fixes the measured long stall instead.
constexpr unsigned int kFifoBlocks = 2u;
constexpr unsigned int kInitialPrefillBlocks = 2u;
constexpr unsigned int kRequiredMask = (1u << kBufferCount) - 1u;
constexpr unsigned int kCoverageVoice = kVoiceCount - 1u;
constexpr unsigned int kCoverageGainQ16 = 16384u;

constexpr bool DeferInitialContinuousBlock(unsigned int expected_next,
                                           unsigned int continues,
                                           unsigned int available)
{
    return expected_next == 0u && continues != 0u &&
           available < kInitialPrefillBlocks;
}

static_assert(kAtlasBytes % kTransferBytes == 0,
              "SFX atlas must end on a complete transfer block");
static_assert(kBlockSamples * sizeof(int) == 4096u,
              "wide SFX transfer must remain exactly 4 KiB");
static_assert(sizeof(int) == 4u,
              "PSP wide SFX sample must be signed 32-bit");
static_assert(kVoiceCount * 32768u + 32768u <= INT32_MAX,
              "wide SFX plus BGM must fit signed 32-bit final mix");
static_assert(kFifoBlocks == 2u,
              "SFX FIFO latency and Main-RAM budget changed");
static_assert(kInitialPrefillBlocks == 2u,
              "initial continuous SFX latency must remain two blocks");
static_assert(kInitialPrefillBlocks <= kFifoBlocks,
              "startup prefill cannot exceed FIFO capacity");
static_assert(DeferInitialContinuousBlock(0u, 1u, 1u),
              "a continuous stream must prefill two FIFO slots");
static_assert(!DeferInitialContinuousBlock(0u, 1u, kInitialPrefillBlocks),
              "a two-block-prefetched continuous stream must start");
static_assert(!DeferInitialContinuousBlock(1u, 1u, 1u),
              "an armed stream must not hide a real underrun");
static_assert(!DeferInitialContinuousBlock(0u, 0u, 1u),
              "a terminal one-block stream must start immediately");

struct AtlasBuffer
{
    unsigned int canonical_offset;
    unsigned int replica_offset;
    unsigned int replica_frames;
    unsigned int frames;
    unsigned int step_fixed;
    bool loaded;
};

struct Voice
{
    int logical_index;
    unsigned int buffer_index;
    unsigned int position_frame;
    unsigned int position_fraction;
    unsigned int gain_q16;
    bool replica;
    bool coverage;
    bool active;
};

struct alignas(64) FifoBlock
{
    int samples[kBlockSamples];
    unsigned int token;
    unsigned int canonical_complete_mask;
    unsigned int replica_complete_mask;
    unsigned int continues;
};

AtlasBuffer gBuffers[kBufferCount];
Voice gVoices[kVoiceCount];
bool gReplicaNext[kBufferCount];
unsigned int gLogicalBuffer[kLogicalCount];
unsigned int gLogicalGainQ16[kLogicalCount];
unsigned char *gStaging;
unsigned int gStageBytes;
unsigned int gUploadedBytes;
unsigned int gAtlasLogicalBytes;
unsigned int gCanonicalBytes;
unsigned int gReplicaBytes;

alignas(64) FifoBlock gFifo[kFifoBlocks];
static_assert(sizeof(FifoBlock) == 4160u,
              "SFX FIFO block accounting changed");
static_assert(sizeof(gFifo) == 8320u,
              "two-block wide SFX FIFO must use exactly 8.125 KiB Main RAM");
volatile unsigned int gFifoRead;
volatile unsigned int gFifoWrite;
volatile unsigned int gPendingLow;
volatile unsigned int gPendingHigh;
volatile unsigned int gStopLow;
volatile unsigned int gStopHigh;
volatile unsigned int gAlive;
volatile unsigned int gReady;
volatile unsigned int gFatal;
volatile unsigned int gExpectedNext;
volatile unsigned int gConsumedToken;
volatile unsigned int gCanonicalOutputMask;
volatile unsigned int gReplicaOutputMask;
volatile unsigned int gMixJobs;
volatile unsigned int gOutputBlocks;
volatile unsigned int gFifoMisses;
volatile unsigned int gCoverageRequested;
volatile unsigned int gCoverageActive;
volatile unsigned int gCoverageComplete;
volatile unsigned int gCoveragePass;
volatile unsigned int gCoverageBuffer;
volatile unsigned int gCoverageSchedulingDone;
SceUID gFeederThread = -1;
bool gProducerPipelineActive;
unsigned int gCoverageNextPass;
unsigned int gCoverageNextBuffer;

void Fail(const char *message)
{
    if (__atomic_exchange_n(&gFatal, 1u, __ATOMIC_ACQ_REL) == 0u)
    {
        __atomic_store_n(&gReady, 0u, __ATOMIC_RELEASE);
        th07_psp_audio4m_latch_fatal(message);
    }
}

bool FlushStage()
{
    if (gStageBytes != kTransferBytes ||
        gUploadedBytes > kAtlasBytes - kTransferBytes)
    {
        return false;
    }
    if (!th07_psp_me_sfx_upload(gStaging, kTransferBytes, gUploadedBytes))
    {
        return false;
    }
    gUploadedBytes += kTransferBytes;
    gStageBytes = 0u;
    return true;
}

bool AppendAtlas(const unsigned char *source, unsigned int bytes)
{
    if (!source || bytes > kAtlasBytes - gAtlasLogicalBytes)
    {
        return false;
    }
    while (bytes != 0u)
    {
        const unsigned int take =
            std::min(bytes, kTransferBytes - gStageBytes);
        std::memcpy(gStaging + gStageBytes, source, take);
        gStageBytes += take;
        gAtlasLogicalBytes += take;
        source += take;
        bytes -= take;
        if (gStageBytes == kTransferBytes && !FlushStage())
        {
            return false;
        }
    }
    return true;
}

bool GatherLocalRange(unsigned int offset, unsigned int bytes,
                      unsigned char *scratch)
{
    while (bytes != 0u)
    {
        if (bytes >= kCacheLineBytes)
        {
            unsigned int transfer = std::min(bytes, kTransferBytes);
            transfer &= ~(kCacheLineBytes - 1u);
            if (!th07_psp_me_sfx_gather(scratch, transfer, offset, 0u, 0u) ||
                !AppendAtlas(scratch, transfer))
            {
                return false;
            }
            offset += transfer;
            bytes -= transfer;
            continue;
        }

        unsigned int window = offset;
        unsigned int copy_offset = 0u;
        if (window + kCacheLineBytes > gUploadedBytes)
        {
            if (gUploadedBytes < kCacheLineBytes || offset < gUploadedBytes - kCacheLineBytes)
            {
                return false;
            }
            window = gUploadedBytes - kCacheLineBytes;
            copy_offset = offset - window;
        }
        if (!th07_psp_me_sfx_gather(scratch, kCacheLineBytes, window, 0u, 0u) ||
            copy_offset + bytes > kCacheLineBytes ||
            !AppendAtlas(scratch + copy_offset, bytes))
        {
            return false;
        }
        bytes = 0u;
    }
    return true;
}

bool AppendCanonicalPrefix(unsigned int offset, unsigned int bytes,
                           unsigned int pending_base,
                           const unsigned char *pending_copy,
                           unsigned char *scratch)
{
    if ((offset | bytes) & 1u || bytes == 0u ||
        offset > gCanonicalBytes || bytes > gCanonicalBytes - offset)
    {
        return false;
    }
    while (bytes != 0u)
    {
        if (offset >= pending_base)
        {
            const unsigned int take = bytes;
            if (!AppendAtlas(pending_copy + (offset - pending_base), take))
            {
                return false;
            }
            return true;
        }
        const unsigned int take = std::min(bytes, pending_base - offset);
        if (!GatherLocalRange(offset, take, scratch))
        {
            return false;
        }
        offset += take;
        bytes -= take;
    }
    return true;
}

void ResetVoices()
{
    for (unsigned int index = 0; index < kVoiceCount; ++index)
    {
        gVoices[index] = {-1, 0u, 0u, 0u, 0u, false, false, false};
    }
}

void ApplyRequests()
{
    const unsigned int stop_low =
        __atomic_exchange_n(&gStopLow, 0u, __ATOMIC_ACQ_REL);
    const unsigned int stop_high =
        __atomic_exchange_n(&gStopHigh, 0u, __ATOMIC_ACQ_REL);
    for (Voice &voice : gVoices)
    {
        if (!voice.active || voice.logical_index < 0)
        {
            continue;
        }
        const unsigned int logical = static_cast<unsigned int>(voice.logical_index);
        const bool stop = logical < 32u
                              ? (stop_low & (1u << logical)) != 0u
                              : (stop_high & (1u << (logical - 32u))) != 0u;
        if (stop)
        {
            voice.active = false;
        }
    }

    const unsigned int pending_low =
        __atomic_exchange_n(&gPendingLow, 0u, __ATOMIC_ACQ_REL);
    const unsigned int pending_high =
        __atomic_exchange_n(&gPendingHigh, 0u, __ATOMIC_ACQ_REL);
    for (unsigned int logical = 0; logical < kLogicalCount; ++logical)
    {
        const bool pending = logical < 32u
                                 ? (pending_low & (1u << logical)) != 0u
                                 : (pending_high & (1u << (logical - 32u))) != 0u;
        if (!pending)
        {
            continue;
        }
        const unsigned int buffer_index = gLogicalBuffer[logical];
        if (buffer_index >= kBufferCount || !gBuffers[buffer_index].loaded)
        {
            Fail("MECC AUDIO4M SFX REQUEST BOUNDS -> COLD REBOOT");
            return;
        }

        Voice *selected = nullptr;
        for (unsigned int voice_index = 0; voice_index < kVoiceCount;
             ++voice_index)
        {
            if (__atomic_load_n(&gCoverageActive, __ATOMIC_ACQUIRE) != 0u &&
                voice_index == kCoverageVoice)
            {
                continue;
            }
            Voice &voice = gVoices[voice_index];
            if (voice.active && voice.logical_index == static_cast<int>(logical))
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
            unsigned int shortest = 0xffffffffu;
            for (unsigned int voice_index = 0; voice_index < kVoiceCount;
                 ++voice_index)
            {
                if (__atomic_load_n(&gCoverageActive, __ATOMIC_ACQUIRE) != 0u &&
                    voice_index == kCoverageVoice)
                {
                    continue;
                }
                Voice &voice = gVoices[voice_index];
                const unsigned int frames = gBuffers[voice.buffer_index].frames;
                const unsigned int remaining =
                    frames > voice.position_frame ? frames - voice.position_frame : 0u;
                if (remaining < shortest)
                {
                    shortest = remaining;
                    selected = &voice;
                }
            }
        }
        selected->logical_index = static_cast<int>(logical);
        selected->buffer_index = buffer_index;
        selected->position_frame = 0u;
        selected->position_fraction = 0u;
        selected->gain_q16 = gLogicalGainQ16[logical];
        selected->replica = gReplicaNext[buffer_index];
        selected->coverage = false;
        selected->active = true;
        gReplicaNext[buffer_index] = !gReplicaNext[buffer_index];
    }
}

void ScheduleCoverage()
{
    if (__atomic_load_n(&gCoverageActive, __ATOMIC_ACQUIRE) == 0u)
    {
        if (__atomic_exchange_n(&gCoverageRequested, 0u, __ATOMIC_ACQ_REL) == 0u)
        {
            return;
        }
        gCoverageNextPass = 0u;
        gCoverageNextBuffer = 0u;
        __atomic_store_n(&gCoverageSchedulingDone, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&gCoveragePass, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&gCoverageBuffer, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&gCoverageComplete, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&gCoverageActive, 1u, __ATOMIC_RELEASE);
    }

    if (__atomic_load_n(&gCoverageSchedulingDone, __ATOMIC_ACQUIRE) != 0u)
    {
        return;
    }

    Voice &voice = gVoices[kCoverageVoice];
    if (voice.active)
    {
        return;
    }
    if (gCoverageNextBuffer == kBufferCount)
    {
        ++gCoverageNextPass;
        gCoverageNextBuffer = 0u;
    }
    if (gCoverageNextPass == 2u)
    {
        __atomic_store_n(&gCoveragePass, 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&gCoverageBuffer, kBufferCount, __ATOMIC_RELAXED);
        __atomic_store_n(&gCoverageSchedulingDone, 1u, __ATOMIC_RELEASE);
        return;
    }

    const unsigned int buffer_index = gCoverageNextBuffer++;
    voice.logical_index = -1;
    voice.buffer_index = buffer_index;
    voice.position_frame = 0u;
    voice.position_fraction = 0u;
    voice.gain_q16 = kCoverageGainQ16;
    voice.replica = gCoverageNextPass != 0u;
    voice.coverage = true;
    voice.active = true;
    __atomic_store_n(&gCoveragePass, gCoverageNextPass, __ATOMIC_RELAXED);
    __atomic_store_n(&gCoverageBuffer, buffer_index, __ATOMIC_RELEASE);
}

int FeederThread(SceSize, void *)
{
    while (__atomic_load_n(&gAlive, __ATOMIC_ACQUIRE) != 0u)
    {
        if (__atomic_load_n(&gReady, __ATOMIC_ACQUIRE) == 0u ||
            __atomic_load_n(&gFatal, __ATOMIC_ACQUIRE) != 0u)
        {
            sceKernelDelayThread(1000);
            continue;
        }
        const unsigned int write =
            __atomic_load_n(&gFifoWrite, __ATOMIC_RELAXED);
        const unsigned int read =
            __atomic_load_n(&gFifoRead, __ATOMIC_ACQUIRE);
        if (write - read >= kFifoBlocks)
        {
            sceKernelDelayThread(250);
            continue;
        }

        ApplyRequests();
        if (__atomic_load_n(&gFatal, __ATOMIC_ACQUIRE) != 0u)
        {
            continue;
        }
        ScheduleCoverage();

        Th07PspMeSfxMixJob job{};
        job.frames = kFramesPerBlock;
        unsigned int voice_indices[kVoiceCount]{};
        unsigned int advances[kVoiceCount]{};
        for (unsigned int voice_index = 0; voice_index < kVoiceCount; ++voice_index)
        {
            Voice &voice = gVoices[voice_index];
            if (!voice.active)
            {
                continue;
            }
            const AtlasBuffer &buffer = gBuffers[voice.buffer_index];
            const unsigned long long source_fixed =
                (static_cast<unsigned long long>(voice.position_frame) << 16) |
                voice.position_fraction;
            const unsigned long long end_fixed =
                static_cast<unsigned long long>(buffer.frames) << 16;
            if (source_fixed >= end_fixed)
            {
                voice.active = false;
                continue;
            }
            const unsigned long long remaining_fixed = end_fixed - source_fixed;
            const unsigned int frames_until_end = static_cast<unsigned int>(
                (remaining_fixed + buffer.step_fixed - 1u) / buffer.step_fixed);
            const unsigned int advance =
                std::min(kFramesPerBlock, frames_until_end);

            const unsigned int job_index = job.voiceCount++;
            Th07PspMeSfxVoice &me_voice = job.voices[job_index];
            if (voice.replica)
            {
                me_voice.segment0Offset = buffer.replica_offset;
                me_voice.segment0Frames = buffer.replica_frames;
                const unsigned int suffix_frames = buffer.frames - buffer.replica_frames;
                me_voice.segment1Offset = suffix_frames != 0u
                                              ? buffer.canonical_offset +
                                                    buffer.replica_frames * sizeof(short)
                                              : 0u;
                me_voice.segment1Frames = suffix_frames;
            }
            else
            {
                me_voice.segment0Offset = buffer.canonical_offset;
                me_voice.segment0Frames = buffer.frames;
                me_voice.segment1Offset = 0u;
                me_voice.segment1Frames = 0u;
            }
            me_voice.sourceFrame = voice.position_frame;
            me_voice.sourceFraction = voice.position_fraction;
            me_voice.stepFixed = buffer.step_fixed;
            me_voice.gainQ16 = voice.gain_q16;
            voice_indices[job_index] = voice_index;
            advances[job_index] = advance;
        }
        if (job.voiceCount == 0u)
        {
            if (gProducerPipelineActive)
            {
                FifoBlock &slot = gFifo[write % kFifoBlocks];
                std::memset(slot.samples, 0, sizeof(slot.samples));
                slot.token = write + 1u;
                slot.canonical_complete_mask = 0u;
                slot.replica_complete_mask = 0u;
                slot.continues = 0u;
                __asm__ volatile("sync");
                __atomic_store_n(&gFifoWrite, write + 1u, __ATOMIC_RELEASE);
                gProducerPipelineActive = false;
                continue;
            }
            sceKernelDelayThread(500);
            continue;
        }

        FifoBlock &slot = gFifo[write % kFifoBlocks];
        if (!th07_psp_me_sfx_mix(&job, slot.samples))
        {
            Fail("MECC AUDIO4M SFX MIX FAILED -> COLD REBOOT");
            continue;
        }
        // A blocking ME job may return after another thread has latched a
        // fatal error or begun shutdown.  Do not publish that stale block or
        // advance voices/counters after the fail-closed boundary.
        if (__atomic_load_n(&gFatal, __ATOMIC_ACQUIRE) != 0u ||
            __atomic_load_n(&gReady, __ATOMIC_ACQUIRE) == 0u ||
            __atomic_load_n(&gAlive, __ATOMIC_ACQUIRE) == 0u)
        {
            continue;
        }

        unsigned int canonical_complete = 0u;
        unsigned int replica_complete = 0u;
        for (unsigned int job_index = 0; job_index < job.voiceCount; ++job_index)
        {
            Voice &voice = gVoices[voice_indices[job_index]];
            const AtlasBuffer &buffer = gBuffers[voice.buffer_index];
            const unsigned long long next =
                static_cast<unsigned long long>(voice.position_fraction) +
                static_cast<unsigned long long>(buffer.step_fixed) *
                    advances[job_index];
            voice.position_frame += static_cast<unsigned int>(next >> 16);
            voice.position_fraction = static_cast<unsigned int>(next) & 0xffffu;
            if (voice.position_frame >= buffer.frames)
            {
                const unsigned int bit = 1u << voice.buffer_index;
                if (voice.replica)
                {
                    replica_complete |= bit;
                }
                else
                {
                    canonical_complete |= bit;
                }
                voice.active = false;
            }
        }
        unsigned int continues = 0u;
        for (const Voice &voice : gVoices)
        {
            if (voice.active)
            {
                continues = 1u;
                break;
            }
        }
        slot.token = write + 1u;
        slot.canonical_complete_mask = canonical_complete;
        slot.replica_complete_mask = replica_complete;
        slot.continues = continues;
        gProducerPipelineActive = continues != 0u;
        __asm__ volatile("sync");
        __atomic_store_n(&gFifoWrite, write + 1u, __ATOMIC_RELEASE);
        __atomic_fetch_add(&gMixJobs, 1u, __ATOMIC_RELAXED);
    }
    sceKernelExitThread(0);
    return 0;
}
} // namespace

int th07_audio4m_sfx_begin(void *staging, unsigned int staging_bytes)
{
    unsigned int base = 0xffffffffu;
    unsigned int bytes = 0u;
    th07_psp_me_sfx_extent(&base, &bytes);
    if (!staging || (reinterpret_cast<unsigned int>(staging) & 63u) != 0u ||
        staging_bytes != kTransferBytes || base != 0u || bytes != kAtlasBytes)
    {
        return 0;
    }
    std::memset(gBuffers, 0, sizeof(gBuffers));
    std::memset(gReplicaNext, 0, sizeof(gReplicaNext));
    std::memset(gLogicalBuffer, 0xff, sizeof(gLogicalBuffer));
    std::memset(gLogicalGainQ16, 0, sizeof(gLogicalGainQ16));
    std::memset(gFifo, 0, sizeof(gFifo));
    ResetVoices();
    gStaging = static_cast<unsigned char *>(staging);
    gStageBytes = 0u;
    gUploadedBytes = 0u;
    gAtlasLogicalBytes = 0u;
    gCanonicalBytes = 0u;
    gReplicaBytes = 0u;
    gFifoRead = 0u;
    gFifoWrite = 0u;
    gPendingLow = 0u;
    gPendingHigh = 0u;
    gStopLow = 0u;
    gStopHigh = 0u;
    gAlive = 0u;
    gReady = 0u;
    gFatal = 0u;
    gExpectedNext = 0u;
    gConsumedToken = 0u;
    gCanonicalOutputMask = 0u;
    gReplicaOutputMask = 0u;
    gMixJobs = 0u;
    gOutputBlocks = 0u;
    gFifoMisses = 0u;
    gCoverageRequested = 0u;
    gCoverageActive = 0u;
    gCoverageComplete = 0u;
    gCoveragePass = 0u;
    gCoverageBuffer = 0u;
    gProducerPipelineActive = false;
    gCoverageSchedulingDone = 0u;
    gCoverageNextPass = 0u;
    gCoverageNextBuffer = 0u;
    gFeederThread = -1;
    return 1;
}

int th07_audio4m_sfx_upload_buffer(unsigned int buffer_index,
                                   const short *samples,
                                   unsigned int frames,
                                   unsigned int step_fixed)
{
    if (!gStaging || buffer_index >= kBufferCount ||
        gBuffers[buffer_index].loaded || !samples || frames == 0u ||
        step_fixed == 0u || frames > (kAtlasBytes - gAtlasLogicalBytes) / sizeof(short))
    {
        return 0;
    }
    // Loading must be strictly sequential; otherwise holes could be mistaken
    // for useful buffer bytes.
    for (unsigned int index = 0; index < buffer_index; ++index)
    {
        if (!gBuffers[index].loaded)
        {
            return 0;
        }
    }
    for (unsigned int index = buffer_index + 1u; index < kBufferCount; ++index)
    {
        if (gBuffers[index].loaded)
        {
            return 0;
        }
    }
    AtlasBuffer &buffer = gBuffers[buffer_index];
    buffer.canonical_offset = gAtlasLogicalBytes;
    buffer.frames = frames;
    buffer.step_fixed = step_fixed;
    if (!AppendAtlas(reinterpret_cast<const unsigned char *>(samples),
                     frames * sizeof(short)))
    {
        return 0;
    }
    buffer.loaded = true;
    return 1;
}

int th07_audio4m_sfx_finalize(void)
{
    if (!gStaging || gAtlasLogicalBytes == 0u || gAtlasLogicalBytes >= kAtlasBytes)
    {
        return 0;
    }
    for (const AtlasBuffer &buffer : gBuffers)
    {
        if (!buffer.loaded || buffer.frames == 0u)
        {
            return 0;
        }
    }
    gCanonicalBytes = gAtlasLogicalBytes;
    gReplicaBytes = kAtlasBytes - gCanonicalBytes;
    if (gReplicaBytes < kBufferCount * sizeof(short) ||
        gReplicaBytes > gCanonicalBytes)
    {
        return 0;
    }

    unsigned int prefix_bytes[kBufferCount];
    unsigned int remaining = gReplicaBytes - kBufferCount * sizeof(short);
    unsigned int capacity = gCanonicalBytes - kBufferCount * sizeof(short);
    for (unsigned int index = 0; index < kBufferCount; ++index)
    {
        const unsigned int buffer_bytes = gBuffers[index].frames * sizeof(short);
        const unsigned int extra_capacity = buffer_bytes - sizeof(short);
        unsigned int extra = capacity != 0u
                                 ? static_cast<unsigned int>(
                                       (static_cast<unsigned long long>(remaining) *
                                        extra_capacity) /
                                       capacity)
                                 : 0u;
        extra &= ~1u;
        extra = std::min(extra, extra_capacity);
        prefix_bytes[index] = sizeof(short) + extra;
    }
    unsigned int assigned = 0u;
    for (unsigned int bytes : prefix_bytes)
    {
        assigned += bytes;
    }
    unsigned int left = gReplicaBytes - assigned;
    for (unsigned int index = 0; left != 0u && index < kBufferCount; ++index)
    {
        const unsigned int buffer_bytes = gBuffers[index].frames * sizeof(short);
        const unsigned int add = std::min(left, buffer_bytes - prefix_bytes[index]);
        const unsigned int even_add = add & ~1u;
        prefix_bytes[index] += even_add;
        left -= even_add;
    }
    if (left != 0u)
    {
        return 0;
    }

    const unsigned int pending_base = gUploadedBytes;
    const unsigned int pending_bytes = gStageBytes;
    unsigned char *pending_copy =
        static_cast<unsigned char *>(memalign(kCacheLineBytes, kTransferBytes));
    unsigned char *scratch =
        static_cast<unsigned char *>(memalign(kCacheLineBytes, kTransferBytes));
    if (!pending_copy || !scratch)
    {
        std::free(pending_copy);
        std::free(scratch);
        return 0;
    }
    std::memcpy(pending_copy, gStaging, pending_bytes);

    bool ok = true;
    for (unsigned int index = 0; index < kBufferCount && ok; ++index)
    {
        AtlasBuffer &buffer = gBuffers[index];
        buffer.replica_offset = gAtlasLogicalBytes;
        buffer.replica_frames = prefix_bytes[index] / sizeof(short);
        ok = AppendCanonicalPrefix(buffer.canonical_offset, prefix_bytes[index],
                                   pending_base, pending_copy, scratch);
    }
    std::free(pending_copy);
    std::free(scratch);
    if (!ok || gAtlasLogicalBytes != kAtlasBytes || gUploadedBytes != kAtlasBytes ||
        gStageBytes != 0u)
    {
        return 0;
    }

    __atomic_store_n(&gAlive, 1u, __ATOMIC_RELEASE);
    gFeederThread = sceKernelCreateThread("th07_sfx_mecc_feed", FeederThread,
                                          0x11, 0x4000,
                                          PSP_THREAD_ATTR_USER, nullptr);
    if (gFeederThread < 0 ||
        sceKernelStartThread(gFeederThread, 0, nullptr) < 0)
    {
        __atomic_store_n(&gAlive, 0u, __ATOMIC_RELEASE);
        if (gFeederThread >= 0)
        {
            sceKernelDeleteThread(gFeederThread);
            gFeederThread = -1;
        }
        return 0;
    }
    __atomic_store_n(&gReady, 1u, __ATOMIC_RELEASE);
    return 1;
}

int th07_audio4m_sfx_start_coverage(void)
{
    if (__atomic_load_n(&gReady, __ATOMIC_ACQUIRE) == 0u ||
        __atomic_load_n(&gFatal, __ATOMIC_ACQUIRE) != 0u ||
        __atomic_load_n(&gCoverageActive, __ATOMIC_ACQUIRE) != 0u ||
        __atomic_load_n(&gCoverageComplete, __ATOMIC_ACQUIRE) != 0u)
    {
        return 0;
    }
    __atomic_store_n(&gCoverageRequested, 1u, __ATOMIC_RELEASE);
    return 1;
}

void th07_audio4m_sfx_request(unsigned int logical_index,
                              unsigned int buffer_index,
                              unsigned int gain_q16)
{
    if (logical_index >= kLogicalCount || buffer_index >= kBufferCount ||
        gain_q16 > 65536u || __atomic_load_n(&gReady, __ATOMIC_ACQUIRE) == 0u)
    {
        return;
    }
    gLogicalBuffer[logical_index] = buffer_index;
    gLogicalGainQ16[logical_index] = gain_q16;
    __asm__ volatile("sync");
    if (logical_index < 32u)
    {
        __atomic_fetch_or(&gPendingLow, 1u << logical_index, __ATOMIC_RELEASE);
    }
    else
    {
        __atomic_fetch_or(&gPendingHigh, 1u << (logical_index - 32u),
                          __ATOMIC_RELEASE);
    }
}

void th07_audio4m_sfx_stop_logical(unsigned int logical_index)
{
    if (logical_index >= kLogicalCount)
    {
        return;
    }
    if (logical_index < 32u)
    {
        const unsigned int bit = 1u << logical_index;
        __atomic_fetch_and(&gPendingLow, ~bit, __ATOMIC_RELEASE);
        __atomic_fetch_or(&gStopLow, bit, __ATOMIC_RELEASE);
    }
    else
    {
        const unsigned int bit = 1u << (logical_index - 32u);
        __atomic_fetch_and(&gPendingHigh, ~bit, __ATOMIC_RELEASE);
        __atomic_fetch_or(&gStopHigh, bit, __ATOMIC_RELEASE);
    }
}

unsigned int th07_audio4m_sfx_consume(short *io, unsigned int frames,
                                      unsigned int *limited_samples)
{
    if (limited_samples)
    {
        *limited_samples = 0u;
    }
    if (!io || frames != kFramesPerBlock ||
        __atomic_load_n(&gFatal, __ATOMIC_ACQUIRE) != 0u)
    {
        return 0u;
    }
    if (__atomic_load_n(&gConsumedToken, __ATOMIC_ACQUIRE) != 0u)
    {
        Fail("MECC AUDIO4M SFX OUTPUT TOKEN LEAK -> COLD REBOOT");
        return 0u;
    }
    const unsigned int read = __atomic_load_n(&gFifoRead, __ATOMIC_RELAXED);
    const unsigned int write = __atomic_load_n(&gFifoWrite, __ATOMIC_ACQUIRE);
    if (read == write)
    {
        if (__atomic_exchange_n(&gExpectedNext, 0u, __ATOMIC_ACQ_REL) != 0u)
        {
            __atomic_fetch_add(&gFifoMisses, 1u, __ATOMIC_RELAXED);
            Fail("MECC AUDIO4M SFX FIFO MISS -> COLD REBOOT");
        }
        return 0u;
    }
    const FifoBlock &slot = gFifo[read % kFifoBlocks];
    // A newly reserved PSP audio channel accepts its first blocking output
    // immediately.  If a continuous SFX stream starts with only one mixed
    // block published, the output worker can ask for the next block while the
    // ME is still producing it and falsely latch a FIFO miss.  Hold the first
    // continuous block until the independent two-block startup prefill is
    // ready.  Once playback has started, a missing promised continuation is
    // still a true underrun and remains fatal below.
    const unsigned int available = write - read;
    if (DeferInitialContinuousBlock(
            __atomic_load_n(&gExpectedNext, __ATOMIC_ACQUIRE),
            slot.continues, available))
    {
        return 0u;
    }
    unsigned int limited = 0u;
    for (unsigned int sample = 0; sample < kBlockSamples; ++sample)
    {
        const int sum = static_cast<int>(io[sample]) + slot.samples[sample];
        if (sum > 32767)
        {
            io[sample] = 32767;
            ++limited;
        }
        else if (sum < -32768)
        {
            io[sample] = -32768;
            ++limited;
        }
        else
        {
            io[sample] = static_cast<short>(sum);
        }
    }
    if (limited_samples)
    {
        *limited_samples = limited;
    }
    __atomic_store_n(&gExpectedNext, slot.continues, __ATOMIC_RELEASE);
    __atomic_store_n(&gConsumedToken, slot.token, __ATOMIC_RELEASE);
    return slot.token;
}

void th07_audio4m_sfx_output_committed(unsigned int token, int submitted)
{
    const unsigned int consumed =
        __atomic_load_n(&gConsumedToken, __ATOMIC_ACQUIRE);
    const unsigned int read = __atomic_load_n(&gFifoRead, __ATOMIC_RELAXED);
    if (token == 0u || consumed != token)
    {
        Fail("MECC AUDIO4M SFX OUTPUT COMMIT MISMATCH -> COLD REBOOT");
        return;
    }
    const FifoBlock &slot = gFifo[read % kFifoBlocks];
    if (slot.token != token)
    {
        Fail("MECC AUDIO4M SFX OUTPUT SLOT MISMATCH -> COLD REBOOT");
        return;
    }
    if (!submitted)
    {
        Fail("MECC AUDIO4M DAC SUBMIT FAILED -> COLD REBOOT");
    }
    else
    {
        const unsigned int canonical =
            __atomic_fetch_or(&gCanonicalOutputMask,
                              slot.canonical_complete_mask,
                              __ATOMIC_RELAXED) |
            slot.canonical_complete_mask;
        const unsigned int replica =
            __atomic_fetch_or(&gReplicaOutputMask,
                              slot.replica_complete_mask,
                              __ATOMIC_RELAXED) |
            slot.replica_complete_mask;
        __atomic_fetch_add(&gOutputBlocks, 1u, __ATOMIC_RELAXED);
        if (__atomic_load_n(&gCoverageSchedulingDone, __ATOMIC_ACQUIRE) != 0u &&
            (canonical & kRequiredMask) == kRequiredMask &&
            (replica & kRequiredMask) == kRequiredMask)
        {
            __atomic_store_n(&gCoverageComplete, 1u, __ATOMIC_RELAXED);
            __atomic_store_n(&gCoverageActive, 0u, __ATOMIC_RELEASE);
        }
    }
    __atomic_store_n(&gConsumedToken, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gFifoRead, read + 1u, __ATOMIC_RELEASE);
}

void th07_audio4m_sfx_shutdown(void)
{
    __atomic_store_n(&gReady, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gCoverageActive, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gAlive, 0u, __ATOMIC_RELEASE);
    if (gFeederThread >= 0)
    {
        sceKernelWaitThreadEnd(gFeederThread, nullptr);
        sceKernelDeleteThread(gFeederThread);
        gFeederThread = -1;
    }
}

int th07_audio4m_sfx_faulted(void)
{
    return __atomic_load_n(&gFatal, __ATOMIC_ACQUIRE) != 0u;
}

void th07_audio4m_sfx_snapshot(Th07Audio4mSfxSnapshot *snapshot)
{
    if (!snapshot)
    {
        return;
    }
    snapshot->atlas_bytes = gAtlasLogicalBytes;
    snapshot->canonical_bytes = gCanonicalBytes;
    snapshot->replica_bytes = gReplicaBytes;
    snapshot->canonical_output_mask =
        __atomic_load_n(&gCanonicalOutputMask, __ATOMIC_ACQUIRE) & kRequiredMask;
    snapshot->replica_output_mask =
        __atomic_load_n(&gReplicaOutputMask, __ATOMIC_ACQUIRE) & kRequiredMask;
    snapshot->mix_jobs = __atomic_load_n(&gMixJobs, __ATOMIC_ACQUIRE);
    snapshot->output_blocks = __atomic_load_n(&gOutputBlocks, __ATOMIC_ACQUIRE);
    snapshot->fifo_misses = __atomic_load_n(&gFifoMisses, __ATOMIC_ACQUIRE);
    snapshot->fatal = __atomic_load_n(&gFatal, __ATOMIC_ACQUIRE);
    unsigned int coverage_active;
    unsigned int coverage_active_after;
    unsigned int coverage_complete;
    do
    {
        coverage_active =
            __atomic_load_n(&gCoverageActive, __ATOMIC_ACQUIRE);
        coverage_complete =
            __atomic_load_n(&gCoverageComplete, __ATOMIC_ACQUIRE);
        coverage_active_after =
            __atomic_load_n(&gCoverageActive, __ATOMIC_ACQUIRE);
    } while (coverage_active != coverage_active_after ||
             (coverage_active != 0u && coverage_complete != 0u));
    snapshot->coverage_active = coverage_active;
    snapshot->coverage_complete = coverage_complete;
    snapshot->coverage_pass =
        __atomic_load_n(&gCoveragePass, __ATOMIC_ACQUIRE);
    snapshot->coverage_buffer =
        __atomic_load_n(&gCoverageBuffer, __ATOMIC_ACQUIRE);
}

#endif
