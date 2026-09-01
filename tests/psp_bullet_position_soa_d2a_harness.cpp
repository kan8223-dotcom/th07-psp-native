#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/PspBulletPositionSoa.hpp"

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__,       \
                    #condition);                                               \
            return __LINE__;                                                   \
        }                                                                      \
    } while (0)

static int verify_layout(void)
{
    Th07PspBulletPositionSoaShadow shadow;
    const uintptr_t base = (uintptr_t)&shadow;
    const uintptr_t generation = (uintptr_t)&shadow.generation[0];
    const uintptr_t manager = (uintptr_t)&shadow.publishManagerSerial[0];
    const uintptr_t calc = (uintptr_t)&shadow.publishCalcSerial[0];
    const uintptr_t x = (uintptr_t)&shadow.posXBits[0];
    const uintptr_t y = (uintptr_t)&shadow.posYBits[0];
    const uintptr_t z = (uintptr_t)&shadow.posZBits[0];
    const uintptr_t pitch =
        TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE * sizeof(uint32_t);

    CHECK(sizeof(shadow) == 25152u);
    CHECK((base & 63u) == 0u);
    CHECK((generation & 63u) == 0u);
    CHECK((manager & 63u) == 0u);
    CHECK((calc & 63u) == 0u);
    CHECK((x & 63u) == 0u);
    CHECK((y & 63u) == 0u);
    CHECK((z & 63u) == 0u);
    CHECK(manager - generation == pitch);
    CHECK(calc - manager == pitch);
    CHECK(x - calc == pitch);
    CHECK(y - x == pitch);
    CHECK(z - y == pitch);
    CHECK(&shadow.generation[1023] < &shadow.generation[1040]);
    return 0;
}

static int verify_raw_bits_and_boundaries(void)
{
    Th07PspBulletPositionSoaShadow shadow;
    memset(&shadow, 0xa5, sizeof(shadow));
    shadow.Reset(0x10203040u, 7u);

    static const uint32_t slots[] = {0u, 31u, 32u, 1023u};
    static const uint32_t xBits[] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x7f7fffffu};
    static const uint32_t yBits[] = {
        0x80000000u, 0x00000000u, 0x007fffffu, 0xff7fffffu};
    static const uint32_t zBits[] = {
        0x3f800000u, 0xbf800000u, 0x00800000u, 0x80800000u};

    for (uint32_t i = 0u; i < 4u; ++i)
    {
        const uint32_t generation = 0xf0000000u + i;
        CHECK(shadow.PublishRaw(slots[i], generation, 0x10203040u, 7u,
                                xBits[i], yBits[i], zBits[i]));
        CHECK(shadow.ValidateRaw(slots[i], generation, 0x10203040u, 7u,
                                 xBits[i], yBits[i], zBits[i]) ==
              TH07_PSP_BULLET_POSITION_SOA_MATCH);
    }
    CHECK(shadow.CountValid() == 4u);
    CHECK((shadow.validBits[0] & 0x80000001u) == 0x80000001u);
    CHECK((shadow.validBits[1] & 1u) != 0u);
    CHECK((shadow.validBits[31] & 0x80000000u) != 0u);

    // Raw comparison distinguishes +0 and -0; numerical equality is not
    // enough for the future authority cutover.
    CHECK(shadow.ValidateRaw(0u, 0xf0000000u, 0x10203040u, 7u,
                             0x80000000u, yBits[0], zBits[0]) ==
          TH07_PSP_BULLET_POSITION_SOA_POSITION_MISMATCH);
    CHECK(Th07PspBulletPositionSoaShadow::FloatBits(-0.0f) == 0x80000000u);
    CHECK(Th07PspBulletPositionSoaShadow::FloatBits(
              Th07PspBulletPositionSoaShadow::BitsFloat(0x00000001u)) ==
          0x00000001u);
    return 0;
}

static int verify_generation_calc_and_reuse(void)
{
    Th07PspBulletPositionSoaShadow shadow;
    shadow.Reset(11u, 100u);
    CHECK(shadow.PublishRaw(17u, 0xffffffffu, 11u, 100u, 1u, 2u, 3u));
    CHECK(shadow.ValidateRaw(17u, 0xfffffffeu, 11u, 100u, 1u, 2u, 3u) ==
          TH07_PSP_BULLET_POSITION_SOA_GENERATION_MISMATCH);
    CHECK(shadow.ValidateRaw(17u, 0xffffffffu, 11u, 99u, 1u, 2u, 3u) ==
          TH07_PSP_BULLET_POSITION_SOA_CALC_MISMATCH);
    shadow.publishManagerSerial[17] = 12u;
    CHECK(shadow.ValidateRaw(17u, 0xffffffffu, 11u, 100u, 1u, 2u, 3u) ==
          TH07_PSP_BULLET_POSITION_SOA_MANAGER_MISMATCH);
    shadow.publishManagerSerial[17] = 11u;

    CHECK(shadow.BeginCalc(11u, 101u) ==
          TH07_PSP_BULLET_POSITION_SOA_BEGIN_ADVANCED);
    // Advancing the pass does not destroy an unvisited slot's prior publish.
    CHECK(shadow.ValidateRaw(17u, 0xffffffffu, 11u, 100u, 1u, 2u, 3u) ==
          TH07_PSP_BULLET_POSITION_SOA_MATCH);
    CHECK(shadow.PublishRaw(17u, 0x12345678u, 11u, 101u, 4u, 5u, 6u));
    CHECK(shadow.ValidateRaw(17u, 0x12345678u, 11u, 101u, 4u, 5u, 6u) ==
          TH07_PSP_BULLET_POSITION_SOA_MATCH);

    shadow.Invalidate(17u);
    CHECK(!shadow.IsSlotValid(17u));
    CHECK(shadow.ValidateRaw(17u, 0x12345678u, 11u, 101u, 4u, 5u, 6u) ==
          TH07_PSP_BULLET_POSITION_SOA_NOT_VALID);
    CHECK(shadow.generation[17] == 0u);
    CHECK(shadow.publishManagerSerial[17] == 0u);

    // A reused slot cannot inherit authority from the old generation.
    CHECK(shadow.PublishRaw(17u, 0x87654321u, 11u, 101u, 7u, 8u, 9u));
    CHECK(shadow.ValidateRaw(17u, 0x12345678u, 11u, 101u, 7u, 8u, 9u) ==
          TH07_PSP_BULLET_POSITION_SOA_GENERATION_MISMATCH);
    return 0;
}

static int verify_load_raw_output_and_mismatches(void)
{
    Th07PspBulletPositionSoaShadow shadow;
    shadow.Reset(0x44556677u, 0x102u);

    // LoadRaw is the D2B handoff: it must return the stored words without a
    // float comparison or conversion.  In particular, signed zero remains
    // observable even though +0.0f == -0.0f in C++.
    CHECK(shadow.PublishRaw(63u, 0x89abcdefu, 0x44556677u, 0x102u,
                            0x00000000u, 0x80000000u, 0x7fc12345u));
    uint32_t xBits = 0xaaaaaaaau;
    uint32_t yBits = 0xbbbbbbbbu;
    uint32_t zBits = 0xccccccccu;
    CHECK(shadow.LoadRaw(63u, 0x89abcdefu, 0x44556677u, 0x102u,
                         &xBits, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_MATCH);
    CHECK(xBits == 0x00000000u);
    CHECK(yBits == 0x80000000u);
    CHECK(zBits == 0x7fc12345u);
    CHECK(shadow.ValidateRaw(63u, 0x89abcdefu, 0x44556677u, 0x102u,
                             0x80000000u, 0x80000000u, 0x7fc12345u) ==
          TH07_PSP_BULLET_POSITION_SOA_POSITION_MISMATCH);

    // No rejected load may leak a partially-authoritative payload.  Keep
    // sentinels in every destination while exercising each identity fence.
#define RESET_OUTPUTS()                                                        \
    do                                                                         \
    {                                                                          \
        xBits = 0xaaaaaaaau;                                                    \
        yBits = 0xbbbbbbbbu;                                                    \
        zBits = 0xccccccccu;                                                    \
    } while (0)
#define CHECK_OUTPUTS_UNCHANGED()                                              \
    do                                                                         \
    {                                                                          \
        CHECK(xBits == 0xaaaaaaaau);                                            \
        CHECK(yBits == 0xbbbbbbbbu);                                            \
        CHECK(zBits == 0xccccccccu);                                            \
    } while (0)

    RESET_OUTPUTS();
    CHECK(shadow.LoadRaw(1024u, 0x89abcdefu, 0x44556677u, 0x102u,
                         &xBits, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_INVALID_SLOT);
    CHECK_OUTPUTS_UNCHANGED();

    RESET_OUTPUTS();
    CHECK(shadow.LoadRaw(63u, 0x89abcdefu, 0x44556676u, 0x102u,
                         &xBits, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_MANAGER_MISMATCH);
    CHECK_OUTPUTS_UNCHANGED();

    shadow.publishManagerSerial[63] = 0x44556676u;
    RESET_OUTPUTS();
    CHECK(shadow.LoadRaw(63u, 0x89abcdefu, 0x44556677u, 0x102u,
                         &xBits, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_MANAGER_MISMATCH);
    CHECK_OUTPUTS_UNCHANGED();
    shadow.publishManagerSerial[63] = 0x44556677u;

    RESET_OUTPUTS();
    CHECK(shadow.LoadRaw(63u, 0x89abcdeeu, 0x44556677u, 0x102u,
                         &xBits, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_GENERATION_MISMATCH);
    CHECK_OUTPUTS_UNCHANGED();

    RESET_OUTPUTS();
    CHECK(shadow.LoadRaw(63u, 0x89abcdefu, 0x44556677u, 0x103u,
                         &xBits, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_CALC_MISMATCH);
    CHECK_OUTPUTS_UNCHANGED();

    RESET_OUTPUTS();
    CHECK(shadow.LoadRaw(62u, 1u, 0x44556677u, 0x102u,
                         &xBits, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_NOT_VALID);
    CHECK_OUTPUTS_UNCHANGED();

    RESET_OUTPUTS();
    CHECK(shadow.LoadRaw(63u, 0x89abcdefu, 0x44556677u, 0x102u,
                         NULL, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_NOT_INITIALIZED);
    CHECK_OUTPUTS_UNCHANGED();

    Th07PspBulletPositionSoaShadow uninitialized;
    memset(&uninitialized, 0, sizeof(uninitialized));
    RESET_OUTPUTS();
    CHECK(uninitialized.LoadRaw(63u, 0x89abcdefu, 0x44556677u, 0x102u,
                                &xBits, &yBits, &zBits) ==
          TH07_PSP_BULLET_POSITION_SOA_NOT_INITIALIZED);
    CHECK_OUTPUTS_UNCHANGED();

#undef CHECK_OUTPUTS_UNCHANGED
#undef RESET_OUTPUTS
    return 0;
}

static int verify_fail_closed_boundaries(void)
{
    Th07PspBulletPositionSoaShadow shadow;
    memset(&shadow, 0, sizeof(shadow));
    CHECK(shadow.ValidateRaw(0u, 1u, 1u, 1u, 0u, 0u, 0u) ==
          TH07_PSP_BULLET_POSITION_SOA_NOT_INITIALIZED);
    CHECK(shadow.BeginCalc(21u, 300u) ==
          TH07_PSP_BULLET_POSITION_SOA_BEGIN_MANAGER_RESET);
    CHECK(shadow.PublishRaw(9u, 33u, 21u, 300u, 10u, 11u, 12u));

    // Wrong manager/calc publication clears that slot instead of preserving
    // a plausible but stale record.
    CHECK(!shadow.PublishRaw(9u, 33u, 22u, 300u, 10u, 11u, 12u));
    CHECK(!shadow.IsSlotValid(9u));
    CHECK(shadow.PublishRaw(9u, 34u, 21u, 300u, 13u, 14u, 15u));
    CHECK(!shadow.PublishRaw(9u, 34u, 21u, 301u, 13u, 14u, 15u));
    CHECK(!shadow.IsSlotValid(9u));

    CHECK(!shadow.PublishRaw(1024u, 1u, 21u, 300u, 0u, 0u, 0u));
    CHECK(shadow.ValidateRaw(1024u, 1u, 21u, 300u, 0u, 0u, 0u) ==
          TH07_PSP_BULLET_POSITION_SOA_INVALID_SLOT);

    CHECK(shadow.PublishRaw(1u, 1u, 21u, 300u, 1u, 2u, 3u));
    CHECK(shadow.PublishRaw(2u, 2u, 21u, 300u, 4u, 5u, 6u));
    shadow.InvalidateAll(); // pause/suspend/demo-restart contract
    CHECK(shadow.CountValid() == 0u);

    CHECK(shadow.BeginCalc(22u, 1u) ==
          TH07_PSP_BULLET_POSITION_SOA_BEGIN_MANAGER_RESET);
    CHECK(shadow.managerSerial == 22u);
    CHECK(shadow.activeCalcSerial == 1u);
    CHECK(shadow.CountValid() == 0u);
    CHECK(shadow.BeginCalc(22u, 1u) ==
          TH07_PSP_BULLET_POSITION_SOA_BEGIN_SAME_CALC);
    return 0;
}

int main(void)
{
    int result = verify_layout();
    if (result != 0)
        return result;
    result = verify_raw_bits_and_boundaries();
    if (result != 0)
        return result;
    result = verify_generation_calc_and_reuse();
    if (result != 0)
        return result;
    result = verify_load_raw_output_and_mismatches();
    if (result != 0)
        return result;
    result = verify_fail_closed_boundaries();
    if (result != 0)
        return result;

    puts("D2A position SoA: 1024 slots, stride1040, six planes, raw bits, generation, serial fences");
    return 0;
}
