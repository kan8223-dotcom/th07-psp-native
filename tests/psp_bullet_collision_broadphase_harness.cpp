#define TH07_PSP_BULLET_COLLISION_BROADPHASE
#include "src/PspBulletCollisionBroadphase.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
struct Rect
{
    float left;
    float top;
    float right;
    float bottom;
};

struct Case
{
    const char *name;
    float posX;
    float posY;
    float sizeX;
    float sizeY;
    bool grazeCanObserve;
    bool border;
    int bombHighWater;
    Rect hit;
    Rect graze;
    bool expectedClear;
};

bool CanonicalSeparate(const Rect &first, const Rect &second)
{
    return second.left > first.right || second.right < first.left ||
           second.top > first.bottom || second.bottom < first.top;
}

// Independent model of the exact comparisons in Player::CheckGraze and
// Player::CalcKillboxCollision.  It is used only in the implication below:
// broadphase true => no canonical observable collision.
bool CanonicalMayObserve(const Case &test)
{
    if (test.bombHighWater != 0)
        return true;

    const float halfX = test.sizeX / 2.0f;
    const float halfY = test.sizeY / 2.0f;
    const Rect bullet = {test.posX - halfX, test.posY - halfY,
                         test.posX + halfX, test.posY + halfY};
    if (!CanonicalSeparate(bullet, test.hit))
        return true;

    if (test.grazeCanObserve)
    {
        const Rect expanded = {bullet.left - 20.0f,
                               bullet.top - 20.0f,
                               bullet.right + 20.0f,
                               bullet.bottom + 20.0f};
        if (!CanonicalSeparate(expanded, test.graze))
            return true;
    }
    return false;
}

bool Broadphase(const Case &test)
{
    return Th07PspBulletCollisionDefinitelyClear(
        test.posX, test.posY, test.sizeX, test.sizeY,
        test.grazeCanObserve, test.border, test.bombHighWater,
        test.hit.left, test.hit.top, test.hit.right, test.hit.bottom,
        test.graze.left, test.graze.top,
        test.graze.right, test.graze.bottom);
}
} // namespace

int main()
{
    constexpr Rect farRect = {1000.0f, 1000.0f, 1010.0f, 1010.0f};
    constexpr Rect hitTouchRight = {2.0f, -1.0f, 4.0f, 1.0f};
    constexpr Rect grazeTouchRight = {22.0f, -1.0f, 24.0f, 1.0f};
    constexpr Rect grazeBeyondRight = {22.001f, -1.0f, 24.0f, 1.0f};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const float maximum = std::numeric_limits<float>::max();

    const std::array<Case, 21> cases = {{
        {"far", 0.0f, 0.0f, 4.0f, 2.0f, true, false, 0,
         farRect, farRect, true},
        {"hit equality is overlap", 0.0f, 0.0f, 4.0f, 2.0f,
         false, false, 0, hitTouchRight, farRect, false},
        {"graze equality is overlap", 0.0f, 0.0f, 4.0f, 2.0f,
         true, false, 0, farRect, grazeTouchRight, false},
        {"graze strict separation", 0.0f, 0.0f, 4.0f, 2.0f,
         true, false, 0, farRect, grazeBeyondRight, true},
        {"timer below sixteen ignores graze", 0.0f, 0.0f, 4.0f, 2.0f,
         false, false, 0, farRect, grazeTouchRight, true},
        {"already grazed ignores graze", 0.0f, 0.0f, 4.0f, 2.0f,
         false, false, 0, farRect, grazeTouchRight, true},
        {"bomb prefix", 0.0f, 0.0f, 4.0f, 2.0f, false, false, 1,
         farRect, farRect, false},
        {"invalid negative bomb prefix", 0.0f, 0.0f, 4.0f, 2.0f,
         false, false, -1, farRect, farRect, false},
        {"border", 0.0f, 0.0f, 4.0f, 2.0f, false, true, 0,
         farRect, farRect, false},
        {"negative width", 0.0f, 0.0f, -4.0f, 2.0f, false, false, 0,
         farRect, farRect, false},
        {"negative height", 0.0f, 0.0f, 4.0f, -2.0f, false, false, 0,
         farRect, farRect, false},
        {"nan position", nan, 0.0f, 4.0f, 2.0f, false, false, 0,
         farRect, farRect, false},
        {"nan size", 0.0f, 0.0f, nan, 2.0f, false, false, 0,
         farRect, farRect, false},
        {"infinite position", infinity, 0.0f, 4.0f, 2.0f,
         false, false, 0, farRect, farRect, false},
        {"finite intermediate overflow", maximum, 0.0f, maximum, 2.0f,
         false, false, 0, farRect, farRect, false},
        {"nan hitbox cannot prove clear", 0.0f, 0.0f, 4.0f, 2.0f,
         false, false, 0, {nan, -1.0f, nan, 1.0f}, farRect, false},
        {"nan hitbox with separate y is canonical clear",
         0.0f, 0.0f, 4.0f, 2.0f, false, false, 0,
         {nan, 2.0f, nan, 3.0f}, farRect, true},
        {"nan graze cannot prove clear", 0.0f, 0.0f, 4.0f, 2.0f,
         true, false, 0, farRect, {nan, -1.0f, nan, 1.0f}, false},
        {"nan graze ignored below timer gate", 0.0f, 0.0f, 4.0f, 2.0f,
         false, false, 0, farRect, {nan, -1.0f, nan, 1.0f}, true},
        {"negative zero is canonical zero", -0.0f, -0.0f, -0.0f, -0.0f,
         false, false, 0, farRect, farRect, true},
        {"dead or spawning cannot graze", 0.0f, 0.0f, 4.0f, 2.0f,
         false, false, 0, farRect, grazeTouchRight, true},
    }};

    std::uint32_t clearCount = 0;
    for (const Case &test : cases)
    {
        const bool clear = Broadphase(test);
        if (clear != test.expectedClear)
        {
            std::fprintf(stderr, "%s: expected clear=%d, actual=%d\n",
                         test.name, test.expectedClear, clear);
            return 1;
        }
        if (clear && (test.border || CanonicalMayObserve(test)))
        {
            std::fprintf(stderr, "%s: unsafe false negative\n", test.name);
            return 2;
        }
        clearCount += clear ? 1u : 0u;
    }

    // Deterministic grid proof around every strict boundary.  This exercises
    // 1,458 combinations without relying on host RNG or epsilon equality.
    constexpr std::array<float, 3> positions = {-21.0f, 0.0f, 21.0f};
    constexpr std::array<float, 3> sizes = {0.0f, 2.0f, 8.0f};
    constexpr std::array<float, 3> edges = {-22.0f, 0.0f, 22.0f};
    constexpr std::array<bool, 2> grazeGates = {false, true};
    std::uint32_t gridCases = 0;
    for (float posX : positions)
    for (float posY : positions)
    for (float sizeX : sizes)
    for (float sizeY : sizes)
    for (float hitX : edges)
    for (float grazeX : edges)
    for (bool grazeCanObserve : grazeGates)
    {
        Case test = {"grid", posX, posY, sizeX, sizeY,
                     grazeCanObserve, false, 0,
                     {hitX, -1.0f, hitX + 1.0f, 1.0f},
                     {grazeX, -1.0f, grazeX + 1.0f, 1.0f}, false};
        const bool clear = Broadphase(test);
        if (clear && CanonicalMayObserve(test))
        {
            std::fprintf(stderr, "grid: unsafe false negative\n");
            return 3;
        }
        ++gridCases;
    }

    std::printf("broadphase conservative: %zu directed, %lu grid, %lu clear\n",
                cases.size(), static_cast<unsigned long>(gridCases),
                static_cast<unsigned long>(clearCount));
    return 0;
}
