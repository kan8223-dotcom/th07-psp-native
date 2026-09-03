#pragma once

#include <cstddef>

#if defined(TH07_PSP_1000)

// PSP-1000 has enough total user RAM for either a decompressed ANM source or
// the gameplay pools, but repeated stage loads leave no single multi-MiB heap
// block.  Reserve one early contiguous block and reuse it for both phases.
bool th07_psp_1000_arena_init();
void *th07_psp_1000_acquire_anm(std::size_t bytes);
bool th07_psp_1000_release_anm(void *ptr);
void th07_psp_1000_trim_to_stage();
#if defined(TH07_PSP_1000_ENEMY_MANIFEST)
bool th07_psp_1000_begin_pools(std::size_t stageExtraBytes);
#else
bool th07_psp_1000_begin_pools();
#endif
void *th07_psp_1000_alloc_pool(std::size_t bytes, std::size_t alignment = 16);
void th07_psp_1000_end_pools();
bool th07_psp_1000_arena_owns(const void *ptr);
std::size_t th07_psp_1000_arena_capacity();
std::size_t th07_psp_1000_pool_bytes_used();

#endif
