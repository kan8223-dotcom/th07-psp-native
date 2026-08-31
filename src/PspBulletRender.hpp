#pragma once

#include "ZunColor.hpp"
#include "inttypes.hpp"

#if defined(TH07_PSP_BULLET_SNAPSHOT_EMITTER)

// A draw-order-preserving, cache-line-sized view of the values consumed by
// AnmManager's PSP bullet emitter.  The live Bullet/AnmVm state is committed
// before this record is produced; this POD owns no gameplay state.
enum PspBulletRenderRecordFlags : u32
{
    PSP_BULLET_RECORD_DRAWABLE = 1u << 0,
    PSP_BULLET_RECORD_CACHED_SINCOS = 1u << 1,
    PSP_BULLET_RECORD_BLEND_ADD = 1u << 2,
    PSP_BULLET_RECORD_ZWRITE_DISABLE = 1u << 3,
    PSP_BULLET_RECORD_ANCHOR_SHIFT = 4,
    PSP_BULLET_RECORD_ANCHOR_MASK = 3u << PSP_BULLET_RECORD_ANCHOR_SHIFT,
};

struct alignas(16) PspBulletRenderRecord
{
    f32 posX;
    f32 posY;
    f32 posZ;
    f32 halfWidth;
    f32 halfHeight;
    f32 rotationZ;
    f32 sine;
    f32 cosine;
    f32 u0;
    f32 u1;
    f32 v0;
    f32 v1;
    ZunColor color;
    i32 sourceFileIndex;
    u32 flags;
};

static_assert(sizeof(PspBulletRenderRecord) == 64,
              "PSP bullet render records must remain one cache line");

#endif
