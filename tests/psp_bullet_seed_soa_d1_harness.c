#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/audio_me.h"

enum
{
    kSlotCount = TH07_PSP_ME_BULLET_COMPACT_MAX_SLOTS,
    kPlaneCount = 14
};

typedef struct ReferenceSlot
{
    uint32_t generation;
    uint32_t posXBits;
    uint32_t posYBits;
    uint32_t posZBits;
    uint32_t velocityXBits;
    uint32_t velocityYBits;
    uint32_t velocityZBits;
    uint32_t spriteWidthBits;
    uint32_t spriteHeightBits;
    uint32_t grazeSizeXBits;
    uint32_t grazeSizeYBits;
    uint32_t nextPosXBits;
    uint32_t nextPosYBits;
    uint32_t nextPosZBits;
} ReferenceSlot;

static const uint32_t kFiniteEdges[] = {
    0x00000000u, /* +0 */
    0x80000000u, /* -0 */
    0x00000001u, /* smallest positive subnormal */
    0x007fffffu, /* largest positive subnormal */
    0x00800000u, /* smallest positive normal */
    0x7f7fffffu, /* largest positive finite */
    0x80800000u, /* smallest negative normal by magnitude */
    0xff7fffffu  /* largest negative finite by magnitude */
};

static int finite_bits(uint32_t bits)
{
    return (bits & 0x7f800000u) != 0x7f800000u;
}

static uint32_t value_for(uint32_t slot, uint32_t plane)
{
    if (plane == 0u)
        return 0x10000u + slot + 1u;
    if (slot < sizeof(kFiniteEdges) / sizeof(kFiniteEdges[0]))
        return kFiniteEdges[(slot + plane - 1u) %
                            (sizeof(kFiniteEdges) / sizeof(kFiniteEdges[0]))];
    return 0x3f000000u ^ (slot * 0x00010101u) ^
           (plane * 0x00100010u);
}

#define FOR_EACH_SEED_FIELD(OP)                                                \
    OP(generation, 0u)                                                         \
    OP(posXBits, 1u)                                                           \
    OP(posYBits, 2u)                                                           \
    OP(posZBits, 3u)                                                           \
    OP(velocityXBits, 4u)                                                      \
    OP(velocityYBits, 5u)                                                      \
    OP(velocityZBits, 6u)                                                      \
    OP(spriteWidthBits, 7u)                                                    \
    OP(spriteHeightBits, 8u)                                                   \
    OP(grazeSizeXBits, 9u)                                                     \
    OP(grazeSizeYBits, 10u)                                                    \
    OP(nextPosXBits, 11u)                                                      \
    OP(nextPosYBits, 12u)                                                      \
    OP(nextPosZBits, 13u)

static void fill_reference(ReferenceSlot *reference, uint32_t count)
{
    memset(reference, 0, sizeof(*reference) * kSlotCount);
    for (uint32_t slot = 0u; slot < count; ++slot)
    {
#define SET_REFERENCE(field, plane) reference[slot].field = value_for(slot, plane);
        FOR_EACH_SEED_FIELD(SET_REFERENCE)
#undef SET_REFERENCE
    }
}

static int verify_case(uint32_t count)
{
    Th07PspMeBulletCompactSeed *seed =
        (Th07PspMeBulletCompactSeed *)calloc(1u, sizeof(*seed));
    ReferenceSlot *reference =
        (ReferenceSlot *)calloc(kSlotCount, sizeof(*reference));
    if (!seed || !reference)
    {
        free(seed);
        free(reference);
        return 1;
    }

    fill_reference(reference, count);
    for (uint32_t slot = 0u; slot < count; ++slot)
    {
#define TRANSPOSE_TO_SOA(field, plane)                                         \
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, field) =                     \
            reference[slot].field;
        FOR_EACH_SEED_FIELD(TRANSPOSE_TO_SOA)
#undef TRANSPOSE_TO_SOA
        seed->candidateBits[slot >> 5u] |= 1u << (slot & 31u);
        if ((slot & 1u) == 0u)
            seed->inBoundsBits[slot >> 5u] |= 1u << (slot & 31u);
    }

    for (uint32_t slot = 0u; slot < kSlotCount; ++slot)
    {
        const uint32_t active = slot < count;
        const uint32_t bit = 1u << (slot & 31u);
        if (((seed->candidateBits[slot >> 5u] & bit) != 0u) != active)
            return 2;
        if (((seed->inBoundsBits[slot >> 5u] & bit) != 0u) !=
            (active && ((slot & 1u) == 0u)))
            return 3;
#define VERIFY_ROUND_TRIP(field, plane)                                        \
        do                                                                      \
        {                                                                       \
            if (TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, field) !=            \
                reference[slot].field)                                          \
                return 4 + (int)(plane);                                        \
        } while (0);
        FOR_EACH_SEED_FIELD(VERIFY_ROUND_TRIP)
#undef VERIFY_ROUND_TRIP
    }

    for (uint32_t slot = 0u;
         slot < sizeof(kFiniteEdges) / sizeof(kFiniteEdges[0]) && slot < count;
         ++slot)
    {
        for (uint32_t plane = 1u; plane < kPlaneCount; ++plane)
        {
            if (!finite_bits(value_for(slot, plane)))
                return 32;
        }
    }

    if (count == kSlotCount)
    {
        const uint32_t slot = kSlotCount - 1u;
        if ((seed->candidateBits[31] & 0x80000000u) == 0u ||
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, generation) !=
                reference[slot].generation ||
            TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, nextPosZBits) !=
                reference[slot].nextPosZBits)
            return 33;
    }

    free(reference);
    free(seed);
    return 0;
}

static int verify_sparse_reuse(void)
{
    static const uint32_t first[] = {0u, 31u, 32u, 511u, 1023u};
    static const uint32_t second[] = {1u, 63u, 512u, 1022u};
    Th07PspMeBulletCompactSeed *seed =
        (Th07PspMeBulletCompactSeed *)calloc(1u, sizeof(*seed));
    if (!seed)
        return 40;

    for (uint32_t index = 0u; index < sizeof(first) / sizeof(first[0]); ++index)
    {
        const uint32_t slot = first[index];
#define SET_SPARSE(field, plane)                                               \
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, field) =                     \
            value_for(slot, plane);
        FOR_EACH_SEED_FIELD(SET_SPARSE)
#undef SET_SPARSE
        seed->candidateBits[slot >> 5u] |= 1u << (slot & 31u);
        seed->inBoundsBits[slot >> 5u] |= 1u << (slot & 31u);
    }

    for (uint32_t index = 0u; index < sizeof(first) / sizeof(first[0]); ++index)
    {
        const uint32_t slot = first[index];
        const uint32_t bit = 1u << (slot & 31u);
        if ((seed->candidateBits[slot >> 5u] & bit) == 0u ||
            (seed->inBoundsBits[slot >> 5u] & bit) == 0u)
            return 41;
#define VERIFY_SPARSE(field, plane)                                            \
        if (TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, field) !=                \
            value_for(slot, plane))                                             \
            return 42 + (int)(plane);
        FOR_EACH_SEED_FIELD(VERIFY_SPARSE)
#undef VERIFY_SPARSE
    }

    memset(seed, 0, sizeof(*seed));
    for (uint32_t index = 0u; index < sizeof(second) / sizeof(second[0]); ++index)
    {
        const uint32_t slot = second[index];
#define SET_REUSED(field, plane)                                               \
        TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, field) =                     \
            (value_for(slot, plane) ^ 0x0055aa55u);
        FOR_EACH_SEED_FIELD(SET_REUSED)
#undef SET_REUSED
        seed->candidateBits[slot >> 5u] |= 1u << (slot & 31u);
    }

    for (uint32_t slot = 0u; slot < kSlotCount; ++slot)
    {
        uint32_t expected = 0u;
        for (uint32_t index = 0u;
             index < sizeof(second) / sizeof(second[0]); ++index)
            expected |= slot == second[index];
        const uint32_t bit = 1u << (slot & 31u);
        if (((seed->candidateBits[slot >> 5u] & bit) != 0u) != expected ||
            (seed->inBoundsBits[slot >> 5u] & bit) != 0u)
            return 60;
        if (expected)
        {
#define VERIFY_REUSED(field, plane)                                            \
            if (TH07_PSP_ME_BULLET_SEED_FIELD(seed, slot, field) !=            \
                (value_for(slot, plane) ^ 0x0055aa55u))                         \
                return 61 + (int)(plane);
            FOR_EACH_SEED_FIELD(VERIFY_REUSED)
#undef VERIFY_REUSED
        }
    }

    free(seed);
    return 0;
}

int main(void)
{
    static const uint32_t counts[] = {0u, 1u, 128u, 512u, 1024u};
    for (uint32_t index = 0u; index < sizeof(counts) / sizeof(counts[0]);
         ++index)
    {
        const int result = verify_case(counts[index]);
        if (result != 0)
        {
            fprintf(stderr, "count %u failed: %d\n", counts[index], result);
            return result;
        }
    }
    {
        const int result = verify_sparse_reuse();
        if (result != 0)
        {
            fprintf(stderr, "sparse reuse failed: %d\n", result);
            return result;
        }
    }
    puts("D1 SoA transpose: 5 counts, sparse reuse, 14 planes, slot1023, finite edges");
    return 0;
}
