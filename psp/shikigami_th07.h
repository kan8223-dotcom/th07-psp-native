#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Th07ShikigamiAudioSnapshot
{
    uint32_t ring_bytes;
    uint32_t ring_fill_frames;
    uint32_t underruns;
    uint32_t generation;
    int32_t bgm_index;
    uint32_t playing;
    uint32_t paused;
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    uint32_t me_upper_base;
    uint32_t me_upper_bytes;
    uint32_t me_jobs;
    uint32_t me_fallbacks;
    uint32_t me_timeouts;
    uint32_t me_max_wait_us;
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M)
    uint32_t sfx_atlas_bytes;
    uint32_t sfx_canonical_bytes;
    uint32_t sfx_replica_bytes;
    uint32_t sfx_canonical_output_mask;
    uint32_t sfx_replica_output_mask;
    uint32_t sfx_mix_jobs;
    uint32_t sfx_output_blocks;
    uint32_t sfx_fifo_misses;
    uint32_t sfx_fatal;
    uint32_t sfx_coverage_active;
    uint32_t sfx_coverage_complete;
    uint32_t sfx_coverage_pass;
    uint32_t sfx_coverage_buffer;
    uint32_t bgm_upload_wraps;
    uint32_t bgm_fetch_wraps;
    uint32_t bgm_output_wraps;
    uint32_t audio4m_proof_flags;
#endif
} Th07ShikigamiAudioSnapshot;

typedef struct Th07ShikigamiFrameSnapshot
{
    uint32_t frame_number;
    uint32_t fps_x10;
    int32_t supervisor_state;
    int32_t stage;
    int32_t spell_index;
    uint32_t game_flags;
    Th07ShikigamiAudioSnapshot audio;
} Th07ShikigamiFrameSnapshot;

enum
{
    TH07_SHIKIGAMI_GAME_DEMO = 1u << 0,
    TH07_SHIKIGAMI_GAME_REPLAY = 1u << 1,
    TH07_SHIKIGAMI_GAME_PAUSED = 1u << 2,
    TH07_SHIKIGAMI_GAME_BOSS = 1u << 3,
    TH07_SHIKIGAMI_GAME_SPELL = 1u << 4,
    TH07_SHIKIGAMI_GAME_DIALOGUE = 1u << 5,
};

enum
{
    TH07_SHIKIGAMI_AUDIO4M_PROOF_FULL_EXTENT = 1u << 0,
    TH07_SHIKIGAMI_AUDIO4M_PROOF_ATLAS_EXACT = 1u << 1,
    TH07_SHIKIGAMI_AUDIO4M_PROOF_CANONICAL_DAC = 1u << 2,
    TH07_SHIKIGAMI_AUDIO4M_PROOF_REPLICA_DAC = 1u << 3,
    TH07_SHIKIGAMI_AUDIO4M_PROOF_BGM_UPLOAD_WRAP = 1u << 4,
    TH07_SHIKIGAMI_AUDIO4M_PROOF_BGM_FETCH_WRAP = 1u << 5,
    TH07_SHIKIGAMI_AUDIO4M_PROOF_BGM_OUTPUT_WRAP = 1u << 6,
    TH07_SHIKIGAMI_AUDIO4M_PROOF_ZERO_FAULTS = 1u << 7,
    TH07_SHIKIGAMI_AUDIO4M_PROOF_REQUIRED = 0xffu,
    /* SE runs from Main RAM: the eDRAM SFX atlas/DAC proofs are structurally
       absent, so PROVEN requires only the extent, BGM wrap and fault bits. */
    TH07_SHIKIGAMI_AUDIO4M_PROOF_REQUIRED_SFX_MAIN_RAM = 0xf1u,
};

/* Packet type 10 is independent of the frozen TH07 STATUS schemas. */
enum
{
    TH07_SHIKIGAMI_PORTRAIT_CACHE_SCHEMA = 1,
    TH07_SHIKIGAMI_PORTRAIT_VALID_BRIDGE_STATE = 1u << 0,
    TH07_SHIKIGAMI_PORTRAIT_VALID_CACHE_SNAPSHOT = 1u << 1,
};

/* Starts only the send-only observer worker. All failures are nonfatal. */
int th07_shikigami_start(void);
void th07_shikigami_shutdown(void);

/* Main-thread publishers. They never call a network API or allocate memory. */
void th07_shikigami_publish_frame(const Th07ShikigamiFrameSnapshot *snapshot);
void th07_shikigami_record_fatal(const char *message);
#if defined(TH07_PSP_PERF_DIAG)
/*
 * Latch a sealed RAM-log snapshot at a non-gameplay boundary.  The caller
 * never performs network work; the observer drains the request later.
 */
int th07_shikigami_perf_log_transport_ready(void);
void th07_shikigami_request_perf_log(void);
#endif

/* Read-only snapshot of the canonical 96K-frame BGM ring and its backend. */
void th07_psp_audio_shikigami_snapshot(Th07ShikigamiAudioSnapshot *snapshot);

#ifdef __cplusplus
}
#endif
