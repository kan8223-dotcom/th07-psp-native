#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
struct Vec3
{
    float x;
    float y;
    float z;
};

struct SlotResult
{
    std::uint32_t posXBits;
    std::uint32_t posYBits;
    std::uint32_t posZBits;
    std::uint16_t generation;
    std::uint16_t flags;
};

struct Observable
{
    Vec3 pos;
    int itemType;
    int timer1;
    int timer2;
    int vmTicks;
    int listLinks;
};

constexpr std::uint16_t kCandidate = 1u << 0;
constexpr std::uint16_t kInBounds = 1u << 1;
constexpr std::uint16_t kNoCollision = 1u << 2;
constexpr int kPointBullet = 9;

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float BitsFloat(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Vec3 CanonicalAdd(const Vec3 &position, const Vec3 &velocity)
{
    // Volatile inputs force one IEEE-754 single-precision addition per field,
    // matching the result-only ME contract rather than host excess precision.
    volatile float px = position.x;
    volatile float py = position.y;
    volatile float pz = position.z;
    volatile float vx = velocity.x;
    volatile float vy = velocity.y;
    volatile float vz = velocity.z;
    return {px + vx, py + vy, pz + vz};
}

bool IsInBounds(const Vec3 &position, float width, float height)
{
    if (width / 2.0f + position.x < 0.0f)
        return false;
    if (position.x - width / 2.0f > 384.0f)
        return false;
    if (height / 2.0f + position.y < 0.0f)
        return false;
    if (position.y - height / 2.0f > 448.0f)
        return false;
    return true;
}

void CanonicalNegativeTail(Observable *observable)
{
    // Both canonical negative collision entry points publish this item type.
    observable->itemType = kPointBullet;
    ++observable->vmTicks;
    ++observable->timer1;
    ++observable->timer2;
    ++observable->listLinks;
}

void AdoptedNegativeTail(Observable *observable, const SlotResult &result)
{
    observable->pos.x = BitsFloat(result.posXBits);
    observable->pos.y = BitsFloat(result.posYBits);
    observable->pos.z = BitsFloat(result.posZBits);
    if ((result.flags & kNoCollision) != 0u)
        observable->itemType = kPointBullet;
    ++observable->vmTicks;
    ++observable->timer1;
    ++observable->timer2;
    ++observable->listLinks;
}

bool Same(const Observable &left, const Observable &right)
{
    return FloatBits(left.pos.x) == FloatBits(right.pos.x) &&
           FloatBits(left.pos.y) == FloatBits(right.pos.y) &&
           FloatBits(left.pos.z) == FloatBits(right.pos.z) &&
           left.itemType == right.itemType &&
           left.timer1 == right.timer1 && left.timer2 == right.timer2 &&
           left.vmTicks == right.vmTicks &&
           left.listLinks == right.listLinks;
}
} // namespace

int main()
{
    const float denormal = std::numeric_limits<float>::denorm_min();
    const std::array<std::pair<Vec3, Vec3>, 8> cases = {{
        {{0.0f, 0.0f, 0.1f}, {1.25f, -0.5f, 0.0f}},
        {{383.75f, 447.75f, -0.0f}, {0.5f, 0.5f, 0.0f}},
        {{-31.0f, 100.0f, 0.1f}, {-2.0f, 0.0f, denormal}},
        {{400.0f, 460.0f, 0.1f}, {-0.25f, -0.5f, -denormal}},
        {{-0.0f, 0.0f, -0.0f}, {-0.0f, -0.0f, 0.0f}},
        {{denormal, -denormal, denormal}, {denormal, denormal, -denormal}},
        {{192.0f, 224.0f, 0.1f}, {3.1415927f, -2.7182817f, 0.25f}},
        {{-1000.0f, 1000.0f, 8.0f}, {0.125f, -0.125f, -16.0f}},
    }};

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        const Vec3 expectedPosition =
            CanonicalAdd(cases[index].first, cases[index].second);
        const bool expectedBounds = IsInBounds(expectedPosition, 32.0f, 16.0f);
        SlotResult result = {FloatBits(expectedPosition.x),
                             FloatBits(expectedPosition.y),
                             FloatBits(expectedPosition.z),
                             7u,
                             static_cast<std::uint16_t>(
                                 kCandidate | kNoCollision |
                                 (expectedBounds ? kInBounds : 0u))};

        Observable canonical = {expectedPosition, -123, 4, 9, 2, 5};
        Observable adopted = {cases[index].first, -123, 4, 9, 2, 5};
        CanonicalNegativeTail(&canonical);
        AdoptedNegativeTail(&adopted, result);
        if (!Same(canonical, adopted) ||
            expectedBounds != ((result.flags & kInBounds) != 0u))
        {
            std::fprintf(stderr, "ME16 mismatch at case %zu\n", index);
            return 1;
        }
    }

    std::printf("8 ME16 motion/bounds/tail cases bit-exact\n");
    return 0;
}
