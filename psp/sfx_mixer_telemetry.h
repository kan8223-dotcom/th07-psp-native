#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A5-MEASURE observer output for one renderer PERF window.  The single audio
 * worker publishes completed 512-frame mixes to a bounded sequence ring; the
 * renderer consumes only committed entries after the timed window has ended.
 */
typedef struct Th07PspSfxMixerWindow
{
    uint32_t mix_total_us;
    uint32_t mix_calls;
    uint32_t mix_average_us;
    uint32_t mix_p99_us;
    uint32_t mix_max_us;
    uint32_t active_voice_visits;
    uint32_t active_voice_max;
    uint32_t divisor_one_calls;
    uint32_t trigger_count;
    uint32_t limited_samples;
    uint32_t sample_overflow;
} Th07PspSfxMixerWindow;

/* Observer-only: no allocation, Memory Stick I/O, or additional timer read. */
void th07_psp_sfx_mixer_window_take(Th07PspSfxMixerWindow *window);
void th07_psp_sfx_mixer_window_discard(void);

#ifdef __cplusplus
}
#endif
