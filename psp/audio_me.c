#include "audio_me.h"

#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
#include <math.h>
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
#include <malloc.h>
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M)
#include <psppower.h>
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
#include "ge4_game_bridge.hpp"
#endif
#endif
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
#include <kubridge.h>
#endif

#if defined(TH07_PSP_MECC_BGM_384K) && defined(TH07_PSP_MECC_AUDIO_4M)
#error "MECC 384K and full-4M audio profiles are mutually exclusive"
#endif
#if defined(TH07_PSP_ME_RENDER_WORKER) && !defined(TH07_PSP_MECC_AUDIO_4M)
#error "ME render worker requires the guarded AUDIO4M custom-core lifecycle"
#endif

// Exact MECC baseline shipped by the public PSPPMD project.  TH07 uses its
// documented auto-load entry and cache helpers only; application code never
// invokes the embedded kernel bridge directly.
void meLibOnProcess(void);
#include "me-core.h"

#if defined(TH07_PSP_ME_RENDER_WORKER)
extern unsigned int embedded_kcall_len;
#endif

extern void th07_psp_boot_note(const char *message);
extern void th07_psp_boot_notef(const char *fmt, ...);
extern const char *th07_psp_game_dir(void);

enum
{
    ME_CMD_NONE = 0,
    ME_CMD_AUDIO_MIX = 1,
    ME_CMD_VERTEX_PACK = 2,
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    ME_CMD_BGM_RESET = 3,
    ME_CMD_BGM_UPLOAD = 4,
    ME_CMD_BGM_FETCH = 5,
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
    ME_CMD_SFX_UPLOAD = 6,
    ME_CMD_SFX_GATHER = 7,
    ME_CMD_SFX_MIX = 8,
#endif
#if defined(TH07_PSP_ME_RENDER_WORKER)
    ME_CMD_RENDER_EXPAND = 9,
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    ME_CMD_RENDER_STREAM = 10,
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    ME_CMD_BULLET_FAST_UPDATE = 11,
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    ME_CMD_BULLET_COMPACT_UPDATE = 12,
#endif
#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
    // Boot-selftest only.  No gameplay caller can publish command 13.
    ME_CMD_EDRAM_SEED_BENCH = 13,
#endif
#endif
    ME_CMD_STOP = 0xff,
    ME_STAT_IDLE = 0,
    ME_STAT_DONE = 1,

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    ME_WORKER_BOOTING = 0,
    ME_WORKER_READY = 1,
    ME_WORKER_STOPPED = 2,

    ME_BGM_RESULT_OK = 0,
    ME_BGM_RESULT_STALE = 1,
    ME_BGM_RESULT_BOUNDS = 2,
#endif

    ME_OWNER_NONE = 0,
    ME_OWNER_AUDIO = 1,
    ME_OWNER_VERTEX = 2,
#if defined(TH07_PSP_MECC_AUDIO_4M)
    ME_OWNER_SHUTDOWN = 3,
#if defined(TH07_PSP_ME_RENDER_WORKER)
    ME_OWNER_RENDER = 4,
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    ME_OWNER_BULLET_FAST = 5,
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    ME_OWNER_BULLET_COMPACT = 6,
#endif
    // Lower numeric values are more urgent on PSP.  Every full-4M ME owner is
    // temporarily raised to the 0x11 SFX-feeder level so a low-priority game
    // or vertex caller cannot cause priority inversion.  The 0x10 DAC worker
    // remains strictly more urgent and can always submit its next block.
    ME_OWNER_PRIORITY_CEILING = 0x11,
#endif

    ME_AUDIO_WAIT_US = 8000,
    ME_VERTEX_WAIT_US = 6000,
    ME_VERTEX_ARENA_BYTES = 256 * 1024,
    ME_AUDIO_ACCUM_FRAMES = 256,

#if defined(TH07_PSP_ME_RENDER_WORKER)
    ME_RENDER_BENCH_TIMEOUT_US = 100000,
    // The ME has no scheduler service to block on, but polling an uncached
    // Main-RAM mailbox every four instructions measurably competes with SC/GE.
    // About two thousand local instructions add only a few microseconds of
    // command latency while reducing idle bus reads by orders of magnitude.
    ME_RENDER_IDLE_BACKOFF_ROUNDS = 256,
    ME_RENDER_RESULT_OK = 0,
    ME_RENDER_RESULT_VERSION = 1,
    ME_RENDER_RESULT_BOUNDS = 2,
    ME_RENDER_RESULT_PROTOCOL = 3,
    ME_RENDER_BENCH_GUARD_BYTES = 64,
    ME_RENDER_BENCH_GUARD_PATTERN = 0x6d,
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    ME_RENDER_STREAM_GUARD_BYTES = 64,
    ME_RENDER_STREAM_GUARD_PATTERN = 0x71,
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    // The physical token-owned pool contains an optional Item prefix followed
    // by the existing Bullet suffix.  Kernel-local limits remain separate;
    // these aliases are only for SC retirement/GE ownership validation.
    ME_RENDER_STREAM_POOL_MAX_VERTEX_BYTES =
        TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_VERTEX_BYTES,
    ME_RENDER_STREAM_POOL_MAX_RUNS =
        TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_RUNS,
#else
    ME_RENDER_STREAM_POOL_MAX_VERTEX_BYTES =
        TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES,
    ME_RENDER_STREAM_POOL_MAX_RUNS =
        TH07_PSP_ME_RENDER_STREAM_MAX_RUNS,
#endif
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    ME_RENDER_RAW_VM_MAX_BYTES = 4096,
    ME_RENDER_RAW_SPRITE_MAX_BYTES = 1024,
    ME_RENDER_RAW_LAYOUT_SELFTEST_VERSION = 0x52545331u, // "RTS1"
    ME_RENDER_RAW_BULLET_STRIDE = 3452,
    ME_RENDER_RAW_BULLET_COUNT = 1024,
    ME_RENDER_RAW_VM_BYTES = 588,
    ME_RENDER_RAW_VM_ROTATION_Z_OFFSET = 8,
    ME_RENDER_RAW_VM_SCALE_X_OFFSET = 24,
    ME_RENDER_RAW_VM_SCALE_Y_OFFSET = 28,
    ME_RENDER_RAW_VM_UV_SCROLL_X_OFFSET = 40,
    ME_RENDER_RAW_VM_UV_SCROLL_Y_OFFSET = 44,
    ME_RENDER_RAW_VM_COLOR_OFFSET = 440,
    ME_RENDER_RAW_VM_COLOR2_OFFSET = 444,
    ME_RENDER_RAW_VM_FLAGS_OFFSET = 448,
    ME_RENDER_RAW_VM_SPRITE_OFFSET = 484,
    ME_RENDER_RAW_SPRITE_BYTES = 64,
    ME_RENDER_RAW_SPRITE_COUNT = 2560,
    ME_RENDER_RAW_SPRITE_SOURCE_OFFSET = 0,
    ME_RENDER_RAW_SPRITE_UV_START_X_OFFSET = 28,
    ME_RENDER_RAW_SPRITE_UV_START_Y_OFFSET = 32,
    ME_RENDER_RAW_SPRITE_UV_END_X_OFFSET = 36,
    ME_RENDER_RAW_SPRITE_UV_END_Y_OFFSET = 40,
    ME_RENDER_RAW_SPRITE_HEIGHT_OFFSET = 44,
    ME_RENDER_RAW_SPRITE_WIDTH_OFFSET = 48,
    ME_RENDER_RAW_VM_VISIBLE = 1u << 0,
    ME_RENDER_RAW_VM_ACTIVE = 1u << 1,
    ME_RENDER_RAW_VM_BLEND_ADD = 1u << 4,
    ME_RENDER_RAW_VM_ANCHOR_SHIFT = 10,
    ME_RENDER_RAW_VM_ANCHOR_MASK = 3u << ME_RENDER_RAW_VM_ANCHOR_SHIFT,
    ME_RENDER_RAW_VM_ZWRITE_DISABLE = 1u << 12,
    ME_RENDER_RAW_VM_USE_COLOR2 = 1u << 16,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    ME_RENDER_LIST_LAYOUT_SELFTEST_VERSION = 0x4c545331u, // "LTS1"
    ME_RENDER_LIST_ACTIVE_WORD_COUNT = 32,
    ME_RENDER_LIST_GENERATION_STRIDE = 4,
    ME_RENDER_LIST_BULLET_NEXT_OFFSET = 3076,
    ME_RENDER_LIST_BULLET_STATE_OFFSET = 3068,
    ME_RENDER_LIST_BULLET_COLLISION_TYPE_OFFSET = 2954,
    ME_RENDER_LIST_BULLET_POS_X_OFFSET = 2956,
    ME_RENDER_LIST_BULLET_POS_Y_OFFSET = 2960,
    ME_RENDER_LIST_BULLET_RENDER_ANGLE_OFFSET = 3436,
    ME_RENDER_LIST_BULLET_SIN_OFFSET = 3440,
    ME_RENDER_LIST_BULLET_COS_OFFSET = 3444,
    ME_RENDER_LIST_BULLET_ROTATION_VALID_OFFSET = 3448,
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    ME_RENDER_ITEM_LAYOUT_SELFTEST_VERSION = 0x49545331u, // "ITS1"
    ME_RENDER_ITEM_STRIDE = 648,
    ME_RENDER_ITEM_COUNT = 1100,
    ME_RENDER_ITEM_ACTIVE_WORD_COUNT = 35,
    ME_RENDER_ITEM_GENERATION_STRIDE = 4,
    ME_RENDER_ITEM_NEXT_OFFSET = 644,
    ME_RENDER_ITEM_IN_USE_OFFSET = 637,
    ME_RENDER_ITEM_TYPE_OFFSET = 636,
    ME_RENDER_ITEM_VM_OFFSET = 0,
    ME_RENDER_ITEM_VM_POS_X_OFFSET = 456,
    ME_RENDER_ITEM_VM_POS_Y_OFFSET = 460,
    ME_RENDER_ITEM_VM_POS_Z_OFFSET = 464,
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    ME_ITEM_MOTION_CURRENT_POS_OFFSET = 588,
    ME_ITEM_MOTION_START_POS_OFFSET = 600,
    ME_ITEM_MOTION_TARGET_POS_OFFSET = 612,
    ME_ITEM_MOTION_TIMER_SUBFRAME_OFFSET = 628,
    ME_ITEM_MOTION_TIMER_CURRENT_OFFSET = 632,
    ME_ITEM_MOTION_STATE_OFFSET = 639,
    ME_ITEM_MOTION_AUTOCOLLECT_OFFSET = 640,
#endif
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    ME_RENDER_EFFECT_LAYOUT_SELFTEST_VERSION = 0x45545331u, // "ETS1"
    ME_RENDER_EFFECT_STRIDE = 728,
    ME_RENDER_EFFECT_COUNT = 408,
    ME_RENDER_EFFECT_ACTIVE_WORD_COUNT = 13,
    ME_RENDER_EFFECT_GENERATION_STRIDE = 4,
    ME_RENDER_EFFECT_NEXT_OFFSET = 724,
    ME_RENDER_EFFECT_IN_USE_OFFSET = 716,
    ME_RENDER_EFFECT_IS_2D_OFFSET = 720,
    ME_RENDER_EFFECT_VM_OFFSET = 0,
    ME_RENDER_EFFECT_VM_POS_X_OFFSET = 456,
    ME_RENDER_EFFECT_VM_POS_Y_OFFSET = 460,
    ME_RENDER_EFFECT_VM_POS_Z_OFFSET = 464,
#endif
#endif
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    ME_BULLET_FAST_TIMEOUT_US = 100000,
    ME_BULLET_FAST_GUARD_BYTES = 64,
    ME_BULLET_FAST_GUARD_PATTERN = 0x86,
    ME_BULLET_FAST_BULLET_STRIDE = 3452,
    ME_BULLET_FAST_BULLET_COUNT = 1024,
    ME_BULLET_FAST_GENERATION_STRIDE = 4,
    ME_BULLET_FAST_SPRITE_STRIDE = 64,
    ME_BULLET_FAST_SPRITE_COUNT = 2560,
    ME_BULLET_FAST_BULLET_STATE_OFFSET = 3068,
    ME_BULLET_FAST_BULLET_POS_X_OFFSET = 2956,
    ME_BULLET_FAST_BULLET_POS_Y_OFFSET = 2960,
    ME_BULLET_FAST_BULLET_POS_Z_OFFSET = 2964,
    ME_BULLET_FAST_BULLET_VELOCITY_X_OFFSET = 2968,
    ME_BULLET_FAST_BULLET_VELOCITY_Y_OFFSET = 2972,
    ME_BULLET_FAST_BULLET_VELOCITY_Z_OFFSET = 2976,
    ME_BULLET_FAST_BULLET_SPAWN_DELAY_OFFSET = 3056,
    ME_BULLET_FAST_BULLET_EX_FLAGS_OFFSET = 3060,
    ME_BULLET_FAST_BULLET_OUT_OF_BOUNDS_TIME_OFFSET = 3070,
    ME_BULLET_FAST_BULLET_CURRENT_COMMAND_INDEX_OFFSET = 3088,
    ME_BULLET_FAST_BULLET_COMMANDS_OFFSET = 3092,
    ME_BULLET_FAST_BULLET_COMMAND_STRIDE = 24,
    ME_BULLET_FAST_BULLET_COMMAND_TYPE_OFFSET = 16,
    ME_BULLET_FAST_BULLET_GRAZE_SIZE_X_OFFSET = 2940,
    ME_BULLET_FAST_BULLET_GRAZE_SIZE_Y_OFFSET = 2944,
    ME_BULLET_FAST_VM_SPRITE_OFFSET = 484,
    ME_BULLET_FAST_SPRITE_WIDTH_OFFSET = 48,
    ME_BULLET_FAST_SPRITE_HEIGHT_OFFSET = 44,
    ME_BULLET_FAST_BOMB_CLEAR_STRIDE = 32,
    ME_BULLET_FAST_BOMB_CLEAR_POS_X_OFFSET = 0,
    ME_BULLET_FAST_BOMB_CLEAR_POS_Y_OFFSET = 4,
    ME_BULLET_FAST_BOMB_CLEAR_POS_Z_OFFSET = 8,
    ME_BULLET_FAST_BOMB_CLEAR_SIZE_X_OFFSET = 12,
    ME_BULLET_FAST_BOMB_CLEAR_SIZE_Y_OFFSET = 16,
    ME_BULLET_FAST_BOMB_CLEAR_CAPACITY = 96,
    ME_BULLET_FAST_STATE_NORMAL = 1,
    ME_BULLET_FAST_PLAYER_STATE_MAX = 4,
    ME_BULLET_FAST_PLAYER_STATE_BORDER = 4,
    ME_BULLET_FAST_COMMAND_COUNT = 5,
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    ME_BULLET_COMPACT_TIMEOUT_US = 100000,
    ME_BULLET_COMPACT_GUARD_BYTES = 64,
    ME_BULLET_COMPACT_GUARD_PATTERN = 0x97,
    ME_BULLET_COMPACT_BULLET_POS_Z_OFFSET = 2964,
    ME_BULLET_COMPACT_BULLET_VELOCITY_X_OFFSET = 2968,
    ME_BULLET_COMPACT_BULLET_VELOCITY_Y_OFFSET = 2972,
    ME_BULLET_COMPACT_BULLET_VELOCITY_Z_OFFSET = 2976,
    ME_BULLET_COMPACT_BULLET_SPAWN_DELAY_OFFSET = 3056,
    ME_BULLET_COMPACT_BULLET_EX_FLAGS_OFFSET = 3060,
    ME_BULLET_COMPACT_BULLET_CURRENT_COMMAND_INDEX_OFFSET = 3088,
    ME_BULLET_COMPACT_BULLET_COMMANDS_OFFSET = 3092,
    ME_BULLET_COMPACT_BULLET_COMMAND_STRIDE = 24,
    ME_BULLET_COMPACT_BULLET_COMMAND_TYPE_OFFSET = 16,
    ME_BULLET_COMPACT_BULLET_GRAZE_SIZE_X_OFFSET = 2940,
    ME_BULLET_COMPACT_BULLET_GRAZE_SIZE_Y_OFFSET = 2944,
    ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE = 32,
    ME_BULLET_COMPACT_BOMB_CLEAR_POS_X_OFFSET = 0,
    ME_BULLET_COMPACT_BOMB_CLEAR_POS_Y_OFFSET = 4,
    ME_BULLET_COMPACT_BOMB_CLEAR_POS_Z_OFFSET = 8,
    ME_BULLET_COMPACT_BOMB_CLEAR_SIZE_X_OFFSET = 12,
    ME_BULLET_COMPACT_BOMB_CLEAR_SIZE_Y_OFFSET = 16,
    ME_BULLET_COMPACT_BOMB_CLEAR_CAPACITY = 96,
    ME_BULLET_COMPACT_STATE_NORMAL = 1,
    ME_BULLET_COMPACT_PLAYER_STATE_MAX = 4,
    ME_BULLET_COMPACT_PLAYER_STATE_BORDER = 4,
    ME_BULLET_COMPACT_COMMAND_COUNT = 5,
#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
    // Upper local eDRAM, deliberately away from the retained lower-region
    // audio experiments.  The guarded mirror exists only while command 13 is
    // executing and is zeroed before the worker publishes completion.
    ME_EDRAM_SEED_BENCH_AREA_BASE = 0x00300000,
    ME_EDRAM_SEED_BENCH_GUARD_BYTES = 64,
    ME_EDRAM_SEED_BENCH_SEED_BASE =
        ME_EDRAM_SEED_BENCH_AREA_BASE + ME_EDRAM_SEED_BENCH_GUARD_BYTES,
    ME_EDRAM_SEED_BENCH_GUARD_PATTERN = 0xa5,
    ME_EDRAM_SEED_BENCH_SAMPLES = 32,
    ME_EDRAM_SEED_BENCH_TIMEOUT_US = 1000000,
    ME_EDRAM_SEED_BENCH_RESULT_OK = 0,
    ME_EDRAM_SEED_BENCH_RESULT_BOUNDS = 1,
    ME_EDRAM_SEED_BENCH_RESULT_GUARD = 2,
    ME_EDRAM_SEED_BENCH_RESULT_MISMATCH = 3,
    ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL = 4,
#endif
#endif
#endif
#endif
#endif

#if !defined(TH07_PSP_MECC_AUDIO_4M)
    // PSPPMD commit 18fb0b1 uses 2 KiB from local ME eDRAM at 0x400.
    // TH07 processes a 1024-frame block as four 256-frame chunks so the
    // stereo 32-bit accumulator remains inside that proven range.
    ME_AUDIO_EDRAM_ACCUM_BASE = 0x00000400,
    ME_AUDIO_EDRAM_ACCUM_FRAMES = ME_AUDIO_ACCUM_FRAMES,
    ME_AUDIO_EDRAM_ACCUM_BYTES = ME_AUDIO_EDRAM_ACCUM_FRAMES * 2 * sizeof(int),
    ME_AUDIO_EDRAM_ACCUM_END = ME_AUDIO_EDRAM_ACCUM_BASE + ME_AUDIO_EDRAM_ACCUM_BYTES,
    ME_AUDIO_EDRAM_FAT_STACK_TOP = 0x00200000,
#else
    // The full-4M profile leaves no local byte for a stack or mixer scratch.
    // Its identical 256-frame stereo-s32 accumulator lives in Main RAM.
    ME_AUDIO_MAIN_ACCUM_FRAMES = ME_AUDIO_ACCUM_FRAMES,
    ME_AUDIO_MAIN_ACCUM_BYTES = ME_AUDIO_MAIN_ACCUM_FRAMES * 2 * sizeof(int),
#endif

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
#if defined(TH07_PSP_MECC_AUDIO_4M) && defined(TH07_PSP_SFX_MAIN_RAM)
    // R18 deliberately abandons the retention-faulting upper half.  Keep the
    // first 64 KiB clear so byte zero never becomes a null C pointer and the
    // legacy PSPPMD scratch at 0x400..0xc00 remains outside the BGM extent.
    // The complete 384 KiB ring is therefore inside lower eDRAM.
    ME_BGM_RING_BASE = 0x00010000,
#else
    // Frozen by the SHIKIGAMI MECC real-hardware PASS on model 3/table 2.
    ME_BGM_RING_BASE = 0x00200000,
#endif
    // Both profiles keep the 384 KiB extent.  The 2 MiB ring
    // experiment stored PCM for ~12 s in the deeper upper region; R8/R9 CRC
    // telemetry proved deterministic, data-dependent corruption there
    // (identical bit flips under cached and uncached ME access), audible as
    // crackle.  The full profile now validates the lower copy instead.
    ME_BGM_RING_BYTES = 0x00060000,
    ME_BGM_RING_END = ME_BGM_RING_BASE + ME_BGM_RING_BYTES,
    ME_BGM_UPLOAD_BYTES = 0x00010000,
    ME_BGM_FETCH_BYTES = 512 * 2 * sizeof(short),
#if !defined(TH07_PSP_MECC_AUDIO_4M)
    ME_BGM_STACK_TOP = 0x00400000,
    ME_BGM_STACK_RESERVED_BYTES = 0x00010000,
    ME_BGM_STACK_BOTTOM = ME_BGM_STACK_TOP - ME_BGM_STACK_RESERVED_BYTES,
#endif
    ME_BGM_REQUIRED_MODEL = 3,
    ME_BGM_REQUIRED_TABLE = 2,
    ME_BGM_READY_TIMEOUT_US = 3000000,
    ME_BGM_COMMAND_TIMEOUT_US = 50000,
    ME_BGM_STOP_TIMEOUT_US = 3000000,
#if defined(TH07_PSP_MECC_AUDIO_4M)
    ME_LOCAL_BYTES = 0x00400000,
#if !defined(TH07_PSP_SFX_MAIN_RAM)
    ME_SFX_ATLAS_BASE = 0x00000000,
    ME_SFX_ATLAS_BYTES = 0x00200000,
    ME_SFX_ATLAS_END = ME_SFX_ATLAS_BASE + ME_SFX_ATLAS_BYTES,
    ME_SFX_TRANSFER_MAX_BYTES = 0x00010000,
    ME_SFX_MIX_OUTPUT_BYTES =
        TH07_PSP_ME_SFX_MAX_MIX_FRAMES * 2 * sizeof(int),
    ME_CACHE_LINE_BYTES = 64,
#endif
#endif
#endif
};

#if !defined(TH07_PSP_MECC_AUDIO_4M)
_Static_assert(ME_AUDIO_EDRAM_ACCUM_END == 0x00000c00,
               "ME audio eDRAM footprint changed; re-audit PSPPMD/MECC layout");
_Static_assert(ME_AUDIO_EDRAM_ACCUM_END < ME_AUDIO_EDRAM_FAT_STACK_TOP,
               "ME audio accumulator collides with the smallest MECC local stack");
#else
_Static_assert(ME_AUDIO_MAIN_ACCUM_BYTES == 2048u,
               "full-4M ME accumulator must remain exactly 2 KiB in Main RAM");
_Static_assert(TH07_ME_MAIN_STACK_BYTES == 8192u,
               "full-4M guarded ME stack must remain exactly 8 KiB");
_Static_assert(TH07_ME_MAIN_STACK_GUARD_BYTES == 64u,
               "ME stack guards must each occupy one cache line");
_Static_assert(((TH07_ME_MAIN_STACK_GUARD_BYTES + TH07_ME_MAIN_STACK_BYTES) & 63u) == 0u,
               "ME Main-RAM stack top must be cache-line aligned");
#endif
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
_Static_assert(ME_SFX_ATLAS_BASE == 0u, "SFX atlas must begin at local byte zero");
_Static_assert(ME_SFX_ATLAS_BYTES == 2097152u, "SFX atlas must be exactly 2 MiB");
_Static_assert(ME_SFX_ATLAS_END == ME_BGM_RING_BASE,
               "SFX and BGM local partitions must be contiguous");
_Static_assert(ME_BGM_RING_BYTES == 393216u,
               "ME BGM ring must remain the proven 384 KiB");
_Static_assert(ME_BGM_RING_END == 0x00260000u, "ME BGM ring end changed");
_Static_assert(ME_BGM_RING_END <= ME_LOCAL_BYTES,
               "ME BGM ring exceeds local eDRAM");
_Static_assert(ME_SFX_TRANSFER_MAX_BYTES * 32u == ME_SFX_ATLAS_BYTES,
               "SFX atlas must divide into exact 64 KiB transfer blocks");
_Static_assert(ME_SFX_MIX_OUTPUT_BYTES == 4096u,
               "wide SFX output must remain one 512-frame stereo-s32 block");
_Static_assert(sizeof(int) == 4u, "PSP wide SFX sample must be signed 32-bit");
_Static_assert(TH07_PSP_ME_SFX_MAX_VOICES * 32768u + 32768u <= INT32_MAX,
               "wide SFX plus BGM must fit signed 32-bit final mix");
#elif defined(TH07_PSP_MECC_AUDIO_4M)
_Static_assert(ME_BGM_RING_BASE == 0x00010000u,
               "Main-RAM SFX profile must keep BGM at lower+64 KiB");
_Static_assert(ME_BGM_RING_BYTES == 393216u,
               "ME BGM ring must remain exactly 384 KiB");
_Static_assert(ME_BGM_RING_END == 0x00070000u,
               "lower ME BGM ring end changed");
_Static_assert(ME_BGM_RING_END <= 0x00200000u,
               "ME BGM ring entered retention-faulting upper eDRAM");
_Static_assert(ME_BGM_RING_END <= ME_LOCAL_BYTES,
               "ME BGM ring exceeds local eDRAM");
#else
_Static_assert(ME_BGM_RING_BYTES == 393216u, "ME BGM ring must remain exactly 384 KiB");
_Static_assert(ME_BGM_RING_END == 0x00260000u, "ME BGM ring end changed");
#endif
_Static_assert((ME_BGM_RING_BASE & 63u) == 0u, "ME BGM ring base is not cache aligned");
_Static_assert((ME_BGM_RING_BYTES & 63u) == 0u, "ME BGM ring size is not cache aligned");
#if !defined(TH07_PSP_MECC_AUDIO_4M)
_Static_assert(ME_BGM_RING_END <= ME_BGM_STACK_BOTTOM,
               "ME BGM ring collides with the explicit model-3 stack reserve");
_Static_assert(ME_BGM_UPLOAD_BYTES * 6u == ME_BGM_RING_BYTES,
               "ME BGM ring must contain six exact 64 KiB upload blocks");
#else
_Static_assert(ME_BGM_UPLOAD_BYTES * 6u == ME_BGM_RING_BYTES,
               "ME BGM ring must contain six exact 64 KiB upload blocks");
#endif
_Static_assert(ME_BGM_RING_BYTES % ME_BGM_FETCH_BYTES == 0u,
               "ME BGM ring must divide into exact 512-frame fetches");
#endif

typedef struct MeVertexPosition
{
    uint32_t x, y, z;
} MeVertexPosition;

typedef struct MeVertexTexPosition
{
    uint32_t u, v, x, y, z;
} MeVertexTexPosition;

typedef struct MeVertexColorPosition
{
    uint32_t color, x, y, z;
} MeVertexColorPosition;

typedef struct MeVertexTexColorPosition
{
    uint32_t u, v, color, x, y, z;
} MeVertexTexColorPosition;

_Static_assert(sizeof(MeVertexPosition) == 12, "ME GE position layout changed");
_Static_assert(sizeof(MeVertexTexPosition) == 20, "ME GE texture layout changed");
_Static_assert(sizeof(MeVertexColorPosition) == 16, "ME GE color layout changed");
_Static_assert(sizeof(MeVertexTexColorPosition) == 24, "ME GE texture/color layout changed");

#if defined(TH07_PSP_ME_RENDER_WORKER)
_Static_assert(sizeof(Th07PspMeRenderRecord32) == 32u,
               "ME render record prefix must remain exactly 32 bytes");
_Static_assert(TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD ==
                   4u * sizeof(MeVertexTexColorPosition),
               "ME render output must remain four native 24-byte vertices");
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
_Static_assert(sizeof(Th07PspMeRenderStreamRecord) ==
                   TH07_PSP_ME_RENDER_STREAM_RECORD_BYTES,
               "I-ME1 record ABI must remain one cache line");
_Static_assert(sizeof(Th07PspMeRenderStreamVertex) ==
                   TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES,
               "render-stream vertex ABI does not match its byte contract");
_Static_assert(_Alignof(Th07PspMeRenderStreamVertex) == 4u,
               "render-stream vertex ABI must retain 32-bit alignment");
#if defined(TH07_PSP_ME_RENDER_UV16)
_Static_assert(offsetof(Th07PspMeRenderStreamVertex, u) == 0u &&
                   offsetof(Th07PspMeRenderStreamVertex, v) == 2u &&
                   offsetof(Th07PspMeRenderStreamVertex, color) == 4u,
               "C1 UV16 prefix layout changed");
#else
_Static_assert(offsetof(Th07PspMeRenderStreamVertex, uBits) == 0u &&
                   offsetof(Th07PspMeRenderStreamVertex, vBits) == 4u &&
                   offsetof(Th07PspMeRenderStreamVertex, color) == 8u,
               "float UV prefix layout changed");
#endif
#if defined(TH07_PSP_ME_RENDER_XYZ16)
#if defined(TH07_PSP_ME_RENDER_UV16)
_Static_assert(offsetof(Th07PspMeRenderStreamVertex, x) ==
                       8u &&
                   offsetof(Th07PspMeRenderStreamVertex, y) == 10u &&
                   offsetof(Th07PspMeRenderStreamVertex, z) == 12u &&
                   offsetof(Th07PspMeRenderStreamVertex, reserved) == 14u,
               "C1 XYZ16 suffix layout changed");
#else
_Static_assert(offsetof(Th07PspMeRenderStreamVertex, x) == 12u &&
                   offsetof(Th07PspMeRenderStreamVertex, y) == 14u &&
                   offsetof(Th07PspMeRenderStreamVertex, z) == 16u &&
                   offsetof(Th07PspMeRenderStreamVertex, reserved) == 18u,
               "C1 XYZ16 suffix layout changed");
#endif
#else
#if defined(TH07_PSP_ME_RENDER_UV16)
_Static_assert(offsetof(Th07PspMeRenderStreamVertex, xBits) == 8u &&
                   offsetof(Th07PspMeRenderStreamVertex, yBits) == 12u &&
                   offsetof(Th07PspMeRenderStreamVertex, zBits) == 16u,
               "UV16/float XYZ suffix layout changed");
#else
_Static_assert(offsetof(Th07PspMeRenderStreamVertex, xBits) == 12u &&
                   offsetof(Th07PspMeRenderStreamVertex, yBits) == 16u &&
                   offsetof(Th07PspMeRenderStreamVertex, zBits) == 20u,
               "native float XYZ suffix layout changed");
#endif
#endif
_Static_assert(sizeof(Th07PspMeRenderStreamRun) == 32u,
               "I-ME1 run ABI must remain 32 bytes");
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
_Static_assert(sizeof(Th07PspMeRenderRawRecord) == 32u,
               "I-ME4 raw record ABI must remain exactly 32 bytes");
_Static_assert(sizeof(Th07PspMeRenderRawLayout) == 116u,
               "I-ME4 runtime layout ABI changed");
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
_Static_assert(sizeof(Th07PspMeRenderListLayout) == 128u,
               "I-ME5 direct-list layout ABI changed");
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
_Static_assert(sizeof(Th07PspMeRenderItemLayout) == 128u,
               "I-ME7 Item direct-list layout ABI changed");
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
_Static_assert(sizeof(Th07PspMeRenderEffectLayout) == 128u,
               "I-ME8 Effect direct-list layout ABI changed");
#endif
#endif
#endif
#endif
#endif
#endif

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
_Static_assert(sizeof(Th07PspMeBulletFastLayout) == 152u,
               "I-ME6 runtime layout ABI changed");
_Static_assert(sizeof(Th07PspMeBulletFastJob) == 216u,
               "I-ME6 job ABI changed");
_Static_assert(sizeof(Th07PspMeBulletFastSlotResult) == 16u,
               "I-ME6 slot result must remain 16 bytes");
_Static_assert(sizeof(Th07PspMeBulletFastOutput) == 16512u,
               "I-ME6 fixed output ABI changed");
_Static_assert((sizeof(Th07PspMeBulletFastOutput) & 63u) == 0u,
               "I-ME6 output must occupy whole cache lines");

typedef struct __attribute__((aligned(64))) MeBulletFastMailbox
{
    Th07PspMeBulletFastJob job;
    uint32_t outputPhys;
    uint32_t outputCapacity;
    volatile uint32_t result;
    volatile uint32_t activeCount;
    volatile uint32_t candidateCount;
    volatile uint32_t inBoundsCount;
    volatile uint32_t noCollisionCount;
    volatile uint32_t firstBadSlot;
    volatile uint32_t invalidateCycles;
    volatile uint32_t kernelCycles;
    volatile uint32_t writebackCycles;
    volatile uint32_t fcr31Before;
    volatile uint32_t fcr31Effective;
    volatile uint32_t fcr31After;
} MeBulletFastMailbox;

_Static_assert((sizeof(MeBulletFastMailbox) & 63u) == 0u,
               "I-ME6 mailbox must occupy whole cache lines");
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
#define ME_BULLET_COMPACT_SEED_METADATA_BYTES \
    offsetof(Th07PspMeBulletCompactSeed, generation)
_Static_assert(sizeof(Th07PspMeBulletCompactSeedSlot) == 64u,
               "D1 keeps the legacy record solely as a transpose oracle");
_Static_assert(TH07_PSP_ME_BULLET_COMPACT_SOA_PLANE_STRIDE == 1040u,
               "D1 SoA plane cache-set skew changed");
_Static_assert(ME_BULLET_COMPACT_SEED_METADATA_BYTES == 320u &&
                   (ME_BULLET_COMPACT_SEED_METADATA_BYTES & 63u) == 0u,
               "D1 SoA metadata prefix must occupy whole cache lines");
_Static_assert(sizeof(Th07PspMeBulletCompactSeed) == 58560u,
               "D1 SoA compact seed bank ABI changed");
#elif defined(TH07_PSP_ME_BULLET_SEED_SLIM)
_Static_assert(sizeof(Th07PspMeBulletCompactSeedSlot) == 56u,
               "C2b compact seed slot must be exactly 56 bytes");
_Static_assert(sizeof(Th07PspMeBulletCompactSeed) == 57664u,
               "C2b compact seed bank ABI changed");
#else
_Static_assert(sizeof(Th07PspMeBulletCompactSeedSlot) == 64u,
               "compact seed slot ABI must remain exactly 64 bytes");
_Static_assert(sizeof(Th07PspMeBulletCompactSeed) == 65728u,
               "compact seed bank ABI changed");
#endif
_Static_assert(sizeof(Th07PspMeBulletCompactSeedHeader) == 64u,
               "compact seed header must occupy one cache line");
_Static_assert((sizeof(Th07PspMeBulletCompactSeed) & 63u) == 0u,
               "compact seed bank must occupy whole cache lines");
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
_Static_assert(sizeof(Th07PspMeItemMotionSeedHeader) == 64u,
               "Item motion seed header must occupy one cache line");
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
_Static_assert(sizeof(Th07PspMeItemMotionSeedSlot) == 48u,
               "C2c Item motion seed slot must be exactly 48 bytes");
_Static_assert(sizeof(Th07PspMeItemMotionSeed) == 53632u,
               "C2c Item motion seed bank ABI changed");
#else
_Static_assert(sizeof(Th07PspMeItemMotionSeedSlot) == 64u,
               "Item motion seed slot must occupy one cache line");
_Static_assert(sizeof(Th07PspMeItemMotionSeed) == 70656u,
               "Item motion seed bank ABI changed");
#endif
_Static_assert(sizeof(Th07PspMeItemMotionOutputHeader) == 64u,
               "Item motion output header must occupy one cache line");
_Static_assert(sizeof(Th07PspMeItemMotionSlotResult) == 32u,
               "Item motion result slot ABI changed");
_Static_assert(sizeof(Th07PspMeItemMotionOutput) == 35456u,
               "Item motion output ABI changed");
_Static_assert((sizeof(Th07PspMeItemMotionSeed) & 63u) == 0u &&
                   (sizeof(Th07PspMeItemMotionOutput) & 63u) == 0u,
               "Item motion arenas must occupy whole cache lines");
_Static_assert(sizeof(Th07PspMeBulletCompactJob) == 128u,
               "A1-MOVE compact job ABI changed");
_Static_assert(sizeof(Th07PspMeBulletCompactCompletion) == 88u,
               "A1-MOVE compact completion ABI changed");
#else
_Static_assert(sizeof(Th07PspMeBulletCompactJob) == 92u,
               "compact job ABI changed");
_Static_assert(sizeof(Th07PspMeBulletCompactCompletion) == 72u,
               "compact completion ABI changed");
#endif
#if defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
_Static_assert(sizeof(Th07PspMeBulletCompactSlotResult) == 4u,
               "C2a compact slot result must be exactly 4 bytes");
_Static_assert(sizeof(Th07PspMeBulletCompactOutput) == 4224u,
               "C2a compact output ABI changed");
#else
_Static_assert(sizeof(Th07PspMeBulletCompactSlotResult) == 16u,
               "compact slot result must remain exactly 16 bytes");
_Static_assert(sizeof(Th07PspMeBulletCompactOutput) == 16512u,
               "compact output ABI changed");
#endif
_Static_assert((sizeof(Th07PspMeBulletCompactOutput) & 63u) == 0u,
               "compact output must occupy whole cache lines");

typedef struct __attribute__((aligned(64))) MeBulletCompactMailbox
{
    Th07PspMeBulletCompactJob job;
    uint32_t seedPhys;
    uint32_t seedCapacity;
    uint32_t outputPhys;
    uint32_t outputCapacity;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    uint32_t itemSeedPhys;
    uint32_t itemSeedCapacity;
    uint32_t itemOutputPhys;
    uint32_t itemOutputCapacity;
#endif
    volatile uint32_t result;
    volatile uint32_t candidateCount;
    volatile uint32_t inBoundsCount;
    volatile uint32_t noCollisionCount;
    volatile uint32_t firstBadSlot;
    volatile uint32_t invalidateCycles;
    volatile uint32_t kernelCycles;
    volatile uint32_t writebackCycles;
    volatile uint32_t fcr31Before;
    volatile uint32_t fcr31Effective;
    volatile uint32_t fcr31After;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    volatile uint32_t itemResult;
    volatile uint32_t itemCandidateCount;
    volatile uint32_t itemProcessedCount;
    volatile uint32_t itemFirstBadSlot;
#endif
} MeBulletCompactMailbox;

_Static_assert((sizeof(MeBulletCompactMailbox) & 63u) == 0u,
               "compact mailbox must occupy whole cache lines");

#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
typedef struct __attribute__((aligned(64))) MeEdramSeedBenchMailbox
{
    Th07PspMeBulletCompactJob job;
    uint32_t seedPhys;
    uint32_t seedCapacity;
    uint32_t mirrorPhys;
    uint32_t mirrorCapacity;
    uint32_t outputPhys;
    uint32_t outputCapacity;
    uint32_t recordCount;
    uint32_t samples;
    volatile uint32_t result;
    volatile uint32_t mainTotalP50;
    volatile uint32_t mainTotalP99;
    volatile uint32_t stageTotalP50;
    volatile uint32_t stageTotalP99;
    volatile uint32_t mirrorTotalP50;
    volatile uint32_t mirrorTotalP99;
    volatile uint32_t mainInvalidateP50;
    volatile uint32_t mainKernelP50;
    volatile uint32_t mainWritebackP50;
    volatile uint32_t mainToLocalP50;
    volatile uint32_t localKernelP50;
    volatile uint32_t localWritebackP50;
    volatile uint32_t localToMainP50;
    volatile uint32_t mismatchWords;
    volatile uint32_t inputHash;
    volatile uint32_t localHash;
    volatile uint32_t guardFaults;
    volatile uint32_t fcr31Before;
    volatile uint32_t fcr31Effective;
    volatile uint32_t fcr31After;
} MeEdramSeedBenchMailbox;

_Static_assert((sizeof(MeEdramSeedBenchMailbox) & 63u) == 0u,
               "M0E mailbox must occupy whole cache lines");
_Static_assert(ME_EDRAM_SEED_BENCH_AREA_BASE >= 0x00200000u,
               "M0E scratch must remain in upper local eDRAM");
_Static_assert((ME_EDRAM_SEED_BENCH_AREA_BASE & 63u) == 0u &&
                   (ME_EDRAM_SEED_BENCH_SEED_BASE & 63u) == 0u,
               "M0E scratch and seed must remain cache-line aligned");
_Static_assert(ME_EDRAM_SEED_BENCH_SEED_BASE +
                       sizeof(Th07PspMeBulletCompactSeed) +
                       ME_EDRAM_SEED_BENCH_GUARD_BYTES <=
                   0x00400000u,
               "M0E guarded seed exceeds local eDRAM");
#endif
#endif

typedef struct MeMixInput
{
    uint32_t sourcePhys;
    uint32_t frames;
    uint32_t destinationFrame;
    uint32_t channels;
    uint32_t sourceFrame;
    uint32_t sourceFraction;
    uint32_t stepFixed;
    uint32_t gainQ16;
    uint32_t sampleFormat;
} MeMixInput;

#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
typedef struct MeSfxVoice
{
    uint32_t segment0Offset;
    uint32_t segment0Frames;
    uint32_t segment1Offset;
    uint32_t segment1Frames;
    uint32_t sourceFrame;
    uint32_t sourceFraction;
    uint32_t stepFixed;
    uint32_t gainQ16;
} MeSfxVoice;

_Static_assert(sizeof(MeSfxVoice) == sizeof(Th07PspMeSfxVoice),
               "SC/ME SFX voice descriptor layout changed");
#endif

typedef struct MeSharedMailbox
{
    volatile uint32_t command;
    volatile uint32_t status;
    volatile uint32_t completedJobs;
    uint32_t reserved0;

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    volatile uint32_t workerState;
    volatile uint32_t commandResult;
    volatile uint32_t suspendRequested;
    uint32_t bgmGeneration;
    uint32_t bgmOffset;
    uint32_t bgmBytes;
    uint32_t bgmBufferPhys;
    volatile uint32_t stackFault;
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
    uint32_t sfxOffset0;
    uint32_t sfxBytes0;
    uint32_t sfxOffset1;
    uint32_t sfxBytes1;
    uint32_t sfxBufferPhys;
    uint32_t sfxFrames;
    uint32_t sfxVoiceCount;
    uint32_t reservedSfx;
    MeSfxVoice sfxVoices[TH07_PSP_ME_SFX_MAX_VOICES];
#endif
#endif

    uint32_t audioFrames;
    uint32_t audioInputCount;
    uint32_t audioMixDivisor;
    uint32_t audioOutputPhys;
    MeMixInput audioInputs[TH07_PSP_ME_MAX_MIX_INPUTS];

    uint32_t positionPhys;
    uint32_t texcoordPhys;
    uint32_t diffusePhys;
    uint32_t vertexOutputPhys;
    uint32_t positionStride;
    uint32_t texcoordStride;
    uint32_t diffuseStride;
    uint32_t vertexCount;
    uint32_t textured;
    uint32_t colored;
    uint32_t vertexOutputBytes;
    uint32_t reserved1;

#if defined(TH07_PSP_ME_RENDER_WORKER)
    uint32_t renderPadding[4];
    uint32_t renderVersion;
    uint32_t renderFlags;
    uint32_t renderFrameSeq;
    uint32_t renderTargetDrawSeq;
    uint32_t renderStageEpoch;
    uint32_t renderManagerEpoch;
    uint32_t renderReplayEpoch;
    uint32_t renderInputPhys;
    uint32_t renderInputBytes;
    uint32_t renderInputStride;
    uint32_t renderRecordCount;
    uint32_t renderOutputPhys;
    uint32_t renderOutputCapacity;
    volatile uint32_t renderOutputBytes;
    volatile uint32_t renderResult;
    volatile uint32_t renderInvalidateCycles;
    volatile uint32_t renderKernelCycles;
    volatile uint32_t renderWritebackCycles;
    volatile uint32_t renderFcr31Before;
    volatile uint32_t renderFcr31Effective;
    volatile uint32_t renderFcr31After;

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    // The M0 descriptor above ends 20 bytes into a cache line.  Keep I-ME1's
    // independently versioned transaction on a fresh line so the SC cannot
    // evict a completion while publishing an unrelated M0 job.
    uint32_t renderStreamPadding[11];
    uint32_t renderStreamVersion;
    uint32_t renderStreamFlags;
    uint32_t renderStreamSlot;
    uint32_t renderStreamGeneration;
    uint32_t renderStreamFrameSeq;
    uint32_t renderStreamTargetDrawSeq;
    uint32_t renderStreamStageEpoch;
    uint32_t renderStreamManagerEpoch;
    uint32_t renderStreamReplayEpoch;
    uint32_t renderStreamGlobalSignature;
    uint32_t renderStreamBucketEnds[6];
    uint32_t renderStreamRecordCount;
    uint32_t renderStreamPayloadHash;
    uint32_t renderStreamOffsetXBits;
    uint32_t renderStreamOffsetYBits;
    uint32_t renderStreamViewportLeftBits;
    uint32_t renderStreamViewportTopBits;
    uint32_t renderStreamViewportRightBits;
    uint32_t renderStreamViewportBottomBits;
    uint32_t renderStreamGlobalColor;
    uint32_t renderStreamConfigFlags;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    Th07PspMeRenderRawLayout renderStreamRawLayout;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    Th07PspMeRenderListLayout renderStreamListLayout;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    Th07PspMeRenderItemLayout renderStreamItemLayout;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    Th07PspMeRenderEffectLayout renderStreamEffectLayout;
#endif
#endif
#endif
#endif
    uint32_t renderStreamInputPhys;
    uint32_t renderStreamInputCapacity;
    uint32_t renderStreamOutputPhys;
    uint32_t renderStreamOutputCapacity;
    uint32_t renderStreamRunPhys;
    uint32_t renderStreamRunCapacity;
    volatile uint32_t renderStreamOutputBytes;
    volatile uint32_t renderStreamVertexCount;
    volatile uint32_t renderStreamRunCount;
    volatile uint32_t renderStreamOutputHash;
    volatile uint32_t renderStreamRunHash;
    volatile uint32_t renderStreamFirstBadRecord;
    volatile uint32_t renderStreamResult;
    volatile uint32_t renderStreamInvalidateCycles;
    volatile uint32_t renderStreamKernelCycles;
    volatile uint32_t renderStreamWritebackCycles;
    volatile uint32_t renderStreamFcr31Before;
    volatile uint32_t renderStreamFcr31Effective;
    volatile uint32_t renderStreamFcr31After;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    volatile uint32_t renderStreamItemResult;
    volatile uint32_t renderStreamItemRecordCount;
    volatile uint32_t renderStreamItemVertexCount;
    volatile uint32_t renderStreamItemRunCount;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    volatile uint32_t renderStreamEffectResult;
    volatile uint32_t renderStreamEffectLayer0RecordCount;
    volatile uint32_t renderStreamEffectLayer0VertexCount;
    volatile uint32_t renderStreamEffectLayer0RunCount;
    volatile uint32_t renderStreamEffectLayer3RecordCount;
    volatile uint32_t renderStreamEffectLayer3VertexCount;
    volatile uint32_t renderStreamEffectLayer3RunCount;
#endif
#endif
#endif
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    MeBulletFastMailbox bulletFast;
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    MeBulletCompactMailbox bulletCompact;
#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
    MeEdramSeedBenchMailbox edramSeedBench;
#endif
#endif
} MeSharedMailbox;

#if defined(TH07_PSP_ME_RENDER_WORKER)
_Static_assert((offsetof(MeSharedMailbox, renderVersion) & 63u) == 0u,
               "ME render mailbox descriptor must start on its own cache line");
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
_Static_assert((offsetof(MeSharedMailbox, renderStreamVersion) & 63u) == 0u,
               "I-ME1 mailbox descriptor must start on its own cache line");
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
_Static_assert((offsetof(MeSharedMailbox, bulletFast) & 63u) == 0u,
               "I-ME6 mailbox descriptor must start on its own cache line");
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
_Static_assert((offsetof(MeSharedMailbox, bulletCompact) & 63u) == 0u,
               "compact mailbox descriptor must start on its own cache line");
#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
_Static_assert((offsetof(MeSharedMailbox, edramSeedBench) & 63u) == 0u,
               "M0E mailbox descriptor must start on its own cache line");
#endif
#endif
#endif

static volatile MeSharedMailbox gMeMailbox __attribute__((aligned(64), section(".uncached")));
static volatile MeSharedMailbox *gMeMailboxUncached;
static short gMeAudioOutput[TH07_PSP_ME_MAX_MIX_FRAMES * 2] __attribute__((aligned(64)));
static int gScWide[TH07_PSP_ME_MAX_MIX_FRAMES * 2] __attribute__((aligned(64)));
static unsigned char gMeVertexArena[ME_VERTEX_ARENA_BYTES] __attribute__((aligned(64)));

#if defined(TH07_PSP_ME_RENDER_WORKER)
enum
{
    ME_RENDER_BENCH_INPUT_BYTES =
        TH07_PSP_ME_RENDER_MAX_RECORDS * 64,
    ME_RENDER_BENCH_OUTPUT_BYTES =
        TH07_PSP_ME_RENDER_MAX_RECORDS *
        TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD
};

static unsigned char gMeRenderBenchInputArea
    [ME_RENDER_BENCH_GUARD_BYTES + ME_RENDER_BENCH_INPUT_BYTES +
     ME_RENDER_BENCH_GUARD_BYTES] __attribute__((aligned(64)));
static unsigned char gMeRenderBenchOutputArea
    [ME_RENDER_BENCH_GUARD_BYTES + ME_RENDER_BENCH_OUTPUT_BYTES +
     ME_RENDER_BENCH_GUARD_BYTES] __attribute__((aligned(64)));
static unsigned char gMeRenderBenchCopyArea
    [ME_RENDER_BENCH_GUARD_BYTES + ME_RENDER_BENCH_OUTPUT_BYTES +
     ME_RENDER_BENCH_GUARD_BYTES] __attribute__((aligned(64)));
static unsigned char gMeRenderRuntimeInput[ME_RENDER_BENCH_INPUT_BYTES]
    __attribute__((aligned(64)));
static unsigned char gMeRenderRuntimeOutput[ME_RENDER_BENCH_OUTPUT_BYTES]
    __attribute__((aligned(64)));

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
typedef struct __attribute__((aligned(64))) MeBulletFastOutputArea
{
    unsigned char guard0[ME_BULLET_FAST_GUARD_BYTES];
    Th07PspMeBulletFastOutput output;
    unsigned char guard1[ME_BULLET_FAST_GUARD_BYTES];
} MeBulletFastOutputArea;

_Static_assert((offsetof(MeBulletFastOutputArea, output) & 63u) == 0u,
               "I-ME6 output must begin on an independent cache line");
_Static_assert((sizeof(MeBulletFastOutputArea) & 63u) == 0u,
               "I-ME6 guarded output must occupy whole cache lines");

static MeBulletFastOutputArea gMeBulletFastOutputArea
    __attribute__((aligned(64)));
static volatile unsigned int gMeBulletFastInFlight;
static Th07PspMeBulletFastJob gMeBulletFastPublishedJob;
static uint32_t gMeBulletFastStartUs;
static uint32_t gMeBulletFastScWritebackUs;
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
typedef struct __attribute__((aligned(64))) MeBulletCompactSeedArea
{
    unsigned char guard0[ME_BULLET_COMPACT_GUARD_BYTES];
    Th07PspMeBulletCompactSeed seed;
    unsigned char guard1[ME_BULLET_COMPACT_GUARD_BYTES];
} MeBulletCompactSeedArea;

typedef struct __attribute__((aligned(64))) MeBulletCompactOutputArea
{
    unsigned char guard0[ME_BULLET_COMPACT_GUARD_BYTES];
    Th07PspMeBulletCompactOutput output;
    unsigned char guard1[ME_BULLET_COMPACT_GUARD_BYTES];
} MeBulletCompactOutputArea;

_Static_assert((offsetof(MeBulletCompactSeedArea, seed) & 63u) == 0u,
               "compact seed must begin on an independent cache line");
_Static_assert((sizeof(MeBulletCompactSeedArea) & 63u) == 0u,
               "guarded compact seed must occupy whole cache lines");
_Static_assert((offsetof(MeBulletCompactOutputArea, output) & 63u) == 0u,
               "compact output must begin on an independent cache line");
_Static_assert((sizeof(MeBulletCompactOutputArea) & 63u) == 0u,
               "guarded compact output must occupy whole cache lines");

static MeBulletCompactSeedArea
    gMeBulletCompactSeedAreas[TH07_PSP_ME_BULLET_COMPACT_BANKS]
        __attribute__((aligned(64)));
static MeBulletCompactOutputArea gMeBulletCompactOutputArea
    __attribute__((aligned(64)));
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
typedef struct __attribute__((aligned(64))) MeItemMotionSeedArea
{
    unsigned char guard0[ME_BULLET_COMPACT_GUARD_BYTES];
    Th07PspMeItemMotionSeed seed;
    unsigned char guard1[ME_BULLET_COMPACT_GUARD_BYTES];
} MeItemMotionSeedArea;

typedef struct __attribute__((aligned(64))) MeItemMotionOutputArea
{
    unsigned char guard0[ME_BULLET_COMPACT_GUARD_BYTES];
    Th07PspMeItemMotionOutput output;
    unsigned char guard1[ME_BULLET_COMPACT_GUARD_BYTES];
} MeItemMotionOutputArea;

_Static_assert((offsetof(MeItemMotionSeedArea, seed) & 63u) == 0u &&
                   (sizeof(MeItemMotionSeedArea) & 63u) == 0u,
               "guarded Item motion seed must be cache-line aligned");
_Static_assert((offsetof(MeItemMotionOutputArea, output) & 63u) == 0u &&
                   (sizeof(MeItemMotionOutputArea) & 63u) == 0u,
               "guarded Item motion output must be cache-line aligned");

static MeItemMotionSeedArea
    gMeItemMotionSeedAreas[TH07_PSP_ME_ITEM_MOTION_BANKS]
        __attribute__((aligned(64)));
static MeItemMotionOutputArea gMeItemMotionOutputArea
    __attribute__((aligned(64)));
static volatile unsigned int gMeItemMotionOutputValid;
static volatile unsigned int gMeItemMotionEnabled;
// Startup alone may request an A1-MOVE command before the public availability
// gate opens.  Gameplay cannot run until audio init returns, so this marker is
// SC-only and never becomes a second production admission path.
static unsigned int gMeItemMotionSelftestInProgress;
static volatile unsigned int gMeItemMotionDiagState =
    TH07_PSP_ME_ITEM_MOTION_STATE_UNAVAILABLE;
static volatile unsigned int gMeItemMotionDiagReason =
    TH07_PSP_ME_ITEM_MOTION_REASON_ME_UNAVAILABLE;
static volatile unsigned int gMeItemMotionDiagSelftestRuns;
static volatile unsigned int gMeItemMotionDiagSelftestFailures;
static volatile unsigned int gMeItemMotionDiagBulletRetryRuns;
static volatile unsigned int gMeItemMotionDiagBulletRetryPasses;
static volatile int gMeItemMotionDiagLastPollResult = -1;
static volatile unsigned int gMeItemMotionDiagLastBulletResult =
    0xffffffffu;
static volatile unsigned int gMeItemMotionDiagLastItemResult =
    0xffffffffu;
static volatile unsigned int gMeItemMotionDiagFirstMismatchSlot =
    0xffffffffu;
#endif
static volatile unsigned int gMeBulletCompactInFlight;
static Th07PspMeBulletCompactJob gMeBulletCompactPublishedJob;
static uint32_t gMeBulletCompactStartUs;
static uint32_t gMeBulletCompactSeedInvalidateUs;
#endif

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
typedef struct MeRenderStreamInputArea
{
    unsigned char guard0[ME_RENDER_STREAM_GUARD_BYTES];
    Th07PspMeRenderStreamRecord records[TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS];
    unsigned char guard1[ME_RENDER_STREAM_GUARD_BYTES];
} MeRenderStreamInputArea;

typedef struct MeRenderStreamOutputArea
{
    unsigned char guard0[ME_RENDER_STREAM_GUARD_BYTES];
    Th07PspMeRenderStreamVertex vertices
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        [TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_VERTEX_BYTES /
#else
        [TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES /
#endif
         TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES];
    unsigned char guard1[ME_RENDER_STREAM_GUARD_BYTES];
} MeRenderStreamOutputArea;

typedef struct MeRenderStreamRunArea
{
    unsigned char guard0[ME_RENDER_STREAM_GUARD_BYTES];
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    Th07PspMeRenderStreamRun runs[TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_RUNS];
#else
    Th07PspMeRenderStreamRun runs[TH07_PSP_ME_RENDER_STREAM_MAX_RUNS];
#endif
    unsigned char guard1[ME_RENDER_STREAM_GUARD_BYTES];
} MeRenderStreamRunArea;

typedef struct MeRenderStreamSlotControl
{
    volatile unsigned int state;
    unsigned int generation;
    Th07PspMeRenderStreamJob publishedJob;
    Th07PspMeRenderStreamCompletion completion;
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    // Keep each SC-only control record on independent cache lines even as the
    // correctness-only completion ABI gains retire diagnostics.
    unsigned char cacheLinePadding[48];
#endif
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    // rawLayout appends 116 bytes to the published job (52 modulo one cache
    // line); keep independent slot controls from sharing SC cache lines.
    unsigned char rawCacheLinePadding[12];
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    // The Item job/layout extension is 128 bytes and the segment-local
    // completion adds 16. Restore the per-slot cache-line multiple.
    unsigned char itemCacheLinePadding[48];
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    // Effect adds one 128-byte layout and seven completion words. Keep every
    // token control on whole, independent cache lines.
    unsigned char effectCacheLinePadding[36];
#endif
#endif
} MeRenderStreamSlotControl;

_Static_assert((sizeof(MeRenderStreamSlotControl) & 63u) == 0u,
               "I-ME1 slot control must occupy whole cache lines");

static MeRenderStreamInputArea
    gMeRenderStreamInputAreas[TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT]
        __attribute__((aligned(64)));
static MeRenderStreamOutputArea
    gMeRenderStreamOutputAreas[TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT]
        __attribute__((aligned(64)));
static MeRenderStreamRunArea
    gMeRenderStreamRunAreas[TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT]
        __attribute__((aligned(64)));
static MeRenderStreamSlotControl
    gMeRenderStreamSlots[TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT]
        __attribute__((aligned(64)));
static volatile unsigned int gMeRenderStreamInFlightSlot = 0xffffffffu;
static volatile unsigned int gMeRenderStreamSubmitted;
static volatile unsigned int gMeRenderStreamCompleted;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
static volatile unsigned int gMeItemRenderEnabled;
// SC-only startup marker.  It remains set when an early return came from the
// optional Item subtest, allowing init to distinguish that clean failure from
// a Bullet/owner failure that must still stop the worker.
static unsigned int gMeItemSelftestInProgress;
// Process-lifetime diagnostic decision.  SHIKIGAMI reads only these atomics;
// it never joins a boot log to infer whether Item was admitted or failed safe.
static volatile unsigned int gMeItemDiagState =
    TH07_PSP_ME_ITEM_STATE_UNAVAILABLE;
static volatile unsigned int gMeItemDiagReason =
    TH07_PSP_ME_ITEM_REASON_ME_UNAVAILABLE;
static volatile unsigned int gMeItemDiagSelftestRuns;
static volatile unsigned int gMeItemDiagSelftestFailures;
static volatile unsigned int gMeItemDiagBulletRetryRuns;
static volatile unsigned int gMeItemDiagBulletRetryPasses;
static volatile int gMeItemDiagLastWaitResult;
static volatile unsigned int gMeItemDiagLastStreamResult;
static volatile unsigned int gMeItemDiagLastItemResult;
#endif
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
static volatile unsigned int gMeRenderStreamDraining;
#endif
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
static volatile unsigned int gMeRenderStreamRetireDiagLogged;
#endif
static uint32_t gMeRenderStreamStartUs;
static uint32_t gMeRenderStreamScWritebackUs;
static uint32_t gMeRenderStreamScOutputPrepareUs;
static uint32_t gMeRenderStreamScSubmitUs;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
// Small boot-test objects.  Their descriptors deliberately advertise the
// production pool strides/counts, but the single exercised slot/element is
// fully backed here; no test access can escape these exact objects.
static unsigned char gMeRenderRawSelftestBullet[ME_RENDER_RAW_BULLET_STRIDE]
    __attribute__((aligned(64)));
static unsigned char gMeRenderRawSelftestSprite[ME_RENDER_RAW_SPRITE_BYTES]
    __attribute__((aligned(64)));
static uint16_t gMeRenderRawSelftestRepresentatives
    [TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT]
        __attribute__((aligned(64)));
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
static uint32_t gMeRenderListSelftestGeneration[1]
    __attribute__((aligned(64)));
static uint32_t gMeRenderListSelftestActiveBits[1]
    __attribute__((aligned(64)));
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
static unsigned char gMeRenderItemSelftestItem[ME_RENDER_ITEM_STRIDE * 2u]
    __attribute__((aligned(64)));
static uint32_t gMeRenderItemSelftestGeneration[2]
    __attribute__((aligned(64)));
static uint32_t gMeRenderItemSelftestActiveBits[1]
    __attribute__((aligned(64)));
static uint32_t gMeRenderItemSelftestSin[2]
    __attribute__((aligned(64)));
static uint32_t gMeRenderItemSelftestCos[2]
    __attribute__((aligned(64)));
static uint32_t gMeRenderItemSelftestPrepareSerial
    __attribute__((aligned(64)));
static uint32_t gMeRenderItemSelftestPreparedSerial
    __attribute__((aligned(64)));
static uint32_t gMeRenderItemSelftestPreparedCount
    __attribute__((aligned(64)));
#endif
#endif
#endif
#endif

static Th07PspMeRenderBenchSummary gMeRenderBenchSummary;
static Th07PspMeRenderBenchCase
    gMeRenderBenchCases[TH07_PSP_ME_RENDER_BENCH_CASES];
static volatile unsigned int gMeRenderInFlight;
static volatile unsigned int gMeRenderSubmitted;
static volatile unsigned int gMeRenderCompleted;
static uint32_t gMeRenderStartUs;
static uint32_t gMeRenderScWritebackUs;
static uint32_t gMeRenderScOutputPrepareUs;
static uint32_t gMeRenderScSubmitUs;
static Th07PspMeRenderJob gMeRenderPublishedJob;

_Static_assert(ME_RENDER_BENCH_INPUT_BYTES == 65536u,
               "M0 input upper bound changed");
_Static_assert(ME_RENDER_BENCH_OUTPUT_BYTES == 98304u,
               "M0 output upper bound changed");
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
_Static_assert(sizeof(((MeRenderStreamInputArea *)0)->records) == 65536u,
               "I-ME1 input pool must remain 64 KiB per slot");
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
_Static_assert(sizeof(((MeRenderStreamOutputArea *)0)->vertices) ==
                   TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_VERTEX_BYTES,
               "I-ME7 combined Item/Bullet output pool changed");
_Static_assert(sizeof(((MeRenderStreamRunArea *)0)->runs) ==
                   TH07_PSP_ME_RENDER_STREAM_TOTAL_MAX_RUNS *
                       sizeof(Th07PspMeRenderStreamRun),
               "I-ME7 combined Item/Bullet run pool changed");
#else
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
_Static_assert(sizeof(((MeRenderStreamOutputArea *)0)->vertices) ==
                   TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES,
               "render-stream output pool does not match its ABI capacity");
#else
_Static_assert(sizeof(((MeRenderStreamOutputArea *)0)->vertices) == 98304u,
               "I-ME1 output pool must remain 96 KiB per slot");
#endif
_Static_assert(sizeof(((MeRenderStreamRunArea *)0)->runs) == 32768u,
               "I-ME1 run pool must remain 32 KiB per slot");
#endif
#endif
#endif

#if defined(TH07_PSP_MECC_AUDIO_4M)
unsigned char gTh07MeMainStackArea[TH07_ME_MAIN_STACK_AREA_BYTES]
    __attribute__((aligned(64)));
static int gMeAudioWide[ME_AUDIO_MAIN_ACCUM_FRAMES * 2] __attribute__((aligned(64)));

_Static_assert(sizeof(gTh07MeMainStackArea) == 8320u,
               "guarded Main-RAM ME stack area changed");
_Static_assert(sizeof(gMeAudioWide) == 2048u,
               "Main-RAM ME accumulator allocation changed");
#endif

static volatile int gMeActive;
static volatile int gMeStarted;
static volatile int gMePoisoned;
static volatile int gMeOwner;
static volatile unsigned int gMeAudioWanted;
static unsigned int gMeVertexArenaOffset;
#if defined(TH07_PSP_MECC_AUDIO_4M)
static volatile SceUID gMeOwnerThread = -1;
static volatile int gMeOwnerOriginalPriority = -1;
#endif

static volatile unsigned int gMeJobs;
static volatile unsigned int gMeFallbacks;
static volatile unsigned int gMeTimeouts;
static volatile unsigned int gMeMaxWaitUs;

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
static volatile int gMeResetCommitted;
static volatile int gMeBgmOwned;
static volatile int gMeUnsafe;

#if defined(TH07_PSP_MECC_AUDIO_4M)
enum
{
    ME_STACK_LOWER_GUARD_PATTERN = 0xa5,
    ME_STACK_UPPER_GUARD_PATTERN = 0x5a,
    ME_STACK_UNUSED_PATTERN = 0xcd
};

static volatile int gMePowerLocked;
static volatile int gMePowerLockOwned;

static int stack_guards_match_on_sc(void);

static int acquire_power_lock(void)
{
    if (__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE))
        return 0;
    // When the optional GE4 portrait cache is enabled it owns the process-wide
    // suspend lock first.  The boot-rescue profile omits that experimental
    // sidecar and therefore takes the established AUDIO4M lock directly.
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    if (th07_psp_ge4_power_lock_held())
    {
        __atomic_store_n(&gMePowerLockOwned, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&gMePowerLocked, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M POWER LOCK BORROWED FROM GE4");
        return 1;
    }
#endif
    const int result = scePowerLock(0);
    if (result != 0)
    {
        // PSP SDK documents only zero success and negative failure.  A
        // positive return violates that contract, so conservatively remember
        // that the kernel lock state may have changed and never try takeover.
        if (result > 0)
        {
            __atomic_store_n(&gMePowerLockOwned, 1, __ATOMIC_RELEASE);
            __atomic_store_n(&gMePowerLocked, 1, __ATOMIC_RELEASE);
        }
        return 0;
    }
    __atomic_store_n(&gMePowerLockOwned, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&gMePowerLocked, 1, __ATOMIC_RELEASE);
    th07_psp_boot_note("MECC AUDIO4M POWER LOCKED");
    return 1;
}

static int release_power_lock_after_stop(void)
{
    if (!__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE))
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M POWER LOCK MISSING -> COLD REBOOT");
        return 0;
    }
    // This is the sole unlock site.  Re-prove the entire terminal packet here
    // rather than relying only on the caller's earlier observations.  A
    // suspend request seen while the switch is supposedly locked is evidence
    // of an interrupted ownership window and therefore remains cold-off.
    if (__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMeOwner, __ATOMIC_ACQUIRE) != ME_OWNER_SHUTDOWN ||
        !gMeMailboxUncached ||
        gMeMailboxUncached->workerState != ME_WORKER_STOPPED ||
        gMeMailboxUncached->command != ME_CMD_NONE ||
        gMeMailboxUncached->status != ME_STAT_DONE ||
        gMeMailboxUncached->suspendRequested != 0u ||
        gMeMailboxUncached->stackFault || !stack_guards_match_on_sc())
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M POWER RELEASE GATE NG -> COLD REBOOT");
        return 0;
    }
    if (__atomic_load_n(&gMePowerLockOwned, __ATOMIC_ACQUIRE))
    {
        const int result = scePowerUnlock(0);
        if (result != 0)
        {
            // The actual kernel lock state is uncertain after an unexpected
            // return. Retain the software latch and require a cold reboot.
            __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
            th07_psp_boot_note("MECC AUDIO4M POWER UNLOCK FAILED -> COLD REBOOT");
            return 0;
        }
        th07_psp_boot_note("MECC AUDIO4M POWER UNLOCKED AFTER STOP/GUARD");
    }
    else
    {
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        // GE remains active through renderer teardown and releases the sole
        // lock only after sceGuTerm and the mandatory aperture restore.
        if (!th07_psp_ge4_power_lock_held())
        {
            __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
            th07_psp_boot_note("MECC AUDIO4M BORROWED LOCK LOST -> COLD REBOOT");
            return 0;
        }
        th07_psp_boot_note("MECC AUDIO4M POWER LOCK RETURNED TO GE4");
#else
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M POWER LOCK OWNERSHIP LOST -> COLD REBOOT");
        return 0;
#endif
    }
    __atomic_store_n(&gMePowerLocked, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gMePowerLockOwned, 0, __ATOMIC_RELEASE);
    return 1;
}

static int stack_guards_match(const volatile unsigned char *area)
{
    for (uint32_t index = 0; index < TH07_ME_MAIN_STACK_GUARD_BYTES; ++index)
    {
        if (area[index] != ME_STACK_LOWER_GUARD_PATTERN)
            return 0;
    }
    const uint32_t upper = TH07_ME_MAIN_STACK_GUARD_BYTES + TH07_ME_MAIN_STACK_BYTES;
    for (uint32_t index = 0; index < TH07_ME_MAIN_STACK_GUARD_BYTES; ++index)
    {
        if (area[upper + index] != ME_STACK_UPPER_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int stack_guards_match_on_me(void)
{
    const volatile unsigned char *area =
        (const volatile unsigned char *)(0x80000000u |
                                         (uint32_t)gTh07MeMainStackArea);
    return stack_guards_match(area);
}

static int stack_guards_match_on_sc(void)
{
    sceKernelDcacheInvalidateRange(gTh07MeMainStackArea,
                                  TH07_ME_MAIN_STACK_GUARD_BYTES);
    sceKernelDcacheInvalidateRange(
        gTh07MeMainStackArea + TH07_ME_MAIN_STACK_GUARD_BYTES +
            TH07_ME_MAIN_STACK_BYTES,
        TH07_ME_MAIN_STACK_GUARD_BYTES);
    return stack_guards_match(gTh07MeMainStackArea);
}

static void initialize_main_stack(void)
{
    memset(gTh07MeMainStackArea, ME_STACK_UNUSED_PATTERN,
           sizeof(gTh07MeMainStackArea));
    memset(gTh07MeMainStackArea, ME_STACK_LOWER_GUARD_PATTERN,
           TH07_ME_MAIN_STACK_GUARD_BYTES);
    memset(gTh07MeMainStackArea + TH07_ME_MAIN_STACK_GUARD_BYTES +
               TH07_ME_MAIN_STACK_BYTES,
           ME_STACK_UPPER_GUARD_PATTERN, TH07_ME_MAIN_STACK_GUARD_BYTES);
    sceKernelDcacheWritebackInvalidateRange(gTh07MeMainStackArea,
                                           sizeof(gTh07MeMainStackArea));
}
#endif

// Replace MECC's weak no-op suspend hooks.  A live custom worker cannot be
// handed back to Sony T2 after resume, so suspend is a one-way safety latch.
__attribute__((noinline, aligned(4)))
void meLibOnSleep(void)
{
    volatile MeSharedMailbox *box =
        (volatile MeSharedMailbox *)(0x40000000u | (uint32_t)&gMeMailbox);
    box->suspendRequested = 1;
    __asm__ volatile("sync");
#if defined(TH07_PSP_MECC_AUDIO_4M)
    // Normally scePowerLock prevents this callback until clean shutdown has
    // already cleared gMeStarted.  If it arrives while the worker is live,
    // latch the violation here as well as in the application callback so the
    // low-level layer is independently fail-closed.
    if (__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE))
    {
        __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&gMePoisoned, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
    }
#endif
}

__attribute__((noinline, aligned(4)))
void meLibOnWake(void)
{
}
#endif

static int running_under_ppsspp(void)
{
    // Test before meLibDefaultInit(): PPSSPP v1.20 can block inside the MECC
    // bridge instead of returning an error that the fallback can consume.
    SceIoStat stat;
    return sceIoGetstat("ms0:/PSP/SYSTEM/ppsspp.ini", &stat) >= 0;
}

static int me_disabled_marker_present(void)
{
    const char *gameDir = th07_psp_game_dir();
    char path[256];
    SceIoStat stat;
    if (!gameDir || snprintf(path, sizeof(path), "%s/TH07PSP_ME.OFF", gameDir) < 0)
        return 0;
    return sceIoGetstat(path, &stat) >= 0;
}

static void record_max(volatile unsigned int *value, unsigned int sample)
{
    unsigned int old = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (sample > old &&
           !__atomic_compare_exchange_n(value, &old, sample, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
    {
    }
}

static uint32_t vertex_bytes(uint32_t textured, uint32_t colored)
{
    if (textured)
        return colored ? sizeof(MeVertexTexColorPosition) : sizeof(MeVertexTexPosition);
    return colored ? sizeof(MeVertexColorPosition) : sizeof(MeVertexPosition);
}

// ME has no usable FPU/VFPU contract.  Vertex floats remain opaque IEEE-754
// bits.  Byte assembly prevents GCC from emitting lwc1/swc1 and also accepts
// independently-strided engine attributes that are not naturally aligned.
static uint32_t load_u32_bits(const unsigned char *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

#if defined(TH07_PSP_ME_RENDER_WORKER)
static __attribute__((always_inline)) inline float me_render_bits_float(uint32_t bits)
{
    union
    {
        uint32_t bits;
        float value;
    } convert;
    convert.bits = bits;
    return convert.value;
}

static __attribute__((always_inline)) inline uint32_t me_render_float_bits(float value)
{
    union
    {
        float value;
        uint32_t bits;
    } convert;
    convert.value = value;
    return convert.bits;
}

static __attribute__((always_inline)) inline uint32_t me_render_read_fcr31(void)
{
    uint32_t value;
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    __asm__ volatile("cfc1 %0, $31" : "=r"(value) : : "memory");
#else
    __asm__ volatile("cfc1 %0, $31" : "=r"(value));
#endif
    return value;
}

static __attribute__((always_inline)) inline void me_render_write_fcr31(uint32_t value)
{
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    uint32_t drained;
    // A render kernel may return with older scalar-FPU work still in flight.
    // CFC1 is the architectural pipeline drain required before changing
    // FCR31.  Keep the exact post-write readback gates: C5's hardware value
    // 0x00003351 equals Allegrex FCR0/FIR rather than a plausible FCR31, so
    // this ordering repair must be verified on hardware and must never be
    // replaced by masking or accepting selected control/status bits.
    __asm__ volatile(
        "cfc1 %0, $31\n\t"
        "nop\n\t"
        "ctc1 %1, $31\n\t"
        "nop\n\t"
        "nop"
        : "=&r"(drained)
        : "r"(value)
        : "memory");
#else
    __asm__ volatile("ctc1 %0, $31\n\tnop\n\tnop" : : "r"(value) : "memory");
#endif
}

static __attribute__((always_inline)) inline uint32_t me_render_read_count(void)
{
    uint32_t value;
    __asm__ volatile("mfc0 %0, $9" : "=r"(value));
    return value;
}

static __attribute__((always_inline)) inline void me_render_start_count(void)
{
    // The ME's CP0 Count register does not advance after reset until it has
    // been initialized.  Every render/update timing sample used to read zero
    // on real hardware because the custom worker never performed that write.
    // IP7 remains disabled; Count is used only as a free-running half-clock
    // counter and per-job unsigned deltas remain valid across wraparound.
    __asm__ volatile("mtc0 $0, $9\n\tsync\n\tnop\n\tnop" : : : "memory");
}

static __attribute__((always_inline)) inline void me_render_idle_backoff(void)
{
    for (uint32_t round = 0u; round < ME_RENDER_IDLE_BACKOFF_ROUNDS; ++round)
    {
        // Local pipeline work only: no syscall, cache operation or shared-bus
        // access belongs in the worker's idle path.
        __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop;");
    }
}

static int me_render_main_ram_range_valid(uint32_t physical, uint32_t bytes)
{
    const uint32_t begin = 0x08000000u;
    const uint32_t end = 0x0c000000u;
    return physical >= begin && physical < end &&
           bytes <= end - physical;
}

static int me_render_ranges_overlap(uint32_t first, uint32_t firstBytes,
                                    uint32_t second, uint32_t secondBytes)
{
    if (!firstBytes || !secondBytes)
        return 0;
    return first < second + secondBytes && second < first + firstBytes;
}

static int me_render_owned_pool_pair_valid(uint32_t inputPhys,
                                           uint32_t inputCapacity,
                                           uint32_t outputPhys,
                                           uint32_t outputCapacity)
{
    const uint32_t benchInput =
        (uint32_t)(gMeRenderBenchInputArea + ME_RENDER_BENCH_GUARD_BYTES) &
        0x1fffffffu;
    const uint32_t benchOutput =
        (uint32_t)(gMeRenderBenchOutputArea + ME_RENDER_BENCH_GUARD_BYTES) &
        0x1fffffffu;
    const uint32_t runtimeInput =
        (uint32_t)gMeRenderRuntimeInput & 0x1fffffffu;
    const uint32_t runtimeOutput =
        (uint32_t)gMeRenderRuntimeOutput & 0x1fffffffu;
    const int exactPair =
        (inputPhys == benchInput && outputPhys == benchOutput) ||
        (inputPhys == runtimeInput && outputPhys == runtimeOutput);
    return exactPair && inputCapacity <= ME_RENDER_BENCH_INPUT_BYTES &&
           outputCapacity <= ME_RENDER_BENCH_OUTPUT_BYTES;
}

static int me_render_bounds_valid(uint32_t version,
                                  uint32_t inputPhys, uint32_t inputBytes,
                                  uint32_t inputStride, uint32_t recordCount,
                                  uint32_t outputPhys, uint32_t outputCapacity,
                                  uint32_t *requiredInput,
                                  uint32_t *requiredOutput)
{
    if (requiredInput)
        *requiredInput = 0;
    if (requiredOutput)
        *requiredOutput = 0;
    if (version != TH07_PSP_ME_RENDER_VERSION ||
        (inputStride != 32u && inputStride != 48u && inputStride != 64u) ||
        recordCount > TH07_PSP_ME_RENDER_MAX_RECORDS ||
        (inputPhys & 63u) != 0u || (outputPhys & 63u) != 0u)
        return 0;

    const uint32_t inputNeeded = recordCount * inputStride;
    const uint32_t outputNeeded =
        recordCount * TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
    if (inputBytes < inputNeeded || outputCapacity < outputNeeded ||
        !me_render_owned_pool_pair_valid(inputPhys, inputBytes, outputPhys,
                                         outputCapacity) ||
        !me_render_main_ram_range_valid(inputPhys, inputNeeded) ||
        !me_render_main_ram_range_valid(outputPhys, outputNeeded) ||
        me_render_ranges_overlap(inputPhys, inputNeeded,
                                 outputPhys, outputNeeded))
        return 0;
    if (requiredInput)
        *requiredInput = inputNeeded;
    if (requiredOutput)
        *requiredOutput = outputNeeded;
    return 1;
}

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
static uint32_t me_render_stream_hash_bytes(const void *data, uint32_t bytes)
{
    const unsigned char *source = (const unsigned char *)data;
    uint32_t hash = 2166136261u;
    for (uint32_t index = 0u; index < bytes; ++index)
    {
        hash ^= source[index];
        hash *= 16777619u;
    }
    return hash;
}

static int me_render_stream_float_bits_finite(uint32_t bits)
{
    return (bits & 0x7f800000u) != 0x7f800000u;
}

#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
static uint32_t me_render_stream_load_u32(const unsigned char *base,
                                         uint32_t offset)
{
    // RAW layout validation proves every live field is naturally aligned.
    // Keep the generic byte loader for arbitrary stream attributes, but let
    // Allegrex use one aligned lw for the hot VM/sprite gather path.
    const void *source = __builtin_assume_aligned(base + offset, 4u);
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    return value;
}

static uint32_t me_render_stream_load_u16(const unsigned char *base,
                                         uint32_t offset)
{
    const void *source = __builtin_assume_aligned(base + offset, 2u);
    uint16_t value;
    memcpy(&value, source, sizeof(value));
    return value;
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
static const volatile unsigned char *me_render_stream_item_uncached(
    uint32_t physical)
{
    return (const volatile unsigned char *)(0x40000000u | physical);
}

static uint32_t me_render_stream_item_load_u32(
    const volatile unsigned char *base, uint32_t offset)
{
    // Item layout validation proves natural alignment.  Keep the volatile
    // load explicit: generation/authority brackets must not be CSE'd or
    // hoisted while ME observes SC-published Main RAM through KSEG uncached.
    const volatile uint32_t *source =
        (const volatile uint32_t *)(base + offset);
    return *source;
}

static uint32_t me_render_stream_item_load_u8(
    const volatile unsigned char *base, uint32_t offset)
{
    return base[offset];
}
#endif

static int me_render_stream_raw_field_valid(uint32_t objectBytes,
                                            uint32_t offset,
                                            uint32_t fieldBytes)
{
    return fieldBytes != 0u && (offset & (fieldBytes - 1u)) == 0u &&
           offset <= objectBytes && fieldBytes <= objectBytes - offset;
}

static int me_render_stream_raw_pool_valid(uint32_t base, uint32_t stride,
                                           uint32_t count,
                                           uint32_t elementBytes,
                                           uint32_t alignment)
{
    if (!base || !stride || !count || !elementBytes ||
        (base & (alignment - 1u)) != 0u ||
        (stride & (alignment - 1u)) != 0u || stride < elementBytes ||
        count - 1u > (UINT32_MAX - elementBytes) / stride)
        return 0;
    const uint32_t bytes = (count - 1u) * stride + elementBytes;
    return me_render_main_ram_range_valid(base, bytes);
}

static int me_render_stream_raw_layout_valid(
    const Th07PspMeRenderRawLayout *layout)
{
    if (!layout)
        return 0;
    const int selftestLayout =
        layout->rawLayoutVersion == ME_RENDER_RAW_LAYOUT_SELFTEST_VERSION;
    if ((!selftestLayout &&
         layout->rawLayoutVersion != TH07_PSP_ME_RENDER_RAW_LAYOUT_VERSION) ||
        layout->rawRecordBytes != TH07_PSP_ME_RENDER_STREAM_RAW_RECORD_BYTES ||
        layout->bulletStride != ME_RENDER_RAW_BULLET_STRIDE ||
        layout->bulletCount !=
            (selftestLayout ? 1u : ME_RENDER_RAW_BULLET_COUNT) ||
        layout->vmBytes != ME_RENDER_RAW_VM_BYTES ||
        layout->vmRotationZOffset != ME_RENDER_RAW_VM_ROTATION_Z_OFFSET ||
        layout->vmScaleXOffset != ME_RENDER_RAW_VM_SCALE_X_OFFSET ||
        layout->vmScaleYOffset != ME_RENDER_RAW_VM_SCALE_Y_OFFSET ||
        layout->vmUvScrollXOffset != ME_RENDER_RAW_VM_UV_SCROLL_X_OFFSET ||
        layout->vmUvScrollYOffset != ME_RENDER_RAW_VM_UV_SCROLL_Y_OFFSET ||
        layout->vmColorOffset != ME_RENDER_RAW_VM_COLOR_OFFSET ||
        layout->vmColor2Offset != ME_RENDER_RAW_VM_COLOR2_OFFSET ||
        layout->vmFlagsOffset != ME_RENDER_RAW_VM_FLAGS_OFFSET ||
        layout->vmSpriteOffset != ME_RENDER_RAW_VM_SPRITE_OFFSET ||
        layout->spriteBytes != ME_RENDER_RAW_SPRITE_BYTES ||
        layout->spriteStride != ME_RENDER_RAW_SPRITE_BYTES ||
        layout->spriteCount !=
            (selftestLayout ? 1u : ME_RENDER_RAW_SPRITE_COUNT) ||
        layout->spriteSourceOffset != ME_RENDER_RAW_SPRITE_SOURCE_OFFSET ||
        layout->spriteUvStartXOffset !=
            ME_RENDER_RAW_SPRITE_UV_START_X_OFFSET ||
        layout->spriteUvStartYOffset !=
            ME_RENDER_RAW_SPRITE_UV_START_Y_OFFSET ||
        layout->spriteUvEndXOffset != ME_RENDER_RAW_SPRITE_UV_END_X_OFFSET ||
        layout->spriteUvEndYOffset != ME_RENDER_RAW_SPRITE_UV_END_Y_OFFSET ||
        layout->spriteHeightOffset != ME_RENDER_RAW_SPRITE_HEIGHT_OFFSET ||
        layout->spriteWidthOffset != ME_RENDER_RAW_SPRITE_WIDTH_OFFSET ||
        layout->representativeStride != sizeof(uint16_t) ||
        layout->representativeCount !=
            TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT ||
        (selftestLayout &&
         (layout->bulletBasePhys !=
              ((uint32_t)gMeRenderRawSelftestBullet & 0x1fffffffu) ||
          layout->spriteBasePhys !=
              ((uint32_t)gMeRenderRawSelftestSprite & 0x1fffffffu) ||
          layout->representativePhys !=
              ((uint32_t)gMeRenderRawSelftestRepresentatives &
               0x1fffffffu))) ||
        !me_render_stream_raw_pool_valid(
            layout->bulletBasePhys, layout->bulletStride,
            layout->bulletCount, layout->bulletStride, sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->spriteBasePhys, layout->spriteStride,
            layout->spriteCount, layout->spriteBytes, sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->representativePhys, layout->representativeStride,
            layout->representativeCount, sizeof(uint16_t), sizeof(uint16_t)))
        return 0;

    const uint32_t bulletPoolBytes =
        (layout->bulletCount - 1u) * layout->bulletStride +
        layout->bulletStride;
    const uint32_t spritePoolBytes =
        (layout->spriteCount - 1u) * layout->spriteStride +
        layout->spriteBytes;
    const uint32_t representativeBytes =
        (layout->representativeCount - 1u) *
            layout->representativeStride + sizeof(uint16_t);
    if (me_render_ranges_overlap(
            layout->bulletBasePhys, bulletPoolBytes,
            layout->spriteBasePhys, spritePoolBytes) ||
        me_render_ranges_overlap(
            layout->bulletBasePhys, bulletPoolBytes,
            layout->representativePhys, representativeBytes) ||
        me_render_ranges_overlap(
            layout->spriteBasePhys, spritePoolBytes,
            layout->representativePhys, representativeBytes))
        return 0;

    return me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmRotationZOffset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmScaleXOffset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmScaleYOffset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmUvScrollXOffset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmUvScrollYOffset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmColorOffset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmColor2Offset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmFlagsOffset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->vmBytes, layout->vmSpriteOffset, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->spriteBytes, layout->spriteSourceOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->spriteBytes, layout->spriteUvStartXOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->spriteBytes, layout->spriteUvStartYOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->spriteBytes, layout->spriteUvEndXOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->spriteBytes, layout->spriteUvEndYOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->spriteBytes, layout->spriteHeightOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->spriteBytes, layout->spriteWidthOffset,
               sizeof(uint32_t));
}

#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
static int me_render_stream_list_bullet_physical(
    uint32_t pointer, const Th07PspMeRenderListLayout *layout,
    uint32_t *physical, uint32_t *slot)
{
    if (physical)
        *physical = 0u;
    if (slot)
        *slot = 0xffffffffu;
    if (!pointer || !layout || !physical || !slot)
        return 0;
    const uint32_t phys = pointer & 0x1fffffffu;
    if (phys < layout->bulletBasePhys)
        return 0;
    const uint32_t delta = phys - layout->bulletBasePhys;
    // Layout admission already proves bulletStride is the frozen PSP ABI.
    // Thus `delta % layout->bulletStride` is exactly the constant operation
    // below, which avoids two variable DIVU hazards for every live list node.
    if (layout->bulletStride != ME_RENDER_RAW_BULLET_STRIDE ||
        (phys & 3u) != 0u || delta % ME_RENDER_RAW_BULLET_STRIDE != 0u)
        return 0;
    const uint32_t slotIndex = delta / ME_RENDER_RAW_BULLET_STRIDE;
    if (slotIndex >= layout->bulletCount ||
        !me_render_main_ram_range_valid(
            phys, ME_RENDER_RAW_BULLET_STRIDE))
        return 0;
    *physical = phys;
    *slot = slotIndex;
    return 1;
}

static int me_render_stream_list_layout_valid(
    const Th07PspMeRenderListLayout *layout,
    const Th07PspMeRenderRawLayout *rawLayout)
{
    if (!layout || !rawLayout)
        return 0;
    const int selftestLayout =
        layout->listLayoutVersion == ME_RENDER_LIST_LAYOUT_SELFTEST_VERSION;
    const uint32_t expectedBulletCount = selftestLayout
        ? 1u : ME_RENDER_RAW_BULLET_COUNT;
    const uint32_t expectedGenerationCount = expectedBulletCount;
    const uint32_t expectedActiveWords = selftestLayout
        ? 1u : ME_RENDER_LIST_ACTIVE_WORD_COUNT;
    if ((!selftestLayout &&
         layout->listLayoutVersion !=
             TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION) ||
        layout->listLayoutBytes != sizeof(*layout) ||
        layout->bulletBasePhys != rawLayout->bulletBasePhys ||
        layout->bulletStride != ME_RENDER_RAW_BULLET_STRIDE ||
        layout->bulletStride != rawLayout->bulletStride ||
        layout->bulletCount != expectedBulletCount ||
        layout->bulletCount != rawLayout->bulletCount ||
        layout->generationStride != ME_RENDER_LIST_GENERATION_STRIDE ||
        layout->generationCount != expectedGenerationCount ||
        layout->activeBitsWordCount != expectedActiveWords ||
        layout->bulletNextOffset != ME_RENDER_LIST_BULLET_NEXT_OFFSET ||
        layout->bulletStateOffset != ME_RENDER_LIST_BULLET_STATE_OFFSET ||
        layout->bulletCollisionTypeOffset !=
            ME_RENDER_LIST_BULLET_COLLISION_TYPE_OFFSET ||
        layout->bulletPosXOffset != ME_RENDER_LIST_BULLET_POS_X_OFFSET ||
        layout->bulletPosYOffset != ME_RENDER_LIST_BULLET_POS_Y_OFFSET ||
        layout->bulletRenderAngleOffset !=
            ME_RENDER_LIST_BULLET_RENDER_ANGLE_OFFSET ||
        layout->bulletSinOffset != ME_RENDER_LIST_BULLET_SIN_OFFSET ||
        layout->bulletCosOffset != ME_RENDER_LIST_BULLET_COS_OFFSET ||
        layout->bulletRotationValidOffset !=
            ME_RENDER_LIST_BULLET_ROTATION_VALID_OFFSET ||
        !me_render_stream_float_bits_finite(layout->arcadeLeftBits) ||
        !me_render_stream_float_bits_finite(layout->arcadeTopBits))
        return 0;

    for (uint32_t state = 0u; state < 5u; ++state)
    {
        if (layout->bulletVmOffsets[state] !=
                state * ME_RENDER_RAW_VM_BYTES ||
            layout->bulletVmOffsets[state] > layout->bulletStride ||
            rawLayout->vmBytes >
                layout->bulletStride - layout->bulletVmOffsets[state])
            return 0;
    }

    if (!me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletNextOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletStateOffset,
            sizeof(uint16_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletCollisionTypeOffset,
            sizeof(uint8_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletPosXOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletPosYOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletRenderAngleOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletSinOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletCosOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletRotationValidOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->generationBasePhys, layout->generationStride,
            layout->generationCount, sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->activeBitsPhys, sizeof(uint32_t),
            layout->activeBitsWordCount, sizeof(uint32_t), sizeof(uint32_t)))
        return 0;

    if (selftestLayout &&
        (layout->generationBasePhys !=
             ((uint32_t)gMeRenderListSelftestGeneration & 0x1fffffffu) ||
         layout->activeBitsPhys !=
             ((uint32_t)gMeRenderListSelftestActiveBits & 0x1fffffffu)))
        return 0;

    const uint32_t bulletBytes =
        layout->bulletCount * layout->bulletStride;
    const uint32_t generationBytes =
        layout->generationCount * layout->generationStride;
    const uint32_t activeBytes =
        layout->activeBitsWordCount * sizeof(uint32_t);
    const uint32_t spriteBytes =
        (rawLayout->spriteCount - 1u) * rawLayout->spriteStride +
        rawLayout->spriteBytes;
    const uint32_t representativeBytes =
        (rawLayout->representativeCount - 1u) *
            rawLayout->representativeStride + sizeof(uint16_t);
    if (me_render_ranges_overlap(
            layout->bulletBasePhys, bulletBytes,
            layout->generationBasePhys, generationBytes) ||
        me_render_ranges_overlap(
            layout->bulletBasePhys, bulletBytes,
            layout->activeBitsPhys, activeBytes) ||
        me_render_ranges_overlap(
            layout->generationBasePhys, generationBytes,
            layout->activeBitsPhys, activeBytes) ||
        me_render_ranges_overlap(
            layout->generationBasePhys, generationBytes,
            rawLayout->spriteBasePhys, spriteBytes) ||
        me_render_ranges_overlap(
            layout->generationBasePhys, generationBytes,
            rawLayout->representativePhys, representativeBytes) ||
        me_render_ranges_overlap(
            layout->activeBitsPhys, activeBytes,
            rawLayout->spriteBasePhys, spriteBytes) ||
        me_render_ranges_overlap(
            layout->activeBitsPhys, activeBytes,
            rawLayout->representativePhys, representativeBytes))
        return 0;

    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
    {
        if (layout->bucketHeadPhys[bucket] != 0u)
        {
            uint32_t physical = 0u;
            uint32_t slot = 0u;
            if (!me_render_stream_list_bullet_physical(
                    layout->bucketHeadPhys[bucket], layout,
                    &physical, &slot) ||
                physical != layout->bucketHeadPhys[bucket])
                return 0;
        }
    }
    return 1;
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
static int me_render_stream_item_physical(
    uint32_t pointer, const Th07PspMeRenderItemLayout *layout,
    uint32_t *physical, uint32_t *slot)
{
    if (physical)
        *physical = 0u;
    if (slot)
        *slot = 0xffffffffu;
    if (!pointer || !layout || !physical || !slot)
        return 0;
    const uint32_t phys = pointer & 0x1fffffffu;
    if (phys < layout->itemBasePhys)
        return 0;
    const uint32_t delta = phys - layout->itemBasePhys;
    if ((phys & 3u) != 0u || layout->itemStride != ME_RENDER_ITEM_STRIDE ||
        delta % ME_RENDER_ITEM_STRIDE != 0u)
        return 0;
    const uint32_t slotIndex = delta / ME_RENDER_ITEM_STRIDE;
    if (slotIndex >= layout->itemCount ||
        !me_render_main_ram_range_valid(phys, ME_RENDER_ITEM_STRIDE))
        return 0;
    *physical = phys;
    *slot = slotIndex;
    return 1;
}

static int me_render_stream_item_layout_valid(
    const Th07PspMeRenderItemLayout *layout,
    const Th07PspMeRenderRawLayout *rawLayout, uint32_t recordCount)
{
    const uint32_t selftest = layout &&
        layout->itemLayoutVersion ==
            ME_RENDER_ITEM_LAYOUT_SELFTEST_VERSION;
    const uint32_t expectedItemCount = selftest ? layout->itemCount :
        ME_RENDER_ITEM_COUNT;
    const uint32_t expectedActiveWords = selftest ? 1u :
        ME_RENDER_ITEM_ACTIVE_WORD_COUNT;
    if (!layout || !rawLayout ||
        (!selftest && layout->itemLayoutVersion !=
                          TH07_PSP_ME_RENDER_ITEM_LAYOUT_VERSION) ||
        layout->itemLayoutBytes != sizeof(*layout) ||
        layout->itemStride != ME_RENDER_ITEM_STRIDE ||
        layout->itemCount != expectedItemCount ||
        (selftest && (layout->itemCount == 0u || layout->itemCount > 2u)) ||
        layout->generationStride != ME_RENDER_ITEM_GENERATION_STRIDE ||
        layout->generationCount != expectedItemCount ||
        layout->activeBitsWordCount != expectedActiveWords ||
        layout->sinStride != sizeof(uint32_t) ||
        layout->cosStride != sizeof(uint32_t) ||
        layout->itemNextOffset != ME_RENDER_ITEM_NEXT_OFFSET ||
        layout->itemInUseOffset != ME_RENDER_ITEM_IN_USE_OFFSET ||
        layout->itemTypeOffset != ME_RENDER_ITEM_TYPE_OFFSET ||
        layout->itemVmOffset != ME_RENDER_ITEM_VM_OFFSET ||
        layout->vmPosXOffset != ME_RENDER_ITEM_VM_POS_X_OFFSET ||
        layout->vmPosYOffset != ME_RENDER_ITEM_VM_POS_Y_OFFSET ||
        layout->vmPosZOffset != ME_RENDER_ITEM_VM_POS_Z_OFFSET ||
        layout->expectedPrepareSerial == 0u ||
        layout->expectedItemCount != recordCount ||
        layout->expectedTotalCount < recordCount ||
        layout->expectedTotalCount > layout->itemCount ||
        recordCount > layout->itemCount ||
        ((recordCount == 0u) != (layout->headPhys == 0u)) ||
        ((recordCount == 0u) != (layout->tailPhys == 0u)) ||
        ((recordCount == layout->expectedTotalCount) !=
         (layout->suffixHeadPhys == 0u)))
        return 0;

    for (uint32_t index = 0u; index < 2u; ++index)
    {
        if (layout->reserved[index] != 0u)
            return 0;
    }
    if (!me_render_stream_raw_field_valid(
            layout->itemStride, layout->itemNextOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->itemStride, layout->itemInUseOffset, sizeof(uint8_t)) ||
        !me_render_stream_raw_field_valid(
            layout->itemStride, layout->itemTypeOffset, sizeof(uint8_t)) ||
        layout->itemVmOffset > layout->itemStride ||
        rawLayout->vmBytes > layout->itemStride - layout->itemVmOffset ||
        !me_render_stream_raw_field_valid(
            rawLayout->vmBytes, layout->vmPosXOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            rawLayout->vmBytes, layout->vmPosYOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            rawLayout->vmBytes, layout->vmPosZOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->itemBasePhys, layout->itemStride, layout->itemCount,
            layout->itemStride, sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->generationBasePhys, layout->generationStride,
            layout->generationCount, sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->activeBitsPhys, sizeof(uint32_t),
            layout->activeBitsWordCount, sizeof(uint32_t),
            sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->sinBasePhys, layout->sinStride, layout->itemCount,
            sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->cosBasePhys, layout->cosStride, layout->itemCount,
            sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->prepareSerialPhys, sizeof(uint32_t), 1u,
            sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->preparedSerialPhys, sizeof(uint32_t), 1u,
            sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->preparedCountPhys, sizeof(uint32_t), 1u,
            sizeof(uint32_t), sizeof(uint32_t)))
        return 0;

    if (recordCount != 0u)
    {
        uint32_t headPhys = 0u;
        uint32_t headSlot = 0u;
        uint32_t tailPhys = 0u;
        uint32_t tailSlot = 0u;
        if (!me_render_stream_item_physical(
                layout->headPhys, layout, &headPhys, &headSlot) ||
            !me_render_stream_item_physical(
                layout->tailPhys, layout, &tailPhys, &tailSlot) ||
            headPhys != layout->headPhys || tailPhys != layout->tailPhys)
            return 0;
        if (layout->suffixHeadPhys != 0u)
        {
            uint32_t suffixPhys = 0u;
            uint32_t suffixSlot = 0u;
            if (!me_render_stream_item_physical(
                    layout->suffixHeadPhys, layout,
                    &suffixPhys, &suffixSlot) ||
                suffixPhys != layout->suffixHeadPhys)
                return 0;
        }
    }

    const uint32_t itemBytes = layout->itemCount * layout->itemStride;
    const uint32_t generationBytes =
        layout->generationCount * layout->generationStride;
    const uint32_t activeBytes =
        layout->activeBitsWordCount * sizeof(uint32_t);
    const uint32_t trigBytes = layout->itemCount * sizeof(uint32_t);
    if (me_render_ranges_overlap(
            layout->itemBasePhys, itemBytes,
            layout->generationBasePhys, generationBytes) ||
        me_render_ranges_overlap(
            layout->itemBasePhys, itemBytes,
            layout->activeBitsPhys, activeBytes) ||
        me_render_ranges_overlap(
            layout->itemBasePhys, itemBytes,
            layout->sinBasePhys, trigBytes) ||
        me_render_ranges_overlap(
            layout->itemBasePhys, itemBytes,
            layout->cosBasePhys, trigBytes) ||
        me_render_ranges_overlap(
            layout->generationBasePhys, generationBytes,
            layout->activeBitsPhys, activeBytes) ||
        me_render_ranges_overlap(
            layout->sinBasePhys, trigBytes,
            layout->cosBasePhys, trigBytes))
        return 0;

    // Structural validation runs both on SC submit and at the start of the
    // ME command, before the ME invalidates the live Item authority.  Never
    // dereference prepare/count pointers here: that was the I-ME8R hardware
    // startup fault.  The cursor's post-invalidate finish check below binds
    // all three live values to this descriptor before any prefix publishes.
    return 1;
}

#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
static int me_render_stream_effect_physical(
    uint32_t pointer, const Th07PspMeRenderEffectLayout *layout,
    uint32_t *physical, uint32_t *slot)
{
    if (physical)
        *physical = 0u;
    if (slot)
        *slot = 0xffffffffu;
    if (!pointer || !layout || !physical || !slot)
        return 0;
    const uint32_t phys = pointer & 0x1fffffffu;
    if (phys < layout->effectBasePhys)
        return 0;
    const uint32_t delta = phys - layout->effectBasePhys;
    if ((phys & 3u) != 0u ||
        layout->effectStride != ME_RENDER_EFFECT_STRIDE ||
        delta % ME_RENDER_EFFECT_STRIDE != 0u)
        return 0;
    const uint32_t slotIndex = delta / ME_RENDER_EFFECT_STRIDE;
    if (slotIndex >= layout->effectCount ||
        !me_render_main_ram_range_valid(phys, ME_RENDER_EFFECT_STRIDE))
        return 0;
    *physical = phys;
    *slot = slotIndex;
    return 1;
}

static int me_render_stream_effect_endpoint_valid(
    uint32_t pointer, uint32_t count,
    const Th07PspMeRenderEffectLayout *layout)
{
    if ((count == 0u) != (pointer == 0u))
        return 0;
    if (count == 0u)
        return 1;
    uint32_t physical = 0u;
    uint32_t slot = 0u;
    return me_render_stream_effect_physical(
               pointer, layout, &physical, &slot) && physical == pointer;
}

static int me_render_stream_effect_layout_valid(
    const Th07PspMeRenderEffectLayout *layout,
    const Th07PspMeRenderRawLayout *rawLayout)
{
    if (!layout || !rawLayout ||
        layout->effectLayoutVersion !=
            TH07_PSP_ME_RENDER_EFFECT_LAYOUT_VERSION ||
        layout->effectLayoutBytes != sizeof(*layout) ||
        layout->effectStride != ME_RENDER_EFFECT_STRIDE ||
        layout->effectCount != ME_RENDER_EFFECT_COUNT ||
        layout->generationStride != ME_RENDER_EFFECT_GENERATION_STRIDE ||
        layout->generationCount != ME_RENDER_EFFECT_COUNT ||
        layout->activeBitsWordCount != ME_RENDER_EFFECT_ACTIVE_WORD_COUNT ||
        layout->sinStride != sizeof(uint32_t) ||
        layout->cosStride != sizeof(uint32_t) ||
        layout->effectNextOffset != ME_RENDER_EFFECT_NEXT_OFFSET ||
        layout->effectInUseOffset != ME_RENDER_EFFECT_IN_USE_OFFSET ||
        layout->effectIs2DOffset != ME_RENDER_EFFECT_IS_2D_OFFSET ||
        layout->effectVmOffset != ME_RENDER_EFFECT_VM_OFFSET ||
        layout->vmPosXOffset != ME_RENDER_EFFECT_VM_POS_X_OFFSET ||
        layout->vmPosYOffset != ME_RENDER_EFFECT_VM_POS_Y_OFFSET ||
        layout->vmPosZOffset != ME_RENDER_EFFECT_VM_POS_Z_OFFSET ||
        layout->expectedPrepareSerial == 0u || layout->reserved != 0u ||
        layout->expectedLayer0Count > layout->effectCount ||
        layout->expectedLayer3Count > layout->effectCount ||
        layout->expectedLayer0Count + layout->expectedLayer3Count >
            layout->effectCount ||
        !me_render_stream_effect_endpoint_valid(
            layout->layer0HeadPhys, layout->expectedLayer0Count, layout) ||
        !me_render_stream_effect_endpoint_valid(
            layout->layer0TailPhys, layout->expectedLayer0Count, layout) ||
        !me_render_stream_effect_endpoint_valid(
            layout->layer3HeadPhys, layout->expectedLayer3Count, layout) ||
        !me_render_stream_effect_endpoint_valid(
            layout->layer3TailPhys, layout->expectedLayer3Count, layout))
        return 0;

    if (!me_render_stream_raw_field_valid(
            layout->effectStride, layout->effectNextOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->effectStride, layout->effectInUseOffset,
            sizeof(uint8_t)) ||
        !me_render_stream_raw_field_valid(
            layout->effectStride, layout->effectIs2DOffset,
            sizeof(uint8_t)) ||
        layout->effectVmOffset > layout->effectStride ||
        rawLayout->vmBytes > layout->effectStride - layout->effectVmOffset ||
        !me_render_stream_raw_field_valid(
            rawLayout->vmBytes, layout->vmPosXOffset, sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            rawLayout->vmBytes, layout->vmPosYOffset, sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            rawLayout->vmBytes, layout->vmPosZOffset, sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->effectBasePhys, layout->effectStride,
            layout->effectCount, layout->effectStride, sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->generationBasePhys, layout->generationStride,
            layout->generationCount, sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->activeBitsPhys, sizeof(uint32_t),
            layout->activeBitsWordCount, sizeof(uint32_t),
            sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->sinBasePhys, layout->sinStride, layout->effectCount,
            sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->cosBasePhys, layout->cosStride, layout->effectCount,
            sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->prepareSerialPhys, sizeof(uint32_t), 1u,
            sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->preparedSerialPhys, sizeof(uint32_t), 1u,
            sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->preparedCountsPhys, sizeof(uint32_t), 2u,
            sizeof(uint32_t), sizeof(uint32_t)))
        return 0;

    const uint32_t effectBytes = layout->effectCount * layout->effectStride;
    const uint32_t generationBytes =
        layout->generationCount * layout->generationStride;
    const uint32_t activeBytes =
        layout->activeBitsWordCount * sizeof(uint32_t);
    const uint32_t trigBytes = layout->effectCount * sizeof(uint32_t);
    if (me_render_ranges_overlap(
            layout->effectBasePhys, effectBytes,
            layout->generationBasePhys, generationBytes) ||
        me_render_ranges_overlap(
            layout->effectBasePhys, effectBytes,
            layout->activeBitsPhys, activeBytes) ||
        me_render_ranges_overlap(
            layout->effectBasePhys, effectBytes,
            layout->sinBasePhys, trigBytes) ||
        me_render_ranges_overlap(
            layout->effectBasePhys, effectBytes,
            layout->cosBasePhys, trigBytes) ||
        me_render_ranges_overlap(
            layout->generationBasePhys, generationBytes,
            layout->activeBitsPhys, activeBytes) ||
        me_render_ranges_overlap(
            layout->sinBasePhys, trigBytes,
            layout->cosBasePhys, trigBytes))
        return 0;

    const unsigned char *prepareSerial =
        (const unsigned char *)(0x80000000u | layout->prepareSerialPhys);
    const unsigned char *preparedSerial =
        (const unsigned char *)(0x80000000u | layout->preparedSerialPhys);
    const unsigned char *preparedCounts =
        (const unsigned char *)(0x80000000u | layout->preparedCountsPhys);
    return me_render_stream_load_u32(prepareSerial, 0u) ==
               layout->expectedPrepareSerial &&
           me_render_stream_load_u32(preparedSerial, 0u) ==
               layout->expectedPrepareSerial &&
           me_render_stream_load_u32(preparedCounts, 0u) ==
               layout->expectedLayer0Count &&
           me_render_stream_load_u32(preparedCounts, sizeof(uint32_t)) ==
               layout->expectedLayer3Count;
}
#endif
#endif
#endif

static int me_render_stream_raw_record_valid(
    const Th07PspMeRenderRawRecord *record,
    const Th07PspMeRenderRawLayout *layout)
{
    if (!record || !layout || record->logicalState < 1u ||
        record->logicalState > 5u || record->slot >= layout->bulletCount ||
        record->generation == 0u || (record->vmPhys & 3u) != 0u ||
        !me_render_stream_float_bits_finite(record->posXBits) ||
        !me_render_stream_float_bits_finite(record->posYBits) ||
        !me_render_stream_float_bits_finite(record->sinBits) ||
        !me_render_stream_float_bits_finite(record->cosBits))
        return 0;

    const uint32_t expectedVm =
        layout->bulletBasePhys + record->slot * layout->bulletStride +
        (record->logicalState - 1u) * layout->vmBytes;
    if (record->vmPhys != expectedVm ||
        !me_render_main_ram_range_valid(record->vmPhys, layout->vmBytes))
        return 0;
    return 1;
}

static int me_render_stream_raw_sprite_physical(
    uint32_t pointer, const Th07PspMeRenderRawLayout *layout,
    uint32_t *physical)
{
    if (physical)
        *physical = 0u;
    if (!pointer || !layout || !physical)
        return 0;
    const uint32_t candidate = pointer & 0x1fffffffu;
    if (candidate < layout->spriteBasePhys)
        return 0;
    const uint32_t delta = candidate - layout->spriteBasePhys;
    if ((candidate & 3u) != 0u || delta % layout->spriteStride != 0u ||
        delta / layout->spriteStride >= layout->spriteCount ||
        !me_render_main_ram_range_valid(candidate, layout->spriteBytes))
        return 0;
    *physical = candidate;
    return 1;
}

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
static uint32_t me_bullet_fast_output_physical(void)
{
    return (uint32_t)&gMeBulletFastOutputArea.output & 0x1fffffffu;
}

static int me_bullet_fast_guards_match(const volatile unsigned char *area)
{
    if (!area)
        return 0;
    for (uint32_t index = 0u; index < ME_BULLET_FAST_GUARD_BYTES; ++index)
    {
        if (area[index] != ME_BULLET_FAST_GUARD_PATTERN)
            return 0;
    }
    const uint32_t upper =
        ME_BULLET_FAST_GUARD_BYTES + sizeof(Th07PspMeBulletFastOutput);
    for (uint32_t index = 0u; index < ME_BULLET_FAST_GUARD_BYTES; ++index)
    {
        if (area[upper + index] != ME_BULLET_FAST_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int me_bullet_fast_guards_match_on_me(void)
{
    const volatile unsigned char *area =
        (const volatile unsigned char *)(0x80000000u |
                                         (uint32_t)&gMeBulletFastOutputArea);
    return me_bullet_fast_guards_match(area);
}

static int me_bullet_fast_layout_valid(
    const Th07PspMeBulletFastLayout *layout)
{
    if (!layout ||
        layout->layoutVersion != TH07_PSP_ME_BULLET_FAST_LAYOUT_VERSION ||
        layout->layoutBytes != sizeof(*layout) ||
        layout->bulletStride != ME_BULLET_FAST_BULLET_STRIDE ||
        layout->bulletCount != ME_BULLET_FAST_BULLET_COUNT ||
        layout->generationStride != ME_BULLET_FAST_GENERATION_STRIDE ||
        layout->generationCount != ME_BULLET_FAST_BULLET_COUNT ||
        layout->activeBitsWordCount !=
            TH07_PSP_ME_BULLET_FAST_ACTIVE_WORDS ||
        layout->spriteStride != ME_BULLET_FAST_SPRITE_STRIDE ||
        layout->spriteCount != ME_BULLET_FAST_SPRITE_COUNT ||
        layout->bulletStateOffset != ME_BULLET_FAST_BULLET_STATE_OFFSET ||
        layout->bulletPosXOffset != ME_BULLET_FAST_BULLET_POS_X_OFFSET ||
        layout->bulletPosYOffset != ME_BULLET_FAST_BULLET_POS_Y_OFFSET ||
        layout->bulletPosZOffset != ME_BULLET_FAST_BULLET_POS_Z_OFFSET ||
        layout->bulletVelocityXOffset !=
            ME_BULLET_FAST_BULLET_VELOCITY_X_OFFSET ||
        layout->bulletVelocityYOffset !=
            ME_BULLET_FAST_BULLET_VELOCITY_Y_OFFSET ||
        layout->bulletVelocityZOffset !=
            ME_BULLET_FAST_BULLET_VELOCITY_Z_OFFSET ||
        layout->bulletSpawnDelayOffset !=
            ME_BULLET_FAST_BULLET_SPAWN_DELAY_OFFSET ||
        layout->bulletExFlagsOffset !=
            ME_BULLET_FAST_BULLET_EX_FLAGS_OFFSET ||
        layout->bulletOutOfBoundsTimeOffset !=
            ME_BULLET_FAST_BULLET_OUT_OF_BOUNDS_TIME_OFFSET ||
        layout->bulletCurrentCommandIndexOffset !=
            ME_BULLET_FAST_BULLET_CURRENT_COMMAND_INDEX_OFFSET ||
        layout->bulletCommandsOffset !=
            ME_BULLET_FAST_BULLET_COMMANDS_OFFSET ||
        layout->bulletCommandStride !=
            ME_BULLET_FAST_BULLET_COMMAND_STRIDE ||
        layout->bulletCommandTypeOffset !=
            ME_BULLET_FAST_BULLET_COMMAND_TYPE_OFFSET ||
        layout->bulletGrazeSizeXOffset !=
            ME_BULLET_FAST_BULLET_GRAZE_SIZE_X_OFFSET ||
        layout->bulletGrazeSizeYOffset !=
            ME_BULLET_FAST_BULLET_GRAZE_SIZE_Y_OFFSET ||
        layout->vmSpriteOffset != ME_BULLET_FAST_VM_SPRITE_OFFSET ||
        layout->spriteWidthOffset != ME_BULLET_FAST_SPRITE_WIDTH_OFFSET ||
        layout->spriteHeightOffset != ME_BULLET_FAST_SPRITE_HEIGHT_OFFSET ||
        layout->bombClearStride != ME_BULLET_FAST_BOMB_CLEAR_STRIDE ||
        layout->bombClearPosXOffset !=
            ME_BULLET_FAST_BOMB_CLEAR_POS_X_OFFSET ||
        layout->bombClearPosYOffset !=
            ME_BULLET_FAST_BOMB_CLEAR_POS_Y_OFFSET ||
        layout->bombClearPosZOffset !=
            ME_BULLET_FAST_BOMB_CLEAR_POS_Z_OFFSET ||
        layout->bombClearSizeXOffset !=
            ME_BULLET_FAST_BOMB_CLEAR_SIZE_X_OFFSET ||
        layout->bombClearSizeYOffset !=
            ME_BULLET_FAST_BOMB_CLEAR_SIZE_Y_OFFSET)
        return 0;

    if (!me_render_stream_raw_pool_valid(
            layout->bulletBasePhys, layout->bulletStride,
            layout->bulletCount, layout->bulletStride, sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->generationBasePhys, layout->generationStride,
            layout->generationCount, sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->activeBitsPhys, sizeof(uint32_t),
            layout->activeBitsWordCount, sizeof(uint32_t), sizeof(uint32_t)) ||
        !me_render_stream_raw_pool_valid(
            layout->spriteBasePhys, layout->spriteStride,
            layout->spriteCount, layout->spriteStride, sizeof(uint32_t)))
        return 0;

    const uint32_t bulletBytes =
        layout->bulletCount * layout->bulletStride;
    const uint32_t generationBytes =
        layout->generationCount * layout->generationStride;
    const uint32_t activeBytes =
        layout->activeBitsWordCount * sizeof(uint32_t);
    const uint32_t spriteBytes =
        layout->spriteCount * layout->spriteStride;
    if (me_render_ranges_overlap(
            layout->bulletBasePhys, bulletBytes,
            layout->generationBasePhys, generationBytes) ||
        me_render_ranges_overlap(
            layout->bulletBasePhys, bulletBytes,
            layout->activeBitsPhys, activeBytes) ||
        me_render_ranges_overlap(
            layout->bulletBasePhys, bulletBytes,
            layout->spriteBasePhys, spriteBytes) ||
        me_render_ranges_overlap(
            layout->generationBasePhys, generationBytes,
            layout->activeBitsPhys, activeBytes) ||
        me_render_ranges_overlap(
            layout->generationBasePhys, generationBytes,
            layout->spriteBasePhys, spriteBytes) ||
        me_render_ranges_overlap(
            layout->activeBitsPhys, activeBytes,
            layout->spriteBasePhys, spriteBytes))
        return 0;

    if (!me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletStateOffset,
            sizeof(uint16_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletPosXOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletPosYOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletPosZOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletVelocityXOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletVelocityYOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletVelocityZOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletSpawnDelayOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletExFlagsOffset,
            sizeof(uint16_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletOutOfBoundsTimeOffset,
            sizeof(uint16_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletCurrentCommandIndexOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletGrazeSizeXOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->bulletStride, layout->bulletGrazeSizeYOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            ME_RENDER_RAW_VM_BYTES, layout->vmSpriteOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->spriteStride, layout->spriteWidthOffset,
            sizeof(uint32_t)) ||
        !me_render_stream_raw_field_valid(
            layout->spriteStride, layout->spriteHeightOffset,
            sizeof(uint32_t)))
        return 0;

    const uint32_t lastCommandType =
        layout->bulletCommandsOffset +
        (ME_BULLET_FAST_COMMAND_COUNT - 1u) *
            layout->bulletCommandStride +
        layout->bulletCommandTypeOffset;
    return me_render_stream_raw_field_valid(
               layout->bulletStride, lastCommandType, sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->bombClearStride, layout->bombClearPosXOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->bombClearStride, layout->bombClearPosYOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->bombClearStride, layout->bombClearPosZOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->bombClearStride, layout->bombClearSizeXOffset,
               sizeof(uint32_t)) &&
           me_render_stream_raw_field_valid(
               layout->bombClearStride, layout->bombClearSizeYOffset,
               sizeof(uint32_t));
}

static int me_bullet_fast_job_valid(
    const Th07PspMeBulletFastJob *job, uint32_t outputPhys,
    uint32_t outputCapacity)
{
    if (!job || job->version != TH07_PSP_ME_BULLET_FAST_UPDATE_VERSION ||
        job->frameSeq == 0u || !me_bullet_fast_layout_valid(&job->layout) ||
        job->playerState > ME_BULLET_FAST_PLAYER_STATE_MAX ||
        job->bombClearCapacity != ME_BULLET_FAST_BOMB_CLEAR_CAPACITY ||
        job->bombClearHighWater > job->bombClearCapacity ||
        job->playfieldRightBits != 0x43c00000u || // 384.0f
        job->playfieldBottomBits != 0x43e00000u || // 448.0f
        outputPhys != me_bullet_fast_output_physical() ||
        outputCapacity != sizeof(Th07PspMeBulletFastOutput) ||
        !me_render_main_ram_range_valid(outputPhys, outputCapacity) ||
        !me_render_stream_raw_pool_valid(
            job->bombClearBasePhys, job->layout.bombClearStride,
            job->bombClearCapacity, job->layout.bombClearStride,
            sizeof(uint32_t)))
        return 0;

    const uint32_t scalarBits[] = {
        job->playerGrazeLeftBits, job->playerGrazeTopBits,
        job->playerGrazeRightBits, job->playerGrazeBottomBits,
        job->playerHitboxLeftBits, job->playerHitboxTopBits,
        job->playerHitboxRightBits, job->playerHitboxBottomBits,
        job->playfieldRightBits, job->playfieldBottomBits
    };
    for (uint32_t index = 0u;
         index < sizeof(scalarBits) / sizeof(scalarBits[0]); ++index)
    {
        if (!me_render_stream_float_bits_finite(scalarBits[index]))
            return 0;
    }
    if (me_render_bits_float(job->playerGrazeLeftBits) >
            me_render_bits_float(job->playerGrazeRightBits) ||
        me_render_bits_float(job->playerGrazeTopBits) >
            me_render_bits_float(job->playerGrazeBottomBits) ||
        me_render_bits_float(job->playerHitboxLeftBits) >
            me_render_bits_float(job->playerHitboxRightBits) ||
        me_render_bits_float(job->playerHitboxTopBits) >
            me_render_bits_float(job->playerHitboxBottomBits))
        return 0;

    const uint32_t bulletBytes =
        job->layout.bulletCount * job->layout.bulletStride;
    const uint32_t generationBytes =
        job->layout.generationCount * job->layout.generationStride;
    const uint32_t activeBytes =
        job->layout.activeBitsWordCount * sizeof(uint32_t);
    const uint32_t spriteBytes =
        job->layout.spriteCount * job->layout.spriteStride;
    const uint32_t bombBytes =
        job->bombClearCapacity * job->layout.bombClearStride;
    const uint32_t bases[] = {
        job->layout.bulletBasePhys, job->layout.generationBasePhys,
        job->layout.activeBitsPhys, job->layout.spriteBasePhys,
        job->bombClearBasePhys, outputPhys
    };
    const uint32_t bytes[] = {
        bulletBytes, generationBytes, activeBytes, spriteBytes,
        bombBytes, outputCapacity
    };
    for (uint32_t first = 0u; first < 6u; ++first)
    {
        for (uint32_t second = first + 1u; second < 6u; ++second)
        {
            if (me_render_ranges_overlap(
                    bases[first], bytes[first], bases[second], bytes[second]))
                return 0;
        }
    }
    return 1;
}

static int me_bullet_fast_sprite_physical(
    uint32_t pointer, const Th07PspMeBulletFastLayout *layout,
    uint32_t *physical)
{
    if (physical)
        *physical = 0u;
    if (!pointer || !layout || !physical)
        return 0;
    const uint32_t candidate = pointer & 0x1fffffffu;
    if (candidate < layout->spriteBasePhys || (candidate & 3u) != 0u)
        return 0;
    const uint32_t delta = candidate - layout->spriteBasePhys;
    if (delta % ME_BULLET_FAST_SPRITE_STRIDE != 0u ||
        delta / ME_BULLET_FAST_SPRITE_STRIDE >= layout->spriteCount ||
        !me_render_main_ram_range_valid(
            candidate, ME_BULLET_FAST_SPRITE_STRIDE))
        return 0;
    *physical = candidate;
    return 1;
}

static int me_bullet_fast_aabb_separate(
    float firstLeft, float firstTop, float firstRight, float firstBottom,
    float secondLeft, float secondTop, float secondRight, float secondBottom)
{
    return secondLeft > firstRight || secondRight < firstLeft ||
           secondTop > firstBottom || secondBottom < firstTop;
}

// Return 1 only when both canonical player-collision paths are guaranteed to
// return zero.  `valid` is cleared for malformed/non-finite live bomb data.
static int me_bullet_fast_no_collision(
    const Th07PspMeBulletFastJob *job, float posX, float posY,
    float grazeSizeX, float grazeSizeY, int *valid)
{
    *valid = 1;
    if (job->playerState == ME_BULLET_FAST_PLAYER_STATE_BORDER)
        return 0;

    const float bulletLeft = posX - grazeSizeX / 2.0f;
    const float bulletTop = posY - grazeSizeY / 2.0f;
    const float bulletRight = posX + grazeSizeX / 2.0f;
    const float bulletBottom = posY + grazeSizeY / 2.0f;
    const uint32_t bulletRectBits[] = {
        me_render_float_bits(bulletLeft), me_render_float_bits(bulletTop),
        me_render_float_bits(bulletRight), me_render_float_bits(bulletBottom)
    };
    for (uint32_t index = 0u; index < 4u; ++index)
    {
        if (!me_render_stream_float_bits_finite(bulletRectBits[index]))
        {
            *valid = 0;
            return 0;
        }
    }

    const unsigned char *bombs =
        (const unsigned char *)(0x80000000u | job->bombClearBasePhys);
    for (uint32_t index = 0u; index < job->bombClearHighWater; ++index)
    {
        const unsigned char *bomb =
            bombs + index * job->layout.bombClearStride;
        const uint32_t bombXBits = me_render_stream_load_u32(
            bomb, job->layout.bombClearPosXOffset);
        const uint32_t bombYBits = me_render_stream_load_u32(
            bomb, job->layout.bombClearPosYOffset);
        const uint32_t bombZBits = me_render_stream_load_u32(
            bomb, job->layout.bombClearPosZOffset);
        const uint32_t bombSizeXBits = me_render_stream_load_u32(
            bomb, job->layout.bombClearSizeXOffset);
        const uint32_t bombSizeYBits = me_render_stream_load_u32(
            bomb, job->layout.bombClearSizeYOffset);
        if (!me_render_stream_float_bits_finite(bombXBits) ||
            !me_render_stream_float_bits_finite(bombYBits) ||
            !me_render_stream_float_bits_finite(bombZBits) ||
            !me_render_stream_float_bits_finite(bombSizeXBits) ||
            !me_render_stream_float_bits_finite(bombSizeYBits))
        {
            *valid = 0;
            return 0;
        }
        const float bombX = me_render_bits_float(bombXBits);
        const float bombY = me_render_bits_float(bombYBits);
        const float bombZ = me_render_bits_float(bombZBits);
        const float bombSizeX = me_render_bits_float(bombSizeXBits);
        const float bombSizeY = me_render_bits_float(bombSizeYBits);
        if (bombZ != 0.0f)
        {
            const float bombLeft = bombX - bombZ / 2.0f;
            const float bombTop = bombY - bombSizeX / 2.0f;
            const float bombRight = bombZ / 2.0f + bombX;
            const float bombBottom = bombSizeX / 2.0f + bombY;
            if (!me_render_stream_float_bits_finite(
                    me_render_float_bits(bombLeft)) ||
                !me_render_stream_float_bits_finite(
                    me_render_float_bits(bombTop)) ||
                !me_render_stream_float_bits_finite(
                    me_render_float_bits(bombRight)) ||
                !me_render_stream_float_bits_finite(
                    me_render_float_bits(bombBottom)))
            {
                *valid = 0;
                return 0;
            }
            if (!me_bullet_fast_aabb_separate(
                    bulletLeft, bulletTop, bulletRight, bulletBottom,
                    bombLeft, bombTop, bombRight, bombBottom))
                return 0;
        }
        else if (bombSizeY != 0.0f)
        {
            const float differenceX = posX - bombX;
            const float differenceY = posY - bombY;
            const float distanceSquared =
                differenceX * differenceX + differenceY * differenceY;
            const float radiusSquared = bombSizeY * bombSizeY;
            if (!me_render_stream_float_bits_finite(
                    me_render_float_bits(distanceSquared)) ||
                !me_render_stream_float_bits_finite(
                    me_render_float_bits(radiusSquared)))
            {
                *valid = 0;
                return 0;
            }
            if (distanceSquared < radiusSquared)
                return 0;
        }
    }

    const float hitLeft = me_render_bits_float(job->playerHitboxLeftBits);
    const float hitTop = me_render_bits_float(job->playerHitboxTopBits);
    const float hitRight = me_render_bits_float(job->playerHitboxRightBits);
    const float hitBottom = me_render_bits_float(job->playerHitboxBottomBits);
    if (!me_bullet_fast_aabb_separate(
            bulletLeft, bulletTop, bulletRight, bulletBottom,
            hitLeft, hitTop, hitRight, hitBottom))
        return 0;

    // CheckGraze returns before its AABB only for DEAD/SPAWNING.  All other
    // states may observe the 20-pixel expansion and therefore need it clear.
    if (job->playerState != 1u && job->playerState != 2u)
    {
        const float grazeLeft = bulletLeft - 20.0f;
        const float grazeTop = bulletTop - 20.0f;
        const float grazeRight = bulletRight + 20.0f;
        const float grazeBottom = bulletBottom + 20.0f;
        if (!me_render_stream_float_bits_finite(
                me_render_float_bits(grazeLeft)) ||
            !me_render_stream_float_bits_finite(
                me_render_float_bits(grazeTop)) ||
            !me_render_stream_float_bits_finite(
                me_render_float_bits(grazeRight)) ||
            !me_render_stream_float_bits_finite(
                me_render_float_bits(grazeBottom)))
        {
            *valid = 0;
            return 0;
        }
        if (!me_bullet_fast_aabb_separate(
                grazeLeft, grazeTop, grazeRight, grazeBottom,
                me_render_bits_float(job->playerGrazeLeftBits),
                me_render_bits_float(job->playerGrazeTopBits),
                me_render_bits_float(job->playerGrazeRightBits),
                me_render_bits_float(job->playerGrazeBottomBits)))
            return 0;
    }
    return 1;
}

static uint32_t me_bullet_fast_update_kernel(
    const Th07PspMeBulletFastJob *job, Th07PspMeBulletFastOutput *output,
    uint32_t *outActiveCount, uint32_t *outCandidateCount,
    uint32_t *outInBoundsCount, uint32_t *outNoCollisionCount,
    uint32_t *outFirstBadSlot)
{
    *outActiveCount = 0u;
    *outCandidateCount = 0u;
    *outInBoundsCount = 0u;
    *outNoCollisionCount = 0u;
    *outFirstBadSlot = 0xffffffffu;
    memset(output, 0, sizeof(*output));

    // Validate the complete active bomb prefix once before a per-candidate
    // collision scan can interpret any of its fields.
    const unsigned char *bombs =
        (const unsigned char *)(0x80000000u | job->bombClearBasePhys);
    for (uint32_t bombIndex = 0u;
         bombIndex < job->bombClearHighWater; ++bombIndex)
    {
        const unsigned char *bomb =
            bombs + bombIndex * job->layout.bombClearStride;
        const uint32_t bits[] = {
            me_render_stream_load_u32(
                bomb, job->layout.bombClearPosXOffset),
            me_render_stream_load_u32(
                bomb, job->layout.bombClearPosYOffset),
            me_render_stream_load_u32(
                bomb, job->layout.bombClearPosZOffset),
            me_render_stream_load_u32(
                bomb, job->layout.bombClearSizeXOffset),
            me_render_stream_load_u32(
                bomb, job->layout.bombClearSizeYOffset)
        };
        for (uint32_t field = 0u; field < 5u; ++field)
        {
            if (!me_render_stream_float_bits_finite(bits[field]))
                return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;
        }
    }

    const unsigned char *activeBits =
        (const unsigned char *)(0x80000000u |
                                job->layout.activeBitsPhys);
    const unsigned char *generations =
        (const unsigned char *)(0x80000000u |
                                job->layout.generationBasePhys);
    const float playfieldRight =
        me_render_bits_float(job->playfieldRightBits);
    const float playfieldBottom =
        me_render_bits_float(job->playfieldBottomBits);

    for (uint32_t wordIndex = 0u;
         wordIndex < job->layout.activeBitsWordCount; ++wordIndex)
    {
        const uint32_t wordOffset = wordIndex * sizeof(uint32_t);
        const uint32_t tracked =
            me_render_stream_load_u32(activeBits, wordOffset);
        for (uint32_t bitIndex = 0u; bitIndex < 32u; ++bitIndex)
        {
            const uint32_t slotBit = 1u << bitIndex;
            if ((tracked & slotBit) == 0u)
                continue;
            const uint32_t slot = wordIndex * 32u + bitIndex;
            *outFirstBadSlot = slot;
            ++*outActiveCount;

            const uint32_t generationOffset =
                slot * job->layout.generationStride;
            const uint32_t generation = me_render_stream_load_u32(
                generations, generationOffset);
            if (generation == 0u)
                return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;

            const uint32_t bulletPhys =
                job->layout.bulletBasePhys +
                slot * ME_BULLET_FAST_BULLET_STRIDE;
            const unsigned char *bullet =
                (const unsigned char *)(0x80000000u | bulletPhys);
            const uint32_t state = me_render_stream_load_u16(
                bullet, job->layout.bulletStateOffset);
            if (state > 5u)
                return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;

            int candidate = 0;
            if (state == ME_BULLET_FAST_STATE_NORMAL)
            {
                const uint32_t exFlags = me_render_stream_load_u16(
                    bullet, job->layout.bulletExFlagsOffset);
                const int32_t spawnDelay = (int32_t)me_render_stream_load_u32(
                    bullet, job->layout.bulletSpawnDelayOffset);
                const int32_t commandIndex =
                    (int32_t)me_render_stream_load_u32(
                        bullet,
                        job->layout.bulletCurrentCommandIndexOffset);
                if (commandIndex < 0)
                    return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;
                if (exFlags == 0u && spawnDelay == 0)
                {
                    if (commandIndex >= ME_BULLET_FAST_COMMAND_COUNT)
                    {
                        candidate = 1;
                    }
                    else
                    {
                        const uint32_t commandTypeOffset =
                            job->layout.bulletCommandsOffset +
                            (uint32_t)commandIndex *
                                job->layout.bulletCommandStride +
                            job->layout.bulletCommandTypeOffset;
                        candidate = me_render_stream_load_u32(
                                        bullet, commandTypeOffset) == 0u;
                    }
                }
            }

            if (candidate)
            {
                const uint32_t posXBits = me_render_stream_load_u32(
                    bullet, job->layout.bulletPosXOffset);
                const uint32_t posYBits = me_render_stream_load_u32(
                    bullet, job->layout.bulletPosYOffset);
                const uint32_t posZBits = me_render_stream_load_u32(
                    bullet, job->layout.bulletPosZOffset);
                const uint32_t velocityXBits = me_render_stream_load_u32(
                    bullet, job->layout.bulletVelocityXOffset);
                const uint32_t velocityYBits = me_render_stream_load_u32(
                    bullet, job->layout.bulletVelocityYOffset);
                const uint32_t velocityZBits = me_render_stream_load_u32(
                    bullet, job->layout.bulletVelocityZOffset);
                const uint32_t grazeSizeXBits = me_render_stream_load_u32(
                    bullet, job->layout.bulletGrazeSizeXOffset);
                const uint32_t grazeSizeYBits = me_render_stream_load_u32(
                    bullet, job->layout.bulletGrazeSizeYOffset);
                const uint32_t motionBits[] = {
                    posXBits, posYBits, posZBits,
                    velocityXBits, velocityYBits, velocityZBits,
                    grazeSizeXBits, grazeSizeYBits
                };
                for (uint32_t field = 0u; field < 8u; ++field)
                {
                    if (!me_render_stream_float_bits_finite(
                            motionBits[field]))
                        return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;
                }

                const uint32_t spritePointer = me_render_stream_load_u32(
                    bullet, job->layout.vmSpriteOffset);
                uint32_t spritePhys = 0u;
                if (!me_bullet_fast_sprite_physical(
                        spritePointer, &job->layout, &spritePhys))
                    return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;
                const unsigned char *sprite =
                    (const unsigned char *)(0x80000000u | spritePhys);
                const uint32_t widthBits = me_render_stream_load_u32(
                    sprite, job->layout.spriteWidthOffset);
                const uint32_t heightBits = me_render_stream_load_u32(
                    sprite, job->layout.spriteHeightOffset);
                if (!me_render_stream_float_bits_finite(widthBits) ||
                    !me_render_stream_float_bits_finite(heightBits))
                    return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;

                const float newPosX =
                    me_render_bits_float(posXBits) +
                    me_render_bits_float(velocityXBits);
                const float newPosY =
                    me_render_bits_float(posYBits) +
                    me_render_bits_float(velocityYBits);
                const float newPosZ =
                    me_render_bits_float(posZBits) +
                    me_render_bits_float(velocityZBits);
                const uint32_t newPosXBits = me_render_float_bits(newPosX);
                const uint32_t newPosYBits = me_render_float_bits(newPosY);
                const uint32_t newPosZBits = me_render_float_bits(newPosZ);
                if (!me_render_stream_float_bits_finite(newPosXBits) ||
                    !me_render_stream_float_bits_finite(newPosYBits) ||
                    !me_render_stream_float_bits_finite(newPosZBits))
                    return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;

                const float halfWidth =
                    me_render_bits_float(widthBits) / 2.0f;
                const float halfHeight =
                    me_render_bits_float(heightBits) / 2.0f;
                if (!me_render_stream_float_bits_finite(
                        me_render_float_bits(halfWidth)) ||
                    !me_render_stream_float_bits_finite(
                        me_render_float_bits(halfHeight)))
                    return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;
                const int inBounds =
                    !(halfWidth + newPosX < 0.0f ||
                      newPosX - halfWidth > playfieldRight ||
                      halfHeight + newPosY < 0.0f ||
                      newPosY - halfHeight > playfieldBottom);

                int collisionInputsValid = 0;
                const int noCollision = me_bullet_fast_no_collision(
                    job, newPosX, newPosY,
                    me_render_bits_float(grazeSizeXBits),
                    me_render_bits_float(grazeSizeYBits),
                    &collisionInputsValid);
                if (!collisionInputsValid)
                    return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;

                Th07PspMeBulletFastSlotResult *result =
                    &output->slots[slot];
                result->posXBits = newPosXBits;
                result->posYBits = newPosYBits;
                result->posZBits = newPosZBits;
                result->generation = (uint16_t)generation;
                result->flags = TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE;
                output->candidateBits[wordIndex] |= slotBit;
                ++*outCandidateCount;
                if (inBounds)
                {
                    result->flags |=
                        TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS;
                    ++*outInBoundsCount;
                }
                if (noCollision)
                {
                    result->flags |=
                        TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION;
                    ++*outNoCollisionCount;
                }
            }

            if (me_render_stream_load_u32(
                    generations, generationOffset) != generation)
                return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;
        }
        if (me_render_stream_load_u32(activeBits, wordOffset) != tracked)
        {
            *outFirstBadSlot = wordIndex * 32u;
            return TH07_PSP_ME_BULLET_FAST_JOB_RECORD;
        }
    }
    *outFirstBadSlot = 0xffffffffu;
    return TH07_PSP_ME_BULLET_FAST_JOB_OK;
}
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
static uint32_t me_bullet_compact_seed_physical(uint32_t bank)
{
    if (bank >= TH07_PSP_ME_BULLET_COMPACT_BANKS)
        return 0u;
    return (uint32_t)&gMeBulletCompactSeedAreas[bank].seed & 0x1fffffffu;
}

static uint32_t me_bullet_compact_output_physical(void)
{
    return (uint32_t)&gMeBulletCompactOutputArea.output & 0x1fffffffu;
}

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
static uint32_t me_item_motion_seed_physical(uint32_t bank)
{
    if (bank >= TH07_PSP_ME_ITEM_MOTION_BANKS)
        return 0u;
    return (uint32_t)&gMeItemMotionSeedAreas[bank].seed & 0x1fffffffu;
}

static uint32_t me_item_motion_output_physical(void)
{
    return (uint32_t)&gMeItemMotionOutputArea.output & 0x1fffffffu;
}

static int me_item_motion_seed_guards_match(
    const volatile unsigned char *area)
{
    if (!area)
        return 0;
    const uint32_t upper = ME_BULLET_COMPACT_GUARD_BYTES +
                           sizeof(Th07PspMeItemMotionSeed);
    for (uint32_t index = 0u; index < ME_BULLET_COMPACT_GUARD_BYTES;
         ++index)
    {
        if (area[index] != ME_BULLET_COMPACT_GUARD_PATTERN ||
            area[upper + index] != ME_BULLET_COMPACT_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int me_item_motion_output_guards_match(
    const volatile unsigned char *area)
{
    if (!area)
        return 0;
    const uint32_t upper = ME_BULLET_COMPACT_GUARD_BYTES +
                           sizeof(Th07PspMeItemMotionOutput);
    for (uint32_t index = 0u; index < ME_BULLET_COMPACT_GUARD_BYTES;
         ++index)
    {
        if (area[index] != ME_BULLET_COMPACT_GUARD_PATTERN ||
            area[upper + index] != ME_BULLET_COMPACT_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int me_item_motion_seed_guards_match_on_me(uint32_t bank)
{
    if (bank >= TH07_PSP_ME_ITEM_MOTION_BANKS)
        return 0;
    return me_item_motion_seed_guards_match(
        (const volatile unsigned char *)(
            0x80000000u | (uint32_t)&gMeItemMotionSeedAreas[bank]));
}

static int me_item_motion_output_guards_match_on_me(void)
{
    return me_item_motion_output_guards_match(
        (const volatile unsigned char *)(
            0x80000000u | (uint32_t)&gMeItemMotionOutputArea));
}

static int me_item_motion_seed_header_valid(
    const Th07PspMeItemMotionSeed *seed, uint32_t bank)
{
    if (!seed || bank >= TH07_PSP_ME_ITEM_MOTION_BANKS)
        return 0;
    const Th07PspMeItemMotionSeedHeader *header = &seed->header;
    return header->version == TH07_PSP_ME_ITEM_MOTION_VERSION &&
           header->headerBytes == sizeof(*header) &&
           header->seedBytes == sizeof(*seed) &&
           header->bank == bank && header->frameSeq != 0u &&
           header->targetDrawSeq == header->frameSeq + 1u &&
           header->itemPrepareSerial != 0u &&
           header->recordCount <= TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS &&
           header->totalCount >= header->recordCount &&
           header->totalCount <= TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS &&
           header->candidateCount <= header->recordCount &&
           header->commitSequence == header->frameSeq &&
           header->reserved0 == 0u && header->reserved1 == 0u &&
           header->committed == TH07_PSP_ME_ITEM_MOTION_COMMITTED;
}

static int me_item_motion_output_header_valid(
    const Th07PspMeItemMotionOutput *output,
    const Th07PspMeItemMotionSeed *seed,
    const Th07PspMeBulletCompactJob *job)
{
    if (!output || !seed || !job)
        return 0;
    const Th07PspMeItemMotionOutputHeader *header = &output->header;
    return header->version == TH07_PSP_ME_ITEM_MOTION_OUTPUT_VERSION &&
           header->headerBytes == sizeof(*header) &&
           header->outputBytes == sizeof(*output) &&
           header->bank == job->seedBank &&
           header->frameSeq == job->frameSeq &&
           header->seedFrameSeq == job->seedFrameSeq &&
           header->seedTargetDrawSeq == job->seedTargetDrawSeq &&
           header->result == TH07_PSP_ME_ITEM_MOTION_RESULT_OK &&
           header->candidateLimit == job->itemMotionCandidateLimit &&
           header->candidateCount == seed->header.candidateCount &&
           header->processedCount <= header->candidateCount &&
           header->processedCount <= header->candidateLimit &&
           header->firstBadSlot == 0xffffffffu &&
           header->reserved0 == 0u && header->reserved1 == 0u &&
           header->reserved2 == 0u &&
           header->committed == TH07_PSP_ME_ITEM_MOTION_COMMITTED;
}

static void me_item_motion_reset_arenas_on_sc(void)
{
    memset(&gMeItemMotionSeedAreas, 0, sizeof(gMeItemMotionSeedAreas));
    for (uint32_t bank = 0u;
         bank < TH07_PSP_ME_ITEM_MOTION_BANKS; ++bank)
    {
        memset(gMeItemMotionSeedAreas[bank].guard0,
               ME_BULLET_COMPACT_GUARD_PATTERN,
               sizeof(gMeItemMotionSeedAreas[bank].guard0));
        memset(gMeItemMotionSeedAreas[bank].guard1,
               ME_BULLET_COMPACT_GUARD_PATTERN,
               sizeof(gMeItemMotionSeedAreas[bank].guard1));
    }
    memset(&gMeItemMotionOutputArea, 0, sizeof(gMeItemMotionOutputArea));
    memset(gMeItemMotionOutputArea.guard0,
           ME_BULLET_COMPACT_GUARD_PATTERN,
           sizeof(gMeItemMotionOutputArea.guard0));
    memset(gMeItemMotionOutputArea.guard1,
           ME_BULLET_COMPACT_GUARD_PATTERN,
           sizeof(gMeItemMotionOutputArea.guard1));
    __atomic_store_n(&gMeItemMotionOutputValid, 0u, __ATOMIC_RELEASE);
    sceKernelDcacheWritebackInvalidateRange(
        gMeItemMotionSeedAreas, sizeof(gMeItemMotionSeedAreas));
    sceKernelDcacheWritebackInvalidateRange(
        &gMeItemMotionOutputArea, sizeof(gMeItemMotionOutputArea));
}
#endif

static int me_bullet_compact_seed_guards_match(
    const volatile unsigned char *area)
{
    if (!area)
        return 0;
    for (uint32_t index = 0u; index < ME_BULLET_COMPACT_GUARD_BYTES;
         ++index)
    {
        if (area[index] != ME_BULLET_COMPACT_GUARD_PATTERN)
            return 0;
    }
    const uint32_t upper = ME_BULLET_COMPACT_GUARD_BYTES +
                           sizeof(Th07PspMeBulletCompactSeed);
    for (uint32_t index = 0u; index < ME_BULLET_COMPACT_GUARD_BYTES;
         ++index)
    {
        if (area[upper + index] != ME_BULLET_COMPACT_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int me_bullet_compact_output_guards_match(
    const volatile unsigned char *area)
{
    if (!area)
        return 0;
    for (uint32_t index = 0u; index < ME_BULLET_COMPACT_GUARD_BYTES;
         ++index)
    {
        if (area[index] != ME_BULLET_COMPACT_GUARD_PATTERN)
            return 0;
    }
    const uint32_t upper = ME_BULLET_COMPACT_GUARD_BYTES +
                           sizeof(Th07PspMeBulletCompactOutput);
    for (uint32_t index = 0u; index < ME_BULLET_COMPACT_GUARD_BYTES;
         ++index)
    {
        if (area[upper + index] != ME_BULLET_COMPACT_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int me_bullet_compact_seed_guards_match_on_me(uint32_t bank)
{
    if (bank >= TH07_PSP_ME_BULLET_COMPACT_BANKS)
        return 0;
    const volatile unsigned char *area =
        (const volatile unsigned char *)(
            0x80000000u | (uint32_t)&gMeBulletCompactSeedAreas[bank]);
    return me_bullet_compact_seed_guards_match(area);
}

static int me_bullet_compact_output_guards_match_on_me(void)
{
    const volatile unsigned char *area =
        (const volatile unsigned char *)(
            0x80000000u | (uint32_t)&gMeBulletCompactOutputArea);
    return me_bullet_compact_output_guards_match(area);
}

static int me_bullet_compact_seed_header_valid(
    const Th07PspMeBulletCompactSeed *seed, uint32_t bank)
{
    if (!seed || bank >= TH07_PSP_ME_BULLET_COMPACT_BANKS)
        return 0;
    const Th07PspMeBulletCompactSeedHeader *header = &seed->header;
    return header->version == TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION &&
           header->headerBytes == sizeof(*header) &&
           header->seedBytes == sizeof(*seed) &&
           header->backend ==
               TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM &&
           header->bank == bank && header->frameSeq != 0u &&
           header->targetDrawSeq != 0u &&
           header->recordCount <= TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS &&
           header->candidateCount <= header->recordCount &&
           header->payloadHash == 0u && header->reserved == 0u &&
           header->commitSequence == header->frameSeq &&
           header->committed == TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED;
}

static int me_bullet_compact_job_valid(
    const Th07PspMeBulletCompactJob *job, uint32_t seedPhys,
    uint32_t seedCapacity, uint32_t outputPhys, uint32_t outputCapacity)
{
    uint32_t allowedFlags =
        TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    allowedFlags |= TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID;
#endif
    if (!job || job->version != TH07_PSP_ME_BULLET_COMPACT_VERSION ||
        job->frameSeq == 0u || job->seedFrameSeq == 0u ||
        job->seedTargetDrawSeq == 0u ||
        (job->flags & ~allowedFlags) != 0u ||
        job->seedBank >= TH07_PSP_ME_BULLET_COMPACT_BANKS ||
        job->playerState > ME_BULLET_COMPACT_PLAYER_STATE_MAX ||
        job->bombClearCapacity != ME_BULLET_COMPACT_BOMB_CLEAR_CAPACITY ||
        job->bombClearHighWater > job->bombClearCapacity ||
        job->playfieldRightBits != 0x43c00000u || // 384.0f
        job->playfieldBottomBits != 0x43e00000u || // 448.0f
        seedPhys != me_bullet_compact_seed_physical(job->seedBank) ||
        seedCapacity != sizeof(Th07PspMeBulletCompactSeed) ||
        outputPhys != me_bullet_compact_output_physical() ||
        outputCapacity != sizeof(Th07PspMeBulletCompactOutput) ||
        !me_render_main_ram_range_valid(seedPhys, seedCapacity) ||
        !me_render_main_ram_range_valid(outputPhys, outputCapacity) ||
        me_render_ranges_overlap(seedPhys, seedCapacity,
                                 outputPhys, outputCapacity))
        return 0;

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    const uint32_t itemEnabled =
        (job->flags & TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u;
    if (itemEnabled)
    {
        const uint32_t itemSeedPhys =
            me_item_motion_seed_physical(job->seedBank);
        const uint32_t itemOutputPhys = me_item_motion_output_physical();
        if (job->itemMotionCandidateLimit == 0u ||
            job->itemMotionCandidateLimit > TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS ||
            job->itemCurrentPowerClass < 0 ||
            job->itemCurrentPowerClass > 128 ||
            job->itemDifficulty < 0 || job->itemDifficulty > 5 ||
            job->itemHasBorder > 1u ||
            !me_render_main_ram_range_valid(
                itemSeedPhys, sizeof(Th07PspMeItemMotionSeed)) ||
            !me_render_main_ram_range_valid(
                itemOutputPhys, sizeof(Th07PspMeItemMotionOutput)) ||
            me_render_ranges_overlap(
                itemSeedPhys, sizeof(Th07PspMeItemMotionSeed),
                itemOutputPhys, sizeof(Th07PspMeItemMotionOutput)) ||
            me_render_ranges_overlap(seedPhys, seedCapacity,
                                     itemSeedPhys,
                                     sizeof(Th07PspMeItemMotionSeed)) ||
            me_render_ranges_overlap(seedPhys, seedCapacity,
                                     itemOutputPhys,
                                     sizeof(Th07PspMeItemMotionOutput)) ||
            me_render_ranges_overlap(outputPhys, outputCapacity,
                                     itemSeedPhys,
                                     sizeof(Th07PspMeItemMotionSeed)) ||
            me_render_ranges_overlap(outputPhys, outputCapacity,
                                     itemOutputPhys,
                                     sizeof(Th07PspMeItemMotionOutput)))
            return 0;
        const uint32_t itemScalars[] = {
            job->itemPlayerPosXBits, job->itemPlayerPosYBits,
            job->itemCollectSpeedBits, job->itemPocYBits,
            job->itemFramerateMultiplierBits
        };
        for (uint32_t index = 0u;
             index < sizeof(itemScalars) / sizeof(itemScalars[0]); ++index)
        {
            if (!me_render_stream_float_bits_finite(itemScalars[index]))
                return 0;
        }
    }
    else if (job->itemMotionCandidateLimit != 0u)
    {
        return 0;
    }
#endif

    const uint32_t scalarBits[] = {
        job->playerGrazeLeftBits, job->playerGrazeTopBits,
        job->playerGrazeRightBits, job->playerGrazeBottomBits,
        job->playerHitboxLeftBits, job->playerHitboxTopBits,
        job->playerHitboxRightBits, job->playerHitboxBottomBits,
        job->playfieldRightBits, job->playfieldBottomBits
    };
    for (uint32_t index = 0u;
         index < sizeof(scalarBits) / sizeof(scalarBits[0]); ++index)
    {
        if (!me_render_stream_float_bits_finite(scalarBits[index]))
            return 0;
    }
    if (me_render_bits_float(job->playerGrazeLeftBits) >
            me_render_bits_float(job->playerGrazeRightBits) ||
        me_render_bits_float(job->playerGrazeTopBits) >
            me_render_bits_float(job->playerGrazeBottomBits) ||
        me_render_bits_float(job->playerHitboxLeftBits) >
            me_render_bits_float(job->playerHitboxRightBits) ||
        me_render_bits_float(job->playerHitboxTopBits) >
            me_render_bits_float(job->playerHitboxBottomBits))
        return 0;

    if ((job->flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID) != 0u)
    {
        const uint32_t bombBytes = job->bombClearCapacity *
                                   ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE;
        if (!me_render_stream_raw_pool_valid(
                job->bombClearBasePhys,
                ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE,
                job->bombClearCapacity,
                ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE,
                sizeof(uint32_t)) ||
            me_render_ranges_overlap(job->bombClearBasePhys, bombBytes,
                                     seedPhys, seedCapacity) ||
            me_render_ranges_overlap(job->bombClearBasePhys, bombBytes,
                                     outputPhys, outputCapacity))
            return 0;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        if ((job->flags &
             TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u &&
            (me_render_ranges_overlap(
                 job->bombClearBasePhys, bombBytes,
                 me_item_motion_seed_physical(job->seedBank),
                 sizeof(Th07PspMeItemMotionSeed)) ||
             me_render_ranges_overlap(
                 job->bombClearBasePhys, bombBytes,
                 me_item_motion_output_physical(),
                 sizeof(Th07PspMeItemMotionOutput))))
            return 0;
#endif
    }
    return 1;
}

static int me_bullet_compact_aabb_separate(
    float firstLeft, float firstTop, float firstRight, float firstBottom,
    float secondLeft, float secondTop, float secondRight, float secondBottom)
{
    return secondLeft > firstRight || secondRight < firstLeft ||
           secondTop > firstBottom || secondBottom < firstTop;
}

static int me_bullet_compact_no_collision(
    const Th07PspMeBulletCompactJob *job, float posX, float posY,
    float grazeSizeX, float grazeSizeY, int *valid)
{
    *valid = 1;
    if ((job->flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID) == 0u ||
        job->playerState == ME_BULLET_COMPACT_PLAYER_STATE_BORDER)
        return 0;

    const float bulletLeft = posX - grazeSizeX / 2.0f;
    const float bulletTop = posY - grazeSizeY / 2.0f;
    const float bulletRight = posX + grazeSizeX / 2.0f;
    const float bulletBottom = posY + grazeSizeY / 2.0f;
    const uint32_t bulletRectBits[] = {
        me_render_float_bits(bulletLeft), me_render_float_bits(bulletTop),
        me_render_float_bits(bulletRight), me_render_float_bits(bulletBottom)
    };
    for (uint32_t index = 0u; index < 4u; ++index)
    {
        if (!me_render_stream_float_bits_finite(bulletRectBits[index]))
        {
            *valid = 0;
            return 0;
        }
    }

    const unsigned char *bombs =
        (const unsigned char *)(0x80000000u | job->bombClearBasePhys);
    for (uint32_t index = 0u; index < job->bombClearHighWater; ++index)
    {
        const unsigned char *bomb =
            bombs + index * ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE;
        const uint32_t bombXBits = me_render_stream_load_u32(
            bomb, ME_BULLET_COMPACT_BOMB_CLEAR_POS_X_OFFSET);
        const uint32_t bombYBits = me_render_stream_load_u32(
            bomb, ME_BULLET_COMPACT_BOMB_CLEAR_POS_Y_OFFSET);
        const uint32_t bombZBits = me_render_stream_load_u32(
            bomb, ME_BULLET_COMPACT_BOMB_CLEAR_POS_Z_OFFSET);
        const uint32_t bombSizeXBits = me_render_stream_load_u32(
            bomb, ME_BULLET_COMPACT_BOMB_CLEAR_SIZE_X_OFFSET);
        const uint32_t bombSizeYBits = me_render_stream_load_u32(
            bomb, ME_BULLET_COMPACT_BOMB_CLEAR_SIZE_Y_OFFSET);
        if (!me_render_stream_float_bits_finite(bombXBits) ||
            !me_render_stream_float_bits_finite(bombYBits) ||
            !me_render_stream_float_bits_finite(bombZBits) ||
            !me_render_stream_float_bits_finite(bombSizeXBits) ||
            !me_render_stream_float_bits_finite(bombSizeYBits))
        {
            *valid = 0;
            return 0;
        }
        const float bombX = me_render_bits_float(bombXBits);
        const float bombY = me_render_bits_float(bombYBits);
        const float bombZ = me_render_bits_float(bombZBits);
        const float bombSizeX = me_render_bits_float(bombSizeXBits);
        const float bombSizeY = me_render_bits_float(bombSizeYBits);
        if (bombZ != 0.0f)
        {
            const float bombLeft = bombX - bombZ / 2.0f;
            const float bombTop = bombY - bombSizeX / 2.0f;
            const float bombRight = bombZ / 2.0f + bombX;
            const float bombBottom = bombSizeX / 2.0f + bombY;
            if (!me_render_stream_float_bits_finite(
                    me_render_float_bits(bombLeft)) ||
                !me_render_stream_float_bits_finite(
                    me_render_float_bits(bombTop)) ||
                !me_render_stream_float_bits_finite(
                    me_render_float_bits(bombRight)) ||
                !me_render_stream_float_bits_finite(
                    me_render_float_bits(bombBottom)))
            {
                *valid = 0;
                return 0;
            }
            if (!me_bullet_compact_aabb_separate(
                    bulletLeft, bulletTop, bulletRight, bulletBottom,
                    bombLeft, bombTop, bombRight, bombBottom))
                return 0;
        }
        else if (bombSizeY != 0.0f)
        {
            const float differenceX = posX - bombX;
            const float differenceY = posY - bombY;
            const float distanceSquared =
                differenceX * differenceX + differenceY * differenceY;
            const float radiusSquared = bombSizeY * bombSizeY;
            if (!me_render_stream_float_bits_finite(
                    me_render_float_bits(distanceSquared)) ||
                !me_render_stream_float_bits_finite(
                    me_render_float_bits(radiusSquared)))
            {
                *valid = 0;
                return 0;
            }
            if (distanceSquared < radiusSquared)
                return 0;
        }
    }

    if (!me_bullet_compact_aabb_separate(
            bulletLeft, bulletTop, bulletRight, bulletBottom,
            me_render_bits_float(job->playerHitboxLeftBits),
            me_render_bits_float(job->playerHitboxTopBits),
            me_render_bits_float(job->playerHitboxRightBits),
            me_render_bits_float(job->playerHitboxBottomBits)))
        return 0;

    if (job->playerState != 1u && job->playerState != 2u)
    {
        const float grazeLeft = bulletLeft - 20.0f;
        const float grazeTop = bulletTop - 20.0f;
        const float grazeRight = bulletRight + 20.0f;
        const float grazeBottom = bulletBottom + 20.0f;
        if (!me_render_stream_float_bits_finite(
                me_render_float_bits(grazeLeft)) ||
            !me_render_stream_float_bits_finite(
                me_render_float_bits(grazeTop)) ||
            !me_render_stream_float_bits_finite(
                me_render_float_bits(grazeRight)) ||
            !me_render_stream_float_bits_finite(
                me_render_float_bits(grazeBottom)))
        {
            *valid = 0;
            return 0;
        }
        if (!me_bullet_compact_aabb_separate(
                grazeLeft, grazeTop, grazeRight, grazeBottom,
                me_render_bits_float(job->playerGrazeLeftBits),
                me_render_bits_float(job->playerGrazeTopBits),
                me_render_bits_float(job->playerGrazeRightBits),
                me_render_bits_float(job->playerGrazeBottomBits)))
            return 0;
    }
    return 1;
}

// Optional sidecar: a malformed/non-eligible update seed must never reject an
// otherwise valid I-ME5 render record.  It simply leaves this slot canonical.
static void me_bullet_compact_capture_seed(
    Th07PspMeBulletCompactSeed *seed, const unsigned char *bullet,
    const unsigned char *vm, uint32_t slot, uint32_t generation,
    uint32_t state, const Th07PspMeRenderRawLayout *rawLayout)
{
    if (!seed || !bullet || !vm || !rawLayout ||
        slot >= TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS || generation == 0u ||
        state != ME_BULLET_COMPACT_STATE_NORMAL)
        return;

    const uint32_t exFlags = me_render_stream_load_u16(
        bullet, ME_BULLET_COMPACT_BULLET_EX_FLAGS_OFFSET);
    const int32_t spawnDelay = (int32_t)me_render_stream_load_u32(
        bullet, ME_BULLET_COMPACT_BULLET_SPAWN_DELAY_OFFSET);
    const int32_t commandIndex = (int32_t)me_render_stream_load_u32(
        bullet, ME_BULLET_COMPACT_BULLET_CURRENT_COMMAND_INDEX_OFFSET);
    if (exFlags != 0u || spawnDelay != 0 || commandIndex < 0)
        return;
    if (commandIndex < ME_BULLET_COMPACT_COMMAND_COUNT)
    {
        const uint32_t commandTypeOffset =
            ME_BULLET_COMPACT_BULLET_COMMANDS_OFFSET +
            (uint32_t)commandIndex * ME_BULLET_COMPACT_BULLET_COMMAND_STRIDE +
            ME_BULLET_COMPACT_BULLET_COMMAND_TYPE_OFFSET;
        if (me_render_stream_load_u32(bullet, commandTypeOffset) != 0u)
            return;
    }

    const uint32_t spritePointer = me_render_stream_load_u32(
        vm, rawLayout->vmSpriteOffset);
    uint32_t spritePhys = 0u;
    if (!me_render_stream_raw_sprite_physical(
            spritePointer, rawLayout, &spritePhys))
        return;
    const unsigned char *sprite =
        (const unsigned char *)(0x80000000u | spritePhys);
    const uint32_t values[] = {
        me_render_stream_load_u32(
            bullet, ME_RENDER_LIST_BULLET_POS_X_OFFSET),
        me_render_stream_load_u32(
            bullet, ME_RENDER_LIST_BULLET_POS_Y_OFFSET),
        me_render_stream_load_u32(
            bullet, ME_BULLET_COMPACT_BULLET_POS_Z_OFFSET),
        me_render_stream_load_u32(
            bullet, ME_BULLET_COMPACT_BULLET_VELOCITY_X_OFFSET),
        me_render_stream_load_u32(
            bullet, ME_BULLET_COMPACT_BULLET_VELOCITY_Y_OFFSET),
        me_render_stream_load_u32(
            bullet, ME_BULLET_COMPACT_BULLET_VELOCITY_Z_OFFSET),
        me_render_stream_load_u32(sprite, rawLayout->spriteWidthOffset),
        me_render_stream_load_u32(sprite, rawLayout->spriteHeightOffset),
        me_render_stream_load_u32(
            bullet, ME_BULLET_COMPACT_BULLET_GRAZE_SIZE_X_OFFSET),
        me_render_stream_load_u32(
            bullet, ME_BULLET_COMPACT_BULLET_GRAZE_SIZE_Y_OFFSET)
    };
    for (uint32_t index = 0u;
         index < sizeof(values) / sizeof(values[0]); ++index)
    {
        if (!me_render_stream_float_bits_finite(values[index]))
            return;
    }

    // This executes inside I-ME5's already-paid direct-list traversal while
    // FCR31 is canonical zero.  Preserve Bullet::Update's scalar operation
    // order exactly: pos += velocity, followed by GameManager::IsInBounds'
    // four width/height half-extent comparisons.
    const float nextPosX = me_render_bits_float(values[0]) +
                           me_render_bits_float(values[3]);
    const float nextPosY = me_render_bits_float(values[1]) +
                           me_render_bits_float(values[4]);
    const float nextPosZ = me_render_bits_float(values[2]) +
                           me_render_bits_float(values[5]);
    const uint32_t nextPosXBits = me_render_float_bits(nextPosX);
    const uint32_t nextPosYBits = me_render_float_bits(nextPosY);
    const uint32_t nextPosZBits = me_render_float_bits(nextPosZ);
    if (!me_render_stream_float_bits_finite(nextPosXBits) ||
        !me_render_stream_float_bits_finite(nextPosYBits) ||
        !me_render_stream_float_bits_finite(nextPosZBits))
        return;

    const float halfWidth = me_render_bits_float(values[6]) / 2.0f;
    const float halfHeight = me_render_bits_float(values[7]) / 2.0f;
    if (!me_render_stream_float_bits_finite(
            me_render_float_bits(halfWidth)) ||
        !me_render_stream_float_bits_finite(
            me_render_float_bits(halfHeight)))
        return;
    const int inBounds =
        !(halfWidth + nextPosX < 0.0f ||
          nextPosX - halfWidth > 384.0f ||
          halfHeight + nextPosY < 0.0f ||
          nextPosY - halfHeight > 448.0f);

#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, generation) = generation;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posXBits) = values[0];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posYBits) = values[1];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posZBits) = values[2];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityXBits) = values[3];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityYBits) = values[4];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityZBits) = values[5];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteWidthBits) = values[6];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteHeightBits) = values[7];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeXBits) = values[8];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeYBits) = values[9];
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosXBits) = nextPosXBits;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosYBits) = nextPosYBits;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosZBits) = nextPosZBits;
    const uint32_t bit = 1u << (slot & 31u);
    const uint32_t word = slot >> 5u;
    seed->inBoundsBits[word] &= ~bit;
    if (inBounds)
        seed->inBoundsBits[word] |= bit;
#else
    Th07PspMeBulletCompactSeedSlot *out = &seed->slots[slot];
    out->generation = generation;
    out->posXBits = values[0];
    out->posYBits = values[1];
    out->posZBits = values[2];
    out->velocityXBits = values[3];
    out->velocityYBits = values[4];
    out->velocityZBits = values[5];
    out->spriteWidthBits = values[6];
    out->spriteHeightBits = values[7];
    out->grazeSizeXBits = values[8];
    out->grazeSizeYBits = values[9];
    out->nextPosXBits = nextPosXBits;
    out->nextPosYBits = nextPosYBits;
    out->nextPosZBits = nextPosZBits;
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    const uint32_t bit = 1u << (slot & 31u);
    const uint32_t word = slot >> 5u;
    seed->inBoundsBits[word] &= ~bit;
    if (inBounds)
        seed->inBoundsBits[word] |= bit;
#else
    out->staticFlags = TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE;
    if (inBounds)
        out->staticFlags |= TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS;
    out->reserved = 0u;
    const uint32_t bit = 1u << (slot & 31u);
#endif
#endif
    if ((seed->candidateBits[slot >> 5u] & bit) == 0u)
    {
        seed->candidateBits[slot >> 5u] |= bit;
        ++seed->header.candidateCount;
    }
}

#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
static void me_bullet_compact_clear_seed_slot(
    Th07PspMeBulletCompactSeed *seed, uint32_t slot)
{
    if (!seed || slot >= TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS)
        return;
    const uint32_t bit = 1u << (slot & 31u);
    seed->candidateBits[slot >> 5u] &= ~bit;
    seed->inBoundsBits[slot >> 5u] &= ~bit;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, generation) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posXBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posYBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posZBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityXBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityYBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityZBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteWidthBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteHeightBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeXBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeYBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosXBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosYBits) = 0u;
    TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosZBits) = 0u;
}
#endif

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
static int me_item_motion_float_bits_supported(uint32_t bits)
{
    const uint32_t exponent = bits & 0x7f800000u;
    const uint32_t mantissa = bits & 0x007fffffu;
    // Initial authority excludes NaN/Inf and non-zero subnormals.  Signed
    // zero remains legal and is compared by raw bits on SC.
    return exponent != 0x7f800000u &&
           (exponent != 0u || mantissa == 0u);
}

static void me_item_motion_seed_reset(
    Th07PspMeItemMotionSeed *seed, uint32_t bank, uint32_t frameSeq)
{
    if (!seed)
        return;
    memset(seed, 0, offsetof(Th07PspMeItemMotionSeed, slots));
    seed->header.bank = bank;
    seed->header.frameSeq = frameSeq;
    seed->header.targetDrawSeq = frameSeq + 1u;
    seed->header.committed = 0u;
}

static void me_item_motion_capture_seed(
    Th07PspMeItemMotionSeed *seed,
    const volatile unsigned char *item, uint32_t slot,
    uint32_t generation)
{
    if (!seed || !item || slot >= TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS ||
        generation == 0u)
        return;

    const uint32_t state = me_render_stream_item_load_u8(
        item, ME_ITEM_MOTION_STATE_OFFSET);
    const uint32_t autoCollect = me_render_stream_item_load_u8(
        item, ME_ITEM_MOTION_AUTOCOLLECT_OFFSET);
    const uint32_t inUse = me_render_stream_item_load_u8(
        item, ME_RENDER_ITEM_IN_USE_OFFSET);
    if (state > 2u || autoCollect > 1u || inUse != 1u)
        return;

    const uint32_t pos[3] = {
        me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_CURRENT_POS_OFFSET + 0u),
        me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_CURRENT_POS_OFFSET + 4u),
        me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_CURRENT_POS_OFFSET + 8u)
    };
    const uint32_t start[3] = {
        me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_START_POS_OFFSET + 0u),
        me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_START_POS_OFFSET + 4u),
        me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_START_POS_OFFSET + 8u)
    };
    uint32_t target[3] = {0u, 0u, 0u};
    int32_t timerCurrent = 0;
    uint32_t timerSubFrameBits = 0u;
    if (state == 2u)
    {
        target[0] = me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_TARGET_POS_OFFSET + 0u);
        target[1] = me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_TARGET_POS_OFFSET + 4u);
        target[2] = me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_TARGET_POS_OFFSET + 8u);
        timerCurrent = (int32_t)me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_TIMER_CURRENT_OFFSET);
        timerSubFrameBits = me_render_stream_item_load_u32(
            item, ME_ITEM_MOTION_TIMER_SUBFRAME_OFFSET);
    }

    for (uint32_t axis = 0u; axis < 3u; ++axis)
    {
        if (!me_item_motion_float_bits_supported(pos[axis]) ||
            !me_item_motion_float_bits_supported(start[axis]) ||
            (state == 2u &&
             !me_item_motion_float_bits_supported(target[axis])))
            return;
    }
    if (state == 2u &&
        !me_item_motion_float_bits_supported(timerSubFrameBits))
        return;

    Th07PspMeItemMotionSeedSlot *out = &seed->slots[slot];
    out->generation = generation;
    out->posXBits = pos[0];
    out->posYBits = pos[1];
    out->posZBits = pos[2];
    out->startXBits = start[0];
    out->startYBits = start[1];
    out->startZBits = start[2];
    out->targetXBits = target[0];
    out->targetYBits = target[1];
    out->targetZBits = target[2];
    out->timerCurrent = timerCurrent;
    out->timerSubFrameBits = timerSubFrameBits;
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    const uint32_t bit = 1u << (slot & 31u);
    const uint32_t word = slot >> 5u;
    seed->stateBit0[word] &= ~bit;
    seed->stateBit1[word] &= ~bit;
    seed->autoCollectBits[word] &= ~bit;
    if ((state & 1u) != 0u)
        seed->stateBit0[word] |= bit;
    if ((state & 2u) != 0u)
        seed->stateBit1[word] |= bit;
    if (autoCollect != 0u)
        seed->autoCollectBits[word] |= bit;
#else
    out->stateAndFlags =
        state |
        (autoCollect << TH07_PSP_ME_ITEM_MOTION_INPUT_AUTOCOLLECT_SHIFT) |
        (inUse << TH07_PSP_ME_ITEM_MOTION_INPUT_INUSE_SHIFT);
    out->reserved0 = 0u;
    out->reserved1 = 0u;
    out->reserved2 = 0u;
    const uint32_t bit = 1u << (slot & 31u);
#endif
    if ((seed->candidateBits[slot >> 5u] & bit) == 0u)
    {
        seed->candidateBits[slot >> 5u] |= bit;
        ++seed->header.candidateCount;
    }
}

static void me_item_motion_clear_seed_slot(
    Th07PspMeItemMotionSeed *seed, uint32_t slot)
{
    if (!seed || slot >= TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS)
        return;
    const uint32_t bit = 1u << (slot & 31u);
    if ((seed->candidateBits[slot >> 5u] & bit) != 0u)
    {
        seed->candidateBits[slot >> 5u] &= ~bit;
        if (seed->header.candidateCount != 0u)
            --seed->header.candidateCount;
    }
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    seed->stateBit0[slot >> 5u] &= ~bit;
    seed->stateBit1[slot >> 5u] &= ~bit;
    seed->autoCollectBits[slot >> 5u] &= ~bit;
#endif
    memset(&seed->slots[slot], 0, sizeof(seed->slots[slot]));
}
#endif

static uint32_t me_bullet_compact_update_kernel(
    const Th07PspMeBulletCompactJob *job,
    const Th07PspMeBulletCompactSeed *seed,
    Th07PspMeBulletCompactOutput *output,
    uint32_t *outCandidateCount, uint32_t *outInBoundsCount,
    uint32_t *outNoCollisionCount, uint32_t *outFirstBadSlot)
{
    *outCandidateCount = 0u;
    *outInBoundsCount = 0u;
    *outNoCollisionCount = 0u;
    *outFirstBadSlot = 0xffffffffu;
    memset(output, 0, sizeof(*output));

    if (!me_bullet_compact_seed_header_valid(seed, job->seedBank) ||
        seed->header.frameSeq != job->seedFrameSeq ||
        seed->header.targetDrawSeq != job->seedTargetDrawSeq ||
        seed->header.stageEpoch != job->stageEpoch ||
        seed->header.managerEpoch != job->managerEpoch ||
        seed->header.replayEpoch != job->replayEpoch)
        return TH07_PSP_ME_BULLET_COMPACT_RESULT_SEED;

    if ((job->flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID) != 0u)
    {
        const unsigned char *bombs =
            (const unsigned char *)(0x80000000u | job->bombClearBasePhys);
        for (uint32_t bombIndex = 0u;
             bombIndex < job->bombClearHighWater; ++bombIndex)
        {
            const unsigned char *bomb =
                bombs + bombIndex * ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE;
            const uint32_t bits[] = {
                me_render_stream_load_u32(
                    bomb, ME_BULLET_COMPACT_BOMB_CLEAR_POS_X_OFFSET),
                me_render_stream_load_u32(
                    bomb, ME_BULLET_COMPACT_BOMB_CLEAR_POS_Y_OFFSET),
                me_render_stream_load_u32(
                    bomb, ME_BULLET_COMPACT_BOMB_CLEAR_POS_Z_OFFSET),
                me_render_stream_load_u32(
                    bomb, ME_BULLET_COMPACT_BOMB_CLEAR_SIZE_X_OFFSET),
                me_render_stream_load_u32(
                    bomb, ME_BULLET_COMPACT_BOMB_CLEAR_SIZE_Y_OFFSET)
            };
            for (uint32_t field = 0u; field < 5u; ++field)
            {
                if (!me_render_stream_float_bits_finite(bits[field]))
                    return TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD;
            }
        }
    }

    uint32_t bitmapCount = 0u;
    for (uint32_t wordIndex = 0u;
         wordIndex < TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS; ++wordIndex)
    {
        const uint32_t candidates = seed->candidateBits[wordIndex];
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
        const uint32_t inBoundsBits = seed->inBoundsBits[wordIndex];
        if ((inBoundsBits & ~candidates) != 0u)
            return TH07_PSP_ME_BULLET_COMPACT_RESULT_SEED;
        for (uint32_t bitIndex = 0u; bitIndex < 32u; ++bitIndex)
        {
            const uint32_t slotBit = 1u << bitIndex;
            if ((candidates & slotBit) == 0u)
                continue;
            const uint32_t slot = wordIndex * 32u + bitIndex;
            *outFirstBadSlot = slot;
            ++bitmapCount;
            const uint32_t generation =
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, generation);
            if (generation == 0u)
                return TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD;
            const uint32_t values[] = {
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posXBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posYBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posZBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityXBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityYBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityZBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteWidthBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteHeightBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeXBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeYBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosXBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosYBits),
                TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosZBits)
            };
            for (uint32_t index = 0u;
                 index < sizeof(values) / sizeof(values[0]); ++index)
            {
                if (!me_render_stream_float_bits_finite(values[index]))
                    return TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD;
            }

            const float nextPosX =
                me_render_bits_float(
                    TH07_PSP_ME_BULLET_SEED_FIELD(
                        seed, slot, nextPosXBits));
            const float nextPosY =
                me_render_bits_float(
                    TH07_PSP_ME_BULLET_SEED_FIELD(
                        seed, slot, nextPosYBits));

            int collisionInputsValid = 0;
            const int noCollision = me_bullet_compact_no_collision(
                job, nextPosX, nextPosY,
                me_render_bits_float(
                    TH07_PSP_ME_BULLET_SEED_FIELD(
                        seed, slot, grazeSizeXBits)),
                me_render_bits_float(
                    TH07_PSP_ME_BULLET_SEED_FIELD(
                        seed, slot, grazeSizeYBits)),
                &collisionInputsValid);
            if (!collisionInputsValid)
                return TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD;

            Th07PspMeBulletCompactSlotResult *result =
                &output->slots[slot];
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
            result->posXBits = TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosXBits);
            result->posYBits = TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosYBits);
            result->posZBits = TH07_PSP_ME_BULLET_SEED_FIELD(
                seed, slot, nextPosZBits);
#endif
            result->generation = (uint16_t)generation;
            result->flags = TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE;
            output->candidateBits[wordIndex] |= slotBit;
            ++*outCandidateCount;
            if ((inBoundsBits & slotBit) != 0u)
            {
                result->flags |=
                    TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS;
                ++*outInBoundsCount;
            }
            if (noCollision)
            {
                result->flags |=
                    TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION;
                ++*outNoCollisionCount;
            }
        }
#else
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
        const uint32_t inBoundsBits = seed->inBoundsBits[wordIndex];
        if ((inBoundsBits & ~candidates) != 0u)
            return TH07_PSP_ME_BULLET_COMPACT_RESULT_SEED;
#endif
        for (uint32_t bitIndex = 0u; bitIndex < 32u; ++bitIndex)
        {
            const uint32_t slotBit = 1u << bitIndex;
            if ((candidates & slotBit) == 0u)
                continue;
            const uint32_t slot = wordIndex * 32u + bitIndex;
            *outFirstBadSlot = slot;
            ++bitmapCount;
            const Th07PspMeBulletCompactSeedSlot *input =
                &seed->slots[slot];
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
            if (input->generation == 0u)
#else
            if (input->generation == 0u || input->reserved != 0u ||
                (input->staticFlags &
                 ~(TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
                   TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS)) != 0u ||
                (input->staticFlags &
                 TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE) == 0u)
#endif
                return TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD;
            const uint32_t values[] = {
                input->posXBits, input->posYBits, input->posZBits,
                input->velocityXBits, input->velocityYBits,
                input->velocityZBits, input->spriteWidthBits,
                input->spriteHeightBits, input->grazeSizeXBits,
                input->grazeSizeYBits, input->nextPosXBits,
                input->nextPosYBits, input->nextPosZBits
            };
            for (uint32_t index = 0u;
                 index < sizeof(values) / sizeof(values[0]); ++index)
            {
                if (!me_render_stream_float_bits_finite(values[index]))
                    return TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD;
            }

            const float nextPosX =
                me_render_bits_float(input->nextPosXBits);
            const float nextPosY =
                me_render_bits_float(input->nextPosYBits);

            int collisionInputsValid = 0;
            const int noCollision = me_bullet_compact_no_collision(
                job, nextPosX, nextPosY,
                me_render_bits_float(input->grazeSizeXBits),
                me_render_bits_float(input->grazeSizeYBits),
                &collisionInputsValid);
            if (!collisionInputsValid)
                return TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD;

            Th07PspMeBulletCompactSlotResult *result =
                &output->slots[slot];
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
            result->posXBits = input->nextPosXBits;
            result->posYBits = input->nextPosYBits;
            result->posZBits = input->nextPosZBits;
#endif
            result->generation = (uint16_t)input->generation;
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
            result->flags = TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE;
#else
            result->flags = (uint16_t)input->staticFlags;
#endif
            output->candidateBits[wordIndex] |= slotBit;
            ++*outCandidateCount;
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
            if ((inBoundsBits & slotBit) != 0u)
#else
            if ((input->staticFlags &
                 TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS) != 0u)
#endif
            {
                result->flags |=
                    TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS;
                ++*outInBoundsCount;
            }
            if (noCollision)
            {
                result->flags |=
                    TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION;
                ++*outNoCollisionCount;
            }
        }
#endif
    }
    if (bitmapCount != seed->header.candidateCount)
        return TH07_PSP_ME_BULLET_COMPACT_RESULT_SEED;
    *outFirstBadSlot = 0xffffffffu;
    return TH07_PSP_ME_BULLET_COMPACT_RESULT_OK;
}

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
// Keep the three libm calls distinct.  The PSP binary audit requires direct
// calls to the same linked scalar newlib functions used by ItemManager and
// Player; VFPU approximations and compiler sincos fusion are forbidden.
static __attribute__((noinline)) float me_item_motion_atan2(float y, float x)
{
    return atan2f(y, x);
}

static __attribute__((noinline)) float me_item_motion_cos(float angle)
{
    return cosf(angle);
}

static __attribute__((noinline)) float me_item_motion_sin(float angle)
{
    return sinf(angle);
}

static uint32_t me_item_motion_update_kernel(
    const Th07PspMeBulletCompactJob *job,
    const Th07PspMeItemMotionSeed *seed,
    Th07PspMeItemMotionOutput *output,
    uint32_t *outCandidateCount, uint32_t *outProcessedCount,
    uint32_t *outFirstBadSlot)
{
    *outCandidateCount = 0u;
    *outProcessedCount = 0u;
    *outFirstBadSlot = 0xffffffffu;
    memset(output, 0, offsetof(Th07PspMeItemMotionOutput, slots));
    output->header.version = TH07_PSP_ME_ITEM_MOTION_OUTPUT_VERSION;
    output->header.headerBytes = sizeof(output->header);
    output->header.outputBytes = sizeof(*output);
    output->header.bank = job->seedBank;
    output->header.frameSeq = job->frameSeq;
    output->header.seedFrameSeq = job->seedFrameSeq;
    output->header.seedTargetDrawSeq = job->seedTargetDrawSeq;
    output->header.result = TH07_PSP_ME_ITEM_MOTION_RESULT_DISABLED;
    output->header.candidateLimit = job->itemMotionCandidateLimit;
    output->header.firstBadSlot = 0xffffffffu;
    output->header.committed = 0u;

    if ((job->flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) == 0u)
        return TH07_PSP_ME_ITEM_MOTION_RESULT_DISABLED;
    if (!me_item_motion_seed_header_valid(seed, job->seedBank) ||
        seed->header.frameSeq != job->seedFrameSeq ||
        seed->header.targetDrawSeq != job->seedTargetDrawSeq ||
        seed->header.stageEpoch != job->stageEpoch ||
        seed->header.managerEpoch != job->managerEpoch)
        return TH07_PSP_ME_ITEM_MOTION_RESULT_SEED;

    const uint32_t lastWord = TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS - 1u;
    if ((seed->candidateBits[lastWord] & 0xfffff000u) != 0u)
        return TH07_PSP_ME_ITEM_MOTION_RESULT_SEED;
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    if (((seed->stateBit0[lastWord] | seed->stateBit1[lastWord] |
          seed->autoCollectBits[lastWord]) & 0xfffff000u) != 0u)
        return TH07_PSP_ME_ITEM_MOTION_RESULT_SEED;
#endif

    for (uint32_t word = TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS;
         word < TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS; ++word)
    {
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
        if (seed->candidateBits[word] != 0u ||
            seed->stateBit0[word] != 0u ||
            seed->stateBit1[word] != 0u ||
            seed->autoCollectBits[word] != 0u)
#else
        if (seed->candidateBits[word] != 0u)
#endif
            return TH07_PSP_ME_ITEM_MOTION_RESULT_SEED;
    }

    const float playerX = me_render_bits_float(job->itemPlayerPosXBits);
    const float playerY = me_render_bits_float(job->itemPlayerPosYBits);
    const float collectSpeed =
        me_render_bits_float(job->itemCollectSpeedBits);
    const float pocY = me_render_bits_float(job->itemPocYBits);
    const float multiplier =
        me_render_bits_float(job->itemFramerateMultiplierBits);

    uint32_t bitmapCount = 0u;
    for (uint32_t wordIndex = 0u;
         wordIndex < TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS; ++wordIndex)
    {
        const uint32_t candidates = seed->candidateBits[wordIndex];
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
        const uint32_t stateBit0 = seed->stateBit0[wordIndex];
        const uint32_t stateBit1 = seed->stateBit1[wordIndex];
        const uint32_t autoCollectBits =
            seed->autoCollectBits[wordIndex];
        if (((stateBit0 | stateBit1 | autoCollectBits) & ~candidates) != 0u)
            return TH07_PSP_ME_ITEM_MOTION_RESULT_SEED;
#endif
        bitmapCount += (uint32_t)__builtin_popcount(candidates);
        for (uint32_t bitIndex = 0u; bitIndex < 32u; ++bitIndex)
        {
            const uint32_t slotBit = 1u << bitIndex;
            if ((candidates & slotBit) == 0u ||
                *outProcessedCount >= job->itemMotionCandidateLimit)
                continue;
            const uint32_t slot = wordIndex * 32u + bitIndex;
            if (slot >= TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS)
                return TH07_PSP_ME_ITEM_MOTION_RESULT_SEED;
            *outFirstBadSlot = slot;
            const Th07PspMeItemMotionSeedSlot *input = &seed->slots[slot];
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
            const uint32_t state =
                ((stateBit0 & slotBit) != 0u ? 1u : 0u) |
                ((stateBit1 & slotBit) != 0u ? 2u : 0u);
            const uint32_t autoCollect =
                (autoCollectBits & slotBit) != 0u ? 1u : 0u;
            if (input->generation == 0u || state > 2u)
#else
            const uint32_t state = input->stateAndFlags &
                TH07_PSP_ME_ITEM_MOTION_INPUT_STATE_MASK;
            const uint32_t autoCollect =
                (input->stateAndFlags >>
                 TH07_PSP_ME_ITEM_MOTION_INPUT_AUTOCOLLECT_SHIFT) & 0xffu;
            const uint32_t inUse =
                (input->stateAndFlags >>
                 TH07_PSP_ME_ITEM_MOTION_INPUT_INUSE_SHIFT) & 0xffu;
            if (input->generation == 0u || state > 2u ||
                autoCollect > 1u || inUse != 1u ||
                (input->stateAndFlags & 0xff000000u) != 0u ||
                input->reserved0 != 0u || input->reserved1 != 0u ||
                input->reserved2 != 0u)
#endif
                return TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD;

            const uint32_t usedBits[] = {
                input->posXBits, input->posYBits, input->posZBits,
                input->startXBits, input->startYBits, input->startZBits
            };
            for (uint32_t index = 0u;
                 index < sizeof(usedBits) / sizeof(usedBits[0]); ++index)
            {
                if (!me_item_motion_float_bits_supported(usedBits[index]))
                    return TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD;
            }
            if (state == 2u &&
                (!me_item_motion_float_bits_supported(input->targetXBits) ||
                 !me_item_motion_float_bits_supported(input->targetYBits) ||
                 !me_item_motion_float_bits_supported(input->targetZBits) ||
                 !me_item_motion_float_bits_supported(
                     input->timerSubFrameBits)))
                return TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD;

            float posX = me_render_bits_float(input->posXBits);
            float posY = me_render_bits_float(input->posYBits);
            float posZ = me_render_bits_float(input->posZBits);
            float startX = me_render_bits_float(input->startXBits);
            float startY = me_render_bits_float(input->startYBits);
            float startZ = me_render_bits_float(input->startZBits);
            uint32_t nextState = state;
            uint32_t nextAutoCollect = autoCollect;
            uint32_t route = 0u;
            uint32_t gotoCollision = 0u;

            if (state == 2u && input->timerCurrent < 60)
            {
                volatile float timerValue =
                    (float)input->timerCurrent +
                    me_render_bits_float(input->timerSubFrameBits);
                volatile float t = timerValue / 60.0f;
                volatile float oneMinusT = 1.0f - t;
                volatile float startTermX = startX * oneMinusT;
                volatile float startTermY = startY * oneMinusT;
                volatile float startTermZ = startZ * oneMinusT;
                volatile float targetTermX =
                    me_render_bits_float(input->targetXBits) * t;
                volatile float targetTermY =
                    me_render_bits_float(input->targetYBits) * t;
                volatile float targetTermZ =
                    me_render_bits_float(input->targetZBits) * t;
                posX = targetTermX + startTermX;
                posY = targetTermY + startTermY;
                posZ = targetTermZ + startTermZ;
                route = TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_INTERP;
                gotoCollision =
                    TH07_PSP_ME_ITEM_MOTION_RESULT_GOTO_COLLISION;
            }
            else
            {
                if (state == 2u)
                {
                    if (input->timerCurrent == 60)
                    {
                        startX = 0.0f;
                        startY = 0.0f;
                        startZ = 0.0f;
                        nextState = 0u;
                        route =
                            TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_STATE2_60;
                    }
                    else
                    {
                        route =
                            TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_STATE2_LATE;
                    }
                }
                else
                {
                    const int pull = state == 1u ||
                        (((job->itemCurrentPowerClass >= 128 ||
                           job->itemDifficulty >= 4) && playerY < pocY) ||
                         job->itemHasBorder == 1u);
                    if (pull)
                    {
                        if (job->playerState != 1u)
                        {
                            volatile float dx = playerX - posX;
                            volatile float dy = playerY - posY;
                            float angle;
                            if (dy == 0.0f && dx == 0.0f)
                                angle = 1.5707964f;
                            else
                                angle = me_item_motion_atan2(dy, dx);
                            startX =
                                me_item_motion_cos(angle) * collectSpeed;
                            startY =
                                me_item_motion_sin(angle) * collectSpeed;
                            nextState = 1u;
                            if (job->itemHasBorder == 1u)
                                nextAutoCollect = 1u;
                            route =
                                TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_HOME;
                        }
                        else
                        {
                            startY = -0.5f;
                            nextState = 0u;
                            route =
                                TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_SPAWN;
                        }
                    }
                    else
                    {
                        startX = 0.0f;
                        startZ = 0.0f;
                        if (startY < -2.2f)
                            startY = -2.2f;
                        route = TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_FALL;
                    }
                }

                volatile float deltaX = startX * multiplier;
                volatile float deltaY = startY * multiplier;
                volatile float deltaZ = startZ * multiplier;
                posX = posX + deltaX;
                posY = posY + deltaY;
                posZ = posZ + deltaZ;
            }

            const uint32_t outputBits[] = {
                me_render_float_bits(posX), me_render_float_bits(posY),
                me_render_float_bits(posZ), me_render_float_bits(startX),
                me_render_float_bits(startY), me_render_float_bits(startZ)
            };
            for (uint32_t index = 0u;
                 index < sizeof(outputBits) / sizeof(outputBits[0]); ++index)
            {
                if (!me_item_motion_float_bits_supported(outputBits[index]))
                    return TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD;
            }

            Th07PspMeItemMotionSlotResult *result = &output->slots[slot];
            result->generation = input->generation;
            result->posXBits = outputBits[0];
            result->posYBits = outputBits[1];
            result->posZBits = outputBits[2];
            result->startXBits = outputBits[3];
            result->startYBits = outputBits[4];
            result->startZBits = outputBits[5];
            result->stateAndRoute =
                nextState |
                (nextAutoCollect <<
                 TH07_PSP_ME_ITEM_MOTION_RESULT_AUTOCOLLECT_SHIFT) |
                TH07_PSP_ME_ITEM_MOTION_RESULT_CANDIDATE |
                gotoCollision | route;
            output->candidateBits[wordIndex] |= slotBit;
            ++*outProcessedCount;
        }
    }

    *outCandidateCount = bitmapCount;
    if (bitmapCount != seed->header.candidateCount ||
        *outProcessedCount > bitmapCount)
        return TH07_PSP_ME_ITEM_MOTION_RESULT_SEED;
    *outFirstBadSlot = 0xffffffffu;
    output->header.result = TH07_PSP_ME_ITEM_MOTION_RESULT_OK;
    output->header.candidateCount = bitmapCount;
    output->header.processedCount = *outProcessedCount;
    output->header.firstBadSlot = 0xffffffffu;
    return TH07_PSP_ME_ITEM_MOTION_RESULT_OK;
}
#endif
#endif
#endif

static __attribute__((always_inline)) inline float
me_render_stream_floor(float value)
{
    // Match PspBulletFloor exactly.  Packet validation proves finite input in
    // the accepted COP1 conversion range before this is reached.
    float result;
    __asm__ volatile("floor.w.s %0, %1\n\t"
                     "cvt.s.w %0, %0"
                     : "=&f"(result)
                     : "f"(value));
    return result;
}

static uint32_t me_render_stream_color(uint32_t color, uint32_t factor,
                                       uint32_t enabled)
{
    if (enabled)
    {
        uint32_t multiplied = 0u;
        for (uint32_t shift = 0u; shift < 32u; shift += 8u)
        {
            uint32_t value = ((color >> shift) & 0xffu) *
                             ((factor >> shift) & 0xffu);
            value >>= 7;
            if (value >= 256u)
                value = 255u;
            multiplied |= value << shift;
        }
        color = multiplied;
    }
    // PspGuColor: ZunColor is BGRA in memory while GU expects RGBA.
    return (color & 0xff00ff00u) | ((color & 0x00ff0000u) >> 16) |
           ((color & 0x000000ffu) << 16);
}

static int me_render_stream_owned_pools_valid(
    uint32_t slot, uint32_t inputPhys, uint32_t inputCapacity,
    uint32_t outputPhys, uint32_t outputCapacity,
    uint32_t runPhys, uint32_t runCapacity)
{
    if (slot >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT)
        return 0;
    const uint32_t expectedInput =
        (uint32_t)gMeRenderStreamInputAreas[slot].records & 0x1fffffffu;
    const uint32_t expectedOutput =
        (uint32_t)gMeRenderStreamOutputAreas[slot].vertices & 0x1fffffffu;
    const uint32_t expectedRuns =
        (uint32_t)gMeRenderStreamRunAreas[slot].runs & 0x1fffffffu;
    return inputPhys == expectedInput && outputPhys == expectedOutput &&
           runPhys == expectedRuns &&
           inputCapacity == sizeof(gMeRenderStreamInputAreas[slot].records) &&
           outputCapacity == sizeof(gMeRenderStreamOutputAreas[slot].vertices) &&
           runCapacity == sizeof(gMeRenderStreamRunAreas[slot].runs);
}

#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
static __attribute__((always_inline)) inline uint32_t
me_render_stream_float_order_key(uint32_t bits)
{
    return (bits & 0x80000000u) ? ~bits : (bits ^ 0x80000000u);
}

static __attribute__((always_inline)) inline int
me_render_stream_finite_bits_le(uint32_t lhsBits, uint32_t rhsBits)
{
    // Callers reject NaN/infinity first.  Ordered IEEE-754 binary32 values
    // can then be compared as monotonic integer keys; treat -0 and +0 as the
    // equality that COP1 comparison defines.  This validator runs before the
    // render command installs its canonical FCR31, so it must not execute a
    // c.lt.s/c.le.s that could leak FCC0 on an early bounds rejection.
    if (((lhsBits | rhsBits) & 0x7fffffffu) == 0u)
        return 1;
    return me_render_stream_float_order_key(lhsBits) <=
           me_render_stream_float_order_key(rhsBits);
}
#endif

static int me_render_stream_bounds_valid(
    uint32_t version, uint32_t flags, uint32_t slot, uint32_t generation,
    const uint32_t bucketEnds[6], uint32_t recordCount,
    uint32_t offsetXBits, uint32_t offsetYBits,
    uint32_t viewportLeftBits, uint32_t viewportTopBits,
    uint32_t viewportRightBits, uint32_t viewportBottomBits,
    uint32_t configFlags,
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    const Th07PspMeRenderRawLayout *rawLayout,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const Th07PspMeRenderListLayout *listLayout,
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    const Th07PspMeRenderItemLayout *itemLayout,
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    const Th07PspMeRenderEffectLayout *effectLayout,
#endif
#endif
#endif
#endif
    uint32_t inputPhys, uint32_t inputCapacity,
    uint32_t outputPhys, uint32_t outputCapacity,
    uint32_t runPhys, uint32_t runCapacity,
    uint32_t *requiredInput)
{
    if (requiredInput)
        *requiredInput = 0u;
    uint32_t allowedJobFlags =
        TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH |
        TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    allowedJobFlags |= TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    allowedJobFlags |= TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    allowedJobFlags |= TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    allowedJobFlags |= TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED;
#endif
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    allowedJobFlags |= TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST;
#endif
#endif
#endif
#endif
    uint32_t expectedVersion = TH07_PSP_ME_RENDER_STREAM_VERSION;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    if ((flags & TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE) != 0u)
        expectedVersion = TH07_PSP_ME_RENDER_STREAM_RAW_VERSION;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const uint32_t directList =
        (flags & TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST) != 0u;
    if (directList)
        expectedVersion = TH07_PSP_ME_RENDER_STREAM_LIST_VERSION;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    const uint32_t itemList =
        (flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    const uint32_t itemMotionSeed =
        (flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED) != 0u;
    if (itemMotionSeed && !itemList)
        return 0;
#endif
    if (itemList)
        expectedVersion = TH07_PSP_ME_RENDER_STREAM_ITEM_VERSION;
    if (itemList && !directList)
        return 0;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    const uint32_t effectList =
        (flags & TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST) != 0u;
    if (effectList)
        expectedVersion = TH07_PSP_ME_RENDER_STREAM_EFFECT_VERSION;
    // The Effect segment reuses the I-ME7 physical auxiliary pool but is
    // transactionally independent from the optional Item prefix. A frame
    // whose Item preparation rejected may still publish Effect at offset 0.
    if (effectList && !directList)
        return 0;
#endif
#endif
    if (directList &&
        ((flags & TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE) != 0u ||
         (flags & TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH) != 0u))
        return 0;
#endif
#endif
    if (version != expectedVersion || generation == 0u ||
        slot >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT ||
        (flags & ~allowedJobFlags) != 0u ||
        (configFlags & ~(TH07_PSP_ME_RENDER_STREAM_CONFIG_COLOR_MUL |
                         TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z)) != 0u ||
        recordCount > TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS ||
        (inputPhys & 63u) != 0u || (outputPhys & 63u) != 0u ||
        (runPhys & 63u) != 0u)
        return 0;

    uint32_t previous = 0u;
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
    {
        if (bucketEnds[bucket] < previous || bucketEnds[bucket] > recordCount)
            return 0;
        previous = bucketEnds[bucket];
    }
    if (previous != recordCount ||
        !me_render_stream_float_bits_finite(offsetXBits) ||
        !me_render_stream_float_bits_finite(offsetYBits) ||
        !me_render_stream_float_bits_finite(viewportLeftBits) ||
        !me_render_stream_float_bits_finite(viewportTopBits) ||
        !me_render_stream_float_bits_finite(viewportRightBits) ||
        !me_render_stream_float_bits_finite(viewportBottomBits))
        return 0;

#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    if (!me_render_stream_finite_bits_le(
            viewportLeftBits, viewportRightBits) ||
        !me_render_stream_finite_bits_le(
            viewportTopBits, viewportBottomBits))
#else
    const float left = me_render_bits_float(viewportLeftBits);
    const float top = me_render_bits_float(viewportTopBits);
    const float right = me_render_bits_float(viewportRightBits);
    const float bottom = me_render_bits_float(viewportBottomBits);
    if (left > right || top > bottom)
#endif
        return 0;

    uint32_t inputStride = TH07_PSP_ME_RENDER_STREAM_RECORD_BYTES;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    if ((flags & TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE) != 0u)
    {
        if (!me_render_stream_raw_layout_valid(rawLayout))
            return 0;
        inputStride = TH07_PSP_ME_RENDER_STREAM_RAW_RECORD_BYTES;
    }
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    else if (directList)
    {
        if (!me_render_stream_raw_layout_valid(rawLayout) ||
            !me_render_stream_list_layout_valid(listLayout, rawLayout) ||
            recordCount > listLayout->bulletCount)
            return 0;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        if (itemList && !me_render_stream_item_layout_valid(
                            itemLayout, rawLayout,
                            itemLayout->expectedItemCount))
            return 0;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        if (effectList &&
            (!me_render_stream_effect_layout_valid(effectLayout, rawLayout) ||
             (itemList ? itemLayout->expectedItemCount : 0u) +
                     effectLayout->expectedLayer0Count +
                     effectLayout->expectedLayer3Count >
                 TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS))
            return 0;
#endif
#endif
        inputStride = 0u;
        uint32_t bucketStart = 0u;
        for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        {
            const uint32_t bucketCount = bucketEnds[bucket] - bucketStart;
            if ((bucketCount == 0u) !=
                (listLayout->bucketHeadPhys[bucket] == 0u))
                return 0;
            bucketStart = bucketEnds[bucket];
        }
    }
#endif
#endif
    const uint32_t inputNeeded = recordCount * inputStride;
    if (!me_render_stream_owned_pools_valid(
            slot, inputPhys, inputCapacity, outputPhys, outputCapacity,
            runPhys, runCapacity) || inputCapacity < inputNeeded ||
        outputCapacity < TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES ||
        runCapacity < TH07_PSP_ME_RENDER_STREAM_MAX_RUNS *
                          sizeof(Th07PspMeRenderStreamRun) ||
        !me_render_main_ram_range_valid(inputPhys, inputCapacity) ||
        !me_render_main_ram_range_valid(outputPhys, outputCapacity) ||
        !me_render_main_ram_range_valid(runPhys, runCapacity) ||
        me_render_ranges_overlap(inputPhys, inputCapacity,
                                 outputPhys, outputCapacity) ||
        me_render_ranges_overlap(inputPhys, inputCapacity,
                                 runPhys, runCapacity) ||
        me_render_ranges_overlap(outputPhys, outputCapacity,
                                 runPhys, runCapacity))
        return 0;
    if (requiredInput)
        *requiredInput = inputNeeded;
    return 1;
}

static __attribute__((always_inline)) inline int
me_render_stream_record_valid_capacity(
    const Th07PspMeRenderStreamRecord *record, uint32_t slotCapacity)
{
    const uint32_t allowedFlags =
        TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE |
        TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED |
        TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD |
        TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE |
        TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK |
        TH07_PSP_ME_RENDER_STREAM_RECORD_RUN_BREAK;
    if ((record->flags & ~allowedFlags) != 0u ||
        record->slot >= slotCapacity ||
        record->slotGeneration == 0u)
        return 0;
    if ((record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE) == 0u)
        return 1;
    if ((record->sourceAndState & 0xffffu) >= 264u)
        return 0;
    if (!me_render_stream_float_bits_finite(record->posXBits) ||
        !me_render_stream_float_bits_finite(record->posYBits) ||
        !me_render_stream_float_bits_finite(record->posZBits) ||
        !me_render_stream_float_bits_finite(record->halfWidthBits) ||
        !me_render_stream_float_bits_finite(record->halfHeightBits) ||
        !me_render_stream_float_bits_finite(record->u0Bits) ||
        !me_render_stream_float_bits_finite(record->u1Bits) ||
        !me_render_stream_float_bits_finite(record->v0Bits) ||
        !me_render_stream_float_bits_finite(record->v1Bits))
        return 0;
    if ((record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED) != 0u &&
        (!me_render_stream_float_bits_finite(record->sinBits) ||
         !me_render_stream_float_bits_finite(record->cosBits)))
        return 0;
    return 1;
}

static __attribute__((always_inline)) inline int me_render_stream_record_valid(
    const Th07PspMeRenderStreamRecord *record)
{
    return me_render_stream_record_valid_capacity(
        record, TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS);
}

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
static __attribute__((always_inline)) inline int
me_render_stream_axis_floor_inputs_valid(
    const Th07PspMeRenderStreamRecord *record,
    uint32_t offsetXBits, uint32_t offsetYBits)
{
    if ((record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE) == 0u ||
        (record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED) != 0u)
        return 1;

    const float posX = me_render_bits_float(record->posXBits);
    const float posY = me_render_bits_float(record->posYBits);
    const float halfWidth = me_render_bits_float(record->halfWidthBits);
    const float halfHeight = me_render_bits_float(record->halfHeightBits);
    const float offsetX = me_render_bits_float(offsetXBits);
    const float offsetY = me_render_bits_float(offsetYBits);
    const uint32_t anchor =
        (record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK) >>
        TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT;
    const float rawLeft = (anchor & 1u) ? posX : posX - halfWidth;
    const float rawRight = (anchor & 1u) ? posX + halfWidth * 2.0f
                                          : posX + halfWidth;
    const float rawTop = (anchor & 2u) ? posY : posY - halfHeight;
    const float rawBottom = (anchor & 2u) ? posY + halfHeight * 2.0f
                                           : posY + halfHeight;
    const float left = rawLeft + offsetX + 0.5f;
    const float right = rawRight + offsetX + 0.5f;
    const float top = rawTop + offsetY + 0.5f;
    const float bottom = rawBottom + offsetY + 0.5f;
    const float cop1Limit = 2147483520.0f;

    return me_render_stream_float_bits_finite(me_render_float_bits(left)) &&
           me_render_stream_float_bits_finite(me_render_float_bits(right)) &&
           me_render_stream_float_bits_finite(me_render_float_bits(top)) &&
           me_render_stream_float_bits_finite(me_render_float_bits(bottom)) &&
           left >= -cop1Limit && left <= cop1Limit &&
           right >= -cop1Limit && right <= cop1Limit &&
           top >= -cop1Limit && top <= cop1Limit &&
           bottom >= -cop1Limit && bottom <= cop1Limit;
}
#endif

#if defined(TH07_PSP_ME_RENDER_XYZ16)
static __attribute__((always_inline)) inline int
me_render_stream_pack_s16(float value, float scale, int16_t *packed)
{
    const float scaled = value * scale;
    // The ordered comparison rejects NaN as well as either infinity.  Never
    // clamp an unrepresentable live value: command 10 rejects the stream and
    // the canonical float SC path draws it instead.
    if (!(scaled >= -32768.0f && scaled < 32767.5f))
        return 0;
    int32_t rounded;
    if (scaled <= -32767.5f)
        rounded = -32768;
    else
        rounded = (int32_t)(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
    *packed = (int16_t)rounded;
    return 1;
}
#endif

#if defined(TH07_PSP_ME_RENDER_UV16)
static __attribute__((always_inline)) inline int
me_render_stream_pack_u16(uint32_t bits, uint16_t *packed)
{
    // PSP GE decodes GU_TEXTURE_16BIT as unsigned q/32768.  Convert the
    // source IEEE-754 word to Q15 using integer arithmetic only: COP1 casts
    // on the custom core can alter FCR31 even when the requested rounding is
    // otherwise exact.  Preserve the old round-half-up domain, including
    // accepting negative zero while rejecting every negative nonzero,
    // NaN/infinity and anything that rounds beyond 65535.
    const uint32_t magnitude = bits & 0x7fffffffu;
    const uint32_t exponent = (magnitude >> 23u) & 0xffu;
    const uint32_t fraction = magnitude & 0x007fffffu;
    if (exponent == 0xffu || ((bits & 0x80000000u) && magnitude != 0u))
        return 0;

    // Zero and every binary32 subnormal are far below half of one Q15 unit.
    if (exponent == 0u)
    {
        *packed = 0u;
        return 1;
    }
    // A biased exponent of 128 is already at least +2.0 -> Q15 65536.
    if (exponent >= 128u)
        return 0;

    const uint32_t significand = 0x00800000u | fraction;
    const uint32_t rightShift = 135u - exponent;
    const uint32_t roundedQ = rightShift >= 32u
        ? 0u
        : (significand + (1u << (rightShift - 1u))) >> rightShift;
    // Legacy COP1 did the bias addition in binary32.  The single predecessor
    // of +0.5 Q15 (scaled bits 0x3effffff) lands exactly halfway between the
    // two floats below/at 1.0; round-to-nearest-even selected 1.0.  Preserve
    // that observable byte result without executing COP1.
    if (bits == 0x377fffffu)
    {
        *packed = 1u;
        return 1;
    }
    if (roundedQ > 65535u)
        return 0;
    *packed = (uint16_t)roundedQ;
    return 1;
}
#endif

static int me_render_stream_write_vertex(
    Th07PspMeRenderStreamVertex *vertex, uint32_t uBits, uint32_t vBits,
    uint32_t color, float x, float y, uint32_t zBits)
{
#if defined(TH07_PSP_ME_RENDER_UV16)
    uint16_t packedU;
    uint16_t packedV;
    if (!me_render_stream_pack_u16(uBits, &packedU) ||
        !me_render_stream_pack_u16(vBits, &packedV))
        return 0;
#endif
#if defined(TH07_PSP_ME_RENDER_XYZ16)
    int16_t packedX;
    int16_t packedY;
    int16_t packedZ;
    // X/Y use 5 fractional bits.  GE's candidate MODEL scale restores them
    // with 1024x; Z retains the normalized signed-16 mapping and scale 1x so
    // 0.01/0.05 depth values are not collapsed by the screen-space scale.
    if (!me_render_stream_pack_s16(x, 32.0f, &packedX) ||
        !me_render_stream_pack_s16(y, 32.0f, &packedY) ||
        !me_render_stream_pack_s16(
            me_render_bits_float(zBits), 32768.0f, &packedZ))
        return 0;
#endif
#if defined(TH07_PSP_ME_RENDER_UV16)
    vertex->u = packedU;
    vertex->v = packedV;
#else
    vertex->uBits = uBits;
    vertex->vBits = vBits;
#endif
    vertex->color = color;
#if defined(TH07_PSP_ME_RENDER_XYZ16)
    vertex->x = packedX;
    vertex->y = packedY;
    vertex->z = packedZ;
    vertex->reserved = 0u;
#else
    vertex->xBits = me_render_float_bits(x);
    vertex->yBits = me_render_float_bits(y);
    vertex->zBits = zBits;
#endif
    return 1;
}

#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
static __attribute__((always_inline)) inline void
me_render_stream_list_prefetch(const void *address)
{
    // Allegrex cache op 0x1e is the proven read-prefetch used by the SC
    // bullet fast path.  Every address reaches here only after pool/stride
    // validation, and the hint has no architectural data dependency.
    __asm__ volatile("cache 0x1e, 0(%0)" : : "r"(address));
}
#endif

static __attribute__((always_inline)) inline int
me_render_stream_reconstruct_raw_record(
    uint32_t posXBits, uint32_t posYBits,
    uint32_t sinBits, uint32_t cosBits, uint32_t vmPhys,
    uint32_t logicalState, uint32_t slot, uint32_t generation,
    const Th07PspMeRenderRawLayout *layout,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const unsigned char *bullet,
    const Th07PspMeRenderListLayout *listLayout, uint32_t directList,
#endif
    Th07PspMeRenderStreamRecord *record)
{
    if (!record || !layout || logicalState < 1u || logicalState > 5u ||
        slot >= layout->bulletCount || generation == 0u ||
        !me_render_stream_float_bits_finite(posXBits) ||
        !me_render_stream_float_bits_finite(posYBits))
        return 0;

    memset(record, 0, sizeof(*record));
    record->posXBits = posXBits;
    record->posYBits = posYBits;
    // Bullet::Draw publishes this fixed depth before AnmManager consumes it.
    record->posZBits = 0x3d4ccccdu; // 0.05f
    record->sinBits = sinBits;
    record->cosBits = cosBits;
    record->slot = slot;
    record->slotGeneration = generation;

    const unsigned char *vm =
        (const unsigned char *)(0x80000000u | vmPhys);
    const uint32_t vmFlags =
        me_render_stream_load_u32(vm, layout->vmFlagsOffset);
    const uint32_t vmColor =
        me_render_stream_load_u32(vm, layout->vmColorOffset);
    const uint32_t spritePointer =
        me_render_stream_load_u32(vm, layout->vmSpriteOffset);

    uint32_t flags = 0u;
    flags |= (((vmFlags & ME_RENDER_RAW_VM_ANCHOR_MASK) >>
               ME_RENDER_RAW_VM_ANCHOR_SHIFT)
              << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT) &
             TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK;
    if ((vmFlags & ME_RENDER_RAW_VM_BLEND_ADD) != 0u)
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD;
    if ((vmFlags & ME_RENDER_RAW_VM_ZWRITE_DISABLE) != 0u)
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE;

    record->color = (vmFlags & ME_RENDER_RAW_VM_USE_COLOR2) != 0u
                        ? me_render_stream_load_u32(
                              vm, layout->vmColor2Offset)
                        : (vmColor & 0xff000000u) | 0x00ffffffu;
    record->sourceAndState =
        (logicalState & 0xffffu) << 16u;
    record->flags = flags;

    // Canonical Bullet::Draw rejects visibility before touching sprite
    // geometry/source.  A hidden VM may therefore legally retain a stale
    // non-null sprite pointer; never validate or dereference it here.
    const int drawable = spritePointer != 0u &&
                         (vmFlags & ME_RENDER_RAW_VM_VISIBLE) != 0u &&
                         (vmFlags & ME_RENDER_RAW_VM_ACTIVE) != 0u &&
                         (vmColor & 0xff000000u) != 0u;
    if (!drawable)
        return me_render_stream_record_valid(record);

    const uint32_t rotationBits =
        me_render_stream_load_u32(vm, layout->vmRotationZOffset);
    if (!me_render_stream_float_bits_finite(rotationBits))
        return 0;
    if ((rotationBits & 0x7fffffffu) != 0u)
    {
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        if (directList)
        {
            if (!bullet || !listLayout ||
                me_render_stream_load_u32(
                    bullet, listLayout->bulletRotationValidOffset) != 1u ||
                me_render_stream_load_u32(
                    bullet, listLayout->bulletRenderAngleOffset) !=
                    rotationBits)
                return 0;
            record->sinBits = me_render_stream_load_u32(
                bullet, listLayout->bulletSinOffset);
            record->cosBits = me_render_stream_load_u32(
                bullet, listLayout->bulletCosOffset);
            if (!me_render_stream_float_bits_finite(record->sinBits) ||
                !me_render_stream_float_bits_finite(record->cosBits))
                return 0;
        }
#endif
        record->flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED;
    }

    uint32_t spritePhys = 0u;
    if (!me_render_stream_raw_sprite_physical(
            spritePointer, layout, &spritePhys))
        return 0;
    const unsigned char *sprite =
        (const unsigned char *)(0x80000000u | spritePhys);
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    if (directList)
    {
        me_render_stream_list_prefetch(sprite);
        me_render_stream_list_prefetch(sprite + 32u);
    }
#endif
    const int32_t originalSource = (int32_t)me_render_stream_load_u32(
        sprite, layout->spriteSourceOffset);
    if (originalSource < 0 ||
        (uint32_t)originalSource >= layout->representativeCount)
        return 0;
    const unsigned char *representatives =
        (const unsigned char *)(0x80000000u |
                               layout->representativePhys);
    const uint32_t source = me_render_stream_load_u16(
        representatives,
        (uint32_t)originalSource * layout->representativeStride);
    if (source >= TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT)
        return 0;

    const float scaleX = me_render_bits_float(
        me_render_stream_load_u32(vm, layout->vmScaleXOffset));
    const float scaleY = me_render_bits_float(
        me_render_stream_load_u32(vm, layout->vmScaleYOffset));
    const float scrollX = me_render_bits_float(
        me_render_stream_load_u32(vm, layout->vmUvScrollXOffset));
    const float scrollY = me_render_bits_float(
        me_render_stream_load_u32(vm, layout->vmUvScrollYOffset));
    const float width = me_render_bits_float(
        me_render_stream_load_u32(sprite, layout->spriteWidthOffset));
    const float height = me_render_bits_float(
        me_render_stream_load_u32(sprite, layout->spriteHeightOffset));
    const float u0 = me_render_bits_float(
        me_render_stream_load_u32(sprite, layout->spriteUvStartXOffset));
    const float v0 = me_render_bits_float(
        me_render_stream_load_u32(sprite, layout->spriteUvStartYOffset));
    const float u1 = me_render_bits_float(
        me_render_stream_load_u32(sprite, layout->spriteUvEndXOffset));
    const float v1 = me_render_bits_float(
        me_render_stream_load_u32(sprite, layout->spriteUvEndYOffset));

    record->halfWidthBits = me_render_float_bits(width * scaleX * 0.5f);
    record->halfHeightBits = me_render_float_bits(height * scaleY * 0.5f);
    record->u0Bits = me_render_float_bits(u0 + scrollX);
    record->u1Bits = me_render_float_bits(u1 + scrollX);
    record->v0Bits = me_render_float_bits(v0 + scrollY);
    record->v1Bits = me_render_float_bits(v1 + scrollY);
    record->sourceAndState |= source;
    record->flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
    return me_render_stream_record_valid(record);
}

static __attribute__((always_inline)) inline int
me_render_stream_reconstruct_raw_input_record(
    const Th07PspMeRenderRawRecord *raw,
    const Th07PspMeRenderRawLayout *layout,
    Th07PspMeRenderStreamRecord *record)
{
    if (!me_render_stream_raw_record_valid(raw, layout))
        return 0;
    return me_render_stream_reconstruct_raw_record(
        raw->posXBits, raw->posYBits, raw->sinBits, raw->cosBits,
        raw->vmPhys, raw->logicalState, raw->slot, raw->generation, layout,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        (const unsigned char *)0, (const Th07PspMeRenderListLayout *)0, 0u,
#endif
        record);
}

#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
typedef struct MeRenderStreamListCursor
{
    uint32_t nextPhys[6];
    uint32_t seen[ME_RENDER_LIST_ACTIVE_WORD_COUNT];
} MeRenderStreamListCursor;

static int me_render_stream_list_cursor_init(
    MeRenderStreamListCursor *cursor, const uint32_t bucketEnds[6],
    uint32_t recordCount, const Th07PspMeRenderListLayout *layout)
{
    if (!cursor || !bucketEnds || !layout)
        return 0;
    memset(cursor, 0, sizeof(*cursor));
    uint32_t bucketStart = 0u;
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
    {
        if (bucketEnds[bucket] < bucketStart ||
            bucketEnds[bucket] > recordCount)
            return 0;
        const uint32_t bucketCount = bucketEnds[bucket] - bucketStart;
        const uint32_t head = layout->bucketHeadPhys[bucket];
        if ((bucketCount == 0u) != (head == 0u))
            return 0;
        cursor->nextPhys[bucket] = head;
        bucketStart = bucketEnds[bucket];
    }
    return bucketStart == recordCount;
}

static __attribute__((always_inline)) inline int
me_render_stream_reconstruct_list_record(
    MeRenderStreamListCursor *cursor, uint32_t recordIndex,
    const uint32_t bucketEnds[6],
    const Th07PspMeRenderRawLayout *rawLayout,
    const Th07PspMeRenderListLayout *listLayout,
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    Th07PspMeBulletCompactSeed *compactSeed,
#endif
    Th07PspMeRenderStreamRecord *record)
{
    if (!cursor || !bucketEnds || !rawLayout || !listLayout || !record)
        return 0;

    uint32_t bucket = 0u;
    while (bucket < 6u && recordIndex >= bucketEnds[bucket])
        ++bucket;
    if (bucket >= 6u || cursor->nextPhys[bucket] == 0u)
        return 0;

    uint32_t bulletPhys = 0u;
    uint32_t slot = 0u;
    if (!me_render_stream_list_bullet_physical(
            cursor->nextPhys[bucket], listLayout, &bulletPhys, &slot) ||
        bulletPhys != cursor->nextPhys[bucket])
        return 0;

    const uint32_t wordIndex = slot >> 5u;
    const uint32_t slotBit = 1u << (slot & 31u);
    if (wordIndex >= listLayout->activeBitsWordCount ||
        (cursor->seen[wordIndex] & slotBit) != 0u)
        return 0;
    cursor->seen[wordIndex] |= slotBit;

    const unsigned char *activeBits =
        (const unsigned char *)(0x80000000u |
                                listLayout->activeBitsPhys);
    if ((me_render_stream_load_u32(
             activeBits, wordIndex * sizeof(uint32_t)) & slotBit) == 0u)
        return 0;

    const unsigned char *bullet =
        (const unsigned char *)(0x80000000u | bulletPhys);
    if ((uint32_t)bullet[listLayout->bulletCollisionTypeOffset] != bucket)
        return 0;
    const uint32_t state = me_render_stream_load_u16(
        bullet, listLayout->bulletStateOffset);
    if (state < 1u || state > 5u)
        return 0;
    const uint32_t vmPhys =
        bulletPhys + listLayout->bulletVmOffsets[state - 1u];
    const unsigned char *vm =
        (const unsigned char *)(0x80000000u | vmPhys);
    me_render_stream_list_prefetch(vm + rawLayout->vmColorOffset);
    me_render_stream_list_prefetch(vm + rawLayout->vmSpriteOffset);

    const unsigned char *generations =
        (const unsigned char *)(0x80000000u |
                                listLayout->generationBasePhys);
    const uint32_t generation = me_render_stream_load_u32(
        generations, slot * ME_RENDER_LIST_GENERATION_STRIDE);
    if (generation == 0u)
        return 0;

    const uint32_t bulletPosXBits = me_render_stream_load_u32(
        bullet, listLayout->bulletPosXOffset);
    const uint32_t bulletPosYBits = me_render_stream_load_u32(
        bullet, listLayout->bulletPosYOffset);
    if (!me_render_stream_float_bits_finite(bulletPosXBits) ||
        !me_render_stream_float_bits_finite(bulletPosYBits))
        return 0;
    const float posX = me_render_bits_float(listLayout->arcadeLeftBits) +
                       me_render_bits_float(bulletPosXBits);
    const float posY = me_render_bits_float(listLayout->arcadeTopBits) +
                       me_render_bits_float(bulletPosYBits);
    const uint32_t posXBits = me_render_float_bits(posX);
    const uint32_t posYBits = me_render_float_bits(posY);
    if (!me_render_stream_float_bits_finite(posXBits) ||
        !me_render_stream_float_bits_finite(posYBits))
        return 0;

    const uint32_t nextPointer = me_render_stream_load_u32(
        bullet, listLayout->bulletNextOffset);
    uint32_t nextPhys = 0u;
    if (nextPointer != 0u)
    {
        uint32_t nextSlot = 0u;
        if (!me_render_stream_list_bullet_physical(
                nextPointer, listLayout, &nextPhys, &nextSlot) ||
            nextPhys != nextPointer)
            return 0;
        const unsigned char *nextBullet =
            (const unsigned char *)(0x80000000u | nextPhys);
        me_render_stream_list_prefetch(
            nextBullet + listLayout->bulletCollisionTypeOffset);
        me_render_stream_list_prefetch(
            nextBullet + listLayout->bulletStateOffset);
    }
    cursor->nextPhys[bucket] = nextPhys;
    const int lastInBucket = recordIndex + 1u == bucketEnds[bucket];
    if (lastInBucket ? nextPhys != 0u : nextPhys == 0u)
        return 0;

    if (!me_render_stream_reconstruct_raw_record(
            posXBits, posYBits, 0u, 0x3f800000u, vmPhys, state, slot,
            generation, rawLayout, bullet, listLayout, 1u, record))
        return 0;
    // Generation brackets the complete live gather.  A zero or changed slot
    // identity is never allowed to publish geometry, even if a violated SC
    // lifetime happened to leave the list pointer itself looking plausible.
    if (me_render_stream_load_u32(
            generations, slot * ME_RENDER_LIST_GENERATION_STRIDE) !=
        generation)
        return 0;
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    // The compact update seed is a non-authoritative sidecar.  Capture is
    // deliberately after the generation bracket; any eligibility/finite
    // failure leaves only this slot canonical and cannot reject geometry.
    me_bullet_compact_capture_seed(compactSeed, bullet, vm, slot,
                                   generation, state, rawLayout);
    // Seed capture reads update-only fields beyond the geometry gather above.
    // Bracket that sidecar separately.  A changed slot invalidates only its
    // candidate; I5 geometry already passed its own bracket and remains valid.
    if (compactSeed &&
        me_render_stream_load_u32(
            generations, slot * ME_RENDER_LIST_GENERATION_STRIDE) !=
            generation)
    {
        const uint32_t compactBit = 1u << (slot & 31u);
        unsigned int *compactWord =
            &compactSeed->candidateBits[slot >> 5u];
        if ((*compactWord & compactBit) != 0u)
        {
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
            me_bullet_compact_clear_seed_slot(compactSeed, slot);
            if (compactSeed->header.candidateCount != 0u)
                --compactSeed->header.candidateCount;
#else
            *compactWord &= ~compactBit;
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
            compactSeed->inBoundsBits[slot >> 5u] &= ~compactBit;
#endif
            if (compactSeed->header.candidateCount != 0u)
                --compactSeed->header.candidateCount;
            memset(&compactSeed->slots[slot], 0,
                   sizeof(compactSeed->slots[slot]));
#endif
        }
    }
#endif
    return 1;
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
typedef struct MeRenderStreamItemCursor
{
    uint32_t nextPhys;
    uint32_t seen[ME_RENDER_ITEM_ACTIVE_WORD_COUNT];
} MeRenderStreamItemCursor;

static int me_render_stream_item_cursor_init(
    MeRenderStreamItemCursor *cursor, uint32_t recordCount,
    const Th07PspMeRenderRawLayout *rawLayout,
    const Th07PspMeRenderItemLayout *itemLayout)
{
    if (!cursor || !me_render_stream_item_layout_valid(
                       itemLayout, rawLayout, recordCount))
        return 0;
    memset(cursor, 0, sizeof(*cursor));
    cursor->nextPhys = itemLayout->headPhys;
    return 1;
}

static __attribute__((always_inline)) inline int
me_render_stream_reconstruct_item_record(
    MeRenderStreamItemCursor *cursor, uint32_t recordIndex,
    uint32_t recordCount, const Th07PspMeRenderRawLayout *rawLayout,
    const Th07PspMeRenderItemLayout *itemLayout,
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    Th07PspMeItemMotionSeed *itemMotionSeed,
#endif
    Th07PspMeRenderStreamRecord *record)
{
    if (!cursor || !rawLayout || !itemLayout || !record ||
        recordIndex >= recordCount || cursor->nextPhys == 0u)
        return 0;

    uint32_t itemPhys = 0u;
    uint32_t slot = 0u;
    if (!me_render_stream_item_physical(
            cursor->nextPhys, itemLayout, &itemPhys, &slot) ||
        itemPhys != cursor->nextPhys)
        return 0;

    const uint32_t wordIndex = slot >> 5u;
    const uint32_t slotBit = 1u << (slot & 31u);
    if (wordIndex >= itemLayout->activeBitsWordCount ||
        (cursor->seen[wordIndex] & slotBit) != 0u)
        return 0;
    cursor->seen[wordIndex] |= slotBit;

    const volatile unsigned char *activeBits =
        me_render_stream_item_uncached(itemLayout->activeBitsPhys);
    if ((me_render_stream_item_load_u32(
             activeBits, wordIndex * sizeof(uint32_t)) & slotBit) == 0u)
        return 0;

    const volatile unsigned char *item =
        me_render_stream_item_uncached(itemPhys);
    if (me_render_stream_item_load_u8(
            item, itemLayout->itemInUseOffset) == 0u)
        return 0;
    const uint32_t itemType = me_render_stream_item_load_u8(
        item, itemLayout->itemTypeOffset);
    if (itemType > 9u)
        return 0;

    const volatile unsigned char *generations =
        me_render_stream_item_uncached(itemLayout->generationBasePhys);
    const uint32_t generation = me_render_stream_item_load_u32(
        generations, slot * itemLayout->generationStride);
    if (generation == 0u)
        return 0;

    const uint32_t vmPhys = itemPhys + itemLayout->itemVmOffset;
    const volatile unsigned char *vm =
        me_render_stream_item_uncached(vmPhys);

    const uint32_t nextPointer = me_render_stream_item_load_u32(
        item, itemLayout->itemNextOffset);
    uint32_t nextPhys = 0u;
    if (nextPointer != 0u)
    {
        uint32_t nextSlot = 0u;
        if (!me_render_stream_item_physical(
                nextPointer, itemLayout, &nextPhys, &nextSlot) ||
            nextPhys != nextPointer)
            return 0;
    }
    const int last = recordIndex + 1u == recordCount;
    if (last ? (itemPhys != itemLayout->tailPhys ||
                nextPhys != itemLayout->suffixHeadPhys)
             : nextPhys == 0u)
        return 0;
    cursor->nextPhys = nextPhys;

    const uint32_t posXBits = me_render_stream_item_load_u32(
        vm, itemLayout->vmPosXOffset);
    const uint32_t posYBits = me_render_stream_item_load_u32(
        vm, itemLayout->vmPosYOffset);
    const uint32_t posZBits = me_render_stream_item_load_u32(
        vm, itemLayout->vmPosZOffset);
    if (!me_render_stream_float_bits_finite(posXBits) ||
        !me_render_stream_float_bits_finite(posYBits) ||
        !me_render_stream_float_bits_finite(posZBits))
        return 0;

    memset(record, 0, sizeof(*record));
    record->posXBits = posXBits;
    record->posYBits = posYBits;
    record->posZBits = posZBits;
    record->slot = slot;
    record->slotGeneration = generation;
    record->sourceAndState = itemType << 16u;

    const uint32_t vmFlags = me_render_stream_item_load_u32(
        vm, rawLayout->vmFlagsOffset);
    const uint32_t vmColor = me_render_stream_item_load_u32(
        vm, rawLayout->vmColorOffset);
    const uint32_t spritePointer = me_render_stream_item_load_u32(
        vm, rawLayout->vmSpriteOffset);
    uint32_t flags =
        (((vmFlags & ME_RENDER_RAW_VM_ANCHOR_MASK) >>
          ME_RENDER_RAW_VM_ANCHOR_SHIFT)
         << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT) &
        TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK;
    if ((vmFlags & ME_RENDER_RAW_VM_BLEND_ADD) != 0u)
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD;
    if ((vmFlags & ME_RENDER_RAW_VM_ZWRITE_DISABLE) != 0u)
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE;
    record->color = (vmFlags & ME_RENDER_RAW_VM_USE_COLOR2) != 0u
        ? me_render_stream_item_load_u32(vm, rawLayout->vmColor2Offset)
        : vmColor;
    record->flags = flags;

    const int drawable = spritePointer != 0u &&
                         (vmFlags & ME_RENDER_RAW_VM_VISIBLE) != 0u &&
                         (vmFlags & ME_RENDER_RAW_VM_ACTIVE) != 0u &&
                         (vmColor & 0xff000000u) != 0u;
    if (drawable)
    {
        const uint32_t rotationBits = me_render_stream_item_load_u32(
            vm, rawLayout->vmRotationZOffset);
        if (!me_render_stream_float_bits_finite(rotationBits))
            return 0;
        if ((rotationBits & 0x7fffffffu) != 0u)
        {
            const volatile unsigned char *sinValues =
                me_render_stream_item_uncached(itemLayout->sinBasePhys);
            const volatile unsigned char *cosValues =
                me_render_stream_item_uncached(itemLayout->cosBasePhys);
            record->sinBits = me_render_stream_item_load_u32(
                sinValues, slot * itemLayout->sinStride);
            record->cosBits = me_render_stream_item_load_u32(
                cosValues, slot * itemLayout->cosStride);
            if (!me_render_stream_float_bits_finite(record->sinBits) ||
                !me_render_stream_float_bits_finite(record->cosBits))
                return 0;
            record->flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED;
        }

        uint32_t spritePhys = 0u;
        if (!me_render_stream_raw_sprite_physical(
                spritePointer, rawLayout, &spritePhys))
            return 0;
        const unsigned char *sprite =
            (const unsigned char *)(0x80000000u | spritePhys);
        me_render_stream_list_prefetch(sprite);
        me_render_stream_list_prefetch(sprite + 32u);
        const int32_t originalSource = (int32_t)me_render_stream_load_u32(
            sprite, rawLayout->spriteSourceOffset);
        if (originalSource < 0 ||
            (uint32_t)originalSource >= rawLayout->representativeCount)
            return 0;
        const unsigned char *representatives =
            (const unsigned char *)(0x80000000u |
                                    rawLayout->representativePhys);
        const uint32_t source = me_render_stream_load_u16(
            representatives,
            (uint32_t)originalSource * rawLayout->representativeStride);
        if (source >= TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT)
            return 0;

        const float scaleX = me_render_bits_float(
            me_render_stream_item_load_u32(vm, rawLayout->vmScaleXOffset));
        const float scaleY = me_render_bits_float(
            me_render_stream_item_load_u32(vm, rawLayout->vmScaleYOffset));
        const float scrollX = me_render_bits_float(
            me_render_stream_item_load_u32(vm, rawLayout->vmUvScrollXOffset));
        const float scrollY = me_render_bits_float(
            me_render_stream_item_load_u32(vm, rawLayout->vmUvScrollYOffset));
        const float width = me_render_bits_float(
            me_render_stream_load_u32(
                sprite, rawLayout->spriteWidthOffset));
        const float height = me_render_bits_float(
            me_render_stream_load_u32(
                sprite, rawLayout->spriteHeightOffset));
        const float u0 = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteUvStartXOffset));
        const float v0 = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteUvStartYOffset));
        const float u1 = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteUvEndXOffset));
        const float v1 = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteUvEndYOffset));
        record->halfWidthBits = me_render_float_bits(width * scaleX * 0.5f);
        record->halfHeightBits =
            me_render_float_bits(height * scaleY * 0.5f);
        record->u0Bits = me_render_float_bits(u0 + scrollX);
        record->u1Bits = me_render_float_bits(u1 + scrollX);
        record->v0Bits = me_render_float_bits(v0 + scrollY);
        record->v1Bits = me_render_float_bits(v1 + scrollY);
        record->sourceAndState |= source;
        record->flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
    }

    if (me_render_stream_item_load_u32(
            generations, slot * itemLayout->generationStride) != generation)
        return 0;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    // A1-MOVE is optional sidecar data.  The final generation bracket above
    // protects this exact uncached Item tail snapshot; an unsupported record
    // simply remains canonical on SC and never rejects valid geometry.
    me_item_motion_capture_seed(itemMotionSeed, item, slot, generation);
    if (me_render_stream_item_load_u32(
            generations, slot * itemLayout->generationStride) != generation)
    {
        me_item_motion_clear_seed_slot(itemMotionSeed, slot);
    }
#endif
    return me_render_stream_record_valid_capacity(
        record, TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS);
}

static int me_render_stream_item_cursor_finish(
    const MeRenderStreamItemCursor *cursor,
    const Th07PspMeRenderItemLayout *itemLayout, uint32_t recordCount)
{
    if (!cursor || !itemLayout ||
        recordCount != itemLayout->expectedItemCount ||
        cursor->nextPhys != itemLayout->suffixHeadPhys)
        return 0;
    const volatile unsigned char *prepareSerial =
        me_render_stream_item_uncached(itemLayout->prepareSerialPhys);
    const volatile unsigned char *preparedSerial =
        me_render_stream_item_uncached(itemLayout->preparedSerialPhys);
    const volatile unsigned char *preparedCount =
        me_render_stream_item_uncached(itemLayout->preparedCountPhys);
    return me_render_stream_item_load_u32(prepareSerial, 0u) ==
               itemLayout->expectedPrepareSerial &&
           me_render_stream_item_load_u32(preparedSerial, 0u) ==
               itemLayout->expectedPrepareSerial &&
           me_render_stream_item_load_u32(preparedCount, 0u) ==
               itemLayout->expectedTotalCount;
}

#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
typedef struct MeRenderStreamEffectCursor
{
    uint32_t nextPhys;
    uint32_t tailPhys;
    uint32_t layer;
    uint32_t seen[ME_RENDER_EFFECT_ACTIVE_WORD_COUNT];
} MeRenderStreamEffectCursor;

static int me_render_stream_effect_cursor_init(
    MeRenderStreamEffectCursor *cursor,
    const Th07PspMeRenderEffectLayout *layout, uint32_t layer)
{
    if (!cursor || !layout || (layer != 0u && layer != 3u))
        return 0;
    memset(cursor, 0, sizeof(*cursor));
    cursor->layer = layer;
    cursor->nextPhys = layer == 0u ? layout->layer0HeadPhys
                                   : layout->layer3HeadPhys;
    cursor->tailPhys = layer == 0u ? layout->layer0TailPhys
                                   : layout->layer3TailPhys;
    return 1;
}

static __attribute__((always_inline)) inline int
me_render_stream_reconstruct_effect_record(
    MeRenderStreamEffectCursor *cursor, uint32_t recordIndex,
    uint32_t recordCount, const Th07PspMeRenderRawLayout *rawLayout,
    const Th07PspMeRenderEffectLayout *layout,
    Th07PspMeRenderStreamRecord *record)
{
    if (!cursor || !rawLayout || !layout || !record ||
        recordIndex >= recordCount || cursor->nextPhys == 0u)
        return 0;
    uint32_t effectPhys = 0u;
    uint32_t slot = 0u;
    if (!me_render_stream_effect_physical(
            cursor->nextPhys, layout, &effectPhys, &slot) ||
        effectPhys != cursor->nextPhys)
        return 0;
    const uint32_t wordIndex = slot >> 5u;
    const uint32_t slotBit = 1u << (slot & 31u);
    if (wordIndex >= layout->activeBitsWordCount ||
        (cursor->seen[wordIndex] & slotBit) != 0u)
        return 0;
    cursor->seen[wordIndex] |= slotBit;

    const unsigned char *activeBits =
        (const unsigned char *)(0x80000000u | layout->activeBitsPhys);
    if ((me_render_stream_load_u32(
             activeBits, wordIndex * sizeof(uint32_t)) & slotBit) == 0u)
        return 0;
    const unsigned char *effect =
        (const unsigned char *)(0x80000000u | effectPhys);
    if (effect[layout->effectInUseOffset] == 0u ||
        effect[layout->effectIs2DOffset] != 0u)
        return 0;
    const unsigned char *generations =
        (const unsigned char *)(0x80000000u |
                                layout->generationBasePhys);
    const uint32_t generation = me_render_stream_load_u32(
        generations, slot * layout->generationStride);
    if (generation == 0u)
        return 0;

    const uint32_t nextPointer = me_render_stream_load_u32(
        effect, layout->effectNextOffset);
    uint32_t nextPhys = 0u;
    if (nextPointer != 0u)
    {
        uint32_t nextSlot = 0u;
        if (!me_render_stream_effect_physical(
                nextPointer, layout, &nextPhys, &nextSlot) ||
            nextPhys != nextPointer)
            return 0;
        me_render_stream_list_prefetch(
            (const unsigned char *)(0x80000000u | nextPhys));
    }
    const int last = recordIndex + 1u == recordCount;
    if (last ? (effectPhys != cursor->tailPhys || nextPhys != 0u)
             : nextPhys == 0u)
        return 0;
    cursor->nextPhys = nextPhys;

    const uint32_t vmPhys = effectPhys + layout->effectVmOffset;
    const unsigned char *vm =
        (const unsigned char *)(0x80000000u | vmPhys);
    me_render_stream_list_prefetch(vm + rawLayout->vmColorOffset);
    me_render_stream_list_prefetch(vm + rawLayout->vmSpriteOffset);
    memset(record, 0, sizeof(*record));
    record->posXBits = me_render_stream_load_u32(
        vm, layout->vmPosXOffset);
    record->posYBits = me_render_stream_load_u32(
        vm, layout->vmPosYOffset);
    record->posZBits = me_render_stream_load_u32(
        vm, layout->vmPosZOffset);
    if (!me_render_stream_float_bits_finite(record->posXBits) ||
        !me_render_stream_float_bits_finite(record->posYBits) ||
        !me_render_stream_float_bits_finite(record->posZBits))
        return 0;
    record->slot = slot;
    record->slotGeneration = generation;
    record->sourceAndState = cursor->layer << 16u;

    const uint32_t vmFlags = me_render_stream_load_u32(
        vm, rawLayout->vmFlagsOffset);
    const uint32_t vmColor = me_render_stream_load_u32(
        vm, rawLayout->vmColorOffset);
    const uint32_t spritePointer = me_render_stream_load_u32(
        vm, rawLayout->vmSpriteOffset);
    const uint32_t additive =
        (vmFlags & ME_RENDER_RAW_VM_BLEND_ADD) != 0u;
    if ((cursor->layer == 0u && additive) ||
        (cursor->layer == 3u && !additive))
        return 0;
    uint32_t flags =
        (((vmFlags & ME_RENDER_RAW_VM_ANCHOR_MASK) >>
          ME_RENDER_RAW_VM_ANCHOR_SHIFT)
         << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT) &
        TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK;
    if (additive)
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD;
    if ((vmFlags & ME_RENDER_RAW_VM_ZWRITE_DISABLE) != 0u)
        flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE;
    record->color = (vmFlags & ME_RENDER_RAW_VM_USE_COLOR2) != 0u
        ? me_render_stream_load_u32(vm, rawLayout->vmColor2Offset)
        : vmColor;
    record->flags = flags;

    const int drawable = spritePointer != 0u &&
                         (vmFlags & ME_RENDER_RAW_VM_VISIBLE) != 0u &&
                         (vmFlags & ME_RENDER_RAW_VM_ACTIVE) != 0u &&
                         (vmColor & 0xff000000u) != 0u;
    if (drawable)
    {
        const uint32_t rotationBits = me_render_stream_load_u32(
            vm, rawLayout->vmRotationZOffset);
        if (!me_render_stream_float_bits_finite(rotationBits))
            return 0;
        if ((rotationBits & 0x7fffffffu) != 0u)
        {
            const unsigned char *sinValues =
                (const unsigned char *)(0x80000000u |
                                        layout->sinBasePhys);
            const unsigned char *cosValues =
                (const unsigned char *)(0x80000000u |
                                        layout->cosBasePhys);
            record->sinBits = me_render_stream_load_u32(
                sinValues, slot * layout->sinStride);
            record->cosBits = me_render_stream_load_u32(
                cosValues, slot * layout->cosStride);
            if (!me_render_stream_float_bits_finite(record->sinBits) ||
                !me_render_stream_float_bits_finite(record->cosBits))
                return 0;
            record->flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED;
        }

        uint32_t spritePhys = 0u;
        if (!me_render_stream_raw_sprite_physical(
                spritePointer, rawLayout, &spritePhys))
            return 0;
        const unsigned char *sprite =
            (const unsigned char *)(0x80000000u | spritePhys);
        me_render_stream_list_prefetch(sprite + 32u);
        const int32_t originalSource = (int32_t)me_render_stream_load_u32(
            sprite, rawLayout->spriteSourceOffset);
        if (originalSource < 0 ||
            (uint32_t)originalSource >= rawLayout->representativeCount)
            return 0;
        const unsigned char *representatives =
            (const unsigned char *)(0x80000000u |
                                    rawLayout->representativePhys);
        const uint32_t source = me_render_stream_load_u16(
            representatives,
            (uint32_t)originalSource * rawLayout->representativeStride);
        if (source >= TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT)
            return 0;
        const float scaleX = me_render_bits_float(
            me_render_stream_load_u32(vm, rawLayout->vmScaleXOffset));
        const float scaleY = me_render_bits_float(
            me_render_stream_load_u32(vm, rawLayout->vmScaleYOffset));
        const float scrollX = me_render_bits_float(
            me_render_stream_load_u32(vm, rawLayout->vmUvScrollXOffset));
        const float scrollY = me_render_bits_float(
            me_render_stream_load_u32(vm, rawLayout->vmUvScrollYOffset));
        const float width = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteWidthOffset));
        const float height = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteHeightOffset));
        const float u0 = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteUvStartXOffset));
        const float v0 = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteUvStartYOffset));
        const float u1 = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteUvEndXOffset));
        const float v1 = me_render_bits_float(me_render_stream_load_u32(
            sprite, rawLayout->spriteUvEndYOffset));
        record->halfWidthBits = me_render_float_bits(width * scaleX * 0.5f);
        record->halfHeightBits =
            me_render_float_bits(height * scaleY * 0.5f);
        record->u0Bits = me_render_float_bits(u0 + scrollX);
        record->u1Bits = me_render_float_bits(u1 + scrollX);
        record->v0Bits = me_render_float_bits(v0 + scrollY);
        record->v1Bits = me_render_float_bits(v1 + scrollY);
        record->sourceAndState |= source;
        record->flags |= TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
    }
    if (me_render_stream_load_u32(
            generations, slot * layout->generationStride) != generation)
        return 0;
    return me_render_stream_record_valid_capacity(
        record, TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS);
}

static int me_render_stream_effect_cursor_finish(
    const MeRenderStreamEffectCursor *cursor,
    const Th07PspMeRenderEffectLayout *layout, uint32_t recordCount)
{
    if (!cursor || !layout || cursor->nextPhys != 0u)
        return 0;
    const unsigned char *prepareSerial =
        (const unsigned char *)(0x80000000u |
                                layout->prepareSerialPhys);
    const unsigned char *preparedSerial =
        (const unsigned char *)(0x80000000u |
                                layout->preparedSerialPhys);
    const unsigned char *preparedCounts =
        (const unsigned char *)(0x80000000u |
                                layout->preparedCountsPhys);
    const uint32_t countOffset = cursor->layer == 0u ? 0u : sizeof(uint32_t);
    return me_render_stream_load_u32(prepareSerial, 0u) ==
               layout->expectedPrepareSerial &&
           me_render_stream_load_u32(preparedSerial, 0u) ==
               layout->expectedPrepareSerial &&
           me_render_stream_load_u32(preparedCounts, countOffset) ==
               recordCount;
}

static int me_render_stream_effect_lists_prevalidate(
    const Th07PspMeRenderRawLayout *rawLayout,
    const Th07PspMeRenderEffectLayout *layout)
{
    uint32_t seen[ME_RENDER_EFFECT_ACTIVE_WORD_COUNT] = {0u};
    const uint32_t layers[2] = {0u, 3u};
    const uint32_t counts[2] = {layout->expectedLayer0Count,
                                layout->expectedLayer3Count};
    for (uint32_t segment = 0u; segment < 2u; ++segment)
    {
        MeRenderStreamEffectCursor cursor;
        if (!me_render_stream_effect_cursor_init(
                &cursor, layout, layers[segment]))
            return 0;
        for (uint32_t index = 0u; index < counts[segment]; ++index)
        {
            Th07PspMeRenderStreamRecord record;
            if (!me_render_stream_reconstruct_effect_record(
                    &cursor, index, counts[segment], rawLayout, layout,
                    &record))
                return 0;
            const uint32_t word = record.slot >> 5u;
            const uint32_t bit = 1u << (record.slot & 31u);
            if ((seen[word] & bit) != 0u)
                return 0;
            seen[word] |= bit;
        }
        if (!me_render_stream_effect_cursor_finish(
                &cursor, layout, counts[segment]))
            return 0;
    }
    return 1;
}

#endif
#endif
#endif
#endif

static __attribute__((noinline)) uint32_t me_render_stream_expand_kernel(
    const Th07PspMeRenderStreamRecord *records, uint32_t recordCount,
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    const Th07PspMeRenderRawLayout *rawLayout, uint32_t rawLive,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const Th07PspMeRenderListLayout *listLayout, uint32_t directList,
    const uint32_t bucketEnds[6],
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    const Th07PspMeRenderItemLayout *itemLayout, uint32_t itemList,
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    const Th07PspMeRenderEffectLayout *effectLayout, uint32_t effectList,
    uint32_t effectLayer,
#endif
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    Th07PspMeBulletCompactSeed *compactSeed,
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    Th07PspMeItemMotionSeed *itemMotionSeed,
#endif
#endif
#endif
    Th07PspMeRenderStreamVertex *vertices, uint32_t vertexCapacity,
    Th07PspMeRenderStreamRun *runs, uint32_t runCapacity,
    uint32_t offsetXBits, uint32_t offsetYBits,
    uint32_t viewportLeftBits, uint32_t viewportTopBits,
    uint32_t viewportRightBits, uint32_t viewportBottomBits,
    uint32_t globalColor, uint32_t configFlags,
    uint32_t *outVertexCount, uint32_t *outRunCount,
    uint32_t *outFirstBadRecord)
{
    const float offsetX = me_render_bits_float(offsetXBits);
    const float offsetY = me_render_bits_float(offsetYBits);
    const float viewportLeft = me_render_bits_float(viewportLeftBits);
    const float viewportTop = me_render_bits_float(viewportTopBits);
    const float viewportRight = me_render_bits_float(viewportRightBits);
    const float viewportBottom = me_render_bits_float(viewportBottomBits);
    uint32_t vertexCount = 0u;
    uint32_t runCount = 0u;
    uint32_t emittedRecords = 0u;
    int generalMode =
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        itemList ? 1 :
#endif
        0;

    *outVertexCount = 0u;
    *outRunCount = 0u;
    *outFirstBadRecord = 0xffffffffu;

#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    MeRenderStreamListCursor listCursor;
    if (directList && !me_render_stream_list_cursor_init(
                          &listCursor, bucketEnds, recordCount, listLayout))
    {
        *outFirstBadRecord = 0u;
        return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
    }
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    MeRenderStreamItemCursor itemCursor;
    if (itemList && !me_render_stream_item_cursor_init(
                        &itemCursor, recordCount, rawLayout, itemLayout))
    {
        *outFirstBadRecord = 0u;
        return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
    }
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    MeRenderStreamEffectCursor effectCursor;
    if (effectList &&
        (!me_render_stream_effect_layout_valid(effectLayout, rawLayout) ||
         !me_render_stream_effect_cursor_init(
             &effectCursor, effectLayout, effectLayer)))
    {
        *outFirstBadRecord = 0u;
        return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
    }
#endif
#endif
#endif

    for (uint32_t recordIndex = 0u; recordIndex < recordCount; ++recordIndex)
    {
        const Th07PspMeRenderStreamRecord *record =
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            effectList ? (const Th07PspMeRenderStreamRecord *)0 :
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            itemList ? (const Th07PspMeRenderStreamRecord *)0 :
#endif
                       &records[recordIndex];
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        Th07PspMeRenderStreamRecord reconstructed;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        if (effectList)
        {
            if (!me_render_stream_reconstruct_effect_record(
                    &effectCursor, recordIndex, recordCount, rawLayout,
                    effectLayout, &reconstructed))
            {
                *outFirstBadRecord = recordIndex;
                return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
            }
            record = &reconstructed;
        }
        else
#endif
        if (itemList)
        {
            if (!me_render_stream_reconstruct_item_record(
                    &itemCursor, recordIndex, recordCount, rawLayout,
                    itemLayout,
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                    itemMotionSeed,
#endif
                    &reconstructed))
            {
                *outFirstBadRecord = recordIndex;
                return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
            }
            record = &reconstructed;
        }
        else
#endif
        if (directList)
        {
            if (!me_render_stream_reconstruct_list_record(
                    &listCursor, recordIndex, bucketEnds, rawLayout,
                    listLayout,
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                    compactSeed,
#endif
                    &reconstructed))
            {
                *outFirstBadRecord = recordIndex;
                return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
            }
            record = &reconstructed;
        }
        else
#endif
        if (rawLive)
        {
            const Th07PspMeRenderRawRecord *rawRecords =
                (const Th07PspMeRenderRawRecord *)records;
            if (!me_render_stream_reconstruct_raw_input_record(
                    &rawRecords[recordIndex], rawLayout, &reconstructed))
            {
                *outFirstBadRecord = recordIndex;
                return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
            }
            record = &reconstructed;
        }
#endif
        if (
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            effectList
                ? !me_render_stream_record_valid_capacity(
                      record, TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS)
                :
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            itemList
                ? !me_render_stream_record_valid_capacity(
                      record, TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS)
                :
#endif
                  !me_render_stream_record_valid(record))
        {
            *outFirstBadRecord = recordIndex;
            return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
        }
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        if (!me_render_stream_axis_floor_inputs_valid(
                record, offsetXBits, offsetYBits))
        {
            *outFirstBadRecord = recordIndex;
            return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
        }
#endif
        if ((record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE) == 0u)
            continue;

        const float posX = me_render_bits_float(record->posXBits);
        const float posY = me_render_bits_float(record->posYBits);
        const float halfWidth = me_render_bits_float(record->halfWidthBits);
        const float halfHeight = me_render_bits_float(record->halfHeightBits);
        const uint32_t anchor =
            (record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK) >>
            TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT;
        float centerX = posX + offsetX;
        float centerY = posY + offsetY;
        if (anchor & 1u)
            centerX += halfWidth;
        if (anchor & 2u)
            centerY += halfHeight;
        const float absWidth = halfWidth < 0.0f ? -halfWidth : halfWidth;
        const float absHeight = halfHeight < 0.0f ? -halfHeight : halfHeight;
        const float bound = absWidth + absHeight;
        if (centerX + bound < viewportLeft ||
            centerY + bound < viewportTop ||
            centerX - bound > viewportRight ||
            centerY - bound > viewportBottom)
            continue;

        float x[4];
        float y[4];
        const int rotated =
            (record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED) != 0u;
        if (!rotated)
        {
            const float rawLeft = (anchor & 1u) ? posX : posX - halfWidth;
            const float rawRight = (anchor & 1u) ? posX + halfWidth * 2.0f
                                                  : posX + halfWidth;
            const float rawTop = (anchor & 2u) ? posY : posY - halfHeight;
            const float rawBottom = (anchor & 2u) ? posY + halfHeight * 2.0f
                                                   : posY + halfHeight;
            const float left =
                me_render_stream_floor(rawLeft + offsetX + 0.5f);
            const float right =
                me_render_stream_floor(rawRight + offsetX + 0.5f);
            const float top =
                me_render_stream_floor(rawTop + offsetY + 0.5f);
            const float bottom =
                me_render_stream_floor(rawBottom + offsetY + 0.5f);
            x[0] = x[2] = left;
            x[1] = x[3] = right;
            y[0] = y[1] = top;
            y[2] = y[3] = bottom;
        }
        else
        {
            const float sine = me_render_bits_float(record->sinBits);
            const float cosine = me_render_bits_float(record->cosBits);
            for (uint32_t corner = 0u; corner < 4u; ++corner)
            {
                const float localX = (corner == 0u || corner == 2u)
                                         ? -halfWidth : halfWidth;
                const float localY = corner < 2u ? -halfHeight : halfHeight;
                x[corner] = localX * cosine - localY * sine + posX + offsetX;
                y[corner] = localX * sine + localY * cosine + posY + offsetY;
                if (anchor & 1u)
                    x[corner] += halfWidth;
                if (anchor & 2u)
                    y[corner] += halfHeight;
            }
        }

#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        if (effectList)
        {
            // DrawPspFastSprite/DrawInner reject against the actual emitted
            // rectangle, before texture/state synchronization. The generic
            // Bullet bound above is deliberately conservative; applying this
            // exact second gate only to Effect preserves both its pixel cull
            // and its observable renderer-state transitions.
            float minX = x[0];
            float maxX = x[0];
            float minY = y[0];
            float maxY = y[0];
            for (uint32_t corner = 1u; corner < 4u; ++corner)
            {
                if (x[corner] < minX)
                    minX = x[corner];
                if (x[corner] > maxX)
                    maxX = x[corner];
                if (y[corner] < minY)
                    minY = y[corner];
                if (y[corner] > maxY)
                    maxY = y[corner];
            }
            if (maxX < viewportLeft || maxY < viewportTop ||
                minX > viewportRight || minY > viewportBottom)
                continue;
        }
#endif

        const int usePairs = !rotated && x[0] <= x[3] && y[0] <= y[3];
        if (!usePairs
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            && !effectList
#endif
        )
            generalMode = 1;
        const uint32_t primitive =
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            effectList ? (usePairs
                ? TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES
                : TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS) :
#endif
            generalMode
            ? TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS
            : TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES;
        const uint32_t verticesThisRecord =
            primitive == TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS
                ? 4u : 2u;
        if (vertexCount > vertexCapacity - verticesThisRecord)
        {
            *outFirstBadRecord = recordIndex;
            return TH07_PSP_ME_RENDER_STREAM_RESULT_OUTPUT_OVERFLOW;
        }

        const uint32_t sourceFileIndex = record->sourceAndState & 0xffffu;
        const uint32_t logicalState = record->sourceAndState >> 16;
        uint32_t renderStateFlags = 0u;
        if (record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD)
            renderStateFlags |= TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD;
        if ((configFlags & TH07_PSP_ME_RENDER_STREAM_CONFIG_DISABLE_Z) == 0u &&
            (record->flags &
             TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE) != 0u)
            renderStateFlags |= TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE;

        Th07PspMeRenderStreamRun *run =
            runCount ? &runs[runCount - 1u] : (Th07PspMeRenderStreamRun *)0;
        const int forceBreak =
            (record->flags & TH07_PSP_ME_RENDER_STREAM_RECORD_RUN_BREAK) != 0u;
        if (!run || forceBreak || run->sourceFileIndex != sourceFileIndex ||
            run->renderStateFlags != renderStateFlags ||
            run->primitive != primitive)
        {
            if (runCount >= runCapacity)
            {
                *outFirstBadRecord = recordIndex;
                return TH07_PSP_ME_RENDER_STREAM_RESULT_RUN_OVERFLOW;
            }
            run = &runs[runCount++];
            run->firstRecord = recordIndex;
            run->recordCount = 0u;
            run->firstVertex = vertexCount;
            run->vertexCount = 0u;
            run->primitive = primitive;
            run->sourceFileIndex = sourceFileIndex;
            run->logicalState = logicalState;
            run->renderStateFlags = renderStateFlags;
        }
        else if (run->logicalState != logicalState)
        {
            // logicalState is diagnostic only and never a merge authority.
            run->logicalState = 0xffffffffu;
        }

        const uint32_t color = me_render_stream_color(
            record->color, globalColor,
            configFlags & TH07_PSP_ME_RENDER_STREAM_CONFIG_COLOR_MUL);
        Th07PspMeRenderStreamVertex *out = &vertices[vertexCount];
        int packed = me_render_stream_write_vertex(
            out + 0u, record->u0Bits, record->v0Bits, color, x[0], y[0],
            record->posZBits);
        if (verticesThisRecord == 2u)
        {
            packed = packed && me_render_stream_write_vertex(
                out + 1u, record->u1Bits, record->v1Bits, color, x[3], y[3],
                record->posZBits);
        }
        else
        {
            packed = packed && me_render_stream_write_vertex(
                out + 1u, record->u1Bits, record->v0Bits, color, x[1], y[1],
                record->posZBits);
            packed = packed && me_render_stream_write_vertex(
                out + 2u, record->u0Bits, record->v1Bits, color, x[2], y[2],
                record->posZBits);
            packed = packed && me_render_stream_write_vertex(
                out + 3u, record->u1Bits, record->v1Bits, color, x[3], y[3],
                record->posZBits);
        }
        if (!packed)
        {
            // C1 has no lossy wrap/clamp fallback for out-of-domain live
            // data.  Reject this optional stream before any bytes are
            // published; the established canonical SC path draws it.
            *outFirstBadRecord = recordIndex;
            return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
        }
        vertexCount += verticesThisRecord;
        ++emittedRecords;
        ++run->recordCount;
        run->vertexCount += verticesThisRecord;
    }

#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    if (directList)
    {
        for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        {
            if (listCursor.nextPhys[bucket] != 0u)
            {
                *outFirstBadRecord = recordCount ? recordCount - 1u : 0u;
                return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
            }
        }
    }
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (itemList && !me_render_stream_item_cursor_finish(
                        &itemCursor, itemLayout, recordCount))
    {
        *outFirstBadRecord = recordCount ? recordCount - 1u : 0u;
        return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
    }
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    if (effectList && !me_render_stream_effect_cursor_finish(
                          &effectCursor, effectLayout, recordCount))
    {
        *outFirstBadRecord = recordCount ? recordCount - 1u : 0u;
        return TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
    }
#endif
#endif
#endif
    (void)emittedRecords;
    *outVertexCount = vertexCount;
    *outRunCount = runCount;
    return TH07_PSP_ME_RENDER_STREAM_RESULT_OK;
}
#endif

// Shared verbatim by SC reference generation and the ME worker.  Keep every
// floating operation explicit: M0 is testing this exact scalar-COP1 sequence,
// not a numerically similar result.  The input supplies sin/cos bits; no trig
// function or VFPU operation is permitted here.
static __attribute__((noinline)) void me_render_expand_kernel(
    const unsigned char *input, uint32_t stride, uint32_t count,
    MeVertexTexColorPosition *output)
{
    for (uint32_t recordIndex = 0; recordIndex < count; ++recordIndex)
    {
        const Th07PspMeRenderRecord32 *record =
            (const Th07PspMeRenderRecord32 *)(input + recordIndex * stride);
        const float centerX = me_render_bits_float(record->centerXBits);
        const float centerY = me_render_bits_float(record->centerYBits);
        const float halfWidth = me_render_bits_float(record->halfWidthBits);
        const float halfHeight = me_render_bits_float(record->halfHeightBits);
        const float sinValue = me_render_bits_float(record->sinBits);
        const float cosValue = me_render_bits_float(record->cosBits);
        const int rotated = (record->flags & TH07_PSP_ME_RENDER_RECORD_ROTATED) != 0;
        MeVertexTexColorPosition *vertices = output + recordIndex * 4u;

        for (uint32_t corner = 0; corner < 4u; ++corner)
        {
            const float localX = (corner == 0u || corner == 2u)
                                     ? -halfWidth
                                     : halfWidth;
            const float localY = corner < 2u ? -halfHeight : halfHeight;
            float x;
            float y;
            if (rotated)
            {
                const float xCos = localX * cosValue;
                const float ySin = localY * sinValue;
                const float xSin = localX * sinValue;
                const float yCos = localY * cosValue;
                const float rotatedX = xCos - ySin;
                const float rotatedY = xSin + yCos;
                x = rotatedX + centerX;
                y = rotatedY + centerY;
            }
            else
            {
                x = centerX + localX;
                y = centerY + localY;
            }
            vertices[corner].u =
                (corner == 0u || corner == 2u) ? 0x00000000u : 0x3f800000u;
            vertices[corner].v = corner < 2u ? 0x00000000u : 0x3f800000u;
            vertices[corner].color = record->color;
            vertices[corner].x = me_render_float_bits(x);
            vertices[corner].y = me_render_float_bits(y);
            vertices[corner].z = 0x3f000000u;
        }
    }
}
#endif

static int clamp_s16(int sample)
{
    if (sample > 32767)
        return 32767;
    if (sample < -32768)
        return -32768;
    return sample;
}

static int decode_mulaw8(unsigned char encoded)
{
    const unsigned int value = (unsigned int)(encoded ^ 0xffu);
    int sample = (int)((((value & 0x0fu) << 3) + 0x84u)
                       << ((value >> 4) & 7u)) - 0x84;
    return (value & 0x80u) ? -sample : sample;
}

static int apply_gain_q16(int sample, uint32_t gainQ16)
{
    if (gainQ16 == 65536u)
        return sample;
    int product = (int)sample * (int)gainQ16;
    // Match TH07's original Q15 mixer exactly.  Allegrex arithmetic right
    // shift rounds negative products down; gainQ16 is the Q15 value doubled.
    return product >> 16;
}

static void mix_on_sc_output(const Th07PspMixJob *job, short *output, int *wideOutput)
{
    const unsigned int frames = job->frames <= TH07_PSP_ME_MAX_MIX_FRAMES
                                    ? job->frames
                                    : TH07_PSP_ME_MAX_MIX_FRAMES;
    const unsigned int samples = frames * 2;
    memset(gScWide, 0, samples * sizeof(gScWide[0]));

    unsigned int inputCount = job->inputCount;
    if (inputCount > TH07_PSP_ME_MAX_MIX_INPUTS)
        inputCount = TH07_PSP_ME_MAX_MIX_INPUTS;
    for (unsigned int inputIndex = 0; inputIndex < inputCount; ++inputIndex)
    {
        const Th07PspMixInput *input = &job->inputs[inputIndex];
        if (!input->samples || input->destinationFrame >= frames || input->frames == 0 ||
            input->stepFixed == 0 || (input->channels != 1 && input->channels != 2) ||
            input->sampleFormat > TH07_PSP_MIX_MULAW8 ||
            (input->sampleFormat == TH07_PSP_MIX_MULAW8 && input->channels != 1))
            continue;
        int *destination = gScWide + input->destinationFrame * 2;
        uint64_t sourceFixed = ((uint64_t)input->sourceFrame << 16) |
                               (uint64_t)(input->sourceFraction & 0xffffu);
        const unsigned int outputFrames = frames - input->destinationFrame;
        for (unsigned int frame = 0; frame < outputFrames; ++frame)
        {
            const unsigned int sourceFrame = (unsigned int)(sourceFixed >> 16);
            if (sourceFrame >= input->frames)
                break;
            if (input->channels == 1)
            {
                const int sourceValue = input->sampleFormat == TH07_PSP_MIX_MULAW8
                                            ? decode_mulaw8(
                                                  ((const unsigned char *)input->samples)[sourceFrame])
                                            : ((const short *)input->samples)[sourceFrame];
                const int value = apply_gain_q16(sourceValue, input->gainQ16);
                destination[frame * 2] += value;
                destination[frame * 2 + 1] += value;
            }
            else
            {
                const short *source = (const short *)input->samples + sourceFrame * 2;
                destination[frame * 2] += apply_gain_q16(source[0], input->gainQ16);
                destination[frame * 2 + 1] += apply_gain_q16(source[1], input->gainQ16);
            }
            sourceFixed += input->stepFixed;
        }
    }

    if (!output && !wideOutput)
        return;
    const int divisor = job->mixDivisor ? (int)job->mixDivisor : 1;
    for (unsigned int sample = 0; sample < samples; ++sample)
    {
        const int mixed = gScWide[sample] / divisor;
        if (wideOutput)
            wideOutput[sample] = mixed;
        else
            output[sample] = (short)clamp_s16(mixed);
    }
}

static void mix_on_sc(const Th07PspMixJob *job, short *output)
{
    mix_on_sc_output(job, output, 0);
}

static void me_invalidate_stream(uint32_t physical, uint32_t stride,
                                 uint32_t count, uint32_t elementBytes)
{
    if (!physical || !stride || !count)
        return;
    const uint32_t start = (0x80000000u | physical) & ~63u;
    const uint32_t last = (0x80000000u | physical) + (count - 1u) * stride + elementBytes;
    const uint32_t end = (last + 63u) & ~63u;
    meLibDcacheInvalidateRange(start, end - start);
}

static void sc_writeback_stream(const void *base, uint32_t stride,
                                uint32_t count, uint32_t elementBytes)
{
    if (!base || !stride || !count)
        return;
    const uintptr_t start = (uintptr_t)base & ~(uintptr_t)63u;
    const uintptr_t last = (uintptr_t)base + (count - 1u) * stride + elementBytes;
    const uintptr_t end = (last + 63u) & ~(uintptr_t)63u;
    sceKernelDcacheWritebackRange((void *)start, end - start);
}

static void finish_me_job(volatile MeSharedMailbox *box)
{
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (!stack_guards_match_on_me())
        box->stackFault = 1;
#endif
    box->completedJobs++;
    __asm__ volatile("sync");
    box->command = ME_CMD_NONE;
    __asm__ volatile("sync");
    box->status = ME_STAT_DONE;
}

static void process_audio_on_me(volatile MeSharedMailbox *box, volatile int *wide)
{
    uint32_t frames = box->audioFrames;
    if (frames > TH07_PSP_ME_MAX_MIX_FRAMES)
        frames = TH07_PSP_ME_MAX_MIX_FRAMES;
    uint32_t inputCount = box->audioInputCount;
    if (inputCount > TH07_PSP_ME_MAX_MIX_INPUTS)
        inputCount = TH07_PSP_ME_MAX_MIX_INPUTS;
    short *output = (short *)(0x80000000u | box->audioOutputPhys);
    const int divisor = box->audioMixDivisor ? (int)box->audioMixDivisor : 1;

    for (uint32_t chunkStart = 0; chunkStart < frames;
         chunkStart += ME_AUDIO_ACCUM_FRAMES)
    {
        uint32_t chunkFrames = frames - chunkStart;
        if (chunkFrames > ME_AUDIO_ACCUM_FRAMES)
            chunkFrames = ME_AUDIO_ACCUM_FRAMES;
        const uint32_t chunkEnd = chunkStart + chunkFrames;
        const uint32_t chunkSamples = chunkFrames * 2;
        for (uint32_t sample = 0; sample < chunkSamples; ++sample)
            wide[sample] = 0;

        for (uint32_t inputIndex = 0; inputIndex < inputCount; ++inputIndex)
        {
            const MeMixInput *input = (const MeMixInput *)&box->audioInputs[inputIndex];
            if (!input->sourcePhys || input->destinationFrame >= frames || input->frames == 0 ||
                input->stepFixed == 0 || (input->channels != 1 && input->channels != 2) ||
                input->sampleFormat > TH07_PSP_MIX_MULAW8 ||
                (input->sampleFormat == TH07_PSP_MIX_MULAW8 && input->channels != 1))
                continue;
            const uint32_t inputStart = input->destinationFrame;
            const uint32_t overlapStart = inputStart > chunkStart ? inputStart : chunkStart;
            const uint32_t overlapEnd = chunkEnd;
            if (overlapStart >= overlapEnd)
                continue;

            uint32_t sourceFrame = input->sourceFrame;
            uint32_t sourceFraction = input->sourceFraction & 0xffffu;
            for (uint32_t skip = inputStart; skip < overlapStart; ++skip)
            {
                const uint32_t nextFraction = sourceFraction + input->stepFixed;
                sourceFrame += nextFraction >> 16;
                sourceFraction = nextFraction & 0xffffu;
            }
            const uint32_t firstSourceFrame = sourceFrame;
            if (firstSourceFrame >= input->frames)
                continue;
            uint32_t finalSourceFrame = sourceFrame;
            uint32_t finalSourceFraction = sourceFraction;
            for (uint32_t scan = overlapStart + 1u; scan < overlapEnd; ++scan)
            {
                const uint32_t nextFraction = finalSourceFraction + input->stepFixed;
                finalSourceFrame += nextFraction >> 16;
                finalSourceFraction = nextFraction & 0xffffu;
            }
            if (finalSourceFrame >= input->frames)
                finalSourceFrame = input->frames - 1u;
            const unsigned char *sourceBytes =
                (const unsigned char *)(0x80000000u | input->sourcePhys);
            const uint32_t sourceFrameBytes = input->sampleFormat == TH07_PSP_MIX_MULAW8
                                                  ? 1u
                                                  : input->channels * sizeof(short);
            const unsigned char *firstSource = sourceBytes + firstSourceFrame * sourceFrameBytes;
            const unsigned char *lastSource = sourceBytes + finalSourceFrame * sourceFrameBytes;
            const uint32_t sourceStart = (uint32_t)firstSource & ~63u;
            const uint32_t sourceEnd =
                ((uint32_t)(lastSource + sourceFrameBytes) + 63u) & ~63u;
            meLibDcacheInvalidateRange(sourceStart, sourceEnd - sourceStart);
            volatile int *destination = wide + (overlapStart - chunkStart) * 2;
            const uint32_t outputFrames = overlapEnd - overlapStart;
            for (uint32_t frame = 0; frame < outputFrames; ++frame)
            {
                if (sourceFrame >= input->frames)
                    break;
                if (input->channels == 1)
                {
                    const int sourceValue = input->sampleFormat == TH07_PSP_MIX_MULAW8
                                                ? decode_mulaw8(sourceBytes[sourceFrame])
                                                : ((const short *)sourceBytes)[sourceFrame];
                    const int value = apply_gain_q16(sourceValue, input->gainQ16);
                    destination[frame * 2] += value;
                    destination[frame * 2 + 1] += value;
                }
                else
                {
                    const short *source = (const short *)sourceBytes + sourceFrame * 2;
                    destination[frame * 2] += apply_gain_q16(source[0], input->gainQ16);
                    destination[frame * 2 + 1] += apply_gain_q16(source[1], input->gainQ16);
                }
                const uint32_t nextFraction = sourceFraction + input->stepFixed;
                sourceFrame += nextFraction >> 16;
                sourceFraction = nextFraction & 0xffffu;
            }
        }

        short *chunkOutput = output + chunkStart * 2;
        for (uint32_t sample = 0; sample < chunkSamples; ++sample)
            chunkOutput[sample] = (short)clamp_s16(wide[sample] / divisor);
    }
    meLibDcacheWritebackRange((uint32_t)output, frames * 2 * sizeof(short));
}

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
static void process_bgm_on_me(volatile MeSharedMailbox *box, uint32_t command,
                              uint32_t *activeGeneration)
{
    box->commandResult = ME_BGM_RESULT_BOUNDS;

    if (command == ME_CMD_BGM_RESET)
    {
        *activeGeneration = box->bgmGeneration;
        box->commandResult = ME_BGM_RESULT_OK;
        return;
    }

    if (box->bgmGeneration != *activeGeneration)
    {
        box->commandResult = ME_BGM_RESULT_STALE;
        return;
    }

    const uint32_t bytes = box->bgmBytes;
    const uint32_t offset = box->bgmOffset;
    const uint32_t expectedBytes = command == ME_CMD_BGM_UPLOAD
                                       ? ME_BGM_UPLOAD_BYTES
                                       : ME_BGM_FETCH_BYTES;
    if (bytes != expectedBytes || (offset & 63u) != 0u ||
        offset > ME_BGM_RING_BYTES || bytes > ME_BGM_RING_BYTES - offset ||
        (box->bgmBufferPhys & 63u) != 0u || box->bgmBufferPhys < 0x08000000u ||
        box->bgmBufferPhys > 0x0c000000u - bytes)
        return;

    // Address the ring uncached in both profiles, exactly like the proven
    // 384K hardware build.  The 4M profile briefly used the 0x80000000 cached
    // mask plus manual dcache maintenance here; R8 PSP-3000 CRC telemetry
    // showed that corrupting 91% of fetched blocks (26928/29554), including
    // tail-biased single-bit flips, audible as periodic crackle.
    volatile uint32_t *local =
        (volatile uint32_t *)(ME_BGM_RING_BASE + offset);
    volatile uint32_t *main =
        (volatile uint32_t *)(0x80000000u | box->bgmBufferPhys);
    const uint32_t words = bytes / sizeof(uint32_t);

    if (command == ME_CMD_BGM_UPLOAD)
    {
        meLibDcacheInvalidateRange((uint32_t)main, bytes);
        for (uint32_t index = 0; index < words; ++index)
            local[index] = main[index];
    }
    else
    {
        for (uint32_t index = 0; index < words; ++index)
            main[index] = local[index];
        meLibDcacheWritebackRange((uint32_t)main, bytes);
    }
    box->commandResult = ME_BGM_RESULT_OK;
}
#endif

#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
static int me_main_range_valid(uint32_t physical, uint32_t bytes,
                               uint32_t alignment)
{
    return bytes != 0u && (physical & (alignment - 1u)) == 0u &&
           physical >= 0x08000000u && bytes <= 0x04000000u &&
           physical <= 0x0c000000u - bytes;
}

static int sfx_segment_valid(uint32_t offset, uint32_t frames)
{
    return frames != 0u && (offset & (sizeof(short) - 1u)) == 0u &&
           offset < ME_SFX_ATLAS_BYTES &&
           frames <= (ME_SFX_ATLAS_BYTES - offset) / sizeof(short);
}

static void me_invalidate_local_sfx(uint32_t offset, uint32_t bytes)
{
    const uint32_t start = (0x80000000u | offset) & ~(ME_CACHE_LINE_BYTES - 1u);
    const uint32_t end =
        ((0x80000000u | offset) + bytes + ME_CACHE_LINE_BYTES - 1u) &
        ~(ME_CACHE_LINE_BYTES - 1u);
    meLibDcacheInvalidateRange(start, end - start);
}

static void process_sfx_transfer_on_me(volatile MeSharedMailbox *box,
                                       uint32_t command)
{
    box->commandResult = ME_BGM_RESULT_BOUNDS;
    const uint32_t offset0 = box->sfxOffset0;
    const uint32_t bytes0 = box->sfxBytes0;
    const uint32_t offset1 = box->sfxOffset1;
    const uint32_t bytes1 = box->sfxBytes1;
    const uint32_t totalBytes = bytes0 + bytes1;

    if (!me_main_range_valid(box->sfxBufferPhys, totalBytes,
                             ME_CACHE_LINE_BYTES) ||
        bytes0 == 0u || bytes0 > ME_SFX_TRANSFER_MAX_BYTES ||
        bytes1 > ME_SFX_TRANSFER_MAX_BYTES ||
        totalBytes > ME_SFX_TRANSFER_MAX_BYTES ||
        offset0 > ME_SFX_ATLAS_BYTES ||
        bytes0 > ME_SFX_ATLAS_BYTES - offset0)
        return;

    if (command == ME_CMD_SFX_UPLOAD)
    {
        if (bytes1 != 0u || offset1 != 0u ||
            ((offset0 | bytes0) & (ME_CACHE_LINE_BYTES - 1u)) != 0u)
            return;
    }
    else
    {
        if (((offset0 | bytes0 | offset1 | bytes1) &
             (sizeof(short) - 1u)) != 0u ||
            (totalBytes & (ME_CACHE_LINE_BYTES - 1u)) != 0u ||
            (bytes1 == 0u && offset1 != 0u) ||
            (bytes1 != 0u &&
             (offset1 > ME_SFX_ATLAS_BYTES ||
              bytes1 > ME_SFX_ATLAS_BYTES - offset1)))
            return;
    }

    volatile unsigned char *main =
        (volatile unsigned char *)(0x80000000u | box->sfxBufferPhys);
    if (command == ME_CMD_SFX_UPLOAD)
    {
        volatile unsigned char *local =
            (volatile unsigned char *)(0x80000000u | offset0);
        meLibDcacheInvalidateRange((uint32_t)main, bytes0);
        for (uint32_t index = 0; index < bytes0; ++index)
            local[index] = main[index];
        meLibDcacheWritebackInvalidateRange((uint32_t)local, bytes0);
    }
    else
    {
        const volatile unsigned char *local0 =
            (const volatile unsigned char *)(0x80000000u | offset0);
        me_invalidate_local_sfx(offset0, bytes0);
        for (uint32_t index = 0; index < bytes0; ++index)
            main[index] = local0[index];
        if (bytes1 != 0u)
        {
            const volatile unsigned char *local1 =
                (const volatile unsigned char *)(0x80000000u | offset1);
            me_invalidate_local_sfx(offset1, bytes1);
            for (uint32_t index = 0; index < bytes1; ++index)
                main[bytes0 + index] = local1[index];
        }
        meLibDcacheWritebackRange((uint32_t)main, totalBytes);
    }
    box->commandResult = ME_BGM_RESULT_OK;
}

static int sfx_voice_valid(const volatile MeSfxVoice *voice)
{
    if (!sfx_segment_valid(voice->segment0Offset, voice->segment0Frames) ||
        voice->stepFixed == 0u || voice->gainQ16 > 65536u)
        return 0;
    if (voice->segment1Frames == 0u)
    {
        if (voice->segment1Offset != 0u)
            return 0;
    }
    else if (!sfx_segment_valid(voice->segment1Offset,
                                voice->segment1Frames))
    {
        return 0;
    }
    const uint32_t totalFrames = voice->segment0Frames + voice->segment1Frames;
    return totalFrames >= voice->segment0Frames &&
           voice->sourceFrame < totalFrames;
}

static void process_sfx_mix_on_me(volatile MeSharedMailbox *box,
                                  volatile int *wide)
{
    box->commandResult = ME_BGM_RESULT_BOUNDS;
    const uint32_t frames = box->sfxFrames;
    const uint32_t voiceCount = box->sfxVoiceCount;
    const uint32_t outputBytes = frames * 2u * sizeof(int);
    if (frames == 0u || frames > TH07_PSP_ME_SFX_MAX_MIX_FRAMES ||
        (frames & 15u) != 0u ||
        voiceCount == 0u || voiceCount > TH07_PSP_ME_SFX_MAX_VOICES ||
        !me_main_range_valid(box->sfxBufferPhys, outputBytes,
                             ME_CACHE_LINE_BYTES))
        return;

    for (uint32_t voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex)
    {
        const volatile MeSfxVoice *voice = &box->sfxVoices[voiceIndex];
        if (!sfx_voice_valid(voice))
            return;
    }

    int *output = (int *)(0x80000000u | box->sfxBufferPhys);
    meLibDcacheInvalidateRange((uint32_t)output, outputBytes);
    for (uint32_t chunkStart = 0; chunkStart < frames;
         chunkStart += ME_AUDIO_ACCUM_FRAMES)
    {
        uint32_t chunkFrames = frames - chunkStart;
        if (chunkFrames > ME_AUDIO_ACCUM_FRAMES)
            chunkFrames = ME_AUDIO_ACCUM_FRAMES;
        const uint32_t chunkSamples = chunkFrames * 2u;
        for (uint32_t sample = 0; sample < chunkSamples; ++sample)
            wide[sample] = 0;

        for (uint32_t voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex)
        {
            const volatile MeSfxVoice *voice = &box->sfxVoices[voiceIndex];
            const uint32_t totalFrames =
                voice->segment0Frames + voice->segment1Frames;
            uint64_t sourceFixed = ((uint64_t)voice->sourceFrame << 16) |
                                   (uint64_t)(voice->sourceFraction & 0xffffu);
            sourceFixed += (uint64_t)voice->stepFixed * chunkStart;
            for (uint32_t frame = 0; frame < chunkFrames; ++frame)
            {
                const uint32_t sourceFrame = (uint32_t)(sourceFixed >> 16);
                if (sourceFrame >= totalFrames)
                    break;
                uint32_t localOffset;
                if (sourceFrame < voice->segment0Frames)
                {
                    localOffset = voice->segment0Offset +
                                  sourceFrame * sizeof(short);
                }
                else
                {
                    localOffset = voice->segment1Offset +
                                  (sourceFrame - voice->segment0Frames) * sizeof(short);
                }
                // OR-ing the cached alias is required here: local byte zero is
                // valid SFX data and must never become a null C pointer.
                const volatile short *source =
                    (const volatile short *)(0x80000000u | localOffset);
                const int value = apply_gain_q16(*source, voice->gainQ16);
                wide[frame * 2u] += value;
                wide[frame * 2u + 1u] += value;
                sourceFixed += voice->stepFixed;
            }
        }

        int *chunkOutput = output + chunkStart * 2u;
        for (uint32_t sample = 0; sample < chunkSamples; ++sample)
            chunkOutput[sample] = wide[sample];
    }
    meLibDcacheWritebackRange((uint32_t)output, outputBytes);
    box->commandResult = ME_BGM_RESULT_OK;
}
#endif

static void process_vertices_on_me(volatile MeSharedMailbox *box)
{
    const uint32_t count = box->vertexCount;
    const int textured = box->textured != 0;
    const int colored = box->colored != 0;
    const unsigned char *positions =
        (const unsigned char *)(0x80000000u | box->positionPhys);
    const unsigned char *texcoords =
        textured ? (const unsigned char *)(0x80000000u | box->texcoordPhys) : 0;
    const unsigned char *diffuse =
        colored ? (const unsigned char *)(0x80000000u | box->diffusePhys) : 0;
    void *output = (void *)(0x80000000u | box->vertexOutputPhys);

    me_invalidate_stream(box->positionPhys, box->positionStride, count, 12);
    if (textured)
        me_invalidate_stream(box->texcoordPhys, box->texcoordStride, count, 8);
    if (colored)
        me_invalidate_stream(box->diffusePhys, box->diffuseStride, count, 4);

    if (textured && colored)
    {
        MeVertexTexColorPosition *out = (MeVertexTexColorPosition *)output;
        for (uint32_t i = 0; i < count; ++i)
        {
            const unsigned char *position = positions + i * box->positionStride;
            const unsigned char *uv = texcoords + i * box->texcoordStride;
            const unsigned char *color = diffuse + i * box->diffuseStride;
            out[i].u = load_u32_bits(uv);
            out[i].v = load_u32_bits(uv + 4);
            out[i].color = load_u32_bits(color);
            out[i].x = load_u32_bits(position);
            out[i].y = load_u32_bits(position + 4);
            out[i].z = load_u32_bits(position + 8);
        }
    }
    else if (textured)
    {
        MeVertexTexPosition *out = (MeVertexTexPosition *)output;
        for (uint32_t i = 0; i < count; ++i)
        {
            const unsigned char *position = positions + i * box->positionStride;
            const unsigned char *uv = texcoords + i * box->texcoordStride;
            out[i].u = load_u32_bits(uv);
            out[i].v = load_u32_bits(uv + 4);
            out[i].x = load_u32_bits(position);
            out[i].y = load_u32_bits(position + 4);
            out[i].z = load_u32_bits(position + 8);
        }
    }
    else if (colored)
    {
        MeVertexColorPosition *out = (MeVertexColorPosition *)output;
        for (uint32_t i = 0; i < count; ++i)
        {
            const unsigned char *position = positions + i * box->positionStride;
            const unsigned char *color = diffuse + i * box->diffuseStride;
            out[i].color = load_u32_bits(color);
            out[i].x = load_u32_bits(position);
            out[i].y = load_u32_bits(position + 4);
            out[i].z = load_u32_bits(position + 8);
        }
    }
    else
    {
        MeVertexPosition *out = (MeVertexPosition *)output;
        for (uint32_t i = 0; i < count; ++i)
        {
            const unsigned char *position = positions + i * box->positionStride;
            out[i].x = load_u32_bits(position);
            out[i].y = load_u32_bits(position + 4);
            out[i].z = load_u32_bits(position + 8);
        }
    }

    meLibDcacheWritebackRange((uint32_t)output, box->vertexOutputBytes);
}

#if defined(TH07_PSP_ME_RENDER_WORKER)
static void process_render_expand_on_me(volatile MeSharedMailbox *box)
{
    box->renderOutputBytes = 0u;
    box->renderInvalidateCycles = 0u;
    box->renderKernelCycles = 0u;
    box->renderWritebackCycles = 0u;
    box->renderFcr31Before = me_render_read_fcr31();
    box->renderFcr31Effective = box->renderFcr31Before;
    box->renderFcr31After = box->renderFcr31Before;

    uint32_t inputBytes = 0u;
    uint32_t outputBytes = 0u;
    if (box->renderVersion != TH07_PSP_ME_RENDER_VERSION)
    {
        box->renderResult = ME_RENDER_RESULT_VERSION;
        return;
    }
    if (box->renderFlags & ~TH07_PSP_ME_RENDER_JOB_COLD_CACHE)
    {
        box->renderResult = ME_RENDER_RESULT_PROTOCOL;
        return;
    }
    if (!me_render_bounds_valid(box->renderVersion,
                                box->renderInputPhys,
                                box->renderInputBytes,
                                box->renderInputStride,
                                box->renderRecordCount,
                                box->renderOutputPhys,
                                box->renderOutputCapacity,
                                &inputBytes, &outputBytes))
    {
        box->renderResult = ME_RENDER_RESULT_BOUNDS;
        return;
    }

    if (box->renderFlags & TH07_PSP_ME_RENDER_JOB_COLD_CACHE)
    {
        // M0-only first-touch mode.  Keep the mandatory range handoff below in
        // both modes; this flush merely distinguishes cold code/data from a
        // repeat run and is never requested by the gameplay shadow path.
        meLibDcacheWritebackInvalidateAll();
        meLibIcacheInvalidateAll();
    }

    const uint32_t invalidateStart = me_render_read_count();
    if (inputBytes)
        meLibDcacheInvalidateRange(0x80000000u | box->renderInputPhys,
                                   inputBytes);
    if (outputBytes)
        meLibDcacheInvalidateRange(0x80000000u | box->renderOutputPhys,
                                   outputBytes);
    const uint32_t invalidateEnd = me_render_read_count();

    const unsigned char *input =
        (const unsigned char *)(0x80000000u | box->renderInputPhys);
    MeVertexTexColorPosition *output =
        (MeVertexTexColorPosition *)(0x80000000u | box->renderOutputPhys);
    const uint32_t originalFcr31 = box->renderFcr31Before;
    me_render_write_fcr31(0u);
    box->renderFcr31Effective = me_render_read_fcr31();
    const uint32_t kernelStart = me_render_read_count();
    me_render_expand_kernel(input, box->renderInputStride,
                            box->renderRecordCount, output);
    const uint32_t kernelEnd = me_render_read_count();
    me_render_write_fcr31(originalFcr31);
    box->renderFcr31After = me_render_read_fcr31();

    const uint32_t writebackStart = me_render_read_count();
    if (outputBytes)
        meLibDcacheWritebackRange((uint32_t)output, outputBytes);
    const uint32_t writebackEnd = me_render_read_count();

    box->renderInvalidateCycles = invalidateEnd - invalidateStart;
    box->renderKernelCycles = kernelEnd - kernelStart;
    box->renderWritebackCycles = writebackEnd - writebackStart;
    box->renderOutputBytes = outputBytes;
    box->renderResult = ME_RENDER_RESULT_OK;
}

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
static void process_bullet_fast_update_on_me(volatile MeSharedMailbox *box)
{
    volatile MeBulletFastMailbox *mail = &box->bulletFast;
    mail->result = TH07_PSP_ME_BULLET_FAST_JOB_PROTOCOL;
    mail->activeCount = 0u;
    mail->candidateCount = 0u;
    mail->inBoundsCount = 0u;
    mail->noCollisionCount = 0u;
    mail->firstBadSlot = 0xffffffffu;
    mail->invalidateCycles = 0u;
    mail->kernelCycles = 0u;
    mail->writebackCycles = 0u;
    mail->fcr31Before = me_render_read_fcr31();
    mail->fcr31Effective = mail->fcr31Before;
    mail->fcr31After = mail->fcr31Before;
    const Th07PspMeBulletFastJob job = mail->job;
    if (job.version != TH07_PSP_ME_BULLET_FAST_UPDATE_VERSION)
    {
        mail->result = TH07_PSP_ME_BULLET_FAST_JOB_VERSION;
        return;
    }
    if (!me_bullet_fast_job_valid(
            &job, mail->outputPhys, mail->outputCapacity))
    {
        mail->result = TH07_PSP_ME_BULLET_FAST_JOB_BOUNDS;
        return;
    }

    const uint32_t invalidateStart = me_render_read_count();
    // SC publishes the scattered Bullet/sprite/player authority with a whole
    // cache writeback.  Drop every old ME alias before the first live read.
    meLibDcacheWritebackInvalidateAll();
    Th07PspMeBulletFastOutput *output =
        (Th07PspMeBulletFastOutput *)(0x80000000u | mail->outputPhys);
    meLibDcacheInvalidateRange((uint32_t)output, mail->outputCapacity);
    const uint32_t invalidateEnd = me_render_read_count();
    if (!me_bullet_fast_guards_match_on_me())
    {
        mail->invalidateCycles = invalidateEnd - invalidateStart;
        mail->result = TH07_PSP_ME_BULLET_FAST_JOB_GUARD;
        return;
    }

    const uint32_t originalFcr31 = mail->fcr31Before;
    me_render_write_fcr31(0u);
    mail->fcr31Effective = me_render_read_fcr31();
    const uint32_t kernelStart = me_render_read_count();
    uint32_t activeCount = 0u;
    uint32_t candidateCount = 0u;
    uint32_t inBoundsCount = 0u;
    uint32_t noCollisionCount = 0u;
    uint32_t firstBadSlot = 0xffffffffu;
    uint32_t result = me_bullet_fast_update_kernel(
        &job, output, &activeCount, &candidateCount,
        &inBoundsCount, &noCollisionCount, &firstBadSlot);
    const uint32_t kernelEnd = me_render_read_count();
    me_render_write_fcr31(originalFcr31);
    mail->fcr31After = me_render_read_fcr31();

    if (mail->fcr31Effective != 0u ||
        mail->fcr31After != originalFcr31)
        result = TH07_PSP_ME_BULLET_FAST_JOB_PROTOCOL;
    if (!me_bullet_fast_guards_match_on_me())
        result = TH07_PSP_ME_BULLET_FAST_JOB_GUARD;

    const uint32_t writebackStart = me_render_read_count();
    if (result == TH07_PSP_ME_BULLET_FAST_JOB_OK)
    {
        meLibDcacheWritebackRange((uint32_t)output, sizeof(*output));
    }
    else
    {
        // A late record reject may have dirtied a prefix.  Discard it rather
        // than allowing a future whole-cache operation to publish an arena SC
        // was correctly told to ignore.
        meLibDcacheInvalidateRange((uint32_t)output, sizeof(*output));
        activeCount = 0u;
        candidateCount = 0u;
        inBoundsCount = 0u;
        noCollisionCount = 0u;
    }
    const uint32_t writebackEnd = me_render_read_count();

    mail->invalidateCycles = invalidateEnd - invalidateStart;
    mail->kernelCycles = kernelEnd - kernelStart;
    mail->writebackCycles = writebackEnd - writebackStart;
    mail->activeCount = activeCount;
    mail->candidateCount = candidateCount;
    mail->inBoundsCount = inBoundsCount;
    mail->noCollisionCount = noCollisionCount;
    mail->firstBadSlot = firstBadSlot;
    mail->result = result;
}
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
static void process_bullet_compact_update_on_me(
    volatile MeSharedMailbox *box)
{
    volatile MeBulletCompactMailbox *mail = &box->bulletCompact;
    mail->result = TH07_PSP_ME_BULLET_COMPACT_RESULT_PROTOCOL;
    mail->candidateCount = 0u;
    mail->inBoundsCount = 0u;
    mail->noCollisionCount = 0u;
    mail->firstBadSlot = 0xffffffffu;
    mail->invalidateCycles = 0u;
    mail->kernelCycles = 0u;
    mail->writebackCycles = 0u;
    mail->fcr31Before = me_render_read_fcr31();
    mail->fcr31Effective = mail->fcr31Before;
    mail->fcr31After = mail->fcr31Before;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    mail->itemResult = TH07_PSP_ME_ITEM_MOTION_RESULT_DISABLED;
    mail->itemCandidateCount = 0u;
    mail->itemProcessedCount = 0u;
    mail->itemFirstBadSlot = 0xffffffffu;
#endif

    const Th07PspMeBulletCompactJob job = mail->job;
    if (job.version != TH07_PSP_ME_BULLET_COMPACT_VERSION)
    {
        mail->result = TH07_PSP_ME_BULLET_COMPACT_RESULT_VERSION;
        return;
    }
    if (!me_bullet_compact_job_valid(
            &job, mail->seedPhys, mail->seedCapacity,
            mail->outputPhys, mail->outputCapacity))
    {
        mail->result = TH07_PSP_ME_BULLET_COMPACT_RESULT_BOUNDS;
        return;
    }

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    const uint32_t itemMotionRequested =
        (job.flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u;
    const uint32_t itemEnvelopeValid = itemMotionRequested
        ? (mail->itemSeedPhys == me_item_motion_seed_physical(job.seedBank) &&
           mail->itemSeedCapacity == sizeof(Th07PspMeItemMotionSeed) &&
           mail->itemOutputPhys == me_item_motion_output_physical() &&
           mail->itemOutputCapacity == sizeof(Th07PspMeItemMotionOutput))
        : (mail->itemSeedPhys == 0u && mail->itemSeedCapacity == 0u &&
           mail->itemOutputPhys == 0u && mail->itemOutputCapacity == 0u);
    const uint32_t itemEnabled =
        itemMotionRequested && itemEnvelopeValid;
#endif

    const uint32_t invalidateStart = me_render_read_count();
    Th07PspMeBulletCompactSeed *seed =
        (Th07PspMeBulletCompactSeed *)(0x80000000u | mail->seedPhys);
    Th07PspMeBulletCompactOutput *output =
        (Th07PspMeBulletCompactOutput *)(0x80000000u | mail->outputPhys);
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    Th07PspMeItemMotionSeed *itemSeed = itemEnabled
        ? (Th07PspMeItemMotionSeed *)(0x80000000u | mail->itemSeedPhys)
        : (Th07PspMeItemMotionSeed *)0;
    Th07PspMeItemMotionOutput *itemOutput = itemEnabled
        ? (Th07PspMeItemMotionOutput *)(
              0x80000000u | mail->itemOutputPhys)
        : (Th07PspMeItemMotionOutput *)0;
#endif
    // Both arenas are contiguous.  Unlike I-ME6 this command never follows a
    // live Bullet or AnmVm pointer, so no whole-cache handoff is necessary.
    meLibDcacheInvalidateRange((uint32_t)seed, mail->seedCapacity);
    meLibDcacheInvalidateRange((uint32_t)output, mail->outputCapacity);
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    if (itemEnabled)
    {
        meLibDcacheInvalidateRange((uint32_t)itemSeed,
                                   mail->itemSeedCapacity);
        meLibDcacheInvalidateRange((uint32_t)itemOutput,
                                   mail->itemOutputCapacity);
    }
#endif
    if ((job.flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID) != 0u &&
        job.bombClearHighWater != 0u)
    {
        meLibDcacheInvalidateRange(
            0x80000000u | job.bombClearBasePhys,
            job.bombClearHighWater * ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE);
    }
    const uint32_t invalidateEnd = me_render_read_count();
    if (!me_bullet_compact_seed_guards_match_on_me(job.seedBank) ||
        !me_bullet_compact_output_guards_match_on_me())
    {
        mail->invalidateCycles = invalidateEnd - invalidateStart;
        mail->result = TH07_PSP_ME_BULLET_COMPACT_RESULT_GUARD;
        return;
    }

    const uint32_t originalFcr31 = mail->fcr31Before;
    me_render_write_fcr31(0u);
    mail->fcr31Effective = me_render_read_fcr31();
    const uint32_t kernelStart = me_render_read_count();
    uint32_t candidateCount = 0u;
    uint32_t inBoundsCount = 0u;
    uint32_t noCollisionCount = 0u;
    uint32_t firstBadSlot = 0xffffffffu;
    uint32_t result = me_bullet_compact_update_kernel(
        &job, seed, output, &candidateCount, &inBoundsCount,
        &noCollisionCount, &firstBadSlot);
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    uint32_t itemCandidateCount = 0u;
    uint32_t itemProcessedCount = 0u;
    uint32_t itemFirstBadSlot = 0xffffffffu;
    uint32_t itemResult = itemMotionRequested && !itemEnvelopeValid
        ? TH07_PSP_ME_ITEM_MOTION_RESULT_BOUNDS
        : TH07_PSP_ME_ITEM_MOTION_RESULT_DISABLED;
    if (itemEnabled)
    {
        if (!me_item_motion_seed_guards_match_on_me(job.seedBank) ||
            !me_item_motion_output_guards_match_on_me())
        {
            itemResult = TH07_PSP_ME_ITEM_MOTION_RESULT_GUARD;
        }
        else
        {
            itemResult = me_item_motion_update_kernel(
                &job, itemSeed, itemOutput,
                &itemCandidateCount, &itemProcessedCount,
                &itemFirstBadSlot);
        }
    }
#endif
    const uint32_t kernelEnd = me_render_read_count();
    me_render_write_fcr31(originalFcr31);
    mail->fcr31After = me_render_read_fcr31();

    if (mail->fcr31Effective != 0u ||
        mail->fcr31After != originalFcr31)
    {
        result = TH07_PSP_ME_BULLET_COMPACT_RESULT_PROTOCOL;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        if (itemMotionRequested)
            itemResult = TH07_PSP_ME_ITEM_MOTION_RESULT_PROTOCOL;
#endif
    }
    if (!me_bullet_compact_seed_guards_match_on_me(job.seedBank) ||
        !me_bullet_compact_output_guards_match_on_me())
        result = TH07_PSP_ME_BULLET_COMPACT_RESULT_GUARD;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    if (itemEnabled &&
        (!me_item_motion_seed_guards_match_on_me(job.seedBank) ||
         !me_item_motion_output_guards_match_on_me()))
        itemResult = TH07_PSP_ME_ITEM_MOTION_RESULT_GUARD;
#endif

    const uint32_t writebackStart = me_render_read_count();
    if (result == TH07_PSP_ME_BULLET_COMPACT_RESULT_OK)
    {
        meLibDcacheWritebackRange((uint32_t)output, sizeof(*output));
    }
    else
    {
        // A reject may have dirtied a result prefix; discard it so a later
        // whole-cache operation cannot publish bytes SC was told to ignore.
        meLibDcacheInvalidateRange((uint32_t)output, sizeof(*output));
        candidateCount = 0u;
        inBoundsCount = 0u;
        noCollisionCount = 0u;
    }
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    if (itemEnabled && itemResult == TH07_PSP_ME_ITEM_MOTION_RESULT_OK)
    {
        // Publish payload with committed=0, then expose the header line last.
        itemOutput->header.committed = 0u;
        meLibDcacheWritebackRange((uint32_t)itemOutput,
                                   sizeof(*itemOutput));
        __asm__ volatile("sync");
        itemOutput->header.committed = TH07_PSP_ME_ITEM_MOTION_COMMITTED;
        meLibDcacheWritebackRange(
            (uint32_t)&itemOutput->header, sizeof(itemOutput->header));
        __asm__ volatile("sync");
    }
    else if (itemEnabled)
    {
        meLibDcacheInvalidateRange((uint32_t)itemOutput,
                                   sizeof(*itemOutput));
        itemCandidateCount = 0u;
        itemProcessedCount = 0u;
    }
#endif
    const uint32_t writebackEnd = me_render_read_count();

    mail->invalidateCycles = invalidateEnd - invalidateStart;
    mail->kernelCycles = kernelEnd - kernelStart;
    mail->writebackCycles = writebackEnd - writebackStart;
    mail->candidateCount = candidateCount;
    mail->inBoundsCount = inBoundsCount;
    mail->noCollisionCount = noCollisionCount;
    mail->firstBadSlot = firstBadSlot;
    mail->result = result;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    mail->itemResult = itemResult;
    mail->itemCandidateCount = itemCandidateCount;
    mail->itemProcessedCount = itemProcessedCount;
    mail->itemFirstBadSlot = itemFirstBadSlot;
#endif
}

#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
static uint32_t me_edram_seed_bench_hash(const void *data, uint32_t bytes)
{
    const uint32_t *words = (const uint32_t *)data;
    uint32_t hash = 2166136261u;
    for (uint32_t index = 0u; index < bytes / sizeof(uint32_t); ++index)
    {
        hash ^= words[index];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t me_edram_seed_bench_mismatch_words(
    const void *lhs, const void *rhs, uint32_t bytes)
{
    const uint32_t *left = (const uint32_t *)lhs;
    const uint32_t *right = (const uint32_t *)rhs;
    uint32_t mismatches = 0u;
    for (uint32_t index = 0u; index < bytes / sizeof(uint32_t); ++index)
    {
        if (left[index] != right[index])
            ++mismatches;
    }
    return mismatches;
}

static void me_edram_seed_bench_sort(
    uint32_t samples[ME_EDRAM_SEED_BENCH_SAMPLES])
{
    for (uint32_t index = 1u; index < ME_EDRAM_SEED_BENCH_SAMPLES;
         ++index)
    {
        const uint32_t value = samples[index];
        uint32_t insert = index;
        while (insert != 0u && samples[insert - 1u] > value)
        {
            samples[insert] = samples[insert - 1u];
            --insert;
        }
        samples[insert] = value;
    }
}

static uint32_t me_edram_seed_bench_p50(
    uint32_t samples[ME_EDRAM_SEED_BENCH_SAMPLES])
{
    me_edram_seed_bench_sort(samples);
    return samples[ME_EDRAM_SEED_BENCH_SAMPLES / 2u];
}

static uint32_t me_edram_seed_bench_p99(
    uint32_t samples[ME_EDRAM_SEED_BENCH_SAMPLES])
{
    me_edram_seed_bench_sort(samples);
    return samples[ME_EDRAM_SEED_BENCH_SAMPLES - 1u];
}

static void me_edram_seed_bench_fill_guards(void)
{
    volatile unsigned char *const area =
        (volatile unsigned char *)ME_EDRAM_SEED_BENCH_AREA_BASE;
    const uint32_t upper = ME_EDRAM_SEED_BENCH_GUARD_BYTES +
                           sizeof(Th07PspMeBulletCompactSeed);
    for (uint32_t index = 0u; index < ME_EDRAM_SEED_BENCH_GUARD_BYTES;
         ++index)
    {
        area[index] = ME_EDRAM_SEED_BENCH_GUARD_PATTERN;
        area[upper + index] = ME_EDRAM_SEED_BENCH_GUARD_PATTERN;
    }
    __asm__ volatile("sync");
}

static int me_edram_seed_bench_guards_match(void)
{
    const volatile unsigned char *const area =
        (const volatile unsigned char *)ME_EDRAM_SEED_BENCH_AREA_BASE;
    const uint32_t upper = ME_EDRAM_SEED_BENCH_GUARD_BYTES +
                           sizeof(Th07PspMeBulletCompactSeed);
    for (uint32_t index = 0u; index < ME_EDRAM_SEED_BENCH_GUARD_BYTES;
         ++index)
    {
        if (area[index] != ME_EDRAM_SEED_BENCH_GUARD_PATTERN ||
            area[upper + index] != ME_EDRAM_SEED_BENCH_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int me_edram_seed_bench_run_kernel(
    const Th07PspMeBulletCompactJob *job,
    const Th07PspMeBulletCompactSeed *seed,
    Th07PspMeBulletCompactOutput *output)
{
    uint32_t candidateCount = 0u;
    uint32_t inBoundsCount = 0u;
    uint32_t noCollisionCount = 0u;
    uint32_t firstBadSlot = 0xffffffffu;
    const uint32_t result = me_bullet_compact_update_kernel(
        job, seed, output, &candidateCount, &inBoundsCount,
        &noCollisionCount, &firstBadSlot);
    return result == TH07_PSP_ME_BULLET_COMPACT_RESULT_OK &&
           candidateCount == seed->header.candidateCount &&
           inBoundsCount == seed->header.candidateCount &&
           noCollisionCount == seed->header.candidateCount &&
           firstBadSlot == 0xffffffffu;
}

static void process_edram_seed_bench_on_me(volatile MeSharedMailbox *box)
{
    volatile MeEdramSeedBenchMailbox *mail = &box->edramSeedBench;
    mail->result = ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL;
    mail->mainTotalP50 = 0u;
    mail->mainTotalP99 = 0u;
    mail->stageTotalP50 = 0u;
    mail->stageTotalP99 = 0u;
    mail->mirrorTotalP50 = 0u;
    mail->mirrorTotalP99 = 0u;
    mail->mainInvalidateP50 = 0u;
    mail->mainKernelP50 = 0u;
    mail->mainWritebackP50 = 0u;
    mail->mainToLocalP50 = 0u;
    mail->localKernelP50 = 0u;
    mail->localWritebackP50 = 0u;
    mail->localToMainP50 = 0u;
    mail->mismatchWords = 0u;
    mail->inputHash = 0u;
    mail->localHash = 0u;
    mail->guardFaults = 0u;
    mail->fcr31Before = me_render_read_fcr31();
    mail->fcr31Effective = mail->fcr31Before;
    mail->fcr31After = mail->fcr31Before;

    const Th07PspMeBulletCompactJob job = mail->job;
    if (mail->samples != ME_EDRAM_SEED_BENCH_SAMPLES ||
        mail->recordCount > TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS ||
        mail->seedCapacity != sizeof(Th07PspMeBulletCompactSeed) ||
        mail->mirrorCapacity != sizeof(Th07PspMeBulletCompactSeed) ||
        mail->outputCapacity != sizeof(Th07PspMeBulletCompactOutput) ||
        !me_bullet_compact_job_valid(
            &job, mail->seedPhys, mail->seedCapacity,
            mail->outputPhys, mail->outputCapacity) ||
        !me_render_main_ram_range_valid(
            mail->mirrorPhys, mail->mirrorCapacity) ||
        me_render_ranges_overlap(
            mail->seedPhys, mail->seedCapacity,
            mail->mirrorPhys, mail->mirrorCapacity) ||
        me_render_ranges_overlap(
            mail->mirrorPhys, mail->mirrorCapacity,
            mail->outputPhys, mail->outputCapacity))
    {
        mail->result = ME_EDRAM_SEED_BENCH_RESULT_BOUNDS;
        return;
    }

    Th07PspMeBulletCompactSeed *const mainSeed =
        (Th07PspMeBulletCompactSeed *)(0x80000000u | mail->seedPhys);
    Th07PspMeBulletCompactSeed *const mainMirror =
        (Th07PspMeBulletCompactSeed *)(0x80000000u | mail->mirrorPhys);
    Th07PspMeBulletCompactOutput *const output =
        (Th07PspMeBulletCompactOutput *)(0x80000000u | mail->outputPhys);
    Th07PspMeBulletCompactSeed *const localSeed =
        (Th07PspMeBulletCompactSeed *)ME_EDRAM_SEED_BENCH_SEED_BASE;
    unsigned char *const expectedOutput =
        gMeRenderBenchCopyArea + ME_RENDER_BENCH_GUARD_BYTES;

    meLibDcacheInvalidateRange((uint32_t)mainSeed, sizeof(*mainSeed));
    meLibDcacheInvalidateRange((uint32_t)mainMirror, sizeof(*mainMirror));
    meLibDcacheInvalidateRange((uint32_t)output, sizeof(*output));
    if (!me_bullet_compact_seed_header_valid(mainSeed, job.seedBank) ||
        mainSeed->header.recordCount != mail->recordCount)
    {
        mail->result = ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL;
        return;
    }

    me_edram_seed_bench_fill_guards();
    memcpy(localSeed, mainSeed, sizeof(*localSeed));
    __asm__ volatile("sync");
    mail->inputHash = me_edram_seed_bench_hash(mainSeed, sizeof(*mainSeed));
    mail->localHash = me_edram_seed_bench_hash(localSeed, sizeof(*localSeed));
    if (mail->inputHash != mail->localHash ||
        !me_edram_seed_bench_guards_match())
    {
        mail->guardFaults = me_edram_seed_bench_guards_match() ? 0u : 1u;
        mail->result = mail->guardFaults
            ? ME_EDRAM_SEED_BENCH_RESULT_GUARD
            : ME_EDRAM_SEED_BENCH_RESULT_MISMATCH;
        memset((void *)ME_EDRAM_SEED_BENCH_AREA_BASE, 0,
               ME_EDRAM_SEED_BENCH_GUARD_BYTES + sizeof(*localSeed) +
                   ME_EDRAM_SEED_BENCH_GUARD_BYTES);
        __asm__ volatile("sync");
        return;
    }

    const uint32_t originalFcr31 = mail->fcr31Before;
    me_render_write_fcr31(0u);
    mail->fcr31Effective = me_render_read_fcr31();
    if (!me_edram_seed_bench_run_kernel(&job, mainSeed, output))
    {
        me_render_write_fcr31(originalFcr31);
        mail->fcr31After = me_render_read_fcr31();
        mail->result = ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL;
        memset((void *)ME_EDRAM_SEED_BENCH_AREA_BASE, 0,
               ME_EDRAM_SEED_BENCH_GUARD_BYTES + sizeof(*localSeed) +
                   ME_EDRAM_SEED_BENCH_GUARD_BYTES);
        __asm__ volatile("sync");
        return;
    }
    memcpy(expectedOutput, output, sizeof(*output));

    uint32_t mainTotal[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t mainInvalidate[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t mainKernel[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t mainWriteback[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t stageTotal[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t mainToLocal[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t localKernel[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t localWriteback[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t mirrorTotal[ME_EDRAM_SEED_BENCH_SAMPLES];
    uint32_t localToMain[ME_EDRAM_SEED_BENCH_SAMPLES];

    for (uint32_t sample = 0u; sample < ME_EDRAM_SEED_BENCH_SAMPLES;
         ++sample)
    {
        const uint32_t mainStart = me_render_read_count();
        const uint32_t mainInvalidateStart = me_render_read_count();
        meLibDcacheInvalidateRange((uint32_t)mainSeed, sizeof(*mainSeed));
        meLibDcacheInvalidateRange((uint32_t)output, sizeof(*output));
        const uint32_t mainInvalidateEnd = me_render_read_count();
        const uint32_t mainKernelStart = me_render_read_count();
        const int mainOk =
            me_edram_seed_bench_run_kernel(&job, mainSeed, output);
        const uint32_t mainKernelEnd = me_render_read_count();
        const uint32_t mainWritebackStart = me_render_read_count();
        meLibDcacheWritebackRange((uint32_t)output, sizeof(*output));
        const uint32_t mainEnd = me_render_read_count();
        mainTotal[sample] = mainEnd - mainStart;
        mainInvalidate[sample] = mainInvalidateEnd - mainInvalidateStart;
        mainKernel[sample] = mainKernelEnd - mainKernelStart;
        mainWriteback[sample] = mainEnd - mainWritebackStart;
        if (!mainOk)
        {
            mail->result = ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL;
            goto edram_bench_done;
        }
        mail->mismatchWords += me_edram_seed_bench_mismatch_words(
            output, expectedOutput, sizeof(*output));

        const uint32_t stageStart = me_render_read_count();
        meLibDcacheInvalidateRange((uint32_t)mainSeed, sizeof(*mainSeed));
        const uint32_t copyStart = me_render_read_count();
        memcpy(localSeed, mainSeed, sizeof(*localSeed));
        __asm__ volatile("sync");
        const uint32_t copyEnd = me_render_read_count();
        meLibDcacheInvalidateRange((uint32_t)output, sizeof(*output));
        const uint32_t localKernelStart = me_render_read_count();
        const int stageOk =
            me_edram_seed_bench_run_kernel(&job, localSeed, output);
        const uint32_t localKernelEnd = me_render_read_count();
        const uint32_t localWritebackStart = me_render_read_count();
        meLibDcacheWritebackRange((uint32_t)output, sizeof(*output));
        const uint32_t stageEnd = me_render_read_count();
        stageTotal[sample] = stageEnd - stageStart;
        mainToLocal[sample] = copyEnd - copyStart;
        localKernel[sample] = localKernelEnd - localKernelStart;
        localWriteback[sample] = stageEnd - localWritebackStart;
        if (!stageOk)
        {
            mail->result = ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL;
            goto edram_bench_done;
        }
        mail->mismatchWords += me_edram_seed_bench_mismatch_words(
            output, expectedOutput, sizeof(*output));

        meLibDcacheInvalidateRange((uint32_t)mainMirror,
                                   sizeof(*mainMirror));
        // Diagnostic only: quantify the extra cost of publishing the local
        // image back to a Main-RAM authority mirror.  This is immediate and
        // deliberately makes no retention-safety claim.  The release gate
        // below compares mainTotal with stageTotal, not this asymmetric path.
        const uint32_t mirrorTotalStart = me_render_read_count();
        const uint32_t mirrorStart = me_render_read_count();
        memcpy(mainMirror, localSeed, sizeof(*mainMirror));
        meLibDcacheWritebackRange((uint32_t)mainMirror,
                                  sizeof(*mainMirror));
        const uint32_t mirrorEnd = me_render_read_count();
        meLibDcacheInvalidateRange((uint32_t)output, sizeof(*output));
        const int mirrorOk =
            me_edram_seed_bench_run_kernel(&job, localSeed, output);
        meLibDcacheWritebackRange((uint32_t)output, sizeof(*output));
        const uint32_t mirrorTotalEnd = me_render_read_count();
        mirrorTotal[sample] = mirrorTotalEnd - mirrorTotalStart;
        localToMain[sample] = mirrorEnd - mirrorStart;
        if (!mirrorOk)
        {
            mail->result = ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL;
            goto edram_bench_done;
        }
        mail->mismatchWords += me_edram_seed_bench_mismatch_words(
            output, expectedOutput, sizeof(*output));
        mail->mismatchWords += me_edram_seed_bench_mismatch_words(
            mainMirror, localSeed, sizeof(*mainMirror));
        if (!me_edram_seed_bench_guards_match())
        {
            ++mail->guardFaults;
            mail->result = ME_EDRAM_SEED_BENCH_RESULT_GUARD;
            goto edram_bench_done;
        }
    }

    mail->mainTotalP50 = me_edram_seed_bench_p50(mainTotal);
    mail->mainTotalP99 = me_edram_seed_bench_p99(mainTotal);
    mail->stageTotalP50 = me_edram_seed_bench_p50(stageTotal);
    mail->stageTotalP99 = me_edram_seed_bench_p99(stageTotal);
    mail->mirrorTotalP50 = me_edram_seed_bench_p50(mirrorTotal);
    mail->mirrorTotalP99 = me_edram_seed_bench_p99(mirrorTotal);
    mail->mainInvalidateP50 = me_edram_seed_bench_p50(mainInvalidate);
    mail->mainKernelP50 = me_edram_seed_bench_p50(mainKernel);
    mail->mainWritebackP50 = me_edram_seed_bench_p50(mainWriteback);
    mail->mainToLocalP50 = me_edram_seed_bench_p50(mainToLocal);
    mail->localKernelP50 = me_edram_seed_bench_p50(localKernel);
    mail->localWritebackP50 = me_edram_seed_bench_p50(localWriteback);
    mail->localToMainP50 = me_edram_seed_bench_p50(localToMain);
    mail->inputHash = me_edram_seed_bench_hash(mainSeed, sizeof(*mainSeed));
    mail->localHash = me_edram_seed_bench_hash(localSeed, sizeof(*localSeed));
    if (mail->inputHash != mail->localHash || mail->mismatchWords != 0u)
        mail->result = ME_EDRAM_SEED_BENCH_RESULT_MISMATCH;
    else
        mail->result = ME_EDRAM_SEED_BENCH_RESULT_OK;

edram_bench_done:
    me_render_write_fcr31(originalFcr31);
    mail->fcr31After = me_render_read_fcr31();
    if (mail->fcr31Effective != 0u ||
        mail->fcr31After != originalFcr31)
        mail->result = ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL;
    if (!me_edram_seed_bench_guards_match())
    {
        ++mail->guardFaults;
        mail->result = ME_EDRAM_SEED_BENCH_RESULT_GUARD;
    }
    // No local byte survives command 13.  Gameplay, pause, suspend and stage
    // teardown therefore have no eDRAM lifetime to manage.
    memset((void *)ME_EDRAM_SEED_BENCH_AREA_BASE, 0,
           ME_EDRAM_SEED_BENCH_GUARD_BYTES + sizeof(*localSeed) +
               ME_EDRAM_SEED_BENCH_GUARD_BYTES);
    __asm__ volatile("sync");
}
#endif
#endif

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
static void process_render_stream_on_me(volatile MeSharedMailbox *box)
{
    box->renderStreamOutputBytes = 0u;
    box->renderStreamVertexCount = 0u;
    box->renderStreamRunCount = 0u;
    box->renderStreamOutputHash = 0u;
    box->renderStreamRunHash = 0u;
    box->renderStreamFirstBadRecord = 0xffffffffu;
    box->renderStreamInvalidateCycles = 0u;
    box->renderStreamKernelCycles = 0u;
    box->renderStreamWritebackCycles = 0u;
    box->renderStreamFcr31Before = me_render_read_fcr31();
    box->renderStreamFcr31Effective = box->renderStreamFcr31Before;
    box->renderStreamFcr31After = box->renderStreamFcr31Before;
    box->renderStreamResult = TH07_PSP_ME_RENDER_STREAM_RESULT_PROTOCOL;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    box->renderStreamItemResult = TH07_PSP_ME_RENDER_STREAM_RESULT_OK;
    box->renderStreamItemRecordCount = 0u;
    box->renderStreamItemVertexCount = 0u;
    box->renderStreamItemRunCount = 0u;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    box->renderStreamEffectResult = TH07_PSP_ME_RENDER_STREAM_RESULT_OK;
    box->renderStreamEffectLayer0RecordCount = 0u;
    box->renderStreamEffectLayer0VertexCount = 0u;
    box->renderStreamEffectLayer0RunCount = 0u;
    box->renderStreamEffectLayer3RecordCount = 0u;
    box->renderStreamEffectLayer3VertexCount = 0u;
    box->renderStreamEffectLayer3RunCount = 0u;
#endif
#endif

    uint32_t bucketEnds[6];
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        bucketEnds[bucket] = box->renderStreamBucketEnds[bucket];
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    const uint32_t rawLive =
        (box->renderStreamFlags &
         TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE) != 0u;
    Th07PspMeRenderRawLayout rawLayout = box->renderStreamRawLayout;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const uint32_t directList =
        (box->renderStreamFlags &
         TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST) != 0u;
    Th07PspMeRenderListLayout listLayout = box->renderStreamListLayout;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    const uint32_t itemList =
        (box->renderStreamFlags &
         TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    const uint32_t itemMotionSidecar =
        (box->renderStreamFlags &
         TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED) != 0u;
#endif
    Th07PspMeRenderItemLayout itemLayout = box->renderStreamItemLayout;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    const uint32_t effectList =
        (box->renderStreamFlags &
         TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST) != 0u;
    Th07PspMeRenderEffectLayout effectLayout =
        box->renderStreamEffectLayout;
#endif
#endif
#endif
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    Th07PspMeBulletCompactSeed *compactSeed =
        (Th07PspMeBulletCompactSeed *)0;
    uint32_t compactSeedBank = 0u;
    uint32_t compactSeedCommitted = 0u;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    Th07PspMeItemMotionSeed *itemMotionSeed =
        (Th07PspMeItemMotionSeed *)0;
    uint32_t itemMotionSeedCommitted = 0u;
#endif
#endif
    uint32_t inputBytes = 0u;
    if (!me_render_stream_bounds_valid(
            box->renderStreamVersion, box->renderStreamFlags,
            box->renderStreamSlot, box->renderStreamGeneration,
            bucketEnds, box->renderStreamRecordCount,
            box->renderStreamOffsetXBits, box->renderStreamOffsetYBits,
            box->renderStreamViewportLeftBits,
            box->renderStreamViewportTopBits,
            box->renderStreamViewportRightBits,
            box->renderStreamViewportBottomBits,
            box->renderStreamConfigFlags,
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
            &rawLayout,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
            &listLayout,
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            &itemLayout,
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            &effectLayout,
#endif
#endif
#endif
#endif
            box->renderStreamInputPhys, box->renderStreamInputCapacity,
            box->renderStreamOutputPhys, box->renderStreamOutputCapacity,
            box->renderStreamRunPhys, box->renderStreamRunCapacity,
            &inputBytes))
    {
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        uint32_t expectedVersion = rawLive
            ? TH07_PSP_ME_RENDER_STREAM_RAW_VERSION
            : TH07_PSP_ME_RENDER_STREAM_VERSION;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        if (directList)
            expectedVersion = TH07_PSP_ME_RENDER_STREAM_LIST_VERSION;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        if (itemList)
            expectedVersion = TH07_PSP_ME_RENDER_STREAM_ITEM_VERSION;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        if (effectList)
            expectedVersion = TH07_PSP_ME_RENDER_STREAM_EFFECT_VERSION;
#endif
#endif
#endif
#else
        const uint32_t expectedVersion = TH07_PSP_ME_RENDER_STREAM_VERSION;
#endif
        box->renderStreamResult =
            box->renderStreamVersion != expectedVersion
                ? TH07_PSP_ME_RENDER_STREAM_RESULT_VERSION
                : TH07_PSP_ME_RENDER_STREAM_RESULT_BOUNDS;
        return;
    }

    const uint32_t invalidateStart = me_render_read_count();
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    if (rawLive)
    {
        // SC publishes arbitrary live Bullet/VM/sprite/table cache lines with
        // sceKernelDcacheWritebackAll().  Drop every prior ME cached alias once
        // before following any of those pointers; no local eDRAM is involved.
        meLibDcacheWritebackInvalidateAll();
    }
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    else if (directList)
    {
        // Bullet/VM/sprite metadata keeps the hardware-accepted cached path.
        // Mutable Item state is read separately through KSEG uncached aliases,
        // so it neither depends on nor pollutes this cache fence.
        meLibDcacheWritebackInvalidateAll();
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        compactSeedBank = box->renderStreamFrameSeq &
                          (TH07_PSP_ME_BULLET_COMPACT_BANKS - 1u);
        compactSeed =
            (Th07PspMeBulletCompactSeed *)(0x80000000u |
                me_bullet_compact_seed_physical(compactSeedBank));
        // Invalidate the old bank before checking its independent guards.
        // Publish committed=0 before any capture so a failed render can never
        // leave the prior contents of this bank looking current to SC.
        meLibDcacheInvalidateRange((uint32_t)compactSeed,
                                   sizeof(*compactSeed));
        if (me_bullet_compact_seed_guards_match_on_me(compactSeedBank))
        {
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
            memset(compactSeed, 0,
                   ME_BULLET_COMPACT_SEED_METADATA_BYTES);
#else
            memset(compactSeed, 0,
                   offsetof(Th07PspMeBulletCompactSeed, slots));
#endif
            compactSeed->header.bank = compactSeedBank;
            compactSeed->header.frameSeq = box->renderStreamFrameSeq;
            compactSeed->header.committed = 0u;
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
            meLibDcacheWritebackRange(
                (uint32_t)compactSeed,
                ME_BULLET_COMPACT_SEED_METADATA_BYTES);
#else
            meLibDcacheWritebackRange(
                (uint32_t)compactSeed,
                offsetof(Th07PspMeBulletCompactSeed, slots));
#endif
        }
        else
        {
            compactSeed = (Th07PspMeBulletCompactSeed *)0;
        }
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        if (itemMotionSidecar)
        {
            itemMotionSeed = (Th07PspMeItemMotionSeed *)(
                0x80000000u |
                me_item_motion_seed_physical(compactSeedBank));
            meLibDcacheInvalidateRange((uint32_t)itemMotionSeed,
                                       sizeof(*itemMotionSeed));
            if (me_item_motion_seed_guards_match_on_me(compactSeedBank))
            {
                me_item_motion_seed_reset(
                    itemMotionSeed, compactSeedBank,
                    box->renderStreamFrameSeq);
                meLibDcacheWritebackRange(
                    (uint32_t)itemMotionSeed,
                    offsetof(Th07PspMeItemMotionSeed, slots));
            }
            else
            {
                itemMotionSeed = (Th07PspMeItemMotionSeed *)0;
            }
        }
#endif
#endif
    }
#endif
#endif
    if (inputBytes)
        meLibDcacheInvalidateRange(0x80000000u |
                                       box->renderStreamInputPhys,
                                   inputBytes);
    meLibDcacheInvalidateRange(0x80000000u | box->renderStreamOutputPhys,
                               box->renderStreamOutputCapacity);
    meLibDcacheInvalidateRange(0x80000000u | box->renderStreamRunPhys,
                               box->renderStreamRunCapacity);
    const uint32_t invalidateEnd = me_render_read_count();

    const Th07PspMeRenderStreamRecord *records =
        (const Th07PspMeRenderStreamRecord *)(0x80000000u |
                                              box->renderStreamInputPhys);
    Th07PspMeRenderStreamVertex *vertices =
        (Th07PspMeRenderStreamVertex *)(0x80000000u |
                                        box->renderStreamOutputPhys);
    Th07PspMeRenderStreamRun *runs =
        (Th07PspMeRenderStreamRun *)(0x80000000u |
                                     box->renderStreamRunPhys);
    if ((box->renderStreamFlags &
         TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH) != 0u &&
        me_render_stream_hash_bytes(records, inputBytes) !=
            box->renderStreamPayloadHash)
    {
        box->renderStreamInvalidateCycles = invalidateEnd - invalidateStart;
        box->renderStreamResult =
            TH07_PSP_ME_RENDER_STREAM_RESULT_INPUT_HASH;
        return;
    }

    const uint32_t originalFcr31 = box->renderStreamFcr31Before;
    me_render_write_fcr31(0u);
    box->renderStreamFcr31Effective = me_render_read_fcr31();
    const uint32_t kernelStart = me_render_read_count();
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    uint32_t itemResult = TH07_PSP_ME_RENDER_STREAM_RESULT_OK;
    uint32_t itemRecordCount = 0u;
    uint32_t itemVertexCount = 0u;
    uint32_t itemRunCount = 0u;
    uint32_t itemFirstBadRecord = 0xffffffffu;
    if (itemList)
    {
        itemRecordCount = itemLayout.expectedItemCount;
        itemResult = me_render_stream_expand_kernel(
            records, itemRecordCount,
            &rawLayout, 0u,
            &listLayout, 0u, bucketEnds,
            &itemLayout, 1u,
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            &effectLayout, 0u, 0u,
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
            (Th07PspMeBulletCompactSeed *)0,
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
            itemMotionSeed,
#endif
            vertices,
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES /
                sizeof(Th07PspMeRenderStreamVertex),
            runs, TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS,
            box->renderStreamOffsetXBits, box->renderStreamOffsetYBits,
            box->renderStreamViewportLeftBits,
            box->renderStreamViewportTopBits,
            box->renderStreamViewportRightBits,
            box->renderStreamViewportBottomBits,
            box->renderStreamGlobalColor, box->renderStreamConfigFlags,
            &itemVertexCount, &itemRunCount, &itemFirstBadRecord);
        if (itemResult != TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
        {
            // Item is an optional prefix. Discard its dirty partial bytes and
            // let the independently authoritative Bullet/compact work start
            // at zero exactly as it did before I-ME7.
            meLibDcacheInvalidateRange(
                (uint32_t)vertices,
                TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES);
            meLibDcacheInvalidateRange(
                (uint32_t)runs,
                TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS *
                    sizeof(Th07PspMeRenderStreamRun));
            itemVertexCount = 0u;
            itemRunCount = 0u;
        }
    }
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    uint32_t effectResult = TH07_PSP_ME_RENDER_STREAM_RESULT_OK;
    uint32_t effectLayer0RecordCount = 0u;
    uint32_t effectLayer0VertexCount = 0u;
    uint32_t effectLayer0RunCount = 0u;
    uint32_t effectLayer3RecordCount = 0u;
    uint32_t effectLayer3VertexCount = 0u;
    uint32_t effectLayer3RunCount = 0u;
    uint32_t effectFirstBadRecord = 0xffffffffu;
    if (effectList)
    {
        effectLayer0RecordCount = effectLayout.expectedLayer0Count;
        effectLayer3RecordCount = effectLayout.expectedLayer3Count;
        const uint32_t auxiliaryRecords = itemRecordCount +
            effectLayer0RecordCount + effectLayer3RecordCount;
        if (auxiliaryRecords > TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS ||
            !me_render_stream_effect_lists_prevalidate(
                &rawLayout, &effectLayout))
        {
            effectResult = TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD;
        }
        else
        {
            const uint32_t auxVertexCapacity =
                TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES /
                sizeof(Th07PspMeRenderStreamVertex);
            effectResult = me_render_stream_expand_kernel(
                records, effectLayer0RecordCount,
                &rawLayout, 0u,
                &listLayout, 0u, bucketEnds,
                &itemLayout, 0u,
                &effectLayout, 1u, 0u,
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                (Th07PspMeBulletCompactSeed *)0,
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                (Th07PspMeItemMotionSeed *)0,
#endif
                vertices + itemVertexCount,
                auxVertexCapacity - itemVertexCount,
                runs + itemRunCount,
                TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS - itemRunCount,
                box->renderStreamOffsetXBits,
                box->renderStreamOffsetYBits,
                box->renderStreamViewportLeftBits,
                box->renderStreamViewportTopBits,
                box->renderStreamViewportRightBits,
                box->renderStreamViewportBottomBits,
                box->renderStreamGlobalColor,
                box->renderStreamConfigFlags,
                &effectLayer0VertexCount, &effectLayer0RunCount,
                &effectFirstBadRecord);
            if (effectResult == TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
            {
                for (uint32_t runIndex = 0u;
                     runIndex < effectLayer0RunCount; ++runIndex)
                    runs[itemRunCount + runIndex].firstVertex +=
                        itemVertexCount;
                effectResult = me_render_stream_expand_kernel(
                    records, effectLayer3RecordCount,
                    &rawLayout, 0u,
                    &listLayout, 0u, bucketEnds,
                    &itemLayout, 0u,
                    &effectLayout, 1u, 3u,
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                    (Th07PspMeBulletCompactSeed *)0,
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                    (Th07PspMeItemMotionSeed *)0,
#endif
                    vertices + itemVertexCount + effectLayer0VertexCount,
                    auxVertexCapacity - itemVertexCount -
                        effectLayer0VertexCount,
                    runs + itemRunCount + effectLayer0RunCount,
                    TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS - itemRunCount -
                        effectLayer0RunCount,
                    box->renderStreamOffsetXBits,
                    box->renderStreamOffsetYBits,
                    box->renderStreamViewportLeftBits,
                    box->renderStreamViewportTopBits,
                    box->renderStreamViewportRightBits,
                    box->renderStreamViewportBottomBits,
                    box->renderStreamGlobalColor,
                    box->renderStreamConfigFlags,
                    &effectLayer3VertexCount, &effectLayer3RunCount,
                    &effectFirstBadRecord);
                if (effectResult == TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
                {
                    const uint32_t vertexBase = itemVertexCount +
                        effectLayer0VertexCount;
                    const uint32_t runBase = itemRunCount +
                        effectLayer0RunCount;
                    for (uint32_t runIndex = 0u;
                         runIndex < effectLayer3RunCount; ++runIndex)
                        runs[runBase + runIndex].firstVertex += vertexBase;
                }
            }
        }
        if (effectResult != TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
        {
            // Both Effect layers are one transaction. Discard every possibly
            // dirty Effect byte; because the boundary can share a cache line
            // with Item, discard/rebuild the complete fixed auxiliary prefix
            // rather than risk publishing half a line from either layer.
            meLibDcacheInvalidateRange(
                (uint32_t)vertices,
                TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES);
            meLibDcacheInvalidateRange(
                (uint32_t)runs,
                TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS *
                    sizeof(Th07PspMeRenderStreamRun));
            effectLayer0VertexCount = 0u;
            effectLayer0RunCount = 0u;
            effectLayer3VertexCount = 0u;
            effectLayer3RunCount = 0u;
            itemVertexCount = 0u;
            itemRunCount = 0u;
            if (itemList)
            {
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                // Effect rollback rebuilds Item geometry.  Reset the optional
                // motion bitmap first so an eligibility change cannot leave a
                // stale candidate from the discarded traversal.
                me_item_motion_seed_reset(
                    itemMotionSeed, compactSeedBank,
                    box->renderStreamFrameSeq);
#endif
                itemResult = me_render_stream_expand_kernel(
                    records, itemRecordCount,
                    &rawLayout, 0u,
                    &listLayout, 0u, bucketEnds,
                    &itemLayout, 1u,
                    &effectLayout, 0u, 0u,
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
                    (Th07PspMeBulletCompactSeed *)0,
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
                    itemMotionSeed,
#endif
                    vertices,
                    TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES /
                        sizeof(Th07PspMeRenderStreamVertex),
                    runs, TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS,
                    box->renderStreamOffsetXBits,
                    box->renderStreamOffsetYBits,
                    box->renderStreamViewportLeftBits,
                    box->renderStreamViewportTopBits,
                    box->renderStreamViewportRightBits,
                    box->renderStreamViewportBottomBits,
                    box->renderStreamGlobalColor,
                    box->renderStreamConfigFlags,
                    &itemVertexCount, &itemRunCount,
                    &itemFirstBadRecord);
                if (itemResult != TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
                {
                    meLibDcacheInvalidateRange(
                        (uint32_t)vertices,
                        TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES);
                    meLibDcacheInvalidateRange(
                        (uint32_t)runs,
                        TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS *
                            sizeof(Th07PspMeRenderStreamRun));
                    itemVertexCount = 0u;
                    itemRunCount = 0u;
                }
            }
        }
    }
#endif
#endif
    uint32_t vertexCount = 0u;
    uint32_t runCount = 0u;
    uint32_t firstBadRecord = 0xffffffffu;
    uint32_t result = me_render_stream_expand_kernel(
        records, box->renderStreamRecordCount,
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        &rawLayout, rawLive,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        &listLayout, directList, bucketEnds,
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            &itemLayout, 0u,
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            &effectLayout, 0u, 0u,
#endif
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        compactSeed,
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        (Th07PspMeItemMotionSeed *)0,
#endif
#endif
#endif
        vertices
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            + itemVertexCount
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            + effectLayer0VertexCount + effectLayer3VertexCount
#endif
#endif
        , TH07_PSP_ME_RENDER_STREAM_MAX_VERTEX_BYTES /
              sizeof(Th07PspMeRenderStreamVertex),
        runs
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            + itemRunCount
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            + effectLayer0RunCount + effectLayer3RunCount
#endif
#endif
        , TH07_PSP_ME_RENDER_STREAM_MAX_RUNS,
        box->renderStreamOffsetXBits, box->renderStreamOffsetYBits,
        box->renderStreamViewportLeftBits, box->renderStreamViewportTopBits,
        box->renderStreamViewportRightBits,
        box->renderStreamViewportBottomBits,
        box->renderStreamGlobalColor, box->renderStreamConfigFlags,
        &vertexCount, &runCount, &firstBadRecord);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
    {
        const uint32_t auxiliaryVertexCount = itemVertexCount
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            + effectLayer0VertexCount + effectLayer3VertexCount
#endif
            ;
        const uint32_t auxiliaryRunCount = itemRunCount
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            + effectLayer0RunCount + effectLayer3RunCount
#endif
            ;
        for (uint32_t runIndex = 0u; runIndex < runCount; ++runIndex)
            runs[auxiliaryRunCount + runIndex].firstVertex +=
                auxiliaryVertexCount;
        vertexCount += auxiliaryVertexCount;
        runCount += auxiliaryRunCount;
    }
#endif
    const uint32_t kernelEnd = me_render_read_count();
    me_render_write_fcr31(originalFcr31);
#if !defined(TH07_PSP_ME_RENDER_UV16) && \
    !defined(TH07_PSP_ME_RENDER_XYZ16)
    box->renderStreamFcr31After = me_render_read_fcr31();
#endif

    uint32_t outputBytes = 0u;
    uint32_t runBytes = 0u;
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    uint32_t outputHash = 0u;
    uint32_t runHash = 0u;
#endif
    const uint32_t writebackStart = me_render_read_count();
    if (result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
        box->renderStreamFcr31Effective == 0u
#if !defined(TH07_PSP_ME_RENDER_UV16) && \
    !defined(TH07_PSP_ME_RENDER_XYZ16)
        && box->renderStreamFcr31After == originalFcr31
#endif
        )
    {
        outputBytes = vertexCount * sizeof(Th07PspMeRenderStreamVertex);
        runBytes = runCount * sizeof(Th07PspMeRenderStreamRun);
        if ((box->renderStreamFlags &
             TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT) != 0u)
        {
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
            outputHash = me_render_stream_hash_bytes(vertices, outputBytes);
            runHash = me_render_stream_hash_bytes(runs, runBytes);
#else
            box->renderStreamOutputHash =
                me_render_stream_hash_bytes(vertices, outputBytes);
            box->renderStreamRunHash =
                me_render_stream_hash_bytes(runs, runBytes);
#endif
        }
        if (outputBytes)
            meLibDcacheWritebackRange((uint32_t)vertices, outputBytes);
        if (runBytes)
            meLibDcacheWritebackRange((uint32_t)runs, runBytes);
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    }

    // C5/C6 hardware returned the Allegrex FIR/FCR0 identity (0x3351) when
    // FCR31 was read immediately after restoring it at the end of a long C1
    // stream.  Do not weaken the exact gate.  Instead, let the already-needed
    // output/run cache writeback form an integer-only settling interval while
    // the token is still ME-owned, then read FCR31 before publishing any
    // sidecar commit marker, completion count, READY state or DONE status.
    // A mismatch leaves only inaccessible provisional bytes and follows the
    // existing protocol/quarantine path.
    __asm__ volatile("sync" : : : "memory");
    box->renderStreamFcr31After = me_render_read_fcr31();
    if (box->renderStreamFcr31Effective != 0u ||
        box->renderStreamFcr31After != originalFcr31)
    {
        outputBytes = 0u;
        runBytes = 0u;
        outputHash = 0u;
        runHash = 0u;
        vertexCount = 0u;
        runCount = 0u;
        result = TH07_PSP_ME_RENDER_STREAM_RESULT_PROTOCOL;
        meLibDcacheInvalidateRange(
            (uint32_t)vertices, box->renderStreamOutputCapacity);
        meLibDcacheInvalidateRange(
            (uint32_t)runs, box->renderStreamRunCapacity);
    }
    if (result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
    {
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        if (itemMotionSeed && itemList &&
            itemResult == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
            me_item_motion_seed_guards_match_on_me(compactSeedBank))
        {
            itemMotionSeed->header.version =
                TH07_PSP_ME_ITEM_MOTION_VERSION;
            itemMotionSeed->header.headerBytes =
                sizeof(Th07PspMeItemMotionSeedHeader);
            itemMotionSeed->header.seedBytes = sizeof(*itemMotionSeed);
            itemMotionSeed->header.bank = compactSeedBank;
            itemMotionSeed->header.frameSeq = box->renderStreamFrameSeq;
            itemMotionSeed->header.targetDrawSeq =
                box->renderStreamTargetDrawSeq;
            itemMotionSeed->header.stageEpoch =
                box->renderStreamStageEpoch;
            itemMotionSeed->header.managerEpoch =
                box->renderStreamManagerEpoch;
            itemMotionSeed->header.itemPrepareSerial =
                itemLayout.expectedPrepareSerial;
            itemMotionSeed->header.recordCount = itemRecordCount;
            itemMotionSeed->header.totalCount =
                itemLayout.expectedTotalCount;
            itemMotionSeed->header.commitSequence =
                box->renderStreamFrameSeq;
            itemMotionSeed->header.reserved0 = 0u;
            itemMotionSeed->header.reserved1 = 0u;
            itemMotionSeed->header.committed = 0u;
            meLibDcacheWritebackRange(
                (uint32_t)itemMotionSeed, sizeof(*itemMotionSeed));
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
            __asm__ volatile("sync" : : : "memory");
#else
            __asm__ volatile("sync");
#endif
            itemMotionSeed->header.committed =
                TH07_PSP_ME_ITEM_MOTION_COMMITTED;
            meLibDcacheWritebackRange(
                (uint32_t)&itemMotionSeed->header,
                sizeof(itemMotionSeed->header));
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
            __asm__ volatile("sync" : : : "memory");
#else
            __asm__ volatile("sync");
#endif
            itemMotionSeedCommitted = 1u;
        }
#endif
        if (compactSeed &&
            me_bullet_compact_seed_guards_match_on_me(compactSeedBank))
        {
            // Header ownership is independent from render geometry.  The
            // bitmap/slot payload was produced incrementally during the I5
            // traversal; deliberately do not rescan it for a hash here.
            compactSeed->header.version =
                TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION;
            compactSeed->header.headerBytes =
                sizeof(Th07PspMeBulletCompactSeedHeader);
            compactSeed->header.seedBytes = sizeof(*compactSeed);
            compactSeed->header.backend =
                TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM;
            compactSeed->header.bank = compactSeedBank;
            compactSeed->header.frameSeq = box->renderStreamFrameSeq;
            compactSeed->header.targetDrawSeq =
                box->renderStreamTargetDrawSeq;
            compactSeed->header.stageEpoch = box->renderStreamStageEpoch;
            compactSeed->header.managerEpoch =
                box->renderStreamManagerEpoch;
            compactSeed->header.replayEpoch = box->renderStreamReplayEpoch;
            compactSeed->header.recordCount =
                box->renderStreamRecordCount;
            compactSeed->header.payloadHash = 0u;
            compactSeed->header.commitSequence =
                box->renderStreamFrameSeq;
            compactSeed->header.reserved = 0u;
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
            compactSeed->header.committed = 0u;
            meLibDcacheWritebackRange((uint32_t)compactSeed,
                                      sizeof(*compactSeed));
            __asm__ volatile("sync" : : : "memory");
            compactSeed->header.committed =
                TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED;
            meLibDcacheWritebackRange(
                (uint32_t)&compactSeed->header,
                sizeof(compactSeed->header));
            __asm__ volatile("sync" : : : "memory");
#else
            __asm__ volatile("sync");
            compactSeed->header.committed =
                TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED;
            meLibDcacheWritebackRange((uint32_t)compactSeed,
                                      sizeof(*compactSeed));
#endif
            compactSeedCommitted = 1u;
        }
#endif
    }
#if !defined(TH07_PSP_ME_RENDER_UV16) && \
    !defined(TH07_PSP_ME_RENDER_XYZ16)
    else if (result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
    {
        result = TH07_PSP_ME_RENDER_STREAM_RESULT_PROTOCOL;
    }
#endif
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE) || \
    defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    else if (
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
             // Packed conversion rejects are legal for both the semantic M0
             // input and the live forms.  Never leave a converted prefix.
             result == TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD
#else
             (rawLive
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
              || directList
#endif
             ) &&
             result == TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD
#endif
             )
    {
        // A live object or C1 numeric conversion may legitimately fail for
        // this frame.
        // Discard all possibly dirty partial output/run lines before reporting
        // the soft RECORD result; none of these bytes may reach SC or GE.
        meLibDcacheInvalidateRange((uint32_t)vertices,
                                   box->renderStreamOutputCapacity);
        meLibDcacheInvalidateRange((uint32_t)runs,
                                   box->renderStreamRunCapacity);
    }
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    if (compactSeed && !compactSeedCommitted)
    {
        // The prefix was already published uncommitted before capture.  Drop
        // any dirty sidecar slots without disturbing the independently valid
        // render command or its completion result.
        meLibDcacheInvalidateRange((uint32_t)compactSeed,
                                   sizeof(*compactSeed));
    }
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    if (itemMotionSeed && !itemMotionSeedCommitted)
    {
        // Geometry and Bullet remain independently valid.  A failed Item
        // motion sidecar is only made unpublishable for the next frame.
        itemMotionSeed->header.committed = 0u;
        meLibDcacheWritebackRange(
            (uint32_t)&itemMotionSeed->header,
            sizeof(itemMotionSeed->header));
        meLibDcacheInvalidateRange((uint32_t)itemMotionSeed,
                                   sizeof(*itemMotionSeed));
    }
#endif
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
    {
        // A top-level Bullet/protocol failure cannot publish the otherwise
        // successful prefix because the slot never enters GE-ready ownership.
        itemVertexCount = 0u;
        itemRunCount = 0u;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        effectLayer0VertexCount = 0u;
        effectLayer0RunCount = 0u;
        effectLayer3VertexCount = 0u;
        effectLayer3RunCount = 0u;
#endif
    }
#endif
    const uint32_t writebackEnd = me_render_read_count();

    box->renderStreamInvalidateCycles = invalidateEnd - invalidateStart;
    box->renderStreamKernelCycles = kernelEnd - kernelStart;
    box->renderStreamWritebackCycles = writebackEnd - writebackStart;
    box->renderStreamOutputBytes = outputBytes;
    box->renderStreamVertexCount = vertexCount;
    box->renderStreamRunCount = runCount;
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    box->renderStreamOutputHash = outputHash;
    box->renderStreamRunHash = runHash;
#endif
    box->renderStreamFirstBadRecord = firstBadRecord;
    box->renderStreamResult = result;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    box->renderStreamItemResult = itemResult;
    box->renderStreamItemRecordCount = itemRecordCount;
    box->renderStreamItemVertexCount = itemVertexCount;
    box->renderStreamItemRunCount = itemRunCount;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    box->renderStreamEffectResult = effectResult;
    box->renderStreamEffectLayer0RecordCount = effectLayer0RecordCount;
    box->renderStreamEffectLayer0VertexCount = effectLayer0VertexCount;
    box->renderStreamEffectLayer0RunCount = effectLayer0RunCount;
    box->renderStreamEffectLayer3RecordCount = effectLayer3RecordCount;
    box->renderStreamEffectLayer3VertexCount = effectLayer3VertexCount;
    box->renderStreamEffectLayer3RunCount = effectLayer3RunCount;
#endif
#endif
}
#endif
#endif

// Runs only on ME.  One mailbox serializes audio and vertex jobs; SC audio
// announces intent before claiming it, while rendering uses a non-blocking
// claim and falls back immediately whenever audio needs the worker.
void meLibOnProcess(void)
{
    volatile MeSharedMailbox *box =
        (volatile MeSharedMailbox *)(0x40000000u | (uint32_t)&gMeMailbox);
#if defined(TH07_PSP_MECC_AUDIO_4M)
    uint32_t wideAddress = 0x80000000u | (uint32_t)gMeAudioWide;
#else
    uint32_t wideAddress = ME_AUDIO_EDRAM_ACCUM_BASE;
#endif
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    uint32_t activeGeneration = 0;
#endif
    __asm__ volatile("" : "+r"(wideAddress));
    volatile int *wide = (volatile int *)wideAddress;

    meLibDcacheWritebackInvalidateAll();
    meLibIcacheInvalidateAll();
#if defined(TH07_PSP_ME_RENDER_WORKER)
    me_render_start_count();
#endif
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    box->command = ME_CMD_NONE;
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (!stack_guards_match_on_me())
        box->stackFault = 1;
#endif
    __asm__ volatile("sync");
    box->workerState = ME_WORKER_READY;
    __asm__ volatile("sync");
#endif

    for (;;)
    {
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
        while (box->command == ME_CMD_NONE && !box->suspendRequested)
#if defined(TH07_PSP_ME_RENDER_WORKER)
            me_render_idle_backoff();
#else
            __asm__ volatile("nop; nop; nop; nop;");
#endif
        if (box->suspendRequested)
        {
#if defined(TH07_PSP_MECC_AUDIO_4M)
            if (!stack_guards_match_on_me())
                box->stackFault = 1;
            box->command = ME_CMD_NONE;
            __asm__ volatile("sync");
            box->status = ME_STAT_DONE;
            __asm__ volatile("sync");
            box->workerState = ME_WORKER_STOPPED;
            __asm__ volatile("sync");
#else
            box->command = ME_CMD_NONE;
            box->status = ME_STAT_DONE;
            __asm__ volatile("sync");
            box->workerState = ME_WORKER_STOPPED;
            __asm__ volatile("sync");
#endif
            meLibHalt();
            return;
        }
#else
        while (box->command == ME_CMD_NONE)
            __asm__ volatile("nop; nop; nop; nop;");
#endif

        const uint32_t command = box->command;
        if (command == ME_CMD_STOP)
        {
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
#if defined(TH07_PSP_MECC_AUDIO_4M)
            if (!stack_guards_match_on_me())
                box->stackFault = 1;
            box->command = ME_CMD_NONE;
            __asm__ volatile("sync");
            box->status = ME_STAT_DONE;
            __asm__ volatile("sync");
            box->workerState = ME_WORKER_STOPPED;
            __asm__ volatile("sync");
#else
            box->command = ME_CMD_NONE;
            box->status = ME_STAT_DONE;
            __asm__ volatile("sync");
            box->workerState = ME_WORKER_STOPPED;
            __asm__ volatile("sync");
#endif
#else
            box->status = ME_STAT_DONE;
#endif
            meLibHalt();
            return;
        }
        if (command == ME_CMD_AUDIO_MIX)
            process_audio_on_me(box, wide);
        else if (command == ME_CMD_VERTEX_PACK)
            process_vertices_on_me(box);
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
        else if (command == ME_CMD_BGM_RESET || command == ME_CMD_BGM_UPLOAD ||
                 command == ME_CMD_BGM_FETCH)
            process_bgm_on_me(box, command, &activeGeneration);
#endif
#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
        else if (command == ME_CMD_SFX_UPLOAD || command == ME_CMD_SFX_GATHER)
            process_sfx_transfer_on_me(box, command);
        else if (command == ME_CMD_SFX_MIX)
            process_sfx_mix_on_me(box, wide);
#endif
#if defined(TH07_PSP_ME_RENDER_WORKER)
        else if (command == ME_CMD_RENDER_EXPAND)
            process_render_expand_on_me(box);
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
        else if (command == ME_CMD_RENDER_STREAM)
            process_render_stream_on_me(box);
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
        else if (command == ME_CMD_BULLET_FAST_UPDATE)
            process_bullet_fast_update_on_me(box);
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        else if (command == ME_CMD_BULLET_COMPACT_UPDATE)
            process_bullet_compact_update_on_me(box);
#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
        else if (command == ME_CMD_EDRAM_SEED_BENCH)
            process_edram_seed_bench_on_me(box);
#endif
#endif
#endif
        else
        {
            box->command = ME_CMD_NONE;
            continue;
        }
        finish_me_job(box);
    }
}

typedef struct MeOwnerPriorityGuard
{
    SceUID thread;
    int originalPriority;
} MeOwnerPriorityGuard;

static int enter_me_candidate_priority(MeOwnerPriorityGuard *guard)
{
    guard->thread = -1;
    guard->originalPriority = -1;
#if defined(TH07_PSP_MECC_AUDIO_4M)
    const SceUID thread = sceKernelGetThreadId();
    const int priority = sceKernelGetThreadCurrentPriority();
    if (thread < 0 || priority < 0)
        return 0;
    if (priority > ME_OWNER_PRIORITY_CEILING &&
        sceKernelChangeThreadPriority(thread, ME_OWNER_PRIORITY_CEILING) < 0)
        return 0;
    guard->thread = thread;
    guard->originalPriority = priority;
#endif
    return 1;
}

#if defined(TH07_PSP_MECC_AUDIO_4M)
static void me_priority_restore_failed(void)
{
    __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
    if (__atomic_exchange_n(&gMePoisoned, 1, __ATOMIC_ACQ_REL) == 0)
    {
        th07_psp_boot_note(
            "MECC AUDIO4M PRIORITY RESTORE FAILED -> COLD REBOOT");
    }
}
#endif

static int restore_me_candidate_priority(MeOwnerPriorityGuard *guard)
{
    int restored = 1;
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (guard->thread >= 0 &&
        guard->originalPriority > ME_OWNER_PRIORITY_CEILING)
    {
        if (sceKernelChangeThreadPriority(guard->thread,
                                          guard->originalPriority) < 0)
        {
            restored = 0;
            me_priority_restore_failed();
        }
    }
#endif
    guard->thread = -1;
    guard->originalPriority = -1;
    return restored;
}

static void publish_me_owner_priority(MeOwnerPriorityGuard *guard)
{
#if defined(TH07_PSP_MECC_AUDIO_4M)
    // The candidate is already at the ceiling before the owner CAS.  Publishing
    // this release context after a successful CAS requires no syscall and has
    // no priority-inversion window.
    __atomic_store_n(&gMeOwnerThread, guard->thread, __ATOMIC_RELAXED);
    __atomic_store_n(&gMeOwnerOriginalPriority, guard->originalPriority,
                     __ATOMIC_RELAXED);
#endif
    guard->thread = -1;
    guard->originalPriority = -1;
}

static void release_me(void)
{
#if defined(TH07_PSP_MECC_AUDIO_4M)
    // Copy the per-owner context before publishing NONE.  A different thread
    // may claim immediately after the release-store and overwrite the globals.
    const SceUID ownerThread =
        __atomic_load_n(&gMeOwnerThread, __ATOMIC_RELAXED);
    const int originalPriority =
        __atomic_load_n(&gMeOwnerOriginalPriority, __ATOMIC_RELAXED);
    __atomic_store_n(&gMeOwnerThread, -1, __ATOMIC_RELAXED);
    __atomic_store_n(&gMeOwnerOriginalPriority, -1, __ATOMIC_RELAXED);
#endif
    __atomic_store_n(&gMeOwner, ME_OWNER_NONE, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_AUDIO_4M)
    // Restore only after other waiters can acquire the owner.  A caller that
    // was already above the ceiling was never lowered and needs no change.
    if (ownerThread >= 0 && originalPriority > ME_OWNER_PRIORITY_CEILING &&
        sceKernelChangeThreadPriority(ownerThread, originalPriority) < 0)
    {
        me_priority_restore_failed();
    }
#endif
}

static int claim_me_for_audio(uint32_t startUs)
{
    __atomic_fetch_add(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
    for (;;)
    {
        MeOwnerPriorityGuard priority;
        if (!enter_me_candidate_priority(&priority))
        {
            __atomic_fetch_sub(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
            return 0;
        }
        int expected = ME_OWNER_NONE;
        if (__atomic_compare_exchange_n(&gMeOwner, &expected, ME_OWNER_AUDIO, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
            publish_me_owner_priority(&priority);
            __atomic_fetch_sub(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
            return 1;
        }
        if (!restore_me_candidate_priority(&priority))
        {
            __atomic_fetch_sub(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
            return 0;
        }
        if (sceKernelGetSystemTimeLow() - startUs >= ME_AUDIO_WAIT_US)
        {
            __atomic_fetch_sub(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
            return 0;
        }
        sceKernelDelayThread(20);
    }
}

static int claim_me_for_vertex(void)
{
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0)
        return 0;
    MeOwnerPriorityGuard priority;
    if (!enter_me_candidate_priority(&priority))
        return 0;
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0)
    {
        restore_me_candidate_priority(&priority);
        return 0;
    }
    int expected = ME_OWNER_NONE;
    if (!__atomic_compare_exchange_n(&gMeOwner, &expected, ME_OWNER_VERTEX, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        (void)restore_me_candidate_priority(&priority);
        return 0;
    }
    publish_me_owner_priority(&priority);
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0)
    {
        release_me();
        return 0;
    }
    return 1;
}

#if defined(TH07_PSP_ME_RENDER_WORKER)
static int claim_me_for_render(void)
{
    // An asynchronous owner must not borrow the caller's thread priority: the
    // caller returns while ME is still running.  Current accepted audio uses no
    // ME jobs, but retain the audio-wanted handoff for fail-safe coexistence.
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
        return 0;
    if (!__sync_bool_compare_and_swap(&gMeOwner, ME_OWNER_NONE,
                                      ME_OWNER_RENDER))
        return 0;
    __atomic_store_n(&gMeOwnerThread, -1, __ATOMIC_RELAXED);
    __atomic_store_n(&gMeOwnerOriginalPriority, -1, __ATOMIC_RELAXED);
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        release_me();
        return 0;
    }
    return 1;
}
#endif

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
static int claim_me_for_bullet_fast(void)
{
    // Unlike the asynchronous renderer, ME16 retires before returning and can
    // safely inherit the established owner priority ceiling for the bounded
    // synchronous window.
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
        return 0;
    MeOwnerPriorityGuard priority;
    if (!enter_me_candidate_priority(&priority))
        return 0;
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        (void)restore_me_candidate_priority(&priority);
        return 0;
    }
    int expected = ME_OWNER_NONE;
    if (!__atomic_compare_exchange_n(
            &gMeOwner, &expected, ME_OWNER_BULLET_FAST, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        (void)restore_me_candidate_priority(&priority);
        return 0;
    }
    publish_me_owner_priority(&priority);
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        release_me();
        return 0;
    }
    return 1;
}
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
static int claim_me_for_bullet_compact(void)
{
    // The priority-9 caller returns immediately and cannot lend its thread
    // priority to an asynchronous owner.  This mirrors render ownership while
    // retaining the audio-wanted fail-safe handoff.
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
        return 0;
    if (!__sync_bool_compare_and_swap(&gMeOwner, ME_OWNER_NONE,
                                      ME_OWNER_BULLET_COMPACT))
        return 0;
    __atomic_store_n(&gMeOwnerThread, -1, __ATOMIC_RELAXED);
    __atomic_store_n(&gMeOwnerOriginalPriority, -1, __ATOMIC_RELAXED);
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        release_me();
        return 0;
    }
    return 1;
}
#endif

static void poison_me(void)
{
    __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gMePoisoned, 1, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
#else
    __atomic_fetch_add(&gMeTimeouts, 1u, __ATOMIC_RELAXED);
#endif
}

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
static void timeout_me(void)
{
    poison_me();
    __atomic_fetch_add(&gMeTimeouts, 1u, __ATOMIC_RELAXED);
}

static int claim_me_for_bgm(uint32_t startUs)
{
    __atomic_fetch_add(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
    for (;;)
    {
        MeOwnerPriorityGuard priority;
        if (!enter_me_candidate_priority(&priority))
        {
            __atomic_fetch_sub(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
            return 0;
        }
        int expected = ME_OWNER_NONE;
        if (__atomic_compare_exchange_n(&gMeOwner, &expected, ME_OWNER_AUDIO, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
            publish_me_owner_priority(&priority);
            __atomic_fetch_sub(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
            return 1;
        }
        if (!restore_me_candidate_priority(&priority))
        {
            __atomic_fetch_sub(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
            return 0;
        }
        if (sceKernelGetSystemTimeLow() - startUs >= ME_BGM_COMMAND_TIMEOUT_US)
        {
            __atomic_fetch_sub(&gMeAudioWanted, 1u, __ATOMIC_ACQ_REL);
            return 0;
        }
        sceKernelDelayThread(20);
    }
}

#if defined(TH07_PSP_MECC_AUDIO_4M)
static int claim_me_for_shutdown(uint32_t startUs)
{
    for (;;)
    {
        MeOwnerPriorityGuard priority;
        if (!enter_me_candidate_priority(&priority))
            return 0;
        int expected = ME_OWNER_NONE;
        if (__atomic_compare_exchange_n(&gMeOwner, &expected, ME_OWNER_SHUTDOWN,
                                        0, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE))
        {
            publish_me_owner_priority(&priority);
            return 1;
        }
        if (!restore_me_candidate_priority(&priority))
            return 0;
        if (sceKernelGetSystemTimeLow() - startUs >= ME_BGM_STOP_TIMEOUT_US)
        {
            return 0;
        }
        sceKernelDelayThread(1000);
    }
}
#endif

static int dispatch_bgm(uint32_t command, void *buffer, uint32_t bytes,
                        uint32_t generation, uint32_t ringOffset)
{
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    if (!claim_me_for_bgm(startUs))
    {
        timeout_me();
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (!box || box->command != ME_CMD_NONE ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
    {
        release_me();
        return 0;
    }

    if (buffer && command == ME_CMD_BGM_UPLOAD)
        sceKernelDcacheWritebackRange(buffer, bytes);
    box->bgmGeneration = generation;
    box->bgmOffset = ringOffset;
    box->bgmBytes = bytes;
    box->bgmBufferPhys = buffer ? ((uint32_t)buffer & 0x1fffffffu) : 0u;
    box->commandResult = ME_BGM_RESULT_BOUNDS;
    box->status = ME_STAT_IDLE;
    __asm__ volatile("sync");
    box->command = command;

    while (box->status != ME_STAT_DONE)
    {
        if (sceKernelGetSystemTimeLow() - startUs >= ME_BGM_COMMAND_TIMEOUT_US)
        {
            timeout_me();
            release_me();
            return 0;
        }
        sceKernelDelayThread(20);
    }

    const uint32_t waitUs = sceKernelGetSystemTimeLow() - startUs;
    record_max(&gMeMaxWaitUs, waitUs);
    const uint32_t result = box->commandResult;
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (box->stackFault)
    {
        poison_me();
        release_me();
        return 0;
    }
#endif
    if (result == ME_BGM_RESULT_OK && buffer && command == ME_CMD_BGM_FETCH)
        sceKernelDcacheInvalidateRange(buffer, bytes);
    if (result != ME_BGM_RESULT_OK && result != ME_BGM_RESULT_STALE)
        poison_me();
    if (result == ME_BGM_RESULT_OK)
        __atomic_fetch_add(&gMeJobs, 1u, __ATOMIC_RELAXED);
    release_me();
    return result == ME_BGM_RESULT_OK;
}
#endif

#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
static int dispatch_sfx_transfer(uint32_t command, void *buffer,
                                 uint32_t bytes0, uint32_t offset0,
                                 uint32_t bytes1, uint32_t offset1)
{
    const uint32_t totalBytes = bytes0 + bytes1;
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    if (!claim_me_for_bgm(startUs))
    {
        timeout_me();
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (!box || box->command != ME_CMD_NONE ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
    {
        release_me();
        return 0;
    }

    if (command == ME_CMD_SFX_UPLOAD)
        sceKernelDcacheWritebackRange(buffer, totalBytes);
    else
        sceKernelDcacheWritebackInvalidateRange(buffer, totalBytes);
    box->sfxOffset0 = offset0;
    box->sfxBytes0 = bytes0;
    box->sfxOffset1 = offset1;
    box->sfxBytes1 = bytes1;
    box->sfxBufferPhys = (uint32_t)buffer & 0x1fffffffu;
    box->commandResult = ME_BGM_RESULT_BOUNDS;
    box->status = ME_STAT_IDLE;
    __asm__ volatile("sync");
    box->command = command;

    while (box->status != ME_STAT_DONE)
    {
        if (sceKernelGetSystemTimeLow() - startUs >= ME_BGM_COMMAND_TIMEOUT_US)
        {
            timeout_me();
            release_me();
            return 0;
        }
        sceKernelDelayThread(20);
    }

    const uint32_t waitUs = sceKernelGetSystemTimeLow() - startUs;
    record_max(&gMeMaxWaitUs, waitUs);
    const uint32_t result = box->commandResult;
    if (box->stackFault)
    {
        poison_me();
        release_me();
        return 0;
    }
    if (result == ME_BGM_RESULT_OK && command == ME_CMD_SFX_GATHER)
        sceKernelDcacheInvalidateRange(buffer, totalBytes);
    if (result != ME_BGM_RESULT_OK)
        poison_me();
    else
        __atomic_fetch_add(&gMeJobs, 1u, __ATOMIC_RELAXED);
    release_me();
    return result == ME_BGM_RESULT_OK;
}

static int dispatch_sfx_mix(const Th07PspMeSfxMixJob *job, int *output)
{
    const uint32_t outputBytes = job->frames * 2u * sizeof(int);
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    if (!claim_me_for_bgm(startUs))
    {
        timeout_me();
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (!box || box->command != ME_CMD_NONE ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
    {
        release_me();
        return 0;
    }

    sceKernelDcacheWritebackInvalidateRange(output, outputBytes);
    box->sfxBufferPhys = (uint32_t)output & 0x1fffffffu;
    box->sfxFrames = job->frames;
    box->sfxVoiceCount = job->voiceCount;
    for (uint32_t index = 0; index < job->voiceCount; ++index)
    {
        box->sfxVoices[index].segment0Offset = job->voices[index].segment0Offset;
        box->sfxVoices[index].segment0Frames = job->voices[index].segment0Frames;
        box->sfxVoices[index].segment1Offset = job->voices[index].segment1Offset;
        box->sfxVoices[index].segment1Frames = job->voices[index].segment1Frames;
        box->sfxVoices[index].sourceFrame = job->voices[index].sourceFrame;
        box->sfxVoices[index].sourceFraction = job->voices[index].sourceFraction;
        box->sfxVoices[index].stepFixed = job->voices[index].stepFixed;
        box->sfxVoices[index].gainQ16 = job->voices[index].gainQ16;
    }
    box->commandResult = ME_BGM_RESULT_BOUNDS;
    box->status = ME_STAT_IDLE;
    __asm__ volatile("sync");
    box->command = ME_CMD_SFX_MIX;

    while (box->status != ME_STAT_DONE)
    {
        if (sceKernelGetSystemTimeLow() - startUs >= ME_BGM_COMMAND_TIMEOUT_US)
        {
            timeout_me();
            release_me();
            return 0;
        }
        sceKernelDelayThread(20);
    }

    const uint32_t waitUs = sceKernelGetSystemTimeLow() - startUs;
    record_max(&gMeMaxWaitUs, waitUs);
    const uint32_t result = box->commandResult;
    if (box->stackFault)
    {
        poison_me();
        release_me();
        return 0;
    }
    if (result == ME_BGM_RESULT_OK)
    {
        sceKernelDcacheInvalidateRange(output, outputBytes);
        __atomic_fetch_add(&gMeJobs, 1u, __ATOMIC_RELAXED);
    }
    else
    {
        poison_me();
    }
    release_me();
    return result == ME_BGM_RESULT_OK;
}
#endif

static int dispatch_audio(const Th07PspMixJob *job, short *output)
{
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    if (!claim_me_for_audio(startUs))
    {
        __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
        mix_on_sc(job, output);
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (!box || box->command != ME_CMD_NONE ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE))
    {
        release_me();
        __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
        mix_on_sc(job, output);
        return 0;
    }

    uint32_t inputCount = job->inputCount;
    if (inputCount > TH07_PSP_ME_MAX_MIX_INPUTS)
        inputCount = TH07_PSP_ME_MAX_MIX_INPUTS;
    for (uint32_t index = 0; index < inputCount; ++index)
    {
        const Th07PspMixInput *input = &job->inputs[index];
        if (input->needsWriteback)
        {
            const uint32_t sampleBytes = input->sampleFormat == TH07_PSP_MIX_MULAW8
                                             ? 1u
                                             : input->channels * sizeof(short);
            sc_writeback_stream(input->samples, sampleBytes, input->frames, sampleBytes);
        }
        box->audioInputs[index].sourcePhys = (uint32_t)input->samples & 0x1fffffffu;
        box->audioInputs[index].frames = input->frames;
        box->audioInputs[index].destinationFrame = input->destinationFrame;
        box->audioInputs[index].channels = input->channels;
        box->audioInputs[index].sourceFrame = input->sourceFrame;
        box->audioInputs[index].sourceFraction = input->sourceFraction;
        box->audioInputs[index].stepFixed = input->stepFixed;
        box->audioInputs[index].gainQ16 = input->gainQ16;
        box->audioInputs[index].sampleFormat = input->sampleFormat;
    }
    box->audioFrames = job->frames;
    box->audioInputCount = inputCount;
    box->audioMixDivisor = job->mixDivisor;
    box->audioOutputPhys = (uint32_t)gMeAudioOutput & 0x1fffffffu;
    box->status = ME_STAT_IDLE;
    __asm__ volatile("sync");
    box->command = ME_CMD_AUDIO_MIX;

    while (box->status != ME_STAT_DONE)
    {
        if (sceKernelGetSystemTimeLow() - startUs >= ME_AUDIO_WAIT_US)
        {
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
            timeout_me();
#else
            poison_me();
#endif
            release_me();
            __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
            mix_on_sc(job, output);
            return 0;
        }
        sceKernelDelayThread(20);
    }

    const uint32_t waitUs = sceKernelGetSystemTimeLow() - startUs;
    record_max(&gMeMaxWaitUs, waitUs);
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (box->stackFault)
    {
        poison_me();
        release_me();
        __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
        mix_on_sc(job, output);
        return 0;
    }
#endif
    sceKernelDcacheInvalidateRange(gMeAudioOutput, job->frames * 2 * sizeof(short));
    memcpy(output, gMeAudioOutput, job->frames * 2 * sizeof(short));
    __atomic_fetch_add(&gMeJobs, 1u, __ATOMIC_RELAXED);
    release_me();
    return 1;
}

void th07_psp_me_vertex_frame_begin(void)
{
    if (!__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        gMeVertexArenaOffset = 0;
}

int th07_psp_me_vertex_pack(const Th07PspMeVertexPack *job, const void **output)
{
    if (output)
        *output = 0;
    if (!job || !output || !job->position || !job->positionStride || !job->count ||
        (job->textured && (!job->texcoord || !job->texcoordStride)) ||
        (job->colored && (!job->diffuse || !job->diffuseStride)) ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        return 0;

    if (!claim_me_for_vertex())
    {
        __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (!box || box->command != ME_CMD_NONE ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE))
    {
        release_me();
        __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
        return 0;
    }

    const uint32_t bytesPerVertex = vertex_bytes(job->textured, job->colored);
    if (job->count > ME_VERTEX_ARENA_BYTES / bytesPerVertex)
    {
        release_me();
        __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
        return 0;
    }
    const uint32_t outputBytes = job->count * bytesPerVertex;
    const uint32_t offset = (gMeVertexArenaOffset + 63u) & ~63u;
    if (offset > ME_VERTEX_ARENA_BYTES || outputBytes > ME_VERTEX_ARENA_BYTES - offset)
    {
        release_me();
        __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
        return 0;
    }
    void *const destination = gMeVertexArena + offset;

    sc_writeback_stream(job->position, job->positionStride, job->count, 12);
    if (job->textured)
        sc_writeback_stream(job->texcoord, job->texcoordStride, job->count, 8);
    if (job->colored)
        sc_writeback_stream(job->diffuse, job->diffuseStride, job->count, 4);

    box->positionPhys = (uint32_t)job->position & 0x1fffffffu;
    box->texcoordPhys = (uint32_t)job->texcoord & 0x1fffffffu;
    box->diffusePhys = (uint32_t)job->diffuse & 0x1fffffffu;
    box->vertexOutputPhys = (uint32_t)destination & 0x1fffffffu;
    box->positionStride = job->positionStride;
    box->texcoordStride = job->texcoordStride;
    box->diffuseStride = job->diffuseStride;
    box->vertexCount = job->count;
    box->textured = job->textured != 0;
    box->colored = job->colored != 0;
    box->vertexOutputBytes = outputBytes;
    box->status = ME_STAT_IDLE;
    __asm__ volatile("sync");
    box->command = ME_CMD_VERTEX_PACK;

    const uint32_t startUs = sceKernelGetSystemTimeLow();
    while (box->status != ME_STAT_DONE)
    {
        if (sceKernelGetSystemTimeLow() - startUs >= ME_VERTEX_WAIT_US)
        {
            // A late worker can only touch its dedicated arena.  The fallback
            // uses libGU list memory, so it cannot be corrupted by that write.
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
            timeout_me();
#else
            poison_me();
#endif
            release_me();
            __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
            return 0;
        }
#if defined(TH07_PSP_MECC_AUDIO_4M)
        // The owner remains at priority 0x11, below the 0x10 DAC worker.
        // Sleeping avoids a hot SC poll while the independent ME completes;
        // DAC submission can preempt this owner at every point.
        sceKernelDelayThread(20);
#else
        __asm__ volatile("nop; nop; nop; nop;");
#endif
    }

    const uint32_t waitUs = sceKernelGetSystemTimeLow() - startUs;
    record_max(&gMeMaxWaitUs, waitUs);
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (box->stackFault)
    {
        poison_me();
        release_me();
        return 0;
    }
#endif
    gMeVertexArenaOffset = offset + outputBytes;
    *output = destination;
    __atomic_fetch_add(&gMeJobs, 1u, __ATOMIC_RELAXED);
    release_me();
    return 1;
}

#if defined(TH07_PSP_ME_RENDER_WORKER)
static void me_render_fill_completion(Th07PspMeRenderCompletion *completion)
{
    if (!completion)
        return;
    volatile MeSharedMailbox *box = gMeMailboxUncached;
    memset(completion, 0, sizeof(*completion));
    completion->version = box->renderVersion;
    completion->flags = box->renderFlags;
    completion->frameSeq = box->renderFrameSeq;
    completion->targetDrawSeq = box->renderTargetDrawSeq;
    completion->stageEpoch = box->renderStageEpoch;
    completion->managerEpoch = box->renderManagerEpoch;
    completion->replayEpoch = box->renderReplayEpoch;
    completion->recordCount = box->renderRecordCount;
    completion->inputStride = box->renderInputStride;
    completion->outputBytes = box->renderOutputBytes;
    completion->result = box->renderResult;
    completion->scWritebackUs = gMeRenderScWritebackUs;
    completion->scOutputPrepareUs = gMeRenderScOutputPrepareUs;
    completion->scSubmitUs = gMeRenderScSubmitUs;
    completion->dispatchWaitUs =
        sceKernelGetSystemTimeLow() - gMeRenderStartUs;
    completion->meInvalidateCycles = box->renderInvalidateCycles;
    completion->meKernelCycles = box->renderKernelCycles;
    completion->meWritebackCycles = box->renderWritebackCycles;
    completion->meFcr31Before = box->renderFcr31Before;
    completion->meFcr31Effective = box->renderFcr31Effective;
    completion->meFcr31After = box->renderFcr31After;
}

int th07_psp_me_render_begin(const Th07PspMeRenderJob *job)
{
    if (!job || !job->input || !job->output ||
        (job->flags & ~TH07_PSP_ME_RENDER_JOB_COLD_CACHE) != 0u ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        return 0;

    const uint32_t inputPhys = (uint32_t)job->input & 0x1fffffffu;
    const uint32_t outputPhys = (uint32_t)job->output & 0x1fffffffu;
    uint32_t requiredInput = 0u;
    uint32_t requiredOutput = 0u;
    if (!me_render_bounds_valid(job->version, inputPhys, job->inputBytes,
                                job->inputStride, job->recordCount,
                                outputPhys, job->outputBytes,
                                &requiredInput, &requiredOutput))
    {
        __atomic_fetch_add(&gMeRenderBenchSummary.boundsFaults, 1u,
                           __ATOMIC_RELAXED);
        return 0;
    }

    unsigned int expectedFlight = 0u;
    if (!__atomic_compare_exchange_n(&gMeRenderInFlight, &expectedFlight, 1u,
                                     0, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
        return 0;
    if (!claim_me_for_render())
    {
        __atomic_store_n(&gMeRenderInFlight, 0u, __ATOMIC_RELEASE);
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (!box || box->command != ME_CMD_NONE || box->status != ME_STAT_DONE ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        box->workerState != ME_WORKER_READY || box->suspendRequested != 0u ||
        box->stackFault)
    {
        release_me();
        __atomic_store_n(&gMeRenderInFlight, 0u, __ATOMIC_RELEASE);
        __atomic_fetch_add(&gMeRenderBenchSummary.protocolFaults, 1u,
                           __ATOMIC_RELAXED);
        return 0;
    }

    const uint32_t writebackStart = sceKernelGetSystemTimeLow();
    if (requiredInput)
        sceKernelDcacheWritebackRange((void *)job->input, requiredInput);
    gMeRenderScWritebackUs = sceKernelGetSystemTimeLow() - writebackStart;
    const uint32_t outputPrepareStart = sceKernelGetSystemTimeLow();
    if (requiredOutput)
        sceKernelDcacheWritebackInvalidateRange(job->output, requiredOutput);
    gMeRenderScOutputPrepareUs =
        sceKernelGetSystemTimeLow() - outputPrepareStart;

    const uint32_t submitStart = sceKernelGetSystemTimeLow();
    box->renderVersion = job->version;
    box->renderFlags = job->flags;
    box->renderFrameSeq = job->frameSeq;
    box->renderTargetDrawSeq = job->targetDrawSeq;
    box->renderStageEpoch = job->stageEpoch;
    box->renderManagerEpoch = job->managerEpoch;
    box->renderReplayEpoch = job->replayEpoch;
    box->renderInputPhys = inputPhys;
    box->renderInputBytes = job->inputBytes;
    box->renderInputStride = job->inputStride;
    box->renderRecordCount = job->recordCount;
    box->renderOutputPhys = outputPhys;
    box->renderOutputCapacity = job->outputBytes;
    box->renderOutputBytes = 0u;
    box->renderResult = ME_RENDER_RESULT_PROTOCOL;
    box->renderInvalidateCycles = 0u;
    box->renderKernelCycles = 0u;
    box->renderWritebackCycles = 0u;
    box->renderFcr31Before = 0u;
    box->renderFcr31Effective = 0u;
    box->renderFcr31After = 0u;
    box->status = ME_STAT_IDLE;
    gMeRenderPublishedJob = *job;
    __asm__ volatile("sync");
    gMeRenderStartUs = submitStart;
    box->command = ME_CMD_RENDER_EXPAND;
    __asm__ volatile("sync");
    gMeRenderScSubmitUs = sceKernelGetSystemTimeLow() - submitStart;
    __atomic_fetch_add(&gMeRenderSubmitted, 1u, __ATOMIC_RELAXED);
    return 1;
}

int th07_psp_me_render_probe(Th07PspMeRenderCompletion *completion)
{
    if (!__atomic_load_n(&gMeRenderInFlight, __ATOMIC_ACQUIRE) ||
        !gMeMailboxUncached)
        return -1;
    if (__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        return -1;
    if (gMeMailboxUncached->status != ME_STAT_DONE)
        return 0;
    __asm__ volatile("sync");
    me_render_fill_completion(completion);
    return 1;
}

int th07_psp_me_render_retire(Th07PspMeRenderCompletion *completion)
{
    Th07PspMeRenderCompletion local;
    const int probe = th07_psp_me_render_probe(&local);
    if (probe <= 0)
        return probe;

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    const uint32_t expectedOutput =
        gMeRenderPublishedJob.recordCount *
        TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
    if (box->command != ME_CMD_NONE || local.result != ME_RENDER_RESULT_OK ||
        local.version != gMeRenderPublishedJob.version ||
        local.flags != gMeRenderPublishedJob.flags ||
        local.frameSeq != gMeRenderPublishedJob.frameSeq ||
        local.targetDrawSeq != gMeRenderPublishedJob.targetDrawSeq ||
        local.stageEpoch != gMeRenderPublishedJob.stageEpoch ||
        local.managerEpoch != gMeRenderPublishedJob.managerEpoch ||
        local.replayEpoch != gMeRenderPublishedJob.replayEpoch ||
        local.recordCount != gMeRenderPublishedJob.recordCount ||
        local.inputStride != gMeRenderPublishedJob.inputStride ||
        local.outputBytes != expectedOutput ||
        local.meFcr31Effective != 0u ||
        local.meFcr31Before != local.meFcr31After || box->stackFault)
    {
        __atomic_fetch_add(&gMeRenderBenchSummary.protocolFaults, 1u,
                           __ATOMIC_RELAXED);
        poison_me();
        return -1;
    }

    const uint32_t invalidateStart = sceKernelGetSystemTimeLow();
    if (expectedOutput)
        sceKernelDcacheInvalidateRange(gMeRenderPublishedJob.output,
                                      expectedOutput);
    local.scInvalidateUs = sceKernelGetSystemTimeLow() - invalidateStart;

    if (!stack_guards_match_on_sc())
    {
        box->stackFault = 1u;
        __atomic_fetch_add(&gMeRenderBenchSummary.guardFaults, 1u,
                           __ATOMIC_RELAXED);
        poison_me();
        return -1;
    }
    if (completion)
        *completion = local;
    release_me();
    __atomic_store_n(&gMeRenderInFlight, 0u, __ATOMIC_RELEASE);
    __atomic_fetch_add(&gMeRenderCompleted, 1u, __ATOMIC_RELAXED);
    return 1;
}

void th07_psp_me_render_hard_fault(void)
{
    if (!__atomic_load_n(&gMeRenderInFlight, __ATOMIC_ACQUIRE))
        return;
    __atomic_fetch_add(&gMeRenderBenchSummary.timeouts, 1u,
                       __ATOMIC_RELAXED);
    poison_me();
    // Do not release the owner or its buffers while a hung ME may still write.
    // Shutdown will fail closed and require the existing cold-reboot path.
}

void th07_psp_me_render_diag_snapshot(unsigned int *meRenderSubmitted,
                                      unsigned int *meRenderCompleted)
{
    if (meRenderSubmitted)
        *meRenderSubmitted =
            __atomic_load_n(&gMeRenderSubmitted, __ATOMIC_ACQUIRE);
    if (meRenderCompleted)
        *meRenderCompleted =
            __atomic_load_n(&gMeRenderCompleted, __ATOMIC_ACQUIRE);
}

void *th07_psp_me_render_runtime_input(void)
{
    return gMeRenderRuntimeInput;
}

void *th07_psp_me_render_runtime_output(void)
{
    return gMeRenderRuntimeOutput;
}

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
static int me_render_stream_guard_bytes_match(const unsigned char *bytes)
{
    for (uint32_t index = 0u; index < ME_RENDER_STREAM_GUARD_BYTES; ++index)
    {
        if (bytes[index] != ME_RENDER_STREAM_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int me_render_stream_guards_match_on_sc(uint32_t slot)
{
    if (slot >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT)
        return 0;
    MeRenderStreamInputArea *input = &gMeRenderStreamInputAreas[slot];
    MeRenderStreamOutputArea *output = &gMeRenderStreamOutputAreas[slot];
    MeRenderStreamRunArea *runs = &gMeRenderStreamRunAreas[slot];
    sceKernelDcacheInvalidateRange(input->guard0, sizeof(input->guard0));
    sceKernelDcacheInvalidateRange(input->guard1, sizeof(input->guard1));
    sceKernelDcacheInvalidateRange(output->guard0, sizeof(output->guard0));
    sceKernelDcacheInvalidateRange(output->guard1, sizeof(output->guard1));
    sceKernelDcacheInvalidateRange(runs->guard0, sizeof(runs->guard0));
    sceKernelDcacheInvalidateRange(runs->guard1, sizeof(runs->guard1));
    return me_render_stream_guard_bytes_match(input->guard0) &&
           me_render_stream_guard_bytes_match(input->guard1) &&
           me_render_stream_guard_bytes_match(output->guard0) &&
           me_render_stream_guard_bytes_match(output->guard1) &&
           me_render_stream_guard_bytes_match(runs->guard0) &&
           me_render_stream_guard_bytes_match(runs->guard1);
}

#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
static uint32_t me_render_stream_guard_fault_bytes(
    const unsigned char *bytes, uint32_t faultBit, uint32_t region,
    uint32_t *firstDetail, uint32_t *firstActual)
{
    for (uint32_t index = 0u; index < ME_RENDER_STREAM_GUARD_BYTES; ++index)
    {
        if (bytes[index] == ME_RENDER_STREAM_GUARD_PATTERN)
            continue;
        if (*firstDetail == 0xffffffffu)
        {
            // High 16 bits identify input0/input1/output0/output1/run0/run1;
            // low 16 bits identify the first damaged byte in that guard.
            *firstDetail = (region << 16) | index;
            *firstActual = bytes[index];
        }
        return faultBit;
    }
    return 0u;
}

static uint32_t me_render_stream_guard_fault_mask_on_sc(
    uint32_t slot, uint32_t *firstDetail, uint32_t *firstActual)
{
    if (firstDetail)
        *firstDetail = 0xffffffffu;
    if (firstActual)
        *firstActual = 0u;
    if (slot >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT || !firstDetail ||
        !firstActual)
        return TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_INPUT |
               TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_OUTPUT |
               TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_RUN;

    MeRenderStreamInputArea *input = &gMeRenderStreamInputAreas[slot];
    MeRenderStreamOutputArea *output = &gMeRenderStreamOutputAreas[slot];
    MeRenderStreamRunArea *runs = &gMeRenderStreamRunAreas[slot];
    sceKernelDcacheInvalidateRange(input->guard0, sizeof(input->guard0));
    sceKernelDcacheInvalidateRange(input->guard1, sizeof(input->guard1));
    sceKernelDcacheInvalidateRange(output->guard0, sizeof(output->guard0));
    sceKernelDcacheInvalidateRange(output->guard1, sizeof(output->guard1));
    sceKernelDcacheInvalidateRange(runs->guard0, sizeof(runs->guard0));
    sceKernelDcacheInvalidateRange(runs->guard1, sizeof(runs->guard1));

    uint32_t mask = 0u;
    mask |= me_render_stream_guard_fault_bytes(
        input->guard0, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_INPUT, 0u,
        firstDetail, firstActual);
    mask |= me_render_stream_guard_fault_bytes(
        input->guard1, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_INPUT, 1u,
        firstDetail, firstActual);
    mask |= me_render_stream_guard_fault_bytes(
        output->guard0, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_OUTPUT, 2u,
        firstDetail, firstActual);
    mask |= me_render_stream_guard_fault_bytes(
        output->guard1, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_OUTPUT, 3u,
        firstDetail, firstActual);
    mask |= me_render_stream_guard_fault_bytes(
        runs->guard0, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_RUN, 4u,
        firstDetail, firstActual);
    mask |= me_render_stream_guard_fault_bytes(
        runs->guard1, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_RUN, 5u,
        firstDetail, firstActual);
    return mask;
}

static void me_render_stream_note_retire_fault(
    Th07PspMeRenderStreamCompletion *completion, uint32_t faultBit,
    uint32_t detail, uint32_t expected, uint32_t actual)
{
    if (completion->retireFaultMask == 0u)
    {
        completion->retireFaultDetail = detail;
        completion->retireFaultExpected = expected;
        completion->retireFaultActual = actual;
    }
    completion->retireFaultMask |= faultBit;
}
#endif

static int me_render_stream_token_matches(
    const Th07PspMeRenderStreamToken *token, uint32_t wantedState)
{
    if (!token || token->slot >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT ||
        token->generation == 0u)
        return 0;
    MeRenderStreamSlotControl *slot = &gMeRenderStreamSlots[token->slot];
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    return __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == wantedState &&
           __atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE) ==
               token->generation;
#else
    return __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == wantedState &&
           slot->generation == token->generation;
#endif
}

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
// Lock a stable state without ever publishing FREE before the token generation
// has been revalidated.  The second generation check closes the state ABA
// window: if a stale caller races an entire later slot lifetime, that lifetime
// is quarantined rather than being exposed to GE or recycled under the stale
// token.  Normal wrong-state/stale-token calls leave the current owner intact.
static MeRenderStreamSlotControl *me_render_stream_begin_sc_transition(
    const Th07PspMeRenderStreamToken *token, uint32_t wantedState)
{
    if (!token)
        return 0;
    const uint32_t slotIndex = token->slot;
    const uint32_t generation = token->generation;
    if (slotIndex >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT || generation == 0u)
        return 0;
    MeRenderStreamSlotControl *slot = &gMeRenderStreamSlots[slotIndex];
    if (__atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE) !=
        generation)
        return 0;

    unsigned int expectedState = wantedState;
    if (!__atomic_compare_exchange_n(
            &slot->state, &expectedState,
            TH07_PSP_ME_RENDER_STREAM_STATE_SC_TRANSITION, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
    if (__atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE) !=
        generation)
    {
        __atomic_store_n(&slot->state,
                         TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED,
                         __ATOMIC_RELEASE);
        return 0;
    }
    return slot;
}

static void me_render_stream_finish_sc_transition(
    MeRenderStreamSlotControl *slot, uint32_t nextState)
{
    __atomic_store_n(&slot->state, nextState, __ATOMIC_RELEASE);
}
#endif

unsigned int th07_psp_me_render_stream_hash(const void *data,
                                             unsigned int bytes)
{
    if (!data && bytes)
        return 0u;
    return me_render_stream_hash_bytes(data, bytes);
}

int th07_psp_me_render_stream_acquire(Th07PspMeRenderStreamBuild *build)
{
    if (!build || !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE)
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        || __atomic_load_n(&gMeRenderStreamDraining, __ATOMIC_ACQUIRE)
#endif
    )
        return 0;
    for (uint32_t slotIndex = 0u;
         slotIndex < TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT; ++slotIndex)
    {
        MeRenderStreamSlotControl *slot = &gMeRenderStreamSlots[slotIndex];
        unsigned int expected = TH07_PSP_ME_RENDER_STREAM_STATE_FREE;
        if (!__atomic_compare_exchange_n(
                &slot->state, &expected,
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
                TH07_PSP_ME_RENDER_STREAM_STATE_SC_TRANSITION, 0,
#else
                TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD, 0,
#endif
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        // Reserve FREE before changing generation.  Publishing SC_BUILD first
        // would let a stale token from the preceding lifetime cancel the new
        // acquisition during the old-generation/new-generation handoff.
        const uint32_t priorGeneration =
            __atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE);
        if (priorGeneration == UINT32_MAX)
        {
            // Never wrap back onto a process-lifetime token value.  A cold
            // reboot is the only safe way to reset an exhausted generation.
            me_render_stream_finish_sc_transition(
                slot, TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED);
            continue;
        }
        uint32_t generation = priorGeneration + 1u;
#else
        uint32_t generation = slot->generation + 1u;
#endif
        if (generation == 0u)
            generation = 1u;
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        __atomic_store_n(&slot->generation, generation, __ATOMIC_RELEASE);
#else
        slot->generation = generation;
#endif
        memset(&slot->publishedJob, 0, sizeof(slot->publishedJob));
        memset(&slot->completion, 0, sizeof(slot->completion));
        build->token.slot = slotIndex;
        build->token.generation = generation;
        build->records = gMeRenderStreamInputAreas[slotIndex].records;
        build->recordCapacity = TH07_PSP_ME_RENDER_STREAM_MAX_RECORDS;
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        me_render_stream_finish_sc_transition(
            slot, TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD);
#endif
        return 1;
    }
    return 0;
}

int th07_psp_me_render_stream_cancel_build(
    const Th07PspMeRenderStreamToken *token)
{
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    MeRenderStreamSlotControl *slot = me_render_stream_begin_sc_transition(
        token, TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD);
    if (!slot)
        return 0;
    me_render_stream_finish_sc_transition(
        slot, TH07_PSP_ME_RENDER_STREAM_STATE_FREE);
    return 1;
#else
    if (!me_render_stream_token_matches(
            token, TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD))
        return 0;
    __atomic_store_n(&gMeRenderStreamSlots[token->slot].state,
                     TH07_PSP_ME_RENDER_STREAM_STATE_FREE,
                     __ATOMIC_RELEASE);
    return 1;
#endif
}

static void me_render_stream_fill_completion(
    Th07PspMeRenderStreamCompletion *completion)
{
    if (!completion)
        return;
    volatile MeSharedMailbox *box = gMeMailboxUncached;
    memset(completion, 0, sizeof(*completion));
    completion->token.slot = box->renderStreamSlot;
    completion->token.generation = box->renderStreamGeneration;
    completion->version = box->renderStreamVersion;
    completion->flags = box->renderStreamFlags;
    completion->frameSeq = box->renderStreamFrameSeq;
    completion->targetDrawSeq = box->renderStreamTargetDrawSeq;
    completion->stageEpoch = box->renderStreamStageEpoch;
    completion->managerEpoch = box->renderStreamManagerEpoch;
    completion->replayEpoch = box->renderStreamReplayEpoch;
    completion->globalSignature = box->renderStreamGlobalSignature;
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        completion->bucketEnds[bucket] = box->renderStreamBucketEnds[bucket];
    completion->recordCount = box->renderStreamRecordCount;
    completion->payloadHash = box->renderStreamPayloadHash;
    completion->outputBytes = box->renderStreamOutputBytes;
    completion->vertexCount = box->renderStreamVertexCount;
    completion->runCount = box->renderStreamRunCount;
    completion->outputHash = box->renderStreamOutputHash;
    completion->runHash = box->renderStreamRunHash;
    completion->firstBadRecord = box->renderStreamFirstBadRecord;
    completion->result = box->renderStreamResult;
    completion->scWritebackUs = gMeRenderStreamScWritebackUs;
    completion->scOutputPrepareUs = gMeRenderStreamScOutputPrepareUs;
    completion->scSubmitUs = gMeRenderStreamScSubmitUs;
    completion->dispatchWaitUs =
        sceKernelGetSystemTimeLow() - gMeRenderStreamStartUs;
    completion->meInvalidateCycles = box->renderStreamInvalidateCycles;
    completion->meKernelCycles = box->renderStreamKernelCycles;
    completion->meWritebackCycles = box->renderStreamWritebackCycles;
    completion->meFcr31Before = box->renderStreamFcr31Before;
    completion->meFcr31Effective = box->renderStreamFcr31Effective;
    completion->meFcr31After = box->renderStreamFcr31After;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    completion->itemResult = box->renderStreamItemResult;
    completion->itemRecordCount = box->renderStreamItemRecordCount;
    completion->itemVertexCount = box->renderStreamItemVertexCount;
    completion->itemRunCount = box->renderStreamItemRunCount;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    completion->effectResult = box->renderStreamEffectResult;
    completion->effectLayer0RecordCount =
        box->renderStreamEffectLayer0RecordCount;
    completion->effectLayer0VertexCount =
        box->renderStreamEffectLayer0VertexCount;
    completion->effectLayer0RunCount =
        box->renderStreamEffectLayer0RunCount;
    completion->effectLayer3RecordCount =
        box->renderStreamEffectLayer3RecordCount;
    completion->effectLayer3VertexCount =
        box->renderStreamEffectLayer3VertexCount;
    completion->effectLayer3RunCount =
        box->renderStreamEffectLayer3RunCount;
#endif
#endif
}

int th07_psp_me_render_stream_submit(const Th07PspMeRenderStreamJob *job)
{
    if (!job || !me_render_stream_token_matches(
                    &job->token, TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD) ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE) ||
        job->frameSeq == 0u || job->targetDrawSeq == 0u
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        || __atomic_load_n(&gMeRenderStreamDraining, __ATOMIC_ACQUIRE)
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        || (((job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u) &&
            !__atomic_load_n(&gMeItemRenderEnabled, __ATOMIC_ACQUIRE))
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        || (((job->flags &
              TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED) != 0u) &&
            !__atomic_load_n(&gMeItemMotionEnabled, __ATOMIC_ACQUIRE) &&
            !gMeItemMotionSelftestInProgress)
#endif
#endif
    )
        return 0;

    const uint32_t slotIndex = job->token.slot;
    MeRenderStreamInputArea *inputArea =
        &gMeRenderStreamInputAreas[slotIndex];
    MeRenderStreamOutputArea *outputArea =
        &gMeRenderStreamOutputAreas[slotIndex];
    MeRenderStreamRunArea *runArea = &gMeRenderStreamRunAreas[slotIndex];
    const uint32_t inputPhys =
        (uint32_t)inputArea->records & 0x1fffffffu;
    const uint32_t outputPhys =
        (uint32_t)outputArea->vertices & 0x1fffffffu;
    const uint32_t runPhys = (uint32_t)runArea->runs & 0x1fffffffu;
    uint32_t requiredInput = 0u;
    uint32_t bucketEnds[6];
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        bucketEnds[bucket] = job->bucketEnds[bucket];
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    const uint32_t rawLive =
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE) != 0u;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    const uint32_t directList =
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST) != 0u;
#endif
#endif
    if (!me_render_stream_bounds_valid(
            job->version, job->flags, job->token.slot,
            job->token.generation, bucketEnds, job->recordCount,
            job->offsetXBits, job->offsetYBits,
            job->viewportLeftBits, job->viewportTopBits,
            job->viewportRightBits, job->viewportBottomBits,
            job->configFlags,
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
            &job->rawLayout,
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
            &job->listLayout,
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            &job->itemLayout,
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            &job->effectLayout,
#endif
#endif
#endif
#endif
            inputPhys, sizeof(inputArea->records),
            outputPhys, sizeof(outputArea->vertices),
            runPhys, sizeof(runArea->runs), &requiredInput))
        return 0;
    for (uint32_t record = 0u; record < job->recordCount; ++record)
    {
        int valid = 0;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        if (directList)
            continue;
#endif
        if (rawLive)
        {
            const Th07PspMeRenderRawRecord *rawRecords =
                (const Th07PspMeRenderRawRecord *)inputArea->records;
            valid = me_render_stream_raw_record_valid(
                &rawRecords[record], &job->rawLayout);
        }
        else
#endif
        {
            valid = me_render_stream_record_valid(&inputArea->records[record]);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
            if (valid)
                valid = me_render_stream_axis_floor_inputs_valid(
                    &inputArea->records[record], job->offsetXBits,
                    job->offsetYBits);
#endif
        }
        if (!valid)
            return 0;
    }
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    uint32_t payloadHash = 0u;
    if ((job->flags &
         TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH) != 0u)
    {
        payloadHash =
            me_render_stream_hash_bytes(inputArea->records, requiredInput);
        if (job->payloadHash != payloadHash)
            return 0;
    }
#else
    const uint32_t payloadHash =
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        directList ? 0u :
#endif
        me_render_stream_hash_bytes(inputArea->records, requiredInput);
    if (job->payloadHash != payloadHash)
        return 0;
#endif

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // Freeze the build lifetime before claiming the shared ME/mailbox.  A
    // concurrent cancel or shutdown callback must either precede this CAS or
    // observe SC_TRANSITION and fail closed; it may not recycle input while
    // submit is handing those bytes to ME.
    MeRenderStreamSlotControl *transitionSlot =
        me_render_stream_begin_sc_transition(
            &job->token, TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD);
    if (!transitionSlot)
        return 0;
#endif

    unsigned int expectedSlot = 0xffffffffu;
    if (!__atomic_compare_exchange_n(
            &gMeRenderStreamInFlightSlot, &expectedSlot, slotIndex, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    {
        me_render_stream_finish_sc_transition(
            transitionSlot, TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD);
        return 0;
    }
#else
        return 0;
#endif
    if (!claim_me_for_render())
    {
        __atomic_store_n(&gMeRenderStreamInFlightSlot, 0xffffffffu,
                         __ATOMIC_RELEASE);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        me_render_stream_finish_sc_transition(
            transitionSlot, TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD);
#endif
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (!box || box->command != ME_CMD_NONE || box->status != ME_STAT_DONE ||
        box->workerState != ME_WORKER_READY || box->suspendRequested != 0u ||
        box->stackFault ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE))
    {
        release_me();
        __atomic_store_n(&gMeRenderStreamInFlightSlot, 0xffffffffu,
                         __ATOMIC_RELEASE);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        me_render_stream_finish_sc_transition(
            transitionSlot, TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD);
#endif
        return 0;
    }

    const uint32_t writebackStart = sceKernelGetSystemTimeLow();
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    if (rawLive)
        sceKernelDcacheWritebackAll();
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    else if (directList)
        sceKernelDcacheWritebackAll();
#endif
    else
#endif
    if (requiredInput)
        sceKernelDcacheWritebackRange(inputArea->records, requiredInput);
    gMeRenderStreamScWritebackUs =
        sceKernelGetSystemTimeLow() - writebackStart;
    const uint32_t outputPrepareStart = sceKernelGetSystemTimeLow();
    // Do not elide these full-pool fences.  I-ME8R was the only hardware
    // profile to do so and stopped during the command-10 direct-list boot
    // selftest.  The I-ME7 contract below is intentionally conservative:
    // publish any SC alias before ME takes write ownership, then invalidate
    // it so the later extent-only retire cannot inherit a stale line.
    sceKernelDcacheWritebackInvalidateRange(outputArea->vertices,
                                             sizeof(outputArea->vertices));
    sceKernelDcacheWritebackInvalidateRange(runArea->runs,
                                             sizeof(runArea->runs));
    gMeRenderStreamScOutputPrepareUs =
        sceKernelGetSystemTimeLow() - outputPrepareStart;

    const uint32_t submitStart = sceKernelGetSystemTimeLow();
    box->renderStreamVersion = job->version;
    box->renderStreamFlags = job->flags;
    box->renderStreamSlot = job->token.slot;
    box->renderStreamGeneration = job->token.generation;
    box->renderStreamFrameSeq = job->frameSeq;
    box->renderStreamTargetDrawSeq = job->targetDrawSeq;
    box->renderStreamStageEpoch = job->stageEpoch;
    box->renderStreamManagerEpoch = job->managerEpoch;
    box->renderStreamReplayEpoch = job->replayEpoch;
    box->renderStreamGlobalSignature = job->globalSignature;
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        box->renderStreamBucketEnds[bucket] = job->bucketEnds[bucket];
    box->renderStreamRecordCount = job->recordCount;
    box->renderStreamPayloadHash = payloadHash;
    box->renderStreamOffsetXBits = job->offsetXBits;
    box->renderStreamOffsetYBits = job->offsetYBits;
    box->renderStreamViewportLeftBits = job->viewportLeftBits;
    box->renderStreamViewportTopBits = job->viewportTopBits;
    box->renderStreamViewportRightBits = job->viewportRightBits;
    box->renderStreamViewportBottomBits = job->viewportBottomBits;
    box->renderStreamGlobalColor = job->globalColor;
    box->renderStreamConfigFlags = job->configFlags;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    box->renderStreamRawLayout = job->rawLayout;
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    box->renderStreamListLayout = job->listLayout;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    box->renderStreamItemLayout = job->itemLayout;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    box->renderStreamEffectLayout = job->effectLayout;
#endif
#endif
#endif
#endif
    box->renderStreamInputPhys = inputPhys;
    box->renderStreamInputCapacity = sizeof(inputArea->records);
    box->renderStreamOutputPhys = outputPhys;
    box->renderStreamOutputCapacity = sizeof(outputArea->vertices);
    box->renderStreamRunPhys = runPhys;
    box->renderStreamRunCapacity = sizeof(runArea->runs);
    box->renderStreamOutputBytes = 0u;
    box->renderStreamVertexCount = 0u;
    box->renderStreamRunCount = 0u;
    box->renderStreamOutputHash = 0u;
    box->renderStreamRunHash = 0u;
    box->renderStreamFirstBadRecord = 0xffffffffu;
    box->renderStreamResult = TH07_PSP_ME_RENDER_STREAM_RESULT_PROTOCOL;
    box->renderStreamInvalidateCycles = 0u;
    box->renderStreamKernelCycles = 0u;
    box->renderStreamWritebackCycles = 0u;
    box->renderStreamFcr31Before = 0u;
    box->renderStreamFcr31Effective = 0u;
    box->renderStreamFcr31After = 0u;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    box->renderStreamItemResult = TH07_PSP_ME_RENDER_STREAM_RESULT_OK;
    box->renderStreamItemRecordCount = 0u;
    box->renderStreamItemVertexCount = 0u;
    box->renderStreamItemRunCount = 0u;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    box->renderStreamEffectResult = TH07_PSP_ME_RENDER_STREAM_RESULT_OK;
    box->renderStreamEffectLayer0RecordCount = 0u;
    box->renderStreamEffectLayer0VertexCount = 0u;
    box->renderStreamEffectLayer0RunCount = 0u;
    box->renderStreamEffectLayer3RecordCount = 0u;
    box->renderStreamEffectLayer3VertexCount = 0u;
    box->renderStreamEffectLayer3RunCount = 0u;
#endif
#endif
    box->status = ME_STAT_IDLE;

    gMeRenderStreamSlots[slotIndex].publishedJob = *job;
    gMeRenderStreamSlots[slotIndex].publishedJob.payloadHash = payloadHash;
#if !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    __atomic_store_n(&gMeRenderStreamSlots[slotIndex].state,
                     TH07_PSP_ME_RENDER_STREAM_STATE_ME_RUNNING,
                     __ATOMIC_RELEASE);
#endif
    __asm__ volatile("sync");
    gMeRenderStreamStartUs = submitStart;
    box->command = ME_CMD_RENDER_STREAM;
    __asm__ volatile("sync");
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // Once command is visible the input is ME-owned.  Publish ME_RUNNING only
    // after that handoff; a shutdown that catches the preceding transition
    // refuses reclamation rather than probing stale DONE mailbox contents.
    me_render_stream_finish_sc_transition(
        transitionSlot, TH07_PSP_ME_RENDER_STREAM_STATE_ME_RUNNING);
#endif
    gMeRenderStreamScSubmitUs = sceKernelGetSystemTimeLow() - submitStart;
    __atomic_fetch_add(&gMeRenderStreamSubmitted, 1u, __ATOMIC_RELAXED);
    return 1;
}

int th07_psp_me_render_stream_probe(
    const Th07PspMeRenderStreamToken *token,
    Th07PspMeRenderStreamCompletion *completion)
{
    if (!me_render_stream_token_matches(
            token, TH07_PSP_ME_RENDER_STREAM_STATE_ME_RUNNING) ||
        __atomic_load_n(&gMeRenderStreamInFlightSlot, __ATOMIC_ACQUIRE) !=
            token->slot || !gMeMailboxUncached ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        return -1;
    if (gMeMailboxUncached->status != ME_STAT_DONE)
        return 0;
    __asm__ volatile("sync");
    me_render_stream_fill_completion(completion);
    return 1;
}

static int me_render_stream_completion_echo_matches(
    const Th07PspMeRenderStreamCompletion *completion,
    const Th07PspMeRenderStreamJob *job)
{
    if (completion->token.slot != job->token.slot ||
        completion->token.generation != job->token.generation ||
        completion->version != job->version ||
        completion->flags != job->flags ||
        completion->frameSeq != job->frameSeq ||
        completion->targetDrawSeq != job->targetDrawSeq ||
        completion->stageEpoch != job->stageEpoch ||
        completion->managerEpoch != job->managerEpoch ||
        completion->replayEpoch != job->replayEpoch ||
        completion->globalSignature != job->globalSignature ||
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
        completion->recordCount != job->recordCount ||
        (((job->flags &
              TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH) != 0u) &&
            completion->payloadHash != job->payloadHash)
#else
        completion->recordCount != job->recordCount ||
        completion->payloadHash != job->payloadHash)
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    )
#endif
        return 0;
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
    {
        if (completion->bucketEnds[bucket] != job->bucketEnds[bucket])
            return 0;
    }
    return 1;
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
static int me_render_stream_item_completion_valid(
    const Th07PspMeRenderStreamCompletion *completion,
    const Th07PspMeRenderStreamJob *job)
{
    const uint32_t hasItem =
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST) != 0u;
    if (!hasItem)
        return completion->itemResult ==
                   TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
               completion->itemRecordCount == 0u &&
               completion->itemVertexCount == 0u &&
               completion->itemRunCount == 0u;

    if (completion->itemResult >
            TH07_PSP_ME_RENDER_STREAM_RESULT_RUN_OVERFLOW ||
        completion->itemRecordCount !=
            job->itemLayout.expectedItemCount ||
        completion->itemRecordCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS ||
        completion->itemVertexCount > completion->vertexCount ||
        completion->itemVertexCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_VERTEX_BYTES /
                sizeof(Th07PspMeRenderStreamVertex) ||
        completion->itemRunCount > completion->runCount ||
        completion->itemRunCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RUNS)
        return 0;

    // Item is an optional prefix.  A semantic reject is legal only after the
    // worker has discarded every dirty prefix byte/run; Bullet remains the
    // top-level result and may still be consumed independently.
    return completion->itemResult ==
               TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
           (completion->itemVertexCount == 0u &&
            completion->itemRunCount == 0u);
}
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
static int me_render_stream_effect_completion_valid(
    const Th07PspMeRenderStreamCompletion *completion,
    const Th07PspMeRenderStreamJob *job)
{
    const uint32_t hasEffect =
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST) != 0u;
    if (!hasEffect)
        return completion->effectResult ==
                   TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
               completion->effectLayer0RecordCount == 0u &&
               completion->effectLayer0VertexCount == 0u &&
               completion->effectLayer0RunCount == 0u &&
               completion->effectLayer3RecordCount == 0u &&
               completion->effectLayer3VertexCount == 0u &&
               completion->effectLayer3RunCount == 0u;
    if (completion->effectResult >
            TH07_PSP_ME_RENDER_STREAM_RESULT_RUN_OVERFLOW ||
        completion->effectLayer0RecordCount !=
            job->effectLayout.expectedLayer0Count ||
        completion->effectLayer3RecordCount !=
            job->effectLayout.expectedLayer3Count ||
        completion->itemRecordCount +
                completion->effectLayer0RecordCount +
                completion->effectLayer3RecordCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS ||
        completion->itemVertexCount > completion->vertexCount ||
        completion->itemRunCount > completion->runCount ||
        completion->effectLayer0VertexCount +
                completion->effectLayer3VertexCount >
            completion->vertexCount - completion->itemVertexCount ||
        completion->effectLayer0RunCount +
                completion->effectLayer3RunCount >
            completion->runCount - completion->itemRunCount)
        return 0;
    return completion->effectResult ==
               TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
           (completion->effectLayer0VertexCount == 0u &&
            completion->effectLayer0RunCount == 0u &&
            completion->effectLayer3VertexCount == 0u &&
            completion->effectLayer3RunCount == 0u);
}
#endif
#endif

int th07_psp_me_render_stream_retire(
    const Th07PspMeRenderStreamToken *token,
    Th07PspMeRenderStreamCompletion *completion,
    Th07PspMeRenderStreamReady *ready)
{
    Th07PspMeRenderStreamCompletion local;
    const int probe = th07_psp_me_render_stream_probe(token, &local);
    if (probe <= 0)
        return probe;
    const uint32_t slotIndex = token->slot;
    MeRenderStreamSlotControl *slot = &gMeRenderStreamSlots[slotIndex];
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // Own the DONE retirement before inspecting/caching its output.  This
    // prevents a concurrent hard-fault/shutdown path from quarantining the
    // slot between probe and a later unconditional READY publication.
    MeRenderStreamSlotControl *transitionSlot =
        me_render_stream_begin_sc_transition(
            token, TH07_PSP_ME_RENDER_STREAM_STATE_ME_RUNNING);
    if (!transitionSlot)
        return -1;
#endif
    const Th07PspMeRenderStreamJob *job = &slot->publishedJob;
    const uint32_t runBytes =
        local.runCount * sizeof(Th07PspMeRenderStreamRun);
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE) || \
    defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    int softRecordReject =
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
        // A C1 conversion-domain miss is an expected per-frame rejection,
        // including for the semantic M0 input form.  Only a completely empty,
        // echoed and guard-clean RECORD completion is recyclable below.
        1 &&
#else
        (job->flags &
         (TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
          | TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST
#endif
         )) != 0u &&
#endif
        local.result == TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD &&
        gMeMailboxUncached->command == ME_CMD_NONE &&
        me_render_stream_completion_echo_matches(&local, job) &&
        local.recordCount != 0u &&
        local.firstBadRecord < local.recordCount &&
        local.outputBytes == 0u && local.vertexCount == 0u &&
        local.runCount == 0u && local.outputHash == 0u &&
        local.runHash == 0u && local.meFcr31Effective == 0u &&
        local.meFcr31Before == local.meFcr31After &&
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        local.itemVertexCount == 0u && local.itemRunCount == 0u &&
        me_render_stream_item_completion_valid(&local, job) &&
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        local.effectLayer0VertexCount == 0u &&
        local.effectLayer0RunCount == 0u &&
        local.effectLayer3VertexCount == 0u &&
        local.effectLayer3RunCount == 0u &&
        me_render_stream_effect_completion_valid(&local, job) &&
#endif
#endif
        !gMeMailboxUncached->stackFault;
#else
    int softRecordReject = 0;
#endif
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    local.retireFaultMask = 0u;
    local.retireFaultDetail = 0u;
    local.retireFaultExpected = 0u;
    local.retireFaultActual = 0u;

    if (gMeMailboxUncached->command != ME_CMD_NONE)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_COMMAND, 0u,
            ME_CMD_NONE, gMeMailboxUncached->command);
    if (local.result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_RESULT,
            local.firstBadRecord, TH07_PSP_ME_RENDER_STREAM_RESULT_OK,
            local.result);
    if (local.token.slot != job->token.slot)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_TOKEN, 0u,
            job->token.slot, local.token.slot);
    if (local.token.generation != job->token.generation)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_TOKEN, 1u,
            job->token.generation, local.token.generation);
    if (local.version != job->version)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_VERSION, 0u,
            job->version, local.version);
    if (local.flags != job->flags)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_FLAGS, 0u,
            job->flags, local.flags);
    if (local.frameSeq != job->frameSeq)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_FRAME, 0u,
            job->frameSeq, local.frameSeq);
    if (local.targetDrawSeq != job->targetDrawSeq)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_TARGET, 0u,
            job->targetDrawSeq, local.targetDrawSeq);
    if (local.stageEpoch != job->stageEpoch)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_STAGE, 0u,
            job->stageEpoch, local.stageEpoch);
    if (local.managerEpoch != job->managerEpoch)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_MANAGER, 0u,
            job->managerEpoch, local.managerEpoch);
    if (local.replayEpoch != job->replayEpoch)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_REPLAY, 0u,
            job->replayEpoch, local.replayEpoch);
    if (local.globalSignature != job->globalSignature)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_SIGNATURE, 0u,
            job->globalSignature, local.globalSignature);
    if (local.recordCount != job->recordCount)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_RECORD_COUNT, 0u,
            job->recordCount, local.recordCount);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    if ((job->flags &
         TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH) != 0u &&
        local.payloadHash != job->payloadHash)
#else
    if (local.payloadHash != job->payloadHash)
#endif
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_PAYLOAD_HASH, 0u,
            job->payloadHash, local.payloadHash);
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
    {
        if (local.bucketEnds[bucket] != job->bucketEnds[bucket])
            me_render_stream_note_retire_fault(
                &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_BUCKET, bucket,
                job->bucketEnds[bucket], local.bucketEnds[bucket]);
    }
    const uint32_t echoFaultBits =
        TH07_PSP_ME_RENDER_STREAM_RETIRE_TOKEN |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_VERSION |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_FLAGS |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_FRAME |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_TARGET |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_STAGE |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_MANAGER |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_REPLAY |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_SIGNATURE |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_RECORD_COUNT |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_PAYLOAD_HASH |
        TH07_PSP_ME_RENDER_STREAM_RETIRE_BUCKET;
    if (!me_render_stream_completion_echo_matches(&local, job) &&
        (local.retireFaultMask & echoFaultBits) == 0u)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_ECHO_OTHER, 0u, 1u,
            0u);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (!me_render_stream_item_completion_valid(&local, job))
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_ECHO_OTHER, 1u, 1u,
            0u);
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    if (!me_render_stream_effect_completion_valid(&local, job))
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_ECHO_OTHER, 2u, 1u,
            0u);
#endif
#endif

    const uint32_t vertexCapacity =
        ME_RENDER_STREAM_POOL_MAX_VERTEX_BYTES /
        sizeof(Th07PspMeRenderStreamVertex);
    if (local.vertexCount > vertexCapacity)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_VERTEX_COUNT, 0u,
            vertexCapacity, local.vertexCount);
    else
    {
        const uint32_t expectedBytes =
            local.vertexCount * sizeof(Th07PspMeRenderStreamVertex);
        if (local.outputBytes != expectedBytes)
            me_render_stream_note_retire_fault(
                &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_OUTPUT_BYTES, 0u,
                expectedBytes, local.outputBytes);
    }
    if (local.runCount > ME_RENDER_STREAM_POOL_MAX_RUNS)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_RUN_COUNT, 0u,
            ME_RENDER_STREAM_POOL_MAX_RUNS, local.runCount);
    if (local.meFcr31Effective != 0u)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_FCR_EFFECTIVE, 0u, 0u,
            local.meFcr31Effective);
    if (local.meFcr31Before != local.meFcr31After)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_FCR_RESTORE, 0u,
            local.meFcr31Before, local.meFcr31After);
    if (gMeMailboxUncached->stackFault)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_STACK, 0u, 0u,
            gMeMailboxUncached->stackFault);
    int valid = local.retireFaultMask == 0u;
#else
    int valid = gMeMailboxUncached->command == ME_CMD_NONE &&
                local.result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
                me_render_stream_completion_echo_matches(&local, job) &&
                local.vertexCount <=
                    ME_RENDER_STREAM_POOL_MAX_VERTEX_BYTES /
                        sizeof(Th07PspMeRenderStreamVertex) &&
                local.outputBytes ==
                    local.vertexCount * sizeof(Th07PspMeRenderStreamVertex) &&
                local.runCount <= ME_RENDER_STREAM_POOL_MAX_RUNS &&
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
                me_render_stream_item_completion_valid(&local, job) &&
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
                me_render_stream_effect_completion_valid(&local, job) &&
#endif
#endif
                local.meFcr31Effective == 0u &&
                local.meFcr31Before == local.meFcr31After &&
                !gMeMailboxUncached->stackFault;
#endif

    const uint32_t invalidateStart = sceKernelGetSystemTimeLow();
    if (local.outputBytes <=
        sizeof(gMeRenderStreamOutputAreas[slotIndex].vertices))
    {
        if (local.outputBytes)
            sceKernelDcacheInvalidateRange(
                gMeRenderStreamOutputAreas[slotIndex].vertices,
                local.outputBytes);
    }
    else
    {
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_OUTPUT_BOUNDS, 0u,
            sizeof(gMeRenderStreamOutputAreas[slotIndex].vertices),
            local.outputBytes);
#else
        valid = 0;
#endif
    }
    if (runBytes <= sizeof(gMeRenderStreamRunAreas[slotIndex].runs))
    {
        if (runBytes)
            sceKernelDcacheInvalidateRange(
                gMeRenderStreamRunAreas[slotIndex].runs, runBytes);
    }
    else
    {
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_RUN_BOUNDS, 0u,
            sizeof(gMeRenderStreamRunAreas[slotIndex].runs), runBytes);
#else
        valid = 0;
#endif
    }
    local.scInvalidateUs = sceKernelGetSystemTimeLow() - invalidateStart;
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    uint32_t guardDetail = 0xffffffffu;
    uint32_t guardActual = 0u;
    const uint32_t guardMask = me_render_stream_guard_fault_mask_on_sc(
        slotIndex, &guardDetail, &guardActual);
    // Mask bits retain every damaged pool.  The detail/actual snapshot names
    // only the first damaged guard byte, matching the Completion first-fault
    // contract even when more than one region bit is set below.
    if (guardMask & TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_INPUT)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_INPUT,
            guardDetail, ME_RENDER_STREAM_GUARD_PATTERN, guardActual);
    if (guardMask & TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_OUTPUT)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_OUTPUT,
            guardDetail, ME_RENDER_STREAM_GUARD_PATTERN, guardActual);
    if (guardMask & TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_RUN)
        me_render_stream_note_retire_fault(
            &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_GUARD_RUN, guardDetail,
            ME_RENDER_STREAM_GUARD_PATTERN, guardActual);

    if (local.result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT) != 0u &&
        local.outputBytes <=
            sizeof(gMeRenderStreamOutputAreas[slotIndex].vertices) &&
        local.runCount <= ME_RENDER_STREAM_POOL_MAX_RUNS)
    {
        const uint32_t outputHash = me_render_stream_hash_bytes(
            gMeRenderStreamOutputAreas[slotIndex].vertices,
            local.outputBytes);
        const uint32_t runHash = me_render_stream_hash_bytes(
            gMeRenderStreamRunAreas[slotIndex].runs, runBytes);
        if (local.outputHash != outputHash)
            me_render_stream_note_retire_fault(
                &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_OUTPUT_HASH, 0u,
                local.outputHash, outputHash);
        if (local.runHash != runHash)
            me_render_stream_note_retire_fault(
                &local, TH07_PSP_ME_RENDER_STREAM_RETIRE_RUN_HASH, 0u,
                local.runHash, runHash);
    }
    valid = local.retireFaultMask == 0u;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE) || \
    defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    softRecordReject = softRecordReject &&
        local.retireFaultMask == TH07_PSP_ME_RENDER_STREAM_RETIRE_RESULT;
#endif
#else
    const int guardsValid = me_render_stream_guards_match_on_sc(slotIndex);
    if (!guardsValid)
    {
        valid = 0;
        softRecordReject = 0;
    }
    if (valid &&
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT) != 0u)
    {
        valid = local.outputHash == me_render_stream_hash_bytes(
                                      gMeRenderStreamOutputAreas[slotIndex].vertices,
                                      local.outputBytes) &&
                local.runHash == me_render_stream_hash_bytes(
                                   gMeRenderStreamRunAreas[slotIndex].runs,
                                   runBytes);
    }
#endif

    const int recyclable = valid || softRecordReject;

#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    unsigned int expectedDiagLogged = 0u;
    if (!valid && __atomic_compare_exchange_n(
                      &gMeRenderStreamRetireDiagLogged,
                      &expectedDiagLogged, 1u, 0, __ATOMIC_ACQ_REL,
                      __ATOMIC_ACQUIRE))
        th07_psp_boot_notef(
            "ME11 RETIRE NG M%08x D%08x E%08x A%08x S%u G%u R%u "
            "Q%u I%u T%u C%u W%u U%u X%u",
            local.retireFaultMask, local.retireFaultDetail,
            local.retireFaultExpected, local.retireFaultActual,
            local.token.slot, local.token.generation, local.result,
            __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE),
            __atomic_load_n(&gMeRenderStreamInFlightSlot, __ATOMIC_ACQUIRE),
            gMeMailboxUncached->status, gMeMailboxUncached->command,
            gMeMailboxUncached->workerState,
            gMeMailboxUncached->suspendRequested,
            __atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE));
#endif

    slot->completion = local;
    if (completion)
        *completion = local;
    if (ready)
    {
        memset(ready, 0, sizeof(*ready));
        if (valid)
        {
            ready->token = *token;
            ready->vertices =
                gMeRenderStreamOutputAreas[slotIndex].vertices;
            ready->vertexBytes = local.outputBytes;
            ready->runs = gMeRenderStreamRunAreas[slotIndex].runs;
            ready->runCount = local.runCount;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            ready->itemResult = local.itemResult;
            ready->itemRecordCount = local.itemRecordCount;
            ready->itemVertexCount = local.itemVertexCount;
            ready->itemRunCount = local.itemRunCount;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            ready->effectResult = local.effectResult;
            ready->effectLayer0RecordCount =
                local.effectLayer0RecordCount;
            ready->effectLayer0VertexCount =
                local.effectLayer0VertexCount;
            ready->effectLayer0RunCount = local.effectLayer0RunCount;
            ready->effectLayer3RecordCount =
                local.effectLayer3RecordCount;
            ready->effectLayer3VertexCount =
                local.effectLayer3VertexCount;
            ready->effectLayer3RunCount = local.effectLayer3RunCount;
#endif
#endif
        }
    }
    release_me();
    __atomic_store_n(&gMeRenderStreamInFlightSlot, 0xffffffffu,
                     __ATOMIC_RELEASE);
    __atomic_fetch_add(&gMeRenderStreamCompleted, 1u, __ATOMIC_RELAXED);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    me_render_stream_finish_sc_transition(
        transitionSlot,
        recyclable ? TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC
                   : TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED);
#else
    __atomic_store_n(
        &slot->state,
        recyclable ? TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC
                   : TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED,
        __ATOMIC_RELEASE);
#endif
    return recyclable ? 1 : -1;
}

int th07_psp_me_render_stream_compare(
    const Th07PspMeRenderStreamToken *token,
    const void *expectedVertices, unsigned int expectedVertexBytes,
    const Th07PspMeRenderStreamRun *expectedRuns,
    unsigned int expectedRunCount,
    Th07PspMeRenderStreamMismatch *mismatch)
{
    if (mismatch)
        memset(mismatch, 0, sizeof(*mismatch));
    if (!me_render_stream_token_matches(
            token, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC) ||
        (!expectedVertices && expectedVertexBytes) ||
        (!expectedRuns && expectedRunCount))
        return -1;
    const uint32_t slotIndex = token->slot;
    const Th07PspMeRenderStreamCompletion *completion =
        &gMeRenderStreamSlots[slotIndex].completion;
    if (completion->result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
        return -1;
    if (expectedVertexBytes != completion->outputBytes ||
        expectedRunCount != completion->runCount)
    {
        if (mismatch)
        {
            mismatch->kind = TH07_PSP_ME_RENDER_STREAM_MISMATCH_SIZE;
            mismatch->expected = expectedVertexBytes;
            mismatch->actual = completion->outputBytes;
            if (expectedVertexBytes == completion->outputBytes)
            {
                mismatch->expected = expectedRunCount;
                mismatch->actual = completion->runCount;
            }
        }
        return 0;
    }
    const uint32_t *expectedVertexWords =
        (const uint32_t *)expectedVertices;
    const uint32_t *actualVertexWords = (const uint32_t *)
        gMeRenderStreamOutputAreas[slotIndex].vertices;
    for (uint32_t word = 0u; word < expectedVertexBytes / 4u; ++word)
    {
        if (expectedVertexWords[word] != actualVertexWords[word])
        {
            if (mismatch)
            {
                mismatch->kind = TH07_PSP_ME_RENDER_STREAM_MISMATCH_VERTEX;
                mismatch->wordIndex = word;
                mismatch->expected = expectedVertexWords[word];
                mismatch->actual = actualVertexWords[word];
            }
            return 0;
        }
    }
    const uint32_t *expectedRunWords = (const uint32_t *)expectedRuns;
    const uint32_t *actualRunWords =
        (const uint32_t *)gMeRenderStreamRunAreas[slotIndex].runs;
    const uint32_t runWords = expectedRunCount *
                              sizeof(Th07PspMeRenderStreamRun) / 4u;
    for (uint32_t word = 0u; word < runWords; ++word)
    {
        if (expectedRunWords[word] != actualRunWords[word])
        {
            if (mismatch)
            {
                mismatch->kind = TH07_PSP_ME_RENDER_STREAM_MISMATCH_RUN;
                mismatch->wordIndex = word;
                mismatch->expected = expectedRunWords[word];
                mismatch->actual = actualRunWords[word];
            }
            return 0;
        }
    }
    if ((completion->flags &
         TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT) != 0u &&
        (completion->outputHash !=
             me_render_stream_hash_bytes(actualVertexWords,
                                          expectedVertexBytes) ||
         completion->runHash !=
             me_render_stream_hash_bytes(actualRunWords, runWords * 4u)))
    {
        if (mismatch)
            mismatch->kind = TH07_PSP_ME_RENDER_STREAM_MISMATCH_HASH;
        return 0;
    }
    return 1;
}

int th07_psp_me_render_stream_release_ready(
    const Th07PspMeRenderStreamToken *token)
{
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    MeRenderStreamSlotControl *slot = me_render_stream_begin_sc_transition(
        token, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC);
    if (!slot)
        return 0;
    me_render_stream_finish_sc_transition(
        slot, TH07_PSP_ME_RENDER_STREAM_STATE_FREE);
    return 1;
#else
    if (!me_render_stream_token_matches(
            token, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC))
        return 0;
    __atomic_store_n(&gMeRenderStreamSlots[token->slot].state,
                     TH07_PSP_ME_RENDER_STREAM_STATE_FREE,
                     __ATOMIC_RELEASE);
    return 1;
#endif
}

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
int th07_psp_me_render_stream_ready_view_matches(
    const Th07PspMeRenderStreamToken *token,
    const Th07PspMeRenderStreamVertex *vertices,
    unsigned int vertexBytes,
    const Th07PspMeRenderStreamRun *runs,
    unsigned int runCount)
{
    if (!me_render_stream_token_matches(
            token, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC))
        return 0;

    const uint32_t slotIndex = token->slot;
    const Th07PspMeRenderStreamCompletion *completion =
        &gMeRenderStreamSlots[slotIndex].completion;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    const Th07PspMeRenderStreamJob *job =
        &gMeRenderStreamSlots[slotIndex].publishedJob;
#endif
    const uint32_t outputCapacity =
        sizeof(gMeRenderStreamOutputAreas[slotIndex].vertices);
    if (completion->token.slot != slotIndex ||
        completion->token.generation != token->generation ||
        completion->result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        completion->outputBytes > outputCapacity ||
        completion->vertexCount >
            outputCapacity / sizeof(Th07PspMeRenderStreamVertex) ||
        completion->outputBytes !=
            completion->vertexCount *
                sizeof(Th07PspMeRenderStreamVertex) ||
        completion->runCount > ME_RENDER_STREAM_POOL_MAX_RUNS
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        || !me_render_stream_item_completion_valid(completion, job)
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        || !me_render_stream_effect_completion_valid(completion, job)
#endif
#endif
        )
        return 0;

    // Exact virtual addresses are part of the ownership proof.  In-pool,
    // physically aliased or merely cache-line-aligned views are not accepted.
    return vertices == gMeRenderStreamOutputAreas[slotIndex].vertices &&
           vertexBytes == completion->outputBytes &&
           runs == gMeRenderStreamRunAreas[slotIndex].runs &&
           runCount == completion->runCount;
}
#endif

int th07_psp_me_render_stream_mark_ge_in_flight(
    const Th07PspMeRenderStreamToken *token)
{
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    MeRenderStreamSlotControl *slot = me_render_stream_begin_sc_transition(
        token, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC);
    if (!slot)
        return 0;

    const uint32_t slotIndex =
        (uint32_t)(slot - &gMeRenderStreamSlots[0]);
    const Th07PspMeRenderStreamCompletion *completion = &slot->completion;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    const Th07PspMeRenderStreamJob *job = &slot->publishedJob;
#endif
    const uint32_t outputCapacity =
        sizeof(gMeRenderStreamOutputAreas[slotIndex].vertices);
    if (completion->result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
    {
        // A validated RAW_LIVE RECORD reject is intentionally recyclable but
        // never GE-consumable.  Leave it releasable by the canonical fallback.
        me_render_stream_finish_sc_transition(
            slot, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC);
        return 0;
    }
    if (completion->outputBytes > outputCapacity ||
        completion->vertexCount >
            outputCapacity / sizeof(Th07PspMeRenderStreamVertex) ||
        completion->outputBytes !=
            completion->vertexCount *
                sizeof(Th07PspMeRenderStreamVertex) ||
        completion->runCount > ME_RENDER_STREAM_POOL_MAX_RUNS
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        || !me_render_stream_item_completion_valid(completion, job)
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
        || !me_render_stream_effect_completion_valid(completion, job)
#endif
#endif
        )
    {
        me_render_stream_finish_sc_transition(
            slot, TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED);
        return 0;
    }

    // ME wrote the range back before READY and retire invalidated it for SC
    // validation.  Re-publish/invalidate the subsequently cached SC lines
    // before GE ownership.  This is the hardware-proven I-ME7 fence and is
    // deliberately mandatory after the I-ME8R lean experiment stopped at
    // boot on a real PSP-3000.
    if (completion->outputBytes)
        sceKernelDcacheWritebackInvalidateRange(
            gMeRenderStreamOutputAreas[slotIndex].vertices,
            completion->outputBytes);
    __asm__ volatile("sync");
    me_render_stream_finish_sc_transition(
        slot, TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT);
    return 1;
#else
    // I-ME1 is shadow-only.  Keep the ABI symbol for the separately reviewed
    // I-ME3 fence increment, but make promotion impossible in this build.
    (void)token;
    return 0;
#endif
}

int th07_psp_me_render_stream_release_after_ge(
    const Th07PspMeRenderStreamToken *token)
{
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // The caller's completed GE list/fence is the authority for entering this
    // function.  Move through SC_TRANSITION so FREE cannot be observed by a
    // new acquire until both state and generation have been revalidated.
    MeRenderStreamSlotControl *slot = me_render_stream_begin_sc_transition(
        token, TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT);
    if (!slot)
        return 0;
    me_render_stream_finish_sc_transition(
        slot, TH07_PSP_ME_RENDER_STREAM_STATE_FREE);
    return 1;
#else
    (void)token;
    return 0;
#endif
}

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
int th07_psp_me_render_stream_abort_ge_mark(
    const Th07PspMeRenderStreamToken *token)
{
    // Caller authority: no GE command may have been enqueued for this token.
    // Low level can prove token/state ownership, but has no GU fence to prove
    // the absence of an enqueue.  Return to READY rather than FREE so normal
    // release_ready remains the single path that exposes the pool for reuse.
    MeRenderStreamSlotControl *slot = me_render_stream_begin_sc_transition(
        token, TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT);
    if (!slot)
        return 0;
    me_render_stream_finish_sc_transition(
        slot, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC);
    return 1;
}
#endif

void th07_psp_me_render_stream_hard_fault(
    const Th07PspMeRenderStreamToken *token)
{
    if (!me_render_stream_token_matches(
            token, TH07_PSP_ME_RENDER_STREAM_STATE_ME_RUNNING) ||
        __atomic_load_n(&gMeRenderStreamInFlightSlot, __ATOMIC_ACQUIRE) !=
            token->slot)
        return;
    __atomic_store_n(&gMeRenderStreamSlots[token->slot].state,
                     TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED,
                     __ATOMIC_RELEASE);
    poison_me();
    // Preserve ME_OWNER_RENDER and the in-flight slot.  A genuinely hung ME
    // may still write this process-lifetime pool; only cold boot reclaims it.
}

static int me_render_stream_drain_for_shutdown(void)
{
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // Shutdown clears gMeActive; the public live-authority barrier instead
    // raises gMeRenderStreamDraining.  Either gate prevents a new publish on
    // the SC thread.  First retire the one possible ME writer, then release
    // only states whose bytes are provably not referenced by ME or GE.  A
    // GE-owned, quarantined or interrupted-transition slot requires the
    // caller's fence path (or cold reboot); this helper never guesses that it
    // is reusable.
    const uint32_t inFlightSlot =
        __atomic_load_n(&gMeRenderStreamInFlightSlot, __ATOMIC_ACQUIRE);
    if (inFlightSlot != 0xffffffffu)
    {
        if (inFlightSlot >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT)
            return 0;
        Th07PspMeRenderStreamToken token;
        token.slot = inFlightSlot;
        token.generation = __atomic_load_n(
            &gMeRenderStreamSlots[inFlightSlot].generation,
            __ATOMIC_ACQUIRE);
        const uint32_t startUs = sceKernelGetSystemTimeLow();
        for (;;)
        {
            const int probe = th07_psp_me_render_stream_probe(&token, 0);
            if (probe > 0)
            {
                if (th07_psp_me_render_stream_retire(&token, 0, 0) != 1)
                    return 0;
                break;
            }
            if (probe < 0)
                return 0;
            if (sceKernelGetSystemTimeLow() - startUs >=
                ME_RENDER_BENCH_TIMEOUT_US)
            {
                th07_psp_me_render_stream_hard_fault(&token);
                return 0;
            }
            sceKernelDelayThread(20);
        }
        if (__atomic_load_n(&gMeRenderStreamInFlightSlot,
                            __ATOMIC_ACQUIRE) != 0xffffffffu)
            return 0;
    }

    for (uint32_t slotIndex = 0u;
         slotIndex < TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT; ++slotIndex)
    {
        MeRenderStreamSlotControl *slot = &gMeRenderStreamSlots[slotIndex];
        const uint32_t state =
            __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
        if (state == TH07_PSP_ME_RENDER_STREAM_STATE_FREE)
            continue;

        Th07PspMeRenderStreamToken token;
        token.slot = slotIndex;
        token.generation =
            __atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE);
        if (state == TH07_PSP_ME_RENDER_STREAM_STATE_SC_BUILD)
        {
            if (!th07_psp_me_render_stream_cancel_build(&token))
                return 0;
            continue;
        }
        if (state == TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC)
        {
            if (!th07_psp_me_render_stream_release_ready(&token))
                return 0;
            continue;
        }
        return 0;
    }
    return 1;
#else
    const uint32_t slotIndex =
        __atomic_load_n(&gMeRenderStreamInFlightSlot, __ATOMIC_ACQUIRE);
    if (slotIndex == 0xffffffffu)
        return 1;
    if (slotIndex >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT)
        return 0;
    Th07PspMeRenderStreamToken token;
    token.slot = slotIndex;
    token.generation = gMeRenderStreamSlots[slotIndex].generation;
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    for (;;)
    {
        const int probe = th07_psp_me_render_stream_probe(&token, 0);
        if (probe > 0)
        {
            (void)th07_psp_me_render_stream_retire(&token, 0, 0);
            return __atomic_load_n(&gMeRenderStreamInFlightSlot,
                                   __ATOMIC_ACQUIRE) == 0xffffffffu;
        }
        if (probe < 0)
            return 0;
        if (sceKernelGetSystemTimeLow() - startUs >=
            ME_RENDER_BENCH_TIMEOUT_US)
        {
            th07_psp_me_render_stream_hard_fault(&token);
            return 0;
        }
        sceKernelDelayThread(20);
    }
#endif
}

#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
int th07_psp_me_render_stream_drain_live(void)
{
    unsigned int expected = 0u;
    if (!__atomic_compare_exchange_n(
            &gMeRenderStreamDraining, &expected, 1u, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;

    int drained = me_render_stream_drain_for_shutdown();
    if (drained &&
        __atomic_load_n(&gMeRenderStreamInFlightSlot,
                        __ATOMIC_ACQUIRE) != 0xffffffffu)
        drained = 0;
    for (uint32_t slotIndex = 0u;
         drained && slotIndex < TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT;
         ++slotIndex)
    {
        const uint32_t state = __atomic_load_n(
            &gMeRenderStreamSlots[slotIndex].state, __ATOMIC_ACQUIRE);
        if (state != TH07_PSP_ME_RENDER_STREAM_STATE_FREE)
            drained = 0;
    }

    __atomic_store_n(&gMeRenderStreamDraining, 0u, __ATOMIC_RELEASE);
    return drained;
}
#endif
#endif

static int me_render_drain_for_shutdown(void)
{
    if (!__atomic_load_n(&gMeRenderInFlight, __ATOMIC_ACQUIRE))
        return 1;

    // Normal HOME/exit can race a shadow job that was published just before
    // the last frame.  It is safe to wait here (never in the draw path), but a
    // true hang must retain ownership and buffers as a quarantined cold-reboot
    // fault instead of pretending that STOP can reclaim them.
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    for (;;)
    {
        const int probe = th07_psp_me_render_probe(0);
        if (probe > 0)
            return th07_psp_me_render_retire(0) == 1;
        if (probe < 0)
            return 0;
        if (sceKernelGetSystemTimeLow() - startUs >=
            ME_RENDER_BENCH_TIMEOUT_US)
        {
            th07_psp_me_render_hard_fault();
            return 0;
        }
        sceKernelDelayThread(20);
    }
}
#endif

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
static void me_bullet_fast_fill_completion(
    Th07PspMeBulletFastCompletion *completion)
{
    if (!completion || !gMeMailboxUncached)
        return;
    volatile MeBulletFastMailbox *mail = &gMeMailboxUncached->bulletFast;
    memset(completion, 0, sizeof(*completion));
    completion->version = mail->job.version;
    completion->frameSeq = mail->job.frameSeq;
    completion->result = mail->result;
    completion->activeCount = mail->activeCount;
    completion->candidateCount = mail->candidateCount;
    completion->inBoundsCount = mail->inBoundsCount;
    completion->noCollisionCount = mail->noCollisionCount;
    completion->firstBadSlot = mail->firstBadSlot;
    completion->scWritebackUs = gMeBulletFastScWritebackUs;
    completion->dispatchWaitUs =
        sceKernelGetSystemTimeLow() - gMeBulletFastStartUs;
    completion->meInvalidateCycles = mail->invalidateCycles;
    completion->meKernelCycles = mail->kernelCycles;
    completion->meWritebackCycles = mail->writebackCycles;
    completion->meFcr31Before = mail->fcr31Before;
    completion->meFcr31Effective = mail->fcr31Effective;
    completion->meFcr31After = mail->fcr31After;
}

static int me_bullet_fast_output_valid(
    const Th07PspMeBulletFastOutput *output,
    const Th07PspMeBulletFastCompletion *completion,
    uint32_t playerState)
{
    if (!output || !completion || completion->activeCount >
            TH07_PSP_ME_BULLET_FAST_MAX_SLOTS ||
        completion->candidateCount > completion->activeCount ||
        completion->inBoundsCount > completion->candidateCount ||
        completion->noCollisionCount > completion->candidateCount ||
        completion->firstBadSlot != 0xffffffffu ||
        (playerState == ME_BULLET_FAST_PLAYER_STATE_BORDER &&
         completion->noCollisionCount != 0u))
        return 0;

    uint32_t candidateCount = 0u;
    uint32_t inBoundsCount = 0u;
    uint32_t noCollisionCount = 0u;
    const uint32_t allowedFlags =
        TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE |
        TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS |
        TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION;
    for (uint32_t slot = 0u;
         slot < TH07_PSP_ME_BULLET_FAST_MAX_SLOTS; ++slot)
    {
        const Th07PspMeBulletFastSlotResult *result =
            &output->slots[slot];
        const int bitmapCandidate =
            (output->candidateBits[slot >> 5u] &
             (1u << (slot & 31u))) != 0u;
        const int flagCandidate =
            (result->flags &
             TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE) != 0u;
        if (bitmapCandidate != flagCandidate ||
            (result->flags & ~allowedFlags) != 0u ||
            (!flagCandidate && result->flags != 0u))
            return 0;
        if (!flagCandidate)
            continue;
        if (!me_render_stream_float_bits_finite(result->posXBits) ||
            !me_render_stream_float_bits_finite(result->posYBits) ||
            !me_render_stream_float_bits_finite(result->posZBits))
            return 0;
        ++candidateCount;
        if ((result->flags &
             TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS) != 0u)
            ++inBoundsCount;
        if ((result->flags &
             TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION) != 0u)
        {
            if (playerState == ME_BULLET_FAST_PLAYER_STATE_BORDER)
                return 0;
            ++noCollisionCount;
        }
    }
    return candidateCount == completion->candidateCount &&
           inBoundsCount == completion->inBoundsCount &&
           noCollisionCount == completion->noCollisionCount;
}

int th07_psp_me_bullet_fast_update_run(
    const Th07PspMeBulletFastJob *job,
    Th07PspMeBulletFastCompletion *completion,
    const Th07PspMeBulletFastOutput **output)
{
    if (completion)
        memset(completion, 0, sizeof(*completion));
    if (output)
        *output = 0;
    if (!job || !completion || !output ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE) ||
        !gMeMailboxUncached ||
        !me_bullet_fast_job_valid(
            job, me_bullet_fast_output_physical(),
            sizeof(Th07PspMeBulletFastOutput)))
        return 0;

    unsigned int expectedFlight = 0u;
    if (!__atomic_compare_exchange_n(
            &gMeBulletFastInFlight, &expectedFlight, 1u, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
    if (!claim_me_for_bullet_fast())
    {
        __atomic_store_n(&gMeBulletFastInFlight, 0u, __ATOMIC_RELEASE);
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (box->command != ME_CMD_NONE || box->status != ME_STAT_DONE ||
        box->workerState != ME_WORKER_READY || box->suspendRequested != 0u ||
        box->stackFault ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        release_me();
        __atomic_store_n(&gMeBulletFastInFlight, 0u, __ATOMIC_RELEASE);
        return 0;
    }

    memset(gMeBulletFastOutputArea.guard0,
           ME_BULLET_FAST_GUARD_PATTERN,
           sizeof(gMeBulletFastOutputArea.guard0));
    memset(gMeBulletFastOutputArea.guard1,
           ME_BULLET_FAST_GUARD_PATTERN,
           sizeof(gMeBulletFastOutputArea.guard1));
    sceKernelDcacheWritebackInvalidateRange(
        &gMeBulletFastOutputArea, sizeof(gMeBulletFastOutputArea));

    const uint32_t writebackStart = sceKernelGetSystemTimeLow();
    // This is the last SC operation on the frozen authority before command
    // publication.  It covers the scattered Bullet/player/sprite pools.
    sceKernelDcacheWritebackAll();
    gMeBulletFastScWritebackUs =
        sceKernelGetSystemTimeLow() - writebackStart;
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        release_me();
        __atomic_store_n(&gMeBulletFastInFlight, 0u, __ATOMIC_RELEASE);
        return 0;
    }

    volatile MeBulletFastMailbox *mail = &box->bulletFast;
    mail->job = *job;
    mail->outputPhys = me_bullet_fast_output_physical();
    mail->outputCapacity = sizeof(Th07PspMeBulletFastOutput);
    mail->result = TH07_PSP_ME_BULLET_FAST_JOB_PROTOCOL;
    mail->activeCount = 0u;
    mail->candidateCount = 0u;
    mail->inBoundsCount = 0u;
    mail->noCollisionCount = 0u;
    mail->firstBadSlot = 0xffffffffu;
    mail->invalidateCycles = 0u;
    mail->kernelCycles = 0u;
    mail->writebackCycles = 0u;
    mail->fcr31Before = 0u;
    mail->fcr31Effective = 0u;
    mail->fcr31After = 0u;
    box->status = ME_STAT_IDLE;
    gMeBulletFastPublishedJob = *job;
    __asm__ volatile("sync");
    gMeBulletFastStartUs = sceKernelGetSystemTimeLow();
    box->command = ME_CMD_BULLET_FAST_UPDATE;
    __asm__ volatile("sync");

    while (box->status != ME_STAT_DONE)
    {
        if (sceKernelGetSystemTimeLow() - gMeBulletFastStartUs >=
            ME_BULLET_FAST_TIMEOUT_US)
        {
            // The process-lifetime arena and owner remain quarantined.  ME may
            // still hold a live Bullet alias, so canonical mutation is unsafe.
            timeout_me();
            return -1;
        }
        sceKernelDelayThread(20);
    }
    __asm__ volatile("sync");

    Th07PspMeBulletFastCompletion local;
    me_bullet_fast_fill_completion(&local);
    const uint32_t invalidateStart = sceKernelGetSystemTimeLow();
    sceKernelDcacheInvalidateRange(
        &gMeBulletFastOutputArea, sizeof(gMeBulletFastOutputArea));
    local.scInvalidateUs =
        sceKernelGetSystemTimeLow() - invalidateStart;

    const Th07PspMeBulletFastJob echoedJob = mail->job;
    const int echoValid =
        memcmp(&echoedJob, &gMeBulletFastPublishedJob,
               sizeof(echoedJob)) == 0;
    const int commonValid =
        box->status == ME_STAT_DONE && box->command == ME_CMD_NONE &&
        box->workerState == ME_WORKER_READY &&
        box->suspendRequested == 0u && echoValid &&
        mail->outputPhys == me_bullet_fast_output_physical() &&
        mail->outputCapacity == sizeof(Th07PspMeBulletFastOutput) &&
        me_bullet_fast_guards_match(
            (const volatile unsigned char *)&gMeBulletFastOutputArea) &&
        !box->stackFault &&
        __atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) &&
        !__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE);
    const int outputValid =
        commonValid &&
        local.result == TH07_PSP_ME_BULLET_FAST_JOB_OK &&
        local.version == gMeBulletFastPublishedJob.version &&
        local.frameSeq == gMeBulletFastPublishedJob.frameSeq &&
        local.meFcr31Effective == 0u &&
        local.meFcr31Before == local.meFcr31After &&
        me_bullet_fast_output_valid(
            &gMeBulletFastOutputArea.output, &local,
            gMeBulletFastPublishedJob.playerState);

    *completion = local;
    if (outputValid)
        *output = &gMeBulletFastOutputArea.output;

    const int completedReject = commonValid && !outputValid &&
        local.version == gMeBulletFastPublishedJob.version &&
        local.frameSeq == gMeBulletFastPublishedJob.frameSeq &&
        (local.result == TH07_PSP_ME_BULLET_FAST_JOB_VERSION ||
         local.result == TH07_PSP_ME_BULLET_FAST_JOB_BOUNDS ||
         local.result == TH07_PSP_ME_BULLET_FAST_JOB_RECORD ||
         local.result == TH07_PSP_ME_BULLET_FAST_JOB_GUARD);
    if (!outputValid && !completedReject)
        poison_me();
    release_me();
    __atomic_store_n(&gMeBulletFastInFlight, 0u, __ATOMIC_RELEASE);
    return outputValid ? 1 : 0;
}
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
static void me_bullet_compact_fill_completion(
    Th07PspMeBulletCompactCompletion *completion)
{
    if (!completion || !gMeMailboxUncached)
        return;
    volatile MeBulletCompactMailbox *mail =
        &gMeMailboxUncached->bulletCompact;
    memset(completion, 0, sizeof(*completion));
    completion->version = mail->job.version;
    completion->frameSeq = mail->job.frameSeq;
    completion->seedFrameSeq = mail->job.seedFrameSeq;
    completion->seedTargetDrawSeq = mail->job.seedTargetDrawSeq;
    completion->result = mail->result;
    completion->candidateCount = mail->candidateCount;
    completion->inBoundsCount = mail->inBoundsCount;
    completion->noCollisionCount = mail->noCollisionCount;
    completion->firstBadSlot = mail->firstBadSlot;
    completion->dispatchAgeUs =
        sceKernelGetSystemTimeLow() - gMeBulletCompactStartUs;
    completion->scSeedInvalidateUs = gMeBulletCompactSeedInvalidateUs;
    completion->meInvalidateCycles = mail->invalidateCycles;
    completion->meKernelCycles = mail->kernelCycles;
    completion->meWritebackCycles = mail->writebackCycles;
    completion->meFcr31Before = mail->fcr31Before;
    completion->meFcr31Effective = mail->fcr31Effective;
    completion->meFcr31After = mail->fcr31After;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    completion->itemResult = mail->itemResult;
    completion->itemCandidateCount = mail->itemCandidateCount;
    completion->itemProcessedCount = mail->itemProcessedCount;
    completion->itemFirstBadSlot = mail->itemFirstBadSlot;
#endif
}

const Th07PspMeBulletCompactSeed *
th07_psp_me_bullet_compact_seed_bank(unsigned int bank)
{
    if (bank >= TH07_PSP_ME_BULLET_COMPACT_BANKS)
        return 0;
    MeBulletCompactSeedArea *area = &gMeBulletCompactSeedAreas[bank];
    sceKernelDcacheInvalidateRange(area, sizeof(*area));
    if (!me_bullet_compact_seed_guards_match(
            (const volatile unsigned char *)area) ||
        !me_bullet_compact_seed_header_valid(&area->seed, bank))
        return 0;
    return &area->seed;
}

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
const Th07PspMeItemMotionSeed *
th07_psp_me_item_motion_seed_bank(unsigned int bank)
{
    if (bank >= TH07_PSP_ME_ITEM_MOTION_BANKS)
        return 0;
    MeItemMotionSeedArea *area = &gMeItemMotionSeedAreas[bank];
    sceKernelDcacheInvalidateRange(area->guard0, sizeof(area->guard0));
    sceKernelDcacheInvalidateRange(&area->seed.header,
                                   sizeof(area->seed.header));
    sceKernelDcacheInvalidateRange(area->guard1, sizeof(area->guard1));
    if (!me_item_motion_seed_guards_match(
            (const volatile unsigned char *)area) ||
        !me_item_motion_seed_header_valid(&area->seed, bank))
        return 0;
    return &area->seed;
}

const Th07PspMeItemMotionOutput *
th07_psp_me_item_motion_last_output(void)
{
    return __atomic_load_n(&gMeItemMotionOutputValid, __ATOMIC_ACQUIRE)
        ? &gMeItemMotionOutputArea.output
        : 0;
}

int th07_psp_me_item_motion_available(void)
{
    return __atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&gMeItemMotionEnabled, __ATOMIC_ACQUIRE)
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
           && __atomic_load_n(&gMeItemRenderEnabled, __ATOMIC_ACQUIRE)
#endif
           ;
}

void th07_psp_me_item_motion_diag_snapshot(
    Th07PspMeItemMotionDiag *snapshot)
{
    if (!snapshot)
        return;
    snapshot->state =
        __atomic_load_n(&gMeItemMotionDiagState, __ATOMIC_ACQUIRE);
    snapshot->reason =
        __atomic_load_n(&gMeItemMotionDiagReason, __ATOMIC_ACQUIRE);
    snapshot->selftestRuns =
        __atomic_load_n(&gMeItemMotionDiagSelftestRuns, __ATOMIC_ACQUIRE);
    snapshot->selftestFailures =
        __atomic_load_n(&gMeItemMotionDiagSelftestFailures,
                        __ATOMIC_ACQUIRE);
    snapshot->bulletRetryRuns =
        __atomic_load_n(&gMeItemMotionDiagBulletRetryRuns,
                        __ATOMIC_ACQUIRE);
    snapshot->bulletRetryPasses =
        __atomic_load_n(&gMeItemMotionDiagBulletRetryPasses,
                        __ATOMIC_ACQUIRE);
    snapshot->lastPollResult =
        __atomic_load_n(&gMeItemMotionDiagLastPollResult,
                        __ATOMIC_ACQUIRE);
    snapshot->lastBulletResult =
        __atomic_load_n(&gMeItemMotionDiagLastBulletResult,
                        __ATOMIC_ACQUIRE);
    snapshot->lastItemResult =
        __atomic_load_n(&gMeItemMotionDiagLastItemResult,
                        __ATOMIC_ACQUIRE);
    snapshot->firstMismatchSlot =
        __atomic_load_n(&gMeItemMotionDiagFirstMismatchSlot,
                        __ATOMIC_ACQUIRE);
}
#endif

int th07_psp_me_bullet_compact_begin(
    const Th07PspMeBulletCompactJob *job)
{
    if (!job ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE) ||
        !gMeMailboxUncached ||
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        (((job->flags &
           TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u) &&
         !__atomic_load_n(&gMeItemMotionEnabled, __ATOMIC_ACQUIRE) &&
         !gMeItemMotionSelftestInProgress) ||
#endif
        !me_bullet_compact_job_valid(
            job, me_bullet_compact_seed_physical(job->seedBank),
            sizeof(Th07PspMeBulletCompactSeed),
            me_bullet_compact_output_physical(),
            sizeof(Th07PspMeBulletCompactOutput)))
        return 0;

    unsigned int expectedFlight = 0u;
    if (!__atomic_compare_exchange_n(
            &gMeBulletCompactInFlight, &expectedFlight, 1u, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
    if (!claim_me_for_bullet_compact())
    {
        __atomic_store_n(&gMeBulletCompactInFlight, 0u,
                         __ATOMIC_RELEASE);
        return 0;
    }

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (box->command != ME_CMD_NONE || box->status != ME_STAT_DONE ||
        box->workerState != ME_WORKER_READY || box->suspendRequested != 0u ||
        box->stackFault ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        release_me();
        __atomic_store_n(&gMeBulletCompactInFlight, 0u,
                         __ATOMIC_RELEASE);
        return 0;
    }

    const uint32_t seedPrepareStart = sceKernelGetSystemTimeLow();
    MeBulletCompactSeedArea *seedArea =
        &gMeBulletCompactSeedAreas[job->seedBank];
    // Only the guard/header lines are needed to authorize publication.  The
    // payload is immutable Main RAM and ME invalidates it before consuming;
    // SC invalidates the complete seed once in poll before JIT adoption.
    sceKernelDcacheInvalidateRange(seedArea->guard0,
                                   sizeof(seedArea->guard0));
    sceKernelDcacheInvalidateRange(&seedArea->seed.header,
                                   sizeof(seedArea->seed.header));
    sceKernelDcacheInvalidateRange(seedArea->guard1,
                                   sizeof(seedArea->guard1));
    gMeBulletCompactSeedInvalidateUs =
        sceKernelGetSystemTimeLow() - seedPrepareStart;
    if (!me_bullet_compact_seed_guards_match(
            (const volatile unsigned char *)seedArea) ||
        !me_bullet_compact_seed_header_valid(
            &seedArea->seed, job->seedBank) ||
        seedArea->seed.header.frameSeq != job->seedFrameSeq ||
        seedArea->seed.header.targetDrawSeq != job->seedTargetDrawSeq ||
        seedArea->seed.header.stageEpoch != job->stageEpoch ||
        seedArea->seed.header.managerEpoch != job->managerEpoch ||
        // replayEpoch is an immutable seed echo, not a comparison against the
        // current ReplayManager::frameId (which advances every replay frame).
        seedArea->seed.header.replayEpoch != job->replayEpoch)
    {
        release_me();
        __atomic_store_n(&gMeBulletCompactInFlight, 0u,
                         __ATOMIC_RELEASE);
        return 0;
    }

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    __atomic_store_n(&gMeItemMotionOutputValid, 0u, __ATOMIC_RELEASE);
    const int itemMotionEnabled =
        (job->flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u;
    if (itemMotionEnabled)
    {
        MeItemMotionSeedArea *itemSeedArea =
            &gMeItemMotionSeedAreas[job->seedBank];
        sceKernelDcacheInvalidateRange(itemSeedArea->guard0,
                                       sizeof(itemSeedArea->guard0));
        sceKernelDcacheInvalidateRange(&itemSeedArea->seed.header,
                                       sizeof(itemSeedArea->seed.header));
        sceKernelDcacheInvalidateRange(itemSeedArea->guard1,
                                       sizeof(itemSeedArea->guard1));
        if (!me_item_motion_seed_guards_match(
                (const volatile unsigned char *)itemSeedArea) ||
            !me_item_motion_seed_header_valid(
                &itemSeedArea->seed, job->seedBank) ||
            itemSeedArea->seed.header.frameSeq != job->seedFrameSeq ||
            itemSeedArea->seed.header.targetDrawSeq !=
                job->seedTargetDrawSeq ||
            itemSeedArea->seed.header.stageEpoch != job->stageEpoch ||
            itemSeedArea->seed.header.managerEpoch != job->managerEpoch)
        {
            release_me();
            __atomic_store_n(&gMeBulletCompactInFlight, 0u,
                             __ATOMIC_RELEASE);
            return 0;
        }
        memset(gMeItemMotionOutputArea.guard0,
               ME_BULLET_COMPACT_GUARD_PATTERN,
               sizeof(gMeItemMotionOutputArea.guard0));
        memset(gMeItemMotionOutputArea.guard1,
               ME_BULLET_COMPACT_GUARD_PATTERN,
               sizeof(gMeItemMotionOutputArea.guard1));
        sceKernelDcacheWritebackInvalidateRange(
            &gMeItemMotionOutputArea, sizeof(gMeItemMotionOutputArea));
    }
#endif

    memset(gMeBulletCompactOutputArea.guard0,
           ME_BULLET_COMPACT_GUARD_PATTERN,
           sizeof(gMeBulletCompactOutputArea.guard0));
    memset(gMeBulletCompactOutputArea.guard1,
           ME_BULLET_COMPACT_GUARD_PATTERN,
           sizeof(gMeBulletCompactOutputArea.guard1));
    sceKernelDcacheWritebackInvalidateRange(
        &gMeBulletCompactOutputArea, sizeof(gMeBulletCompactOutputArea));
    if ((job->flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID) != 0u &&
        job->bombClearHighWater != 0u)
    {
        // Validated `Phys` is in 0x08000000..0x0bffffff, which is also PSP's
        // cached KUSEG Main-RAM alias.  It is not a zero-based bus offset.
        void *const bombClearCached =
            (void *)(uintptr_t)job->bombClearBasePhys;
        sceKernelDcacheWritebackRange(
            bombClearCached,
            job->bombClearHighWater * ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE);
    }
    if (__atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        release_me();
        __atomic_store_n(&gMeBulletCompactInFlight, 0u,
                         __ATOMIC_RELEASE);
        return 0;
    }

    volatile MeBulletCompactMailbox *mail = &box->bulletCompact;
    mail->job = *job;
    mail->seedPhys = me_bullet_compact_seed_physical(job->seedBank);
    mail->seedCapacity = sizeof(Th07PspMeBulletCompactSeed);
    mail->outputPhys = me_bullet_compact_output_physical();
    mail->outputCapacity = sizeof(Th07PspMeBulletCompactOutput);
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    mail->itemSeedPhys = itemMotionEnabled
        ? me_item_motion_seed_physical(job->seedBank) : 0u;
    mail->itemSeedCapacity = itemMotionEnabled
        ? sizeof(Th07PspMeItemMotionSeed) : 0u;
    mail->itemOutputPhys = itemMotionEnabled
        ? me_item_motion_output_physical() : 0u;
    mail->itemOutputCapacity = itemMotionEnabled
        ? sizeof(Th07PspMeItemMotionOutput) : 0u;
#endif
    mail->result = TH07_PSP_ME_BULLET_COMPACT_RESULT_PROTOCOL;
    mail->candidateCount = 0u;
    mail->inBoundsCount = 0u;
    mail->noCollisionCount = 0u;
    mail->firstBadSlot = 0xffffffffu;
    mail->invalidateCycles = 0u;
    mail->kernelCycles = 0u;
    mail->writebackCycles = 0u;
    mail->fcr31Before = 0u;
    mail->fcr31Effective = 0u;
    mail->fcr31After = 0u;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    mail->itemResult = TH07_PSP_ME_ITEM_MOTION_RESULT_DISABLED;
    mail->itemCandidateCount = 0u;
    mail->itemProcessedCount = 0u;
    mail->itemFirstBadSlot = 0xffffffffu;
#endif
    box->status = ME_STAT_IDLE;
    gMeBulletCompactPublishedJob = *job;
    __asm__ volatile("sync");
    gMeBulletCompactStartUs = sceKernelGetSystemTimeLow();
    box->command = ME_CMD_BULLET_COMPACT_UPDATE;
    __asm__ volatile("sync");
    return 1;
}

int th07_psp_me_bullet_compact_poll(
    Th07PspMeBulletCompactCompletion *completion,
    const Th07PspMeBulletCompactOutput **output,
    const Th07PspMeBulletCompactSeed **seed)
{
    if (completion)
        memset(completion, 0, sizeof(*completion));
    if (output)
        *output = 0;
    if (seed)
        *seed = 0;
    if (!completion || !output || !seed ||
        !__atomic_load_n(&gMeBulletCompactInFlight, __ATOMIC_ACQUIRE) ||
        !gMeMailboxUncached)
        return -1;
    if (__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        return -2;

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (box->status != ME_STAT_DONE)
    {
        if (sceKernelGetSystemTimeLow() - gMeBulletCompactStartUs <
            ME_BULLET_COMPACT_TIMEOUT_US)
            return 0;
        // A late worker may still write its process-lifetime output arena.
        // Keep ownership quarantined and require the existing cold reboot.
        timeout_me();
        return -2;
    }
    __asm__ volatile("sync");

    Th07PspMeBulletCompactCompletion local;
    me_bullet_compact_fill_completion(&local);
    MeBulletCompactSeedArea *seedArea =
        &gMeBulletCompactSeedAreas[gMeBulletCompactPublishedJob.seedBank];
    // The p9 SC launcher already invalidated this immutable seed before
    // publication. Command 12 only reads it, so invalidating the full seed
    // bank again here would discard the exact cache lines the p12 JIT adopter is
    // about to consume. Only the ME-written output needs invalidation.
    const uint32_t outputInvalidateStart = sceKernelGetSystemTimeLow();
    sceKernelDcacheInvalidateRange(
        &gMeBulletCompactOutputArea, sizeof(gMeBulletCompactOutputArea));
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    const int itemMotionRequested =
        (gMeBulletCompactPublishedJob.flags &
         TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID) != 0u;
    if (itemMotionRequested)
    {
        sceKernelDcacheInvalidateRange(
            &gMeItemMotionSeedAreas[
                gMeBulletCompactPublishedJob.seedBank],
            sizeof(gMeItemMotionSeedAreas[0]));
        sceKernelDcacheInvalidateRange(
            &gMeItemMotionOutputArea, sizeof(gMeItemMotionOutputArea));
    }
    __atomic_store_n(&gMeItemMotionOutputValid, 0u, __ATOMIC_RELEASE);
#endif
    local.scOutputInvalidateUs =
        sceKernelGetSystemTimeLow() - outputInvalidateStart;

    volatile MeBulletCompactMailbox *mail = &box->bulletCompact;
    const Th07PspMeBulletCompactJob echoedJob = mail->job;
    const int echoValid =
        memcmp(&echoedJob, &gMeBulletCompactPublishedJob,
               sizeof(echoedJob)) == 0;
    const int commonValid =
        box->status == ME_STAT_DONE && box->command == ME_CMD_NONE &&
        box->workerState == ME_WORKER_READY &&
        box->suspendRequested == 0u && echoValid &&
        mail->seedPhys == me_bullet_compact_seed_physical(
                              gMeBulletCompactPublishedJob.seedBank) &&
        mail->seedCapacity == sizeof(Th07PspMeBulletCompactSeed) &&
        mail->outputPhys == me_bullet_compact_output_physical() &&
        mail->outputCapacity == sizeof(Th07PspMeBulletCompactOutput) &&
        me_bullet_compact_seed_guards_match(
            (const volatile unsigned char *)seedArea) &&
        me_bullet_compact_output_guards_match(
            (const volatile unsigned char *)&gMeBulletCompactOutputArea) &&
        !box->stackFault &&
        !__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE);

    const Th07PspMeBulletCompactSeed *seedValue = &seedArea->seed;
    const int seedValid = commonValid &&
        me_bullet_compact_seed_header_valid(
            seedValue, gMeBulletCompactPublishedJob.seedBank) &&
        seedValue->header.frameSeq ==
            gMeBulletCompactPublishedJob.seedFrameSeq &&
        seedValue->header.targetDrawSeq ==
            gMeBulletCompactPublishedJob.seedTargetDrawSeq &&
        seedValue->header.stageEpoch ==
            gMeBulletCompactPublishedJob.stageEpoch &&
        seedValue->header.managerEpoch ==
            gMeBulletCompactPublishedJob.managerEpoch &&
        seedValue->header.replayEpoch ==
            gMeBulletCompactPublishedJob.replayEpoch;
    uint32_t bitmapCount = 0u;
    for (uint32_t word = 0u;
         seedValid && word < TH07_PSP_ME_BULLET_COMPACT_ACTIVE_WORDS;
         ++word)
    {
        bitmapCount += (uint32_t)__builtin_popcount(
            gMeBulletCompactOutputArea.output.candidateBits[word]);
    }
    const int outputValid = seedValid &&
        local.result == TH07_PSP_ME_BULLET_COMPACT_RESULT_OK &&
        local.version == gMeBulletCompactPublishedJob.version &&
        local.frameSeq == gMeBulletCompactPublishedJob.frameSeq &&
        local.seedFrameSeq ==
            gMeBulletCompactPublishedJob.seedFrameSeq &&
        local.seedTargetDrawSeq ==
            gMeBulletCompactPublishedJob.seedTargetDrawSeq &&
        local.firstBadSlot == 0xffffffffu &&
        local.candidateCount == seedValue->header.candidateCount &&
        bitmapCount == local.candidateCount &&
        local.inBoundsCount <= local.candidateCount &&
        local.noCollisionCount <= local.candidateCount &&
        memcmp(gMeBulletCompactOutputArea.output.candidateBits,
               seedValue->candidateBits,
               sizeof(seedValue->candidateBits)) == 0 &&
        local.meFcr31Effective == 0u &&
        local.meFcr31Before == local.meFcr31After;

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    int itemOutputValid = 0;
    if (itemMotionRequested && commonValid && outputValid)
    {
        MeItemMotionSeedArea *itemSeedArea =
            &gMeItemMotionSeedAreas[gMeBulletCompactPublishedJob.seedBank];
        const Th07PspMeItemMotionSeed *itemSeedValue =
            &itemSeedArea->seed;
        const Th07PspMeItemMotionOutput *itemOutputValue =
            &gMeItemMotionOutputArea.output;
        const int itemEnvelopeValid =
            mail->itemSeedPhys == me_item_motion_seed_physical(
                                      gMeBulletCompactPublishedJob.seedBank) &&
            mail->itemSeedCapacity == sizeof(Th07PspMeItemMotionSeed) &&
            mail->itemOutputPhys == me_item_motion_output_physical() &&
            mail->itemOutputCapacity == sizeof(Th07PspMeItemMotionOutput) &&
            me_item_motion_seed_guards_match(
                (const volatile unsigned char *)itemSeedArea) &&
            me_item_motion_output_guards_match(
                (const volatile unsigned char *)&gMeItemMotionOutputArea) &&
            me_item_motion_seed_header_valid(
                itemSeedValue, gMeBulletCompactPublishedJob.seedBank) &&
            itemSeedValue->header.frameSeq ==
                gMeBulletCompactPublishedJob.seedFrameSeq &&
            itemSeedValue->header.targetDrawSeq ==
                gMeBulletCompactPublishedJob.seedTargetDrawSeq &&
            itemSeedValue->header.stageEpoch ==
                gMeBulletCompactPublishedJob.stageEpoch &&
            itemSeedValue->header.managerEpoch ==
                gMeBulletCompactPublishedJob.managerEpoch &&
            local.itemResult == TH07_PSP_ME_ITEM_MOTION_RESULT_OK &&
            local.itemCandidateCount == itemSeedValue->header.candidateCount &&
            local.itemProcessedCount <= local.itemCandidateCount &&
            local.itemProcessedCount <=
                gMeBulletCompactPublishedJob.itemMotionCandidateLimit &&
            local.itemFirstBadSlot == 0xffffffffu &&
            me_item_motion_output_header_valid(
                itemOutputValue, itemSeedValue,
                &gMeBulletCompactPublishedJob);

        uint32_t itemBitmapCount = 0u;
        int itemPayloadValid = itemEnvelopeValid;
        for (uint32_t word = 0u;
             itemPayloadValid &&
             word < TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS; ++word)
        {
            const uint32_t outputBits =
                itemOutputValue->candidateBits[word];
            const uint32_t seedBits = itemSeedValue->candidateBits[word];
            if ((outputBits & ~seedBits) != 0u ||
                (word == TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS - 1u &&
                 (outputBits & 0xfffff000u) != 0u) ||
                (word >= TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS &&
                 outputBits != 0u))
            {
                itemPayloadValid = 0;
                break;
            }
            itemBitmapCount += (uint32_t)__builtin_popcount(outputBits);
            if (word >= TH07_PSP_ME_ITEM_MOTION_ACTIVE_WORDS)
                continue;
            for (uint32_t bitIndex = 0u; bitIndex < 32u; ++bitIndex)
            {
                const uint32_t slotBit = 1u << bitIndex;
                if ((outputBits & slotBit) == 0u)
                    continue;
                const uint32_t slot = word * 32u + bitIndex;
                if (slot >= TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS)
                {
                    itemPayloadValid = 0;
                    break;
                }
                const Th07PspMeItemMotionSlotResult *itemResult =
                    &itemOutputValue->slots[slot];
                const uint32_t state = itemResult->stateAndRoute &
                    TH07_PSP_ME_ITEM_MOTION_RESULT_STATE_MASK;
                const uint32_t autoCollect =
                    (itemResult->stateAndRoute >>
                     TH07_PSP_ME_ITEM_MOTION_RESULT_AUTOCOLLECT_SHIFT) &
                    0xffu;
                const uint32_t allowedResultBits =
                    TH07_PSP_ME_ITEM_MOTION_RESULT_STATE_MASK |
                    (0xffu <<
                     TH07_PSP_ME_ITEM_MOTION_RESULT_AUTOCOLLECT_SHIFT) |
                    TH07_PSP_ME_ITEM_MOTION_RESULT_CANDIDATE |
                    TH07_PSP_ME_ITEM_MOTION_RESULT_GOTO_COLLISION |
                    TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_MASK;
                const uint32_t values[] = {
                    itemResult->posXBits, itemResult->posYBits,
                    itemResult->posZBits, itemResult->startXBits,
                    itemResult->startYBits, itemResult->startZBits
                };
                if (itemResult->generation !=
                        itemSeedValue->slots[slot].generation ||
                    state > 2u || autoCollect > 1u ||
                    (itemResult->stateAndRoute & ~allowedResultBits) != 0u ||
                    (itemResult->stateAndRoute &
                     TH07_PSP_ME_ITEM_MOTION_RESULT_CANDIDATE) == 0u)
                {
                    itemPayloadValid = 0;
                    break;
                }
                for (uint32_t valueIndex = 0u;
                     valueIndex < sizeof(values) / sizeof(values[0]);
                     ++valueIndex)
                {
                    if (!me_item_motion_float_bits_supported(
                            values[valueIndex]))
                    {
                        itemPayloadValid = 0;
                        break;
                    }
                }
                if (!itemPayloadValid)
                    break;
            }
        }
        itemOutputValid = itemPayloadValid &&
            itemBitmapCount == local.itemProcessedCount &&
            itemBitmapCount == itemOutputValue->header.processedCount;
    }
    const int itemCleanReject =
        itemMotionRequested && commonValid && outputValid &&
        !itemOutputValid &&
        local.itemResult != TH07_PSP_ME_ITEM_MOTION_RESULT_PROTOCOL;
#endif

    *completion = local;
    if (outputValid)
    {
        *output = &gMeBulletCompactOutputArea.output;
        *seed = seedValue;
    }
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    __atomic_store_n(&gMeItemMotionOutputValid,
                     itemOutputValid ? 1u : 0u, __ATOMIC_RELEASE);
    if (itemCleanReject)
    {
        // The worker is DONE and the same command's Bullet segment passed its
        // full contract, so there is no late-writer ambiguity.  Permanently
        // close only A1-MOVE and discard its disjoint arenas; Item geometry
        // and Bullet ME stay live.  PROTOCOL/FCR/common failures never reach
        // this branch and retain the existing poison/cold-reboot behavior.
        __atomic_store_n(&gMeItemMotionEnabled, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagLastPollResult, 1,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagLastBulletResult,
                         local.result, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagLastItemResult,
                         local.itemResult, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagFirstMismatchSlot,
                         local.itemFirstBadSlot, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagReason,
                         TH07_PSP_ME_ITEM_MOTION_REASON_ITEM_CONTRACT,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagState,
                         TH07_PSP_ME_ITEM_MOTION_STATE_SAFE_FALLBACK,
                         __ATOMIC_RELEASE);
        me_item_motion_reset_arenas_on_sc();
        th07_psp_boot_note(
            "A1M RUNTIME ITEM REJECT -> MOVE OFF; BULLET ME CONTINUES");
    }
#endif

    const int completedReject = commonValid && !outputValid &&
        local.version == gMeBulletCompactPublishedJob.version &&
        local.frameSeq == gMeBulletCompactPublishedJob.frameSeq &&
        local.seedFrameSeq ==
            gMeBulletCompactPublishedJob.seedFrameSeq &&
        (local.result == TH07_PSP_ME_BULLET_COMPACT_RESULT_VERSION ||
         local.result == TH07_PSP_ME_BULLET_COMPACT_RESULT_BOUNDS ||
         local.result == TH07_PSP_ME_BULLET_COMPACT_RESULT_SEED ||
         local.result == TH07_PSP_ME_BULLET_COMPACT_RESULT_RECORD ||
         local.result == TH07_PSP_ME_BULLET_COMPACT_RESULT_GUARD);
    if (!outputValid && !completedReject)
        poison_me();
    release_me();
    __atomic_store_n(&gMeBulletCompactInFlight, 0u, __ATOMIC_RELEASE);
    if (!outputValid && !completedReject)
        return -2;
    return outputValid ? 1 : -1;
}

static int me_bullet_compact_drain_for_shutdown(void)
{
    if (!__atomic_load_n(&gMeBulletCompactInFlight, __ATOMIC_ACQUIRE))
        return 1;
    for (;;)
    {
        Th07PspMeBulletCompactCompletion completion;
        const Th07PspMeBulletCompactOutput *output = 0;
        const Th07PspMeBulletCompactSeed *seed = 0;
        const int result = th07_psp_me_bullet_compact_poll(
            &completion, &output, &seed);
        if (result == 1 || result == -1)
            return __atomic_load_n(&gMeBulletCompactInFlight,
                                   __ATOMIC_ACQUIRE) == 0u;
        if (result == -2)
            return 0;
        sceKernelDelayThread(20);
    }
}

int th07_psp_me_bullet_compact_drain_live(void)
{
    return me_bullet_compact_drain_for_shutdown();
}

#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
static int dispatch_edram_seed_bench(
    const Th07PspMeBulletCompactJob *job, uint32_t recordCount,
    uint32_t *mainP50, uint32_t *stageP50, uint32_t *mirrorP50)
{
    if (!job || !mainP50 || !stageP50 || !mirrorP50 ||
        __atomic_load_n(&gMeBulletCompactInFlight, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE) ||
        !gMeMailboxUncached || !claim_me_for_bullet_compact())
        return 0;

    volatile MeSharedMailbox *box = gMeMailboxUncached;
    if (box->command != ME_CMD_NONE || box->status != ME_STAT_DONE ||
        box->workerState != ME_WORKER_READY || box->suspendRequested != 0u ||
        box->stackFault ||
        __atomic_load_n(&gMeAudioWanted, __ATOMIC_ACQUIRE) != 0u)
    {
        release_me();
        return 0;
    }

    const uint32_t mirrorBank = job->seedBank ^ 1u;
    MeBulletCompactSeedArea *sourceArea =
        &gMeBulletCompactSeedAreas[job->seedBank];
    MeBulletCompactSeedArea *mirrorArea =
        &gMeBulletCompactSeedAreas[mirrorBank];
    memset(mirrorArea->guard0, ME_BULLET_COMPACT_GUARD_PATTERN,
           sizeof(mirrorArea->guard0));
    memset(mirrorArea->guard1, ME_BULLET_COMPACT_GUARD_PATTERN,
           sizeof(mirrorArea->guard1));
    sceKernelDcacheWritebackInvalidateRange(mirrorArea,
                                            sizeof(*mirrorArea));
    sceKernelDcacheWritebackInvalidateRange(
        &gMeBulletCompactOutputArea, sizeof(gMeBulletCompactOutputArea));

    volatile MeEdramSeedBenchMailbox *mail = &box->edramSeedBench;
    mail->job = *job;
    mail->seedPhys = me_bullet_compact_seed_physical(job->seedBank);
    mail->seedCapacity = sizeof(Th07PspMeBulletCompactSeed);
    mail->mirrorPhys = me_bullet_compact_seed_physical(mirrorBank);
    mail->mirrorCapacity = sizeof(Th07PspMeBulletCompactSeed);
    mail->outputPhys = me_bullet_compact_output_physical();
    mail->outputCapacity = sizeof(Th07PspMeBulletCompactOutput);
    mail->recordCount = recordCount;
    mail->samples = ME_EDRAM_SEED_BENCH_SAMPLES;
    mail->result = ME_EDRAM_SEED_BENCH_RESULT_PROTOCOL;
    box->status = ME_STAT_IDLE;
    __asm__ volatile("sync");
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    box->command = ME_CMD_EDRAM_SEED_BENCH;
    __asm__ volatile("sync");

    while (box->status != ME_STAT_DONE)
    {
        if (sceKernelGetSystemTimeLow() - startUs >=
            ME_EDRAM_SEED_BENCH_TIMEOUT_US)
        {
            timeout_me();
            return 0;
        }
        sceKernelDelayThread(20);
    }
    __asm__ volatile("sync");

    const Th07PspMeBulletCompactJob echoedJob = mail->job;
    const uint32_t result = mail->result;
    const uint32_t localMainP50 = mail->mainTotalP50;
    const uint32_t localStageP50 = mail->stageTotalP50;
    const uint32_t localMirrorP50 = mail->mirrorTotalP50;
    const uint32_t mainP99 = mail->mainTotalP99;
    const uint32_t stageP99 = mail->stageTotalP99;
    const uint32_t mirrorP99 = mail->mirrorTotalP99;
    const uint32_t mainInvalidateP50 = mail->mainInvalidateP50;
    const uint32_t mainKernelP50 = mail->mainKernelP50;
    const uint32_t mainWritebackP50 = mail->mainWritebackP50;
    const uint32_t mainToLocalP50 = mail->mainToLocalP50;
    const uint32_t localKernelP50 = mail->localKernelP50;
    const uint32_t localWritebackP50 = mail->localWritebackP50;
    const uint32_t localToMainP50 = mail->localToMainP50;
    const uint32_t mismatchWords = mail->mismatchWords;
    const uint32_t inputHash = mail->inputHash;
    const uint32_t localHash = mail->localHash;
    const uint32_t guardFaults = mail->guardFaults;
    const uint32_t fcr31Before = mail->fcr31Before;
    const uint32_t fcr31Effective = mail->fcr31Effective;
    const uint32_t fcr31After = mail->fcr31After;

    // Read both source and mirror through SC only after the ME has published
    // completion.  This proves the local-to-Main writeback crossed the CPU
    // boundary; an ME-cache-only memcmp is not sufficient evidence.
    sceKernelDcacheInvalidateRange(sourceArea, sizeof(*sourceArea));
    sceKernelDcacheInvalidateRange(mirrorArea, sizeof(*mirrorArea));
    sceKernelDcacheInvalidateRange(
        &gMeBulletCompactOutputArea, sizeof(gMeBulletCompactOutputArea));
    const uint32_t scSourceHash = me_edram_seed_bench_hash(
        &sourceArea->seed, sizeof(sourceArea->seed));
    const uint32_t scMirrorHash = me_edram_seed_bench_hash(
        &mirrorArea->seed, sizeof(mirrorArea->seed));
    const int scMirrorValid =
        scSourceHash == inputHash && scMirrorHash == inputHash &&
        memcmp(&sourceArea->seed, &mirrorArea->seed,
               sizeof(sourceArea->seed)) == 0;
    const int commonValid =
        box->command == ME_CMD_NONE && box->status == ME_STAT_DONE &&
        box->workerState == ME_WORKER_READY &&
        box->suspendRequested == 0u && !box->stackFault &&
        memcmp(&echoedJob, job, sizeof(*job)) == 0 &&
        mail->seedPhys == me_bullet_compact_seed_physical(job->seedBank) &&
        mail->mirrorPhys == me_bullet_compact_seed_physical(mirrorBank) &&
        mail->outputPhys == me_bullet_compact_output_physical() &&
        me_bullet_compact_seed_guards_match(
            (const volatile unsigned char *)mirrorArea) &&
        me_bullet_compact_output_guards_match(
            (const volatile unsigned char *)&gMeBulletCompactOutputArea) &&
        scMirrorValid &&
        result == ME_EDRAM_SEED_BENCH_RESULT_OK &&
        mismatchWords == 0u && inputHash == localHash &&
        guardFaults == 0u && fcr31Effective == 0u &&
        fcr31Before == fcr31After;

    release_me();
    *mainP50 = localMainP50;
    *stageP50 = localStageP50;
    *mirrorP50 = localMirrorP50;
    th07_psp_boot_notef(
        "MEED A/B N%lu I%u MT%lu/%lu ST%lu/%lu L2MT%lu/%lu "
        "MI%lu MK%lu MW%lu EC%lu EK%lu EW%lu LM%lu MM%lu "
        "IH%08lx LH%08lx SH%08lx MH%08lx SV%d G%lu R%lu",
        (unsigned long)recordCount, ME_EDRAM_SEED_BENCH_SAMPLES,
        (unsigned long)localMainP50, (unsigned long)mainP99,
        (unsigned long)localStageP50, (unsigned long)stageP99,
        (unsigned long)localMirrorP50, (unsigned long)mirrorP99,
        (unsigned long)mainInvalidateP50,
        (unsigned long)mainKernelP50,
        (unsigned long)mainWritebackP50,
        (unsigned long)mainToLocalP50,
        (unsigned long)localKernelP50,
        (unsigned long)localWritebackP50,
        (unsigned long)localToMainP50,
        (unsigned long)mismatchWords,
        (unsigned long)inputHash, (unsigned long)localHash,
        (unsigned long)scSourceHash, (unsigned long)scMirrorHash,
        scMirrorValid,
        (unsigned long)guardFaults, (unsigned long)result);
    return commonValid;
}
#endif
#endif

int th07_psp_me_audio_mix(const Th07PspMixJob *job, short *output)
{
    if (!job || !output || job->frames == 0 || job->frames > TH07_PSP_ME_MAX_MIX_FRAMES ||
        job->inputCount > TH07_PSP_ME_MAX_MIX_INPUTS)
        return 0;
    if (__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) &&
        !__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        return dispatch_audio(job, output);
    __atomic_fetch_add(&gMeFallbacks, 1u, __ATOMIC_RELAXED);
    mix_on_sc(job, output);
    return 0;
}

int th07_psp_sc_audio_mix_into(const Th07PspMixJob *job, short *io,
                               unsigned int *limitedSamples)
{
    if (!job || !io || job->frames == 0 || job->frames > TH07_PSP_ME_MAX_MIX_FRAMES ||
        job->inputCount > TH07_PSP_ME_MAX_MIX_INPUTS)
        return 0;
    mix_on_sc_output(job, 0, 0);

    const unsigned int samples = job->frames * 2;
    const int divisor = job->mixDivisor ? (int)job->mixDivisor : 1;
    unsigned int limited = 0;
    for (unsigned int sample = 0; sample < samples; ++sample)
    {
        const int background = io[sample];
        int effect = divisor == 1 ? gScWide[sample] : gScWide[sample] / divisor;
        if (effect > 0)
        {
            const int headroom = 32767 - background;
            if (effect > headroom)
            {
                effect = headroom;
                ++limited;
            }
        }
        else if (effect < 0)
        {
            const int headroom = -32768 - background;
            if (effect < headroom)
            {
                effect = headroom;
                ++limited;
            }
        }
        io[sample] = (short)(background + effect);
    }
    if (limitedSamples)
        *limitedSamples = limited;
    return 1;
}

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
int th07_psp_me_bgm_reset(unsigned int generation)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    return dispatch_bgm(ME_CMD_BGM_RESET, 0, 0, generation, 0);
#else
    (void)generation;
    return 0;
#endif
}

int th07_psp_me_bgm_upload(const void *source, unsigned int bytes,
                           unsigned int generation, unsigned int ringOffset)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    if (!source || ((uintptr_t)source & 63u) != 0u ||
        bytes != ME_BGM_UPLOAD_BYTES || (ringOffset & 63u) != 0u ||
        ringOffset > ME_BGM_RING_BYTES || bytes > ME_BGM_RING_BYTES - ringOffset)
        return 0;
    return dispatch_bgm(ME_CMD_BGM_UPLOAD, (void *)source, bytes,
                        generation, ringOffset);
#else
    (void)source;
    (void)bytes;
    (void)generation;
    (void)ringOffset;
    return 0;
#endif
}

int th07_psp_me_bgm_fetch(void *destination, unsigned int bytes,
                          unsigned int generation, unsigned int ringOffset)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    if (!destination || ((uintptr_t)destination & 63u) != 0u ||
        bytes != ME_BGM_FETCH_BYTES || (ringOffset & 63u) != 0u ||
        ringOffset > ME_BGM_RING_BYTES || bytes > ME_BGM_RING_BYTES - ringOffset)
        return 0;
    return dispatch_bgm(ME_CMD_BGM_FETCH, destination, bytes,
                        generation, ringOffset);
#else
    (void)destination;
    (void)bytes;
    (void)generation;
    (void)ringOffset;
    return 0;
#endif
}

void th07_psp_me_bgm_commit_owned(void)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (!th07_psp_me_audio_stack_guard_ok() ||
        !__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) ||
        !gMeMailboxUncached ||
        gMeMailboxUncached->workerState != ME_WORKER_READY ||
        gMeMailboxUncached->suspendRequested != 0u)
        return;
#endif
    if (__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) &&
        !__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        __atomic_store_n(&gMeBgmOwned, 1, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_AUDIO_4M)
    // Recheck after publication so a broken power-lock/suspend race cannot
    // leave a durable ownership ACK behind.
    if (__atomic_load_n(&gMeBgmOwned, __ATOMIC_ACQUIRE) &&
        (!__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE) ||
         !__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) ||
         !gMeMailboxUncached ||
         gMeMailboxUncached->workerState != ME_WORKER_READY ||
         gMeMailboxUncached->suspendRequested != 0u ||
         gMeMailboxUncached->stackFault))
    {
        __atomic_store_n(&gMeBgmOwned, 0, __ATOMIC_RELEASE);
        poison_me();
    }
#endif
#endif
}

int th07_psp_me_bgm_is_active(void)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    return __atomic_load_n(&gMeBgmOwned, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE)
#if defined(TH07_PSP_MECC_AUDIO_4M)
           && __atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) &&
           gMeMailboxUncached &&
           gMeMailboxUncached->workerState == ME_WORKER_READY &&
           gMeMailboxUncached->suspendRequested == 0u &&
           !gMeMailboxUncached->stackFault
#endif
        ;
#else
    return 0;
#endif
}

void th07_psp_me_bgm_extent(unsigned int *base, unsigned int *bytes)
{
    unsigned int ownedBase = 0;
    unsigned int ownedBytes = 0;
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    if (__atomic_load_n(&gMeBgmOwned, __ATOMIC_ACQUIRE)
#if defined(TH07_PSP_MECC_AUDIO_4M)
        && th07_psp_me_bgm_is_active()
#endif
        )
    {
        ownedBase = ME_BGM_RING_BASE;
        ownedBytes = ME_BGM_RING_BYTES;
    }
#endif
    if (base)
        *base = ownedBase;
    if (bytes)
        *bytes = ownedBytes;
}

int th07_psp_me_audio_faulted(void)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    return __atomic_load_n(&gMeUnsafe, __ATOMIC_ACQUIRE)
#if defined(TH07_PSP_MECC_AUDIO_4M)
           || (gMeMailboxUncached && gMeMailboxUncached->stackFault) ||
           (__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) &&
            (!gMeMailboxUncached ||
             gMeMailboxUncached->suspendRequested != 0u))
#endif
        ;
#else
    return 0;
#endif
}

int th07_psp_me_audio_reset_committed(void)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    return __atomic_load_n(&gMeResetCommitted, __ATOMIC_ACQUIRE);
#else
    return 0;
#endif
}

void th07_psp_me_audio_suspend_latch(void)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
#if defined(TH07_PSP_MECC_AUDIO_4M)
    // A switch request queued under scePowerLock may be delivered from inside
    // scePowerUnlock.  Once STOP + guard confirmation cleared gMeStarted,
    // that delivery is safe and must not retroactively poison the shutdown.
    if (__atomic_load_n(&gMeResetCommitted, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE))
        poison_me();
#else
    if (__atomic_load_n(&gMeResetCommitted, __ATOMIC_ACQUIRE))
        poison_me();
#endif
#endif
}

void th07_psp_me_audio_diag_snapshot(unsigned int *jobs, unsigned int *fallbacks,
                                     unsigned int *timeouts, unsigned int *maxWaitUs)
{
    if (jobs)
        *jobs = __atomic_load_n(&gMeJobs, __ATOMIC_ACQUIRE);
    if (fallbacks)
        *fallbacks = __atomic_load_n(&gMeFallbacks, __ATOMIC_ACQUIRE);
    if (timeouts)
        *timeouts = __atomic_load_n(&gMeTimeouts, __ATOMIC_ACQUIRE);
    if (maxWaitUs)
        *maxWaitUs = __atomic_load_n(&gMeMaxWaitUs, __ATOMIC_ACQUIRE);
}
#endif

#if defined(TH07_PSP_MECC_AUDIO_4M)
#if !defined(TH07_PSP_SFX_MAIN_RAM)
static int sc_main_pointer_valid(const void *pointer, uint32_t bytes,
                                 uint32_t alignment)
{
    if (!pointer || ((uintptr_t)pointer & (alignment - 1u)) != 0u)
        return 0;
    const uint32_t physical = (uint32_t)pointer & 0x1fffffffu;
    return physical >= 0x08000000u && bytes != 0u &&
           bytes <= 0x04000000u && physical <= 0x0c000000u - bytes;
}

static int sfx_public_voice_valid(const Th07PspMeSfxVoice *voice)
{
    MeSfxVoice local;
    local.segment0Offset = voice->segment0Offset;
    local.segment0Frames = voice->segment0Frames;
    local.segment1Offset = voice->segment1Offset;
    local.segment1Frames = voice->segment1Frames;
    local.sourceFrame = voice->sourceFrame;
    local.sourceFraction = voice->sourceFraction;
    local.stepFixed = voice->stepFixed;
    local.gainQ16 = voice->gainQ16;
    return sfx_voice_valid(&local);
}

int th07_psp_me_sfx_upload(const void *source, unsigned int bytes,
                           unsigned int atlasOffset)
{
    if (!sc_main_pointer_valid(source, bytes, ME_CACHE_LINE_BYTES) ||
        bytes > ME_SFX_TRANSFER_MAX_BYTES ||
        ((bytes | atlasOffset) & (ME_CACHE_LINE_BYTES - 1u)) != 0u ||
        atlasOffset > ME_SFX_ATLAS_BYTES ||
        bytes > ME_SFX_ATLAS_BYTES - atlasOffset)
        return 0;
    return dispatch_sfx_transfer(ME_CMD_SFX_UPLOAD, (void *)source,
                                 bytes, atlasOffset, 0, 0);
}

int th07_psp_me_sfx_gather(void *destination,
                           unsigned int bytes0, unsigned int atlasOffset0,
                           unsigned int bytes1, unsigned int atlasOffset1)
{
    if (bytes0 == 0u || bytes0 > ME_SFX_TRANSFER_MAX_BYTES ||
        bytes1 > ME_SFX_TRANSFER_MAX_BYTES ||
        bytes1 > ME_SFX_TRANSFER_MAX_BYTES - bytes0)
        return 0;
    const uint32_t totalBytes = bytes0 + bytes1;
    if (!sc_main_pointer_valid(destination, totalBytes, ME_CACHE_LINE_BYTES) ||
        (totalBytes & (ME_CACHE_LINE_BYTES - 1u)) != 0u ||
        ((bytes0 | atlasOffset0 | bytes1 | atlasOffset1) &
         (sizeof(short) - 1u)) != 0u ||
        atlasOffset0 > ME_SFX_ATLAS_BYTES ||
        bytes0 > ME_SFX_ATLAS_BYTES - atlasOffset0 ||
        (bytes1 == 0u && atlasOffset1 != 0u) ||
        (bytes1 != 0u &&
         (atlasOffset1 > ME_SFX_ATLAS_BYTES ||
          bytes1 > ME_SFX_ATLAS_BYTES - atlasOffset1)))
        return 0;
    return dispatch_sfx_transfer(ME_CMD_SFX_GATHER, destination,
                                 bytes0, atlasOffset0, bytes1, atlasOffset1);
}

int th07_psp_me_sfx_mix(const Th07PspMeSfxMixJob *job, int *output)
{
    if (!job || job->frames == 0u ||
        job->frames > TH07_PSP_ME_SFX_MAX_MIX_FRAMES ||
        (job->frames & 15u) != 0u || job->voiceCount == 0u ||
        job->voiceCount > TH07_PSP_ME_SFX_MAX_VOICES)
        return 0;
    const uint32_t outputBytes = job->frames * 2u * sizeof(int);
    if (!sc_main_pointer_valid(output, outputBytes, ME_CACHE_LINE_BYTES))
        return 0;
    for (uint32_t index = 0; index < job->voiceCount; ++index)
    {
        if (!sfx_public_voice_valid(&job->voices[index]))
            return 0;
    }
    return dispatch_sfx_mix(job, output);
}

void th07_psp_me_sfx_extent(unsigned int *base, unsigned int *bytes)
{
    unsigned int ownedBase = 0;
    unsigned int ownedBytes = 0;
    if (__atomic_load_n(&gMeBgmOwned, __ATOMIC_ACQUIRE) &&
        th07_psp_me_bgm_is_active())
    {
        ownedBase = ME_SFX_ATLAS_BASE;
        ownedBytes = ME_SFX_ATLAS_BYTES;
    }
    if (base)
        *base = ownedBase;
    if (bytes)
        *bytes = ownedBytes;
}
#else
// This profile keeps every SE sample in Main RAM.  Retain fail-closed ABI
// stubs because audio4m_sfx.cpp is part of the diagnostic link, but provide no
// command that can overwrite the lower-eDRAM BGM ring.
int th07_psp_me_sfx_upload(const void *source, unsigned int bytes,
                           unsigned int atlasOffset)
{
    (void)source;
    (void)bytes;
    (void)atlasOffset;
    return 0;
}

int th07_psp_me_sfx_gather(void *destination,
                           unsigned int bytes0, unsigned int atlasOffset0,
                           unsigned int bytes1, unsigned int atlasOffset1)
{
    (void)destination;
    (void)bytes0;
    (void)atlasOffset0;
    (void)bytes1;
    (void)atlasOffset1;
    return 0;
}

int th07_psp_me_sfx_mix(const Th07PspMeSfxMixJob *job, int *output)
{
    (void)job;
    (void)output;
    return 0;
}

void th07_psp_me_sfx_extent(unsigned int *base, unsigned int *bytes)
{
    if (base)
        *base = 0;
    if (bytes)
        *bytes = 0;
}
#endif

int th07_psp_me_audio_stack_guard_ok(void)
{
    if (!gMeMailboxUncached || gMeMailboxUncached->stackFault)
        return 0;
    if (!stack_guards_match_on_sc())
    {
        gMeMailboxUncached->stackFault = 1;
        poison_me();
        return 0;
    }
    return 1;
}

int th07_psp_me_audio_power_locked(void)
{
    return __atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE);
}
#endif

#if defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)
static int selftest_sfx_4m(void)
{
    static short upload[32] __attribute__((aligned(64)));
    static short gathered[32] __attribute__((aligned(64)));
    int *const mixed = gScWide;
    Th07PspMeSfxMixJob job;

    for (uint32_t index = 0; index < 32u; ++index)
        upload[index] = (short)((int)index * 101 - 1400);
    upload[0] = 30000;
    upload[1] = -30000;
    memset(gathered, 0, sizeof(gathered));
    memset(mixed, 0x7f,
           TH07_PSP_ME_SFX_MAX_MIX_FRAMES * 2u * sizeof(*mixed));
    if (!th07_psp_me_sfx_upload(upload, sizeof(upload), 0u) ||
        !th07_psp_me_sfx_gather(gathered, 32u, 0u, 32u, 32u))
        return 0;
    if (memcmp(gathered, upload, sizeof(upload)) != 0)
        return 0;

    memset(&job, 0, sizeof(job));
    job.frames = TH07_PSP_ME_SFX_MAX_MIX_FRAMES;
    job.voiceCount = 1u;
    job.voices[0].segment0Offset = 0u;
    job.voices[0].segment0Frames = 8u;
    job.voices[0].segment1Offset = 32u;
    job.voices[0].segment1Frames = 8u;
    job.voices[0].sourceFrame = 6u;
    job.voices[0].stepFixed = 65536u;
    job.voices[0].gainQ16 = 65536u;
    if (!th07_psp_me_sfx_mix(&job, mixed))
        return 0;
    for (uint32_t frame = 0; frame < job.frames; ++frame)
    {
        int expected = 0;
        if (frame < 2u)
            expected = upload[6u + frame];
        else if (frame < 10u)
            expected = upload[16u + frame - 2u];
        if (mixed[frame * 2u] != expected ||
            mixed[frame * 2u + 1u] != expected)
            return 0;
    }

    // Two loud voices must leave ME as a wide bus.  Saturating either sample
    // here would destroy valid cancellation against an opposite-polarity BGM
    // sample before the final DAC conversion.
    memset(&job, 0, sizeof(job));
    job.frames = TH07_PSP_ME_SFX_MAX_MIX_FRAMES;
    job.voiceCount = 2u;
    for (uint32_t voice = 0; voice < job.voiceCount; ++voice)
    {
        job.voices[voice].segment0Offset = 0u;
        job.voices[voice].segment0Frames = 32u;
        job.voices[voice].stepFixed = 65536u;
        job.voices[voice].gainQ16 = 65536u;
    }
    if (!th07_psp_me_sfx_mix(&job, mixed) ||
        mixed[0] != 60000 || mixed[1] != 60000 ||
        mixed[2] != -60000 || mixed[3] != -60000)
        return 0;
    return th07_psp_me_audio_stack_guard_ok();
}
#endif

static int selftest_audio(void)
{
    static short testStereo[TH07_PSP_ME_MAX_MIX_FRAMES * 2] __attribute__((aligned(64)));
    static unsigned char testMono[TH07_PSP_ME_MAX_MIX_FRAMES] __attribute__((aligned(64)));
    short expected[TH07_PSP_ME_MAX_MIX_FRAMES * 2] __attribute__((aligned(64)));
    short actual[TH07_PSP_ME_MAX_MIX_FRAMES * 2] __attribute__((aligned(64)));
    Th07PspMixJob test;

    for (uint32_t frame = 0; frame < TH07_PSP_ME_MAX_MIX_FRAMES; ++frame)
    {
        testStereo[frame * 2] = (short)((int)(frame % 127u) * 97 - 6000);
        testStereo[frame * 2 + 1] = (short)(5000 - (int)(frame % 113u) * 83);
        testMono[frame] = (unsigned char)(frame * 37u + 11u);
    }
    memset(&test, 0, sizeof(test));
    test.frames = TH07_PSP_ME_MAX_MIX_FRAMES;
    test.inputCount = 2;
    test.mixDivisor = 1;
    test.inputs[0].samples = testStereo;
    test.inputs[0].frames = TH07_PSP_ME_MAX_MIX_FRAMES;
    test.inputs[0].channels = 2;
    test.inputs[0].stepFixed = 65536u;
    test.inputs[0].gainQ16 = 65536u;
    test.inputs[0].needsWriteback = 1;
    test.inputs[1].samples = testMono;
    test.inputs[1].frames = 700;
    test.inputs[1].destinationFrame = 200;
    test.inputs[1].channels = 1;
    test.inputs[1].sourceFrame = 3;
    test.inputs[1].sourceFraction = 0x4000u;
    test.inputs[1].stepFixed = 32768u;
    test.inputs[1].gainQ16 = 49152u;
    test.inputs[1].needsWriteback = 1;
    test.inputs[1].sampleFormat = TH07_PSP_MIX_MULAW8;
    mix_on_sc(&test, expected);
    if (!dispatch_audio(&test, actual))
        return 0;
    if (memcmp(expected, actual, sizeof(expected)) != 0)
        return 0;

    // Verify the runtime SFX-only wide path and its sign-aware saturation.
    short into[4] = {32760, -32760, 100, -100};
    short mono[2] = {1000, -1000};
    unsigned int limited = 0;
    memset(&test, 0, sizeof(test));
    test.frames = 2;
    test.inputCount = 1;
    test.mixDivisor = 1;
    test.inputs[0].samples = mono;
    test.inputs[0].frames = 2;
    test.inputs[0].channels = 1;
    test.inputs[0].stepFixed = 65536u;
    test.inputs[0].gainQ16 = 65536u;
    if (!th07_psp_sc_audio_mix_into(&test, into, &limited))
        return 0;
    return limited == 1 && into[0] == 32767 && into[1] == -31760 &&
           into[2] == -900 && into[3] == -1100;
}

static int selftest_vertices(void)
{
    typedef struct TestSource
    {
        float x, y, z, w;
        float u, v;
        uint32_t color;
    } TestSource;
    static TestSource source[4] __attribute__((aligned(64)));
    MeVertexTexColorPosition expected[4] __attribute__((aligned(64)));
    Th07PspMeVertexPack test;
    const void *actual = 0;

    memset(&test, 0, sizeof(test));
    for (uint32_t i = 0; i < 4; ++i)
    {
        source[i].x = (float)i + 0.25f;
        source[i].y = (float)i * -2.0f;
        source[i].z = 0.5f;
        source[i].w = 1.0f;
        source[i].u = (float)i / 4.0f;
        source[i].v = 1.0f - source[i].u;
        source[i].color = 0x80402010u + i;
        expected[i].u = float_bits(source[i].u);
        expected[i].v = float_bits(source[i].v);
        expected[i].color = source[i].color;
        expected[i].x = float_bits(source[i].x);
        expected[i].y = float_bits(source[i].y);
        expected[i].z = float_bits(source[i].z);
    }
    test.position = &source[0].x;
    test.texcoord = &source[0].u;
    test.diffuse = &source[0].color;
    test.positionStride = sizeof(source[0]);
    test.texcoordStride = sizeof(source[0]);
    test.diffuseStride = sizeof(source[0]);
    test.count = 4;
    test.textured = 1;
    test.colored = 1;

    if (!th07_psp_me_vertex_pack(&test, &actual))
        return 0;
    sceKernelDcacheInvalidateRange((void *)actual, sizeof(expected));
    const int matched = memcmp(actual, expected, sizeof(expected)) == 0;
    // Drop the cached aliases loaded by memcmp.  Runtime output is thereafter
    // written by ME and consumed only by GE.
    sceKernelDcacheInvalidateRange(gMeVertexArena, sizeof(gMeVertexArena));
    return matched;
}

#if defined(TH07_PSP_ME_RENDER_WORKER)
static unsigned char *me_render_bench_input(void)
{
    return gMeRenderBenchInputArea + ME_RENDER_BENCH_GUARD_BYTES;
}

static unsigned char *me_render_bench_output(void)
{
    return gMeRenderBenchOutputArea + ME_RENDER_BENCH_GUARD_BYTES;
}

static unsigned char *me_render_bench_copy(void)
{
    return gMeRenderBenchCopyArea + ME_RENDER_BENCH_GUARD_BYTES;
}

static void me_render_bench_initialize_guards(void)
{
    memset(gMeRenderBenchInputArea, ME_RENDER_BENCH_GUARD_PATTERN,
           sizeof(gMeRenderBenchInputArea));
    memset(gMeRenderBenchOutputArea, ME_RENDER_BENCH_GUARD_PATTERN,
           sizeof(gMeRenderBenchOutputArea));
    memset(gMeRenderBenchCopyArea, ME_RENDER_BENCH_GUARD_PATTERN,
           sizeof(gMeRenderBenchCopyArea));
    // Establish the guard baseline in physical Main RAM before any later
    // invalidate. The ME sees physical aliases and the final guard check
    // deliberately drops SC cache lines, so cached-only sentinels would make
    // the benchmark either false-fail or compare against stale BSS contents.
    sceKernelDcacheWritebackRange(gMeRenderBenchInputArea,
                                  sizeof(gMeRenderBenchInputArea));
    sceKernelDcacheWritebackRange(gMeRenderBenchOutputArea,
                                  sizeof(gMeRenderBenchOutputArea));
    sceKernelDcacheWritebackRange(gMeRenderBenchCopyArea,
                                  sizeof(gMeRenderBenchCopyArea));
}

static int me_render_bench_area_guard_ok(const unsigned char *area,
                                         uint32_t payloadBytes)
{
    for (uint32_t index = 0; index < ME_RENDER_BENCH_GUARD_BYTES; ++index)
    {
        if (area[index] != ME_RENDER_BENCH_GUARD_PATTERN ||
            area[ME_RENDER_BENCH_GUARD_BYTES + payloadBytes + index] !=
                ME_RENDER_BENCH_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int me_render_bench_guards_ok(void)
{
    sceKernelDcacheInvalidateRange(gMeRenderBenchInputArea,
                                  sizeof(gMeRenderBenchInputArea));
    sceKernelDcacheInvalidateRange(gMeRenderBenchOutputArea,
                                  sizeof(gMeRenderBenchOutputArea));
    sceKernelDcacheInvalidateRange(gMeRenderBenchCopyArea,
                                  sizeof(gMeRenderBenchCopyArea));
    return me_render_bench_area_guard_ok(gMeRenderBenchInputArea,
                                         ME_RENDER_BENCH_INPUT_BYTES) &&
           me_render_bench_area_guard_ok(gMeRenderBenchOutputArea,
                                         ME_RENDER_BENCH_OUTPUT_BYTES) &&
           me_render_bench_area_guard_ok(gMeRenderBenchCopyArea,
                                         ME_RENDER_BENCH_OUTPUT_BYTES);
}

static void me_render_bench_fill_input(uint32_t count, uint32_t stride)
{
    static const uint32_t sinBits[8] = {
        0x00000000u, 0x3f3504f3u, 0x3f800000u, 0x3f3504f3u,
        0x80000000u, 0xbf3504f3u, 0xbf800000u, 0xbf3504f3u};
    static const uint32_t cosBits[8] = {
        0x3f800000u, 0x3f3504f3u, 0x00000000u, 0xbf3504f3u,
        0xbf800000u, 0xbf3504f3u, 0x80000000u, 0x3f3504f3u};
    static const uint32_t boundaryBits[8] = {
        0x00000000u, 0x80000000u, 0x3f000000u, 0xbf000000u,
        0x3f7fffffu, 0x3f800001u, 0x00800000u, 0x007fffffu};
    unsigned char *const input = me_render_bench_input();
    memset(input, 0xa7, ME_RENDER_BENCH_INPUT_BYTES);
    for (uint32_t index = 0; index < count; ++index)
    {
        Th07PspMeRenderRecord32 *record =
            (Th07PspMeRenderRecord32 *)(input + index * stride);
        const float centerX = (float)((int)(index % 513u) - 64) + 0.25f;
        const float centerY = (float)((int)((index * 7u) % 641u) - 64) - 0.5f;
        const float halfWidth = (float)(1u << (index & 5u)) * 0.5f;
        const float halfHeight = (float)(1u << ((index + 2u) & 5u)) * 0.5f;
        record->centerXBits = float_bits(centerX);
        record->centerYBits = float_bits(centerY);
        record->halfWidthBits = float_bits(halfWidth);
        record->halfHeightBits = float_bits(halfHeight);
        record->sinBits = sinBits[index & 7u];
        record->cosBits = cosBits[index & 7u];
        record->color = 0x80000000u | (index * 0x00010203u);
        record->flags = (index & 1u) ? TH07_PSP_ME_RENDER_RECORD_ROTATED : 0u;

        // Seed the front of every corpus with signed zero, normal-boundary and
        // adjacent-to-one operands.  These expose FCR31/flush-mode differences
        // while the remainder resembles real TH07 positions and half-sizes.
        if (index < 8u)
        {
            record->centerXBits = boundaryBits[index];
            record->centerYBits = boundaryBits[(index + 3u) & 7u];
            record->halfWidthBits = boundaryBits[(index + 2u) & 7u];
            record->halfHeightBits = boundaryBits[(index + 5u) & 7u];
        }
    }
}

static uint32_t me_render_bench_compare(uint32_t count, uint32_t stride)
{
    const unsigned char *const input = me_render_bench_input();
    const MeVertexTexColorPosition *const actual =
        (const MeVertexTexColorPosition *)me_render_bench_copy();
    uint32_t mismatchWords = 0u;
    const uint32_t originalFcr31 = me_render_read_fcr31();
    me_render_write_fcr31(0u);
    for (uint32_t record = 0; record < count; ++record)
    {
        MeVertexTexColorPosition expected[4];
        me_render_expand_kernel(input + record * stride, stride, 1u, expected);
        const uint32_t *expectedWords = (const uint32_t *)expected;
        const uint32_t *actualWords =
            (const uint32_t *)(actual + record * 4u);
        for (uint32_t word = 0; word < 4u * 6u; ++word)
        {
            if (expectedWords[word] != actualWords[word])
                ++mismatchWords;
        }
    }
    me_render_write_fcr31(originalFcr31);
    return mismatchWords;
}

static void me_render_sort_samples(uint32_t *samples, uint32_t count)
{
    for (uint32_t index = 1u; index < count; ++index)
    {
        const uint32_t value = samples[index];
        uint32_t insert = index;
        while (insert != 0u && samples[insert - 1u] > value)
        {
            samples[insert] = samples[insert - 1u];
            --insert;
        }
        samples[insert] = value;
    }
}

static int me_render_bench_dispatch(uint32_t count, uint32_t stride,
                                    uint32_t sequence, int coldCache,
                                    Th07PspMeRenderCompletion *completion)
{
    Th07PspMeRenderJob job;
    memset(&job, 0, sizeof(job));
    job.version = TH07_PSP_ME_RENDER_VERSION;
    job.flags = coldCache ? TH07_PSP_ME_RENDER_JOB_COLD_CACHE : 0u;
    job.frameSeq = sequence;
    job.targetDrawSeq = sequence + 1u;
    job.stageEpoch = 0x4d304100u;
    job.managerEpoch = sequence ^ 0x51a7c3e9u;
    job.replayEpoch = 0x52504c59u;
    job.input = me_render_bench_input();
    job.inputBytes = count * stride;
    job.inputStride = stride;
    job.recordCount = count;
    job.output = me_render_bench_output();
    job.outputBytes = count * TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
    if (!th07_psp_me_render_begin(&job))
        return 0;

    const uint32_t startUs = sceKernelGetSystemTimeLow();
    while (th07_psp_me_render_probe(0) == 0)
    {
        if (sceKernelGetSystemTimeLow() - startUs >=
            ME_RENDER_BENCH_TIMEOUT_US)
        {
            th07_psp_me_render_hard_fault();
            return 0;
        }
        sceKernelDelayThread(20);
    }
    return th07_psp_me_render_retire(completion) == 1;
}

static int me_render_bench_throughput(uint32_t *sequence)
{
    enum { kSamples = 128 };
    uint32_t kernelCycles[kSamples];
    uint32_t roundTripUs[kSamples];
    me_render_bench_fill_input(TH07_PSP_ME_RENDER_MAX_RECORDS, 64u);
    for (uint32_t sample = 0u; sample < kSamples; ++sample)
    {
        Th07PspMeRenderCompletion completion;
        if (!me_render_bench_dispatch(TH07_PSP_ME_RENDER_MAX_RECORDS, 64u,
                                      (*sequence)++, 0, &completion) ||
            completion.result != ME_RENDER_RESULT_OK ||
            completion.outputBytes != ME_RENDER_BENCH_OUTPUT_BYTES ||
            completion.meFcr31Effective != 0u ||
            completion.meFcr31Before != completion.meFcr31After)
            return 0;
        kernelCycles[sample] = completion.meKernelCycles;
        roundTripUs[sample] = completion.dispatchWaitUs;
    }
    me_render_sort_samples(kernelCycles, kSamples);
    me_render_sort_samples(roundTripUs, kSamples);
    // ceil(0.99 * 128) - 1 = 126.
    gMeRenderBenchSummary.throughputSamples = kSamples;
    gMeRenderBenchSummary.kernelP99Cycles = kernelCycles[126];
    gMeRenderBenchSummary.roundTripP99Us = roundTripUs[126];
    return me_render_bench_guards_ok();
}

static int selftest_render_bench(void)
{
    static const uint32_t counts[5] = {0u, 128u, 512u, 768u, 1024u};
    static const uint32_t strides[3] = {32u, 48u, 64u};
    const uint32_t takeoverUs = gMeRenderBenchSummary.takeoverUs;
    const uint32_t prxBytes = gMeRenderBenchSummary.prxBytes;
    const uint32_t prxWriteUs = gMeRenderBenchSummary.prxWriteUs;
    const uint32_t prxLoadUs = gMeRenderBenchSummary.prxLoadUs;
    const int prxWriteResult = gMeRenderBenchSummary.prxWriteResult;
    const int prxLoadResult = gMeRenderBenchSummary.prxLoadResult;
    memset(&gMeRenderBenchSummary, 0, sizeof(gMeRenderBenchSummary));
    memset(gMeRenderBenchCases, 0, sizeof(gMeRenderBenchCases));
    gMeRenderBenchSummary.version = TH07_PSP_ME_RENDER_VERSION;
    gMeRenderBenchSummary.takeoverUs = takeoverUs;
    gMeRenderBenchSummary.prxBytes = prxBytes;
    gMeRenderBenchSummary.prxWriteUs = prxWriteUs;
    gMeRenderBenchSummary.prxLoadUs = prxLoadUs;
    gMeRenderBenchSummary.prxWriteResult = prxWriteResult;
    gMeRenderBenchSummary.prxLoadResult = prxLoadResult;
    gMeRenderBenchSummary.meEdramBytes = 0u;
    gMeRenderBenchSummary.scFcr31Before = me_render_read_fcr31();
    me_render_bench_initialize_guards();
    uint32_t sequence = 1u;
    uint32_t caseIndex = 0u;
    int allPassed = 1;

    for (uint32_t cacheMode = TH07_PSP_ME_RENDER_CACHE_COLD;
         cacheMode <= TH07_PSP_ME_RENDER_CACHE_WARM; ++cacheMode)
    {
        for (uint32_t strideIndex = 0; strideIndex < 3u; ++strideIndex)
        {
            const uint32_t stride = strides[strideIndex];
            for (uint32_t countIndex = 0; countIndex < 5u; ++countIndex)
            {
                const uint32_t count = counts[countIndex];
                const uint32_t outputBytes =
                    count * TH07_PSP_ME_RENDER_OUTPUT_BYTES_PER_RECORD;
                Th07PspMeRenderBenchCase *benchCase =
                    &gMeRenderBenchCases[caseIndex++];
                benchCase->recordCount = count;
                benchCase->inputStride = stride;
                benchCase->cacheMode = cacheMode;
                benchCase->outputBytes = outputBytes;
                if (outputBytes > gMeRenderBenchSummary.maxOutputBytes)
                    gMeRenderBenchSummary.maxOutputBytes = outputBytes;

                me_render_bench_fill_input(count, stride);
                memset(me_render_bench_output(), 0x39,
                       ME_RENDER_BENCH_OUTPUT_BYTES);
                memset(me_render_bench_copy(), 0xc6,
                       ME_RENDER_BENCH_OUTPUT_BYTES);

                if (cacheMode == TH07_PSP_ME_RENDER_CACHE_WARM)
                {
                    Th07PspMeRenderCompletion warmup;
                    if (!me_render_bench_dispatch(count, stride, sequence++, 0,
                                                  &warmup))
                    {
                        benchCase->result = ME_RENDER_RESULT_PROTOCOL;
                        ++gMeRenderBenchSummary.failedCases;
                        allPassed = 0;
                        goto render_bench_done;
                    }
                }
                else
                {
                    sceKernelDcacheWritebackInvalidateAll();
                }

                Th07PspMeRenderCompletion completion;
                if (!me_render_bench_dispatch(
                        count, stride, sequence++,
                        cacheMode == TH07_PSP_ME_RENDER_CACHE_COLD,
                                              &completion))
                {
                    benchCase->result = ME_RENDER_RESULT_PROTOCOL;
                    ++gMeRenderBenchSummary.failedCases;
                    allPassed = 0;
                    goto render_bench_done;
                }

                const uint32_t copyStart = sceKernelGetSystemTimeLow();
                if (outputBytes)
                    memcpy(me_render_bench_copy(), me_render_bench_output(),
                           outputBytes);
                benchCase->scCopyUs = sceKernelGetSystemTimeLow() - copyStart;
                benchCase->result = completion.result;
                benchCase->scWritebackUs = completion.scWritebackUs;
                benchCase->scOutputPrepareUs = completion.scOutputPrepareUs;
                benchCase->scSubmitUs = completion.scSubmitUs;
                benchCase->dispatchWaitUs = completion.dispatchWaitUs;
                benchCase->scInvalidateUs = completion.scInvalidateUs;
                benchCase->meInvalidateCycles = completion.meInvalidateCycles;
                benchCase->meKernelCycles = completion.meKernelCycles;
                benchCase->meWritebackCycles = completion.meWritebackCycles;
                benchCase->mismatchWords =
                    me_render_bench_compare(count, stride);
                gMeRenderBenchSummary.mismatchWords +=
                    benchCase->mismatchWords;
                if (caseIndex == 1u)
                {
                    gMeRenderBenchSummary.meFcr31Before =
                        completion.meFcr31Before;
                }
                gMeRenderBenchSummary.meFcr31After = completion.meFcr31After;

                const int guardsOk = me_render_bench_guards_ok();
                const uint32_t cpuMHz = scePowerGetCpuClockFrequency();
                const uint32_t kernelUs = cpuMHz
                    ? (uint32_t)(((uint64_t)completion.meKernelCycles * 2u) /
                                 cpuMHz)
                    : 0xffffffffu;
                const int fcrOk =
                    completion.meFcr31Before == completion.meFcr31After &&
                    completion.meFcr31Effective == 0u;
                if (completion.result == ME_RENDER_RESULT_OK &&
                    benchCase->mismatchWords == 0u && guardsOk && fcrOk &&
                    kernelUs <= 10000u)
                {
                    ++gMeRenderBenchSummary.passedCases;
                }
                else
                {
                    ++gMeRenderBenchSummary.failedCases;
                    if (!guardsOk)
                        ++gMeRenderBenchSummary.guardFaults;
                    allPassed = 0;
                }

                th07_psp_boot_notef(
                    "MERW M0A N%lu S%lu C%lu WB%lu OP%lu SU%lu DW%lu MI%lu MK%lu MO%lu SI%lu CP%lu MM%lu R%lu",
                    (unsigned long)count, (unsigned long)stride,
                    (unsigned long)cacheMode,
                    (unsigned long)benchCase->scWritebackUs,
                    (unsigned long)benchCase->scOutputPrepareUs,
                    (unsigned long)benchCase->scSubmitUs,
                    (unsigned long)benchCase->dispatchWaitUs,
                    (unsigned long)benchCase->meInvalidateCycles,
                    (unsigned long)benchCase->meKernelCycles,
                    (unsigned long)benchCase->meWritebackCycles,
                    (unsigned long)benchCase->scInvalidateUs,
                    (unsigned long)benchCase->scCopyUs,
                    (unsigned long)benchCase->mismatchWords,
                    (unsigned long)benchCase->result);
            }
        }
    }

    if (allPassed && !me_render_bench_throughput(&sequence))
    {
        ++gMeRenderBenchSummary.protocolFaults;
        allPassed = 0;
    }
    if (allPassed)
    {
        const uint32_t cpuMHz = scePowerGetCpuClockFrequency();
        const uint32_t kernelP99Us = cpuMHz
            ? (uint32_t)(((uint64_t)gMeRenderBenchSummary.kernelP99Cycles *
                          2u) /
                         cpuMHz)
            : 0xffffffffu;
        if (kernelP99Us > 10000u)
        {
            ++gMeRenderBenchSummary.performanceFaults;
            allPassed = 0;
        }
    }

render_bench_done:
    gMeRenderBenchSummary.caseCount = caseIndex;
    gMeRenderBenchSummary.scFcr31After = me_render_read_fcr31();
    if (gMeRenderBenchSummary.scFcr31After !=
        gMeRenderBenchSummary.scFcr31Before)
    {
        ++gMeRenderBenchSummary.protocolFaults;
        allPassed = 0;
    }
    gMeRenderBenchSummary.ready =
        allPassed && caseIndex == TH07_PSP_ME_RENDER_BENCH_CASES &&
        gMeRenderBenchSummary.passedCases == TH07_PSP_ME_RENDER_BENCH_CASES &&
        gMeRenderBenchSummary.failedCases == 0u &&
        gMeRenderBenchSummary.mismatchWords == 0u &&
        gMeRenderBenchSummary.timeouts == 0u &&
        gMeRenderBenchSummary.boundsFaults == 0u &&
        gMeRenderBenchSummary.guardFaults == 0u &&
        gMeRenderBenchSummary.protocolFaults == 0u &&
        gMeRenderBenchSummary.performanceFaults == 0u;
    th07_psp_boot_notef(
        "MERW M0A %s CASES%lu PASS%lu FAIL%lu MM%lu TO%lu BD%lu GD%lu PR%lu PF%lu OUT%lu KP99%lu RT99%lu EDRAM0",
        gMeRenderBenchSummary.ready ? "PASS" : "FAIL",
        (unsigned long)gMeRenderBenchSummary.caseCount,
        (unsigned long)gMeRenderBenchSummary.passedCases,
        (unsigned long)gMeRenderBenchSummary.failedCases,
        (unsigned long)gMeRenderBenchSummary.mismatchWords,
        (unsigned long)gMeRenderBenchSummary.timeouts,
        (unsigned long)gMeRenderBenchSummary.boundsFaults,
        (unsigned long)gMeRenderBenchSummary.guardFaults,
        (unsigned long)gMeRenderBenchSummary.protocolFaults,
        (unsigned long)gMeRenderBenchSummary.performanceFaults,
        (unsigned long)gMeRenderBenchSummary.maxOutputBytes,
        (unsigned long)gMeRenderBenchSummary.kernelP99Cycles,
        (unsigned long)gMeRenderBenchSummary.roundTripP99Us);
    return gMeRenderBenchSummary.ready != 0u;
}

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
static void me_render_stream_selftest_vertex(
    Th07PspMeRenderStreamVertex *vertex, float u, float v, uint32_t color,
    float x, float y, float z)
{
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    if (!me_render_stream_write_vertex(
            vertex, float_bits(u), float_bits(v), color, x, y,
            float_bits(z)))
        memset(vertex, 0xa5, sizeof(*vertex));
#else
    vertex->uBits = float_bits(u);
    vertex->vBits = float_bits(v);
    vertex->color = color;
    vertex->xBits = float_bits(x);
    vertex->yBits = float_bits(y);
    vertex->zBits = float_bits(z);
#endif
}

#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
// Independent C1 M0 oracle.  Do not call me_render_stream_write_vertex():
// this side must still catch a broken production packer.  The fixtures below
// are finite and deliberately stay well inside every selected ABI's range.
static void me_render_stream_c1_reference_vertex(
    Th07PspMeRenderStreamVertex *vertex,
#if defined(TH07_PSP_ME_RENDER_UV16)
    uint16_t uQ, uint16_t vQ,
#else
    float u, float v,
#endif
    uint32_t color,
    float x, float y, float z)
{
#if defined(TH07_PSP_ME_RENDER_UV16)
    // The staircase constructs these exact Q15 values before creating the
    // input floats.  Write them directly so the independent oracle cannot
    // perturb or depend on SC COP1 state while diagnosing the ME packer.
    vertex->u = uQ;
    vertex->v = vQ;
#else
    vertex->uBits = float_bits(u);
    vertex->vBits = float_bits(v);
#endif
    vertex->color = color;
#if defined(TH07_PSP_ME_RENDER_XYZ16)
    const float scaledX = x * 32.0f;
    const float scaledY = y * 32.0f;
    const float scaledZ = z * 32768.0f;
    vertex->x = (int16_t)(scaledX + (scaledX >= 0.0f ? 0.5f : -0.5f));
    vertex->y = (int16_t)(scaledY + (scaledY >= 0.0f ? 0.5f : -0.5f));
    vertex->z = (int16_t)(scaledZ + (scaledZ >= 0.0f ? 0.5f : -0.5f));
    vertex->reserved = 0u;
#else
    vertex->xBits = float_bits(x);
    vertex->yBits = float_bits(y);
    vertex->zBits = float_bits(z);
#endif
}

// C1 M0 keeps the production retire path free of synchronous diagnostics.
// If that path quarantines a completed candidate, the caller owns a stable,
// non-recyclable slot and can reproduce the same validation after the
// SC_TRANSITION has ended.  Keep these values aligned with the optional
// retire diagnostics ABI, with OWNERSHIP as the C1-only extension.
enum
{
    ME_RENDER_C1_DIAG_COMMAND = 1u << 0,
    ME_RENDER_C1_DIAG_RESULT = 1u << 1,
    ME_RENDER_C1_DIAG_TOKEN = 1u << 2,
    ME_RENDER_C1_DIAG_VERSION = 1u << 3,
    ME_RENDER_C1_DIAG_FLAGS = 1u << 4,
    ME_RENDER_C1_DIAG_FRAME = 1u << 5,
    ME_RENDER_C1_DIAG_TARGET = 1u << 6,
    ME_RENDER_C1_DIAG_STAGE = 1u << 7,
    ME_RENDER_C1_DIAG_MANAGER = 1u << 8,
    ME_RENDER_C1_DIAG_REPLAY = 1u << 9,
    ME_RENDER_C1_DIAG_SIGNATURE = 1u << 10,
    ME_RENDER_C1_DIAG_RECORD_COUNT = 1u << 11,
    ME_RENDER_C1_DIAG_PAYLOAD_HASH = 1u << 12,
    ME_RENDER_C1_DIAG_BUCKET = 1u << 13,
    ME_RENDER_C1_DIAG_VERTEX_COUNT = 1u << 14,
    ME_RENDER_C1_DIAG_OUTPUT_BYTES = 1u << 15,
    ME_RENDER_C1_DIAG_RUN_COUNT = 1u << 16,
    ME_RENDER_C1_DIAG_FCR_EFFECTIVE = 1u << 17,
    ME_RENDER_C1_DIAG_FCR_RESTORE = 1u << 18,
    ME_RENDER_C1_DIAG_STACK = 1u << 19,
    ME_RENDER_C1_DIAG_OUTPUT_BOUNDS = 1u << 20,
    ME_RENDER_C1_DIAG_RUN_BOUNDS = 1u << 21,
    ME_RENDER_C1_DIAG_GUARD_INPUT = 1u << 22,
    ME_RENDER_C1_DIAG_GUARD_OUTPUT = 1u << 23,
    ME_RENDER_C1_DIAG_GUARD_RUN = 1u << 24,
    ME_RENDER_C1_DIAG_OUTPUT_HASH = 1u << 25,
    ME_RENDER_C1_DIAG_RUN_HASH = 1u << 26,
    ME_RENDER_C1_DIAG_ECHO_OTHER = 1u << 27,
    ME_RENDER_C1_DIAG_OWNERSHIP = 1u << 28
};

typedef struct MeRenderC1RetireDiag
{
    uint32_t mask;
    uint32_t detail;
    uint32_t expected;
    uint32_t actual;
    uint32_t fatalMask;
} MeRenderC1RetireDiag;

// Set only by the C1 M0 caller after a completed command has been retired,
// quarantined and independently reproved free of ownership/guard corruption.
// The init path consumes it synchronously to stop Custom Core and continue as
// the canonical all-SC renderer.  Every other C1 failure remains fail-stop.
static int gMeRenderC1M0SafeFailure;

static void me_render_stream_c1_note_fault(
    MeRenderC1RetireDiag *diag, uint32_t faultBit, uint32_t detail,
    uint32_t expected, uint32_t actual)
{
    if (diag->mask == 0u)
    {
        diag->detail = detail;
        diag->expected = expected;
        diag->actual = actual;
    }
    diag->mask |= faultBit;
}

static uint32_t me_render_stream_c1_guard_fault_bytes(
    const unsigned char *bytes, uint32_t faultBit, uint32_t region,
    uint32_t *firstDetail, uint32_t *firstActual)
{
    for (uint32_t index = 0u; index < ME_RENDER_STREAM_GUARD_BYTES; ++index)
    {
        if (bytes[index] == ME_RENDER_STREAM_GUARD_PATTERN)
            continue;
        if (*firstDetail == 0xffffffffu)
        {
            *firstDetail = (region << 16) | index;
            *firstActual = bytes[index];
        }
        return faultBit;
    }
    return 0u;
}

static uint32_t me_render_stream_c1_guard_fault_mask(
    uint32_t slot, uint32_t *firstDetail, uint32_t *firstActual)
{
    *firstDetail = 0xffffffffu;
    *firstActual = 0u;
    if (slot >= TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT)
        return ME_RENDER_C1_DIAG_GUARD_INPUT |
               ME_RENDER_C1_DIAG_GUARD_OUTPUT |
               ME_RENDER_C1_DIAG_GUARD_RUN;

    MeRenderStreamInputArea *input = &gMeRenderStreamInputAreas[slot];
    MeRenderStreamOutputArea *output = &gMeRenderStreamOutputAreas[slot];
    MeRenderStreamRunArea *runs = &gMeRenderStreamRunAreas[slot];
    sceKernelDcacheInvalidateRange(input->guard0, sizeof(input->guard0));
    sceKernelDcacheInvalidateRange(input->guard1, sizeof(input->guard1));
    sceKernelDcacheInvalidateRange(output->guard0, sizeof(output->guard0));
    sceKernelDcacheInvalidateRange(output->guard1, sizeof(output->guard1));
    sceKernelDcacheInvalidateRange(runs->guard0, sizeof(runs->guard0));
    sceKernelDcacheInvalidateRange(runs->guard1, sizeof(runs->guard1));

    uint32_t mask = 0u;
    mask |= me_render_stream_c1_guard_fault_bytes(
        input->guard0, ME_RENDER_C1_DIAG_GUARD_INPUT, 0u, firstDetail,
        firstActual);
    mask |= me_render_stream_c1_guard_fault_bytes(
        input->guard1, ME_RENDER_C1_DIAG_GUARD_INPUT, 1u, firstDetail,
        firstActual);
    mask |= me_render_stream_c1_guard_fault_bytes(
        output->guard0, ME_RENDER_C1_DIAG_GUARD_OUTPUT, 2u, firstDetail,
        firstActual);
    mask |= me_render_stream_c1_guard_fault_bytes(
        output->guard1, ME_RENDER_C1_DIAG_GUARD_OUTPUT, 3u, firstDetail,
        firstActual);
    mask |= me_render_stream_c1_guard_fault_bytes(
        runs->guard0, ME_RENDER_C1_DIAG_GUARD_RUN, 4u, firstDetail,
        firstActual);
    mask |= me_render_stream_c1_guard_fault_bytes(
        runs->guard1, ME_RENDER_C1_DIAG_GUARD_RUN, 5u, firstDetail,
        firstActual);
    return mask;
}

static int me_render_stream_c1_diagnose_retire(
    const Th07PspMeRenderStreamToken *token,
    const Th07PspMeRenderStreamJob *job,
    const Th07PspMeRenderStreamCompletion *completion,
    MeRenderC1RetireDiag *diag)
{
    memset(diag, 0, sizeof(*diag));
    const uint32_t slotState =
        token && token->slot < TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT
            ? __atomic_load_n(&gMeRenderStreamSlots[token->slot].state,
                              __ATOMIC_ACQUIRE)
            : 0xffffffffu;
    const int quarantined = me_render_stream_token_matches(
        token, TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED);
    if (!quarantined)
    {
        // The worker or SC may still own the pools.  Do not inspect them.
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_OWNERSHIP, 0u,
            TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED, slotState);
        diag->fatalMask = diag->mask;
        return 0;
    }

    const uint32_t command = gMeMailboxUncached
        ? gMeMailboxUncached->command
        : 0xffffffffu;
    const uint32_t stackFault = gMeMailboxUncached
        ? gMeMailboxUncached->stackFault
        : 0xffffffffu;
    if (command != ME_CMD_NONE)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_COMMAND, 0u, ME_CMD_NONE, command);
    uint32_t ownershipActual = 0u;
    if (!gMeMailboxUncached || gMeMailboxUncached->status != ME_STAT_DONE)
        ownershipActual |= 1u << 0;
    if (!gMeMailboxUncached ||
        gMeMailboxUncached->workerState != ME_WORKER_READY)
        ownershipActual |= 1u << 1;
    if (!gMeMailboxUncached || gMeMailboxUncached->suspendRequested != 0u)
        ownershipActual |= 1u << 2;
    if (__atomic_load_n(&gMeRenderStreamInFlightSlot,
                        __ATOMIC_ACQUIRE) != 0xffffffffu)
        ownershipActual |= 1u << 3;
    if (__atomic_load_n(&gMeOwner, __ATOMIC_ACQUIRE) != ME_OWNER_NONE)
        ownershipActual |= 1u << 4;
    if (__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        ownershipActual |= 1u << 5;
    if (__atomic_load_n(&gMeUnsafe, __ATOMIC_ACQUIRE))
        ownershipActual |= 1u << 6;
    if (!__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE))
        ownershipActual |= 1u << 7;
    if (!stack_guards_match_on_sc())
        ownershipActual |= 1u << 8;
    if (ownershipActual != 0u)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_OWNERSHIP, 1u, 0u,
            ownershipActual);
    if (completion->result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_RESULT, completion->firstBadRecord,
            TH07_PSP_ME_RENDER_STREAM_RESULT_OK, completion->result);
    if (completion->token.slot != job->token.slot)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_TOKEN, 0u, job->token.slot,
            completion->token.slot);
    if (completion->token.generation != job->token.generation)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_TOKEN, 1u, job->token.generation,
            completion->token.generation);
    if (completion->version != job->version)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_VERSION, 0u, job->version,
            completion->version);
    if (completion->flags != job->flags)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_FLAGS, 0u, job->flags,
            completion->flags);
    if (completion->frameSeq != job->frameSeq)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_FRAME, 0u, job->frameSeq,
            completion->frameSeq);
    if (completion->targetDrawSeq != job->targetDrawSeq)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_TARGET, 0u, job->targetDrawSeq,
            completion->targetDrawSeq);
    if (completion->stageEpoch != job->stageEpoch)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_STAGE, 0u, job->stageEpoch,
            completion->stageEpoch);
    if (completion->managerEpoch != job->managerEpoch)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_MANAGER, 0u, job->managerEpoch,
            completion->managerEpoch);
    if (completion->replayEpoch != job->replayEpoch)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_REPLAY, 0u, job->replayEpoch,
            completion->replayEpoch);
    if (completion->globalSignature != job->globalSignature)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_SIGNATURE, 0u, job->globalSignature,
            completion->globalSignature);
    if (completion->recordCount != job->recordCount)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_RECORD_COUNT, 0u, job->recordCount,
            completion->recordCount);
    if ((job->flags &
         TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH) != 0u &&
        completion->payloadHash != job->payloadHash)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_PAYLOAD_HASH, 0u, job->payloadHash,
            completion->payloadHash);
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
    {
        if (completion->bucketEnds[bucket] != job->bucketEnds[bucket])
            me_render_stream_c1_note_fault(
                diag, ME_RENDER_C1_DIAG_BUCKET, bucket,
                job->bucketEnds[bucket], completion->bucketEnds[bucket]);
    }

    const uint32_t echoFaultBits =
        ME_RENDER_C1_DIAG_TOKEN | ME_RENDER_C1_DIAG_VERSION |
        ME_RENDER_C1_DIAG_FLAGS | ME_RENDER_C1_DIAG_FRAME |
        ME_RENDER_C1_DIAG_TARGET | ME_RENDER_C1_DIAG_STAGE |
        ME_RENDER_C1_DIAG_MANAGER | ME_RENDER_C1_DIAG_REPLAY |
        ME_RENDER_C1_DIAG_SIGNATURE | ME_RENDER_C1_DIAG_RECORD_COUNT |
        ME_RENDER_C1_DIAG_PAYLOAD_HASH | ME_RENDER_C1_DIAG_BUCKET;
    if (!me_render_stream_completion_echo_matches(completion, job) &&
        (diag->mask & echoFaultBits) == 0u)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_ECHO_OTHER, 0u, 1u, 0u);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (!me_render_stream_item_completion_valid(completion, job))
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_ECHO_OTHER, 1u, 1u, 0u);
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    if (!me_render_stream_effect_completion_valid(completion, job))
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_ECHO_OTHER, 2u, 1u, 0u);
#endif
#endif

    const uint32_t vertexCapacity =
        ME_RENDER_STREAM_POOL_MAX_VERTEX_BYTES /
        sizeof(Th07PspMeRenderStreamVertex);
    if (completion->vertexCount > vertexCapacity)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_VERTEX_COUNT, 0u, vertexCapacity,
            completion->vertexCount);
    else
    {
        const uint32_t expectedBytes =
            completion->vertexCount *
            sizeof(Th07PspMeRenderStreamVertex);
        if (completion->outputBytes != expectedBytes)
            me_render_stream_c1_note_fault(
                diag, ME_RENDER_C1_DIAG_OUTPUT_BYTES, 0u, expectedBytes,
                completion->outputBytes);
    }
    if (completion->runCount > ME_RENDER_STREAM_POOL_MAX_RUNS)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_RUN_COUNT, 0u,
            ME_RENDER_STREAM_POOL_MAX_RUNS, completion->runCount);
    if (completion->meFcr31Effective != 0u)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_FCR_EFFECTIVE, 0u, 0u,
            completion->meFcr31Effective);
    if (completion->meFcr31Before != completion->meFcr31After)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_FCR_RESTORE, 0u,
            completion->meFcr31Before, completion->meFcr31After);
    if (stackFault != 0u)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_STACK, 0u, 0u, stackFault);

    const uint32_t outputCapacity =
        sizeof(gMeRenderStreamOutputAreas[token->slot].vertices);
    const uint32_t runCapacity =
        sizeof(gMeRenderStreamRunAreas[token->slot].runs);
    uint32_t runBytes = 0u;
    const int outputExtentSafe = completion->outputBytes <= outputCapacity;
    const int runExtentSafe =
        completion->runCount <= ME_RENDER_STREAM_POOL_MAX_RUNS;
    if (!outputExtentSafe)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_OUTPUT_BOUNDS, 0u, outputCapacity,
            completion->outputBytes);
    if (runExtentSafe)
        runBytes = completion->runCount *
                   sizeof(Th07PspMeRenderStreamRun);
    if (!runExtentSafe || runBytes > runCapacity)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_RUN_BOUNDS, 0u, runCapacity,
            runExtentSafe ? runBytes : completion->runCount);

    uint32_t guardDetail = 0xffffffffu;
    uint32_t guardActual = 0u;
    const uint32_t guardMask = me_render_stream_c1_guard_fault_mask(
        token->slot, &guardDetail, &guardActual);
    if (guardMask & ME_RENDER_C1_DIAG_GUARD_INPUT)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_GUARD_INPUT, guardDetail,
            ME_RENDER_STREAM_GUARD_PATTERN, guardActual);
    if (guardMask & ME_RENDER_C1_DIAG_GUARD_OUTPUT)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_GUARD_OUTPUT, guardDetail,
            ME_RENDER_STREAM_GUARD_PATTERN, guardActual);
    if (guardMask & ME_RENDER_C1_DIAG_GUARD_RUN)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_GUARD_RUN, guardDetail,
            ME_RENDER_STREAM_GUARD_PATTERN, guardActual);

    if (completion->result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT) != 0u &&
        outputExtentSafe && runExtentSafe && runBytes <= runCapacity)
    {
        const uint32_t outputHash = me_render_stream_hash_bytes(
            gMeRenderStreamOutputAreas[token->slot].vertices,
            completion->outputBytes);
        const uint32_t runHash = me_render_stream_hash_bytes(
            gMeRenderStreamRunAreas[token->slot].runs, runBytes);
        if (completion->outputHash != outputHash)
            me_render_stream_c1_note_fault(
                diag, ME_RENDER_C1_DIAG_OUTPUT_HASH, 0u,
                completion->outputHash, outputHash);
        if (completion->runHash != runHash)
            me_render_stream_c1_note_fault(
                diag, ME_RENDER_C1_DIAG_RUN_HASH, 0u,
                completion->runHash, runHash);
    }

    if (diag->mask == 0u)
        me_render_stream_c1_note_fault(
            diag, ME_RENDER_C1_DIAG_ECHO_OTHER, 0xffffffffu, 1u, 0u);

    // Hash/shape/result faults prove C1 incorrect but are safe to disable and
    // fall back from: ME is done, the token is quarantined, and no GE reader
    // ever saw this M0 output.  A sole FCR restore mismatch is also safe for
    // this C1-only path because the caller does not resume the worker: it
    // closes admission, discards every slot, stops Custom Core, and continues
    // with the canonical all-SC renderer.  FCR-effective, identity,
    // ownership, bounds, guard and stack faults remain fatal.
    const uint32_t fatalBits =
        ME_RENDER_C1_DIAG_COMMAND | ME_RENDER_C1_DIAG_TOKEN |
        ME_RENDER_C1_DIAG_VERSION | ME_RENDER_C1_DIAG_FLAGS |
        ME_RENDER_C1_DIAG_FRAME | ME_RENDER_C1_DIAG_TARGET |
        ME_RENDER_C1_DIAG_STAGE | ME_RENDER_C1_DIAG_MANAGER |
        ME_RENDER_C1_DIAG_REPLAY | ME_RENDER_C1_DIAG_SIGNATURE |
        ME_RENDER_C1_DIAG_RECORD_COUNT |
        ME_RENDER_C1_DIAG_PAYLOAD_HASH | ME_RENDER_C1_DIAG_BUCKET |
        ME_RENDER_C1_DIAG_FCR_EFFECTIVE | ME_RENDER_C1_DIAG_STACK |
        ME_RENDER_C1_DIAG_OUTPUT_BOUNDS |
        ME_RENDER_C1_DIAG_RUN_BOUNDS |
        ME_RENDER_C1_DIAG_GUARD_INPUT |
        ME_RENDER_C1_DIAG_GUARD_OUTPUT |
        ME_RENDER_C1_DIAG_GUARD_RUN |
        ME_RENDER_C1_DIAG_ECHO_OTHER |
        ME_RENDER_C1_DIAG_OWNERSHIP;
    diag->fatalMask = diag->mask & fatalBits;
    return diag->fatalMask == 0u;
}

static int me_render_stream_c1_ready_fallback_safe(
    const Th07PspMeRenderStreamToken *token,
    const Th07PspMeRenderStreamJob *job,
    const Th07PspMeRenderStreamCompletion *completion)
{
    if (!token || !job || !completion || !gMeMailboxUncached ||
        !me_render_stream_token_matches(
            token, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC) ||
        gMeMailboxUncached->command != ME_CMD_NONE ||
        gMeMailboxUncached->status != ME_STAT_DONE ||
        gMeMailboxUncached->workerState != ME_WORKER_READY ||
        gMeMailboxUncached->suspendRequested != 0u ||
        gMeMailboxUncached->stackFault != 0u ||
        __atomic_load_n(&gMeRenderStreamInFlightSlot,
                        __ATOMIC_ACQUIRE) != 0xffffffffu ||
        __atomic_load_n(&gMeOwner, __ATOMIC_ACQUIRE) != ME_OWNER_NONE ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMeUnsafe, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) ||
        !stack_guards_match_on_sc() ||
        completion->result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        !me_render_stream_completion_echo_matches(completion, job) ||
        completion->meFcr31Effective != 0u ||
        completion->meFcr31Before != completion->meFcr31After ||
        completion->vertexCount >
            ME_RENDER_STREAM_POOL_MAX_VERTEX_BYTES /
                sizeof(Th07PspMeRenderStreamVertex) ||
        completion->outputBytes !=
            completion->vertexCount *
                sizeof(Th07PspMeRenderStreamVertex) ||
        completion->runCount > ME_RENDER_STREAM_POOL_MAX_RUNS)
        return 0;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (!me_render_stream_item_completion_valid(completion, job))
        return 0;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    if (!me_render_stream_effect_completion_valid(completion, job))
        return 0;
#endif
#endif
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    if (completion->retireFaultMask != 0u)
        return 0;
#endif
    return 1;
}

static int selftest_render_stream_c1_m0(void)
{
    // Candidate-only M0 staircase.  It measures the production command-10
    // pack/writeback path without submitting a GE command.  Runtime direct
    // jobs intentionally carry neither optional FNV flag; this M0 therefore
    // uses the same contract and proves every output/run word against the
    // independent oracle above.  Pixel/depth equality remains a separate
    // real-hardware readback gate.
    static const uint32_t counts[4] = {0u, 128u, 512u, 1024u};
    static Th07PspMeRenderStreamVertex expected[2048]
        __attribute__((aligned(64)));
    gMeRenderC1M0SafeFailure = 0;
    for (uint32_t caseIndex = 0u; caseIndex < 4u; ++caseIndex)
    {
        const uint32_t count = counts[caseIndex];
        Th07PspMeRenderStreamBuild build;
        if (!th07_psp_me_render_stream_acquire(&build))
            return 0;
        // Alternate poison makes stale, duplicated and under-written output
        // visible even when a slot is reused by a later staircase case.
        memset(gMeRenderStreamOutputAreas[build.token.slot].vertices,
               (caseIndex & 1u) ? 0xa5 : 0x5a,
               sizeof(gMeRenderStreamOutputAreas[build.token.slot].vertices));
        memset(gMeRenderStreamRunAreas[build.token.slot].runs,
               (caseIndex & 1u) ? 0x3c : 0xc3,
               sizeof(gMeRenderStreamRunAreas[build.token.slot].runs));
        memset(build.records, 0, count * sizeof(*build.records));
        for (uint32_t index = 0u; index < count; ++index)
        {
            const uint32_t u0Q = 1024u + (index * 17u) % 8192u;
            const uint32_t u1Q = u0Q + 4096u;
            const uint32_t v0Q = 2048u + (index * 29u) % 8192u;
            const uint32_t v1Q = v0Q + 4096u;
            const float u0 = (float)u0Q * (1.0f / 32768.0f);
            const float u1 = (float)u1Q * (1.0f / 32768.0f);
            const float v0 = (float)v0Q * (1.0f / 32768.0f);
            const float v1 = (float)v1Q * (1.0f / 32768.0f);
            const float posX = 48.0f + (float)(index & 63u) * 8.0f;
            const float posY = 48.0f + (float)((index >> 6u) & 15u) * 24.0f;
            const float halfWidth = 2.0f + (float)(index & 3u);
            const float halfHeight = 3.0f + (float)((index >> 2u) & 3u);
            const uint32_t sourceColor =
                0x80000000u | ((index * 0x001d3b57u) & 0x00ffffffu);
            const uint32_t guColor =
                (sourceColor & 0xff00ff00u) |
                ((sourceColor & 0x00ff0000u) >> 16) |
                ((sourceColor & 0x000000ffu) << 16);
            Th07PspMeRenderStreamRecord *record = &build.records[index];
            record->posXBits = float_bits(posX);
            record->posYBits = float_bits(posY);
            record->posZBits = float_bits(0.05f);
            record->halfWidthBits = float_bits(halfWidth);
            record->halfHeightBits = float_bits(halfHeight);
            record->sinBits = float_bits(0.0f);
            record->cosBits = float_bits(1.0f);
            record->u0Bits = float_bits(u0);
            record->u1Bits = float_bits(u1);
            record->v0Bits = float_bits(v0);
            record->v1Bits = float_bits(v1);
            record->color = sourceColor;
            record->sourceAndState = 1u | (10u << 16u);
            record->flags = TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
            record->slot = index;
            record->slotGeneration = index + 1u;

            me_render_stream_c1_reference_vertex(
                &expected[index * 2u],
#if defined(TH07_PSP_ME_RENDER_UV16)
                (uint16_t)u0Q, (uint16_t)v0Q,
#else
                u0, v0,
#endif
                guColor,
                posX - halfWidth, posY - halfHeight, 0.05f);
            me_render_stream_c1_reference_vertex(
                &expected[index * 2u + 1u],
#if defined(TH07_PSP_ME_RENDER_UV16)
                (uint16_t)u1Q, (uint16_t)v1Q,
#else
                u1, v1,
#endif
                guColor,
                posX + halfWidth, posY + halfHeight, 0.05f);
        }

        Th07PspMeRenderStreamJob job;
        memset(&job, 0, sizeof(job));
        job.token = build.token;
        job.version = TH07_PSP_ME_RENDER_STREAM_VERSION;
        job.flags = 0u;
        job.frameSeq = 0xc100u + caseIndex;
        job.targetDrawSeq = 0xc200u + caseIndex;
        job.stageEpoch = 1u;
        job.managerEpoch = 2u;
        job.replayEpoch = 3u;
        job.globalSignature = 0xc1160000u |
            TH07_PSP_ME_RENDER_STREAM_VERTEX_BYTES;
        for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
            job.bucketEnds[bucket] = count;
        job.recordCount = count;
        job.payloadHash = 0u;
        job.viewportLeftBits = float_bits(0.0f);
        job.viewportTopBits = float_bits(0.0f);
        job.viewportRightBits = float_bits(640.0f);
        job.viewportBottomBits = float_bits(480.0f);
        job.globalColor = 0xffffffffu;

        if (!th07_psp_me_render_stream_submit(&job))
        {
            (void)th07_psp_me_render_stream_cancel_build(&build.token);
            return 0;
        }
        const uint32_t startUs = sceKernelGetSystemTimeLow();
        while (th07_psp_me_render_stream_probe(&build.token, 0) == 0)
        {
            if (sceKernelGetSystemTimeLow() - startUs >=
                ME_RENDER_BENCH_TIMEOUT_US)
            {
                th07_psp_me_render_stream_hard_fault(&build.token);
                return 0;
            }
            sceKernelDelayThread(20);
        }

        Th07PspMeRenderStreamCompletion completion;
        Th07PspMeRenderStreamReady ready;
        memset(&completion, 0, sizeof(completion));
        memset(&ready, 0, sizeof(ready));
        const int retireResult = th07_psp_me_render_stream_retire(
            &build.token, &completion, &ready);
        if (retireResult != 1)
        {
            MeRenderC1RetireDiag diag;
            const int safe = me_render_stream_c1_diagnose_retire(
                &build.token, &job, &completion, &diag);
            const int quarantined = me_render_stream_token_matches(
                &build.token,
                TH07_PSP_ME_RENDER_STREAM_STATE_QUARANTINED);
            // This is the only synchronous failure report.  retire() has
            // already ended SC_TRANSITION and made the slot non-recyclable.
            th07_psp_boot_notef(
                "MERW C1M0 RETIRE-NG F%lu N%lu RR%d M%08lx D%08lx "
                "E%08lx A%08lx Z%08lx FB%08lx FA%08lx SAFE%d Q%d",
                (unsigned long)(
#if defined(TH07_PSP_ME_RENDER_UV16)
                    1u |
#endif
#if defined(TH07_PSP_ME_RENDER_XYZ16)
                    2u |
#endif
                    0u),
                (unsigned long)count, retireResult,
                (unsigned long)diag.mask,
                (unsigned long)diag.detail,
                (unsigned long)diag.expected,
                (unsigned long)diag.actual,
                (unsigned long)diag.fatalMask,
                (unsigned long)completion.meFcr31Before,
                (unsigned long)completion.meFcr31After,
                safe, quarantined);
            if (safe)
                gMeRenderC1M0SafeFailure = 1;
            return 0;
        }
        Th07PspMeRenderStreamRun expectedRun;
        memset(&expectedRun, 0, sizeof(expectedRun));
        if (count)
        {
            expectedRun.recordCount = count;
            expectedRun.vertexCount = count * 2u;
            expectedRun.primitive =
                TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES;
            expectedRun.sourceFileIndex = 1u;
            expectedRun.logicalState = 10u;
        }
        Th07PspMeRenderStreamMismatch mismatch;
        const uint32_t expectedBytes =
            count * 2u * sizeof(Th07PspMeRenderStreamVertex);
        const uint32_t expectedRunCount = count ? 1u : 0u;
        const int shapeValid =
            completion.result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
            completion.vertexCount == count * 2u &&
            completion.outputBytes == expectedBytes &&
            completion.runCount == expectedRunCount &&
            ready.vertexBytes == expectedBytes &&
            ready.runCount == expectedRunCount;
        const int compareResult = shapeValid
            ? th07_psp_me_render_stream_compare(
                &build.token, expected, expectedBytes,
                count ? &expectedRun : (const Th07PspMeRenderStreamRun *)0,
                expectedRunCount, &mismatch)
            : -1;
        const int compared = compareResult == 1;
        // The token still owns READY_SC here.  Validate its guards while that
        // ownership is stable, then release it before the synchronous boot
        // note can enter Memory Stick I/O.  The former order logged first and
        // reread guards after publishing FREE, so its N=0 hardware failure
        // could not even distinguish release from an ownership-invalid guard
        // read before the following 128-record case.
        const int readyOwned = me_render_stream_token_matches(
            &build.token, TH07_PSP_ME_RENDER_STREAM_STATE_READY_SC);
        const int guardsValid = readyOwned &&
            me_render_stream_guards_match_on_sc(build.token.slot);
        const int exactMismatch = shapeValid && compareResult == 0 &&
            (mismatch.kind == TH07_PSP_ME_RENDER_STREAM_MISMATCH_VERTEX ||
             mismatch.kind == TH07_PSP_ME_RENDER_STREAM_MISMATCH_RUN);
        const int safeExactMismatch = exactMismatch && guardsValid &&
            me_render_stream_c1_ready_fallback_safe(
                &build.token, &job, &completion);
        const int released = (compared || safeExactMismatch) && guardsValid &&
            th07_psp_me_render_stream_release_ready(&build.token);
        if (safeExactMismatch && released)
            gMeRenderC1M0SafeFailure = 1;
        th07_psp_boot_notef(
            "MERW C1M0 F%lu N%lu VB%lu H0 MI%lu MK%lu MW%lu "
            "OK%d Q%d G%d R%d X%d S%d",
            (unsigned long)(
#if defined(TH07_PSP_ME_RENDER_UV16)
                1u |
#endif
#if defined(TH07_PSP_ME_RENDER_XYZ16)
                2u |
#endif
                0u),
            (unsigned long)count, (unsigned long)expectedBytes,
            (unsigned long)completion.meInvalidateCycles,
            (unsigned long)completion.meKernelCycles,
            (unsigned long)completion.meWritebackCycles, compared,
            readyOwned, guardsValid, released, exactMismatch,
            safeExactMismatch && released);
        if (!compared || !readyOwned || !guardsValid || !released)
            return 0;
    }

    // Exercise the real publish/worker/retire fail-closed contract, not just
    // the scalar helper.  UV's +2 endpoint quantizes to unsigned 65536 and
    // XYZ's +1024 screen coordinate quantizes to signed +32768; both must
    // reject rather than saturate.  The ME
    // may have touched a prefix before detecting the second vertex, so READY
    // must expose no vertex or run from this command.
    Th07PspMeRenderStreamBuild rejectBuild;
    if (!th07_psp_me_render_stream_acquire(&rejectBuild))
        return 0;
    Th07PspMeRenderStreamRecord *rejectRecord = &rejectBuild.records[0];
    memset(rejectRecord, 0, sizeof(*rejectRecord));
#if defined(TH07_PSP_ME_RENDER_XYZ16)
    rejectRecord->posXBits = float_bits(1027.5f);
    rejectRecord->halfWidthBits = float_bits(4.0f);
#else
    rejectRecord->posXBits = float_bits(100.0f);
    rejectRecord->halfWidthBits = float_bits(4.0f);
#endif
    rejectRecord->posYBits = float_bits(100.0f);
    rejectRecord->posZBits = float_bits(0.05f);
    rejectRecord->halfHeightBits = float_bits(6.0f);
    rejectRecord->sinBits = float_bits(0.0f);
    rejectRecord->cosBits = float_bits(1.0f);
    rejectRecord->u0Bits = float_bits(0.25f);
#if defined(TH07_PSP_ME_RENDER_UV16)
    rejectRecord->u1Bits = float_bits(2.0f);
#else
    rejectRecord->u1Bits = float_bits(0.75f);
#endif
    rejectRecord->v0Bits = float_bits(0.125f);
    rejectRecord->v1Bits = float_bits(0.875f);
    rejectRecord->color = 0x80604020u;
    rejectRecord->sourceAndState = 1u | (10u << 16u);
    rejectRecord->flags = TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
    rejectRecord->slotGeneration = 1u;

    Th07PspMeRenderStreamJob rejectJob;
    memset(&rejectJob, 0, sizeof(rejectJob));
    rejectJob.token = rejectBuild.token;
    rejectJob.version = TH07_PSP_ME_RENDER_STREAM_VERSION;
    rejectJob.flags = TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH |
                      TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
    rejectJob.frameSeq = 0xc1ffu;
    rejectJob.targetDrawSeq = 0xc2ffu;
    rejectJob.stageEpoch = 1u;
    rejectJob.managerEpoch = 2u;
    rejectJob.replayEpoch = 3u;
    rejectJob.globalSignature = 0xc116ffffu;
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        rejectJob.bucketEnds[bucket] = 1u;
    rejectJob.recordCount = 1u;
    rejectJob.payloadHash = th07_psp_me_render_stream_hash(
        rejectBuild.records, sizeof(*rejectRecord));
    rejectJob.viewportLeftBits = float_bits(-2048.0f);
    rejectJob.viewportTopBits = float_bits(-2048.0f);
    rejectJob.viewportRightBits = float_bits(2048.0f);
    rejectJob.viewportBottomBits = float_bits(2048.0f);
    rejectJob.globalColor = 0xffffffffu;
    if (!th07_psp_me_render_stream_submit(&rejectJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&rejectBuild.token);
        return 0;
    }
    const uint32_t rejectStartUs = sceKernelGetSystemTimeLow();
    while (th07_psp_me_render_stream_probe(&rejectBuild.token, 0) == 0)
    {
        if (sceKernelGetSystemTimeLow() - rejectStartUs >=
            ME_RENDER_BENCH_TIMEOUT_US)
        {
            th07_psp_me_render_stream_hard_fault(&rejectBuild.token);
            return 0;
        }
        sceKernelDelayThread(20);
    }
    Th07PspMeRenderStreamCompletion rejectCompletion;
    Th07PspMeRenderStreamReady rejectReady;
    if (th07_psp_me_render_stream_retire(
            &rejectBuild.token, &rejectCompletion, &rejectReady) != 1 ||
        rejectCompletion.result !=
            TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD ||
        rejectCompletion.firstBadRecord != 0u ||
        rejectCompletion.outputBytes != 0u ||
        rejectCompletion.vertexCount != 0u ||
        rejectCompletion.runCount != 0u ||
        rejectReady.vertices != 0 || rejectReady.vertexBytes != 0u ||
        rejectReady.runs != 0 || rejectReady.runCount != 0u ||
        th07_psp_me_render_stream_mark_ge_in_flight(&rejectBuild.token) ||
        !me_render_stream_guards_match_on_sc(rejectBuild.token.slot) ||
        !th07_psp_me_render_stream_release_ready(&rejectBuild.token))
        return 0;
    th07_psp_boot_note("MERW C1M0 RANGE-REJECT PASS");
    th07_psp_boot_note("MERW C1M0 PASS CASES4 GE0 READBACK-PENDING");
    return 1;
}
#endif

#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
static void me_render_raw_selftest_store_u32(unsigned char *base,
                                             uint32_t offset,
                                             uint32_t value)
{
    memcpy(base + offset, &value, sizeof(value));
}

#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
static void me_render_list_selftest_store_u16(unsigned char *base,
                                              uint32_t offset,
                                              uint16_t value)
{
    memcpy(base + offset, &value, sizeof(value));
}
#endif

static int me_render_raw_selftest_wait(
    const Th07PspMeRenderStreamToken *token,
    Th07PspMeRenderStreamCompletion *completion,
    Th07PspMeRenderStreamReady *ready)
{
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    for (;;)
    {
        const int probe = th07_psp_me_render_stream_probe(token, 0);
        if (probe > 0)
            return th07_psp_me_render_stream_retire(
                       token, completion, ready) == 1;
        if (probe < 0)
            return 0;
        if (sceKernelGetSystemTimeLow() - startUs >=
            ME_RENDER_BENCH_TIMEOUT_US)
        {
            th07_psp_me_render_stream_hard_fault(token);
            return 0;
        }
        sceKernelDelayThread(20);
    }
}

static void me_render_raw_selftest_job_common(
    Th07PspMeRenderStreamJob *job,
    const Th07PspMeRenderStreamToken *token)
{
    memset(job, 0, sizeof(*job));
    job->token = *token;
    job->frameSeq = 0x4e40u;
    job->targetDrawSeq = 0x4e41u;
    job->stageEpoch = 0x41u;
    job->managerEpoch = 0x42u;
    job->replayEpoch = 0x43u;
    job->globalSignature = 0x4d453134u;
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        job->bucketEnds[bucket] = 1u;
    job->recordCount = 1u;
    job->offsetXBits = float_bits(10.0f);
    job->offsetYBits = float_bits(20.0f);
    job->viewportLeftBits = float_bits(0.0f);
    job->viewportTopBits = float_bits(0.0f);
    job->viewportRightBits = float_bits(640.0f);
    job->viewportBottomBits = float_bits(480.0f);
    job->globalColor = 0xffffffffu;
}

static void me_render_raw_selftest_layout(
    Th07PspMeRenderRawLayout *layout)
{
    memset(layout, 0, sizeof(*layout));
    layout->rawLayoutVersion = ME_RENDER_RAW_LAYOUT_SELFTEST_VERSION;
    layout->rawRecordBytes = TH07_PSP_ME_RENDER_STREAM_RAW_RECORD_BYTES;
    layout->bulletBasePhys =
        (uint32_t)gMeRenderRawSelftestBullet & 0x1fffffffu;
    layout->bulletStride = ME_RENDER_RAW_BULLET_STRIDE;
    layout->bulletCount = 1u;
    layout->spriteBasePhys =
        (uint32_t)gMeRenderRawSelftestSprite & 0x1fffffffu;
    layout->spriteStride = ME_RENDER_RAW_SPRITE_BYTES;
    layout->spriteCount = 1u;
    layout->representativePhys =
        (uint32_t)gMeRenderRawSelftestRepresentatives & 0x1fffffffu;
    layout->representativeStride = sizeof(uint16_t);
    layout->representativeCount =
        TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT;
    layout->vmBytes = ME_RENDER_RAW_VM_BYTES;
    layout->vmRotationZOffset = ME_RENDER_RAW_VM_ROTATION_Z_OFFSET;
    layout->vmScaleXOffset = ME_RENDER_RAW_VM_SCALE_X_OFFSET;
    layout->vmScaleYOffset = ME_RENDER_RAW_VM_SCALE_Y_OFFSET;
    layout->vmUvScrollXOffset = ME_RENDER_RAW_VM_UV_SCROLL_X_OFFSET;
    layout->vmUvScrollYOffset = ME_RENDER_RAW_VM_UV_SCROLL_Y_OFFSET;
    layout->vmColorOffset = ME_RENDER_RAW_VM_COLOR_OFFSET;
    layout->vmColor2Offset = ME_RENDER_RAW_VM_COLOR2_OFFSET;
    layout->vmFlagsOffset = ME_RENDER_RAW_VM_FLAGS_OFFSET;
    layout->vmSpriteOffset = ME_RENDER_RAW_VM_SPRITE_OFFSET;
    layout->spriteBytes = ME_RENDER_RAW_SPRITE_BYTES;
    layout->spriteSourceOffset = ME_RENDER_RAW_SPRITE_SOURCE_OFFSET;
    layout->spriteUvStartXOffset = ME_RENDER_RAW_SPRITE_UV_START_X_OFFSET;
    layout->spriteUvStartYOffset = ME_RENDER_RAW_SPRITE_UV_START_Y_OFFSET;
    layout->spriteUvEndXOffset = ME_RENDER_RAW_SPRITE_UV_END_X_OFFSET;
    layout->spriteUvEndYOffset = ME_RENDER_RAW_SPRITE_UV_END_Y_OFFSET;
    layout->spriteHeightOffset = ME_RENDER_RAW_SPRITE_HEIGHT_OFFSET;
    layout->spriteWidthOffset = ME_RENDER_RAW_SPRITE_WIDTH_OFFSET;
}

static int selftest_render_stream_raw_live(void)
{
    memset(gMeRenderRawSelftestBullet, 0,
           sizeof(gMeRenderRawSelftestBullet));
    memset(gMeRenderRawSelftestSprite, 0,
           sizeof(gMeRenderRawSelftestSprite));
    for (uint32_t source = 0u;
         source < TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT; ++source)
        gMeRenderRawSelftestRepresentatives[source] = (uint16_t)source;
    gMeRenderRawSelftestRepresentatives[7] = 3u;

    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_ROTATION_Z_OFFSET,
        float_bits(0.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_SCALE_X_OFFSET,
        float_bits(2.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_SCALE_Y_OFFSET,
        float_bits(1.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_UV_SCROLL_X_OFFSET,
        float_bits(0.25f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_UV_SCROLL_Y_OFFSET,
        float_bits(0.125f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_COLOR_OFFSET,
        0x80442211u);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_COLOR2_OFFSET,
        0x80604020u);
    const uint32_t vmFlags =
        ME_RENDER_RAW_VM_VISIBLE | ME_RENDER_RAW_VM_ACTIVE |
        ME_RENDER_RAW_VM_BLEND_ADD | ME_RENDER_RAW_VM_ZWRITE_DISABLE |
        ME_RENDER_RAW_VM_USE_COLOR2 |
        (1u << ME_RENDER_RAW_VM_ANCHOR_SHIFT);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_FLAGS_OFFSET, vmFlags);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_SPRITE_OFFSET,
        (uint32_t)gMeRenderRawSelftestSprite);

    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_SOURCE_OFFSET, 7u);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_UV_START_X_OFFSET,
        float_bits(0.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_UV_START_Y_OFFSET,
        float_bits(0.25f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_UV_END_X_OFFSET,
        float_bits(0.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_UV_END_Y_OFFSET,
        float_bits(0.75f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_HEIGHT_OFFSET,
        float_bits(8.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_WIDTH_OFFSET,
        float_bits(8.0f));

    Th07PspMeRenderStreamBuild semanticBuild;
    if (!th07_psp_me_render_stream_acquire(&semanticBuild))
        return 0;
    Th07PspMeRenderStreamRecord *semantic = semanticBuild.records;
    memset(semantic, 0, sizeof(*semantic));
    semantic->posXBits = float_bits(100.0f);
    semantic->posYBits = float_bits(100.0f);
    semantic->posZBits = float_bits(0.05f);
    semantic->halfWidthBits = float_bits(8.0f);
    semantic->halfHeightBits = float_bits(6.0f);
    semantic->sinBits = float_bits(0.5f);
    semantic->cosBits = float_bits(0.5f);
    semantic->u0Bits = float_bits(0.25f);
    semantic->u1Bits = float_bits(0.75f);
    semantic->v0Bits = float_bits(0.375f);
    semantic->v1Bits = float_bits(0.875f);
    semantic->color = 0x80604020u;
    semantic->sourceAndState = 3u | (1u << 16u);
    semantic->flags = TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE |
                      TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED |
                      TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD |
                      TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE |
                      (1u << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT);
    semantic->slot = 0u;
    semantic->slotGeneration = 9u;

    Th07PspMeRenderStreamJob semanticJob;
    me_render_raw_selftest_job_common(&semanticJob, &semanticBuild.token);
    semanticJob.version = TH07_PSP_ME_RENDER_STREAM_VERSION;
    semanticJob.flags = TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH |
                        TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
    semanticJob.payloadHash = th07_psp_me_render_stream_hash(
        semantic, sizeof(*semantic));
    if (!th07_psp_me_render_stream_submit(&semanticJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&semanticBuild.token);
        return 0;
    }

    Th07PspMeRenderStreamCompletion semanticCompletion;
    Th07PspMeRenderStreamReady semanticReady;
    if (!me_render_raw_selftest_wait(
            &semanticBuild.token, &semanticCompletion, &semanticReady) ||
        semanticCompletion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        semanticCompletion.outputBytes > 4u *
            sizeof(Th07PspMeRenderStreamVertex) ||
        semanticCompletion.runCount != 1u)
        return 0;
    Th07PspMeRenderStreamVertex expectedVertices[4]
        __attribute__((aligned(64)));
    Th07PspMeRenderStreamRun expectedRun __attribute__((aligned(64)));
    const uint32_t expectedVertexBytes = semanticCompletion.outputBytes;
    memcpy(expectedVertices, semanticReady.vertices, expectedVertexBytes);
    memcpy(&expectedRun, semanticReady.runs, sizeof(expectedRun));
    if (!th07_psp_me_render_stream_release_ready(&semanticBuild.token))
        return 0;

    Th07PspMeRenderStreamBuild rawBuild;
    if (!th07_psp_me_render_stream_acquire(&rawBuild))
        return 0;
    Th07PspMeRenderRawRecord *raw =
        (Th07PspMeRenderRawRecord *)rawBuild.records;
    memset(raw, 0, sizeof(*raw));
    raw->posXBits = float_bits(100.0f);
    raw->posYBits = float_bits(100.0f);
    raw->sinBits = float_bits(0.5f);
    raw->cosBits = float_bits(0.5f);
    raw->vmPhys =
        (uint32_t)gMeRenderRawSelftestBullet & 0x1fffffffu;
    raw->logicalState = 1u;
    raw->slot = 0u;
    raw->generation = 9u;

    Th07PspMeRenderStreamJob rawJob;
    me_render_raw_selftest_job_common(&rawJob, &rawBuild.token);
    rawJob.version = TH07_PSP_ME_RENDER_STREAM_RAW_VERSION;
    rawJob.flags = TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE |
                   TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH |
                   TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
    rawJob.payloadHash = th07_psp_me_render_stream_hash(raw, sizeof(*raw));
    me_render_raw_selftest_layout(&rawJob.rawLayout);
    if (!th07_psp_me_render_stream_submit(&rawJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&rawBuild.token);
        return 0;
    }

    Th07PspMeRenderStreamCompletion rawCompletion;
    Th07PspMeRenderStreamReady rawReady;
    if (!me_render_raw_selftest_wait(
            &rawBuild.token, &rawCompletion, &rawReady) ||
        rawCompletion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        rawCompletion.outputBytes != expectedVertexBytes ||
        rawCompletion.runCount != 1u ||
        rawReady.vertexBytes != expectedVertexBytes ||
        rawReady.runCount != 1u ||
        memcmp(rawReady.vertices, expectedVertices,
               expectedVertexBytes) != 0 ||
        memcmp(rawReady.runs, &expectedRun, sizeof(expectedRun)) != 0 ||
        !th07_psp_me_render_stream_release_ready(&rawBuild.token))
        return 0;

    // A live source race is a per-frame canonical fallback, not a permanent
    // worker fault.  Exercise the actual ME RECORD result and recyclable token.
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_SOURCE_OFFSET,
        TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT);
    Th07PspMeRenderStreamBuild rejectBuild;
    if (!th07_psp_me_render_stream_acquire(&rejectBuild))
        return 0;
    memcpy(rejectBuild.records, raw, sizeof(*raw));
    Th07PspMeRenderStreamJob rejectJob = rawJob;
    rejectJob.token = rejectBuild.token;
    rejectJob.frameSeq++;
    rejectJob.targetDrawSeq++;
    if (!th07_psp_me_render_stream_submit(&rejectJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&rejectBuild.token);
        return 0;
    }
    Th07PspMeRenderStreamCompletion rejectCompletion;
    Th07PspMeRenderStreamReady rejectReady;
    if (!me_render_raw_selftest_wait(
            &rejectBuild.token, &rejectCompletion, &rejectReady) ||
        rejectCompletion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD ||
        rejectCompletion.firstBadRecord != 0u ||
        rejectCompletion.outputBytes != 0u ||
        rejectCompletion.vertexCount != 0u || rejectCompletion.runCount != 0u ||
        rejectReady.vertices != 0 || rejectReady.vertexBytes != 0u ||
        rejectReady.runs != 0 || rejectReady.runCount != 0u ||
        th07_psp_me_render_stream_mark_ge_in_flight(&rejectBuild.token) ||
        !th07_psp_me_render_stream_release_ready(&rejectBuild.token))
        return 0;

    return 1;
}

#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
static void me_render_list_selftest_layout(
    Th07PspMeRenderListLayout *layout)
{
    memset(layout, 0, sizeof(*layout));
    layout->listLayoutVersion = ME_RENDER_LIST_LAYOUT_SELFTEST_VERSION;
    layout->listLayoutBytes = sizeof(*layout);
    layout->bulletBasePhys =
        (uint32_t)gMeRenderRawSelftestBullet & 0x1fffffffu;
    layout->bulletStride = ME_RENDER_RAW_BULLET_STRIDE;
    layout->bulletCount = 1u;
    layout->generationBasePhys =
        (uint32_t)gMeRenderListSelftestGeneration & 0x1fffffffu;
    layout->generationStride = ME_RENDER_LIST_GENERATION_STRIDE;
    layout->generationCount = 1u;
    layout->activeBitsPhys =
        (uint32_t)gMeRenderListSelftestActiveBits & 0x1fffffffu;
    layout->activeBitsWordCount = 1u;
    layout->bucketHeadPhys[0] = layout->bulletBasePhys;
    layout->bulletNextOffset = ME_RENDER_LIST_BULLET_NEXT_OFFSET;
    layout->bulletStateOffset = ME_RENDER_LIST_BULLET_STATE_OFFSET;
    layout->bulletCollisionTypeOffset =
        ME_RENDER_LIST_BULLET_COLLISION_TYPE_OFFSET;
    layout->bulletPosXOffset = ME_RENDER_LIST_BULLET_POS_X_OFFSET;
    layout->bulletPosYOffset = ME_RENDER_LIST_BULLET_POS_Y_OFFSET;
    layout->bulletRenderAngleOffset =
        ME_RENDER_LIST_BULLET_RENDER_ANGLE_OFFSET;
    layout->bulletSinOffset = ME_RENDER_LIST_BULLET_SIN_OFFSET;
    layout->bulletCosOffset = ME_RENDER_LIST_BULLET_COS_OFFSET;
    layout->bulletRotationValidOffset =
        ME_RENDER_LIST_BULLET_ROTATION_VALID_OFFSET;
    for (uint32_t state = 0u; state < 5u; ++state)
        layout->bulletVmOffsets[state] = state * ME_RENDER_RAW_VM_BYTES;
    layout->arcadeLeftBits = float_bits(10.0f);
    layout->arcadeTopBits = float_bits(20.0f);
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
static void me_item_diag_begin(void)
{
    __atomic_store_n(&gMeItemDiagState, TH07_PSP_ME_ITEM_STATE_TESTING,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagReason, TH07_PSP_ME_ITEM_REASON_NONE,
                     __ATOMIC_RELEASE);
    __atomic_add_fetch(&gMeItemDiagSelftestRuns, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&gMeItemDiagLastWaitResult, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastStreamResult, 0xffffffffu,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastItemResult, 0xffffffffu,
                     __ATOMIC_RELEASE);
}

static int me_item_diag_fail(
    unsigned int reason, int waitResult,
    const Th07PspMeRenderStreamCompletion *completion)
{
    __atomic_store_n(&gMeItemDiagLastWaitResult, waitResult,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastStreamResult,
                     completion ? completion->result : 0xffffffffu,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastItemResult,
                     completion ? completion->itemResult : 0xffffffffu,
                     __ATOMIC_RELEASE);
    __atomic_add_fetch(&gMeItemDiagSelftestFailures, 1u,
                       __ATOMIC_ACQ_REL);
    __atomic_store_n(&gMeItemDiagReason, reason, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagState, TH07_PSP_ME_ITEM_STATE_FAILED,
                     __ATOMIC_RELEASE);
    return 0;
}

static void me_item_diag_pass(void)
{
    // Do not leave the expected RECORD result from the reject probe looking
    // like a runtime failure.  The decision packet reports the normalized
    // final contract: the complete three-part Item selftest passed.
    __atomic_store_n(&gMeItemDiagLastWaitResult, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastStreamResult,
                     TH07_PSP_ME_RENDER_STREAM_RESULT_OK,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastItemResult,
                     TH07_PSP_ME_RENDER_STREAM_RESULT_OK,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagReason,
                     TH07_PSP_ME_ITEM_REASON_SELFTEST_PASS,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagState, TH07_PSP_ME_ITEM_STATE_ENABLED,
                     __ATOMIC_RELEASE);
}

static void me_render_item_selftest_layout(
    Th07PspMeRenderItemLayout *layout)
{
    memset(layout, 0, sizeof(*layout));
    layout->itemLayoutVersion = ME_RENDER_ITEM_LAYOUT_SELFTEST_VERSION;
    layout->itemLayoutBytes = sizeof(*layout);
    layout->itemBasePhys =
        (uint32_t)gMeRenderItemSelftestItem & 0x1fffffffu;
    layout->itemStride = ME_RENDER_ITEM_STRIDE;
    layout->itemCount = 2u;
    layout->generationBasePhys =
        (uint32_t)gMeRenderItemSelftestGeneration & 0x1fffffffu;
    layout->generationStride = ME_RENDER_ITEM_GENERATION_STRIDE;
    layout->generationCount = 2u;
    layout->activeBitsPhys =
        (uint32_t)gMeRenderItemSelftestActiveBits & 0x1fffffffu;
    layout->activeBitsWordCount = 1u;
    layout->sinBasePhys =
        (uint32_t)gMeRenderItemSelftestSin & 0x1fffffffu;
    layout->sinStride = sizeof(uint32_t);
    layout->cosBasePhys =
        (uint32_t)gMeRenderItemSelftestCos & 0x1fffffffu;
    layout->cosStride = sizeof(uint32_t);
    layout->headPhys = layout->itemBasePhys;
    layout->tailPhys = layout->itemBasePhys;
    layout->itemNextOffset = ME_RENDER_ITEM_NEXT_OFFSET;
    layout->itemInUseOffset = ME_RENDER_ITEM_IN_USE_OFFSET;
    layout->itemTypeOffset = ME_RENDER_ITEM_TYPE_OFFSET;
    layout->itemVmOffset = ME_RENDER_ITEM_VM_OFFSET;
    layout->vmPosXOffset = ME_RENDER_ITEM_VM_POS_X_OFFSET;
    layout->vmPosYOffset = ME_RENDER_ITEM_VM_POS_Y_OFFSET;
    layout->vmPosZOffset = ME_RENDER_ITEM_VM_POS_Z_OFFSET;
    layout->prepareSerialPhys =
        (uint32_t)&gMeRenderItemSelftestPrepareSerial & 0x1fffffffu;
    layout->preparedSerialPhys =
        (uint32_t)&gMeRenderItemSelftestPreparedSerial & 0x1fffffffu;
    layout->preparedCountPhys =
        (uint32_t)&gMeRenderItemSelftestPreparedCount & 0x1fffffffu;
    layout->expectedPrepareSerial = gMeRenderItemSelftestPrepareSerial;
    layout->expectedItemCount = 1u;
    layout->expectedTotalCount = 2u;
    layout->suffixHeadPhys =
        layout->itemBasePhys + ME_RENDER_ITEM_STRIDE;
}
#endif

static int selftest_render_stream_direct_list(void)
{
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    gMeItemSelftestInProgress = 0u;
#endif
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM DIRECT BEGIN");
#endif
    memset(gMeRenderRawSelftestBullet, 0,
           sizeof(gMeRenderRawSelftestBullet));
    memset(gMeRenderRawSelftestSprite, 0,
           sizeof(gMeRenderRawSelftestSprite));
    for (uint32_t source = 0u;
         source < TH07_PSP_ME_RENDER_RAW_REPRESENTATIVE_COUNT; ++source)
        gMeRenderRawSelftestRepresentatives[source] = (uint16_t)source;
    gMeRenderRawSelftestRepresentatives[7] = 3u;
    gMeRenderListSelftestGeneration[0] = 9u;
    gMeRenderListSelftestActiveBits[0] = 1u;

    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_ROTATION_Z_OFFSET,
        float_bits(0.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_SCALE_X_OFFSET,
        float_bits(2.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_SCALE_Y_OFFSET,
        float_bits(1.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_UV_SCROLL_X_OFFSET,
        float_bits(0.25f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_UV_SCROLL_Y_OFFSET,
        float_bits(0.125f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_COLOR_OFFSET,
        0x80442211u);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_COLOR2_OFFSET,
        0x80604020u);
    const uint32_t vmFlags =
        ME_RENDER_RAW_VM_VISIBLE | ME_RENDER_RAW_VM_ACTIVE |
        ME_RENDER_RAW_VM_BLEND_ADD | ME_RENDER_RAW_VM_ZWRITE_DISABLE |
        ME_RENDER_RAW_VM_USE_COLOR2 |
        (1u << ME_RENDER_RAW_VM_ANCHOR_SHIFT);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_FLAGS_OFFSET, vmFlags);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_RAW_VM_SPRITE_OFFSET,
        (uint32_t)gMeRenderRawSelftestSprite);

    gMeRenderRawSelftestBullet[ME_RENDER_LIST_BULLET_COLLISION_TYPE_OFFSET] =
        0u;
    me_render_list_selftest_store_u16(
        gMeRenderRawSelftestBullet, ME_RENDER_LIST_BULLET_STATE_OFFSET, 1u);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_LIST_BULLET_POS_X_OFFSET,
        float_bits(90.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_LIST_BULLET_POS_Y_OFFSET,
        float_bits(80.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_LIST_BULLET_NEXT_OFFSET, 0u);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet,
        ME_RENDER_LIST_BULLET_RENDER_ANGLE_OFFSET, float_bits(0.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_LIST_BULLET_SIN_OFFSET,
        float_bits(0.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_LIST_BULLET_COS_OFFSET,
        float_bits(0.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet,
        ME_RENDER_LIST_BULLET_ROTATION_VALID_OFFSET, 1u);

    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_SOURCE_OFFSET, 7u);
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_UV_START_X_OFFSET,
        float_bits(0.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_UV_START_Y_OFFSET,
        float_bits(0.25f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_UV_END_X_OFFSET,
        float_bits(0.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_UV_END_Y_OFFSET,
        float_bits(0.75f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_HEIGHT_OFFSET,
        float_bits(8.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestSprite, ME_RENDER_RAW_SPRITE_WIDTH_OFFSET,
        float_bits(8.0f));

    Th07PspMeRenderStreamBuild semanticBuild;
    if (!th07_psp_me_render_stream_acquire(&semanticBuild))
        return 0;
    Th07PspMeRenderStreamRecord *semantic = semanticBuild.records;
    memset(semantic, 0, sizeof(*semantic));
    semantic->posXBits = float_bits(100.0f);
    semantic->posYBits = float_bits(100.0f);
    semantic->posZBits = float_bits(0.05f);
    semantic->halfWidthBits = float_bits(8.0f);
    semantic->halfHeightBits = float_bits(6.0f);
    semantic->sinBits = float_bits(0.5f);
    semantic->cosBits = float_bits(0.5f);
    semantic->u0Bits = float_bits(0.25f);
    semantic->u1Bits = float_bits(0.75f);
    semantic->v0Bits = float_bits(0.375f);
    semantic->v1Bits = float_bits(0.875f);
    semantic->color = 0x80604020u;
    semantic->sourceAndState = 3u | (1u << 16u);
    semantic->flags = TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE |
                      TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED |
                      TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD |
                      TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE |
                      (1u << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT);
    semantic->slot = 0u;
    semantic->slotGeneration = 9u;

    Th07PspMeRenderStreamJob semanticJob;
    me_render_raw_selftest_job_common(&semanticJob, &semanticBuild.token);
    semanticJob.version = TH07_PSP_ME_RENDER_STREAM_VERSION;
    semanticJob.flags = TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH |
                        TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
    semanticJob.payloadHash = th07_psp_me_render_stream_hash(
        semantic, sizeof(*semantic));
    if (!th07_psp_me_render_stream_submit(&semanticJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&semanticBuild.token);
        return 0;
    }
    Th07PspMeRenderStreamCompletion semanticCompletion;
    Th07PspMeRenderStreamReady semanticReady;
    if (!me_render_raw_selftest_wait(
            &semanticBuild.token, &semanticCompletion, &semanticReady) ||
        semanticCompletion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        semanticCompletion.outputBytes >
            4u * sizeof(Th07PspMeRenderStreamVertex) ||
        semanticCompletion.runCount != 1u)
        return 0;
    Th07PspMeRenderStreamVertex expectedVertices[4]
        __attribute__((aligned(64)));
    Th07PspMeRenderStreamRun expectedRun __attribute__((aligned(64)));
    const uint32_t expectedVertexBytes = semanticCompletion.outputBytes;
    memcpy(expectedVertices, semanticReady.vertices, expectedVertexBytes);
    memcpy(&expectedRun, semanticReady.runs, sizeof(expectedRun));
    if (!th07_psp_me_render_stream_release_ready(&semanticBuild.token))
        return 0;
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM DIRECT SEM PASS");
#endif

    Th07PspMeRenderStreamBuild directBuild;
    if (!th07_psp_me_render_stream_acquire(&directBuild))
        return 0;
    Th07PspMeRenderStreamJob directJob;
    me_render_raw_selftest_job_common(&directJob, &directBuild.token);
    directJob.version = TH07_PSP_ME_RENDER_STREAM_LIST_VERSION;
    directJob.flags = TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST |
                      TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
    me_render_raw_selftest_layout(&directJob.rawLayout);
    me_render_list_selftest_layout(&directJob.listLayout);
    if (!th07_psp_me_render_stream_submit(&directJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&directBuild.token);
        return 0;
    }
    Th07PspMeRenderStreamCompletion directCompletion;
    Th07PspMeRenderStreamReady directReady;
    if (!me_render_raw_selftest_wait(
            &directBuild.token, &directCompletion, &directReady) ||
        directCompletion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        directCompletion.outputBytes != expectedVertexBytes ||
        directCompletion.runCount != 1u ||
        directReady.vertexBytes != expectedVertexBytes ||
        directReady.runCount != 1u ||
        memcmp(directReady.vertices, expectedVertices,
               expectedVertexBytes) != 0 ||
        memcmp(directReady.runs, &expectedRun, sizeof(expectedRun)) != 0 ||
        !th07_psp_me_render_stream_release_ready(&directBuild.token))
        return 0;
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM DIRECT LIST PASS");
#endif

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (__atomic_load_n(&gMeItemRenderEnabled, __ATOMIC_ACQUIRE))
    {
    gMeItemSelftestInProgress = 1u;
    me_item_diag_begin();
    // Exercise the real combined command-10 ABI. Item is an optional prefix;
    // Bullet and its compact sidecar remain independently authoritative.
    memset(gMeRenderItemSelftestItem, 0,
           sizeof(gMeRenderItemSelftestItem));
    gMeRenderItemSelftestGeneration[0] = 13u;
    gMeRenderItemSelftestGeneration[1] = 14u;
    gMeRenderItemSelftestActiveBits[0] = 3u;
    gMeRenderItemSelftestSin[0] = float_bits(0.0f);
    gMeRenderItemSelftestCos[0] = float_bits(1.0f);
    gMeRenderItemSelftestPrepareSerial = 0x49370001u;
    gMeRenderItemSelftestPreparedSerial =
        gMeRenderItemSelftestPrepareSerial;
    gMeRenderItemSelftestPreparedCount = 2u;
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_ROTATION_Z_OFFSET,
        float_bits(0.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_SCALE_X_OFFSET,
        float_bits(2.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_SCALE_Y_OFFSET,
        float_bits(1.5f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_UV_SCROLL_X_OFFSET,
        float_bits(0.25f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_UV_SCROLL_Y_OFFSET,
        float_bits(0.125f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_COLOR_OFFSET,
        0x80442211u);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_COLOR2_OFFSET,
        0x80604020u);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_FLAGS_OFFSET, vmFlags);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_SPRITE_OFFSET,
        (uint32_t)gMeRenderRawSelftestSprite);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_VM_POS_X_OFFSET,
        float_bits(100.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_VM_POS_Y_OFFSET,
        float_bits(100.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_VM_POS_Z_OFFSET,
        float_bits(0.01f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_NEXT_OFFSET,
        (uint32_t)(gMeRenderItemSelftestItem + ME_RENDER_ITEM_STRIDE));
    gMeRenderItemSelftestItem[ME_RENDER_ITEM_TYPE_OFFSET] = 2u;
    gMeRenderItemSelftestItem[ME_RENDER_ITEM_IN_USE_OFFSET] = 1u;

    Th07PspMeRenderStreamBuild itemBuild;
    if (!th07_psp_me_render_stream_acquire(&itemBuild))
    {
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
        th07_psp_boot_note("ME1A I0 ACQ0");
#endif
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_LIVE_ACQUIRE, -1, NULL);
    }
    Th07PspMeRenderStreamJob itemJob;
    me_render_raw_selftest_job_common(&itemJob, &itemBuild.token);
    itemJob.version = TH07_PSP_ME_RENDER_STREAM_ITEM_VERSION;
    itemJob.flags = TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST |
                    TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST |
                    TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
    me_render_raw_selftest_layout(&itemJob.rawLayout);
    me_render_list_selftest_layout(&itemJob.listLayout);
    me_render_item_selftest_layout(&itemJob.itemLayout);
    if (!th07_psp_me_render_stream_submit(&itemJob))
    {
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
        th07_psp_boot_note("ME1A I0 SUB0");
#endif
        (void)th07_psp_me_render_stream_cancel_build(&itemBuild.token);
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_LIVE_SUBMIT, -1, NULL);
    }
    Th07PspMeRenderStreamCompletion itemCompletion;
    Th07PspMeRenderStreamReady itemReady;
    memset(&itemCompletion, 0, sizeof(itemCompletion));
    memset(&itemReady, 0, sizeof(itemReady));
    const int itemWait = me_render_raw_selftest_wait(
        &itemBuild.token, &itemCompletion, &itemReady);
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    th07_psp_boot_notef(
        "ME1A I0 W%d T%u I%u RC%u IC%u IV%u IR%u O%u V%u R%u "
        "F%u M%08x D%08x E%08x A%08x",
        itemWait, itemCompletion.result, itemCompletion.itemResult,
        itemCompletion.recordCount, itemCompletion.itemRecordCount,
        itemCompletion.itemVertexCount, itemCompletion.itemRunCount,
        itemCompletion.outputBytes, itemCompletion.vertexCount,
        itemCompletion.runCount, itemCompletion.firstBadRecord,
        itemCompletion.retireFaultMask, itemCompletion.retireFaultDetail,
        itemCompletion.retireFaultExpected, itemCompletion.retireFaultActual);
#else
    const uint32_t itemReadyRun0First =
        itemReady.runs != NULL && itemReady.runCount > 0u
            ? itemReady.runs[0].firstVertex : 0xffffffffu;
    const uint32_t itemReadyRun0Primitive =
        itemReady.runs != NULL && itemReady.runCount > 0u
            ? itemReady.runs[0].primitive : 0xffffffffu;
    const uint32_t itemReadyRun1First =
        itemReady.runs != NULL && itemReady.runCount > 1u
            ? itemReady.runs[1].firstVertex : 0xffffffffu;
    th07_psp_boot_notef(
        "ME1A I0 W%d T%u I%u RC%u IC%u IV%u IR%u O%u V%u R%u F%u "
        "RV%u RR%u R0F%u R0P%u R1F%u",
        itemWait, itemCompletion.result, itemCompletion.itemResult,
        itemCompletion.recordCount, itemCompletion.itemRecordCount,
        itemCompletion.itemVertexCount, itemCompletion.itemRunCount,
        itemCompletion.outputBytes, itemCompletion.vertexCount,
        itemCompletion.runCount, itemCompletion.firstBadRecord,
        itemReady.itemVertexCount, itemReady.itemRunCount,
        itemReadyRun0First, itemReadyRun0Primitive, itemReadyRun1First);
#endif
#endif
    if (!itemWait ||
        itemCompletion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        itemCompletion.itemResult !=
            TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        itemCompletion.itemRecordCount != 1u ||
        itemCompletion.itemVertexCount != 4u ||
        itemCompletion.itemRunCount != 1u ||
        itemCompletion.outputBytes != expectedVertexBytes +
            4u * sizeof(Th07PspMeRenderStreamVertex) ||
        itemCompletion.runCount != 2u ||
        itemReady.runs == NULL || itemReady.runCount != 2u ||
        itemReady.itemVertexCount != 4u || itemReady.itemRunCount != 1u ||
        itemReady.runs[0].firstVertex != 0u ||
        itemReady.runs[0].primitive !=
            TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS ||
        itemReady.runs[1].firstVertex != 4u ||
        !th07_psp_me_render_stream_release_ready(&itemBuild.token))
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_LIVE_CONTRACT, itemWait,
            &itemCompletion);
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM DIRECT ITEM PASS");
#endif

    // Authority values are live Main-RAM state, not descriptor structure.
    // A mismatch must pass submit/bounds validation, then fail closed only
    // after the ME has invalidated and walked the Item list.  This regression
    // test prevents the pre-invalidate live dereference that stopped I-ME8R.
    gMeRenderItemSelftestPreparedSerial =
        gMeRenderItemSelftestPrepareSerial ^ 1u;
    Th07PspMeRenderStreamBuild itemAuthorityBuild;
    if (!th07_psp_me_render_stream_acquire(&itemAuthorityBuild))
    {
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
        th07_psp_boot_note("ME1A IA ACQ0");
#endif
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_AUTH_ACQUIRE, -1, NULL);
    }
    Th07PspMeRenderStreamJob itemAuthorityJob = itemJob;
    itemAuthorityJob.token = itemAuthorityBuild.token;
    itemAuthorityJob.frameSeq++;
    itemAuthorityJob.targetDrawSeq++;
    if (!th07_psp_me_render_stream_submit(&itemAuthorityJob))
    {
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
        th07_psp_boot_note("ME1A IA SUB0");
#endif
        (void)th07_psp_me_render_stream_cancel_build(
            &itemAuthorityBuild.token);
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_AUTH_SUBMIT, -1, NULL);
    }
    Th07PspMeRenderStreamCompletion itemAuthorityCompletion;
    Th07PspMeRenderStreamReady itemAuthorityReady;
    memset(&itemAuthorityCompletion, 0, sizeof(itemAuthorityCompletion));
    memset(&itemAuthorityReady, 0, sizeof(itemAuthorityReady));
    const int itemAuthorityWait = me_render_raw_selftest_wait(
        &itemAuthorityBuild.token, &itemAuthorityCompletion,
        &itemAuthorityReady);
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    const uint32_t itemAuthorityReadyRun0First =
        itemAuthorityReady.runs != NULL && itemAuthorityReady.runCount > 0u
            ? itemAuthorityReady.runs[0].firstVertex : 0xffffffffu;
    th07_psp_boot_notef(
        "ME1A IA W%d T%u I%u RC%u IC%u IV%u IR%u O%u V%u R%u F%u "
        "RV%u RR%u R0F%u",
        itemAuthorityWait, itemAuthorityCompletion.result,
        itemAuthorityCompletion.itemResult,
        itemAuthorityCompletion.recordCount,
        itemAuthorityCompletion.itemRecordCount,
        itemAuthorityCompletion.itemVertexCount,
        itemAuthorityCompletion.itemRunCount,
        itemAuthorityCompletion.outputBytes,
        itemAuthorityCompletion.vertexCount,
        itemAuthorityCompletion.runCount,
        itemAuthorityCompletion.firstBadRecord,
        itemAuthorityReady.itemVertexCount,
        itemAuthorityReady.itemRunCount,
        itemAuthorityReadyRun0First);
#endif
    if (!itemAuthorityWait ||
        itemAuthorityCompletion.result !=
            TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        itemAuthorityCompletion.itemResult !=
            TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD ||
        itemAuthorityCompletion.itemRecordCount != 1u ||
        itemAuthorityCompletion.itemVertexCount != 0u ||
        itemAuthorityCompletion.itemRunCount != 0u ||
        itemAuthorityCompletion.outputBytes != expectedVertexBytes ||
        itemAuthorityCompletion.runCount != 1u ||
        itemAuthorityReady.runs == NULL ||
        itemAuthorityReady.runCount != 1u ||
        itemAuthorityReady.itemVertexCount != 0u ||
        itemAuthorityReady.itemRunCount != 0u ||
        itemAuthorityReady.runs[0].firstVertex != 0u ||
        !th07_psp_me_render_stream_release_ready(
            &itemAuthorityBuild.token))
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_AUTH_CONTRACT, itemAuthorityWait,
            &itemAuthorityCompletion);
    gMeRenderItemSelftestPreparedSerial =
        gMeRenderItemSelftestPrepareSerial;
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM DIRECT ITEM-AUTH PASS");
#endif

    // A corrupt Item list discards only the prefix. The same Bullet suffix
    // remains top-level OK and restarts at vertex zero.
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_NEXT_OFFSET,
        (uint32_t)gMeRenderItemSelftestItem);
    Th07PspMeRenderStreamBuild itemRejectBuild;
    if (!th07_psp_me_render_stream_acquire(&itemRejectBuild))
    {
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
        th07_psp_boot_note("ME1A IR ACQ0");
#endif
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_REJECT_ACQUIRE, -1, NULL);
    }
    Th07PspMeRenderStreamJob itemRejectJob = itemJob;
    itemRejectJob.token = itemRejectBuild.token;
    itemRejectJob.frameSeq++;
    itemRejectJob.targetDrawSeq++;
    if (!th07_psp_me_render_stream_submit(&itemRejectJob))
    {
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
        th07_psp_boot_note("ME1A IR SUB0");
#endif
        (void)th07_psp_me_render_stream_cancel_build(
            &itemRejectBuild.token);
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_REJECT_SUBMIT, -1, NULL);
    }
    Th07PspMeRenderStreamCompletion itemRejectCompletion;
    Th07PspMeRenderStreamReady itemRejectReady;
    memset(&itemRejectCompletion, 0, sizeof(itemRejectCompletion));
    memset(&itemRejectReady, 0, sizeof(itemRejectReady));
    const int itemRejectWait = me_render_raw_selftest_wait(
        &itemRejectBuild.token, &itemRejectCompletion, &itemRejectReady);
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    const uint32_t itemRejectReadyRun0First =
        itemRejectReady.runs != NULL && itemRejectReady.runCount > 0u
            ? itemRejectReady.runs[0].firstVertex : 0xffffffffu;
    th07_psp_boot_notef(
        "ME1A IR W%d T%u I%u RC%u IC%u IV%u IR%u O%u V%u R%u F%u "
        "RV%u RR%u R0F%u",
        itemRejectWait, itemRejectCompletion.result,
        itemRejectCompletion.itemResult, itemRejectCompletion.recordCount,
        itemRejectCompletion.itemRecordCount,
        itemRejectCompletion.itemVertexCount,
        itemRejectCompletion.itemRunCount,
        itemRejectCompletion.outputBytes, itemRejectCompletion.vertexCount,
        itemRejectCompletion.runCount, itemRejectCompletion.firstBadRecord,
        itemRejectReady.itemVertexCount, itemRejectReady.itemRunCount,
        itemRejectReadyRun0First);
#endif
    if (!itemRejectWait ||
        itemRejectCompletion.result !=
            TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        itemRejectCompletion.itemResult !=
            TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD ||
        itemRejectCompletion.itemRecordCount != 1u ||
        itemRejectCompletion.itemVertexCount != 0u ||
        itemRejectCompletion.itemRunCount != 0u ||
        itemRejectCompletion.outputBytes != expectedVertexBytes ||
        itemRejectCompletion.runCount != 1u ||
        itemRejectReady.runs == NULL || itemRejectReady.runCount != 1u ||
        itemRejectReady.itemVertexCount != 0u ||
        itemRejectReady.itemRunCount != 0u ||
        itemRejectReady.runs[0].firstVertex != 0u ||
        !th07_psp_me_render_stream_release_ready(
            &itemRejectBuild.token))
        return me_item_diag_fail(
            TH07_PSP_ME_ITEM_REASON_REJECT_CONTRACT, itemRejectWait,
            &itemRejectCompletion);
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM DIRECT ITEM-REJECT PASS");
#endif
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_NEXT_OFFSET,
        (uint32_t)(gMeRenderItemSelftestItem + ME_RENDER_ITEM_STRIDE));
    me_item_diag_pass();
    gMeItemSelftestInProgress = 0u;
    }
#endif

    // Make the one live node point to itself.  The bucket-end authority says
    // this is its final record, so ME must reject the cycle before any partial
    // output can become visible and recycle the soft RECORD completion.
    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_LIST_BULLET_NEXT_OFFSET,
        (uint32_t)gMeRenderRawSelftestBullet);
    Th07PspMeRenderStreamBuild rejectBuild;
    if (!th07_psp_me_render_stream_acquire(&rejectBuild))
        return 0;
    Th07PspMeRenderStreamJob rejectJob = directJob;
    rejectJob.token = rejectBuild.token;
    rejectJob.frameSeq++;
    rejectJob.targetDrawSeq++;
    if (!th07_psp_me_render_stream_submit(&rejectJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&rejectBuild.token);
        return 0;
    }
    Th07PspMeRenderStreamCompletion rejectCompletion;
    Th07PspMeRenderStreamReady rejectReady;
    if (!me_render_raw_selftest_wait(
            &rejectBuild.token, &rejectCompletion, &rejectReady) ||
        rejectCompletion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD ||
        rejectCompletion.firstBadRecord != 0u ||
        rejectCompletion.outputBytes != 0u ||
        rejectCompletion.vertexCount != 0u || rejectCompletion.runCount != 0u ||
        rejectReady.vertices != 0 || rejectReady.vertexBytes != 0u ||
        rejectReady.runs != 0 || rejectReady.runCount != 0u ||
        th07_psp_me_render_stream_mark_ge_in_flight(&rejectBuild.token) ||
        !th07_psp_me_render_stream_release_ready(&rejectBuild.token))
        return 0;

    me_render_raw_selftest_store_u32(
        gMeRenderRawSelftestBullet, ME_RENDER_LIST_BULLET_NEXT_OFFSET, 0u);
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM DIRECT REJECT PASS");
#endif
    return 1;
}
#endif
#endif

static int selftest_render_stream(void)
{
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    // Recovery-profile breadcrumb: if hardware stops, BOOT.LOG identifies
    // the last command-10 phase completed without another diagnostic run.
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
#if defined(TH07_PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP)
    th07_psp_boot_note("MERW STREAM BEGIN E1 L1");
#else
    th07_psp_boot_note("MERW STREAM BEGIN E1 L0");
#endif
#else
#if defined(TH07_PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP)
    th07_psp_boot_note("MERW STREAM BEGIN E0 L1");
#else
    th07_psp_boot_note("MERW STREAM BEGIN E0 L0");
#endif
#endif
#endif
    Th07PspMeRenderStreamBuild build;
    if (!me_render_stream_guards_match_on_sc(0u) ||
        !th07_psp_me_render_stream_acquire(&build))
        return 0;

    Th07PspMeRenderStreamRecord *records = build.records;
    memset(records, 0, 6u * sizeof(*records));
    const uint32_t baseFlags =
        TH07_PSP_ME_RENDER_STREAM_RECORD_DRAWABLE;
    const float selftestUv1 = 1.0f;
    for (uint32_t record = 0u; record < 6u; ++record)
    {
        records[record].slot = 10u + record;
        records[record].slotGeneration = 100u + record;
        records[record].posZBits = float_bits(0.5f);
        records[record].halfWidthBits = float_bits(4.0f);
        records[record].halfHeightBits = float_bits(6.0f);
        records[record].sinBits = float_bits(0.0f);
        records[record].cosBits = float_bits(1.0f);
        records[record].u0Bits = float_bits(0.0f);
        records[record].u1Bits = float_bits(selftestUv1);
        records[record].v0Bits = float_bits(0.0f);
        records[record].v1Bits = float_bits(selftestUv1);
        records[record].color = 0x80402010u;
        records[record].sourceAndState = 1u | (10u << 16);
        records[record].flags = baseFlags;
    }

    // Pair-mode axis record.
    records[0].posXBits = float_bits(100.0f);
    records[0].posYBits = float_bits(100.0f);

    // A rotated record outside the viewport must neither emit nor switch UQ
    // mode; even its explicit break is ignored just like canonical culling.
    records[1].posXBits = float_bits(5000.0f);
    records[1].posYBits = float_bits(5000.0f);
    records[1].sinBits = float_bits(0.5f);
    records[1].cosBits = float_bits(0.5f);
    records[1].flags =
        baseFlags | TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED |
        TH07_PSP_ME_RENDER_STREAM_RECORD_RUN_BREAK;

    // First visible general quad.  Anchor is deliberately applied after the
    // mul/add chain; 0.5 sin/cos keeps independently-known integer results.
    records[2].posXBits = float_bits(200.0f);
    records[2].posYBits = float_bits(100.0f);
    records[2].posZBits = float_bits(0.25f);
    records[2].sinBits = float_bits(0.5f);
    records[2].cosBits = float_bits(0.5f);
    records[2].flags =
        baseFlags | TH07_PSP_ME_RENDER_STREAM_RECORD_ROTATED |
        TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_MASK;

    // Once general mode begins, a later axis record stays a four-vertex UQ
    // and merges across the logical bucket boundary with identical state.
    records[3].posXBits = float_bits(300.0f);
    records[3].posYBits = float_bits(100.0f);
    records[3].posZBits = float_bits(0.75f);
    records[3].halfWidthBits = float_bits(-4.0f);

    // Different source/blend/effective-z starts the final ordered run and
    // exercises horizontal anchor semantics on the axis floor path.
    records[4].posXBits = float_bits(50.0f);
    records[4].posYBits = float_bits(50.0f);
    records[4].posZBits = float_bits(0.125f);
    records[4].sourceAndState = 2u | (20u << 16);
    records[4].flags =
        baseFlags | TH07_PSP_ME_RENDER_STREAM_RECORD_BLEND_ADD |
        TH07_PSP_ME_RENDER_STREAM_RECORD_ZWRITE_DISABLE |
        (1u << TH07_PSP_ME_RENDER_STREAM_RECORD_ANCHOR_SHIFT);

    // Non-drawable slots still carry stable identity but own no render data.
    records[5].flags = 0u;

    Th07PspMeRenderStreamJob job;
    memset(&job, 0, sizeof(job));
    job.token = build.token;
    job.version = TH07_PSP_ME_RENDER_STREAM_VERSION;
    job.flags = TH07_PSP_ME_RENDER_STREAM_JOB_VERIFY_INPUT_HASH |
                TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
    job.frameSeq = 0x1234u;
    job.targetDrawSeq = 0x7788u;
    job.stageEpoch = 7u;
    job.managerEpoch = 9u;
    job.replayEpoch = 11u;
    job.globalSignature = 0x89abcdefu;
    for (uint32_t bucket = 0u; bucket < 6u; ++bucket)
        job.bucketEnds[bucket] = bucket + 1u;
    job.recordCount = 6u;
    job.payloadHash = th07_psp_me_render_stream_hash(
        records, job.recordCount * sizeof(*records));
    job.offsetXBits = float_bits(10.0f);
    job.offsetYBits = float_bits(20.0f);
    job.viewportLeftBits = float_bits(0.0f);
    job.viewportTopBits = float_bits(0.0f);
    job.viewportRightBits = float_bits(640.0f);
    job.viewportBottomBits = float_bits(480.0f);
    job.globalColor = 0x406080a0u;
    job.configFlags = TH07_PSP_ME_RENDER_STREAM_CONFIG_COLOR_MUL;

#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // Prove the guarded COP1 floor domain independently of the real ME job.
    // The ordinary axis record is accepted; finite values just outside the
    // symmetric floor.w.s range and arithmetic overflow are rejected.
    Th07PspMeRenderStreamRecord floorProbe = records[0];
    if (!me_render_stream_axis_floor_inputs_valid(
            &floorProbe, job.offsetXBits, job.offsetYBits))
    {
        (void)th07_psp_me_render_stream_cancel_build(&build.token);
        return 0;
    }
    floorProbe.posXBits = float_bits(2147483648.0f);
    if (me_render_stream_axis_floor_inputs_valid(
            &floorProbe, float_bits(0.0f), float_bits(0.0f)))
    {
        (void)th07_psp_me_render_stream_cancel_build(&build.token);
        return 0;
    }
    floorProbe.posXBits = float_bits(-2147483648.0f);
    if (me_render_stream_axis_floor_inputs_valid(
            &floorProbe, float_bits(0.0f), float_bits(0.0f)))
    {
        (void)th07_psp_me_render_stream_cancel_build(&build.token);
        return 0;
    }
    floorProbe = records[0];
    floorProbe.halfWidthBits = 0x7f7fffffu;
    if (me_render_stream_axis_floor_inputs_valid(
            &floorProbe, job.offsetXBits, job.offsetYBits))
    {
        (void)th07_psp_me_render_stream_cancel_build(&build.token);
        return 0;
    }
#endif

    if (!th07_psp_me_render_stream_submit(&job))
    {
        (void)th07_psp_me_render_stream_cancel_build(&build.token);
        return 0;
    }
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    while (th07_psp_me_render_stream_probe(&build.token, 0) == 0)
    {
        if (sceKernelGetSystemTimeLow() - startUs >=
            ME_RENDER_BENCH_TIMEOUT_US)
        {
            th07_psp_me_render_stream_hard_fault(&build.token);
            return 0;
        }
        sceKernelDelayThread(20);
    }

    Th07PspMeRenderStreamCompletion completion;
    Th07PspMeRenderStreamReady ready;
    if (th07_psp_me_render_stream_retire(&build.token, &completion,
                                         &ready) != 1)
        return 0;

    static Th07PspMeRenderStreamVertex expectedVertices[14]
        __attribute__((aligned(64)));
    static Th07PspMeRenderStreamRun expectedRuns[3]
        __attribute__((aligned(64)));
    const uint32_t color = 0x40142030u;
    me_render_stream_selftest_vertex(&expectedVertices[0], 0.0f, 0.0f,
                                     color, 106.0f, 114.0f, 0.5f);
    me_render_stream_selftest_vertex(&expectedVertices[1], selftestUv1,
                                     selftestUv1,
                                     color, 114.0f, 126.0f, 0.5f);
    me_render_stream_selftest_vertex(&expectedVertices[2], 0.0f, 0.0f,
                                     color, 215.0f, 121.0f, 0.25f);
    me_render_stream_selftest_vertex(&expectedVertices[3], selftestUv1, 0.0f,
                                     color, 219.0f, 125.0f, 0.25f);
    me_render_stream_selftest_vertex(&expectedVertices[4], 0.0f, selftestUv1,
                                     color, 209.0f, 127.0f, 0.25f);
    me_render_stream_selftest_vertex(&expectedVertices[5], selftestUv1,
                                     selftestUv1,
                                     color, 213.0f, 131.0f, 0.25f);
    me_render_stream_selftest_vertex(&expectedVertices[6], 0.0f, 0.0f,
                                     color, 314.0f, 114.0f, 0.75f);
    me_render_stream_selftest_vertex(&expectedVertices[7], selftestUv1, 0.0f,
                                     color, 306.0f, 114.0f, 0.75f);
    me_render_stream_selftest_vertex(&expectedVertices[8], 0.0f, selftestUv1,
                                     color, 314.0f, 126.0f, 0.75f);
    me_render_stream_selftest_vertex(&expectedVertices[9], selftestUv1,
                                     selftestUv1,
                                     color, 306.0f, 126.0f, 0.75f);
    me_render_stream_selftest_vertex(&expectedVertices[10], 0.0f, 0.0f,
                                     color, 60.0f, 64.0f, 0.125f);
    me_render_stream_selftest_vertex(&expectedVertices[11], selftestUv1,
                                     0.0f,
                                     color, 68.0f, 64.0f, 0.125f);
    me_render_stream_selftest_vertex(&expectedVertices[12], 0.0f,
                                     selftestUv1,
                                     color, 60.0f, 76.0f, 0.125f);
    me_render_stream_selftest_vertex(&expectedVertices[13], selftestUv1,
                                     selftestUv1,
                                     color, 68.0f, 76.0f, 0.125f);

    memset(expectedRuns, 0, sizeof(expectedRuns));
    expectedRuns[0].firstRecord = 0u;
    expectedRuns[0].recordCount = 1u;
    expectedRuns[0].firstVertex = 0u;
    expectedRuns[0].vertexCount = 2u;
    expectedRuns[0].primitive =
        TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES;
    expectedRuns[0].sourceFileIndex = 1u;
    expectedRuns[0].logicalState = 10u;
    expectedRuns[1].firstRecord = 2u;
    expectedRuns[1].recordCount = 2u;
    expectedRuns[1].firstVertex = 2u;
    expectedRuns[1].vertexCount = 8u;
    expectedRuns[1].primitive = TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS;
    expectedRuns[1].sourceFileIndex = 1u;
    expectedRuns[1].logicalState = 10u;
    expectedRuns[2].firstRecord = 4u;
    expectedRuns[2].recordCount = 1u;
    expectedRuns[2].firstVertex = 10u;
    expectedRuns[2].vertexCount = 4u;
    expectedRuns[2].primitive = TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS;
    expectedRuns[2].sourceFileIndex = 2u;
    expectedRuns[2].logicalState = 20u;
    expectedRuns[2].renderStateFlags =
        TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD |
        TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE;

    Th07PspMeRenderStreamMismatch mismatch;
    const int compared =
        completion.result == TH07_PSP_ME_RENDER_STREAM_RESULT_OK &&
        completion.vertexCount == 14u && completion.runCount == 3u &&
        completion.outputBytes == sizeof(expectedVertices) &&
        ready.vertexBytes == sizeof(expectedVertices) &&
        ready.runCount == 3u &&
        th07_psp_me_render_stream_compare(
            &build.token, expectedVertices, sizeof(expectedVertices),
            expectedRuns, 3u, &mismatch) == 1 &&
        me_render_stream_guards_match_on_sc(build.token.slot);
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    if (!compared ||
        !th07_psp_me_render_stream_ready_view_matches(
            &build.token, ready.vertices, ready.vertexBytes,
            ready.runs, ready.runCount) ||
        th07_psp_me_render_stream_ready_view_matches(
            &build.token, ready.vertices + 8u, ready.vertexBytes,
            ready.runs, ready.runCount) ||
        th07_psp_me_render_stream_ready_view_matches(
            &build.token, ready.vertices, ready.vertexBytes,
            ready.runs + 2u, ready.runCount) ||
        th07_psp_me_render_stream_ready_view_matches(
            &build.token, ready.vertices,
            ready.vertexBytes - sizeof(*ready.vertices),
            ready.runs, ready.runCount) ||
        th07_psp_me_render_stream_ready_view_matches(
            &build.token, ready.vertices, ready.vertexBytes,
            ready.runs, ready.runCount - 1u))
    {
        (void)th07_psp_me_render_stream_release_ready(&build.token);
        return 0;
    }
    // No GE command references this boot-test slot, so an immediate simulated
    // fence completion is safe.  Exercise the I-ME2 ownership edges and prove
    // that duplicate mark, shadow release after mark, and duplicate GE release
    // all fail without exposing the slot as FREE.
    if (!th07_psp_me_render_stream_mark_ge_in_flight(&build.token) ||
        th07_psp_me_render_stream_ready_view_matches(
            &build.token, ready.vertices, ready.vertexBytes,
            ready.runs, ready.runCount) ||
        th07_psp_me_render_stream_mark_ge_in_flight(&build.token) ||
        th07_psp_me_render_stream_release_ready(&build.token) ||
        !th07_psp_me_render_stream_abort_ge_mark(&build.token) ||
        !th07_psp_me_render_stream_ready_view_matches(
            &build.token, ready.vertices, ready.vertexBytes,
            ready.runs, ready.runCount) ||
        th07_psp_me_render_stream_abort_ge_mark(&build.token) ||
        th07_psp_me_render_stream_release_after_ge(&build.token) ||
        !th07_psp_me_render_stream_mark_ge_in_flight(&build.token) ||
        !th07_psp_me_render_stream_release_after_ge(&build.token) ||
        th07_psp_me_render_stream_release_after_ge(&build.token))
        return 0;

    // The direct-performance profile deliberately carries no hash flags.
    // A bogus caller hash must be ignored and normalized to zero; even the
    // empty FNV value would be nonzero, so this one job proves SC submit, ME
    // input, ME output and SC retire all skipped their diagnostic scans.
    Th07PspMeRenderStreamBuild perfBuild;
    if (!th07_psp_me_render_stream_acquire(&perfBuild))
        return 0;
    Th07PspMeRenderStreamJob perfJob;
    memset(&perfJob, 0, sizeof(perfJob));
    perfJob.token = perfBuild.token;
    perfJob.version = TH07_PSP_ME_RENDER_STREAM_VERSION;
    perfJob.frameSeq = 0x2234u;
    perfJob.targetDrawSeq = 0x8788u;
    perfJob.payloadHash = 0xfeedfaceu;
    if (!th07_psp_me_render_stream_submit(&perfJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&perfBuild.token);
        return 0;
    }
    const uint32_t perfStartUs = sceKernelGetSystemTimeLow();
    while (th07_psp_me_render_stream_probe(&perfBuild.token, 0) == 0)
    {
        if (sceKernelGetSystemTimeLow() - perfStartUs >=
            ME_RENDER_BENCH_TIMEOUT_US)
        {
            th07_psp_me_render_stream_hard_fault(&perfBuild.token);
            return 0;
        }
        sceKernelDelayThread(20);
    }
    Th07PspMeRenderStreamCompletion perfCompletion;
    Th07PspMeRenderStreamReady perfReady;
    if (th07_psp_me_render_stream_retire(
            &perfBuild.token, &perfCompletion, &perfReady) != 1 ||
        perfCompletion.flags != 0u || perfCompletion.payloadHash != 0u ||
        perfCompletion.outputHash != 0u || perfCompletion.runHash != 0u ||
        perfCompletion.outputBytes != 0u || perfCompletion.vertexCount != 0u ||
        perfCompletion.runCount != 0u || perfReady.vertexBytes != 0u ||
        perfReady.runCount != 0u ||
        !th07_psp_me_render_stream_ready_view_matches(
            &perfBuild.token, perfReady.vertices, perfReady.vertexBytes,
            perfReady.runs, perfReady.runCount) ||
        !th07_psp_me_render_stream_release_ready(&perfBuild.token))
        return 0;
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM BASE PASS");
#endif
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    if (!selftest_render_stream_raw_live())
        return 0;
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM RAW PASS");
#endif
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    if (!selftest_render_stream_direct_list())
        return 0;
#if defined(TH07_PSP_ME_STARTUP_BREADCRUMBS)
    th07_psp_boot_note("MERW STREAM DIRECT PASS");
#endif
#endif
#endif
#else
    if (!th07_psp_me_render_stream_release_ready(&build.token) || !compared)
        return 0;
#endif

    // Prove all three ownership slots can coexist in SC_BUILD and that the
    // generation-checked fourth acquisition fails without aliasing a pool.
    Th07PspMeRenderStreamBuild held[TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT];
    for (uint32_t index = 0u;
         index < TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT; ++index)
    {
        if (!th07_psp_me_render_stream_acquire(&held[index]))
            return 0;
        for (uint32_t prior = 0u; prior < index; ++prior)
        {
            if (held[prior].token.slot == held[index].token.slot)
                return 0;
        }
    }
    Th07PspMeRenderStreamBuild excess;
    if (th07_psp_me_render_stream_acquire(&excess))
        return 0;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (__atomic_load_n(&gMeItemRenderEnabled, __ATOMIC_ACQUIRE))
        th07_psp_boot_note(
            "ME1A SELFTEST PASS ITEM-PREFIX + LIST-LIVE EDRAM0");
    else
        th07_psp_boot_note(
            "ME15 SELFTEST PASS LIST-LIVE EDRAM0 ITEM-OFF");
#elif defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    th07_psp_boot_note("ME15 SELFTEST PASS LIST-LIVE EDRAM0");
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    th07_psp_boot_note("ME14 SELFTEST PASS RAW-LIVE EDRAM0");
#elif defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // build.token is now stale while its former slot has a live SC_BUILD
    // generation in held[].  Every stale ownership edge must fail without
    // canceling, promoting or releasing that newer lifetime.
    if (th07_psp_me_render_stream_cancel_build(&build.token) ||
        th07_psp_me_render_stream_release_ready(&build.token) ||
        th07_psp_me_render_stream_mark_ge_in_flight(&build.token) ||
        th07_psp_me_render_stream_abort_ge_mark(&build.token) ||
        th07_psp_me_render_stream_release_after_ge(&build.token))
        return 0;
#endif
    for (uint32_t index = 0u;
         index < TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT; ++index)
    {
        if (!th07_psp_me_render_stream_cancel_build(&held[index].token))
            return 0;
    }
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    th07_psp_boot_note("ME12 SELFTEST PASS EDRAM0 GE-OWNER");
#else
    th07_psp_boot_note("ME11 SELFTEST PASS EDRAM0 SHADOW");
#endif
    return 1;
}
#endif

#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
static int selftest_bullet_fast_update(void)
{
    const uint32_t bulletBytes =
        ME_BULLET_FAST_BULLET_COUNT * ME_BULLET_FAST_BULLET_STRIDE;
    const uint32_t spriteBytes =
        ME_BULLET_FAST_SPRITE_COUNT * ME_BULLET_FAST_SPRITE_STRIDE;
    const uint32_t generationBytes =
        ME_BULLET_FAST_BULLET_COUNT * sizeof(uint32_t);
    const uint32_t activeBytes =
        TH07_PSP_ME_BULLET_FAST_ACTIVE_WORDS * sizeof(uint32_t);
    const uint32_t bombBytes =
        ME_BULLET_FAST_BOMB_CLEAR_CAPACITY *
        ME_BULLET_FAST_BOMB_CLEAR_STRIDE;
    unsigned char *bullets =
        (unsigned char *)memalign(64u, bulletBytes);
    unsigned char *sprites =
        (unsigned char *)memalign(64u, spriteBytes);
    uint32_t *generations =
        (uint32_t *)memalign(64u, generationBytes);
    uint32_t *activeBits =
        (uint32_t *)memalign(64u, activeBytes);
    unsigned char *bombs =
        (unsigned char *)memalign(64u, bombBytes);
    int passed = 0;
    if (!bullets || !sprites || !generations || !activeBits || !bombs)
        goto cleanup;

    memset(bullets, 0, bulletBytes);
    memset(sprites, 0, spriteBytes);
    memset(generations, 0, generationBytes);
    memset(activeBits, 0, activeBytes);
    memset(bombs, 0, bombBytes);
    me_render_raw_selftest_store_u32(
        sprites, ME_BULLET_FAST_SPRITE_WIDTH_OFFSET,
        me_render_float_bits(32.0f));
    me_render_raw_selftest_store_u32(
        sprites, ME_BULLET_FAST_SPRITE_HEIGHT_OFFSET,
        me_render_float_bits(16.0f));

    for (uint32_t slot = 0u;
         slot < ME_BULLET_FAST_BULLET_COUNT; ++slot)
    {
        unsigned char *bullet =
            bullets + slot * ME_BULLET_FAST_BULLET_STRIDE;
        me_render_list_selftest_store_u16(
            bullet, ME_BULLET_FAST_BULLET_STATE_OFFSET,
            ME_BULLET_FAST_STATE_NORMAL);
        me_render_list_selftest_store_u16(
            bullet, ME_BULLET_FAST_BULLET_EX_FLAGS_OFFSET, 0u);
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_SPAWN_DELAY_OFFSET, 0u);
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_CURRENT_COMMAND_INDEX_OFFSET,
            ME_BULLET_FAST_COMMAND_COUNT);
        const float posX = 64.0f + (float)(slot & 7u) * 0.5f;
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_POS_X_OFFSET,
            me_render_float_bits(posX));
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_POS_Y_OFFSET,
            me_render_float_bits(100.0f));
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_POS_Z_OFFSET,
            me_render_float_bits(0.125f));
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_VELOCITY_X_OFFSET,
            me_render_float_bits(0.25f));
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_VELOCITY_Y_OFFSET,
            me_render_float_bits(-0.5f));
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_VELOCITY_Z_OFFSET,
            me_render_float_bits(0.25f));
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_GRAZE_SIZE_X_OFFSET,
            me_render_float_bits(4.0f));
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_BULLET_GRAZE_SIZE_Y_OFFSET,
            me_render_float_bits(6.0f));
        me_render_raw_selftest_store_u32(
            bullet, ME_BULLET_FAST_VM_SPRITE_OFFSET,
            (uint32_t)sprites);
        generations[slot] = slot + 1u;
    }

    Th07PspMeBulletFastJob job;
    memset(&job, 0, sizeof(job));
    job.version = TH07_PSP_ME_BULLET_FAST_UPDATE_VERSION;
    job.frameSeq = 1u;
    Th07PspMeBulletFastLayout *layout = &job.layout;
    layout->layoutVersion = TH07_PSP_ME_BULLET_FAST_LAYOUT_VERSION;
    layout->layoutBytes = sizeof(*layout);
    layout->bulletBasePhys = (uint32_t)bullets & 0x1fffffffu;
    layout->bulletStride = ME_BULLET_FAST_BULLET_STRIDE;
    layout->bulletCount = ME_BULLET_FAST_BULLET_COUNT;
    layout->generationBasePhys = (uint32_t)generations & 0x1fffffffu;
    layout->generationStride = ME_BULLET_FAST_GENERATION_STRIDE;
    layout->generationCount = ME_BULLET_FAST_BULLET_COUNT;
    layout->activeBitsPhys = (uint32_t)activeBits & 0x1fffffffu;
    layout->activeBitsWordCount = TH07_PSP_ME_BULLET_FAST_ACTIVE_WORDS;
    layout->spriteBasePhys = (uint32_t)sprites & 0x1fffffffu;
    layout->spriteStride = ME_BULLET_FAST_SPRITE_STRIDE;
    layout->spriteCount = ME_BULLET_FAST_SPRITE_COUNT;
    layout->bulletStateOffset = ME_BULLET_FAST_BULLET_STATE_OFFSET;
    layout->bulletPosXOffset = ME_BULLET_FAST_BULLET_POS_X_OFFSET;
    layout->bulletPosYOffset = ME_BULLET_FAST_BULLET_POS_Y_OFFSET;
    layout->bulletPosZOffset = ME_BULLET_FAST_BULLET_POS_Z_OFFSET;
    layout->bulletVelocityXOffset =
        ME_BULLET_FAST_BULLET_VELOCITY_X_OFFSET;
    layout->bulletVelocityYOffset =
        ME_BULLET_FAST_BULLET_VELOCITY_Y_OFFSET;
    layout->bulletVelocityZOffset =
        ME_BULLET_FAST_BULLET_VELOCITY_Z_OFFSET;
    layout->bulletSpawnDelayOffset =
        ME_BULLET_FAST_BULLET_SPAWN_DELAY_OFFSET;
    layout->bulletExFlagsOffset = ME_BULLET_FAST_BULLET_EX_FLAGS_OFFSET;
    layout->bulletOutOfBoundsTimeOffset =
        ME_BULLET_FAST_BULLET_OUT_OF_BOUNDS_TIME_OFFSET;
    layout->bulletCurrentCommandIndexOffset =
        ME_BULLET_FAST_BULLET_CURRENT_COMMAND_INDEX_OFFSET;
    layout->bulletCommandsOffset = ME_BULLET_FAST_BULLET_COMMANDS_OFFSET;
    layout->bulletCommandStride = ME_BULLET_FAST_BULLET_COMMAND_STRIDE;
    layout->bulletCommandTypeOffset =
        ME_BULLET_FAST_BULLET_COMMAND_TYPE_OFFSET;
    layout->bulletGrazeSizeXOffset =
        ME_BULLET_FAST_BULLET_GRAZE_SIZE_X_OFFSET;
    layout->bulletGrazeSizeYOffset =
        ME_BULLET_FAST_BULLET_GRAZE_SIZE_Y_OFFSET;
    layout->vmSpriteOffset = ME_BULLET_FAST_VM_SPRITE_OFFSET;
    layout->spriteWidthOffset = ME_BULLET_FAST_SPRITE_WIDTH_OFFSET;
    layout->spriteHeightOffset = ME_BULLET_FAST_SPRITE_HEIGHT_OFFSET;
    layout->bombClearStride = ME_BULLET_FAST_BOMB_CLEAR_STRIDE;
    layout->bombClearPosXOffset = ME_BULLET_FAST_BOMB_CLEAR_POS_X_OFFSET;
    layout->bombClearPosYOffset = ME_BULLET_FAST_BOMB_CLEAR_POS_Y_OFFSET;
    layout->bombClearPosZOffset = ME_BULLET_FAST_BOMB_CLEAR_POS_Z_OFFSET;
    layout->bombClearSizeXOffset = ME_BULLET_FAST_BOMB_CLEAR_SIZE_X_OFFSET;
    layout->bombClearSizeYOffset = ME_BULLET_FAST_BOMB_CLEAR_SIZE_Y_OFFSET;
    job.playerState = 0u;
    job.playerGrazeLeftBits = me_render_float_bits(10000.0f);
    job.playerGrazeTopBits = me_render_float_bits(10000.0f);
    job.playerGrazeRightBits = me_render_float_bits(10001.0f);
    job.playerGrazeBottomBits = me_render_float_bits(10001.0f);
    job.playerHitboxLeftBits = me_render_float_bits(10000.0f);
    job.playerHitboxTopBits = me_render_float_bits(10000.0f);
    job.playerHitboxRightBits = me_render_float_bits(10001.0f);
    job.playerHitboxBottomBits = me_render_float_bits(10001.0f);
    job.bombClearBasePhys = (uint32_t)bombs & 0x1fffffffu;
    job.bombClearHighWater = 0u;
    job.bombClearCapacity = ME_BULLET_FAST_BOMB_CLEAR_CAPACITY;
    job.playfieldRightBits = me_render_float_bits(384.0f);
    job.playfieldBottomBits = me_render_float_bits(448.0f);

    static const uint32_t counts[4] = {0u, 128u, 512u, 1024u};
    for (uint32_t caseIndex = 0u; caseIndex < 4u; ++caseIndex)
    {
        const uint32_t count = counts[caseIndex];
        memset(activeBits, 0, activeBytes);
        for (uint32_t word = 0u; word < count / 32u; ++word)
            activeBits[word] = 0xffffffffu;
        job.frameSeq++;
        Th07PspMeBulletFastCompletion completion;
        const Th07PspMeBulletFastOutput *output = 0;
        if (th07_psp_me_bullet_fast_update_run(
                &job, &completion, &output) != 1 || !output ||
            completion.result != TH07_PSP_ME_BULLET_FAST_JOB_OK ||
            completion.activeCount != count ||
            completion.candidateCount != count ||
            completion.inBoundsCount != count ||
            completion.noCollisionCount != count ||
            completion.firstBadSlot != 0xffffffffu)
            goto cleanup;
        for (uint32_t slot = 0u; slot < count; ++slot)
        {
            const float expectedX =
                64.0f + (float)(slot & 7u) * 0.5f + 0.25f;
            const Th07PspMeBulletFastSlotResult *result =
                &output->slots[slot];
            if (result->posXBits != me_render_float_bits(expectedX) ||
                result->posYBits != me_render_float_bits(99.5f) ||
                result->posZBits != me_render_float_bits(0.375f) ||
                result->generation != (uint16_t)(slot + 1u) ||
                result->flags !=
                    (TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE |
                     TH07_PSP_ME_BULLET_FAST_SLOT_IN_BOUNDS |
                     TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION))
                goto cleanup;
        }
    }

    // Exact out-of-bounds classification with collision still independently
    // proven negative.
    memset(activeBits, 0, activeBytes);
    activeBits[0] = 1u;
    me_render_raw_selftest_store_u32(
        bullets, ME_BULLET_FAST_BULLET_POS_X_OFFSET,
        me_render_float_bits(500.0f));
    me_render_raw_selftest_store_u32(
        bullets, ME_BULLET_FAST_BULLET_VELOCITY_X_OFFSET,
        me_render_float_bits(0.0f));
    job.frameSeq++;
    Th07PspMeBulletFastCompletion edgeCompletion;
    const Th07PspMeBulletFastOutput *edgeOutput = 0;
    if (th07_psp_me_bullet_fast_update_run(
            &job, &edgeCompletion, &edgeOutput) != 1 || !edgeOutput ||
        edgeCompletion.inBoundsCount != 0u ||
        edgeCompletion.noCollisionCount != 1u ||
        edgeOutput->slots[0].flags !=
            (TH07_PSP_ME_BULLET_FAST_SLOT_CANDIDATE |
             TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION))
        goto cleanup;

    // A live bomb overlap and BORDER each forbid negative-collision adoption.
    me_render_raw_selftest_store_u32(
        bullets, ME_BULLET_FAST_BULLET_POS_X_OFFSET,
        me_render_float_bits(100.0f));
    me_render_raw_selftest_store_u32(
        bombs, ME_BULLET_FAST_BOMB_CLEAR_POS_X_OFFSET,
        me_render_float_bits(100.0f));
    me_render_raw_selftest_store_u32(
        bombs, ME_BULLET_FAST_BOMB_CLEAR_POS_Y_OFFSET,
        me_render_float_bits(99.5f));
    me_render_raw_selftest_store_u32(
        bombs, ME_BULLET_FAST_BOMB_CLEAR_POS_Z_OFFSET,
        me_render_float_bits(32.0f));
    me_render_raw_selftest_store_u32(
        bombs, ME_BULLET_FAST_BOMB_CLEAR_SIZE_X_OFFSET,
        me_render_float_bits(32.0f));
    job.bombClearHighWater = 1u;
    job.frameSeq++;
    if (th07_psp_me_bullet_fast_update_run(
            &job, &edgeCompletion, &edgeOutput) != 1 || !edgeOutput ||
        edgeCompletion.noCollisionCount != 0u ||
        (edgeOutput->slots[0].flags &
         TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION) != 0u)
        goto cleanup;
    job.bombClearHighWater = 0u;
    job.playerState = ME_BULLET_FAST_PLAYER_STATE_BORDER;
    job.frameSeq++;
    if (th07_psp_me_bullet_fast_update_run(
            &job, &edgeCompletion, &edgeOutput) != 1 || !edgeOutput ||
        edgeCompletion.noCollisionCount != 0u ||
        (edgeOutput->slots[0].flags &
         TH07_PSP_ME_BULLET_FAST_SLOT_NO_COLLISION) != 0u)
        goto cleanup;

    // Local guard and SC bounds gates must both reject without publishing a
    // command or damaging the process-lifetime arena.
    gMeBulletFastOutputArea.guard0[0] ^= 0xffu;
    if (me_bullet_fast_guards_match(
            (const volatile unsigned char *)&gMeBulletFastOutputArea))
        goto cleanup;
    gMeBulletFastOutputArea.guard0[0] ^= 0xffu;
    Th07PspMeBulletFastJob badJob = job;
    ++badJob.layout.bulletStride;
    if (th07_psp_me_bullet_fast_update_run(
            &badJob, &edgeCompletion, &edgeOutput) != 0 || edgeOutput != 0 ||
        __atomic_load_n(&gMeBulletFastInFlight, __ATOMIC_ACQUIRE) != 0u)
        goto cleanup;

    sceKernelDcacheWritebackInvalidateRange(
        &gMeBulletFastOutputArea, sizeof(gMeBulletFastOutputArea));
    th07_psp_boot_note(
        "ME16 SELFTEST PASS RESULT-ONLY C0/128/512/1024 EDRAM0");
    passed = 1;

cleanup:
    free(bombs);
    free(activeBits);
    free(generations);
    free(sprites);
    free(bullets);
    return passed;
}
#endif

#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
static int selftest_bullet_compact_update(void)
{
    static unsigned char bombs
        [ME_BULLET_COMPACT_BOMB_CLEAR_CAPACITY *
         ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE]
        __attribute__((aligned(64)));
    static const uint32_t counts[4] = {0u, 128u, 512u, 1024u};
#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
    uint32_t main512 = 0u;
    uint32_t stage512 = 0u;
    uint32_t mirror512 = 0u;
    uint32_t main1024 = 0u;
    uint32_t stage1024 = 0u;
    uint32_t mirror1024 = 0u;
    th07_psp_boot_notef(
        "MEED SELFTEST BEGIN AREA%08lx SEED%08lx BYTES%lu RUNTIME0",
        (unsigned long)ME_EDRAM_SEED_BENCH_AREA_BASE,
        (unsigned long)ME_EDRAM_SEED_BENCH_SEED_BASE,
        (unsigned long)sizeof(Th07PspMeBulletCompactSeed));
#endif
    memset(bombs, 0, sizeof(bombs));

    for (uint32_t caseIndex = 0u; caseIndex < 4u; ++caseIndex)
    {
        const uint32_t count = counts[caseIndex];
        const uint32_t bank = caseIndex & 1u;
        MeBulletCompactSeedArea *area =
            &gMeBulletCompactSeedAreas[bank];
        memset(&area->seed, 0, sizeof(area->seed));
        Th07PspMeBulletCompactSeed *seed = &area->seed;
        seed->header.version = TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION;
        seed->header.headerBytes = sizeof(seed->header);
        seed->header.seedBytes = sizeof(*seed);
        seed->header.backend =
            TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM;
        seed->header.bank = bank;
        seed->header.frameSeq = 0x1700u + caseIndex;
        seed->header.targetDrawSeq = 0x2700u + caseIndex;
        seed->header.stageEpoch = 7u;
        seed->header.managerEpoch = 9u;
        // Deliberately advances each case like normal replay playback.  The
        // compact command accepts it only as the immutable seed echo.
        seed->header.replayEpoch = 0x3700u + caseIndex;
        seed->header.recordCount = count;
        seed->header.candidateCount = count;
        seed->header.payloadHash = 0u;
        seed->header.commitSequence = seed->header.frameSeq;
        seed->header.reserved = 0u;
        for (uint32_t slot = 0u; slot < count; ++slot)
        {
            seed->candidateBits[slot >> 5u] |= 1u << (slot & 31u);
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, generation) =
                0x10000u + slot + 1u;
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posXBits) =
                me_render_float_bits(
                64.0f + (float)(slot & 7u) * 0.5f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posYBits) =
                me_render_float_bits(100.0f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, posZBits) =
                me_render_float_bits(0.25f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityXBits) =
                me_render_float_bits(0.25f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityYBits) =
                me_render_float_bits(-0.5f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, velocityZBits) =
                me_render_float_bits(0.125f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteWidthBits) =
                me_render_float_bits(16.0f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, spriteHeightBits) =
                me_render_float_bits(16.0f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeXBits) =
                me_render_float_bits(4.0f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, grazeSizeYBits) =
                me_render_float_bits(4.0f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosXBits) =
                me_render_float_bits(
                64.25f + (float)(slot & 7u) * 0.5f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosYBits) =
                me_render_float_bits(99.5f);
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosZBits) =
                me_render_float_bits(0.375f);
            seed->inBoundsBits[slot >> 5u] |= 1u << (slot & 31u);
#else
            Th07PspMeBulletCompactSeedSlot *record = &seed->slots[slot];
            record->generation = 0x10000u + slot + 1u;
            record->posXBits = me_render_float_bits(
                64.0f + (float)(slot & 7u) * 0.5f);
            record->posYBits = me_render_float_bits(100.0f);
            record->posZBits = me_render_float_bits(0.25f);
            record->velocityXBits = me_render_float_bits(0.25f);
            record->velocityYBits = me_render_float_bits(-0.5f);
            record->velocityZBits = me_render_float_bits(0.125f);
            record->spriteWidthBits = me_render_float_bits(16.0f);
            record->spriteHeightBits = me_render_float_bits(16.0f);
            record->grazeSizeXBits = me_render_float_bits(4.0f);
            record->grazeSizeYBits = me_render_float_bits(4.0f);
            record->nextPosXBits = me_render_float_bits(
                64.25f + (float)(slot & 7u) * 0.5f);
            record->nextPosYBits = me_render_float_bits(99.5f);
            record->nextPosZBits = me_render_float_bits(0.375f);
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
            seed->inBoundsBits[slot >> 5u] |= 1u << (slot & 31u);
#else
            record->staticFlags =
                TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
                TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS;
#endif
#endif
        }
        __asm__ volatile("sync");
        seed->header.committed =
            TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED;
        sceKernelDcacheWritebackInvalidateRange(area, sizeof(*area));

        Th07PspMeBulletCompactJob job;
        memset(&job, 0, sizeof(job));
        job.version = TH07_PSP_ME_BULLET_COMPACT_VERSION;
        job.frameSeq = caseIndex + 1u;
        job.flags =
            TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID;
        job.seedBank = bank;
        job.seedFrameSeq = seed->header.frameSeq;
        job.seedTargetDrawSeq = seed->header.targetDrawSeq;
        job.stageEpoch = seed->header.stageEpoch;
        job.managerEpoch = seed->header.managerEpoch;
        job.replayEpoch = seed->header.replayEpoch;
        job.playerState = 0u;
        job.playerGrazeLeftBits = me_render_float_bits(10000.0f);
        job.playerGrazeTopBits = me_render_float_bits(10000.0f);
        job.playerGrazeRightBits = me_render_float_bits(10001.0f);
        job.playerGrazeBottomBits = me_render_float_bits(10001.0f);
        job.playerHitboxLeftBits = me_render_float_bits(10000.0f);
        job.playerHitboxTopBits = me_render_float_bits(10000.0f);
        job.playerHitboxRightBits = me_render_float_bits(10001.0f);
        job.playerHitboxBottomBits = me_render_float_bits(10001.0f);
        job.bombClearBasePhys = (uint32_t)bombs & 0x1fffffffu;
        job.bombClearHighWater = 0u;
        job.bombClearCapacity =
            ME_BULLET_COMPACT_BOMB_CLEAR_CAPACITY;
        job.playfieldRightBits = me_render_float_bits(384.0f);
        job.playfieldBottomBits = me_render_float_bits(448.0f);
        if (!th07_psp_me_bullet_compact_begin(&job))
            return 0;

        Th07PspMeBulletCompactCompletion completion;
        const Th07PspMeBulletCompactOutput *output = 0;
        const Th07PspMeBulletCompactSeed *publishedSeed = 0;
        int poll = 0;
        const uint32_t startUs = sceKernelGetSystemTimeLow();
        while ((poll = th07_psp_me_bullet_compact_poll(
                    &completion, &output, &publishedSeed)) == 0)
        {
            if (sceKernelGetSystemTimeLow() - startUs >=
                ME_BULLET_COMPACT_TIMEOUT_US)
                return 0;
            sceKernelDelayThread(20);
        }
        if (poll != 1 || !output || !publishedSeed ||
            completion.result != TH07_PSP_ME_BULLET_COMPACT_RESULT_OK ||
            completion.candidateCount != count ||
            completion.inBoundsCount != count ||
            completion.noCollisionCount != count ||
            completion.firstBadSlot != 0xffffffffu)
            return 0;
        if (count != 0u)
        {
            const uint32_t slot = count - 1u;
            const Th07PspMeBulletCompactSlotResult *result =
                &output->slots[slot];
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
            if (result->posXBits !=
                    TH07_PSP_ME_BULLET_SEED_FIELD(
                        publishedSeed, slot, nextPosXBits) ||
                result->posYBits !=
                    TH07_PSP_ME_BULLET_SEED_FIELD(
                        publishedSeed, slot, nextPosYBits) ||
                result->posZBits !=
                    TH07_PSP_ME_BULLET_SEED_FIELD(
                        publishedSeed, slot, nextPosZBits) ||
                result->generation !=
#else
            if (result->generation !=
#endif
                    (uint16_t)TH07_PSP_ME_BULLET_SEED_FIELD(
                        publishedSeed, slot, generation) ||
#else
#if !defined(TH07_PSP_ME_BULLET_OUTPUT_SLIM)
            if (result->posXBits !=
                    publishedSeed->slots[slot].nextPosXBits ||
                result->posYBits !=
                    publishedSeed->slots[slot].nextPosYBits ||
                result->posZBits !=
                    publishedSeed->slots[slot].nextPosZBits ||
                result->generation !=
#else
            if (result->generation !=
#endif
                    (uint16_t)publishedSeed->slots[slot].generation ||
#endif
                result->flags !=
                    (TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
                     TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS |
                     TH07_PSP_ME_BULLET_COMPACT_SLOT_NO_COLLISION))
                return 0;
        }
#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
        uint32_t mainP50 = 0u;
        uint32_t stageP50 = 0u;
        uint32_t mirrorP50 = 0u;
        if (!dispatch_edram_seed_bench(
                &job, count, &mainP50, &stageP50, &mirrorP50))
        {
            th07_psp_boot_notef("MEED SELFTEST NG N%lu -> COLD REBOOT",
                                (unsigned long)count);
            return 0;
        }
        if (count == 512u)
        {
            main512 = mainP50;
            stage512 = stageP50;
            mirror512 = mirrorP50;
        }
        else if (count == 1024u)
        {
            main1024 = mainP50;
            stage1024 = stageP50;
            mirror1024 = mirrorP50;
        }
#endif
    }

#if defined(TH07_PSP_ME_EDRAM_SEED_BENCH)
    const uint32_t cpuMHz = scePowerGetCpuClockFrequency();
    const uint32_t minWinCycles = (cpuMHz * 400u + 1u) / 2u;
    // This is the only symmetric, immediately implementable A/B here:
    // both paths start from the already-published Main-RAM seed and end with
    // the same Main-RAM output.  ST adds Main-to-local staging and substitutes
    // the local kernel.  L2MT is diagnostic only and never drives GO.
    const uint32_t denseDelta = main1024 > stage1024
        ? main1024 - stage1024
        : 0u;
    const int denseTenPercent = main1024 != 0u &&
        (uint64_t)stage1024 * 100u <= (uint64_t)main1024 * 90u;
    const int midNonRegress = stage512 <= main512;
    const int promote = denseDelta >= minWinCycles &&
                        denseTenPercent && midNonRegress;
    th07_psp_boot_note(
        "MEED SELFTEST PASS CASES4 MM0 GUARD0 HASH0 RUNTIME0");
    th07_psp_boot_notef(
        "MEED DECISION GO%d PATH=MAIN-vs-STAGE M512%lu/S512%lu/"
        "L2M512%lu M1024%lu/S1024%lu/L2M1024%lu D%lu NEED%lu "
        "PCT%d MID%d",
        promote, (unsigned long)main512, (unsigned long)stage512,
        (unsigned long)mirror512,
        (unsigned long)main1024, (unsigned long)stage1024,
        (unsigned long)mirror1024, (unsigned long)denseDelta,
        (unsigned long)minWinCycles, denseTenPercent, midNonRegress);
#endif

    for (uint32_t bank = 0u;
         bank < TH07_PSP_ME_BULLET_COMPACT_BANKS; ++bank)
    {
        gMeBulletCompactSeedAreas[bank].seed.header.committed = 0u;
        sceKernelDcacheWritebackInvalidateRange(
            &gMeBulletCompactSeedAreas[bank],
            sizeof(gMeBulletCompactSeedAreas[bank]));
    }
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
    // Keep hardware logs self-identifying.  The build ID distinguishes the
    // trusted-reader half of the D1 matrix; this line proves that the worker
    // and SC agreed on the BS13 plane ABI before gameplay is allowed to run.
    th07_psp_boot_note(
        "D1 SEED BS13 SOA14 STRIDE1040 BYTES58560");
#endif
    th07_psp_boot_note(
        "ME17 SELFTEST PASS COMPACT C0/128/512/1024 MAINRAM");
    return 1;
}

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
enum
{
    ME_ITEM_MOTION_SELFTEST_FATAL = -1,
    ME_ITEM_MOTION_SELFTEST_SAFE_FAIL = 0,
    ME_ITEM_MOTION_SELFTEST_PASS = 1,
    ME_ITEM_MOTION_SELFTEST_CASES = 5,
    ME_ITEM_MOTION_SELFTEST_MAX_RECORDS = 5
};

typedef struct MeItemMotionSelftestExpected
{
    Th07PspMeItemMotionOutputHeader header;
    uint32_t candidateBits[TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS];
    uint32_t slotCount;
    uint32_t slots[ME_ITEM_MOTION_SELFTEST_MAX_RECORDS];
    Th07PspMeItemMotionSlotResult
        results[ME_ITEM_MOTION_SELFTEST_MAX_RECORDS];
} MeItemMotionSelftestExpected;

static void me_item_motion_selftest_seed_slot(
    Th07PspMeItemMotionSeed *seed, uint32_t slot, uint32_t state,
    uint32_t autoCollect, float posX, float posY, float posZ,
    float startX, float startY, float startZ,
    float targetX, float targetY, float targetZ,
    int32_t timerCurrent, float timerSubFrame)
{
    Th07PspMeItemMotionSeedSlot *record = &seed->slots[slot];
    seed->candidateBits[slot >> 5u] |= 1u << (slot & 31u);
    ++seed->header.candidateCount;
    record->generation = 0x1a100u + slot;
    record->posXBits = me_render_float_bits(posX);
    record->posYBits = me_render_float_bits(posY);
    record->posZBits = me_render_float_bits(posZ);
    record->startXBits = me_render_float_bits(startX);
    record->startYBits = me_render_float_bits(startY);
    record->startZBits = me_render_float_bits(startZ);
    record->targetXBits = me_render_float_bits(targetX);
    record->targetYBits = me_render_float_bits(targetY);
    record->targetZBits = me_render_float_bits(targetZ);
    record->timerCurrent = timerCurrent;
    record->timerSubFrameBits = me_render_float_bits(timerSubFrame);
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    const uint32_t bit = 1u << (slot & 31u);
    const uint32_t word = slot >> 5u;
    if ((state & 1u) != 0u)
        seed->stateBit0[word] |= bit;
    if ((state & 2u) != 0u)
        seed->stateBit1[word] |= bit;
    if (autoCollect != 0u)
        seed->autoCollectBits[word] |= bit;
#else
    record->stateAndFlags =
        state |
        (autoCollect << TH07_PSP_ME_ITEM_MOTION_INPUT_AUTOCOLLECT_SHIFT) |
        (1u << TH07_PSP_ME_ITEM_MOTION_INPUT_INUSE_SHIFT);
#endif
}

static void me_item_motion_selftest_prepare(
    uint32_t caseIndex, unsigned char *bombs,
    Th07PspMeBulletCompactJob *job,
    Th07PspMeBulletCompactSeed **bulletSeed,
    Th07PspMeItemMotionSeed **itemSeed, int *expectItemReject)
{
    const uint32_t bank = caseIndex & 1u;
    MeBulletCompactSeedArea *bulletArea =
        &gMeBulletCompactSeedAreas[bank];
    MeItemMotionSeedArea *itemArea = &gMeItemMotionSeedAreas[bank];
    memset(&bulletArea->seed, 0, sizeof(bulletArea->seed));
    memset(&itemArea->seed, 0, sizeof(itemArea->seed));
    *bulletSeed = &bulletArea->seed;
    *itemSeed = &itemArea->seed;
    *expectItemReject = caseIndex == 4u;

    Th07PspMeBulletCompactSeed *const bs = *bulletSeed;
    bs->header.version = TH07_PSP_ME_BULLET_COMPACT_SEED_VERSION;
    bs->header.headerBytes = sizeof(bs->header);
    bs->header.seedBytes = sizeof(*bs);
    bs->header.backend = TH07_PSP_ME_BULLET_COMPACT_BACKEND_MAIN_RAM;
    bs->header.bank = bank;
    bs->header.frameSeq = 0x1a00u + caseIndex;
    bs->header.targetDrawSeq = bs->header.frameSeq + 1u;
    bs->header.stageEpoch = 0x71u;
    bs->header.managerEpoch = 0x91u;
    bs->header.replayEpoch = 0x171u + caseIndex;
    bs->header.recordCount = 1u;
    bs->header.candidateCount = 1u;
    bs->header.commitSequence = bs->header.frameSeq;
    bs->candidateBits[0] = 1u;
#if defined(TH07_PSP_ME_BULLET_SEED_SOA)
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, generation) = 0x10001u;
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, posXBits) = me_render_float_bits(64.0f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, posYBits) = me_render_float_bits(100.0f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, posZBits) = me_render_float_bits(0.25f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, velocityXBits) = me_render_float_bits(0.25f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, velocityYBits) = me_render_float_bits(-0.5f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, velocityZBits) = me_render_float_bits(0.125f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, spriteWidthBits) = me_render_float_bits(16.0f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, spriteHeightBits) = me_render_float_bits(16.0f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, grazeSizeXBits) = me_render_float_bits(4.0f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, grazeSizeYBits) = me_render_float_bits(4.0f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, nextPosXBits) = me_render_float_bits(64.25f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, nextPosYBits) = me_render_float_bits(99.5f);
    TH07_PSP_ME_BULLET_SEED_FIELD(bs, 0u, nextPosZBits) = me_render_float_bits(0.375f);
    bs->inBoundsBits[0] = 1u;
#else
    bs->slots[0].generation = 0x10001u;
    bs->slots[0].posXBits = me_render_float_bits(64.0f);
    bs->slots[0].posYBits = me_render_float_bits(100.0f);
    bs->slots[0].posZBits = me_render_float_bits(0.25f);
    bs->slots[0].velocityXBits = me_render_float_bits(0.25f);
    bs->slots[0].velocityYBits = me_render_float_bits(-0.5f);
    bs->slots[0].velocityZBits = me_render_float_bits(0.125f);
    bs->slots[0].spriteWidthBits = me_render_float_bits(16.0f);
    bs->slots[0].spriteHeightBits = me_render_float_bits(16.0f);
    bs->slots[0].grazeSizeXBits = me_render_float_bits(4.0f);
    bs->slots[0].grazeSizeYBits = me_render_float_bits(4.0f);
    bs->slots[0].nextPosXBits = me_render_float_bits(64.25f);
    bs->slots[0].nextPosYBits = me_render_float_bits(99.5f);
    bs->slots[0].nextPosZBits = me_render_float_bits(0.375f);
#if defined(TH07_PSP_ME_BULLET_SEED_SLIM)
    bs->inBoundsBits[0] = 1u;
#else
    bs->slots[0].staticFlags =
        TH07_PSP_ME_BULLET_COMPACT_SLOT_CANDIDATE |
        TH07_PSP_ME_BULLET_COMPACT_SLOT_IN_BOUNDS;
#endif
#endif
    __asm__ volatile("sync");
    bs->header.committed = TH07_PSP_ME_BULLET_COMPACT_SEED_COMMITTED;

    Th07PspMeItemMotionSeed *const is = *itemSeed;
    is->header.version = TH07_PSP_ME_ITEM_MOTION_VERSION;
    is->header.headerBytes = sizeof(is->header);
    is->header.seedBytes = sizeof(*is);
    is->header.bank = bank;
    is->header.frameSeq = bs->header.frameSeq;
    is->header.targetDrawSeq = bs->header.targetDrawSeq;
    is->header.stageEpoch = bs->header.stageEpoch;
    is->header.managerEpoch = bs->header.managerEpoch;
    is->header.itemPrepareSerial = 0x51u + caseIndex;
    is->header.commitSequence = is->header.frameSeq;

    memset(job, 0, sizeof(*job));
    job->version = TH07_PSP_ME_BULLET_COMPACT_VERSION;
    job->frameSeq = 0x1b00u + caseIndex;
    job->flags =
        TH07_PSP_ME_BULLET_COMPACT_JOB_COLLISION_SNAPSHOT_VALID |
        TH07_PSP_ME_BULLET_COMPACT_JOB_ITEM_MOTION_VALID;
    job->seedBank = bank;
    job->seedFrameSeq = bs->header.frameSeq;
    job->seedTargetDrawSeq = bs->header.targetDrawSeq;
    job->stageEpoch = bs->header.stageEpoch;
    job->managerEpoch = bs->header.managerEpoch;
    job->replayEpoch = bs->header.replayEpoch;
    job->playerGrazeLeftBits = me_render_float_bits(10000.0f);
    job->playerGrazeTopBits = me_render_float_bits(10000.0f);
    job->playerGrazeRightBits = me_render_float_bits(10001.0f);
    job->playerGrazeBottomBits = me_render_float_bits(10001.0f);
    job->playerHitboxLeftBits = me_render_float_bits(10000.0f);
    job->playerHitboxTopBits = me_render_float_bits(10000.0f);
    job->playerHitboxRightBits = me_render_float_bits(10001.0f);
    job->playerHitboxBottomBits = me_render_float_bits(10001.0f);
    job->bombClearBasePhys = (uint32_t)bombs & 0x1fffffffu;
    job->bombClearCapacity = ME_BULLET_COMPACT_BOMB_CLEAR_CAPACITY;
    job->playfieldRightBits = me_render_float_bits(384.0f);
    job->playfieldBottomBits = me_render_float_bits(448.0f);
    job->itemPlayerPosXBits = me_render_float_bits(100.0f);
    job->itemPlayerPosYBits = me_render_float_bits(80.0f);
    job->itemCollectSpeedBits = me_render_float_bits(4.0f);
    job->itemPocYBits = me_render_float_bits(128.0f);
    job->itemFramerateMultiplierBits = me_render_float_bits(1.0f);

    if (caseIndex == 0u)
    {
        job->itemMotionCandidateLimit = 5u;
        job->itemCurrentPowerClass = 128;
        job->itemDifficulty = 4;
        job->itemHasBorder = 1u;
        me_item_motion_selftest_seed_slot(
            is, 0u, 1u, 0u, 10.0f, 80.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f);
        // dx==dy==0 exercises the canonical pi/2 special case.
        me_item_motion_selftest_seed_slot(
            is, 1u, 1u, 1u, 100.0f, 80.0f, 0.5f,
            1.0f, 2.0f, 0.25f, 0.0f, 0.0f, 0.0f, 0, 0.0f);
        me_item_motion_selftest_seed_slot(
            is, 2u, 2u, 0u, 1.0f, 2.0f, 3.0f,
            10.0f, 20.0f, 30.0f, 70.0f, 80.0f, 90.0f, 30, 0.0f);
        me_item_motion_selftest_seed_slot(
            is, 3u, 2u, 0u, 2.0f, 3.0f, 4.0f,
            5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 60, 0.0f);
        me_item_motion_selftest_seed_slot(
            is, 4u, 2u, 0u, 3.0f, 4.0f, 5.0f,
            0.5f, -0.75f, 0.25f, 8.0f, 9.0f, 10.0f, 61, 0.0f);
        is->header.recordCount = is->header.totalCount = 5u;
    }
    else if (caseIndex == 1u)
    {
        job->playerState = 1u;
        job->itemMotionCandidateLimit = 2u;
        job->itemCurrentPowerClass = 128;
        job->itemDifficulty = 4;
        job->itemHasBorder = 1u;
        me_item_motion_selftest_seed_slot(
            is, 0u, 1u, 0u, 11.0f, 22.0f, 0.0f,
            1.0f, -3.0f, 0.25f, 0.0f, 0.0f, 0.0f, 0, 0.0f);
        me_item_motion_selftest_seed_slot(
            is, 1u, 0u, 1u, 33.0f, 44.0f, 0.0f,
            -1.0f, -2.0f, -0.25f, 0.0f, 0.0f, 0.0f, 0, 0.0f);
        is->header.recordCount = is->header.totalCount = 2u;
    }
    else if (caseIndex == 2u || caseIndex == 4u)
    {
        job->itemPlayerPosYBits = me_render_float_bits(200.0f);
        job->itemMotionCandidateLimit = caseIndex == 2u ? 1u : 2u;
        job->itemCurrentPowerClass = 0;
        job->itemDifficulty = 0;
        job->itemHasBorder = 0u;
        me_item_motion_selftest_seed_slot(
            is, 0u, 0u, 0u, 50.0f, 60.0f, 0.0f,
            3.0f, -3.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f);
        me_item_motion_selftest_seed_slot(
            is, 1u, 0u, 0u, 70.0f, 80.0f, 0.0f,
            -3.0f, -1.5f, -2.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f);
        is->header.recordCount = is->header.totalCount = 2u;
        if (*expectItemReject)
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
        {
            // State 3 is not representable by Item.  Prove that the compact
            // bitplanes reject it without relying on a removed padding word.
            is->stateBit0[0] |= 1u;
            is->stateBit1[0] |= 1u;
        }
#else
            is->slots[0].reserved0 = 1u;
#endif
    }
    else
    {
        // Nontrivial third-quadrant HOME plus non-integral interpolation.
        // This case's raw words come from the independent Allegrex/newlib
        // oracle in tests/me_item_motion_golden_psp.c, not this kernel.
        job->itemMotionCandidateLimit = 2u;
        job->itemFramerateMultiplierBits = me_render_float_bits(0.75f);
        job->itemCurrentPowerClass = 128;
        job->itemDifficulty = 4;
        job->itemHasBorder = 1u;
        me_item_motion_selftest_seed_slot(
            is, 0u, 1u, 0u, 124.0f, 112.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f);
        me_item_motion_selftest_seed_slot(
            is, 1u, 2u, 0u, 1.0f, 2.0f, 3.0f,
            10.0f, 20.0f, 30.0f, 70.0f, 80.0f, 90.0f, 30, 0.25f);
        is->header.recordCount = is->header.totalCount = 2u;
    }
    __asm__ volatile("sync");
    is->header.committed = TH07_PSP_ME_ITEM_MOTION_COMMITTED;
    sceKernelDcacheWritebackInvalidateRange(bulletArea, sizeof(*bulletArea));
    sceKernelDcacheWritebackInvalidateRange(itemArea, sizeof(*itemArea));
}

typedef struct MeItemMotionGoldenSlot
{
    uint32_t caseIndex;
    uint32_t slot;
    Th07PspMeItemMotionSlotResult result;
} MeItemMotionGoldenSlot;

// These are raw IEEE-754 results from the canonical Item update equations,
// deliberately independent from me_item_motion_update_kernel().  Every route
// is represented.  Case 3 was generated by compiling the standalone oracle
// in tests/me_item_motion_golden_psp.c against this PSP SDK's newlib 4.5.0 and
// executing that Allegrex ELF; it covers multiplier=.75, timer subframe=.25,
// and a third-quadrant atan2f.  Case 0 also retains the zero-delta HOME path.
static int me_item_motion_selftest_golden_matches(
    uint32_t caseIndex, const Th07PspMeItemMotionOutput *output)
{
    static const MeItemMotionGoldenSlot golden[] = {
        {0u, 0u, {0x0001a100u, 0x41600000u, 0x42a00000u, 0x00000000u,
                  0x40800000u, 0x00000000u, 0x00000000u, 0x02010101u}},
        {0u, 2u, {0x0001a102u, 0x42200000u, 0x42480000u, 0x42700000u,
                  0x41200000u, 0x41a00000u, 0x41f00000u, 0x04030002u}},
        {0u, 3u, {0x0001a103u, 0x40000000u, 0x40400000u, 0x40800000u,
                  0x00000000u, 0x00000000u, 0x00000000u, 0x05010000u}},
        {0u, 4u, {0x0001a104u, 0x40600000u, 0x40500000u, 0x40a80000u,
                  0x3f000000u, 0xbf400000u, 0x3e800000u, 0x06010002u}},
        {1u, 0u, {0x0001a100u, 0x41400000u, 0x41ac0000u, 0x3e800000u,
                  0x3f800000u, 0xbf000000u, 0x3e800000u, 0x03010000u}},
        {1u, 1u, {0x0001a101u, 0x42000000u, 0x422e0000u, 0xbe800000u,
                  0xbf800000u, 0xbf000000u, 0xbe800000u, 0x03010100u}},
        {2u, 0u, {0x0001a100u, 0x42480000u, 0x42673333u, 0x00000000u,
                  0x00000000u, 0xc00ccccdu, 0x00000000u, 0x01010000u}},
        {3u, 0u, {0x0001a100u, 0x42f46666u, 0x42db3333u, 0x00000000u,
                  0xc019999bu, 0xc04cccccu, 0x00000000u, 0x02010101u}},
        {3u, 1u, {0x0001a101u, 0x42210000u, 0x42490000u, 0x42710000u,
                  0x41200000u, 0x41a00000u, 0x41f00000u, 0x04030002u}},
    };
    const uint32_t expectedBitmap = caseIndex == 0u ? 0x1fu :
                                    (caseIndex == 1u || caseIndex == 3u)
                                        ? 0x03u : 0x01u;
    const uint32_t expectedProcessed = caseIndex == 0u ? 5u :
                                       (caseIndex == 1u || caseIndex == 3u)
                                           ? 2u : 1u;
    if (!output || output->candidateBits[0] != expectedBitmap ||
        output->header.processedCount != expectedProcessed)
        return 0;
    for (uint32_t index = 0u;
         index < sizeof(golden) / sizeof(golden[0]); ++index)
    {
        if (golden[index].caseIndex != caseIndex)
            continue;
        if (memcmp(&output->slots[golden[index].slot],
                   &golden[index].result,
                   sizeof(golden[index].result)) != 0)
            return 0;
    }
    return 1;
}

static int me_item_motion_selftest_one(
    uint32_t caseIndex, unsigned char *bombs, unsigned int *failureReason)
{
    Th07PspMeBulletCompactJob job;
    Th07PspMeBulletCompactSeed *bulletSeed;
    Th07PspMeItemMotionSeed *itemSeed;
    int expectItemReject;
    me_item_motion_selftest_prepare(
        caseIndex, bombs, &job, &bulletSeed, &itemSeed, &expectItemReject);

    MeItemMotionSelftestExpected expected;
    memset(&expected, 0, sizeof(expected));
    uint32_t expectedCandidates = 0u;
    uint32_t expectedProcessed = 0u;
    uint32_t expectedFirstBad = 0xffffffffu;
    const uint32_t originalFcr31 = me_render_read_fcr31();
    me_render_write_fcr31(0u);
    // Match the production contract: verify the effective mode before libm
    // runs.  atan2f/cosf/sinf may legitimately set sticky inexact afterward.
    const uint32_t effectiveFcr31 = me_render_read_fcr31();
    const uint32_t expectedResult = me_item_motion_update_kernel(
        &job, itemSeed, &gMeItemMotionOutputArea.output,
        &expectedCandidates, &expectedProcessed, &expectedFirstBad);
    me_render_write_fcr31(originalFcr31);
    const uint32_t restoredFcr31 = me_render_read_fcr31();
    if (effectiveFcr31 != 0u || restoredFcr31 != originalFcr31 ||
        ((!expectItemReject && expectedResult !=
             TH07_PSP_ME_ITEM_MOTION_RESULT_OK) ||
         (expectItemReject && expectedResult !=
             TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD)))
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_COMMON_FATAL;
        return ME_ITEM_MOTION_SELFTEST_FATAL;
    }
    if (!expectItemReject && !me_item_motion_selftest_golden_matches(
                                 caseIndex,
                                 &gMeItemMotionOutputArea.output))
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_BIT_MISMATCH;
        return ME_ITEM_MOTION_SELFTEST_SAFE_FAIL;
    }
    if (!expectItemReject)
    {
        expected.header = gMeItemMotionOutputArea.output.header;
        memcpy(expected.candidateBits,
               gMeItemMotionOutputArea.output.candidateBits,
               sizeof(expected.candidateBits));
        for (uint32_t slot = 0u;
             slot < itemSeed->header.recordCount; ++slot)
        {
            if ((expected.candidateBits[slot >> 5u] &
                 (1u << (slot & 31u))) == 0u)
                continue;
            const uint32_t index = expected.slotCount++;
            expected.slots[index] = slot;
            expected.results[index] =
                gMeItemMotionOutputArea.output.slots[slot];
        }
    }

    if (!th07_psp_me_bullet_compact_begin(&job))
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_BEGIN;
        return ME_ITEM_MOTION_SELFTEST_FATAL;
    }
    Th07PspMeBulletCompactCompletion completion;
    const Th07PspMeBulletCompactOutput *output = 0;
    const Th07PspMeBulletCompactSeed *publishedSeed = 0;
    int poll = 0;
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    while ((poll = th07_psp_me_bullet_compact_poll(
                &completion, &output, &publishedSeed)) == 0)
    {
        if (sceKernelGetSystemTimeLow() - startUs >=
            ME_BULLET_COMPACT_TIMEOUT_US)
        {
            *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_COMMON_FATAL;
            return ME_ITEM_MOTION_SELFTEST_FATAL;
        }
        sceKernelDelayThread(20);
    }
    __atomic_store_n(&gMeItemMotionDiagLastPollResult, poll,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagLastBulletResult,
                     completion.result, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagLastItemResult,
                     completion.itemResult, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagFirstMismatchSlot,
                     completion.itemFirstBadSlot, __ATOMIC_RELEASE);
    if (poll != 1 || !output || !publishedSeed ||
        completion.result != TH07_PSP_ME_BULLET_COMPACT_RESULT_OK)
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_COMMON_FATAL;
        return ME_ITEM_MOTION_SELFTEST_FATAL;
    }

    const Th07PspMeItemMotionOutput *actual =
        th07_psp_me_item_motion_last_output();
    if (expectItemReject)
    {
        if (completion.itemResult != TH07_PSP_ME_ITEM_MOTION_RESULT_RECORD ||
            actual != 0)
        {
            *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_ITEM_CONTRACT;
            return ME_ITEM_MOTION_SELFTEST_SAFE_FAIL;
        }
        return ME_ITEM_MOTION_SELFTEST_PASS;
    }
    if (!actual ||
        completion.itemResult != TH07_PSP_ME_ITEM_MOTION_RESULT_OK ||
        completion.itemCandidateCount != expectedCandidates ||
        completion.itemProcessedCount != expectedProcessed ||
        completion.itemFirstBadSlot != expectedFirstBad ||
        actual->header.version != expected.header.version ||
        actual->header.bank != expected.header.bank ||
        actual->header.frameSeq != expected.header.frameSeq ||
        actual->header.seedFrameSeq != expected.header.seedFrameSeq ||
        actual->header.seedTargetDrawSeq != expected.header.seedTargetDrawSeq ||
        actual->header.result != expected.header.result ||
        actual->header.candidateLimit != expected.header.candidateLimit ||
        actual->header.candidateCount != expected.header.candidateCount ||
        actual->header.processedCount != expected.header.processedCount ||
        actual->header.firstBadSlot != expected.header.firstBadSlot ||
        memcmp(actual->candidateBits, expected.candidateBits,
               sizeof(expected.candidateBits)) != 0)
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_ITEM_CONTRACT;
        return ME_ITEM_MOTION_SELFTEST_SAFE_FAIL;
    }
    for (uint32_t index = 0u; index < expected.slotCount; ++index)
    {
        const uint32_t slot = expected.slots[index];
        if (memcmp(&actual->slots[slot], &expected.results[index],
                   sizeof(expected.results[index])) != 0)
        {
            __atomic_store_n(&gMeItemMotionDiagFirstMismatchSlot, slot,
                             __ATOMIC_RELEASE);
            *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_BIT_MISMATCH;
            return ME_ITEM_MOTION_SELFTEST_SAFE_FAIL;
        }
    }
    return ME_ITEM_MOTION_SELFTEST_PASS;
}

static int me_item_motion_selftest_capture_probe(
    unsigned int *failureReason)
{
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM) && \
    defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
    if (!gMeItemMotionSelftestInProgress)
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_COMMON_FATAL;
        return ME_ITEM_MOTION_SELFTEST_FATAL;
    }

    // Reuse the already-proven direct-list fixtures, but rebuild Item slot 0
    // from zero so this probe independently exercises the actual command-10
    // tail capture and commit transaction.
    memset(gMeRenderItemSelftestItem, 0,
           sizeof(gMeRenderItemSelftestItem));
    gMeRenderItemSelftestGeneration[0] = 13u;
    gMeRenderItemSelftestGeneration[1] = 14u;
    gMeRenderItemSelftestActiveBits[0] = 3u;
    gMeRenderItemSelftestSin[0] = me_render_float_bits(0.0f);
    gMeRenderItemSelftestCos[0] = me_render_float_bits(1.0f);
    gMeRenderItemSelftestPrepareSerial = 0x49374d01u;
    gMeRenderItemSelftestPreparedSerial =
        gMeRenderItemSelftestPrepareSerial;
    gMeRenderItemSelftestPreparedCount = 2u;
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_ROTATION_Z_OFFSET,
        me_render_float_bits(0.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_SCALE_X_OFFSET,
        me_render_float_bits(1.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_SCALE_Y_OFFSET,
        me_render_float_bits(1.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_COLOR_OFFSET,
        0xffffffffu);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_COLOR2_OFFSET,
        0xffffffffu);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_FLAGS_OFFSET,
        ME_RENDER_RAW_VM_VISIBLE | ME_RENDER_RAW_VM_ACTIVE);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_RAW_VM_SPRITE_OFFSET,
        (uint32_t)gMeRenderRawSelftestSprite);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_VM_POS_X_OFFSET,
        me_render_float_bits(1.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_VM_POS_Y_OFFSET,
        me_render_float_bits(2.0f));
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_VM_POS_Z_OFFSET,
        me_render_float_bits(3.0f));
    const uint32_t motionWords[] = {
        0x3f800000u, 0x40000000u, 0x40400000u,
        0x41200000u, 0x41a00000u, 0x41f00000u,
        0x428c0000u, 0x42a00000u, 0x42b40000u
    };
    for (uint32_t word = 0u; word < 3u; ++word)
    {
        me_render_raw_selftest_store_u32(
            gMeRenderItemSelftestItem,
            ME_ITEM_MOTION_CURRENT_POS_OFFSET + word * 4u,
            motionWords[word]);
        me_render_raw_selftest_store_u32(
            gMeRenderItemSelftestItem,
            ME_ITEM_MOTION_START_POS_OFFSET + word * 4u,
            motionWords[3u + word]);
        me_render_raw_selftest_store_u32(
            gMeRenderItemSelftestItem,
            ME_ITEM_MOTION_TARGET_POS_OFFSET + word * 4u,
            motionWords[6u + word]);
    }
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_ITEM_MOTION_TIMER_SUBFRAME_OFFSET,
        0x3f000000u);
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_ITEM_MOTION_TIMER_CURRENT_OFFSET, 30u);
    gMeRenderItemSelftestItem[ME_ITEM_MOTION_STATE_OFFSET] = 2u;
    gMeRenderItemSelftestItem[ME_ITEM_MOTION_AUTOCOLLECT_OFFSET] = 1u;
    me_render_raw_selftest_store_u32(
        gMeRenderItemSelftestItem, ME_RENDER_ITEM_NEXT_OFFSET,
        (uint32_t)(gMeRenderItemSelftestItem + ME_RENDER_ITEM_STRIDE));
    gMeRenderItemSelftestItem[ME_RENDER_ITEM_TYPE_OFFSET] = 2u;
    gMeRenderItemSelftestItem[ME_RENDER_ITEM_IN_USE_OFFSET] = 1u;

    Th07PspMeRenderStreamBuild build;
    if (!th07_psp_me_render_stream_acquire(&build))
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_BEGIN;
        return ME_ITEM_MOTION_SELFTEST_FATAL;
    }
    Th07PspMeRenderStreamJob renderJob;
    me_render_raw_selftest_job_common(&renderJob, &build.token);
    renderJob.version = TH07_PSP_ME_RENDER_STREAM_ITEM_VERSION;
    renderJob.flags = TH07_PSP_ME_RENDER_STREAM_JOB_DIRECT_LIST |
                      TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST |
                      TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_MOTION_SEED |
                      TH07_PSP_ME_RENDER_STREAM_JOB_HASH_OUTPUT;
    me_render_raw_selftest_layout(&renderJob.rawLayout);
    me_render_list_selftest_layout(&renderJob.listLayout);
    me_render_item_selftest_layout(&renderJob.itemLayout);
    if (!th07_psp_me_render_stream_submit(&renderJob))
    {
        (void)th07_psp_me_render_stream_cancel_build(&build.token);
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_BEGIN;
        return ME_ITEM_MOTION_SELFTEST_FATAL;
    }
    Th07PspMeRenderStreamCompletion completion;
    Th07PspMeRenderStreamReady ready;
    memset(&completion, 0, sizeof(completion));
    memset(&ready, 0, sizeof(ready));
    const int waited = me_render_raw_selftest_wait(
        &build.token, &completion, &ready);
    if (!waited ||
        completion.result != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        completion.itemResult != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        completion.itemRecordCount != 1u || ready.itemVertexCount != 4u ||
        !th07_psp_me_render_stream_release_ready(&build.token))
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_COMMON_FATAL;
        return ME_ITEM_MOTION_SELFTEST_FATAL;
    }

    const uint32_t bank = renderJob.frameSeq &
        (TH07_PSP_ME_ITEM_MOTION_BANKS - 1u);
    MeItemMotionSeedArea *area = &gMeItemMotionSeedAreas[bank];
    // ME owns the complete seed payload.  Full invalidation is intentional in
    // this startup-only proof; runtime reads keep their narrow/JIT policy.
    sceKernelDcacheInvalidateRange(area, sizeof(*area));
    const Th07PspMeItemMotionSeed *seed = &area->seed;
    const Th07PspMeItemMotionSeedSlot *slot = &seed->slots[0];
    int bitmapValid = seed->candidateBits[0] == 1u;
    for (uint32_t word = 1u;
         word < TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS; ++word)
    {
        if (seed->candidateBits[word] != 0u)
            bitmapValid = 0;
    }
    const int headerValid =
        me_item_motion_seed_guards_match(
            (const volatile unsigned char *)area) &&
        me_item_motion_seed_header_valid(seed, bank) &&
        seed->header.frameSeq == renderJob.frameSeq &&
        seed->header.targetDrawSeq == renderJob.targetDrawSeq &&
        seed->header.stageEpoch == renderJob.stageEpoch &&
        seed->header.managerEpoch == renderJob.managerEpoch &&
        seed->header.itemPrepareSerial ==
            gMeRenderItemSelftestPrepareSerial &&
        seed->header.recordCount == 1u &&
        seed->header.totalCount == 2u &&
        seed->header.candidateCount == 1u &&
        bitmapValid;
    const uint32_t *slotWords = (const uint32_t *)slot;
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    bitmapValid = bitmapValid && seed->stateBit0[0] == 0u &&
        seed->stateBit1[0] == 1u &&
        seed->autoCollectBits[0] == 1u;
    for (uint32_t word = 1u;
         word < TH07_PSP_ME_ITEM_MOTION_BITMAP_WORDS; ++word)
    {
        if (seed->stateBit0[word] != 0u || seed->stateBit1[word] != 0u ||
            seed->autoCollectBits[word] != 0u)
            bitmapValid = 0;
    }
    const uint32_t expectedSlotWords[12] = {
        13u,
        0x3f800000u, 0x40000000u, 0x40400000u,
        0x41200000u, 0x41a00000u, 0x41f00000u,
        0x428c0000u, 0x42a00000u, 0x42b40000u,
        30u, 0x3f000000u
    };
#else
    const uint32_t expectedStateFlags =
        2u | (1u << TH07_PSP_ME_ITEM_MOTION_INPUT_AUTOCOLLECT_SHIFT) |
        (1u << TH07_PSP_ME_ITEM_MOTION_INPUT_INUSE_SHIFT);
    const uint32_t expectedSlotWords[16] = {
        13u,
        0x3f800000u, 0x40000000u, 0x40400000u,
        0x41200000u, 0x41a00000u, 0x41f00000u,
        0x428c0000u, 0x42a00000u, 0x42b40000u,
        30u, 0x3f000000u, expectedStateFlags, 0u, 0u, 0u
    };
#endif
    if (!headerValid || !bitmapValid ||
        memcmp(slotWords, expectedSlotWords,
               sizeof(expectedSlotWords)) != 0)
    {
        *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_ITEM_CONTRACT;
        return ME_ITEM_MOTION_SELFTEST_SAFE_FAIL;
    }
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    th07_psp_boot_note("A1M COMMAND10 SIDECAR PASS IM02 C1");
#else
    th07_psp_boot_note("A1M COMMAND10 SIDECAR PASS IM01 C1");
#endif
    return ME_ITEM_MOTION_SELFTEST_PASS;
#else
    (void)failureReason;
    return ME_ITEM_MOTION_SELFTEST_FATAL;
#endif
}

static int selftest_item_motion_update(unsigned int *failureReason)
{
    static unsigned char bombs
        [ME_BULLET_COMPACT_BOMB_CLEAR_CAPACITY *
         ME_BULLET_COMPACT_BOMB_CLEAR_STRIDE]
        __attribute__((aligned(64)));
    memset(bombs, 0, sizeof(bombs));
    *failureReason = TH07_PSP_ME_ITEM_MOTION_REASON_NONE;
    const int captureProbe = me_item_motion_selftest_capture_probe(
        failureReason);
    if (captureProbe != ME_ITEM_MOTION_SELFTEST_PASS)
        return captureProbe;
    for (uint32_t caseIndex = 0u;
         caseIndex < ME_ITEM_MOTION_SELFTEST_CASES; ++caseIndex)
    {
        const int result = me_item_motion_selftest_one(
            caseIndex, bombs, failureReason);
        if (result != ME_ITEM_MOTION_SELFTEST_PASS)
            return result;
    }
    // Normalize the deliberate Item RECORD probe to the published PASS
    // contract so one boot-note/telemetry line cannot be misread as failure.
    __atomic_store_n(&gMeItemMotionDiagLastPollResult, 1,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagLastBulletResult,
                     TH07_PSP_ME_BULLET_COMPACT_RESULT_OK,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagLastItemResult,
                     TH07_PSP_ME_ITEM_MOTION_RESULT_OK,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagFirstMismatchSlot, 0xffffffffu,
                     __ATOMIC_RELEASE);
    return ME_ITEM_MOTION_SELFTEST_PASS;
}

static int me_item_motion_failure_recoverable(void)
{
    return gMeItemMotionSelftestInProgress && gMeMailboxUncached &&
           __atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&gMeUnsafe, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&gMeBulletCompactInFlight, __ATOMIC_ACQUIRE) &&
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
           __atomic_load_n(&gMeRenderStreamInFlightSlot,
                           __ATOMIC_ACQUIRE) == 0xffffffffu &&
#endif
           __atomic_load_n(&gMeOwner, __ATOMIC_ACQUIRE) == ME_OWNER_NONE &&
           gMeMailboxUncached->command == ME_CMD_NONE &&
           gMeMailboxUncached->status == ME_STAT_DONE &&
           gMeMailboxUncached->workerState == ME_WORKER_READY &&
           gMeMailboxUncached->suspendRequested == 0u &&
           gMeMailboxUncached->stackFault == 0u;
}

static void me_item_motion_selftest_cleanup(void)
{
    me_item_motion_reset_arenas_on_sc();
    for (uint32_t bank = 0u;
         bank < TH07_PSP_ME_BULLET_COMPACT_BANKS; ++bank)
    {
        gMeBulletCompactSeedAreas[bank].seed.header.committed = 0u;
        sceKernelDcacheWritebackInvalidateRange(
            &gMeBulletCompactSeedAreas[bank],
            sizeof(gMeBulletCompactSeedAreas[bank]));
    }
}
#endif
#endif

void th07_psp_me_render_bench_snapshot(
    Th07PspMeRenderBenchSummary *summary,
    Th07PspMeRenderBenchCase *cases,
    unsigned int caseCapacity)
{
    if (summary)
        *summary = gMeRenderBenchSummary;
    if (!cases || !caseCapacity)
        return;
    uint32_t count = gMeRenderBenchSummary.caseCount;
    if (count > TH07_PSP_ME_RENDER_BENCH_CASES)
        count = TH07_PSP_ME_RENDER_BENCH_CASES;
    if (count > caseCapacity)
        count = caseCapacity;
    memcpy(cases, gMeRenderBenchCases, count * sizeof(*cases));
}
#endif

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
static int wait_for_worker_state(uint32_t wanted, uint32_t timeoutUs)
{
    const uint32_t startUs = sceKernelGetSystemTimeLow();
    while (gMeMailboxUncached->workerState != wanted)
    {
        if (sceKernelGetSystemTimeLow() - startUs >= timeoutUs)
            return 0;
        sceKernelDelayThread(1000);
    }
    return 1;
}
#endif

#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
static void initialize_render_stream_slots(void)
{
    memset(gMeRenderStreamInputAreas, 0,
           sizeof(gMeRenderStreamInputAreas));
    memset(gMeRenderStreamOutputAreas, 0,
           sizeof(gMeRenderStreamOutputAreas));
    memset(gMeRenderStreamRunAreas, 0,
           sizeof(gMeRenderStreamRunAreas));
    memset(gMeRenderStreamSlots, 0, sizeof(gMeRenderStreamSlots));
    for (uint32_t slot = 0u;
         slot < TH07_PSP_ME_RENDER_STREAM_SLOT_COUNT; ++slot)
    {
        memset(gMeRenderStreamInputAreas[slot].guard0,
               ME_RENDER_STREAM_GUARD_PATTERN,
               sizeof(gMeRenderStreamInputAreas[slot].guard0));
        memset(gMeRenderStreamInputAreas[slot].guard1,
               ME_RENDER_STREAM_GUARD_PATTERN,
               sizeof(gMeRenderStreamInputAreas[slot].guard1));
        memset(gMeRenderStreamOutputAreas[slot].guard0,
               ME_RENDER_STREAM_GUARD_PATTERN,
               sizeof(gMeRenderStreamOutputAreas[slot].guard0));
        memset(gMeRenderStreamOutputAreas[slot].guard1,
               ME_RENDER_STREAM_GUARD_PATTERN,
               sizeof(gMeRenderStreamOutputAreas[slot].guard1));
        memset(gMeRenderStreamRunAreas[slot].guard0,
               ME_RENDER_STREAM_GUARD_PATTERN,
               sizeof(gMeRenderStreamRunAreas[slot].guard0));
        memset(gMeRenderStreamRunAreas[slot].guard1,
               ME_RENDER_STREAM_GUARD_PATTERN,
               sizeof(gMeRenderStreamRunAreas[slot].guard1));
        gMeRenderStreamSlots[slot].state =
            TH07_PSP_ME_RENDER_STREAM_STATE_FREE;
    }
    gMeRenderStreamInFlightSlot = 0xffffffffu;
    gMeRenderStreamSubmitted = 0u;
    gMeRenderStreamCompleted = 0u;
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
    gMeRenderStreamDraining = 0u;
#endif
#if defined(TH07_PSP_ME_RENDER_RETIRE_DIAG)
    gMeRenderStreamRetireDiagLogged = 0u;
#endif
    gMeRenderStreamStartUs = 0u;
    gMeRenderStreamScWritebackUs = 0u;
    gMeRenderStreamScOutputPrepareUs = 0u;
    gMeRenderStreamScSubmitUs = 0u;
    sceKernelDcacheWritebackInvalidateRange(
        gMeRenderStreamInputAreas, sizeof(gMeRenderStreamInputAreas));
    sceKernelDcacheWritebackInvalidateRange(
        gMeRenderStreamOutputAreas, sizeof(gMeRenderStreamOutputAreas));
    sceKernelDcacheWritebackInvalidateRange(
        gMeRenderStreamRunAreas, sizeof(gMeRenderStreamRunAreas));
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
int th07_psp_me_item_render_available(void)
{
    return __atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&gMeItemRenderEnabled, __ATOMIC_ACQUIRE);
}

void th07_psp_me_item_render_diag_snapshot(Th07PspMeItemRenderDiag *snapshot)
{
    if (!snapshot)
        return;
    snapshot->itemState =
        __atomic_load_n(&gMeItemDiagState, __ATOMIC_ACQUIRE);
    snapshot->itemReason =
        __atomic_load_n(&gMeItemDiagReason, __ATOMIC_ACQUIRE);
    snapshot->itemSelftestRuns =
        __atomic_load_n(&gMeItemDiagSelftestRuns, __ATOMIC_ACQUIRE);
    snapshot->itemSelftestFailures =
        __atomic_load_n(&gMeItemDiagSelftestFailures, __ATOMIC_ACQUIRE);
    snapshot->bulletRetryRuns =
        __atomic_load_n(&gMeItemDiagBulletRetryRuns, __ATOMIC_ACQUIRE);
    snapshot->bulletRetryPasses =
        __atomic_load_n(&gMeItemDiagBulletRetryPasses, __ATOMIC_ACQUIRE);
    snapshot->lastWaitResult =
        __atomic_load_n(&gMeItemDiagLastWaitResult, __ATOMIC_ACQUIRE);
    snapshot->lastStreamResult =
        __atomic_load_n(&gMeItemDiagLastStreamResult, __ATOMIC_ACQUIRE);
    snapshot->lastItemResult =
        __atomic_load_n(&gMeItemDiagLastItemResult, __ATOMIC_ACQUIRE);
}

static int me_render_stream_item_failure_recoverable(void)
{
    return gMeItemSelftestInProgress && gMeMailboxUncached &&
           __atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&gMeUnsafe, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&gMeRenderStreamInFlightSlot,
                           __ATOMIC_ACQUIRE) == 0xffffffffu &&
           gMeMailboxUncached->command == ME_CMD_NONE &&
           gMeMailboxUncached->status == ME_STAT_DONE &&
           gMeMailboxUncached->workerState == ME_WORKER_READY &&
           gMeMailboxUncached->suspendRequested == 0u &&
           gMeMailboxUncached->stackFault == 0u;
}
#endif
#endif

int th07_psp_me_audio_init(void)
{
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    if (__atomic_load_n(&gMeResetCommitted, __ATOMIC_ACQUIRE))
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC BGM REINIT DENIED -> COLD REBOOT");
        return -1;
    }
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (__atomic_load_n(&gMeUnsafe, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE))
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M POWER STATE REINIT DENIED -> COLD REBOOT");
        return -1;
    }
#endif
#endif
    gMeMailboxUncached =
        (volatile MeSharedMailbox *)(0x40000000u | (uint32_t)&gMeMailbox);
    memset((void *)&gMeMailbox, 0, sizeof(gMeMailbox));
    memset(gMeAudioOutput, 0, sizeof(gMeAudioOutput));
    memset(gMeVertexArena, 0, sizeof(gMeVertexArena));
#if defined(TH07_PSP_MECC_AUDIO_4M)
    memset(gMeAudioWide, 0, sizeof(gMeAudioWide));
    initialize_main_stack();
#endif
    gMeOwner = ME_OWNER_NONE;
    gMeAudioWanted = 0;
#if defined(TH07_PSP_MECC_AUDIO_4M)
    gMeOwnerThread = -1;
    gMeOwnerOriginalPriority = -1;
#endif
    gMePoisoned = 0;
    gMeVertexArenaOffset = 0;
#if defined(TH07_PSP_ME_RENDER_WORKER)
    memset(&gMeRenderBenchSummary, 0, sizeof(gMeRenderBenchSummary));
    memset(gMeRenderBenchCases, 0, sizeof(gMeRenderBenchCases));
    memset(&gMeRenderPublishedJob, 0, sizeof(gMeRenderPublishedJob));
    gMeRenderInFlight = 0u;
    gMeRenderSubmitted = 0u;
    gMeRenderCompleted = 0u;
    gMeRenderStartUs = 0u;
    gMeRenderScWritebackUs = 0u;
    gMeRenderScOutputPrepareUs = 0u;
    gMeRenderScSubmitUs = 0u;
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    memset(&gMeBulletFastOutputArea, 0,
           sizeof(gMeBulletFastOutputArea));
    memset(&gMeBulletFastPublishedJob, 0,
           sizeof(gMeBulletFastPublishedJob));
    gMeBulletFastInFlight = 0u;
    gMeBulletFastStartUs = 0u;
    gMeBulletFastScWritebackUs = 0u;
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    memset(&gMeBulletCompactSeedAreas, 0,
           sizeof(gMeBulletCompactSeedAreas));
    for (uint32_t bank = 0u;
         bank < TH07_PSP_ME_BULLET_COMPACT_BANKS; ++bank)
    {
        memset(gMeBulletCompactSeedAreas[bank].guard0,
               ME_BULLET_COMPACT_GUARD_PATTERN,
               sizeof(gMeBulletCompactSeedAreas[bank].guard0));
        memset(gMeBulletCompactSeedAreas[bank].guard1,
               ME_BULLET_COMPACT_GUARD_PATTERN,
               sizeof(gMeBulletCompactSeedAreas[bank].guard1));
    }
    memset(&gMeBulletCompactOutputArea, 0,
           sizeof(gMeBulletCompactOutputArea));
    memset(gMeBulletCompactOutputArea.guard0,
           ME_BULLET_COMPACT_GUARD_PATTERN,
           sizeof(gMeBulletCompactOutputArea.guard0));
    memset(gMeBulletCompactOutputArea.guard1,
           ME_BULLET_COMPACT_GUARD_PATTERN,
           sizeof(gMeBulletCompactOutputArea.guard1));
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    me_item_motion_reset_arenas_on_sc();
    __atomic_store_n(&gMeItemMotionEnabled, 0u, __ATOMIC_RELEASE);
    gMeItemMotionSelftestInProgress = 0u;
    __atomic_store_n(&gMeItemMotionDiagState,
                     TH07_PSP_ME_ITEM_MOTION_STATE_UNAVAILABLE,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagReason,
                     TH07_PSP_ME_ITEM_MOTION_REASON_ME_UNAVAILABLE,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagSelftestRuns, 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagSelftestFailures, 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagBulletRetryRuns, 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagBulletRetryPasses, 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagLastPollResult, -1,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagLastBulletResult, 0xffffffffu,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagLastItemResult, 0xffffffffu,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionDiagFirstMismatchSlot, 0xffffffffu,
                     __ATOMIC_RELEASE);
#endif
    memset(&gMeBulletCompactPublishedJob, 0,
           sizeof(gMeBulletCompactPublishedJob));
    gMeBulletCompactInFlight = 0u;
    gMeBulletCompactStartUs = 0u;
    gMeBulletCompactSeedInvalidateUs = 0u;
#endif
    gMeRenderBenchSummary.version = TH07_PSP_ME_RENDER_VERSION;
    gMeRenderBenchSummary.prxBytes = embedded_kcall_len;
    gMeRenderBenchSummary.meEdramBytes = 0u;
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    initialize_render_stream_slots();
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    __atomic_store_n(&gMeItemRenderEnabled, 0u, __ATOMIC_RELEASE);
    gMeItemSelftestInProgress = 0u;
    __atomic_store_n(&gMeItemDiagState,
                     TH07_PSP_ME_ITEM_STATE_UNAVAILABLE,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagReason,
                     TH07_PSP_ME_ITEM_REASON_ME_UNAVAILABLE,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagSelftestRuns, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagSelftestFailures, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagBulletRetryRuns, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagBulletRetryPasses, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastWaitResult, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastStreamResult, 0xffffffffu,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemDiagLastItemResult, 0xffffffffu,
                     __ATOMIC_RELEASE);
#endif
#endif
#endif
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    gMeBgmOwned = 0;
    gMeUnsafe = 0;
#endif
    sceKernelDcacheWritebackInvalidateAll();

    if (running_under_ppsspp())
    {
        th07_psp_boot_note("ME AUDIO OFF (PPSSPP -> SC)");
        return 0;
    }
    if (me_disabled_marker_present())
    {
        th07_psp_boot_note("ME AUDIO OFF (MARKER -> SC)");
        return 0;
    }

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    const int model = kuKernelGetModel();
#if defined(TH07_PSP_MECC_AUDIO_4M)
    th07_psp_boot_notef("MECC AUDIO4M MODEL %d", model);
#else
    th07_psp_boot_notef("MECC BGM MODEL %d", model);
#endif
    if (model != ME_BGM_REQUIRED_MODEL)
    {
        th07_psp_boot_note("MECC BGM OFF (MODEL != PSP-3000)");
        return 0;
    }
#endif

#if defined(TH07_PSP_MECC_AUDIO_4M)
    // Acquire before meLibDefaultInit crosses the custom-core ownership
    // boundary.  A pending power-switch request remains deferred until the ME
    // is stopped and both Main-RAM stack guards are confirmed below.
    if (!acquire_power_lock())
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M POWER LOCK FAILED -> NO TAKEOVER");
        return -1;
    }
#endif

    th07_psp_boot_note("ME AUDIO INIT BEGIN (PSPPMD)");
#if defined(TH07_PSP_ME_RENDER_WORKER)
    const uint32_t meTakeoverStartUs = sceKernelGetSystemTimeLow();
#endif
    const int mapper = meLibDefaultInit();
#if defined(TH07_PSP_ME_RENDER_WORKER)
    gMeRenderBenchSummary.takeoverUs =
        sceKernelGetSystemTimeLow() - meTakeoverStartUs;
    gMeRenderBenchSummary.prxWriteUs = meLibPrxWriteUs;
    gMeRenderBenchSummary.prxLoadUs = meLibPrxLoadUs;
    gMeRenderBenchSummary.prxWriteResult = meLibPrxWriteResult;
    gMeRenderBenchSummary.prxLoadResult = meLibPrxLoadResult;
    th07_psp_boot_notef("MERW TAKEOVER %luus PRX %luB W%luus/R%d L%luus/R%d",
                        (unsigned long)gMeRenderBenchSummary.takeoverUs,
                        (unsigned long)gMeRenderBenchSummary.prxBytes,
                        (unsigned long)gMeRenderBenchSummary.prxWriteUs,
                        gMeRenderBenchSummary.prxWriteResult,
                        (unsigned long)gMeRenderBenchSummary.prxLoadUs,
                        gMeRenderBenchSummary.prxLoadResult);
#endif
    th07_psp_boot_notef("ME AUDIO INIT R%d", mapper);
    if (mapper < 0)
    {
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
        // meLibDefaultInit loads the bridge and may replace the Sony MeRpc
        // event callback before a mapper error is returned.  Do not claim a
        // reversible SC fallback after crossing that boundary.
        __atomic_store_n(&gMeResetCommitted, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC BGM TAKEOVER FAILED -> COLD REBOOT");
        return -1;
#else
        th07_psp_boot_note("ME AUDIO OFF (SC FALLBACK)");
        return 0;
#endif
    }
    __atomic_store_n(&gMeStarted, 1, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    __atomic_store_n(&gMeResetCommitted, 1, __ATOMIC_RELEASE);
    if (mapper != ME_BGM_REQUIRED_TABLE)
    {
        (void)wait_for_worker_state(ME_WORKER_READY, ME_BGM_READY_TIMEOUT_US);
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC BGM TABLE NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
    }
    if (!wait_for_worker_state(ME_WORKER_READY, ME_BGM_READY_TIMEOUT_US))
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC BGM READY TIMEOUT -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
    }
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (!__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE) ||
        gMeMailboxUncached->workerState != ME_WORKER_READY ||
        gMeMailboxUncached->suspendRequested != 0u ||
        gMeMailboxUncached->stackFault || !stack_guards_match_on_sc())
    {
        gMeMailboxUncached->stackFault = 1;
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M STACK GUARD NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
    }
#endif
    __atomic_store_n(&gMeActive, 1, __ATOMIC_RELEASE);
#else
    __atomic_store_n(&gMeActive, 1, __ATOMIC_RELEASE);
    sceKernelDelayThread(50000);
#endif

    if (!selftest_audio())
    {
        __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("ME AUDIO SELFTEST NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
#else
        th07_psp_boot_note("ME AUDIO SELFTEST NG -> ALL SC");
        return 0;
#endif
    }
    gMeVertexArenaOffset = 0;
    if (!selftest_vertices())
    {
        __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("ME CORE SELFTEST NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
#else
        th07_psp_boot_note("ME CORE SELFTEST NG -> ALL SC");
        return 0;
#endif
    }

#if defined(TH07_PSP_ME_RENDER_WORKER)
    if (!selftest_render_bench())
    {
        __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MERW M0A SELFTEST NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
    }
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
    int renderStreamSelftestPassed;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    __atomic_store_n(&gMeItemRenderEnabled, 1u, __ATOMIC_RELEASE);
#endif
    renderStreamSelftestPassed = selftest_render_stream();
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
    if (renderStreamSelftestPassed)
        renderStreamSelftestPassed = selftest_render_stream_c1_m0();
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (!renderStreamSelftestPassed &&
        me_render_stream_item_failure_recoverable())
    {
        // Item is an optional optimization.  A clean Item-only failure must
        // never turn a playable RID22-class Bullet worker into an XMB loop.
        // ME is idle here, so discard every startup slot, permanently close
        // Item admission, and rerun the independent Bullet ownership proof.
        __atomic_store_n(&gMeItemRenderEnabled, 0u, __ATOMIC_RELEASE);
        gMeItemSelftestInProgress = 0u;
        initialize_render_stream_slots();
        th07_psp_boot_note(
            "ME1A SELFTEST NG -> ITEM OFF; RETRY BULLET ME");
        __atomic_add_fetch(&gMeItemDiagBulletRetryRuns, 1u,
                           __ATOMIC_ACQ_REL);
        renderStreamSelftestPassed = selftest_render_stream();
        if (renderStreamSelftestPassed)
        {
            __atomic_add_fetch(&gMeItemDiagBulletRetryPasses, 1u,
                               __ATOMIC_ACQ_REL);
            __atomic_store_n(&gMeItemDiagState,
                             TH07_PSP_ME_ITEM_STATE_SAFE_FALLBACK,
                             __ATOMIC_RELEASE);
            th07_psp_boot_note(
                "ME ITEM OFF; BULLET ME ACTIVE (SAFE FALLBACK)");
        }
        else
        {
            __atomic_store_n(&gMeItemDiagReason,
                             TH07_PSP_ME_ITEM_REASON_BULLET_RETRY_FAILED,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&gMeItemDiagState,
                             TH07_PSP_ME_ITEM_STATE_FAILED,
                             __ATOMIC_RELEASE);
        }
    }
#endif
    if (!renderStreamSelftestPassed)
    {
#if defined(TH07_PSP_ME_RENDER_UV16) || \
    defined(TH07_PSP_ME_RENDER_XYZ16)
        if (gMeRenderC1M0SafeFailure)
        {
            // C1 is optional.  Its M0 worker is DONE, no ME/GE owner remains,
            // every guard is intact, and the bad slot has never reached GE.
            // Close all admission first, discard the boot-only quarantined
            // pools, then stop Custom Core before returning the established
            // init result 0 (main.cpp selects the canonical all-SC path).
            __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            __atomic_store_n(&gMeItemRenderEnabled, 0u,
                             __ATOMIC_RELEASE);
            gMeItemSelftestInProgress = 0u;
#endif
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
            __atomic_store_n(&gMeItemMotionEnabled, 0u,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&gMeItemMotionOutputValid, 0u,
                             __ATOMIC_RELEASE);
            gMeItemMotionSelftestInProgress = 0u;
#endif
            initialize_render_stream_slots();
            th07_psp_boot_note(
                "MERW C1M0 CLEAN NG -> C1/ME OFF; STOP FOR ALL SC");
            th07_psp_me_audio_shutdown();
            if (!__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) &&
                !__atomic_load_n(&gMeUnsafe, __ATOMIC_ACQUIRE) &&
                !__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE))
            {
                th07_psp_boot_note(
                    "MERW C1 OFF; CANONICAL SC CONTINUE");
                return 0;
            }
            __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
            th07_psp_boot_note(
                "MERW C1 FALLBACK STOP NG -> COLD REBOOT");
            return -1;
        }
#endif
        __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        th07_psp_boot_note("ME1A SELFTEST NG -> COLD REBOOT");
#elif defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
        th07_psp_boot_note("ME15 SELFTEST NG -> COLD REBOOT");
#elif defined(TH07_PSP_ME_RENDER_RAW_LIVE)
        th07_psp_boot_note("ME14 SELFTEST NG -> COLD REBOOT");
#else
        th07_psp_boot_note("ME11 SELFTEST NG -> COLD REBOOT");
#endif
        th07_psp_me_audio_shutdown();
        return -1;
    }
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
    if (!selftest_bullet_fast_update())
    {
        __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("ME16 SELFTEST NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
    }
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
    if (!selftest_bullet_compact_update())
    {
        __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("ME17 SELFTEST NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
    }
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    if (__atomic_load_n(&gMeItemRenderEnabled, __ATOMIC_ACQUIRE))
    {
        unsigned int itemMotionFailureReason =
            TH07_PSP_ME_ITEM_MOTION_REASON_NONE;
        __atomic_store_n(&gMeItemMotionDiagState,
                         TH07_PSP_ME_ITEM_MOTION_STATE_TESTING,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagReason,
                         TH07_PSP_ME_ITEM_MOTION_REASON_NONE,
                         __ATOMIC_RELEASE);
        __atomic_add_fetch(&gMeItemMotionDiagSelftestRuns, 1u,
                           __ATOMIC_ACQ_REL);
        gMeItemMotionSelftestInProgress = 1u;
        const int itemMotionSelftest = selftest_item_motion_update(
            &itemMotionFailureReason);
        if (itemMotionSelftest == ME_ITEM_MOTION_SELFTEST_PASS)
        {
            gMeItemMotionSelftestInProgress = 0u;
            me_item_motion_selftest_cleanup();
            __atomic_store_n(&gMeItemMotionEnabled, 1u,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&gMeItemMotionDiagReason,
                             TH07_PSP_ME_ITEM_MOTION_REASON_SELFTEST_PASS,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&gMeItemMotionDiagState,
                             TH07_PSP_ME_ITEM_MOTION_STATE_ENABLED,
                             __ATOMIC_RELEASE);
            th07_psp_boot_note(
                "A1M SELFTEST PASS ROUTES6 BITEXACT; ITEM MOVE ON");
        }
        else if (itemMotionSelftest ==
                     ME_ITEM_MOTION_SELFTEST_SAFE_FAIL &&
                 me_item_motion_failure_recoverable())
        {
            // RID28 rule: Item motion is optional and owns disjoint arenas.
            // A completed Item-only mismatch closes only command-10 sidecar
            // admission, destroys those arenas, and reproves base command 12.
            __atomic_store_n(&gMeItemMotionEnabled, 0u,
                             __ATOMIC_RELEASE);
            __atomic_add_fetch(&gMeItemMotionDiagSelftestFailures, 1u,
                               __ATOMIC_ACQ_REL);
            __atomic_store_n(&gMeItemMotionDiagReason,
                             itemMotionFailureReason,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&gMeItemMotionDiagState,
                             TH07_PSP_ME_ITEM_MOTION_STATE_FAILED,
                             __ATOMIC_RELEASE);
            gMeItemMotionSelftestInProgress = 0u;
            me_item_motion_selftest_cleanup();
            th07_psp_boot_note(
                "A1M SELFTEST NG -> MOVE OFF; RETRY ME17");
            __atomic_add_fetch(&gMeItemMotionDiagBulletRetryRuns, 1u,
                               __ATOMIC_ACQ_REL);
            if (selftest_bullet_compact_update())
            {
                __atomic_add_fetch(&gMeItemMotionDiagBulletRetryPasses, 1u,
                                   __ATOMIC_ACQ_REL);
                __atomic_store_n(&gMeItemMotionDiagState,
                                 TH07_PSP_ME_ITEM_MOTION_STATE_SAFE_FALLBACK,
                                 __ATOMIC_RELEASE);
                th07_psp_boot_note(
                    "A1M MOVE OFF; ITEM DRAW+ME17 ACTIVE (SAFE FALLBACK)");
            }
            else
            {
                itemMotionFailureReason =
                    TH07_PSP_ME_ITEM_MOTION_REASON_BULLET_RETRY_FAILED;
                __atomic_store_n(&gMeItemMotionDiagReason,
                                 itemMotionFailureReason,
                                 __ATOMIC_RELEASE);
                __atomic_store_n(&gMeItemMotionDiagState,
                                 TH07_PSP_ME_ITEM_MOTION_STATE_FAILED,
                                 __ATOMIC_RELEASE);
                __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
                th07_psp_boot_note(
                    "A1M ME17 RETRY NG -> COLD REBOOT");
                th07_psp_me_audio_shutdown();
                return -1;
            }
        }
        else
        {
            // Begin/timeout/common mailbox/stack/FCR failures cannot prove
            // that a late ME writer is gone.  Preserve the existing fail-stop.
            gMeItemMotionSelftestInProgress = 0u;
            __atomic_store_n(&gMeItemMotionEnabled, 0u,
                             __ATOMIC_RELEASE);
            __atomic_add_fetch(&gMeItemMotionDiagSelftestFailures, 1u,
                               __ATOMIC_ACQ_REL);
            __atomic_store_n(&gMeItemMotionDiagReason,
                             itemMotionFailureReason ==
                                     TH07_PSP_ME_ITEM_MOTION_REASON_NONE
                                 ? TH07_PSP_ME_ITEM_MOTION_REASON_COMMON_FATAL
                                 : itemMotionFailureReason,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&gMeItemMotionDiagState,
                             TH07_PSP_ME_ITEM_MOTION_STATE_FAILED,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
            th07_psp_boot_note("A1M COMMON SELFTEST NG -> COLD REBOOT");
            th07_psp_me_audio_shutdown();
            return -1;
        }
    }
    else
#endif
    {
        __atomic_store_n(&gMeItemMotionEnabled, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagReason,
                         TH07_PSP_ME_ITEM_MOTION_REASON_ITEM_DRAW_UNAVAILABLE,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&gMeItemMotionDiagState,
                         TH07_PSP_ME_ITEM_MOTION_STATE_UNAVAILABLE,
                         __ATOMIC_RELEASE);
        th07_psp_boot_note("A1M OFF (ITEM DRAW UNAVAILABLE)");
    }
#endif
#endif
#endif

#if defined(TH07_PSP_MECC_AUDIO_4M)
#if !defined(TH07_PSP_SFX_MAIN_RAM)
    if (!selftest_sfx_4m())
    {
        __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M SFX SELFTEST NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
    }
#endif
    // TEST success is not an ownership proof by itself.  Re-snapshot the
    // power/worker/abort state after the last test and before returning a live
    // full-4M backend to the caller.
    if (!__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) ||
        gMeMailboxUncached->workerState != ME_WORKER_READY ||
        gMeMailboxUncached->suspendRequested != 0u ||
        gMeMailboxUncached->stackFault || !stack_guards_match_on_sc())
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M POST-TEST OWNERSHIP NG -> COLD REBOOT");
        th07_psp_me_audio_shutdown();
        return -1;
    }
    th07_psp_boot_note("MECC AUDIO4M MAIN STACK 8K GUARD OK / ACCUM 2K");
#endif

    gMeVertexArenaOffset = 0;
    gMeJobs = gMeFallbacks = gMeTimeouts = gMeMaxWaitUs = 0;
    th07_psp_boot_notef("ME AUDIO ON MAP%d", mapper);
    return 1;
}

void th07_psp_me_audio_shutdown(void)
{
    __atomic_store_n(&gMeActive, 0, __ATOMIC_RELEASE);
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    __atomic_store_n(&gMeItemMotionEnabled, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gMeItemMotionOutputValid, 0u, __ATOMIC_RELEASE);
#endif
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    if (!__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) || !gMeMailboxUncached)
        return;

#if defined(TH07_PSP_ME_RENDER_WORKER)
    if (!me_render_drain_for_shutdown()
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
        || !me_render_stream_drain_for_shutdown()
#endif
#if defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
        || __atomic_load_n(&gMeBulletFastInFlight, __ATOMIC_ACQUIRE) != 0u
#endif
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        || !me_bullet_compact_drain_for_shutdown()
#endif
        )
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_notef(
            "MERW STOP DIAG IF%u P%u A%u ST%u CMD%u R%u W%u SR%u SF%u "
            "SUB%u CMP%u TO%u PF%u",
            __atomic_load_n(&gMeRenderInFlight, __ATOMIC_ACQUIRE),
            __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE),
            __atomic_load_n(&gMeActive, __ATOMIC_ACQUIRE),
            gMeMailboxUncached ? gMeMailboxUncached->status : 0xffffffffu,
            gMeMailboxUncached ? gMeMailboxUncached->command : 0xffffffffu,
            gMeMailboxUncached ? gMeMailboxUncached->renderResult
                               : 0xffffffffu,
            gMeMailboxUncached ? gMeMailboxUncached->workerState
                               : 0xffffffffu,
            gMeMailboxUncached ? gMeMailboxUncached->suspendRequested
                               : 0xffffffffu,
            gMeMailboxUncached ? gMeMailboxUncached->stackFault
                               : 0xffffffffu,
            __atomic_load_n(&gMeRenderSubmitted, __ATOMIC_ACQUIRE),
            __atomic_load_n(&gMeRenderCompleted, __ATOMIC_ACQUIRE),
            gMeRenderBenchSummary.timeouts,
            gMeRenderBenchSummary.protocolFaults);
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
        th07_psp_boot_notef(
            "ME11 STOP DIAG SLOT%u SUB%u CMP%u",
            __atomic_load_n(&gMeRenderStreamInFlightSlot,
                            __ATOMIC_ACQUIRE),
            __atomic_load_n(&gMeRenderStreamSubmitted, __ATOMIC_ACQUIRE),
            __atomic_load_n(&gMeRenderStreamCompleted, __ATOMIC_ACQUIRE));
#endif
        th07_psp_boot_note("MERW STOP DRAIN NG -> COLD REBOOT");
        return;
    }
#endif

    const uint32_t startUs = sceKernelGetSystemTimeLow();
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (!claim_me_for_shutdown(startUs))
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M STOP OWNER BUSY -> COLD REBOOT");
        return;
    }
    if (!__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE) ||
        gMeMailboxUncached->workerState != ME_WORKER_READY ||
        gMeMailboxUncached->suspendRequested != 0u)
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M STOP PRECONDITION NG -> COLD REBOOT");
        return;
    }
#endif
    while (gMeMailboxUncached->command != ME_CMD_NONE &&
           sceKernelGetSystemTimeLow() - startUs < ME_BGM_STOP_TIMEOUT_US)
        sceKernelDelayThread(1000);
    if (gMeMailboxUncached->command != ME_CMD_NONE)
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC BGM STOP BUSY -> COLD REBOOT");
        return;
    }

    gMeMailboxUncached->status = ME_STAT_IDLE;
    __asm__ volatile("sync");
    gMeMailboxUncached->command = ME_CMD_STOP;
#if defined(TH07_PSP_MECC_AUDIO_4M)
    __asm__ volatile("sync");
#endif
    if (!wait_for_worker_state(ME_WORKER_STOPPED, ME_BGM_STOP_TIMEOUT_US) ||
        gMeMailboxUncached->status != ME_STAT_DONE
#if defined(TH07_PSP_MECC_AUDIO_4M)
        || gMeMailboxUncached->command != ME_CMD_NONE
#endif
        )
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC BGM STOP TIMEOUT -> COLD REBOOT");
        return;
    }
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (gMeMailboxUncached->stackFault || !stack_guards_match_on_sc())
    {
        gMeMailboxUncached->stackFault = 1;
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M STACK GUARD NG -> COLD REBOOT");
        return;
    }
#endif
    __atomic_store_n(&gMeStarted, 0, __ATOMIC_RELEASE);
#if defined(TH07_PSP_MECC_AUDIO_4M)
    if (!release_power_lock_after_stop())
        return;
    if (__atomic_load_n(&gMePowerLocked, __ATOMIC_ACQUIRE) ||
        gMeMailboxUncached->workerState != ME_WORKER_STOPPED)
    {
        __atomic_store_n(&gMeUnsafe, 1, __ATOMIC_RELEASE);
        th07_psp_boot_note("MECC AUDIO4M STOP EXIT GATE NG -> COLD REBOOT");
        return;
    }
    release_me();
#endif
    th07_psp_boot_note("MECC BGM STOPPED (COLD REBOOT BEFORE OTHER ME APP)");
#else
    if (!__atomic_load_n(&gMeStarted, __ATOMIC_ACQUIRE) || !gMeMailboxUncached ||
        __atomic_load_n(&gMePoisoned, __ATOMIC_ACQUIRE))
        return;

    const uint32_t startUs = sceKernelGetSystemTimeLow();
    while (gMeMailboxUncached->command != ME_CMD_NONE &&
           sceKernelGetSystemTimeLow() - startUs < ME_AUDIO_WAIT_US)
        sceKernelDelayThread(50);
    if (gMeMailboxUncached->command == ME_CMD_NONE)
    {
        gMeMailboxUncached->status = ME_STAT_IDLE;
        __asm__ volatile("sync");
        gMeMailboxUncached->command = ME_CMD_STOP;
        while (gMeMailboxUncached->status != ME_STAT_DONE &&
               sceKernelGetSystemTimeLow() - startUs < ME_AUDIO_WAIT_US)
            sceKernelDelayThread(50);
    }
    __atomic_store_n(&gMeStarted, 0, __ATOMIC_RELEASE);
#endif
}

void th07_psp_me_audio_diag_window(unsigned int *jobs, unsigned int *fallbacks,
                                   unsigned int *timeouts, unsigned int *maxWaitUs)
{
    if (jobs)
        *jobs = __atomic_exchange_n(&gMeJobs, 0, __ATOMIC_ACQ_REL);
    if (fallbacks)
        *fallbacks = __atomic_exchange_n(&gMeFallbacks, 0, __ATOMIC_ACQ_REL);
    if (timeouts)
        *timeouts = __atomic_exchange_n(&gMeTimeouts, 0, __ATOMIC_ACQ_REL);
    if (maxWaitUs)
        *maxWaitUs = __atomic_exchange_n(&gMeMaxWaitUs, 0, __ATOMIC_ACQ_REL);
}
