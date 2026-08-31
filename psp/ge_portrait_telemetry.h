#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    TH07_PSP_PORTRAIT_SLOT_COUNT = 6,
    /* Physical cache capacity is six roles, but a stage requires only the two
       resident player roles plus the entries present in its face archive. */
    TH07_PSP_PORTRAIT_CAPACITY_MASK =
        (1u << TH07_PSP_PORTRAIT_SLOT_COUNT) - 1u,
    TH07_PSP_PORTRAIT_PLAYER_MASK = 0x03u,
    TH07_PSP_PORTRAIT_STAGE_MASK = 0x3cu,

    TH07_PSP_PORTRAIT_ROLE_SELF = 1,
    TH07_PSP_PORTRAIT_ROLE_BOMB = 2,
    TH07_PSP_PORTRAIT_ROLE_STAGE_0 = 3,
    TH07_PSP_PORTRAIT_ROLE_STAGE_1 = 4,
    TH07_PSP_PORTRAIT_ROLE_STAGE_2 = 5,
    TH07_PSP_PORTRAIT_ROLE_STAGE_3 = 6,

    TH07_PSP_PORTRAIT_CACHE_POOL_INITIALIZED = 1u << 0,
    TH07_PSP_PORTRAIT_CACHE_LEDGER_VALID = 1u << 1,
};

typedef struct Th07PspPortraitSlotSnapshot
{
    uint32_t role;
    uint32_t texture_slot;
    uint32_t raw_address;
    uint32_t allocation_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t psm;
    uint32_t source_hash;
    uint32_t readback_hash;
    uint32_t upload_generation;
    uint32_t draw_count;
} Th07PspPortraitSlotSnapshot;

typedef struct Th07PspPortraitCacheSnapshot
{
    uint32_t flags;
    uint32_t cache_generation;
    uint32_t stage;
    /* Zero while no stage prewarm is committed.  Once the stage face archive
       has loaded successfully, this is the exact player + stage role mask;
       it is not the six-slot physical capacity mask. */
    uint32_t required_mask;
    uint32_t owned_mask;
    uint32_t verified_mask;
    uint32_t sampled_mask;
    uint32_t pool_raw_base;
    uint32_t pool_bytes;
    uint32_t live_bytes;
    uint32_t fallback_count;
    uint32_t migration_count;
    uint32_t allocation_failure_count;
    uint32_t invariant_failure_count;
    Th07PspPortraitSlotSnapshot slots[TH07_PSP_PORTRAIT_SLOT_COUNT];
} Th07PspPortraitCacheSnapshot;

/* Lock-free read of the renderer-published portrait allocation ledger. */
int th07_psp_portrait_cache_snapshot(Th07PspPortraitCacheSnapshot *snapshot);

#ifdef __cplusplus
}
#endif
