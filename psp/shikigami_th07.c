#include "shikigami_th07.h"

#include <arpa/inet.h>
#include <kubridge.h>
#include <pspge.h>
#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <psputility.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#if defined(TH07_PSP_PERF_DIAG)
#include "perf_log_telemetry.h"
#endif

#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
#include "ge4_game_bridge.hpp"
#include "ge_portrait_telemetry.h"
#endif

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
#include "audio_me.h"
#endif

#ifndef TH07_SHIKIGAMI_HOST_IPV4
#define TH07_SHIKIGAMI_HOST_IPV4 ""
#endif

#ifndef TH07_SHIKIGAMI_PORT
#define TH07_SHIKIGAMI_PORT 9996
#endif

#ifndef TH07_SHIKIGAMI_BUILD_ID
#define TH07_SHIKIGAMI_BUILD_ID 0x20260827u
#endif

#define TH07_SHIKIGAMI_PROFILE 1

extern void th07_psp_boot_note(const char *message);
extern void th07_psp_boot_notef(const char *format, ...);

enum
{
    SHIKIGAMI_PROTOCOL_VERSION = 1,
    SHIKIGAMI_PACKET_HELLO = 1,
    SHIKIGAMI_PACKET_HEARTBEAT = 2,
    SHIKIGAMI_PACKET_SHUTDOWN = 3,
    SHIKIGAMI_PACKET_TH07_STATUS = 8,
    SHIKIGAMI_PACKET_TH07_EVENT = 9,
    SHIKIGAMI_PACKET_TH07_PORTRAIT_CACHE = 10,
#if defined(TH07_PSP_PERF_DIAG)
    /* Independent of the frozen STATUS and portrait schemas. */
    SHIKIGAMI_PACKET_TH07_PERF_LOG = 11,
#endif
    /* Independent startup decision; never changes frozen STATUS v1/v2. */
    SHIKIGAMI_PACKET_TH07_ITEM_ME = 12,
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    /* A1-MOVE has an independent fail-down gate from Item geometry. */
    SHIKIGAMI_PACKET_TH07_A1_MOVE = 13,
#endif

    SHIKIGAMI_HEADER_BYTES = 24,
    SHIKIGAMI_IDENTITY_BYTES = 8,
    SHIKIGAMI_IDENTITY_PACKET_BYTES = 32,
#if defined(TH07_PSP_MECC_AUDIO_4M)
    SHIKIGAMI_STATUS_SCHEMA = 2,
    SHIKIGAMI_STATUS_PAYLOAD_BYTES = 204,
#else
    SHIKIGAMI_STATUS_SCHEMA = 1,
    SHIKIGAMI_STATUS_PAYLOAD_BYTES = 136,
#endif
    SHIKIGAMI_STATUS_PACKET_BYTES =
        SHIKIGAMI_HEADER_BYTES + SHIKIGAMI_STATUS_PAYLOAD_BYTES,
    SHIKIGAMI_EVENT_PAYLOAD_BYTES = 48,
    SHIKIGAMI_EVENT_PACKET_BYTES =
        SHIKIGAMI_HEADER_BYTES + SHIKIGAMI_EVENT_PAYLOAD_BYTES,
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    /* identity(8) + schema/valid(4) + nine diagnostic words(36). */
    SHIKIGAMI_ITEM_ME_PAYLOAD_BYTES = 48,
    SHIKIGAMI_ITEM_ME_PACKET_BYTES =
        SHIKIGAMI_HEADER_BYTES + SHIKIGAMI_ITEM_ME_PAYLOAD_BYTES,
    SHIKIGAMI_ITEM_ME_VALID_DECISION = 1u << 0,
    SHIKIGAMI_ITEM_ME_VALID_FAILURE_DETAIL = 1u << 1,
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    /* identity(8) + schema/valid(4) + ten diagnostic words(40). */
    SHIKIGAMI_A1_MOVE_PAYLOAD_BYTES = 52,
    SHIKIGAMI_A1_MOVE_PACKET_BYTES =
        SHIKIGAMI_HEADER_BYTES + SHIKIGAMI_A1_MOVE_PAYLOAD_BYTES,
    SHIKIGAMI_A1_MOVE_VALID_DECISION = 1u << 0,
    SHIKIGAMI_A1_MOVE_VALID_FAILURE_DETAIL = 1u << 1,
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    /* identity + schema/valid + bridge(3 words) + cache header(14 words) +
     * six complete slot records(11 words each). */
    SHIKIGAMI_PORTRAIT_CACHE_PAYLOAD_BYTES = 344,
    SHIKIGAMI_PORTRAIT_CACHE_PACKET_BYTES =
        SHIKIGAMI_HEADER_BYTES + SHIKIGAMI_PORTRAIT_CACHE_PAYLOAD_BYTES,
#endif

#if defined(TH07_PSP_PERF_DIAG)
    SHIKIGAMI_PERF_LOG_SCHEMA = 1,
    SHIKIGAMI_PERF_LOG_CHUNK_BYTES = 960,
    /* identity(8) + schema/header(44) + variable bytes. */
    SHIKIGAMI_PERF_LOG_FIXED_PAYLOAD_BYTES = 52,
    SHIKIGAMI_PERF_LOG_PACKET_BYTES =
        SHIKIGAMI_HEADER_BYTES + SHIKIGAMI_PERF_LOG_FIXED_PAYLOAD_BYTES +
        SHIKIGAMI_PERF_LOG_CHUNK_BYTES,
    SHIKIGAMI_PERF_LOG_CHUNKS_PER_TICK = 16,
    SHIKIGAMI_PERF_LOG_SEND_ROUNDS = 2,

    SHIKIGAMI_PERF_LOG_BEGIN = 1u << 0,
    SHIKIGAMI_PERF_LOG_END = 1u << 1,
    SHIKIGAMI_PERF_LOG_VALID = 1u << 2,
    SHIKIGAMI_PERF_LOG_RETRY = 1u << 3,
#endif

    SHIKIGAMI_FLAG_CONNECTED = 1u << 0,
    SHIKIGAMI_FLAG_NONBLOCKING = 1u << 1,

    SHIKIGAMI_VALID_MODEL_CAPACITY = 1u << 0,
    SHIKIGAMI_VALID_MAIN_HEAP_API = 1u << 1,
    SHIKIGAMI_VALID_GE_APERTURE_API = 1u << 2,
    SHIKIGAMI_VALID_GE_PRIOR_EVIDENCE = 1u << 3,
    SHIKIGAMI_VALID_FPS = 1u << 4,
    SHIKIGAMI_VALID_AUDIO_RING = 1u << 5,
    SHIKIGAMI_VALID_ME_PERF_WINDOW = 1u << 6,
    SHIKIGAMI_VALID_FRAME_PERF = 1u << 7,
    SHIKIGAMI_VALID_ME_NATIVE_ALLOCATOR = 1u << 8,
    SHIKIGAMI_VALID_ME_UPPER_OWNED = 1u << 9,
    SHIKIGAMI_VALID_AUDIO4M_USAGE = 1u << 10,
    SHIKIGAMI_VALID_GE_UPPER_OWNED = 1u << 11,

    SHIKIGAMI_STATUS_BGM_PLAYING = 1u << 0,
    SHIKIGAMI_STATUS_BGM_PAUSED = 1u << 1,
    SHIKIGAMI_STATUS_DEMO = 1u << 2,
    SHIKIGAMI_STATUS_REPLAY = 1u << 3,
    SHIKIGAMI_STATUS_GAME_PAUSED = 1u << 4,
    SHIKIGAMI_STATUS_BOSS = 1u << 5,
    SHIKIGAMI_STATUS_SPELL = 1u << 6,
    SHIKIGAMI_STATUS_DIALOGUE = 1u << 7,
    SHIKIGAMI_STATUS_FATAL_SEEN = 1u << 8,
    SHIKIGAMI_STATUS_MODEL3 = 1u << 9,
    SHIKIGAMI_STATUS_TELEMETRY_ONLINE = 1u << 10,
    SHIKIGAMI_STATUS_GE_PRIOR_NOT_RUNTIME_OWNER = 1u << 11,
    SHIKIGAMI_STATUS_AUDIO4M_PROVEN = 1u << 12,
    SHIKIGAMI_STATUS_GE_UPPER_PORTRAIT = 1u << 13,
    SHIKIGAMI_STATUS_SFX_MAIN_RAM = 1u << 14,
    SHIKIGAMI_STATUS_BGM_MAIN_RAM = 1u << 15,

    SHIKIGAMI_EVENT_SUPERVISOR = 1,
    SHIKIGAMI_EVENT_STAGE = 2,
    SHIKIGAMI_EVENT_BGM = 3,
    SHIKIGAMI_EVENT_UNDERRUN = 4,
    SHIKIGAMI_EVENT_FATAL = 5,
    SHIKIGAMI_EVENT_GAME_FLAGS = 6,

    SHIKIGAMI_NOTICE_NONE = 0,
    SHIKIGAMI_NOTICE_ONLINE = 1,
    SHIKIGAMI_NOTICE_SETUP_FAILED = 2,
    SHIKIGAMI_NOTICE_SEND_FAILED = 3,

    SHIKIGAMI_STAGE_NONE = 0,
    SHIKIGAMI_STAGE_NET_INIT = 1,
    SHIKIGAMI_STAGE_INET_INIT = 2,
    SHIKIGAMI_STAGE_APCTL_INIT = 3,
    SHIKIGAMI_STAGE_WAIT_IP = 4,
    SHIKIGAMI_STAGE_DEST_IP = 5,
    SHIKIGAMI_STAGE_UDP_SOCKET = 6,
    SHIKIGAMI_STAGE_NONBLOCK = 7,

    PSP_1000_MAIN_BYTES = 32u * 1024u * 1024u,
    PSP_SLIM_MAIN_BYTES = 64u * 1024u * 1024u,
    PSP_1000_LOCAL_EDRAM_BYTES = 2u * 1024u * 1024u,
    PSP_SLIM_LOCAL_EDRAM_BYTES = 4u * 1024u * 1024u,
    GE_UPPER_PRIOR_BASE = 0x04200000u,
    GE_UPPER_PRIOR_BYTES = 2u * 1024u * 1024u,
};

typedef struct ObserverSnapshot
{
    uint32_t frame_number;
    uint32_t fps_x10;
    int32_t supervisor_state;
    int32_t stage;
    int32_t spell_index;
    uint32_t game_flags;
    Th07ShikigamiAudioSnapshot audio;
    uint32_t fatal_count;
    uint32_t fatal_hash;
} ObserverSnapshot;

typedef struct ObserverIdentity
{
    uint8_t model;
    uint8_t profile;
    uint16_t state;
    uint32_t local_ipv4;
} ObserverIdentity;

#if defined(TH07_PSP_PERF_DIAG)
typedef struct PerfLogTransfer
{
    Th07PspPerfLogSnapshot snapshot;
    uint32_t request_id;
    uint32_t served_request_id;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint16_t round;
    int active;
} PerfLogTransfer;
#endif

static const char g_target_ipv4[] = TH07_SHIKIGAMI_HOST_IPV4;
static volatile int g_running;
static SceUID g_worker_thread = -1;
#if defined(TH07_PSP_PERF_DIAG)
/* True only after the UDP observer has completed setup.  A compiled-in
 * observer with an empty destination must not suppress the Memory Stick
 * fallback for the RAM performance log. */
static volatile int g_perf_log_transport_ready;
#endif

/* One main-thread writer and one worker reader use this sequence counter. */
static volatile uint32_t g_snapshot_sequence;
static volatile uint32_t g_frame_number;
static volatile uint32_t g_fps_x10;
static volatile int32_t g_supervisor_state;
static volatile int32_t g_stage;
static volatile int32_t g_spell_index;
static volatile uint32_t g_game_flags;
static volatile uint32_t g_ring_bytes;
static volatile uint32_t g_ring_fill_frames;
static volatile uint32_t g_underruns;
static volatile uint32_t g_audio_generation;
static volatile int32_t g_bgm_index;
static volatile uint32_t g_bgm_playing;
static volatile uint32_t g_bgm_paused;
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
static volatile uint32_t g_me_upper_base;
static volatile uint32_t g_me_upper_bytes;
static volatile uint32_t g_me_jobs;
static volatile uint32_t g_me_fallbacks;
static volatile uint32_t g_me_timeouts;
static volatile uint32_t g_me_max_wait_us;
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M)
static volatile uint32_t g_sfx_atlas_bytes;
static volatile uint32_t g_sfx_canonical_bytes;
static volatile uint32_t g_sfx_replica_bytes;
static volatile uint32_t g_sfx_canonical_output_mask;
static volatile uint32_t g_sfx_replica_output_mask;
static volatile uint32_t g_sfx_mix_jobs;
static volatile uint32_t g_sfx_output_blocks;
static volatile uint32_t g_sfx_fifo_misses;
static volatile uint32_t g_sfx_fatal;
static volatile uint32_t g_sfx_coverage_active;
static volatile uint32_t g_sfx_coverage_complete;
static volatile uint32_t g_sfx_coverage_pass;
static volatile uint32_t g_sfx_coverage_buffer;
static volatile uint32_t g_bgm_upload_wraps;
static volatile uint32_t g_bgm_fetch_wraps;
static volatile uint32_t g_bgm_output_wraps;
static volatile uint32_t g_audio4m_proof_flags;
#endif
static volatile uint32_t g_fatal_count;
static volatile uint32_t g_fatal_hash;
static volatile uint32_t g_notice;
static volatile uint32_t g_notice_stage;
static volatile int32_t g_notice_result;
#if defined(TH07_PSP_PERF_DIAG)
static volatile uint32_t g_perf_log_request;
#endif

static int observer_running(void)
{
    return __atomic_load_n(&g_running, __ATOMIC_ACQUIRE);
}

static void put_be16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void put_be32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static uint32_t uptime_ms(void)
{
    return (uint32_t)(sceKernelGetSystemTimeWide() / 1000u);
}

static uint32_t hash_message(const char *message)
{
    uint32_t hash = 2166136261u;
    const unsigned char *cursor = (const unsigned char *)message;

    if (!cursor)
        return 0;
    while (*cursor)
    {
        hash ^= *cursor++;
        hash *= 16777619u;
    }
    return hash;
}

#if defined(TH07_PSP_PERF_DIAG)
static uint32_t perf_log_crc32(const uint8_t *data, uint32_t bytes)
{
    uint32_t crc = 0xffffffffu;
    uint32_t index;

    for (index = 0; index < bytes; ++index)
    {
        uint32_t bit;
        crc ^= data[index];
        for (bit = 0; bit < 8u; ++bit)
        {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}
#endif

static const char *stage_name(uint32_t stage)
{
    switch (stage)
    {
    case SHIKIGAMI_STAGE_NET_INIT:
        return "NET_INIT";
    case SHIKIGAMI_STAGE_INET_INIT:
        return "INET_INIT";
    case SHIKIGAMI_STAGE_APCTL_INIT:
        return "APCTL_INIT";
    case SHIKIGAMI_STAGE_WAIT_IP:
        return "WAIT_IP";
    case SHIKIGAMI_STAGE_DEST_IP:
        return "DEST_IP";
    case SHIKIGAMI_STAGE_UDP_SOCKET:
        return "UDP_SOCKET";
    case SHIKIGAMI_STAGE_NONBLOCK:
        return "NONBLOCK";
    default:
        return "UNKNOWN";
    }
}

static void publish_notice(uint32_t notice, uint32_t stage, int result)
{
    __atomic_store_n(&g_notice_stage, stage, __ATOMIC_RELAXED);
    __atomic_store_n(&g_notice_result, result, __ATOMIC_RELAXED);
    __atomic_store_n(&g_notice, notice, __ATOMIC_RELEASE);
}

/* Keep Memory Stick logging on the game's existing main-thread path. */
static void flush_notice(void)
{
    uint32_t notice = __atomic_exchange_n(&g_notice, SHIKIGAMI_NOTICE_NONE,
                                          __ATOMIC_ACQ_REL);
    uint32_t stage;
    int result;

    if (notice == SHIKIGAMI_NOTICE_NONE)
        return;
    stage = __atomic_load_n(&g_notice_stage, __ATOMIC_RELAXED);
    result = __atomic_load_n(&g_notice_result, __ATOMIC_RELAXED);
    if (notice == SHIKIGAMI_NOTICE_ONLINE)
        th07_psp_boot_note("SHIKIGAMI TH07 ONLINE");
    else if (notice == SHIKIGAMI_NOTICE_SETUP_FAILED)
        th07_psp_boot_notef("SHIKIGAMI TH07 OFF %s R%08X",
                            stage_name(stage), (unsigned int)result);
    else if (notice == SHIKIGAMI_NOTICE_SEND_FAILED)
        th07_psp_boot_notef("SHIKIGAMI TH07 SEND FAILED R%08X NONFATAL",
                            (unsigned int)result);
}

static void read_snapshot(ObserverSnapshot *snapshot)
{
    uint32_t before;
    uint32_t after;

    for (;;)
    {
        before = __atomic_load_n(&g_snapshot_sequence, __ATOMIC_ACQUIRE);
        if (before & 1u)
        {
            sceKernelDelayThread(0);
            continue;
        }
        snapshot->frame_number = __atomic_load_n(&g_frame_number, __ATOMIC_RELAXED);
        snapshot->fps_x10 = __atomic_load_n(&g_fps_x10, __ATOMIC_RELAXED);
        snapshot->supervisor_state =
            __atomic_load_n(&g_supervisor_state, __ATOMIC_RELAXED);
        snapshot->stage = __atomic_load_n(&g_stage, __ATOMIC_RELAXED);
        snapshot->spell_index = __atomic_load_n(&g_spell_index, __ATOMIC_RELAXED);
        snapshot->game_flags = __atomic_load_n(&g_game_flags, __ATOMIC_RELAXED);
        snapshot->audio.ring_bytes = __atomic_load_n(&g_ring_bytes, __ATOMIC_RELAXED);
        snapshot->audio.ring_fill_frames =
            __atomic_load_n(&g_ring_fill_frames, __ATOMIC_RELAXED);
        snapshot->audio.underruns = __atomic_load_n(&g_underruns, __ATOMIC_RELAXED);
        snapshot->audio.generation =
            __atomic_load_n(&g_audio_generation, __ATOMIC_RELAXED);
        snapshot->audio.bgm_index = __atomic_load_n(&g_bgm_index, __ATOMIC_RELAXED);
        snapshot->audio.playing = __atomic_load_n(&g_bgm_playing, __ATOMIC_RELAXED);
        snapshot->audio.paused = __atomic_load_n(&g_bgm_paused, __ATOMIC_RELAXED);
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
        snapshot->audio.me_upper_base =
            __atomic_load_n(&g_me_upper_base, __ATOMIC_RELAXED);
        snapshot->audio.me_upper_bytes =
            __atomic_load_n(&g_me_upper_bytes, __ATOMIC_RELAXED);
        snapshot->audio.me_jobs = __atomic_load_n(&g_me_jobs, __ATOMIC_RELAXED);
        snapshot->audio.me_fallbacks =
            __atomic_load_n(&g_me_fallbacks, __ATOMIC_RELAXED);
        snapshot->audio.me_timeouts =
            __atomic_load_n(&g_me_timeouts, __ATOMIC_RELAXED);
        snapshot->audio.me_max_wait_us =
            __atomic_load_n(&g_me_max_wait_us, __ATOMIC_RELAXED);
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M)
        snapshot->audio.sfx_atlas_bytes =
            __atomic_load_n(&g_sfx_atlas_bytes, __ATOMIC_RELAXED);
        snapshot->audio.sfx_canonical_bytes =
            __atomic_load_n(&g_sfx_canonical_bytes, __ATOMIC_RELAXED);
        snapshot->audio.sfx_replica_bytes =
            __atomic_load_n(&g_sfx_replica_bytes, __ATOMIC_RELAXED);
        snapshot->audio.sfx_canonical_output_mask =
            __atomic_load_n(&g_sfx_canonical_output_mask, __ATOMIC_RELAXED);
        snapshot->audio.sfx_replica_output_mask =
            __atomic_load_n(&g_sfx_replica_output_mask, __ATOMIC_RELAXED);
        snapshot->audio.sfx_mix_jobs =
            __atomic_load_n(&g_sfx_mix_jobs, __ATOMIC_RELAXED);
        snapshot->audio.sfx_output_blocks =
            __atomic_load_n(&g_sfx_output_blocks, __ATOMIC_RELAXED);
        snapshot->audio.sfx_fifo_misses =
            __atomic_load_n(&g_sfx_fifo_misses, __ATOMIC_RELAXED);
        snapshot->audio.sfx_fatal =
            __atomic_load_n(&g_sfx_fatal, __ATOMIC_RELAXED);
        snapshot->audio.sfx_coverage_active =
            __atomic_load_n(&g_sfx_coverage_active, __ATOMIC_RELAXED);
        snapshot->audio.sfx_coverage_complete =
            __atomic_load_n(&g_sfx_coverage_complete, __ATOMIC_RELAXED);
        snapshot->audio.sfx_coverage_pass =
            __atomic_load_n(&g_sfx_coverage_pass, __ATOMIC_RELAXED);
        snapshot->audio.sfx_coverage_buffer =
            __atomic_load_n(&g_sfx_coverage_buffer, __ATOMIC_RELAXED);
        snapshot->audio.bgm_upload_wraps =
            __atomic_load_n(&g_bgm_upload_wraps, __ATOMIC_RELAXED);
        snapshot->audio.bgm_fetch_wraps =
            __atomic_load_n(&g_bgm_fetch_wraps, __ATOMIC_RELAXED);
        snapshot->audio.bgm_output_wraps =
            __atomic_load_n(&g_bgm_output_wraps, __ATOMIC_RELAXED);
        snapshot->audio.audio4m_proof_flags =
            __atomic_load_n(&g_audio4m_proof_flags, __ATOMIC_RELAXED);
#endif
        after = __atomic_load_n(&g_snapshot_sequence, __ATOMIC_ACQUIRE);
        if (before == after && !(after & 1u))
            break;
    }

    snapshot->fatal_count = __atomic_load_n(&g_fatal_count, __ATOMIC_ACQUIRE);
    snapshot->fatal_hash = __atomic_load_n(&g_fatal_hash, __ATOMIC_ACQUIRE);
}

void th07_shikigami_publish_frame(const Th07ShikigamiFrameSnapshot *snapshot)
{
    if (!snapshot)
        return;

    __atomic_fetch_add(&g_snapshot_sequence, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&g_frame_number, snapshot->frame_number, __ATOMIC_RELAXED);
    __atomic_store_n(&g_fps_x10, snapshot->fps_x10, __ATOMIC_RELAXED);
    __atomic_store_n(&g_supervisor_state, snapshot->supervisor_state, __ATOMIC_RELAXED);
    __atomic_store_n(&g_stage, snapshot->stage, __ATOMIC_RELAXED);
    __atomic_store_n(&g_spell_index, snapshot->spell_index, __ATOMIC_RELAXED);
    __atomic_store_n(&g_game_flags, snapshot->game_flags, __ATOMIC_RELAXED);
    __atomic_store_n(&g_ring_bytes, snapshot->audio.ring_bytes, __ATOMIC_RELAXED);
    __atomic_store_n(&g_ring_fill_frames, snapshot->audio.ring_fill_frames,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_underruns, snapshot->audio.underruns, __ATOMIC_RELAXED);
    __atomic_store_n(&g_audio_generation, snapshot->audio.generation,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_bgm_index, snapshot->audio.bgm_index, __ATOMIC_RELAXED);
    __atomic_store_n(&g_bgm_playing, snapshot->audio.playing, __ATOMIC_RELAXED);
    __atomic_store_n(&g_bgm_paused, snapshot->audio.paused, __ATOMIC_RELAXED);
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    __atomic_store_n(&g_me_upper_base, snapshot->audio.me_upper_base,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_me_upper_bytes, snapshot->audio.me_upper_bytes,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_me_jobs, snapshot->audio.me_jobs, __ATOMIC_RELAXED);
    __atomic_store_n(&g_me_fallbacks, snapshot->audio.me_fallbacks,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_me_timeouts, snapshot->audio.me_timeouts,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_me_max_wait_us, snapshot->audio.me_max_wait_us,
                     __ATOMIC_RELAXED);
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M)
    __atomic_store_n(&g_sfx_atlas_bytes, snapshot->audio.sfx_atlas_bytes,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_canonical_bytes,
                     snapshot->audio.sfx_canonical_bytes, __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_replica_bytes, snapshot->audio.sfx_replica_bytes,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_canonical_output_mask,
                     snapshot->audio.sfx_canonical_output_mask,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_replica_output_mask,
                     snapshot->audio.sfx_replica_output_mask,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_mix_jobs, snapshot->audio.sfx_mix_jobs,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_output_blocks, snapshot->audio.sfx_output_blocks,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_fifo_misses, snapshot->audio.sfx_fifo_misses,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_fatal, snapshot->audio.sfx_fatal,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_coverage_active,
                     snapshot->audio.sfx_coverage_active, __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_coverage_complete,
                     snapshot->audio.sfx_coverage_complete, __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_coverage_pass,
                     snapshot->audio.sfx_coverage_pass, __ATOMIC_RELAXED);
    __atomic_store_n(&g_sfx_coverage_buffer,
                     snapshot->audio.sfx_coverage_buffer, __ATOMIC_RELAXED);
    __atomic_store_n(&g_bgm_upload_wraps, snapshot->audio.bgm_upload_wraps,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_bgm_fetch_wraps, snapshot->audio.bgm_fetch_wraps,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_bgm_output_wraps, snapshot->audio.bgm_output_wraps,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_audio4m_proof_flags,
                     snapshot->audio.audio4m_proof_flags, __ATOMIC_RELAXED);
#endif
    __atomic_fetch_add(&g_snapshot_sequence, 1u, __ATOMIC_RELEASE);
    flush_notice();
}

void th07_shikigami_record_fatal(const char *message)
{
    __atomic_store_n(&g_fatal_hash, hash_message(message), __ATOMIC_RELEASE);
    __atomic_fetch_add(&g_fatal_count, 1u, __ATOMIC_ACQ_REL);
}

#if defined(TH07_PSP_PERF_DIAG)
int th07_shikigami_perf_log_transport_ready(void)
{
    return __atomic_load_n(&g_perf_log_transport_ready, __ATOMIC_ACQUIRE);
}

void th07_shikigami_request_perf_log(void)
{
    uint32_t request =
        __atomic_add_fetch(&g_perf_log_request, 1u, __ATOMIC_RELEASE);
    if (request == 0u)
        __atomic_add_fetch(&g_perf_log_request, 1u, __ATOMIC_RELEASE);
}
#endif

static void fill_header(uint8_t *packet, uint16_t packet_type,
                        uint16_t payload_bytes, uint32_t *sequence)
{
    packet[0] = 'S';
    packet[1] = 'K';
    packet[2] = 'P';
    packet[3] = 'S';
    put_be16(packet + 4, SHIKIGAMI_PROTOCOL_VERSION);
    put_be16(packet + 6, packet_type);
    put_be32(packet + 8, (*sequence)++);
    put_be32(packet + 12, uptime_ms());
    put_be16(packet + 16, payload_bytes);
    put_be16(packet + 18,
             SHIKIGAMI_FLAG_CONNECTED | SHIKIGAMI_FLAG_NONBLOCKING);
    put_be32(packet + 20, TH07_SHIKIGAMI_BUILD_ID);
}

static void fill_identity(uint8_t *payload, const ObserverIdentity *identity)
{
    payload[0] = identity->model;
    payload[1] = identity->profile;
    put_be16(payload + 2, identity->state);
    put_be32(payload + 4, identity->local_ipv4);
}

static int send_identity_packet(int socket_id,
                                const struct sockaddr_in *destination,
                                uint16_t packet_type, uint32_t *sequence,
                                const ObserverIdentity *identity)
{
    uint8_t packet[SHIKIGAMI_IDENTITY_PACKET_BYTES];
    int result;

    memset(packet, 0, sizeof(packet));
    fill_header(packet, packet_type, SHIKIGAMI_IDENTITY_BYTES, sequence);
    fill_identity(packet + SHIKIGAMI_HEADER_BYTES, identity);
    result = (int)sceNetInetSendto(socket_id, packet, sizeof(packet), MSG_DONTWAIT,
                                   (const struct sockaddr *)destination,
                                   sizeof(*destination));
    return result == (int)sizeof(packet) ? 0 : result;
}

static uint32_t make_status_flags(const ObserverSnapshot *snapshot, uint8_t model,
                                  int ge_upper_owned)
{
    uint32_t flags = SHIKIGAMI_STATUS_TELEMETRY_ONLINE;
    if (ge_upper_owned)
        flags |= SHIKIGAMI_STATUS_GE_UPPER_PORTRAIT;
    else
        flags |= SHIKIGAMI_STATUS_GE_PRIOR_NOT_RUNTIME_OWNER;
    if (snapshot->audio.playing)
        flags |= SHIKIGAMI_STATUS_BGM_PLAYING;
    if (snapshot->audio.paused)
        flags |= SHIKIGAMI_STATUS_BGM_PAUSED;
    if (snapshot->game_flags & TH07_SHIKIGAMI_GAME_DEMO)
        flags |= SHIKIGAMI_STATUS_DEMO;
    if (snapshot->game_flags & TH07_SHIKIGAMI_GAME_REPLAY)
        flags |= SHIKIGAMI_STATUS_REPLAY;
    if (snapshot->game_flags & TH07_SHIKIGAMI_GAME_PAUSED)
        flags |= SHIKIGAMI_STATUS_GAME_PAUSED;
    if (snapshot->game_flags & TH07_SHIKIGAMI_GAME_BOSS)
        flags |= SHIKIGAMI_STATUS_BOSS;
    if (snapshot->game_flags & TH07_SHIKIGAMI_GAME_SPELL)
        flags |= SHIKIGAMI_STATUS_SPELL;
    if (snapshot->game_flags & TH07_SHIKIGAMI_GAME_DIALOGUE)
        flags |= SHIKIGAMI_STATUS_DIALOGUE;
    if (snapshot->fatal_count)
        flags |= SHIKIGAMI_STATUS_FATAL_SEEN;
    if (model == 3)
        flags |= SHIKIGAMI_STATUS_MODEL3;
#if defined(TH07_PSP_MECC_AUDIO_4M)
#if defined(TH07_PSP_SFX_MAIN_RAM)
    flags |= SHIKIGAMI_STATUS_SFX_MAIN_RAM;
#if defined(TH07_PSP_BGM_MAIN_RAM)
    flags |= SHIKIGAMI_STATUS_BGM_MAIN_RAM;
#endif
    if (snapshot->audio.audio4m_proof_flags ==
        TH07_SHIKIGAMI_AUDIO4M_PROOF_REQUIRED_SFX_MAIN_RAM)
        flags |= SHIKIGAMI_STATUS_AUDIO4M_PROVEN;
#else
    if (snapshot->audio.audio4m_proof_flags ==
        TH07_SHIKIGAMI_AUDIO4M_PROOF_REQUIRED)
        flags |= SHIKIGAMI_STATUS_AUDIO4M_PROVEN;
#endif
#endif
    return flags;
}

static int send_status_packet(int socket_id,
                              const struct sockaddr_in *destination,
                              uint32_t *sequence,
                              const ObserverIdentity *identity,
                              uint32_t ge_aperture_bytes,
                              const ObserverSnapshot *snapshot)
{
    uint8_t packet[SHIKIGAMI_STATUS_PACKET_BYTES];
    uint8_t *payload;
    uint16_t valid = SHIKIGAMI_VALID_MAIN_HEAP_API |
                     SHIKIGAMI_VALID_GE_APERTURE_API |
                     SHIKIGAMI_VALID_FPS |
                     SHIKIGAMI_VALID_AUDIO_RING;
    uint32_t main_physical = 0;
    uint32_t local_edram_physical = 0;
    uint32_t ge_prior_base = 0;
    uint32_t ge_prior_bytes = 0;
    int ge_upper_owned = 0;
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    uint32_t me_upper_base = 0;
    uint32_t me_upper_bytes = 0;
#endif
    int result;

    if (identity->model == 0)
    {
        main_physical = PSP_1000_MAIN_BYTES;
        local_edram_physical = PSP_1000_LOCAL_EDRAM_BYTES;
        valid |= SHIKIGAMI_VALID_MODEL_CAPACITY;
    }
    else if (identity->model != 0xff)
    {
        main_physical = PSP_SLIM_MAIN_BYTES;
        local_edram_physical = PSP_SLIM_LOCAL_EDRAM_BYTES;
        valid |= SHIKIGAMI_VALID_MODEL_CAPACITY;
    }
    if (identity->model == 3)
    {
        ge_prior_base = GE_UPPER_PRIOR_BASE;
        ge_prior_bytes = GE_UPPER_PRIOR_BYTES;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        // This function runs only on the 1 Hz observer worker. Read the
        // bridge's process-static latch here; never extend the frame snapshot.
        ge_upper_owned = th07_psp_ge4_active();
#endif
        if (ge_upper_owned)
            valid |= SHIKIGAMI_VALID_GE_UPPER_OWNED;
        else
            valid |= SHIKIGAMI_VALID_GE_PRIOR_EVIDENCE;
    }
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (snapshot->audio.me_upper_bytes != 0)
    {
        me_upper_base = snapshot->audio.me_upper_base;
        me_upper_bytes = snapshot->audio.me_upper_bytes;
        valid |= SHIKIGAMI_VALID_ME_UPPER_OWNED;
    }
    valid |= SHIKIGAMI_VALID_ME_PERF_WINDOW |
             SHIKIGAMI_VALID_AUDIO4M_USAGE;
#elif defined(TH07_PSP_MECC_BGM_384K)
    if (snapshot->audio.me_upper_base != 0 &&
        snapshot->audio.me_upper_bytes != 0)
    {
        me_upper_base = snapshot->audio.me_upper_base;
        me_upper_bytes = snapshot->audio.me_upper_bytes;
        valid |= SHIKIGAMI_VALID_ME_UPPER_OWNED;
    }
    valid |= SHIKIGAMI_VALID_ME_PERF_WINDOW;
#endif

    memset(packet, 0, sizeof(packet));
    fill_header(packet, SHIKIGAMI_PACKET_TH07_STATUS,
                SHIKIGAMI_STATUS_PAYLOAD_BYTES, sequence);
    payload = packet + SHIKIGAMI_HEADER_BYTES;
    fill_identity(payload, identity);
    put_be16(payload + 8, SHIKIGAMI_STATUS_SCHEMA);
    put_be16(payload + 10, valid);
    put_be32(payload + 12,
             make_status_flags(snapshot, identity->model, ge_upper_owned));
    put_be32(payload + 16, snapshot->frame_number);
    put_be32(payload + 20, (uint32_t)snapshot->supervisor_state);
    put_be32(payload + 24, (uint32_t)snapshot->stage);
    put_be32(payload + 28, snapshot->fps_x10);
    put_be32(payload + 32, main_physical);
    put_be32(payload + 36, (uint32_t)sceKernelTotalFreeMemSize());
    put_be32(payload + 40, (uint32_t)sceKernelMaxFreeMemSize());
    put_be32(payload + 44, local_edram_physical);
    put_be32(payload + 48, ge_aperture_bytes);
    put_be32(payload + 52, ge_prior_base);
    put_be32(payload + 56, ge_prior_bytes);
    put_be32(payload + 60, local_edram_physical);
    put_be32(payload + 64, 0); /* ME allocator-managed: not valid in this backend. */
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    put_be32(payload + 68, me_upper_base);
    put_be32(payload + 72, me_upper_bytes);
#else
    put_be32(payload + 68, 0); /* ME upper owned base: UNKNOWN. */
    put_be32(payload + 72, 0); /* ME upper owned bytes: UNKNOWN. */
#endif
    put_be32(payload + 76, snapshot->audio.ring_bytes);
    put_be32(payload + 80, snapshot->audio.ring_fill_frames);
    put_be32(payload + 84, snapshot->audio.underruns);
    put_be32(payload + 88, snapshot->audio.generation);
    put_be32(payload + 92, (uint32_t)snapshot->audio.bgm_index);
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    put_be32(payload + 96, snapshot->audio.me_jobs);
    put_be32(payload + 100, snapshot->audio.me_fallbacks);
    put_be32(payload + 104, snapshot->audio.me_timeouts);
    put_be32(payload + 108, snapshot->audio.me_max_wait_us);
#else
    put_be32(payload + 96, 0);  /* ME performance window is not active. */
    put_be32(payload + 100, 0);
    put_be32(payload + 104, 0);
    put_be32(payload + 108, 0);
#endif
    put_be32(payload + 112, (uint32_t)snapshot->spell_index);
    put_be32(payload + 116, snapshot->fatal_count);
    put_be32(payload + 120, snapshot->fatal_hash);
    put_be32(payload + 124, 0); /* CPU timing: valid flag deliberately clear. */
    put_be32(payload + 128, 0); /* GE wait: valid flag deliberately clear. */
    put_be32(payload + 132, 0); /* I/O wait: no existing measurement. */
#if defined(TH07_PSP_MECC_AUDIO_4M)
    put_be32(payload + 136, snapshot->audio.sfx_atlas_bytes);
    put_be32(payload + 140, snapshot->audio.sfx_canonical_bytes);
    put_be32(payload + 144, snapshot->audio.sfx_replica_bytes);
    put_be32(payload + 148, snapshot->audio.sfx_canonical_output_mask);
    put_be32(payload + 152, snapshot->audio.sfx_replica_output_mask);
    put_be32(payload + 156, snapshot->audio.sfx_mix_jobs);
    put_be32(payload + 160, snapshot->audio.sfx_output_blocks);
    put_be32(payload + 164, snapshot->audio.sfx_fifo_misses);
    put_be32(payload + 168, snapshot->audio.sfx_fatal);
    put_be32(payload + 172, snapshot->audio.sfx_coverage_active);
    put_be32(payload + 176, snapshot->audio.sfx_coverage_complete);
    put_be32(payload + 180, snapshot->audio.sfx_coverage_pass);
    put_be32(payload + 184, snapshot->audio.sfx_coverage_buffer);
    put_be32(payload + 188, snapshot->audio.bgm_upload_wraps);
    put_be32(payload + 192, snapshot->audio.bgm_fetch_wraps);
    put_be32(payload + 196, snapshot->audio.bgm_output_wraps);
    put_be32(payload + 200, snapshot->audio.audio4m_proof_flags);
#endif

    result = (int)sceNetInetSendto(socket_id, packet, sizeof(packet), MSG_DONTWAIT,
                                   (const struct sockaddr *)destination,
                                   sizeof(*destination));
    return result == (int)sizeof(packet) ? 0 : result;
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
static int send_item_me_packet(int socket_id,
                               const struct sockaddr_in *destination,
                               uint32_t *sequence,
                               const ObserverIdentity *identity)
{
    uint8_t packet[SHIKIGAMI_ITEM_ME_PACKET_BYTES];
    uint8_t *payload;
    Th07PspMeItemRenderDiag diag;
    uint16_t valid = SHIKIGAMI_ITEM_ME_VALID_DECISION;
    int result;

    memset(&diag, 0, sizeof(diag));
    th07_psp_me_item_render_diag_snapshot(&diag);
    if (diag.itemSelftestFailures != 0u)
        valid |= SHIKIGAMI_ITEM_ME_VALID_FAILURE_DETAIL;

    memset(packet, 0, sizeof(packet));
    fill_header(packet, SHIKIGAMI_PACKET_TH07_ITEM_ME,
                SHIKIGAMI_ITEM_ME_PAYLOAD_BYTES, sequence);
    payload = packet + SHIKIGAMI_HEADER_BYTES;
    fill_identity(payload, identity);
    put_be16(payload + 8, TH07_PSP_ME_ITEM_DIAG_SCHEMA);
    put_be16(payload + 10, valid);
    put_be32(payload + 12, diag.itemState);
    put_be32(payload + 16, diag.itemReason);
    put_be32(payload + 20, diag.itemSelftestRuns);
    put_be32(payload + 24, diag.itemSelftestFailures);
    put_be32(payload + 28, diag.bulletRetryRuns);
    put_be32(payload + 32, diag.bulletRetryPasses);
    put_be32(payload + 36, (uint32_t)diag.lastWaitResult);
    put_be32(payload + 40, diag.lastStreamResult);
    put_be32(payload + 44, diag.lastItemResult);

    result = (int)sceNetInetSendto(socket_id, packet, sizeof(packet),
                                   MSG_DONTWAIT,
                                   (const struct sockaddr *)destination,
                                   sizeof(*destination));
    return result == (int)sizeof(packet) ? 0 : result;
}
#endif

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
static int send_a1_move_packet(int socket_id,
                               const struct sockaddr_in *destination,
                               uint32_t *sequence,
                               const ObserverIdentity *identity)
{
    uint8_t packet[SHIKIGAMI_A1_MOVE_PACKET_BYTES];
    uint8_t *payload;
    Th07PspMeItemMotionDiag diag;
    uint16_t valid = SHIKIGAMI_A1_MOVE_VALID_DECISION;
    int result;

    memset(&diag, 0, sizeof(diag));
    th07_psp_me_item_motion_diag_snapshot(&diag);
    if (diag.selftestFailures != 0u ||
        diag.state == TH07_PSP_ME_ITEM_MOTION_STATE_SAFE_FALLBACK ||
        diag.state == TH07_PSP_ME_ITEM_MOTION_STATE_FAILED)
    {
        valid |= SHIKIGAMI_A1_MOVE_VALID_FAILURE_DETAIL;
    }

    memset(packet, 0, sizeof(packet));
    fill_header(packet, SHIKIGAMI_PACKET_TH07_A1_MOVE,
                SHIKIGAMI_A1_MOVE_PAYLOAD_BYTES, sequence);
    payload = packet + SHIKIGAMI_HEADER_BYTES;
    fill_identity(payload, identity);
    put_be16(payload + 8, TH07_PSP_ME_ITEM_MOTION_DIAG_SCHEMA);
    put_be16(payload + 10, valid);
    put_be32(payload + 12, diag.state);
    put_be32(payload + 16, diag.reason);
    put_be32(payload + 20, diag.selftestRuns);
    put_be32(payload + 24, diag.selftestFailures);
    put_be32(payload + 28, diag.bulletRetryRuns);
    put_be32(payload + 32, diag.bulletRetryPasses);
    put_be32(payload + 36, (uint32_t)diag.lastPollResult);
    put_be32(payload + 40, diag.lastBulletResult);
    put_be32(payload + 44, diag.lastItemResult);
    put_be32(payload + 48, diag.firstMismatchSlot);

    result = (int)sceNetInetSendto(socket_id, packet, sizeof(packet),
                                   MSG_DONTWAIT,
                                   (const struct sockaddr *)destination,
                                   sizeof(*destination));
    return result == (int)sizeof(packet) ? 0 : result;
}
#endif

#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
static void fill_portrait_slot(uint8_t *payload,
                               const Th07PspPortraitSlotSnapshot *slot)
{
    put_be32(payload + 0, slot->role);
    put_be32(payload + 4, slot->texture_slot);
    put_be32(payload + 8, slot->raw_address);
    put_be32(payload + 12, slot->allocation_bytes);
    put_be32(payload + 16, slot->width);
    put_be32(payload + 20, slot->height);
    put_be32(payload + 24, slot->psm);
    put_be32(payload + 28, slot->source_hash);
    put_be32(payload + 32, slot->readback_hash);
    put_be32(payload + 36, slot->upload_generation);
    put_be32(payload + 40, slot->draw_count);
}

static int send_portrait_cache_packet(
    int socket_id, const struct sockaddr_in *destination, uint32_t *sequence,
    const ObserverIdentity *identity)
{
    uint8_t packet[SHIKIGAMI_PORTRAIT_CACHE_PACKET_BYTES];
    uint8_t *payload;
    uint8_t *slot_payload;
    Th07PspPortraitCacheSnapshot cache;
    uint16_t valid = TH07_SHIKIGAMI_PORTRAIT_VALID_BRIDGE_STATE;
    int slot_index;
    int bridge_active;
    int power_lock_held;
    uint32_t live_aperture_bytes;
    int result;

    /* This function is called only by the observer's one-Hz block.  Capture
     * every proof field into one datagram; the receiver never joins samples. */
    bridge_active = th07_psp_ge4_active();
    power_lock_held = th07_psp_ge4_power_lock_held();
    live_aperture_bytes = (uint32_t)sceGeEdramGetSize();
    memset(&cache, 0, sizeof(cache));
    if (th07_psp_portrait_cache_snapshot(&cache) > 0)
        valid |= TH07_SHIKIGAMI_PORTRAIT_VALID_CACHE_SNAPSHOT;

    memset(packet, 0, sizeof(packet));
    fill_header(packet, SHIKIGAMI_PACKET_TH07_PORTRAIT_CACHE,
                SHIKIGAMI_PORTRAIT_CACHE_PAYLOAD_BYTES, sequence);
    payload = packet + SHIKIGAMI_HEADER_BYTES;
    fill_identity(payload, identity);
    put_be16(payload + 8, TH07_SHIKIGAMI_PORTRAIT_CACHE_SCHEMA);
    put_be16(payload + 10, valid);
    put_be32(payload + 12, bridge_active != 0);
    put_be32(payload + 16, power_lock_held != 0);
    put_be32(payload + 20, live_aperture_bytes);
    put_be32(payload + 24, cache.flags);
    put_be32(payload + 28, cache.cache_generation);
    put_be32(payload + 32, cache.stage);
    put_be32(payload + 36, cache.required_mask);
    put_be32(payload + 40, cache.owned_mask);
    put_be32(payload + 44, cache.verified_mask);
    put_be32(payload + 48, cache.sampled_mask);
    put_be32(payload + 52, cache.pool_raw_base);
    put_be32(payload + 56, cache.pool_bytes);
    put_be32(payload + 60, cache.live_bytes);
    put_be32(payload + 64, cache.fallback_count);
    put_be32(payload + 68, cache.migration_count);
    put_be32(payload + 72, cache.allocation_failure_count);
    put_be32(payload + 76, cache.invariant_failure_count);
    slot_payload = payload + 80;
    for (slot_index = 0; slot_index < TH07_PSP_PORTRAIT_SLOT_COUNT;
         ++slot_index)
    {
        fill_portrait_slot(slot_payload + (uint32_t)slot_index * 44u,
                           &cache.slots[slot_index]);
    }

    result = (int)sceNetInetSendto(socket_id, packet, sizeof(packet), MSG_DONTWAIT,
                                   (const struct sockaddr *)destination,
                                   sizeof(*destination));
    return result == (int)sizeof(packet) ? 0 : result;
}
#endif

#if defined(TH07_PSP_PERF_DIAG)
static int send_perf_log_chunk(
    int socket_id, const struct sockaddr_in *destination, uint32_t *sequence,
    const ObserverIdentity *identity, const PerfLogTransfer *transfer)
{
    uint8_t packet[SHIKIGAMI_PERF_LOG_PACKET_BYTES];
    uint8_t *payload = packet + SHIKIGAMI_HEADER_BYTES;
    uint8_t *chunk = payload + SHIKIGAMI_PERF_LOG_FIXED_PAYLOAD_BYTES;
    uint32_t offset =
        (uint32_t)transfer->chunk_index * SHIKIGAMI_PERF_LOG_CHUNK_BYTES;
    uint32_t remaining = transfer->snapshot.total_bytes > offset
                             ? transfer->snapshot.total_bytes - offset
                             : 0u;
    uint16_t data_bytes =
        (uint16_t)(remaining < SHIKIGAMI_PERF_LOG_CHUNK_BYTES
                       ? remaining
                       : SHIKIGAMI_PERF_LOG_CHUNK_BYTES);
    uint16_t flags = 0;
    uint16_t payload_bytes =
        (uint16_t)(SHIKIGAMI_PERF_LOG_FIXED_PAYLOAD_BYTES + data_bytes);
    uint32_t chunk_crc;
    uint32_t packet_bytes = SHIKIGAMI_HEADER_BYTES + payload_bytes;
    int result;

    memset(packet, 0, packet_bytes);
    if (data_bytes != 0u &&
        th07_psp_perf_log_snapshot_read(&transfer->snapshot, offset, chunk,
                                        data_bytes) != data_bytes)
    {
        /* Gameplay or stage loading resumed, or a fallback flush moved the
         * prefix.  Pause and retry from this exact chunk at the next safe
         * observer tick; never read around the main-thread writer. */
        return -1;
    }

    if (transfer->chunk_index == 0u)
        flags |= SHIKIGAMI_PERF_LOG_BEGIN;
    if ((uint16_t)(transfer->chunk_index + 1u) == transfer->chunk_count)
        flags |= SHIKIGAMI_PERF_LOG_END;
    if (transfer->snapshot.valid)
        flags |= SHIKIGAMI_PERF_LOG_VALID;
    if (transfer->round != 0u)
        flags |= SHIKIGAMI_PERF_LOG_RETRY;
    chunk_crc = perf_log_crc32(chunk, data_bytes);

    fill_header(packet, SHIKIGAMI_PACKET_TH07_PERF_LOG, payload_bytes,
                sequence);
    fill_identity(payload, identity);
    put_be16(payload + 8, SHIKIGAMI_PERF_LOG_SCHEMA);
    put_be16(payload + 10, flags);
    put_be32(payload + 12, transfer->snapshot.run_id);
    put_be32(payload + 16, transfer->request_id);
    put_be32(payload + 20, transfer->snapshot.window_id);
    put_be32(payload + 24, transfer->snapshot.total_bytes);
    put_be32(payload + 28, offset);
    put_be32(payload + 32, transfer->snapshot.log_crc32);
    put_be32(payload + 36, chunk_crc);
    put_be32(payload + 40, transfer->snapshot.dropped_lines);
    put_be16(payload + 44, transfer->chunk_index);
    put_be16(payload + 46, transfer->chunk_count);
    put_be16(payload + 48, data_bytes);
    put_be16(payload + 50, 0);

    result = (int)sceNetInetSendto(socket_id, packet, packet_bytes,
                                   MSG_DONTWAIT,
                                   (const struct sockaddr *)destination,
                                   sizeof(*destination));
    return result == (int)packet_bytes ? 0 : result;
}

static void service_perf_log_transfer(
    int socket_id, const struct sockaddr_in *destination, uint32_t *sequence,
    const ObserverIdentity *identity, PerfLogTransfer *transfer)
{
    uint32_t requested =
        __atomic_load_n(&g_perf_log_request, __ATOMIC_ACQUIRE);
    unsigned int sent;

    if (!transfer->active)
    {
        uint32_t chunks;
        if (requested == 0u || requested == transfer->served_request_id)
            return;
        if (!th07_psp_perf_log_snapshot_begin(&transfer->snapshot))
            return;
        chunks = (transfer->snapshot.total_bytes +
                  SHIKIGAMI_PERF_LOG_CHUNK_BYTES - 1u) /
                 SHIKIGAMI_PERF_LOG_CHUNK_BYTES;
        if (chunks == 0u)
            chunks = 1u;
        if (chunks > 0xffffu)
            return;
        transfer->request_id = requested;
        transfer->chunk_index = 0u;
        transfer->chunk_count = (uint16_t)chunks;
        transfer->round = 0u;
        transfer->active = 1;
    }

    for (sent = 0; sent < SHIKIGAMI_PERF_LOG_CHUNKS_PER_TICK; ++sent)
    {
        if (send_perf_log_chunk(socket_id, destination, sequence, identity,
                                transfer) != 0)
        {
            return;
        }
        ++transfer->chunk_index;
        if (transfer->chunk_index != transfer->chunk_count)
            continue;
        ++transfer->round;
        if (transfer->round < SHIKIGAMI_PERF_LOG_SEND_ROUNDS)
        {
            transfer->chunk_index = 0u;
            continue;
        }
        transfer->served_request_id = transfer->request_id;
        transfer->active = 0;
        return;
    }
}
#endif

static int send_event_packet(int socket_id,
                             const struct sockaddr_in *destination,
                             uint32_t *sequence,
                             const ObserverIdentity *identity,
                             uint16_t event_type, uint32_t event_count,
                             uint32_t old_value, uint32_t new_value,
                             const ObserverSnapshot *snapshot)
{
    uint8_t packet[SHIKIGAMI_EVENT_PACKET_BYTES];
    uint8_t *payload;
    int result;

    memset(packet, 0, sizeof(packet));
    fill_header(packet, SHIKIGAMI_PACKET_TH07_EVENT,
                SHIKIGAMI_EVENT_PAYLOAD_BYTES, sequence);
    payload = packet + SHIKIGAMI_HEADER_BYTES;
    fill_identity(payload, identity);
    put_be16(payload + 8, 1); /* TH07 event schema. */
    put_be16(payload + 10, event_type);
    put_be32(payload + 12, event_count);
    put_be32(payload + 16, old_value);
    put_be32(payload + 20, new_value);
    put_be32(payload + 24, snapshot->frame_number);
    put_be32(payload + 28, (uint32_t)snapshot->supervisor_state);
    put_be32(payload + 32, (uint32_t)snapshot->stage);
    put_be32(payload + 36, (uint32_t)snapshot->audio.bgm_index);
    put_be32(payload + 40, snapshot->audio.underruns);
    put_be32(payload + 44, snapshot->fatal_hash);

    result = (int)sceNetInetSendto(socket_id, packet, sizeof(packet), MSG_DONTWAIT,
                                   (const struct sockaddr *)destination,
                                   sizeof(*destination));
    return result == (int)sizeof(packet) ? 0 : result;
}

static int wait_for_profile_one(ObserverIdentity *identity)
{
    int retry;
    int result;
    int state = PSP_NET_APCTL_STATE_DISCONNECTED;

    result = sceUtilityCheckNetParam(TH07_SHIKIGAMI_PROFILE);
    if (result != 0)
        return result;
    result = sceNetApctlConnect(TH07_SHIKIGAMI_PROFILE);
    if (result < 0)
        return result;

    for (retry = 0; retry < 300 && observer_running(); ++retry)
    {
        result = sceNetApctlGetState(&state);
        if (result < 0)
            return result;
        if (state == PSP_NET_APCTL_STATE_GOT_IP)
        {
            union SceNetApctlInfo info;
            struct in_addr address;

            memset(&info, 0, sizeof(info));
            memset(&address, 0, sizeof(address));
            result = sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info);
            if (result < 0)
                return result;
            if (inet_aton(info.ip, &address) == 0)
                return -1;
            identity->profile = TH07_SHIKIGAMI_PROFILE;
            identity->state = (uint16_t)state;
            identity->local_ipv4 = ntohl(address.s_addr);
            return 0;
        }
        sceKernelDelayThread(100u * 1000u);
    }
    return -1;
}

static void send_changed_events(int socket_id,
                                const struct sockaddr_in *destination,
                                uint32_t *sequence,
                                const ObserverIdentity *identity,
                                uint32_t *event_count,
                                const ObserverSnapshot *previous,
                                const ObserverSnapshot *current)
{
    if (current->supervisor_state != previous->supervisor_state)
        send_event_packet(socket_id, destination, sequence, identity,
                          SHIKIGAMI_EVENT_SUPERVISOR, ++*event_count,
                          (uint32_t)previous->supervisor_state,
                          (uint32_t)current->supervisor_state, current);
    if (current->stage != previous->stage)
        send_event_packet(socket_id, destination, sequence, identity,
                          SHIKIGAMI_EVENT_STAGE, ++*event_count,
                          (uint32_t)previous->stage, (uint32_t)current->stage,
                          current);
    if (current->audio.bgm_index != previous->audio.bgm_index ||
        current->audio.playing != previous->audio.playing)
        send_event_packet(socket_id, destination, sequence, identity,
                          SHIKIGAMI_EVENT_BGM, ++*event_count,
                          (uint32_t)previous->audio.bgm_index,
                          (uint32_t)current->audio.bgm_index, current);
    if (current->audio.underruns != previous->audio.underruns)
        send_event_packet(socket_id, destination, sequence, identity,
                          SHIKIGAMI_EVENT_UNDERRUN, ++*event_count,
                          previous->audio.underruns, current->audio.underruns,
                          current);
    if (current->fatal_count != previous->fatal_count)
        send_event_packet(socket_id, destination, sequence, identity,
                          SHIKIGAMI_EVENT_FATAL, ++*event_count,
                          previous->fatal_count, current->fatal_count, current);
    if (current->game_flags != previous->game_flags)
        send_event_packet(socket_id, destination, sequence, identity,
                          SHIKIGAMI_EVENT_GAME_FLAGS, ++*event_count,
                          previous->game_flags, current->game_flags, current);
}

static int observer_worker(SceSize args, void *argp)
{
    int common_loaded = 0;
    int inet_loaded = 0;
    int net_initialized = 0;
    int inet_initialized = 0;
    int apctl_initialized = 0;
    int apctl_connected = 0;
    int socket_id = -1;
    int nonblocking = 1;
    int result;
    int hello_result;
    int status_result;
    uint32_t failure_stage = SHIKIGAMI_STAGE_NONE;
    uint32_t sequence = 1;
    uint32_t event_count = 0;
    uint32_t ge_aperture_bytes = 0;
    uint32_t ticks = 0;
    ObserverIdentity identity;
    ObserverSnapshot previous;
    ObserverSnapshot current;
    struct sockaddr_in destination;
#if defined(TH07_PSP_PERF_DIAG)
    PerfLogTransfer perf_log_transfer;
#endif

    (void)args;
    (void)argp;
    memset(&identity, 0, sizeof(identity));
    identity.model = 0xff;
    identity.profile = TH07_SHIKIGAMI_PROFILE;
    memset(&previous, 0, sizeof(previous));
#if defined(TH07_PSP_PERF_DIAG)
    memset(&perf_log_transfer, 0, sizeof(perf_log_transfer));
#endif

    result = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    if (result >= 0)
        common_loaded = 1;
    result = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
    if (result >= 0)
        inet_loaded = 1;

    failure_stage = SHIKIGAMI_STAGE_NET_INIT;
    result = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024);
    if (result < 0)
        goto cleanup;
    net_initialized = 1;
    failure_stage = SHIKIGAMI_STAGE_INET_INIT;
    result = sceNetInetInit();
    if (result < 0)
        goto cleanup;
    inet_initialized = 1;
    failure_stage = SHIKIGAMI_STAGE_APCTL_INIT;
    result = sceNetApctlInit(0x8000, 48);
    if (result < 0)
        goto cleanup;
    apctl_initialized = 1;

    failure_stage = SHIKIGAMI_STAGE_WAIT_IP;
    result = wait_for_profile_one(&identity);
    if (result != 0)
        goto cleanup;
    apctl_connected = 1;
    result = kuKernelGetModel();
    if (result >= 0 && result <= 255)
        identity.model = (uint8_t)result;

    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(TH07_SHIKIGAMI_PORT);
    failure_stage = SHIKIGAMI_STAGE_DEST_IP;
    if (inet_aton(g_target_ipv4, &destination.sin_addr) == 0)
    {
        result = -1;
        goto cleanup;
    }

    failure_stage = SHIKIGAMI_STAGE_UDP_SOCKET;
    socket_id = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
    if (socket_id < 0)
    {
        result = socket_id;
        goto cleanup;
    }
    failure_stage = SHIKIGAMI_STAGE_NONBLOCK;
    result = sceNetInetSetsockopt(socket_id, SOL_SOCKET, SO_NONBLOCK,
                                 &nonblocking, sizeof(nonblocking));
    if (result < 0)
        goto cleanup;
    failure_stage = SHIKIGAMI_STAGE_NONE;
#if defined(TH07_PSP_PERF_DIAG)
    __atomic_store_n(&g_perf_log_transport_ready, 1, __ATOMIC_RELEASE);
#endif

    ge_aperture_bytes = (uint32_t)sceGeEdramGetSize();
    read_snapshot(&current);
    previous = current;
    hello_result = send_identity_packet(socket_id, &destination,
                                        SHIKIGAMI_PACKET_HELLO, &sequence,
                                        &identity);
    status_result = send_status_packet(socket_id, &destination, &sequence,
                                       &identity, ge_aperture_bytes, &current);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    send_item_me_packet(socket_id, &destination, &sequence, &identity);
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    send_a1_move_packet(socket_id, &destination, &sequence, &identity);
#endif
    if (hello_result == 0 || status_result == 0)
        publish_notice(SHIKIGAMI_NOTICE_ONLINE, SHIKIGAMI_STAGE_NONE, 0);
    else
        publish_notice(SHIKIGAMI_NOTICE_SEND_FAILED, SHIKIGAMI_STAGE_NONE,
                       status_result);

    while (observer_running())
    {
        sceKernelDelayThread(100u * 1000u);
        if (!observer_running())
            break;
        read_snapshot(&current);
        send_changed_events(socket_id, &destination, &sequence, &identity,
                            &event_count, &previous, &current);
        previous = current;
#if defined(TH07_PSP_PERF_DIAG)
        /* The snapshot API refuses reads while gameplay/stage load is active.
         * All network work stays on this low-priority observer thread. */
        service_perf_log_transfer(socket_id, &destination, &sequence,
                                  &identity, &perf_log_transfer);
#endif
        if (++ticks >= 10u)
        {
            send_identity_packet(socket_id, &destination,
                                 SHIKIGAMI_PACKET_HEARTBEAT, &sequence,
                                 &identity);
            send_status_packet(socket_id, &destination, &sequence, &identity,
                               ge_aperture_bytes, &current);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            send_item_me_packet(socket_id, &destination, &sequence,
                                &identity);
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
            send_a1_move_packet(socket_id, &destination, &sequence,
                                &identity);
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
            send_portrait_cache_packet(socket_id, &destination, &sequence,
                                       &identity);
#endif
            ticks = 0;
        }
    }

    read_snapshot(&current);
    send_changed_events(socket_id, &destination, &sequence, &identity,
                        &event_count, &previous, &current);
    send_status_packet(socket_id, &destination, &sequence, &identity,
                       ge_aperture_bytes, &current);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    send_item_me_packet(socket_id, &destination, &sequence, &identity);
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    send_a1_move_packet(socket_id, &destination, &sequence, &identity);
#endif
    send_identity_packet(socket_id, &destination, SHIKIGAMI_PACKET_SHUTDOWN,
                         &sequence, &identity);

cleanup:
#if defined(TH07_PSP_PERF_DIAG)
    __atomic_store_n(&g_perf_log_transport_ready, 0, __ATOMIC_RELEASE);
#endif
    if (failure_stage != SHIKIGAMI_STAGE_NONE)
        publish_notice(SHIKIGAMI_NOTICE_SETUP_FAILED, failure_stage, result);
    if (socket_id >= 0)
        sceNetInetClose(socket_id);
    if (apctl_connected)
        sceNetApctlDisconnect();
    if (apctl_initialized)
        sceNetApctlTerm();
    if (inet_initialized)
        sceNetInetTerm();
    if (net_initialized)
        sceNetTerm();
    if (inet_loaded)
        sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
    if (common_loaded)
        sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    return 0;
}

int th07_shikigami_start(void)
{
    int result;

    if (g_target_ipv4[0] == '\0')
    {
#if defined(TH07_PSP_PERF_DIAG)
        __atomic_store_n(&g_perf_log_transport_ready, 0, __ATOMIC_RELEASE);
#endif
        return 0;
    }
    if (g_worker_thread >= 0)
        return 1;

#if defined(TH07_PSP_PERF_DIAG)
    __atomic_store_n(&g_perf_log_transport_ready, 0, __ATOMIC_RELEASE);
#endif
    __atomic_store_n(&g_running, 1, __ATOMIC_RELEASE);
    g_worker_thread = sceKernelCreateThread("th07_shikigami_observer",
                                            observer_worker, 0x30, 0x4000,
                                            PSP_THREAD_ATTR_USER, 0);
    if (g_worker_thread < 0)
    {
        __atomic_store_n(&g_running, 0, __ATOMIC_RELEASE);
        th07_psp_boot_note("SHIKIGAMI TH07 OFF create thread");
        return 0;
    }
    result = sceKernelStartThread(g_worker_thread, 0, 0);
    if (result < 0)
    {
        sceKernelDeleteThread(g_worker_thread);
        g_worker_thread = -1;
        __atomic_store_n(&g_running, 0, __ATOMIC_RELEASE);
        th07_psp_boot_note("SHIKIGAMI TH07 OFF start thread");
        return 0;
    }
    th07_psp_boot_notef("SHIKIGAMI TH07 START DEST%s:%d PROFILE1",
                        g_target_ipv4, TH07_SHIKIGAMI_PORT);
    return 1;
}

void th07_shikigami_shutdown(void)
{
    SceUInt timeout;
    int result;

    if (g_worker_thread < 0)
        return;
#if defined(TH07_PSP_PERF_DIAG)
    __atomic_store_n(&g_perf_log_transport_ready, 0, __ATOMIC_RELEASE);
#endif
    __atomic_store_n(&g_running, 0, __ATOMIC_RELEASE);
    timeout = 2u * 1000u * 1000u;
    result = sceKernelWaitThreadEnd(g_worker_thread, &timeout);
    if (result >= 0)
    {
        sceKernelDeleteThread(g_worker_thread);
        flush_notice();
        th07_psp_boot_note("SHIKIGAMI TH07 STOPPED");
    }
    else
    {
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        /* A networking teardown stuck past the bounded wait must not keep a
         * user thread alive after main returns.  This observer owns no game
         * state and process exit is already committed, so force-delete is the
         * safe final fallback. */
        sceKernelTerminateDeleteThread(g_worker_thread);
        th07_psp_boot_note("SHIKIGAMI TH07 STOP timeout; observer terminated");
#else
        th07_psp_boot_note("SHIKIGAMI TH07 STOP timeout; process exit continues");
#endif
    }
    g_worker_thread = -1;
}
