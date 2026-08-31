#pragma once

#if defined(TH07_PSP_MECC_AUDIO_4M)

struct Th07Audio4mSfxSnapshot
{
    unsigned int atlas_bytes;
    unsigned int canonical_bytes;
    unsigned int replica_bytes;
    unsigned int canonical_output_mask;
    unsigned int replica_output_mask;
    unsigned int mix_jobs;
    unsigned int output_blocks;
    unsigned int fifo_misses;
    unsigned int fatal;
    unsigned int coverage_active;
    unsigned int coverage_complete;
    unsigned int coverage_pass;
    unsigned int coverage_buffer;
};

// `staging` is the existing aligned 64 KiB BGM I/O block.  Sound loading is
// complete before BGM streaming starts, so sharing it adds no Main-RAM cost.
int th07_audio4m_sfx_begin(void *staging, unsigned int staging_bytes);
int th07_audio4m_sfx_upload_buffer(unsigned int buffer_index,
                                   const short *samples,
                                   unsigned int frames,
                                   unsigned int step_fixed);
int th07_audio4m_sfx_finalize(void);

// Starts the diagnostic profile's one-voice audible sweep.  Every canonical
// sound and every replica prefix must reach the real DAC before coverage is
// complete; this is deliberately not a memory-only self-test.  Normal game
// initialization never calls this API: a diagnostic entry must do so explicitly.
int th07_audio4m_sfx_start_coverage(void);

void th07_audio4m_sfx_request(unsigned int logical_index,
                              unsigned int buffer_index,
                              unsigned int gain_q16);
void th07_audio4m_sfx_stop_logical(unsigned int logical_index);

// Nonblocking output-side API.  consume() only reads a published 4 KiB wide block;
// the ME is never waited on by the priority-0x10 DAC thread.  The returned
// token must be committed after sceAudioOutputBlocking returns.
unsigned int th07_audio4m_sfx_consume(short *io, unsigned int frames,
                                      unsigned int *limited_samples);
void th07_audio4m_sfx_output_committed(unsigned int token, int submitted);

void th07_audio4m_sfx_shutdown(void);
int th07_audio4m_sfx_faulted(void);
void th07_audio4m_sfx_snapshot(Th07Audio4mSfxSnapshot *snapshot);

#endif
