#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only view of one sealed prefix of the diagnostic RAM log.  The
 * observer records this metadata once, then copies bounded chunks while the
 * main thread is outside gameplay.  buffer_generation detects an unexpected
 * Memory Stick flush/memmove while a transfer is in flight.
 */
typedef struct Th07PspPerfLogSnapshot
{
    uint32_t run_id;
    uint32_t window_id;
    uint32_t total_bytes;
    uint32_t log_crc32;
    uint32_t dropped_lines;
    uint32_t valid;
    uint32_t buffer_generation;
} Th07PspPerfLogSnapshot;

/* These APIs perform no allocation, network operation or Memory Stick I/O. */
void th07_psp_perf_log_seal(void);
int th07_psp_perf_log_snapshot_begin(Th07PspPerfLogSnapshot *snapshot);
uint32_t th07_psp_perf_log_snapshot_read(
    const Th07PspPerfLogSnapshot *snapshot, uint32_t offset,
    void *destination, uint32_t capacity);

#ifdef __cplusplus
}
#endif
