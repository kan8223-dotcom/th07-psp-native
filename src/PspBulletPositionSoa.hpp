#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// D2A's SC-owned position shadow.  This type deliberately has no dependency
// on Bullet or PSP headers so its layout and fail-closed rules can be tested
// on the host.  AoS remains authoritative while D2A is enabled.
enum
{
    TH07_PSP_BULLET_POSITION_SOA_VERSION = 0x42503231u, // "BP21"
    TH07_PSP_BULLET_POSITION_SOA_CAPACITY = 1024u,
    TH07_PSP_BULLET_POSITION_SOA_VALID_WORDS = 32u,
    // One extra 64-byte line rotates equal slot indices across D-cache sets.
    TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE = 1040u,
    TH07_PSP_BULLET_POSITION_SOA_ALIGNMENT = 64u
};

enum Th07PspBulletPositionSoaBeginResult
{
    TH07_PSP_BULLET_POSITION_SOA_BEGIN_MANAGER_RESET = 0,
    TH07_PSP_BULLET_POSITION_SOA_BEGIN_ADVANCED = 1,
    TH07_PSP_BULLET_POSITION_SOA_BEGIN_SAME_CALC = 2
};

enum Th07PspBulletPositionSoaValidation
{
    TH07_PSP_BULLET_POSITION_SOA_INVALID_SLOT = 0,
    TH07_PSP_BULLET_POSITION_SOA_NOT_INITIALIZED = 1,
    TH07_PSP_BULLET_POSITION_SOA_MANAGER_MISMATCH = 2,
    TH07_PSP_BULLET_POSITION_SOA_NOT_VALID = 3,
    TH07_PSP_BULLET_POSITION_SOA_GENERATION_MISMATCH = 4,
    TH07_PSP_BULLET_POSITION_SOA_CALC_MISMATCH = 5,
    TH07_PSP_BULLET_POSITION_SOA_POSITION_MISMATCH = 6,
    TH07_PSP_BULLET_POSITION_SOA_MATCH = 7
};

struct alignas(TH07_PSP_BULLET_POSITION_SOA_ALIGNMENT)
Th07PspBulletPositionSoaShadow
{
    // Exactly one cache line.  managerSerial owns the whole sidecar;
    // activeCalcSerial names the calc pass currently publishing new values.
    uint32_t version;
    uint32_t structBytes;
    uint32_t capacity;
    uint32_t planeStrideWords;
    uint32_t managerSerial;
    uint32_t activeCalcSerial;
    uint32_t reservedHeader[10];

    // Publication sets the validity bit last.  D2A is SC-local, so no atomic
    // or cache-operation contract is implied by this helper.
    uint32_t validBits[TH07_PSP_BULLET_POSITION_SOA_VALID_WORDS];

    // Slot generations are never truncated.  Both ownership serials are
    // repeated per slot, so manager reset/reuse and overlapping traversals
    // can be proven locally without trusting mutable header state alone.
    // publishCalcSerial is per-slot:
    // during a forward/reverse traversal, an unvisited slot can still hold
    // the preceding pass while already-visited slots hold the current pass.
    uint32_t generation[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE];
    uint32_t publishManagerSerial[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE];
    uint32_t publishCalcSerial[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE];
    uint32_t posXBits[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE];
    uint32_t posYBits[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE];
    uint32_t posZBits[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE];

    static uint32_t FloatBits(float value)
    {
        uint32_t bits;
        static_assert(sizeof(bits) == sizeof(value),
                      "PSP position shadow requires IEEE-sized float words");
        memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    static float BitsFloat(uint32_t bits)
    {
        float value;
        static_assert(sizeof(bits) == sizeof(value),
                      "PSP position shadow requires IEEE-sized float words");
        memcpy(&value, &bits, sizeof(value));
        return value;
    }

    bool IsInitialized() const
    {
        return version == TH07_PSP_BULLET_POSITION_SOA_VERSION &&
               structBytes == sizeof(*this) &&
               capacity == TH07_PSP_BULLET_POSITION_SOA_CAPACITY &&
               planeStrideWords ==
                   TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE;
    }

    void Reset(uint32_t newManagerSerial, uint32_t initialCalcSerial)
    {
        memset(this, 0, sizeof(*this));
        version = TH07_PSP_BULLET_POSITION_SOA_VERSION;
        structBytes = (uint32_t)sizeof(*this);
        capacity = TH07_PSP_BULLET_POSITION_SOA_CAPACITY;
        planeStrideWords = TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE;
        managerSerial = newManagerSerial;
        activeCalcSerial = initialCalcSerial;
    }

    Th07PspBulletPositionSoaBeginResult
    BeginCalc(uint32_t newManagerSerial, uint32_t newCalcSerial)
    {
        if (!IsInitialized() || managerSerial != newManagerSerial)
        {
            Reset(newManagerSerial, newCalcSerial);
            return TH07_PSP_BULLET_POSITION_SOA_BEGIN_MANAGER_RESET;
        }
        if (activeCalcSerial == newCalcSerial)
            return TH07_PSP_BULLET_POSITION_SOA_BEGIN_SAME_CALC;
        activeCalcSerial = newCalcSerial;
        return TH07_PSP_BULLET_POSITION_SOA_BEGIN_ADVANCED;
    }

    bool IsSlotValid(uint32_t slot) const
    {
        if (slot >= TH07_PSP_BULLET_POSITION_SOA_CAPACITY)
            return false;
        return (validBits[slot >> 5u] & (1u << (slot & 31u))) != 0u;
    }

    void Invalidate(uint32_t slot)
    {
        if (slot >= TH07_PSP_BULLET_POSITION_SOA_CAPACITY)
            return;
        validBits[slot >> 5u] &= ~(1u << (slot & 31u));
        generation[slot] = 0u;
        publishManagerSerial[slot] = 0u;
        publishCalcSerial[slot] = 0u;
        posXBits[slot] = 0u;
        posYBits[slot] = 0u;
        posZBits[slot] = 0u;
    }

    void InvalidateAll()
    {
        memset(validBits, 0, sizeof(validBits));
    }

    bool PublishRaw(uint32_t slot,
                    uint32_t fullGeneration,
                    uint32_t expectedManagerSerial,
                    uint32_t publishCalc,
                    uint32_t xBits,
                    uint32_t yBits,
                    uint32_t zBits)
    {
        if (slot >= TH07_PSP_BULLET_POSITION_SOA_CAPACITY)
            return false;
        if (!IsInitialized() || expectedManagerSerial == 0u ||
            publishCalc == 0u || managerSerial != expectedManagerSerial ||
            activeCalcSerial != publishCalc)
        {
            Invalidate(slot);
            return false;
        }

        generation[slot] = fullGeneration;
        publishManagerSerial[slot] = expectedManagerSerial;
        publishCalcSerial[slot] = publishCalc;
        posXBits[slot] = xBits;
        posYBits[slot] = yBits;
        posZBits[slot] = zBits;
        validBits[slot >> 5u] |= 1u << (slot & 31u);
        return true;
    }

    bool Publish(uint32_t slot,
                 uint32_t fullGeneration,
                 uint32_t expectedManagerSerial,
                 uint32_t publishCalc,
                 float x,
                 float y,
                 float z)
    {
        return PublishRaw(slot, fullGeneration, expectedManagerSerial,
                          publishCalc, FloatBits(x), FloatBits(y),
                          FloatBits(z));
    }

    Th07PspBulletPositionSoaValidation
    LoadRaw(uint32_t slot,
            uint32_t fullGeneration,
            uint32_t expectedManagerSerial,
            uint32_t expectedPublishCalc,
            uint32_t *xBits,
            uint32_t *yBits,
            uint32_t *zBits) const
    {
        if (slot >= TH07_PSP_BULLET_POSITION_SOA_CAPACITY)
            return TH07_PSP_BULLET_POSITION_SOA_INVALID_SLOT;
        if (!xBits || !yBits || !zBits || !IsInitialized())
            return TH07_PSP_BULLET_POSITION_SOA_NOT_INITIALIZED;
        if (expectedManagerSerial == 0u ||
            managerSerial != expectedManagerSerial)
            return TH07_PSP_BULLET_POSITION_SOA_MANAGER_MISMATCH;
        if (!IsSlotValid(slot))
            return TH07_PSP_BULLET_POSITION_SOA_NOT_VALID;
        if (publishManagerSerial[slot] != expectedManagerSerial)
            return TH07_PSP_BULLET_POSITION_SOA_MANAGER_MISMATCH;
        if (generation[slot] != fullGeneration)
            return TH07_PSP_BULLET_POSITION_SOA_GENERATION_MISMATCH;
        if (publishCalcSerial[slot] != expectedPublishCalc)
            return TH07_PSP_BULLET_POSITION_SOA_CALC_MISMATCH;

        const uint32_t loadedX = posXBits[slot];
        const uint32_t loadedY = posYBits[slot];
        const uint32_t loadedZ = posZBits[slot];

        // Bracket the payload with the publication identity.  D2B is SC-local,
        // but this also makes the exact lifetime rule explicit for the ME
        // position-source ABI: a slot changing during XYZ reads is rejected,
        // never partially consumed.
        if (!IsSlotValid(slot))
            return TH07_PSP_BULLET_POSITION_SOA_NOT_VALID;
        if (publishManagerSerial[slot] != expectedManagerSerial)
            return TH07_PSP_BULLET_POSITION_SOA_MANAGER_MISMATCH;
        if (generation[slot] != fullGeneration)
            return TH07_PSP_BULLET_POSITION_SOA_GENERATION_MISMATCH;
        if (publishCalcSerial[slot] != expectedPublishCalc)
            return TH07_PSP_BULLET_POSITION_SOA_CALC_MISMATCH;

        *xBits = loadedX;
        *yBits = loadedY;
        *zBits = loadedZ;
        return TH07_PSP_BULLET_POSITION_SOA_MATCH;
    }

    Th07PspBulletPositionSoaValidation
    ValidateRaw(uint32_t slot,
                uint32_t fullGeneration,
                uint32_t expectedManagerSerial,
                uint32_t expectedPublishCalc,
                uint32_t xBits,
                uint32_t yBits,
                uint32_t zBits) const
    {
        uint32_t loadedX = 0u;
        uint32_t loadedY = 0u;
        uint32_t loadedZ = 0u;
        const Th07PspBulletPositionSoaValidation loaded = LoadRaw(
            slot, fullGeneration, expectedManagerSerial,
            expectedPublishCalc, &loadedX, &loadedY, &loadedZ);
        if (loaded != TH07_PSP_BULLET_POSITION_SOA_MATCH)
            return loaded;
        if (loadedX != xBits || loadedY != yBits || loadedZ != zBits)
            return TH07_PSP_BULLET_POSITION_SOA_POSITION_MISMATCH;
        return TH07_PSP_BULLET_POSITION_SOA_MATCH;
    }

    Th07PspBulletPositionSoaValidation
    Validate(uint32_t slot,
             uint32_t fullGeneration,
             uint32_t expectedManagerSerial,
             uint32_t expectedPublishCalc,
             float x,
             float y,
             float z) const
    {
        return ValidateRaw(slot, fullGeneration, expectedManagerSerial,
                           expectedPublishCalc, FloatBits(x), FloatBits(y),
                           FloatBits(z));
    }

    uint32_t CountValid() const
    {
        uint32_t count = 0u;
        for (uint32_t word = 0u;
             word < TH07_PSP_BULLET_POSITION_SOA_VALID_WORDS; ++word)
        {
            uint32_t bits = validBits[word];
            while (bits != 0u)
            {
                bits &= bits - 1u;
                ++count;
            }
        }
        return count;
    }
};

static_assert(TH07_PSP_BULLET_POSITION_SOA_CAPACITY == 1024u,
              "D2A stable-slot capacity changed");
static_assert(TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE == 1040u,
              "D2A cache-set-skewed plane stride changed");
static_assert(offsetof(Th07PspBulletPositionSoaShadow, validBits) == 64u,
              "D2A metadata must occupy one cache line");
static_assert(offsetof(Th07PspBulletPositionSoaShadow, generation) == 192u,
              "D2A bitmap prefix must occupy whole cache lines");
static_assert(offsetof(Th07PspBulletPositionSoaShadow, publishCalcSerial) -
                      offsetof(Th07PspBulletPositionSoaShadow,
                               publishManagerSerial) ==
                  TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE * sizeof(uint32_t),
              "D2A manager-serial plane stride changed");
static_assert(offsetof(Th07PspBulletPositionSoaShadow, publishManagerSerial) -
                      offsetof(Th07PspBulletPositionSoaShadow, generation) ==
                  TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE * sizeof(uint32_t),
              "D2A generation plane stride changed");
static_assert(offsetof(Th07PspBulletPositionSoaShadow, posXBits) -
                      offsetof(Th07PspBulletPositionSoaShadow,
                               publishCalcSerial) ==
                  TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE * sizeof(uint32_t),
              "D2A calc-serial plane stride changed");
static_assert(offsetof(Th07PspBulletPositionSoaShadow, posYBits) -
                      offsetof(Th07PspBulletPositionSoaShadow, posXBits) ==
                  TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE * sizeof(uint32_t),
              "D2A X plane stride changed");
static_assert(offsetof(Th07PspBulletPositionSoaShadow, posZBits) -
                      offsetof(Th07PspBulletPositionSoaShadow, posYBits) ==
                  TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE * sizeof(uint32_t),
              "D2A Y plane stride changed");
static_assert(alignof(Th07PspBulletPositionSoaShadow) ==
                  TH07_PSP_BULLET_POSITION_SOA_ALIGNMENT,
              "D2A sidecar alignment changed");
static_assert(sizeof(Th07PspBulletPositionSoaShadow) == 25152u,
              "D2A sidecar ABI changed");
