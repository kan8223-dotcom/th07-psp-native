#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
struct Vec3
{
    float x;
    float y;
    float z;
};

struct Bullet
{
    Vec3 pos;
    Vec3 velocity;
    std::uint32_t generation;
    std::uint32_t state;
    std::uint16_t exFlags;
    int spawnDelay;
    int commandIndex;
};

struct Seed
{
    std::uint32_t generation;
    std::uint32_t nextX;
    std::uint32_t nextY;
    std::uint32_t nextZ;
    std::uint16_t flags;
    std::uint16_t reserved;
};

constexpr std::uint32_t kNormal = 1u;
constexpr std::uint16_t kCandidate = 1u << 0;
constexpr std::uint16_t kInBounds = 1u << 1;

std::uint32_t Bits(float value)
{
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float Float(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Vec3 Add(const Vec3 &left, const Vec3 &right)
{
    volatile float lx = left.x;
    volatile float ly = left.y;
    volatile float lz = left.z;
    volatile float rx = right.x;
    volatile float ry = right.y;
    volatile float rz = right.z;
    return {lx + rx, ly + ry, lz + rz};
}

bool Same(const Vec3 &left, const Vec3 &right)
{
    return Bits(left.x) == Bits(right.x) &&
           Bits(left.y) == Bits(right.y) &&
           Bits(left.z) == Bits(right.z);
}

bool TryTrusted(Bullet *bullet, const Seed &seed,
                std::uint32_t captureEpoch, std::uint32_t currentEpoch)
{
    constexpr std::uint16_t allowed = kCandidate | kInBounds;
    if (!bullet || captureEpoch != currentEpoch ||
        bullet->state != kNormal || bullet->generation != seed.generation ||
        seed.generation == 0u || seed.reserved != 0u ||
        (seed.flags & ~allowed) != 0u ||
        (seed.flags & kCandidate) == 0u)
    {
        return false;
    }
    bullet->pos = {Float(seed.nextX), Float(seed.nextY), Float(seed.nextZ)};
    return true;
}

bool SnapshotMatches(int playerState, int capturedState)
{
    return playerState == capturedState;
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
        {{denormal, -denormal, denormal},
         {denormal, denormal, -denormal}},
        {{192.0f, 224.0f, 0.1f}, {3.1415927f, -2.7182817f, 0.25f}},
        {{-1000.0f, 1000.0f, 8.0f}, {0.125f, -0.125f, -16.0f}},
    }};

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        const Vec3 canonical = Add(cases[index].first, cases[index].second);
        Bullet trusted = {cases[index].first, cases[index].second,
                          0x10007u, kNormal, 0u, 0, 5};
        const Seed seed = {trusted.generation, Bits(canonical.x),
                           Bits(canonical.y), Bits(canonical.z),
                           kCandidate | kInBounds, 0u};
        if (!TryTrusted(&trusted, seed, 19u, 19u) ||
            !Same(trusted.pos, canonical))
        {
            std::fprintf(stderr, "trusted motion mismatch %zu\n", index);
            return 1;
        }
    }

    Bullet changed = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f},
                      9u, kNormal, 0u, 0, 5};
    const Vec3 expected = Add(changed.pos, changed.velocity);
    const Seed seed = {changed.generation, Bits(expected.x), Bits(expected.y),
                       Bits(expected.z), kCandidate, 0u};
    if (TryTrusted(&changed, seed, 20u, 21u))
    {
        std::fprintf(stderr, "mutation epoch failed open\n");
        return 2;
    }
    changed.pos = Add(changed.pos, changed.velocity);
    if (!Same(changed.pos, expected))
    {
        std::fprintf(stderr, "canonical fallback changed arithmetic\n");
        return 3;
    }

    Bullet stale = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
                    10u, kNormal, 0u, 0, 5};
    if (TryTrusted(&stale, seed, 20u, 20u))
    {
        std::fprintf(stderr, "generation mismatch failed open\n");
        return 4;
    }

    // One pre-loop validation covers every negative ME collision result.
    // A canonical Player collision changes observable state, so the single
    // post-call relatch disables all later results.
    int playerState = 0;
    const int capturedState = 0;
    int snapshotChecks = 1;
    bool collisionAllowed = SnapshotMatches(playerState, capturedState);
    int accepted = 0;
    for (int bullet = 0; bullet < 1000 && collisionAllowed; ++bullet)
        ++accepted;
    playerState = 1;
    ++snapshotChecks;
    collisionAllowed = SnapshotMatches(playerState, capturedState);
    if (accepted != 1000 || collisionAllowed || snapshotChecks != 2)
    {
        std::fprintf(stderr, "Player snapshot latch mismatch\n");
        return 5;
    }

    std::printf("trusted seed: 8 bit-exact, epoch/gen fail-closed, 2 snapshot checks\n");
    return 0;
}
